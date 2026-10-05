/* Compile check: psy_rt.h as a C translation unit with the implementation
 * enabled. CMake and CI build this as C11 with warnings as errors, once with
 * threading and once with PSYRT_NO_THREADS (which drops the deadline worker
 * and the pump); psy_rt.cpp builds the same source as C++17. With
 * -DPSY_TRACY_CHECK=ON, CMake also builds it with PSYRT_TRACY against a
 * fetched Tracy. It runs and returns 0 to prove it links.
 *
 * The event ring, the clock correlation and the instrumentation macros are
 * outside the thread guard: they exist in every build. In the default mode
 * the check also proves that no macro evaluates its arguments. */
#define PSY_RT_IMPLEMENTATION
#include "psy_rt.h"

#include <string.h>

#ifndef PSYRT_NO_THREADS
/* Everything below names a pump declaration, so the compile check covers the
 * pump's prototypes and not just the header's syntax, and the #ifndef is the
 * check that PSYRT_NO_THREADS really removes them: this block must fail to
 * compile without threads, which is why it is guarded here and nowhere inside
 * main. Nothing starts a thread; a compile check stays a compile check. */
static void pump_msg(void* ctx, const void* msg, uint32_t seq) {
    (void)ctx; (void)msg; (void)seq;
}

static bool pump_idle(void* ctx) {
    (void)ctx;
    return false;
}

/* One field per entry point, spelled out, so a changed signature is a compile
 * error here rather than a surprise in a caller. */
typedef struct pump_api {
    bool (*start)(psyrt_pump*, const psyrt_pump_desc*);
    void (*stop)(psyrt_pump*);
    int  (*submit)(psyrt_pump*, const void*);
    int  (*pending)(const psyrt_pump*);
    uint32_t (*done_seq)(const psyrt_pump*);
    int  (*wait)(psyrt_pump*, uint32_t, uint64_t);
    void (*lock)(psyrt_pump*);
    void (*unlock)(psyrt_pump*);
    bool (*is_running)(const psyrt_pump*);
    const char* (*error)(const psyrt_pump*);
    psyrt_policy (*policy)(const psyrt_pump*);
    psyrt_msg_fn  on_msg;
    psyrt_idle_fn on_idle;
} pump_api;

static const pump_api g_pump_api = {
    psyrt_pump_start, psyrt_pump_stop, psyrt_pump_submit, psyrt_pump_pending,
    psyrt_pump_done_seq, psyrt_pump_wait, psyrt_pump_lock, psyrt_pump_unlock,
    psyrt_pump_is_running, psyrt_pump_error, psyrt_pump_policy,
    pump_msg, pump_idle
};

/* Every desc field, by name. */
static psyrt_pump_desc pump_desc(void) {
    psyrt_pump_desc d;
    memset(&d, 0, sizeof(d));
    d.msg_size     = sizeof(uint32_t);
    d.capacity     = 2;
    d.ring         = NULL;
    d.on_msg       = g_pump_api.on_msg;
    d.on_idle      = g_pump_api.on_idle;
    d.ctx          = NULL;
    d.on_start     = NULL;
    d.start_ctx    = NULL;
    d.below_normal = false;
    d.pin_cpu      = -1;
    d.drop_on_stop = false;
    return d;
}
#endif /* PSYRT_NO_THREADS */

typedef struct ring_api {
    bool (*open)(psyrt_ring*, const psyrt_ring_desc*);
    const char* (*error)(const psyrt_ring*);
    int  (*push)(psyrt_ring*, const psyrt_event*);
    int  (*drain)(psyrt_ring*, psyrt_event*, int);
    uint32_t (*capacity)(const psyrt_ring*);
    uint32_t (*dropped)(const psyrt_ring*);
    int  (*correlate)(const psyrt_corr_desc*, psyrt_corr*);
    void (*trace_set)(psyrt_ring*);
    psyrt_ring* (*trace_get)(void);
    uint32_t (*thread_id)(void);
    uint64_t (*interrupt_time)(void);
    const char* (*strerror)(int);
    int64_t (*ticks_to_ns)(int64_t);
} ring_api;

static const ring_api g_ring_api = {
    psyrt_ring_open, psyrt_ring_error, psyrt_ring_push, psyrt_ring_drain,
    psyrt_ring_capacity, psyrt_ring_dropped, psyrt_correlate,
    psyrt_trace_set_ring, psyrt_trace_ring, psyrt_thread_id,
    psyrt_interrupt_time_100ns, psyrt_strerror, psyrt_ticks_to_ns
};

