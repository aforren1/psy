/* Compile check: psy_rt.h as a C translation unit with the implementation
 * enabled. CMake and CI build this as C11 with warnings as errors, once with
 * threading and once with PSYRT_NO_THREADS (which drops the deadline worker
 * and the pump); psy_rt.cpp builds the same source as C++17. It runs and
 * returns 0 to prove it links. */
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

int main(void) {
    int rc = 0;
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
