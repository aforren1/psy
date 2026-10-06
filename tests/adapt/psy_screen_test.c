/* psy_screen_test.c - self-checking test for psy_screen.h's core: prediction,
 * the snap rule, drops versus late targets, depth learning, estimated
 * records, the ring record, phases, wait_flip, the group calls, the errors,
 * the mode picker and the parameter table. No framework: it returns 0 when
 * every check passed and 1 after printing each failure.
 *
 * It needs no SDL, no display and no GPU: PSYSCR_NO_SDL builds the core
 * alone, and a scripted presenter (the Presenter extension interface) plays
 * a display with the depth, drops, path changes and missing statistics each
 * case asks for.
 *
 * The core and the script run on a virtual clock that only the test moves:
 * the header's PSYSCR__NOW() and PSYSCR__SLEEP_UNTIL() seam points at it.
 * So the host's scheduling cannot change a result, and preemption is a
 * scripted input (the thread loses N ns at a chosen point), not an
 * accident of a busy CI runner. One smoke test runs the simulated backend
 * in real time, with bounds a loaded runner meets.
 *
 *     gcc -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Werror -O2 -I. \
 *         -o screen_test tests/adapt/psy_screen_test.c -lm -pthread && ./screen_test
 *     cl /nologo /W4 /WX /I. tests\adapt\psy_screen_test.c
 */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
/* getenv() is C4996 under /W4 /WX, and _dupenv_s() is not portable. */
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef PSYSCR_NO_SDL
#define PSYSCR_NO_SDL
#endif
/* The virtual clock. Starts far from 0, so no grid arithmetic meets it.
 * long long, not int64_t: no system header may come before psy_rt.h,
 * which sets the feature macros glibc reads once. */
static int g_virtual = 1;
static long long g_vt = 1000000000000LL;
static long long vnow(void);
static void vsleep_until(long long t);
#define PSYSCR__NOW() ((int64_t)vnow())
#define PSYSCR__SLEEP_UNTIL(t, spin) vsleep_until((long long)(t))
/* The trigger worker's wake-up: recorded here, and run by the clock as it
 * passes, through the same function the worker thread calls. */
struct psyscr_screen;
static void varm(struct psyscr_screen* s, long long t);
#define PSYSCR__ARM(s, t)       varm((s), (long long)(t))
#define PSYSCR__WORKER_START(s) (1)
#define PSYSCR__WORKER_STOP(s)  ((void)(s))

#define PSY_SCREEN_IMPLEMENTATION
#include "psy_screen.h"

static long long vnow(void) { return g_virtual ? g_vt : (long long)psyrt_now_ns(); }

static struct { psyscr_screen* s; long long t; } g_armed[4];
/* Wake-ups armed from the frame thread, not from the worker's own run:
 * each is a submit, the bulk of what arming a trigger costs. */
static int g_in_worker, g_frame_arms;
static void varm(struct psyscr_screen* s, long long t) {
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
            psyscr_screen* s = g_armed[best].s;
            long long t = g_armed[best].t;
            g_armed[best].s = NULL;
            if (t > g_vt) g_vt = t;
            g_in_worker = 1;
            psyscr__trig_run(s, (int64_t)g_vt, 0);
            g_in_worker = 0;
        }
    }
}
static void vforget(psyscr_screen* s) {
    int i;
    for (i = 0; i < 4; i++) if (g_armed[i].s == s) g_armed[i].s = NULL;
}
static void vsleep_until(long long t) {
    if (!g_virtual) { psyrt_sleep_until((uint64_t)t, PSYRT_DEFAULT_SPIN_NS); return; }
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
    fprintf(stderr, "psy_screen_test: FAIL at line %d: %s\n", line, what);
    g_failures++;
}
static void fail_i(int line, const char* what, long long got, long long want) {
    fprintf(stderr, "psy_screen_test: FAIL at line %d: %s (got %lld, want %lld)\n", line, what, got, want);
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

static int sc_open(void* ctx, const psyscr_presenter_open* in, psyscr_caps* caps, char* err, size_t cap) {
    script* c = (script*)ctx;
    (void)in; (void)err; (void)cap;
    c->t0 = now_ns();
    c->last_shown = -1;
    caps->kind = PSYSCR_FIXED_GRID;
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
    return PSYSCR_OK;
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
static int sc_acquire(void* ctx, int64_t deadline_ns, psyscr_vblank* newest) {
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
    return PSYSCR_OK;
}

static int sc_present(void* ctx, const psyscr_present_req* req) {
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
    return PSYSCR_OK;
}

static int sc_completions(void* ctx, psyscr_vblank* out, int cap) {
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
        case 1: out[n].flags = PSYSCR_FLIP_SKIPPED; out[n].t_ns = 0; break;
        case 2: out[n].flags = PSYSCR_FLIP_CANCELED; out[n].t_ns = 0; break;
        case 3: out[n].flags = PSYSCR_FLIP_OCCLUDED; out[n].t_ns = 0; break;
        case 4: out[n].flags = PSYSCR_FLIP_ONSET_PLANNED; break;
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

static const psyscr_presenter g_scripted = {
    PSYSCR_PRESENTER_VERSION, "scripted", false, false, false,
    sc_open, sc_close, sc_acquire, sc_present, sc_completions, NULL, NULL, NULL, sc_gpu_done
};
/* The same display, drawing the patch and the codes itself. */
static const psyscr_presenter g_scripted_codes = {
    PSYSCR_PRESENTER_VERSION, "scripted", false, false, true,
    sc_open, sc_close, sc_acquire, sc_present, sc_completions, NULL, NULL, NULL, sc_gpu_done
};

/* ------------------------------------------------------------------ ring */

static unsigned char g_ring_mem[PSYRT_RING_BYTES(4096)];
static psyrt_ring g_ring;
static psyrt_event g_ev[4096];

static void ring_reset(void) {
    psyrt_ring_desc d;
    d.memory = g_ring_mem;
    d.bytes = sizeof g_ring_mem;
    if (!psyrt_ring_open(&g_ring, &d)) fail(__LINE__, "ring open");
}

static int ring_drain(void) {
    int n = psyrt_ring_drain(&g_ring, g_ev, 4096);
    return n < 0 ? 0 : n;
}

static bool open_scripted(psyscr_screen* s, script* c, double lead, uint32_t index) {
    psyscr_desc d;
    memset(s, 0, sizeof *s);
    memset(&d, 0, sizeof d);
    d.backend = PSYSCR_BACKEND_CUSTOM;
    d.presenter = &g_scripted;
    d.presenter_ctx = c;
    d.ring = &g_ring;
    d.display_index = index;
    d.lead = lead;
    return psyscr_open(s, &d);
}

static void busy_until(int64_t t) {
    if (g_virtual) { if (t > g_vt) g_vt = t; return; }
    while (now_ns() < t) { }
}

/* ------------------------------------------------------------- the cases */

/* Prediction is exact on a clean grid, and the ring carries the record. */
static void test_prediction_and_ring(void) {
    static script c;
    psyscr_screen s;
    static psyscr_frame f;   /* static: gcc -O3 cannot see begin() fill it */
    int i, checked = 0, late = 0, n, flips = 0;
    int64_t pred[200];
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = PSYSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 3));
    if (!psyscr_is_open(&s)) return;
    ring_drain();
    for (i = 0; i < 200; i++) {
        psyscr_record out;
        CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
        CHECK_I(f.index, i);
        CHECK_I(f.period, P_NS);
        CHECK_I((f.onset - c.t0) % P_NS, 0);
        /* the first vblank a present now can make, not a later one */
        CHECK(f.onset - now_ns() <= (int64_t)c.depth * P_NS);
        pred[i] = f.onset;
        if (i > 1) CHECK(f.last != NULL);
        psyscr_mark(&s, PSYSCR_PHASE_EVALUATE);
        CHECK_I(psyscr_flip_at(&s, f.onset, &out), PSYSCR_OK);
        CHECK_I(out.index, i);
        CHECK_I(out.target, f.onset);
        CHECK(out.flags & PSYSCR_FLIP_PENDING);
        if (!(out.flags & PSYSCR_FLIP_LATE_TARGET)) CHECK_I(out.planned, f.onset);
    }
    psyscr_close(&s);
    CHECK_I(c.closed, 1);
    n = ring_drain();
    for (i = 0; i < n; i++) {
        const psyrt_event* e = &g_ev[i];
        uint32_t idx, flags;
        if (e->source != PSYRT_SRC_SCREEN || e->kind != PSYSCR_EV_FLIP) continue;
        flips++;
        CHECK_I(e->aux, 3);
        idx = e->u.u32[9];
        flags = PSYSCR_EV_FLAGS_OF(e->u.u16[5]);
        CHECK_I(PSYSCR_EV_PATH_OF(e->u.u16[5]), PSYSCR_PATH_OVERLAY);
        CHECK_I(PSYSCR_EV_TIER_OF(e->u.u16[5]), (flags & PSYSCR_FLIP_ESTIMATED) ? PSYSCR_TIER_3 : PSYSCR_TIER_1);
        if (idx >= 200) { fail(__LINE__, "record index out of range"); continue; }
        CHECK_I(e->u.i64[0], pred[idx]);
        CHECK(!(flags & PSYSCR_FLIP_PENDING));
        if (flags & PSYSCR_FLIP_LATE_TARGET) { late++; continue; }
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
    psyscr_screen s;
    static psyscr_frame f;   /* static: gcc -O3 cannot see begin() fill it */
    psyscr_record rec[60];
    int i, j, n;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = PSYSCR_PATH_INDEPENDENT;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 3));
    if (!psyscr_is_open(&s)) return;
    /* warmup used ids 1..6; frame k has id 7 + k */
    c.drop[7 + 20] = 1;
    for (i = 0; i < 60; i++) {
        CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
        if (i == 40) busy_until(f.onset + P_NS / 2);   /* past its vblank */
        CHECK_I(psyscr_flip(&s), PSYSCR_OK);
    }
    psyscr_close(&s);
    memset(rec, 0, sizeof rec);
    n = ring_drain();
    for (j = 0; j < n; j++) {
        const psyrt_event* e = &g_ev[j];
        uint32_t idx;
        if (e->source != PSYRT_SRC_SCREEN || e->kind != PSYSCR_EV_FLIP) continue;
        idx = e->u.u32[9];
        if (idx < 60) {
            rec[idx].dropped = e->u.u16[4];
            rec[idx].flags = (uint16_t)PSYSCR_EV_FLAGS_OF(e->u.u16[5]);
            rec[idx].onset = (int64_t)e->t_ns;
            rec[idx].target = e->u.i64[0];
        }
    }
    if (!(rec[20].flags & PSYSCR_FLIP_LATE_TARGET)) {
        CHECK_I(rec[20].dropped, 1);
        CHECK_I(rec[20].onset - rec[20].target, P_NS);
    }
    CHECK(rec[40].flags & PSYSCR_FLIP_LATE_TARGET);
    CHECK_I(rec[40].dropped, 0);
    CHECK_I(rec[40].onset - rec[40].target, P_NS);
    /* One drop is not a new depth: the frames after it are on time, not
     * early, and the late frame is not either. */
    for (i = 21; i < 60; i++) {
        if (i == 40 || (rec[i].flags & PSYSCR_FLIP_LATE_TARGET)) continue;
        CHECK_I(rec[i].dropped, 0);
        CHECK(!(rec[i].flags & PSYSCR_FLIP_EARLY));
        CHECK_I(rec[i].onset, rec[i].target);
    }
}

