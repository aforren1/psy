/* psy_timeline_test.c - self-checking test for psy_timeline.h. No framework:
 * it returns 0 when every check passed and 1 after printing each failure.
 *
 * Build and run it with the warnings as errors, and again under the
 * sanitizers:
 *
 *     gcc -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Werror -O2 -I. \
 *         -o tl_test tests/adapt/psy_timeline_test.c -lm && ./tl_test
 *     gcc -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Werror -O1 -g -I. \
 *         -fsanitize=address,undefined -fno-sanitize-recover=all \
 *         -o tl_test tests/adapt/psy_timeline_test.c -lm && ./tl_test
 *     cl /nologo /W4 /WX /I. tests\adapt\psy_timeline_test.c
 *
 * Written from the manual (the comment block and the declarations of
 * psy_timeline.h), not from the implementation. The quantization, the
 * easings and the randomized check use models written here from the
 * manual's wording, so the two can disagree. Where the manual reads two
 * ways, the check takes the literal reading and the comment says so.
 *
 * TL_MUT=<bits> breaks the track model on purpose (the MUT_* bits below,
 * the tween takeover's included); every bit must make the run fail.
 * TL_TRACE=<seed> traces a model seed, its tweens and takeovers too.
 *
 * Two checks take the literal reading of a tween's "now" ("the base time
 * of the base's last evaluated onset") where the base's time went back:
 * after a rewinding anchor, and after a pause at an earlier RT time. The
 * randomized model sets start_set in those cases, so it does not depend
 * on the reading.
 */
#define PSY_TIMELINE_IMPLEMENTATION
#include "psy_timeline.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- harness */

static int g_failures = 0;

static void fail(int line, const char* what) {
    fprintf(stderr, "psy_timeline_test: FAIL at line %d: %s\n", line, what);
    g_failures++;
}

static void fail_i(int line, const char* what, long long got, long long want) {
    fprintf(stderr, "psy_timeline_test: FAIL at line %d: %s (got %lld, want %lld)\n",
            line, what, got, want);
    g_failures++;
}

static void fail_f(int line, const char* what, double got, double want) {
    fprintf(stderr, "psy_timeline_test: FAIL at line %d: %s (got %.9g, want %.9g)\n",
            line, what, got, want);
    g_failures++;
}

static void fail_s(int line, const char* what, const char* got, const char* want) {
    fprintf(stderr, "psy_timeline_test: FAIL at line %d: %s\n  got:  [%s]\n  want: [%s]\n",
            line, what, got, want);
    g_failures++;
}

/* long long, not long: long is 32 bits on Windows and times are 64. */
#define CHECK(cond) do { if (!(cond)) fail(__LINE__, #cond); } while (0)
#define CHECK_I(got, want) do { long long g_ = (long long)(got), w_ = (long long)(want); \
    if (g_ != w_) fail_i(__LINE__, #got, g_, w_); } while (0)
#define CHECK_F(got, want, tol) do { double g_ = (double)(got), w_ = (double)(want); \
    if (!(fabs(g_ - w_) <= (tol))) fail_f(__LINE__, #got, g_, w_); } while (0)
#define CHECK_S(got, want) do { const char* g_ = (got); const char* w_ = (want); \
    if (strcmp(g_, w_) != 0) fail_s(__LINE__, #got, g_, w_); } while (0)

/* Built at run time: MSVC rejects 0.0/0.0 as a constant expression. */
static volatile double g_zero = 0.0;
static double make_nan(void) { return g_zero / g_zero; }
static double make_inf(void) { return 1.0 / g_zero; }

#define S_NS  PSYTL_NS_PER_S
#define MS_NS PSYTL_NS_PER_MS
#define P60   INT64_C(16666667)
#define T0    PSYTL_NS_PER_S
#define LIM62 INT64_C(4611686018427387904)

/* Handles are about 12 KB each, so not on the stack. */
#define STORE_CAP 1024
static psytl_timeline g_tl, g_tl2, g_closed;
static psytl_event g_store[STORE_CAP], g_store2[STORE_CAP];
static psytl_event g_fired[STORE_CAP];
static psytl_event g_snap[STORE_CAP];

/* ---------------------------------------------------------------- the rng */

static uint64_t g_rng = 1;

static uint64_t sm64(void) {
    uint64_t z = (g_rng += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

static int rnd_int(int n) { return (int)(sm64() % (uint64_t)n); }

/* Uniform in [lo, hi]. */
static int64_t rnd_range(int64_t lo, int64_t hi) {
    return lo + (int64_t)(sm64() % (uint64_t)(hi - lo + 1));
}

/* ---------------------------------------------------------------- helpers */

static psytl_event ev(int base, int64_t time, int kind, int target, float value) {
    psytl_event e;
    memset(&e, 0, sizeof e);
    e.time = time;
    e.base = (uint8_t)base;
    e.kind = (uint8_t)kind;
    e.target = target;
    e.value = value;
    return e;
}

static int add1(psytl_timeline* tl, int base, int64_t time, int kind, int target, float value) {
    psytl_event e = ev(base, time, kind, target, value);
    return psytl_add(tl, &e);
}

static psytl_key mkkey(int64_t t, float v, int ease) {
    psytl_key k;
    memset(&k, 0, sizeof k);
    k.time = t;
    k.value = v;
    k.ease = (uint8_t)ease;
    return k;
}

static psytl_key mkbez(int64_t t, float v, int curve) {
    psytl_key k = mkkey(t, v, PSYTL_EASE_BEZIER);
    k.curve = (uint16_t)curve;
    return k;
}

static psytl_curve mkcurve(float x1, float cy1, float x2, float cy2) {
    psytl_curve c;
    c.x1 = x1;
    c.y1 = cy1;
    c.x2 = x2;
    c.y2 = cy2;
    return c;
}

static psytl_track trk_keys(const psytl_key* k, int n) {
    psytl_track tr;
    memset(&tr, 0, sizeof tr);
    tr.keys = k;
    tr.n_keys = n;
    return tr;
}

static psytl_track trk_samples(const float* s, int n, int64_t t0, int64_t rate, int interp) {
    psytl_track tr;
    memset(&tr, 0, sizeof tr);
    tr.samples = s;
    tr.n_samples = n;
    tr.t0 = t0;
    tr.rate = rate;
    tr.interp = interp;
    return tr;
}

/* The v0.1.0 psytl_sample(keys, n, t). */
static double ksample(const psytl_key* k, int n, int64_t t) {
    psytl_track tr = trk_keys(k, n);
    return psytl_sample(&tr, t);
}

static bool open_h(psytl_timeline* tl, psytl_event* store, int cap, int nch,
                   const float* init, double lead) {
    psytl_desc d;
    memset(&d, 0, sizeof d);
    if (store && cap > 0) memset(store, 0, (size_t)cap * sizeof *store);
    d.events = store;
    d.event_capacity = cap;
    d.n_channels = nch;
    d.initial = init;
    /* Every check here that passes 0.0 means no lead at all. Since v0.2.0
     * a desc lead of 0 means the default, half a frame, so 0.0 maps to
     * PSYTL_LEAD_NONE; test_lead_default() checks the raw 0. */
    d.lead = lead == 0.0 ? PSYTL_LEAD_NONE : lead;
    return psytl_open(tl, &d);
}

static int eval_at(psytl_timeline* tl, int64_t onset, int64_t period, int64_t index,
                   psytl_event* fired, int cap) {
    psytl_frame f;
    f.onset = onset;
    f.period = period;
    f.index = index;
    return psytl_evaluate(tl, &f, fired, cap);
}

static int n_events(const psytl_timeline* tl, int base) {
    int n = -1;
    (void)psytl_events(tl, base, &n);
    return n;
}

static int key_cmp(const psytl_event* a, const psytl_event* b) {
    if (a->base != b->base) return a->base < b->base ? -1 : 1;
    if (a->time != b->time) return a->time < b->time ? -1 : 1;
    if (a->id != b->id) return a->id < b->id ? -1 : 1;
    return 0;
}

/* RT target order of the report: onset - residual, then id. */
static int report_cmp(const psytl_event* a, const psytl_event* b) {
    int64_t ta = a->onset - a->residual, tb = b->onset - b->residual;
    if (ta != tb) return ta < tb ? -1 : 1;
    if (a->id != b->id) return a->id < b->id ? -1 : 1;
    return 0;
}

static void check_sorted(const psytl_timeline* tl, int line) {
    int n = 0, i, b, total = 0;
    const psytl_event* p = psytl_events(tl, PSYTL_ALL_BASES, &n);
    if (n > 0 && !p) { fail(line, "psytl_events(ALL) is NULL with n > 0"); return; }
    for (i = 1; i < n; i++)
        if (key_cmp(&p[i - 1], &p[i]) >= 0) { fail(line, "storage not sorted by base, time, id"); break; }
    for (b = 0; b < PSYTL_MAX_BASES; b++) {
        int nb = 0;
        const psytl_event* q = psytl_events(tl, b, &nb);
        for (i = 0; i < nb; i++)
            if (q[i].base != b) { fail(line, "psytl_events(base) holds another base"); break; }
        total += nb;
    }
    if (total != n) fail_i(line, "sum of per-base psytl_events counts", total, n);
}

static bool is_pending_reset(const psytl_event* e) {
    return e && e->frame == -1 && e->onset == 0 && e->residual == 0 && e->flags == 0;
}

/* ------------------------------------------------- reference easing model */

#define REF_PI 3.14159265358979323846

static double ref_ease(int ease, double v0, double v1, double u) {
    switch (ease) {
    case PSYTL_EASE_STEP:     return v0;
    case PSYTL_EASE_COSINE:   return v0 + (v1 - v0) * ((1.0 - cos(REF_PI * u)) / 2.0);
    case PSYTL_EASE_QUAD_IN:  return v0 + (v1 - v0) * (u * u);
    case PSYTL_EASE_QUAD_OUT: return v0 + (v1 - v0) * (1.0 - (1.0 - u) * (1.0 - u));
    case PSYTL_EASE_LOG:      return v0 * pow(v1 / v0, u);
    default:                  return v0 + (v1 - v0) * u;
    }
}

/* TL_MUT=<bits> breaks the track model on purpose. Each bit must make the
 * run fail; that shows the checks can see the difference. */
static unsigned g_mut;
#define MUT_PHANTOM   1u   /* end samples repeated, not extrapolated          */
#define MUT_STEP_NS   2u   /* sample i at t0 + i * trunc(1e9 / rate)          */
#define MUT_HOLD_LATE 4u   /* the hold after the last cycle starts a cycle late */
#define MUT_HOLD_FOLD 8u   /* the hold reads start + period folded: start     */
#define MUT_BEZ_PARAM 16u  /* the curve parameter taken for x                 */
#define MUT_STEP_ONSET 32u /* STEP tracks read at the onset, not the window end */
#define MUT_TW_FROM_CALL 64u /* a tween's from read at the call, not at its start */
#define MUT_TW_SLOPE  128u /* keep_velocity ignores the old track's slope      */
#define MUT_TW_YOYO   256u /* yoyo's return leg missing                         */
/* v0.3.0: skip and peek, in the random model (ops on) only. */
#define MUT_SKIP_FIRES   512u    /* a skip is only an anchor: what it passes fires late */
#define MUT_SKIP_NOCHAN  1024u   /* a skipped SET does not count for its channel   */
#define MUT_SKIP_REACH   2048u   /* a skip leaves the reach of an evaluated base   */
#define MUT_SKIP_NOW     4096u   /* a skip leaves the furthest onset: no rewind back */
#define MUT_SKIP_AT      8192u   /* the event exactly at bt is skipped too         */
#define MUT_SKIP_UNREW   16384u  /* a rewind leaves skipped events skipped         */
#define MUT_PEEK_END     32768u  /* peek's window closed at to_rt                  */
#define MUT_PEEK_FIRED   65536u  /* peek lists fired events too                    */
#define MUT_PEEK_ONSET   131072u /* a peek copy's onset is its base time           */
#define MUT_SKIP_LATEW   262144u /* a skip back leaves waiting late events pending */

#define NS_E9 INT64_C(1000000000)

/* CSS cubic-bezier(): x(s) and y(s) from (0, 0) to (1, 1). */
static double bez_x(const psytl_curve* c, double s) {
    double a = 1.0 - s;
    return 3.0 * a * a * s * (double)c->x1 + 3.0 * a * s * s * (double)c->x2 + s * s * s;
}

static double bez_y(const psytl_curve* c, double s) {
    double a = 1.0 - s;
    return 3.0 * a * a * s * (double)c->y1 + 3.0 * a * s * s * (double)c->y2 + s * s * s;
}

/* The smallest s in [0, 1] with x(s) >= u, by bisection: x rises with s
 * when x1 and x2 are in [0, 1]. */
static double bez_s(const psytl_curve* c, double u) {
    double lo = 0.0, hi = 1.0;
    int i;
    if (u <= 0.0) return 0.0;
    if (u >= 1.0) return 1.0;
    for (i = 0; i < 80; i++) {
        double m = 0.5 * (lo + hi);
        if (bez_x(c, m) < u) lo = m; else hi = m;
    }
    return hi;
}

static double ref_bezier(const psytl_curve* c, double u) {
    if (g_mut & MUT_BEZ_PARAM) return bez_y(c, u);
    return bez_y(c, bez_s(c, u));
}

/* Every y a solver may return that stops within tolx of u in x. Where x
 * is flat in s (x1 or x2 at a limit) that is a wide range of s. */
static void bez_range(const psytl_curve* c, double u, double tolx, double* lo, double* hi) {
    double sa, sb;
    int i;
    if (g_mut & MUT_BEZ_PARAM) { *lo = *hi = bez_y(c, u); return; }
    sa = bez_s(c, u - tolx);
    sb = bez_s(c, u + tolx);
    *lo = *hi = bez_y(c, sa);
    for (i = 1; i <= 64; i++) {
        double y = bez_y(c, sa + (sb - sa) * (double)i / 64.0);
        if (y < *lo) *lo = y;
        if (y > *hi) *hi = y;
    }
}

/* TRACKS, by a linear scan: first key's value before it, last key's value
 * from it on, otherwise the last key i with t_i <= t, whose successor is
 * then strictly later (so at equal times the later key wins). */
static double ref_keyed(const psytl_key* k, int n, const psytl_curve* cv, int64_t t) {
    int i = 0, j;
    double u;
    if (!k || n < 1) return 0.0;
    if (t < k[0].time) return k[0].value;
    if (t >= k[n - 1].time) return k[n - 1].value;
    for (j = 0; j < n; j++) if (k[j].time <= t) i = j;
    u = (double)(t - k[i].time) / (double)(k[i + 1].time - k[i].time);
    if (k[i].ease == PSYTL_EASE_BEZIER) {
        double y = cv ? ref_bezier(&cv[k[i].curve], u) : u;
        return (double)k[i].value + ((double)k[i + 1].value - (double)k[i].value) * y;
    }
    return ref_ease(k[i].ease, k[i].value, k[i + 1].value, u);
}

static double ref_sample(const psytl_key* k, int n, int64_t t) {
    return ref_keyed(k, n, NULL, t);
}

/* Where t falls on a sampled track: -1 before t0, else the index i of the
 * sample at or before t, and *fnum = (t - t0) rate - i 1e9, the fraction's
 * numerator over 1e9. Integer arithmetic: sample i is at t0 + i 1e9 / rate
 * exactly. (t - t0) rate may not fit 64 bits, so it is split at whole
 * seconds; r rate < 1e18 does. */
static int64_t ref_sidx(const psytl_track* tr, int64_t t, int64_t* fnum) {
    int64_t d = t - tr->t0, q, r;
    *fnum = 0;
    if (d < 0) return -1;
    if (g_mut & MUT_STEP_NS) {
        int64_t step = NS_E9 / tr->rate, i = d / step;
        *fnum = (d - i * step) * NS_E9 / step;
        return i;
    }
    q = d / NS_E9;
    r = d % NS_E9;
    if (q > (int64_t)tr->n_samples) return (int64_t)tr->n_samples;   /* past the end */
    *fnum = r * tr->rate % NS_E9;
    return q * tr->rate + r * tr->rate / NS_E9;
}

static double ref_sampled(const psytl_track* tr, int64_t t) {
    const float* v = tr->samples;
    int n = tr->n_samples;
    int64_t i, fnum;
    double u, p0, p1, p2, p3;
    if (!v || n < 1) return 0.0;
    i = ref_sidx(tr, t, &fnum);
    if (i < 0) return v[0];
    if (i >= n - 1) return v[n - 1];
    u = (double)fnum / 1e9;
    p1 = v[i];
    p2 = v[i + 1];
    switch (tr->interp) {
    case PSYTL_INTERP_STEP:
        return p1;
    case PSYTL_INTERP_CUBIC:
        /* Phantoms: v_-1 = 2 v_0 - v_1, v_n = 2 v_n-1 - v_n-2. */
        p0 = i > 0 ? (double)v[i - 1] : (g_mut & MUT_PHANTOM) ? p1 : 2.0 * p1 - p2;
        p3 = i + 2 < n ? (double)v[i + 2] : (g_mut & MUT_PHANTOM) ? p2 : 2.0 * p2 - p1;
        return 0.5 * (2.0 * p1 + (p2 - p0) * u + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * u * u +
                      (3.0 * p1 - p0 - 3.0 * p2 + p3) * u * u * u);
    default:
        return p1 + (p2 - p1) * u;
    }
}

static double ref_base(const psytl_track* tr, int64_t t) {
    if (tr->keys && tr->n_keys > 0) return ref_keyed(tr->keys, tr->n_keys, tr->curves, t);
    if (tr->samples && tr->n_samples > 0) return ref_sampled(tr, t);
    return 0.0;
}

/* REPEAT: from the start (first key's time, or t0), t reads the track at
 * start + ((t - start) mod period) for `repeats` cycles (0: forever), then
 * holds the value at start + period read without repeating. Before the
 * start, no repeat. */
static double ref_track(const psytl_track* tr, int64_t t) {
    int64_t start, k;
    if (tr->period <= 0) return ref_base(tr, t);
    if (tr->keys && tr->n_keys > 0) start = tr->keys[0].time;
    else if (tr->samples && tr->n_samples > 0) start = tr->t0;
    else return 0.0;
    if (t < start) return ref_base(tr, t);
    k = (t - start) / tr->period;
    if (tr->repeats > 0 && k >= (int64_t)tr->repeats + ((g_mut & MUT_HOLD_LATE) ? 1 : 0))
        return ref_base(tr, (g_mut & MUT_HOLD_FOLD) ? start : start + tr->period);
    return ref_base(tr, start + (t - start) % tr->period);
}

/* The track a tween stands for: two keys, from at start to `to` at
 * start + duration, the curve copied. */
static psytl_track tween_track(psytl_key* kk, psytl_curve* cc, int64_t start, int64_t dur,
                               float from, float to, int ease, const psytl_curve* c) {
    psytl_track tr;
    kk[0] = mkkey(start, from, ease);
    kk[1] = mkkey(start + dur, to, PSYTL_EASE_LINEAR);
    tr = trk_keys(kk, 2);
    if (c) {
        *cc = *c;
        tr.curves = cc;
        tr.n_curves = 1;
    }
    return tr;
}

/* A STEP track (TRACKS): sampled with PSYTL_INTERP_STEP, or keyed with
 * every key but the last PSYTL_EASE_STEP, a tween included. An evaluate
 * reads it at the end of the lead window, base time at onset + lead x
 * period; every other track at the onset. One key: no key but the last
 * breaks the rule, and the value is constant anyway. */
static int trk_is_step(const psytl_track* tr) {
    int i;
    if (g_mut & MUT_STEP_ONSET) return 0;
    if (tr->keys && tr->n_keys > 0) {
        for (i = 0; i + 1 < tr->n_keys; i++)
            if (tr->keys[i].ease != PSYTL_EASE_STEP) return 0;
        return 1;
    }
    return tr->samples && tr->n_samples > 0 && tr->interp == PSYTL_INTERP_STEP;
}

static double tol_of(double v) { return 1e-6 * (1.0 + fabs(v)); }
/* For psytl_sample's double against the model's: the same +, -, *, / in
 * another order. */
static double tol_d(double v) { return 1e-9 * (1.0 + fabs(v)); }

/* ------------------------------------------------ tween reference model */

static psytl_tween_desc twd(float to, int64_t dur) {
    psytl_tween_desc d;
    memset(&d, 0, sizeof d);
    d.to = to;
    d.duration = dur;
    return d;
}

static psytl_tween_desc twd_at(float to, int64_t dur, int64_t start) {
    psytl_tween_desc d = twd(to, dur);
    d.start = start;
    d.start_set = true;
    return d;
}

/* A channel's driver in the model: a track the caller set, or a tween as
 * it is at its start (TWEENS). own: the keys and the curve are k[] and c,
 * so the struct may be copied. herm: keep_velocity's Hermite segment. */
typedef struct rdrv {
    psytl_track tr;
    psytl_key   k[3];
    psytl_curve c;
    int         own, herm;
    double      hf, hm, ht;   /* Hermite: from, start slope x duration, to */
    int64_t     hs, hd;       /* Hermite: start and duration               */
} rdrv;

static psytl_track rdrv_track(const rdrv* r) {
    psytl_track t = r->tr;
    if (r->own) {
        t.keys = r->k;
        t.curves = t.n_curves > 0 ? &r->c : NULL;
    }
    return t;
}

/* The cubic Hermite segment from `from` with tangent m0 (slope x duration)
 * to `to` with tangent 0, at u in [0, 1]. */
static double ref_hermite(double from, double m0, double to, double u) {
    double u2 = u * u, u3 = u2 * u;
    return (2.0 * u3 - 3.0 * u2 + 1.0) * from + (u3 - 2.0 * u2 + u) * m0 + (3.0 * u2 - 2.0 * u3) * to;
}

static double rdrv_value(const rdrv* r, int64_t t) {
    psytl_track tr;
    if (r->herm) {
        /* Before the start (after a rewind): the start value, as a keyed
         * track gives its first key's value before it. */
        if (t < r->hs) return r->hf;
        if (t >= r->hs + r->hd) return r->ht;
        return ref_hermite(r->hf, r->hm, r->ht, (double)(t - r->hs) / (double)r->hd);
    }
    tr = rdrv_track(r);
    return ref_track(&tr, t);
}

static int rdrv_step(const rdrv* r) {
    psytl_track tr;
    if (r->herm) return 0;
    tr = rdrv_track(r);
    return trk_is_step(&tr);
}

static rdrv rdrv_plain(const psytl_track* tr) {
    rdrv r;
    memset(&r, 0, sizeof r);
    r.tr = *tr;
    return r;
}

/* THE TAKEOVER: the tween `d` taking over at its start s from the old
 * driver (NULL: no track, the channel holds `held`). from is the old
 * track's value at s; with keep_velocity the start slope is the old
 * track's over the last microsecond before s. at_call is the channel's
 * value when the tween was posted, for MUT_TW_FROM_CALL only. Keys: from
 * at s with d's ease, `to` at s + duration (with d's ease under yoyo), and
 * under yoyo from again at s + 2 duration; cycles repeat the whole. */
static void rdrv_tween(rdrv* r, const psytl_tween_desc* d, int64_t s, const rdrv* old, double held,
                       double at_call) {
    double from;
    int n = 2, yoyo = d->yoyo ? 1 : 0, multi = d->cycles > 1 || d->cycles == PSYTL_FOREVER;
    memset(r, 0, sizeof *r);
    if (d->from_set) from = (double)d->from;
    else if (g_mut & MUT_TW_FROM_CALL) from = at_call;
    else from = old ? rdrv_value(old, s) : held;
    from = (double)(float)from;   /* the keys are float */
    if (d->keep_velocity) {
        r->herm = 1;
        r->hf = from;
        r->ht = (double)d->to;
        r->hs = s;
        r->hd = d->duration;
        r->hm = (old && !(g_mut & MUT_TW_SLOPE))
                    ? (rdrv_value(old, s) - rdrv_value(old, s - 1000)) / 1000.0 * (double)d->duration
                    : 0.0;
        return;
    }
    r->own = 1;
    r->k[0] = mkkey(s, (float)from, d->ease);
    r->k[1] = mkkey(s + d->duration, d->to, yoyo ? d->ease : PSYTL_EASE_LINEAR);
    if (yoyo && !(g_mut & MUT_TW_YOYO)) {
        r->k[2] = mkkey(s + 2 * d->duration, (float)from, PSYTL_EASE_LINEAR);
        n = 3;
    }
    if (d->ease == PSYTL_EASE_BEZIER) {
        r->c = d->curve;
        r->tr.n_curves = 1;
    }
    r->tr.n_keys = n;
    if (multi) {
        r->tr.period = (yoyo ? 2 : 1) * d->duration;
        r->tr.repeats = d->cycles == PSYTL_FOREVER ? 0 : d->cycles;
    }
}

static int curve_ok(const psytl_curve* c) {
    return c->x1 >= 0.0f && c->x1 <= 1.0f && c->x2 >= 0.0f && c->x2 <= 1.0f &&
           fabs((double)c->y1) <= 3.4e38 && fabs((double)c->y2) <= 3.4e38;
}

/* TWEENS' rejections, but the time limits (the callers stay far from them). */
static int tw_valid(const psytl_tween_desc* d) {
    int multi = d->cycles > 1 || d->cycles == PSYTL_FOREVER;
    if (d->duration < 0) return 0;
    if (!(fabs((double)d->to) <= 3.4e38)) return 0;
    if (d->from_set && !(fabs((double)d->from) <= 3.4e38)) return 0;
    if (d->ease < 0 || d->ease > PSYTL_EASE_BEZIER) return 0;
    if (d->ease == PSYTL_EASE_BEZIER && !curve_ok(&d->curve)) return 0;
    if (d->ease == PSYTL_EASE_LOG && (!d->from_set || d->from <= 0.0f || d->to <= 0.0f)) return 0;
    if (d->cycles < PSYTL_FOREVER) return 0;
    if ((multi || d->yoyo) && d->duration == 0) return 0;
    if (d->keep_velocity &&
        (d->ease != PSYTL_EASE_LINEAR || d->from_set || d->yoyo || multi || d->duration == 0)) return 0;
    return 1;
}

/* ------------------------------------------------------- 1. open and misc */

static void open_fail(int line, psytl_event* store, int cap, int nch, const float* init, double lead) {
    if (open_h(&g_tl, store, cap, nch, init, lead)) fail(line, "open accepted a bad desc");
    else if (psytl_error(&g_tl)[0] == '\0') fail(line, "open failed with an empty message");
}

static void test_open(void) {
    float init[4] = { 0.5f, -1.0f, 2.0f, 3.0f };
    float bad[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    const float* v;
    psytl_event e;
    psytl_desc d;
    int i;

    /* A zeroed handle is not open. */
    CHECK(!psytl_is_open(&g_closed));
    CHECK(psytl_values(&g_closed) == NULL);
    e = ev(0, 0, PSYTL_MARK, 0, 0.0f);
    CHECK_I(psytl_add(&g_closed, &e), PSYTL_ERR_CLOSED);
    CHECK_I(eval_at(&g_closed, 0, 0, 0, NULL, 0), PSYTL_ERR_CLOSED);
    CHECK_I(psytl_anchor(&g_closed, 1, 0, 0), PSYTL_ERR_CLOSED);

    open_fail(__LINE__, g_store, -1, 4, NULL, 0.0);
    open_fail(__LINE__, NULL, 4, 4, NULL, 0.0);
    open_fail(__LINE__, g_store, 4, -1, NULL, 0.0);
    open_fail(__LINE__, g_store, 4, PSYTL_MAX_CHANNELS + 1, NULL, 0.0);
    open_fail(__LINE__, g_store, 4, 4, NULL, -0.01);
    open_fail(__LINE__, g_store, 4, 4, NULL, -0.99);
    open_fail(__LINE__, g_store, 4, 4, NULL, -1.01);
    open_fail(__LINE__, g_store, 4, 4, NULL, 1.0);
    open_fail(__LINE__, g_store, 4, 4, NULL, 1.5);
    open_fail(__LINE__, g_store, 4, 4, NULL, make_nan());
    open_fail(__LINE__, g_store, 4, 4, NULL, make_inf());
    open_fail(__LINE__, g_store, 4, 4, NULL, -make_inf());
    bad[2] = (float)make_nan();
    open_fail(__LINE__, g_store, 4, 4, bad, 0.0);
    bad[2] = 0.0f;
    bad[1] = (float)make_inf();
    open_fail(__LINE__, g_store, 4, 4, bad, 0.0);
    bad[1] = (float)-make_inf();
    open_fail(__LINE__, g_store, 4, 4, bad, 0.0);

    /* A failed open leaves a never-opened handle closed. */
    memset(&d, 0, sizeof d);
    d.event_capacity = -1;
    CHECK(!psytl_open(&g_closed, &d));
    CHECK(!psytl_is_open(&g_closed));
    CHECK(psytl_values(&g_closed) == NULL);

    /* Accepted edges. */
    CHECK(open_h(&g_tl, NULL, 0, 0, NULL, 0.0));
    CHECK_S(psytl_error(&g_tl), "");
    CHECK(psytl_is_open(&g_tl));
    CHECK(open_h(&g_tl, g_store, 4, PSYTL_MAX_CHANNELS, NULL, 0.999));
    v = psytl_values(&g_tl);
    CHECK(v != NULL);
    if (v) for (i = 0; i < PSYTL_MAX_CHANNELS; i++) if (v[i] != 0.0f) { fail(__LINE__, "NULL initial is not all 0"); break; }

    /* initial is copied at open. */
    CHECK(open_h(&g_tl, g_store, 4, 4, init, 0.5));
    init[0] = 99.0f;
    v = psytl_values(&g_tl);
    CHECK(v != NULL);
    if (v) {
        CHECK(v[0] == 0.5f);
        CHECK(v[1] == -1.0f);
        CHECK(v[2] == 2.0f);
        CHECK(v[3] == 3.0f);
    }
    CHECK(psytl_value(&g_tl, 1) == -1.0f);

    /* open() resets the events and restarts the ids. */
    CHECK_I(add1(&g_tl, 0, 5, PSYTL_MARK, 0, 0.0f), 0);
    CHECK_I(add1(&g_tl, 0, 6, PSYTL_MARK, 0, 0.0f), 1);
    CHECK(open_h(&g_tl, g_store, 4, 4, NULL, 0.0));
    CHECK_I(n_events(&g_tl, PSYTL_ALL_BASES), 0);
    CHECK_I(add1(&g_tl, 0, 5, PSYTL_MARK, 0, 0.0f), 0);
}

static void test_misc(void) {
    char buf[64];
    int c;
    CHECK_I(psytl_ns(0.0), 0);
    CHECK_I(psytl_ns(1.0), S_NS);
    CHECK_I(psytl_ns(-2.0), -2 * S_NS);
    CHECK_I(psytl_ns(0.25), 250000000);
    CHECK_I(psytl_ns(1.4e-9), 1);
    CHECK_I(psytl_ns(1.6e-9), 2);
    CHECK_I(psytl_ns(-1.4e-9), -1);
    CHECK_I(psytl_ns(-1.6e-9), -2);
    CHECK_I(psytl_ns(0.4e-9), 0);
    /* 1/1024 s is exactly 976562.5 ns: a tie, away from zero (to-even
     * would give 976562). */
    CHECK_I(psytl_ns(1.0 / 1024.0), 976563);
    CHECK_I(psytl_ns(-1.0 / 1024.0), -976563);
    CHECK_I(psytl_ns(3.0 / 1024.0), 2929688);
    CHECK_I(psytl_ns(4611686018.0), INT64_C(4611686018000000000));
    CHECK_I(psytl_ns(5e9), LIM62);
    CHECK_I(psytl_ns(1e300), LIM62);
    CHECK_I(psytl_ns(-1e300), -LIM62);
    CHECK_I(psytl_ns(make_inf()), LIM62);
    CHECK_I(psytl_ns(-make_inf()), -LIM62);
    CHECK_I(psytl_ns(make_nan()), 0);

    for (c = -6; c <= -1; c++) {
        const char* s = psytl_strerror(c);
        CHECK(s != NULL);
        if (s) { CHECK(s[0] != '\0'); CHECK(strcmp(s, "ok") != 0); }
    }
    CHECK(strcmp(psytl_strerror(PSYTL_ERR_ARG), psytl_strerror(PSYTL_ERR_ORDER)) != 0);
    CHECK_S(psytl_strerror(0), "ok");
    CHECK_S(psytl_strerror(7), "ok");

    snprintf(buf, sizeof buf, "%d.%d.%d", PSYTL_VERSION_MAJOR, PSYTL_VERSION_MINOR,
             PSYTL_VERSION_PATCH);
    CHECK_S(buf, PSYTL_VERSION_STRING);
    CHECK_S(psytl_version(), PSYTL_VERSION_STRING);
}

/* --------------------------------------------------------- 2. quantization */

static int64_t ceil_div(int64_t a, int64_t b) {   /* b > 0 */
    int64_t q = a / b;
    if (a % b != 0 && a > 0) q++;
    return q;
}

/* The grid frame an event at RT time t lands on: the first k >= 0 with
 * T0 + k P + w >= t. */
static int64_t quant_k(int64_t t, int64_t P, int64_t w) {
    int64_t k = ceil_div(t - w - T0, P);
    return k < 0 ? 0 : k;
}

/* The grid's period P is the frame period in whole ns. Displays now run
 * from 60 Hz to 500 Hz and beyond, and nothing in the header may assume a
 * rate, so the grid checks run at several. */
static void quant_grid(int64_t P, int lead_num, int lead_den, int base, uint64_t seed) {
    enum { NE = 300, NF = 420 };
    static int64_t ev_rt[NE];
    static int ev_id[NE];
    static int cnt[NF];
    const int64_t w = P * lead_num / lead_den;   /* truncated, P > 0 */
    int64_t off = 0;   /* base time = RT + off */
    int i, k, bad_n = 0, bad_order = 0, bad_land = 0, first_bad = -1;

    g_rng = seed;
    CHECK(open_h(&g_tl, g_store, NE + 4, 1, NULL, (double)lead_num / (double)lead_den));
    if (base != 0) {
        CHECK_I(psytl_anchor(&g_tl, base, T0 + 12345, 500 * MS_NS), 0);
        off = 500 * MS_NS - (T0 + 12345);
    }
    memset(cnt, 0, sizeof cnt);
    for (i = 0; i < NE; i++) {
        psytl_event e;
        int64_t t = rnd_range(T0 - P, T0 + 400 * P);
        int64_t g = T0 + (t - T0) / P * P;
        if (i % 10 == 0) t = g;            /* on a frame                 */
        if (i % 10 == 1) t = g + w;        /* on a window edge           */
        if (i % 10 == 2) t = g + w + 1;    /* just past a window edge    */
        ev_rt[i] = t;
        e = ev(base, t + off, PSYTL_MARK, 0, 0.0f);
        e.code = i;
        ev_id[i] = psytl_add(&g_tl, &e);
        CHECK_I(ev_id[i], i);
        k = (int)quant_k(t, P, w);
        if (k < NF) cnt[k]++;
    }
    for (k = 0; k < NF; k++) {
        int64_t onset = T0 + (int64_t)k * P;
        int n = eval_at(&g_tl, onset, P, 1000 + k, g_fired, STORE_CAP), j;
        if (n != cnt[k]) { if (!bad_n) fail_i(__LINE__, "events fired on a grid frame", n, cnt[k]); bad_n++; }
        for (j = 0; j < n && j < STORE_CAP; j++) {
            int src = g_fired[j].code;
            if (src < 0 || src >= NE || quant_k(ev_rt[src], P, w) != k) bad_land++;
            if (j > 0 && report_cmp(&g_fired[j - 1], &g_fired[j]) >= 0) bad_order++;
        }
    }
    for (i = 0; i < NE; i++) {
        const psytl_event* e = psytl_find(&g_tl, ev_id[i]);
        int64_t kk = quant_k(ev_rt[i], P, w), on = T0 + kk * P;
        if (!e || e->frame != 1000 + kk || e->onset != on || e->residual != on - ev_rt[i] ||
            (unsigned)e->flags != PSYTL_EV_FIRED) {
            if (first_bad < 0) first_bad = i;
            bad_land++;
        }
        /* Nearest frame, except events before the first frame. */
        if (e && kk > 0 && lead_num * 2 == lead_den && (e->residual > P / 2 || e->residual < -P / 2)) bad_land++;
    }
    if (first_bad >= 0) {
        const psytl_event* e = psytl_find(&g_tl, ev_id[first_bad]);
        int64_t kk = quant_k(ev_rt[first_bad], P, w);
        fprintf(stderr, "  quant P %lld lead %d/%d base %d: event rt %lld want frame %lld residual %lld; "
                "got frame %lld onset %lld residual %lld flags %u\n",
                (long long)P, lead_num, lead_den, base, (long long)ev_rt[first_bad], (long long)(1000 + kk),
                (long long)(T0 + kk * P - ev_rt[first_bad]),
                e ? (long long)e->frame : -99, e ? (long long)e->onset : -99,
                e ? (long long)e->residual : -99, e ? (unsigned)e->flags : 0u);
    }
    if (bad_land) fail_i(__LINE__, "grid landings that disagree with the model", bad_land, 0);
    if (bad_order) fail_i(__LINE__, "grid reports out of RT target order", bad_order, 0);
    (void)bad_n;
}

/* Period 0: lead does nothing; first onset at or after the event. */
static void quant_vrr(uint64_t seed) {
    enum { NE = 250, NF = 300 };
    static int64_t on[NF], ev_t[NE];
    static int ev_id[NE];
    int i, k, bad = 0;
    g_rng = seed;
    on[0] = T0;
    for (k = 1; k < NF; k++) on[k] = on[k - 1] + rnd_range(4 * MS_NS, 20 * MS_NS);
    CHECK(open_h(&g_tl, g_store, NE + 4, 1, NULL, 0.5));
    for (i = 0; i < NE; i++) {
        psytl_event e;
        ev_t[i] = rnd_range(T0 - 10 * MS_NS, on[NF - 10]);
        if (i % 7 == 0) ev_t[i] = on[rnd_int(NF - 10)];
        e = ev(0, ev_t[i], PSYTL_MARK, 0, 0.0f);
        ev_id[i] = psytl_add(&g_tl, &e);
    }
    for (k = 0; k < NF; k++) eval_at(&g_tl, on[k], 0, k, NULL, 0);
    for (i = 0; i < NE; i++) {
        const psytl_event* e = psytl_find(&g_tl, ev_id[i]);
        int want = 0;
        while (on[want] < ev_t[i]) want++;
        if (!e || e->frame != want || e->onset != on[want] || e->residual != on[want] - ev_t[i]) bad++;
    }
    if (bad) fail_i(__LINE__, "period-0 landings that disagree with the model", bad, 0);
}

/* ---------------------------------------------------------------- 3. bases */

static void test_bases(void) {
    static psytl_key kk[2];
    float init[6] = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
    int64_t t = 0;
    int id_a, id_b, id_s2, k, n, total;

    kk[0] = mkkey(0, 0.0f, PSYTL_EASE_LINEAR);
    kk[1] = mkkey(S_NS, 1000.0f, PSYTL_EASE_LINEAR);

    CHECK(open_h(&g_tl, g_store, 32, 6, init, 0.0));
    CHECK(psytl_base_time(&g_tl, 0, 123, &t));
    CHECK_I(t, 123);
    CHECK(!psytl_base_time(&g_tl, 1, 0, &t));
    CHECK(!psytl_base_time(&g_tl, PSYTL_MAX_BASES, 0, &t));
    CHECK(!psytl_base_time(&g_tl, -1, 0, &t));
    CHECK_I(psytl_anchor(&g_tl, 0, 0, 0), PSYTL_ERR_ARG);
    CHECK_I(psytl_pause(&g_tl, 0, 0), PSYTL_ERR_ARG);
    CHECK_I(psytl_stop(&g_tl, 0), PSYTL_ERR_ARG);
    CHECK_I(psytl_anchor(&g_tl, PSYTL_MAX_BASES, 0, 0), PSYTL_ERR_ARG);
    CHECK_I(psytl_anchor(&g_tl, -1, 0, 0), PSYTL_ERR_ARG);
    CHECK_I(psytl_pause(&g_tl, PSYTL_MAX_BASES, 0), PSYTL_ERR_ARG);
    CHECK_I(psytl_resume(&g_tl, PSYTL_MAX_BASES, 0), PSYTL_ERR_ARG);
    CHECK_I(psytl_stop(&g_tl, PSYTL_MAX_BASES), PSYTL_ERR_ARG);
    /* |base_time| >= 2^62 is rejected. */
    CHECK_I(psytl_anchor(&g_tl, 1, 0, LIM62), PSYTL_ERR_ARG);
    CHECK_I(psytl_anchor(&g_tl, 1, 0, -LIM62), PSYTL_ERR_ARG);
    CHECK_I(psytl_anchor(&g_tl, 1, 0, INT64_MAX), PSYTL_ERR_ARG);
    CHECK(!psytl_base_time(&g_tl, 1, 0, &t));   /* the rejected anchors left it stopped */
    CHECK_I(psytl_anchor(&g_tl, 1, 0, LIM62 - 1), 0);
    CHECK_I(psytl_stop(&g_tl, 1), 0);
    CHECK(psytl_base_time(&g_tl, 0, 5, &t));
    CHECK_I(t, 5);
    CHECK_I(psytl_pause(&g_tl, 1, 0), PSYTL_ERR_ORDER);
    CHECK_I(psytl_resume(&g_tl, 1, 0), PSYTL_ERR_ORDER);
    CHECK_I(psytl_anchor(&g_tl, 1, 1000, 50), 0);
    CHECK(psytl_base_time(&g_tl, 1, 1100, &t));
    CHECK_I(t, 150);
    CHECK(psytl_base_time(&g_tl, 1, 900, &t));
    CHECK_I(t, -50);
    CHECK_I(psytl_resume(&g_tl, 1, 1100), PSYTL_ERR_ORDER);
    CHECK_I(psytl_pause(&g_tl, 1, 1200), 0);
    CHECK(psytl_base_time(&g_tl, 1, 99999, &t));
    CHECK_I(t, 250);
    CHECK_I(psytl_pause(&g_tl, 1, 1300), PSYTL_ERR_ORDER);
    CHECK_I(psytl_resume(&g_tl, 1, 2000), 0);
    CHECK(psytl_base_time(&g_tl, 1, 2100, &t));
    CHECK_I(t, 350);
    CHECK_I(psytl_pause(&g_tl, 1, 2200), 0);
    CHECK_I(psytl_anchor(&g_tl, 1, 3000, 7), 0);   /* from paused */
    CHECK(psytl_base_time(&g_tl, 1, 3010, &t));
    CHECK_I(t, 17);
    CHECK_I(psytl_stop(&g_tl, 1), 0);
    CHECK(!psytl_base_time(&g_tl, 1, 3010, &t));
    CHECK_I(psytl_pause(&g_tl, 1, 3020), PSYTL_ERR_ORDER);
    CHECK_I(psytl_resume(&g_tl, 1, 3020), PSYTL_ERR_ORDER);
    CHECK_I(psytl_anchor(&g_tl, 1, 3000, 7), 0);   /* from stopped */
    CHECK_I(psytl_stop(&g_tl, 1), 0);

    /* A stopped base: nothing fires, no track writes, nothing due. */
    id_a = add1(&g_tl, 2, 0, PSYTL_ONSET, 0, 0.0f);
    CHECK(id_a >= 0);
    CHECK(add1(&g_tl, 2, -1000, PSYTL_MARK, 0, 0.0f) >= 0);
    CHECK_I(psytl_set_keys(&g_tl, 1, 2, kk, 2), 0);
    total = 0;
    for (k = 0; k <= 120; k++) total += eval_at(&g_tl, (int64_t)k * P60, P60, k, NULL, 0);
    CHECK_I(total, 0);
    CHECK(psytl_value(&g_tl, 0) == 0.5f);
    CHECK(psytl_value(&g_tl, 1) == 0.5f);
    CHECK(is_pending_reset(psytl_find(&g_tl, id_a)));
    CHECK(!psytl_next_due(&g_tl, &t));

    /* Stopping holds the channels. */
    CHECK(add1(&g_tl, 1, 0, PSYTL_SET, 2, 5.0f) >= 0);
    CHECK_I(psytl_anchor(&g_tl, 1, 3 * S_NS, 0), 0);
    CHECK_I(eval_at(&g_tl, 3 * S_NS, P60, 1000, NULL, 0), 1);
    CHECK(psytl_value(&g_tl, 2) == 5.0f);
    id_s2 = add1(&g_tl, 1, 10 * MS_NS, PSYTL_SET, 2, 7.0f);
    CHECK_I(psytl_stop(&g_tl, 1), 0);
    total = 0;
    for (k = 1; k <= 20; k++) total += eval_at(&g_tl, 3 * S_NS + k * P60, P60, 1000 + k, NULL, 0);
    CHECK_I(total, 0);
    CHECK(psytl_value(&g_tl, 2) == 5.0f);
    CHECK(is_pending_reset(psytl_find(&g_tl, id_s2)));
    CHECK_I(psytl_set_keys(&g_tl, 3, 3, kk, 2), 0);
    CHECK_I(psytl_anchor(&g_tl, 3, 3500 * MS_NS, 0), 0);
    eval_at(&g_tl, 3500 * MS_NS, P60, 1100, NULL, 0);
    CHECK(psytl_value(&g_tl, 3) == 0.0f);
    eval_at(&g_tl, 3500 * MS_NS + 10 * P60, P60, 1110, NULL, 0);
    CHECK(psytl_value(&g_tl, 3) == (float)ksample(kk, 2, 10 * P60));
    CHECK_I(psytl_stop(&g_tl, 3), 0);
    eval_at(&g_tl, 3500 * MS_NS + 20 * P60, P60, 1120, NULL, 0);
    CHECK(psytl_value(&g_tl, 3) == (float)ksample(kk, 2, 10 * P60));

    /* A paused base evaluates at its frozen time. */
    CHECK_I(psytl_anchor(&g_tl, 4, 4 * S_NS, 0), 0);
    CHECK_I(eval_at(&g_tl, 4 * S_NS, 0, 2000, NULL, 0), 0);
    CHECK_I(psytl_pause(&g_tl, 4, 4500 * MS_NS), 0);
    CHECK_I(psytl_set_keys(&g_tl, 4, 4, kk, 2), 0);
    id_a = add1(&g_tl, 4, 400 * MS_NS, PSYTL_MARK, 0, 0.0f);
    id_b = add1(&g_tl, 4, 600 * MS_NS, PSYTL_MARK, 0, 0.0f);
    n = eval_at(&g_tl, 5 * S_NS, 0, 2001, g_fired, 8);
    CHECK_I(n, 1);
    if (n == 1) {
        CHECK_I(g_fired[0].id, id_a);
        CHECK_I(g_fired[0].residual, 100 * MS_NS);
        CHECK_I(g_fired[0].onset, 5 * S_NS);
        CHECK_I(g_fired[0].frame, 2001);
        CHECK_I(g_fired[0].flags, PSYTL_EV_FIRED);
    }
    CHECK_F(psytl_value(&g_tl, 4), 500.0, 1e-3);
    CHECK_I(eval_at(&g_tl, 6 * S_NS, 0, 2002, NULL, 0), 0);
    CHECK_F(psytl_value(&g_tl, 4), 500.0, 1e-3);
    CHECK(psytl_base_time(&g_tl, 4, 6 * S_NS, &t));
    CHECK_I(t, 500 * MS_NS);
    CHECK_I(psytl_resume(&g_tl, 4, 7 * S_NS), 0);
    CHECK(psytl_base_time(&g_tl, 4, 7100 * MS_NS, &t));
    CHECK_I(t, 600 * MS_NS);
    n = eval_at(&g_tl, 7100 * MS_NS, 0, 2003, g_fired, 8);
    CHECK_I(n, 1);
    if (n == 1) { CHECK_I(g_fired[0].id, id_b); CHECK_I(g_fired[0].residual, 0); }
    CHECK_F(psytl_value(&g_tl, 4), 600.0, 1e-3);
}

/* ------------------------------------------------------------- 4. channels */

static void test_channels(void) {
    static psytl_key kk[2];
    float init[8];
    psytl_event b[4];
    int i, n, next;

    for (i = 0; i < 8; i++) init[i] = 0.25f;
    kk[0] = mkkey(0, 0.0f, PSYTL_EASE_LINEAR);
    kk[1] = mkkey(1000, 1.0f, PSYTL_EASE_LINEAR);
    CHECK(open_h(&g_tl, g_store, 24, 8, init, 0.0));

    CHECK_I(add1(&g_tl, 0, 100, PSYTL_ONSET, 0, 0.0f), 0);
    CHECK_I(add1(&g_tl, 0, 200, PSYTL_OFFSET, 0, 0.0f), 1);
    CHECK_I(add1(&g_tl, 0, 150, PSYTL_SET, 1, 3.5f), 2);
    CHECK_I(eval_at(&g_tl, 50, 0, 0, NULL, 0), 0);
    CHECK(psytl_value(&g_tl, 0) == 0.25f);
    CHECK(psytl_value(&g_tl, 1) == 0.25f);
    CHECK_I(eval_at(&g_tl, 120, 0, 1, NULL, 0), 1);
    CHECK(psytl_value(&g_tl, 0) == 1.0f);
    CHECK_I(eval_at(&g_tl, 160, 0, 2, NULL, 0), 1);
    CHECK(psytl_value(&g_tl, 1) == 3.5f);
    CHECK_I(eval_at(&g_tl, 250, 0, 3, NULL, 0), 1);
    CHECK(psytl_value(&g_tl, 0) == 0.0f);

    /* A late event with an earlier time does not overwrite. */
    CHECK_I(add1(&g_tl, 0, 100, PSYTL_SET, 1, 7.0f), 3);
    n = eval_at(&g_tl, 260, 0, 4, g_fired, 4);
    CHECK_I(n, 1);
    if (n == 1) {
        CHECK_I(g_fired[0].flags, PSYTL_EV_FIRED | PSYTL_EV_LATE);
        CHECK_I(g_fired[0].residual, 160);
    }
    CHECK(psytl_value(&g_tl, 1) == 3.5f);
    /* Same time, later id: it is the latest (time, id). */
    CHECK_I(add1(&g_tl, 0, 150, PSYTL_SET, 1, 9.0f), 4);
    CHECK_I(eval_at(&g_tl, 270, 0, 5, NULL, 0), 1);
    CHECK(psytl_value(&g_tl, 1) == 9.0f);
    CHECK_I(add1(&g_tl, 0, 149, PSYTL_SET, 1, 11.0f), 5);
    CHECK_I(eval_at(&g_tl, 280, 0, 6, NULL, 0), 1);
    CHECK(psytl_value(&g_tl, 1) == 9.0f);

    /* The payload comes back unchanged. */
    b[0] = ev(0, 290, PSYTL_MARK, -7, 1.5f);
    b[0].code = 42;
    b[0].user = UINT64_C(0x0123456789ABCDEF);
    CHECK_I(psytl_add(&g_tl, &b[0]), 6);
    CHECK_I(add1(&g_tl, 0, 295, 200, 12345, -2.0f), 7);
    n = eval_at(&g_tl, 300, 0, 7, g_fired, 4);
    CHECK_I(n, 2);
    if (n == 2) {
        CHECK_I(g_fired[0].id, 6);
        CHECK_I(g_fired[0].target, -7);
        CHECK_I(g_fired[0].code, 42);
        CHECK(g_fired[0].user == UINT64_C(0x0123456789ABCDEF));
        CHECK(g_fired[0].value == 1.5f);
        CHECK_I(g_fired[0].kind, PSYTL_MARK);
        CHECK_I(g_fired[1].id, 7);
        CHECK_I(g_fired[1].kind, 200);
        CHECK_I(g_fired[1].target, 12345);
    }

    /* Validation. */
    CHECK_I(add1(&g_tl, 0, 0, 5, 0, 0.0f), PSYTL_ERR_ARG);
    CHECK_I(add1(&g_tl, 0, 0, 15, 0, 0.0f), PSYTL_ERR_ARG);
    CHECK_I(add1(&g_tl, 0, 1000, 16, 0, 0.0f), 8);
    CHECK_I(add1(&g_tl, 0, 1000, 255, 0, 0.0f), 9);
    CHECK_I(add1(&g_tl, 0, 0, PSYTL_ONSET, 8, 0.0f), PSYTL_ERR_ARG);
    CHECK_I(add1(&g_tl, 0, 0, PSYTL_ONSET, -1, 0.0f), PSYTL_ERR_ARG);
    CHECK_I(add1(&g_tl, 0, 0, PSYTL_SET, 3, (float)make_nan()), PSYTL_ERR_ARG);
    CHECK_I(add1(&g_tl, 0, 0, PSYTL_SET, 3, (float)make_inf()), PSYTL_ERR_ARG);
    CHECK_I(add1(&g_tl, PSYTL_MAX_BASES, 0, PSYTL_MARK, 0, 0.0f), PSYTL_ERR_ARG);
    CHECK_I(add1(&g_tl, 255, 0, PSYTL_MARK, 0, 0.0f), PSYTL_ERR_ARG);
    CHECK_I(add1(&g_tl, 0, 1000, PSYTL_TRIGGER, 999, 0.0f), 10);   /* target is the caller's */

    /* Binding: two bases, events vs keys both ways. */
    CHECK_I(add1(&g_tl, 1, 0, PSYTL_ONSET, 0, 0.0f), PSYTL_ERR_BOUND);
    CHECK_I(add1(&g_tl, 2, 0, PSYTL_SET, 1, 1.0f), PSYTL_ERR_BOUND);
    CHECK_I(psytl_set_keys(&g_tl, 0, 0, kk, 2), PSYTL_ERR_BOUND);
    CHECK_I(psytl_set_keys(&g_tl, 2, 1, kk, 2), 0);
    CHECK_I(add1(&g_tl, 1, 0, PSYTL_ONSET, 2, 0.0f), PSYTL_ERR_BOUND);
    CHECK_I(add1(&g_tl, 0, 0, PSYTL_ONSET, 2, 0.0f), PSYTL_ERR_BOUND);
    /* set_keys replaces keys on any base and rebinds the channel. */
    CHECK_I(psytl_set_keys(&g_tl, 2, 2, kk, 2), 0);
    CHECK_I(add1(&g_tl, 2, 0, PSYTL_ONSET, 2, 0.0f), PSYTL_ERR_BOUND);
    CHECK_I(psytl_anchor(&g_tl, 2, 400, 500), 0);   /* base 1 stays stopped */
    CHECK_I(eval_at(&g_tl, 400, 0, 8, NULL, 0), 0);
    CHECK_F(psytl_value(&g_tl, 2), 0.5, 1e-6);       /* written by base 2 */
    CHECK_I(psytl_stop(&g_tl, 2), 0);

    /* add_n: all or none. */
    n = n_events(&g_tl, PSYTL_ALL_BASES);
    b[0] = ev(0, 1000, PSYTL_MARK, 0, 0.0f);
    b[1] = ev(1, 1000, PSYTL_ONSET, 3, 0.0f);
    b[2] = ev(2, 1000, PSYTL_OFFSET, 3, 0.0f);
    CHECK_I(psytl_add_n(&g_tl, b, 3), PSYTL_ERR_BOUND);
    CHECK_I(n_events(&g_tl, PSYTL_ALL_BASES), n);
    b[1] = ev(0, 1000, 7, 0, 0.0f);
    CHECK_I(psytl_add_n(&g_tl, b, 2), PSYTL_ERR_ARG);
    b[1] = ev(0, 1000, PSYTL_SET, 3, (float)make_nan());
    CHECK_I(psytl_add_n(&g_tl, b, 2), PSYTL_ERR_ARG);
    b[1] = ev(1, 1000, PSYTL_ONSET, 0, 0.0f);   /* ch0 is base 0's */
    CHECK_I(psytl_add_n(&g_tl, b, 2), PSYTL_ERR_BOUND);
    CHECK_I(n_events(&g_tl, PSYTL_ALL_BASES), n);
    CHECK(psytl_add_n(&g_tl, b, -1) < 0);
    CHECK_I(psytl_add_n(&g_tl, b, 0), 11);   /* failed adds consumed no ids */
    b[0] = ev(1, 1000, PSYTL_ONSET, 3, 0.0f);
    b[1] = ev(1, 2000, PSYTL_OFFSET, 3, 0.0f);
    b[2] = ev(3, 5, PSYTL_MARK, 0, 0.0f);
    CHECK_I(psytl_add_n(&g_tl, b, 3), 11);
    CHECK(psytl_find(&g_tl, 11) && psytl_find(&g_tl, 11)->time == 1000 && psytl_find(&g_tl, 11)->kind == PSYTL_ONSET);
    CHECK(psytl_find(&g_tl, 12) && psytl_find(&g_tl, 12)->time == 2000 && psytl_find(&g_tl, 12)->kind == PSYTL_OFFSET);
    CHECK(psytl_find(&g_tl, 13) && psytl_find(&g_tl, 13)->base == 3 && psytl_find(&g_tl, 13)->time == 5);
    CHECK_I(add1(&g_tl, 0, 3000, PSYTL_MARK, 0, 0.0f), 14);
    CHECK(is_pending_reset(psytl_find(&g_tl, 14)));
    check_sorted(&g_tl, __LINE__);

    /* FULL. */
    next = 15;
    while (n_events(&g_tl, PSYTL_ALL_BASES) < 24) {
        CHECK_I(add1(&g_tl, 0, 4000, PSYTL_MARK, 0, 0.0f), next);
        next++;
        if (next > 40) break;
    }
    CHECK_I(add1(&g_tl, 0, 4000, PSYTL_MARK, 0, 0.0f), PSYTL_ERR_FULL);
    CHECK_I(psytl_remove(&g_tl, 15), 0);
    b[0] = ev(0, 1, PSYTL_MARK, 0, 0.0f);
    b[1] = ev(0, 2, PSYTL_MARK, 0, 0.0f);
    CHECK_I(psytl_add_n(&g_tl, b, 2), PSYTL_ERR_FULL);
    CHECK_I(n_events(&g_tl, PSYTL_ALL_BASES), 23);
    CHECK_I(psytl_add_n(&g_tl, b, 1), next);
    check_sorted(&g_tl, __LINE__);
}

/* --------------------------------------------------------------- 5. tracks */

static void test_sample(void) {
    static psytl_key k[16];
    const int64_t M = MS_NS;
    int64_t t;
    int i, j, n, bad = 0;

    k[0] = mkkey(0, 1.0f, PSYTL_EASE_LINEAR);
    k[1] = mkkey(100 * M, 3.0f, PSYTL_EASE_STEP);
    k[2] = mkkey(200 * M, 5.0f, PSYTL_EASE_COSINE);
    k[3] = mkkey(300 * M, 1.0f, PSYTL_EASE_QUAD_IN);
    k[4] = mkkey(400 * M, 9.0f, PSYTL_EASE_QUAD_OUT);
    k[5] = mkkey(500 * M, 2.0f, PSYTL_EASE_LOG);
    k[6] = mkkey(600 * M, 32.0f, PSYTL_EASE_LINEAR);
    for (t = -50 * M; t <= 700 * M; t += 7 * M + 1) {
        double want = ref_sample(k, 7, t), got = ksample(k, 7, t);
        if (!(fabs(got - want) <= tol_of(want))) bad++;
    }
    if (bad) fail_i(__LINE__, "psytl_sample points off the model", bad, 0);
    for (i = 0; i < 7; i++) CHECK(ksample(k, 7, k[i].time) == (double)k[i].value);
    CHECK_F(ksample(k, 7, 50 * M), 2.0, 1e-6);
    CHECK_F(ksample(k, 7, 150 * M), 3.0, 1e-6);
    CHECK_F(ksample(k, 7, 199 * M), 3.0, 1e-6);
    CHECK_F(ksample(k, 7, 250 * M), 3.0, 1e-6);
    CHECK_F(ksample(k, 7, 350 * M), 3.0, 1e-6);
    CHECK_F(ksample(k, 7, 450 * M), 3.75, 1e-6);
    CHECK_F(ksample(k, 7, 550 * M), 8.0, 1e-6);
    CHECK(ksample(k, 7, -1) == 1.0);
    CHECK(ksample(k, 7, INT64_C(1000000000000000000)) == 32.0);
    CHECK(ksample(NULL, 3, 5) == 0.0);
    CHECK(ksample(k, 0, 5) == 0.0);
    /* n < 0 is not a track set_track accepts, and psytl_sample does no
     * validation, so v0.1.0's check of it is gone. */
    CHECK(ksample(k, 1, -5) == 1.0);
    CHECK(ksample(k, 1, 5) == 1.0);

    /* Equal times: a jump; at the shared time the later key. */
    k[0] = mkkey(10, 0.0f, PSYTL_EASE_LINEAR);
    k[1] = mkkey(20, 1.0f, PSYTL_EASE_LINEAR);
    k[2] = mkkey(20, 5.0f, PSYTL_EASE_LINEAR);
    k[3] = mkkey(30, 6.0f, PSYTL_EASE_LINEAR);
    CHECK_F(ksample(k, 4, 19), 0.9, 1e-6);
    CHECK(ksample(k, 4, 20) == 5.0);
    CHECK_F(ksample(k, 4, 25), 5.5, 1e-6);
    CHECK(ksample(k, 4, 30) == 6.0);
    CHECK(ksample(k, 4, 9) == 0.0);
    CHECK(ksample(k, 3, 20) == 5.0);   /* jump at the end */
    CHECK_F(ksample(k, 3, 19), 0.9, 1e-6);

    /* Random tracks of every easing. */
    g_rng = 77;
    bad = 0;
    for (i = 0; i < 300; i++) {
        n = 1 + rnd_int(10);
        t = rnd_range(-100 * M, 100 * M);
        for (j = 0; j < n; j++) {
            if (j > 0 && rnd_int(7) != 0) t += rnd_range(1, 50 * M);
            k[j] = mkkey(t, (float)(0.1 + (double)rnd_int(1000) / 100.0), rnd_int(6));
        }
        for (j = 0; j < 60 + n; j++) {
            int64_t ts = j < n ? k[j].time : rnd_range(k[0].time - 10 * M, k[n - 1].time + 10 * M);
            double want = ref_sample(k, n, ts), got = ksample(k, n, ts);
            if (!(fabs(got - want) <= tol_of(want))) {
                if (!bad) fprintf(stderr, "  sample: n %d t %lld got %.9g want %.9g\n",
                                  n, (long long)ts, got, want);
                bad++;
            }
        }
    }
    if (bad) fail_i(__LINE__, "random psytl_sample points off the model", bad, 0);
}

static void test_tracks(void) {
    static psytl_key ramp[2], bad[3], d[12];
    float init[4] = { 0.75f, 0.25f, -2.0f, 0.0f };
    int64_t onset, bt, prev;
    int k, i, mism = 0;

    CHECK(open_h(&g_tl, g_store, 16, 4, init, 0.5));

    /* Rejections. */
    bad[0] = mkkey(10, 1.0f, PSYTL_EASE_LINEAR); bad[1] = mkkey(5, 2.0f, PSYTL_EASE_LINEAR);
    CHECK_I(psytl_set_keys(&g_tl, 0, 1, bad, 2), PSYTL_ERR_ARG);
    bad[0] = mkkey(0, 1.0f, 7); bad[1] = mkkey(10, 2.0f, PSYTL_EASE_LINEAR);
    CHECK_I(psytl_set_keys(&g_tl, 0, 1, bad, 2), PSYTL_ERR_ARG);
    bad[0] = mkkey(0, (float)make_nan(), PSYTL_EASE_LINEAR);
    CHECK_I(psytl_set_keys(&g_tl, 0, 1, bad, 2), PSYTL_ERR_ARG);
    bad[0] = mkkey(0, 1.0f, PSYTL_EASE_LINEAR); bad[1] = mkkey(10, (float)make_inf(), PSYTL_EASE_LINEAR);
    CHECK_I(psytl_set_keys(&g_tl, 0, 1, bad, 2), PSYTL_ERR_ARG);
    bad[0] = mkkey(0, 0.0f, PSYTL_EASE_LOG); bad[1] = mkkey(10, 1.0f, PSYTL_EASE_LINEAR);
    CHECK_I(psytl_set_keys(&g_tl, 0, 1, bad, 2), PSYTL_ERR_ARG);
    bad[0] = mkkey(0, 1.0f, PSYTL_EASE_LOG); bad[1] = mkkey(10, -1.0f, PSYTL_EASE_LINEAR);
    CHECK_I(psytl_set_keys(&g_tl, 0, 1, bad, 2), PSYTL_ERR_ARG);
    bad[1] = mkkey(10, 0.0f, PSYTL_EASE_LINEAR);
    CHECK_I(psytl_set_keys(&g_tl, 0, 1, bad, 2), PSYTL_ERR_ARG);
    bad[0] = mkkey(0, 1.0f, PSYTL_EASE_LINEAR); bad[1] = mkkey(10, 2.0f, PSYTL_EASE_LINEAR);
    CHECK_I(psytl_set_keys(&g_tl, -1, 1, bad, 2), PSYTL_ERR_ARG);
    CHECK_I(psytl_set_keys(&g_tl, 4, 1, bad, 2), PSYTL_ERR_ARG);
    CHECK_I(psytl_set_keys(&g_tl, 0, PSYTL_MAX_BASES, bad, 2), PSYTL_ERR_ARG);
    CHECK_I(psytl_set_keys(&g_tl, 0, -1, bad, 2), PSYTL_ERR_ARG);
    CHECK_I(psytl_set_keys(&g_tl, 0, 1, bad, -1), PSYTL_ERR_ARG);
    CHECK_I(psytl_set_keys(&g_tl, 0, 1, NULL, 2), PSYTL_ERR_ARG);
    /* Literal: "The last key's ease is not used", so a LOG on the last key
     * with a negative value is no LOG segment. */
    bad[0] = mkkey(0, 1.0f, PSYTL_EASE_LINEAR); bad[1] = mkkey(10, -1.0f, PSYTL_EASE_LOG);
    CHECK_I(psytl_set_keys(&g_tl, 3, 2, bad, 2), 0);
    CHECK_I(psytl_set_keys(&g_tl, 3, 2, NULL, 0), 0);
    /* The rejections left channel 0 free. */
    CHECK_I(psytl_set_keys(&g_tl, 0, 2, bad, 2), 0);
    CHECK_I(psytl_set_keys(&g_tl, 0, 2, NULL, 0), 0);
    CHECK(psytl_value(&g_tl, 0) == 0.75f);

    /* The track writes at the base time of the onset; lead does not apply. */
    ramp[0] = mkkey(0, 0.0f, PSYTL_EASE_LINEAR);
    ramp[1] = mkkey(S_NS, 1000.0f, PSYTL_EASE_LINEAR);
    CHECK_I(psytl_set_keys(&g_tl, 0, 1, ramp, 2), 0);
    CHECK(psytl_value(&g_tl, 0) == 0.75f);   /* changes at the next evaluate */
    CHECK_I(psytl_anchor(&g_tl, 1, 5 * S_NS, 0), 0);
    for (k = 0; k <= 70; k++) {
        onset = 5 * S_NS + k * P60;
        bt = k * P60;
        eval_at(&g_tl, onset, P60, k, NULL, 0);
        if (psytl_value(&g_tl, 0) != (float)ksample(ramp, 2, bt) ||
            !(fabs(psytl_value(&g_tl, 0) - ref_sample(ramp, 2, bt)) <= tol_of(ref_sample(ramp, 2, bt)))) {
            if (!mism) fprintf(stderr, "  ramp: bt %lld got %.9g want %.9g\n", (long long)bt,
                               (double)psytl_value(&g_tl, 0), ref_sample(ramp, 2, bt));
            mism++;
        }
    }
    if (mism) fail_i(__LINE__, "ramp values off the base time of the onset", mism, 0);
    onset = 5 * S_NS + 70 * P60;
    CHECK_I(psytl_set_keys(&g_tl, 0, 1, NULL, 0), 0);
    CHECK(psytl_value(&g_tl, 0) == 0.75f);   /* at once */
    eval_at(&g_tl, onset + P60, P60, 71, NULL, 0);
    CHECK(psytl_value(&g_tl, 0) == 0.75f);

    /* Forward-moving and jumping sequences agree with psytl_sample. */
    g_rng = 99;
    bt = 0;
    for (i = 0; i < 12; i++) {
        if (i > 0) bt += rnd_int(5) == 0 ? 0 : rnd_range(1, 250 * MS_NS);
        d[i] = mkkey(bt, (float)(0.5 + (double)rnd_int(400) / 40.0), rnd_int(6));
    }
    CHECK_I(psytl_set_keys(&g_tl, 1, 2, d, 12), 0);
    onset += 2 * P60;
    CHECK_I(psytl_anchor(&g_tl, 2, onset, -50 * MS_NS), 0);
    mism = 0;
    prev = onset;
    for (k = 0; k < 600; k++) {
        int64_t t = 0;
        onset = prev + rnd_range(0, P60);
        prev = onset;
        eval_at(&g_tl, onset, P60, 100 + k, NULL, 0);
        CHECK(psytl_base_time(&g_tl, 2, onset, &t));
        if (psytl_value(&g_tl, 1) != (float)ksample(d, 12, t)) mism++;
    }
    for (k = 0; k < 300; k++) {
        int64_t t = rnd_range(-100 * MS_NS, d[11].time + 100 * MS_NS);
        onset = prev + rnd_range(1, P60);
        prev = onset;
        CHECK_I(psytl_anchor(&g_tl, 2, onset, t), 0);
        eval_at(&g_tl, onset, P60, 1000 + k, NULL, 0);
        if (psytl_value(&g_tl, 1) != (float)ksample(d, 12, t)) mism++;
        if (!(fabs(psytl_value(&g_tl, 1) - ref_sample(d, 12, t)) <= 1e-5 * (1.0 + fabs(ref_sample(d, 12, t))))) mism++;
    }
    if (mism) fail_i(__LINE__, "keyed channel values off psytl_sample", mism, 0);
}

/* ------------------------------------------------- 5b. sampled tracks */

static float g_big[600000];
static float g_sv[8192];
static int g_cv_prints;

/* A channel's value against the track it stands for, read at base time bt:
 * psytl_sample's value stored as float, and the model's within float
 * rounding. Returns 1 on a mismatch. */
static int chan_vs(int ch, const psytl_track* tr, int64_t bt) {
    float v = psytl_value(&g_tl, ch), s = (float)psytl_sample(tr, bt);
    double want = ref_track(tr, bt);
    if (v != s || !(fabs((double)v - want) <= tol_of(want))) {
        if (g_cv_prints++ < 8)
            fprintf(stderr, "  channel %d at base time %lld: got %.9g, psytl_sample %.9g, model %.9g\n",
                    ch, (long long)bt, (double)v, (double)s, want);
        return 1;
    }
    return 0;
}

/* The base time a track on base b is read at for a frame at `onset` whose
 * lead window is w ns (TRACKS): the window's end for a STEP track, the
 * onset otherwise. A paused base gives its frozen time either way. */
static int64_t read_bt(int b, const psytl_track* tr, int64_t onset, int64_t w) {
    int64_t t = 0;
    (void)psytl_base_time(&g_tl, b, trk_is_step(tr) ? onset + w : onset, &t);
    return t;
}

/* The first whole ns at or after sample i's exact time t0 + i 1e9 / rate. */
static int64_t samp_ceil(int64_t t0, int64_t i, int64_t rate) {
    return t0 + (i * NS_E9 + rate - 1) / rate;
}

static int samp_exact(int64_t i, int64_t rate) { return (i * NS_E9) % rate == 0; }

/* Placement, read through STEP on a table whose sample i is i: sample i
 * shows from the first whole ns at or after its exact time and not 1 ns
 * before. A step rounded to whole ns drifts off this within a few samples
 * at any rate that does not divide 1e9. */
static void sampled_place(int64_t rate, int64_t t0, int n) {
    psytl_track st = trk_samples(g_big, n, t0, rate, PSYTL_INTERP_STEP);
    psytl_track li = trk_samples(g_big, n, t0, rate, PSYTL_INTERP_LINEAR);
    int64_t i, last = samp_ceil(t0, n - 1, rate);
    int bad = 0, bad_m = 0, bad_x = 0, f0 = g_failures;
    for (i = 1; i < n; i++) {
        int64_t e = samp_ceil(t0, i, rate);
        double a = psytl_sample(&st, e), b = psytl_sample(&st, e - 1);
        if (a != (double)i || b != (double)(i - 1)) {
            if (!bad) fprintf(stderr, "  sample %lld (first ns %lld): got %.9g there, %.9g 1 ns before\n",
                              (long long)i, (long long)e, a, b);
            bad++;
        }
        if (a != ref_track(&st, e) || b != ref_track(&st, e - 1)) bad_m++;
        if (samp_exact(i, rate) && psytl_sample(&li, e) != (double)i) bad_x++;
    }
    if (bad) fail_i(__LINE__, "STEP samples off t0 + i 1e9 / rate", bad, 0);
    if (bad_m) fail_i(__LINE__, "STEP samples off the model", bad_m, 0);
    if (bad_x) fail_i(__LINE__, "LINEAR not exact on a sample at a whole ns", bad_x, 0);
    /* Before t0 the first sample; from the last sample's time on, the last. */
    CHECK(psytl_sample(&st, t0 - 1) == 0.0);
    CHECK(psytl_sample(&li, t0 - 1) == 0.0);
    CHECK(psytl_sample(&li, t0 - 10 * S_NS) == 0.0);
    CHECK(psytl_sample(&li, t0) == 0.0);
    CHECK(psytl_sample(&li, last) == (double)(n - 1));
    CHECK(psytl_sample(&st, last) == (double)(n - 1));
    CHECK(psytl_sample(&li, last + 1) == (double)(n - 1));
    CHECK(psytl_sample(&li, last + 1000 * S_NS) == (double)(n - 1));
    CHECK(psytl_sample(&li, last - 1) < (double)(n - 1));
    CHECK(psytl_sample(&st, last - 1) == (double)(n - 2));
    if (g_failures != f0)
        fprintf(stderr, "  in sampled_place(rate %lld, t0 %lld, n %d)\n", (long long)rate, (long long)t0, n);
}

/* LINEAR, STEP and CUBIC against the model at random times, on samples,
 * 1 ns before them, half way, and next to both ends. */
static void sampled_interp(int64_t rate, int64_t t0, int n, uint64_t seed) {
    int64_t last = samp_ceil(t0, n - 1, rate), step = NS_E9 / rate + 1;
    int interp, j, f0 = g_failures;
    g_rng = seed;
    for (j = 0; j < n; j++) g_sv[j] = (float)(rnd_int(2001) - 1000) / 64.0f;
    for (interp = 0; interp < 3; interp++) {
        psytl_track tr = trk_samples(g_sv, n, t0, rate, interp);
        int bad = 0, bad_on = 0;
        for (j = 0; j < 5000; j++) {
            int64_t i = rnd_range(0, n - 1), t;
            double got, want;
            switch (j % 5) {
            case 0: t = rnd_range(t0 - S_NS, last + S_NS); break;
            case 1: t = samp_ceil(t0, i, rate); break;
            case 2: t = samp_ceil(t0, i, rate) - 1; break;
            case 3: t = t0 + (2 * i + 1) * NS_E9 / (2 * rate); break;
            default:
                t = rnd_int(2) ? t0 + rnd_range(-2, 2 * step + 2) : last + rnd_range(-2 * step - 2, 2);
                break;
            }
            got = psytl_sample(&tr, t);
            want = ref_track(&tr, t);
            if (!(fabs(got - want) <= tol_d(want))) {
                if (!bad) fprintf(stderr, "  interp %d t %lld: got %.17g want %.17g\n", interp, (long long)t, got, want);
                bad++;
            }
            if (j % 5 == 1 && samp_exact(i, rate) && got != (double)g_sv[i]) bad_on++;
        }
        if (bad) fail_i(__LINE__, "sampled values off the model", bad, 0);
        if (bad_on) fail_i(__LINE__, "sampled value not exact on a sample", bad_on, 0);
    }
    if (g_failures != f0)
        fprintf(stderr, "  in sampled_interp(rate %lld, t0 %lld, n %d)\n", (long long)rate, (long long)t0, n);
}

static void test_sampled(void) {
    /* 120, 7 and 999983 (prime) do not divide 1e9; 1e9 is a sample per ns. */
    static const int64_t rates[6] = { 120, 7, 999983, INT64_C(1000000000), 1, 60 };
    static const int ns[6] = { 600000, 3000, 200000, 200000, 100, 5000 };
    static const int64_t t0s[3] = { 0, -5 * PSYTL_NS_PER_S + 3, 777777777 };
    int r, z, i;
    for (i = 0; i < 600000; i++) g_big[i] = (float)i;
    for (r = 0; r < 6; r++)
        for (z = 0; z < 3; z++) sampled_place(rates[r], t0s[z], ns[r]);
    for (r = 0; r < 6; r++)
        for (z = 0; z < 3; z++)
            sampled_interp(rates[r], t0s[z], ns[r] < 8192 ? ns[r] : 8192, 1000 + 10 * (uint64_t)r + (uint64_t)z);
}

static void test_sampled_cubic(void) {
    static float q[4] = { 0.0f, 1.0f, 4.0f, 9.0f }, two[2] = { 3.0f, -5.0f }, one[1] = { 2.5f };
    const int64_t T = -S_NS, M = MS_NS;
    psytl_track c = trk_samples(q, 4, T, 10, PSYTL_INTERP_CUBIC), l;
    int64_t t;
    int interp, bad = 0;

    /* By hand: v = i^2, which Catmull-Rom reproduces inside; the linear
     * phantoms (-1 before, 14 after) bend the end segments: 0.375 and
     * 6.375, where repeated ends would give 0.3125 and 6.3125. */
    CHECK_F(psytl_sample(&c, T + 50 * M), 0.375, 1e-12);
    CHECK_F(psytl_sample(&c, T + 150 * M), 2.25, 1e-12);
    CHECK_F(psytl_sample(&c, T + 250 * M), 6.375, 1e-12);
    CHECK(psytl_sample(&c, T + 100 * M) == 1.0);
    CHECK(psytl_sample(&c, T + 200 * M) == 4.0);
    CHECK(psytl_sample(&c, T + 300 * M) == 9.0);
    CHECK(psytl_sample(&c, T - 1) == 0.0);
    CHECK(psytl_sample(&c, T + 10 * S_NS) == 9.0);
    /* The model gives the same by-hand values. */
    CHECK_F(ref_track(&c, T + 50 * M), 0.375, 1e-12);
    CHECK_F(ref_track(&c, T + 150 * M), 2.25, 1e-12);
    CHECK_F(ref_track(&c, T + 250 * M), 6.375, 1e-12);

    /* Two samples: CUBIC is LINEAR. */
    c = trk_samples(two, 2, T, 3, PSYTL_INTERP_CUBIC);
    l = trk_samples(two, 2, T, 3, PSYTL_INTERP_LINEAR);
    for (t = T - 10; t <= T + S_NS / 3 + 10; t += 1234567) {
        double a = psytl_sample(&c, t), b = psytl_sample(&l, t);
        if (!(fabs(a - b) <= 1e-12 * (1.0 + fabs(b)))) bad++;
    }
    for (t = T + S_NS / 3 - 3; t <= T + S_NS / 3 + 3; t++)
        if (!(fabs(psytl_sample(&c, t) - psytl_sample(&l, t)) <= 1e-12)) bad++;
    if (bad) fail_i(__LINE__, "two-sample CUBIC differs from LINEAR", bad, 0);
    /* T + S / 6 is 166666666 ns: u = 0.499999998, not 0.5. */
    CHECK_F(psytl_sample(&c, T + S_NS / 6), 3.0 - 8.0 * 0.499999998, 1e-12);

    /* One sample: that value everywhere. */
    for (interp = 0; interp < 3; interp++) {
        psytl_track s = trk_samples(one, 1, T, 7, interp);
        CHECK(psytl_sample(&s, T - 1) == 2.5);
        CHECK(psytl_sample(&s, T) == 2.5);
        CHECK(psytl_sample(&s, T + 1) == 2.5);
        CHECK(psytl_sample(&s, T + 100 * S_NS) == 2.5);
    }
}

/* USAGE's 10-minute target and more: 600000 samples at 120/s (83 min). */
static void test_long_table(void) {
    const int N = 600000;
    const int64_t rate = 120, t0 = -7 * S_NS + 3, R = S_NS, P = 2 * MS_NS;
    int64_t last, t, onset, idx = 0, bt0;
    int i, interp, k, bad = 0, bad_e = 0;
    psytl_track tr;

    for (i = 0; i < N; i++)
        g_big[i] = (float)(40.0 * sin(0.0131 * (double)i) + 25.0 * cos(0.00377 * (double)i + 1.0) +
                           3.0 * sin(0.21 * (double)i));
    last = samp_ceil(t0, N - 1, rate);
    g_rng = 4242;
    for (interp = 0; interp < 3; interp++) {
        tr = trk_samples(g_big, N, t0, rate, interp);
        for (k = 0; k < 20000; k++) {
            double got, want;
            t = rnd_range(t0 - S_NS, last + S_NS);
            got = psytl_sample(&tr, t);
            want = ref_track(&tr, t);
            if (!(fabs(got - want) <= tol_d(want))) {
                if (!bad) fprintf(stderr, "  long table interp %d t %lld: got %.17g want %.17g\n",
                                  interp, (long long)t, got, want);
                bad++;
            }
        }
        for (t = t0 - S_NS; t <= last + S_NS; t += 7777777) {
            double got = psytl_sample(&tr, t), want = ref_track(&tr, t);
            if (!(fabs(got - want) <= tol_d(want))) {
                if (!bad) fprintf(stderr, "  long table interp %d t %lld: got %.17g want %.17g\n",
                                  interp, (long long)t, got, want);
                bad++;
            }
        }
    }
    if (bad) fail_i(__LINE__, "long-table values off the model", bad, 0);

    /* Through evaluate: forward at 500 Hz from before t0, forward past the
     * end, then jumps. */
    tr = trk_samples(g_big, N, t0, rate, PSYTL_INTERP_CUBIC);
    CHECK(open_h(&g_tl, g_store, 4, 1, NULL, 0.5));
    CHECK_I(psytl_set_track(&g_tl, 0, 1, &tr), 0);
    bt0 = t0 - 100 * MS_NS;
    CHECK_I(psytl_anchor(&g_tl, 1, R, bt0), 0);
    onset = R;
    for (k = 0; k < 30000; k++, onset += P) {
        eval_at(&g_tl, onset, P, idx++, NULL, 0);
        bad_e += chan_vs(0, &tr, bt0 + (onset - R));
    }
    bt0 = last - 30 * S_NS;
    CHECK_I(psytl_anchor(&g_tl, 1, onset, bt0), 0);
    for (k = 0; k < 30000; k++, onset += P) {
        eval_at(&g_tl, onset, P, idx++, NULL, 0);
        bad_e += chan_vs(0, &tr, bt0 + (int64_t)k * P);
    }
    CHECK(psytl_value(&g_tl, 0) == g_big[N - 1]);
    for (k = 0; k < 3000; k++, onset += P) {
        bt0 = rnd_range(t0 - S_NS, last + S_NS);
        CHECK_I(psytl_anchor(&g_tl, 1, onset, bt0), 0);
        eval_at(&g_tl, onset, P, idx++, NULL, 0);
        bad_e += chan_vs(0, &tr, bt0);
    }
    if (bad_e) fail_i(__LINE__, "long-table channel values off psytl_sample", bad_e, 0);
}

/* ------------------------------------------------------- 5c. REPEAT */

static void test_repeat(void) {
    static psytl_key k[4];
    static float sv[10];
    const int64_t M = MS_NS, ST = 1000, T = -3 * S_NS;
    psytl_track tr;
    int i, bad = 0, bad_hi = 0, rep;

    /* Keyed: 0 -> 10 over 100 ms, 10 until 150 ms, then 4 toward 20 at
     * 400 ms; period 200 ms. The value at start + period is
     * 4 + 16 * 50 / 250 = 7.2; the key at 400 ms is never reached. */
    k[0] = mkkey(ST, 0.0f, PSYTL_EASE_LINEAR);
    k[1] = mkkey(ST + 100 * M, 10.0f, PSYTL_EASE_STEP);
    k[2] = mkkey(ST + 150 * M, 4.0f, PSYTL_EASE_LINEAR);
    k[3] = mkkey(ST + 400 * M, 20.0f, PSYTL_EASE_LINEAR);
    tr = trk_keys(k, 4);
    tr.period = 200 * M;
    tr.repeats = 3;
    /* Before the start, no repeat: the first key's value. */
    CHECK(psytl_sample(&tr, ST - 1) == 0.0);
    CHECK(psytl_sample(&tr, -S_NS) == 0.0);
    CHECK(psytl_sample(&tr, ST) == 0.0);
    CHECK_F(psytl_sample(&tr, ST + 50 * M), 5.0, 1e-12);
    CHECK(psytl_sample(&tr, ST + 120 * M) == 10.0);
    CHECK_F(psytl_sample(&tr, ST + 199 * M), 4.0 + 16.0 * 49.0 / 250.0, 1e-9);
    CHECK(psytl_sample(&tr, ST + 200 * M) == 0.0);   /* the second cycle */
    CHECK_F(psytl_sample(&tr, ST + 250 * M), 5.0, 1e-12);
    CHECK(psytl_sample(&tr, ST + 520 * M) == 10.0);
    CHECK_F(psytl_sample(&tr, ST + 600 * M - 1), 4.0 + 16.0 * (double)(50 * M - 1) / (double)(250 * M), 1e-9);
    /* After 3 cycles: the value at start + period, read without folding. */
    CHECK_F(psytl_sample(&tr, ST + 600 * M), 7.2, 1e-6);
    CHECK_F(psytl_sample(&tr, ST + 600 * M + 1), 7.2, 1e-6);
    CHECK_F(psytl_sample(&tr, ST + 10 * S_NS), 7.2, 1e-6);
    CHECK_F(psytl_sample(&tr, INT64_C(1000000000000000)), 7.2, 1e-6);
    CHECK_F(ref_track(&tr, ST + 600 * M), 7.2, 1e-6);
    CHECK_F(ref_track(&tr, ST + 10 * S_NS), 7.2, 1e-6);
    tr.repeats = 1;
    CHECK_F(psytl_sample(&tr, ST + 200 * M - 1), 4.0 + 16.0 * (double)(50 * M - 1) / (double)(250 * M), 1e-9);
    CHECK_F(psytl_sample(&tr, ST + 200 * M), 7.2, 1e-6);
    CHECK_F(ref_track(&tr, ST + 200 * M), 7.2, 1e-6);
    /* repeats 0: forever. */
    tr.repeats = 0;
    CHECK_F(psytl_sample(&tr, ST + 1000 * 200 * M + 50 * M), 5.0, 1e-12);
    CHECK(psytl_sample(&tr, ST + INT64_C(123456789) * 200 * M + 120 * M) == 10.0);
    CHECK(psytl_sample(&tr, ST - 1) == 0.0);
    /* Random times, three counts; a key past start + period never shows. */
    g_rng = 31337;
    for (rep = 0; rep < 3; rep++) {
        tr.repeats = rep == 0 ? 0 : rep == 1 ? 3 : 1;
        for (i = 0; i < 5000; i++) {
            int64_t t = i % 2 ? rnd_range(ST - S_NS, ST + 2 * S_NS) : rnd_range(ST, ST + INT64_C(1000000000000));
            double got = psytl_sample(&tr, t), want = ref_track(&tr, t);
            if (!(fabs(got - want) <= tol_d(want))) {
                if (!bad) fprintf(stderr, "  repeat keyed t %lld: got %.17g want %.17g\n", (long long)t, got, want);
                bad++;
            }
            if (got > 10.0) bad_hi++;
        }
    }
    if (bad) fail_i(__LINE__, "repeated keyed values off the model", bad, 0);
    if (bad_hi) fail_i(__LINE__, "a key past start + period was reached", bad_hi, 0);

    /* Sampled, sample i = i at 100/s from t0 = -3 s; period 55 ms, which is
     * not a whole number of samples. The value at start + period is 5.5. */
    for (i = 0; i < 10; i++) sv[i] = (float)i;
    tr = trk_samples(sv, 10, T, 100, PSYTL_INTERP_LINEAR);
    tr.period = 55 * M;
    tr.repeats = 4;
    CHECK(psytl_sample(&tr, T - 1) == 0.0);
    CHECK_F(psytl_sample(&tr, T + 23 * M), 2.3, 1e-9);
    CHECK_F(psytl_sample(&tr, T + 55 * M + 23 * M), 2.3, 1e-9);
    CHECK(psytl_sample(&tr, T + 2 * 55 * M) == 0.0);
    CHECK_F(psytl_sample(&tr, T + 3 * 55 * M + 54 * M), 5.4, 1e-9);
    CHECK_F(psytl_sample(&tr, T + 4 * 55 * M - 1), 5.4999999, 1e-9);
    CHECK_F(psytl_sample(&tr, T + 4 * 55 * M), 5.5, 1e-9);
    CHECK_F(psytl_sample(&tr, T + 4 * 55 * M + 1), 5.5, 1e-9);
    CHECK_F(psytl_sample(&tr, T + 100 * S_NS), 5.5, 1e-9);
    CHECK_F(ref_track(&tr, T + 4 * 55 * M), 5.5, 1e-9);
    tr.repeats = 0;
    CHECK_F(psytl_sample(&tr, T + 1000 * 55 * M + 23 * M), 2.3, 1e-9);
    /* A period longer than the table: the last sample until the wrap. */
    tr.period = 200 * M;
    CHECK(psytl_sample(&tr, T + 150 * M) == 9.0);
    CHECK_F(psytl_sample(&tr, T + 200 * M + 45 * M), 4.5, 1e-9);
    tr.repeats = 2;
    CHECK(psytl_sample(&tr, T + 400 * M) == 9.0);
    CHECK(psytl_sample(&tr, T + 400 * M - 1) == 9.0);
    CHECK_F(psytl_sample(&tr, T + 200 * M + 1), 1e-7, 1e-12);
    /* CUBIC and STEP with a repeat, at random. */
    bad = 0;
    for (rep = 0; rep < 4; rep++) {
        tr = trk_samples(sv, 10, T, 100, rep % 2 ? PSYTL_INTERP_CUBIC : PSYTL_INTERP_STEP);
        tr.period = rep < 2 ? 37 * M + 1 : 333 * M;
        tr.repeats = rep;
        for (i = 0; i < 5000; i++) {
            int64_t t = rnd_range(T - 50 * M, T + 3 * S_NS);
            double got = psytl_sample(&tr, t), want = ref_track(&tr, t);
            if (!(fabs(got - want) <= tol_d(want))) {
                if (!bad) fprintf(stderr, "  repeat sampled t %lld: got %.17g want %.17g\n", (long long)t, got, want);
                bad++;
            }
        }
    }
    if (bad) fail_i(__LINE__, "repeated sampled values off the model", bad, 0);
}

/* USAGE's 7.5 Hz flicker for 10 minutes then off, through evaluate at a
 * display rate of hz, with frames at the exact multiples of 1/hz rounded
 * to ns and then moved by up to +-jit ns, as a predicted onset is. It is a
 * STEP track, so it is read at the end of the lead window: each frame
 * against the square wave there. At 60 Hz the edges fall on frame onsets;
 * with lock set, frame k must show on exactly when k mod 8 < 4. */
static int64_t flicker_run(int64_t hz, int lead_num, int lead_den, int64_t bt_first, int64_t jit, int lock) {
    static psytl_key sq[2];
    const int64_t R = S_NS, P = (NS_E9 + hz / 2) / hz, w = P * lead_num / lead_den;
    const int64_t FP = 4 * S_NS / 30, HALF = 2 * S_NS / 30, END = 4500 * FP;
    psytl_track tr;
    int64_t k, bad = 0, bad_s = 0, bad_m = 0, bad_lock = 0, n_on = 0, n_off = 0;
    int f0 = g_failures;

    sq[0] = mkkey(0, 1.0f, PSYTL_EASE_STEP);
    sq[1] = mkkey(HALF, 0.0f, PSYTL_EASE_STEP);
    tr = trk_keys(sq, 2);
    tr.period = FP;
    tr.repeats = 4500;
    CHECK(open_h(&g_tl, g_store, 4, 1, NULL, (double)lead_num / (double)lead_den));
    CHECK_I(psytl_set_track(&g_tl, 0, 1, &tr), 0);
    CHECK_I(psytl_anchor(&g_tl, 1, R, bt_first), 0);
    for (k = 0; ; k++) {
        int64_t onset = R + (k * NS_E9 + hz / 2) / hz + (jit ? rnd_range(-jit, jit) : 0);
        int64_t rd = bt_first + (onset - R) + w;   /* the window's end on the base */
        float v, want;
        if (rd > END + S_NS) break;
        eval_at(&g_tl, onset, P, k, NULL, 0);
        v = psytl_value(&g_tl, 0);
        want = rd < 0 ? 1.0f : rd >= END ? 0.0f : (rd % FP < HALF ? 1.0f : 0.0f);
        if (v != want) {
            if (!bad) fprintf(stderr, "  flicker frame %lld read at %lld: got %g want %g\n",
                              (long long)k, (long long)rd, (double)v, (double)want);
            bad++;
        }
        if (v != (float)psytl_sample(&tr, rd)) bad_s++;
        if (v != (float)ref_track(&tr, rd)) bad_m++;
        if (lock && rd < END - P && v != ((k % 8) < 4 ? 1.0f : 0.0f)) bad_lock++;
        if (v != 0.0f) n_on++; else n_off++;
    }
    if (bad) fail_i(__LINE__, "flicker frames off the square wave at the window's end", bad, 0);
    if (bad_s) fail_i(__LINE__, "flicker frames off psytl_sample at the window's end", bad_s, 0);
    if (bad_m) fail_i(__LINE__, "flicker frames off the model", bad_m, 0);
    if (lock && bad_lock) fail_i(__LINE__, "60 Hz flicker frames off frame k mod 8 < 4", bad_lock, 0);
    if (n_on < 1000 || n_off < 1000) fail_i(__LINE__, "flicker: too few on or off frames", n_on < n_off ? n_on : n_off, 1000);
    if (g_failures != f0)
        fprintf(stderr, "  in flicker_run(%lld Hz, lead %d/%d, first base time %lld, jitter %lld)\n",
                (long long)hz, lead_num, lead_den, (long long)bt_first, (long long)jit);
    return bad_lock;
}

static void test_flicker(void) {
    int64_t wrong;
    g_rng = 7575;
    (void)flicker_run(60, 1, 2, 0, 1000, 1);
    (void)flicker_run(60, 1, 2, 0, 0, 1);
    (void)flicker_run(144, 1, 4, 0, 300, 0);
    (void)flicker_run(240, 1, 2, -37 * MS_NS, 300, 0);
    (void)flicker_run(500, 0, 1, -37 * MS_NS, 100, 0);
    /* Lead 0 reads the STEP track at the onset too, so with onset noise
     * the frames next to an edge fall on either side of it (the manual's
     * reason for reading at the window's end). Not a failure of the
     * header: a check that lead 0 does read at the onset. */
    wrong = 0;
    {
        static psytl_key sq[2];
        const int64_t FP = 4 * S_NS / 30, HALF = 2 * S_NS / 30;
        psytl_track tr;
        int64_t k;
        sq[0] = mkkey(0, 1.0f, PSYTL_EASE_STEP);
        sq[1] = mkkey(HALF, 0.0f, PSYTL_EASE_STEP);
        tr = trk_keys(sq, 2);
        tr.period = FP;
        CHECK(open_h(&g_tl, g_store, 4, 1, NULL, 0.0));
        CHECK_I(psytl_set_track(&g_tl, 0, 1, &tr), 0);
        CHECK_I(psytl_anchor(&g_tl, 1, S_NS, 0), 0);
        for (k = 0; k < 36000; k++) {
            int64_t onset = S_NS + (k * NS_E9 + 30) / 60 + rnd_range(-1000, 1000);
            eval_at(&g_tl, onset, P60, k, NULL, 0);
            if (psytl_value(&g_tl, 0) != ((k % 8) < 4 ? 1.0f : 0.0f)) wrong++;
            if (psytl_value(&g_tl, 0) != (float)psytl_sample(&tr, onset - S_NS)) {
                fail(__LINE__, "lead 0: STEP track not read at the onset");
                break;
            }
        }
    }
    if (wrong == 0) fail(__LINE__, "lead 0 with onset noise: no frame off the frame-locked wave (vacuous)");
}

/* -------------------------------------------------------- 5d. BEZIER */

static const psytl_curve g_bz[10] = {
    { 0.0f, 0.0f, 1.0f, 1.0f },     /* CSS linear: y(s) = x(s)            */
    { 0.42f, 0.0f, 0.58f, 1.0f },   /* CSS ease-in-out                    */
    { 0.25f, 0.1f, 0.25f, 1.0f },   /* CSS ease                           */
    { 0.68f, -0.6f, 0.32f, 1.6f },  /* overshoots both keys               */
    { 0.0f, 1.0f, 1.0f, 0.0f },     /* x at the limits                    */
    { 1.0f, 0.0f, 0.0f, 1.0f },     /* x at the limits, the other way     */
    { 0.0f, 0.0f, 0.0f, 0.0f },     /* y(s) = x(s) = s^3                  */
    { 1.0f, 1.0f, 1.0f, 1.0f },     /* y(s) = x(s), x flat at s = 1       */
    { 0.0f, 0.5f, 0.0f, 1.0f },     /* x = s^3: steep at the start        */
    { 1.0f, 0.0f, 1.0f, 0.3f },     /* x flat at the end                  */
};

static void test_bezier(void) {
    static psytl_key k[2], k2[2];
    int c, j, bad = 0, bad_lin = 0, bad2 = 0;
    double ymin = 1.0, ymax = 0.0;
    g_rng = 606;
    for (c = 0; c < 10; c++) {
        psytl_track tr, tr2;
        k[0] = mkbez(0, 0.0f, c);
        k[1] = mkkey(S_NS, 1.0f, PSYTL_EASE_LINEAR);
        k2[0] = mkbez(0, 2.0f, c);
        k2[1] = mkkey(S_NS, -3.0f, PSYTL_EASE_LINEAR);
        tr = trk_keys(k, 2);
        tr.curves = g_bz;
        tr.n_curves = 10;
        tr2 = tr;
        tr2.keys = k2;
        CHECK(psytl_sample(&tr, 0) == 0.0);
        CHECK(psytl_sample(&tr, S_NS) == 1.0);
        CHECK(psytl_sample(&tr2, -1) == 2.0);
        for (j = 0; j <= 3000; j++) {
            int64_t t = j <= 1000 ? (int64_t)j * (S_NS / 1000) : j <= 1100 ? (int64_t)(j - 1000) : rnd_range(0, S_NS - 1);
            double u = (double)t / 1e9, got = psytl_sample(&tr, t), got2 = psytl_sample(&tr2, t), lo, hi;
            /* "The y is found to 1e-12 in x": any y within that, with a
             * little room for rounding in x. */
            bez_range(&g_bz[c], u, 1.5e-12, &lo, &hi);
            if (!(got >= lo - 1e-12 && got <= hi + 1e-12)) {
                if (!bad) fprintf(stderr, "  bezier %d u %.17g: got %.17g, y in [%.17g, %.17g]\n", c, u, got, lo, hi);
                bad++;
            }
            if (!(got2 >= 2.0 - 5.0 * hi - 1e-11 && got2 <= 2.0 - 5.0 * lo + 1e-11)) bad2++;
            if ((c == 0 || c == 6 || c == 7) && !(fabs(got - u) <= 1e-11)) {
                if (!bad_lin) fprintf(stderr, "  bezier %d (linear) u %.17g: got %.17g\n", c, u, got);
                bad_lin++;
            }
            if (c == 3) {
                if (got < ymin) ymin = got;
                if (got > ymax) ymax = got;
            }
        }
    }
    if (bad) fail_i(__LINE__, "BEZIER y off the bisection model", bad, 0);
    if (bad2) fail_i(__LINE__, "BEZIER value off v0 + (v1 - v0) y", bad2, 0);
    if (bad_lin) fail_i(__LINE__, "cubic-bezier(0,0,1,1) and y = x curves differ from LINEAR", bad_lin, 0);
    CHECK(ymin < -0.05);
    CHECK(ymax > 1.05);
    /* By hand: ease-in-out is 0.5 at u = 0.5, near enough: 0.42f + 0.58f
     * is not 1 in float, so the curve is not quite symmetric. */
    k[0] = mkbez(0, 0.0f, 1);
    k[1] = mkkey(S_NS, 1.0f, PSYTL_EASE_LINEAR);
    {
        psytl_track tr = trk_keys(k, 2);
        tr.curves = g_bz;
        tr.n_curves = 10;
        CHECK_F(psytl_sample(&tr, S_NS / 2), 0.5, 1e-6);
        CHECK_F(ref_track(&tr, S_NS / 2), 0.5, 1e-6);
        /* And the overshoot curve below 0 early, as y1 < 0 makes it. */
        k[0] = mkbez(0, 0.0f, 3);
        CHECK(psytl_sample(&tr, S_NS / 5) < 0.0);
        CHECK(ref_track(&tr, S_NS / 5) < 0.0);
        CHECK_F(ref_track(&tr, S_NS / 5), psytl_sample(&tr, S_NS / 5), 1e-9);
    }
}

/* ------------------------------------------------- 5e. set_track rules */

#define REJ_TRACK(trv) CHECK_I(psytl_set_track(&g_tl, 0, 1, &(trv)), PSYTL_ERR_ARG)
#define ACC_TRACK(trv) CHECK_I(psytl_set_track(&g_tl, 3, 1, &(trv)), 0)

static void test_set_track(void) {
    static psytl_key k[3], kb[3];
    static float s[4] = { 1.0f, 2.0f, 3.0f, 4.0f }, sb[4];
    static psytl_curve cv[2], cb[2];
    float init[4] = { 0.5f, -0.5f, 2.0f, 0.0f };
    psytl_track gk, gs, b;
    psytl_tween_desc td;
    int id;

    CHECK(open_h(&g_tl, g_store, 16, 4, init, 0.0));
    k[0] = mkkey(0, 1.0f, PSYTL_EASE_LINEAR);
    k[1] = mkkey(10, 2.0f, PSYTL_EASE_LINEAR);
    k[2] = mkkey(20, 3.0f, PSYTL_EASE_LINEAR);
    cv[0] = mkcurve(0.42f, 0.0f, 0.58f, 1.0f);
    cv[1] = mkcurve(0.25f, 0.1f, 0.25f, 1.0f);
    gk = trk_keys(k, 3);
    gs = trk_samples(s, 4, 0, 100, PSYTL_INTERP_LINEAR);
    ACC_TRACK(gk);   /* a keyed track's rate 0 is not a sampled rate */
    ACC_TRACK(gs);

    /* Neither form, or both. */
    memset(&b, 0, sizeof b); REJ_TRACK(b);
    b = gk; b.keys = NULL; REJ_TRACK(b);
    b = gk; b.n_keys = 0; REJ_TRACK(b);        /* literal: no keys is not the keyed form */
    b = gs; b.samples = NULL; REJ_TRACK(b);
    b = gs; b.n_samples = 0; REJ_TRACK(b);
    b = gk; b.samples = s; b.n_samples = 4; b.rate = 100; REJ_TRACK(b);
    /* Negative counts. */
    b = gk; b.n_keys = -1; REJ_TRACK(b);
    b = gs; b.n_samples = -1; REJ_TRACK(b);
    b = gk; b.curves = cv; b.n_curves = -1; REJ_TRACK(b);
    /* Keys: order, ease, value, LOG. */
    b = trk_keys(kb, 3);
    memcpy(kb, k, sizeof kb); kb[2].time = 5; REJ_TRACK(b);
    memcpy(kb, k, sizeof kb); kb[0].ease = 7; REJ_TRACK(b);
    memcpy(kb, k, sizeof kb); kb[1].ease = 255; REJ_TRACK(b);
    memcpy(kb, k, sizeof kb); kb[2].ease = 7; REJ_TRACK(b);   /* the last key's too */
    memcpy(kb, k, sizeof kb); kb[0].value = (float)make_nan(); REJ_TRACK(b);
    memcpy(kb, k, sizeof kb); kb[1].value = (float)-make_inf(); REJ_TRACK(b);
    memcpy(kb, k, sizeof kb); kb[2].value = (float)make_inf(); REJ_TRACK(b);
    memcpy(kb, k, sizeof kb); kb[0].ease = PSYTL_EASE_LOG; kb[0].value = 0.0f; REJ_TRACK(b);
    memcpy(kb, k, sizeof kb); kb[0].ease = PSYTL_EASE_LOG; kb[1].value = -1.0f; REJ_TRACK(b);
    memcpy(kb, k, sizeof kb); kb[1].ease = PSYTL_EASE_LOG; kb[2].value = 0.0f; REJ_TRACK(b);
    memcpy(kb, k, sizeof kb); kb[2].ease = PSYTL_EASE_LOG; kb[2].value = -1.0f; ACC_TRACK(b);
    /* Samples. */
    b = gs; b.samples = sb;
    memcpy(sb, s, sizeof sb); sb[0] = (float)make_nan(); REJ_TRACK(b);
    memcpy(sb, s, sizeof sb); sb[3] = (float)make_inf(); REJ_TRACK(b);
    memcpy(sb, s, sizeof sb); sb[1] = (float)-make_inf(); REJ_TRACK(b);
    memcpy(sb, s, sizeof sb); ACC_TRACK(b);
    /* Curves: y finite, x in [0, 1]. */
    memcpy(kb, k, sizeof kb);
    kb[0] = mkbez(0, 1.0f, 1);
    b = trk_keys(kb, 3);
    b.curves = cb;
    b.n_curves = 2;
    memcpy(cb, cv, sizeof cb); ACC_TRACK(b);
    memcpy(cb, cv, sizeof cb); cb[1].y1 = (float)make_nan(); REJ_TRACK(b);
    memcpy(cb, cv, sizeof cb); cb[1].y2 = (float)make_inf(); REJ_TRACK(b);
    memcpy(cb, cv, sizeof cb); cb[1].x1 = -0.25f; REJ_TRACK(b);
    memcpy(cb, cv, sizeof cb); cb[1].x2 = 1.25f; REJ_TRACK(b);
    memcpy(cb, cv, sizeof cb); cb[1].x1 = (float)make_nan(); REJ_TRACK(b);
    memcpy(cb, cv, sizeof cb); cb[1].x2 = 1.0000001f; REJ_TRACK(b);
    memcpy(cb, cv, sizeof cb); cb[1].x1 = -1e-30f; REJ_TRACK(b);
    memcpy(cb, cv, sizeof cb); cb[1].y1 = -3.0f; cb[1].y2 = 4.0f; ACC_TRACK(b);   /* y may leave [0, 1] */
    memcpy(cb, cv, sizeof cb); cb[1].x1 = 0.0f; cb[1].x2 = 1.0f; ACC_TRACK(b);
    memcpy(cb, cv, sizeof cb); cb[1].x1 = 1.0f; cb[1].x2 = 0.0f; ACC_TRACK(b);
    /* Literal: the rule names any curve, so curve 0, which no key uses,
     * is checked too. */
    memcpy(cb, cv, sizeof cb); cb[0].x1 = 2.0f; REJ_TRACK(b);
    memcpy(cb, cv, sizeof cb); cb[0].y2 = (float)make_nan(); REJ_TRACK(b);
    /* Curve index below n_curves, on every key but the last. */
    memcpy(cb, cv, sizeof cb);
    kb[0] = mkbez(0, 1.0f, 2); REJ_TRACK(b);
    kb[0] = mkbez(0, 1.0f, 1); kb[1] = mkbez(10, 2.0f, 5); REJ_TRACK(b);
    kb[1] = mkkey(10, 2.0f, PSYTL_EASE_LINEAR); kb[2] = mkbez(20, 3.0f, 99); ACC_TRACK(b);
    kb[2] = mkkey(20, 3.0f, PSYTL_EASE_LINEAR);
    b.curves = NULL; b.n_curves = 0; REJ_TRACK(b);   /* index 1 is not below 0 */
    /* n_curves > 0 with curves NULL, no BEZIER key. */
    b = gk; b.n_curves = 1; REJ_TRACK(b);
    /* Rate. */
    b = gs; b.rate = 0; REJ_TRACK(b);
    b = gs; b.rate = -1; REJ_TRACK(b);
    b = gs; b.rate = NS_E9 + 1; REJ_TRACK(b);
    b = gs; b.rate = 1; ACC_TRACK(b);
    b = gs; b.rate = NS_E9; ACC_TRACK(b);
    /* A last sample at or past 2^62, its time rounded up to a whole ns.
     * gs has 4 samples: at rate 1 the last is t0 + 3 s; at rate 3 it is
     * t0 + 1 s exactly; at rate 7 it is t0 + 3/7 s = t0 + 428571428.57 ns,
     * which rounds up to 428571429. */
    b = gs; b.rate = 1; b.t0 = LIM62 - 3 * S_NS; REJ_TRACK(b);              /* last at 2^62         */
    b = gs; b.rate = 1; b.t0 = LIM62 - 3 * S_NS - 1; ACC_TRACK(b);          /* last at 2^62 - 1 ns  */
    b = gs; b.rate = 3; b.t0 = LIM62 - S_NS; REJ_TRACK(b);
    b = gs; b.rate = 3; b.t0 = LIM62 - S_NS - 1; ACC_TRACK(b);
    b = gs; b.rate = 7; b.t0 = LIM62 - 428571429; REJ_TRACK(b);             /* rounds up to 2^62    */
    b = gs; b.rate = 7; b.t0 = LIM62 - 428571430; ACC_TRACK(b);
    /* Interp. */
    b = gs; b.interp = 3; REJ_TRACK(b);
    b = gs; b.interp = -1; REJ_TRACK(b);
    b = gs; b.interp = PSYTL_INTERP_CUBIC; ACC_TRACK(b);
    /* Period and repeats. */
    b = gk; b.period = -1; REJ_TRACK(b);
    b = gs; b.period = -5; REJ_TRACK(b);
    b = gk; b.period = LIM62; REJ_TRACK(b);
    b = gk; b.period = LIM62 - 1; ACC_TRACK(b);
    b = gk; b.repeats = -1; REJ_TRACK(b);
    b = gk; b.period = 10; b.repeats = -1; REJ_TRACK(b);
    b = gk; b.repeats = 2; REJ_TRACK(b);   /* repeats > 0 with period 0 */
    b = gk; b.period = 10; b.repeats = 2; ACC_TRACK(b);
    /* Times beyond +-2^62. */
    b = trk_keys(kb, 3);
    memcpy(kb, k, sizeof kb); kb[2].time = LIM62 + 1; REJ_TRACK(b);
    memcpy(kb, k, sizeof kb); kb[0].time = -LIM62 - 1; REJ_TRACK(b);
    b = gs; b.t0 = -LIM62 - 1; REJ_TRACK(b);
    b = gs; b.t0 = LIM62 + 1; REJ_TRACK(b);
    /* Channel and base. */
    CHECK_I(psytl_set_track(&g_tl, 4, 1, &gk), PSYTL_ERR_ARG);
    CHECK_I(psytl_set_track(&g_tl, -1, 1, &gk), PSYTL_ERR_ARG);
    CHECK_I(psytl_set_track(&g_tl, 0, PSYTL_MAX_BASES, &gk), PSYTL_ERR_ARG);
    CHECK_I(psytl_set_track(&g_tl, 0, -1, &gk), PSYTL_ERR_ARG);

    /* The rejections left channel 0 free, at its initial value. */
    CHECK(psytl_value(&g_tl, 0) == 0.5f);
    id = add1(&g_tl, 5, 0, PSYTL_SET, 0, 1.0f);
    CHECK(id >= 0);
    CHECK_I(psytl_remove(&g_tl, id), 0);
    /* A rejected set_track leaves the track it would have replaced. */
    CHECK_I(psytl_set_track(&g_tl, 1, 1, &gk), 0);
    b = gs; b.interp = 3;
    CHECK_I(psytl_set_track(&g_tl, 1, 1, &b), PSYTL_ERR_ARG);
    CHECK_I(psytl_anchor(&g_tl, 1, S_NS, 15), 0);
    eval_at(&g_tl, S_NS, 0, 0, NULL, 0);
    CHECK_F(psytl_value(&g_tl, 1), 2.5, 1e-6);
    /* NULL removes at once: the initial value, and the channel is free. */
    CHECK_I(psytl_set_track(&g_tl, 1, 1, NULL), 0);
    CHECK(psytl_value(&g_tl, 1) == -0.5f);
    eval_at(&g_tl, S_NS + 10, 0, 1, NULL, 0);
    CHECK(psytl_value(&g_tl, 1) == -0.5f);
    id = add1(&g_tl, 2, 0, PSYTL_SET, 1, 1.0f);
    CHECK(id >= 0);
    /* A channel with events takes no track and no tween. */
    CHECK_I(psytl_set_track(&g_tl, 1, 2, &gs), PSYTL_ERR_BOUND);
    CHECK_I(psytl_set_track(&g_tl, 1, 3, &gk), PSYTL_ERR_BOUND);
    td = twd_at(1.0f, 10, 0);
    CHECK_I(psytl_tween(&g_tl, 1, 2, &td), PSYTL_ERR_BOUND);
    CHECK_I(psytl_remove(&g_tl, id), 0);
    CHECK_I(psytl_set_track(&g_tl, 1, 2, &gs), 0);   /* free again */
    /* A tracked channel takes no event, on its base or another. */
    CHECK_I(add1(&g_tl, 2, 0, PSYTL_SET, 1, 1.0f), PSYTL_ERR_BOUND);
    CHECK_I(add1(&g_tl, 0, 0, PSYTL_ONSET, 1, 0.0f), PSYTL_ERR_BOUND);
    /* A tween replaces the track, on another base (which stops the track
     * at once), and binds the same way. */
    CHECK_I(psytl_tween(&g_tl, 1, 3, &td), 0);
    CHECK_I(add1(&g_tl, 3, 0, PSYTL_SET, 1, 1.0f), PSYTL_ERR_BOUND);
    CHECK_I(add1(&g_tl, 2, 0, PSYTL_SET, 1, 1.0f), PSYTL_ERR_BOUND);
    CHECK_I(psytl_set_track(&g_tl, 1, 3, NULL), 0);
    CHECK(psytl_value(&g_tl, 1) == -0.5f);
    CHECK(add1(&g_tl, 3, 0, PSYTL_SET, 1, 1.0f) >= 0);
    /* clear frees a channel with a track: initial value, free. */
    CHECK_I(psytl_set_track(&g_tl, 2, 4, &gs), 0);
    CHECK_I(psytl_anchor(&g_tl, 4, S_NS + 20, 25 * MS_NS), 0);
    eval_at(&g_tl, S_NS + 20, 0, 2, NULL, 0);
    CHECK_F(psytl_value(&g_tl, 2), 3.5, 1e-6);
    CHECK_I(psytl_clear(&g_tl, 4), 0);
    CHECK(psytl_value(&g_tl, 2) == 2.0f);
    CHECK(add1(&g_tl, 5, 0, PSYTL_SET, 2, 1.0f) >= 0);
    /* set_keys is set_track with keys only: same value. */
    CHECK_I(psytl_set_keys(&g_tl, 0, 1, k, 3), 0);
    eval_at(&g_tl, S_NS + 30, 0, 3, NULL, 0);
    CHECK_F(psytl_value(&g_tl, 0), ksample(k, 3, 15 + 30), 1e-6);
}

/* ----------------------------------------------------------- 5f. tweens */

static int64_t g_tw_R;
static int64_t g_tw_frame;

static void tw_frame(int64_t bt) {
    eval_at(&g_tl, g_tw_R + bt, P60, g_tw_frame++, NULL, 0);
}

/* A channel's value against a model driver read at base time bt, within
 * float rounding. Returns 1 on a mismatch. */
static int chk_drv(int ch, const rdrv* r, int64_t bt) {
    double want = rdrv_value(r, bt), got = (double)psytl_value(&g_tl, ch);
    if (!(fabs(got - want) <= tol_of(want))) {
        if (g_cv_prints++ < 8)
            fprintf(stderr, "  channel %d at base time %lld: got %.9g, model %.9g\n", ch, (long long)bt, got, want);
        return 1;
    }
    return 0;
}

/* Tweens from a function whose frame is gone before the evaluate, with the
 * desc on that frame (its inline curve included) changed after the call. */
static void tween_from_helper(int ch, int base, int64_t start, int64_t dur, float to) {
    psytl_tween_desc d = twd_at(to, dur, start);
    volatile psytl_tween_desc* vd = &d;
    d.ease = PSYTL_EASE_BEZIER;
    d.curve = mkcurve(0.42f, 0.0f, 0.58f, 1.0f);
    CHECK_I(psytl_tween(&g_tl, ch, base, &d), 0);
    vd->curve.x1 = 1.0f;
    vd->curve.y1 = 5.0f;
    vd->curve.x2 = 0.0f;
    vd->curve.y2 = -5.0f;
    vd->to = -7.0f;
    vd->duration = 1;
    vd->start = 0;
}

static void stack_scribble(void) {
    volatile unsigned char buf[8192];
    int i;
    for (i = 0; i < (int)sizeof buf; i++) buf[i] = (unsigned char)(0xA5 ^ i);
}

static void test_tween(void) {
    static psytl_key tk[2];
    static psytl_curve tc;
    const int64_t M = MS_NS;
    float init[6] = { 0.25f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f };
    psytl_track tr;
    psytl_tween_desc d;
    rdrv r1, r2;
    int64_t bt;
    int bad;

    g_cv_prints = 0;
    CHECK(open_h(&g_tl, g_store, 16, 6, init, 0.5));
    g_tw_R = S_NS;
    g_tw_frame = 0;
    CHECK_I(psytl_anchor(&g_tl, 1, g_tw_R, 0), 0);

    /* A channel with no track starts from its value (THE TAKEOVER), held
     * until the start, and shows `to` from start + duration on. */
    d = twd_at(1.25f, 200 * M, 100 * M);
    CHECK_I(psytl_tween(&g_tl, 0, 1, &d), 0);
    CHECK(psytl_value(&g_tl, 0) == 0.25f);
    tr = tween_track(tk, &tc, 100 * M, 200 * M, 0.25f, 1.25f, PSYTL_EASE_LINEAR, NULL);
    bad = 0;
    for (bt = 0; bt <= 400 * M; bt += 5 * M) {
        tw_frame(bt);
        bad += chan_vs(0, &tr, bt);
        if (bt == 95 * M || bt == 100 * M) CHECK(psytl_value(&g_tl, 0) == 0.25f);
        if (bt == 200 * M) CHECK(psytl_value(&g_tl, 0) == 0.75f);
        if (bt == 300 * M || bt == 400 * M) CHECK(psytl_value(&g_tl, 0) == 1.25f);
    }
    if (bad) fail_i(__LINE__, "tween values off the model", bad, 0);

    /* A tween posted during another: the first runs on until the second's
     * start, and the second starts from the first's value there,
     * 10 x 550 / 1000 = 5.5, not from the 4.72 the last frame before the
     * call showed (the v0.1.0 rule). */
    d = twd_at(10.0f, 1000 * M, 400 * M);
    CHECK_I(psytl_tween(&g_tl, 1, 1, &d), 0);
    rdrv_tween(&r1, &d, 400 * M, NULL, 0.0, 0.0);
    for (bt = 404 * M; bt <= 872 * M; bt += 4 * M) tw_frame(bt);
    CHECK_F(psytl_value(&g_tl, 1), 4.72, 1e-5);
    d = twd_at(0.0f, 200 * M, 950 * M);
    d.ease = PSYTL_EASE_COSINE;
    CHECK_I(psytl_tween(&g_tl, 1, 1, &d), 0);
    rdrv_tween(&r2, &d, 950 * M, &r1, 0.0, (double)psytl_value(&g_tl, 1));
    bad = 0;
    for (bt = 900 * M; bt <= 1300 * M; bt += 5 * M) {
        tw_frame(bt);
        bad += chk_drv(1, bt < 950 * M ? &r1 : &r2, bt);
        if (bt == 920 * M) CHECK_F(psytl_value(&g_tl, 1), 5.2, 1e-5);   /* the first, running on */
        if (bt == 950 * M) CHECK_F(psytl_value(&g_tl, 1), 5.5, 1e-5);
        if (bt == 1050 * M) CHECK_F(psytl_value(&g_tl, 1), 2.75, 1e-5);
    }
    CHECK(psytl_value(&g_tl, 1) == 0.0f);
    if (bad) fail_i(__LINE__, "tween during a tween off the model", bad, 0);

    /* Duration 0 jumps at start. */
    d = twd_at(3.0f, 0, 1350 * M);
    CHECK_I(psytl_tween(&g_tl, 2, 1, &d), 0);
    tw_frame(1310 * M);
    CHECK(psytl_value(&g_tl, 2) == 0.5f);
    tw_frame(1350 * M - 1);
    CHECK(psytl_value(&g_tl, 2) == 0.5f);
    tw_frame(1350 * M);
    CHECK(psytl_value(&g_tl, 2) == 3.0f);
    tw_frame(1351 * M);
    CHECK(psytl_value(&g_tl, 2) == 3.0f);

    /* LOG needs from_set, even on a channel whose value is > 0, and both
     * ends > 0. */
    d = twd_at(2.0f, 0, 1355 * M);
    CHECK_I(psytl_tween(&g_tl, 3, 1, &d), 0);
    tw_frame(1360 * M);
    CHECK(psytl_value(&g_tl, 3) == 2.0f);
    d = twd_at(8.0f, 100 * M, 1400 * M);
    d.ease = PSYTL_EASE_LOG;
    CHECK_I(psytl_tween(&g_tl, 3, 1, &d), PSYTL_ERR_ARG);   /* no from_set */
    d.from_set = true;
    d.from = 2.0f;
    d.to = 0.0f;
    CHECK_I(psytl_tween(&g_tl, 3, 1, &d), PSYTL_ERR_ARG);
    d.to = -1.0f;
    CHECK_I(psytl_tween(&g_tl, 3, 1, &d), PSYTL_ERR_ARG);
    d.to = 8.0f;
    d.from = 0.0f;
    CHECK_I(psytl_tween(&g_tl, 3, 1, &d), PSYTL_ERR_ARG);
    d.from = -2.0f;
    CHECK_I(psytl_tween(&g_tl, 3, 1, &d), PSYTL_ERR_ARG);
    d.from = 2.0f;
    CHECK_I(psytl_tween(&g_tl, 3, 1, &d), 0);
    tr = tween_track(tk, &tc, 1400 * M, 100 * M, 2.0f, 8.0f, PSYTL_EASE_LOG, NULL);
    bad = 0;
    for (bt = 1380 * M; bt <= 1550 * M; bt += 2 * M) {
        tw_frame(bt);
        bad += chan_vs(3, &tr, bt);
        if (bt == 1450 * M) CHECK_F(psytl_value(&g_tl, 3), 4.0, 1e-5);
    }
    if (bad) fail_i(__LINE__, "LOG tween off the model", bad, 0);

    /* BEZIER needs a valid inline curve, and the desc is copied. */
    d = twd_at(2.0f, 300 * M, 1600 * M);
    d.ease = PSYTL_EASE_BEZIER;
    d.curve = mkcurve(1.5f, 0.0f, 0.5f, 1.0f);
    CHECK_I(psytl_tween(&g_tl, 4, 1, &d), PSYTL_ERR_ARG);
    d.curve = mkcurve(0.5f, (float)make_nan(), 0.5f, 1.0f);
    CHECK_I(psytl_tween(&g_tl, 4, 1, &d), PSYTL_ERR_ARG);
    tween_from_helper(4, 1, 1600 * M, 300 * M, 2.0f);
    stack_scribble();
    d.curve = mkcurve(0.42f, 0.0f, 0.58f, 1.0f);
    tr = tween_track(tk, &tc, 1600 * M, 300 * M, 0.0f, 2.0f, PSYTL_EASE_BEZIER, &d.curve);
    /* Other rejections leave the waiting tween in place. */
    d = twd_at(2.0f, 300 * M, 1600 * M);
    d.ease = 7;
    CHECK_I(psytl_tween(&g_tl, 4, 1, &d), PSYTL_ERR_ARG);
    d = twd_at((float)make_nan(), 300 * M, 1600 * M);
    CHECK_I(psytl_tween(&g_tl, 4, 1, &d), PSYTL_ERR_ARG);
    d = twd_at((float)make_inf(), 300 * M, 1600 * M);
    CHECK_I(psytl_tween(&g_tl, 4, 1, &d), PSYTL_ERR_ARG);
    d = twd_at(2.0f, -1, 1600 * M);
    CHECK_I(psytl_tween(&g_tl, 4, 1, &d), PSYTL_ERR_ARG);
    CHECK_I(psytl_tween(&g_tl, 4, 1, NULL), PSYTL_ERR_ARG);
    d = twd_at(2.0f, 300 * M, 1600 * M);
    CHECK_I(psytl_tween(&g_tl, 6, 1, &d), PSYTL_ERR_ARG);
    CHECK_I(psytl_tween(&g_tl, -1, 1, &d), PSYTL_ERR_ARG);
    CHECK_I(psytl_tween(&g_tl, 4, PSYTL_MAX_BASES, &d), PSYTL_ERR_ARG);
    CHECK_I(psytl_tween(&g_tl, 4, -1, &d), PSYTL_ERR_ARG);
    d = twd_at(2.0f, 0, LIM62 + 1);
    CHECK_I(psytl_tween(&g_tl, 4, 1, &d), PSYTL_ERR_ARG);
    d = twd_at(2.0f, 100, LIM62 - 10);
    CHECK_I(psytl_tween(&g_tl, 4, 1, &d), PSYTL_ERR_ARG);
    bad = 0;
    for (bt = 1560 * M; bt <= 1950 * M; bt += 3 * M) {
        tw_frame(bt);
        bad += chan_vs(4, &tr, bt);
    }
    if (bad) fail_i(__LINE__, "BEZIER tween off the copied curve", bad, 0);
    CHECK_F(psytl_sample(&tr, 1750 * M), 1.0, 1e-6);   /* the symmetric curve at u = 0.5 */

    /* BOUND both ways. */
    CHECK(add1(&g_tl, 0, 0, PSYTL_SET, 5, 1.0f) >= 0);
    d = twd_at(1.0f, 10, 0);
    CHECK_I(psytl_tween(&g_tl, 5, 0, &d), PSYTL_ERR_BOUND);
    CHECK_I(add1(&g_tl, 1, 0, PSYTL_SET, 0, 1.0f), PSYTL_ERR_BOUND);
    CHECK_I(add1(&g_tl, 2, 0, PSYTL_SET, 0, 1.0f), PSYTL_ERR_BOUND);

    /* clear of the tweens' base frees them, at their initial values. */
    CHECK_I(psytl_clear(&g_tl, 1), 0);
    CHECK(psytl_value(&g_tl, 0) == 0.25f);
    CHECK(psytl_value(&g_tl, 1) == 0.0f);
    CHECK(psytl_value(&g_tl, 2) == 0.5f);
    CHECK(psytl_value(&g_tl, 3) == 0.0f);
    CHECK(psytl_value(&g_tl, 4) == 0.0f);
    CHECK(add1(&g_tl, 2, 0, PSYTL_SET, 0, 1.0f) >= 0);
    CHECK(add1(&g_tl, 3, 0, PSYTL_SET, 4, 1.0f) >= 0);
    tw_frame(2000 * M);
    CHECK(psytl_value(&g_tl, 4) == 0.0f);   /* base 3 is stopped */
}

/* PSYTL_S and PSYTL_MS: rounded to nearest, both signs, constant
 * expressions. */
static void test_tween_macros(void) {
    static const int64_t st[6] = { PSYTL_MS(200), PSYTL_S(0.2), PSYTL_S(-0.25), PSYTL_MS(-1.25),
                                   PSYTL_S(1.5), PSYTL_MS(0) };
    int i, bad = 0;
    CHECK_I(st[0], 200000000);
    CHECK_I(st[1], 200000000);
    CHECK_I(st[2], -250000000);
    CHECK_I(st[3], -1250000);
    CHECK_I(st[4], 1500000000);
    CHECK_I(st[5], 0);
    CHECK_I(PSYTL_S(2.0000000004), INT64_C(2000000000));
    CHECK_I(PSYTL_S(2.0000000006), INT64_C(2000000001));
    CHECK_I(PSYTL_S(-2.0000000004), -INT64_C(2000000000));
    CHECK_I(PSYTL_S(-2.0000000006), -INT64_C(2000000001));
    CHECK_I(PSYTL_S(-0.0000000014), -1);
    CHECK_I(PSYTL_S(-0.0000000016), -2);
    CHECK_I(PSYTL_S(0.999999999), 999999999);
    CHECK_I(PSYTL_S(3600.0), 3600 * S_NS);
    CHECK_I(PSYTL_S(-3), -3 * S_NS);
    CHECK_I(PSYTL_MS(200), 200 * MS_NS);
    CHECK_I(PSYTL_MS(0.0000004), 0);
    CHECK_I(PSYTL_MS(0.0000006), 1);
    CHECK_I(PSYTL_MS(-0.0000004), 0);
    CHECK_I(PSYTL_MS(-0.0000006), -1);
    CHECK_I(PSYTL_MS(16.6666667), 16666667);
    CHECK_I(PSYTL_MS(-16.6666667), -16666667);
    CHECK_I(PSYTL_MS(1.5), 1500000);
    CHECK_I(PSYTL_FOREVER, -1);
    /* Away from ties, PSYTL_S agrees with psytl_ns. */
    g_rng = 5150;
    for (i = 0; i < 4000; i++) {
        double x = (double)rnd_range(-INT64_C(1000000000000000), INT64_C(1000000000000000)) * 1e-13;
        if (PSYTL_S(x) != psytl_ns(x)) {
            if (!bad) fprintf(stderr, "  PSYTL_S(%.17g) = %lld, psytl_ns %lld\n", x, (long long)PSYTL_S(x),
                              (long long)psytl_ns(x));
            bad++;
        }
    }
    if (bad) fail_i(__LINE__, "PSYTL_S differs from psytl_ns", bad, 0);
}

/* Opens g_tl with 8 channels at 0.5 and lead 0.5. */
static void tw_open(void) {
    float init[8] = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
    CHECK(open_h(&g_tl, g_store, 16, 8, init, 0.5));
}

/* A jump (duration 0, LINEAR: read at the onset) to 9 on a channel at 0.5
 * with no track shows 0.5 at base time s - 1 and 9 at s. The base runs
 * with base time bt0 at RT rt0. */
static void jump_probe(int line, int ch, int64_t rt0, int64_t bt0, int64_t s) {
    eval_at(&g_tl, rt0 + (s - 1 - bt0), P60, g_tw_frame++, NULL, 0);
    if (psytl_value(&g_tl, ch) != 0.5f)
        fail_f(line, "tween took over before its start (base time s - 1)", (double)psytl_value(&g_tl, ch), 0.5);
    eval_at(&g_tl, rt0 + (s - bt0), P60, g_tw_frame++, NULL, 0);
    if (psytl_value(&g_tl, ch) != 9.0f)
        fail_f(line, "tween did not take over at its start (base time s)", (double)psytl_value(&g_tl, ch), 9.0);
}

/* start_set, "now" and delay, and a zeroed desc. */
static void test_tween_now(void) {
    static psytl_key kr[2];
    const int64_t M = MS_NS, R = S_NS;
    psytl_tween_desc d;
    int k;

    g_tw_frame = 0;
    /* Anchored, never evaluated: now is the anchor's base time. */
    tw_open();
    CHECK_I(psytl_anchor(&g_tl, 2, R, 300 * M), 0);
    d = twd(9.0f, 0);
    d.delay = 100 * M;
    CHECK_I(psytl_tween(&g_tl, 0, 2, &d), 0);
    jump_probe(__LINE__, 0, R, 300 * M, 400 * M);
    /* The same with the anchor's RT time in the future. */
    tw_open();
    CHECK_I(psytl_anchor(&g_tl, 2, R + 50 * M, 300 * M), 0);
    d.delay = 10 * M;
    CHECK_I(psytl_tween(&g_tl, 0, 2, &d), 0);
    jump_probe(__LINE__, 0, R + 50 * M, 300 * M, 310 * M);

    /* Never anchored: 0. The tween waits on the stopped base. */
    tw_open();
    d.delay = 50 * M;
    CHECK_I(psytl_tween(&g_tl, 1, 3, &d), 0);
    eval_at(&g_tl, R, P60, g_tw_frame++, NULL, 0);
    CHECK(psytl_value(&g_tl, 1) == 0.5f);
    CHECK_I(psytl_anchor(&g_tl, 3, 2 * R, 0), 0);
    jump_probe(__LINE__, 1, 2 * R, 0, 50 * M);

    /* start_set: start + delay, a negative delay too. */
    tw_open();
    CHECK_I(psytl_anchor(&g_tl, 1, R, 0), 0);
    d = twd_at(9.0f, 0, 200 * M);
    d.delay = 30 * M;
    CHECK_I(psytl_tween(&g_tl, 0, 1, &d), 0);
    d.delay = 0;
    CHECK_I(psytl_tween(&g_tl, 1, 1, &d), 0);
    d = twd_at(9.0f, 0, 500 * M);
    d.delay = -100 * M;
    CHECK_I(psytl_tween(&g_tl, 2, 1, &d), 0);
    jump_probe(__LINE__, 1, R, 0, 200 * M);
    jump_probe(__LINE__, 0, R, 0, 230 * M);
    jump_probe(__LINE__, 2, R, 0, 400 * M);

    /* After evaluates: the base time of the last evaluated onset. */
    tw_open();
    CHECK_I(psytl_anchor(&g_tl, 1, R, 0), 0);
    for (k = 0; k <= 6; k++) eval_at(&g_tl, R + k * P60, P60, g_tw_frame++, NULL, 0);
    d = twd(9.0f, 0);
    d.delay = 20 * M;
    CHECK_I(psytl_tween(&g_tl, 0, 1, &d), 0);
    jump_probe(__LINE__, 0, R, 0, 6 * P60 + 20 * M);

    /* Paused: an evaluate of a paused base is at its frozen time. */
    tw_open();
    CHECK_I(psytl_anchor(&g_tl, 1, R, 0), 0);
    eval_at(&g_tl, R, P60, g_tw_frame++, NULL, 0);
    CHECK_I(psytl_pause(&g_tl, 1, R + 70 * M), 0);
    eval_at(&g_tl, R + 100 * M, P60, g_tw_frame++, NULL, 0);
    d = twd(9.0f, 0);
    d.delay = 10 * M;
    CHECK_I(psytl_tween(&g_tl, 0, 1, &d), 0);
    eval_at(&g_tl, R + 150 * M, P60, g_tw_frame++, NULL, 0);
    CHECK(psytl_value(&g_tl, 0) == 0.5f);
    CHECK_I(psytl_resume(&g_tl, 1, R + 200 * M), 0);
    jump_probe(__LINE__, 0, R + 200 * M, 70 * M, 80 * M);

    /* Base 0 is never anchored: now is 0 before the first evaluate, then
     * the last onset. */
    tw_open();
    d = twd(9.0f, 0);
    d.delay = R + 5 * M;
    CHECK_I(psytl_tween(&g_tl, 0, 0, &d), 0);
    jump_probe(__LINE__, 0, 0, 0, R + 5 * M);
    d.delay = 7 * M;
    CHECK_I(psytl_tween(&g_tl, 1, 0, &d), 0);
    jump_probe(__LINE__, 1, 0, 0, R + 12 * M);

    /* An anchor after the last evaluate sets now to its base time: after
     * a rewinding anchor to 0, now is 0 (not the last onset's 500 ms, and
     * not the rewind's just-before-0), so a delay of 10 ms starts at
     * 10 ms. */
    tw_open();
    CHECK_I(psytl_anchor(&g_tl, 1, R, 0), 0);
    eval_at(&g_tl, R + 500 * M, P60, g_tw_frame++, NULL, 0);
    CHECK_I(psytl_anchor(&g_tl, 1, R + 510 * M, 0), 0);
    d = twd(9.0f, 0);
    d.delay = 10 * M;
    CHECK_I(psytl_tween(&g_tl, 0, 1, &d), 0);
    jump_probe(__LINE__, 0, R + 510 * M, 0, 10 * M);
    /* Literal reading: a pause at an RT time before the last onset moves
     * the base back without a rewind; the next evaluate's onset is then
     * at 60 ms, behind the furthest (100 ms), and now is 60 ms. */
    tw_open();
    CHECK_I(psytl_anchor(&g_tl, 1, R, 0), 0);
    eval_at(&g_tl, R + 100 * M, P60, g_tw_frame++, NULL, 0);
    CHECK_I(psytl_pause(&g_tl, 1, R + 60 * M), 0);
    eval_at(&g_tl, R + 110 * M, P60, g_tw_frame++, NULL, 0);
    d = twd(9.0f, 0);
    d.delay = 20 * M;
    CHECK_I(psytl_tween(&g_tl, 0, 1, &d), 0);
    CHECK_I(psytl_resume(&g_tl, 1, R + 200 * M), 0);
    eval_at(&g_tl, R + 220 * M, P60, g_tw_frame++, NULL, 0);   /* base time 80 ms */
    if (psytl_value(&g_tl, 0) != 9.0f)
        fail(__LINE__, "literal reading: now is the last evaluated onset's base time (60 ms), but the tween "
                       "had not taken over at 60 + 20 ms (the furthest evaluated, 100 ms, gives 120 ms)");
    /* And after a forward anchor: the anchor's base time, 300 ms. */
    tw_open();
    CHECK_I(psytl_anchor(&g_tl, 1, R, 0), 0);
    eval_at(&g_tl, R + 100 * M, P60, g_tw_frame++, NULL, 0);
    CHECK_I(psytl_anchor(&g_tl, 1, R + 100 * M, 300 * M), 0);
    d.delay = 250 * M;
    CHECK_I(psytl_tween(&g_tl, 0, 1, &d), 0);
    jump_probe(__LINE__, 0, R + 100 * M, 300 * M, 550 * M);
    /* A pause after the last evaluate: the frozen time. Evaluated at
     * 100 ms, paused at 140 ms, so now is 140 ms; resumed later, a delay
     * of 20 ms starts at 160 ms. */
    tw_open();
    CHECK_I(psytl_anchor(&g_tl, 1, R, 0), 0);
    eval_at(&g_tl, R + 100 * M, P60, g_tw_frame++, NULL, 0);
    CHECK_I(psytl_pause(&g_tl, 1, R + 140 * M), 0);
    d = twd(9.0f, 0);
    d.delay = 20 * M;
    CHECK_I(psytl_tween(&g_tl, 0, 1, &d), 0);
    CHECK_I(psytl_resume(&g_tl, 1, R + 300 * M), 0);
    jump_probe(__LINE__, 0, R + 300 * M, 140 * M, 160 * M);

    /* A zeroed desc: a LINEAR jump to 0 now, from a held value or a
     * running track. It changes nothing before the next evaluate. */
    tw_open();
    kr[0] = mkkey(0, 0.0f, PSYTL_EASE_LINEAR);
    kr[1] = mkkey(S_NS, 10.0f, PSYTL_EASE_LINEAR);
    CHECK_I(psytl_set_keys(&g_tl, 1, 1, kr, 2), 0);
    CHECK_I(psytl_anchor(&g_tl, 1, R, 0), 0);
    eval_at(&g_tl, R + 10 * M, P60, g_tw_frame++, NULL, 0);
    CHECK(psytl_value(&g_tl, 0) == 0.5f);
    CHECK_F(psytl_value(&g_tl, 1), 0.1, 1e-6);
    memset(&d, 0, sizeof d);
    CHECK_I(psytl_tween(&g_tl, 0, 1, &d), 0);
    CHECK_I(psytl_tween(&g_tl, 1, 1, &d), 0);
    CHECK(psytl_value(&g_tl, 0) == 0.5f);
    CHECK_F(psytl_value(&g_tl, 1), 0.1, 1e-6);
    eval_at(&g_tl, R + 10 * M, P60, g_tw_frame++, NULL, 0);   /* the same onset: now itself */
    CHECK(psytl_value(&g_tl, 0) == 0.0f);
    CHECK(psytl_value(&g_tl, 1) == 0.0f);
    eval_at(&g_tl, R + 500 * M, P60, g_tw_frame++, NULL, 0);
    CHECK(psytl_value(&g_tl, 0) == 0.0f);
    CHECK(psytl_value(&g_tl, 1) == 0.0f);
    /* Before any evaluate on an anchored base: at the anchor's base time. */
    tw_open();
    CHECK_I(psytl_anchor(&g_tl, 2, R, 40 * M), 0);
    CHECK_I(psytl_tween(&g_tl, 2, 2, &d), 0);
    eval_at(&g_tl, R - 1, P60, g_tw_frame++, NULL, 0);
    CHECK(psytl_value(&g_tl, 2) == 0.5f);
    eval_at(&g_tl, R, P60, g_tw_frame++, NULL, 0);
    CHECK(psytl_value(&g_tl, 2) == 0.0f);
}

/* A model channel as a chain of drivers: d[0] (or nothing, the initial
 * value, when none0) until at[1], d[1] until at[2], and so on. */
typedef struct rstage {
    rdrv    d[3];
    int64_t at[3];
    int     n, none0;
    double  init;
} rstage;

static double rstage_value(const rstage* s, int64_t t) {
    int k = 0, i;
    for (i = 1; i < s->n; i++)
        if (t >= s->at[i]) k = i;
    if (k == 0 && s->none0) return s->init;
    return rdrv_value(&s->d[k], t);
}

static void rstage_tween(rstage* st, const psytl_tween_desc* d, int64_t s, double at_call) {
    const rdrv* old = (st->n == 1 && st->none0) ? NULL : &st->d[st->n - 1];
    rdrv_tween(&st->d[st->n], d, s, old, st->init, at_call);
    st->at[st->n] = s;
    st->n++;
}

enum { GR_N = 7, GR_NCH = 8, GR_K = 26 };
static float g_gr[GR_N][GR_NCH][GR_K];
static rstage g_rst[GR_NCH];

/* THE TAKEOVER: from is the old track's value at the start, for keyed,
 * sampled, repeating and tween old tracks, with and without
 * keep_velocity, and does not depend on the frames. The same calls run on
 * seven frame grids (1, 2, 3, 5, 6 and 10 ms and an irregular one), the
 * second tweens posted at base time 60 ms for a start of 123.456789 ms
 * that no grid has; every frame against the model, and the values at
 * every 30 ms, which all grids have, compared across grids bit for bit. */
static void test_tween_from(void) {
    static psytl_key k0[4], k2[3], k5[2], k7[2];
    static float s1[40], s3[30];
    static const int64_t gp[6] = { 1000000, 2000000, 3000000, 5000000, 6000000, 10000000 };
    const int64_t M = MS_NS, R = S_NS, S = 123456789, TC = 60 * M, END = 750 * M, C30 = 30 * M;
    float init[GR_NCH] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 2.0f, 0.0f };
    psytl_track trs[GR_NCH];
    psytl_tween_desc d1[GR_NCH], d2[GR_NCH];
    int g, ch, i, k, bad = 0, bad_g = 0;
    int64_t bt, idx = 0;

    k0[0] = mkkey(0, 1.0f, PSYTL_EASE_COSINE);
    k0[1] = mkkey(200 * M, 4.0f, PSYTL_EASE_QUAD_IN);
    k0[2] = mkkey(450 * M, -2.0f, PSYTL_EASE_LINEAR);
    k0[3] = mkkey(900 * M, 3.0f, PSYTL_EASE_LINEAR);
    for (i = 0; i < 40; i++) s1[i] = (float)(2.0 * sin(0.3 * (double)i) + 0.1 * (double)i);
    k2[0] = mkkey(10 * M, 0.0f, PSYTL_EASE_LINEAR);
    k2[1] = mkkey(60 * M, 5.0f, PSYTL_EASE_QUAD_OUT);
    k2[2] = mkkey(80 * M, 2.0f, PSYTL_EASE_LINEAR);
    for (i = 0; i < 30; i++) s3[i] = (float)((i * 7) % 11) - 5.0f;
    k5[0] = mkkey(0, 0.0f, PSYTL_EASE_COSINE);
    k5[1] = mkkey(600 * M, 10.0f, PSYTL_EASE_LINEAR);
    k7[0] = mkkey(0, 0.0f, PSYTL_EASE_LINEAR);
    k7[1] = mkkey(S_NS, 10.0f, PSYTL_EASE_LINEAR);
    memset(trs, 0, sizeof trs);
    trs[0] = trk_keys(k0, 4);                                          /* keyed            */
    trs[1] = trk_samples(s1, 40, -100 * M, 60, PSYTL_INTERP_CUBIC);    /* sampled          */
    trs[2] = trk_keys(k2, 3);                                          /* repeating keyed  */
    trs[2].period = 75 * M;
    trs[3] = trk_samples(s3, 30, 0, 120, PSYTL_INTERP_LINEAR);         /* repeating sampled */
    trs[3].period = 47 * M;
    trs[5] = trk_keys(k5, 2);                                          /* keyed, kv after  */
    trs[7] = trk_keys(k7, 2);                                          /* kv after kv      */
    /* First tweens, posted before the anchor: channel 4 and 6 have no
     * track before them; 7's is a keep_velocity takeover at 50 ms. */
    d1[4] = twd_at(5.0f, 400 * M, 20 * M);
    d1[4].from_set = true;
    d1[4].from = 1.0f;
    d1[6] = twd_at(6.0f, 500 * M, 0);
    d1[6].from_set = true;
    d1[6].from = -3.0f;
    d1[6].ease = PSYTL_EASE_QUAD_OUT;
    d1[7] = twd_at(-4.0f, 300 * M, 50 * M);
    d1[7].keep_velocity = true;
    /* The second tweens, posted at 60 ms. Channel 1's start is now + delay. */
    d2[0] = twd_at(7.0f, 250 * M, S);
    d2[0].ease = PSYTL_EASE_COSINE;
    d2[1] = twd(-1.0f, 300 * M);
    d2[1].delay = S - TC;
    d2[2] = twd_at(3.0f, 200 * M, S);
    d2[2].ease = PSYTL_EASE_QUAD_IN;
    d2[3] = twd_at(2.0f, 260 * M, S);
    d2[3].ease = PSYTL_EASE_BEZIER;
    d2[3].curve = mkcurve(0.42f, 0.0f, 0.58f, 1.0f);
    d2[4] = twd_at(0.0f, 200 * M, S);
    d2[4].ease = PSYTL_EASE_COSINE;
    d2[5] = twd_at(4.0f, 300 * M, S);
    d2[5].keep_velocity = true;
    d2[6] = twd_at(0.0f, 250 * M, S);
    d2[6].keep_velocity = true;
    d2[7] = twd_at(1.0f, 200 * M, S);
    d2[7].keep_velocity = true;

    for (ch = 0; ch < GR_NCH; ch++) {
        rstage* st = &g_rst[ch];
        memset(st, 0, sizeof *st);
        st->n = 1;
        st->init = (double)init[ch];
        if (ch == 4 || ch == 6) st->none0 = 1;
        else st->d[0] = rdrv_plain(&trs[ch]);
    }
    rstage_tween(&g_rst[4], &d1[4], 20 * M, (double)init[4]);
    rstage_tween(&g_rst[6], &d1[6], 0, (double)init[6]);
    rstage_tween(&g_rst[7], &d1[7], 50 * M, (double)init[7]);
    for (ch = 0; ch < GR_NCH; ch++) rstage_tween(&g_rst[ch], &d2[ch], S, rstage_value(&g_rst[ch], TC));
    /* By hand: the repeating keyed track at S reads 48.456789 ms, 5 x
     * 38.456789 / 50; the first tween on channel 4, 1 + 4 x 103.456789 /
     * 400. */
    CHECK_F(rdrv_value(&g_rst[2].d[1], S), 5.0 * 38.456789 / 50.0, 1e-5);
    CHECK_F(rdrv_value(&g_rst[4].d[2], S), 1.0 + 4.0 * 103.456789 / 400.0, 1e-5);

    g_rng = 777;
    g_cv_prints = 0;
    for (g = 0; g < GR_N; g++) {
        CHECK(open_h(&g_tl, g_store, 8, GR_NCH, init, 0.5));
        for (ch = 0; ch < GR_NCH; ch++)
            if (ch != 4 && ch != 6) CHECK_I(psytl_set_track(&g_tl, ch, 1, &trs[ch]), 0);
        CHECK_I(psytl_tween(&g_tl, 4, 1, &d1[4]), 0);
        CHECK_I(psytl_tween(&g_tl, 6, 1, &d1[6]), 0);
        CHECK_I(psytl_tween(&g_tl, 7, 1, &d1[7]), 0);
        CHECK_I(psytl_anchor(&g_tl, 1, R, 0), 0);
        bt = 0;
        for (;;) {
            eval_at(&g_tl, R + bt, g < 6 ? gp[g] : 5 * M, idx++, NULL, 0);
            for (ch = 0; ch < GR_NCH; ch++) {
                double want = rstage_value(&g_rst[ch], bt), got = (double)psytl_value(&g_tl, ch);
                if (!(fabs(got - want) <= tol_of(want))) {
                    if (g_cv_prints++ < 8)
                        fprintf(stderr, "  takeover grid %d channel %d base time %lld: got %.9g, model %.9g\n", g,
                                ch, (long long)bt, got, want);
                    bad++;
                }
                if (bt % C30 == 0) g_gr[g][ch][bt / C30] = psytl_value(&g_tl, ch);
            }
            if (bt == TC)
                for (ch = 0; ch < GR_NCH; ch++) CHECK_I(psytl_tween(&g_tl, ch, 1, &d2[ch]), 0);
            if (bt >= END) break;
            if (g < 6) {
                bt += gp[g];
            } else {
                int64_t nx = bt + rnd_range(100000, 7 * M), m = (bt / C30 + 1) * C30;
                bt = nx > m ? m : nx;
            }
        }
    }
    if (bad) fail_i(__LINE__, "takeover values off the model", bad, 0);
    for (g = 1; g < GR_N; g++)
        for (ch = 0; ch < GR_NCH; ch++)
            for (k = 0; k < GR_K; k++)
                if (g_gr[g][ch][k] != g_gr[0][ch][k]) {
                    if (!bad_g)
                        fprintf(stderr, "  grid %d channel %d at %d ms: %.9g, 1 ms grid %.9g\n", g, ch, k * 30,
                                (double)g_gr[g][ch][k], (double)g_gr[0][ch][k]);
                    bad_g++;
                }
    if (bad_g) fail_i(__LINE__, "takeover values depend on the frame grid", bad_g, 0);
}

/* keep_velocity: no step in value or in velocity at the start, and the
 * Hermite model. Frames every 1 ms, the start half way between two. */
static void test_tween_velocity(void) {
    static psytl_key kr[2];
    static float sv[200];
    static float v[4][601];
    const int64_t M = MS_NS, R = S_NS, S = 500 * M + 500000;
    float init[8] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    psytl_track tr, ts;
    psytl_tween_desc d;
    rdrv old_r, old_s, r[4];
    double left, right, mright, slope;
    int ch, k, bad = 0;

    g_cv_prints = 0;
    for (k = 0; k < 200; k++) sv[k] = (float)(3.0 * sin(0.1 * (double)k));
    kr[0] = mkkey(0, 0.0f, PSYTL_EASE_LINEAR);
    kr[1] = mkkey(S_NS, 10.0f, PSYTL_EASE_LINEAR);
    tr = trk_keys(kr, 2);
    ts = trk_samples(sv, 200, 0, 120, PSYTL_INTERP_CUBIC);
    old_r = rdrv_plain(&tr);
    old_s = rdrv_plain(&ts);
    CHECK(open_h(&g_tl, g_store, 8, 8, init, 0.5));
    CHECK_I(psytl_set_track(&g_tl, 0, 1, &tr), 0);
    CHECK_I(psytl_set_track(&g_tl, 1, 1, &tr), 0);
    CHECK_I(psytl_set_track(&g_tl, 2, 1, &ts), 0);
    /* 0: a ramp at 10/s retargeted to 4; 1: the same without
     * keep_velocity, whose velocity steps; 2: a sampled sine; 3: no track
     * (literal: velocity 0, so a smoothstep from 1 to 3). */
    d = twd_at(4.0f, 300 * M, S);
    d.keep_velocity = true;
    CHECK_I(psytl_tween(&g_tl, 0, 1, &d), 0);
    rdrv_tween(&r[0], &d, S, &old_r, 0.0, 0.0);
    d.keep_velocity = false;
    CHECK_I(psytl_tween(&g_tl, 1, 1, &d), 0);
    rdrv_tween(&r[1], &d, S, &old_r, 0.0, 0.0);
    d = twd_at(0.0f, 400 * M, S);
    d.keep_velocity = true;
    CHECK_I(psytl_tween(&g_tl, 2, 1, &d), 0);
    rdrv_tween(&r[2], &d, S, &old_s, 0.0, 0.0);
    d = twd_at(3.0f, 200 * M, S);
    d.keep_velocity = true;
    CHECK_I(psytl_tween(&g_tl, 3, 1, &d), 0);
    rdrv_tween(&r[3], &d, S, NULL, 1.0, 1.0);
    CHECK_I(psytl_anchor(&g_tl, 1, R, 0), 0);
    for (k = 0; k <= 600; k++) {
        int64_t bt = 400 * M + (int64_t)k * M;
        eval_at(&g_tl, R + bt, M, k, NULL, 0);
        for (ch = 0; ch < 4; ch++) {
            v[ch][k] = psytl_value(&g_tl, ch);
            if (bt < S) {
                const rdrv* o = ch == 2 ? &old_s : &old_r;
                if (ch == 3) {
                    if (v[ch][k] != 1.0f) bad++;
                } else {
                    bad += chk_drv(ch, o, bt);
                }
            } else {
                bad += chk_drv(ch, &r[ch], bt);
            }
        }
    }
    if (bad) fail_i(__LINE__, "keep_velocity values off the Hermite model", bad, 0);
    /* Frame 100 is at 500 ms, frame 101 at 501 ms; the start is between. */
    left = ((double)v[0][100] - (double)v[0][99]) / 1e-3;
    right = ((double)v[0][102] - (double)v[0][101]) / 1e-3;
    mright = (rdrv_value(&r[0], 502 * M) - rdrv_value(&r[0], 501 * M)) / 1e-3;
    CHECK_F(left, 10.0, 0.01);
    CHECK_F(right, mright, 5e-3);
    if (!(fabs(right - left) < 1.0)) fail_f(__LINE__, "keep_velocity: velocity steps at the start", right, left);
    if (!(fabs((double)v[0][101] - 5.01) < 1e-4))
        fail_f(__LINE__, "keep_velocity: value steps at the start", (double)v[0][101], 5.01);
    /* The finite differences see a step: without keep_velocity, 10/s to
     * (4 - 5.005) / 0.3 s. */
    left = ((double)v[1][100] - (double)v[1][99]) / 1e-3;
    right = ((double)v[1][102] - (double)v[1][101]) / 1e-3;
    CHECK(fabs(right - left) > 10.0);
    /* The sampled sine: its slope over the last microsecond, continued. */
    slope = (ref_track(&ts, S) - ref_track(&ts, S - 1000)) * 1e6;   /* per second */
    left = ((double)v[2][100] - (double)v[2][99]) / 1e-3;
    right = ((double)v[2][102] - (double)v[2][101]) / 1e-3;
    if (!(fabs(right - left) < 3.0)) fail_f(__LINE__, "keep_velocity on a sampled track: velocity steps", right, left);
    CHECK(fabs(slope) > 10.0);   /* not vacuous: the sine moves fast there */
    /* No track: velocity 0 at the start. */
    CHECK(fabs((double)v[3][101] - 1.0) < 1e-3);
    CHECK_F(v[3][100 + 100], 1.0 + 2.0 * (3.0 * 0.4975 * 0.4975 - 2.0 * 0.4975 * 0.4975 * 0.4975), 1e-5);
}

/* cycles and yoyo, against the model and by hand. */
static void test_tween_cycles(void) {
    const int64_t M = MS_NS, R = S_NS, S = 50 * M;
    float init[8] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    psytl_tween_desc d[8];
    rdrv r[8];
    int ch, bad = 0, bad23 = 0;
    int64_t bt, onset = R, far;

    g_cv_prints = 0;
    CHECK(open_h(&g_tl, g_store, 8, 8, init, 0.5));
    d[0] = twd_at(1.0f, 100 * M, S);
    d[0].from_set = true;
    d[0].from = 0.0f;
    d[0].cycles = 3;
    d[1] = d[0];
    d[1].cycles = PSYTL_FOREVER;
    d[2] = d[0];
    d[2].cycles = 0;
    d[3] = d[0];
    d[3].cycles = 1;
    d[4] = twd_at(6.0f, 100 * M, S);
    d[4].from_set = true;
    d[4].from = 2.0f;
    d[4].ease = PSYTL_EASE_COSINE;
    d[4].yoyo = true;
    d[5] = twd_at(1.0f, 40 * M, S);
    d[5].from_set = true;
    d[5].from = -1.0f;
    d[5].yoyo = true;
    d[5].cycles = 3;
    d[6] = twd_at(1.0f, 100 * M, S);
    d[6].from_set = true;
    d[6].from = 0.0f;
    d[6].ease = PSYTL_EASE_QUAD_IN;
    d[6].yoyo = true;
    d[7] = twd_at(1.0f, 60 * M, S);
    d[7].from_set = true;
    d[7].from = 0.0f;
    d[7].ease = PSYTL_EASE_BEZIER;
    d[7].curve = mkcurve(0.68f, -0.6f, 0.32f, 1.6f);
    d[7].yoyo = true;
    d[7].cycles = PSYTL_FOREVER;
    for (ch = 0; ch < 8; ch++) {
        CHECK_I(psytl_tween(&g_tl, ch, 1, &d[ch]), 0);
        rdrv_tween(&r[ch], &d[ch], S, NULL, 0.0, 0.0);
    }
    CHECK_I(psytl_anchor(&g_tl, 1, R, 0), 0);
    for (bt = 0; bt <= 1000 * M; bt += M) {
        float v0, v1, v4, v5, v6;
        onset = R + bt;
        eval_at(&g_tl, onset, M, bt / M, NULL, 0);
        /* No track before them: the channels hold 0 until the start. */
        for (ch = 0; ch < 8; ch++) {
            if (bt >= S) bad += chk_drv(ch, &r[ch], bt);
            else if (psytl_value(&g_tl, ch) != 0.0f) bad++;
        }
        if (psytl_value(&g_tl, 2) != psytl_value(&g_tl, 3)) bad23++;
        v0 = psytl_value(&g_tl, 0);
        v1 = psytl_value(&g_tl, 1);
        v4 = psytl_value(&g_tl, 4);
        v5 = psytl_value(&g_tl, 5);
        v6 = psytl_value(&g_tl, 6);
        if (bt == S + 99 * M) CHECK_F(v0, 0.99, 1e-6);
        if (bt == S + 100 * M) {
            CHECK(v0 == 0.0f);   /* the second cycle */
            CHECK(psytl_value(&g_tl, 2) == 1.0f);   /* once: `to` from the end */
        }
        if (bt == S + 250 * M) {
            CHECK_F(v0, 0.5, 1e-6);
            CHECK_F(v1, 0.5, 1e-6);
        }
        if (bt == S + 300 * M) {
            CHECK(v0 == 1.0f);   /* after 3 cycles: the value at start + duration */
            CHECK(v1 == 0.0f);   /* forever: the fourth cycle */
        }
        if (bt == S + 350 * M) CHECK_F(v1, 0.5, 1e-6);
        if (bt == S + 50 * M) CHECK_F(v4, 4.0, 1e-5);
        if (bt == S + 100 * M) CHECK_F(v4, 6.0, 1e-6);
        if (bt == S + 150 * M) CHECK_F(v4, 4.0, 1e-5);   /* on the way back */
        if (bt == S + 200 * M || bt == S + 300 * M) CHECK(v4 == 2.0f);   /* back at from, and held */
        if (bt == S + 20 * M || bt == S + 60 * M) CHECK_F(v5, 0.0, 1e-6);
        if (bt == S + 40 * M) CHECK_F(v5, 1.0, 1e-6);
        if (bt == S + 80 * M) CHECK_F(v5, -1.0, 1e-6);
        if (bt == S + 239 * M) CHECK_F(v5, -0.95, 1e-6);
        if (bt == S + 240 * M || bt == S + 500 * M) CHECK(v5 == -1.0f);
        /* Literal reading: the return leg has the same ease, so QUAD_IN
         * back is 1 + (0 - 1) 0.5^2 = 0.75 half way; a mirrored (GSAP)
         * return would give 0.25. */
        if (bt == S + 150 * M && !(fabs((double)v6 - 0.75) <= 1e-6))
            fail_f(__LINE__, "literal reading: yoyo's return leg eases to + (from - to) e(u)", (double)v6, 0.75);
    }
    if (bad) fail_i(__LINE__, "cycles and yoyo values off the model", bad, 0);
    if (bad23) fail_i(__LINE__, "cycles 0 and cycles 1 differ", bad23, 0);
    /* Far on: forever keeps cycling, the counted ones hold. */
    far = S + INT64_C(123456789) * 100 * M + 25 * M;
    onset += M;
    CHECK_I(psytl_anchor(&g_tl, 1, onset, far), 0);
    eval_at(&g_tl, onset, M, 2000, NULL, 0);
    for (ch = 0; ch < 8; ch++) bad += chk_drv(ch, &r[ch], far);
    CHECK_F(psytl_value(&g_tl, 1), 0.25, 1e-6);
    CHECK(psytl_value(&g_tl, 0) == 1.0f);
    CHECK(psytl_value(&g_tl, 4) == 2.0f);
    CHECK(psytl_value(&g_tl, 5) == -1.0f);
    CHECK(psytl_value(&g_tl, 6) == 0.0f);
    if (bad) fail_i(__LINE__, "cycles far on off the model", bad, 0);
}

/* STEP tweens: taken over at the end of the lead window, from the old
 * track's value at the start; a yoyo STEP tween is a STEP track whose
 * edges land where events at the same times land; USAGE's flicker as a
 * tween. */
static void test_tween_step(void) {
    static psytl_key kr[2];
    static const int lnum[6] = { 0, 1, 1, 3, 999, 1 }, lden[6] = { 1, 4, 2, 4, 1000, 3 };
    const int64_t M = MS_NS, R = S_NS, P = 10 * M;
    psytl_track tr;
    psytl_tween_desc d;
    rdrv old, r0, r1, rw;
    int trial, lead, j, bad = 0, bad_m = 0, n_edge = 0;
    int64_t bt, wrong = 0;

    kr[0] = mkkey(0, 0.0f, PSYTL_EASE_LINEAR);
    kr[1] = mkkey(S_NS, 100.0f, PSYTL_EASE_LINEAR);
    tr = trk_keys(kr, 2);
    old = rdrv_plain(&tr);
    g_cv_prints = 0;
    for (lead = 0; lead < 2; lead++) {
        int64_t w = lead ? P / 2 : 0;
        CHECK(open_h(&g_tl, g_store, 8, 4, NULL, lead ? 0.5 : 0.0));
        CHECK_I(psytl_set_track(&g_tl, 0, 1, &tr), 0);
        CHECK_I(psytl_set_track(&g_tl, 1, 1, &tr), 0);
        d = twd_at(50.0f, 30 * M, 103 * M);
        d.ease = PSYTL_EASE_STEP;
        CHECK_I(psytl_tween(&g_tl, 0, 1, &d), 0);
        rdrv_tween(&r0, &d, 103 * M, &old, 0.0, 0.0);
        d.ease = PSYTL_EASE_LINEAR;
        CHECK_I(psytl_tween(&g_tl, 1, 1, &d), 0);
        rdrv_tween(&r1, &d, 103 * M, &old, 0.0, 0.0);
        CHECK_I(psytl_anchor(&g_tl, 1, R, 0), 0);
        for (bt = 0; bt <= 200 * M; bt += P) {
            int64_t we = bt + ((g_mut & MUT_STEP_ONSET) ? 0 : w);
            eval_at(&g_tl, R + bt, P, bt / P, NULL, 0);
            bad += chk_drv(0, we < 103 * M ? &old : &r0, we < 103 * M ? bt : we);
            bad += chk_drv(1, bt < 103 * M ? &old : &r1, bt);
            if (bt == 100 * M) {
                /* Lead 0.5: the window ends at 105 ms, so the STEP tween
                 * shows from = the ramp at 103 ms; the LINEAR one waits. */
                CHECK_F(psytl_value(&g_tl, 0), lead ? 10.3 : 10.0, 1e-5);
                CHECK_F(psytl_value(&g_tl, 1), 10.0, 1e-5);
            }
            if (bt == 110 * M) CHECK_F(psytl_value(&g_tl, 0), 10.3, 1e-5);
            if (bt == 130 * M) CHECK(psytl_value(&g_tl, 0) == (lead ? 50.0f : 10.3f));
        }
    }
    if (bad) fail_i(__LINE__, "STEP tween takeover off the window's end", bad, 0);

    /* Random: a yoyo STEP tween over 2 cycles, 0 -> 1 -> 0 -> 1 -> 0 with
     * edges at te, te + dd, te + 2 dd, te + 3 dd, against SET events at
     * those times, at every lead, period 0 too, with onset noise. */
    g_rng = 9191;
    bad = 0;
    for (trial = 0; trial < 200; trial++) {
        int li = trial % 6;
        int64_t PP = trial % 7 == 0 ? 0 : trial % 3 == 0 ? P60 : trial % 3 == 1 ? 6944444 : 2000000;
        int64_t te = rnd_range(0, 50 * P60), dd = rnd_range(1, 3 * P60), onset = R, bt0 = rnd_range(-3 * P60, 0);
        int64_t w = PP * lnum[li] / lden[li], k;
        CHECK(open_h(&g_tl, g_store, 8, 2, NULL, (double)lnum[li] / (double)lden[li]));
        for (j = 0; j < 4; j++) CHECK(add1(&g_tl, 1, te + j * dd, PSYTL_SET, 0, j % 2 == 0 ? 1.0f : 0.0f) >= 0);
        d = twd_at(1.0f, dd, te - dd);
        d.from_set = true;
        d.from = 0.0f;
        d.ease = PSYTL_EASE_STEP;
        d.yoyo = true;
        d.cycles = 2;
        CHECK_I(psytl_tween(&g_tl, 1, 1, &d), 0);
        rdrv_tween(&rw, &d, te - dd, NULL, 0.0, 0.0);
        CHECK_I(psytl_anchor(&g_tl, 1, R, bt0), 0);
        for (k = 0; ; k++) {
            int64_t b2, rd;
            float ve, vt, want;
            eval_at(&g_tl, onset, PP, k, NULL, 0);
            b2 = bt0 + (onset - R);
            rd = rdrv_step(&rw) ? b2 + w : b2;
            ve = psytl_value(&g_tl, 0);
            vt = psytl_value(&g_tl, 1);
            want = rd < te - dd ? 0.0f : (float)rdrv_value(&rw, rd);
            if (vt != ve) {
                if (!bad)
                    fprintf(stderr, "  yoyo STEP: trial %d lead %d/%d P %lld te %lld dd %lld frame bt %lld: tween %g, "
                                    "events %g\n", trial, lnum[li], lden[li], (long long)PP, (long long)te,
                            (long long)dd, (long long)b2, (double)vt, (double)ve);
                bad++;
            }
            if (vt != want) bad_m++;
            if (k > 0 && ve != 0.0f) n_edge++;
            if (b2 > te + 4 * dd + 3 * (PP ? PP : 20 * M)) break;
            onset += PP ? PP + rnd_range(-PP / 50, PP / 50) : rnd_range(4 * M, 20 * M);
        }
    }
    if (bad) fail_i(__LINE__, "yoyo STEP tween edges not on the events' frames", bad, 0);
    if (bad_m) fail_i(__LINE__, "yoyo STEP tween off the model at the window's end", bad_m, 0);
    if (n_edge == 0) fail(__LINE__, "yoyo STEP: no frame showed 1 (vacuous)");

    /* USAGE's 7.5 Hz flicker as a tween: 1 -> 0 -> 1, STEP, forever. At
     * 60 Hz with 1 us of onset noise frame k shows 1 when k mod 8 < 4. */
    CHECK(open_h(&g_tl, g_store, 4, 1, NULL, 0.5));
    d = twd_at(0.0f, 2 * S_NS / 30, 0);
    d.from_set = true;
    d.from = 1.0f;
    d.ease = PSYTL_EASE_STEP;
    d.yoyo = true;
    d.cycles = PSYTL_FOREVER;
    CHECK_I(psytl_tween(&g_tl, 0, 1, &d), 0);
    CHECK_I(psytl_anchor(&g_tl, 1, R, 0), 0);
    for (bt = 0; bt < 3600; bt++) {
        eval_at(&g_tl, R + (bt * NS_E9 + 30) / 60 + rnd_range(-1000, 1000), P60, bt, NULL, 0);
        if (psytl_value(&g_tl, 0) != ((bt % 8) < 4 ? 1.0f : 0.0f)) wrong++;
    }
    if (wrong) fail_i(__LINE__, "tween flicker frames off k mod 8 < 4", wrong, 0);
}

/* Retargeting a moving channel just before a frame: the next frame shows
 * where the old motion would have been, with no repeated position. */
static void test_tween_retarget(void) {
    static psytl_key kr[2];
    psytl_track tr;
    psytl_tween_desc d;
    rdrv old, r[3];
    const int64_t R = S_NS, M = MS_NS;
    float v10[3], v11[3], v12[3];
    int ch, k, bad = 0;

    g_cv_prints = 0;
    CHECK(open_h(&g_tl, g_store, 8, 4, NULL, 0.5));
    kr[0] = mkkey(0, 0.0f, PSYTL_EASE_LINEAR);
    kr[1] = mkkey(S_NS, 100.0f, PSYTL_EASE_LINEAR);
    tr = trk_keys(kr, 2);
    old = rdrv_plain(&tr);
    for (ch = 0; ch < 3; ch++) CHECK_I(psytl_set_track(&g_tl, ch, 1, &tr), 0);
    CHECK_I(psytl_anchor(&g_tl, 1, R, 0), 0);
    for (k = 0; k <= 10; k++) {
        eval_at(&g_tl, R + k * P60, P60, k, NULL, 0);
        for (ch = 0; ch < 3; ch++) bad += chk_drv(ch, &old, k * P60);
    }
    for (ch = 0; ch < 3; ch++) v10[ch] = psytl_value(&g_tl, ch);
    /* 0: start one frame on (now + P); 1: the same with keep_velocity;
     * 2: start now, the frame already shown. */
    d = twd(0.0f, 300 * M);
    d.delay = P60;
    CHECK_I(psytl_tween(&g_tl, 0, 1, &d), 0);
    rdrv_tween(&r[0], &d, 11 * P60, &old, 0.0, (double)v10[0]);
    d.keep_velocity = true;
    CHECK_I(psytl_tween(&g_tl, 1, 1, &d), 0);
    rdrv_tween(&r[1], &d, 11 * P60, &old, 0.0, (double)v10[1]);
    d = twd(0.0f, 300 * M);
    CHECK_I(psytl_tween(&g_tl, 2, 1, &d), 0);
    rdrv_tween(&r[2], &d, 10 * P60, &old, 0.0, (double)v10[2]);
    for (k = 11; k <= 40; k++) {
        eval_at(&g_tl, R + k * P60, P60, k, NULL, 0);
        for (ch = 0; ch < 3; ch++) {
            bad += chk_drv(ch, &r[ch], k * P60);
            if (k == 11) v11[ch] = psytl_value(&g_tl, ch);
            if (k == 12) v12[ch] = psytl_value(&g_tl, ch);
        }
    }
    if (bad) fail_i(__LINE__, "retargeted values off the model", bad, 0);
    CHECK_F(v11[0], 100.0 * (double)(11 * P60) / 1e9, 1e-4);   /* where the ramp would be */
    CHECK_F(v11[1], 100.0 * (double)(11 * P60) / 1e9, 1e-4);
    CHECK(v11[0] - v10[0] > 1.0f);   /* no repeated position */
    CHECK(v11[1] - v10[1] > 1.0f);
    CHECK(v12[0] < v11[0]);          /* LINEAR turns at once */
    CHECK(v12[1] - v11[1] > 1.0f);   /* keep_velocity carries on */
    CHECK(v11[2] < v10[2]);          /* from the frame shown, toward 0 */
    CHECK_F(v11[2], (double)v10[2] * (1.0 - (double)P60 / 3e8), 1e-4);
}

static void rej_tw(int line, int ch, int base, const psytl_tween_desc* d) {
    int ret = psytl_tween(&g_tl, ch, base, d);
    if (ret != PSYTL_ERR_ARG) fail_i(line, "psytl_tween did not reject with PSYTL_ERR_ARG", ret, PSYTL_ERR_ARG);
}

static void acc_tw(int line, int ch, const psytl_tween_desc* d) {
    int ret = psytl_tween(&g_tl, ch, 1, d);
    if (ret != 0) fail_i(line, "psytl_tween rejected a good desc", ret, 0);
}

/* Every rejection TWEENS lists, each alone; none changes the channel. */
static void test_tween_reject(void) {
    static psytl_key kr[2];
    const int64_t M = MS_NS, R = S_NS;
    const int CYC_MIN = -2147483647 - 1;
    psytl_tween_desc g, b;
    int pass, ch;

    kr[0] = mkkey(0, 0.0f, PSYTL_EASE_LINEAR);
    kr[1] = mkkey(S_NS, 10.0f, PSYTL_EASE_LINEAR);
    for (pass = 0; pass < 2; pass++) {
        /* 0: a channel with a track and a waiting tween; 1: a free one. */
        ch = pass;
        tw_open();
        CHECK_I(psytl_set_keys(&g_tl, 0, 1, kr, 2), 0);
        g = twd_at(50.0f, 0, 300 * M);
        CHECK_I(psytl_tween(&g_tl, 0, 1, &g), 0);
        g = twd_at(2.0f, 100 * M, 200 * M);
        CHECK_I(psytl_tween(&g_tl, ch, 1, NULL), PSYTL_ERR_ARG);
        b = g; b.duration = -1; rej_tw(__LINE__, ch, 1, &b);
        b = g; b.start = LIM62; rej_tw(__LINE__, ch, 1, &b);
        b = g; b.start = -LIM62; rej_tw(__LINE__, ch, 1, &b);
        b = g; b.delay = LIM62; rej_tw(__LINE__, ch, 1, &b);
        b = g; b.delay = -LIM62; rej_tw(__LINE__, ch, 1, &b);
        b = g; b.start_set = false; b.delay = LIM62; rej_tw(__LINE__, ch, 1, &b);
        b = g; b.start = LIM62 - 100 * M; rej_tw(__LINE__, ch, 1, &b);   /* start + duration at 2^62 */
        b = g; b.start = LIM62 - 200 * M; b.yoyo = true; rej_tw(__LINE__, ch, 1, &b);   /* x 2 */
        b = g; b.to = (float)make_nan(); rej_tw(__LINE__, ch, 1, &b);
        b = g; b.to = (float)make_inf(); rej_tw(__LINE__, ch, 1, &b);
        b = g; b.to = (float)-make_inf(); rej_tw(__LINE__, ch, 1, &b);
        b = g; b.from_set = true; b.from = (float)make_nan(); rej_tw(__LINE__, ch, 1, &b);
        b = g; b.from_set = true; b.from = (float)make_inf(); rej_tw(__LINE__, ch, 1, &b);
        b = g; b.from_set = true; b.from = (float)-make_inf(); rej_tw(__LINE__, ch, 1, &b);
        b = g; b.ease = 7; rej_tw(__LINE__, ch, 1, &b);
        b = g; b.ease = -1; rej_tw(__LINE__, ch, 1, &b);
        b = g; b.ease = 255; rej_tw(__LINE__, ch, 1, &b);
        b = g; b.ease = PSYTL_EASE_BEZIER; b.curve = mkcurve(1.5f, 0.0f, 0.5f, 1.0f); rej_tw(__LINE__, ch, 1, &b);
        b = g; b.ease = PSYTL_EASE_BEZIER; b.curve = mkcurve(0.5f, 0.0f, -0.25f, 1.0f); rej_tw(__LINE__, ch, 1, &b);
        b = g; b.ease = PSYTL_EASE_BEZIER; b.curve = mkcurve(0.5f, 0.0f, 1.0000001f, 1.0f); rej_tw(__LINE__, ch, 1, &b);
        b = g; b.ease = PSYTL_EASE_BEZIER; b.curve = mkcurve((float)make_nan(), 0.0f, 0.5f, 1.0f); rej_tw(__LINE__, ch, 1, &b);
        b = g; b.ease = PSYTL_EASE_BEZIER; b.curve = mkcurve(0.5f, (float)make_nan(), 0.5f, 1.0f); rej_tw(__LINE__, ch, 1, &b);
        b = g; b.ease = PSYTL_EASE_BEZIER; b.curve = mkcurve(0.5f, 0.0f, 0.5f, (float)make_inf()); rej_tw(__LINE__, ch, 1, &b);
        b = g; b.ease = PSYTL_EASE_LOG; b.to = 5.0f; rej_tw(__LINE__, ch, 1, &b);   /* no from_set */
        b = g; b.ease = PSYTL_EASE_LOG; b.from_set = true; b.from = 2.0f; b.to = 0.0f; rej_tw(__LINE__, ch, 1, &b);
        b = g; b.ease = PSYTL_EASE_LOG; b.from_set = true; b.from = 2.0f; b.to = -1.0f; rej_tw(__LINE__, ch, 1, &b);
        b = g; b.ease = PSYTL_EASE_LOG; b.from_set = true; b.from = 0.0f; b.to = 5.0f; rej_tw(__LINE__, ch, 1, &b);
        b = g; b.ease = PSYTL_EASE_LOG; b.from_set = true; b.from = -3.0f; b.to = 5.0f; rej_tw(__LINE__, ch, 1, &b);
        b = g; b.cycles = -2; rej_tw(__LINE__, ch, 1, &b);
        b = g; b.cycles = CYC_MIN; rej_tw(__LINE__, ch, 1, &b);
        b = g; b.duration = 0; b.cycles = 2; rej_tw(__LINE__, ch, 1, &b);
        b = g; b.duration = 0; b.cycles = PSYTL_FOREVER; rej_tw(__LINE__, ch, 1, &b);
        b = g; b.duration = 0; b.yoyo = true; rej_tw(__LINE__, ch, 1, &b);
        b = g; b.keep_velocity = true; b.ease = PSYTL_EASE_COSINE; rej_tw(__LINE__, ch, 1, &b);
        b = g; b.keep_velocity = true; b.ease = PSYTL_EASE_STEP; rej_tw(__LINE__, ch, 1, &b);
        b = g; b.keep_velocity = true; b.ease = PSYTL_EASE_BEZIER; b.curve = mkcurve(0.42f, 0.0f, 0.58f, 1.0f); rej_tw(__LINE__, ch, 1, &b);
        b = g; b.keep_velocity = true; b.from_set = true; b.from = 1.0f; rej_tw(__LINE__, ch, 1, &b);
        b = g; b.keep_velocity = true; b.yoyo = true; rej_tw(__LINE__, ch, 1, &b);
        b = g; b.keep_velocity = true; b.cycles = 2; rej_tw(__LINE__, ch, 1, &b);
        b = g; b.keep_velocity = true; b.cycles = PSYTL_FOREVER; rej_tw(__LINE__, ch, 1, &b);   /* literal: more than one */
        b = g; b.keep_velocity = true; b.duration = 0; rej_tw(__LINE__, ch, 1, &b);
        /* Not TWEENS', but arguments. */
        rej_tw(__LINE__, -1, 1, &g);
        rej_tw(__LINE__, 8, 1, &g);
        rej_tw(__LINE__, ch, -1, &g);
        rej_tw(__LINE__, ch, PSYTL_MAX_BASES, &g);
        if (pass == 0) {
            /* The track and the waiting tween are as they were. */
            CHECK_I(psytl_anchor(&g_tl, 1, R, 0), 0);
            eval_at(&g_tl, R + 290 * M, P60, 0, NULL, 0);
            CHECK_F(psytl_value(&g_tl, 0), 2.9, 1e-6);
            eval_at(&g_tl, R + 300 * M, P60, 1, NULL, 0);
            CHECK(psytl_value(&g_tl, 0) == 50.0f);
        } else {
            /* Still free, and at its value. */
            CHECK(psytl_value(&g_tl, 1) == 0.5f);
            CHECK(add1(&g_tl, 2, 0, PSYTL_SET, 1, 1.0f) >= 0);
        }
    }

    /* Accepted edges, each on channel 7 (each replaces the last). */
    tw_open();
    g = twd_at(2.0f, 100 * M, 200 * M);
    b = g; b.start = LIM62 - 100 * M - 1; acc_tw(__LINE__, 7, &b);
    b = g; b.start = LIM62 - 200 * M - 1; b.yoyo = true; acc_tw(__LINE__, 7, &b);
    b = g; b.start = -LIM62 + 1; acc_tw(__LINE__, 7, &b);
    b = g; b.start = 0; b.delay = -LIM62 + 1; acc_tw(__LINE__, 7, &b);
    b = g; b.delay = -150 * M; acc_tw(__LINE__, 7, &b);
    b = g; b.duration = 0; acc_tw(__LINE__, 7, &b);
    /* Literal: from is checked only with from_set, the curve only with
     * BEZIER, and cycles alone are not multiplied into the time limit. */
    b = g; b.from = (float)make_nan(); acc_tw(__LINE__, 7, &b);
    b = g; b.curve = mkcurve(2.0f, (float)make_nan(), -1.0f, 0.0f); acc_tw(__LINE__, 7, &b);
    b = g; b.start = LIM62 - 200 * M - 1; b.cycles = 5; acc_tw(__LINE__, 7, &b);
    b = g; b.ease = PSYTL_EASE_BEZIER; acc_tw(__LINE__, 7, &b);   /* cubic-bezier(0, 0, 0, 0) is a curve */
    b = g; b.ease = PSYTL_EASE_LOG; b.from_set = true; b.from = 2.0f; b.to = 5.0f; acc_tw(__LINE__, 7, &b);
    b = g; b.keep_velocity = true; b.cycles = 1; acc_tw(__LINE__, 7, &b);
    b = g; b.keep_velocity = true; acc_tw(__LINE__, 7, &b);
    b = g; b.cycles = PSYTL_FOREVER; acc_tw(__LINE__, 7, &b);
    b = g; b.cycles = 1000000; b.yoyo = true; acc_tw(__LINE__, 7, &b);
    b = g; b.ease = PSYTL_EASE_STEP; b.yoyo = true; acc_tw(__LINE__, 7, &b);
    b = g; b.to = -3.4e38f; acc_tw(__LINE__, 7, &b);
}

/* Cancellation and replacement: set_track (NULL and set_keys too) cancels
 * a waiting tween; a second tween before the first starts replaces it,
 * later or earlier; a clear of the base cancels it (literal: the channel
 * is freed). */
static void test_tween_cancel(void) {
    static psytl_key kr[2], kn[2];
    const int64_t M = MS_NS, R = S_NS;
    psytl_track tr, tn;
    psytl_tween_desc d;
    int64_t bt;
    int ch;

    kr[0] = mkkey(0, 0.0f, PSYTL_EASE_LINEAR);
    kr[1] = mkkey(S_NS, 10.0f, PSYTL_EASE_LINEAR);
    kn[0] = mkkey(0, 0.0f, PSYTL_EASE_LINEAR);
    kn[1] = mkkey(S_NS, -10.0f, PSYTL_EASE_LINEAR);
    tr = trk_keys(kr, 2);
    tn = trk_keys(kn, 2);
    tw_open();
    for (ch = 0; ch < 7; ch++)
        if (ch != 2 && ch != 5) CHECK_I(psytl_set_track(&g_tl, ch, 1, &tr), 0);
    d = twd_at(50.0f, 0, 300 * M);
    for (ch = 0; ch < 7; ch++)
        if (ch != 5) CHECK_I(psytl_tween(&g_tl, ch, 1, &d), 0);
    CHECK_I(psytl_tween(&g_tl, 5, 2, &d), 0);
    CHECK_I(psytl_anchor(&g_tl, 1, R, 0), 0);
    CHECK_I(psytl_anchor(&g_tl, 2, R, 0), 0);
    for (bt = 0; bt <= 600 * M; bt += 10 * M) {
        double ramp = 10.0 * (double)bt / 1e9;
        eval_at(&g_tl, R + bt, P60, bt / (10 * M), NULL, 0);
        if (bt <= 200 * M) {
            for (ch = 0; ch < 7; ch++)
                if (ch != 2 && ch != 5) CHECK_F(psytl_value(&g_tl, ch), ramp, 1e-6);
            CHECK(psytl_value(&g_tl, 2) == 0.5f);
            CHECK(psytl_value(&g_tl, 5) == 0.5f);
        } else {
            CHECK_F(psytl_value(&g_tl, 0), -ramp, 1e-6);   /* the new track, no takeover */
            CHECK(psytl_value(&g_tl, 1) == 0.5f);           /* removed: initial, no takeover */
            CHECK(psytl_value(&g_tl, 2) == 0.5f);
            if (bt < 400 * M) CHECK_F(psytl_value(&g_tl, 3), ramp, 1e-6);
            else CHECK(psytl_value(&g_tl, 3) == -50.0f);
            if (bt < 250 * M) CHECK_F(psytl_value(&g_tl, 4), ramp, 1e-6);
            else CHECK(psytl_value(&g_tl, 4) == -50.0f);
            CHECK(psytl_value(&g_tl, 5) == 0.5f);
            CHECK_F(psytl_value(&g_tl, 6), -ramp, 1e-6);
        }
        if (bt == 200 * M) {
            CHECK_I(psytl_set_track(&g_tl, 0, 1, &tn), 0);
            CHECK_I(psytl_set_track(&g_tl, 1, 1, NULL), 0);
            CHECK(psytl_value(&g_tl, 1) == 0.5f);
            CHECK_I(psytl_set_keys(&g_tl, 2, 1, NULL, 0), 0);
            d = twd_at(-50.0f, 0, 400 * M);
            CHECK_I(psytl_tween(&g_tl, 3, 1, &d), 0);
            d = twd_at(-50.0f, 0, 250 * M);
            CHECK_I(psytl_tween(&g_tl, 4, 1, &d), 0);
            CHECK_I(psytl_clear(&g_tl, 2), 0);
            CHECK(psytl_value(&g_tl, 5) == 0.5f);
            CHECK_I(psytl_set_keys(&g_tl, 6, 1, kn, 2), 0);
        }
    }
    /* Removed, set_keys'd away and cleared channels are free. */
    CHECK(add1(&g_tl, 3, 0, PSYTL_SET, 1, 1.0f) >= 0);
    CHECK(add1(&g_tl, 3, 0, PSYTL_SET, 2, 1.0f) >= 0);
    CHECK(add1(&g_tl, 3, 0, PSYTL_SET, 5, 1.0f) >= 0);
}

/* A track on another base stops at once at its last value; a tween on
 * another base replaces a waiting one. */
static void test_tween_cross_base(void) {
    static psytl_key kb[2], kr[2];
    const int64_t M = MS_NS, R = S_NS;
    psytl_tween_desc d;
    int64_t bt;
    double u;

    kb[0] = mkkey(5 * S_NS, 0.0f, PSYTL_EASE_LINEAR);
    kb[1] = mkkey(6 * S_NS, 10.0f, PSYTL_EASE_LINEAR);
    kr[0] = mkkey(0, 0.0f, PSYTL_EASE_LINEAR);
    kr[1] = mkkey(S_NS, 10.0f, PSYTL_EASE_LINEAR);
    tw_open();
    CHECK_I(psytl_set_keys(&g_tl, 0, 2, kb, 2), 0);
    CHECK_I(psytl_set_keys(&g_tl, 1, 2, kb, 2), 0);
    CHECK_I(psytl_set_keys(&g_tl, 2, 1, kr, 2), 0);
    d = twd_at(50.0f, 0, 300 * M);
    CHECK_I(psytl_tween(&g_tl, 2, 1, &d), 0);
    CHECK_I(psytl_anchor(&g_tl, 1, R, 0), 0);
    CHECK_I(psytl_anchor(&g_tl, 2, R, 5 * S_NS), 0);
    for (bt = 0; bt <= 100 * M; bt += 10 * M) eval_at(&g_tl, R + bt, P60, bt / (10 * M), NULL, 0);
    CHECK_F(psytl_value(&g_tl, 0), 1.0, 1e-6);
    CHECK_F(psytl_value(&g_tl, 2), 1.0, 1e-6);
    d = twd_at(20.0f, 100 * M, 300 * M);
    CHECK_I(psytl_tween(&g_tl, 0, 1, &d), 0);
    CHECK_F(psytl_value(&g_tl, 0), 1.0, 1e-6);   /* stopped, at its value */
    d.keep_velocity = true;
    CHECK_I(psytl_tween(&g_tl, 1, 1, &d), 0);
    /* Channel 2: a tween on base 2 replaces the waiting one on base 1 and
     * stops the base-1 track. */
    d = twd_at(-5.0f, 0, 5 * S_NS + 200 * M);
    CHECK_I(psytl_tween(&g_tl, 2, 2, &d), 0);
    CHECK_I(add1(&g_tl, 2, 0, PSYTL_SET, 0, 1.0f), PSYTL_ERR_BOUND);
    CHECK_I(add1(&g_tl, 1, 0, PSYTL_SET, 0, 1.0f), PSYTL_ERR_BOUND);
    for (bt = 110 * M; bt <= 500 * M; bt += 10 * M) {
        float v0, v1, v2;
        eval_at(&g_tl, R + bt, P60, bt / (10 * M), NULL, 0);
        v0 = psytl_value(&g_tl, 0);
        v1 = psytl_value(&g_tl, 1);
        v2 = psytl_value(&g_tl, 2);
        if (bt < 300 * M) {
            CHECK_F(v0, 1.0, 1e-6);
            CHECK_F(v1, 1.0, 1e-6);
        } else {
            u = bt >= 400 * M ? 1.0 : (double)(bt - 300 * M) / 1e8;
            CHECK_F(v0, 1.0 + 19.0 * u, 1e-5);
            /* Literal: a stopped track has no velocity, so keep_velocity
             * starts at rest. */
            CHECK_F(v1, 1.0 + 19.0 * (3.0 * u * u - 2.0 * u * u * u), 1e-5);
        }
        if (bt < 200 * M) CHECK_F(v2, 1.0, 1e-6);
        else CHECK(v2 == -5.0f);
    }
}

/* A rewind does not undo a takeover; a waiting tween survives a rewind. */
static void test_tween_rewind(void) {
    static psytl_key kr[2];
    const int64_t M = MS_NS, R = S_NS;
    psytl_track tr;
    psytl_tween_desc d;
    rdrv old, r0;
    int64_t bt, onset = R, idx = 0;
    int bad = 0;

    g_cv_prints = 0;
    kr[0] = mkkey(0, 0.0f, PSYTL_EASE_LINEAR);
    kr[1] = mkkey(S_NS, 10.0f, PSYTL_EASE_LINEAR);
    tr = trk_keys(kr, 2);
    old = rdrv_plain(&tr);
    tw_open();
    CHECK_I(psytl_set_track(&g_tl, 0, 1, &tr), 0);
    d = twd_at(50.0f, 200 * M, 300 * M);
    CHECK_I(psytl_tween(&g_tl, 0, 1, &d), 0);
    rdrv_tween(&r0, &d, 300 * M, &old, 0.0, 0.0);
    CHECK_I(psytl_anchor(&g_tl, 1, R, 0), 0);
    for (bt = 0; bt <= 600 * M; bt += 10 * M) {
        onset = R + bt;
        eval_at(&g_tl, onset, P60, idx++, NULL, 0);
        bad += chk_drv(0, bt < 300 * M ? &old : &r0, bt);
    }
    onset += 10 * M;
    CHECK_I(psytl_anchor(&g_tl, 1, onset, 0), 0);
    for (bt = 0; bt <= 500 * M; bt += 10 * M) {
        eval_at(&g_tl, onset + bt, P60, idx++, NULL, 0);
        bad += chk_drv(0, &r0, bt);
        if (bt == 100 * M) CHECK_F(psytl_value(&g_tl, 0), 3.0, 1e-6);   /* the tween's from, not the ramp's 1 */
        if (bt == 400 * M) CHECK_F(psytl_value(&g_tl, 0), 26.5, 1e-5);
    }
    if (bad) fail_i(__LINE__, "values after a rewind off the takeover", bad, 0);

    /* A tween still waiting survives a rewind and takes over at its start
     * from the old track's value there. */
    tw_open();
    CHECK_I(psytl_set_track(&g_tl, 0, 1, &tr), 0);
    d = twd_at(50.0f, 0, 300 * M);
    CHECK_I(psytl_tween(&g_tl, 0, 1, &d), 0);
    CHECK_I(psytl_anchor(&g_tl, 1, R, 0), 0);
    for (bt = 0; bt <= 200 * M; bt += 10 * M) {
        onset = R + bt;
        eval_at(&g_tl, onset, P60, idx++, NULL, 0);
    }
    onset += 10 * M;
    CHECK_I(psytl_anchor(&g_tl, 1, onset, 0), 0);
    for (bt = 0; bt <= 400 * M; bt += 10 * M) {
        eval_at(&g_tl, onset + bt, P60, idx++, NULL, 0);
        if (bt < 300 * M) CHECK_F(psytl_value(&g_tl, 0), 10.0 * (double)bt / 1e9, 1e-6);
        else CHECK(psytl_value(&g_tl, 0) == 50.0f);
    }
}

/* ----------------------------------- 5g. evaluate writes every kind of track */

static int te_check(const psytl_track* trs, int64_t onset, int64_t P, int64_t idx) {
    int ch, bad = 0;
    eval_at(&g_tl, onset, P, idx, NULL, 0);
    for (ch = 0; ch < 6; ch++) bad += chan_vs(ch, &trs[ch], read_bt(2, &trs[ch], onset, P / 2));
    return bad;
}

static void test_track_eval(void) {
    static psytl_key k0[5], k3[4], tk[2];
    static psytl_curve c0[2], tc;
    static float s1[400], s2[30000], s5[50];
    static psytl_track trs[6];
    static const int64_t rates[3] = { 1000000, 16666667, 6944444 };
    const int64_t M = MS_NS;
    float init[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.75f, 0.0f };
    int64_t onset = S_NS, idx = 0, P, bt = 0;
    float prev[6];
    int i, k, ch, bad = 0, bad_p = 0, seq;

    g_cv_prints = 0;
    for (i = 0; i < 400; i++) s1[i] = (float)(3.0 * sin(0.05 * (double)i) + 0.5 * cos(0.31 * (double)i));
    for (i = 0; i < 30000; i++) s2[i] = (float)((i * 7919) % 1000) / 100.0f;
    for (i = 0; i < 50; i++) s5[i] = (float)(i % 7) - 3.0f;
    c0[0] = mkcurve(0.68f, -0.6f, 0.32f, 1.6f);
    c0[1] = mkcurve(0.42f, 0.0f, 0.58f, 1.0f);
    k0[0] = mkbez(0, 1.0f, 1);
    k0[1] = mkkey(300 * M, 5.0f, PSYTL_EASE_COSINE);
    k0[2] = mkbez(500 * M, 2.0f, 0);
    k0[3] = mkkey(900 * M, 3.0f, PSYTL_EASE_QUAD_IN);
    k0[4] = mkkey(1200 * M, 4.0f, PSYTL_EASE_LINEAR);
    trs[0] = trk_keys(k0, 5);
    trs[0].curves = c0;
    trs[0].n_curves = 2;
    trs[1] = trk_samples(s1, 400, -500 * M, 120, PSYTL_INTERP_CUBIC);
    trs[2] = trk_samples(s2, 30000, 1 * M, 999983, PSYTL_INTERP_LINEAR);
    trs[2].period = 37 * M;
    k3[0] = mkkey(200 * M, 0.0f, PSYTL_EASE_STEP);
    k3[1] = mkkey(300 * M, 1.0f, PSYTL_EASE_QUAD_IN);
    k3[2] = mkkey(400 * M, 3.0f, PSYTL_EASE_LINEAR);
    k3[3] = mkkey(450 * M, 2.0f, PSYTL_EASE_LINEAR);
    trs[3] = trk_keys(k3, 4);
    trs[3].period = 250 * M;
    trs[3].repeats = 7;
    trs[5] = trk_samples(s5, 50, -2 * S_NS, 7, PSYTL_INTERP_STEP);   /* a STEP track */
    trs[5].period = 3 * S_NS + 1;
    trs[5].repeats = 2;

    CHECK(open_h(&g_tl, g_store, 8, 6, init, 0.5));
    for (ch = 0; ch < 6; ch++)
        if (ch != 4) CHECK_I(psytl_set_track(&g_tl, ch, 2, &trs[ch]), 0);
    {
        /* No track before it: from is the channel's value, 0.75, held
         * until the start, as the tween's first key gives before it. */
        psytl_tween_desc d = twd_at(-3.0f, 700 * M, 100 * M);
        d.ease = PSYTL_EASE_QUAD_OUT;
        CHECK_I(psytl_tween(&g_tl, 4, 2, &d), 0);
    }
    trs[4] = tween_track(tk, &tc, 100 * M, 700 * M, 0.75f, -3.0f, PSYTL_EASE_QUAD_OUT, NULL);
    for (ch = 0; ch < 6; ch++) CHECK(psytl_value(&g_tl, ch) == init[ch]);   /* nothing before an evaluate */
    CHECK_I(psytl_anchor(&g_tl, 2, onset, -600 * M), 0);

    g_rng = 2468;
    for (seq = 0; seq < 6; seq++) {
        int nf = seq == 0 ? 3000 : seq == 1 ? 300 : seq == 2 ? 500 : seq == 3 ? 1000 : seq == 4 ? 2000 : 1500;
        P = seq == 0 ? rates[0] : seq == 4 ? rates[2] : rates[1];
        if (seq == 4) CHECK_I(psytl_anchor(&g_tl, 2, onset, -100 * M), 0);
        for (k = 0; k < nf; k++) {
            if (seq == 2) {   /* backward, a frame at a time */
                CHECK(psytl_base_time(&g_tl, 2, onset, &bt));
                CHECK_I(psytl_anchor(&g_tl, 2, onset, bt - rnd_range(0, 20 * M)), 0);
            } else if (seq == 3) {   /* jumps, a quarter of them onto keys and samples */
                int r = rnd_int(4);
                if (r == 0) bt = k0[rnd_int(5)].time + rnd_range(-1, 1);
                else if (r == 1) bt = k3[rnd_int(4)].time + rnd_range(-1, 1) + (int64_t)rnd_int(9) * 250 * M;
                else bt = rnd_range(-3 * S_NS, 10 * S_NS);
                CHECK_I(psytl_anchor(&g_tl, 2, onset, bt), 0);
            } else if (seq == 5) {   /* small steps within segments, then a jump */
                if (k % 100 == 0) CHECK_I(psytl_anchor(&g_tl, 2, onset, rnd_range(-S_NS, 8 * S_NS)), 0);
            }
            bad += te_check(trs, onset, P, idx++);
            onset += seq == 3 ? rnd_range(1, P) : seq == 5 ? rnd_range(0, 3 * M) : P;
        }
    }
    if (bad) fail_i(__LINE__, "tracked channels off psytl_sample at the read time", bad, 0);

    /* Paused: every track at the frozen time, frame after frame. */
    CHECK_I(psytl_pause(&g_tl, 2, onset), 0);
    bad = te_check(trs, onset, P60, idx++);
    for (ch = 0; ch < 6; ch++) prev[ch] = psytl_value(&g_tl, ch);
    for (k = 0; k < 20; k++) {
        onset += P60;
        bad += te_check(trs, onset, P60, idx++);
        for (ch = 0; ch < 6; ch++)
            if (psytl_value(&g_tl, ch) != prev[ch]) bad_p++;
    }
    if (bad) fail_i(__LINE__, "paused tracked channels off psytl_sample", bad, 0);
    if (bad_p) fail_i(__LINE__, "paused tracked channels moved", bad_p, 0);
}

/* ------------------------------------- 5h. STEP tracks at the lead window */

/* A STEP track is read at the end of the lead window, so its change
 * lands on the frame an event at the same time lands on, at any lead; a
 * LINEAR track with the same keys, and a mixed track, at the onset. */
static void test_step_read(void) {
    static psytl_key ks[2], kl[2], km[4], tk[2];
    static float s01[2] = { 0.0f, 1.0f };
    static psytl_curve tc;
    static const int lnum[6] = { 0, 1, 1, 3, 999, 1 }, lden[6] = { 1, 4, 2, 4, 1000, 3 };
    const int64_t M = MS_NS, R = S_NS;
    psytl_track trs[6];
    psytl_tween_desc td;
    int trial, ch, bad = 0, bad_lin = 0, early = 0, lead_frames = 0;

    /* By hand, P = 10 ms: STEP keys change at 25 ms. Lead 0: the frame at
     * 30 ms shows it; lead 0.5: the frame at 20 ms (window end 25 ms). */
    for (trial = 0; trial < 2; trial++) {
        int64_t bt;
        ks[0] = mkkey(15 * M, 0.0f, PSYTL_EASE_STEP);
        ks[1] = mkkey(25 * M, 1.0f, PSYTL_EASE_LINEAR);   /* the last key's ease is not used */
        kl[0] = mkkey(15 * M, 0.0f, PSYTL_EASE_LINEAR);
        kl[1] = mkkey(25 * M, 1.0f, PSYTL_EASE_LINEAR);
        km[0] = mkkey(0, 0.0f, PSYTL_EASE_LINEAR);
        km[1] = mkkey(10 * M, 0.0f, PSYTL_EASE_STEP);
        km[2] = mkkey(25 * M, 1.0f, PSYTL_EASE_LINEAR);
        km[3] = mkkey(40 * M, 1.0f, PSYTL_EASE_LINEAR);
        CHECK(open_h(&g_tl, g_store, 8, 7, NULL, trial ? 0.5 : 0.0));
        CHECK_I(psytl_set_keys(&g_tl, 0, 1, ks, 2), 0);
        CHECK_I(psytl_set_keys(&g_tl, 1, 1, kl, 2), 0);
        CHECK_I(psytl_set_keys(&g_tl, 2, 1, km, 4), 0);
        td = twd_at(1.0f, 10 * M, 15 * M);
        td.ease = PSYTL_EASE_STEP;
        CHECK_I(psytl_tween(&g_tl, 3, 1, &td), 0);
        td = twd_at(1.0f, 0, 25 * M);
        td.ease = PSYTL_EASE_STEP;
        CHECK_I(psytl_tween(&g_tl, 4, 1, &td), 0);
        td.ease = PSYTL_EASE_LINEAR;
        CHECK_I(psytl_tween(&g_tl, 5, 1, &td), 0);
        CHECK(add1(&g_tl, 1, 25 * M, PSYTL_SET, 6, 1.0f) >= 0);
        CHECK_I(psytl_anchor(&g_tl, 1, R, 0), 0);
        for (bt = 0; bt <= 40 * M; bt += 10 * M) {
            float want_step = (trial ? bt + 5 * M : bt) >= 25 * M ? 1.0f : 0.0f;
            eval_at(&g_tl, R + bt, 10 * M, bt / (10 * M), NULL, 0);
            CHECK(psytl_value(&g_tl, 0) == want_step);
            CHECK(psytl_value(&g_tl, 3) == want_step);   /* tween with STEP ease */
            CHECK(psytl_value(&g_tl, 4) == want_step);   /* STEP jump, duration 0 */
            CHECK(psytl_value(&g_tl, 6) == want_step);   /* the event */
            CHECK(psytl_value(&g_tl, 1) == (float)ksample(kl, 2, bt));   /* at the onset */
            CHECK(psytl_value(&g_tl, 2) == (float)ksample(km, 4, bt));   /* mixed: at the onset */
            CHECK(psytl_value(&g_tl, 5) == (bt >= 25 * M ? 1.0f : 0.0f));   /* LINEAR jump: at the onset */
            if (bt == 20 * M) {
                CHECK_F(psytl_value(&g_tl, 1), 0.5, 1e-6);
                CHECK(psytl_value(&g_tl, 2) == 0.0f);
            }
        }
    }

    /* Random: change time te, lead, period (0 is variable refresh), and
     * onset noise. Channel 0 an event at te; 1 keyed STEP keys; 2 sampled
     * STEP at 1e9/s; 3 sampled STEP at 7/s with sample 1 just before te;
     * 4 a STEP tween; 5 LINEAR with channel 1's keys, read at the onset. */
    g_rng = 8080;
    for (trial = 0; trial < 300; trial++) {
        int li = trial % 6;
        int64_t P = trial % 7 == 0 ? 0 : trial % 3 == 0 ? P60 : trial % 3 == 1 ? 6944444 : 2000000;
        int64_t te = rnd_range(0, 50 * P60), d = rnd_range(1, 3 * P60), onset = R, bt0 = rnd_range(-3 * P60, 0);
        int64_t k;
        CHECK(open_h(&g_tl, g_store, 8, 6, NULL, (double)lnum[li] / (double)lden[li]));
        ks[0] = mkkey(te - d, 0.0f, PSYTL_EASE_STEP);
        ks[1] = mkkey(te, 1.0f, PSYTL_EASE_LINEAR);
        kl[0] = mkkey(te - d, 0.0f, PSYTL_EASE_LINEAR);
        kl[1] = mkkey(te, 1.0f, PSYTL_EASE_LINEAR);
        trs[1] = trk_keys(ks, 2);
        trs[2] = trk_samples(s01, 2, te - 1, NS_E9, PSYTL_INTERP_STEP);
        trs[3] = trk_samples(s01, 2, te - 142857143, 7, PSYTL_INTERP_STEP);   /* sample 1 at te - 1/7 ns */
        trs[5] = trk_keys(kl, 2);
        CHECK(add1(&g_tl, 1, te, PSYTL_SET, 0, 1.0f) >= 0);
        for (ch = 1; ch < 6; ch++)
            if (ch != 4) CHECK_I(psytl_set_track(&g_tl, ch, 1, &trs[ch]), 0);
        td = twd_at(1.0f, 5, te - 5);
        td.ease = PSYTL_EASE_STEP;
        CHECK_I(psytl_tween(&g_tl, 4, 1, &td), 0);
        trs[4] = tween_track(tk, &tc, te - 5, 5, 0.0f, 1.0f, PSYTL_EASE_STEP, NULL);
        CHECK_I(psytl_anchor(&g_tl, 1, R, bt0), 0);
        for (k = 0; ; k++) {
            int64_t bt;
            float ve;
            eval_at(&g_tl, onset, P, k, NULL, 0);
            bt = bt0 + (onset - R);
            ve = psytl_value(&g_tl, 0);
            for (ch = 1; ch < 5; ch++) {
                if (psytl_value(&g_tl, ch) != ve) {
                    if (!bad) fprintf(stderr, "  step read: trial %d lead %d/%d P %lld te %lld frame bt %lld: "
                                      "channel %d %g, event channel %g\n", trial, lnum[li], lden[li],
                                      (long long)P, (long long)te, (long long)bt, ch,
                                      (double)psytl_value(&g_tl, ch), (double)ve);
                    bad++;
                }
            }
            if (psytl_value(&g_tl, 5) != (float)psytl_sample(&trs[5], bt)) bad_lin++;
            if (ve == 1.0f && bt < te) early++;
            if (bt > te + 3 * (P ? P : 20 * M)) break;
            if (lnum[li] > 0 && P > 0) lead_frames++;
            onset += P ? P + rnd_range(-P / 50, P / 50) : rnd_range(4 * M, 20 * M);
        }
    }
    if (bad) fail_i(__LINE__, "STEP track changes not on the event's frame", bad, 0);
    if (bad_lin) fail_i(__LINE__, "LINEAR track with STEP's keys not read at the onset", bad_lin, 0);
    if (early == 0 || lead_frames == 0) fail(__LINE__, "step read: no frame showed a change early (vacuous)");
}

/* --------------------------------------------------------- 6. the report */

static int g_rep[8];

static void report_setup(void) {
    CHECK(open_h(&g_tl, g_store, 32, 2, NULL, 0.0));
    CHECK_I(psytl_anchor(&g_tl, 1, 1000, 0), 0);
    CHECK_I(psytl_anchor(&g_tl, 2, 2000, 500), 0);
    g_rep[0] = add1(&g_tl, 0, 1300, PSYTL_MARK, 0, 0.0f);    /* RT 1300 */
    g_rep[1] = add1(&g_tl, 1, 100, PSYTL_MARK, 0, 0.0f);     /* RT 1100 */
    g_rep[2] = add1(&g_tl, 2, 0, PSYTL_MARK, 0, 0.0f);       /* RT 1500 */
    g_rep[3] = add1(&g_tl, 1, 300, PSYTL_MARK, 0, 0.0f);     /* RT 1300 */
    g_rep[4] = add1(&g_tl, 0, 1100, PSYTL_MARK, 0, 0.0f);    /* RT 1100 */
    g_rep[5] = add1(&g_tl, 2, -400, PSYTL_MARK, 0, 0.0f);    /* RT 1100 */
    g_rep[6] = add1(&g_tl, 0, 1050, PSYTL_TRIGGER, 0, 0.0f); /* RT 1050 */
    g_rep[7] = add1(&g_tl, 1, 5000, PSYTL_MARK, 0, 0.0f);    /* RT 6000, pending */
}

static void test_report(void) {
    static const int order[7] = { 6, 1, 4, 5, 0, 3, 2 };
    static const int64_t rt[7] = { 1050, 1100, 1100, 1100, 1300, 1300, 1500 };
    int i, n;

    report_setup();
    n = eval_at(&g_tl, 3000, 0, 7, g_fired, 16);
    CHECK_I(n, 7);
    for (i = 0; i < 7 && i < n; i++) {
        CHECK_I(g_fired[i].id, g_rep[order[i]]);
        CHECK_I(g_fired[i].onset - g_fired[i].residual, rt[i]);
        CHECK_I(g_fired[i].frame, 7);
    }
    CHECK(is_pending_reset(psytl_find(&g_tl, g_rep[7])));

    report_setup();
    for (i = 0; i < 8; i++) g_fired[i].id = -999;
    n = eval_at(&g_tl, 3000, 0, 7, g_fired, 3);
    CHECK_I(n, 7);
    CHECK_I(g_fired[0].id, g_rep[6]);
    CHECK_I(g_fired[1].id, g_rep[1]);
    CHECK_I(g_fired[2].id, g_rep[4]);
    CHECK_I(g_fired[3].id, -999);
    for (i = 0; i < 7; i++) {
        const psytl_event* e = psytl_find(&g_tl, g_rep[i]);
        CHECK(e && (e->flags & PSYTL_EV_FIRED) && e->frame == 7);
    }

    report_setup();
    CHECK_I(eval_at(&g_tl, 3000, 0, 7, NULL, 0), 7);
    CHECK_I(eval_at(&g_tl, 2999, 0, 8, NULL, 0), PSYTL_ERR_ORDER);
    CHECK_I(eval_at(&g_tl, 3000, -1, 8, NULL, 0), PSYTL_ERR_ARG);
    CHECK_I(eval_at(&g_tl, 3000, 0, 8, g_fired, -1), PSYTL_ERR_ARG);
    CHECK_I(eval_at(&g_tl, 3000, 0, 8, NULL, 2), PSYTL_ERR_ARG);
    /* Equal onset: only what was added since. */
    CHECK_I(eval_at(&g_tl, 3000, 0, 8, NULL, 0), 0);
    i = add1(&g_tl, 0, 2500, PSYTL_MARK, 0, 0.0f);
    n = eval_at(&g_tl, 3000, 0, 9, g_fired, 4);
    CHECK_I(n, 1);
    if (n == 1) {
        CHECK_I(g_fired[0].id, i);
        CHECK_I(g_fired[0].residual, 500);
        CHECK_I(g_fired[0].flags, PSYTL_EV_FIRED | PSYTL_EV_LATE);
    }
    i = add1(&g_tl, 1, 1999, PSYTL_MARK, 0, 0.0f);
    n = eval_at(&g_tl, 3000, 0, 10, g_fired, 4);
    CHECK_I(n, 1);
    if (n == 1) { CHECK_I(g_fired[0].id, i); CHECK_I(g_fired[0].residual, 1); }
    CHECK_I(eval_at(&g_tl, 3000, 0, 11, NULL, 0), 0);
}

/* --------------------------------------------------------- 7. late events */

static void test_late(void) {
    const int64_t P = P60, O = S_NS, W = P60 / 2;   /* 8333333 */
    int a, b, c, d, f, g, n, i;

    CHECK(open_h(&g_tl, g_store, 32, 2, NULL, 0.5));
    CHECK_I(eval_at(&g_tl, O, P, 0, NULL, 0), 0);
    a = add1(&g_tl, 0, O + W, PSYTL_MARK, 0, 0.0f);       /* at the window: late */
    b = add1(&g_tl, 0, O + W - 5, PSYTL_MARK, 0, 0.0f);
    c = add1(&g_tl, 0, O + W + 1, PSYTL_MARK, 0, 0.0f);   /* past it: not late */
    d = add1(&g_tl, 0, 0, PSYTL_MARK, 0, 0.0f);
    CHECK_I(psytl_anchor(&g_tl, 1, O, 0), 0);
    n = eval_at(&g_tl, O + P, P, 1, g_fired, 8);
    CHECK_I(n, 4);
    if (n == 4) {
        CHECK_I(g_fired[0].id, d);
        CHECK_I(g_fired[1].id, b);
        CHECK_I(g_fired[2].id, a);
        CHECK_I(g_fired[3].id, c);
    }
    CHECK(psytl_find(&g_tl, a) && (unsigned)psytl_find(&g_tl, a)->flags == (PSYTL_EV_FIRED | PSYTL_EV_LATE));
    CHECK(psytl_find(&g_tl, b) && (unsigned)psytl_find(&g_tl, b)->flags == (PSYTL_EV_FIRED | PSYTL_EV_LATE));
    CHECK(psytl_find(&g_tl, c) && (unsigned)psytl_find(&g_tl, c)->flags == PSYTL_EV_FIRED);
    CHECK(psytl_find(&g_tl, d) && (unsigned)psytl_find(&g_tl, d)->flags == (PSYTL_EV_FIRED | PSYTL_EV_LATE));
    CHECK(psytl_find(&g_tl, a) && psytl_find(&g_tl, a)->residual == P - W);
    CHECK(psytl_find(&g_tl, c) && psytl_find(&g_tl, c)->residual == P - W - 1);
    CHECK(psytl_find(&g_tl, d) && psytl_find(&g_tl, d)->residual == O + P);
    /* Base 1: its last window ended at P + W. */
    f = add1(&g_tl, 1, P, PSYTL_MARK, 0, 0.0f);
    g = add1(&g_tl, 1, P + W + 1, PSYTL_MARK, 0, 0.0f);
    n = eval_at(&g_tl, O + 2 * P, P, 2, g_fired, 8);
    CHECK_I(n, 2);
    for (i = 0; i < n && i < 2; i++) {
        if (g_fired[i].id == f) {
            CHECK_I(g_fired[i].flags, PSYTL_EV_FIRED | PSYTL_EV_LATE);
            CHECK_I(g_fired[i].residual, P);
        } else {
            CHECK_I(g_fired[i].id, g);
            CHECK_I(g_fired[i].flags, PSYTL_EV_FIRED);
            CHECK_I(g_fired[i].residual, P - W - 1);
        }
    }
}

/* ---------------------------------------------------------------- 8. rewind */

static void test_rewind(void) {
    const int64_t P = 10 * MS_NS, R = S_NS, M = MS_NS;
    int id[8], k, n, y;
    const psytl_event* e;

    CHECK(open_h(&g_tl, g_store, 32, 4, NULL, 0.0));
    id[0] = add1(&g_tl, 1, 0, PSYTL_SET, 0, 1.0f);
    id[1] = add1(&g_tl, 1, 100 * M, PSYTL_SET, 0, 2.0f);
    id[2] = add1(&g_tl, 1, 200 * M, PSYTL_SET, 0, 3.0f);
    id[3] = add1(&g_tl, 1, 300 * M, PSYTL_SET, 0, 4.0f);
    id[4] = add1(&g_tl, 1, 0, PSYTL_MARK, 0, 0.0f);
    id[5] = add1(&g_tl, 1, 150 * M, PSYTL_ONSET, 1, 0.0f);
    id[6] = add1(&g_tl, 1, 250 * M, PSYTL_TRIGGER, 0, 0.0f);
    CHECK_I(psytl_anchor(&g_tl, 1, R, 0), 0);
    for (k = 0; k <= 25; k++) eval_at(&g_tl, R + k * P, P, k, NULL, 0);
    CHECK(psytl_find(&g_tl, id[0]) && psytl_find(&g_tl, id[0])->frame == 0);
    CHECK(psytl_find(&g_tl, id[1]) && psytl_find(&g_tl, id[1])->frame == 10);
    CHECK(psytl_find(&g_tl, id[5]) && psytl_find(&g_tl, id[5])->frame == 15);
    CHECK(psytl_find(&g_tl, id[2]) && psytl_find(&g_tl, id[2])->frame == 20);
    CHECK(psytl_find(&g_tl, id[6]) && psytl_find(&g_tl, id[6])->frame == 25);
    CHECK(psytl_value(&g_tl, 0) == 3.0f);
    CHECK(psytl_value(&g_tl, 1) == 1.0f);

    /* Anchor back to 100 ms (the base's last onset was at 250 ms): every
     * fired event at or after 100 ms, 100 ms included, is pending again at
     * once, and the channels are recomputed. */
    CHECK_I(psytl_anchor(&g_tl, 1, R + 26 * P, 100 * M), 0);
    CHECK(is_pending_reset(psytl_find(&g_tl, id[1])));
    CHECK(is_pending_reset(psytl_find(&g_tl, id[2])));
    CHECK(is_pending_reset(psytl_find(&g_tl, id[5])));
    CHECK(is_pending_reset(psytl_find(&g_tl, id[6])));
    CHECK(is_pending_reset(psytl_find(&g_tl, id[3])));
    e = psytl_find(&g_tl, id[0]);
    CHECK(e && e->frame == 0 && e->onset == R && (unsigned)e->flags == PSYTL_EV_FIRED);
    e = psytl_find(&g_tl, id[4]);
    CHECK(e && e->frame == 0 && e->onset == R && (unsigned)e->flags == PSYTL_EV_FIRED);
    CHECK(psytl_value(&g_tl, 0) == 1.0f);
    CHECK(psytl_value(&g_tl, 1) == 0.0f);
    n = eval_at(&g_tl, R + 26 * P, P, 26, g_fired, 8);
    CHECK_I(n, 1);
    if (n >= 1) { CHECK_I(g_fired[0].id, id[1]); CHECK_I(g_fired[0].residual, 0); }
    CHECK(psytl_value(&g_tl, 0) == 2.0f);
    for (k = 27; k <= 46; k++) eval_at(&g_tl, R + k * P, P, k, NULL, 0);
    e = psytl_find(&g_tl, id[5]);
    CHECK(e && e->frame == 31 && e->onset == R + 31 * P && e->residual == 0 && (unsigned)e->flags == PSYTL_EV_FIRED);
    CHECK(psytl_find(&g_tl, id[2]) && psytl_find(&g_tl, id[2])->frame == 36);
    CHECK(psytl_find(&g_tl, id[6]) && psytl_find(&g_tl, id[6])->frame == 41);
    CHECK(psytl_find(&g_tl, id[3]) && psytl_find(&g_tl, id[3])->frame == 46);
    CHECK(psytl_value(&g_tl, 0) == 4.0f);
    CHECK(psytl_value(&g_tl, 1) == 1.0f);

    /* Base time of the last onset is 300 ms. An anchor exactly there
     * rewinds the event exactly there. */
    CHECK_I(psytl_anchor(&g_tl, 1, R + 47 * P, 300 * M), 0);
    CHECK(is_pending_reset(psytl_find(&g_tl, id[3])));
    CHECK(psytl_value(&g_tl, 0) == 3.0f);
    n = eval_at(&g_tl, R + 47 * P, P, 47, g_fired, 8);
    CHECK_I(n, 1);
    if (n >= 1) { CHECK_I(g_fired[0].id, id[3]); CHECK_I(g_fired[0].frame, 47); }

    /* An anchor at a later base time rewinds nothing; the next evaluate
     * fires what it passed over, late by the residual, not flagged LATE. */
    id[7] = add1(&g_tl, 1, 320 * M, PSYTL_SET, 0, 8.0f);
    CHECK_I(psytl_anchor(&g_tl, 1, R + 48 * P, 400 * M), 0);
    CHECK(psytl_find(&g_tl, id[3]) && psytl_find(&g_tl, id[3])->frame == 47);
    n = eval_at(&g_tl, R + 48 * P, P, 48, g_fired, 8);
    CHECK_I(n, 1);
    if (n >= 1) {
        CHECK_I(g_fired[0].id, id[7]);
        CHECK_I(g_fired[0].residual, 80 * M);
        CHECK_I(g_fired[0].flags, PSYTL_EV_FIRED);
    }
    CHECK(psytl_value(&g_tl, 0) == 8.0f);

    /* Last onset base time 400 ms; 401 rewinds nothing. The event at 390
     * is late (reach 400) and fires next. */
    CHECK_I(psytl_anchor(&g_tl, 1, R + 49 * P, 401 * M), 0);
    id[7] = add1(&g_tl, 1, 390 * M, PSYTL_SET, 2, 6.0f);
    CHECK_I(eval_at(&g_tl, R + 49 * P, P, 49, NULL, 0), 1);
    CHECK(psytl_value(&g_tl, 2) == 6.0f);
    /* An anchor at a later bt (405 > 401) whose RT time is after the next
     * onset: that window ends at 385, behind the reach (401). An evaluate
     * never un-fires, so 390 stays fired. A late event past the window
     * (398 <= reach) waits for it, pending with flags 0. */
    CHECK_I(psytl_anchor(&g_tl, 1, R + 50 * P + 20 * M, 405 * M), 0);
    y = add1(&g_tl, 1, 398 * M, PSYTL_SET, 3, 5.0f);
    CHECK_I(eval_at(&g_tl, R + 50 * P, P, 50, NULL, 0), 0);
    e = psytl_find(&g_tl, id[7]);
    CHECK(e && e->frame == 49 && (unsigned)e->flags == (PSYTL_EV_FIRED | PSYTL_EV_LATE));
    CHECK(psytl_value(&g_tl, 2) == 6.0f);
    CHECK(is_pending_reset(psytl_find(&g_tl, y)));
    n = eval_at(&g_tl, R + 50 * P + 15 * M, P, 51, g_fired, 8);   /* base time 400 */
    CHECK_I(n, 1);
    if (n >= 1) {
        CHECK_I(g_fired[0].id, y);
        CHECK_I(g_fired[0].residual, 2 * M);
        CHECK_I(g_fired[0].flags, PSYTL_EV_FIRED | PSYTL_EV_LATE);
    }
    CHECK(psytl_value(&g_tl, 3) == 5.0f);
    /* REWIND's own case: an earlier bt (395 <= 401) whose RT time is after
     * the next onset. At once: 398 pending again, 390 stays fired. The
     * next onset is at base time 380: nothing fires, nothing un-fires. */
    CHECK_I(psytl_anchor(&g_tl, 1, R + 51 * P + 20 * M, 395 * M), 0);
    CHECK(is_pending_reset(psytl_find(&g_tl, y)));
    CHECK(psytl_value(&g_tl, 3) == 0.0f);
    e = psytl_find(&g_tl, id[7]);
    CHECK(e && e->frame == 49);
    CHECK_I(eval_at(&g_tl, R + 51 * P + 5 * M, P, 52, NULL, 0), 0);   /* base time 380 */
    e = psytl_find(&g_tl, id[7]);
    CHECK(e && e->frame == 49);
    CHECK(psytl_value(&g_tl, 2) == 6.0f);
    n = eval_at(&g_tl, R + 51 * P + 23 * M, P, 53, g_fired, 8);   /* base time 398 */
    CHECK_I(n, 1);
    if (n >= 1) {
        CHECK_I(g_fired[0].id, y);
        CHECK_I(g_fired[0].residual, 0);
        CHECK_I(g_fired[0].flags, PSYTL_EV_FIRED);   /* re-fired, not added late */
    }
    check_sorted(&g_tl, __LINE__);
}

/* Two anchors at base time 0 run a trial twice. */
static void test_trial_twice(void) {
    const int64_t P = P60, R1 = 2 * S_NS, R2 = R1 + 41 * P60;
    static int seen1[16], seen2[16];
    const psytl_event* p;
    float v1[3];
    int id[6], i, k, n, np;

    CHECK(open_h(&g_tl, g_store, 32, 3, NULL, 0.0));
    id[0] = add1(&g_tl, 1, 0, PSYTL_ONSET, 0, 0.0f);
    id[1] = add1(&g_tl, 1, 35 * MS_NS + 7, PSYTL_SET, 1, 0.5f);
    id[2] = add1(&g_tl, 1, 100 * MS_NS, PSYTL_TRIGGER, 0, 0.0f);
    id[3] = add1(&g_tl, 1, 100 * MS_NS, PSYTL_MARK, 0, 0.0f);
    id[4] = add1(&g_tl, 1, 333 * MS_NS, PSYTL_MARK, 0, 0.0f);
    id[5] = add1(&g_tl, 1, 500 * MS_NS, PSYTL_OFFSET, 0, 0.0f);
    for (i = 0; i < 6; i++) CHECK_I(id[i], i);
    memset(seen1, 0, sizeof seen1);
    memset(seen2, 0, sizeof seen2);

    CHECK_I(psytl_anchor(&g_tl, 1, R1, 0), 0);
    for (k = 0; k <= 40; k++) {
        n = eval_at(&g_tl, R1 + k * P, P, k, g_fired, 16);
        for (i = 0; i < n && i < 16; i++) if (g_fired[i].id >= 0 && g_fired[i].id < 16) seen1[g_fired[i].id]++;
    }
    p = psytl_events(&g_tl, 1, &np);
    CHECK_I(np, 6);
    if (np == 6 && p) memcpy(g_snap, p, 6 * sizeof *p);
    for (i = 0; i < 3; i++) v1[i] = psytl_value(&g_tl, i);

    CHECK_I(psytl_anchor(&g_tl, 1, R2, 0), 0);
    for (k = 41; k <= 81; k++) {
        n = eval_at(&g_tl, R1 + k * P, P, k, g_fired, 16);
        for (i = 0; i < n && i < 16; i++) if (g_fired[i].id >= 0 && g_fired[i].id < 16) seen2[g_fired[i].id]++;
    }
    for (i = 0; i < 6; i++) {
        if (seen1[i] != 1) fail_i(__LINE__, "trial run 1: times event fired", seen1[i], 1);
        if (seen2[i] != 1) fail_i(__LINE__, "trial run 2: times event fired", seen2[i], 1);
    }
    p = psytl_events(&g_tl, 1, &np);
    CHECK_I(np, 6);
    for (i = 0; i < 6 && i < np && p; i++) {
        CHECK_I(p[i].id, g_snap[i].id);
        CHECK_I(p[i].residual, g_snap[i].residual);
        CHECK_I(p[i].flags, g_snap[i].flags);
        CHECK_I(p[i].frame, g_snap[i].frame + 41);
        CHECK_I(p[i].onset, g_snap[i].onset + 41 * P);
    }
    for (i = 0; i < 3; i++) CHECK(psytl_value(&g_tl, i) == v1[i]);
    (void)R2;
}

/* --------------------------------------------- 9. remove, prune and clear */

static void test_remove(void) {
    static psytl_key kk[2];
    float init[6] = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
    int A, B, C, D, E, F, G, H, I, J, K, L, X, next;
    float v2;
    int64_t t = 0;
    const psytl_event* e;

    kk[0] = mkkey(0, 0.0f, PSYTL_EASE_LINEAR);
    kk[1] = mkkey(1000, 1000.0f, PSYTL_EASE_LINEAR);
    CHECK(open_h(&g_tl, g_store, 32, 6, init, 0.0));
    CHECK_I(psytl_anchor(&g_tl, 1, 0, 0), 0);
    CHECK_I(psytl_anchor(&g_tl, 2, 0, 0), 0);
    A = add1(&g_tl, 1, 10, PSYTL_SET, 0, 3.0f);
    B = add1(&g_tl, 1, 20, PSYTL_SET, 0, 5.0f);
    C = add1(&g_tl, 1, 100, PSYTL_SET, 0, 7.0f);
    D = add1(&g_tl, 1, 15, PSYTL_ONSET, 1, 0.0f);
    E = add1(&g_tl, 1, 5, PSYTL_MARK, 0, 0.0f);
    L = add1(&g_tl, 1, 400, PSYTL_OFFSET, 1, 0.0f);
    F = add1(&g_tl, 2, 10, PSYTL_ONSET, 2, 0.0f);
    G = add1(&g_tl, 2, 50, PSYTL_SET, 2, 4.0f);
    CHECK_I(psytl_set_keys(&g_tl, 3, 1, kk, 2), 0);
    CHECK_I(psytl_set_keys(&g_tl, 4, 2, kk, 2), 0);
    check_sorted(&g_tl, __LINE__);
    CHECK_I(n_events(&g_tl, 1), 6);
    CHECK_I(n_events(&g_tl, 2), 2);
    CHECK_I(n_events(&g_tl, PSYTL_ALL_BASES), 8);

    CHECK_I(eval_at(&g_tl, 30, 0, 30, NULL, 0), 5);
    CHECK(psytl_value(&g_tl, 0) == 5.0f);
    CHECK(psytl_value(&g_tl, 1) == 1.0f);
    CHECK(psytl_value(&g_tl, 2) == 1.0f);
    CHECK_F(psytl_value(&g_tl, 3), 30.0, 1e-4);

    CHECK_I(psytl_remove(&g_tl, B), 0);
    CHECK(psytl_find(&g_tl, B) == NULL);
    CHECK(psytl_value(&g_tl, 0) == 3.0f);
    CHECK_I(psytl_remove(&g_tl, C), 0);
    CHECK(psytl_value(&g_tl, 0) == 3.0f);
    CHECK_I(psytl_remove(&g_tl, A), 0);
    CHECK(psytl_value(&g_tl, 0) == 0.5f);
    H = add1(&g_tl, 2, 1000, PSYTL_ONSET, 0, 0.0f);   /* channel 0 is free */
    CHECK(H >= 0);
    CHECK_I(psytl_remove(&g_tl, H), 0);
    CHECK_I(psytl_remove(&g_tl, B), PSYTL_ERR_NOT_FOUND);
    CHECK_I(psytl_remove(&g_tl, 9999), PSYTL_ERR_NOT_FOUND);
    CHECK_I(psytl_remove(&g_tl, -1), PSYTL_ERR_NOT_FOUND);
    e = psytl_find(&g_tl, D);
    CHECK(e && e->base == 1 && e->time == 15 && e->kind == PSYTL_ONSET && e->target == 1 &&
          e->frame == 30 && e->onset == 30 && e->residual == 15);
    CHECK(psytl_find(&g_tl, 9999) == NULL);
    check_sorted(&g_tl, __LINE__);

    CHECK_I(eval_at(&g_tl, 60, 0, 60, NULL, 0), 1);   /* G */
    CHECK(psytl_value(&g_tl, 2) == 4.0f);
    CHECK_I(psytl_prune(&g_tl, 2), 2);
    CHECK_I(n_events(&g_tl, 2), 0);
    CHECK(psytl_value(&g_tl, 2) == 4.0f);
    CHECK_F(psytl_value(&g_tl, 4), 60.0, 1e-4);
    I = add1(&g_tl, 1, 2000, PSYTL_ONSET, 2, 0.0f);   /* channel 2 is free */
    CHECK(I >= 0);
    CHECK_I(psytl_remove(&g_tl, I), 0);
    CHECK(psytl_value(&g_tl, 2) == 4.0f);   /* I never fired: no recompute */
    v2 = psytl_value(&g_tl, 2);   /* later checks are relative, so one cause fails once */
    if (v2 != 4.0f) fprintf(stderr, "  channel 2 after removing an unfired event: %g\n", (double)v2);
    CHECK_I(add1(&g_tl, 1, 0, PSYTL_ONSET, 4, 0.0f), PSYTL_ERR_BOUND);   /* keys survive prune */
    CHECK_I(psytl_prune(&g_tl, 1), 2);   /* D, E */
    CHECK(psytl_find(&g_tl, D) == NULL && psytl_find(&g_tl, E) == NULL);
    CHECK(psytl_value(&g_tl, 1) == 1.0f);
    CHECK_I(n_events(&g_tl, 1), 1);   /* L */

    J = add1(&g_tl, 1, 70, PSYTL_SET, 5, 9.0f);
    K = add1(&g_tl, 1, 500, PSYTL_SET, 5, 6.0f);
    X = add1(&g_tl, 0, 85, PSYTL_MARK, 0, 0.0f);
    CHECK_I(eval_at(&g_tl, 80, 0, 80, NULL, 0), 1);
    CHECK(psytl_value(&g_tl, 5) == 9.0f);
    /* Rewind base 1 to 0: channels recompute from what remains. */
    CHECK_I(psytl_anchor(&g_tl, 1, 90, 0), 0);
    CHECK(is_pending_reset(psytl_find(&g_tl, J)));
    CHECK(psytl_value(&g_tl, 5) == 0.5f);
    CHECK(psytl_value(&g_tl, 1) == 0.5f);   /* L is pending, D was pruned */
    /* Channel 2 is free (pruned, then bound by I, then I removed): it is
     * not base 1's, so the rewind of base 1 leaves it. */
    CHECK(psytl_value(&g_tl, 2) == v2);
    CHECK_I(eval_at(&g_tl, 90, 0, 90, NULL, 0), 1);   /* X */
    CHECK_F(psytl_value(&g_tl, 3), 0.0, 1e-6);
    CHECK_I(psytl_prune(&g_tl, PSYTL_ALL_BASES), 1);
    CHECK(psytl_find(&g_tl, X) == NULL);
    check_sorted(&g_tl, __LINE__);

    /* clear(1): events, keys, channels back to initial and free. */
    CHECK_I(n_events(&g_tl, 1), 3);   /* L, J, K */
    next = add1(&g_tl, 0, 5000, PSYTL_MARK, 0, 0.0f);
    CHECK(psytl_value(&g_tl, 2) == v2);
    CHECK_I(psytl_clear(&g_tl, 1), 3);
    CHECK(psytl_value(&g_tl, 2) == v2);   /* free (pruned, then I removed): untouched */
    CHECK_I(n_events(&g_tl, 1), 0);
    CHECK(psytl_value(&g_tl, 1) == 0.5f);
    CHECK(psytl_value(&g_tl, 3) == 0.5f);
    CHECK(psytl_value(&g_tl, 5) == 0.5f);
    CHECK(psytl_base_time(&g_tl, 1, 100, &t));
    CHECK_I(t, 10);
    CHECK_I(psytl_set_keys(&g_tl, 3, 2, kk, 2), 0);
    CHECK_I(add1(&g_tl, 2, 1000, PSYTL_SET, 5, 1.0f), next + 1);   /* ids never reused */
    CHECK(add1(&g_tl, 2, 1000, PSYTL_ONSET, 1, 0.0f) >= 0);
    CHECK_I(n_events(&g_tl, PSYTL_ALL_BASES), 3);
    CHECK_I(psytl_clear(&g_tl, PSYTL_ALL_BASES), 3);
    CHECK_I(n_events(&g_tl, PSYTL_ALL_BASES), 0);
    CHECK(psytl_value(&g_tl, 3) == 0.5f);
    CHECK(psytl_value(&g_tl, 4) == 0.5f);
    CHECK(psytl_value(&g_tl, 1) == 0.5f);
    CHECK(psytl_value(&g_tl, 5) == 0.5f);
    CHECK(psytl_value(&g_tl, 2) == v2);   /* freed by a prune: clear leaves it */
    CHECK_I(psytl_set_keys(&g_tl, 4, 1, kk, 2), 0);   /* free after clear */
    CHECK_I(add1(&g_tl, 0, 1, PSYTL_MARK, 0, 0.0f), next + 3);
    (void)F; (void)G; (void)L; (void)K;
}

/* Points the manual settled after the first run of this test. */
static void test_settled(void) {
    static psytl_key kk[2];
    const int64_t R = S_NS;
    int64_t t = 0;
    int id, k, total, n;
    const psytl_event* e;

    /* A pause with a lead does not un-fire an event a frame already
     * showed early, and it does not fire again after resume. */
    CHECK(open_h(&g_tl, g_store, 8, 2, NULL, 0.5));
    id = add1(&g_tl, 1, 20 * MS_NS, PSYTL_ONSET, 1, 0.0f);
    CHECK_I(psytl_anchor(&g_tl, 1, R, 0), 0);
    CHECK_I(eval_at(&g_tl, R, P60, 0, NULL, 0), 0);
    n = eval_at(&g_tl, R + P60, P60, 1, g_fired, 4);   /* window ends at 25 ms */
    CHECK_I(n, 1);
    if (n >= 1) CHECK_I(g_fired[0].residual, P60 - 20 * MS_NS);
    CHECK_I(psytl_pause(&g_tl, 1, R + 17 * MS_NS), 0);   /* frozen at 17 ms < 20 ms */
    total = 0;
    for (k = 2; k <= 5; k++) total += eval_at(&g_tl, R + k * P60, P60, k, NULL, 0);
    CHECK_I(psytl_resume(&g_tl, 1, R + 6 * P60), 0);
    for (k = 6; k <= 12; k++) total += eval_at(&g_tl, R + k * P60, P60, k, NULL, 0);
    CHECK_I(total, 0);
    e = psytl_find(&g_tl, id);
    CHECK(e && e->frame == 1 && e->onset == R + P60 && (unsigned)e->flags == PSYTL_EV_FIRED);
    CHECK(psytl_value(&g_tl, 1) == 1.0f);
    CHECK(!psytl_next_due(&g_tl, &t));

    /* A late add is pending with flags 0 until it fires. */
    id = add1(&g_tl, 0, R, PSYTL_MARK, 0, 0.0f);
    CHECK(is_pending_reset(psytl_find(&g_tl, id)));
    CHECK_I(eval_at(&g_tl, R + 13 * P60, P60, 13, NULL, 0), 1);
    e = psytl_find(&g_tl, id);
    CHECK(e && (unsigned)e->flags == (PSYTL_EV_FIRED | PSYTL_EV_LATE));

    /* Lateness is by the reach, not by the last window. A pause behind the
     * reach: an event added between the frozen time and the reach is late,
     * waits for the window, and fires with LATE. */
    CHECK(open_h(&g_tl, g_store, 8, 2, NULL, 0.0));
    CHECK_I(psytl_anchor(&g_tl, 1, R, 0), 0);
    CHECK_I(eval_at(&g_tl, R + 100 * MS_NS, 0, 0, NULL, 0), 0);   /* reach 100 ms */
    CHECK_I(psytl_pause(&g_tl, 1, R + 50 * MS_NS), 0);           /* frozen at 50 ms */
    CHECK_I(eval_at(&g_tl, R + 110 * MS_NS, 0, 1, NULL, 0), 0);
    id = add1(&g_tl, 1, 70 * MS_NS, PSYTL_MARK, 0, 0.0f);
    CHECK_I(eval_at(&g_tl, R + 120 * MS_NS, 0, 2, NULL, 0), 0);  /* waits */
    CHECK(is_pending_reset(psytl_find(&g_tl, id)));
    CHECK_I(psytl_resume(&g_tl, 1, R + 130 * MS_NS), 0);
    n = eval_at(&g_tl, R + 150 * MS_NS, 0, 3, g_fired, 4);       /* base time 70 ms */
    CHECK_I(n, 1);
    if (n >= 1) {
        CHECK_I(g_fired[0].residual, 0);
        CHECK_I(g_fired[0].flags, PSYTL_EV_FIRED | PSYTL_EV_LATE);
    }
    /* A rewinding anchor whose RT time is after the next onset: the reach
     * is just before bt, so bt - 2 is late and bt is not. */
    CHECK_I(psytl_anchor(&g_tl, 2, R + 150 * MS_NS, 0), 0);
    CHECK_I(eval_at(&g_tl, R + 250 * MS_NS, 0, 4, NULL, 0), 0);   /* base 2 at 100 ms */
    CHECK_I(psytl_anchor(&g_tl, 2, R + 300 * MS_NS, 80 * MS_NS), 0);
    id = add1(&g_tl, 2, 80 * MS_NS - 2, PSYTL_MARK, 0, 0.0f);
    k = add1(&g_tl, 2, 80 * MS_NS, PSYTL_MARK, 0, 0.0f);
    CHECK_I(eval_at(&g_tl, R + 260 * MS_NS, 0, 5, NULL, 0), 0);   /* base 2 at 40 ms */
    /* Added after a window behind the reach: still at or before it. */
    total = add1(&g_tl, 2, 80 * MS_NS - 5, PSYTL_MARK, 0, 0.0f);
    n = eval_at(&g_tl, R + 300 * MS_NS, 0, 6, g_fired, 4);        /* base 2 at 80 ms */
    CHECK_I(n, 3);
    e = psytl_find(&g_tl, total);
    CHECK(e && e->residual == 5 && (unsigned)e->flags == (PSYTL_EV_FIRED | PSYTL_EV_LATE));
    e = psytl_find(&g_tl, id);
    CHECK(e && e->residual == 2 && (unsigned)e->flags == (PSYTL_EV_FIRED | PSYTL_EV_LATE));
    e = psytl_find(&g_tl, k);
    CHECK(e && e->residual == 0 && (unsigned)e->flags == PSYTL_EV_FIRED);

    /* Base 0: every base call is ARG. */
    CHECK_I(psytl_resume(&g_tl, 0, 0), PSYTL_ERR_ARG);
    CHECK_I(psytl_pause(&g_tl, 0, 0), PSYTL_ERR_ARG);
    CHECK_I(psytl_stop(&g_tl, 0), PSYTL_ERR_ARG);
    CHECK_I(psytl_anchor(&g_tl, 0, 0, 0), PSYTL_ERR_ARG);

    /* The last key's ease must be a psytl_ease, though it is not used.
     * (6 is PSYTL_EASE_BEZIER since v0.2.0, so 7 is the first that is not.) */
    kk[0] = mkkey(0, 1.0f, PSYTL_EASE_LINEAR);
    kk[1] = mkkey(10, 2.0f, 7);
    CHECK_I(psytl_set_keys(&g_tl, 0, 1, kk, 2), PSYTL_ERR_ARG);
    kk[1] = mkkey(10, 2.0f, 255);
    CHECK_I(psytl_set_keys(&g_tl, 0, 1, kk, 2), PSYTL_ERR_ARG);
    kk[1] = mkkey(10, 0.0f, PSYTL_EASE_LOG);   /* LOG's > 0 check skips it */
    CHECK_I(psytl_set_keys(&g_tl, 0, 1, kk, 2), 0);
    /* Keyed: events are BOUND until set_keys with n = 0. */
    CHECK_I(add1(&g_tl, 1, 0, PSYTL_ONSET, 0, 0.0f), PSYTL_ERR_BOUND);
    CHECK_I(psytl_set_keys(&g_tl, 0, 1, NULL, 0), 0);
    CHECK(add1(&g_tl, 2, 0, PSYTL_ONSET, 0, 0.0f) >= 0);
    CHECK_I(psytl_set_keys(&g_tl, 0, 2, kk, 2), PSYTL_ERR_BOUND);

    /* open() with a NULL or bad desc fails and leaves the handle closed,
     * also a handle that was open. */
    CHECK(!psytl_open(&g_closed, NULL));
    CHECK(!psytl_is_open(&g_closed));
    CHECK(psytl_values(&g_closed) == NULL);
    CHECK(open_h(&g_tl, g_store, 8, 2, NULL, 0.0));
    CHECK(!psytl_open(&g_tl, NULL));
    CHECK(psytl_error(&g_tl)[0] != 0);
    CHECK(!psytl_is_open(&g_tl));
    CHECK(psytl_values(&g_tl) == NULL);
    CHECK_I(add1(&g_tl, 0, 0, PSYTL_MARK, 0, 0.0f), PSYTL_ERR_CLOSED);
    CHECK(open_h(&g_tl, g_store, 8, 2, NULL, 0.0));
    CHECK(!open_h(&g_tl, g_store, 8, 2, NULL, 2.0));
    CHECK(!psytl_is_open(&g_tl));
    CHECK(psytl_values(&g_tl) == NULL);
}

/* ------------------------------------------------------------ 10. next_due */

static void test_next_due(void) {
    int64_t t = 0;
    CHECK(open_h(&g_tl, g_store, 16, 2, NULL, 0.0));
    CHECK(!psytl_next_due(&g_tl, &t));
    CHECK(add1(&g_tl, 0, 500, PSYTL_MARK, 0, 0.0f) >= 0);
    CHECK(psytl_next_due(&g_tl, &t));
    CHECK_I(t, 500);
    CHECK(add1(&g_tl, 1, 0, PSYTL_MARK, 0, 0.0f) >= 0);   /* stopped: not due */
    CHECK(psytl_next_due(&g_tl, &t));
    CHECK_I(t, 500);
    CHECK_I(psytl_anchor(&g_tl, 1, 100, 0), 0);
    CHECK(psytl_next_due(&g_tl, &t));
    CHECK_I(t, 100);
    CHECK_I(psytl_pause(&g_tl, 1, 150), 0);   /* paused: not due */
    CHECK(psytl_next_due(&g_tl, &t));
    CHECK_I(t, 500);
    CHECK_I(psytl_resume(&g_tl, 1, 300), 0);   /* base time 50 at RT 300 */
    CHECK(psytl_next_due(&g_tl, &t));
    CHECK_I(t, 250);
    CHECK_I(eval_at(&g_tl, 260, 0, 0, NULL, 0), 1);
    CHECK(psytl_next_due(&g_tl, &t));
    CHECK_I(t, 500);
    CHECK(add1(&g_tl, 0, 10, PSYTL_MARK, 0, 0.0f) >= 0);   /* late: in the past */
    CHECK(psytl_next_due(&g_tl, &t));
    CHECK_I(t, 10);
    CHECK_I(eval_at(&g_tl, 270, 0, 1, NULL, 0), 1);
    CHECK(psytl_next_due(&g_tl, &t));
    CHECK_I(t, 500);
    CHECK_I(eval_at(&g_tl, 600, 0, 2, NULL, 0), 1);
    CHECK(!psytl_next_due(&g_tl, &t));
    CHECK(add1(&g_tl, 1, 1000, PSYTL_MARK, 0, 0.0f) >= 0);
    CHECK(psytl_next_due(&g_tl, &t));
    CHECK_I(t, 1250);
}

/* -------------------------------------------------------------- 11. replay */

static uint64_t fnv(uint64_t h, const void* p, size_t n) {
    const unsigned char* c = (const unsigned char*)p;
    size_t i;
    for (i = 0; i < n; i++) { h ^= c[i]; h *= UINT64_C(1099511628211); }
    return h;
}

static psytl_key g_rk6[4], g_rk7[3], g_rkb[3];
static float g_rs[64];
static psytl_curve g_rc[2];
static psytl_track g_rtr[3];

/* Channels 6 and 7 keyed, 8 swapped between sampled, repeated and
 * BEZIER tracks and none, 9 tweened. */
static uint64_t replay_run(psytl_timeline* tl, psytl_event* store) {
    float init[10] = { 0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f };
    psytl_event fired[8], b[3];
    uint64_t h = UINT64_C(14695981039346656037);
    int64_t onset = T0;
    int step, r, ret, n, nf, j;

    g_rng = 0xC0FFEE;
    if (!open_h(tl, store, 64, 10, init, 0.5)) return 0;
    psytl_set_keys(tl, 6, 1, g_rk6, 4);
    psytl_set_keys(tl, 7, 0, g_rk7, 3);
    psytl_set_track(tl, 8, 2, &g_rtr[0]);
    for (step = 0; step < 600; step++) {
        r = rnd_int(110);
        if (r < 35) {
            int base = rnd_int(4), kind = rnd_int(5);
            int64_t t = base == 0 ? onset + rnd_range(-3 * P60, 10 * P60) : rnd_range(-3 * P60, 80 * P60);
            psytl_event e = ev(base, t, kind, rnd_int(6), (float)rnd_int(50));
            e.user = sm64();
            ret = psytl_add(tl, &e);
        } else if (r < 42) {
            for (j = 0; j < 3; j++) b[j] = ev(rnd_int(4), rnd_range(0, 60 * P60), rnd_int(5), rnd_int(6), 1.0f);
            ret = psytl_add_n(tl, b, 1 + rnd_int(3));
        } else if (r < 55) {
            ret = psytl_remove(tl, rnd_int(step + 1));
        } else if (r < 61) {
            ret = psytl_anchor(tl, 1 + rnd_int(3), onset + rnd_range(-P60, 2 * P60), rnd_range(-P60, 40 * P60));
        } else if (r < 64) {
            ret = psytl_pause(tl, 1 + rnd_int(3), onset + rnd_range(0, P60));
        } else if (r < 67) {
            ret = psytl_resume(tl, 1 + rnd_int(3), onset + rnd_range(0, P60));
        } else if (r < 69) {
            ret = psytl_prune(tl, rnd_int(5) - 1);
        } else if (r < 70) {
            ret = psytl_clear(tl, 2 + rnd_int(2));
        } else if (r < 73) {
            ret = psytl_skip(tl, 1 + rnd_int(3), onset + rnd_range(-P60, 2 * P60), rnd_range(-P60, 60 * P60));
        } else if (r < 75) {
            int64_t from = onset + rnd_range(-5 * P60, 5 * P60);
            memset(fired, 0, sizeof fired);
            ret = psytl_peek(tl, rnd_int(5) - 1, from, from + rnd_range(0, 20 * P60), fired, 8);
            nf = ret < 8 ? ret : 8;
            if (nf > 0) h = fnv(h, fired, (size_t)nf * sizeof fired[0]);
        } else if (r < 100) {
            onset += rnd_int(8) == 0 ? 0 : P60;
            memset(fired, 0, sizeof fired);
            n = eval_at(tl, onset, P60, step, fired, 8);
            nf = n < 8 ? n : 8;
            if (nf > 0) h = fnv(h, fired, (size_t)nf * sizeof fired[0]);
            if (psytl_values(tl)) h = fnv(h, psytl_values(tl), 10 * sizeof(float));
            ret = n;
        } else if (r < 105) {
            int pick = rnd_int(4);
            ret = psytl_set_track(tl, 8, 1 + rnd_int(3), pick == 3 ? NULL : &g_rtr[pick]);
        } else {
            /* Every field at random; many are rejected, and the codes are
             * hashed too. Channel 8 or 9, so a tween also takes over from
             * the swapped tracks. */
            psytl_tween_desc d;
            int base = rnd_int(4), ch = 8 + rnd_int(2);
            memset(&d, 0, sizeof d);
            d.ease = rnd_int(7);
            d.start_set = rnd_int(3) != 0;
            d.start = base == 0 ? onset + rnd_range(-3 * P60, 10 * P60) : rnd_range(-3 * P60, 60 * P60);
            d.delay = rnd_int(3) == 0 ? rnd_range(-P60, 5 * P60) : 0;
            d.duration = rnd_int(4) == 0 ? 0 : rnd_range(1, 20 * P60);
            d.to = (float)rnd_int(40) / 4.0f;
            d.from = (float)(1 + rnd_int(40)) / 4.0f;
            d.from_set = d.ease == PSYTL_EASE_LOG || rnd_int(3) == 0;
            if (d.ease == PSYTL_EASE_BEZIER) d.curve = g_rc[rnd_int(2)];
            d.cycles = rnd_int(5) - 1;
            d.yoyo = rnd_int(3) == 0;
            if (rnd_int(3) == 0) {
                d.keep_velocity = true;
                d.ease = PSYTL_EASE_LINEAR;
                d.from_set = false;
                d.yoyo = false;
                d.cycles = rnd_int(2);
                if (d.duration == 0) d.duration = P60;
            }
            ret = psytl_tween(tl, ch, base, &d);
        }
        h = fnv(h, &ret, sizeof ret);
    }
    return h;
}

static void test_replay(void) {
    uint64_t h1, h2;
    int i;
    const float* v1;
    const float* v2;
    g_rk6[0] = mkkey(0, 0.0f, PSYTL_EASE_COSINE);
    g_rk6[1] = mkkey(10 * P60, 2.0f, PSYTL_EASE_LOG);
    g_rk6[2] = mkkey(30 * P60, 9.0f, PSYTL_EASE_QUAD_OUT);
    g_rk6[3] = mkkey(50 * P60, 1.0f, PSYTL_EASE_LINEAR);
    g_rk7[0] = mkkey(T0, 0.0f, PSYTL_EASE_LINEAR);
    g_rk7[1] = mkkey(T0 + S_NS, 1.0f, PSYTL_EASE_STEP);
    g_rk7[2] = mkkey(T0 + 2 * S_NS, 3.0f, PSYTL_EASE_LINEAR);
    for (i = 0; i < 64; i++) g_rs[i] = (float)((i * 37) % 64) / 8.0f;
    g_rc[0] = mkcurve(0.42f, 0.0f, 0.58f, 1.0f);
    g_rc[1] = mkcurve(0.68f, -0.6f, 0.32f, 1.6f);
    g_rkb[0] = mkbez(0, 1.0f, 1);
    g_rkb[1] = mkkey(40 * MS_NS, 3.0f, PSYTL_EASE_COSINE);
    g_rkb[2] = mkkey(90 * MS_NS, 0.5f, PSYTL_EASE_LINEAR);
    g_rtr[0] = trk_samples(g_rs, 64, 0, 120, PSYTL_INTERP_CUBIC);
    g_rtr[0].period = 300 * MS_NS;
    g_rtr[0].repeats = 5;
    g_rtr[1] = trk_keys(g_rkb, 3);
    g_rtr[1].curves = g_rc;
    g_rtr[1].n_curves = 2;
    g_rtr[1].period = 100 * MS_NS;
    g_rtr[2] = trk_samples(g_rs, 16, -S_NS, 7, PSYTL_INTERP_STEP);
    h1 = replay_run(&g_tl, g_store);
    h2 = replay_run(&g_tl2, g_store2);
    CHECK(h1 != 0);
    CHECK(h1 == h2);
    CHECK(memcmp(g_store, g_store2, 64 * sizeof g_store[0]) == 0);
    CHECK_I(n_events(&g_tl, PSYTL_ALL_BASES), n_events(&g_tl2, PSYTL_ALL_BASES));
    v1 = psytl_values(&g_tl);
    v2 = psytl_values(&g_tl2);
    CHECK(v1 && v2 && memcmp(v1, v2, 10 * sizeof(float)) == 0);
}

/* ------------------------------------------- 11b. lead, skip and peek */

/* QUANTIZATION: psytl_lead() is the lead in use, and a window ends
 * lead x period, truncated, after the onset. */
static void test_lead(void) {
    static psytl_event s[4];
    static psytl_timeline t;
    const double in[5] = { 0.0, 0.25, 0.5, 0.999, PSYTL_LEAD_NONE };
    const double want[5] = { 0.5, 0.25, 0.5, 0.999, 0.0 };
    const int64_t P = P60;
    int k;
    CHECK(psytl_lead(NULL) == 0.0);
    memset(&t, 0, sizeof t);
    CHECK(psytl_lead(&t) == 0.0);
    for (k = 0; k < 5; k++) {
        psytl_desc d;
        int64_t w;
        int a, b;
        memset(&d, 0, sizeof d);
        memset(s, 0, sizeof s);
        d.events = s;
        d.event_capacity = 4;
        d.n_channels = 1;
        d.lead = in[k];
        CHECK(psytl_open(&t, &d));
        CHECK(psytl_lead(&t) == want[k]);
        w = (int64_t)(psytl_lead(&t) * (double)P);
        a = add1(&t, 0, 10 * P + w, PSYTL_MARK, 0, 0.0f);
        b = add1(&t, 0, 10 * P + w + 1, PSYTL_MARK, 0, 0.0f);
        CHECK_I(eval_at(&t, 10 * P, P, 10, NULL, 0), 1);
        CHECK(psytl_find(&t, a) && psytl_find(&t, a)->frame == 10);
        CHECK(is_pending_reset(psytl_find(&t, b)));
    }
    CHECK(!open_h(&t, s, 4, 1, NULL, 2.0));
    CHECK(psytl_lead(&t) == 0.0);
}

#define SK_N 4096
static psytl_event g_sk_a[SK_N], g_sk_b[SK_N];
static psytl_timeline g_ska, g_skb;

/* A movie base (1): a MARK every second for an hour (code = the second)
 * and a SET of channel 0 to the second every 10 s. 3960 events. */
static void skip_movie(psytl_timeline* tl, psytl_event* st) {
    int i;
    CHECK(open_h(tl, st, SK_N, 3, NULL, 0.5));
    for (i = 0; i < 3600; i++) {
        psytl_event e = ev(1, (int64_t)i * S_NS, PSYTL_MARK, 0, 0.0f);
        e.code = i;
        CHECK(psytl_add(tl, &e) >= 0);
        if (i % 10 == 0) CHECK(add1(tl, 1, (int64_t)i * S_NS, PSYTL_SET, 0, (float)i) >= 0);
    }
}

static const psytl_event* find_at(const psytl_timeline* tl, int base, int64_t t, int kind) {
    int n = 0, i;
    const psytl_event* p = psytl_events(tl, base, &n);
    for (i = 0; i < n; i++)
        if (p[i].time == t && p[i].kind == kind) return &p[i];
    return NULL;
}

static int count_flags(const psytl_timeline* tl, int base, unsigned mask) {
    int n = 0, i, c = 0;
    const psytl_event* p = psytl_events(tl, base, &n);
    for (i = 0; i < n; i++)
        if (p[i].flags & mask) c++;
    return c;
}

static void test_skip_seek(void) {
    const int64_t P = P60, R = 5 * S_NS, B = 1800 * S_NS, R2 = R + 60 * P60, R3 = R2 + 2 * S_NS;
    const psytl_event* e;
    int n, i, k, x, prunable;
    int64_t due = 0;

    skip_movie(&g_ska, g_sk_a);
    skip_movie(&g_skb, g_sk_b);
    CHECK_I(psytl_anchor(&g_ska, 1, R, 0), 0);
    CHECK_I(psytl_anchor(&g_skb, 1, R, 0), 0);
    for (k = 0; k < 60; k++) {   /* one second of play: the events at 0 fire */
        (void)eval_at(&g_ska, R + k * P, P, k, NULL, 0);
        (void)eval_at(&g_skb, R + k * P, P, k, NULL, 0);
    }
    CHECK_I(count_flags(&g_ska, 1, PSYTL_EV_FIRED), 2);

    /* The 30-minute seek. Skipped: MARKs 1..1799 and SETs 10..1790. The
     * channel already shows the last SET passed over. */
    CHECK_I(psytl_skip(&g_ska, 1, R2, B), 1799 + 179);
    CHECK_I(psytl_anchor(&g_skb, 1, R2, B), 0);
    CHECK(psytl_value(&g_ska, 0) == 1790.0f);
    e = find_at(&g_ska, 1, 1000 * S_NS, PSYTL_MARK);
    CHECK(e && e->flags == PSYTL_EV_SKIPPED && e->frame == -1 && e->onset == R2
          && e->residual == B - 1000 * S_NS && e->code == 1000);
    e = find_at(&g_ska, 1, 0, PSYTL_MARK);
    CHECK(e && e->flags == PSYTL_EV_FIRED && e->frame == 0);
    e = find_at(&g_ska, 1, B, PSYTL_MARK);
    CHECK(is_pending_reset(e));
    CHECK(psytl_next_due(&g_ska, &due));
    CHECK_I(due, R2);

    /* The next frame: the skip fires only what is at B; the anchor fires
     * all 1980 on the same frame. Both channels end the same. */
    n = eval_at(&g_ska, R2, P, 60, g_fired, 16);
    CHECK_I(n, 2);
    for (i = 0; i < n && i < 2; i++) {
        CHECK_I(g_fired[i].time, B);
        CHECK_I(g_fired[i].residual, 0);
        CHECK_I(g_fired[i].flags, PSYTL_EV_FIRED);
    }
    CHECK_I(eval_at(&g_skb, R2, P, 60, NULL, 0), 1980);
    CHECK(psytl_value(&g_ska, 0) == 1800.0f);
    CHECK(psytl_value(&g_skb, 0) == 1800.0f);
    CHECK(psytl_next_due(&g_ska, &due));
    CHECK_I(due, R2 + S_NS);

    /* The reach moved to the seek: an add before B is late and fires on
     * the next frame; one after waits for its time. */
    x = add1(&g_ska, 1, B - 5 * S_NS, PSYTL_MARK, 0, 0.0f);
    k = add1(&g_ska, 1, B + 2 * P, PSYTL_MARK, 0, 0.0f);
    n = eval_at(&g_ska, R2 + P, P, 61, g_fired, 16);
    CHECK_I(n, 1);
    if (n >= 1) {
        CHECK_I(g_fired[0].id, x);
        CHECK_I(g_fired[0].flags, PSYTL_EV_FIRED | PSYTL_EV_LATE);
        CHECK_I(g_fired[0].residual, P + 5 * S_NS);
    }
    CHECK(is_pending_reset(psytl_find(&g_ska, k)));

    /* An anchor back to 1000 s: skipped events from there are pending
     * again, the ones before stay skipped, and the channel is recomputed
     * from what is fired or skipped. */
    CHECK_I(psytl_anchor(&g_ska, 1, R3, 1000 * S_NS), 0);
    CHECK(is_pending_reset(find_at(&g_ska, 1, 1000 * S_NS, PSYTL_MARK)));
    CHECK(is_pending_reset(find_at(&g_ska, 1, 1500 * S_NS, PSYTL_SET)));
    CHECK(is_pending_reset(find_at(&g_ska, 1, B, PSYTL_MARK)));
    e = find_at(&g_ska, 1, 999 * S_NS, PSYTL_MARK);
    CHECK(e && e->flags == PSYTL_EV_SKIPPED && e->onset == R2);
    CHECK(psytl_value(&g_ska, 0) == 990.0f);
    n = eval_at(&g_ska, R3, P, 200, g_fired, 16);
    CHECK_I(n, 2);
    for (i = 0; i < n && i < 2; i++) {
        CHECK_I(g_fired[i].time, 1000 * S_NS);
        CHECK_I(g_fired[i].flags, PSYTL_EV_FIRED);
    }
    CHECK(psytl_value(&g_ska, 0) == 1000.0f);

    /* Prune deletes the fired and the skipped; channel values stay. */
    prunable = count_flags(&g_ska, 1, PSYTL_EV_FIRED | PSYTL_EV_SKIPPED);
    CHECK(prunable > 1000);
    n = n_events(&g_ska, 1);
    CHECK_I(psytl_prune(&g_ska, 1), prunable);
    CHECK_I(n_events(&g_ska, 1), n - prunable);
    CHECK_I(count_flags(&g_ska, 1, PSYTL_EV_FIRED | PSYTL_EV_SKIPPED), 0);
    CHECK(psytl_value(&g_ska, 0) == 1000.0f);
    check_sorted(&g_ska, __LINE__);
}

/* A skip back is an anchor back, when no late event waits before bt. */
static void test_skip_back(void) {
    const int64_t P = P60, R = S_NS;
    int k;
    skip_movie(&g_ska, g_sk_a);
    skip_movie(&g_skb, g_sk_b);
    CHECK_I(psytl_anchor(&g_ska, 1, R, 0), 0);
    CHECK_I(psytl_anchor(&g_skb, 1, R, 0), 0);
    for (k = 0; k < 600; k++) {
        (void)eval_at(&g_ska, R + k * P, P, k, NULL, 0);
        (void)eval_at(&g_skb, R + k * P, P, k, NULL, 0);
    }
    CHECK(psytl_value(&g_ska, 0) == 0.0f);   /* 600 frames is 9.99 s: SET 10 not yet */
    CHECK_I(psytl_skip(&g_ska, 1, R + 600 * P, 3 * S_NS), 0);
    CHECK_I(psytl_anchor(&g_skb, 1, R + 600 * P, 3 * S_NS), 0);
    CHECK(memcmp(g_sk_a, g_sk_b, sizeof g_sk_a) == 0);
    for (k = 600; k < 700; k++) {
        CHECK_I(eval_at(&g_ska, R + k * P, P, k, NULL, 0), eval_at(&g_skb, R + k * P, P, k, NULL, 0));
    }
    CHECK(memcmp(g_sk_a, g_sk_b, sizeof g_sk_a) == 0);
    CHECK(psytl_value(&g_ska, 0) == psytl_value(&g_skb, 0));
}

/* A skip back also skips a late event before bt still waiting for a
 * window; the anchor leaves it to fire late. */
static void test_skip_late_wait(void) {
    const int64_t P = 10 * MS_NS, M = MS_NS;
    psytl_timeline* t[2];
    psytl_event* st[2];
    int j, id90[2], id97[2], k, n;
    t[0] = &g_tl;
    t[1] = &g_tl2;
    st[0] = g_store;
    st[1] = g_store2;
    for (j = 0; j < 2; j++) {
        CHECK(open_h(t[j], st[j], 16, 2, NULL, 0.5));
        id97[j] = add1(t[j], 1, 97 * M, PSYTL_MARK, 0, 0.0f);
        CHECK_I(psytl_anchor(t[j], 1, 0, 0), 0);
        for (k = 0; k <= 10; k++) (void)eval_at(t[j], k * P, P, k, NULL, 0);   /* reach 105 ms */
        CHECK(psytl_find(t[j], id97[j]) && psytl_find(t[j], id97[j])->frame == 10);
        /* Forward to 101 ms, but at an RT time after the next onset: that
         * window ends at 76 ms, behind the reach. */
        CHECK_I(psytl_anchor(t[j], 1, 140 * M, 101 * M), 0);
        id90[j] = add1(t[j], 1, 90 * M, PSYTL_MARK, 0, 0.0f);    /* late: <= reach */
        CHECK_I(eval_at(t[j], 110 * M, P, 11, NULL, 0), 0);      /* it waits */
        CHECK(is_pending_reset(psytl_find(t[j], id90[j])));
    }
    /* Back to 95 ms (<= the furthest onset, 100 ms): 97 is pending again
     * in both; the skip also skips 90. */
    CHECK_I(psytl_skip(&g_tl, 1, 120 * M, 95 * M), 1);
    CHECK_I(psytl_anchor(&g_tl2, 1, 120 * M, 95 * M), 0);
    CHECK(is_pending_reset(psytl_find(&g_tl, id97[0])));
    CHECK(is_pending_reset(psytl_find(&g_tl2, id97[1])));
    CHECK(psytl_find(&g_tl, id90[0]) && psytl_find(&g_tl, id90[0])->flags == PSYTL_EV_SKIPPED
          && psytl_find(&g_tl, id90[0])->residual == 5 * M);
    n = eval_at(&g_tl, 120 * M, P, 12, g_fired, 4);
    CHECK_I(n, 1);
    if (n >= 1) { CHECK_I(g_fired[0].id, id97[0]); CHECK_I(g_fired[0].flags, PSYTL_EV_FIRED); }
    n = eval_at(&g_tl2, 120 * M, P, 12, g_fired, 4);
    CHECK_I(n, 2);
    if (n >= 2) {
        CHECK_I(g_fired[0].id, id90[1]);
        CHECK_I(g_fired[0].flags, PSYTL_EV_FIRED | PSYTL_EV_LATE);
        CHECK_I(g_fired[1].id, id97[1]);
    }
}

/* Refusals change nothing; a stopped or paused base runs after a skip; an
 * event exactly at bt is not skipped; remove of a skipped event that the
 * channel shows recomputes the channel. */
static void test_skip_misc(void) {
    static psytl_timeline snap;
    const int64_t M = MS_NS;
    int i, a, s5, x, n;
    int64_t bt = 0;

    CHECK(open_h(&g_tl, g_store, 32, 4, NULL, 0.5));
    for (i = 0; i < 10; i++) CHECK(add1(&g_tl, 2, i * M, PSYTL_MARK, 0, 0.0f) >= 0);
    s5 = add1(&g_tl, 2, 5 * M, PSYTL_SET, 1, 7.0f);
    a = add1(&g_tl, 2, 4 * M, PSYTL_SET, 1, 3.0f);
    memcpy(&snap, &g_tl, sizeof snap);
    memcpy(g_snap, g_store, 32 * sizeof g_store[0]);
    CHECK_I(psytl_skip(&g_tl, 0, 0, 0), PSYTL_ERR_ARG);
    CHECK_I(psytl_skip(&g_tl, PSYTL_MAX_BASES, 0, 0), PSYTL_ERR_ARG);
    CHECK_I(psytl_skip(&g_tl, -1, 0, 0), PSYTL_ERR_ARG);
    CHECK_I(psytl_skip(&g_tl, 2, 0, LIM62), PSYTL_ERR_ARG);
    CHECK_I(psytl_skip(&g_tl, 2, 0, -LIM62), PSYTL_ERR_ARG);
    CHECK(memcmp(&snap, &g_tl, sizeof snap) == 0);
    CHECK(memcmp(g_snap, g_store, 32 * sizeof g_store[0]) == 0);
    CHECK_I(psytl_skip(&g_closed, 2, 0, 0), PSYTL_ERR_CLOSED);
    CHECK_I(psytl_skip(NULL, 2, 0, 0), PSYTL_ERR_CLOSED);

    /* Base 2 was never anchored: MARKs 0..4 and the SET at 4 ms are
     * skipped; the MARK and SET exactly at 5 ms are not. */
    CHECK_I(psytl_skip(&g_tl, 2, 1000 * M, 5 * M), 6);
    CHECK(psytl_base_time(&g_tl, 2, 1001 * M, &bt));
    CHECK_I(bt, 6 * M);
    CHECK(psytl_value(&g_tl, 1) == 3.0f);
    CHECK(is_pending_reset(psytl_find(&g_tl, s5)));
    x = add1(&g_tl, 2, 3 * M, PSYTL_MARK, 0, 0.0f);   /* late: the base passed it */
    n = eval_at(&g_tl, 1000 * M, 0, 0, g_fired, 8);
    CHECK_I(n, 3);
    CHECK(psytl_find(&g_tl, x) && psytl_find(&g_tl, x)->flags == (PSYTL_EV_FIRED | PSYTL_EV_LATE));
    CHECK(psytl_find(&g_tl, s5) && psytl_find(&g_tl, s5)->flags == PSYTL_EV_FIRED);
    CHECK(psytl_value(&g_tl, 1) == 7.0f);

    /* Remove the shown SET; the skipped one at 4 ms shows again. */
    CHECK_I(psytl_remove(&g_tl, s5), 0);
    CHECK(psytl_value(&g_tl, 1) == 3.0f);
    CHECK_I(psytl_remove(&g_tl, a), 0);
    CHECK(psytl_value(&g_tl, 1) == 0.0f);   /* channel free again: initial */

    /* A skip runs a paused base; a paused seek is a skip and a pause. */
    CHECK_I(psytl_anchor(&g_tl, 3, 0, 0), 0);
    CHECK_I(psytl_pause(&g_tl, 3, 10), 0);
    CHECK_I(psytl_skip(&g_tl, 3, 20, 50), 0);
    CHECK(psytl_base_time(&g_tl, 3, 30, &bt));
    CHECK_I(bt, 60);
    CHECK_I(psytl_pause(&g_tl, 3, 20), 0);
    CHECK(psytl_base_time(&g_tl, 3, 999, &bt));
    CHECK_I(bt, 50);
    /* A skip to where the base already is skips nothing more. */
    CHECK_I(psytl_skip(&g_tl, 2, 2000 * M, 5 * M), 0);
    check_sorted(&g_tl, __LINE__);
}

/* Tracks are a function of base time: after a skip and after an anchor to
 * the same place the next frame's values are the same bits, a waiting
 * tween included, which takes over from the old track at its start. */
static void test_skip_tracks(void) {
    static psytl_key ramp[2];
    psytl_timeline* t[2];
    psytl_event* st[2];
    psytl_tween_desc d;
    const int64_t P = P60, S = S_NS;
    int j;
    double from, want;
    ramp[0] = mkkey(0, 0.0f, PSYTL_EASE_LINEAR);
    ramp[1] = mkkey(100 * S, 10.0f, PSYTL_EASE_LINEAR);
    t[0] = &g_tl;
    t[1] = &g_tl2;
    st[0] = g_store;
    st[1] = g_store2;
    for (j = 0; j < 2; j++) {
        CHECK(open_h(t[j], st[j], 16, 3, NULL, 0.5));
        CHECK_I(psytl_set_keys(t[j], 1, 1, ramp, 2), 0);
        CHECK_I(psytl_set_keys(t[j], 2, 1, ramp, 2), 0);
        CHECK_I(psytl_anchor(t[j], 1, S, 0), 0);
        CHECK_I(eval_at(t[j], S, P, 0, NULL, 0), 0);
        d = twd_at(3.0f, 20 * S, 40 * S);
        CHECK_I(psytl_tween(t[j], 2, 1, &d), 0);
    }
    CHECK_I(psytl_skip(&g_tl, 1, 2 * S, 50 * S), 0);
    CHECK_I(psytl_anchor(&g_tl2, 1, 2 * S, 50 * S), 0);
    CHECK_I(eval_at(&g_tl, 2 * S, P, 1, NULL, 0), 0);
    CHECK_I(eval_at(&g_tl2, 2 * S, P, 1, NULL, 0), 0);
    CHECK(memcmp(psytl_values(&g_tl), psytl_values(&g_tl2), 3 * sizeof(float)) == 0);
    CHECK_F(psytl_value(&g_tl, 1), 5.0, 1e-6);
    from = 4.0;                                   /* the ramp at the tween's start */
    want = from + (3.0 - from) * 0.5;             /* halfway through, linear */
    CHECK_F(psytl_value(&g_tl, 2), want, 1e-5);
}

/* PEEK on fixed bases and times (the RT times of report_setup()). */
static int g_pk[9];
static psytl_timeline g_pk_snap;

static int peek_pure(int line, int base, int64_t from, int64_t to, psytl_event* out, int cap) {
    int r, i;
    memcpy(&g_pk_snap, &g_tl, sizeof g_tl);
    memcpy(g_snap, g_store, 32 * sizeof g_store[0]);
    for (i = 0; i < 16; i++) g_fired[i].id = -999;
    r = psytl_peek(&g_tl, base, from, to, out, cap);
    if (memcmp(&g_pk_snap, &g_tl, sizeof g_tl) != 0) fail(line, "peek changed the handle");
    if (memcmp(g_snap, g_store, 32 * sizeof g_store[0]) != 0) fail(line, "peek changed the storage");
    if (out == g_fired && cap >= 0 && cap < 16 && g_fired[cap].id != -999) fail(line, "peek wrote past cap");
    return r;
}

/* out[0..n) has the ids want[0..n) (indexes into g_pk) at RT times rt[]. */
static void peek_expect(int line, int n, const int* want, const int64_t* rt) {
    int i;
    for (i = 0; i < n; i++) {
        const psytl_event* s = psytl_find(&g_tl, g_fired[i].id);
        if (g_fired[i].id != g_pk[want[i]]) { fail_i(line, "peek id", g_fired[i].id, g_pk[want[i]]); return; }
        if (g_fired[i].onset != rt[i]) fail_i(line, "peek onset (RT time)", g_fired[i].onset, rt[i]);
        if (g_fired[i].residual != 0 || g_fired[i].frame != -1 || g_fired[i].flags != 0)
            fail(line, "peek copy: residual 0, frame -1, flags 0");
        if (!s || s->time != g_fired[i].time || s->base != g_fired[i].base || s->kind != g_fired[i].kind
            || s->code != g_fired[i].code || s->user != g_fired[i].user)
            fail(line, "peek copy differs from the storage");
    }
}

static void test_peek(void) {
    static const int all[7] = { 6, 1, 4, 5, 0, 3, 2 };
    static const int64_t all_rt[7] = { 1050, 1100, 1100, 1100, 1300, 1300, 1500 };
    int i, n;
    int ids[3];
    int64_t rts[3];

    report_setup();   /* base 1 at (1000, 0), base 2 at (2000, 500) */
    for (i = 0; i < 8; i++) g_pk[i] = g_rep[i];
    g_pk[8] = add1(&g_tl, 3, 10, PSYTL_MARK, 0, 0.0f);   /* base 3 stopped */
    n = peek_pure(__LINE__, PSYTL_ALL_BASES, 0, 2000, g_fired, 16);
    CHECK_I(n, 7);
    if (n == 7) peek_expect(__LINE__, 7, all, all_rt);
    /* The window is [from, to). */
    n = peek_pure(__LINE__, PSYTL_ALL_BASES, 1100, 1300, g_fired, 16);
    CHECK_I(n, 3);
    if (n == 3) peek_expect(__LINE__, 3, all + 1, all_rt + 1);
    n = peek_pure(__LINE__, PSYTL_ALL_BASES, 1101, 1301, g_fired, 16);
    CHECK_I(n, 2);
    if (n == 2) peek_expect(__LINE__, 2, all + 4, all_rt + 4);
    CHECK_I(peek_pure(__LINE__, PSYTL_ALL_BASES, 1100, 1100, g_fired, 16), 0);
    CHECK_I(peek_pure(__LINE__, PSYTL_ALL_BASES, 1099, 1100, g_fired, 16), 0);
    CHECK_I(peek_pure(__LINE__, PSYTL_ALL_BASES, 1100, 1101, g_fired, 16), 3);
    /* One base at a time; base 3 is stopped; base 1's pending event at
     * 6000 is in a later window. */
    n = peek_pure(__LINE__, 1, 0, 2000, g_fired, 16);
    CHECK_I(n, 2);
    ids[0] = 1; ids[1] = 3; rts[0] = 1100; rts[1] = 1300;
    if (n == 2) peek_expect(__LINE__, 2, ids, rts);
    n = peek_pure(__LINE__, 2, 0, 2000, g_fired, 16);
    CHECK_I(n, 2);
    ids[0] = 5; ids[1] = 2; rts[0] = 1100; rts[1] = 1500;
    if (n == 2) peek_expect(__LINE__, 2, ids, rts);
    n = peek_pure(__LINE__, 0, 0, 2000, g_fired, 16);
    CHECK_I(n, 3);
    ids[0] = 6; ids[1] = 4; ids[2] = 0; rts[0] = 1050; rts[1] = 1100; rts[2] = 1300;
    if (n == 3) peek_expect(__LINE__, 3, ids, rts);
    CHECK_I(peek_pure(__LINE__, 3, -LIM62 + 1, LIM62 - 1, g_fired, 16), 0);
    CHECK_I(peek_pure(__LINE__, 1, 0, 6000, g_fired, 16), 2);
    CHECK_I(peek_pure(__LINE__, 1, 0, 6001, g_fired, 16), 3);
    /* The cap: the earliest, and the total. NULL with cap 0 counts. */
    n = peek_pure(__LINE__, PSYTL_ALL_BASES, 0, 2000, g_fired, 3);
    CHECK_I(n, 7);
    peek_expect(__LINE__, 3, all, all_rt);
    CHECK_I(peek_pure(__LINE__, PSYTL_ALL_BASES, 0, 2000, NULL, 0), 7);
    CHECK_I(peek_pure(__LINE__, PSYTL_ALL_BASES, 0, 2000, g_fired, 0), 7);
    /* Refusals. */
    CHECK_I(peek_pure(__LINE__, PSYTL_ALL_BASES, 0, 2000, g_fired, -1), PSYTL_ERR_ARG);
    CHECK_I(peek_pure(__LINE__, PSYTL_ALL_BASES, 0, 2000, NULL, 2), PSYTL_ERR_ARG);
    CHECK_I(peek_pure(__LINE__, PSYTL_ALL_BASES, 2000, 1999, g_fired, 4), PSYTL_ERR_ARG);
    CHECK_I(peek_pure(__LINE__, PSYTL_MAX_BASES, 0, 2000, g_fired, 4), PSYTL_ERR_ARG);
    CHECK_I(peek_pure(__LINE__, -2, 0, 2000, g_fired, 4), PSYTL_ERR_ARG);
    CHECK_I(peek_pure(__LINE__, 1, -LIM62, 0, g_fired, 4), PSYTL_ERR_ARG);
    CHECK_I(peek_pure(__LINE__, 1, 0, LIM62, g_fired, 4), PSYTL_ERR_ARG);
    CHECK_I(psytl_peek(&g_closed, 1, 0, 1, NULL, 0), PSYTL_ERR_CLOSED);
    CHECK_I(psytl_peek(NULL, 1, 0, 1, NULL, 0), PSYTL_ERR_CLOSED);

    /* Fired events are not pending: after a frame at 1100, four are gone. */
    CHECK_I(eval_at(&g_tl, 1100, 0, 0, NULL, 0), 4);
    n = peek_pure(__LINE__, PSYTL_ALL_BASES, 0, 2000, g_fired, 16);
    CHECK_I(n, 3);
    if (n == 3) peek_expect(__LINE__, 3, all + 4, all_rt + 4);
    /* A late event is pending, at its RT time in the past. */
    g_pk[0] = add1(&g_tl, 0, 1000, PSYTL_MARK, 0, 0.0f);
    n = peek_pure(__LINE__, PSYTL_ALL_BASES, 0, 2000, g_fired, 16);
    CHECK_I(n, 4);
    ids[0] = 0; rts[0] = 1000;
    if (n >= 1) peek_expect(__LINE__, 1, ids, rts);
    CHECK_I(peek_pure(__LINE__, PSYTL_ALL_BASES, 1001, 2000, g_fired, 16), 3);
    /* A paused base has no RT times; resumed, its times move. Base 2 at
     * RT 1200 is at base time -300; resumed at 3000. */
    CHECK_I(psytl_pause(&g_tl, 2, 1200), 0);
    CHECK_I(peek_pure(__LINE__, 2, -LIM62 + 1, LIM62 - 1, g_fired, 16), 0);
    CHECK_I(psytl_resume(&g_tl, 2, 3000), 0);
    n = peek_pure(__LINE__, 2, 0, 4000, g_fired, 16);
    CHECK_I(n, 1);   /* -400 fired at 1100; 0 is at RT 3300 */
    ids[0] = 2; rts[0] = 3300;
    if (n == 1) peek_expect(__LINE__, 1, ids, rts);
    /* A skipped event is not pending: base 1 to 400 skips 300. */
    CHECK_I(psytl_skip(&g_tl, 1, 1150, 400), 1);
    n = peek_pure(__LINE__, 1, -LIM62 + 1, LIM62 - 1, g_fired, 16);
    CHECK_I(n, 1);
    ids[0] = 7; rts[0] = 1150 + 4600;
    if (n == 1) peek_expect(__LINE__, 1, ids, rts);
}

/* The audio path's use: each 60 Hz frame peeks [last to, onset + L) with
 * L the audio lead plus a frame, then evaluates. Every event is seen in
 * exactly one peek, at least L - P before its RT time and before the frame
 * that fires it, and fires exactly once. */
static void test_peek_lookahead(void) {
    enum { NE = 200 };
    static int seen[NE + 8], fired_n[NE + 8];
    static int64_t seen_at[NE + 8], rt_of[NE + 8];
    static psytl_event out[64];
    const int64_t P = P60, R = S_NS, L = 55 * MS_NS + P60;
    int64_t to = R, worst = INT64_MAX;
    int i, k, n, bad_lead = 0, bad_order = 0, bad_seen = 0, bad_fire = 0;
    g_rng = 4242;
    CHECK(open_h(&g_tl, g_store, NE + 8, 1, NULL, 0.5));
    CHECK_I(psytl_anchor(&g_tl, 1, R, 0), 0);
    memset(seen, 0, sizeof seen);
    memset(fired_n, 0, sizeof fired_n);
    for (i = 0; i < NE; i++) {
        int64_t t = rnd_range(L, 20 * S_NS);
        int id = add1(&g_tl, 1, t, PSYTL_MARK, 0, 0.0f);
        CHECK_I(id, i);
        rt_of[i] = R + t;
    }
    for (k = 0; k < 22 * 60; k++) {
        int64_t T = R + k * P;
        n = psytl_peek(&g_tl, 1, to, T + L, out, 64);
        CHECK(n >= 0 && n <= 64);
        for (i = 0; i < n && i < 64; i++) {
            int id = out[i].id;
            if (id < 0 || id >= NE || seen[id]) { bad_seen++; continue; }
            seen[id] = 1;
            seen_at[id] = T;
            if (out[i].onset != rt_of[id]) bad_seen++;
            if (rt_of[id] - T < L - P) bad_lead++;
            if (rt_of[id] - T < worst) worst = rt_of[id] - T;
        }
        to = T + L;
        n = eval_at(&g_tl, T, P, k, out, 64);
        for (i = 0; i < n && i < 64; i++) {
            int id = out[i].id;
            if (id < 0 || id >= NE) { bad_fire++; continue; }
            fired_n[id]++;
            if (!seen[id] || seen_at[id] >= T) bad_order++;
        }
    }
    for (i = 0; i < NE; i++) {
        if (!seen[i]) bad_seen++;
        if (fired_n[i] != 1) bad_fire++;
    }
    CHECK_I(bad_seen, 0);
    CHECK_I(bad_lead, 0);
    CHECK_I(bad_order, 0);
    CHECK_I(bad_fire, 0);
    CHECK(worst >= L - P);
}

/* -------------------------------------------- 12. the randomized model */

enum { M_CAP = 48, M_NCH = 8, M_EVCH = 6 };

/* Passed by the base: fired, or skipped (SKIP). */
#define M_DONE (PSYTL_EV_FIRED | PSYTL_EV_SKIPPED)

typedef struct mev {
    psytl_event e;
    int late;    /* added at or before its base's reach             */
    int ambig;   /* LATE bit not checked: see the note in m_rewind() */
} mev;

typedef struct mbase {
    int state;          /* 0 stopped, 1 running, 2 paused */
    int64_t art, abt;   /* running: base time abt at RT art */
    int64_t frozen;     /* paused */
    int64_t last_hi;    /* the reach: furthest window end since the last rewind */
    int64_t last_on;    /* furthest base time of an evaluated onset, same span */
    int evaluated;
    /* A tween's "now" (TWEENS): the base time of the last evaluated onset,
     * the last anchor's base time, and how many anchors, pauses and
     * resumes since the last evaluate of the base. */
    int64_t ev_bt, anc_bt;
    int ev_any, moved;
    int64_t nowv;       /* "now" as TWEENS defines it: set by every evaluate,
                         * anchor, pause and resume of the base */
} mbase;

static mev m_ev[M_CAP + 8];
static int m_n, m_next_id;
static mbase m_b[PSYTL_MAX_BASES];
static float m_init[M_NCH], m_trackv[M_NCH];
static int64_t m_onset, m_index, m_period, m_w, m_pe;
static int m_has_onset;
static psytl_key m_k6[6], m_k7[5];
/* Channels 6 and 7: the driver each has (none after a tween on another
 * base stopped it), its base, and a tween waiting for its start, resolved
 * (m_ps the absolute start, m_pcall the value at the call, m_pstep a STEP
 * tween, promoted at the window's end). set_track and tween replace them;
 * nothing removes them, so they stay BOUND. */
static rdrv m_drv[M_NCH];
static int m_has_drv[M_NCH], m_tb[M_NCH], m_pend[M_NCH], m_pstep[M_NCH];
static psytl_tween_desc m_pd[M_NCH];
static int64_t m_ps[M_NCH];
static float m_pcall[M_NCH];
static psytl_key m_krk[M_NCH][3];
static float m_samp[64];
static const psytl_curve m_curv[2] = { { 0.42f, 0.0f, 0.58f, 1.0f }, { 0.68f, -0.6f, 0.32f, 1.6f } };
static mev m_fired[M_CAP + 8];
static int m_nf;
static uint64_t m_seed;
static uint64_t m_trace_seed;   /* TL_TRACE */
static int m_step;
/* What the randomized check exercised, printed on a pass so that a
 * vacuous run shows. */
static long m_stat_eval, m_stat_fired, m_stat_late, m_stat_arew, m_stat_wrew, m_stat_unfired, m_stat_err, m_stat_wait, m_stat_trk;
static long m_stat_tw, m_stat_twrej, m_stat_promo, m_stat_kv, m_stat_xbase, m_stat_twnow;
static long m_stat_skip, m_stat_skipped, m_stat_skipback, m_stat_skiplate, m_stat_peek, m_stat_peeked, m_stat_peektrunc;
static const char* m_op;

static int m_fail(const char* what, long long got, long long want) {
    fprintf(stderr, "psy_timeline_test: FAIL model seed %llu step %d op %s: %s (got %lld, want %lld)\n",
            (unsigned long long)m_seed, m_step, m_op, what, got, want);
    g_failures++;
    return 1;
}

static void print_ev(const char* tag, const psytl_event* e) {
    fprintf(stderr, "  %s: id %d base %d time %lld kind %d target %d value %g flags %u "
            "frame %lld onset %lld residual %lld\n", tag, e->id, e->base, (long long)e->time,
            e->kind, e->target, (double)e->value, (unsigned)e->flags, (long long)e->frame,
            (long long)e->onset, (long long)e->residual);
}

static int64_t m_bt(int b, int64_t rt) {
    if (b == 0) return rt;
    if (m_b[b].state == 2) return m_b[b].frozen;
    return m_b[b].abt + (rt - m_b[b].art);
}

/* A tween's "now" on base b (TWEENS): the base time of the last evaluated
 * onset, unless an anchor, pause or resume came after it, which set now
 * to the anchor's base time or the frozen time; 0 on a base never
 * evaluated or anchored. Base 0 is never anchored. *ok is always 1: the
 * rule has no reading left open. */
static int64_t m_now(int b, int* ok) {
    *ok = 1;
    if (b == 0) return m_has_onset ? m_onset : 0;
    return m_b[b].nowv;
}

static int is_chan_kind(int kind) {
    return kind == PSYTL_ONSET || kind == PSYTL_OFFSET || kind == PSYTL_SET;
}

/* -2 keyed, -1 free, else the base of its events. */
static int m_bind(int ch) {
    int i;
    if (ch == 6 || ch == 7) return -2;
    for (i = 0; i < m_n; i++)
        if (is_chan_kind(m_ev[i].e.kind) && m_ev[i].e.target == ch) return m_ev[i].e.base;
    return -1;
}

static float m_chan_value(int ch) {
    int i, best = -1;
    for (i = 0; i < m_n; i++) {
        const psytl_event* e = &m_ev[i].e;
        if (!is_chan_kind(e->kind) || e->target != ch || !(e->flags & ((g_mut & MUT_SKIP_NOCHAN) ? PSYTL_EV_FIRED : M_DONE))) continue;
        if (best < 0 || e->time > m_ev[best].e.time ||
            (e->time == m_ev[best].e.time && e->id > m_ev[best].e.id)) best = i;
    }
    if (best < 0) return m_init[ch];
    if (m_ev[best].e.kind == PSYTL_ONSET) return 1.0f;
    if (m_ev[best].e.kind == PSYTL_OFFSET) return 0.0f;
    return m_ev[best].e.value;
}

static void m_unfire(mev* m) {
    m->e.flags = 0;
    m->e.frame = -1;
    m->e.onset = 0;
    m->e.residual = 0;
    m->late = 0;
}

/* A rewinding anchor of base b at bt: fired events with time >= bt are
 * pending again. A pending event that was added late and is now past the
 * reach: the manual fixes lateness at add and sets the flag at fire, but
 * does not say whether a rewind past it clears it, so its LATE bit is not
 * compared afterwards. */
static void m_rewind(int b, int64_t bt) {
    int i;
    for (i = 0; i < m_n; i++) {
        mev* m = &m_ev[i];
        if (m->e.base != b || m->e.time < bt) continue;
        if (m->e.flags & ((g_mut & MUT_SKIP_UNREW) ? PSYTL_EV_FIRED : M_DONE)) { m_unfire(m); m_stat_unfired++; }
        else if (m->late) m->ambig = 1;
    }
}

static int fired_cmp(const void* a, const void* b) {
    return report_cmp(&((const mev*)a)->e, &((const mev*)b)->e);
}

static int store_cmp(const void* a, const void* b) {
    return key_cmp(&((const mev*)a)->e, &((const mev*)b)->e);
}

static void m_evaluate(int64_t onset, int64_t index) {
    int b, i, ch;
    m_nf = 0;
    for (b = 0; b < PSYTL_MAX_BASES; b++) {
        int64_t hi, bt_on;
        if (b != 0 && m_b[b].state == 0) continue;
        bt_on = m_bt(b, onset);
        hi = m_b[b].state == 2 && b != 0 ? m_b[b].frozen : m_bt(b, onset + m_w);
        /* An evaluate never un-fires: a window behind the reach fires
         * only what it reaches; later events wait. */
        if (m_b[b].evaluated && hi < m_b[b].last_hi) m_stat_wrew++;
        for (i = 0; i < m_n; i++) {
            mev* m = &m_ev[i];
            if (m->e.base != b || (m->e.flags & M_DONE) || m->e.time > hi) continue;
            m->e.flags = (uint16_t)(PSYTL_EV_FIRED | (m->late ? PSYTL_EV_LATE : 0u));
            m->e.frame = index;
            m->e.onset = onset;
            m->e.residual = bt_on - m->e.time;
            m_fired[m_nf++] = *m;
            m_stat_fired++;
            if (m->late) m_stat_late++;
        }
        for (i = 0; i < m_n; i++)
            if (m_ev[i].e.base == b && !(m_ev[i].e.flags & M_DONE) && m_ev[i].late) m_stat_wait++;
        if (!m_b[b].evaluated || hi > m_b[b].last_hi) m_b[b].last_hi = hi;
        if (!m_b[b].evaluated || bt_on > m_b[b].last_on) m_b[b].last_on = bt_on;
        m_b[b].evaluated = 1;
        m_b[b].ev_bt = bt_on;
        m_b[b].nowv = bt_on;
        m_b[b].ev_any = 1;
        m_b[b].moved = 0;
        /* A waiting tween takes over once its read time reaches its start
         * (the window's end for a STEP tween, else the onset), from the
         * old driver's value at the start. Then a STEP track at the
         * window's end, any other at the onset. */
        for (ch = 6; ch < M_NCH; ch++) {
            if (m_tb[ch] != b) continue;
            if (m_pend[ch] && (m_pstep[ch] ? m_bt(b, onset + m_w) : bt_on) >= m_ps[ch]) {
                rdrv nd;
                rdrv_tween(&nd, &m_pd[ch], m_ps[ch], m_has_drv[ch] ? &m_drv[ch] : NULL, (double)m_trackv[ch],
                           (double)m_pcall[ch]);
                if (m_trace_seed == m_seed)
                    fprintf(stderr, "  [%d] takeover ch%d b%d at %lld (onset bt %lld) old %s %.9g -> from %.9g\n", m_step,
                            ch, b, (long long)m_ps[ch], (long long)bt_on, m_has_drv[ch] ? "track" : "held",
                            m_has_drv[ch] ? rdrv_value(&m_drv[ch], m_ps[ch]) : (double)m_trackv[ch],
                            nd.herm ? nd.hf : (double)nd.k[0].value);
                m_drv[ch] = nd;
                m_has_drv[ch] = 1;
                m_pend[ch] = 0;
                m_stat_promo++;
            }
            if (m_has_drv[ch])
                m_trackv[ch] = (float)rdrv_value(&m_drv[ch], rdrv_step(&m_drv[ch]) ? m_bt(b, onset + m_w) : bt_on);
        }
    }
    qsort(m_fired, (size_t)m_nf, sizeof m_fired[0], fired_cmp);
}

static const char* ev_diff(const psytl_event* g, const mev* m) {
    const psytl_event* w = &m->e;
    unsigned mask;
    if (g->id != w->id) return "id";
    if (g->time != w->time) return "time";
    if (g->base != w->base) return "base";
    if (g->kind != w->kind) return "kind";
    if (g->target != w->target) return "target";
    if (g->value != w->value) return "value";
    if (g->code != w->code) return "code";
    if (g->user != w->user) return "user";
    if (g->reserved_ != 0) return "reserved_";
    if (g->frame != w->frame) return "frame";
    if (g->onset != w->onset) return "onset";
    if (g->residual != w->residual) return "residual";
    /* Pending: flags 0, LATE included (set at fire). */
    mask = (w->flags & PSYTL_EV_FIRED) && m->ambig ? ~PSYTL_EV_LATE : 0xFFFFu;
    if ((g->flags & mask) != (w->flags & mask)) return "flags";
    return NULL;
}

static int m_check(void) {
    static mev sorted[M_CAP + 8];
    const psytl_event* p;
    const float* v;
    int n = -1, i, ch;
    p = psytl_events(&g_tl, PSYTL_ALL_BASES, &n);
    if (n != m_n) return m_fail("event count", n, m_n);
    memcpy(sorted, m_ev, (size_t)m_n * sizeof m_ev[0]);
    qsort(sorted, (size_t)m_n, sizeof sorted[0], store_cmp);
    for (i = 0; i < n; i++) {
        const char* d = ev_diff(&p[i], &sorted[i]);
        if (d) {
            print_ev("got ", &p[i]);
            print_ev("want", &sorted[i].e);
            return m_fail(d, i, i);
        }
    }
    v = psytl_values(&g_tl);
    if (!v) return m_fail("values NULL", 0, 1);
    for (ch = 0; ch < M_EVCH; ch++) {
        float want = m_chan_value(ch);
        if (v[ch] != want) {
            fprintf(stderr, "  channel %d got %g want %g\n", ch, (double)v[ch], (double)want);
            return m_fail("event channel value", ch, ch);
        }
    }
    for (ch = 6; ch < 8; ch++) {
        if (!(fabs((double)v[ch] - (double)m_trackv[ch]) <= 1e-5 * (1.0 + fabs((double)m_trackv[ch])))) {
            fprintf(stderr, "  channel %d got %.9g want %.9g\n", ch, (double)v[ch], (double)m_trackv[ch]);
            return m_fail("track channel value", ch, ch);
        }
    }
    return 0;
}

static int64_t m_rand_time(int b) {
    int64_t now = m_has_onset ? m_onset : T0, t;
    if (b != 0 && m_b[b].state == 0) t = rnd_range(-5 * m_pe, 30 * m_pe);
    else t = m_bt(b, now) + rnd_range(-4 * m_pe, 8 * m_pe);
    if (rnd_int(3) == 0) t -= t % m_pe;   /* ties on a coarse grid */
    /* On or next to a window edge: the last one, or one a few frames on. */
    if (rnd_int(5) == 0 && m_b[b].evaluated)
        t = m_b[b].last_hi + (int64_t)rnd_int(4) * m_pe + rnd_range(-1, 1);
    /* At the base time of the last onset, where an anchor rewinds from. */
    else if (rnd_int(6) == 0 && m_b[b].evaluated)
        t = m_b[b].last_on + rnd_range(-1, 1);
    return t;
}

static psytl_event m_rand_event(void) {
    psytl_event e;
    int r = rnd_int(100), b = rnd_int(4);
    memset(&e, 0, sizeof e);
    e.base = (uint8_t)b;
    e.time = m_rand_time(b);
    e.code = (int32_t)rnd_range(-100000, 100000);
    e.user = sm64();
    e.value = (float)rnd_int(400) / 8.0f - 10.0f;
    e.target = (int32_t)rnd_range(-1000, 1000);
    /* The header assigns these; the caller's garbage must not survive. */
    e.id = 4242;
    e.flags = (uint16_t)0xFFFFu;
    e.frame = 77;
    e.onset = 88;
    e.residual = 99;
    if (r < 15) e.kind = PSYTL_MARK;
    else if (r < 25) e.kind = PSYTL_TRIGGER;
    else if (r < 45) { e.kind = PSYTL_ONSET; e.target = rnd_int(M_EVCH); }
    else if (r < 60) { e.kind = PSYTL_OFFSET; e.target = rnd_int(M_EVCH); }
    else if (r < 85) { e.kind = PSYTL_SET; e.target = rnd_int(M_EVCH); }
    else if (r < 92) e.kind = (uint8_t)(16 + rnd_int(240));
    else if (r < 95) e.kind = (uint8_t)(5 + rnd_int(11));
    else if (r < 97) { e.kind = PSYTL_SET; e.target = rnd_int(M_EVCH); e.value = (float)make_nan(); }
    else if (r < 98) { e.kind = PSYTL_MARK; e.base = PSYTL_MAX_BASES; }
    else { e.kind = PSYTL_ONSET; e.target = 6 + rnd_int(2); }
    /* Mostly on the channel's own base, so BOUND does not dominate. */
    if (is_chan_kind(e.kind) && e.target >= 0 && e.target < M_EVCH && e.base < PSYTL_MAX_BASES &&
        m_bind(e.target) >= 0 && rnd_int(5) != 0) {
        e.base = (uint8_t)m_bind(e.target);
        e.time = m_rand_time(e.base);
    }
    return e;
}

/* The expected code of an add of es[0..n), and how many error classes it
 * has; with more than one, any negative code is accepted. */
static int m_predict(const psytl_event* es, int n, int* classes) {
    int tmp[M_NCH], ch, i, arg = 0, bound = 0, full = 0;
    for (ch = 0; ch < M_NCH; ch++) tmp[ch] = m_bind(ch);
    for (i = 0; i < n; i++) {
        const psytl_event* e = &es[i];
        int bad = 0;
        if (e->base >= PSYTL_MAX_BASES || (e->kind >= 5 && e->kind <= 15)) { arg = 1; bad = 1; }
        if (is_chan_kind(e->kind)) {
            if (e->target < 0 || e->target >= M_NCH) { arg = 1; continue; }
            if (e->kind == PSYTL_SET && !(fabs((double)e->value) <= 3.4e38)) { arg = 1; bad = 1; }
            if (tmp[e->target] == -2 || (tmp[e->target] >= 0 && tmp[e->target] != e->base)) bound = 1;
            else if (!bad) tmp[e->target] = e->base;
        }
    }
    if (m_n + n > M_CAP) full = 1;
    *classes = arg + bound + full;
    if (arg) return PSYTL_ERR_ARG;
    if (bound) return PSYTL_ERR_BOUND;
    if (full) return PSYTL_ERR_FULL;
    return 0;
}

/* TL_TRACE=<seed> prints the model's view of every step of that seed. */
static void m_trace(const char* op, int b, int64_t x, int64_t y, int ret) {
    if (m_trace_seed != m_seed) return;
    fprintf(stderr, "  [%d] %s b%d %lld %lld ret %d", m_step, op, b, (long long)x, (long long)y, ret);
    if (b >= 0 && b < PSYTL_MAX_BASES)
        fprintf(stderr, " | state %d reach %lld last_on %lld", m_b[b].state,
                (long long)m_b[b].last_hi, (long long)m_b[b].last_on);
    fputc('\n', stderr);
}

static void m_append(const psytl_event* es, int n) {
    int i;
    for (i = 0; i < n; i++) {
        mev* m = &m_ev[m_n++];
        const mbase* bb = &m_b[es[i].base];
        m->e = es[i];
        m->e.id = m_next_id++;
        m->e.flags = 0;
        m->e.frame = -1;
        m->e.onset = 0;
        m->e.residual = 0;
        m->late = bb->evaluated && es[i].time <= bb->last_hi;
        m->ambig = 0;
        m_trace("added", es[i].base, m->e.id, es[i].time, m->late);
    }
}

static int m_check_add(int ret, const psytl_event* es, int n) {
    int classes = 0, want = m_predict(es, n, &classes);
    if (want == 0) {
        if (ret != m_next_id) return m_fail("add return", ret, m_next_id);
        m_append(es, n);
    } else {
        if (ret >= 0) return m_fail("add accepted a bad batch", ret, want);
        m_stat_err++;
        if (classes == 1 && ret != want) return m_fail("add error code", ret, want);
    }
    return 0;
}

/* SKIP, from the manual: an anchor (with its rewind), then every pending
 * event of the base before bt is skipped (flags SKIPPED, frame -1, onset
 * rt, residual bt - time), and the reach and the furthest evaluated onset
 * move to just before bt when they were behind it. */
static int m_op_skip(int64_t now) {
    int b = rnd_int(12) == 0 ? (rnd_int(2) ? 0 : PSYTL_MAX_BASES) : 1 + rnd_int(3);
    int ok = b > 0 && b < PSYTL_MAX_BASES;
    int64_t rt = now + rnd_range(-2 * m_pe, 3 * m_pe), bt;
    int q = rnd_int(5), ret, want = 0, i, rewound = 0;
    m_op = "skip";
    if (q == 1 && ok && m_b[b].evaluated) {
        bt = m_b[b].last_on + rnd_range(-3, 3) * (m_pe / 3);            /* near the rewind threshold */
    } else if (q == 2 && ok && m_b[b].state != 0) {
        bt = m_bt(b, rt) + rnd_range(-10 * m_pe, 30 * m_pe);            /* mostly forward */
    } else if (q == 3 && ok && m_n > 0) {
        const mev* m = &m_ev[rnd_int(m_n)];                             /* on an event's time */
        bt = (m->e.base == b ? m->e.time : rnd_range(0, 40 * m_pe)) + rnd_range(-1, 1);
    } else {
        bt = rnd_range(-5 * m_pe, 60 * m_pe);
    }
    ret = psytl_skip(&g_tl, b, rt, bt);
    if (!ok) {
        if (ret != PSYTL_ERR_ARG) return m_fail("skip bad base", ret, PSYTL_ERR_ARG);
        m_trace("skip", b, rt, bt, ret);
        return 0;
    }
    if (m_b[b].evaluated && bt <= m_b[b].last_on) {
        m_rewind(b, bt);
        m_b[b].last_hi = bt - 1;
        m_b[b].last_on = bt - 1;
        m_stat_arew++;
        m_stat_skipback++;
        rewound = 1;
    }
    m_b[b].state = 1;
    m_b[b].art = rt;
    m_b[b].abt = bt;
    m_b[b].anc_bt = bt;
    m_b[b].nowv = bt;
    m_b[b].moved++;
    if (!(g_mut & MUT_SKIP_FIRES)) {
        for (i = 0; i < m_n; i++) {
            mev* m = &m_ev[i];
            if (m->e.base != b || (m->e.flags & M_DONE)) continue;
            if (m->e.time > bt || (m->e.time == bt && !(g_mut & MUT_SKIP_AT))) continue;
            if (rewound && m->late && (g_mut & MUT_SKIP_LATEW)) continue;
            if (m->late) m_stat_skiplate++;
            m->e.flags = PSYTL_EV_SKIPPED;
            m->e.frame = -1;
            m->e.onset = rt;
            m->e.residual = bt - m->e.time;
            m->late = 0;
            m->ambig = 0;
            want++;
        }
        if (!m_b[b].evaluated || (bt - 1 > m_b[b].last_hi && !(g_mut & MUT_SKIP_REACH))) m_b[b].last_hi = bt - 1;
        if (!m_b[b].evaluated || (bt - 1 > m_b[b].last_on && !(g_mut & MUT_SKIP_NOW))) m_b[b].last_on = bt - 1;
        m_b[b].evaluated = 1;
    }
    m_stat_skip++;
    m_stat_skipped += want;
    m_trace("skip", b, rt, bt, ret);
    if (ret != want) return m_fail("skip return", ret, want);
    return 0;
}

/* PEEK, from the manual: the pending events of running bases (base 0
 * always runs) with RT time in [from, to), by RT time then id, onset the
 * RT time, residual 0; and nothing in the handle or the storage changes. */
static int m_op_peek(int64_t now) {
    static psytl_timeline snap_tl;
    static psytl_event snap_store[M_CAP];
    static mev want[M_CAP + 8];
    int r = rnd_int(20), b, cap, ret, nw = 0, i, lim;
    int64_t from, to;
    psytl_event* outp;
    m_op = "peek";
    b = r == 0 ? (rnd_int(2) ? PSYTL_MAX_BASES : -2) : r < 8 ? PSYTL_ALL_BASES : rnd_int(4);
    from = now + rnd_range(-10 * m_pe, 10 * m_pe);
    to = from + (rnd_int(15) == 0 ? -rnd_range(1, m_pe) : rnd_range(0, 30 * m_pe));
    if (rnd_int(3) == 0 && m_n > 0) {
        /* An edge on an event's RT time, or a ns either side of it. */
        const mev* m = &m_ev[rnd_int(m_n)];
        int eb = m->e.base;
        if (eb == 0 || m_b[eb].state == 1) {
            int64_t rte = eb == 0 ? m->e.time : m_b[eb].art + (m->e.time - m_b[eb].abt);
            if (rnd_int(2)) from = rte + rnd_range(-1, 1);
            else to = rte + rnd_range(-1, 1);
            if (to < from && rnd_int(4) != 0) { int64_t x = to; to = from; from = x; }
        }
    }
    cap = rnd_int(6);
    outp = (cap == 0 && rnd_int(2)) ? NULL : g_fired;
    for (i = 0; i < 16; i++) g_fired[i].id = -999;
    memcpy(&snap_tl, &g_tl, sizeof g_tl);
    memcpy(snap_store, g_store, sizeof snap_store);
    ret = psytl_peek(&g_tl, b, from, to, outp, cap);
    if (memcmp(&snap_tl, &g_tl, sizeof g_tl) != 0) return m_fail("peek changed the handle", 1, 0);
    if (memcmp(snap_store, g_store, sizeof snap_store) != 0) return m_fail("peek changed the storage", 1, 0);
    m_trace("peek", b, from, to, ret);
    if (b < PSYTL_ALL_BASES || b >= PSYTL_MAX_BASES || to < from) {
        if (ret != PSYTL_ERR_ARG) return m_fail("peek bad argument", ret, PSYTL_ERR_ARG);
        return 0;
    }
    for (i = 0; i < m_n; i++) {
        const mev* m = &m_ev[i];
        int eb = m->e.base;
        int64_t rte;
        if (b != PSYTL_ALL_BASES && eb != b) continue;
        if (eb != 0 && m_b[eb].state != 1) continue;
        if (m->e.flags & ((g_mut & MUT_PEEK_FIRED) ? PSYTL_EV_SKIPPED : M_DONE)) continue;
        rte = eb == 0 ? m->e.time : m_b[eb].art + (m->e.time - m_b[eb].abt);
        if (rte < from || rte > to || (rte == to && !(g_mut & MUT_PEEK_END))) continue;
        want[nw] = *m;
        want[nw].e.onset = (g_mut & MUT_PEEK_ONSET) ? m->e.time : rte;
        want[nw].e.residual = 0;
        want[nw].ambig = 0;
        nw++;
    }
    qsort(want, (size_t)nw, sizeof want[0], fired_cmp);
    m_stat_peek++;
    m_stat_peeked += nw;
    if (nw > cap) m_stat_peektrunc++;
    if (ret != nw) return m_fail("peek return", ret, nw);
    lim = nw < cap ? nw : cap;
    for (i = 0; i < lim; i++) {
        const char* d = ev_diff(&g_fired[i], &want[i]);
        if (d) {
            print_ev("got ", &g_fired[i]);
            print_ev("want", &want[i].e);
            return m_fail(d, i, i);
        }
    }
    if (outp && cap < 16 && g_fired[cap].id != -999) return m_fail("peek wrote past cap", cap, cap);
    return 0;
}

/* ops: also skip and peek (v0.3.0). Off, the run is v0.2.0's, op for op. */
static int model_run(uint64_t seed, int lead_num, int lead_den, int64_t period, int steps, int ops) {
    int ch, i;
    m_seed = seed;
    m_op = "open";
    m_step = -1;
    g_rng = seed;
    memset(m_b, 0, sizeof m_b);
    m_b[0].state = 1;
    m_n = 0;
    m_next_id = 0;
    m_has_onset = 0;
    m_onset = 0;
    m_index = 0;
    m_period = period;
    m_w = period * lead_num / lead_den;
    m_pe = period > 0 ? period : 10 * MS_NS;
    for (ch = 0; ch < M_NCH; ch++) { m_init[ch] = 0.125f * (float)ch - 0.3f; m_trackv[ch] = m_init[ch]; }
    m_k6[0] = mkkey(0, 0.0f, PSYTL_EASE_LINEAR);
    m_k6[1] = mkkey(5 * m_pe, 2.0f, PSYTL_EASE_COSINE);
    m_k6[2] = mkkey(5 * m_pe, 7.0f, PSYTL_EASE_STEP);
    m_k6[3] = mkkey(12 * m_pe, 3.0f, PSYTL_EASE_QUAD_IN);
    m_k6[4] = mkkey(20 * m_pe, 3.5f, PSYTL_EASE_LINEAR);
    m_k6[5] = mkkey(30 * m_pe, 10.0f, PSYTL_EASE_LINEAR);
    m_k7[0] = mkkey(T0 + 2 * m_pe, 1.0f, PSYTL_EASE_LINEAR);
    m_k7[1] = mkkey(T0 + 10 * m_pe, 4.0f, PSYTL_EASE_QUAD_OUT);
    m_k7[2] = mkkey(T0 + 40 * m_pe, 2.0f, PSYTL_EASE_STEP);
    m_k7[3] = mkkey(T0 + 40 * m_pe, 6.0f, PSYTL_EASE_COSINE);
    m_k7[4] = mkkey(T0 + 80 * m_pe, 0.5f, PSYTL_EASE_LINEAR);
    for (i = 0; i < 64; i++) m_samp[i] = (float)((i * 29) % 64) / 8.0f - 3.0f;
    memset(m_drv, 0, sizeof m_drv);
    memset(m_pend, 0, sizeof m_pend);
    memset(m_has_drv, 0, sizeof m_has_drv);
    {
        psytl_track t6 = trk_keys(m_k6, 6), t7 = trk_keys(m_k7, 5);
        m_drv[6] = rdrv_plain(&t6);
        m_drv[7] = rdrv_plain(&t7);
    }
    m_has_drv[6] = m_has_drv[7] = 1;
    m_tb[6] = 1;
    m_tb[7] = 0;
    if (!open_h(&g_tl, g_store, M_CAP, M_NCH, m_init, (double)lead_num / (double)lead_den))
        return m_fail("open", 0, 1);
    /* open_h maps 0 to PSYTL_LEAD_NONE, whose lead in use is 0. */
    if (psytl_lead(&g_tl) != (double)lead_num / (double)lead_den)
        return m_fail("psytl_lead x 1000", (long long)(psytl_lead(&g_tl) * 1000.0), lead_num * 1000 / lead_den);
    if (psytl_set_keys(&g_tl, 6, 1, m_k6, 6) != 0) return m_fail("set_keys 6", 0, 1);
    if (psytl_set_keys(&g_tl, 7, 0, m_k7, 5) != 0) return m_fail("set_keys 7", 0, 1);
    if (m_check()) return 1;

    for (m_step = 0; m_step < steps; m_step++) {
        int r = rnd_int(ops ? 126 : 110), ret;
        int64_t now = m_has_onset ? m_onset : T0;
        /* Near full, mostly remove, so FULL does not dominate the adds. */
        if (m_n >= M_CAP - 6 && r < 40 && rnd_int(5) != 0) r = 45;
        if (r < 30) {
            psytl_event e = m_rand_event();
            m_op = "add";
            ret = psytl_add(&g_tl, &e);
            if (m_check_add(ret, &e, 1)) return 1;
        } else if (r < 40) {
            psytl_event es[4];
            int n = rnd_int(8) == 0 ? 0 : 1 + rnd_int(4);
            m_op = "add_n";
            for (i = 0; i < n; i++) es[i] = m_rand_event();
            ret = psytl_add_n(&g_tl, es, n);
            if (m_check_add(ret, es, n)) return 1;
        } else if (r < 55) {
            int id, idx = -1, want;
            m_op = "remove";
            if (m_n > 0 && rnd_int(10) != 0) id = m_ev[rnd_int(m_n)].e.id;
            else id = rnd_int(m_next_id + 3) - 1;
            for (i = 0; i < m_n; i++) if (m_ev[i].e.id == id) idx = i;
            want = idx >= 0 ? 0 : PSYTL_ERR_NOT_FOUND;
            ret = psytl_remove(&g_tl, id);
            if (ret != want) return m_fail("remove return", ret, want);
            if (idx >= 0) {
                memmove(&m_ev[idx], &m_ev[idx + 1], (size_t)(m_n - idx - 1) * sizeof m_ev[0]);
                m_n--;
            }
        } else if (r < 63) {
            int b = rnd_int(12) == 0 ? (rnd_int(2) ? 0 : PSYTL_MAX_BASES) : 1 + rnd_int(3);
            int64_t rt = now + rnd_range(-2 * m_pe, 3 * m_pe), bt;
            int q = rnd_int(4);
            m_op = "anchor";
            if (q == 0) bt = 0;
            else if (q == 1 && b > 0 && b < PSYTL_MAX_BASES && m_b[b].evaluated) bt = m_b[b].last_on + rnd_range(-3, 3) * (m_pe / 3);
            else if (q == 2 && b > 0 && b < PSYTL_MAX_BASES && m_b[b].state != 0) bt = m_bt(b, rt) + rnd_range(-20 * m_pe, 5 * m_pe);
            else bt = rnd_range(-5 * m_pe, 40 * m_pe);
            ret = psytl_anchor(&g_tl, b, rt, bt);
            if (b == 0 || b == PSYTL_MAX_BASES) {
                if (ret != PSYTL_ERR_ARG) return m_fail("anchor bad base", ret, PSYTL_ERR_ARG);
            } else {
                if (ret != 0) return m_fail("anchor return", ret, 0);
                /* "Furthest base time of an onset evaluated": read as
                 * since the last rewind, like the reach; a rewind sets
                 * both to just before bt. */
                if (m_b[b].evaluated && bt <= m_b[b].last_on) {
                    m_rewind(b, bt);
                    m_b[b].last_hi = bt - 1;
                    m_b[b].last_on = bt - 1;
                    m_stat_arew++;
                }
                m_b[b].state = 1;
                m_b[b].art = rt;
                m_b[b].abt = bt;
                m_b[b].anc_bt = bt;
                m_b[b].nowv = bt;
                m_b[b].moved++;
            }
            m_trace("anchor", b, rt, bt, ret);
        } else if (r < 68) {
            int b = rnd_int(15) == 0 ? 0 : 1 + rnd_int(3), want;
            int64_t rt = now + rnd_range(-m_pe, 2 * m_pe);
            m_op = "pause";
            want = b == 0 ? PSYTL_ERR_ARG : (m_b[b].state != 1 ? PSYTL_ERR_ORDER : 0);
            ret = psytl_pause(&g_tl, b, rt);
            if (ret != want) return m_fail("pause return", ret, want);
            if (want == 0) { m_b[b].frozen = m_bt(b, rt); m_b[b].state = 2; m_b[b].moved++;
                             m_b[b].nowv = m_b[b].frozen; }
            m_trace("pause", b, rt, m_b[b].frozen, ret);
        } else if (r >= 118) {
            if (m_op_peek(now)) return 1;
        } else if (r >= 110) {
            if (m_op_skip(now)) return 1;
        } else if (r >= 100) {
            /* Replace channel 6's or 7's driver: its keys again, a sampled
             * track with or without a repeat, a repeated keyed track with
             * a BEZIER segment, or a tween that waits for its start. */
            int tch = 6 + rnd_int(2), b = rnd_int(4), q = rnd_int(5);
            int64_t bnow = m_bt(b, now);
            psytl_track tr = trk_keys(m_k6, 6);
            if (q == 0) {
                tr = tch == 6 ? trk_keys(m_k6, 6) : trk_keys(m_k7, 5);
            } else if (q == 1) {
                static const int64_t mrates[4] = { 120, 7, 60, 999983 };
                tr = trk_samples(m_samp, 64, bnow + rnd_range(-20 * m_pe, 5 * m_pe), mrates[rnd_int(4)], rnd_int(3));
                if (rnd_int(2)) { tr.period = rnd_range(m_pe / 2, 30 * m_pe); tr.repeats = rnd_int(4); }
            } else if (q == 2) {
                int64_t st = bnow + rnd_range(-10 * m_pe, 5 * m_pe);
                m_krk[tch][0] = mkbez(st, 1.0f, rnd_int(2));
                m_krk[tch][1] = mkkey(st + rnd_range(1, 4 * m_pe), 6.0f, rnd_int(5));
                m_krk[tch][2] = mkkey(m_krk[tch][1].time + rnd_range(0, 4 * m_pe), -2.0f, PSYTL_EASE_LINEAR);
                tr = trk_keys(m_krk[tch], 3);
                tr.curves = m_curv;
                tr.n_curves = 2;
                tr.period = rnd_range(m_pe, 10 * m_pe);
                tr.repeats = rnd_int(5);
            }
            if (q < 3) {
                m_op = "set_track";
                ret = psytl_set_track(&g_tl, tch, b, &tr);
                if (ret != 0) return m_fail("set_track return", ret, 0);
                m_drv[tch] = rdrv_plain(&tr);
                m_has_drv[tch] = 1;
                m_pend[tch] = 0;   /* set_track cancels a waiting tween */
                m_tb[tch] = b;
            } else {
                static const int cyc[6] = { 0, 0, 1, 2, 3, PSYTL_FOREVER };
                psytl_tween_desc d;
                int okn = 0, want;
                int64_t bn = m_now(b, &okn);
                memset(&d, 0, sizeof d);
                d.to = (float)rnd_int(64) / 8.0f - 2.0f;
                d.duration = rnd_int(4) == 0 ? 0 : rnd_range(1, 15 * m_pe);
                d.ease = rnd_int(7);
                if (d.ease == PSYTL_EASE_BEZIER) {
                    d.curve = m_curv[rnd_int(2)];
                    if (rnd_int(10) == 0) d.curve.x2 = 1.5f;
                }
                d.from_set = rnd_int(3) == 0;
                d.from = (float)rnd_int(64) / 8.0f - 2.0f;
                if (d.ease == PSYTL_EASE_LOG && rnd_int(6) != 0) {
                    d.from_set = true;
                    d.from = (float)(1 + rnd_int(32)) / 8.0f;
                    d.to = (float)(1 + rnd_int(32)) / 8.0f;
                }
                d.cycles = cyc[rnd_int(6)];
                d.yoyo = rnd_int(4) == 0;
                if (rnd_int(3) == 0) {
                    d.keep_velocity = true;
                    if (rnd_int(5) != 0) {   /* mostly a valid one */
                        d.ease = PSYTL_EASE_LINEAR;
                        d.from_set = false;
                        d.yoyo = false;
                        d.cycles = rnd_int(2);
                        if (d.duration == 0) d.duration = rnd_range(1, 15 * m_pe);
                    }
                }
                d.start_set = !okn || rnd_int(2) == 0;
                d.start = bnow + rnd_range(-3 * m_pe, 6 * m_pe);
                if (rnd_int(3) == 0) d.delay = rnd_range(-2 * m_pe, 3 * m_pe);
                want = tw_valid(&d) ? 0 : PSYTL_ERR_ARG;
                m_op = "tween";
                ret = psytl_tween(&g_tl, tch, b, &d);
                if (ret != want) return m_fail("tween return", ret, want);
                if (ret == 0) {
                    if (m_has_drv[tch] && m_tb[tch] != b) {   /* another base: stopped at its value */
                        m_has_drv[tch] = 0;
                        m_stat_xbase++;
                    }
                    m_pend[tch] = 1;
                    m_pd[tch] = d;
                    m_ps[tch] = (d.start_set ? d.start : bn) + d.delay;
                    m_pcall[tch] = m_trackv[tch];
                    m_pstep[tch] = d.ease == PSYTL_EASE_STEP && !(g_mut & MUT_STEP_ONSET);
                    m_tb[tch] = b;
                    m_stat_tw++;
                    if (m_trace_seed == m_seed)
                        fprintf(stderr, "  [%d] tween ch%d b%d start %lld (set %d, now %lld, delay %lld) dur %lld "
                                "to %g from %g (set %d) ease %d cycles %d yoyo %d kv %d; value %g\n", m_step, tch, b,
                                (long long)m_ps[tch], (int)d.start_set, (long long)bn, (long long)d.delay,
                                (long long)d.duration, (double)d.to, (double)d.from, (int)d.from_set, d.ease,
                                d.cycles, (int)d.yoyo, (int)d.keep_velocity, (double)m_trackv[tch]);
                    if (d.keep_velocity) m_stat_kv++;
                    if (!d.start_set) m_stat_twnow++;
                } else {
                    m_stat_twrej++;
                }
            }
            m_stat_trk++;
            m_trace(m_op, b, tch, q, ret);
        } else if (r < 73) {
            int b = 1 + rnd_int(3), want;
            int64_t rt = now + rnd_range(0, 2 * m_pe);
            m_op = "resume";
            want = m_b[b].state != 2 ? PSYTL_ERR_ORDER : 0;
            ret = psytl_resume(&g_tl, b, rt);
            if (ret != want) return m_fail("resume return", ret, want);
            if (want == 0) { m_b[b].state = 1; m_b[b].art = rt; m_b[b].abt = m_b[b].frozen; m_b[b].moved++;
                             m_b[b].nowv = m_b[b].frozen; }
            m_trace("resume", b, rt, m_b[b].frozen, ret);
        } else {
            int64_t inc, onset, due = 0, mdue = 0;
            int cap = rnd_int(7), n, lim, has, mhas = 0;
            psytl_event* fp;
            m_op = "evaluate";
            if (m_has_onset && rnd_int(40) == 0) {
                ret = eval_at(&g_tl, m_onset - 1, m_period, m_index, g_fired, 4);
                if (ret != PSYTL_ERR_ORDER) return m_fail("evaluate backward onset", ret, PSYTL_ERR_ORDER);
                if (m_check()) return 1;
                continue;
            }
            if (m_period > 0) {
                int r2 = rnd_int(100);
                inc = r2 < 12 ? 0 : r2 < 85 ? m_pe : r2 < 95 ? 2 * m_pe : rnd_range(3 * m_pe, 12 * m_pe);
            } else {
                inc = rnd_int(8) == 0 ? 0 : rnd_range(MS_NS, 25 * MS_NS);
            }
            onset = m_has_onset ? m_onset + inc : T0;
            fp = (cap == 0 && rnd_int(2)) ? NULL : g_fired;
            for (i = 0; i < 16; i++) g_fired[i].id = -999;
            n = eval_at(&g_tl, onset, m_period, m_index, fp, cap);
            m_evaluate(onset, m_index);
            m_stat_eval++;
            for (i = 1; i < 4; i++) m_trace("eval", i, onset, m_bt(i, onset), n);
            m_onset = onset;
            m_has_onset = 1;
            if (n != m_nf) return m_fail("evaluate return", n, m_nf);
            lim = n < cap ? n : cap;
            for (i = 0; i < lim; i++) {
                const char* d = ev_diff(&g_fired[i], &m_fired[i]);
                if (d) {
                    print_ev("got ", &g_fired[i]);
                    print_ev("want", &m_fired[i].e);
                    return m_fail(d, i, i);
                }
            }
            if (fp && cap < 16 && g_fired[cap].id != -999) return m_fail("wrote past cap", cap, cap);
            m_index += 1 + rnd_int(2);
            has = psytl_next_due(&g_tl, &due);
            for (i = 0; i < m_n; i++) {
                const psytl_event* e = &m_ev[i].e;
                int64_t t;
                if (e->flags & M_DONE) continue;
                if (e->base != 0 && m_b[e->base].state != 1) continue;
                t = e->base == 0 ? e->time : m_b[e->base].art + (e->time - m_b[e->base].abt);
                if (!mhas || t < mdue) { mdue = t; mhas = 1; }
            }
            if (has != (mhas != 0)) return m_fail("next_due found", has, mhas);
            if (has && due != mdue) return m_fail("next_due time", due, mdue);
        }
        if (m_check()) return 1;
    }
    return 0;
}

static void test_model(void) {
    static const struct { uint64_t seed; int num, den; int64_t period; } cfg[] = {
        { 1, 0, 1, P60 }, { 2, 1, 2, P60 }, { 3, 1, 4, P60 }, { 4, 1, 2, 0 },
        { 5, 0, 1, 0 },   { 6, 1, 2, P60 }, { 7, 0, 1, P60 }, { 8, 3, 4, P60 },
        { 9, 0, 1, P60 }, { 10, 1, 2, P60 },
    };
    size_t i;
    const char* tr = getenv("TL_TRACE");
    m_trace_seed = tr ? (uint64_t)strtoull(tr, NULL, 10) : 0;
    for (i = 0; i < sizeof cfg / sizeof cfg[0]; i++)
        (void)model_run(cfg[i].seed, cfg[i].num, cfg[i].den, cfg[i].period, 800, 0);
    /* The same leads and periods with skip and peek among the ops; other
     * seeds, so the v0.2.0 runs above stay as they were. */
    for (i = 0; i < sizeof cfg / sizeof cfg[0]; i++)
        (void)model_run(cfg[i].seed + 100, cfg[i].num, cfg[i].den, cfg[i].period, 1000, 1);
}

/* QUANTIZATION: a desc lead of 0 is the default, 0.5; PSYTL_LEAD_NONE is
 * never early. An event a third of a period after frame 5's onset lands on
 * frame 5 (a third early) by default and on frame 6 under NONE. */
static void test_lead_default(void) {
    static psytl_event sa[4], sb[4];
    static psytl_timeline ta, tb;
    const int64_t P = P60;
    const double leads[3] = { 0.0, 0.5, PSYTL_LEAD_NONE };
    int64_t landed[3], resid[3];
    int li, k;
    for (li = 0; li < 3; li++) {
        psytl_desc d;
        psytl_event e;
        const psytl_event* r;
        memset(&d, 0, sizeof d);
        memset(sa, 0, sizeof sa);
        d.events = sa;
        d.event_capacity = 4;
        d.n_channels = 1;
        d.lead = leads[li];
        CHECK(psytl_open(&ta, &d));
        e = ev(0, 5 * P + P / 3, PSYTL_ONSET, 0, 0.0f);
        CHECK_I(psytl_add(&ta, &e), 0);
        for (k = 0; k <= 8; k++) (void)eval_at(&ta, k * P, P, k, NULL, 0);
        r = psytl_find(&ta, 0);
        CHECK(r != NULL);
        landed[li] = r ? r->frame : -99;
        resid[li] = r ? r->residual : -99;
    }
    CHECK_I(landed[0], 5);
    CHECK_I(resid[0], -(P / 3));
    CHECK_I(landed[1], 5);
    CHECK_I(landed[2], 6);
    CHECK_I(resid[2], P - P / 3);
    /* The default and an explicit 0.5 write the same bytes. */
    {
        psytl_desc d;
        psytl_event e;
        memset(&d, 0, sizeof d);
        memset(sa, 0, sizeof sa);
        memset(sb, 0, sizeof sb);
        d.n_channels = 1;
        d.event_capacity = 4;
        d.events = sa;
        CHECK(psytl_open(&ta, &d));
        d.events = sb;
        d.lead = 0.5;
        CHECK(psytl_open(&tb, &d));
        for (k = 0; k < 4; k++) {
            e = ev(0, k * P + (k + 1) * P / 5, PSYTL_MARK, 0, 0.0f);
            CHECK(psytl_add(&ta, &e) >= 0);
            CHECK(psytl_add(&tb, &e) >= 0);
        }
        for (k = 0; k <= 6; k++) {
            (void)eval_at(&ta, k * P, P, k, NULL, 0);
            (void)eval_at(&tb, k * P, P, k, NULL, 0);
        }
        CHECK(memcmp(sa, sb, sizeof sa) == 0);
    }
}

/* ------------------------------------------------------------------- main */

int main(void) {
    const char* mu = getenv("TL_MUT");
    g_mut = mu ? (unsigned)strtoul(mu, NULL, 10) : 0u;
    test_open();
    test_lead_default();
    test_misc();
    {
        /* 60, 144, 240, 360, 500 and 1000 Hz, periods rounded to ns. */
        static const int64_t periods[] = { 16666667, 6944444, 4166667, 2777778,
                                           2000000, 1000000 };
        int pi;
        for (pi = 0; pi < (int)(sizeof periods / sizeof periods[0]); pi++) {
            uint64_t sd = 11 + 10 * (uint64_t)pi;
            quant_grid(periods[pi], 0, 1, 0, sd);
            quant_grid(periods[pi], 1, 2, 0, sd + 1);
            quant_grid(periods[pi], 1, 4, 0, sd + 2);
            quant_grid(periods[pi], 0, 1, 3, sd + 3);
            quant_grid(periods[pi], 1, 2, 5, sd + 4);
        }
    }
    quant_vrr(16);
    test_bases();
    test_channels();
    test_sample();
    test_tracks();
    test_sampled();
    test_sampled_cubic();
    test_long_table();
    test_repeat();
    test_flicker();
    test_bezier();
    test_set_track();
    test_tween();
    test_tween_macros();
    test_tween_now();
    test_tween_from();
    test_tween_velocity();
    test_tween_cycles();
    test_tween_step();
    test_tween_retarget();
    test_tween_reject();
    test_tween_cancel();
    test_tween_cross_base();
    test_tween_rewind();
    test_track_eval();
    test_step_read();
    test_report();
    test_late();
    test_rewind();
    test_trial_twice();
    test_settled();
    test_remove();
    test_next_due();
    test_replay();
    test_lead();
    test_skip_seek();
    test_skip_back();
    test_skip_late_wait();
    test_skip_misc();
    test_skip_tracks();
    test_peek();
    test_peek_lookahead();
    test_model();
    if (m_stat_promo < 100 || m_stat_kv < 20 || m_stat_xbase < 20 || m_stat_twnow < 50 || m_stat_twrej < 20)
        fail(__LINE__, "model: too few tween takeovers, keep_velocity, cross-base, now or rejected tweens (vacuous)");
    if (m_stat_skip < 300 || m_stat_skipped < 300 || m_stat_skipback < 50 || m_stat_skiplate < 20
        || m_stat_peek < 300 || m_stat_peeked < 300 || m_stat_peektrunc < 50)
        fail(__LINE__, "model: too few skips, skipped events, skips back, late skips or peeks (vacuous)");
    fprintf(g_failures ? stderr : stdout,
            "psy_timeline_test: %s (model: %ld evaluates, %ld fired, %ld late, "
            "%ld anchor rewinds, %ld windows behind reach, %ld un-fired, %ld rejected adds, %ld late waits, "
            "%ld track ops; tweens %ld accepted, %ld rejected, %ld took over, %ld keep_velocity, "
            "%ld cross-base, %ld from now; %ld skips (%ld back) skipped %ld events (%ld late); "
            "%ld peeks listed %ld events, %ld over the cap)\n",
            g_failures ? "FAILED" : "all checks passed",
            m_stat_eval, m_stat_fired, m_stat_late, m_stat_arew, m_stat_wrew, m_stat_unfired, m_stat_err, m_stat_wait,
            m_stat_trk, m_stat_tw, m_stat_twrej, m_stat_promo, m_stat_kv, m_stat_xbase, m_stat_twnow,
            m_stat_skip, m_stat_skipback, m_stat_skipped, m_stat_skiplate, m_stat_peek, m_stat_peeked,
            m_stat_peektrunc);
    if (g_failures) {
        fprintf(stderr, "psy_timeline_test: %d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
