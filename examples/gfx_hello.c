/* gfx_hello.c - a gabor in a window: psy_gfx.h's USAGE example.
 *
 * The frame loop is the manual's ten lines. A mid-gray 800 x 600 window, a
 * gabor of sigma 32 px and 1/32 cycle per pixel at contrast 0.5 in its
 * center, drifting at 1 Hz: its phase is computed from the predicted onset
 * of each frame. Low contrast and small, because the display may be
 * somebody's working screen.
 *
 * Usage: gfx_hello [--sim] [--frames N] [--cache DIR] [--no-cache]
 *   --sim       no window and no GL: the simulated display and the null
 *               backend, for CI
 *   --frames N  stop after N frames (default: until Shift+Esc or the window closes)
 *   --cache DIR keep compiled programs in DIR; the default is the per-user
 *               folder of psygfx_default_cache_dir(), when there is one
 *   --no-cache  compile every program; read and write no cache file
 * Exit code: 0, 1 when the screen or the gfx did not open, 2 for a bad
 * argument.
 */
#define PSY_GFX_IMPLEMENTATION
#include "psy_gfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static psyscr_screen scr;
static psygfx_gfx gfx;

int main(int argc, char** argv) {
    psyscr_desc sd;
    psygfx_desc gd;
    psygfx_gabor_desc gab;
    psygfx_stim g;
    psyscr_frame f;
    int64_t t0 = 0, frames = -1;
    static psygfx_file_cache pcache;
    static char cache_dir[512];
    const psygfx_cache* cache = NULL;
    int i, no_cache = 0;
    char line[600];

    memset(&sd, 0, sizeof sd);
    sd.windowed = true;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--sim")) sd.backend = PSYSCR_BACKEND_SIM;
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoll(argv[++i]);
        else if (!strcmp(argv[i], "--cache") && i + 1 < argc && (cache = psygfx_file_cache_init(&pcache, argv[i + 1])) != NULL) i++;
        else if (!strcmp(argv[i], "--no-cache")) no_cache = 1;
        else { fprintf(stderr, "usage: gfx_hello [--sim] [--frames N] [--cache DIR] [--no-cache]\n"); return 2; }
    }
    /* The per-user folder unless told otherwise, so that a second run loads
     * the programs instead of compiling them (PROGRAM CACHE). */
    if (no_cache) cache = NULL;
    else if (!cache && psygfx_default_cache_dir(cache_dir, sizeof cache_dir) == PSYGFX_OK)
        cache = psygfx_file_cache_init(&pcache, cache_dir);

    /* In C99 these are compound literals with designated initializers:
     *   psyscr_open(&scr, &(psyscr_desc){ .windowed = true });
     *   psygfx_open(&gfx, &(psygfx_desc){ .screen = &scr, .background = { 0.5f, 0.5f, 0.5f } });
     *   psygfx_gabor(&(psygfx_gabor_desc){ .sf = 1 / 32.0f, .sigma = 32, .contrast = 0.5f });
     * Written out field by field here so the file also builds as C++17. */
    if (!psyscr_open(&scr, &sd)) { fprintf(stderr, "gfx_hello: %s\n", psyscr_error(&scr)); return 1; }
    memset(&gd, 0, sizeof gd);
    gd.screen = &scr;
    gd.background[0] = gd.background[1] = gd.background[2] = 0.5f;
    gd.cache = cache;
    if (!psygfx_open(&gfx, &gd)) { fprintf(stderr, "gfx_hello: %s\n", psygfx_error(&gfx)); return 1; }
    memset(&gab, 0, sizeof gab);
    gab.sf = 1 / 32.0f;
    gab.sigma = 32;
    gab.contrast = 0.5f;
    g = psygfx_gabor(&gab);
    psygfx_describe(&gfx, line, sizeof line);
    printf("%s\n", line);

    while (psyscr_begin(&scr, &f) == PSYSCR_OK) {   /* Shift+Esc ends it */
        if (f.index == 0) t0 = f.onset;
        g.phase = (float)fmod(1.0 * (double)(f.onset - t0) * 1e-9, 1.0);   /* 1 Hz drift */
        psygfx_begin(&gfx, &f);
        psygfx_draw(&gfx, &g);
        psygfx_end(&gfx);
        psyscr_flip(&scr);
        if (frames > 0 && f.index + 1 >= frames) break;
    }
    psygfx_close(&gfx);
    psyscr_close(&scr);
    return 0;
}