/* A path change brings a new depth, adopted at once; a depth change on the
 * same path takes three votes. */
static void test_depth(void) {
    static script c;
    psyscr_screen s;
    static psyscr_frame f;   /* static: gcc -O3 cannot see begin() fill it */
    int i, j, n, path_events = 0, after_ok = 0;
    uint32_t dropped[80];
    uint16_t flags[80];
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = PSYSCR_PATH_OVERLAY;
    c.change_id = 7 + 30;
    c.depth2 = 2;
    c.path2 = PSYSCR_PATH_COMPOSED;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 3));
    if (!psyscr_is_open(&s)) return;
    for (i = 0; i < 80; i++) {
        CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
        CHECK_I(psyscr_flip(&s), PSYSCR_OK);
    }
    psyscr_close(&s);
    memset(dropped, 0, sizeof dropped);
    memset(flags, 0, sizeof flags);
    n = ring_drain();
    for (j = 0; j < n; j++) {
        const psyrt_event* e = &g_ev[j];
        if (e->source == PSYRT_SRC_SCREEN && e->kind == PSYSCR_EV_PATH && e->u.u16[1] == PSYSCR_PATH_COMPOSED) path_events++;
        if (e->source == PSYRT_SRC_SCREEN && e->kind == PSYSCR_EV_FLIP && e->u.u32[9] < 80) {
            dropped[e->u.u32[9]] = e->u.u16[4];
            flags[e->u.u32[9]] = (uint16_t)PSYSCR_EV_FLAGS_OF(e->u.u16[5]);
        }
    }
    CHECK_I(path_events, 1);
    if (getenv("PSYSCR_TEST_TRACE"))
        for (i = 26; i < 42; i++) printf("  depth case: frame %d dropped %u flags 0x%x\n", i, dropped[i], flags[i]);
    /* Frames 30 and 31 were predicted at depth 1 and drop. The change is
     * known from frame 30's statistic, and a new path is adopted on one
     * vote, so from frame 32 on all are on time. */
    if (!(flags[30] & PSYSCR_FLIP_LATE_TARGET)) CHECK_I(dropped[30], 1);
    for (i = 32; i < 80; i++)
        if (!(flags[i] & PSYSCR_FLIP_LATE_TARGET)) { CHECK_I(dropped[i], 0); after_ok++; }
    CHECK(after_ok >= 36);
    for (i = 5; i < 30; i++)
        if (!(flags[i] & PSYSCR_FLIP_LATE_TARGET)) CHECK_I(dropped[i], 0);
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
    psyscr_screen s;
    static psyscr_frame f;   /* static: gcc -O3 cannot see begin() fill it */
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
    c.path = PSYSCR_PATH_INDEPENDENT;
    if (case_ == 0) {
        c.drop[7 + K] = c.drop[7 + K + 1] = c.drop[7 + K + 2] = 1;
    } else {
        c.change_id = 7 + K;
        c.depth2 = 2;
        c.path2 = PSYSCR_PATH_INDEPENDENT;   /* same path: no path event */
        c.free_at_flip = case_ == 1;
    }
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 3));
    if (!psyscr_is_open(&s)) return;
    for (i = 0; i < N; i++) {
        CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
        /* never a vblank a flip in flight already has */
        if (i && f.vblank <= prev_vb) behind++;
        prev_vb = f.vblank;
        rng = rng * 1664525u + 1013904223u;
        busy_until(now_ns() + (int64_t)((rng >> 8) % (P_NS / 4)));
        CHECK_I(psyscr_flip(&s), PSYSCR_OK);
    }
    psyscr_close(&s);
    CHECK_I(behind, 0);
    memset(dropped, 0, sizeof dropped);
    memset(flags, 0, sizeof flags);
    memset(onset, 0, sizeof onset);
    n = ring_drain();
    for (j = 0; j < n; j++) {
        const psyrt_event* e = &g_ev[j];
        if (e->source == PSYRT_SRC_SCREEN && e->kind == PSYSCR_EV_FLIP && e->u.u32[9] < N) {
            dropped[e->u.u32[9]] = e->u.u16[4];
            flags[e->u.u32[9]] = (uint16_t)PSYSCR_EV_FLAGS_OF(e->u.u16[5]);
            onset[e->u.u32[9]] = (int64_t)e->t_ns;
        }
    }
    /* frames before K are on time at depth 1; from K, case 1 and 2 miss
     * until three misses raise the depth */
    for (i = K + 3; i < N; i++) {
        if (flags[i] & PSYSCR_FLIP_LATE_TARGET) { late++; continue; }
        if (dropped[i]) { drops_after++; if (i >= N - 150) drops_end++; }
        /* a gap that ends on a LATE_TARGET frame is the caller's lateness */
        if (i + 1 < N && !(flags[i + 1] & PSYSCR_FLIP_LATE_TARGET) && onset[i + 1] - onset[i] == 2 * P_NS) half++;
    }
    for (i = N - 40; i < N - 1; i++) {
        int64_t want = case_ == 1 ? 2 * P_NS : P_NS;   /* one in flight, slot at the flip */
        if (!(flags[i + 1] & PSYSCR_FLIP_LATE_TARGET) && onset[i + 1] - onset[i] == want) steady++;
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
    if (getenv("PSYSCR_TEST_TRACE"))
        printf("  native depth %d: drops after the misses %d, half-rate gaps %d\n", case_, drops_after, half);
}

