/* screen_hello.c - the psy_screen.h frame loop with a psy_timeline.h trial.
 *
 * The trial of psy_timeline.h's USAGE section on a real display: a
 * fixation square from 0 to 0.5 s, then a "grating" square whose contrast
 * ramps up over 100 ms with a raised cosine, holds at 0.5 and ramps down to
 * end at 1.2 s, and a trigger at the grating's onset. There is no
 * psy_gfx.h yet, so both stimuli are scissored clears of a small square, a
 * few gray levels above a dark gray screen. The timeline evaluates at the
 * predicted onset of the flip each frame draws for, and each fired event is
 * printed with the measured onset of its flip.
 *
 * The frame loop itself (USAGE in psy_screen.h) is the ten lines in main().
 *
 * Usage: screen_hello [--sim] [--windowed]
 *   --sim       no window: the simulated display (CI runs this)
 *   --windowed  an 800 x 600 window instead of borderless fullscreen
 * Shift+Esc or closing the window ends the trial at once.
 * Exit code: 0, 1 when the screen did not open, 2 for a bad argument.
 */
/* psy_screen.h first: psy_rt.h, which it includes, sets the feature-test
 * macro glibc reads at the first system header. */
#define PSY_SCREEN_IMPLEMENTATION
#include "psy_screen.h"
#define PSY_TIMELINE_IMPLEMENTATION
#include "psy_timeline.h"

#include <stdio.h>
#include <string.h>

enum { FIX_ON, GRATING_ON, CONTRAST, N_CHANNELS };
enum { TRIAL = 1 };

#define S PSYTL_NS_PER_S

static const psytl_key ramp[] = {
    { S / 2,          0.0f, PSYTL_EASE_COSINE, 0, 0 },
    { S / 2 + S / 10, 0.5f, PSYTL_EASE_LINEAR, 0, 0 },
    { 11 * S / 10,    0.5f, PSYTL_EASE_COSINE, 0, 0 },
    { 6 * S / 5,      0.0f, PSYTL_EASE_LINEAR, 0, 0 },
};

static psytl_event storage[64];
static psytl_timeline tl;
static psyscr_screen scr;

#if defined(_WIN32)
    #define GLCALL __stdcall
#else
    #define GLCALL
#endif
static void (GLCALL *glClearColor_)(float, float, float, float);
static void (GLCALL *glClear_)(unsigned int);
static void (GLCALL *glScissor_)(int, int, int, int);
static void (GLCALL *glEnable_)(unsigned int);
static void (GLCALL *glDisable_)(unsigned int);
static void (GLCALL *glViewport_)(int, int, int, int);

static psytl_event ev(int64_t t, int kind, int target, int code) {
    psytl_event e;
    memset(&e, 0, sizeof e);
    e.time = t;
    e.base = TRIAL;
    e.kind = (uint8_t)kind;
    e.target = target;
    e.code = code;
    return e;
}

/* Low contrast on purpose: the display may be somebody's working screen. */
static void draw(const float* v) {
    int w = scr.caps.mode.w, h = scr.caps.mode.h, side = h / 6;
    float g;
    if (!glClear_) return;
    glViewport_(0, 0, w, h);
    glDisable_(0x0C11u);                       /* GL_SCISSOR_TEST */
    glClearColor_(0.2f, 0.2f, 0.2f, 1.0f);
    glClear_(0x4000u);
    glEnable_(0x0C11u);
    if (v[FIX_ON] > 0.5f) {
        glScissor_(w / 2 - 6, h / 2 - 6, 12, 12);
        glClearColor_(0.3f, 0.3f, 0.3f, 1.0f);
        glClear_(0x4000u);
    }
    if (v[GRATING_ON] > 0.5f) {
        g = 0.2f + 0.2f * v[CONTRAST];
        glScissor_(w / 2 - side / 2, h / 2 - side / 2, side, side);
        glClearColor_(g, g, g, 1.0f);
        glClear_(0x4000u);
    }
    glDisable_(0x0C11u);
}

static const char* kind_name(int k) {
    switch (k) {
    case PSYTL_TRIGGER: return "trigger";
    case PSYTL_ONSET:   return "onset";
    case PSYTL_OFFSET:  return "offset";
    default:            return "event";
    }
}

