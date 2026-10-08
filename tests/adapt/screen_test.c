/* screen_test.c - self-checking test for ysp/screen.h's core: prediction,
 * the snap rule, drops versus late targets, depth learning, estimated
 * records, the ring record, phases, wait_flip, the group calls, the errors,
 * the mode picker and the parameter table. No framework: it returns 0 when
 * every check passed and 1 after printing each failure.
 *
 * It needs no SDL, no display and no GPU: YSCR_NO_SDL builds the core
 * alone, and a scripted presenter (the Presenter extension interface) plays
 * a display with the depth, drops, path changes and missing statistics each
 * case asks for.
 *
 * The core and the script run on a virtual clock that only the test moves:
 * the header's YSCR__NOW() and YSCR__SLEEP_UNTIL() seam points at it.
 * So the host's scheduling cannot change a result, and preemption is a
 * scripted input (the thread loses N ns at a chosen point), not an
 * accident of a busy CI runner. One smoke test runs the simulated backend
 * in real time, with bounds a loaded runner meets.
 *
 *     gcc -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Werror -O2 -Iinclude \
 *         -o screen_test tests/adapt/screen_test.c -lm -pthread && ./screen_test
 *     cl /nologo /W4 /WX /Iinclude tests\adapt\screen_test.c
 */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
/* getenv() is C4996 under /W4 /WX, and _dupenv_s() is not portable. */
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef YSCR_NO_SDL
#define YSCR_NO_SDL
#endif
/* The virtual clock. Starts far from 0, so no grid arithmetic meets it.
 * long long, not int64_t: no system header may come before ysp/rt.h,
 * which sets the feature macros glibc reads once. */
static int g_virtual = 1;
static long long g_vt = 1000000000000LL;
static long long vnow(void);
static void vsleep_until(long long t);
#define YSCR__NOW() ((int64_t)vnow())
#define YSCR__SLEEP_UNTIL(t, spin) vsleep_until((long long)(t))
/* The trigger worker's wake-up: recorded here, and run by the clock as it
 * passes, through the same function the worker thread calls. */
struct yscr_screen;
static void varm(struct yscr_screen* s, long long t);
#define YSCR__ARM(s, t)       varm((s), (long long)(t))
#define YSCR__WORKER_START(s) (1)
#define YSCR__WORKER_STOP(s)  ((void)(s))
/* The input bridge's doorbells: recorded here in the order rung, refused
 * while g_bell_refuse is set (SDL's queue full). Threads ring them. */
static int g_bell_refuse;
static volatile long g_nbells;
static int g_bells[16384];
static int test_bell(int seq);
#define YSCR__DOORBELL(seq) test_bell(seq)
/* Text input as SDL would report it for the window: -1 = what the header
 * set (no other library), else 0 or 1 as another library left it. */
static int g_ti = -1;
#define YSCR__TEXT_INPUT_ACTIVE(s) (g_ti >= 0 ? g_ti : (s)->text_input)

#define YSP_SCREEN_IMPLEMENTATION
#include "ysp/screen.h"

static long long vnow(void) { return g_virtual ? g_vt : (long long)yrt_now_ns(); }

static struct { yscr_screen* s; long long t; } g_armed[4];
/* Wake-ups armed from the frame thread, not from the worker's own run:
 * each is a submit, the bulk of what arming a trigger costs. */
static int g_in_worker, g_frame_arms;
static void varm(struct yscr_screen* s, long long t) {
    int i, free_i = -1;
    if (!g_in_worker) g_frame_arms++;
    for (i = 0; i < 4; i++) {
        if (g_armed[i].s == s) { g_armed[i].t = t; return; }
        if (!g_armed[i].s && free_i < 0) free_i = i;
    }
    if (free_i >= 0) { g_armed[free_i].s = s; g_armed[free_i].t = t; }
}
/* Fire every wake-up due by `to`, each at its own time, in time order. */
static void vrun(long long to) {
    for (;;) {
        int i, best = -1;
        for (i = 0; i < 4; i++)
            if (g_armed[i].s && g_armed[i].t <= to && (best < 0 || g_armed[i].t < g_armed[best].t)) best = i;
        if (best < 0) break;
        {
            yscr_screen* s = g_armed[best].s;
            long long t = g_armed[best].t;
            g_armed[best].s = NULL;
            if (t > g_vt) g_vt = t;
            g_in_worker = 1;
            yscr__trig_run(s, (int64_t)g_vt, 0);
            g_in_worker = 0;
        }
    }
}
static void vforget(yscr_screen* s) {
    int i;
    for (i = 0; i < 4; i++) if (g_armed[i].s == s) g_armed[i].s = NULL;
}
static void vsleep_until(long long t) {
    if (!g_virtual) { yrt_sleep_until((uint64_t)t, YRT_DEFAULT_SPIN_NS); return; }
    vrun(t);
    if (t > g_vt) g_vt = t;
}
/* The thread loses ns: preemption, or a slow frame */
static void vlose(int64_t ns) { vrun(g_vt + ns); g_vt += ns; }

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- harness */

static int g_failures = 0;

static void fail(int line, const char* what) {
    fprintf(stderr, "screen_test: FAIL at line %d: %s\n", line, what);
    g_failures++;
}
static void fail_i(int line, const char* what, long long got, long long want) {
    fprintf(stderr, "screen_test: FAIL at line %d: %s (got %lld, want %lld)\n", line, what, got, want);
    g_failures++;
}
#define CHECK(cond) do { if (!(cond)) fail(__LINE__, #cond); } while (0)
#define CHECK_I(got, want) do { long long g_ = (long long)(got), w_ = (long long)(want); \
    if (g_ != w_) fail_i(__LINE__, #got " == " #want, g_, w_); } while (0)

#define P_NS 4000000          /* the scripted display's period: 250 Hz */

/* ------------------------------------------------------ scripted presenter */

#define MAX_ID 4096

typedef struct flight {
    uint64_t id;
    int64_t  count;
    uint8_t  path;
    int      used;
} flight;

typedef struct script {
    int64_t  t0;
    int      native;              /* honors target_count, as GLX OML does   */
    int      depth;               /* vblanks from present to flip           */
    uint8_t  path;
    uint64_t change_id;           /* from this present on: depth2, path2    */
    int      depth2;
    uint8_t  path2;
    int      flap;                /* > 0: from change_id on, the path and
                                   * depth switch every flap presents     */
    int      free_at_flip;        /* the slot frees at the flip, as on
                                   * COMPOSITION, not depth - 1 before    */
    unsigned char drop[MAX_ID];   /* one extra vblank for this present     */
    unsigned char missing[MAX_ID];/* no statistic for this present          */
    unsigned char act[MAX_ID];    /* 1 skipped, 2 canceled, 3 no time with
                                   * OCCLUDED, 4 an ONSET_PLANNED time      */
    int64_t  preempt_next;        /* ns the next present call loses, after
                                   * the header planned its flip            */
    int64_t  gpu_ns[MAX_ID];      /* GPU work after the present call        */
    int64_t  gpu_done_t[MAX_ID];  /* when that work finished                */
    uint64_t max_id;
    /* codes as a draws_patch presenter got them, per present id */
    uint32_t code_seen[MAX_ID][3];
    flight   fl[8];
    int64_t  last_shown;
    int      opened, closed, presents;
} script;

static int64_t now_ns(void) { return vnow(); }

static int64_t sc_count(const script* c, int64_t t) {
    int64_t d = t - c->t0;
    return d >= 0 ? d / P_NS : -((-d + P_NS - 1) / P_NS);
}

static int sc_open(void* ctx, const yscr_presenter_open* in, yscr_caps* caps, char* err, size_t cap) {
    script* c = (script*)ctx;
    (void)in; (void)err; (void)cap;
    c->t0 = now_ns();
    c->last_shown = -1;
    caps->kind = YSCR_FIXED_GRID;
    caps->period_ns = P_NS;
    caps->native_target = c->native != 0;
    caps->hw_onset = true;
    caps->max_in_flight = 1;
    caps->mode.w = 640;
    caps->mode.h = 480;
    caps->mode.refresh_num = 250;
    caps->mode.refresh_den = 1;
    caps->mode.period_ns = P_NS;
    c->opened++;
    return YSCR_OK;
}

static void sc_close(void* ctx) { ((script*)ctx)->closed++; }

static int sc_second(const script* c, uint64_t id) {
    if (!c->change_id || id < c->change_id) return 0;
    return c->flap > 0 ? (int)(((id - c->change_id) / (uint64_t)c->flap) % 2 == 0) : 1;
}
static int sc_depth(const script* c, uint64_t id) {
    return sc_second(c, id) ? c->depth2 : c->depth;
}

/* The slot frees depth - 1 vblanks before the flip, as DXGI's waitable
 * object does on a composed path. */
static int sc_acquire(void* ctx, int64_t deadline_ns, yscr_vblank* newest) {
    script* c = (script*)ctx;
    int i;
    int64_t until = 0;
    (void)deadline_ns;
    for (i = 0; i < 8; i++) {
        if (!c->fl[i].used) continue;
        {
            int64_t free_at = c->t0 + (c->fl[i].count - (c->free_at_flip ? 1 : sc_depth(c, c->fl[i].id)) + 1) * P_NS;
            if (free_at > until) until = free_at;
        }
    }
    if (until > now_ns()) vsleep_until(until);
    newest->t_ns = 0;
    return YSCR_OK;
}

static int sc_present(void* ctx, const yscr_present_req* req) {
    script* c = (script*)ctx;
    int i, depth = sc_depth(c, req->present_id);
    int64_t shown;
    if (c->preempt_next) { vlose(c->preempt_next); c->preempt_next = 0; }
    shown = sc_count(c, now_ns()) + depth;
    if (req->present_id < MAX_ID) {
        int64_t done = now_ns() + c->gpu_ns[req->present_id];
        c->gpu_done_t[req->present_id] = done;
        if (c->gpu_ns[req->present_id]) shown = sc_count(c, done) + depth;   /* the GPU decides */
        if (req->present_id > c->max_id) c->max_id = req->present_id;
        {
            int k;
            for (k = 0; k < req->n_codes && k < 3; k++)
                c->code_seen[req->present_id][k] = req->codes[k].px ? req->codes[k].px[0] : req->codes[k].value;
        }
    }
    if (c->native && req->target_count > shown) shown = req->target_count;
    if (shown <= c->last_shown) shown = c->last_shown + 1;
    if (req->present_id < MAX_ID && c->drop[req->present_id]) shown++;
    c->last_shown = shown;
    for (i = 0; i < 8; i++) {
        if (c->fl[i].used) continue;
        c->fl[i].used = 1;
        c->fl[i].id = req->present_id;
        c->fl[i].count = shown;
        c->fl[i].path = sc_second(c, req->present_id) ? c->path2 : c->path;
        break;
    }
    c->presents++;
    return YSCR_OK;
}

static int sc_completions(void* ctx, yscr_vblank* out, int cap) {
    script* c = (script*)ctx;
    int n = 0;
    int64_t now = now_ns();
    for (;;) {   /* oldest first */
        int i, best = -1;
        for (i = 0; i < 8; i++)
            if (c->fl[i].used && c->t0 + c->fl[i].count * P_NS <= now && (best < 0 || c->fl[i].id < c->fl[best].id)) best = i;
        if (best < 0 || n >= cap) break;
        c->fl[best].used = 0;
        if (c->fl[best].id < MAX_ID && c->missing[c->fl[best].id]) continue;
        out[n].present_id = c->fl[best].id;
        out[n].t_ns = c->t0 + c->fl[best].count * P_NS;
        out[n].count = c->fl[best].count;
        out[n].path = c->fl[best].path;
        out[n].flags = 0;
        if (c->fl[best].id < MAX_ID) switch (c->act[c->fl[best].id]) {
        case 1: out[n].flags = YSCR_FLIP_SKIPPED; out[n].t_ns = 0; break;
        case 2: out[n].flags = YSCR_FLIP_CANCELED; out[n].t_ns = 0; break;
        case 3: out[n].flags = YSCR_FLIP_OCCLUDED; out[n].t_ns = 0; break;
        case 4: out[n].flags = YSCR_FLIP_ONSET_PLANNED; break;
        default: break;
        }
        out[n].tier = 0;
        out[n].reserved_ = 0;
        n++;
    }
    return n;
}

static uint64_t sc_gpu_done(void* ctx) {
    script* c = (script*)ctx;
    uint64_t id, best = 0;
    for (id = 1; id <= c->max_id && id < MAX_ID; id++) if (c->gpu_done_t[id] <= now_ns()) best = id; else break;
    return best;
}

static const yscr_presenter g_scripted = {
    YSCR_PRESENTER_VERSION, "scripted", false, false, false,
    sc_open, sc_close, sc_acquire, sc_present, sc_completions, NULL, NULL, NULL, sc_gpu_done
};
/* The same display, drawing the patch and the codes itself. */
static const yscr_presenter g_scripted_codes = {
    YSCR_PRESENTER_VERSION, "scripted", false, false, true,
    sc_open, sc_close, sc_acquire, sc_present, sc_completions, NULL, NULL, NULL, sc_gpu_done
};

/* ------------------------------------------------------------------ ring */

static unsigned char g_ring_mem[YRT_RING_BYTES(4096)];
static yrt_ring g_ring;
static yrt_event g_ev[4096];

static void ring_reset(void) {
    yrt_ring_desc d;
    d.memory = g_ring_mem;
    d.bytes = sizeof g_ring_mem;
    if (!yrt_ring_open(&g_ring, &d)) fail(__LINE__, "ring open");
}

static int ring_drain(void) {
    int n = yrt_ring_drain(&g_ring, g_ev, 4096);
    return n < 0 ? 0 : n;
}

static bool open_scripted(yscr_screen* s, script* c, double lead, uint32_t index) {
    yscr_desc d;
    memset(s, 0, sizeof *s);
    memset(&d, 0, sizeof d);
    d.backend = YSCR_BACKEND_CUSTOM;
    d.presenter = &g_scripted;
    d.presenter_ctx = c;
    d.ring = &g_ring;
    d.display_index = index;
    d.lead = lead;
    return yscr_open(s, &d);
}

static void busy_until(int64_t t) {
    if (g_virtual) { if (t > g_vt) g_vt = t; return; }
    while (now_ns() < t) { }
}

/* ------------------------------------------------------------- the cases */

/* Prediction is exact on a clean grid, and the ring carries the record. */
static void test_prediction_and_ring(void) {
    static script c;
    yscr_screen s;
    static yscr_frame f;   /* static: gcc -O3 cannot see begin() fill it */
    int i, checked = 0, late = 0, n, flips = 0;
    int64_t pred[200];
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = YSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 3));
    if (!yscr_is_open(&s)) return;
    ring_drain();
    for (i = 0; i < 200; i++) {
        yscr_record out;
        CHECK_I(yscr_begin(&s, &f), YSCR_OK);
        CHECK_I(f.index, i);
        CHECK_I(f.period, P_NS);
        CHECK_I((f.onset - c.t0) % P_NS, 0);
        /* the first vblank a present now can make, not a later one */
        CHECK(f.onset - now_ns() <= (int64_t)c.depth * P_NS);
        pred[i] = f.onset;
        if (i > 1) CHECK(f.last != NULL);
        yscr_mark(&s, YSCR_PHASE_EVALUATE);
        CHECK_I(yscr_flip_at(&s, f.onset, &out), YSCR_OK);
        CHECK_I(out.index, i);
        CHECK_I(out.target, f.onset);
        CHECK(out.flags & YSCR_FLIP_PENDING);
        if (!(out.flags & YSCR_FLIP_LATE_TARGET)) CHECK_I(out.planned, f.onset);
    }
    yscr_close(&s);
    CHECK_I(c.closed, 1);
    n = ring_drain();
    for (i = 0; i < n; i++) {
        const yrt_event* e = &g_ev[i];
        uint32_t idx, flags;
        if (e->source != YRT_SRC_SCREEN || e->kind != YSCR_EV_FLIP) continue;
        flips++;
        CHECK_I(e->aux, 3);
        idx = e->u.u32[9];
        flags = YSCR_EV_FLAGS_OF(e->u.u16[5]);
        CHECK_I(YSCR_EV_PATH_OF(e->u.u16[5]), YSCR_PATH_OVERLAY);
        CHECK_I(YSCR_EV_TIER_OF(e->u.u16[5]), (flags & YSCR_FLIP_ESTIMATED) ? YSCR_TIER_3 : YSCR_TIER_1);
        if (idx >= 200) { fail(__LINE__, "record index out of range"); continue; }
        CHECK_I(e->u.i64[0], pred[idx]);
        CHECK(!(flags & YSCR_FLIP_PENDING));
        if (flags & YSCR_FLIP_LATE_TARGET) { late++; continue; }
        CHECK_I(e->u.u16[4], 0);                       /* dropped */
        CHECK_I((int64_t)e->t_ns, pred[idx]);          /* onset exactly predicted */
        checked++;
    }
    CHECK_I(flips, 200);
    CHECK(checked >= 150);
    if (late) printf("  prediction: %d of 200 frames late on this machine (skipped)\n", late);
}

