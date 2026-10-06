/* Compile check: psy_gfx.h as a C translation unit with the implementation
 * enabled. CMake and CI build this as C11 with warnings as errors when SDL3
 * is found; psy_gfx.cpp builds the same source as C++17. It runs three
 * frames on the simulated display, where psy_gfx.h opens its null backend,
 * so it passes on a machine with no display and no GPU. The frames hold a
 * group member and a target pass, so v0.2's calls link and run too. */
#define PSY_GFX_IMPLEMENTATION
#include "psy_gfx.h"

int main(void) {
    psyscr_screen s;
    psyscr_desc d;
    psygfx_gfx g;
    psygfx_desc gd;
    psygfx_gabor_desc gab;
    psygfx_stim st;
    psyscr_frame f;
    psygfx_group_desc grd;
    psygfx_group grp;
    psygfx_target_desc td;
    psygfx_tex t;
    static const float zero[4] = { 0, 0, 0, 0 };
    static const uint8_t mask[4] = { 0, 255, 255, 0 };
    float sdf[16];
    int i, n = 0, m = 0, k = 0;
    memset(&s, 0, sizeof s);
    memset(&d, 0, sizeof d);
    memset(&g, 0, sizeof g);
    memset(&gd, 0, sizeof gd);
    memset(&gab, 0, sizeof gab);
    memset(&grd, 0, sizeof grd);
    memset(&td, 0, sizeof td);
    d.backend = PSYSCR_BACKEND_SIM;
    d.sim_period_ns = 2000000;
    if (!psyscr_open(&s, &d)) return 1;
    gd.screen = &s;
    if (!psygfx_open(&g, &gd)) return 2;
    gab.sf = 1.0f / 32.0f;
    gab.sigma = 32.0f;
    gab.group = &grp;
    grd.scale = 2.0f;
    grp = psygfx_group_make(&grd);
    st = psygfx_gabor(&gab);
    td.w = 64; td.h = 64;
    t = psygfx_target(&g, &td);
    if (t.id == 0) return 9;
    for (i = 0; i < 3; i++) {
        if (psyscr_begin(&s, &f) != PSYSCR_OK) return 3;
        if (psygfx_begin(&g, &f) != PSYGFX_OK) return 4;
        if (psygfx_begin_target(&g, t, zero) != PSYGFX_OK) return 10;
        if (psygfx_draw(&g, &st) != PSYGFX_OK) return 5;
        if (psygfx_end_target(&g) != PSYGFX_OK) return 11;
        if (psygfx_draw(&g, &st) != PSYGFX_OK) return 5;
        if (psygfx_end(&g) != PSYGFX_OK) return 6;
        if (psyscr_flip(&s) != PSYSCR_OK) return 7;
    }
    psygfx_texture_free(&g, t);
    psygfx_close(&g);
    psyscr_close(&s);
    if (psygfx_sdf_from_mask(sdf, mask, 2, 2, 1) != PSYGFX_OK) return 12;
    return psygfx_params(&n) && n == PSYGFX_P_COUNT && psygfx_desc_params(&m) && m > 0 &&
           psygfx_group_params(&k) && k > 0 ? 0 : 8;
}
