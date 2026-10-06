/* video_check.c - the GPU paths against the upload path, on real hardware:
 * one MP4 drawn through UPLOAD (planes uploaded) and through PSYVID_PATH_GPU
 * (DXVA on the screen's device, one GPU copy a frame), frame by frame in manual
 * mode, each read back with psygfx_read_scene(). It checks that each frame
 * shows its own index (the 16 bars of tests/media/make_video_clips.sh) and
 * that the two paths draw the same values.
 *
 * Usage: video_check --file CLIP.mp4 [--frames N] [--ahead N]
 *   The clip needs its .psyvi beside it and the bars (the script's c_*
 *   clips). The window is the clip's size; the screen opens with
 *   desc.d3d11_video. It needs a GPU and a window, so it is not a ctest.
 * Exit code: 0 when every frame matched; 1 on a mismatch or a failure to
 * open; 2 for a bad argument; 3 when the GPU path is not available here
 * (printed: the renderer or the screen cannot).
 */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef PSYVID_NO_PL_MPEG
#define PSYVID_NO_PL_MPEG
#endif
#define PSY_VIDEO_IMPLEMENTATION
#include "psy_video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static psyscr_screen scr;
static psygfx_gfx gfx;
static psyvid_movie mv[2];

static int bars(const float* row, int w) {
    int k, v = 0;
    for (k = 0; k < 16; k++) {
        int x = (k * w) / 16 + w / 32;
        if (row[(size_t)x * 4] > 0.5f) v |= 1 << k;
    }
    return v;
}

int main(int argc, char** argv) {
    const char* file = NULL;
    int frames = 60, ahead = 0, i, k, w, h, bad = 0, done = 0;
    psyscr_desc sd;
    psygfx_desc gd;
    psyvid_desc vd;
    psyvid_info info;
    psygfx_stim st[2];
    psyvid_stim_desc sdsc;
    float* img[2];
    double worst = 0.0;
    int worst_x = -1, worst_y = -1;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--file") && i + 1 < argc) file = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ahead") && i + 1 < argc) ahead = atoi(argv[++i]);
        else { fprintf(stderr, "usage: video_check --file CLIP.mp4 [--frames N] [--ahead N]\n"); return 2; }
    }
    if (!file || frames <= 0 || ahead < 0) { fprintf(stderr, "usage: video_check --file CLIP.mp4 [--frames N] [--ahead N]\n"); return 2; }
    {
        psyvid_desc q;
        char err[300];
        memset(&q, 0, sizeof q);
        q.path = file;
        if (!psyvid_probe(&q, &info, err, sizeof err)) { fprintf(stderr, "video_check: %s\n", err); return 1; }
    }
    w = info.w; h = info.h;
    memset(&sd, 0, sizeof sd);
    sd.windowed = true;
    sd.window_w = w; sd.window_h = h;
    sd.d3d11_video = true;
    if (!psyscr_open(&scr, &sd)) { fprintf(stderr, "video_check: %s\n", psyscr_error(&scr)); return 3; }
    memset(&gd, 0, sizeof gd);
    gd.screen = &scr;
    if (!psygfx_open(&gfx, &gd)) { fprintf(stderr, "video_check: %s\n", psygfx_error(&gfx)); return 1; }
    for (k = 0; k < 2; k++) {
        memset(&vd, 0, sizeof vd);
        vd.path = file;
        vd.light = PSYVID_LIGHT_CODES;
        vd.gpu_path = k ? PSYVID_PATH_GPU : PSYVID_PATH_UPLOAD;
        vd.hw_decode = PSYVID_HW_DXVA;
        vd.ahead = ahead;
        if (!psyvid_open(&mv[k], &gfx, &vd)) {
            fprintf(stderr, "video_check: %s: %s\n", k ? "GPU" : "UPLOAD", psyvid_error(&mv[k]));
            return k ? 3 : 1;
        }
        memset(&sdsc, 0, sizeof sdsc);
        st[k] = psyvid_stim(&mv[k], &sdsc);
    }
    img[0] = (float*)malloc((size_t)w * h * 4 * sizeof(float));
    img[1] = (float*)malloc((size_t)w * h * 4 * sizeof(float));
    if (!img[0] || !img[1]) return 1;
    /* frames 0, 1, ... then a jump back and a few across GOPs */
    for (i = 0; i < frames && done < frames; i++) {
        int64_t want = i < frames / 2 ? i : (int64_t)((i * 37) % (int)info.frames);
        int got[2] = { 0, 0 }, tries;
        for (k = 0; k < 2; k++) psyvid_show(&mv[k], want);
        for (tries = 0; tries < 120 && !(got[0] && got[1]); tries++) {
            psyscr_frame f;
            int which;
            psyvid_record r;
            if (psyscr_begin(&scr, &f) != PSYSCR_OK) { fprintf(stderr, "video_check: the screen closed\n"); return 1; }
            for (k = 0; k < 2; k++)
                if (psyvid_update(&mv[k], &f) < 0) { fprintf(stderr, "video_check: %s: %s\n", k ? "GPU" : "UPLOAD", psyvid_error(&mv[k])); return 1; }
            /* draw the path that has the frame and has not been read yet */
            which = -1;
            for (k = 0; k < 2 && which < 0; k++)
                if (!got[k] && psyvid_last(&mv[k], &r) == PSYVID_OK && r.display == f.index && r.frame == want) which = k;
            psygfx_begin(&gfx, &f);
            if (which >= 0) psygfx_draw(&gfx, &st[which]);
            psygfx_end(&gfx);
            if (which >= 0) {
                psygfx_read_scene(&gfx, 0, 0, w, h, img[which]);
                got[which] = 1;
            }
            psyscr_flip(&scr);
        }
        if (!(got[0] && got[1])) { fprintf(stderr, "video_check: frame %lld never shown\n", (long long)want); return 1; }
        for (k = 0; k < 2; k++) {
            int b = bars(img[k] + (size_t)32 * w * 4, w);
            if (b != (int)(want & 0xffff)) { fprintf(stderr, "video_check: %s frame %lld shows %d\n", k ? "GPU" : "UPLOAD", (long long)want, b); bad++; }
        }
        {
            size_t p, n = (size_t)w * h * 4;
            for (p = 0; p < n; p++) {
                double d = fabs((double)img[0][p] - (double)img[1][p]);
                if (d > worst) { worst = d; worst_x = (int)((p / 4) % (size_t)w); worst_y = (int)((p / 4) / (size_t)w); }
            }
        }
        done++;
    }
    printf("video_check: %s, %d x %d, %d frames: bars %s; GPU against UPLOAD, worst difference %.3g (pixel %d, %d)\n",
           file, w, h, done, bad ? "WRONG" : "right", worst, worst_x, worst_y);
    {
        char line[1024];
        psyvid_describe(&mv[1], line, sizeof line);
        printf("%s\n", line);
    }
    for (k = 0; k < 2; k++) psyvid_close(&mv[k]);
    psygfx_close(&gfx);
    psyscr_close(&scr);
    free(img[0]); free(img[1]);
    return bad || worst > 1e-6 ? 1 : 0;
}
