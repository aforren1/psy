/* gp_pairwise - learning an observer's preferences from comparisons with
 * PSYGP_LIK_PAIRWISE.
 *
 * Each trial shows two stimuli and asks which one the observer prefers. The
 * observer has a utility over a 2-D box, the bump of examples/gp_optimize.c
 * with its peak at (0.3, 0.7), and prefers x1 to x2 with probability
 * Phi(2 (u(x1) - u(x2))). psy_gp.h models the utility as a GP and each
 * comparison as a probit on the difference of two of its values; the header
 * proposes the next pair (psygp_next_pair) and reports the best stimulus found
 * so far (psygp_argmax). The program runs three pair-selection rules over a
 * dozen response streams and prints how far psygp_argmax() is from the peak
 * after every 20 comparisons, and in how many streams it ends within 0.1:
 *
 *   PSYGP_ACQ_BALD      the best stimulus so far against the candidate whose
 *                       comparison with it carries the most information
 *   PSYGP_ACQ_BALV      the same with the variance of the comparison's outcome
 *   PSYGP_ACQ_THOMPSON  the favorites of two posterior draws of the utility
 *
 *   cc -O2 -I. -o gp_pairwise examples/gp_pairwise.c -lm
 *   cl /O2 /I. examples\gp_pairwise.c
 *
 * Exit code: 0.
 */
#define PSY_GP_IMPLEMENTATION
#include "psy_gp.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define N_PAIRS 80
#define N_STREAMS 12
#define N_MARKS (N_PAIRS / 20)

static uint64_t rng_state;

static double rng_u(void) {
    uint64_t z = (rng_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z = z ^ (z >> 31);
    return (double)(z >> 11) * (1.0 / 9007199254740992.0);
}

static double rng_cb(void* ctx) { (void)ctx; return rng_u(); }

static double utility(const double* x) {
    double a = x[0] - 0.3, b = x[1] - 0.7;
    return 2.0 * exp(-(a * a + b * b) / (2.0 * 0.15 * 0.15)) - 1.0;
}

static int prefer(const double* x1, const double* x2) {
    double p[2];
    p[1] = 0.5 * erfc(-(2.0 * (utility(x1) - utility(x2))) / sqrt(2.0));
    p[0] = 1.0 - p[1];
    return psygp_simulate_outcome(p, 2, rng_u());
}

static double session(psygp_acq acq, int stream, double* dist) {
    psygp_desc d;
    psygp_gp g;
    clock_t spent = 0;
    int n = 0;
    rng_state = 0x243F6A8885A308D3ull + (uint64_t)stream * 0x9E3779B97F4A7C15ull;
    memset(&d, 0, sizeof(d));
    d.n_dims = 2;
    d.lo[0] = 0.0; d.hi[0] = 1.0; d.lo[1] = 0.0; d.hi[1] = 1.0;
    d.lik = PSYGP_LIK_PAIRWISE;
    d.acq = acq;
    d.grid[0] = 15; d.grid[1] = 15;
    d.n_init = 6;
    d.fit = true; d.fit_every = 10;
    d.refine_steps = 2;
    d.stop_trials = N_PAIRS;
    d.rng = rng_cb;
    if (!psygp_open(&g, &d)) {
        fprintf(stderr, "gp_pairwise: %s\n", psygp_error(&g));
        return 0.0;
    }
    while (!psygp_done(&g)) {
        double x1[2], x2[2], am[2];
        clock_t t0 = clock();
        psygp_next_pair(&g, x1, x2);
        psygp_update_pair(&g, x1, x2, prefer(x1, x2));
        spent += clock() - t0;
        n = psygp_n_trials(&g);
        if (n % 20 == 0) {
            psygp_argmax(&g, am, NULL);
            dist[n / 20 - 1] = hypot(am[0] - 0.3, am[1] - 0.7);
        }
    }
    psygp_close(&g);
    return 1000.0 * (double)spent / CLOCKS_PER_SEC / (double)n;
}

int main(void) {
    static const psygp_acq acqs[3] = { PSYGP_ACQ_BALD, PSYGP_ACQ_BALV, PSYGP_ACQ_THOMPSON };
    static const char* const names[3] = { "BALD", "BALV", "Thompson" };
    printf("gp_pairwise: a preference observer with its favorite stimulus at (0.3, 0.7),\n"
           "%d comparisons, %d streams; the distance of psygp_argmax() from it\n\n",
           N_PAIRS, N_STREAMS);
    printf("  method     ");
    for (int k = 0; k < N_MARKS; k++) printf("  %5d", 20 * (k + 1));
    printf("   found   ms/pair\n");
    for (int a = 0; a < 3; a++) {
        double mean[N_MARKS] = { 0.0 }, ms = 0.0;
        int found = 0;
        for (int s = 0; s < N_STREAMS; s++) {
            double dist[N_MARKS];
            memset(dist, 0, sizeof(dist));
            ms += session(acqs[a], s, dist) / N_STREAMS;
            for (int k = 0; k < N_MARKS; k++) mean[k] += dist[k] / N_STREAMS;
            if (dist[N_MARKS - 1] < 0.1) found++;
        }
        printf("  %-9s  ", names[a]);
        for (int k = 0; k < N_MARKS; k++) printf("  %5.3f", mean[k]);
        printf("   %2d/%-2d  %7.2f\n", found, N_STREAMS, ms);
    }
    return 0;
}
