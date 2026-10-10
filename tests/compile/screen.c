/* Compile check: ysp/screen.h as a C translation unit with the
 * implementation enabled. CMake and CI build this as C11 with warnings as
 * errors when SDL3 is found; screen.cpp builds the same source as C++17.
 * It runs three frames on the simulated display, which needs no window, so
 * it passes on a machine with no display and no ANGLE. */
#define YSP_SCREEN_IMPLEMENTATION
#include "ysp/screen.h"

/* v0.5.2: desc.on_context runs once, before settling (skipped on SIM) */
static void on_context(void* ctx, yscr_screen* s) {
    if (yscr_is_open(s)) ++*(int*)ctx;
}

int main(void) {
    yscr_screen s;
    yscr_desc d;
    yscr_frame f;
    yscr_sync_info si;
    yscr_settle_info st;
    char line[1024];
    int i, n = 0, hooked = 0;
    memset(&s, 0, sizeof s);
    memset(&d, 0, sizeof d);
    d.backend = YSCR_BACKEND_SIM;
    d.sim_period_ns = 2000000;
    d.depth = 2;   /* v0.4.4: a pinned depth */
    d.settle = YSCR_SETTLE_STRICT;   /* v0.5.0: SIM skips settling, recorded */
    d.settle_flips = 6;
    d.foreground = YSCR_FOREGROUND_FORCE;   /* v0.5.1: no window, n/a */
    d.on_context = on_context;
    d.on_context_ctx = &hooked;
    if (!yscr_open(&s, &d)) return 1;
    for (i = 0; i < 3; i++) {
        if (yscr_begin(&s, &f) != YSCR_OK) return 2;
        if (yscr_flip(&s) != YSCR_OK) return 3;
    }
    yscr_sync_check(&s, &si);   /* v0.4.2: the simulated display is synced */
    if (si.untimed || si.fired_index != -1) return 5;
    yscr_describe(&s, line, sizeof line);
    if (!strstr(line, "depth=2(pin,0 changes)")) return 6;
    yscr_settle_check(&s, &st);
    if (st.result != YSCR_SETTLE_SKIPPED || !strstr(line, "settle=SKIPPED(sim)")) return 7;
    if (st.foreground != YSCR_FG_NA || !strstr(line, "foreground=n/a")) return 8;
    if (hooked != 1 || st.hook_ns < 0) return 9;
    yscr_close(&s);
    return yscr_params(&n) && n > 0 ? 0 : 4;
}