/* A single miss on a backend that holds frames to their target is a drop,
 * not a new depth: three in a row are needed. And a flip the display shows
 * before its planned vblank (a DXGI-like path whose depth fell from 2 to
 * 1) carries EARLY, and only such a flip does. */
static void test_one_miss_and_early(void) {
    static script c;
    psyscr_screen s;
    static psyscr_frame f;
    int i, j, n, early = 0, wrong = 0, depth_moved = 0;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.native = 1;
    c.path = PSYSCR_PATH_INDEPENDENT;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 0));
    if (psyscr_is_open(&s)) {
        for (i = 0; i < 60; i++) {
            CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
            if (i == 20) c.drop[s.next_id + 1] = 1;
            CHECK_I(psyscr_flip(&s), PSYSCR_OK);
            if (i > 20 && s.depth != 1) depth_moved++;
        }
        psyscr_close(&s);
    }
    CHECK_I(depth_moved, 0);

    memset(&c, 0, sizeof c);
    c.depth = 2;
    c.path = PSYSCR_PATH_COMPOSED;
    c.change_id = 40;
    c.depth2 = 1;
    c.path2 = PSYSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 0));
    if (!psyscr_is_open(&s)) return;
    for (i = 0; i < 60; i++) {
        CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
        CHECK_I(psyscr_flip(&s), PSYSCR_OK);
    }
    psyscr_close(&s);
    n = ring_drain();
    for (j = 0; j < n; j++) {
        const psyrt_event* e = &g_ev[j];
        int is_early, flagged;
        if (e->source != PSYRT_SRC_SCREEN || e->kind != PSYSCR_EV_FLIP) continue;
        is_early = (int64_t)e->t_ns < e->u.i64[0];
        flagged = (PSYSCR_EV_FLAGS_OF(e->u.u16[5]) & PSYSCR_FLIP_EARLY) != 0;
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
    psyscr_screen s;
    static psyscr_frame f;
    enum { N = 300 };
    static int64_t vb[N], pred[N], shown_t[N];
    int i, j, n, same = 0, back = 0, run = 0, max_run = 0, early = 0, unused = 0, unexplained = 0;
    uint32_t rng = 7u;
    memset(&c, 0, sizeof c);
    c.native = native;
    c.depth = 2;
    c.path = PSYSCR_PATH_COMPOSED;
    c.change_id = 30;
    c.flap = 9;
    c.depth2 = 1;
    c.path2 = PSYSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 0));
    if (!psyscr_is_open(&s)) return;
    for (i = 0; i < N; i++) {
        CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
        vb[i] = f.vblank;
        pred[i] = f.onset;
        if (i && vb[i] == vb[i - 1]) same++;
        if (i && vb[i] < vb[i - 1]) back++;
        rng = rng * 1664525u + 1013904223u;
        busy_until(now_ns() + (int64_t)((rng >> 8) % (P_NS / 3)));
        CHECK_I(psyscr_flip(&s), PSYSCR_OK);
    }
    psyscr_close(&s);
    memset(shown_t, 0, sizeof shown_t);
    n = ring_drain();
    for (j = 0; j < n; j++) {
        const psyrt_event* e = &g_ev[j];
        if (e->source == PSYRT_SRC_SCREEN && e->kind == PSYSCR_EV_FLIP && e->u.u32[9] < N &&
            !(PSYSCR_EV_FLAGS_OF(e->u.u16[5]) & (PSYSCR_FLIP_SKIPPED | PSYSCR_FLIP_CANCELED)))
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
    if (getenv("PSYSCR_TEST_TRACE"))
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
    c.path = PSYSCR_PATH_OVERLAY;
    CHECK(open_scripted(&s, &c, 0, 0));
    if (!psyscr_is_open(&s)) return;
    {
        psyscr_record r;
        CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
        s.last_planned = f.vblank;   /* as if the frame before had it */
        CHECK_I(psyscr_flip_at(&s, f.onset, &r), PSYSCR_OK);
        CHECK(r.planned > f.onset + f.period / 2);
        CHECK(r.flags & PSYSCR_FLIP_LATE_TARGET);
    }
    psyscr_close(&s);
}

/* f.done carries every record completed since the begin() before, in
 * the ring's order, including those completed inside flip_at() while it
 * held a frame and those with no statistic (ESTIMATED); f.last is its
 * final one. And psyscr_native() has nothing to give off Windows' swap
 * paths. */
static void test_done_and_native(void) {
    static script c;
    psyscr_screen s;
    static psyscr_frame f;
    enum { N = 120 };
    static int64_t got[4 * N];
    psyscr_native_info nat;
    int i, j, n, n_got = 0, n_ring = 0, order_ok = 1, held = 0;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = PSYSCR_PATH_OVERLAY;
    for (i = 0; i < MAX_ID; i += 17) c.missing[i] = 1;
    for (i = 5; i < MAX_ID; i += 23) c.drop[i] = 1;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 0));
    if (!psyscr_is_open(&s)) return;
    CHECK_I(psyscr_native(&s, &nat), PSYSCR_ERR_NOT_IMPLEMENTED);
    CHECK(nat.d3d11_device == NULL && nat.d3d11_context == NULL && nat.egl_display == NULL);
    CHECK(nat.luid_low == 0 && nat.luid_high == 0);
    CHECK_I(psyscr_native(&s, NULL), PSYSCR_ERR_ARG);
    for (i = 0; i < N; i++) {
        int k;
        CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
        CHECK(f.n_done >= 0 && f.n_done <= PSYSCR_MAX_DONE);
        if (f.n_done > 0) {
            CHECK(f.last != NULL);
            if (f.last) CHECK_I(f.done[f.n_done - 1].index, f.last->index);
        }
        if (f.n_done > 1) held++;
        for (k = 0; k < f.n_done && n_got < 4 * N; k++) {
            CHECK(!(f.done[k].flags & PSYSCR_FLIP_PENDING));
            got[n_got++] = f.done[k].index;
        }
        /* every fourth frame 3 vblanks ahead: the hold reads statistics */
        CHECK_I(psyscr_flip_at(&s, f.onset + (i % 4 == 0 ? 3 * f.period : 0), NULL), PSYSCR_OK);
    }
    psyscr_close(&s);
    CHECK_I(f.done_lost, 0);
    CHECK_I(psyscr_native(&s, &nat), PSYSCR_ERR_CLOSED);
    n = ring_drain();
    for (j = 0; j < n; j++) {
        const psyrt_event* e = &g_ev[j];
        if (e->source != PSYRT_SRC_SCREEN || e->kind != PSYSCR_EV_FLIP) continue;
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
    psyscr_screen s;
    static psyscr_frame f;   /* static: gcc -O3 cannot see begin() fill it */
    enum { N = 120 };
    static int64_t planned[N], onset[N];
    static uint32_t dropped[N];
    static uint16_t flags[N];
    int i, j, n;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.native = native;
    c.path = PSYSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 3));
    if (!psyscr_is_open(&s)) return;
    memset(planned, 0, sizeof planned);
    for (i = 0; i < N; i++) {
        psyscr_record out;
        int64_t vb;
        CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
        vlose(P_NS / 20);   /* a short draw */
        vb = c.t0 + (sc_count(&c, now_ns()) + 1) * P_NS;   /* the next vblank */
        switch (i % 20) {
        case 5: vlose(P_NS + P_NS / 2); break;            /* caller preempted */
        case 9: c.preempt_next = P_NS + P_NS / 2; break;  /* preempted in the call */
        case 13: busy_until(vb - P_NS / 16); break;       /* inside the margin */
        case 17: busy_until(vb - 1000); break;            /* 1 us before a vblank */
        default: break;
        }
        CHECK_I(psyscr_flip_at(&s, f.onset, &out), PSYSCR_OK);
        planned[i] = out.planned;
    }
    psyscr_close(&s);
    memset(onset, 0, sizeof onset);
    memset(dropped, 0, sizeof dropped);
    memset(flags, 0, sizeof flags);
    n = ring_drain();
    for (j = 0; j < n; j++) {
        const psyrt_event* e = &g_ev[j];
        uint32_t idx;
        if (e->source != PSYRT_SRC_SCREEN || e->kind != PSYSCR_EV_FLIP) continue;
        idx = e->u.u32[9];
        if (idx >= N) continue;
        onset[idx] = (int64_t)e->t_ns;
        dropped[idx] = e->u.u16[4];
        flags[idx] = (uint16_t)PSYSCR_EV_FLAGS_OF(e->u.u16[5]);
    }
    for (i = 0; i < N; i++) {
        int k = i % 20;
        CHECK(onset[i] != 0);
        CHECK(!(flags[i] & PSYSCR_FLIP_EARLY));
        CHECK(onset[i] >= planned[i]);
        if (k == 5) {
            CHECK(flags[i] & PSYSCR_FLIP_LATE_TARGET);
            CHECK_I(dropped[i], 0);
            CHECK_I(onset[i], planned[i]);
        } else if (k == 9) {
            CHECK(!(flags[i] & PSYSCR_FLIP_LATE_TARGET));
            CHECK(dropped[i] >= 1);
        } else if (k == 13 || k == 17) {
            CHECK_I(dropped[i], 0);
            CHECK_I(onset[i], planned[i]);
        } else if (k != 6 && k != 10) {   /* the frame after a lost one may be late */
            CHECK(!(flags[i] & PSYSCR_FLIP_LATE_TARGET));
            CHECK_I(dropped[i], 0);
            CHECK_I(onset[i], planned[i]);
        }
    }
}


