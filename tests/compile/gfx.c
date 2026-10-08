/* Compile check: ysp/gfx.h as a C translation unit with the implementation
 * enabled. CMake and CI build this as C11 with warnings as errors when SDL3
 * is found; gfx.cpp builds the same source as C++17. It runs three
 * frames on the simulated display, where ysp/gfx.h opens its null backend,
 * so it passes on a machine with no display and no GPU. The frames hold a
 * group member and a target pass, so v0.2's calls link and run too. */
#define YSP_GFX_IMPLEMENTATION
#include "ysp/gfx.h"

int main(void) {
    yscr_screen s;
    yscr_desc d;
    ygfx_gfx g;
    ygfx_desc gd;
    ygfx_gabor_desc gab;
    ygfx_stim st;
    yscr_frame f;
    ygfx_group_desc grd;
    ygfx_group grp;
    ygfx_target_desc td;
    ygfx_tex t;
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
    d.backend = YSCR_BACKEND_SIM;
    d.sim_period_ns = 2000000;
    if (!yscr_open(&s, &d)) return 1;
    gd.screen = &s;
    if (!ygfx_open(&g, &gd)) return 2;
    gab.sf = 1.0f / 32.0f;
    gab.sigma = 32.0f;
    gab.group = &grp;
    grd.scale = 2.0f;
    grp = ygfx_group_make(&grd);
    st = ygfx_gabor(&gab);
    td.w = 64; td.h = 64;
    t = ygfx_target(&g, &td);
    if (t.id == 0) return 9;
    for (i = 0; i < 3; i++) {
        if (yscr_begin(&s, &f) != YSCR_OK) return 3;
        if (ygfx_begin(&g, &f) != YGFX_OK) return 4;
        if (ygfx_begin_target(&g, t, zero) != YGFX_OK) return 10;
        if (ygfx_draw(&g, &st) != YGFX_OK) return 5;
        if (ygfx_end_target(&g) != YGFX_OK) return 11;
        if (ygfx_draw(&g, &st) != YGFX_OK) return 5;
        if (ygfx_end(&g) != YGFX_OK) return 6;
        if (yscr_flip(&s) != YSCR_OK) return 7;
    }
    ygfx_texture_free(&g, t);
    ygfx_close(&g);
    yscr_close(&s);
    if (ygfx_sdf_from_mask(sdf, mask, 2, 2, 1) != YGFX_OK) return 12;
    return ygfx_params(&n) && n == YGFX_P_COUNT && ygfx_desc_params(&m) && m > 0 &&
           ygfx_group_params(&k) && k > 0 ? 0 : 8;
}