/* An injected drop is a drop, a busy frame is a late target, and neither
 * moves the depth. */
static void test_drop_and_late(void) {
    static script c;
    yscr_screen s;
    static yscr_frame f;   /* static: gcc -O3 cannot see begin() fill it */
    yscr_record rec[60];
    int i, j, n;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = YSCR_PATH_INDEPENDENT;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 3));
    if (!yscr_is_open(&s)) return;
    /* warmup used ids 1..6; frame k has id 7 + k */
    c.drop[7 + 20] = 1;
    for (i = 0; i < 60; i++) {
        CHECK_I(yscr_begin(&s, &f), YSCR_OK);
        if (i == 40) busy_until(f.onset + P_NS / 2);   /* past its vblank */
        CHECK_I(yscr_flip(&s), YSCR_OK);
    }
    yscr_close(&s);
    memset(rec, 0, sizeof rec);
    n = ring_drain();
    for (j = 0; j < n; j++) {
        const yrt_event* e = &g_ev[j];
        uint32_t idx;
        if (e->source != YRT_SRC_SCREEN || e->kind != YSCR_EV_FLIP) continue;
        idx = e->u.u32[9];
        if (idx < 60) {
            rec[idx].dropped = e->u.u16[4];
            rec[idx].flags = (uint16_t)YSCR_EV_FLAGS_OF(e->u.u16[5]);
            rec[idx].onset = (int64_t)e->t_ns;
            rec[idx].target = e->u.i64[0];
        }
    }
    if (!(rec[20].flags & YSCR_FLIP_LATE_TARGET)) {
        CHECK_I(rec[20].dropped, 1);
        CHECK_I(rec[20].onset - rec[20].target, P_NS);
    }
    CHECK(rec[40].flags & YSCR_FLIP_LATE_TARGET);
    CHECK_I(rec[40].dropped, 0);
    CHECK_I(rec[40].onset - rec[40].target, P_NS);
    /* One drop is not a new depth: the frames after it are on time, not
     * early, and the late frame is not either. */
    for (i = 21; i < 60; i++) {
        if (i == 40 || (rec[i].flags & YSCR_FLIP_LATE_TARGET)) continue;
        CHECK_I(rec[i].dropped, 0);
        CHECK(!(rec[i].flags & YSCR_FLIP_EARLY));
        CHECK_I(rec[i].onset, rec[i].target);
    }
}

/* A path change brings a new depth, adopted at once; a depth change on the
 * same path takes three votes. */
static void test_depth(void) {
    static script c;
    yscr_screen s;
    static yscr_frame f;   /* static: gcc -O3 cannot see begin() fill it */
    int i, j, n, path_events = 0, after_ok = 0;
    uint32_t dropped[80];
    uint16_t flags[80];
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = YSCR_PATH_OVERLAY;
    c.change_id = 7 + 30;
    c.depth2 = 2;
    c.path2 = YSCR_PATH_COMPOSED;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 3));
    if (!yscr_is_open(&s)) return;
    for (i = 0; i < 80; i++) {
        CHECK_I(yscr_begin(&s, &f), YSCR_OK);
        CHECK_I(yscr_flip(&s), YSCR_OK);
    }
    yscr_close(&s);
    memset(dropped, 0, sizeof dropped);
    memset(flags, 0, sizeof flags);
    n = ring_drain();
    for (j = 0; j < n; j++) {
        const yrt_event* e = &g_ev[j];
        if (e->source == YRT_SRC_SCREEN && e->kind == YSCR_EV_PATH && e->u.u16[1] == YSCR_PATH_COMPOSED) path_events++;
        if (e->source == YRT_SRC_SCREEN && e->kind == YSCR_EV_FLIP && e->u.u32[9] < 80) {
            dropped[e->u.u32[9]] = e->u.u16[4];
            flags[e->u.u32[9]] = (uint16_t)YSCR_EV_FLAGS_OF(e->u.u16[5]);
        }
    }
    CHECK_I(path_events, 1);
    if (getenv("YSCR_TEST_TRACE"))
        for (i = 26; i < 42; i++) printf("  depth case: frame %d dropped %u flags 0x%x\n", i, dropped[i], flags[i]);
    /* Frames 30 and 31 were predicted at depth 1 and drop. The change is
     * known from frame 30's statistic, and a new path is adopted on one
     * vote, so from frame 32 on all are on time. */
    if (!(flags[30] & YSCR_FLIP_LATE_TARGET)) CHECK_I(dropped[30], 1);
    for (i = 32; i < 80; i++)
        if (!(flags[i] & YSCR_FLIP_LATE_TARGET)) { CHECK_I(dropped[i], 0); after_ok++; }
    CHECK(after_ok >= 36);
    for (i = 5; i < 30; i++)
        if (!(flags[i] & YSCR_FLIP_LATE_TARGET)) CHECK_I(dropped[i], 0);
}

/* A backend that shows a frame at its target cannot show a smaller depth
 * by an on-time flip, and the header never tries one (a failed try drops a
 * frame). Case 0: three misses raise the depth, and the slack seen before
 * them lowers it again within a few flips, with no miss (COMPOSITION ran
 * at half rate without this). Case 1: the display really needs 2 from the
 * misses on and frees its slot at the flip; the old evidence may cost one
 * miss, which clears it, and then no more. Case 2: the same, but the slot
 * frees a vblank early, as DXGI composed does. A random wait of up to a
 * quarter period before each flip gives the slack a spread, as a real
 * frame loop has. */
static void test_native_depth(int case_) {
    static script c;
    yscr_screen s;
    static yscr_frame f;   /* static: gcc -O3 cannot see begin() fill it */
    enum { N = 400, K = 60 };
    static uint32_t dropped[N];
    static uint16_t flags[N];
    static int64_t onset[N];
    int i, j, n, late = 0, drops_after = 0, drops_end = 0, half = 0, steady = 0, behind = 0;
    int64_t prev_vb = 0;
    uint32_t rng = 99u + (uint32_t)case_;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.native = 1;
    c.path = YSCR_PATH_INDEPENDENT;
    if (case_ == 0) {
        c.drop[7 + K] = c.drop[7 + K + 1] = c.drop[7 + K + 2] = 1;
    } else {
        c.change_id = 7 + K;
        c.depth2 = 2;
        c.path2 = YSCR_PATH_INDEPENDENT;   /* same path: no path event */
        c.free_at_flip = case_ == 1;
    }
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 3));
    if (!yscr_is_open(&s)) return;
    for (i = 0; i < N; i++) {
        CHECK_I(yscr_begin(&s, &f), YSCR_OK);
        /* never a vblank a flip in flight already has */
        if (i && f.vblank <= prev_vb) behind++;
        prev_vb = f.vblank;
        rng = rng * 1664525u + 1013904223u;
        busy_until(now_ns() + (int64_t)((rng >> 8) % (P_NS / 4)));
        CHECK_I(yscr_flip(&s), YSCR_OK);
    }
    yscr_close(&s);
    CHECK_I(behind, 0);
    memset(dropped, 0, sizeof dropped);
    memset(flags, 0, sizeof flags);
    memset(onset, 0, sizeof onset);
    n = ring_drain();
    for (j = 0; j < n; j++) {
        const yrt_event* e = &g_ev[j];
        if (e->source == YRT_SRC_SCREEN && e->kind == YSCR_EV_FLIP && e->u.u32[9] < N) {
            dropped[e->u.u32[9]] = e->u.u16[4];
            flags[e->u.u32[9]] = (uint16_t)YSCR_EV_FLAGS_OF(e->u.u16[5]);
            onset[e->u.u32[9]] = (int64_t)e->t_ns;
        }
    }
    /* frames before K are on time at depth 1; from K, case 1 and 2 miss
     * until three misses raise the depth */
    for (i = K + 3; i < N; i++) {
        if (flags[i] & YSCR_FLIP_LATE_TARGET) { late++; continue; }
        if (dropped[i]) { drops_after++; if (i >= N - 150) drops_end++; }
        /* a gap that ends on a LATE_TARGET frame is the caller's lateness */
        if (i + 1 < N && !(flags[i + 1] & YSCR_FLIP_LATE_TARGET) && onset[i + 1] - onset[i] == 2 * P_NS) half++;
    }
    for (i = N - 40; i < N - 1; i++) {
        int64_t want = case_ == 1 ? 2 * P_NS : P_NS;   /* one in flight, slot at the flip */
        if (!(flags[i + 1] & YSCR_FLIP_LATE_TARGET) && onset[i + 1] - onset[i] == want) steady++;
    }
    CHECK(steady >= 30);
    if (case_ == 0) {
        CHECK_I(drops_after, 0);
        /* back to one frame per vblank within a few flips: 7 or 14 here, as
         * the streak of 4 can break on a slack with little evidence */
        CHECK(half <= 20);
    } else {
        CHECK(drops_after <= 1);   /* at most the miss that clears the evidence */
        CHECK_I(drops_end, 0);
    }
    if (late) printf("  native depth %d: %d frames late on this machine\n", case_, late);
    if (getenv("YSCR_TEST_TRACE"))
        printf("  native depth %d: drops after the misses %d, half-rate gaps %d\n", case_, drops_after, half);
}

/* A single miss on a backend that holds frames to their target is a drop,
 * not a new depth: three in a row are needed. And a flip the display shows
 * before its planned vblank (a DXGI-like path whose depth fell from 2 to
 * 1) carries EARLY, and only such a flip does. */
static void test_one_miss_and_early(void) {
    static script c;
    yscr_screen s;
    static yscr_frame f;
    int i, j, n, early = 0, wrong = 0, depth_moved = 0;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.native = 1;
    c.path = YSCR_PATH_INDEPENDENT;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 0));
    if (yscr_is_open(&s)) {
        for (i = 0; i < 60; i++) {
            CHECK_I(yscr_begin(&s, &f), YSCR_OK);
            if (i == 20) c.drop[s.next_id + 1] = 1;
            CHECK_I(yscr_flip(&s), YSCR_OK);
            if (i > 20 && s.depth != 1) depth_moved++;
        }
        yscr_close(&s);
    }
    CHECK_I(depth_moved, 0);

    memset(&c, 0, sizeof c);
    c.depth = 2;
    c.path = YSCR_PATH_COMPOSED;
    c.change_id = 40;
    c.depth2 = 1;
    c.path2 = YSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 0));
    if (!yscr_is_open(&s)) return;
    for (i = 0; i < 60; i++) {
        CHECK_I(yscr_begin(&s, &f), YSCR_OK);
        CHECK_I(yscr_flip(&s), YSCR_OK);
    }
    yscr_close(&s);
    n = ring_drain();
    for (j = 0; j < n; j++) {
        const yrt_event* e = &g_ev[j];
        int is_early, flagged;
        if (e->source != YRT_SRC_SCREEN || e->kind != YSCR_EV_FLIP) continue;
        is_early = (int64_t)e->t_ns < e->u.i64[0];
        flagged = (YSCR_EV_FLAGS_OF(e->u.u16[5]) & YSCR_FLIP_EARLY) != 0;
        if (flagged) early++;
        if (is_early != flagged) wrong++;
    }
    CHECK(early >= 1);   /* the first flip after the depth fell */
    CHECK_I(wrong, 0);
}

/* A window that moves between the composed path (depth 2) and the
 * overlay (depth 1), as DXGI_FLIP did in a window (the video agent's
 * report). When the depth falls, the header learns it from the flips, so
 * a flip planned at the old depth shows a vblank early (EARLY). Then:
 *   - begin() never plans a frame for the vblank the frame before it was
 *     planned for: it did, for the flip after each early one (same onset);
 *   - one early flip per fall, not three: a flip that waited in the queue
 *     behind the one before it closed the path change's one-vote window;
 *   - a vblank no frame shows on comes only after an early flip (that
 *     flip shows twice) or before a late one. */
