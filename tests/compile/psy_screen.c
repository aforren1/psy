/* Compile check: psy_screen.h as a C translation unit with the
 * implementation enabled. CMake and CI build this as C11 with warnings as
 * errors when SDL3 is found; psy_screen.cpp builds the same source as C++17.
 * It runs three frames on the simulated display, which needs no window, so
 * it passes on a machine with no display and no ANGLE. */
#define PSY_SCREEN_IMPLEMENTATION
#include "psy_screen.h"

int main(void) {
    psyscr_screen s;
    psyscr_desc d;
    psyscr_frame f;
    int i, n = 0;
    memset(&s, 0, sizeof s);
    memset(&d, 0, sizeof d);
    d.backend = PSYSCR_BACKEND_SIM;
    d.sim_period_ns = 2000000;
    if (!psyscr_open(&s, &d)) return 1;
    for (i = 0; i < 3; i++) {
        if (psyscr_begin(&s, &f) != PSYSCR_OK) return 2;
        if (psyscr_flip(&s) != PSYSCR_OK) return 3;
    }
    psyscr_close(&s);
    return psyscr_params(&n) && n > 0 ? 0 : 4;
}
