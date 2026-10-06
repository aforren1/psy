/* gfx_trial.c - the psy_timeline.h trial with psy_gfx.h stimuli.
 *
 * A fixation cross from 0 to 0.5 s, then a grating in a circular aperture
 * with a raised-cosine edge, whose contrast ramps up over 100 ms with a
 * raised cosine, holds at 0.3, and ramps down to end at 1.2 s; a trigger
 * at the grating's onset. The timeline drives the stimuli through
 * bindings: the fixation's and the grating's `visible` from onset and
 * offset events, the grating's contrast from a keyed track. Each fired
 * event is printed with its frame and residual.
 *
 * Usage: gfx_trial [--sim]
 * Exit code: 0, 1 when the screen or the gfx did not open, 2 for a bad
 * argument.
 */
#define PSY_GFX_IMPLEMENTATION
#include "psy_gfx.h"
#define PSY_TIMELINE_IMPLEMENTATION
#include "psy_timeline.h"

#include <stdio.h>
#include <string.h>

enum { FIX_ON, GRATING_ON, CONTRAST, N_CHANNELS };
enum { TRIAL = 1 };
#define S PSYTL_NS_PER_S

static const psytl_key ramp[] = {
    { S / 2,          0.0f, PSYTL_EASE_COSINE, 0, 0 },
    { S / 2 + S / 10, 0.3f, PSYTL_EASE_LINEAR, 0, 0 },
    { 11 * S / 10,    0.3f, PSYTL_EASE_COSINE, 0, 0 },
    { 6 * S / 5,      0.0f, PSYTL_EASE_LINEAR, 0, 0 },
};

static psytl_event storage[64];
static psytl_timeline tl;
static psyscr_screen scr;
static psygfx_gfx gfx;
static psygfx_stim fix, grating;
static const psygfx_bind binds[] = {
    { &fix,     PSYGFX_P_VISIBLE,  FIX_ON,     NULL },
    { &grating, PSYGFX_P_VISIBLE,  GRATING_ON, NULL },
    { &grating, PSYGFX_P_CONTRAST, CONTRAST,   NULL },
};

static psytl_event ev(int64_t t, int kind, int target, int code) {
    psytl_event e;
    memset(&e, 0, sizeof e);
    e.time = t; e.base = TRIAL; e.kind = (uint8_t)kind; e.target = target; e.code = code;
    return e;
}

int main(int argc, char** argv) {
    psyscr_desc sd;
    psygfx_desc gd;
    psytl_desc td;
    psygfx_shape_desc fd;
    psygfx_grating_desc grd;
    psyscr_frame f;
    psytl_event fired[8], e[5];
    int i, n;
    int64_t t0 = 0;

    memset(&sd, 0, sizeof sd);
    sd.windowed = true;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--sim")) sd.backend = PSYSCR_BACKEND_SIM;
        else { fprintf(stderr, "usage: gfx_trial [--sim]\n"); return 2; }
    }
    memset(&td, 0, sizeof td);
    td.events = storage; td.event_capacity = 64; td.n_channels = N_CHANNELS;
    if (!psytl_open(&tl, &td)) { fprintf(stderr, "%s\n", psytl_error(&tl)); return 1; }
    e[0] = ev(0,         PSYTL_ONSET,   FIX_ON, 0);
    e[1] = ev(S / 2,     PSYTL_OFFSET,  FIX_ON, 0);
    e[2] = ev(S / 2,     PSYTL_ONSET,   GRATING_ON, 0);
    e[3] = ev(S / 2,     PSYTL_TRIGGER, 0, 12);
    e[4] = ev(6 * S / 5, PSYTL_OFFSET,  GRATING_ON, 0);
    psytl_add_n(&tl, e, 5);
    psytl_set_keys(&tl, CONTRAST, TRIAL, ramp, 4);

    if (!psyscr_open(&scr, &sd)) { fprintf(stderr, "gfx_trial: %s\n", psyscr_error(&scr)); return 1; }
    memset(&gd, 0, sizeof gd);
    gd.screen = &scr;
    gd.background[0] = gd.background[1] = gd.background[2] = 0.5f;
    if (!psygfx_open(&gfx, &gd)) { fprintf(stderr, "gfx_trial: %s\n", psygfx_error(&gfx)); return 1; }

    memset(&fd, 0, sizeof fd);
    fd.shape = PSYGFX_CROSS; fd.w = fd.h = 16; fd.shape_p[0] = 3;
    fd.edge = PSYGFX_EDGE_COSINE; fd.edge_width = 1;
    fd.color[0] = fd.color[1] = fd.color[2] = 0.35f;
    fix = psygfx_shape(&fd);
    memset(&grd, 0, sizeof grd);
    grd.w = 200; grd.sf = 1 / 20.0f; grd.ori = 45;
    grd.aperture = PSYGFX_CIRCLE; grd.edge = PSYGFX_EDGE_COSINE; grd.edge_width = 20;
    grating = psygfx_grating(&grd);
    fix.visible = grating.visible = 0.0f;   /* the timeline turns them on */

    while (psyscr_begin(&scr, &f) == PSYSCR_OK) {
        if (f.index == 0) { psytl_anchor(&tl, TRIAL, f.onset, 0); t0 = f.onset; }
        n = psytl_evaluate(&tl, &(psytl_frame){ f.onset, f.period, f.index }, fired, 8);
        psygfx_apply(binds, 3, psytl_values(&tl));
        psyscr_mark(&scr, PSYSCR_PHASE_EVALUATE);
        psygfx_begin(&gfx, &f);
        psygfx_draw(&gfx, &fix);
        psygfx_draw(&gfx, &grating);
        psygfx_end(&gfx);
        for (i = 0; i < n && i < 8; i++)
            printf("frame %3lld  kind %d ch %d code %2d  at %+8.3f ms, residual %+.3f ms\n",
                   (long long)fired[i].frame, fired[i].kind, fired[i].target, fired[i].code,
                   (double)(fired[i].onset - t0) / 1e6, (double)fired[i].residual / 1e6);
        psyscr_flip(&scr);
        if (f.onset - t0 > 13 * S / 10 || f.index > 400) break;
    }
    psygfx_close(&gfx);
    psyscr_close(&scr);
    return 0;
}