static void test_flap_prediction(int native) {
    static script c;
    yscr_screen s;
    static yscr_frame f;
    enum { N = 300 };
    static int64_t vb[N], pred[N], shown_t[N];
    int i, j, n, same = 0, back = 0, run = 0, max_run = 0, early = 0, unused = 0, unexplained = 0;
    uint32_t rng = 7u;
    memset(&c, 0, sizeof c);
    c.native = native;
    c.depth = 2;
    c.path = YSCR_PATH_COMPOSED;
    c.change_id = 30;
    c.flap = 9;
    c.depth2 = 1;
    c.path2 = YSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 0));
    if (!yscr_is_open(&s)) return;
    for (i = 0; i < N; i++) {
        CHECK_I(yscr_begin(&s, &f), YSCR_OK);
        vb[i] = f.vblank;
        pred[i] = f.onset;
        if (i && vb[i] == vb[i - 1]) same++;
        if (i && vb[i] < vb[i - 1]) back++;
        rng = rng * 1664525u + 1013904223u;
        busy_until(now_ns() + (int64_t)((rng >> 8) % (P_NS / 3)));
        CHECK_I(yscr_flip(&s), YSCR_OK);
    }
    yscr_close(&s);
    memset(shown_t, 0, sizeof shown_t);
    n = ring_drain();
    for (j = 0; j < n; j++) {
        const yrt_event* e = &g_ev[j];
        if (e->source == YRT_SRC_SCREEN && e->kind == YSCR_EV_FLIP && e->u.u32[9] < N &&
            !(YSCR_EV_FLAGS_OF(e->u.u16[5]) & (YSCR_FLIP_SKIPPED | YSCR_FLIP_CANCELED)))
            shown_t[e->u.u32[9]] = (int64_t)e->t_ns;
    }
    for (i = 0; i < N; i++) {
        int is_early = shown_t[i] && shown_t[i] < pred[i] - P_NS / 2;
        early += is_early;
        run = is_early ? run + 1 : 0;
        if (run > max_run) max_run = run;
        if (i && shown_t[i] && shown_t[i - 1] && shown_t[i] - shown_t[i - 1] > P_NS + P_NS / 2) {
            unused++;
            if (!(shown_t[i - 1] < pred[i - 1] - P_NS / 2) && !(shown_t[i] > pred[i] + P_NS / 2)) unexplained++;
        }
    }
    if (getenv("YSCR_TEST_TRACE"))
        printf("  flap %d: early %d (longest run %d), same vblank %d, back %d, unused vblanks %d (unexplained %d)\n",
               native, early, max_run, same, back, unused, unexplained);
    CHECK_I(same, 0);
    CHECK_I(back, 0);
    CHECK(max_run <= 1);
    /* On a backend that holds frames, one frame in flight at depth 2 runs
     * at half rate on a path whose depth the header has not lowered; that
     * is the depth rule (DEPTH), not this check. */
    if (!native) { CHECK(early >= 1); CHECK_I(unexplained, 0); }

    /* flip_at() keeps the rule too: a target on the vblank the last frame
     * was planned for, after that frame completed (as an early one does),
     * goes one vblank on, flagged LATE_TARGET. */
    memset(&c, 0, sizeof c);
    c.native = native;
    c.depth = 1;
    c.path = YSCR_PATH_OVERLAY;
    CHECK(open_scripted(&s, &c, 0, 0));
    if (!yscr_is_open(&s)) return;
    {
        yscr_record r;
        CHECK_I(yscr_begin(&s, &f), YSCR_OK);
        s.last_planned = f.vblank;   /* as if the frame before had it */
        CHECK_I(yscr_flip_at(&s, f.onset, &r), YSCR_OK);
        CHECK(r.planned > f.onset + f.period / 2);
        CHECK(r.flags & YSCR_FLIP_LATE_TARGET);
    }
    yscr_close(&s);
}

/* f.done carries every record completed since the begin() before, in
 * the ring's order, including those completed inside flip_at() while it
 * held a frame and those with no statistic (ESTIMATED); f.last is its
 * final one. And yscr_native() has nothing to give off Windows' swap
 * paths. */
static void test_done_and_native(void) {
    static script c;
    yscr_screen s;
    static yscr_frame f;
    enum { N = 120 };
    static int64_t got[4 * N];
    yscr_native_info nat;
    int i, j, n, n_got = 0, n_ring = 0, order_ok = 1, held = 0;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = YSCR_PATH_OVERLAY;
    for (i = 0; i < MAX_ID; i += 17) c.missing[i] = 1;
    for (i = 5; i < MAX_ID; i += 23) c.drop[i] = 1;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 0));
    if (!yscr_is_open(&s)) return;
    CHECK_I(yscr_native(&s, &nat), YSCR_ERR_NOT_IMPLEMENTED);
    CHECK(nat.d3d11_device == NULL && nat.d3d11_context == NULL && nat.egl_display == NULL);
    CHECK(nat.luid_low == 0 && nat.luid_high == 0);
    CHECK_I(yscr_native(&s, NULL), YSCR_ERR_ARG);
    for (i = 0; i < N; i++) {
        int k;
        CHECK_I(yscr_begin(&s, &f), YSCR_OK);
        CHECK(f.n_done >= 0 && f.n_done <= YSCR_MAX_DONE);
        if (f.n_done > 0) {
            CHECK(f.last != NULL);
            if (f.last) CHECK_I(f.done[f.n_done - 1].index, f.last->index);
        }
        if (f.n_done > 1) held++;
        for (k = 0; k < f.n_done && n_got < 4 * N; k++) {
            CHECK(!(f.done[k].flags & YSCR_FLIP_PENDING));
            got[n_got++] = f.done[k].index;
        }
        /* every fourth frame 3 vblanks ahead: the hold reads statistics */
        CHECK_I(yscr_flip_at(&s, f.onset + (i % 4 == 0 ? 3 * f.period : 0), NULL), YSCR_OK);
    }
    yscr_close(&s);
    CHECK_I(f.done_lost, 0);
    CHECK_I(yscr_native(&s, &nat), YSCR_ERR_CLOSED);
    n = ring_drain();
    for (j = 0; j < n; j++) {
        const yrt_event* e = &g_ev[j];
        if (e->source != YRT_SRC_SCREEN || e->kind != YSCR_EV_FLIP) continue;
        if (n_ring < n_got && (int64_t)e->u.u32[9] != got[n_ring]) order_ok = 0;
        n_ring++;
    }
    CHECK(n_got >= N - 3);           /* all but the last, which complete in close() */
    CHECK(n_ring >= n_got);
    CHECK(order_ok);
    CHECK(held > 0);                 /* some begin() delivered more than one */
}

/* Preemption, on purpose. Lateness of the calling thread makes a flip late,
 * never early: losing time between begin() and the flip is the caller's
 * (LATE_TARGET); losing it inside the present call, after the header
 * planned the flip, is a drop the header cannot see coming; and a flip
 * whose call lands within the margin before a vblank waits for that
 * vblank, because a DXGI-like display would otherwise show it a vblank
 * before the one the header planned (it did, with EARLY set, before the
 * fix). */
static void test_preemption(int native) {
    static script c;
    yscr_screen s;
    static yscr_frame f;   /* static: gcc -O3 cannot see begin() fill it */
    enum { N = 120 };
    static int64_t planned[N], onset[N];
    static uint32_t dropped[N];
    static uint16_t flags[N];
    int i, j, n;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.native = native;
    c.path = YSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 3));
    if (!yscr_is_open(&s)) return;
    memset(planned, 0, sizeof planned);
    for (i = 0; i < N; i++) {
        yscr_record out;
        int64_t vb;
        CHECK_I(yscr_begin(&s, &f), YSCR_OK);
        vlose(P_NS / 20);   /* a short draw */
        vb = c.t0 + (sc_count(&c, now_ns()) + 1) * P_NS;   /* the next vblank */
        switch (i % 20) {
        case 5: vlose(P_NS + P_NS / 2); break;            /* caller preempted */
        case 9: c.preempt_next = P_NS + P_NS / 2; break;  /* preempted in the call */
        case 13: busy_until(vb - P_NS / 16); break;       /* inside the margin */
        case 17: busy_until(vb - 1000); break;            /* 1 us before a vblank */
        default: break;
        }
        CHECK_I(yscr_flip_at(&s, f.onset, &out), YSCR_OK);
        planned[i] = out.planned;
    }
    yscr_close(&s);
    memset(onset, 0, sizeof onset);
    memset(dropped, 0, sizeof dropped);
    memset(flags, 0, sizeof flags);
    n = ring_drain();
    for (j = 0; j < n; j++) {
        const yrt_event* e = &g_ev[j];
        uint32_t idx;
        if (e->source != YRT_SRC_SCREEN || e->kind != YSCR_EV_FLIP) continue;
        idx = e->u.u32[9];
        if (idx >= N) continue;
        onset[idx] = (int64_t)e->t_ns;
        dropped[idx] = e->u.u16[4];
        flags[idx] = (uint16_t)YSCR_EV_FLAGS_OF(e->u.u16[5]);
    }
    for (i = 0; i < N; i++) {
        int k = i % 20;
        CHECK(onset[i] != 0);
        CHECK(!(flags[i] & YSCR_FLIP_EARLY));
        CHECK(onset[i] >= planned[i]);
        if (k == 5) {
            CHECK(flags[i] & YSCR_FLIP_LATE_TARGET);
            CHECK_I(dropped[i], 0);
            CHECK_I(onset[i], planned[i]);
        } else if (k == 9) {
            CHECK(!(flags[i] & YSCR_FLIP_LATE_TARGET));
            CHECK(dropped[i] >= 1);
        } else if (k == 13 || k == 17) {
            CHECK_I(dropped[i], 0);
            CHECK_I(onset[i], planned[i]);
        } else if (k != 6 && k != 10) {   /* the frame after a lost one may be late */
            CHECK(!(flags[i] & YSCR_FLIP_LATE_TARGET));
            CHECK_I(dropped[i], 0);
            CHECK_I(onset[i], planned[i]);
        }
    }
}


/* -------------------------------------------------------- triggers, codes */

static struct { int n; yscr_trigger_info e[1024]; } g_tl;
static void tl_fn(void* ctx, const yscr_trigger_info* i) {
    (void)ctx;
    if (g_tl.n < 1024) g_tl.e[g_tl.n++] = *i;
}
static struct { int n; yscr_record r[512]; yscr_trigger_result t[512]; int nt[512]; } g_fl;
static void fl_fn(void* ctx, const yscr_record* r, const yscr_trigger_result* t, int n) {
    (void)ctx;
    if (g_fl.n < 512) {
        g_fl.r[g_fl.n] = *r;
        g_fl.nt[g_fl.n] = n;
        if (n > 0) g_fl.t[g_fl.n] = t[0];
        g_fl.n++;
    }
}

static bool open_trig(yscr_screen* s, script* c, int n_ch, const int64_t* offsets, bool fence) {
    static yscr_trigger_desc td[4];
    yscr_desc d;
    int i;
    memset(s, 0, sizeof *s);
    memset(&d, 0, sizeof d);
    for (i = 0; i < n_ch; i++) {
        memset(&td[i], 0, sizeof td[i]);
        td[i].fn = tl_fn;
        td[i].offset_ns = offsets ? offsets[i] : 0;
    }
    d.backend = YSCR_BACKEND_CUSTOM;
    d.presenter = &g_scripted;
    d.presenter_ctx = c;
    d.ring = &g_ring;
    d.triggers = td;
    d.n_triggers = n_ch;
    d.trigger_fence = fence;
    g_tl.n = 0;
    g_fl.n = 0;
    if (!yscr_open(s, &d)) return false;
    yscr_on_flip(s, fl_fn, NULL);
    return true;
}

/* A trigger fires at its flip's planned vblank. Lateness the header knows
 * before that vblank moves it; lateness it learns after is reported, once,
 * by the after-flip hook. Every case on purpose, on the virtual clock:
 *   i % 20 == 5   the caller loses 1.5 periods before flip_at (LATE_TARGET)
 *   i % 20 == 9   the present call loses 1.5 periods (MOVED, no mismatch)
 *   i % 20 == 13  the frame misses after a timely present (FIRED_EARLY) */
static void test_triggers(void) {
    static script c;
    yscr_screen s;
    static yscr_frame f;   /* static: gcc -O3 cannot see begin() fill it */
    enum { N = 100 };
    int i, j, n, by_frame[N], seen = 0, recs = 0;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = YSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_trig(&s, &c, 1, NULL, false));
    if (!yscr_is_open(&s)) return;
    for (i = 0; i < N; i++) {
        CHECK_I(yscr_begin(&s, &f), YSCR_OK);
        CHECK_I(yscr_trigger(&s, 0, (uint32_t)i), YSCR_OK);
        vlose(P_NS / 20);
        if (i % 20 == 5) vlose(P_NS + P_NS / 2);
        if (i % 20 == 9) c.preempt_next = P_NS + P_NS / 2;
        if (i % 20 == 13) c.drop[s.next_id + 1] = 1;
        CHECK_I(yscr_flip_at(&s, f.onset, NULL), YSCR_OK);
    }
    CHECK_I(yscr_trigger(&s, 0, 1), YSCR_ERR_ORDER);   /* outside a frame */
    yscr_close(&s);
    vforget(&s);
    for (i = 0; i < N; i++) by_frame[i] = -1;
    for (j = 0; j < g_tl.n; j++) if (g_tl.e[j].frame >= 0 && g_tl.e[j].frame < N) by_frame[g_tl.e[j].frame] = j;
    for (j = 0; j < g_fl.n; j++) {
        const yscr_record* r = &g_fl.r[j];
        const yscr_trigger_result* t = &g_fl.t[j];
        int k;
        if (r->index < 0 || r->index >= N) continue;
        recs++;
        CHECK_I(g_fl.nt[j], 1);
        if (g_fl.nt[j] != 1) continue;
        k = (int)(r->index % 20);
        CHECK(by_frame[r->index] >= 0);
        CHECK_I(t->code, r->index);
        CHECK(!(t->flags & YSCR_TRIG_FLUSHED));
        if (k == 13) {
            CHECK_I(r->dropped, 1);
            CHECK(t->flags & YSCR_TRIG_FIRED_EARLY);
            CHECK_I(t->mismatch, 1);
            CHECK_I(t->fired_ns, r->onset - P_NS);   /* on the planned vblank */
        } else {
            CHECK_I(t->mismatch, 0);
            CHECK_I(t->fired_ns, r->onset);           /* exactly on the onset */
            CHECK(!(t->flags & (YSCR_TRIG_FIRED_EARLY | YSCR_TRIG_FIRED_LATE)));
            if (k == 9) { CHECK(t->flags & YSCR_TRIG_MOVED); CHECK(r->dropped >= 1); }
            if (k == 5) { CHECK(r->flags & YSCR_FLIP_LATE_TARGET); CHECK(!(t->flags & YSCR_TRIG_MOVED)); }
            if (k != 9 && k != 13) CHECK_I(r->dropped, 0);
        }
        seen++;
    }
    CHECK(recs >= N - 1);
    CHECK_I(seen, recs);
    /* the ring has one TRIGGER record per trigger, with the same verdict */
    n = ring_drain();
    {
        int trig = 0, early = 0;
        for (j = 0; j < n; j++) {
            const yrt_event* e = &g_ev[j];
            if (e->source != YRT_SRC_SCREEN || e->kind != YSCR_EV_TRIGGER) continue;
            trig++;
            if (e->u.u16[13] & YSCR_TRIG_FIRED_EARLY) { early++; CHECK_I(e->u.i32[7], 1); }
            CHECK_I((int64_t)e->t_ns, e->u.i64[0]);   /* fired at its deadline */
        }
        CHECK_I(trig, N);
        CHECK_I(early, N / 20);
    }
}

