/* gfx_bench.c - what psy_gfx.h costs per frame, offscreen.
 *
 * Opens a headless GL ES 3.0 context (tests/adapt/psy_gfx_headless.h) at the
 * panel's size and times each workload of docs/psy_gfx.md's measurement plan:
 *   cpu   psygfx_begin() to psygfx_end() on the frame thread, per frame,
 *         mean and p99 over the frames
 *   gpu   the workload's frames back to back between two glFinish() calls,
 *         per frame, minus nothing: GPU-bound throughput, the per-frame cost
 *         a frame loop must leave room for. No timer query, which cost about
 *         250 us of CPU per frame through ANGLE (docs/psy_screen.md).
 * Nothing is shown. Take the measurement lock of your machine first: the
 * numbers mean nothing while another program loads the GPU.
 *
 * Usage: gfx_bench [--device hardware|warp|swiftshader|mesa|llvmpipe]
 *                  [--size W H] [--frames N] [--only NAME] [--scene 16|32]
 *                  [--cache DIR] [--open-only]
 *   --scene  the scene format of the workloads after the empty frames
 *   --cache  a program cache in DIR (psygfx_file_cache_init); :mem: one in memory
 *   --open-only  print psygfx_open()'s time and what the cache did, then exit
 * Exit code: 0, 1 when no GL ES 3.0 context opened, 2 for a bad argument.
 * On Windows set PSYSCR_ANGLE_DIR to ANGLE's directory.
 */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#define PSY_GFX_IMPLEMENTATION
#include "psy_gfx.h"
#include "tests/adapt/psy_gfx_headless.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
    #define GLCALL __stdcall
#else
    #define GLCALL
#endif

static psygfx_headless hl;
static psyscr_screen scr;
static psygfx_gfx gfx;
static int W = 1920, H = 1200, FRAMES = 120;
static const char* only = NULL;
static psygfx_format scene_fmt = PSYGFX_FORMAT_NONE;   /* the workloads' scene; 0 = the default */
static psygfx_file_cache pcache;          /* --cache DIR */
static const psygfx_cache* cache_desc = NULL;
static int open_only = 0;
static void (GLCALL *glFinish_)(void);

#define MAXF 2000
static double cpu_us[MAXF];

static int cmp_d(const void* a, const void* b) {
    double x = *(const double*)a, y = *(const double*)b;
    return x < y ? -1 : (x > y);
}

static double p99(double* v, int n) {
    qsort(v, (size_t)n, sizeof v[0], cmp_d);
    return v[(int)(0.99 * (n - 1))];
}

static int run(const char* name) { return !only || strstr(name, only) != NULL; }

static void report(const char* name, int n, double wall_ns, int frames) {
    double mean = 0;
    int i;
    for (i = 0; i < n; i++) mean += cpu_us[i];
    mean /= n;
    printf("| %-52s | %9.1f | %9.1f | %9.3f |\n", name, mean, p99(cpu_us, n), wall_ns / frames * 1e-6);
    fflush(stdout);
}

/* Draws `s` (n of them) for FRAMES frames; prep() runs before each frame,
 * outside begin..end, for uploads. */
static void bench(const char* name, const psygfx_stim* s, int n, void (*prep)(int), int timed_prep) {
    psyscr_frame f;
    uint64_t t0, a, b;
    int i;
    if (!run(name)) return;
    memset(&f, 0, sizeof f);
    f.period = 16666667;
    for (i = 0; i < 5; i++) {   /* warm: lazy shader variants, first uploads */
        if (prep) prep(i);
        psygfx_begin(&gfx, &f);
        psygfx_draw_n(&gfx, s, n);
        psygfx_end(&gfx);
    }
    glFinish_();
    t0 = psyrt_now_ns();
    for (i = 0; i < FRAMES; i++) {
        f.index = i;
        f.onset = (int64_t)psyrt_now_ns();
        a = psyrt_now_ns();
        if (prep) prep(i);
        if (!timed_prep) a = psyrt_now_ns();
        psygfx_begin(&gfx, &f);
        psygfx_draw_n(&gfx, s, n);
        psygfx_end(&gfx);
        b = psyrt_now_ns();
        if (i < MAXF) cpu_us[i] = (double)(b - a) * 1e-3;
    }
    glFinish_();
    report(name, FRAMES < MAXF ? FRAMES : MAXF, (double)(psyrt_now_ns() - t0), FRAMES);
}

/* --cache :mem: keeps the programs in memory: the cache's cost without the
 * file system's */
static struct { uint64_t key[64]; void* p[64]; size_t n[64]; int count; } memc;
static size_t memc_load(void* u, uint64_t key, void* dst, size_t cap) {
    int i;
    (void)u;
    for (i = 0; i < memc.count; i++)
        if (memc.key[i] == key) { if (dst && cap >= memc.n[i]) memcpy(dst, memc.p[i], memc.n[i]); return memc.n[i]; }
    return 0;
}
static int memc_store(void* u, uint64_t key, const void* data, size_t n) {
    (void)u;
    if (memc.count == 64 || !(memc.p[memc.count] = malloc(n))) return -1;
    memcpy(memc.p[memc.count], data, n);
    memc.key[memc.count] = key; memc.n[memc.count++] = n;
    return 0;
}
static const psygfx_cache memc_cache = { memc_load, memc_store, NULL };

static bool reopen(psygfx_format fmt, psygfx_dither dither, int lut) {
    psygfx_desc gd;
    psygfx_close(&gfx);
    memset(&gd, 0, sizeof gd);
    gd.screen = &scr;
    gd.background[0] = gd.background[1] = gd.background[2] = 0.5f;
    psygfx__test_scene32 = fmt == PSYGFX_RGBA32F;   /* a private seam: measured, then removed from the API */
    gd.dither = dither;
    gd.max_draws = 1100;
    gd.cache = cache_desc;
    if (!psygfx_open(&gfx, &gd)) { fprintf(stderr, "gfx_bench: %s\n", psygfx_error(&gfx)); return false; }
    if (lut) {
        static float t[3 * 4096];
        int k;
        for (k = 0; k < 3 * 4096; k++) t[k] = (float)pow((double)(k % 4096) / 4095.0, 1.0 / 2.2);
        psygfx_set_lut(&gfx, t, 4096);
    }
    return true;
}

/* --- uploads ------------------------------------------------------------------ */

static psygfx_buf dot_buf;
static float* dot_xy;
static int dot_n;
static void prep_dots(int frame) {
    int i;
    for (i = 0; i < dot_n; i++) {
        dot_xy[2 * i] = (float)((i * 7919 + frame * 13) % 1800) - 900.0f;
        dot_xy[2 * i + 1] = (float)((i * 104729 + frame * 7) % 1100) - 550.0f;
    }
    psygfx_buffer_update(&gfx, dot_buf, 0, dot_xy, (size_t)dot_n * 8);
}

static psygfx_tex up_tex;
static void* up_data;
static void prep_upload(int frame) {
    (void)frame;
    psygfx_texture_update(&gfx, up_tex, 0, 0, W, H, up_data, 0);
}

/* --- raw GL: the variants psy_gfx.h does not carry ------------------------------ */

typedef unsigned int GLu;
static void (GLCALL *glGenBuffers_)(int, GLu*);
static void (GLCALL *glBindBuffer_)(GLu, GLu);
static void (GLCALL *glBufferData_)(GLu, ptrdiff_t, const void*, GLu);
static void (GLCALL *glBufferSubData_)(GLu, ptrdiff_t, ptrdiff_t, const void*);
static void (GLCALL *glBindBufferRange_)(GLu, GLu, GLu, ptrdiff_t, ptrdiff_t);
static void (GLCALL *glBindTexture_)(GLu, GLu);
static void (GLCALL *glActiveTexture_)(GLu);
static void (GLCALL *glTexSubImage2D_)(GLu, int, int, int, int, int, GLu, GLu, const void*);
static void (GLCALL *glPixelStorei_)(GLu, int);
static void (GLCALL *glUniform4fv_)(int, int, const float*);
static int  (GLCALL *glGetUniformLocation_)(GLu, const char*);
static GLu  (GLCALL *glCreateShader_)(GLu);
static void (GLCALL *glShaderSource_)(GLu, int, const char* const*, const int*);
static void (GLCALL *glCompileShader_)(GLu);
static GLu  (GLCALL *glCreateProgram_)(void);
static void (GLCALL *glAttachShader_)(GLu, GLu);
static void (GLCALL *glLinkProgram_)(GLu);
static void (GLCALL *glUseProgram_)(GLu);
static void (GLCALL *glDrawArrays_)(GLu, int, int);
static void (GLCALL *glGenVertexArrays_)(int, GLu*);
static void (GLCALL *glBindVertexArray_)(GLu);
static void (GLCALL *glBindFramebuffer_)(GLu, GLu);
static void (GLCALL *glViewport_)(int, int, int, int);
static GLu  (GLCALL *glGetUniformBlockIndex_)(GLu, const char*);
static void (GLCALL *glUniformBlockBinding_)(GLu, GLu, GLu);
static void (GLCALL *glEnable_)(GLu);
static void (GLCALL *glDisable_)(GLu);
static void (GLCALL *glBlendFunc_)(GLu, GLu);