static unsigned char g_ring_mem[PSYRT_RING_BYTES(4)];

/* One record through a 4-slot ring, a correlation of the clock with itself,
 * and every macro once with the ring attached. */
static int ring_check(void) {
    int rc = 0;
    psyrt_ring ring;
    psyrt_ring_desc rd;
    psyrt_event ev, out[8];
    psyrt_corr_desc cd;
    psyrt_corr c;
    memset(&rd, 0, sizeof(rd));
    rd.memory = g_ring_mem;
    rd.bytes  = sizeof(g_ring_mem);
    if (!g_ring_api.open(&ring, &rd)) return 1;
    if (g_ring_api.capacity(&ring) != 4u) rc = 1;
    memset(&ev, 0, sizeof(ev));
    ev.source = (uint16_t)PSYRT_SRC_USER;
    ev.kind   = 1;
    ev.u.i64[0] = 42;
    if (g_ring_api.push(&ring, &ev) != 0) rc = 1;
    if (g_ring_api.drain(&ring, out, 8) != 1 || out[0].u.i64[0] != 42) rc = 1;
    if (out[0].tid != g_ring_api.thread_id() || out[0].t_ns == 0) rc = 1;
    if (g_ring_api.dropped(&ring) != 0u || g_ring_api.error(&ring)[0] != '\0') rc = 1;
    if (g_ring_api.strerror(PSYRT_ERR_FULL)[0] == '\0') rc = 1;
    memset(&cd, 0, sizeof(cd));
    cd.read = psyrt_now_ns;
    if (g_ring_api.correlate(&cd, &c) != 16 || c.width_ns == 0) rc = 1;
    (void)g_ring_api.interrupt_time();
    if (g_ring_api.ticks_to_ns(0) != 0) rc = 1;

    g_ring_api.trace_set(&ring);
    if (g_ring_api.trace_get() != &ring) rc = 1;
    {
        /* `fill` exists only for the plot: the default mode must still count
         * it as used, or -Werror stops the build. */
        double fill = 0.5;
        PSYRT_THREAD_INIT("compile check");
        PSYRT_THREAD_NAME("compile check");
        PSYRT_ZONE(z, "compile.zone");
        PSYRT_ZONE_VALUE(z, 7);
        PSYRT_PLOT("fill", fill);
        PSYRT_MESSAGE("hello");
        PSYRT_MESSAGEF("trial %d", 3);
        PSYRT_FRAME_MARK();
        PSYRT_FRAME_MARK_NAMED("display 2");
        PSYRT_ZONE_END(z);
    }
#ifdef __cplusplus
    {
        PSYRT_ZONE_SCOPED("compile.scoped");
    }
#endif
#if !defined(PSYRT_TRACY) && !defined(PSYRT_TRACE_RING)
    {
        /* The default mode compiles to nothing and evaluates nothing. */
        int count = 0;
        PSYRT_PLOT("x", (count++, 1.0));
        PSYRT_ZONE_VALUE(z, count++);
        PSYRT_MESSAGEF("%d", count++);
        if (count != 0) rc = 1;
        if (g_ring_api.drain(&ring, out, 8) != 0) rc = 1;
    }
#elif defined(PSYRT_TRACE_RING)
    if (g_ring_api.drain(&ring, out, 8) <= 0) rc = 1;
#endif
    g_ring_api.trace_set(NULL);
    return rc;
}

int main(void) {
    int rc = ring_check();
#ifndef PSYRT_NO_THREADS
    psyrt_pump p;
    psyrt_pump_desc d = pump_desc();
    memset(&p, 0, sizeof(p));
    /* A zeroed handle, so nothing runs: these are the answers the manual
     * promises for a pump that was never started. */
    if (d.capacity != 2) rc = 1;
    if (g_pump_api.done_seq(&p) != 0u) rc = 1;
    if (g_pump_api.is_running(&p)) rc = 1;
    if (g_pump_api.pending(&p) != 0) rc = 1;
    if (g_pump_api.submit(&p, &d) != PSYRT_ERR_STOPPED) rc = 1;
    if (g_pump_api.wait(&p, 1u, 0) != PSYRT_ERR_STOPPED) rc = 1;
    if (g_pump_api.policy(&p) != PSYRT_POLICY_NONE) rc = 1;
    if (g_pump_api.error(&p)[0] != '\0') rc = 1;
    g_pump_api.lock(&p);
    g_pump_api.unlock(&p);
    g_pump_api.stop(&p);
    if (!g_pump_api.start) rc = 1;
#endif
    return rc;
}