/* The GPU check: a frame whose GPU work runs past its vblank is moved
 * before the trigger fires, so the trigger lands on the vblank shown. */
static void test_trigger_fence(int fence) {
    static script c;
    yscr_screen s;
    static yscr_frame f;
    int i, j, early = 0, gpu_moved = 0;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = YSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_trig(&s, &c, 1, NULL, fence != 0));
    if (!yscr_is_open(&s)) return;
    for (i = 0; i < 40; i++) {
        CHECK_I(yscr_begin(&s, &f), YSCR_OK);
        CHECK_I(yscr_trigger(&s, 0, (uint32_t)i), YSCR_OK);
        if (i % 10 == 4) c.gpu_ns[s.next_id + 1] = P_NS + P_NS / 5;   /* finishes after its vblank */
        CHECK_I(yscr_flip_at(&s, f.onset, NULL), YSCR_OK);
    }
    yscr_close(&s);
    vforget(&s);
    for (j = 0; j < g_fl.n; j++) {
        const yscr_trigger_result* t = &g_fl.t[j];
        if (g_fl.r[j].index < 0 || g_fl.nt[j] != 1) continue;
        if (t->flags & YSCR_TRIG_FIRED_EARLY) early++;
        if (t->flags & YSCR_TRIG_GPU_MOVED) { gpu_moved++; CHECK_I(t->mismatch, 0); CHECK_I(t->fired_ns, g_fl.r[j].onset); }
    }
    if (fence) { CHECK_I(early, 0); CHECK_I(gpu_moved, 4); }
    else { CHECK_I(early, 4); CHECK_I(gpu_moved, 0); }
}

/* Several channels with offsets fire in deadline order; trigger_at fires at
 * its time; a frame takes at most YSCR_MAX_JOBS; close flushes what is
 * still armed, flagged. */
static void test_trigger_channels(void) {
    static script c;
    yscr_screen s;
    static yscr_frame f;
    static const int64_t off[3] = { 0, 1000000, -500000 };
    int i, j, refused = 0;
    int64_t t_at;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = YSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_trig(&s, &c, 3, off, false));
    if (!yscr_is_open(&s)) return;
    CHECK_I(yscr_begin(&s, &f), YSCR_OK);
    for (i = 0; i < 3; i++) CHECK_I(yscr_trigger(&s, i, 100u + (uint32_t)i), YSCR_OK);
    CHECK_I(yscr_trigger(&s, 3, 1), YSCR_ERR_ARG);
    for (i = 3; i < YSCR_MAX_JOBS + 1; i++) if (yscr_trigger(&s, 0, 0) == YSCR_ERR_REFUSED) refused++;
    CHECK_I(refused, 1);
    CHECK_I(yscr_flip_at(&s, f.onset, NULL), YSCR_OK);
    for (i = 0; i < 3; i++) {   /* the frame's 16 jobs fire and free their slots */
        CHECK_I(yscr_begin(&s, &f), YSCR_OK);
        CHECK_I(yscr_flip_at(&s, f.onset, NULL), YSCR_OK);
    }
    t_at = now_ns() + 7 * P_NS + 12345;
    CHECK_I(yscr_trigger_at(&s, 1, 777, t_at), YSCR_OK);
    CHECK_I(yscr_trigger_at(&s, 0, 888, now_ns() + 1000000000LL), YSCR_OK);   /* still armed at close */
    for (i = 0; i < 12; i++) {
        CHECK_I(yscr_begin(&s, &f), YSCR_OK);
        CHECK_I(yscr_flip_at(&s, f.onset, NULL), YSCR_OK);
    }
    yscr_close(&s);
    vforget(&s);
    /* the frame's 16 in deadline order: channel 2 (-0.5 ms), the 14 on
     * channel 0, channel 1 (+1 ms) */
    CHECK(g_tl.n >= YSCR_MAX_JOBS);
    if (g_tl.n >= YSCR_MAX_JOBS) {
        CHECK_I(g_tl.e[0].channel, 2);
        CHECK_I(g_tl.e[1].channel, 0);
        CHECK_I(g_tl.e[YSCR_MAX_JOBS - 1].channel, 1);
        CHECK_I(g_tl.e[0].fired_ns, g_tl.e[1].fired_ns - 500000);
        CHECK_I(g_tl.e[YSCR_MAX_JOBS - 1].fired_ns, g_tl.e[1].fired_ns + 1000000);
    }
    for (j = 0; j < g_tl.n; j++) {
        if (g_tl.e[j].code == 777) { CHECK_I(g_tl.e[j].fired_ns, t_at + 1000000); CHECK_I(g_tl.e[j].frame, -1); }
        if (g_tl.e[j].code == 888) CHECK(g_tl.e[j].flags & YSCR_TRIG_FLUSHED);
    }
    CHECK_I(g_tl.n, YSCR_MAX_JOBS + 2);
}

/* The race the lock settles: a trigger the worker has taken (FIRING) when
 * its flip turns out late is a mismatch, never moved and never fired twice.
 * The worker's choice is made by hand here, between the two calls the
 * real threads would make. */
static void test_trigger_race(void) {
    static script c;
    yscr_screen s;
    static yscr_frame f;
    yscr__pend fake;
    yscr__job* j = NULL;
    int i, fired_before;
    int64_t deadline;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = YSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_trig(&s, &c, 1, NULL, false));
    if (!yscr_is_open(&s)) return;
    CHECK_I(yscr_begin(&s, &f), YSCR_OK);
    CHECK_I(yscr_trigger(&s, 0, 5), YSCR_OK);
    CHECK_I(yscr_flip_at(&s, f.onset, NULL), YSCR_OK);
    for (i = 0; i < YSCR_MAX_JOBS; i++) if (s.job[i].state == YSCR__J_ARMED) j = &s.job[i];
    CHECK(j != NULL);
    if (j) {
        j->state = YSCR__J_FIRING;   /* the worker took it */
        deadline = j->deadline;
        fired_before = g_tl.n;
        memset(&fake, 0, sizeof fake);
        fake.id = j->pend_id;
        fake.rec.index = j->frame;
        fake.rec.onset = deadline + P_NS;
        yscr__trig_flip_done(&s, &fake, j->count + 1);   /* shown a vblank later */
        CHECK_I(j->state, YSCR__J_FIRING);
        CHECK_I(j->deadline, deadline);                    /* not moved */
        CHECK_I(j->mismatch, 1);
        CHECK(j->flags & YSCR_TRIG_FIRED_EARLY);
        yscr__trig_run(&s, deadline + 10 * P_NS, 0);    /* the worker runs again */
        CHECK_I(g_tl.n, fired_before);                     /* no second firing */
        j->state = YSCR__J_FIRED;                        /* the worker finishes */
        j->fired = deadline;
    }
    /* Not taken yet: the record moves it to the vblank shown, where it
     * fires once, with no mismatch. */
    CHECK_I(yscr_begin(&s, &f), YSCR_OK);
    CHECK_I(yscr_trigger(&s, 0, 6), YSCR_OK);
    CHECK_I(yscr_flip_at(&s, f.onset, NULL), YSCR_OK);
    j = NULL;
    for (i = 0; i < YSCR_MAX_JOBS; i++) if (s.job[i].state == YSCR__J_ARMED && s.job[i].code == 6) j = &s.job[i];
    CHECK(j != NULL);
    if (j) {
        deadline = j->deadline;
        fired_before = g_tl.n;
        memset(&fake, 0, sizeof fake);
        fake.id = j->pend_id;
        fake.rec.index = j->frame;
        fake.rec.onset = deadline + P_NS;
        yscr__trig_flip_done(&s, &fake, j->count + 1);
        CHECK_I(j->state, YSCR__J_ARMED);
        CHECK(j->deadline > deadline + P_NS / 2);          /* one vblank on */
        CHECK(j->flags & YSCR_TRIG_MOVED);
        CHECK_I(j->mismatch, 0);
        yscr__trig_run(&s, deadline, 0);                 /* the old deadline: nothing */
        CHECK_I(g_tl.n, fired_before);
        yscr__trig_run(&s, j->deadline, 0);
        CHECK_I(g_tl.n, fired_before + 1);
        yscr__trig_run(&s, j->deadline + P_NS, 0);
        CHECK_I(g_tl.n, fired_before + 1);
    }
    /* A worker late past two deadlines fires both in deadline order, not
     * in the order they were armed. */
    {
        int64_t t0 = now_ns() + 50 * P_NS;
        fired_before = g_tl.n;
        CHECK_I(yscr_trigger_at(&s, 0, 71, t0 + 1000), YSCR_OK);
        CHECK_I(yscr_trigger_at(&s, 0, 72, t0), YSCR_OK);
        yscr__trig_run(&s, t0 + 5000, 0);
        CHECK_I(g_tl.n, fired_before + 2);
        if (g_tl.n == fired_before + 2) {
            CHECK_I(g_tl.e[fired_before].code, 72);
            CHECK_I(g_tl.e[fired_before + 1].code, 71);
        }
    }
    yscr_close(&s);
    vforget(&s);
}

/* After a flip trigger fires, the worker arms itself for the next
 * vblank, so a trigger every frame costs the frame thread no submit; one
 * that skips frames, or comes after a late frame, still fires on its
 * onset. */
static void test_trigger_spec(void) {
    static script c;
    yscr_screen s;
    static yscr_frame f;
    enum { N = 60 };
    int i, j, steady = 0, fired = 0;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = YSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_trig(&s, &c, 1, NULL, false));
    if (!yscr_is_open(&s)) return;
    for (i = 0; i < N; i++) {
        CHECK_I(yscr_begin(&s, &f), YSCR_OK);
        if (i == 10) g_frame_arms = 0;
        if (i < 30 || i % 4 == 0) CHECK_I(yscr_trigger(&s, 0, (uint32_t)i), YSCR_OK);
        vlose(P_NS / 20);
        if (i == 41) vlose(P_NS + P_NS / 2);   /* a late frame: LATE_TARGET */
        CHECK_I(yscr_flip_at(&s, f.onset, NULL), YSCR_OK);
        if (i == 29) steady = g_frame_arms;
    }
    yscr_close(&s);
    vforget(&s);
    CHECK_I(steady, 0);   /* frames 10 to 29: a trigger each, no submit */
    for (j = 0; j < g_fl.n; j++) {
        if (g_fl.r[j].index < 0 || g_fl.nt[j] != 1) continue;
        CHECK_I(g_fl.t[j].fired_ns, g_fl.r[j].onset);
        CHECK_I(g_fl.t[j].mismatch, 0);
        fired++;
    }
    CHECK_I(fired, 30 + 7);
    CHECK_I(g_tl.n, 30 + 7);
}

/* Codes reach a presenter that draws them, per flip: one-flip, timed and
 * held values, a ROW, the rest value between them, the CODE ring record;
 * and slots that overlap each other or the patch are refused. */
static void test_codes(void) {
    static script c;
    yscr_screen s;
    yscr_desc d;
    static yscr_frame f;
    uint32_t pat[8];
    uint32_t want0[40], want1[40];
    uint64_t id[40];
    int i, j, n, codes_seen = 0;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = YSCR_PATH_OVERLAY;
    memset(&s, 0, sizeof s);
    memset(&d, 0, sizeof d);
    d.backend = YSCR_BACKEND_CUSTOM;
    d.presenter = &g_scripted_codes;
    d.presenter_ctx = &c;
    d.ring = &g_ring;
    d.codes[0] = yscr_slot_pixel_mode();
    d.codes[1] = yscr_slot_psync();
    d.codes[1].rest = 0x000001u;
    d.n_codes = 2;
    /* refused: a patch over the Pixel Mode pixel, then two slots that meet */
    d.patch.on = true;
    CHECK(!yscr_open(&s, &d));
    CHECK(strstr(yscr_error(&s), "patch") != NULL);
    d.patch.on = false;
    d.codes[1].x = 0;
    CHECK(!yscr_open(&s, &d));
    CHECK(strstr(yscr_error(&s), "overlap") != NULL);
    d.codes[1] = yscr_slot_psync();
    d.codes[1].rest = 0x000001u;
    ring_reset();
    CHECK(yscr_open(&s, &d));
    if (!yscr_is_open(&s)) return;
    yscr_psync_pattern(pat, 42);
    CHECK_I(pat[0], 0x0000FFu);
    CHECK_I(pat[7], 42u << 8);
    for (i = 0; i < 40; i++) {
        CHECK_I(yscr_begin(&s, &f), YSCR_OK);
        if (i == 3) yscr_code(&s, 0, 0x123456u);
        if (i == 5) yscr_code_frames(&s, 0, 0xABCDEFu, 3);
        if (i == 10) CHECK_I(yscr_code_row(&s, 1, pat, 8, 1), YSCR_OK);
        if (i == 12) yscr_code_frames(&s, 0, 0x010203u, YSCR_CODE_HOLD);
        if (i == 20) yscr_code_frames(&s, 0, 0, 0);   /* back to rest */
        want0[i] = (i == 3) ? 0x123456u : (i >= 5 && i < 8) ? 0xABCDEFu : (i >= 12 && i < 20) ? 0x010203u : 0u;
        want1[i] = (i == 10) ? 0x0000FFu : 0x000001u;
        id[i] = s.next_id + 1;
        CHECK_I(yscr_flip_at(&s, f.onset, NULL), YSCR_OK);
    }
    CHECK_I(yscr_code_row(&s, 0, pat, 8, 1), YSCR_ERR_ARG);   /* slot 0 is SOLID */
    yscr_close(&s);
    for (i = 0; i < 40; i++) {
        CHECK_I(c.code_seen[id[i]][0], want0[i]);
        CHECK_I(c.code_seen[id[i]][1], want1[i]);
    }
    n = ring_drain();
    for (j = 0; j < n; j++) {
        const yrt_event* e = &g_ev[j];
        uint32_t idx;
        if (e->source != YRT_SRC_SCREEN || e->kind != YSCR_EV_CODE) continue;
        idx = e->u.u32[0];
        if (idx >= 40) continue;
        CHECK_I(e->u.u16[3], 2);
        CHECK_I(e->u.u32[2], want0[idx]);
        CHECK_I(e->u.u32[3], want1[idx]);
        CHECK(e->u.u16[2] & YSCR_CODE_RISK_UNVERIFIED);   /* no self test on a script */
        codes_seen++;
    }
    CHECK(codes_seen >= 39);
}

/* The present callback runs once per flip, before the present, and moves
 * the GL epoch; the generation is fixed from open to close. */