/* ISO C converts function pointers by cast; a copy keeps one macro for all. */
static void load_one(void* dst, const char* name) {
    psyscr_proc p = psyscr_gl_proc(&scr, name);
    memcpy(dst, &p, sizeof p);
}
#define LOAD(n) load_one(&n##_, #n)

static void load_raw(void) {
    LOAD(glGenBuffers); LOAD(glBindBuffer); LOAD(glBufferData); LOAD(glBufferSubData); LOAD(glBindBufferRange);
    LOAD(glBindTexture); LOAD(glActiveTexture); LOAD(glTexSubImage2D); LOAD(glPixelStorei); LOAD(glUniform4fv);
    LOAD(glGetUniformLocation); LOAD(glCreateShader); LOAD(glShaderSource); LOAD(glCompileShader);
    LOAD(glCreateProgram); LOAD(glAttachShader); LOAD(glLinkProgram); LOAD(glUseProgram); LOAD(glDrawArrays);
    LOAD(glGenVertexArrays); LOAD(glBindVertexArray); LOAD(glBindFramebuffer); LOAD(glViewport);
    LOAD(glGetUniformBlockIndex); LOAD(glUniformBlockBinding); LOAD(glEnable); LOAD(glDisable); LOAD(glBlendFunc);
}

static GLu raw_program(const char* fs_decl) {
    static char vs[1024], fs[1024];
    const char* pv = vs;
    const char* pf = fs;
    GLu v, f, p;
    snprintf(vs, sizeof vs,
             "#version 300 es\nprecision highp float;\n%s\nflat out vec4 c;\n"
             "void main(){ vec4 q = U(0); vec2 k = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1)) * 2.0 - 1.0;"
             " c = U(1); gl_Position = vec4(q.xy + k * q.zw, 0.0, 1.0); }\n", fs_decl);
    snprintf(fs, sizeof fs, "#version 300 es\nprecision highp float;\nflat in vec4 c; out vec4 o;\nvoid main(){ o = c; }\n");
    v = glCreateShader_(0x8B31); glShaderSource_(v, 1, &pv, NULL); glCompileShader_(v);
    f = glCreateShader_(0x8B30); glShaderSource_(f, 1, &pf, NULL); glCompileShader_(f);
    p = glCreateProgram_(); glAttachShader_(p, v); glAttachShader_(p, f); glLinkProgram_(p);
    return p;
}

/* N draws of a 256 x 256 quad, 16 vec4 of uniforms each: glUniform4fv per
 * draw, against one upload per frame and a bound range per draw. */
static void raw_uniforms(int n) {
    static float u[1024 * 64];
    GLu pu = raw_program("uniform vec4 u[16];\n#define U(i) u[i]");
    GLu pb = raw_program("layout(std140) uniform B { vec4 u[16]; };\n#define U(i) u[i]");
    GLu vao, ubo;
    int loc, i, fr, variant;
    char name[80];
    for (i = 0; i < n * 64; i += 64) {
        u[i] = -0.5f; u[i + 1] = 0.0f; u[i + 2] = 256.0f / (float)W; u[i + 3] = 256.0f / (float)H;
        u[i + 4] = u[i + 5] = u[i + 6] = 0.5f; u[i + 7] = 1.0f;
    }
    glGenVertexArrays_(1, &vao);
    glBindVertexArray_(vao);
    glGenBuffers_(1, &ubo);
    glBindBuffer_(0x8A11, ubo);
    glBufferData_(0x8A11, (ptrdiff_t)sizeof u, NULL, 0x88E8);
    glUniformBlockBinding_(pb, glGetUniformBlockIndex_(pb, "B"), 0);
    loc = glGetUniformLocation_(pu, "u");
    glBindFramebuffer_(0x8D40, 0);
    glViewport_(0, 0, W, H);
    glDisable_(0x0BE2);
    for (variant = 0; variant < 2; variant++) {
        uint64_t t0, a;
        glUseProgram_(variant ? pb : pu);
        glFinish_();
        t0 = psyrt_now_ns();
        for (fr = 0; fr < FRAMES; fr++) {
            a = psyrt_now_ns();
            if (variant) glBufferSubData_(0x8A11, 0, (ptrdiff_t)(n * 256), u);
            for (i = 0; i < n; i++) {
                if (variant) glBindBufferRange_(0x8A11, 0, ubo, (ptrdiff_t)i * 256, 256);
                else glUniform4fv_(loc, 16, u + i * 64);
                glDrawArrays_(0x0005, 0, 4);
            }
            if (fr < MAXF) cpu_us[fr] = (double)(psyrt_now_ns() - a) * 1e-3;
        }
        glFinish_();
        snprintf(name, sizeof name, "raw GL, %d draws: %s", n, variant ? "UBO once + range per draw" : "glUniform4fv per draw");
        report(name, FRAMES < MAXF ? FRAMES : MAXF, (double)(psyrt_now_ns() - t0), FRAMES);
    }
    psygfx_reset_state(&gfx);
}

/* Full-frame RGBA8 upload: glTexSubImage2D from memory against a pixel
 * unpack buffer, both on psy_gfx.h's texture. */
static void raw_pbo(void) {
    GLu pbo;
    int fr, variant;
    for (variant = 0; variant < 2; variant++) {
        uint64_t t0, a;
        if (variant) {
            glGenBuffers_(1, &pbo);
            glBindBuffer_(0x88EC, pbo);
            glBufferData_(0x88EC, (ptrdiff_t)W * H * 4, NULL, 0x88E0);   /* STREAM_DRAW */
        }
        glActiveTexture_(0x84C0);
        glBindTexture_(0x0DE1, ((psygfx__gl*)gfx.bctx)->tex[gfx.tex[up_tex.id - 1].bid - 1].tex);
        glPixelStorei_(0x0CF5, 1);
        glFinish_();
        t0 = psyrt_now_ns();
        for (fr = 0; fr < FRAMES; fr++) {
            a = psyrt_now_ns();
            if (variant) {
                glBufferData_(0x88EC, (ptrdiff_t)W * H * 4, NULL, 0x88E0);
                glBufferSubData_(0x88EC, 0, (ptrdiff_t)W * H * 4, up_data);
                glTexSubImage2D_(0x0DE1, 0, 0, 0, W, H, 0x1908, 0x1401, (const void*)0);
            } else {
                glTexSubImage2D_(0x0DE1, 0, 0, 0, W, H, 0x1908, 0x1401, up_data);
            }
            if (fr < MAXF) cpu_us[fr] = (double)(psyrt_now_ns() - a) * 1e-3;
        }
        glFinish_();
        report(variant ? "upload RGBA8, full frame: PBO (raw GL)" : "upload RGBA8, full frame: glTexSubImage2D (raw GL)",
               FRAMES < MAXF ? FRAMES : MAXF, (double)(psyrt_now_ns() - t0), FRAMES);
        if (variant) glBindBuffer_(0x88EC, 0);
    }
    psygfx_reset_state(&gfx);
}


/* --- v0.2 ------------------------------------------------------------------ */

/* Interleaved comparison. The GPU's clock ramps over seconds, so rows run
 * one after another compare clock states, not workloads (measured: an empty
 * frame first in the run took 1.48 ms, the same frame with an empty target
 * pass later 0.41 ms). Here the workloads take turns in blocks of CMP_BLOCK
 * frames, each block between two glFinish calls; each workload's time is
 * the median over CMP_REPS rounds, and its cost is the median of its
 * difference from workload 0 (an empty frame) in the same round. */
typedef struct {
    const char* name;
    const psygfx_stim* s;          /* drawn into the scene                    */
    int n;
    psygfx_tex t;                  /* a target pass first when pass != 0      */
    const psygfx_stim* in;         /* drawn into that pass                    */
    int n_in;
    int pass;
    void (*prep)(int);
} bench_work;

enum { CMP_MAX = 8, CMP_REPS = 25, CMP_BLOCK = 12 };
static double cmp_gpu[CMP_MAX][CMP_REPS], cmp_cpu[CMP_MAX][CMP_REPS], cmp_tmp[CMP_REPS];

static double median_of(const double* v, int n) {
    int i;
    for (i = 0; i < n; i++) cmp_tmp[i] = v[i];
    qsort(cmp_tmp, (size_t)n, sizeof(double), cmp_d);
    return n % 2 ? cmp_tmp[n / 2] : 0.5 * (cmp_tmp[n / 2 - 1] + cmp_tmp[n / 2]);
}

