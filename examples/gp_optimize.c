/* gp_optimize - finding the stimulus an observer rates highest with psy_gp.h's
 * optimization acquisitions, rather than a threshold.
 *
 * The field is a bump over a 2-D box with its maximum at (0.3, 0.7): a
 * continuous rating y = bump(x) + noise under PSYGP_LIK_GAUSSIAN, and a yes/no
 * preference p = Phi(2 bump(x)) under PSYGP_LIK_BERNOULLI. Each of the three
 * acquisitions runs a session on both, and after every tenth trial the program
 * asks psygp_argmax() where the maximum is and prints how far that is from the
 * true one, averaged over a dozen response streams, and in how many of them
 * the final argmax is within 0.1 of it:
 *
 *   PSYGP_ACQ_UCB       the target quantity's mean plus desc.acq_beta sds
 *   PSYGP_ACQ_EI        its expected improvement over the best mean at a trial
 *   PSYGP_ACQ_THOMPSON  the argmax of one joint posterior sample (desc.rng)
 *
 *   cc -O2 -I. -o gp_optimize examples/gp_optimize.c -lm
 *   cl /O2 /I. examples\gp_optimize.c
 *
 * Exit code: 0.
 */
#define PSY_GP_IMPLEMENTATION
#include "psy_gp.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define N_TRIALS 80
#define N_STREAMS 12
#define N_MARKS (N_TRIALS / 10)

static uint64_t rng_state;

static double rng_u(void) {
    uint64_t z = (rng_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z = z ^ (z >> 31);
    return (double)(z >> 11) * (1.0 / 9007199254740992.0);
}

static double rng_cb(void* ctx) { (void)ctx; return rng_u(); }

static double bump(const double* x) {
    double a = x[0] - 0.3, b = x[1] - 0.7;
    return 2.0 * exp(-(a * a + b * b) / (2.0 * 0.15 * 0.15)) - 1.0;
}

/* One session; dist[k] gets the argmax's distance from the optimum after
 * trial 10 (k + 1). Returns the CPU milliseconds per trial of next + update. */
static double session(psygp_acq acq, psygp_lik lik, int stream, double* dist) {
    psygp_desc d;
    psygp_gp g;
    clock_t spent = 0;
    int n = 0;
    rng_state = 0x243F6A8885A308D3ull + (uint64_t)stream * 0x9E3779B97F4A7C15ull;
    memset(&d, 0, sizeof(d));
    d.n_dims = 2;
    d.lo[0] = 0.0; d.hi[0] = 1.0; d.lo[1] = 0.0; d.hi[1] = 1.0;
    d.lik = lik;
    d.acq = acq;
    d.grid[0] = 21; d.grid[1] = 21;
    d.n_init = 8;
    d.fit = true; d.fit_every = 10;
    d.refine_steps = 2;
    d.stop_trials = N_TRIALS;
    d.rng = rng_cb;
    if (!psygp_open(&g, &d)) {
        fprintf(stderr, "gp_optimize: %s\n", psygp_error(&g));
        return 0.0;
    }
    while (!psygp_done(&g)) {
        double x[2], fv, am[2];
        clock_t t0 = clock();
        psygp_next(&g, x);
        fv = bump(x);
        if (lik == PSYGP_LIK_GAUSSIAN) {
            double u1 = rng_u(), u2 = rng_u();
            if (u1 < 1e-300) u1 = 1e-300;
            psygp_update_real(&g, x, fv + 0.1 * sqrt(-2.0 * log(u1)) *
                                          cos(6.283185307179586 * u2));
        } else {
            double p[2];
            p[1] = 0.5 * erfc(-(2.0 * fv) / sqrt(2.0));
            p[0] = 1.0 - p[1];
            psygp_update(&g, x, psygp_simulate_outcome(p, 2, rng_u()));
        }
        spent += clock() - t0;
        n = psygp_n_trials(&g);
        if (n % 10 == 0) {
            psygp_argmax(&g, am, NULL);
            dist[n / 10 - 1] = hypot(am[0] - 0.3, am[1] - 0.7);
        }
    }
    psygp_close(&g);
    return 1000.0 * (double)spent / CLOCKS_PER_SEC / (double)n;
}

int main(void) {
    static const psygp_acq acqs[3] = { PSYGP_ACQ_UCB, PSYGP_ACQ_EI, PSYGP_ACQ_THOMPSON };
    static const char* const names[3] = { "UCB", "EI", "Thompson" };
    static const psygp_lik liks[2] = { PSYGP_LIK_GAUSSIAN, PSYGP_LIK_BERNOULLI };
    static const char* const lnames[2] = { "rating (GAUSSIAN)", "yes/no (BERNOULLI)" };
    printf("gp_optimize: a bump with its maximum at (0.3, 0.7), %d trials, %d streams;\n"
           "the distance of psygp_argmax() from the maximum after every 10 trials\n\n",
           N_TRIALS, N_STREAMS);
    for (int l = 0; l < 2; l++) {
        printf("%s\n  method     ", lnames[l]);
        for (int k = 0; k < N_MARKS; k++) printf("  %5d", 10 * (k + 1));
        printf("   found   ms/trial\n");
        for (int a = 0; a < 3; a++) {
            double mean[N_MARKS] = { 0.0 }, ms = 0.0;
            int found = 0;
            for (int s = 0; s < N_STREAMS; s++) {
                double dist[N_MARKS];
                memset(dist, 0, sizeof(dist));
                ms += session(acqs[a], liks[l], s, dist) / N_STREAMS;
                for (int k = 0; k < N_MARKS; k++) mean[k] += dist[k] / N_STREAMS;
                if (dist[N_MARKS - 1] < 0.1) found++;
            }
            printf("  %-9s  ", names[a]);
            for (int k = 0; k < N_MARKS; k++) printf("  %5.3f", mean[k]);
            printf("   %2d/%-2d  %7.2f\n", found, N_STREAMS, ms);
        }
        printf("\n");
    }
    return 0;
}
