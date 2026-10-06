/* Compile check: psy_video.h as a C translation unit with the implementation
 * enabled. CMake and CI build this as C11 with warnings as errors when SDL3
 * is found; psy_video.cpp builds the same source as C++17. It writes a small
 * frame sequence beside the program, plays it for a few frames on the
 * simulated display (psy_gfx.h's null backend) and removes it, so it passes
 * on a machine with no display and no GPU. */
#define PSY_VIDEO_IMPLEMENTATION
#include "psy_video.h"

#include <stdio.h>

int main(void) {
    psyscr_screen s;
    psyscr_desc d;
    psygfx_gfx g;
    psygfx_desc gd;
    psyvid_seq w;
    psyvid_seq_desc sd;
    psyvid_movie mv;
    psyvid_desc vd;
    psyvid_stim_desc st;
    psygfx_stim film;
    psyscr_frame f;
    static uint8_t px[16 * 8 * 4];
    const void* planes[3];
    int32_t strides[3] = { 16 * 4, 0, 0 };
    const char* path = "psy_video_compile.psyseq";
    char line[1024];
    int i, n = 0, rc = 0;
    memset(&s, 0, sizeof s);
    memset(&d, 0, sizeof d);
    memset(&g, 0, sizeof g);
    memset(&gd, 0, sizeof gd);
    memset(&w, 0, sizeof w);
    memset(&sd, 0, sizeof sd);
    memset(&mv, 0, sizeof mv);
    memset(&vd, 0, sizeof vd);
    memset(&st, 0, sizeof st);
    sd.path = path; sd.w = 16; sd.h = 8; sd.format = PSYVID_FMT_RGBA8;
    sd.compression = PSYVID_SEQ_QOI; sd.fps_num = 30; sd.fps_den = 1;
    if (!psyvid_seq_create(&w, &sd)) return 1;
    planes[0] = px; planes[1] = NULL; planes[2] = NULL;
    for (i = 0; i < 4; i++) {
        memset(px, i * 40, sizeof px);
        if (psyvid_seq_write(&w, planes, strides, NULL) != PSYVID_OK) return 2;
    }
    if (psyvid_seq_close(&w) != PSYVID_OK) return 3;
    d.backend = PSYSCR_BACKEND_SIM;
    d.sim_period_ns = 2000000;
    if (!psyscr_open(&s, &d)) return 4;
    gd.screen = &s;
    if (!psygfx_open(&g, &gd)) return 5;
    vd.path = path;
    vd.ahead = 2;
    if (!psyvid_open(&mv, &g, &vd)) { fprintf(stderr, "%s\n", psyvid_error(&mv)); return 6; }
    film = psyvid_stim(&mv, &st);
    if (psyvid_play_at(&mv, PSYVID_ASAP) <= 0) return 7;
    for (i = 0; i < 400 && rc == PSYVID_OK; i++) {
        if (psyscr_begin(&s, &f) != PSYSCR_OK) return 8;
        rc = psyvid_update(&mv, &f);
        if (psygfx_begin(&g, &f) != PSYGFX_OK) return 9;
        if (psygfx_draw(&g, &film) != PSYGFX_OK) return 10;
        if (psygfx_end(&g) != PSYGFX_OK) return 11;
        if (psyscr_flip(&s) != PSYSCR_OK) return 12;
    }
    if (rc != PSYVID_ENDED) { fprintf(stderr, "update: %d %s\n", rc, psyvid_error(&mv)); return 13; }
    psyvid_describe(&mv, line, sizeof line);
    psyvid_close(&mv);
    psygfx_close(&g);
    psyscr_close(&s);
    remove(path);
    return psyvid_params(&n) && n > 0 && psyvid_xxh64("", 0, 0) == UINT64_C(0xEF46DB3751D8E999) ? 0 : 14;
}
