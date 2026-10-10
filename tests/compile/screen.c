/* Compile check: ysp/screen.h as a C translation unit with the
 * implementation enabled. CMake and CI build this as C11 with warnings as
 * errors when SDL3 is found; screen.cpp builds the same source as C++17.
 * It runs three frames on the simulated display, which needs no window, so
 * it passes on a machine with no display and no ANGLE. */
#define YSP_SCREEN_IMPLEMENTATION
#include "ysp/screen.h"

int main(void) {
    yscr_screen s;
    yscr_desc d;
    yscr_frame f;
    yscr_sync_info si;
    yscr_settle_info st;
    char line[1024];
    int i, n = 0;
    memset(&s, 0, sizeof s);
    memset(&d, 0, sizeof d);
    d.backend = YSCR_BACKEND_SIM;
    d.sim_period_ns = 2000000;
    d.depth = 2;   /* v0.4.4: a pinned depth */
    d.settle = YSCR_SETTLE_STRICT;   /* v0.5.0: SIM skips settling, recorded */
    d.settle_flips = 6;
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
    yscr_close(&s);
    return yscr_params(&n) && n > 0 ? 0 : 4;
}
