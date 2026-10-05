/* timeline_trial.c - one trial's timeline on a simulated 60 Hz display.
 *
 * The USAGE example of psy_timeline.h, run for real: a fixation point from
 * 0 to 0.5 s, then a grating whose contrast ramps up over 100 ms with a
 * raised cosine, holds at 0.5 and ramps down to end at 1.2 s, and a trigger
 * at the grating's onset. The trial runs twice from the same events: the
 * second run anchors the trial base at base time 0 again, which rewinds it.
 *
 * The display is simulated: frame k has its onset at k / 60 s on a grid,
 * and the second trial starts a third of a frame off that grid, so its
 * events land with a residual. Frame 30 of the simulated loop, the grating onset, is dropped
 * (the next onset is a frame later), so whatever was due on it lands late
 * on the frame after, and the residual says by how much.
 *
 * Prints one line per fired event: frame, predicted onset, kind, channel or
 * code, the residual in ms and whether it was late, then the contrast
 * channel every five frames of the first trial.
 *
 * Nothing here touches hardware and nothing is a timing measurement: the
 * onsets are made up, which is the point of a timeline that takes the
 * onset as an argument.
 *
 * Build (from the repository root):
 *     cc -O2 -I. -o timeline_trial examples/timeline_trial.c -lm   # Linux / macOS
 *     cl /O2 /I. examples\timeline_trial.c                         # Windows (MSVC)
 *
 * Usage: timeline_trial
 * Exit code: 0, or 1 if a call failed.
 */
#define PSY_TIMELINE_IMPLEMENTATION
#include "psy_timeline.h"

#include <stdio.h>
#include <string.h>

enum { FIX_ON, GRATING_ON, CONTRAST, N_CHANNELS };
enum { TRIAL = 1 };

#define S PSYTL_NS_PER_S

/* The keys stay valid while the channel uses them, so they are static. */
static const psytl_key ramp[] = {
    { S / 2,          0.0f, PSYTL_EASE_COSINE, 0, 0 },
    { S / 2 + S / 10, 0.5f, PSYTL_EASE_LINEAR, 0, 0 },
    { 11 * S / 10,    0.5f, PSYTL_EASE_COSINE, 0, 0 },
    { 6 * S / 5,      0.0f, PSYTL_EASE_LINEAR, 0, 0 },
};

static psytl_event storage[64];
static psytl_timeline tl;

static psytl_event ev(int64_t t, int kind, int target, int code) {
    psytl_event e;
    memset(&e, 0, sizeof(e));
    e.time = t;
    e.base = TRIAL;
    e.kind = (uint8_t)kind;
    e.target = target;
    e.code = code;
    return e;
}

static const char* kind_name(int k) {
    switch (k) {
    case PSYTL_MARK:    return "mark";
    case PSYTL_TRIGGER: return "trigger";
    case PSYTL_ONSET:   return "onset";
    case PSYTL_OFFSET:  return "offset";
    case PSYTL_SET:     return "set";
    default:            return "user";
    }
}

/* Simulated grid: frame k at round(k * 1e9 / 60) ns. */
static int64_t grid(int64_t k) { return (k * S * 2 + 60) / 120; }

/* Run frames [k0, k0 + n) with the trial base anchored at `anchor`.
 * `drop` is a loop frame whose flip misses, so the loop skips a vblank. */
static int run_trial(int64_t* k, int n, int64_t anchor, int drop, bool show_contrast) {
    int i, j;
    int64_t period = grid(1) - grid(0);
    if (psytl_anchor(&tl, TRIAL, anchor, 0) < 0) return 1;
    for (i = 0; i < n; i++) {
        psytl_frame f;
        psytl_event fired[8];
        int nf;
        if (i == drop) (*k)++;
        f.onset = grid(*k);
        f.period = period;
        f.index = *k;
        nf = psytl_evaluate(&tl, &f, fired, 8);
        if (nf < 0) {
            fprintf(stderr, "evaluate: %s\n", psytl_strerror(nf));
            return 1;
        }
        for (j = 0; j < nf && j < 8; j++) {
            const psytl_event* e = &fired[j];
            printf("  frame %4lld  onset %9.3f ms  %-7s %s %2d  residual %+7.3f ms%s\n",
                   (long long)e->frame, (double)e->onset / 1e6, kind_name(e->kind),
                   e->kind == PSYTL_TRIGGER ? "code   " : "channel",
                   e->kind == PSYTL_TRIGGER ? e->code : e->target,
                   (double)e->residual / 1e6,
                   (e->flags & PSYTL_EV_LATE) ? "  late" : "");
        }
        if (show_contrast && i % 5 == 0 && i >= 25 && i <= 80)
            printf("                                   contrast at trial frame %2d: %.4f\n",
                   i, (double)psytl_value(&tl, CONTRAST));
        (*k)++;
    }
    return 0;
}

int main(void) {
    psytl_desc d;
    psytl_event e[5];
    int64_t k = 0;
    int rc;

    memset(&d, 0, sizeof(d));
    d.events = storage;
    d.event_capacity = 64;
    d.n_channels = N_CHANNELS;
    if (!psytl_open(&tl, &d)) {
        fprintf(stderr, "%s\n", psytl_error(&tl));
        return 1;
    }
    e[0] = ev(0,         PSYTL_ONSET,   FIX_ON, 0);
    e[1] = ev(S / 2,     PSYTL_OFFSET,  FIX_ON, 0);
    e[2] = ev(S / 2,     PSYTL_ONSET,   GRATING_ON, 0);
    e[3] = ev(S / 2,     PSYTL_TRIGGER, 0, 12);
    e[4] = ev(6 * S / 5, PSYTL_OFFSET,  GRATING_ON, 0);
    rc = psytl_add_n(&tl, e, 5);
    if (rc < 0) { fprintf(stderr, "add: %s\n", psytl_strerror(rc)); return 1; }
    rc = psytl_set_keys(&tl, CONTRAST, TRIAL, ramp, 4);
    if (rc < 0) { fprintf(stderr, "keys: %s\n", psytl_strerror(rc)); return 1; }

    printf("psy_timeline %s, simulated 60 Hz\n", psytl_version());
    printf("trial 1: anchored on the grid at frame %lld; loop frame 30 drops\n", (long long)k);
    if (run_trial(&k, 80, grid(k), 30, true)) return 1;

    k += 10;
    printf("trial 2: anchored a third of a frame before frame %lld\n", (long long)k);
    if (run_trial(&k, 80, grid(k) - (grid(1) - grid(0)) / 3, -1, false)) return 1;
    return 0;
}