/* -------------------------------------------------------- triggers, codes */

static struct { int n; psyscr_trigger_info e[1024]; } g_tl;
static void tl_fn(void* ctx, const psyscr_trigger_info* i) {
    (void)ctx;
    if (g_tl.n < 1024) g_tl.e[g_tl.n++] = *i;
}
static struct { int n; psyscr_record r[512]; psyscr_trigger_result t[512]; int nt[512]; } g_fl;
static void fl_fn(void* ctx, const psyscr_record* r, const psyscr_trigger_result* t, int n) {
    (void)ctx;
    if (g_fl.n < 512) {
        g_fl.r[g_fl.n] = *r;
        g_fl.nt[g_fl.n] = n;
        if (n > 0) g_fl.t[g_fl.n] = t[0];
        g_fl.n++;
    }
}

static bool open_trig(psyscr_screen* s, script* c, int n_ch, const int64_t* offsets, bool fence) {
    static psyscr_trigger_desc td[4];
    psyscr_desc d;
    int i;
    memset(s, 0, sizeof *s);
    memset(&d, 0, sizeof d);
    for (i = 0; i < n_ch; i++) {
        memset(&td[i], 0, sizeof td[i]);
        td[i].fn = tl_fn;
        td[i].offset_ns = offsets ? offsets[i] : 0;
    }
    d.backend = PSYSCR_BACKEND_CUSTOM;
    d.presenter = &g_scripted;
    d.presenter_ctx = c;
    d.ring = &g_ring;
    d.triggers = td;
    d.n_triggers = n_ch;
    d.trigger_fence = fence;
    g_tl.n = 0;
    g_fl.n = 0;
    if (!psyscr_open(s, &d)) return false;
    psyscr_on_flip(s, fl_fn, NULL);
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
    psyscr_screen s;
    static psyscr_frame f;   /* static: gcc -O3 cannot see begin() fill it */
    enum { N = 100 };
    int i, j, n, by_frame[N], seen = 0, recs = 0;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = PSYSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_trig(&s, &c, 1, NULL, false));
    if (!psyscr_is_open(&s)) return;
    for (i = 0; i < N; i++) {
        CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
        CHECK_I(psyscr_trigger(&s, 0, (uint32_t)i), PSYSCR_OK);
        vlose(P_NS / 20);
        if (i % 20 == 5) vlose(P_NS + P_NS / 2);
        if (i % 20 == 9) c.preempt_next = P_NS + P_NS / 2;
        if (i % 20 == 13) c.drop[s.next_id + 1] = 1;
        CHECK_I(psyscr_flip_at(&s, f.onset, NULL), PSYSCR_OK);
    }
    CHECK_I(psyscr_trigger(&s, 0, 1), PSYSCR_ERR_ORDER);   /* outside a frame */
    psyscr_close(&s);
    vforget(&s);
    for (i = 0; i < N; i++) by_frame[i] = -1;
    for (j = 0; j < g_tl.n; j++) if (g_tl.e[j].frame >= 0 && g_tl.e[j].frame < N) by_frame[g_tl.e[j].frame] = j;
    for (j = 0; j < g_fl.n; j++) {
        const psyscr_record* r = &g_fl.r[j];
        const psyscr_trigger_result* t = &g_fl.t[j];
        int k;
        if (r->index < 0 || r->index >= N) continue;
        recs++;
        CHECK_I(g_fl.nt[j], 1);
        if (g_fl.nt[j] != 1) continue;
        k = (int)(r->index % 20);
        CHECK(by_frame[r->index] >= 0);
        CHECK_I(t->code, r->index);
        CHECK(!(t->flags & PSYSCR_TRIG_FLUSHED));
        if (k == 13) {
            CHECK_I(r->dropped, 1);
            CHECK(t->flags & PSYSCR_TRIG_FIRED_EARLY);
            CHECK_I(t->mismatch, 1);
            CHECK_I(t->fired_ns, r->onset - P_NS);   /* on the planned vblank */
        } else {
            CHECK_I(t->mismatch, 0);
            CHECK_I(t->fired_ns, r->onset);           /* exactly on the onset */
            CHECK(!(t->flags & (PSYSCR_TRIG_FIRED_EARLY | PSYSCR_TRIG_FIRED_LATE)));
            if (k == 9) { CHECK(t->flags & PSYSCR_TRIG_MOVED); CHECK(r->dropped >= 1); }
            if (k == 5) { CHECK(r->flags & PSYSCR_FLIP_LATE_TARGET); CHECK(!(t->flags & PSYSCR_TRIG_MOVED)); }
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
            const psyrt_event* e = &g_ev[j];
            if (e->source != PSYRT_SRC_SCREEN || e->kind != PSYSCR_EV_TRIGGER) continue;
            trig++;
            if (e->u.u16[13] & PSYSCR_TRIG_FIRED_EARLY) { early++; CHECK_I(e->u.i32[7], 1); }
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
    psyscr_screen s;
    static psyscr_frame f;
    int i, j, early = 0, gpu_moved = 0;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = PSYSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_trig(&s, &c, 1, NULL, fence != 0));
    if (!psyscr_is_open(&s)) return;
    for (i = 0; i < 40; i++) {
        CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
        CHECK_I(psyscr_trigger(&s, 0, (uint32_t)i), PSYSCR_OK);
        if (i % 10 == 4) c.gpu_ns[s.next_id + 1] = P_NS + P_NS / 5;   /* finishes after its vblank */
        CHECK_I(psyscr_flip_at(&s, f.onset, NULL), PSYSCR_OK);
    }
    psyscr_close(&s);
    vforget(&s);
    for (j = 0; j < g_fl.n; j++) {
        const psyscr_trigger_result* t = &g_fl.t[j];
        if (g_fl.r[j].index < 0 || g_fl.nt[j] != 1) continue;
        if (t->flags & PSYSCR_TRIG_FIRED_EARLY) early++;
        if (t->flags & PSYSCR_TRIG_GPU_MOVED) { gpu_moved++; CHECK_I(t->mismatch, 0); CHECK_I(t->fired_ns, g_fl.r[j].onset); }
    }
    if (fence) { CHECK_I(early, 0); CHECK_I(gpu_moved, 4); }
    else { CHECK_I(early, 4); CHECK_I(gpu_moved, 0); }
}

/* Several channels with offsets fire in deadline order; trigger_at fires at
 * its time; a frame takes at most PSYSCR_MAX_JOBS; close flushes what is
 * still armed, flagged. */