static void cmp_frame(const bench_work* w, psyscr_frame* f, int i) {
    static const float zero[4] = { 0, 0, 0, 0 };
    if (w->prep) w->prep(i);   /* before begin: an upload must be */
    psygfx_begin(&gfx, f);
    if (w->pass) {
        psygfx_begin_target(&gfx, w->t, zero);
        if (w->n_in) psygfx_draw_n(&gfx, w->in, w->n_in);
        psygfx_end_target(&gfx);
    }
    if (w->n) psygfx_draw_n(&gfx, w->s, w->n);
    psygfx_end(&gfx);
}

static void bench_cmp(const char* title, const bench_work* w, int nw) {
    psyscr_frame f;
    double d[CMP_REPS];
    uint64_t t0, a;
    int r, k, i, frame = 0;
    if (!run(title) || nw > CMP_MAX) return;
    memset(&f, 0, sizeof f);
    f.period = 16666667;
    for (k = 0; k < nw; k++)   /* warm: lazy variants, the clock up */
        for (i = 0; i < 30; i++) cmp_frame(&w[k], &f, frame++);
    for (r = 0; r < CMP_REPS; r++) {
        for (k = 0; k < nw; k++) {
            double cpu = 0;
            glFinish_();
            t0 = psyrt_now_ns();
            for (i = 0; i < CMP_BLOCK; i++) {
                f.index = frame;
                a = psyrt_now_ns();
                cmp_frame(&w[k], &f, frame++);
                cpu += (double)(psyrt_now_ns() - a) * 1e-3;
            }
            glFinish_();
            cmp_gpu[k][r] = (double)(psyrt_now_ns() - t0) * 1e-6 / CMP_BLOCK;
            cmp_cpu[k][r] = cpu / CMP_BLOCK;
        }
    }
    printf("\n%s: %d rounds of %d frames each, interleaved\n\n", title, CMP_REPS, CMP_BLOCK);
    printf("| %-48s | %8s | %8s | %8s | %8s |\n", "workload", "ms", "ms - [0]", "cpu us", "us - [0]");
    printf("|--------------------------------------------------|----------|----------|----------|----------|\n");
    for (k = 0; k < nw; k++) {
        double gm = median_of(cmp_gpu[k], CMP_REPS), cm = median_of(cmp_cpu[k], CMP_REPS), gd, cd;
        for (r = 0; r < CMP_REPS; r++) d[r] = cmp_gpu[k][r] - cmp_gpu[0][r];
        gd = median_of(d, CMP_REPS);
        for (r = 0; r < CMP_REPS; r++) d[r] = cmp_cpu[k][r] - cmp_cpu[0][r];
        cd = median_of(d, CMP_REPS);
        printf("| %-48s | %8.3f | %8.3f | %8.1f | %8.1f |\n", w[k].name, gm, gd, cm, cd);
    }
    fflush(stdout);
}

/* --- v0.4: video ---------------------------------------------------------------- */

static psygfx_tex vid_t[3];
static psygfx_planes vid_p[3];
static void prep_vid_rgba(int frame) { (void)frame; psygfx_texture_update(&gfx, vid_t[0], 0, 0, 1920, 1080, vid_p[0].data[0], 0); }
static void prep_vid_nv12(int frame) { (void)frame; psygfx_texture_update_planes(&gfx, vid_t[1], &vid_p[1]); }

/* The CPU time of one update call, mean and p99 over n calls. */
static void vid_update_cpu(const char* name, int k, int n) {
    int i;
    for (i = 0; i < n && i < MAXF; i++) {
        uint64_t a = psyrt_now_ns();
        if (k == 0) psygfx_texture_update(&gfx, vid_t[0], 0, 0, 1920, 1080, vid_p[0].data[0], 0);
        else psygfx_texture_update_planes(&gfx, vid_t[k], &vid_p[k]);
        cpu_us[i] = (double)(psyrt_now_ns() - a) * 1e-3;
        if (i % 4 == 3) glFinish_();   /* a frame loop's pace, not a queue of uploads */
    }
    glFinish_();
    {
        double mean = 0;
        int m = n < MAXF ? n : MAXF;
        for (i = 0; i < m; i++) mean += cpu_us[i];
        printf("| %-48s | %8.1f | %8.1f |\n", name, mean / m, p99(cpu_us, m));
    }
}

static void bench_v04_video(void) {
    static psygfx_stim st[8];
    bench_work w[CMP_MAX];
    psygfx_texture_desc td;
    psygfx_image_desc idd;
    unsigned char* mem;
    size_t ny = 1920 * 1080;
    int k, i;
    static const char* const tname[] = { "LINEAR", "BT1886", "DEVICE" };
    if (!run("v0.4 video")) return;
    if (W < 1920 || H < 1080) { printf("v0.4 video: needs a 1920 x 1080 scene or larger\n"); return; }
    if (!reopen(scene_fmt, PSYGFX_DITHER_NONE, 0)) return;
    mem = (unsigned char*)malloc(ny * 4 + ny * 3);
    if (!mem) return;
    for (i = 0; i < (int)(ny * 7); i++) mem[i] = (unsigned char)(16 + (i * 37) % 220);
    memset(vid_p, 0, sizeof vid_p);
    vid_p[0].data[0] = mem;                                                         /* RGBA8 */
    vid_p[1].data[0] = mem + ny * 4; vid_p[1].data[1] = mem + ny * 5;               /* NV12 */
    vid_p[2].data[0] = mem + ny * 4; vid_p[2].data[1] = mem + ny * 5; vid_p[2].data[2] = mem + ny * 5 + ny / 4;   /* I420 */
    for (k = 0; k < 3; k++) {
        memset(&td, 0, sizeof td);
        td.w = 1920; td.h = 1080; td.format = k == 0 ? PSYGFX_RGBA8 : (k == 1 ? PSYGFX_NV12 : PSYGFX_I420);
        if (k) {
            td.enc.matrix = PSYGFX_MATRIX_BT709; td.enc.range = PSYGFX_RANGE_LIMITED; td.enc.transfer = PSYGFX_TRC_BT1886;
            td.enc.primaries = PSYGFX_PRIM_DEVICE; td.enc.siting = PSYGFX_SITING_LEFT;
            td.planes = &vid_p[k];
        } else {
            td.data = mem;
        }
        vid_t[k] = psygfx_texture(&gfx, &td);
        if (!vid_t[k].id) { fprintf(stderr, "gfx_bench: %s\n", psygfx_error(&gfx)); free(mem); return; }
    }
    memset(&idd, 0, sizeof idd);
    for (k = 0; k < 3; k++) { idd.tex = vid_t[k]; st[k] = psygfx_image(&gfx, &idd); }
    memset(w, 0, sizeof w);
    w[0].name = "empty frame";
    w[1].name = "RGBA8 1920x1080, draw"; w[1].s = &st[0]; w[1].n = 1;
    w[2].name = "NV12 1920x1080 BT.709 BT1886, draw"; w[2].s = &st[1]; w[2].n = 1;
    w[3].name = "I420 1920x1080 BT.709 BT1886, draw"; w[3].s = &st[2]; w[3].n = 1;
    w[4].name = "RGBA8, upload + draw"; w[4].s = &st[0]; w[4].n = 1; w[4].prep = prep_vid_rgba;
    w[5].name = "NV12, upload planes + draw"; w[5].s = &st[1]; w[5].n = 1; w[5].prep = prep_vid_nv12;
    bench_cmp("v0.4 video, 1080p", w, 6);
    /* the transfers: the same NV12 frame, one texture per transfer */
    {
        psygfx_tex tt[3];
        for (k = 0; k < 3; k++) {
            memset(&td, 0, sizeof td);
            td.w = 1920; td.h = 1080; td.format = PSYGFX_NV12; td.planes = &vid_p[1];
            td.enc.matrix = PSYGFX_MATRIX_BT709; td.enc.range = PSYGFX_RANGE_LIMITED;
            td.enc.transfer = (uint8_t)(k == 0 ? PSYGFX_TRC_LINEAR : (k == 1 ? PSYGFX_TRC_BT1886 : PSYGFX_TRC_DEVICE));
            td.enc.primaries = PSYGFX_PRIM_DEVICE; td.enc.siting = PSYGFX_SITING_LEFT;
            tt[k] = psygfx_texture(&gfx, &td);
            idd.tex = tt[k];
            st[3 + k] = psygfx_image(&gfx, &idd);
        }
        memset(w, 0, sizeof w);
        w[0].name = "empty frame";
        w[1].name = "RGBA8 1920x1080, draw"; w[1].s = &st[0]; w[1].n = 1;
        for (k = 0; k < 3; k++) {
            static char nm[3][64];
            snprintf(nm[k], sizeof nm[k], "NV12 1920x1080, transfer %s, draw", tname[k]);
            w[2 + k].name = nm[k]; w[2 + k].s = &st[3 + k]; w[2 + k].n = 1;
        }
        bench_cmp("v0.4 video transfers, 1080p", w, 5);
        for (k = 0; k < 3; k++) psygfx_texture_free(&gfx, tt[k]);
    }
    printf("\nv0.4 video: the update call's CPU time, 1920 x 1080, 400 calls (a glFinish every 4)\n\n");
    printf("| %-48s | %8s | %8s |\n", "update", "mean us", "p99 us");
    printf("|--------------------------------------------------|----------|----------|\n");
    for (i = 0; i < 2; i++) {   /* A/B/A/B */
        vid_update_cpu("RGBA8, psygfx_texture_update (8.3 MB)", 0, 400);
        vid_update_cpu("NV12, psygfx_texture_update_planes (3.1 MB)", 1, 400);
        vid_update_cpu("I420, psygfx_texture_update_planes (3.1 MB)", 2, 400);
    }
    {   /* rebind: per frame, nothing but a store */
        uint64_t a = psyrt_now_ns();
        for (i = 0; i < 100000; i++) psygfx_texture_rebind(&gfx, vid_t[1], (i & 1) ? vid_t[1] : vid_t[1]);
        printf("\npsygfx_texture_rebind: %.3f us per call (100000 calls)\n", (double)(psyrt_now_ns() - a) * 1e-3 / 100000);
    }
    for (k = 0; k < 3; k++) psygfx_texture_free(&gfx, vid_t[k]);
    free(mem);
}