int main(int argc, char** argv) {
    psyscr_desc d;
    psytl_desc td;
    psyscr_frame f;
    psytl_event fired[8];
    int i, n, rc = PSYSCR_OK;
    bool sim = false, windowed = false;
    int64_t onset_of[400];
    psytl_event e[5];

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--sim")) sim = true;
        else if (!strcmp(argv[i], "--windowed")) windowed = true;
        else { fprintf(stderr, "usage: screen_hello [--sim] [--windowed]\n"); return 2; }
    }

    memset(&td, 0, sizeof td);
    td.events = storage;
    td.event_capacity = 64;
    td.n_channels = N_CHANNELS;
    if (!psytl_open(&tl, &td)) { fprintf(stderr, "%s\n", psytl_error(&tl)); return 1; }
    e[0] = ev(0,         PSYTL_ONSET,   FIX_ON, 0);
    e[1] = ev(S / 2,     PSYTL_OFFSET,  FIX_ON, 0);
    e[2] = ev(S / 2,     PSYTL_ONSET,   GRATING_ON, 0);
    e[3] = ev(S / 2,     PSYTL_TRIGGER, 0, 12);
    e[4] = ev(6 * S / 5, PSYTL_OFFSET,  GRATING_ON, 0);
    psytl_add_n(&tl, e, 5);
    psytl_set_keys(&tl, CONTRAST, TRIAL, ramp, 4);

    memset(&d, 0, sizeof d);
    d.backend = sim ? PSYSCR_BACKEND_SIM : PSYSCR_BACKEND_AUTO;
    d.windowed = windowed;
    if (!psyscr_open(&scr, &d)) { fprintf(stderr, "screen_hello: %s\n", psyscr_error(&scr)); return 1; }
    glClearColor_ = (void (GLCALL*)(float, float, float, float))psyscr_gl_proc(&scr, "glClearColor");
    glClear_ = (void (GLCALL*)(unsigned int))psyscr_gl_proc(&scr, "glClear");
    glScissor_ = (void (GLCALL*)(int, int, int, int))psyscr_gl_proc(&scr, "glScissor");
    glEnable_ = (void (GLCALL*)(unsigned int))psyscr_gl_proc(&scr, "glEnable");
    glDisable_ = (void (GLCALL*)(unsigned int))psyscr_gl_proc(&scr, "glDisable");
    glViewport_ = (void (GLCALL*)(int, int, int, int))psyscr_gl_proc(&scr, "glViewport");

    /* The frame loop: 1.3 s of trial, the trial's time 0 on its first frame. */
    while ((rc = psyscr_begin(&scr, &f)) == PSYSCR_OK) {
        if (f.index == 0) psytl_anchor(&tl, TRIAL, f.onset, 0);
        n = psytl_evaluate(&tl, &(psytl_frame){ f.onset, f.period, f.index }, fired, 8);
        draw(psytl_values(&tl));
        onset_of[f.index] = f.onset;
        for (i = 0; i < n && i < 8; i++)
            printf("frame %3lld  %-7s ch %d code %2d  predicted %+8.3f ms after trial start, residual %+.3f ms\n",
                   (long long)fired[i].frame, kind_name(fired[i].kind), fired[i].target, fired[i].code,
                   (double)(fired[i].onset - onset_of[0]) / 1e6, (double)fired[i].residual / 1e6);
        psyscr_flip(&scr);
        if (f.onset - onset_of[0] > 13 * S / 10 || f.index == 399) break;
    }
    {
        psyscr_record r;
        if (rc == PSYSCR_OK && psyscr_wait_flip(&scr, &r) == PSYSCR_OK)
            printf("last flip: frame %lld, onset %+.3f ms after its prediction, %u vblanks dropped\n",
                   (long long)r.index, (double)(r.onset - r.target) / 1e6, r.dropped);
    }
    psyscr_close(&scr);
    if (rc < 0) { fprintf(stderr, "screen_hello: %s\n", psyscr_strerror(rc)); return 1; }
    return 0;
}