static void test_trigger_channels(void) {
    static script c;
    psyscr_screen s;
    static psyscr_frame f;
    static const int64_t off[3] = { 0, 1000000, -500000 };
    int i, j, refused = 0;
    int64_t t_at;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = PSYSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_trig(&s, &c, 3, off, false));
    if (!psyscr_is_open(&s)) return;
    CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
    for (i = 0; i < 3; i++) CHECK_I(psyscr_trigger(&s, i, 100u + (uint32_t)i), PSYSCR_OK);
    CHECK_I(psyscr_trigger(&s, 3, 1), PSYSCR_ERR_ARG);
    for (i = 3; i < PSYSCR_MAX_JOBS + 1; i++) if (psyscr_trigger(&s, 0, 0) == PSYSCR_ERR_REFUSED) refused++;
    CHECK_I(refused, 1);
    CHECK_I(psyscr_flip_at(&s, f.onset, NULL), PSYSCR_OK);
    for (i = 0; i < 3; i++) {   /* the frame's 16 jobs fire and free their slots */
        CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
        CHECK_I(psyscr_flip_at(&s, f.onset, NULL), PSYSCR_OK);
    }
    t_at = now_ns() + 7 * P_NS + 12345;
    CHECK_I(psyscr_trigger_at(&s, 1, 777, t_at), PSYSCR_OK);
    CHECK_I(psyscr_trigger_at(&s, 0, 888, now_ns() + 1000000000LL), PSYSCR_OK);   /* still armed at close */
    for (i = 0; i < 12; i++) {
        CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
        CHECK_I(psyscr_flip_at(&s, f.onset, NULL), PSYSCR_OK);
    }
    psyscr_close(&s);
    vforget(&s);
    /* the frame's 16 in deadline order: channel 2 (-0.5 ms), the 14 on
     * channel 0, channel 1 (+1 ms) */
    CHECK(g_tl.n >= PSYSCR_MAX_JOBS);
    if (g_tl.n >= PSYSCR_MAX_JOBS) {
        CHECK_I(g_tl.e[0].channel, 2);
        CHECK_I(g_tl.e[1].channel, 0);
        CHECK_I(g_tl.e[PSYSCR_MAX_JOBS - 1].channel, 1);
        CHECK_I(g_tl.e[0].fired_ns, g_tl.e[1].fired_ns - 500000);
        CHECK_I(g_tl.e[PSYSCR_MAX_JOBS - 1].fired_ns, g_tl.e[1].fired_ns + 1000000);
    }
    for (j = 0; j < g_tl.n; j++) {
        if (g_tl.e[j].code == 777) { CHECK_I(g_tl.e[j].fired_ns, t_at + 1000000); CHECK_I(g_tl.e[j].frame, -1); }
        if (g_tl.e[j].code == 888) CHECK(g_tl.e[j].flags & PSYSCR_TRIG_FLUSHED);
    }
    CHECK_I(g_tl.n, PSYSCR_MAX_JOBS + 2);
}

/* The race the lock settles: a trigger the worker has taken (FIRING) when
 * its flip turns out late is a mismatch, never moved and never fired twice.
 * The worker's choice is made by hand here, between the two calls the
 * real threads would make. */
static void test_trigger_race(void) {
    static script c;
    psyscr_screen s;
    static psyscr_frame f;
    psyscr__pend fake;
    psyscr__job* j = NULL;
    int i, fired_before;
    int64_t deadline;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = PSYSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_trig(&s, &c, 1, NULL, false));
    if (!psyscr_is_open(&s)) return;
    CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
    CHECK_I(psyscr_trigger(&s, 0, 5), PSYSCR_OK);
    CHECK_I(psyscr_flip_at(&s, f.onset, NULL), PSYSCR_OK);
    for (i = 0; i < PSYSCR_MAX_JOBS; i++) if (s.job[i].state == PSYSCR__J_ARMED) j = &s.job[i];
    CHECK(j != NULL);
    if (j) {
        j->state = PSYSCR__J_FIRING;   /* the worker took it */
        deadline = j->deadline;
        fired_before = g_tl.n;
        memset(&fake, 0, sizeof fake);
        fake.id = j->pend_id;
        fake.rec.index = j->frame;
        fake.rec.onset = deadline + P_NS;
        psyscr__trig_flip_done(&s, &fake, j->count + 1);   /* shown a vblank later */
        CHECK_I(j->state, PSYSCR__J_FIRING);
        CHECK_I(j->deadline, deadline);                    /* not moved */
        CHECK_I(j->mismatch, 1);
        CHECK(j->flags & PSYSCR_TRIG_FIRED_EARLY);
        psyscr__trig_run(&s, deadline + 10 * P_NS, 0);    /* the worker runs again */
        CHECK_I(g_tl.n, fired_before);                     /* no second firing */
        j->state = PSYSCR__J_FIRED;                        /* the worker finishes */
        j->fired = deadline;
    }
    /* Not taken yet: the record moves it to the vblank shown, where it
     * fires once, with no mismatch. */
    CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
    CHECK_I(psyscr_trigger(&s, 0, 6), PSYSCR_OK);
    CHECK_I(psyscr_flip_at(&s, f.onset, NULL), PSYSCR_OK);
    j = NULL;
    for (i = 0; i < PSYSCR_MAX_JOBS; i++) if (s.job[i].state == PSYSCR__J_ARMED && s.job[i].code == 6) j = &s.job[i];
    CHECK(j != NULL);
    if (j) {
        deadline = j->deadline;
        fired_before = g_tl.n;
        memset(&fake, 0, sizeof fake);
        fake.id = j->pend_id;
        fake.rec.index = j->frame;
        fake.rec.onset = deadline + P_NS;
        psyscr__trig_flip_done(&s, &fake, j->count + 1);
        CHECK_I(j->state, PSYSCR__J_ARMED);
        CHECK(j->deadline > deadline + P_NS / 2);          /* one vblank on */
        CHECK(j->flags & PSYSCR_TRIG_MOVED);
        CHECK_I(j->mismatch, 0);
        psyscr__trig_run(&s, deadline, 0);                 /* the old deadline: nothing */
        CHECK_I(g_tl.n, fired_before);
        psyscr__trig_run(&s, j->deadline, 0);
        CHECK_I(g_tl.n, fired_before + 1);
        psyscr__trig_run(&s, j->deadline + P_NS, 0);
        CHECK_I(g_tl.n, fired_before + 1);
    }
    /* A worker late past two deadlines fires both in deadline order, not
     * in the order they were armed. */
    {
        int64_t t0 = now_ns() + 50 * P_NS;
        fired_before = g_tl.n;
        CHECK_I(psyscr_trigger_at(&s, 0, 71, t0 + 1000), PSYSCR_OK);
        CHECK_I(psyscr_trigger_at(&s, 0, 72, t0), PSYSCR_OK);
        psyscr__trig_run(&s, t0 + 5000, 0);
        CHECK_I(g_tl.n, fired_before + 2);
        if (g_tl.n == fired_before + 2) {
            CHECK_I(g_tl.e[fired_before].code, 72);
            CHECK_I(g_tl.e[fired_before + 1].code, 71);
        }
    }
    psyscr_close(&s);
    vforget(&s);
}

/* After a flip trigger fires, the worker arms itself for the next
 * vblank, so a trigger every frame costs the frame thread no submit; one
 * that skips frames, or comes after a late frame, still fires on its
 * onset. */
