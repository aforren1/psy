/* Compile check: ysp/rt.h as a C translation unit with the implementation
 * enabled. CMake and CI build this as C11 with warnings as errors, once with
 * threading and once with YRT_NO_THREADS (which drops the deadline worker
 * and the pump); rt.cpp builds the same source as C++17. With
 * -DYSP_TRACY_CHECK=ON, CMake also builds it with YRT_TRACY against a
 * fetched Tracy. It runs and returns 0 to prove it links.
 *
 * The event ring, the clock correlation and the instrumentation macros are
 * outside the thread guard: they exist in every build. In the default mode
 * the check also proves that no macro evaluates its arguments. */
#define YSP_RT_IMPLEMENTATION
#include "ysp/rt.h"

#include <string.h>

#ifndef YRT_NO_THREADS
/* Everything below names a pump declaration, so the compile check covers the
 * pump's prototypes and not just the header's syntax, and the #ifndef is the
 * check that YRT_NO_THREADS really removes them: this block must fail to
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
    bool (*start)(yrt_pump*, const yrt_pump_desc*);
    void (*stop)(yrt_pump*);
    int  (*submit)(yrt_pump*, const void*);
    int  (*pending)(const yrt_pump*);
    uint32_t (*done_seq)(const yrt_pump*);
    int  (*wait)(yrt_pump*, uint32_t, uint64_t);
    void (*lock)(yrt_pump*);
    void (*unlock)(yrt_pump*);
    bool (*is_running)(const yrt_pump*);
    const char* (*error)(const yrt_pump*);
    yrt_policy (*policy)(const yrt_pump*);
    yrt_msg_fn  on_msg;
    yrt_idle_fn on_idle;
} pump_api;

static const pump_api g_pump_api = {
    yrt_pump_start, yrt_pump_stop, yrt_pump_submit, yrt_pump_pending,
    yrt_pump_done_seq, yrt_pump_wait, yrt_pump_lock, yrt_pump_unlock,
    yrt_pump_is_running, yrt_pump_error, yrt_pump_policy,
    pump_msg, pump_idle
};

/* Every desc field, by name. */
static yrt_pump_desc pump_desc(void) {
    yrt_pump_desc d;
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
#endif /* YRT_NO_THREADS */

typedef struct ring_api {
    bool (*open)(yrt_ring*, const yrt_ring_desc*);
    const char* (*error)(const yrt_ring*);
    int  (*push)(yrt_ring*, const yrt_event*);
    int  (*drain)(yrt_ring*, yrt_event*, int);
    uint32_t (*capacity)(const yrt_ring*);
    uint32_t (*dropped)(const yrt_ring*);
    int  (*correlate)(const yrt_corr_desc*, yrt_corr*);
    void (*trace_set)(yrt_ring*);
    yrt_ring* (*trace_get)(void);
    uint32_t (*thread_id)(void);
    uint64_t (*interrupt_time)(void);
    const char* (*strerror)(int);
    int64_t (*ticks_to_ns)(int64_t);
} ring_api;

static const ring_api g_ring_api = {
    yrt_ring_open, yrt_ring_error, yrt_ring_push, yrt_ring_drain,
    yrt_ring_capacity, yrt_ring_dropped, yrt_correlate,
    yrt_trace_set_ring, yrt_trace_ring, yrt_thread_id,
    yrt_interrupt_time_100ns, yrt_strerror, yrt_ticks_to_ns
};

static unsigned char g_ring_mem[YRT_RING_BYTES(4)];

/* One record through a 4-slot ring, a correlation of the clock with itself,
 * and every macro once with the ring attached. */
static int ring_check(void) {
    int rc = 0;
    yrt_ring ring;
    yrt_ring_desc rd;
    yrt_event ev, out[8];
    yrt_corr_desc cd;
    yrt_corr c;
    memset(&rd, 0, sizeof(rd));
    rd.memory = g_ring_mem;
    rd.bytes  = sizeof(g_ring_mem);
    if (!g_ring_api.open(&ring, &rd)) return 1;
    if (g_ring_api.capacity(&ring) != 4u) rc = 1;
    memset(&ev, 0, sizeof(ev));
    ev.source = (uint16_t)YRT_SRC_USER;
    ev.kind   = 1;
    ev.u.i64[0] = 42;
    if (g_ring_api.push(&ring, &ev) != 0) rc = 1;
    if (g_ring_api.drain(&ring, out, 8) != 1 || out[0].u.i64[0] != 42) rc = 1;
    if (out[0].tid != g_ring_api.thread_id() || out[0].t_ns == 0) rc = 1;
    if (g_ring_api.dropped(&ring) != 0u || g_ring_api.error(&ring)[0] != '\0') rc = 1;
    if (g_ring_api.strerror(YRT_ERR_FULL)[0] == '\0') rc = 1;
    memset(&cd, 0, sizeof(cd));
    cd.read = yrt_now_ns;
    if (g_ring_api.correlate(&cd, &c) != 16 || c.width_ns == 0) rc = 1;
    (void)g_ring_api.interrupt_time();
    if (g_ring_api.ticks_to_ns(0) != 0) rc = 1;

    g_ring_api.trace_set(&ring);
    if (g_ring_api.trace_get() != &ring) rc = 1;
    {
        /* `fill` exists only for the plot: the default mode must still count
         * it as used, or -Werror stops the build. */
        double fill = 0.5;
        YRT_THREAD_INIT("compile check");
        YRT_THREAD_NAME("compile check");
        YRT_ZONE(z, "compile.zone");
        YRT_ZONE_VALUE(z, 7);
        YRT_PLOT("fill", fill);
        YRT_MESSAGE("hello");
        YRT_MESSAGEF("trial %d", 3);
        YRT_FRAME_MARK();
        YRT_FRAME_MARK_NAMED("display 2");
        YRT_ZONE_END(z);
    }
#ifdef __cplusplus
    {
        YRT_ZONE_SCOPED("compile.scoped");
    }
#endif
#if !defined(YRT_TRACY) && !defined(YRT_TRACE_RING)
    {
        /* The default mode compiles to nothing and evaluates nothing. */
        int count = 0;
        YRT_PLOT("x", (count++, 1.0));
        YRT_ZONE_VALUE(z, count++);
        YRT_MESSAGEF("%d", count++);
        if (count != 0) rc = 1;
        if (g_ring_api.drain(&ring, out, 8) != 0) rc = 1;
    }
#elif defined(YRT_TRACE_RING)
    if (g_ring_api.drain(&ring, out, 8) <= 0) rc = 1;
#endif
    g_ring_api.trace_set(NULL);
    return rc;
}

int main(void) {
    int rc = ring_check();
#ifndef YRT_NO_THREADS
    yrt_pump p;
    yrt_pump_desc d = pump_desc();
    memset(&p, 0, sizeof(p));
    /* A zeroed handle, so nothing runs: these are the answers the manual
     * promises for a pump that was never started. */
    if (d.capacity != 2) rc = 1;
    if (g_pump_api.done_seq(&p) != 0u) rc = 1;
    if (g_pump_api.is_running(&p)) rc = 1;
    if (g_pump_api.pending(&p) != 0) rc = 1;
    if (g_pump_api.submit(&p, &d) != YRT_ERR_STOPPED) rc = 1;
    if (g_pump_api.wait(&p, 1u, 0) != YRT_ERR_STOPPED) rc = 1;
    if (g_pump_api.policy(&p) != YRT_POLICY_NONE) rc = 1;
    if (g_pump_api.error(&p)[0] != '\0') rc = 1;
    g_pump_api.lock(&p);
    g_pump_api.unlock(&p);
    g_pump_api.stop(&p);
    if (!g_pump_api.start) rc = 1;
#endif
    return rc;
}