/* --- v0.4: instances and interleaved kinds ------------------------------------- */

static psygfx_inst bi_el[10000];
static void prep_ring(int frame) { (void)frame; }
static void prep_turn(int frame) {   /* the elements change every frame, as a drifting field */
    int i;
    for (i = 0; i < 10000; i++) bi_el[i].ori = (float)((i * 7 + frame) % 360);
}

static bool open_big(int max_draws) {
    psygfx_desc gd;
    psygfx_close(&gfx);
    memset(&gd, 0, sizeof gd);
    gd.screen = &scr;
    gd.background[0] = gd.background[1] = gd.background[2] = 0.5f;
    psygfx__test_scene32 = 0;
    gd.max_draws = max_draws;
    gd.cache = cache_desc;
    if (!psygfx_open(&gfx, &gd)) { fprintf(stderr, "gfx_bench: %s\n", psygfx_error(&gfx)); return false; }
    return true;
}

static void bench_v04_inst(void) {
    static psygfx_stim plain[10000];
    psygfx_stim inst[6];
    psygfx_instances_desc id;
    psygfx_gabor_desc gd;
    psygfx_shape_desc sd;
    bench_work w[CMP_MAX];
    int i;
    if (!run("v0.4 inst")) return;
    if (!open_big(10100)) return;
    /* 10000 gabors of 32 x 32 on a 100 x 100 grid */
    psygfx_inst_grid(bi_el, 100, 100, 19, 12);
    memset(&gd, 0, sizeof gd);
    gd.sigma = 4; gd.sf = 1 / 8.0f; gd.contrast = 0.2f;
    for (i = 0; i < 10000; i++) {
        gd.x = bi_el[i].x; gd.y = bi_el[i].y; gd.ori = (float)((i * 7) % 360);
        plain[i] = psygfx_gabor(&gd);
        bi_el[i].ori = (float)((i * 7) % 360);
    }
    gd.x = gd.y = 0; gd.ori = 0;
    memset(&id, 0, sizeof id);
    id.inst = bi_el; id.n = 10000; id.fields = PSYGFX_I_XY | PSYGFX_I_ORI;
    inst[0] = psygfx_gabor(&gd);
    inst[0] = psygfx_instances(&gfx, &inst[0], &id);
    memset(&sd, 0, sizeof sd);
    sd.shape = PSYGFX_LINE; sd.w = 16; sd.shape_p[0] = 2; sd.edge = PSYGFX_EDGE_COSINE; sd.edge_width = 1;
    sd.color[0] = sd.color[1] = sd.color[2] = 0.8f;
    inst[1] = psygfx_shape(&sd);
    inst[1] = psygfx_instances(&gfx, &inst[1], &id);
    sd.shape = PSYGFX_RRECT; sd.w = 14; sd.h = 9; sd.shape_p[0] = sd.shape_p[1] = sd.shape_p[2] = sd.shape_p[3] = 3;
    inst[2] = psygfx_shape(&sd);
    inst[2] = psygfx_instances(&gfx, &inst[2], &id);
    if (!inst[0].n_inst || !inst[1].n_inst || !inst[2].n_inst) { fprintf(stderr, "gfx_bench: %s\n", psygfx_error(&gfx)); return; }
    memset(w, 0, sizeof w);
    w[0].name = "empty frame"; w[0].prep = prep_ring;
    w[1].name = "10000 gabors 32x32, one stimulus each"; w[1].s = plain; w[1].n = 10000; w[1].prep = prep_ring;
    w[2].name = "10000 gabors 32x32, instanced, ring"; w[2].s = &inst[0]; w[2].n = 1; w[2].prep = prep_ring;
    w[3].name = "10000 gabors, instanced, ori set each frame"; w[3].s = &inst[0]; w[3].n = 1; w[3].prep = prep_turn;
    w[4].name = "10000 LINE 16 px, instanced"; w[4].s = &inst[1]; w[4].n = 1; w[4].prep = prep_ring;
    w[5].name = "10000 RRECT 14x9 (vector), instanced"; w[5].s = &inst[2]; w[5].n = 1; w[5].prep = prep_ring;
    bench_cmp("v0.4 inst, 10000 elements", w, 6);
    {   /* 1000 gabors of 256 x 256: the GPU should not care */
        static psygfx_stim big[1000];
        psygfx_inst_grid(bi_el, 40, 25, 45, 45);
        gd.sigma = 32; gd.sf = 1 / 32.0f;
        for (i = 0; i < 1000; i++) { gd.x = bi_el[i].x; gd.y = bi_el[i].y; big[i] = psygfx_gabor(&gd); }
        gd.x = gd.y = 0;
        id.n = 1000; id.fields = PSYGFX_I_XY;
        inst[3] = psygfx_gabor(&gd);
        inst[3] = psygfx_instances(&gfx, &inst[3], &id);
        memset(w, 0, sizeof w);
        w[0].name = "empty frame"; w[0].prep = prep_ring;
        w[1].name = "1000 gabors 256x256, one stimulus each"; w[1].s = big; w[1].n = 1000; w[1].prep = prep_ring;
        w[2].name = "1000 gabors 256x256, instanced"; w[2].s = &inst[3]; w[2].n = 1; w[2].prep = prep_ring;
        bench_cmp("v0.4 inst, 1000 large gabors", w, 3);
    }
    {   /* the hit test over 10000 rects: the target in a search array */
        uint64_t t0;
        int hits = 0;
        id.n = 10000; id.fields = PSYGFX_I_XY | PSYGFX_I_ORI;
        psygfx_inst_grid(bi_el, 100, 100, 19, 12);
        sd.shape = PSYGFX_RECT; sd.w = 14; sd.h = 4;
        inst[4] = psygfx_shape(&sd);
        inst[4] = psygfx_instances(&gfx, &inst[4], &id);
        t0 = psyrt_now_ns();
        for (i = 0; i < 100; i++) hits += psygfx_hit_index(&gfx, &inst[4], 960.0f + (float)(i % 10), 600.0f + (float)(i / 10)) >= 0;
        printf("\npsygfx_hit_index over 10000 RECT elements: %.1f us per call (100 calls, %d hits)\n",
               (double)(psyrt_now_ns() - t0) * 1e-3 / 100, hits);
    }
}

static void prep_order(int frame) { (void)frame; gfx.no_reorder = 0; }
static void prep_call_order(int frame) { (void)frame; gfx.no_reorder = 1; }

/* Interleaved kinds: 1000 stimuli, four kinds in turn, against the same
 * stimuli in groups of one kind (what batching gets today). */