static void test_trigger_spec(void) {
    static script c;
    psyscr_screen s;
    static psyscr_frame f;
    enum { N = 60 };
    int i, j, steady = 0, fired = 0;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = PSYSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_trig(&s, &c, 1, NULL, false));
    if (!psyscr_is_open(&s)) return;
    for (i = 0; i < N; i++) {
        CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
        if (i == 10) g_frame_arms = 0;
        if (i < 30 || i % 4 == 0) CHECK_I(psyscr_trigger(&s, 0, (uint32_t)i), PSYSCR_OK);
        vlose(P_NS / 20);
        if (i == 41) vlose(P_NS + P_NS / 2);   /* a late frame: LATE_TARGET */
        CHECK_I(psyscr_flip_at(&s, f.onset, NULL), PSYSCR_OK);
        if (i == 29) steady = g_frame_arms;
    }
    psyscr_close(&s);
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
    psyscr_screen s;
    psyscr_desc d;
    static psyscr_frame f;
    uint32_t pat[8];
    uint32_t want0[40], want1[40];
    uint64_t id[40];
    int i, j, n, codes_seen = 0;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = PSYSCR_PATH_OVERLAY;
    memset(&s, 0, sizeof s);
    memset(&d, 0, sizeof d);
    d.backend = PSYSCR_BACKEND_CUSTOM;
    d.presenter = &g_scripted_codes;
    d.presenter_ctx = &c;
    d.ring = &g_ring;
    d.codes[0] = psyscr_slot_pixel_mode();
    d.codes[1] = psyscr_slot_psync();
    d.codes[1].rest = 0x000001u;
    d.n_codes = 2;
    /* refused: a patch over the Pixel Mode pixel, then two slots that meet */
    d.patch.on = true;
    CHECK(!psyscr_open(&s, &d));
    CHECK(strstr(psyscr_error(&s), "patch") != NULL);
    d.patch.on = false;
    d.codes[1].x = 0;
    CHECK(!psyscr_open(&s, &d));
    CHECK(strstr(psyscr_error(&s), "overlap") != NULL);
    d.codes[1] = psyscr_slot_psync();
    d.codes[1].rest = 0x000001u;
    ring_reset();
    CHECK(psyscr_open(&s, &d));
    if (!psyscr_is_open(&s)) return;
    psyscr_psync_pattern(pat, 42);
    CHECK_I(pat[0], 0x0000FFu);
    CHECK_I(pat[7], 42u << 8);
    for (i = 0; i < 40; i++) {
        CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
        if (i == 3) psyscr_code(&s, 0, 0x123456u);
        if (i == 5) psyscr_code_frames(&s, 0, 0xABCDEFu, 3);
        if (i == 10) CHECK_I(psyscr_code_row(&s, 1, pat, 8, 1), PSYSCR_OK);
        if (i == 12) psyscr_code_frames(&s, 0, 0x010203u, PSYSCR_CODE_HOLD);
        if (i == 20) psyscr_code_frames(&s, 0, 0, 0);   /* back to rest */
        want0[i] = (i == 3) ? 0x123456u : (i >= 5 && i < 8) ? 0xABCDEFu : (i >= 12 && i < 20) ? 0x010203u : 0u;
        want1[i] = (i == 10) ? 0x0000FFu : 0x000001u;
        id[i] = s.next_id + 1;
        CHECK_I(psyscr_flip_at(&s, f.onset, NULL), PSYSCR_OK);
    }
    CHECK_I(psyscr_code_row(&s, 0, pat, 8, 1), PSYSCR_ERR_ARG);   /* slot 0 is SOLID */
    psyscr_close(&s);
    for (i = 0; i < 40; i++) {
        CHECK_I(c.code_seen[id[i]][0], want0[i]);
        CHECK_I(c.code_seen[id[i]][1], want1[i]);
    }
    n = ring_drain();
    for (j = 0; j < n; j++) {
        const psyrt_event* e = &g_ev[j];
        uint32_t idx;
        if (e->source != PSYRT_SRC_SCREEN || e->kind != PSYSCR_EV_CODE) continue;
        idx = e->u.u32[0];
        if (idx >= 40) continue;
        CHECK_I(e->u.u16[3], 2);
        CHECK_I(e->u.u32[2], want0[idx]);
        CHECK_I(e->u.u32[3], want1[idx]);
        CHECK(e->u.u16[2] & PSYSCR_CODE_RISK_UNVERIFIED);   /* no self test on a script */
        codes_seen++;
    }
    CHECK(codes_seen >= 39);
}

/* The present callback runs once per flip, before the present, and moves
 * the GL epoch; the generation is fixed from open to close. */
static int g_pres_calls;
static int64_t g_pres_index;
static void pres_fn(void* ctx, const psyscr_present_info* i) { (void)ctx; g_pres_calls++; g_pres_index = i->index; }
static void test_present_hook(void) {
    static script c;
    psyscr_screen s;
    static psyscr_frame f;
    uint32_t e0, g0;
    int i;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = PSYSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 3));
    if (!psyscr_is_open(&s)) return;
    psyscr_on_present(&s, pres_fn, NULL);
    e0 = psyscr_gl_epoch(&s);
    g0 = psyscr_gl_generation(&s);
    CHECK(g0 != 0);
    g_pres_calls = 0;
    for (i = 0; i < 5; i++) {
        CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
        CHECK_I(psyscr_flip_at(&s, f.onset, NULL), PSYSCR_OK);
        CHECK_I(g_pres_index, i);
    }
    CHECK_I(g_pres_calls, 5);
    CHECK_I(psyscr_gl_epoch(&s) - e0, 5);
    CHECK_I(psyscr_gl_generation(&s), g0);
    psyscr_close(&s);
    CHECK_I(psyscr_gl_generation(&s), 0);
}

/* Missing statistics give ESTIMATED records at the planned vblank. */
static void test_missing(void) {
    static script c;
    psyscr_screen s;
    static psyscr_frame f;   /* static: gcc -O3 cannot see begin() fill it */
    int i, j, n, est = 0, est_ok = 0;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = PSYSCR_PATH_OVERLAY;
    for (i = 10; i < 13; i++) c.missing[7 + i] = 1;
    c.missing[7 + 20] = 1;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 3));
    if (!psyscr_is_open(&s)) return;
    for (i = 0; i < 40; i++) {
        CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
        CHECK_I(psyscr_flip(&s), PSYSCR_OK);
    }
    psyscr_close(&s);
    n = ring_drain();
    for (j = 0; j < n; j++) {
        const psyrt_event* e = &g_ev[j];
        uint32_t idx, flags;
        if (e->source != PSYRT_SRC_SCREEN || e->kind != PSYSCR_EV_FLIP) continue;
        idx = e->u.u32[9];
        flags = PSYSCR_EV_FLAGS_OF(e->u.u16[5]);
        if (flags & PSYSCR_FLIP_ESTIMATED) {
            est++;
            if (idx == 10 || idx == 11 || idx == 12 || idx == 20) est_ok++;
            if (!(flags & PSYSCR_FLIP_LATE_TARGET)) CHECK_I((int64_t)e->t_ns, e->u.i64[0]);
        }
    }
    CHECK_I(est, 4);
    CHECK_I(est_ok, 4);
}

/* The snap: nearest vblank at lead 0.5, the next one at or after under
 * PSYSCR_LEAD_NONE; a hold to a later vblank lands on it, natively or by
 * the header's own wait. */
static void test_snap_and_hold(int native, double lead) {
    static script c;
    psyscr_screen s;
    static psyscr_frame f;   /* static: gcc -O3 cannot see begin() fill it */
    int i, checked = 0, j, n;
    int64_t want[100];
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.native = native;
    c.path = PSYSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_scripted(&s, &c, lead, 3));
    if (!psyscr_is_open(&s)) return;
    for (i = 0; i < 100; i++) {
        int ahead = i % 4;
        /* offsets inside +-0.4 frame, and one 1 us after the vblank */
        int64_t u = (i % 3 == 0) ? 1000 : ((i % 3 == 1) ? -(int64_t)(0.4 * P_NS) : (int64_t)(0.4 * P_NS));
        int64_t t, expect;
        psyscr_record out;
        CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
        t = f.onset + (int64_t)ahead * P_NS + u;
        expect = f.onset + (int64_t)ahead * P_NS;
        if (lead < 0 && u > 0) expect += P_NS;          /* never early */
        want[i] = expect;
        CHECK_I(psyscr_flip_at(&s, t, &out), PSYSCR_OK);
        if (!(out.flags & PSYSCR_FLIP_LATE_TARGET) && expect >= f.onset) CHECK_I(out.planned, expect);
    }
    psyscr_close(&s);
    n = ring_drain();
    for (j = 0; j < n; j++) {
        const psyrt_event* e = &g_ev[j];
        uint32_t idx, flags;
        if (e->source != PSYRT_SRC_SCREEN || e->kind != PSYSCR_EV_FLIP) continue;
        idx = e->u.u32[9];
        flags = PSYSCR_EV_FLAGS_OF(e->u.u16[5]);
        if (idx >= 100 || (flags & PSYSCR_FLIP_LATE_TARGET)) continue;
        CHECK_I((int64_t)e->t_ns, want[idx]);
        CHECK(!(flags & PSYSCR_FLIP_EARLY));
        checked++;
    }
    CHECK(checked >= 60);
}

