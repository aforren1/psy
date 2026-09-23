/* stair_sim.c - run a 3-down-1-up staircase against a simulated observer.
 *
 * Prints the whole track, one line per trial: the level the rule proposed,
 * the level "shown" (quantized here, to show that next() and update() are
 * separate), the response, and a mark at each reversal. Ends with the four
 * estimators beside the observer's true threshold, so you can see how far a
 * summary of a short track lands from the answer.
 *
 * The observer is a Weibull psychometric function for a 2AFC task: it guesses
 * correctly half the time at zero contrast and its threshold is at 79.4%
 * correct, which is where a 1-up-3-down staircase converges. The generator is
 * splitmix64 inside this file, seeded from the command line, because
 * psy_stair.h owns no random source: psyst_simulate_response() takes the
 * uniform variate the caller drew, so a run is reproducible from its seed.
 *
 * Nothing here touches hardware and nothing here is a timing measurement.
 *
 * Build (from the repository root):
 *     cc -O2 -I. -o stair_sim examples/stair_sim.c -lm   # Linux / macOS
 *     cl /O2 /I. examples\stair_sim.c                    # Windows (MSVC)
 * or:  cmake -B build && cmake --build build
 *
 * Usage: stair_sim [seed] [threshold]
 *     stair_sim            # seed 1, true threshold 0.20
 *     stair_sim 7 0.05     # seed 7, true threshold 0.05
 *
 * Exit code: 0 always.
 */
#define PSY_STAIR_IMPLEMENTATION
#include "psy_stair.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- the simulated observer -------------------------------------------- */

/* Weibull slope and guess rate. A 2AFC task guesses at 0.5; beta = 3 is a
 * typical contrast-detection slope. */
#define OBS_BETA  3.0
#define OBS_GAMMA 0.5

/* p(correct) at contrast x for an observer whose 81.6% point is `alpha`.
 * (1 - exp(-1) = 0.632 of the way from 0.5 to 1.) */
static double observer_p(double x, double alpha) {
    if (x <= 0.0) return OBS_GAMMA;
    return OBS_GAMMA + (1.0 - OBS_GAMMA) * (1.0 - exp(-pow(x / alpha, OBS_BETA)));
}

/* The contrast at which the observer is correct a proportion p of the time. */
static double observer_level(double p, double alpha) {
    double q = (p - OBS_GAMMA) / (1.0 - OBS_GAMMA);
    return alpha * pow(-log(1.0 - q), 1.0 / OBS_BETA);
}

/* --- the generator ----------------------------------------------------- */

/* splitmix64. One line of state, no dependency, and the same stream on every
 * platform, which is what makes a printed track worth comparing. */
static uint64_t g_rng;

static double uniform01(void) {
    uint64_t z;
    g_rng += 0x9E3779B97F4A7C15ULL;
    z = g_rng;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z = z ^ (z >> 31);
    return (double)(z >> 11) * (1.0 / 9007199254740992.0);
}

/* The display cannot show every contrast, so quantize to 12 bits. This is the
 * whole reason psyst_update() takes the level that was shown. */
static double quantize(double x) {
    double q = floor(x * 4096.0 + 0.5) / 4096.0;
    return q > 0.0 ? q : 1.0 / 4096.0;
}

