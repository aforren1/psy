/* video_bench.c - what the decode thread spends per frame: pl_mpeg's decode,
 * the frame hash, the I420 to RGBA8 conversion, and QOI's decode, at
 * 1280 x 720 and 1920 x 1080 (or --size). CPU only: no window, no GPU.
 *
 * The MPEG-1 clip is the caller's (--mpg PATH, an MPEG-PS file; make one with
 * ffmpeg's testsrc2, docs/psy_video.md). The other inputs are generated: a
 * smooth frame (gradient, bar, blocks, as video_play draws) and a noisy one,
 * which are QOI's best and worst cases.
 *
 * Usage: video_bench [--mpg PATH] [--size WxH] [--reps N]
 * Exit code: 0; 1 when the clip does not decode; 2 for a bad argument.
 */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS   /* sscanf() and fopen() under /W4 /WX */
#endif
#define PSY_VIDEO_IMPLEMENTATION
#include "psy_video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmp_i64(const void* a, const void* b) {
    int64_t x = *(const int64_t*)a, y = *(const int64_t*)b;
    return x < y ? -1 : x > y;
}

static void report(const char* what, int64_t* t, int n, double bytes) {
    double mean = 0;
    int i;
    for (i = 0; i < n; i++) mean += (double)t[i];
    mean /= n;
    qsort(t, (size_t)n, sizeof *t, cmp_i64);
    printf("  %-34s mean %7.3f ms  p50 %7.3f  p99 %7.3f  max %7.3f", what, mean / 1e6, t[n / 2] / 1e6,
           t[(n * 99) / 100] / 1e6, t[n - 1] / 1e6);
    if (bytes > 0) printf("  %6.0f MB/s", bytes / (mean / 1e9) / 1e6);
    printf("\n");
}

static void bench_size(int w, int h, int reps) {
    size_t px = (size_t)w * h;
    uint8_t* rgba = (uint8_t*)malloc(px * 4);
    uint8_t* back = (uint8_t*)malloc(px * 4);
    uint8_t* enc = (uint8_t*)malloc(psyvid_qoi_max_bytes(w, h, 4));
    uint8_t* yuv = (uint8_t*)malloc(px + 2 * (size_t)((w + 1) / 2) * ((h + 1) / 2));
    int16_t* rows = (int16_t*)malloc(psyvid_yuv_rows_bytes(w));
    int64_t* t = (int64_t*)malloc(sizeof(int64_t) * (size_t)reps);
    psyvid_planes p;
    int k, r, x, y;
    uint64_t sink = 0;
    if (!rgba || !back || !enc || !yuv || !rows || !t) { fprintf(stderr, "video_bench: out of memory\n"); exit(1); }
    printf("%d x %d, %d repetitions\n", w, h, reps);
    psyvid__planes_layout(PSYVID_FMT_I420, w, h, yuv, &p);
    for (k = 0; k < (int)(px + 2 * (size_t)((w + 1) / 2) * ((h + 1) / 2)); k++) yuv[k] = (uint8_t)(k * 2654435761u >> 24);
    for (r = 0; r < reps; r++) {
        int64_t t0 = (int64_t)psyrt_now_ns();
        psyvid_yuv_to_rgba(&p, PSYVID_FMT_I420, w, h, PSYVID_MATRIX_BT709, PSYVID_RANGE_LIMITED, PSYVID_SITING_LEFT,
                           PSYVID_CHROMA_SITED, rgba, w * 4, rows);
        t[r] = (int64_t)psyrt_now_ns() - t0;
    }
    report("I420 to RGBA8, sited chroma", t, reps, (double)px * 4);
    for (r = 0; r < reps; r++) {
        int64_t t0 = (int64_t)psyrt_now_ns();
        psyvid_yuv_to_rgba(&p, PSYVID_FMT_I420, w, h, PSYVID_MATRIX_BT709, PSYVID_RANGE_LIMITED, PSYVID_SITING_LEFT,
                           PSYVID_CHROMA_NEAREST, rgba, w * 4, rows);
        t[r] = (int64_t)psyrt_now_ns() - t0;
    }
    report("I420 to RGBA8, nearest chroma", t, reps, (double)px * 4);
    for (r = 0; r < reps; r++) {
        int64_t t0 = (int64_t)psyrt_now_ns();
        sink ^= psyvid__hash_planes(PSYVID_FMT_I420, &p);
        t[r] = (int64_t)psyrt_now_ns() - t0;
    }
    report("XXH64 of the I420 planes", t, reps, (double)px * 1.5);
    for (k = 0; k < 2; k++) {
        int64_t n = 0;
        char what[64];
        for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
            uint8_t* q = rgba + ((size_t)y * w + x) * 4;
            int v = k == 0 ? (112 + (x * 32) / w + ((x / 16 + y / 16) & 1) * 8) : (int)((uint32_t)((y * w + x) * 2654435761u) >> 24);
            q[0] = (uint8_t)v; q[1] = (uint8_t)(k ? v ^ 0x55 : v); q[2] = (uint8_t)(k ? v ^ 0xaa : v); q[3] = 255;
        }
        n = psyvid_qoi_encode(rgba, w, h, 4, enc, psyvid_qoi_max_bytes(w, h, 4));
        for (r = 0; r < reps; r++) {
            int64_t t0 = (int64_t)psyrt_now_ns();
            if (psyvid_qoi_decode(enc, (size_t)n, back, w, h) != PSYVID_OK) { fprintf(stderr, "video_bench: QOI failed\n"); exit(1); }
            t[r] = (int64_t)psyrt_now_ns() - t0;
        }
        if (memcmp(back, rgba, px * 4) != 0) { fprintf(stderr, "video_bench: QOI round trip differs\n"); exit(1); }
        snprintf(what, sizeof what, "QOI decode, %s (%.1f:1)", k == 0 ? "smooth" : "noise", (double)px * 4 / (double)n);
        report(what, t, reps, (double)px * 4);
    }
    printf("  (%llx)\n", (unsigned long long)(sink & 0xF));
    free(rgba); free(back); free(enc); free(yuv); free(rows); free(t);
}