/* Phases, wait_flip, offsets and the patch value clamp. */
static void test_phases_and_wait(void) {
    static script c;
    psyscr_screen s;
    psyscr_desc d;
    static psyscr_frame f;   /* static: gcc -O3 cannot see begin() fill it */
    psyscr_record r;
    int64_t t0;
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = PSYSCR_PATH_OVERLAY;
    memset(&s, 0, sizeof s);
    memset(&d, 0, sizeof d);
    d.backend = PSYSCR_BACKEND_CUSTOM;
    d.presenter = &g_scripted;
    d.presenter_ctx = &c;
    d.onset_offset_ns = 7000;
    CHECK(psyscr_open(&s, &d));
    if (!psyscr_is_open(&s)) return;
    CHECK_I(psyscr_wait_flip(&s, &r), PSYSCR_ERR_ORDER);   /* nothing flipped yet */
    CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
    CHECK_I((f.onset - 7000 - c.t0) % P_NS, 0);
    t0 = now_ns();
    busy_until(t0 + 300000);
    psyscr_mark(&s, PSYSCR_PHASE_EVALUATE);
    busy_until(now_ns() + 200000);
    psyscr_mark(&s, PSYSCR_PHASE_UPLOAD);
    psyscr_set_patch(&s, 2.0f);
    CHECK_I(psyscr_flip(&s), PSYSCR_OK);
    CHECK_I(psyscr_wait_flip(&s, &r), PSYSCR_OK);
    CHECK(!(r.flags & PSYSCR_FLIP_PENDING));
    CHECK(r.phase_ns[PSYSCR_PHASE_EVALUATE] >= 300000);
    CHECK(r.phase_ns[PSYSCR_PHASE_UPLOAD] >= 200000);
    CHECK(r.phase_ns[PSYSCR_PHASE_UPLOAD] < 2000000);
    CHECK_I(r.phase_ns[PSYSCR_PHASE_GPU], PSYSCR_PHASE_UNKNOWN);
    if (!(r.flags & PSYSCR_FLIP_LATE_TARGET)) {
        CHECK_I(r.onset, f.onset);
        CHECK_I((r.onset - 7000 - c.t0) % P_NS, 0);
        CHECK_I(r.residual, 0);
    }
    CHECK(s.patch_value == 1.0f);
    psyscr_close(&s);
}

/* Flips that were never shown, flips with no time, planned times, tiers
 * and min_tier. */
static void test_unshown_and_tiers(void) {
    static script c;
    psyscr_screen s;
    psyscr_desc d;
    static psyscr_frame f;   /* static: gcc -O3 cannot see begin() fill it */
    psyscr_caps caps;
    int i, j, n, seen[6];
    memset(&c, 0, sizeof c);
    c.depth = 1;
    c.path = PSYSCR_PATH_COMPOSED;
    /* warmup used ids 1 to 6 or more; act on ids far enough in */
    memset(&s, 0, sizeof s);
    memset(&d, 0, sizeof d);
    d.backend = PSYSCR_BACKEND_CUSTOM;
    d.presenter = &g_scripted;
    d.presenter_ctx = &c;
    d.ring = &g_ring;
    d.min_tier = 1;
    ring_reset();
    CHECK(psyscr_open(&s, &d));
    if (!psyscr_is_open(&s)) return;
    {
        uint64_t base = s.next_id + 1;   /* the id of frame 0 */
        c.act[base + 10] = 1;
        c.act[base + 11] = 2;
        c.act[base + 12] = 3;
        c.act[base + 13] = 4;
    }
    for (i = 0; i < 30; i++) {
        CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
        CHECK_I(psyscr_flip(&s), PSYSCR_OK);
    }
    psyscr_get_caps(&s, &caps);
    CHECK_I(caps.worst_tier, PSYSCR_TIER_3);
    psyscr_close(&s);
    memset(seen, 0, sizeof seen);
    n = ring_drain();
    for (j = 0; j < n; j++) {
        const psyrt_event* e = &g_ev[j];
        uint32_t idx, flags, tier;
        if (e->source != PSYRT_SRC_SCREEN || e->kind != PSYSCR_EV_FLIP) continue;
        idx = e->u.u32[9];
        flags = PSYSCR_EV_FLAGS_OF(e->u.u16[5]);
        tier = PSYSCR_EV_TIER_OF(e->u.u16[5]);
        if (flags & PSYSCR_FLIP_LATE_TARGET) continue;
        /* never shown: the ring carries the planned vblank */
        if (idx == 10) { CHECK(flags & PSYSCR_FLIP_SKIPPED); CHECK_I((int64_t)e->t_ns, e->u.i64[0]); CHECK_I(e->u.u16[4], 0); seen[0]++; }
        else if (idx == 11) { CHECK(flags & PSYSCR_FLIP_CANCELED); CHECK_I((int64_t)e->t_ns, e->u.i64[0]); seen[1]++; }
        else if (idx == 12) {
            CHECK(flags & PSYSCR_FLIP_OCCLUDED); CHECK(flags & PSYSCR_FLIP_ESTIMATED);
            CHECK_I((int64_t)e->t_ns, e->u.i64[0]); CHECK_I(tier, PSYSCR_TIER_3); seen[2]++;
        } else if (idx == 13) {
            CHECK(flags & PSYSCR_FLIP_ONSET_PLANNED); CHECK_I(tier, PSYSCR_TIER_3);
            CHECK(flags & PSYSCR_FLIP_BELOW_TIER); seen[3]++;
        } else if (idx > 14) {
            /* composed with an OS time: tier 2, below min_tier 1 */
            CHECK_I(tier, PSYSCR_TIER_2);
            CHECK(flags & PSYSCR_FLIP_BELOW_TIER);
            CHECK_I(e->u.u16[4], 0);
            seen[4]++;
        }
    }
    CHECK_I(seen[0], 1); CHECK_I(seen[1], 1); CHECK_I(seen[2], 1); CHECK_I(seen[3], 1);
    CHECK(seen[4] >= 10);
    /* the simulated display has its own tier */
    memset(&s, 0, sizeof s);
    memset(&d, 0, sizeof d);
    d.backend = PSYSCR_BACKEND_SIM;
    d.sim_period_ns = P_NS;
    CHECK(psyscr_open(&s, &d));
    for (i = 0; i < 3; i++) { CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK); CHECK_I(psyscr_flip(&s), PSYSCR_OK); }
    {
        psyscr_record r;
        CHECK_I(psyscr_wait_flip(&s, &r), PSYSCR_OK);
        CHECK_I(r.tier, PSYSCR_TIER_SIM);
    }
    psyscr_close(&s);
    d.min_tier = 4;
    CHECK(!psyscr_open(&s, &d));
}

/* Two screens as a group. */
static void test_group(void) {
    static script c[2];
    static psyscr_screen s[2];
    psyscr_screen* sp[2];
    psyscr_frame f[2];
    int i, j, n, per[2] = { 0, 0 };
    memset(c, 0, sizeof c);
    c[0].depth = 1; c[0].path = PSYSCR_PATH_OVERLAY;
    c[1].depth = 1; c[1].path = PSYSCR_PATH_OVERLAY;
    ring_reset();
    CHECK(open_scripted(&s[0], &c[0], 0, 3));
    CHECK(open_scripted(&s[1], &c[1], 0, 4));
    sp[0] = &s[0]; sp[1] = &s[1];
    for (i = 0; i < 30; i++) {
        CHECK_I(psyscr_begin_group(sp, 2, f), PSYSCR_OK);
        CHECK_I(psyscr_flip_group_at(sp, 2, f[0].onset), PSYSCR_OK);
    }
    psyscr_close(&s[0]);
    psyscr_close(&s[1]);
    n = ring_drain();
    for (j = 0; j < n; j++)
        if (g_ev[j].source == PSYRT_SRC_SCREEN && g_ev[j].kind == PSYSCR_EV_FLIP) {
            if (g_ev[j].aux == 3) per[0]++;
            else if (g_ev[j].aux == 4) per[1]++;
        }
    CHECK_I(per[0], 30);
    CHECK_I(per[1], 30);
    CHECK_I(psyscr_begin_group(NULL, 2, f), PSYSCR_ERR_ARG);
    CHECK_I(psyscr_begin_group(sp, 2, f), PSYSCR_ERR_CLOSED);
}