int main(int argc, char** argv) {
    static psyst_stair s;
    psyst_desc d;
    double alpha = 0.20;
    double truth, est_rev, est_med, est_tri, est_last;
    unsigned long seed = 1;
    int n = 0, i;
    const psyst_trial* h;

    if (argc > 1) seed = strtoul(argv[1], NULL, 10);
    if (argc > 2) alpha = atof(argv[2]);
    if (!(alpha > 0.0) || !(alpha < 1.0)) {
        fprintf(stderr, "stair_sim: threshold must be in (0, 1)\n");
        return 0;
    }
    g_rng = (uint64_t)seed;

    /* 1-up-3-down on log contrast: converges on 79.4% correct. The schedule
     * halves the step at each of the first two reversals, so the track closes
     * on threshold fast and then sits still. */
    memset(&d, 0, sizeof(d));
    d.start          = 0.5;
    d.n_up           = 1;
    d.n_down         = 3;
    d.step_type      = PSYST_STEP_LOG;
    d.steps[0]       = 0.30;      /* a factor of 2 */
    d.steps[1]       = 0.15;
    d.steps[2]       = 0.075;
    d.n_steps        = 3;
    d.min            = 1.0 / 4096.0;
    d.max            = 1.0;
    d.initial_rule   = true;
    d.stop_reversals = 12;
    d.stop_trials    = 200;
    d.stop_at_limit  = 20;

    if (!psyst_open(&s, &d)) {
        fprintf(stderr, "stair_sim: %s\n", psyst_error(&s));
        return 0;
    }

    truth = observer_level(psyst_convergence_p(d.n_up, d.n_down, d.step_down_scale),
                           alpha);

    printf("stair_sim: 1-up-3-down, LOG steps 0.30 / 0.15 / 0.075, "
           "initial rule on, seed %lu\n", seed);
    printf("observer: Weibull, alpha %.4f, beta %.1f, guess %.2f; "
           "converges on p = %.4f at contrast %.5f\n",
           alpha, OBS_BETA, OBS_GAMMA,
           psyst_convergence_p(d.n_up, d.n_down, d.step_down_scale), truth);
    printf("\n trial   proposed      shown   p(corr)  resp  step  rev\n");

    while (!psyst_done(&s)) {
        double proposed = psyst_next(&s);
        double shown = quantize(proposed);
        double p = observer_p(shown, alpha);
        int resp = psyst_simulate_response(p, uniform01());
        int ev = psyst_update(&s, shown, resp);
        if (ev < 0) {
            fprintf(stderr, "stair_sim: %s\n", psyst_strerror(ev));
            break;
        }
        h = psyst_history(&s, &n);
        printf("  %4d   %8.5f   %8.5f   %6.3f   %3d  %4d  %s\n",
               n, proposed, shown, p, resp,
               (int)h[n - 1].step_index,
               (ev & PSYST_EVENT_REVERSAL) ? "<--" : "");
    }

    est_rev  = psyst_estimate(&s, PSYST_EST_REVERSALS);
    est_med  = psyst_estimate(&s, PSYST_EST_MEDIAN_REV);
    est_tri  = psyst_estimate(&s, PSYST_EST_TRIALS);
    est_last = psyst_estimate(&s, PSYST_EST_LAST);

    printf("\nstopped after %d trials and %d reversals, reason %d\n",
           psyst_n_trials(&s), psyst_n_reversals(&s), (int)psyst_stop_reason(&s));
    printf("reversal levels:");
    for (i = 0; i < psyst_n_reversals(&s); i++)
        printf(" %.5f", psyst_reversal_level(&s, i));
    printf("\n\n");

    printf("estimator          level    p(corr)   error (log10)   n\n");
    printf("EST_REVERSALS   %8.5f   %6.3f   %+8.4f        %3d\n",
           est_rev, observer_p(est_rev, alpha), log10(est_rev / truth),
           psyst_estimate_count(&s, PSYST_EST_REVERSALS));
    printf("EST_MEDIAN_REV  %8.5f   %6.3f   %+8.4f        %3d\n",
           est_med, observer_p(est_med, alpha), log10(est_med / truth),
           psyst_estimate_count(&s, PSYST_EST_MEDIAN_REV));
    printf("EST_TRIALS      %8.5f   %6.3f   %+8.4f        %3d\n",
           est_tri, observer_p(est_tri, alpha), log10(est_tri / truth),
           psyst_estimate_count(&s, PSYST_EST_TRIALS));
    printf("EST_LAST        %8.5f   %6.3f   %+8.4f        %3d\n",
           est_last, observer_p(est_last, alpha), log10(est_last / truth),
           psyst_estimate_count(&s, PSYST_EST_LAST));
    printf("truth           %8.5f   %6.3f\n", truth,
           observer_p(truth, alpha));
    printf("\nOne track is one sample. The error above is this seed's, not the\n"
           "procedure's: run several seeds before believing a number.\n");
    return 0;
}