#ifndef PSYVID_NO_PL_MPEG
static int bench_mpg(const char* path) {
    plm_t* plm = plm_create_with_filename(path);
    int64_t* t;
    int n = 0, cap = 1 << 16, w, h;
    uint8_t* rgba;
    int16_t* rows;
    int64_t *th, *tc;
    if (!plm) { fprintf(stderr, "video_bench: pl_mpeg cannot open %s\n", path); return 1; }
    plm_set_audio_enabled(plm, FALSE);
    w = plm_get_width(plm); h = plm_get_height(plm);
    t = (int64_t*)malloc(sizeof(int64_t) * (size_t)cap);
    th = (int64_t*)malloc(sizeof(int64_t) * (size_t)cap);
    tc = (int64_t*)malloc(sizeof(int64_t) * (size_t)cap);
    rgba = (uint8_t*)malloc((size_t)w * h * 4);
    rows = (int16_t*)malloc(psyvid_yuv_rows_bytes(w));
    if (!t || !th || !tc || !rgba || !rows) return 1;
    printf("%s: %d x %d at %.3f fps\n", path, w, h, plm_get_framerate(plm));
    for (;;) {
        int64_t t0 = (int64_t)psyrt_now_ns(), t1, t2;
        plm_frame_t* f = plm_decode_video(plm);
        psyvid_planes p;
        if (!f || n >= cap) break;
        t1 = (int64_t)psyrt_now_ns();
        memset(&p, 0, sizeof p);
        p.data[0] = f->y.data; p.stride[0] = (int32_t)f->y.width; p.w[0] = w; p.h[0] = h;
        p.data[1] = f->cb.data; p.stride[1] = (int32_t)f->cb.width; p.w[1] = (w + 1) / 2; p.h[1] = (h + 1) / 2;
        p.data[2] = f->cr.data; p.stride[2] = (int32_t)f->cr.width; p.w[2] = (w + 1) / 2; p.h[2] = (h + 1) / 2;
        (void)psyvid__hash_planes(PSYVID_FMT_I420, &p);
        t2 = (int64_t)psyrt_now_ns();
        psyvid_yuv_to_rgba(&p, PSYVID_FMT_I420, w, h, PSYVID_MATRIX_BT601, PSYVID_RANGE_LIMITED, PSYVID_SITING_CENTER,
                           PSYVID_CHROMA_SITED, rgba, w * 4, rows);
        tc[n] = (int64_t)psyrt_now_ns() - t2;
        th[n] = t2 - t1;
        t[n] = t1 - t0;
        n++;
    }
    plm_destroy(plm);
    if (n == 0) { fprintf(stderr, "video_bench: no frames in %s\n", path); return 1; }
    printf("  %d frames\n", n);
    report("pl_mpeg decode", t, n, 0);
    report("XXH64 of the planes", th, n, (double)w * h * 1.5);
    report("I420 to RGBA8, sited", tc, n, (double)w * h * 4);
    free(t); free(th); free(tc); free(rgba); free(rows);
    return 0;
}
#endif

int main(int argc, char** argv) {
    const char* mpg = NULL;
    int w = 0, h = 0, reps = 100, i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--mpg") && i + 1 < argc) mpg = argv[++i];
        else if (!strcmp(argv[i], "--size") && i + 1 < argc && sscanf(argv[i + 1], "%dx%d", &w, &h) == 2) i++;
        else if (!strcmp(argv[i], "--reps") && i + 1 < argc) reps = atoi(argv[++i]);
        else { fprintf(stderr, "usage: video_bench [--mpg PATH] [--size WxH] [--reps N]\n"); return 2; }
    }
    if (reps <= 0 || (w != 0 && (w <= 0 || h <= 0))) { fprintf(stderr, "video_bench: bad size or count\n"); return 2; }
    if (mpg) {
#ifndef PSYVID_NO_PL_MPEG
        return bench_mpg(mpg);
#else
        fprintf(stderr, "video_bench: built with PSYVID_NO_PL_MPEG\n");
        return 1;
#endif
    }
    if (w > 0) bench_size(w, h, reps);
    else { bench_size(1280, 720, reps); bench_size(1920, 1080, reps); }
    return 0;
}
