/* gfx_load.c - psy_gfx.h in a real frame loop, under load, with the flip
 * records as the measurement.
 *
 * An 800 x 600 window (or the simulated display) draws N small gabors and
 * M dots whose positions are uploaded every frame, marks the phases the
 * manual's frame loop marks (EVALUATE, UPLOAD; the rest is DRAW), and at the
 * end prints, from psy_screen.h's flip records: frames, drops, late targets,
 * and the p50 and p99 of the DRAW, UPLOAD and SWAP phases. Low contrast, no
 * flicker: the display may be somebody's working screen.
 *
 * Usage: gfx_load [--sim] [--gabors N] [--dots N] [--seconds S] [--composition]
 *                 [--topmost] [--allocs | --allocs-control]
 *   --topmost         keep the window on top and take the foreground (Windows)
 *   --allocs          count C runtime heap calls in the frame loop after
 *                     frame 30 (MSVC debug builds)
 *   --allocs-control  the same, with one malloc per frame the count must see
 * Exit code: 0, 1 when the screen or the gfx did not open, 2 for a bad
 * argument.
 */
#define PSY_GFX_IMPLEMENTATION
#include "psy_gfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
/* Windows gives the foreground to a program started from the background
 * after it sends one input event; a zero-size mouse move is that event. */
static void take_foreground(SDL_Window* w) {
    HWND h = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    INPUT in;
    memset(&in, 0, sizeof in);
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof in);
    SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(h);
}
#endif

static volatile long g_allocs;
static volatile int g_count_allocs;
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
static int alloc_hook(int type, void* data, size_t size, int block, long req, const unsigned char* file, int line) {
    (void)data; (void)size; (void)block; (void)req; (void)file; (void)line;
    if (g_count_allocs && (type == _HOOK_ALLOC || type == _HOOK_REALLOC)) g_allocs++;
    return 1;
}
#endif

static psyscr_screen scr;
static psygfx_gfx gfx;
static psygfx_stim stims[1001];
static float xy[2 * 100000];
static psyrt_event ring_mem[8192];
static psyrt_ring ring;

#define MAXREC 40000
static uint32_t ph[3][MAXREC];

static int cmp_u(const void* a, const void* b) {
    uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
    return x < y ? -1 : (x > y);
}

static double pct(uint32_t* v, int n, double p) {
    if (n <= 0) return 0;
    qsort(v, (size_t)n, sizeof v[0], cmp_u);
    return v[(int)(p * (n - 1))] * 1e-3;
}

int main(int argc, char** argv) {
    psyscr_desc sd;
    psygfx_desc gd;
    psyrt_ring_desc rd;
    psyscr_frame f;
    psygfx_buf dots_buf = { 0 };
    int n_gabors = 0, n_dots = 0, i, n_stims = 0, nrec = 0, topmost = 0, allocs = 0;
    double seconds = 10;
    long dropped = 0, late = 0, flips = 0;
    uint32_t seed = 1;
    char line[400];

    memset(&sd, 0, sizeof sd);
    sd.windowed = true;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--sim")) sd.backend = PSYSCR_BACKEND_SIM;
        else if (!strcmp(argv[i], "--composition")) sd.backend = PSYSCR_BACKEND_COMPOSITION;
        else if (!strcmp(argv[i], "--gabors") && i + 1 < argc) n_gabors = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dots") && i + 1 < argc) n_dots = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--topmost")) topmost = 1;
        else if (!strcmp(argv[i], "--allocs")) allocs = 1;
        else if (!strcmp(argv[i], "--allocs-control")) allocs = 2;
        else {
            fprintf(stderr, "usage: gfx_load [--sim] [--gabors N] [--dots N] [--seconds S] [--composition] "
                            "[--topmost] [--allocs | --allocs-control]\n");
            return 2;
        }
    }
    if (n_gabors < 0 || n_gabors > 1000 || n_dots < 0 || n_dots > 100000 || !(seconds > 0)) {
        fprintf(stderr, "gfx_load: gabors 0..1000, dots 0..100000, seconds > 0\n");
        return 2;
    }
    memset(&rd, 0, sizeof rd);
    rd.memory = ring_mem;
    rd.bytes = sizeof ring_mem;
    if (!psyrt_ring_open(&ring, &rd)) { fprintf(stderr, "gfx_load: ring\n"); return 1; }
    sd.ring = &ring;
    if (!psyscr_open(&scr, &sd)) { fprintf(stderr, "gfx_load: %s\n", psyscr_error(&scr)); return 1; }
    memset(&gd, 0, sizeof gd);
    gd.screen = &scr;
    gd.background[0] = gd.background[1] = gd.background[2] = 0.5f;
    gd.max_draws = 1024;
    if (!psygfx_open(&gfx, &gd)) { fprintf(stderr, "gfx_load: %s\n", psygfx_error(&gfx)); return 1; }
    psyscr_describe(&scr, line, sizeof line);
    printf("%s\n", line);
    psygfx_describe(&gfx, line, sizeof line);
    printf("%s\n", line);
