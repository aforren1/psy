/* device_test.c - self-checking test for ysp/device.h. No framework: it
 * returns 0 when every check passed and 1 after printing each failure.
 *
 * A simulated board (a ysp line protocol board, a Cedrus XID box, a
 * photodiode frame board) sits behind a fake transport. Its clock runs at a
 * stated rate from a stated origin; it sends syncs and the edges the test
 * schedules, answers timer and identity queries, and hands its bytes to the
 * host after a delay drawn from a model of the USB path (a native-USB
 * microframe and a tail, or an FTDI latency timer). Most of the test runs
 * in manual mode on a virtual clock: a read that finds nothing advances the
 * clock by its timeout, so a minute of device time takes milliseconds and
 * every run is the same. One part runs the reader thread on the real
 * clock. The reference for each event is the true host time of its edge.
 *
 *     gcc -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Werror -Iinclude \
 *         -o device_test tests/adapt/device_test.c -lsetupapi   (Windows)
 */
#define YSP_DEVICE_IMPLEMENTATION
#include "ysp/device.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond) do { g_checks++; if (!(cond)) { \
    fprintf(stderr, "device_test: FAIL at line %d: %s\n", __LINE__, #cond); g_failures++; } } while (0)
#define CHECK_I(got, want) do { long long g_ = (long long)(got), w_ = (long long)(want); g_checks++; \
    if (g_ != w_) { fprintf(stderr, "device_test: FAIL at line %d: %s (got %lld, want %lld)\n", \
                            __LINE__, #got, g_, w_); g_failures++; } } while (0)

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static double u01(void) {
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17;
    return ((double)(g_rng >> 11) + 1.0) / 9007199254740992.0;
}

/* --- the virtual clock ------------------------------------------------------ */

static int64_t g_now = 1000000000000LL;    /* the host clock reads 1000 s */
static bool    g_real;                     /* the thread test: the real clock */
static int64_t now_fn(void* ctx) { (void)ctx; return g_real ? (int64_t)yrt_now_ns() : g_now; }

/* --- the simulated board ---------------------------------------------------- */

#define QBYTES (1 << 16)

typedef struct board {
    int      family;
    /* its clock: ticks = (host - t0) / tick, from tick u0 */
    int64_t  t0;
    double   tick;            /* true ns per tick                         */
    uint64_t u0;
    /* the path to the host */
    double   frame_ns;        /* phase of a frame or latency timer        */
    double   tail_ns;         /* exponential tail                         */
    /* bytes to the host: when each becomes readable */
    uint8_t  q[QBYTES];
    int64_t  qt[QBYTES];
    uint32_t qh, qtail;
    /* what it sends */
    int64_t  next_sync;       /* LINE and PHOTO: every 100 ms             */
    int64_t  edges[4096];     /* host times of edges the test scheduled  */
    int      n_edges, next_edge;
    int      level;
    /* host to board */
    char     line[64];
    int      nline;
    /* faults */
    int64_t  gone_from, gone_until;   /* unplugged: open and read fail     */
    int      future_edge;             /* this edge is stamped 60 ms ahead  */
    int64_t  mute_from, mute_until;   /* plugged, but it sends nothing     */
    bool     no_identity;             /* never answers "i" or "_c1"       */
    bool     reset_on_return;         /* its clock restarts after an unplug */
    bool     open;
    int      opens;
    int64_t  generated_until;
} board;

static board g_b;

static uint64_t b_ticks(const board* b, int64_t host) {
    return b->u0 + (uint64_t)((double)(host - b->t0) / b->tick);
}

static bool b_muted(const board* b, int64_t t) { return t >= b->mute_from && t < b->mute_until; }
static bool b_gone(const board* b, int64_t t) { return t >= b->gone_from && t < b->gone_until; }

/* Queue bytes made at host time t; they arrive after the path's delay, in
 * order (a later chunk never overtakes an earlier one). */
static void b_send(board* b, int64_t t, const void* bytes, int n) {
    int64_t at = t + (int64_t)(u01() * b->frame_ns + -b->tail_ns * log(u01()));
    int i;
    if (b->qtail != b->qh && at < b->qt[(b->qtail - 1) % QBYTES]) at = b->qt[(b->qtail - 1) % QBYTES];
    for (i = 0; i < n; i++) {
        b->q[b->qtail % QBYTES] = ((const uint8_t*)bytes)[i];
        b->qt[b->qtail % QBYTES] = at;
        b->qtail++;
    }
}

static void b_le32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static void b_edge(board* b, int64_t t) {
    uint64_t u = b_ticks(b, t);
    if (b->future_edge && b->next_edge == b->future_edge) u += (uint64_t)(60e6 / b->tick);
    b->level ^= 1;
    if (b->family == YBOX_LINE) {
        char s[64];
        int n = snprintf(s, sizeof s, "E %lu 1 %d\n", (unsigned long)(uint32_t)u, b->level);
        b_send(b, t, s, n);
    } else if (b->family == YBOX_XID) {
        uint8_t f[6] = { 'k', 0, 0, 0, 0, 0 };
        f[1] = (uint8_t)((1u << 5) | (b->level ? 0x10u : 0u));    /* key 1, port 0 */
        b_le32(f + 2, (uint32_t)u);
        b_send(b, t, f, 6);
    } else {
        uint8_t f[6] = { 0xA5, 0, 0, 0, 0, 0 };
        f[1] = (uint8_t)b->level;
        b_le32(f + 2, (uint32_t)u);
        b_send(b, t, f, 6);
    }
}

static void b_sync(board* b, int64_t t) {
    uint64_t u = b_ticks(b, t);
    if (b->family == YBOX_LINE) {
        char s[40];
        int n = snprintf(s, sizeof s, "S %lu\n", (unsigned long)(uint32_t)u);
        b_send(b, t, s, n);
    } else if (b->family == YBOX_PHOTO) {
        uint8_t f[6] = { 0x5A, 0, 0, 0, 0, 0 };
        b_le32(f + 2, (uint32_t)u);
        b_send(b, t, f, 6);
    }
}

/* Everything the board makes up to host time t. */
static void b_run(board* b, int64_t t) {
    for (;;) {
        int64_t te = b->next_edge < b->n_edges ? b->edges[b->next_edge] : INT64_MAX;
        int64_t ts = b->family == YBOX_XID ? INT64_MAX : b->next_sync;
        int64_t tn = te < ts ? te : ts;
        if (tn > t) break;
        if (tn == te) {
            if (!b_muted(b, te) && !b_gone(b, te) && b->open) b_edge(b, te);
            b->next_edge++;
        } else {
            if (!b_muted(b, ts) && !b_gone(b, ts) && b->open) b_sync(b, ts);
            b->next_sync += 100000000;
        }
    }
    if (t > b->generated_until) b->generated_until = t;
}

/* A byte from the host, at host time t (it reaches the board 0.2 ms later). */
static void b_hear(board* b, uint8_t c, int64_t t) {
    int64_t at = t + 200000;
    if (b_muted(b, at)) return;
    if (b->family == YBOX_LINE) {
        if (c != '\n') { if (b->nline < 63) b->line[b->nline++] = (char)c; return; }
        b->line[b->nline] = '\0';
        b->nline = 0;
        if (b->line[0] == 'q') {
            char s[64];
            int n = snprintf(s, sizeof s, "Q %s %lu\n", b->line + 2, (unsigned long)(uint32_t)b_ticks(b, at));
            b_send(b, at, s, n);
        } else if (b->line[0] == 'i' && !b->no_identity) {
            b_send(b, at, "I ysp-line 1 sim test\n", 22);
        }
        return;
    }
    if (b->family == YBOX_XID) {
        if (b->nline < 63) b->line[b->nline++] = (char)c;
        b->line[b->nline] = '\0';
        if (b->nline < 3) return;
        if (memcmp(b->line, "_e5", 3) == 0) {
            uint8_t f[7] = { '_', 'e', '5', 0, 0, 0, 0 };
            b_le32(f + 3, (uint32_t)b_ticks(b, at));
            b_send(b, at, f, 7);
        } else if (memcmp(b->line, "_c1", 3) == 0 && !b->no_identity) {
            b_send(b, at, "_xid0", 5);
        } else if (memcmp(b->line, "_d2", 3) == 0) {
            b_send(b, at, "2", 1);
        } else if (memcmp(b->line, "_d3", 3) == 0) {
            b_send(b, at, "1", 1);
        } else if (memcmp(b->line, "_d4", 3) == 0) {
            b_send(b, at, "2", 1);
        }
        b->nline = 0;
    }
}

/* --- the fake transport ----------------------------------------------------- */

static void* t_open(void* user, const char* key, char* found, size_t cap) {
    board* b = (board*)user;
    int64_t t = now_fn(NULL);
    if (b_gone(b, t) || !ydev_key_match(key, "serial:16C0:0483:SIM1:1-1")) return NULL;
    if (b->opens > 0 && b->reset_on_return) { b->t0 = t; b->u0 = 0; }
    b->open = true;
    b->opens++;
    b->qh = b->qtail;                 /* a fresh port: nothing waiting */
    b->next_sync = t + 50000000;
    while (b->next_edge < b->n_edges && b->edges[b->next_edge] < t) b->next_edge++;
    snprintf(found, cap, "SIM1");
    return b;
}

static int t_read(void* conn, uint8_t* buf, int cap, int timeout_ms) {
    board* b = (board*)conn;
    int64_t t = now_fn(NULL), end = t + (int64_t)timeout_ms * 1000000;
    int n = 0;
    if (b_gone(b, t)) { b->open = false; return -1; }
    b_run(b, g_real ? t : end);
    /* the first chunk readable by end: the clock jumps to it */
    if (b->qh != b->qtail && b->qt[b->qh % QBYTES] <= end) {
        int64_t at = b->qt[b->qh % QBYTES];
        if (b_gone(b, at)) { if (!g_real) g_now = b->gone_from; b->open = false; return -1; }
        if (!g_real && at > g_now) g_now = at;
        while (n < cap && b->qh != b->qtail && b->qt[b->qh % QBYTES] <= (g_real ? t : g_now))
            buf[n++] = b->q[b->qh++ % QBYTES];
        if (n > 0) return n;
    }
    if (!g_real) {
        if (b_gone(b, end)) { g_now = b->gone_from > g_now ? b->gone_from : g_now; b->open = false; return -1; }
        g_now = end;
    } else {
        (void)yrt_sleep_ns(1000000);
    }
    return 0;
}

static int t_write(void* conn, const uint8_t* buf, int n) {
    board* b = (board*)conn;
    int i;
    int64_t t = now_fn(NULL);
    if (b_gone(b, t)) return -1;
    b_run(b, t);
    for (i = 0; i < n; i++) b_hear(b, buf[i], t);
    return n;
}

static void t_close(void* conn) { ((board*)conn)->open = false; }

static ydev_transport fake(void) {
    ydev_transport tr;
    memset(&tr, 0, sizeof tr);
    tr.open = t_open;
    tr.read = t_read;
    tr.write = t_write;
    tr.close = t_close;
    tr.user = &g_b;
    return tr;
}

/* --- the sink and the log ------------------------------------------------------ */

static yin_event g_ev[8192];
static int       g_nev;
static void sink(void* ctx, const yin_event* e) { (void)ctx; if (g_nev < 8192) g_ev[g_nev++] = *e; }

static unsigned char g_ring_mem[YRT_RING_BYTES(16384)];
static yrt_ring      g_ring;
static yrt_event     g_rec[16384];

static void ring_reset(void) {
    yrt_ring_desc rd;
    memset(&rd, 0, sizeof rd);
    rd.memory = g_ring_mem;
    rd.bytes = sizeof g_ring_mem;
    memset(&g_ring, 0, sizeof g_ring);
    CHECK(yrt_ring_open(&g_ring, &rd));
}

static ydev_device g_dev;

static void board_init(int family, double ppm, double frame_ns, double tail_ns) {
    memset(&g_b, 0, sizeof g_b);
    g_b.family = family;
    g_b.tick = (family == YBOX_XID ? 1e6 : 1e3) * (1.0 + ppm * 1e-6);
    g_b.t0 = g_now - 123456789;
    g_b.u0 = family == YBOX_XID ? 77777 : 4294000000u;   /* LINE wraps soon */
    g_b.frame_ns = frame_ns;
    g_b.tail_ns = tail_ns;
    g_b.gone_from = g_b.gone_until = INT64_MAX;
    g_b.mute_from = g_b.mute_until = INT64_MAX;
}

static void schedule_edges(int64_t from, int64_t to, int64_t period, int64_t jitter) {
    int64_t t = from;
    while (t < to && g_b.n_edges < 4096) {
        g_b.edges[g_b.n_edges++] = t;
        t += period + (int64_t)(u01() * (double)jitter);
    }
}

static ydev_desc desc_for(int family) {
    ydev_desc d;
    memset(&d, 0, sizeof d);
    d.role = family == YBOX_XID ? "resp" : "photo";
    d.family = family;
    d.key = "serial:16C0:0483::";
    d.device = 3;
    d.kind = family == YBOX_XID ? 0 : YIN_KIND_SYNC;
    d.sink = sink;
    d.transport = fake();
    d.ring = &g_ring;
    d.manual = true;
    d.now = now_fn;
    return d;
}

/* The caller's loop: a poll every 20 ms. A poll that reads moves the
 * virtual clock itself; one that does not (no device) is the caller's
 * frame passing. */
static void run_until(int64_t t) {
    while (g_now < t) {
        int64_t before = g_now;
        (void)ydev_poll(&g_dev, 20);
        if (g_now == before) g_now += 20000000;
    }
}

/* Map each delivered event back to its edge: events come in edge order, so
 * the k-th event after the board opened is the k-th edge sent. Returns the
 * extremes of t minus truth for events after `from`. */
static void event_errors(int first_ev, int first_edge, int64_t from, double* lo, double* hi, int* n) {
    int i, k = first_edge;
    *lo = 1e300; *hi = -1e300; *n = 0;
    for (i = first_ev; i < g_nev && k < g_b.n_edges; i++, k++) {
        double e = (double)g_ev[i].t - (double)g_b.edges[k];
        if (g_b.edges[k] < from) continue;
        if (e < *lo) *lo = e;
        if (e > *hi) *hi = e;
        (*n)++;
    }
}

/* --- the cases ---------------------------------------------------------------------- */

/* A line-protocol photodiode: edges every 250 ms for 60 s, native USB (a
 * 125 us microframe and a 50 us tail), 30 ppm fast, a counter that wraps;
 * unplugged at 30 s for 3 s, its clock restarted when it comes back. */
static void test_line_unplug(void) {
    ydev_stats st;
    int64_t t_start = g_now, t_unplug, i, n_ev_before;
    double lo, hi;
    int n, k, recs, states[16], ns = 0, gaps = 0, texts = 0, fits = 0;
    int64_t gap_first = 0, gap_last = 0;
    ring_reset();
    g_nev = 0;
    board_init(YBOX_LINE, 30.0, 125000.0, 50000.0);
    schedule_edges(t_start + 100000000, t_start + 60000000000LL, 250000000, 20000000);
    t_unplug = t_start + 30000000000LL;
    g_b.gone_from = t_unplug;
    g_b.gone_until = t_unplug + 3000000000LL;
    g_b.reset_on_return = true;
    {
        ydev_desc d = desc_for(YBOX_LINE);
        CHECK(ydev_start(&g_dev, &d));
    }
    CHECK_I(ydev_poll(&g_dev, 20), YDEV_RUNNING);
    ydev_get_stats(&g_dev, &st);
    CHECK(strcmp(st.ident, "ysp-line 1 sim test") == 0);
    CHECK(strcmp(st.found, "SIM1") == 0);
    run_until(t_unplug - 1);
    n_ev_before = g_nev;
    event_errors(0, 0, t_start + 10000000000LL, &lo, &hi, &n);
    printf("  line, 10 to 30 s: %d edges, event t minus edge %+.1f .. %+.1f us\n", n, lo / 1e3, hi / 1e3);
    CHECK(n > 70 && lo > -100e3 && hi < 300e3);
    run_until(t_unplug + 100000000);
    CHECK_I(ydev_state(&g_dev), YDEV_LOST);
    run_until(t_unplug + 5000000000LL);
    CHECK_I(ydev_state(&g_dev), YDEV_RUNNING);
    run_until(t_start + 60000000000LL);
    ydev_get_stats(&g_dev, &st);
    /* the events after the return map through the new fit */
    for (k = 0; k < g_b.n_edges && g_b.edges[k] < t_unplug + 3000000000LL; k++) { }
    {
        /* edges sent after reopening: the first edge at or after the reopen */
        int first_edge = 0;
        for (i = 0; i < g_b.n_edges; i++) if (g_b.edges[i] >= t_unplug + 3000000000LL) { first_edge = (int)i; break; }
        /* the reopen happened at the first retry after the board returned */
        while (first_edge < g_b.n_edges && g_ev[n_ev_before].ticks != 0 &&
               fabs((double)g_ev[n_ev_before].t - (double)g_b.edges[first_edge]) > 50e6) first_edge++;
        event_errors((int)n_ev_before, first_edge, t_unplug + 13000000000LL, &lo, &hi, &n);
        printf("  line after the unplug and a clock reset, 43 to 60 s: %d edges, %+.1f .. %+.1f us\n", n, lo / 1e3,
               hi / 1e3);
        CHECK(n > 50 && lo > -100e3 && hi < 300e3);
    }
    CHECK_I(st.opens, 2);
    CHECK(st.probes > 500 && st.answers > 0.95 * (double)st.probes);
    CHECK_I(st.clamped, 0);
    CHECK(st.fit.ready && st.fit.slope && fabs(st.fit.ppm - 30.0) < 3.0);
    CHECK(st.fit.width_ns > 0 && st.fit.width_ns <= 1300000);
    printf("  line: %llu probes, %llu answers, %llu pairs, refused %llu, fit %+.2f ppm, spread %.1f us, width %.1f us\n",
           (unsigned long long)st.probes, (unsigned long long)st.answers, (unsigned long long)st.pairs,
           (unsigned long long)st.refused, st.fit.ppm, (double)st.fit.spread_ns / 1e3, (double)st.fit.width_ns / 1e3);
    ydev_stop(&g_dev);
    CHECK_I(ydev_state(&g_dev), YDEV_CLOSED);
    /* the log */
    recs = yrt_ring_drain(&g_ring, g_rec, 16384);
    for (i = 0; i < recs; i++) {
        const yrt_event* r = &g_rec[i];
        if (r->source == YRT_SRC_DEVICE) {
            CHECK_I(r->aux, 3);
            if (r->kind == YDEV_REC_STATE && ns < 16) states[ns++] = (int)r->u.u32[1];
            if (r->kind == YDEV_REC_GAP) { gaps++; gap_first = r->u.i64[0]; gap_last = r->u.i64[1]; }
            if (r->kind == YDEV_REC_TEXT) texts++;
        } else if (r->source == YRT_SRC_RT && r->kind == YRT_KIND_FIT) {
            CHECK_I(r->aux, 3);
            fits++;
        }
    }
    CHECK(ns == 5 && states[0] == YDEV_OPENING && states[1] == YDEV_RUNNING && states[2] == YDEV_LOST &&
          states[3] == YDEV_RUNNING && states[4] == YDEV_CLOSED);
    CHECK_I(gaps, 1);
    CHECK(gap_first <= t_unplug && gap_first > t_unplug - 200000000);
    CHECK(gap_last >= t_unplug + 3000000000LL && gap_last < t_unplug + 4100000000LL);
    CHECK(texts >= 5);    /* role, port and ident twice */
    CHECK(fits > 100);
    printf("  line: states opening, running, lost, running, closed; gap %.3f s; %d texts, %d fit records\n",
           (double)(gap_last - gap_first) / 1e9, texts, fits);
}

/* A Cedrus XID box: presses every 2 to 4 s behind a 16 ms FTDI latency
 * timer, 50 ppm slow, timer queries every 100 ms. */
static void test_xid(void) {
    ydev_stats st;
    int64_t t_start = g_now;
    double lo, hi, lo_early, hi_early;
    int n, n_early;
    ring_reset();
    g_nev = 0;
    board_init(YBOX_XID, -50.0, 16000000.0, 100000.0);
    schedule_edges(t_start + 500000000, t_start + 300000000000LL, 2000000000, 2000000000);
    {
        ydev_desc d = desc_for(YBOX_XID);
        CHECK(ydev_start(&g_dev, &d));
    }
    CHECK_I(ydev_poll(&g_dev, 20), YDEV_RUNNING);
    run_until(t_start + 300000000000LL);
    ydev_get_stats(&g_dev, &st);
    CHECK(strcmp(st.ident, "xid product 2 model 1 firmware 2") == 0);
    event_errors(0, 0, t_start + 10000000000LL, &lo_early, &hi_early, &n_early);
    event_errors(0, 0, t_start + 120000000000LL, &lo, &hi, &n);
    printf("  xid, presses every 2 to 4 s, 16 ms latency timer: from 10 s %+.1f .. %+.1f us (%d), from 120 s "
           "%+.1f .. %+.1f us (%d)\n", lo_early / 1e3, hi_early / 1e3, n_early, lo / 1e3, hi / 1e3, n);
    printf("  xid: %llu probes, %llu answers, refused %llu, fit %+.2f ppm, spread %.1f us, width %.1f us\n",
           (unsigned long long)st.probes, (unsigned long long)st.answers, (unsigned long long)st.refused,
           st.fit.ppm, (double)st.fit.spread_ns / 1e3, (double)st.fit.width_ns / 1e3);
    /* the box's ms timer: an event is in [tick, tick + 1 ms) */
    CHECK(n > 30 && lo > -1500e3 && hi < 1500e3);
    /* unc_us: the 1 ms tick plus the bracket the map rests on, near the
     * measured error and far from the 15 ms spread of the answers */
    CHECK(g_nev > 0 && g_ev[g_nev - 1].unc_us >= 1000 && g_ev[g_nev - 1].unc_us <= 5000);   /* 1428 measured */
    printf("  xid: the last event's unc_us %u\n", (unsigned)g_ev[g_nev - 1].unc_us);
    CHECK(g_nev > 0 && g_ev[0].kind == YIN_KIND_BOX && g_ev[0].control == 1 && g_ev[0].device == 3);
    CHECK(st.answers > 2000);
    ydev_stop(&g_dev);
}

/* The photodiode frame: no queries, syncs every 100 ms. */
static void test_photo(void) {
    ydev_stats st;
    int64_t t_start = g_now;
    double lo, hi;
    int n;
    ring_reset();
    g_nev = 0;
    board_init(YBOX_PHOTO, 10.0, 1000000.0, 100000.0);
    schedule_edges(t_start + 100000000, t_start + 60000000000LL, 100000000, 30000000);
    {
        ydev_desc d = desc_for(YBOX_PHOTO);
        d.kind = 0;   /* the family's: SYNC */
        CHECK(ydev_start(&g_dev, &d));
    }
    run_until(t_start + 60000000000LL);
    ydev_get_stats(&g_dev, &st);
    event_errors(0, 0, t_start + 10000000000LL, &lo, &hi, &n);
    printf("  photo frame, 1 ms USB frame: %d edges, %+.1f .. %+.1f us; %llu probes\n", n, lo / 1e3, hi / 1e3,
           (unsigned long long)st.probes);
    CHECK(n > 300 && lo > -150e3 && hi < 400e3);
    CHECK_I(st.probes, 0);
    CHECK(g_nev > 0 && g_ev[0].kind == YIN_KIND_SYNC && g_ev[0].unc_us > 0 && (g_ev[0].flags & YIN_DEVTICKS));
    ydev_stop(&g_dev);
}

/* An edge stamped 60 ms past its arrival (a board bug): its pair is held
 * as an outlier and its map would pass the read time, so it is clamped to
 * the read time and counted. */
static void test_clamp(void) {
    int64_t t_start = g_now;
    ydev_stats st;
    int i, found = 0;
    ring_reset();
    g_nev = 0;
    board_init(YBOX_LINE, 0.0, 125000.0, 50000.0);
    schedule_edges(t_start + 100000000, t_start + 20000000000LL, 250000000, 0);
    g_b.future_edge = 60;
    {
        ydev_desc d = desc_for(YBOX_LINE);
        CHECK(ydev_start(&g_dev, &d));
    }
    run_until(t_start + 20000000000LL);
    ydev_get_stats(&g_dev, &st);
    CHECK_I(st.clamped, 1);
    for (i = 0; i < g_nev; i++)
        if (g_ev[i].t > g_b.edges[60] + 30000000 && g_ev[i].t < g_b.edges[60] + 100000000) found++;
    CHECK_I(found, 0);   /* no event mapped into the future */
    ydev_stop(&g_dev);
    printf("  an edge stamped 60 ms ahead: clamped to its read time, counted: ok\n");
}

/* Plugged in but silent for 3 s: LOST after 2 s, then found again. */
static void test_silence(void) {
    int64_t t_start = g_now;
    ydev_stats st;
    ring_reset();
    g_nev = 0;
    board_init(YBOX_LINE, 0.0, 125000.0, 50000.0);
    g_b.mute_from = t_start + 10000000000LL;
    g_b.mute_until = t_start + 13000000000LL;
    {
        ydev_desc d = desc_for(YBOX_LINE);
        CHECK(ydev_start(&g_dev, &d));
    }
    run_until(t_start + 12500000000LL);
    CHECK_I(ydev_state(&g_dev), YDEV_LOST);
    run_until(t_start + 20000000000LL);
    CHECK_I(ydev_state(&g_dev), YDEV_RUNNING);
    ydev_get_stats(&g_dev, &st);
    CHECK_I(st.opens, 2);
    ydev_stop(&g_dev);
    printf("  silence: lost after 2 s, running again: ok\n");
}

/* The port at the key answers nothing: FAILED, no retry. */
static void test_wrong_device(void) {
    int64_t t_start = g_now;
    ydev_stats st;
    ring_reset();
    board_init(YBOX_LINE, 0.0, 125000.0, 50000.0);
    g_b.no_identity = true;
    {
        ydev_desc d = desc_for(YBOX_LINE);
        CHECK(ydev_start(&g_dev, &d));
    }
    CHECK_I(ydev_poll(&g_dev, 20), YDEV_FAILED);
    CHECK(g_now - t_start >= 1500000000LL && g_now - t_start < 2000000000LL);
    run_until(g_now + 5000000000LL);
    ydev_get_stats(&g_dev, &st);
    CHECK_I(st.state, YDEV_FAILED);
    CHECK_I(g_b.opens, 1);
    CHECK(strstr(ydev_error(&g_dev), "did not answer") != NULL);
    ydev_stop(&g_dev);
    printf("  wrong device: failed after 1.5 s, not retried: ok\n");
}

/* The reader thread on the real clock for 1 s. */
static void test_thread(void) {
    ydev_stats st;
    int64_t t0;
    g_real = true;
    t0 = (int64_t)yrt_now_ns();
    ring_reset();
    g_nev = 0;
    board_init(YBOX_LINE, 20.0, 125000.0, 50000.0);
    g_b.t0 = t0 - 1000000000;
    schedule_edges(t0 + 100000000, t0 + 1100000000, 50000000, 0);
    {
        ydev_desc d = desc_for(YBOX_LINE);
        d.manual = false;
        d.now = NULL;          /* the reader thread's own clock */
        d.no_elevate = true;   /* a test thread need not be elevated */
        CHECK(ydev_start(&g_dev, &d));
    }
    (void)yrt_sleep_ns(1200000000);
    ydev_stop(&g_dev);         /* joins: g_ev is ours again */
    ydev_get_stats(&g_dev, &st);
    printf("  thread: %d events in 1.2 s, %llu probes, state %s after stop\n", g_nev,
           (unsigned long long)st.probes, ydev_state_name(st.state));
    CHECK(g_nev >= 15);
    CHECK(st.probes >= 8);
    CHECK_I(st.state, YDEV_CLOSED);
    g_real = false;
}

static void test_keys(void) {
    yser_port_info p;
    char k[160];
    memset(&p, 0, sizeof p);
    p.vid = 0x16C0; p.pid = 0x0483;
    snprintf(p.serial_number, sizeof p.serial_number, "1234560");
    snprintf(p.location, sizeof p.location, "Port_#0003.Hub_#0001");
    CHECK(ydev_port_key(&p, k, sizeof k) > 0 && strcmp(k, "serial:16C0:0483:1234560:Port_#0003.Hub_#0001") == 0);
    CHECK(ydev_port_key(&p, k, 10) < 0 && k[0] == '\0');
    CHECK(ydev_key_match("serial:16C0:0483::", "serial:16C0:0483:1234560:Port_#0003.Hub_#0001"));
    CHECK(ydev_key_match("serial:16c0", "serial:16C0:0483:1234560:x"));
    CHECK(ydev_key_match("serial:::1234560", "serial:16C0:0483:1234560:x"));
    CHECK(!ydev_key_match("serial:::1234561", "serial:16C0:0483:1234560:x"));
    CHECK(!ydev_key_match("serial:0403", "serial:16C0:0483:1234560:x"));
    CHECK(!ydev_key_match("serial:16C0:0483:1234560:Port_#0004", "serial:16C0:0483:1234560:Port_#0003"));
    CHECK(ydev_key_match("port:COM5", "port:COM5"));
    CHECK(!ydev_key_match("port:COM5", "port:COM50"));
    CHECK(!ydev_key_match("port:COM5", "serial:16C0:0483::"));
    CHECK(!ydev_key_match("usb:1", "usb:1"));
    CHECK(!ydev_key_match(NULL, "port:x"));
    printf("  match keys: ok\n");
}

static void test_desc(void) {
    ydev_desc d = desc_for(YBOX_LINE);
    ydev_desc e;
    e = d; e.role = NULL;      CHECK(!ydev_start(&g_dev, &e) && strstr(ydev_error(&g_dev), "role"));
    e = d; e.family = 9;       CHECK(!ydev_start(&g_dev, &e) && strstr(ydev_error(&g_dev), "family"));
    e = d; e.key = "";         CHECK(!ydev_start(&g_dev, &e) && strstr(ydev_error(&g_dev), "key"));
    e = d; e.device = 0;       CHECK(!ydev_start(&g_dev, &e) && strstr(ydev_error(&g_dev), "device"));
    e = d; e.kind = 16;        CHECK(!ydev_start(&g_dev, &e) && strstr(ydev_error(&g_dev), "kind"));
    e = d; e.transport.read = NULL; CHECK(!ydev_start(&g_dev, &e) && strstr(ydev_error(&g_dev), "transport"));
    e = d; memset(&e.transport, 0, sizeof e.transport); e.key = "usb:1";
    CHECK(!ydev_start(&g_dev, &e) && strstr(ydev_error(&g_dev), "serial:"));
    ydev_stop(&g_dev);         /* a stop after a failed start is harmless */
    {
        yin_source s;
        board_init(YBOX_LINE, 0.0, 125000.0, 50000.0);
        CHECK(ydev_start(&g_dev, &d));
        s = ydev_source(&g_dev);
        CHECK(s.kind == YIN_KIND_SYNC && s.device == 3 && s.tier == YIN_TIER_UNKNOWN && s.note != NULL);
        ydev_stop(&g_dev);
        ydev_stop(&g_dev);     /* twice */
    }
    printf("  desc checks and the source entry: ok\n");
}

int main(void) {
    printf("device_test: ysp/device.h %s, %d-byte instance\n", ydev_version(), (int)sizeof(ydev_device));
    test_keys();
    test_desc();
    test_line_unplug();
    test_xid();
    test_photo();
    test_clamp();
    test_silence();
    test_wrong_device();
    test_thread();
    if (g_failures) {
        fprintf(stderr, "device_test: %d of %d checks FAILED\n", g_failures, g_checks);
        return 1;
    }
    printf("device_test: all %d checks passed\n", g_checks);
    return 0;
}