static void bench_v04_mixed(void) {
    static psygfx_stim inter[1000], grouped[1000], over[1000], dense[1000];
    psygfx_gabor_desc gd;
    psygfx_shape_desc sd;
    psygfx_grating_desc rd;
    bench_work w[CMP_MAX];
    int i, k, n = 0;
    if (!run("v0.4 mixed")) return;
    if (!open_big(1100)) return;
    psygfx_inst_grid(bi_el, 40, 25, 46, 46);
    for (i = 0; i < 1000; i++) {
        float x = bi_el[i].x, y = bi_el[i].y;
        switch (i % 4) {
        case 0:
            memset(&gd, 0, sizeof gd);
            gd.x = x; gd.y = y; gd.sigma = 5; gd.sf = 1 / 8.0f; gd.contrast = 0.2f; gd.ori = (float)(i % 180);
            inter[i] = psygfx_gabor(&gd);
            break;
        case 1:
            memset(&sd, 0, sizeof sd);
            sd.shape = PSYGFX_CIRCLE; sd.x = x; sd.y = y; sd.w = 30; sd.edge = PSYGFX_EDGE_COSINE; sd.edge_width = 1.5f;
            sd.color[0] = 0.8f; sd.color[1] = 0.3f; sd.color[2] = 0.2f;
            inter[i] = psygfx_shape(&sd);
            break;
        case 2:
            memset(&rd, 0, sizeof rd);
            rd.x = x; rd.y = y; rd.w = 32; rd.sf = 1 / 6.0f; rd.contrast = 0.2f; rd.aperture = PSYGFX_CIRCLE;
            inter[i] = psygfx_grating(&rd);
            break;
        default:
            memset(&sd, 0, sizeof sd);
            sd.shape = PSYGFX_RRECT; sd.x = x; sd.y = y; sd.w = 34; sd.h = 20;
            sd.shape_p[0] = sd.shape_p[1] = sd.shape_p[2] = sd.shape_p[3] = 5;
            sd.color[0] = 0.2f; sd.color[1] = 0.4f; sd.color[2] = 0.8f;
            inter[i] = psygfx_shape(&sd);
            break;
        }
    }
    for (k = 0; k < 4; k++) for (i = k; i < 1000; i += 4) grouped[n++] = inter[i];
    /* 10 % overlap their neighbor: every tenth moved onto the next cell */
    for (i = 0; i < 1000; i++) { over[i] = inter[i]; if (i % 10 == 0) over[i].x += 30.0f; }
    /* dense: random places, most draws overlap something (the reorder's
     * cost with little to gain) */
    for (i = 0; i < 1000; i++) {
        dense[i] = inter[i];
        dense[i].x = (float)((i * 7919) % 1800) - 900.0f;
        dense[i].y = (float)((i * 104729) % 1100) - 550.0f;
    }
    memset(w, 0, sizeof w);
    w[0].name = "empty frame"; w[0].prep = prep_order;
    w[1].name = "1000, 4 kinds interleaved, call order"; w[1].s = inter; w[1].n = 1000; w[1].prep = prep_call_order;
    w[2].name = "the same, reordered"; w[2].s = inter; w[2].n = 1000; w[2].prep = prep_order;
    w[3].name = "the same, grouped by kind by the caller"; w[3].s = grouped; w[3].n = 1000; w[3].prep = prep_call_order;
    w[4].name = "10 % overlapping, call order"; w[4].s = over; w[4].n = 1000; w[4].prep = prep_call_order;
    w[5].name = "10 % overlapping, reordered"; w[5].s = over; w[5].n = 1000; w[5].prep = prep_order;
    w[6].name = "random places (dense), call order"; w[6].s = dense; w[6].n = 1000; w[6].prep = prep_call_order;
    w[7].name = "random places (dense), reordered"; w[7].s = dense; w[7].n = 1000; w[7].prep = prep_order;
    bench_cmp("v0.4 mixed kinds", w, 8);
    gfx.no_reorder = 0;
}

static psygfx_group bench_group;
static void prep_group(int frame) { bench_group.x = (float)(frame % 200) - 100.0f; }

static void bench_v02(psygfx_stim* g) {
    static psygfx_stim st[8], sprites[1000], members[100], rects[2];
    static float pent[10];
    static unsigned char atlas[512 * 512 * 4];
    enum { M = 256, P = 16 };
    static uint8_t mask[M * M];
    static float sdf[(M + 2 * P) * (M + 2 * P)];
    bench_work w[CMP_MAX];
    psygfx_shape_desc sd;
    psygfx_texture_desc td;
    psygfx_target_desc rd;
    psygfx_image_desc idd;
    psygfx_group_desc gd;
    psygfx_tex mtex, atex, t;
    int i, x, y;

    /* strokes against fills: a large circle and a large pentagon */
    memset(&sd, 0, sizeof sd);
    sd.shape = PSYGFX_CIRCLE; sd.w = (float)(H - 40); sd.edge = PSYGFX_EDGE_COSINE; sd.edge_width = 2;
    sd.color[0] = sd.color[1] = sd.color[2] = 0.6f;
    st[0] = psygfx_shape(&sd);
    sd.stroke = 8;
    st[1] = psygfx_shape(&sd);
    for (i = 0; i < 5; i++) {
        pent[2 * i] = (float)(cos(i * 2 * 3.14159265358979 / 5) * 0.48 * (H - 40));
        pent[2 * i + 1] = (float)(sin(i * 2 * 3.14159265358979 / 5) * 0.48 * (H - 40));
    }
    sd.shape = PSYGFX_POLYGON; sd.stroke = 0; sd.shape_p[0] = 5; sd.vertices = pent; sd.h = sd.w;
    st[2] = psygfx_shape(&sd);
    sd.stroke = 8; sd.join = PSYGFX_JOIN_MITER;
    st[3] = psygfx_shape(&sd);
    sd.join = PSYGFX_JOIN_ROUND;
    st[4] = psygfx_shape(&sd);
    /* a mask of the same circle */
    for (y = 0; y < M; y++)
        for (x = 0; x < M; x++)
            mask[y * M + x] = (uint8_t)(((x - 128) * (x - 128) + (y - 128) * (y - 128) <= 120 * 120) ? 255 : 0);
    psygfx_sdf_from_mask(sdf, mask, M, M, P);
    memset(&td, 0, sizeof td);
    td.w = td.h = M + 2 * P; td.format = PSYGFX_R16F; td.data = sdf;
    mtex = psygfx_texture(&gfx, &td);
    memset(&sd, 0, sizeof sd);
    sd.shape = PSYGFX_MASK_TEX; sd.mask = mtex; sd.w = sd.h = (float)(H - 40);
    sd.edge = PSYGFX_EDGE_COSINE; sd.edge_width = 2;
    sd.color[0] = sd.color[1] = sd.color[2] = 0.6f;
    st[5] = psygfx_shape(&sd);
    /* a large rect, filled and with a stroke beveled below the limit */
    memset(&sd, 0, sizeof sd);
    sd.w = (float)(W - 40); sd.h = (float)(H - 40); sd.edge = PSYGFX_EDGE_COSINE; sd.edge_width = 2;
    sd.color[0] = sd.color[1] = sd.color[2] = 0.6f;
    rects[0] = psygfx_shape(&sd);
    sd.stroke = 8; sd.join = PSYGFX_JOIN_MITER; sd.miter_limit = 1.2f;
    rects[1] = psygfx_shape(&sd);
    memset(w, 0, sizeof w);
    w[0].name = "empty frame";
    w[1].name = "circle, fill";                           w[1].s = &st[0]; w[1].n = 1;
    w[2].name = "circle, 8 px stroke";                    w[2].s = &st[1]; w[2].n = 1;
    w[3].name = "pentagon, fill";                         w[3].s = &st[2]; w[3].n = 1;
    w[4].name = "pentagon, 8 px stroke, MITER";           w[4].s = &st[3]; w[4].n = 1;
    w[5].name = "pentagon, 8 px stroke, ROUND";           w[5].s = &st[4]; w[5].n = 1;
    w[6].name = "MASK_TEX circle (256 + 32 texels), fill"; w[6].s = &st[5]; w[6].n = 1;
    w[7].name = "rect, fill";                             w[7].s = &rects[0]; w[7].n = 1;
    bench_cmp("v0.2 strokes and masks", w, 8);
    w[1].name = "rect, 8 px stroke, MITER, limit 1.2";    w[1].s = &rects[1]; w[1].n = 1;
    w[2] = w[7];
    bench_cmp("v0.2 strokes, beveled rect", w, 3);
    psygfx_texture_free(&gfx, mtex);

    /* 1000 sprites of 32 x 32 from one 512 x 512 atlas */
    for (i = 0; i < 512 * 512 * 4; i++) atlas[i] = (unsigned char)(i * 13);
    memset(&td, 0, sizeof td);
    td.w = td.h = 512; td.format = PSYGFX_RGBA8; td.data = atlas;
    atex = psygfx_texture(&gfx, &td);
    for (i = 0; i < 1000; i++) {
        memset(&idd, 0, sizeof idd);
        idd.tex = atex; idd.src[0] = (float)((i % 16) * 32); idd.src[1] = (float)(((i / 16) % 16) * 32);
        idd.src[2] = idd.src[3] = 32;
        idd.x = (float)((i * 37) % 1800) - 900.0f; idd.y = (float)((i * 53) % 1100) - 550.0f;
        sprites[i] = psygfx_image(&gfx, &idd);
    }
    /* a group of 100 gabors moved each frame by a bound x, at scale 1 */
    memset(&gd, 0, sizeof gd);
    gd.ori = 10;
    bench_group = psygfx_group_make(&gd);
    for (i = 0; i < 100; i++) { members[i] = g[i]; members[i].group = &bench_group; }
    memset(w, 0, sizeof w);
    w[0].name = "empty frame";
    w[1].name = "1000 sprites 32 x 32, one atlas";        w[1].s = sprites; w[1].n = 1000;
    w[2].name = "100 gabors, no group";                   w[2].s = g; w[2].n = 100;
    w[3].name = "the same 100 gabors in a group, x moved"; w[3].s = members; w[3].n = 100; w[3].prep = prep_group;
    bench_cmp("v0.2 sprites and groups", w, 4);
    psygfx_texture_free(&gfx, atex);

    /* targets: a full-screen RGBA16F target; 1000 gabors drawn into it once
     * and added each frame, against drawing them each frame */
    memset(&rd, 0, sizeof rd);
    rd.w = W; rd.h = H;
    t = psygfx_target(&gfx, &rd);
    {
        psyscr_frame f;
        static const float zero[4] = { 0, 0, 0, 0 };
        memset(&f, 0, sizeof f);
        psygfx_begin(&gfx, &f);
        psygfx_begin_target(&gfx, t, zero);
        psygfx_draw_n(&gfx, g, 1000);
        psygfx_end_target(&gfx);
        psygfx_end(&gfx);
    }
    memset(&idd, 0, sizeof idd);
    idd.tex = t; idd.add = true;
    st[6] = psygfx_image(&gfx, &idd);
    memset(&idd, 0, sizeof idd);
    idd.tex = t;
    st[7] = psygfx_image(&gfx, &idd);
    memset(w, 0, sizeof w);
    w[0].name = "empty frame";
    w[1].name = "an empty target pass (cleared)";         w[1].t = t; w[1].pass = 1;
    w[2].name = "full-screen target composite, COLOR";    w[2].s = &st[7]; w[2].n = 1;
    w[3].name = "full-screen target composite, ADD";      w[3].s = &st[6]; w[3].n = 1;
    w[4].name = "1000 gabors drawn each frame";           w[4].s = g; w[4].n = 1000;
    bench_cmp("v0.2 targets", w, 5);
    {   /* The pass switch: 16 passes a frame, each a small shape into the
         * target and one into the scene between them, against the same 32
         * shapes all in the scene. Each pass then costs two framebuffer
         * binds, two viewports and a frame block range. */
        psyscr_frame f;
        psygfx_stim dot;
        double cpu[CMP_REPS], base[CMP_REPS], gpu[CMP_REPS], gbase[CMP_REPS];
        uint64_t a0, t0;
        int r, k;
        memset(&sd, 0, sizeof sd);
        sd.w = sd.h = 8; sd.color[0] = sd.color[1] = sd.color[2] = 0.6f;
        dot = psygfx_shape(&sd);
        memset(&f, 0, sizeof f);
        for (r = 0; r < CMP_REPS; r++) {
            for (k = 0; k < 2; k++) {
                double c = 0;
                glFinish_();
                t0 = psyrt_now_ns();
                for (i = 0; i < CMP_BLOCK; i++) {
                    int q;
                    a0 = psyrt_now_ns();
                    psygfx_begin(&gfx, &f);
                    for (q = 0; q < PSYGFX_MAX_PASSES; q++) {
                        if (k) psygfx_begin_target(&gfx, t, NULL);
                        psygfx_draw(&gfx, &dot);
                        if (k) psygfx_end_target(&gfx);
                        psygfx_draw(&gfx, &dot);
                    }
                    psygfx_end(&gfx);
                    c += (double)(psyrt_now_ns() - a0) * 1e-3;
                }
                glFinish_();
                (k ? gpu : gbase)[r] = (double)(psyrt_now_ns() - t0) * 1e-3 / CMP_BLOCK;
                (k ? cpu : base)[r] = c / CMP_BLOCK;
            }
        }
        for (r = 0; r < CMP_REPS; r++) {
            cpu[r] = (cpu[r] - base[r]) / PSYGFX_MAX_PASSES;
            gpu[r] = (gpu[r] - gbase[r]) / PSYGFX_MAX_PASSES;
        }
        printf("\nv0.2 pass switch: %.1f us of CPU and %.1f us of frame time per target pass (median of %d rounds, "
               "%d passes a frame, each between scene draws, against the same draws with no pass)\n",
               median_of(cpu, CMP_REPS), median_of(gpu, CMP_REPS), CMP_REPS, PSYGFX_MAX_PASSES);
    }
    psygfx_texture_free(&gfx, t);
}