static int g_pres_calls;
static int64_t g_pres_index;
static void pres_fn(void* ctx, const yscr_present_info* i) { (void)ctx; g_pres_calls++; g_pres_index = i->index; }
static void test_present_hook(void) {
    static script c;
    yscr_screen s;
    static yscr_frame f;
    uint32_t e0, g0;
    int i;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = YSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 3));
    if (!yscr_is_open(&s)) return;
    yscr_on_present(&s, pres_fn, NULL);
    e0 = yscr_gl_epoch(&s);
    g0 = yscr_gl_generation(&s);
    CHECK(g0 != 0);
    g_pres_calls = 0;
    for (i = 0; i < 5; i++) {
        CHECK_I(yscr_begin(&s, &f), YSCR_OK);
        CHECK_I(yscr_flip_at(&s, f.onset, NULL), YSCR_OK);
        CHECK_I(g_pres_index, i);
    }
    CHECK_I(g_pres_calls, 5);
    CHECK_I(yscr_gl_epoch(&s) - e0, 5);
    CHECK_I(yscr_gl_generation(&s), g0);
    yscr_close(&s);
    CHECK_I(yscr_gl_generation(&s), 0);
}

/* Missing statistics give ESTIMATED records at the planned vblank. */
static void test_missing(void) {
    static script c;
    yscr_screen s;
    static yscr_frame f;   /* static: gcc -O3 cannot see begin() fill it */
    int i, j, n, est = 0, est_ok = 0;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = YSCR_PATH_OVERLAY;
    for (i = 10; i < 13; i++) c.missing[7 + i] = 1;
    c.missing[7 + 20] = 1;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 3));
    if (!yscr_is_open(&s)) return;
    for (i = 0; i < 40; i++) {
        CHECK_I(yscr_begin(&s, &f), YSCR_OK);
        CHECK_I(yscr_flip(&s), YSCR_OK);
    }
    yscr_close(&s);
    n = ring_drain();
    for (j = 0; j < n; j++) {
        const yrt_event* e = &g_ev[j];
        uint32_t idx, flags;
        if (e->source != YRT_SRC_SCREEN || e->kind != YSCR_EV_FLIP) continue;
        idx = e->u.u32[9];
        flags = YSCR_EV_FLAGS_OF(e->u.u16[5]);
        if (flags & YSCR_FLIP_ESTIMATED) {
            est++;
            if (idx == 10 || idx == 11 || idx == 12 || idx == 20) est_ok++;
            if (!(flags & YSCR_FLIP_LATE_TARGET)) CHECK_I((int64_t)e->t_ns, e->u.i64[0]);
        }
    }
    CHECK_I(est, 4);
    CHECK_I(est_ok, 4);
}

/* Text input (INPUT): off at open; a ring record per change, none for a
 * moved rectangle; the flag on the flips planned while it is on. The
 * scripted backend has no window, so no SDL call is made. */
static void test_text_input(void) {
    static script c;
    yscr_screen s;
    static yscr_frame f;   /* static: gcc -O3 cannot see begin() fill it */
    yscr_record out;
    int i, j, n, recs = 0;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = YSCR_PATH_OVERLAY;
    CHECK_I(yscr_text_input(NULL, true, 0, 0, 1, 1), YSCR_ERR_ARG);
    memset(&s, 0, sizeof s);
    CHECK_I(yscr_text_input(&s, true, 0, 0, 1, 1), YSCR_ERR_CLOSED);
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 5));
    if (!yscr_is_open(&s)) return;
    CHECK_I(yscr_text_input(&s, true, 0, 0, -1, 1), YSCR_ERR_ARG);
    for (i = 0; i < 12; i++) {
        CHECK_I(yscr_begin(&s, &f), YSCR_OK);
        if (i == 4) CHECK_I(yscr_text_input(&s, true, 10, 20, 300, 40), YSCR_OK);
        if (i == 6) CHECK_I(yscr_text_input(&s, true, 50, 20, 300, 40), YSCR_OK);   /* moved: no record */
        if (i == 8) CHECK_I(yscr_text_input(&s, false, 0, 0, 0, 0), YSCR_OK);
        if (i == 9) CHECK_I(yscr_text_input(&s, false, 0, 0, 0, 0), YSCR_OK);     /* no change */
        CHECK_I(yscr_flip_at(&s, f.onset, &out), YSCR_OK);
        CHECK_I((out.flags & YSCR_FLIP_TEXT_INPUT) != 0, i >= 4 && i < 8);
    }
    yscr_close(&s);
    n = ring_drain();
    for (j = 0; j < n; j++) {
        const yrt_event* e = &g_ev[j];
        if (e->source != YRT_SRC_SCREEN) continue;
        if (e->kind == YSCR_EV_TEXT_INPUT) {
            CHECK_I(e->aux, 5);
            CHECK_I(e->u.u32[0], recs == 0 ? 1 : 0);
            CHECK_I(e->u.u32[5], 0);                 /* commanded, not observed */
            if (recs == 0) { CHECK_I(e->u.i32[1], 10); CHECK_I(e->u.i32[2], 20); CHECK_I(e->u.i32[3], 300); CHECK_I(e->u.i32[4], 40); }
            recs++;
        }
    }
    CHECK_I(recs, 2);
    /* v0.3.3: another library turns text input on (frame 3) and off
     * (frame 7) behind the header's back: the same records, external */
    ring_reset();
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = YSCR_PATH_OVERLAY;
    CHECK(open_scripted(&s, &c, 0, 5));
    if (!yscr_is_open(&s)) return;
    for (i = 0; i < 10; i++) {
        CHECK_I(yscr_begin(&s, &f), YSCR_OK);
        if (i == 3) g_ti = 1;
        if (i == 7) g_ti = 0;
        CHECK_I(yscr_flip_at(&s, f.onset, &out), YSCR_OK);
        CHECK_I((out.flags & YSCR_FLIP_TEXT_INPUT) != 0, i >= 3 && i < 7);
    }
    g_ti = -1;
    yscr_close(&s);
    n = ring_drain();
    recs = 0;
    for (j = 0; j < n; j++) {
        const yrt_event* e = &g_ev[j];
        if (e->source != YRT_SRC_SCREEN || e->kind != YSCR_EV_TEXT_INPUT) continue;
        CHECK_I(e->u.u32[0], recs == 0 ? 1 : 0);
        CHECK_I(e->u.u32[5], 1);
        recs++;
    }
    CHECK_I(recs, 2);
}

/* v0.3.4: one YSCR_EV_DEVICE record per change of an input device. */
static void test_devices(void) {
    static script c;
    yscr_screen s;
    int i, j, n, recs = 0, added = 0;
    char longname[40];
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = YSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 5));
    if (!yscr_is_open(&s)) return;
    memset(longname, 'k', sizeof longname);
    longname[30] = 0;
    yscr__device_log(&s, YSCR_DEV_KEYBOARD, YSCR_DEV_PRESENT, 0x1001, "Keyboard A");
    yscr__device_log(&s, YSCR_DEV_KEYBOARD, YSCR_DEV_ADDED, 0x1001, "Keyboard A");    /* SDL again */
    yscr__device_log(&s, YSCR_DEV_MOUSE, YSCR_DEV_PRESENT, 0x1001, "Mouse");           /* same id, other kind */
    yscr__device_log(&s, YSCR_DEV_KEYBOARD, YSCR_DEV_ADDED, 0x2002, longname);
    yscr__device_log(&s, YSCR_DEV_KEYBOARD, YSCR_DEV_REMOVED, 0x2002, NULL);
    yscr__device_log(&s, YSCR_DEV_KEYBOARD, YSCR_DEV_REMOVED, 0x2002, NULL);           /* gone already */
    yscr__device_log(&s, YSCR_DEV_TOUCH, YSCR_DEV_ADDED, 0x123456789ULL, NULL);
    for (i = 0; i < 40; i++)                                                                /* past the table */
        yscr__device_log(&s, YSCR_DEV_GAMEPAD, YSCR_DEV_ADDED, 100 + (uint64_t)i, "pad");
    yscr_close(&s);
    n = ring_drain();
    for (j = 0; j < n; j++) {
        const yrt_event* e = &g_ev[j];
        if (e->source != YRT_SRC_SCREEN || e->kind != YSCR_EV_DEVICE) continue;
        CHECK_I(e->aux, 5);
        if (recs == 0) {
            CHECK_I(e->u.u32[0], YSCR_DEV_KEYBOARD);
            CHECK_I(e->u.u32[1], YSCR_DEV_PRESENT);
            CHECK_I(e->u.u64[1], 0x1001);
            CHECK(!strcmp(YSCR_DEV_NAME_OF(e), "Keyboard A"));
        }
        if (recs == 1) CHECK_I(e->u.u32[0], YSCR_DEV_MOUSE);
        if (recs == 2) { CHECK_I(strlen(YSCR_DEV_NAME_OF(e)), 23); CHECK_I(e->u.u64[1], 0x2002); }
        if (recs == 3) { CHECK_I(e->u.u32[1], YSCR_DEV_REMOVED); CHECK_I(YSCR_DEV_NAME_OF(e)[0], 0); }
        if (recs == 4) { CHECK_I(e->u.u32[0], YSCR_DEV_TOUCH); CHECK(e->u.u64[1] == 0x123456789ULL); }
        if (e->u.u32[0] == YSCR_DEV_GAMEPAD) added++;
        recs++;
    }
    CHECK_I(recs, 5 + 40);
    CHECK_I(added, 40);
}

static int test_bell(int seq) {
    long k;
    if (g_bell_refuse) return 0;
#if defined(_WIN32)
    k = InterlockedIncrement(&g_nbells) - 1;
#else
    k = __atomic_add_fetch(&g_nbells, 1, __ATOMIC_SEQ_CST) - 1;
#endif
    if (k < 16384) g_bells[k] = seq;
    return 1;
}

static void bridge_reset(void) {
    uint32_t type = yscr__in.type;
    memset(&yscr__in, 0, sizeof yscr__in);
    yscr__in.type = type;
    g_nbells = 0;
    g_bell_refuse = 0;
}

static yin_event box_event(int control) {
    yin_event e;
    memset(&e, 0, sizeof e);
    e.t = 1000 + control;
    e.kind = YIN_KIND_BOX;
    e.type = YIN_PRESS;
    e.control = (uint32_t)control;
    return e;
}

#define BRIDGE_THREADS 4
#define BRIDGE_EACH 1000
#if defined(_WIN32)
static DWORD WINAPI bridge_producer(LPVOID arg) {
#else
static void* bridge_producer(void* arg) {
#endif
    int id = (int)(intptr_t)arg, i;
    for (i = 0; i < BRIDGE_EACH; i++) {
        yin_event e = box_event(i);
        e.device = (uint32_t)id;
        yscr_push_input(&e);
    }
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

/* v0.4.0: the input bridge on its seam (no SDL): store, doorbell, decode. */
static void test_bridge(void) {
    static script c;
    yscr_screen s;
    yin_event e, o;
    yscr_input_stats st;
    int i, j, n, seq_b, recs = 0, last[BRIDGE_THREADS];
    bridge_reset();
    yscr__in.type = 0;
    e = box_event(1);
    CHECK_I(yscr_push_input(&e), YSCR_ERR_CLOSED);     /* no SDL screen yet */
    CHECK_I(yscr_push_input(NULL), YSCR_ERR_ARG);
    yscr__in.type = 0x8001;                              /* as SDL_RegisterEvents gives */
    CHECK_I(yscr_input_event_type(), 0x8001);
    /* in order, each doorbell its own record, the producer's stamp kept */
    for (i = 0; i < 3; i++) { e = box_event(i); CHECK_I(yscr_push_input(&e), YSCR_OK); }
    CHECK_I(g_nbells, 3);
    for (i = 0; i < 3; i++) {
        CHECK(yscr__in_decode(NULL, g_bells[i], &o));
        CHECK_I(o.control, i);
        CHECK_I(o.t, 1000 + i);
    }
    CHECK(!yscr__in_decode(NULL, g_bells[1], &o));       /* decoded already: not lost */
    CHECK_I(yscr__in.lost, 0);
    /* a skipped doorbell keeps its record, and no later doorbell takes it */
    bridge_reset();
    for (i = 0; i < 3; i++) { e = box_event(10 + i); yscr_push_input(&e); }
    CHECK(yscr__in_decode(NULL, g_bells[2], &o));
    CHECK_I(o.control, 12);
    CHECK(yscr__in_decode(NULL, g_bells[0], &o));
    CHECK_I(o.control, 10);
    seq_b = g_bells[1];
    CHECK(yscr__in_decode(NULL, seq_b, &o));            /* late, still its own */
    CHECK_I(o.control, 11);
    /* never decoded: overwritten when the store wraps, then reported lost */
    bridge_reset();
    e = box_event(77);
    yscr_push_input(&e);
    seq_b = g_bells[0];
    for (i = 0; i < YSCR_INPUT_STORE; i++) { e = box_event(i); yscr_push_input(&e); }
    CHECK_I(yscr__in.overwritten, 1);
    CHECK(!yscr__in_decode(NULL, seq_b, &o));
    CHECK_I(yscr__in.lost, 1);
    CHECK(yscr__in_decode(NULL, g_bells[1 + YSCR_INPUT_STORE - 1], &o));
    CHECK_I(o.control, YSCR_INPUT_STORE - 1);
    /* SDL's queue full: the doorbell is refused, the record kept, and the
     * doorbell rung again later, oldest first */
    bridge_reset();
    g_bell_refuse = 1;
    for (i = 0; i < 3; i++) { e = box_event(20 + i); CHECK_I(yscr_push_input(&e), YSCR_OK); }
    CHECK_I(g_nbells, 0);
    CHECK_I(yscr__in.refused, 3);
    CHECK_I(yscr__in.pending, 3);
    CHECK_I(yscr__in_rebell(), 0);                       /* still full */
    g_bell_refuse = 0;
    CHECK_I(yscr__in_rebell(), 3);
    CHECK_I(yscr__in.pending, 0);
    for (i = 0; i < 3; i++) {
        CHECK(yscr__in_decode(NULL, g_bells[i], &o));
        CHECK_I(o.control, 20 + i);
    }
    CHECK_I(yscr__in_rebell(), 0);
    /* a refused doorbell whose record was decoded some other way: nothing to ring */
    g_bell_refuse = 1;
    e = box_event(30);
    yscr_push_input(&e);
    g_bell_refuse = 0;
    CHECK(yscr__in_take(3, &o, NULL) == 1);              /* sequence 3 */
    CHECK_I(yscr__in.pending, 0);
    CHECK_I(yscr__in_rebell(), 0);
    /* producer threads: every event once, each producer's in its order */
    bridge_reset();
    {
#if defined(_WIN32)
        HANDLE th[BRIDGE_THREADS];
        for (i = 0; i < BRIDGE_THREADS; i++) th[i] = CreateThread(NULL, 0, bridge_producer, (LPVOID)(intptr_t)i, 0, NULL);
        WaitForMultipleObjects(BRIDGE_THREADS, th, TRUE, INFINITE);
        for (i = 0; i < BRIDGE_THREADS; i++) CloseHandle(th[i]);
#else
        pthread_t th[BRIDGE_THREADS];
        for (i = 0; i < BRIDGE_THREADS; i++) pthread_create(&th[i], NULL, bridge_producer, (void*)(intptr_t)i);
        for (i = 0; i < BRIDGE_THREADS; i++) pthread_join(th[i], NULL);
#endif
    }
    CHECK_I(g_nbells, BRIDGE_THREADS * BRIDGE_EACH);
    for (i = 0; i < BRIDGE_THREADS; i++) last[i] = -1;
    n = 0;
    for (i = 0; i < (int)g_nbells; i++) {
        if (!yscr__in_decode(NULL, g_bells[i], &o)) continue;
        n++;
        if (o.device < BRIDGE_THREADS) {
            if ((int)o.control <= last[o.device]) { CHECK((int)o.control > last[o.device]); break; }
            last[o.device] = (int)o.control;
        }
    }
    CHECK_I(n, BRIDGE_THREADS * BRIDGE_EACH);
    for (i = 0; i < BRIDGE_THREADS; i++) CHECK_I(last[i], BRIDGE_EACH - 1);
    yscr_get_input_stats(&st);
    CHECK_I(st.stored, BRIDGE_THREADS * BRIDGE_EACH);
    CHECK_I(st.overwritten, 0);
    CHECK_I(st.lost, 0);
    /* the ring record at begin(): once per change */
    bridge_reset();
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = YSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 5));
    if (!yscr_is_open(&s)) return;
    g_bell_refuse = 1;
    e = box_event(1);
    yscr_push_input(&e);
    g_bell_refuse = 0;
    yscr__in_log(&s);
    yscr__in_log(&s);                                    /* no change: no record */
    yscr__in_rebell();
    yscr__in_log(&s);                                    /* only the waiting count fell */
    yscr_close(&s);
    n = ring_drain();
    for (j = 0; j < n; j++)
        if (g_ev[j].source == YRT_SRC_SCREEN && g_ev[j].kind == YSCR_EV_INPUT_LOST) {
            CHECK_I(g_ev[j].u.u32[1], 1);
            CHECK_I(g_ev[j].u.u32[3], 1);
            recs++;
        }
    CHECK_I(recs, 1);
    bridge_reset();
}

