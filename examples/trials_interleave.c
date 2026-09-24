/* trials_interleave.c - three staircases and a catch condition, interleaved.
 *
 * The second USAGE example of psy_trials.h, run for real: three psy_stair.h
 * staircases (1-up-2-down, 1-up-3-down, and a weighted 1-up-1-down aimed at
 * 75%) share one session with 24 catch trials. While any staircase runs,
 * about 9 trials in 10 go to a staircase; the catch trials never come two
 * in a row. The schedule is 24 identical catch trials, so no order of it
 * keeps them apart: the track trials do, because next() runs a track trial
 * whenever the next catch trial would follow another.
 *
 * The observer is a 2AFC Weibull in contrast. A catch trial is a blank, so
 * the observer is at chance on it; a lapsing observer would show up there.
 * Each staircase trial stores its level as the trial's record.
 *
 * Prints one line per trial (which track, or catch, the level and the
 * response), then each staircase's estimate beside the level where the
 * observer really is at that staircase's target, and a check that no two
 * catch trials were adjacent. The rule holds only while a staircase is left
 * to separate them: with some seeds (3, 4 and 9 of 1..11) the staircases
 * finish while catch trials remain, those run back to back, and the history
 * flags each with PSYTR_FLAG_VIOLATION, which the summary counts.
 *
 * Nothing here touches hardware and nothing is a timing measurement.
 *
 * Build (from the repository root):
 *     cc -O2 -I. -o trials_interleave examples/trials_interleave.c -lm
 *     cl /O2 /I. examples\trials_interleave.c
 *
 * Usage: trials_interleave [seed]      (default 7)
 * Exit code: 0, or 1 if a handle could not be opened.
 */
#define PSY_TRIALS_IMPLEMENTATION
#include "psy_trials.h"
#define PSY_STAIR_IMPLEMENTATION
#include "psy_stair.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What psytr_next() fills; a starting value so no path reads it unset. */
static const psytr_trial_info ti_zero = { 0, 0, 0, 0, 0, false, false, false, false, false };

#define ALPHA 0.10   /* the observer's 81.6% point */
#define BETA  3.0

static double p_correct(double c) {
    if (c <= 0.0) return 0.5;
    return 0.5 + 0.5 * (1.0 - exp(-pow(c / ALPHA, BETA)));
}

/* The contrast at which the observer is correct with probability p. */
static double level_for(double p) {
    return ALPHA * pow(-log(1.0 - (p - 0.5) / 0.5), 1.0 / BETA);
}

/* The caller's three-line adapter from the manual. */
static bool psytr_stair_done(void* ctx) {
    return psyst_done((const psyst_stair*)ctx);
}

int main(int argc, char** argv) {
    static psytr_trials t;
    static psyst_stair s[3];
    static double levels[PSYTR_MAX_TRIALS];
    static const char* names[3] = { "1-up-2-down", "1-up-3-down", "weighted 75%" };
    double target[3];
    uint64_t seed = 7u, sim;
    psyst_desc sd;
    psytr_desc d;
    psytr_trial_info ti = ti_zero;
    const psytr_trial* h;
    int i, n = 0, adjacent = 0, flagged = 0, per_track[3] = { 0, 0, 0 }, n_catch = 0;
    int first_track = -1;
    const double* rec;

    if (argc > 1) seed = (uint64_t)strtoul(argv[1], NULL, 10);
    sim = seed * 0x9E3779B97F4A7C15ULL + 1u;

    for (i = 0; i < 3; i++) {
        memset(&sd, 0, sizeof(sd));
        sd.start = 0.3;
        sd.n_up = 1;
        sd.n_down = (i == 0) ? 2 : (i == 1) ? 3 : 1;
        sd.step_down_scale = (i == 2) ? psyst_weighted_scale(0.75) : 0.0;
        sd.step_type = PSYST_STEP_LOG;
        sd.steps[0] = 0.2;
        sd.steps[1] = 0.1;
        sd.steps[2] = 0.05;
        sd.n_steps = 3;
        sd.min = 0.001;
        sd.max = 1.0;
        sd.stop_trials = 80;
        if (!psyst_open(&s[i], &sd)) {
            fprintf(stderr, "trials_interleave: %s\n", psyst_error(&s[i]));
            return 1;
        }
        target[i] = psyst_convergence_p(sd.n_up, sd.n_down, sd.step_down_scale);
    }

    memset(&d, 0, sizeof(d));
    d.n_conditions = 1;                          /* the catch trial */
    d.reps         = 24;
    d.order        = PSYTR_ORDER_CONSTRAINED;
    d.constraints[0] = psytr_min_gap(PSYTR_CONDITION, 0, 1);
    d.n_constraints  = 1;
    for (i = 0; i < 3; i++) d.tracks[i] = psytr_track(&s[i], psytr_stair_done);
    d.n_tracks    = 3;
    d.track_rate  = 0.9;
    d.records     = levels;
    d.record_size = sizeof(double);
    d.rng = psytr_splitmix;
    d.rng_ctx = &seed;

    printf("seed %lu: 3 staircases, 80 trials each, and 24 catch trials; "
           "track_rate 0.9, catch min_gap 1\n\n", (unsigned long)seed);
    if (!psytr_open(&t, &d)) {
        fprintf(stderr, "trials_interleave: %s\n", psytr_error(&t));
        return 1;
    }

    printf("trial  source        level    resp\n");
    while (psytr_next(&t, &ti) >= 0) {
        if (ti.track >= 0) {
            double level = psyst_next(&s[ti.track]);
            int r = psytr_splitmix(&sim) < p_correct(level) ? 1 : 0;
            psyst_update(&s[ti.track], level, r);
            psytr_update(&t, r, &level);
            per_track[ti.track]++;
            printf("%5d  track %d   %9.5f   %d\n", ti.index, ti.track, level, r);
        } else {
            int r = psytr_splitmix(&sim) < p_correct(0.0) ? 1 : 0;
            psytr_update(&t, r, NULL);
            printf("%5d  catch                   %d\n", ti.index, r);
        }
    }

    h = psytr_history(&t, &n);
    for (i = 0; i < n; i++) {
        if (h[i].track >= 0 && first_track < 0) first_track = i;
        if (h[i].track < 0) n_catch++;
        if (i > 0 && h[i].track < 0 && h[i - 1].track < 0) adjacent++;
        if (h[i].flags & PSYTR_FLAG_VIOLATION) flagged++;
    }

    rec = (const double*)psytr_record(&t, first_track);
    printf("\n%d trials: %d, %d and %d to the staircases, %d catch; trial %d, the first "
           "staircase trial, has its level in its record: %.5f\n", n, per_track[0],
           per_track[1], per_track[2], n_catch, first_track, rec ? *rec : -1.0);
    printf("catch trials correct: %.3f (chance is 0.5)\n", psytr_proportion(&t, 0, 1));
    printf("adjacent catch trials: %d; trials flagged PSYTR_FLAG_VIOLATION: %d\n\n",
           adjacent, flagged);
    printf("staircase       target p   estimate   true level   error (log10)\n");
    for (i = 0; i < 3; i++) {
        double est = psyst_estimate(&s[i], PSYST_EST_REVERSALS);
        double truth = level_for(target[i]);
        printf("%-14s  %8.4f  %9.5f  %11.5f   %+8.4f\n", names[i], target[i], est, truth,
               log10(est / truth));
    }
    printf("\nOne session is one sample; run several seeds before believing an error.\n");
    return 0;
}