/* --- v0.3 ------------------------------------------------------------------ */

static void bench_v03(void) {
    static psygfx_stim st[8], comp[6], many[100], glyphs;
    static psygfx_prim pr[6][32], sprims[100][4];
    static psycol_cal cal;
    static psygfx_paint rgbp, okp;
    static psygfx_fx fx;
    static float msdf[64 * 64 * 4];
    static psygfx_glyph gl[200];
    static const float xy[4][2] = { { 0.64f, 0.33f }, { 0.30f, 0.60f }, { 0.15f, 0.06f }, { 0.3127f, 0.3290f } };
    bench_work w[CMP_MAX];
    psygfx_shape_desc sd;
    psygfx_compound_desc cd;
    psygfx_desc gd;
    psygfx_texture_desc td;
    psygfx_glyphs_desc gld;
    psygfx_tex at;
    psygfx_buf gb;
    double open_ms[3];
    int i, k;
    /* open time with every program, three times */
    psycol_cal_nominal(&cal, xy, 80.0f, 2.2);
    for (k = 0; k < 3; k++) {
        uint64_t t0;
        psygfx_close(&gfx);
        memset(&gd, 0, sizeof gd);
        gd.screen = &scr; gd.cal = &cal; gd.max_draws = 4096;
        gd.background[0] = gd.background[1] = gd.background[2] = 0.5f;
        t0 = psyrt_now_ns();
        if (!psygfx_open(&gfx, &gd)) { fprintf(stderr, "gfx_bench: %s\n", psygfx_error(&gfx)); return; }
        open_ms[k] = (double)(psyrt_now_ns() - t0) * 1e-6;
    }
    printf("\nv0.3 psygfx_open (10 programs, the vector and glyph programs included): %.0f, %.0f, %.0f ms\n",
           open_ms[0], open_ms[1], open_ms[2]);

    /* single primitives against v0.2's rect, near full screen */
    memset(&sd, 0, sizeof sd);
    sd.w = (float)(W - 40); sd.h = (float)(H - 40); sd.edge = PSYGFX_EDGE_COSINE; sd.edge_width = 2;
    sd.color[0] = sd.color[1] = sd.color[2] = 0.6f; sd.shape_p[1] = 40;
    st[0] = psygfx_shape(&sd);                                      /* v0.2 rounded RECT */
    sd.shape = PSYGFX_RRECT; sd.shape_p[0] = sd.shape_p[1] = sd.shape_p[2] = sd.shape_p[3] = 40;
    st[1] = psygfx_shape(&sd);
    memset(&rgbp, 0, sizeof rgbp);
    rgbp.kind = PSYGFX_PAINT_LINEAR; rgbp.n = 3; rgbp.x0 = (float)(-W / 2); rgbp.x1 = (float)(W / 2);
    rgbp.stops[0].color[0] = 0.8f; rgbp.stops[1].t = 0.5f; rgbp.stops[1].color[1] = 0.8f; rgbp.stops[2].t = 1; rgbp.stops[2].color[2] = 0.8f;
    okp = rgbp; okp.space = PSYGFX_SPACE_OKLAB;
    sd.paint = &rgbp; st[2] = psygfx_shape(&sd);
    sd.paint = &okp; st[3] = psygfx_shape(&sd);
    sd.paint = NULL;
    memset(&sd, 0, sizeof sd);
    sd.edge = PSYGFX_EDGE_COSINE; sd.edge_width = 2; sd.color[0] = sd.color[1] = sd.color[2] = 0.6f;
    sd.shape = PSYGFX_STAR; sd.w = (float)(H - 40); sd.shape_p[0] = 5; sd.shape_p[1] = 0.5f; sd.shape_p[2] = 10;
    st[4] = psygfx_shape(&sd);
    sd.shape = PSYGFX_ELLIPSE; sd.w = (float)(W - 40); sd.h = (float)(H - 40);
    st[5] = psygfx_shape(&sd);
    sd.shape = PSYGFX_CIRCLE; sd.w = sd.h = (float)(H - 40); sd.stroke = 8;
    st[6] = psygfx_shape(&sd);
    sd.dash[0] = 30; sd.dash[1] = 15;
    st[7] = psygfx_shape(&sd);
    memset(w, 0, sizeof w);
    w[0].name = "empty frame";
    w[1].name = "v0.2 rounded rect, fill";               w[1].s = &st[0]; w[1].n = 1;
    w[2].name = "RRECT (vector program), fill";          w[2].s = &st[1]; w[2].n = 1;
    w[3].name = "RRECT, linear paint, RGB";              w[3].s = &st[2]; w[3].n = 1;
    w[4].name = "RRECT, linear paint, OKLAB";            w[4].s = &st[3]; w[4].n = 1;
    w[5].name = "STAR 5, rounded, fill";                 w[5].s = &st[4]; w[5].n = 1;
    w[6].name = "ELLIPSE, fill";                         w[6].s = &st[5]; w[6].n = 1;
    w[7].name = "v0.2 circle, 8 px stroke";              w[7].s = &st[6]; w[7].n = 1;
    bench_cmp("v0.3 primitives", w, 8);
    w[1] = w[7];
    w[2].name = "circle, 8 px stroke, dashed";           w[2].s = &st[7]; w[2].n = 1;
    bench_cmp("v0.3 dashes", w, 3);

    /* compounds: n circles of 300 px over the screen, smooth union; then 8
     * with every effect in a 600 x 400 box */
    for (k = 0; k < 4; k++) {
        int n = k == 0 ? 1 : (k == 1 ? 2 : (k == 2 ? 8 : 32));
        for (i = 0; i < n; i++) {
            memset(&pr[k][i], 0, sizeof pr[k][i]);
            pr[k][i].shape = PSYGFX_CIRCLE; pr[k][i].w = 300; pr[k][i].op = PSYGFX_OP_SMOOTH_UNION; pr[k][i].k = 40;
            pr[k][i].x = (float)((i * 397) % (W - 300)) - (W - 300) / 2.0f;
            pr[k][i].y = (float)((i * 211) % (H - 300)) - (H - 300) / 2.0f;
        }
        memset(&cd, 0, sizeof cd);
        cd.prims = pr[k]; cd.n = n; cd.w = (float)W; cd.h = (float)H; cd.edge = PSYGFX_EDGE_COSINE; cd.edge_width = 2;
        cd.color[0] = cd.color[1] = cd.color[2] = 0.6f;
        comp[k] = psygfx_compound(&cd);
    }
    for (i = 0; i < 8; i++) {
        memset(&pr[4][i], 0, sizeof pr[4][i]);
        pr[4][i].shape = PSYGFX_CIRCLE; pr[4][i].w = 120; pr[4][i].op = PSYGFX_OP_SMOOTH_UNION; pr[4][i].k = 30;
        pr[4][i].x = (float)(i % 4 * 140 - 210); pr[4][i].y = (float)(i / 4 * 160 - 80);
    }
    memset(&fx, 0, sizeof fx);
    fx.dx = 8; fx.dy = 8; fx.drop.sigma = 8; fx.drop.opacity = 0.5f; fx.glow.sigma = 6; fx.glow.opacity = 0.4f;
    fx.inner.sigma = 4; fx.inner.opacity = 0.5f; fx.band[0].a1 = -2; fx.band[0].opacity = 1; fx.band[1].a1 = 3; fx.band[1].a2 = 5; fx.band[1].opacity = 1;
    memset(&cd, 0, sizeof cd);
    cd.prims = pr[4]; cd.n = 8; cd.w = 600; cd.h = 400; cd.edge = PSYGFX_EDGE_COSINE; cd.edge_width = 2;
    cd.color[0] = cd.color[1] = cd.color[2] = 0.6f;
    comp[4] = psygfx_compound(&cd);
    cd.fx = &fx;
    comp[5] = psygfx_compound(&cd);
    memset(w, 0, sizeof w);
    w[0].name = "empty frame";
    w[1].name = "compound, 1 circle, full screen";        w[1].s = &comp[0]; w[1].n = 1;
    w[2].name = "compound, 2 circles";                    w[2].s = &comp[1]; w[2].n = 1;
    w[3].name = "compound, 8 circles";                    w[3].s = &comp[2]; w[3].n = 1;
    w[4].name = "compound, 32 circles";                   w[4].s = &comp[3]; w[4].n = 1;
    w[5].name = "compound of 8, 600 x 400 box";           w[5].s = &comp[4]; w[5].n = 1;
    w[6].name = "the same with every effect";             w[6].s = &comp[5]; w[6].n = 1;
    bench_cmp("v0.3 compounds", w, 7);

    /* the CPU: 100 small compounds (one draw each) against 100 rects
     * (batched); a run of 200 MSDF glyphs */
    for (i = 0; i < 100; i++) {
        for (k = 0; k < 4; k++) {
            memset(&sprims[i][k], 0, sizeof sprims[i][k]);
            sprims[i][k].shape = PSYGFX_CIRCLE; sprims[i][k].w = 16; sprims[i][k].x = (float)(k * 10 - 15);
            sprims[i][k].op = PSYGFX_OP_SMOOTH_UNION; sprims[i][k].k = 4;
        }
        memset(&cd, 0, sizeof cd);
        cd.prims = sprims[i]; cd.n = 4; cd.w = 60; cd.h = 20; cd.color[0] = 0.6f; cd.edge = PSYGFX_EDGE_COSINE; cd.edge_width = 1;
        cd.x = (float)((i * 37) % 1800) - 900.0f; cd.y = (float)((i * 53) % 1100) - 550.0f;
        many[i] = psygfx_compound(&cd);
    }
    for (i = 0; i < 64 * 64; i++) {
        double x = i % 64 + 0.5 - 32, y = i / 64 + 0.5 - 32, d = sqrt(x * x + y * y) - 20, v = 0.5 - d / 16;
        msdf[4 * i] = msdf[4 * i + 1] = msdf[4 * i + 2] = msdf[4 * i + 3] = (float)(v < 0 ? 0 : (v > 1 ? 1 : v));
    }
    memset(&td, 0, sizeof td);
    td.w = td.h = 64; td.format = PSYGFX_RGBA16F; td.data = msdf; td.sdf = PSYGFX_SDF_MSDF; td.sdf_range = 16;
    at = psygfx_texture(&gfx, &td);
    for (i = 0; i < 200; i++) { memset(&gl[i], 0, sizeof gl[i]); gl[i].x = (float)(i % 40 * 40); gl[i].y = (float)(i / 40 * 40); gl[i].sw = gl[i].sh = 64; }
    gb = psygfx_buffer(&gfx, sizeof gl);
    psygfx_buffer_update(&gfx, gb, 0, gl, sizeof gl);
    memset(&gld, 0, sizeof gld);
    gld.atlas = at; gld.buf = gb; gld.count = 200; gld.scale = 0.6f; gld.w = 1600; gld.h = 220;
    gld.edge = PSYGFX_EDGE_COSINE; gld.edge_width = 1; gld.color[0] = 0.9f;
    glyphs = psygfx_glyphs(&gld);
    memset(w, 0, sizeof w);
    w[0].name = "empty frame";
    w[1].name = "100 compounds of 4, 60 x 20 (one draw each)"; w[1].s = many; w[1].n = 100;
    w[2].name = "a run of 200 MSDF glyphs, 38 px";             w[2].s = &glyphs; w[2].n = 1;
    bench_cmp("v0.3 CPU and glyphs", w, 3);
    psygfx_buffer_free(&gfx, gb);
    psygfx_texture_free(&gfx, at);
}