/* v0.3.5: the raw mouse reader's pure parts on synthetic RAWMOUSE data:
 * the decoder, the queue, the unlisted-device log, the guard, the refusal. */
static void test_raw_mice(void) {
    static script c;
    yscr_screen s;
    yscr_desc d;
    yscr_mouse_event e, out;
    int j, n, unlisted_recs = 0, rm_recs = 0;
    int32_t susp = 0;
    /* the decoder: winuser.h's RI_MOUSE_* bits */
    yscr__mouse_decode(0, 0x0001 | 0x0008, 0, 3, -4, 0x77, 1000, &e);
    CHECK_I(e.down, YSCR_MOUSE_LEFT);
    CHECK_I(e.up, YSCR_MOUSE_RIGHT);
    CHECK_I(e.dx, 3);
    CHECK_I(e.dy, -4);
    CHECK_I(e.device, 0x77);
    CHECK_I(e.t, 1000);
    CHECK_I(e.flags, 0);
    yscr__mouse_decode(0, 0x0010 | 0x0040 | 0x0200, 0, 0, 0, 1, 0, &e);
    CHECK_I(e.down, YSCR_MOUSE_MIDDLE | YSCR_MOUSE_X1);
    CHECK_I(e.up, YSCR_MOUSE_X2);
    yscr__mouse_decode(0, 0x0020 | 0x0080 | 0x0100 | 0x0002 | 0x0004, 0, 0, 0, 1, 0, &e);
    CHECK_I(e.up, YSCR_MOUSE_MIDDLE | YSCR_MOUSE_X1 | YSCR_MOUSE_LEFT);
    CHECK_I(e.down, YSCR_MOUSE_X2 | YSCR_MOUSE_RIGHT);
    yscr__mouse_decode(0, 0x0400, (uint16_t)0xFF88, 0, 0, 1, 0, &e);     /* one notch toward the user */
    CHECK_I(e.wheel, -120);
    CHECK_I(e.hwheel, 0);
    yscr__mouse_decode(0, 0x0800, 240, 0, 0, 1, 0, &e);
    CHECK_I(e.hwheel, 240);
    CHECK_I(e.wheel, 0);
    yscr__mouse_decode(0x0001 | 0x0002, 0, 0, 65535, 32768, 1, 0, &e);
    CHECK_I(e.flags, YSCR_MOUSE_ABSOLUTE | YSCR_MOUSE_VIRTUAL_DESKTOP);
    CHECK_I(e.dx, 65535);
    /* the reader's reports go onto the bridge (origin 1); the deprecated
     * yscr_poll_mouse() reads them back, one event of a report at a time */
    bridge_reset();
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = YSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 5));
    if (!yscr_is_open(&s)) return;
    yscr__device_log(&s, YSCR_DEV_MOUSE, YSCR_DEV_PRESENT, 0x77, "Mouse");
    {
        yin_event ev[12], o;
        int k, nn;
        yscr__mouse_decode(0, 0x0001, 0, 0, 0, 0x77, 10, &e);
        nn = yin_from_mouse(&e, ev, 12);
        for (k = 0; k < nn; k++) yscr__in_push(&ev[k], 1);
        memset(&o, 0, sizeof o);
        o.kind = YIN_KIND_BOX;                       /* another producer's */
        yscr__in_push(&o, 0);
        yscr__mouse_decode(0, 0, 0, 5, -2, 0x99, 11, &e);
        nn = yin_from_mouse(&e, ev, 12);
        for (k = 0; k < nn; k++) yscr__in_push(&ev[k], 1);
        yscr__in_push(&ev[0], 1);
        yscr__mouse_decode(0, 0x0400, 120, 0, 0, 0, 12, &e);         /* injected */
        nn = yin_from_mouse(&e, ev, 12);
        for (k = 0; k < nn; k++) yscr__in_push(&ev[k], 1);
    }
    CHECK(yscr_poll_mouse(&s, &out));
    CHECK_I(out.flags & YSCR_MOUSE_UNLISTED, 0);
    CHECK_I(out.down, YSCR_MOUSE_LEFT);
    CHECK_I(out.t, 10);
    CHECK(yscr_poll_mouse(&s, &out));
    CHECK_I(out.flags & YSCR_MOUSE_UNLISTED, YSCR_MOUSE_UNLISTED);
    CHECK_I(out.dx, 5);
    CHECK_I(out.dy, -2);
    CHECK(yscr_poll_mouse(&s, &out));
    CHECK_I(out.flags & YSCR_MOUSE_UNLISTED, YSCR_MOUSE_UNLISTED);
    CHECK(yscr_poll_mouse(&s, &out));
    CHECK_I(out.flags, 0);
    CHECK_I(out.wheel, 120);
    CHECK(!yscr_poll_mouse(&s, &out));
    {   /* their doorbells now decode as taken, not lost; the box event decodes */
        yin_event o;
        int k, taken = 0, decoded = 0;
        for (k = 0; k < (int)g_nbells; k++) {
            if (yscr__in_decode(&s, g_bells[k], &o)) { decoded++; CHECK_I(o.kind, YIN_KIND_BOX); }
            else taken++;
        }
        CHECK_I(decoded, 1);
        CHECK_I(taken, 4);
        CHECK_I(yscr__in.lost, 0);
    }
    /* the guard: relative mode suspends once; a lost registration asks again */
    CHECK_I(yscr__rm_guard(&susp, 0, 1), 0);
    CHECK_I(yscr__rm_guard(&susp, 1, 1), YSCR_RAW_MICE_SUSPENDED);
    CHECK_I(yscr__rm_guard(&susp, 1, 0), 0);
    CHECK_I(yscr__rm_guard(&susp, 0, 0), YSCR_RAW_MICE_REGISTERED);
    CHECK_I(susp, 0);
    CHECK_I(yscr__rm_guard(&susp, 0, 1), 0);
    yscr__rm_log(&s, YSCR_RAW_MICE_REGISTERED);
    yscr_close(&s);
    n = ring_drain();
    for (j = 0; j < n; j++) {
        const yrt_event* r = &g_ev[j];
        if (r->source != YRT_SRC_SCREEN) continue;
        if (r->kind == YSCR_EV_DEVICE && r->u.u64[1] == 0x99) {
            CHECK_I(r->u.u32[0], YSCR_DEV_MOUSE);
            CHECK_I(r->u.u32[1], YSCR_DEV_ADDED);
            CHECK(!strcmp(YSCR_DEV_NAME_OF(r), "raw, not in SDL's list"));
            unlisted_recs++;
        }
        if (r->kind == YSCR_EV_RAW_MICE) {
            CHECK_I(r->u.u32[0], YSCR_RAW_MICE_REGISTERED);
            rm_recs++;
        }
    }
    CHECK_I(unlisted_recs, 1);
    CHECK_I(rm_recs, 1);
    /* refused where there is no reader: the simulated display */
    memset(&s, 0, sizeof s);
    memset(&d, 0, sizeof d);
    d.backend = YSCR_BACKEND_SIM;
    d.raw_mice = true;
    CHECK(!yscr_open(&s, &d));
    CHECK(strstr(yscr_error(&s), "raw_mice") != NULL);
}

/* The snap: nearest vblank at lead 0.5, the next one at or after under
 * YSCR_LEAD_NONE; a hold to a later vblank lands on it, natively or by
 * the header's own wait. */
static void test_snap_and_hold(int native, double lead) {
    static script c;
    yscr_screen s;
    static yscr_frame f;   /* static: gcc -O3 cannot see begin() fill it */
    int i, checked = 0, j, n;
    int64_t want[100];
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.native = native;
    c.path = YSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_scripted(&s, &c, lead, 3));
    if (!yscr_is_open(&s)) return;
    for (i = 0; i < 100; i++) {
        int ahead = i % 4;
        /* offsets inside +-0.4 frame, and one 1 us after the vblank */
        int64_t u = (i % 3 == 0) ? 1000 : ((i % 3 == 1) ? -(int64_t)(0.4 * P_NS) : (int64_t)(0.4 * P_NS));
        int64_t t, expect;
        yscr_record out;
        CHECK_I(yscr_begin(&s, &f), YSCR_OK);
        t = f.onset + (int64_t)ahead * P_NS + u;
        expect = f.onset + (int64_t)ahead * P_NS;
        if (lead < 0 && u > 0) expect += P_NS;          /* never early */
        want[i] = expect;
        CHECK_I(yscr_flip_at(&s, t, &out), YSCR_OK);
        if (!(out.flags & YSCR_FLIP_LATE_TARGET) && expect >= f.onset) CHECK_I(out.planned, expect);
    }
    yscr_close(&s);
    n = ring_drain();
    for (j = 0; j < n; j++) {
        const yrt_event* e = &g_ev[j];
        uint32_t idx, flags;
        if (e->source != YRT_SRC_SCREEN || e->kind != YSCR_EV_FLIP) continue;
        idx = e->u.u32[9];
        flags = YSCR_EV_FLAGS_OF(e->u.u16[5]);
        if (idx >= 100 || (flags & YSCR_FLIP_LATE_TARGET)) continue;
        CHECK_I((int64_t)e->t_ns, want[idx]);
        CHECK(!(flags & YSCR_FLIP_EARLY));
        checked++;
    }
    CHECK(checked >= 60);
}

/* Phases, wait_flip, offsets and the patch value clamp. */
static void test_phases_and_wait(void) {
    static script c;
    yscr_screen s;
    yscr_desc d;
    static yscr_frame f;   /* static: gcc -O3 cannot see begin() fill it */
    yscr_record r;
    int64_t t0;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = YSCR_PATH_OVERLAY;
    memset(&s, 0, sizeof s);
    memset(&d, 0, sizeof d);
    d.backend = YSCR_BACKEND_CUSTOM;
    d.presenter = &g_scripted;
    d.presenter_ctx = &c;
    d.onset_offset_ns = 7000;
    CHECK(yscr_open(&s, &d));
    if (!yscr_is_open(&s)) return;
    CHECK_I(yscr_wait_flip(&s, &r), YSCR_ERR_ORDER);   /* nothing flipped yet */
    CHECK_I(yscr_begin(&s, &f), YSCR_OK);
    CHECK_I((f.onset - 7000 - c.t0) % P_NS, 0);
    t0 = now_ns();
    busy_until(t0 + 300000);
    yscr_mark(&s, YSCR_PHASE_EVALUATE);
    busy_until(now_ns() + 200000);
    yscr_mark(&s, YSCR_PHASE_UPLOAD);
    yscr_set_patch(&s, 2.0f);
    CHECK_I(yscr_flip(&s), YSCR_OK);
    CHECK_I(yscr_wait_flip(&s, &r), YSCR_OK);
    CHECK(!(r.flags & YSCR_FLIP_PENDING));
    CHECK(r.phase_ns[YSCR_PHASE_EVALUATE] >= 300000);
    CHECK(r.phase_ns[YSCR_PHASE_UPLOAD] >= 200000);
    CHECK(r.phase_ns[YSCR_PHASE_UPLOAD] < 2000000);
    CHECK_I(r.phase_ns[YSCR_PHASE_GPU], YSCR_PHASE_UNKNOWN);
    if (!(r.flags & YSCR_FLIP_LATE_TARGET)) {
        CHECK_I(r.onset, f.onset);
        CHECK_I((r.onset - 7000 - c.t0) % P_NS, 0);
        CHECK_I(r.residual, 0);
    }
    CHECK(s.patch_value == 1.0f);
    yscr_close(&s);
}

/* Flips that were never shown, flips with no time, planned times, tiers
 * and min_tier. */