#if defined(_WIN32)
    if (topmost && psyscr_window(&scr)) take_foreground(psyscr_window(&scr));
#else
    if (topmost) printf("--topmost acts on Win32 windows; ignored here\n");
#endif
#if defined(_MSC_VER) && defined(_DEBUG)
    if (allocs) _CrtSetAllocHook(alloc_hook);
#else
    if (allocs) printf("--allocs needs an MSVC debug build; not counted\n");
#endif
    for (i = 0; i < n_gabors; i++) {
        psygfx_gabor_desc gb;
        memset(&gb, 0, sizeof gb);
        gb.sigma = 6; gb.sf = 0.08f; gb.contrast = 0.1f; gb.ori = (float)(i * 17 % 180);
        gb.place = PSYGFX_TOP_LEFT;
        gb.x = (float)(30 + (i * 37) % 740);
        gb.y = (float)(30 + (i * 53) % 540);
        stims[n_stims++] = psygfx_gabor(&gb);
    }
    if (n_dots) {
        psygfx_dots_desc dd;
        dots_buf = psygfx_buffer(&gfx, (size_t)n_dots * 8);
        memset(&dd, 0, sizeof dd);
        dd.buf = dots_buf; dd.count = (uint32_t)n_dots; dd.dot_size = 3; dd.edge = PSYGFX_EDGE_COSINE; dd.edge_width = 1;
        dd.color[0] = dd.color[1] = dd.color[2] = 0.56f;
        dd.aperture = PSYGFX_CIRCLE; dd.w = 500;
        stims[n_stims++] = psygfx_dots(&dd);
        for (i = 0; i < 2 * n_dots; i++) { seed = seed * 1664525u + 1013904223u; xy[i] = (float)(seed >> 8) / 16777216.0f * 500.0f - 250.0f; }
    }
    {
        int64_t t_end = 0;
        while (psyscr_begin(&scr, &f) == PSYSCR_OK) {
            psyrt_event ev[64];
            int n, k;
            if (f.index == 0) t_end = f.onset + (int64_t)(seconds * 1e9);
            g_count_allocs = allocs && f.index >= 30;
            if (allocs == 2 && f.index >= 30) free(malloc(16));   /* the control */
            for (k = 0; k < n_gabors; k++) stims[k].phase = (float)fmod((double)(f.onset % 1000000000) * 1e-9, 1.0);
            psyscr_mark(&scr, PSYSCR_PHASE_EVALUATE);
            if (n_dots) {
                for (k = 0; k < 2 * n_dots; k++) {   /* a small random walk, wrapped */
                    seed = seed * 1664525u + 1013904223u;
                    xy[k] += (float)((int)(seed >> 29) - 3) * 0.5f;
                    if (xy[k] > 250) xy[k] -= 500;
                    if (xy[k] < -250) xy[k] += 500;
                }
                psygfx_buffer_update(&gfx, dots_buf, 0, xy, (size_t)n_dots * 8);
            }
            psyscr_mark(&scr, PSYSCR_PHASE_UPLOAD);
            psygfx_begin(&gfx, &f);
            psygfx_draw_n(&gfx, stims, n_stims);
            psygfx_end(&gfx);
            psyscr_flip(&scr);
            n = psyrt_ring_drain(&ring, ev, 64);
            for (k = 0; k < n; k++) {
                unsigned flags;
                if (ev[k].source != PSYRT_SRC_SCREEN || ev[k].kind != PSYSCR_EV_FLIP) continue;
                flags = PSYSCR_EV_FLAGS_OF(ev[k].u.u16[5]);
                flips++;
                dropped += ev[k].u.u16[4];
                late += (flags & PSYSCR_FLIP_LATE_TARGET) != 0;
                if (nrec < MAXREC) {
                    ph[0][nrec] = ev[k].u.u32[3 + PSYSCR_PHASE_DRAW];
                    ph[1][nrec] = ev[k].u.u32[3 + PSYSCR_PHASE_UPLOAD];
                    ph[2][nrec] = ev[k].u.u32[3 + PSYSCR_PHASE_SWAP];
                    nrec++;
                }
            }
            g_count_allocs = 0;
            if (f.onset >= t_end) break;
        }
    }
    psygfx_close(&gfx);
    psyscr_close(&scr);
    if (allocs) printf("C runtime heap calls in the frame loop after frame 30: %ld\n", (long)g_allocs);
    printf("gabors %d, dots %d: %ld flips, %ld dropped vblanks, %ld late targets\n", n_gabors, n_dots, flips, dropped, late);
    printf("DRAW p50 %.1f us p99 %.1f us; UPLOAD p50 %.1f us p99 %.1f us; SWAP p50 %.1f us p99 %.1f us\n",
           pct(ph[0], nrec, 0.5), pct(ph[0], nrec, 0.99), pct(ph[1], nrec, 0.5), pct(ph[1], nrec, 0.99),
           pct(ph[2], nrec, 0.5), pct(ph[2], nrec, 0.99));
    return 0;
}