int main(int argc, char** argv) {
    psyscr_desc d;
    psygfx_stim g[1000], one;
    psygfx_gabor_desc gd;
    int i;
    char line[400];
    hl.device = PSYGFX_HL_HARDWARE;
#if !defined(_WIN32)
    hl.device = PSYGFX_HL_MESA;
#endif
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--device") && i + 1 < argc) {
            const char* v = argv[++i];
            if (!strcmp(v, "hardware")) hl.device = PSYGFX_HL_HARDWARE;
            else if (!strcmp(v, "warp")) hl.device = PSYGFX_HL_WARP;
            else if (!strcmp(v, "swiftshader")) hl.device = PSYGFX_HL_SWIFTSHADER;
            else if (!strcmp(v, "mesa")) hl.device = PSYGFX_HL_MESA;
            else if (!strcmp(v, "llvmpipe")) hl.device = PSYGFX_HL_MESA_SOFTWARE;
            else { fprintf(stderr, "gfx_bench: unknown device %s\n", v); return 2; }
        } else if (!strcmp(argv[i], "--size") && i + 2 < argc) {
            W = atoi(argv[++i]); H = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            FRAMES = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--only") && i + 1 < argc) {
            only = argv[++i];
        } else if (!strcmp(argv[i], "--cache") && i + 1 < argc) {
            i++;
            cache_desc = strcmp(argv[i], ":mem:") ? psygfx_file_cache_init(&pcache, argv[i]) : &memc_cache;
        } else if (!strcmp(argv[i], "--open-only")) {
            open_only = 1;
        } else if (!strcmp(argv[i], "--scene") && i + 1 < argc) {
            i++;
            scene_fmt = !strcmp(argv[i], "16") ? PSYGFX_RGBA16F : (!strcmp(argv[i], "32") ? PSYGFX_RGBA32F : PSYGFX_FORMAT_NONE);
        } else {
            fprintf(stderr, "usage: gfx_bench [--device hardware|warp|swiftshader|mesa|llvmpipe] [--size W H] "
                            "[--frames N] [--only NAME] [--scene 16|32] [--cache DIR] [--open-only]\n");
            return 2;
        }
    }
    if (W < 256 || H < 256 || FRAMES < 10 || FRAMES > MAXF) { fprintf(stderr, "gfx_bench: bad size or frames\n"); return 2; }
    hl.w = W; hl.h = H;
    memset(&d, 0, sizeof d);
    d.backend = PSYSCR_BACKEND_CUSTOM;
    d.presenter = &psygfx_headless_presenter;
    d.presenter_ctx = &hl;
    d.sim_period_ns = 1000000;
    if (!psyscr_open(&scr, &d)) { fprintf(stderr, "gfx_bench: %s\n", psyscr_error(&scr)); return 1; }
    load_one(&glFinish_, "glFinish");
    load_raw();
    {
        uint64_t t0 = psyrt_now_ns();
        if (!reopen(scene_fmt, PSYGFX_DITHER_NONE, 0)) return 1;
        printf("psygfx_open (all built-in programs): %.0f ms, batch %d stimuli\n",
               (double)(psyrt_now_ns() - t0) * 1e-6, PSYGFX__BATCH);
        if (open_only) {
            psygfx_programs ps;
            psygfx_program_stats(&gfx, &ps);
            printf("open_ms %.1f internal_ms %.1f loaded %u compiled %u rejected %u stored %u store_ms %.1f\n",
                   (double)(psyrt_now_ns() - t0) * 1e-6, (double)ps.open_ns * 1e-6, ps.loaded, ps.compiled,
                   ps.rejected, ps.stored, (double)ps.store_ns * 1e-6);
            psygfx_close(&gfx);
            psyscr_close(&scr);
            return 0;
        }
    }
    psygfx_describe(&gfx, line, sizeof line);
    printf("%s\n%d x %d, %d frames per row\n\n", line, W, H, FRAMES);
    printf("| %-52s | %9s | %9s | %9s |\n", "workload", "cpu us", "cpu p99", "gpu ms");
    printf("|%s|%s|%s|%s|\n", "------------------------------------------------------", "-----------", "-----------", "-----------");

    /* the output stage and the scene format */
    bench("empty frame, RGBA16F scene, identity CLUT", NULL, 0, NULL, 0);
    if (reopen(PSYGFX_RGBA16F, PSYGFX_DITHER_ORDERED, 1)) bench("empty frame, RGBA16F, CLUT 4096, ordered dither", NULL, 0, NULL, 0);
    if (reopen(PSYGFX_RGBA32F, PSYGFX_DITHER_NONE, 0)) bench("empty frame, RGBA32F scene, identity CLUT", NULL, 0, NULL, 0);
    if (reopen(PSYGFX_RGBA32F, PSYGFX_DITHER_ORDERED, 1)) bench("empty frame, RGBA32F, CLUT 4096, ordered dither", NULL, 0, NULL, 0);
    if (run("bare")) {   /* the floor: a clear of framebuffer 0 and nothing else */
        uint64_t t0;
        void (GLCALL *clear)(GLu) = NULL;
        load_one(&clear, "glClear");
        glBindFramebuffer_(0x8D40, 0);
        glFinish_();
        t0 = psyrt_now_ns();
        for (i = 0; i < FRAMES; i++) { uint64_t a = psyrt_now_ns(); clear(0x4000); cpu_us[i] = (double)(psyrt_now_ns() - a) * 1e-3; }
        glFinish_();
        report("bare: clear framebuffer 0 only (raw GL)", FRAMES, (double)(psyrt_now_ns() - t0), FRAMES);
        psygfx_reset_state(&gfx);
    }
    if (!reopen(scene_fmt, PSYGFX_DITHER_NONE, 0)) return 1;

    /* gabors, batched and one draw each */
    memset(&gd, 0, sizeof gd);
    gd.sigma = 32; gd.sf = 1 / 32.0f; gd.contrast = 0.2f;   /* 256 x 256 */
    for (i = 0; i < 1000; i++) {
        gd.x = (float)((i * 37) % 1600) - 800.0f;
        gd.y = (float)((i * 53) % 900) - 450.0f;
        g[i] = psygfx_gabor(&gd);
    }
    {
        static const int ns[4] = { 1, 10, 100, 1000 };
        int k;
        for (k = 0; k < 4; k++) {
            char name[80];
            snprintf(name, sizeof name, "gabors 256x256 x %d, batched", ns[k]);
            gfx.max_batch = PSYGFX__BATCH;
            bench(name, g, ns[k], NULL, 0);
            snprintf(name, sizeof name, "gabors 256x256 x %d, one draw each", ns[k]);
            gfx.max_batch = 1;
            bench(name, g, ns[k], NULL, 0);
        }
        gfx.max_batch = PSYGFX__BATCH;
    }
    if (run("raw")) { raw_uniforms(100); raw_uniforms(1000); }

    /* dots, positions uploaded every frame */
    {
        static const int ns[3] = { 1000, 10000, 100000 };
        int k;
        dot_xy = (float*)malloc(100000 * 8);
        dot_buf = psygfx_buffer(&gfx, 100000 * 8);
        for (k = 0; k < 3; k++) {
            psygfx_dots_desc dd;
            char name[80];
            memset(&dd, 0, sizeof dd);
            dd.buf = dot_buf; dd.count = (uint32_t)ns[k]; dd.dot_size = 4; dd.edge = PSYGFX_EDGE_COSINE; dd.edge_width = 1;
            dd.color[0] = dd.color[1] = dd.color[2] = 0.6f;
            one = psygfx_dots(&dd);
            dot_n = ns[k];
            snprintf(name, sizeof name, "dots x %d, 4 px, upload every frame", ns[k]);
            bench(name, &one, 1, prep_dots, 1);
        }
        psygfx_buffer_free(&gfx, dot_buf);
        free(dot_xy);
    }

    /* noise */
    {
        psygfx_noise_desc nd;
        memset(&nd, 0, sizeof nd);
        nd.w = (float)W; nd.h = (float)H; nd.check = 1; nd.contrast = 0.2f; nd.aperture = PSYGFX_NO_APERTURE;
        one = psygfx_noise(&nd);
        bench("noise, full screen, GPU hash", &one, 1, NULL, 0);
    }
    {
        psygfx_texture_desc td;
        psygfx_image_desc idd;
        float* nb = (float*)malloc((size_t)W * H * sizeof(float));
        uint64_t t0 = psyrt_now_ns();
        psygfx_noise_fill(nb, W, H, 7, PSYGFX_UNIFORM);
        if (run("noise")) {
            char name[80];
            snprintf(name, sizeof name, "noise_fill on the CPU, %dx%d (once)", W, H);
            printf("| %-52s | %9.1f | %9s | %9s |\n", name, (double)(psyrt_now_ns() - t0) * 1e-3, "", "");
        }
        memset(&td, 0, sizeof td);
        td.w = W; td.h = H; td.format = PSYGFX_R32F;
        up_tex = psygfx_texture(&gfx, &td);
        up_data = nb;
        memset(&idd, 0, sizeof idd);
        idd.tex = up_tex; idd.modulation = true; idd.contrast = 0.2f;
        one = psygfx_image(&gfx, &idd);
        bench("noise, full screen, CPU R32F upload + draw", &one, 1, prep_upload, 1);
        psygfx_texture_free(&gfx, up_tex);
        free(nb);
    }

    /* a video-sized frame */
    {
        psygfx_texture_desc td;
        psygfx_image_desc idd;
        unsigned char* px = (unsigned char*)malloc((size_t)W * H * 4);
        for (i = 0; i < W * H * 4; i++) px[i] = (unsigned char)(i * 31);
        memset(&td, 0, sizeof td);
        td.w = W; td.h = H; td.format = PSYGFX_RGBA8;
        up_tex = psygfx_texture(&gfx, &td);
        up_data = px;
        memset(&idd, 0, sizeof idd);
        idd.tex = up_tex;
        one = psygfx_image(&gfx, &idd);
        bench("image RGBA8, full frame: upload + draw", &one, 1, prep_upload, 1);
        if (run("raw")) raw_pbo();
        psygfx_texture_free(&gfx, up_tex);
        free(px);
    }
    if (run("v0.2")) bench_v02(g);
    if (run("v0.3")) bench_v03();
    bench_v04_video(); bench_v04_inst(); bench_v04_mixed();   /* each checks --only */
    psygfx_close(&gfx);
    psyscr_close(&scr);
    return 0;
}