static void test_unshown_and_tiers(void) {
    static script c;
    yscr_screen s;
    yscr_desc d;
    static yscr_frame f;   /* static: gcc -O3 cannot see begin() fill it */
    yscr_caps caps;
    int i, j, n, seen[6];
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = YSCR_PATH_COMPOSED;
    /* warmup used ids 1 to 6 or more; act on ids far enough in */
    memset(&s, 0, sizeof s);
    memset(&d, 0, sizeof d);
    d.backend = YSCR_BACKEND_CUSTOM;
    d.presenter = &g_scripted;
    d.presenter_ctx = &c;
    d.ring = &g_ring;
    d.min_tier = 1;
    ring_reset();
    CHECK(yscr_open(&s, &d));
    if (!yscr_is_open(&s)) return;
    {
        uint64_t base = s.next_id + 1;   /* the id of frame 0 */
        c.act[base + 10] = 1;
        c.act[base + 11] = 2;
        c.act[base + 12] = 3;
        c.act[base + 13] = 4;
    }
    for (i = 0; i < 30; i++) {
        CHECK_I(yscr_begin(&s, &f), YSCR_OK);
        CHECK_I(yscr_flip(&s), YSCR_OK);
    }
    yscr_get_caps(&s, &caps);
    CHECK_I(caps.worst_tier, YSCR_TIER_3);
    CHECK(!caps.raw_keyboard);              /* no window: no raw keyboard path */
    yscr_close(&s);
    memset(seen, 0, sizeof seen);
    n = ring_drain();
    for (j = 0; j < n; j++) {
        const yrt_event* e = &g_ev[j];
        uint32_t idx, flags, tier;
        if (e->source != YRT_SRC_SCREEN || e->kind != YSCR_EV_FLIP) continue;
        idx = e->u.u32[9];
        flags = YSCR_EV_FLAGS_OF(e->u.u16[5]);
        tier = YSCR_EV_TIER_OF(e->u.u16[5]);
        if (flags & YSCR_FLIP_LATE_TARGET) continue;
        /* never shown: the ring carries the planned vblank */
        if (idx == 10) { CHECK(flags & YSCR_FLIP_SKIPPED); CHECK_I((int64_t)e->t_ns, e->u.i64[0]); CHECK_I(e->u.u16[4], 0); seen[0]++; }
        else if (idx == 11) { CHECK(flags & YSCR_FLIP_CANCELED); CHECK_I((int64_t)e->t_ns, e->u.i64[0]); seen[1]++; }
        else if (idx == 12) {
            CHECK(flags & YSCR_FLIP_OCCLUDED); CHECK(flags & YSCR_FLIP_ESTIMATED);
            CHECK_I((int64_t)e->t_ns, e->u.i64[0]); CHECK_I(tier, YSCR_TIER_3); seen[2]++;
        } else if (idx == 13) {
            CHECK(flags & YSCR_FLIP_ONSET_PLANNED); CHECK_I(tier, YSCR_TIER_3);
            CHECK(flags & YSCR_FLIP_BELOW_TIER); seen[3]++;
        } else if (idx > 14) {
            /* composed with an OS time: tier 2, below min_tier 1 */
            CHECK_I(tier, YSCR_TIER_2);
            CHECK(flags & YSCR_FLIP_BELOW_TIER);
            CHECK_I(e->u.u16[4], 0);
            seen[4]++;
        }
    }
    CHECK_I(seen[0], 1); CHECK_I(seen[1], 1); CHECK_I(seen[2], 1); CHECK_I(seen[3], 1);
    CHECK(seen[4] >= 10);
    /* the simulated display has its own tier */
    memset(&s, 0, sizeof s);
    memset(&d, 0, sizeof d);
    d.backend = YSCR_BACKEND_SIM;
    d.sim_period_ns = P_NS;
    CHECK(yscr_open(&s, &d));
    for (i = 0; i < 3; i++) { CHECK_I(yscr_begin(&s, &f), YSCR_OK); CHECK_I(yscr_flip(&s), YSCR_OK); }
    {
        yscr_record r;
        CHECK_I(yscr_wait_flip(&s, &r), YSCR_OK);
        CHECK_I(r.tier, YSCR_TIER_SIM);
    }
    yscr_close(&s);
    d.min_tier = 4;
    CHECK(!yscr_open(&s, &d));
}

/* Two screens as a group. */
static void test_group(void) {
    static script c[2];
    static yscr_screen s[2];
    yscr_screen* sp[2];
    yscr_frame f[2];
    int i, j, n, per[2] = { 0, 0 };
    memset(c, 0, sizeof c);
    c[0].depth = 1; c[0].path = YSCR_PATH_OVERLAY;
    c[1].depth = 1; c[1].path = YSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_scripted(&s[0], &c[0], 0, 3));
    CHECK(open_scripted(&s[1], &c[1], 0, 4));
    sp[0] = &s[0]; sp[1] = &s[1];
    for (i = 0; i < 30; i++) {
        CHECK_I(yscr_begin_group(sp, 2, f), YSCR_OK);
        CHECK_I(yscr_flip_group_at(sp, 2, f[0].onset), YSCR_OK);
    }
    yscr_close(&s[0]);
    yscr_close(&s[1]);
    n = ring_drain();
    for (j = 0; j < n; j++)
        if (g_ev[j].source == YRT_SRC_SCREEN && g_ev[j].kind == YSCR_EV_FLIP) {
            if (g_ev[j].aux == 3) per[0]++;
            else if (g_ev[j].aux == 4) per[1]++;
        }
    CHECK_I(per[0], 30);
    CHECK_I(per[1], 30);
    CHECK_I(yscr_begin_group(NULL, 2, f), YSCR_ERR_ARG);
    CHECK_I(yscr_begin_group(sp, 2, f), YSCR_ERR_CLOSED);
}

static void test_errors(void) {
    static script c;
    yscr_screen s;
    yscr_desc d;
    static yscr_frame f;   /* static: gcc -O3 cannot see begin() fill it */
    yscr_presenter bad = g_scripted;
    memset(&s, 0, sizeof s);
    memset(&d, 0, sizeof d);
    CHECK(!yscr_open(&s, NULL));
    CHECK(!yscr_open(NULL, &d));
    CHECK(!yscr_open(&s, &d));                       /* AUTO: no display backend without SDL */
    CHECK(strlen(yscr_error(&s)) > 0);
    d.backend = YSCR_BACKEND_SIM;
    d.vrr = true;
    CHECK(!yscr_open(&s, &d));
    CHECK(strstr(yscr_error(&s), "sweep") != NULL);
    d.vrr = false;
    d.lead = 1.5;
    CHECK(!yscr_open(&s, &d));
    d.lead = 0;
    d.patch.corner = 4;
    CHECK(!yscr_open(&s, &d));
    d.patch.corner = 0;
    d.backend = YSCR_BACKEND_GLX_OML;
    CHECK(!yscr_open(&s, &d));
    d.backend = YSCR_BACKEND_CUSTOM;
    CHECK(!yscr_open(&s, &d));                       /* no presenter */
    bad.version = 99;
    d.presenter = &bad;
    CHECK(!yscr_open(&s, &d));
    CHECK_I(yscr_begin(&s, &f), YSCR_ERR_CLOSED);
    CHECK_I(yscr_flip(&s), YSCR_ERR_CLOSED);
    CHECK_I(yscr_begin(NULL, &f), YSCR_ERR_ARG);
    memset(&c, 0, sizeof c);
    c.depth = 1;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 3));
    CHECK(!yscr_open(&s, &d));                       /* already open */
    CHECK(yscr_is_open(&s));
    CHECK_I(yscr_flip(&s), YSCR_ERR_ORDER);        /* flip before begin */
    CHECK_I(yscr_begin(&s, &f), YSCR_OK);
    CHECK_I(yscr_begin(&s, &f), YSCR_ERR_ORDER);   /* begin twice */
    CHECK_I(yscr_wait_flip(&s, NULL), YSCR_ERR_ORDER);
    CHECK_I(yscr_flip(&s), YSCR_OK);
    CHECK(yscr_gl_proc(&s, "glClear") == NULL);
    CHECK(yscr_window(&s) == NULL);
    CHECK_I(yscr_restamp(&s, 123), 0);
    CHECK_I(yscr_displays(NULL, 0), YSCR_ERR_NOT_IMPLEMENTED);
    yscr_close(&s);
    yscr_close(&s);                                  /* twice is fine */
    CHECK(!yscr_is_open(&s));
    CHECK(strcmp(yscr_strerror(YSCR_ERR_REFUSED), "refused") == 0);
    CHECK(strcmp(yscr_version(), YSCR_VERSION_STRING) == 0);
}

static yscr_mode mk(int w, int h, int num, int den) {
    yscr_mode m;
    memset(&m, 0, sizeof m);
    m.w = w; m.h = h; m.refresh_num = num; m.refresh_den = den;
    m.period_ns = (int64_t)den * 1000000000 / num;
    return m;
}

static void test_mode_multiple(void) {
    yscr_mode list[6], out;
    double ppm = 0;
    int k;
    list[0] = mk(1920, 1080, 60, 1);
    list[1] = mk(1920, 1080, 60000, 1001);
    list[2] = mk(1920, 1080, 120, 1);
    list[3] = mk(1920, 1080, 144, 1);
    list[4] = mk(1280, 720, 240, 1);
    list[5] = mk(1920, 1080, 15413000, 256880);   /* this laptop's 60.0008 Hz */
    /* 59.94 film-rate video wants 59.94 Hz, not 60 */
    k = yscr_mode_multiple_in(list, 6, 60000, 1001, 1920, 1080, 0, &out, &ppm);
    CHECK_I(k, 1);
    CHECK_I(out.refresh_num, 60000);
    CHECK(ppm > -1 && ppm < 1);
    /* 24000/1001: 59.94 is 2.5x, not an integer; 120 and 144 fail; refused */
    k = yscr_mode_multiple_in(list, 6, 24000, 1001, 1920, 1080, 0, &out, &ppm);
    CHECK_I(k, YSCR_ERR_REFUSED);
    /* 24 Hz: 144 = 6 x 24 and 120 = 5 x 24; the highest wins */
    k = yscr_mode_multiple_in(list, 6, 24, 1, 1920, 1080, 0, &out, &ppm);
    CHECK_I(k, 6);
    CHECK_I(out.refresh_num, 144);
    /* 30 Hz at 1920 x 1080: 120 (x4); 240 is another size */
    k = yscr_mode_multiple_in(list, 6, 30, 1, 1920, 1080, 0, &out, &ppm);
    CHECK_I(k, 4);
    CHECK_I(out.refresh_num, 120);
    /* any size: 240 */
    k = yscr_mode_multiple_in(list, 6, 30, 1, 0, 0, 0, &out, &ppm);
    CHECK_I(k, 8);
    CHECK_I(out.w, 1280);
    /* 60.0008 Hz is 13 ppm from 60: inside 200 ppm, outside 5 */
    k = yscr_mode_multiple_in(list + 5, 1, 60, 1, 0, 0, 0, &out, &ppm);
    CHECK_I(k, 1);
    CHECK(ppm > 12.5 && ppm < 13.5);
    k = yscr_mode_multiple_in(list + 5, 1, 60, 1, 0, 0, 5, &out, &ppm);
    CHECK_I(k, YSCR_ERR_REFUSED);
    CHECK_I(out.refresh_num, 15413000);                /* the nearest, reported */
    CHECK_I(yscr_mode_multiple_in(list, 6, 0, 1, 0, 0, 0, &out, &ppm), YSCR_ERR_ARG);
    CHECK_I(yscr_mode_multiple_in(NULL, 6, 60, 1, 0, 0, 0, &out, &ppm), YSCR_ERR_ARG);
}

static void test_params(void) {
    int n = 0, i, j, gamepads = 0;
    const yscr_param* p = yscr_params(&n);
    CHECK(p != NULL);
    CHECK(n >= 15);
    for (i = 0; i < n; i++)   /* v0.3.3: off by default */
        if (!strcmp(p[i].name, "gamepads")) gamepads += p[i].def == 0 && !strcmp(p[i].type, "bool");
    CHECK_I(gamepads, 1);
    for (i = 0; i < n; i++) {
        CHECK(p[i].name && p[i].type && p[i].unit && p[i].doc);
        CHECK(p[i].min <= p[i].def && p[i].def <= p[i].max);
        for (j = 0; j < i; j++) CHECK(strcmp(p[i].name, p[j].name) != 0);
        CHECK(strcmp(p[i].name, "presenter") != 0 && strcmp(p[i].name, "ring") != 0 &&
              strcmp(p[i].name, "angle_dir") != 0);
    }
}

/* Smoke test: the simulated backend in real time, the one test here that
 * the host's scheduling can touch. Its bounds hold on a loaded runner:
 * every onset is on the simulated grid, and a flip that was neither late
 * nor dropped has the onset begin() predicted. */
static void test_sim(void) {
    yscr_screen s;
    yscr_desc d;
    static yscr_frame f;   /* static: gcc -O3 cannot see begin() fill it */
    char line[256];
    int i, late = 0, onset_ok = 0, completed = 0;
    int64_t prev = 0, grid0 = 0;
    g_virtual = 0;
    memset(&s, 0, sizeof s);
    memset(&d, 0, sizeof d);
    d.backend = YSCR_BACKEND_SIM;
    d.sim_period_ns = P_NS;
    CHECK(yscr_open(&s, &d));
    if (!yscr_is_open(&s)) { g_virtual = 1; return; }
    for (i = 0; i < 120; i++) {
        yscr_record out;
        CHECK_I(yscr_begin(&s, &f), YSCR_OK);
        CHECK_I(f.period, P_NS);
        if (!grid0) grid0 = f.onset;
        CHECK_I((f.onset - grid0) % P_NS, 0);
        if (i && f.last && f.last->index == i - 1) {
            completed++;
            CHECK_I(f.last->path, YSCR_PATH_SIMULATED);
            CHECK_I((f.last->onset - grid0) % P_NS, 0);
            CHECK(f.last->onset >= f.last->planned);   /* never early */
            if (!(f.last->flags & YSCR_FLIP_LATE_TARGET) && f.last->dropped == 0) {
                CHECK_I(f.last->onset, prev);
                onset_ok++;
            }
        }
        prev = f.onset;
        CHECK_I(yscr_flip_at(&s, f.onset, &out), YSCR_OK);
        if (out.flags & YSCR_FLIP_LATE_TARGET) late++;
    }
    CHECK(yscr_describe(&s, line, sizeof line) > 0);
    CHECK(strstr(line, "backend=sim") != NULL);
    yscr_close(&s);
    g_virtual = 1;
    CHECK(completed >= 1);
    if (late || onset_ok < completed) printf("  sim: %d of 120 frames late, %d of %d on the prediction\n", late, onset_ok, completed);
}

/* ------------------------------------------------------------ abort, panic */

/* One frame; the begin() result. */
static int one_frame(yscr_screen* s, yscr_frame* f) {
    int rc = yscr_begin(s, f);
    if (rc == YSCR_OK) CHECK_I(yscr_flip(s), YSCR_OK);
    return rc;
}

#define SDLK_F4_ 0x4000003Du
#define SDLK_F13_ 0x40000068u

/* The detector, the edge report and its ring record, the reasons. The
 * key feed is the one SDL's event watch and the Windows hook call. */
