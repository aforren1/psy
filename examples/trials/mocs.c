/* trials_mocs.c - the method of constant stimuli with a constrained order.
 *
 * The first USAGE example of ysp/trials.h, run for real: 2 orientations x 5
 * contrasts x 20 repetitions, 200 trials in random order with never the same
 * orientation four times running. A simulated observer answers each trial
 * with a Weibull psychometric function of contrast (2AFC, so it guesses at
 * 0.5); orientation does not matter to it.
 *
 * Prints the meta line and the CSV (header and one row per trial) that a
 * real session would write to its data file, then the proportion correct per
 * condition beside the observer's true probability, and the longest run of
 * one orientation, which the constraint caps at 3.
 *
 * The generator is ytr_splitmix on a seed the program owns and prints,
 * which is the whole reproducibility story: the same seed prints the same
 * file. Nothing here touches hardware and nothing is a timing measurement.
 *
 * Build (from the repository root):
 *     cc -O2 -Iinclude -o trials_mocs examples/trials/mocs.c -lm   # Linux / macOS
 *     cl /O2 /Iinclude examples\trials\mocs.c                      # Windows (MSVC)
 *
 * Usage: trials_mocs [seed]      (default 20260923)
 * Exit code: 0, or 1 if the session could not be opened.
 */
#define YSP_TRIALS_IMPLEMENTATION
#include "ysp/trials.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What ytr_next() fills; a starting value so no path reads it unset. */
static const ytr_trial_info ti_zero = { 0, 0, 0, 0, 0, false, false, false, false, false };

static const double contrasts[5] = { 0.02, 0.04, 0.08, 0.16, 0.32 };

/* 2AFC Weibull: 0.5 at zero contrast, 81.6% at alpha. */
static double p_correct(double c) {
    const double alpha = 0.08, beta = 3.0;
    return 0.5 + 0.5 * (1.0 - exp(-pow(c / alpha, beta)));
}

int main(int argc, char** argv) {
    static ytr_trials t;           /* about 90 KB: not on the stack */
    uint64_t seed = 20260923u;
    uint64_t sim = 0;                /* the observer's own stream */
    ytr_desc d;
    ytr_trial_info ti = ti_zero;
    char line[512];
    int c, i, n = 0, run = 0, longest = 0, prev_ori = -1;
    const ytr_trial* h;

    if (argc > 1) seed = (uint64_t)strtoul(argv[1], NULL, 10);
    sim = seed ^ 0x5DEECE66DULL;

    memset(&d, 0, sizeof(d));
    d.factors[0] = ytr_factor("orientation", 2);
    d.factors[1] = ytr_factor("contrast", 5);
    d.n_factors  = 2;
    d.reps       = 20;
    d.order      = YTR_ORDER_CONSTRAINED;
    d.constraints[0] = ytr_max_run(0, YTR_ANY_LEVEL, 3);
    d.n_constraints  = 1;
    d.rng = ytr_splitmix;
    d.rng_ctx = &seed;

    printf("# seed=%lu\n", (unsigned long)seed);
    if (!ytr_open(&t, &d)) {
        fprintf(stderr, "trials_mocs: %s\n", ytr_error(&t));
        return 1;
    }
    ytr_format_meta(&t, line, sizeof(line));
    printf("# %s", line);
    ytr_format_header(&t, line, sizeof(line));
    fputs(line, stdout);

    while (ytr_next(&t, &ti) >= 0) {
        double con = contrasts[ytr_level(&t, ti.condition, 1)];
        int correct = ytr_splitmix(&sim) < p_correct(con) ? 1 : 0;
        ytr_update(&t, correct, NULL);
        ytr_format_row(&t, ti.index, line, sizeof(line));
        fputs(line, stdout);
    }

    printf("\ncond  ori  contrast   n   p(correct)  true p\n");
    for (c = 0; c < ytr_n_conditions(&t); c++) {
        double con = contrasts[ytr_level(&t, c, 1)];
        printf("%4d  %3d  %8.3f  %3d   %9.3f  %6.3f\n", c, ytr_level(&t, c, 0), con,
               ytr_n_valid(&t, c), ytr_proportion(&t, c, 1), p_correct(con));
    }

    h = ytr_history(&t, &n);
    for (i = 0; i < n; i++) {
        int ori = ytr_level(&t, h[i].condition, 0);
        run = (ori == prev_ori) ? run + 1 : 1;
        prev_ori = ori;
        if (run > longest) longest = run;
    }
    printf("\n%d trials; longest run of one orientation: %d (the constraint allows 3); "
           "repair swaps at open: %d\n", n, longest, t.swaps);
    return 0;
}