static void test_errors(void) {
    static script c;
    psyscr_screen s;
    psyscr_desc d;
    static psyscr_frame f;   /* static: gcc -O3 cannot see begin() fill it */
    psyscr_presenter bad = g_scripted;
    memset(&s, 0, sizeof s);
    memset(&d, 0, sizeof d);
    CHECK(!psyscr_open(&s, NULL));
    CHECK(!psyscr_open(NULL, &d));
    CHECK(!psyscr_open(&s, &d));                       /* AUTO: no display backend without SDL */
    CHECK(strlen(psyscr_error(&s)) > 0);
    d.backend = PSYSCR_BACKEND_SIM;
    d.vrr = true;
    CHECK(!psyscr_open(&s, &d));
    CHECK(strstr(psyscr_error(&s), "sweep") != NULL);
    d.vrr = false;
    d.lead = 1.5;
    CHECK(!psyscr_open(&s, &d));
    d.lead = 0;
    d.patch.corner = 4;
    CHECK(!psyscr_open(&s, &d));
    d.patch.corner = 0;
    d.backend = PSYSCR_BACKEND_GLX_OML;
    CHECK(!psyscr_open(&s, &d));
    d.backend = PSYSCR_BACKEND_CUSTOM;
    CHECK(!psyscr_open(&s, &d));                       /* no presenter */
    bad.version = 99;
    d.presenter = &bad;
    CHECK(!psyscr_open(&s, &d));
    CHECK_I(psyscr_begin(&s, &f), PSYSCR_ERR_CLOSED);
    CHECK_I(psyscr_flip(&s), PSYSCR_ERR_CLOSED);
    CHECK_I(psyscr_begin(NULL, &f), PSYSCR_ERR_ARG);
    memset(&c, 0, sizeof c);
    c.depth = 1;
    ring_reset();
    CHECK(open_scripted(&s, &c, 0, 3));
    CHECK(!psyscr_open(&s, &d));                       /* already open */
    CHECK(psyscr_is_open(&s));
    CHECK_I(psyscr_flip(&s), PSYSCR_ERR_ORDER);        /* flip before begin */
    CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
    CHECK_I(psyscr_begin(&s, &f), PSYSCR_ERR_ORDER);   /* begin twice */
    CHECK_I(psyscr_wait_flip(&s, NULL), PSYSCR_ERR_ORDER);
    CHECK_I(psyscr_flip(&s), PSYSCR_OK);
    CHECK(psyscr_gl_proc(&s, "glClear") == NULL);
    CHECK(psyscr_window(&s) == NULL);
    CHECK_I(psyscr_restamp(&s, 123), 0);
    CHECK_I(psyscr_displays(NULL, 0), PSYSCR_ERR_NOT_IMPLEMENTED);
    psyscr_close(&s);
    psyscr_close(&s);                                  /* twice is fine */
    CHECK(!psyscr_is_open(&s));
    CHECK(strcmp(psyscr_strerror(PSYSCR_ERR_REFUSED), "refused") == 0);
    CHECK(strcmp(psyscr_version(), PSYSCR_VERSION_STRING) == 0);
}

static psyscr_mode mk(int w, int h, int num, int den) {
    psyscr_mode m;
    memset(&m, 0, sizeof m);
    m.w = w; m.h = h; m.refresh_num = num; m.refresh_den = den;
    m.period_ns = (int64_t)den * 1000000000 / num;
    return m;
}

static void test_mode_multiple(void) {
    psyscr_mode list[6], out;
    double ppm = 0;
    int k;
    list[0] = mk(1920, 1080, 60, 1);
    list[1] = mk(1920, 1080, 60000, 1001);
    list[2] = mk(1920, 1080, 120, 1);
    list[3] = mk(1920, 1080, 144, 1);
    list[4] = mk(1280, 720, 240, 1);
    list[5] = mk(1920, 1080, 15413000, 256880);   /* this laptop's 60.0008 Hz */
    /* 59.94 film-rate video wants 59.94 Hz, not 60 */
    k = psyscr_mode_multiple_in(list, 6, 60000, 1001, 1920, 1080, 0, &out, &ppm);
    CHECK_I(k, 1);
    CHECK_I(out.refresh_num, 60000);
    CHECK(ppm > -1 && ppm < 1);
    /* 24000/1001: 59.94 is 2.5x, not an integer; 120 and 144 fail; refused */
    k = psyscr_mode_multiple_in(list, 6, 24000, 1001, 1920, 1080, 0, &out, &ppm);
    CHECK_I(k, PSYSCR_ERR_REFUSED);
    /* 24 Hz: 144 = 6 x 24 and 120 = 5 x 24; the highest wins */
    k = psyscr_mode_multiple_in(list, 6, 24, 1, 1920, 1080, 0, &out, &ppm);
    CHECK_I(k, 6);
    CHECK_I(out.refresh_num, 144);
    /* 30 Hz at 1920 x 1080: 120 (x4); 240 is another size */
    k = psyscr_mode_multiple_in(list, 6, 30, 1, 1920, 1080, 0, &out, &ppm);
    CHECK_I(k, 4);
    CHECK_I(out.refresh_num, 120);
    /* any size: 240 */
    k = psyscr_mode_multiple_in(list, 6, 30, 1, 0, 0, 0, &out, &ppm);
    CHECK_I(k, 8);
    CHECK_I(out.w, 1280);
    /* 60.0008 Hz is 13 ppm from 60: inside 200 ppm, outside 5 */
    k = psyscr_mode_multiple_in(list + 5, 1, 60, 1, 0, 0, 0, &out, &ppm);
    CHECK_I(k, 1);
    CHECK(ppm > 12.5 && ppm < 13.5);
    k = psyscr_mode_multiple_in(list + 5, 1, 60, 1, 0, 0, 5, &out, &ppm);
    CHECK_I(k, PSYSCR_ERR_REFUSED);
    CHECK_I(out.refresh_num, 15413000);                /* the nearest, reported */
    CHECK_I(psyscr_mode_multiple_in(list, 6, 0, 1, 0, 0, 0, &out, &ppm), PSYSCR_ERR_ARG);
    CHECK_I(psyscr_mode_multiple_in(NULL, 6, 60, 1, 0, 0, 0, &out, &ppm), PSYSCR_ERR_ARG);
}

static void test_params(void) {
    int n = 0, i, j;
    const psyscr_param* p = psyscr_params(&n);
    CHECK(p != NULL);
    CHECK(n >= 15);
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
    psyscr_screen s;
    psyscr_desc d;
    static psyscr_frame f;   /* static: gcc -O3 cannot see begin() fill it */
    char line[256];
    int i, late = 0, onset_ok = 0, completed = 0;
    int64_t prev = 0, grid0 = 0;
    g_virtual = 0;
    memset(&s, 0, sizeof s);
    memset(&d, 0, sizeof d);
    d.backend = PSYSCR_BACKEND_SIM;
    d.sim_period_ns = P_NS;
    CHECK(psyscr_open(&s, &d));
    if (!psyscr_is_open(&s)) { g_virtual = 1; return; }
    for (i = 0; i < 120; i++) {
        psyscr_record out;
        CHECK_I(psyscr_begin(&s, &f), PSYSCR_OK);
        CHECK_I(f.period, P_NS);
        if (!grid0) grid0 = f.onset;
        CHECK_I((f.onset - grid0) % P_NS, 0);
        if (i && f.last && f.last->index == i - 1) {
            completed++;
            CHECK_I(f.last->path, PSYSCR_PATH_SIMULATED);
            CHECK_I((f.last->onset - grid0) % P_NS, 0);
            CHECK(f.last->onset >= f.last->planned);   /* never early */
            if (!(f.last->flags & PSYSCR_FLIP_LATE_TARGET) && f.last->dropped == 0) {
                CHECK_I(f.last->onset, prev);
                onset_ok++;
            }
        }
        prev = f.onset;
        CHECK_I(psyscr_flip_at(&s, f.onset, &out), PSYSCR_OK);
        if (out.flags & PSYSCR_FLIP_LATE_TARGET) late++;
    }
    CHECK(psyscr_describe(&s, line, sizeof line) > 0);
    CHECK(strstr(line, "backend=sim") != NULL);
    psyscr_close(&s);
    g_virtual = 1;
    CHECK(completed >= 1);
    if (late || onset_ok < completed) printf("  sim: %d of 120 frames late, %d of %d on the prediction\n", late, onset_ok, completed);
}

int main(void) {
    psyrt_thread_elevate(NULL);
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
    test_snap_and_hold(1, 0);
    test_snap_and_hold(0, 0);
    test_snap_and_hold(1, PSYSCR_LEAD_NONE);
    test_snap_and_hold(0, PSYSCR_LEAD_NONE);
    test_phases_and_wait();
    test_group();
    test_unshown_and_tiers();
    test_sim();
    if (g_failures) {
        fprintf(stderr, "psy_screen_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("psy_screen_test: all checks passed\n");
    return 0;
}