static void test_abort(void) {
    static script c;
    static yscr_screen s, s2;
    static yscr_frame f;
    yscr_desc d;
    int i, n, found = 0;
    int32_t held = 0;
    int64_t t;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = YSCR_PATH_OVERLAY;
    ring_reset();
    /* an abort from before open is not this screen's */
    yscr_request_abort();
    CHECK(open_scripted(&s, &c, 0, 5));
    if (!yscr_is_open(&s)) return;
    CHECK_I(one_frame(&s, &f), YSCR_OK);
    ring_drain();

    /* Shift+Esc, the default: once, then the loop goes on */
    t = vnow() - 1234;
    CHECK_I(yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, t, YSCR__AB_SDL), 0);
    f.index = 77;
    CHECK_I(yscr_begin(&s, &f), YSCR_QUIT);
    CHECK_I(f.abort, YSCR_ABORT_KEY);
    CHECK_I(f.abort_presses, 1);
    CHECK_I(f.abort_ns, t);
    CHECK_I(f.index, 0);                           /* the rest of f is 0 */
    CHECK(f.last == NULL);
    CHECK_I(yscr_flip(&s), YSCR_ERR_ORDER);    /* no frame begun */
    CHECK_I(one_frame(&s, &f), YSCR_OK);         /* reported once */
    n = ring_drain();
    for (i = 0; i < n; i++)
        if (g_ev[i].source == YRT_SRC_SCREEN && g_ev[i].kind == YSCR_EV_ABORT) {
            found++;
            CHECK_I((int64_t)g_ev[i].t_ns, t);      /* the press, not the report */
            CHECK_I(g_ev[i].aux, 5);
            CHECK_I(g_ev[i].u.u32[0], YSCR_ABORT_KEY);
            CHECK_I(g_ev[i].u.u32[1], YSCR__AB_SDL);
            CHECK_I(g_ev[i].u.u32[2], YSCR_KEY_ESCAPE);
            CHECK_I(g_ev[i].u.u32[3], YSCR_MOD_SHIFT);
            CHECK(g_ev[i].u.i64[2] > t);
        }
    CHECK_I(found, 1);

    /* Esc alone, another key: nothing; extra modifiers: still the combination */
    yscr__abort_key(YSCR_KEY_ESCAPE, 0, vnow(), YSCR__AB_SDL);
    yscr__abort_key('q', YSCR_MOD_SHIFT, vnow() + 100000000, YSCR__AB_SDL);
    yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_CTRL, vnow() + 200000000, YSCR__AB_SDL);
    CHECK_I(one_frame(&s, &f), YSCR_OK);
    vlose(400000000);
    yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT | YSCR_MOD_CTRL, vnow(), YSCR__AB_SDL);
    CHECK_I(one_frame(&s, &f), YSCR_QUIT);
    CHECK_I(f.abort_presses, 1);

    /* one press seen by the hook and by SDL (twice: its raw and its message
     * path) is one press; 200 ms later is two */
    vlose(400000000);
    t = vnow();
    yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, t, YSCR__AB_HOOK);
    yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, t + 400000, YSCR__AB_SDL);
    yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, t + 11000000, YSCR__AB_SDL);
    yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, t + 200000000, YSCR__AB_SDL);
    yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, t + 200000000 - 5000000, YSCR__AB_HOOK);
    CHECK_I(one_frame(&s, &f), YSCR_QUIT);
    CHECK_I(f.abort_presses, 2);
    CHECK_I(f.abort_ns, t);

    /* repeats: a key-down only after an up */
    CHECK_I(yscr__abort_edge(&held, 1), 1);
    CHECK_I(yscr__abort_edge(&held, 1), 0);
    CHECK_I(yscr__abort_edge(&held, 1), 0);
    CHECK_I(yscr__abort_edge(&held, 0), 0);
    CHECK_I(yscr__abort_edge(&held, 1), 1);

    /* every reason, in one report */
    vlose(400000000);
    yscr__abort_push(YSCR_ABORT_CLOSE, YSCR__AB_SDL, 0, 0, vnow());
    yscr__abort_push(YSCR_ABORT_ALT_F4, YSCR__AB_SDL, SDLK_F4_, YSCR_MOD_ALT, vnow());
    yscr__abort_push(YSCR_ABORT_QUIT, YSCR__AB_SDL, 0, 0, vnow());
    yscr_request_abort();
    CHECK_I(one_frame(&s, &f), YSCR_QUIT);
    CHECK_I(f.abort, YSCR_ABORT_CLOSE | YSCR_ABORT_ALT_F4 | YSCR_ABORT_QUIT | YSCR_ABORT_REQUEST);
    CHECK_I(f.abort_presses, 0);
    CHECK_I(one_frame(&s, &f), YSCR_OK);

    /* more than the log holds between two begin()s: the newest are reported */
    for (i = 0; i < YSCR__ABORT_LOG; i++) yscr_request_abort();
    for (i = 0; i < 4; i++) {
        vlose(100000000);
        yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, vnow(), YSCR__AB_SDL);
    }
    CHECK_I(one_frame(&s, &f), YSCR_QUIT);
    CHECK_I(f.abort, YSCR_ABORT_REQUEST | YSCR_ABORT_KEY);
    CHECK_I(f.abort_presses, 4);                   /* the lost 4 are not read twice */
    CHECK_I(one_frame(&s, &f), YSCR_OK);

    /* one combination per process */
    memset(&d, 0, sizeof d);
    d.backend = YSCR_BACKEND_SIM;
    d.abort_keys.mods = YSCR_MOD_CTRL;
    memset(&s2, 0, sizeof s2);
    CHECK(!yscr_open(&s2, &d));
    CHECK(strstr(yscr_error(&s2), "abort_keys") != NULL);
    d.abort_keys.mods = YSCR_MOD_SHIFT;          /* the same as the default */
    CHECK(yscr_open(&s2, &d));
    yscr_close(&s2);
    yscr_close(&s);

    /* the key alone, and off */
    memset(&d, 0, sizeof d);
    d.backend = YSCR_BACKEND_CUSTOM;
    d.presenter = &g_scripted;
    d.presenter_ctx = &c;
    d.abort_keys.mods = YSCR_MOD_NONE;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    memset(&s, 0, sizeof s);
    CHECK(yscr_open(&s, &d));
    yscr__abort_key(YSCR_KEY_ESCAPE, 0, vnow(), YSCR__AB_SDL);
    CHECK_I(one_frame(&s, &f), YSCR_QUIT);
    CHECK_I(f.abort, YSCR_ABORT_KEY);
    yscr_close(&s);
    d.abort_keys.mods = 0;
    d.abort_keys.off = true;
    d.abort_keys.key = SDLK_F4_ - 3;               /* F1 */
    memset(&c, 0, sizeof c);
    c.depth = 1;
    memset(&s, 0, sizeof s);
    CHECK(yscr_open(&s, &d));
    vlose(400000000);
    yscr__abort_key(SDLK_F4_ - 3, YSCR_MOD_SHIFT, vnow(), YSCR__AB_SDL);
    CHECK_I(one_frame(&s, &f), YSCR_OK);
    yscr_request_abort();                        /* requests still abort */
    CHECK_I(one_frame(&s, &f), YSCR_QUIT);
    yscr_close(&s);
    d.abort_keys.off = false;
    d.abort_keys.mods = 0x40;                      /* not a YSCR_MOD_* */
    memset(&s, 0, sizeof s);
    CHECK(!yscr_open(&s, &d));
}

/* An abort on a group: reported on every member, and none begins. */
static void test_abort_group(void) {
    static script c[2];
    static yscr_screen s[2];
    yscr_screen* sp[2];
    static yscr_frame f[2];
    int i;
    memset(c, 0, sizeof c);
    c[0].depth = 1; c[1].depth = 1;
    ring_reset();
    CHECK(open_scripted(&s[0], &c[0], 0, 1));
    CHECK(open_scripted(&s[1], &c[1], 0, 2));
    sp[0] = &s[0]; sp[1] = &s[1];
    for (i = 0; i < 5; i++) {
        CHECK_I(yscr_begin_group(sp, 2, f), YSCR_OK);
        CHECK_I(yscr_flip_group_at(sp, 2, f[0].onset), YSCR_OK);
    }
    yscr_request_abort();
    CHECK_I(yscr_begin_group(sp, 2, f), YSCR_QUIT);
    CHECK_I(f[0].abort, YSCR_ABORT_REQUEST);
    CHECK_I(f[1].abort, YSCR_ABORT_REQUEST);
    CHECK(!s[0].begun && !s[1].begun);
    CHECK_I(yscr_begin_group(sp, 2, f), YSCR_OK);
    CHECK_I(yscr_flip_group_at(sp, 2, f[0].onset), YSCR_OK);
    yscr_close(&s[0]);
    yscr_close(&s[1]);
}

/* The panic rule: 3 presses in 2 s, and no abort reported for 10 s. */
static void test_panic_rule(void) {
    static script c;
    static yscr_screen s;
    static yscr_frame f;
    yscr_desc d;
    int64_t t;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 1));
    yscr__panic_config(3, 2000, 10000);
    vlose(20000000000LL);                          /* any earlier report is old */
    /* a hung loop: nothing reports */
    t = vnow();
    CHECK_I(yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, t, YSCR__AB_HOOK), 0);
    CHECK_I(yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, t + 500000000, YSCR__AB_HOOK), 0);
    CHECK_I(yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, t + 1000000000, YSCR__AB_HOOK), 1);
    /* spread over 2.5 s: not three in the window */
    vlose(20000000000LL);
    t = vnow();
    CHECK_I(yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, t, YSCR__AB_HOOK), 0);
    CHECK_I(yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, t + 1250000000, YSCR__AB_HOOK), 0);
    CHECK_I(yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, t + 2500000000LL, YSCR__AB_HOOK), 0);
    /* a tool that injects by virtual key (remote desktop, assistive
     * software): each press reported twice, the second 34.2 ms later, the
     * longest gap measured (INPUT). Two presses, not four: no panic. */
    vlose(20000000000LL);
    t = vnow();
    CHECK_I(yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, t, YSCR__AB_HOOK), 0);
    CHECK_I(yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, t + 34200000, YSCR__AB_SDL), 0);
    CHECK_I(yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, t + 500000000, YSCR__AB_HOOK), 0);
    CHECK_I(yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, t + 534200000, YSCR__AB_SDL), 0);
    /* a live loop reports press 1: presses 2 and 3 are aborts, not a panic */
    vlose(20000000000LL);
    CHECK_I(one_frame(&s, &f), YSCR_QUIT);       /* the presses above */
    vlose(20000000000LL);
    t = vnow();
    CHECK_I(yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, t, YSCR__AB_HOOK), 0);
    CHECK_I(one_frame(&s, &f), YSCR_QUIT);
    vlose(400000000);
    CHECK_I(yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, vnow(), YSCR__AB_HOOK), 0);
    vlose(400000000);
    CHECK_I(yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, vnow(), YSCR__AB_HOOK), 0);
    CHECK_I(one_frame(&s, &f), YSCR_QUIT);
    CHECK_I(f.abort_presses, 2);
    /* the loop reported, then hung for longer than the grace */
    vlose(10500000000LL);
    t = vnow();
    CHECK_I(yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, t, YSCR__AB_HOOK), 0);
    CHECK_I(yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, t + 300000000, YSCR__AB_HOOK), 0);
    CHECK_I(yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, t + 600000000, YSCR__AB_HOOK), 1);
    /* off without an armed watchdog */
    yscr__panic_config(0, 0, 0);
    vlose(20000000000LL);
    t = vnow();
    yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, t, YSCR__AB_HOOK);
    yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, t + 300000000, YSCR__AB_HOOK);
    CHECK_I(yscr__abort_key(YSCR_KEY_ESCAPE, YSCR_MOD_SHIFT, t + 600000000, YSCR__AB_HOOK), 0);
    yscr_close(&s);
    /* refusals at open */
    memset(&d, 0, sizeof d);
    d.backend = YSCR_BACKEND_SIM;
    d.panic = true;
    d.abort_keys.off = true;
    memset(&s, 0, sizeof s);
    CHECK(!yscr_open(&s, &d));
    d.abort_keys.off = false;
    d.abort_keys.key = SDLK_F13_;                  /* the hook cannot name it */
    CHECK(!yscr_open(&s, &d));
    d.abort_keys.key = 0;
    d.panic_presses = -1;
    CHECK(!yscr_open(&s, &d));
    d.panic_presses = 0;
    CHECK(yscr_open(&s, &d));                    /* SIM: nothing to arm */
    {
        char line[512];
        yscr_describe(&s, line, sizeof line);
        CHECK(strstr(line, "abort=shift+esc panic=n/a") != NULL);
    }
    yscr_close(&s);
}

/* The icon: each size fills its square, matches the art (FNV-1a of the
 * RGBA, from the previews' script) and has transparent corners. */
static void test_icon(void) {
    static uint8_t px[48 * 48 * 4];
    static const uint32_t fnv[3] = { 0xF62F93D2u, 0xC53A724Fu, 0x4DECB55Bu };
    yscr_screen s;
    yscr_desc d;
    static const uint8_t one[4] = { 1, 2, 3, 255 };
    int k, i;
    for (k = 0; k < 3; k++) {
        int side = yscr__icon_size[k][0];
        uint32_t h = 0x811C9DC5u;
        memset(px, 0xAB, sizeof px);
        CHECK(yscr__icon_decode(k, px, side * 4));
        for (i = 0; i < side * side * 4; i++) { h ^= px[i]; h *= 0x01000193u; }
        CHECK_I(h, fnv[k]);
        CHECK_I(px[3], 0);
        CHECK_I(px[(side * side - 1) * 4 + 3], 0);
    }
    memset(&s, 0, sizeof s);
    memset(&d, 0, sizeof d);
    d.backend = YSCR_BACKEND_SIM;
    d.icon_rgba = one;
    CHECK(!yscr_open(&s, &d));                   /* no size */
    d.icon_w = 1; d.icon_h = 257;
    CHECK(!yscr_open(&s, &d));
    d.icon_h = 1;
    CHECK(yscr_open(&s, &d));                    /* no window: not used */
    yscr_close(&s);
}

int main(void) {
    yrt_thread_elevate(NULL);
    test_errors();
    test_mode_multiple();
    test_params();
    test_prediction_and_ring();
    test_drop_and_late();
    test_depth();
    test_native_depth(0);
    test_native_depth(1);
    test_native_depth(2);
    test_one_miss_and_early();
    test_flap_prediction(0);
    test_flap_prediction(1);
    test_done_and_native();
    test_preemption(0);
    test_preemption(1);
    test_triggers();
    test_trigger_fence(0);
    test_trigger_fence(1);
    test_trigger_channels();
    test_trigger_race();
    test_trigger_spec();
    test_codes();
    test_present_hook();
    test_missing();
    test_text_input();
    test_devices();
    test_bridge();
    test_raw_mice();
    test_snap_and_hold(1, 0);
    test_snap_and_hold(0, 0);
    test_snap_and_hold(1, YSCR_LEAD_NONE);
    test_snap_and_hold(0, YSCR_LEAD_NONE);
    test_phases_and_wait();
    test_group();
    test_unshown_and_tiers();
    test_abort();
    test_abort_group();
    test_panic_rule();
    test_icon();
    test_sim();
    if (g_failures) {
        fprintf(stderr, "screen_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("screen_test: all checks passed\n");
    return 0;
}
