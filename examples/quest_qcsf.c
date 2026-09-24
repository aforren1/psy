/* quest_qcsf.c - the quick CSF as a psy_quest.h custom model.
 *
 * Lesmes, Lu, Baek & Albright (2010), "Bayesian adaptive estimation of the
 * contrast sensitivity function: the quick CSF method", J. Vision 10(3):17.
 * The model is four parameters and a formula, which is why psy_quest.h ships
 * it as an example of PSYQ_PF_CUSTOM rather than as a built-in:
 *
 *   log10 S(f) = gmax - log10(2) * ( (log10 f - fmax) / (bw log10(2) / 2) )^2
 *
 * truncated below the peak frequency at gmax - delta. The four parameters are
 * the peak gain gmax (log10 sensitivity), the peak frequency fmax (log10
 * cycles/degree), the bandwidth bw (octaves, full width at half maximum) and
 * the truncation delta (log10 units below the peak). A trial is a grating at
 * one frequency and one contrast, scored correct or not by a log-Weibull on
 * the distance from the contrast to 1 / S(f).
 *
 * The stimulus grid is two-dimensional here, which is the whole point: the
 * method chooses BOTH the frequency and the contrast of the next trial.
 *
 * It is also both ways of handing psy_quest.h a model. qcsf_pf() is the
 * per-cell callback (desc.pf_fn), which the header calls S*P times at open.
 * qcsf_pf_batch() is the batch callback (desc.pf_batch), which it calls once
 * per stimulus with the whole parameter grid as a matrix: 140 calls here
 * instead of 156,800. The two are checked against each other at startup and
 * must agree exactly. A binding writes the batch one, because 156,800 calls
 * into a scripting language is the cost that matters and one array expression
 * per stimulus is not.
 *
 * The grid sizes are modest so the program runs in a second. A qCSF grid with
 * the grain Lesmes et al. use needs about 115 MB of likelihood table, which
 * is what psyq_desc.no_table is for; this program prints psyq_memory_size()
 * before it opens so the number is in the log either way.
 *
 * Build (from the repository root):
 *     cc -O2 -I. -o quest_qcsf examples/quest_qcsf.c -lm
 *     cl /O2 /I. examples\quest_qcsf.c
 *
 * Usage: quest_qcsf [trials] [seed]
 *
 * Exit code: 0 always.
 */
#define PSY_QUEST_IMPLEMENTATION
#include "psy_quest.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NFREQ   10
#define NCON    14
#define N_GMAX   8
#define N_FMAX   7
#define N_BW     5
#define N_DELTA  4

/* Fixed parts of the observer: the psychometric slope on log10 contrast, the
 * 2AFC guess rate and the lapse rate. Free them by giving them axes of their
 * own and widening the parameter vector. */
#define QCSF_SLOPE  2.0
#define QCSF_GUESS  0.5
#define QCSF_LAPSE  0.04

/* log10 sensitivity at log10 frequency lf, per the log parabola above. */
static double qcsf_log_s(double lf, const double* p) {
    const double kappa = 0.30102999566398120;   /* log10(2) */
    double gmax = p[0], fmax = p[1], bw = p[2], delta = p[3];
    double half = 0.5 * bw * kappa;             /* half width, log10 units */
    double d = (lf - fmax) / half;
    double s = gmax - kappa * d * d;
    double floor_lo = gmax - delta;             /* low-frequency truncation */
    if (lf < fmax && s < floor_lo) s = floor_lo;
    return s;
}

/* The custom psychometric function, one cell per call: two outcomes,
 * p[1] = correct. It must be a pure function of its arguments, and the header
 * runs it S*P times at open. */
static void qcsf_pf(void* ctx, const double* stim, const double* params, double* p) {
    double thresh, z, f;
    (void)ctx;
    thresh = -qcsf_log_s(stim[0], params);      /* log10 threshold contrast */
    z = QCSF_SLOPE * (stim[1] - thresh);
    f = 1.0 - exp(-pow(10.0, z));
    p[1] = QCSF_GUESS + (1.0 - QCSF_GUESS - QCSF_LAPSE) * f;
    p[0] = 1.0 - p[1];
}

/* The same model, a batch per call. `stims` is S rows of 2 (log frequency,
 * log contrast), `params` is P rows of 4, and out[(s*P + i)*2 + k] takes the
 * K probabilities of every cell. The header calls this once per stimulus.
 *
 * In C the loop is the same arithmetic as above, written once; the reason the
 * entry point exists is that in a binding the inner loop is not a loop. A
 * NumPy callable receives the P x 4 matrix as an array and evaluates the whole
 * column of thresholds in one expression, so the interpreter is entered once
 * per stimulus instead of once per cell. */
static void qcsf_pf_batch(void* ctx, const double* stims, int S,
                          const double* params, int P, float* out) {
    int s, i;
    for (s = 0; s < S; s++) {
        const double* stim = stims + (size_t)s * 2;
        for (i = 0; i < P; i++) {
            double p[2] = {0};
            qcsf_pf(ctx, stim, params + (size_t)i * 4, p);
            out[((size_t)s * (size_t)P + (size_t)i) * 2 + 0] = (float)p[0];
            out[((size_t)s * (size_t)P + (size_t)i) * 2 + 1] = (float)p[1];
        }
    }
}

static uint64_t g_rng;

static double next_u(void) {
    uint64_t z = (g_rng += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z = z ^ (z >> 31);
    return (double)(z >> 11) * (1.0 / 9007199254740992.0);
}

static double g_freq[NFREQ], g_con[NCON];
static double g_gmax[N_GMAX], g_fmax[N_FMAX], g_bw[N_BW], g_delta[N_DELTA];

static void build_axes(void) {
    int i;
    /* 0.5 to 16 cycles/degree, log spaced; contrast 0.2% to 100%, log10. */
    for (i = 0; i < NFREQ; i++) g_freq[i] = log10(0.5) + (log10(16.0) - log10(0.5)) * (double)i / (NFREQ - 1);
    for (i = 0; i < NCON; i++)  g_con[i]  = -2.7 + 2.7 * (double)i / (NCON - 1);
    for (i = 0; i < N_GMAX; i++)  g_gmax[i]  = 1.4 + 1.4 * (double)i / (N_GMAX - 1);   /* 25 to 630 */
    for (i = 0; i < N_FMAX; i++)  g_fmax[i]  = log10(0.8) + (log10(8.0) - log10(0.8)) * (double)i / (N_FMAX - 1);
    for (i = 0; i < N_BW; i++)    g_bw[i]    = 1.0 + 4.0 * (double)i / (N_BW - 1);     /* octaves */
    for (i = 0; i < N_DELTA; i++) g_delta[i] = 0.1 + 1.4 * (double)i / (N_DELTA - 1);
}

int main(int argc, char** argv) {
    int trials = (argc > 1) ? atoi(argv[1]) : 60;
    uint64_t seed = (argc > 2) ? strtoull(argv[2], NULL, 0) : 0xC5F1234ull;
    psyq_desc d, dcell;
    static psyq_quest q;
    static psyq_quest qcell;
    double truth[4], est[4] = {0};
    size_t bytes, bytes_cell;
    double worst = 0.0;
    int t, i;

    if (trials < 1) trials = 1;
    if (trials > 2000) trials = 2000;
    build_axes();

    /* The observer: a peak sensitivity of about 200 at 2.5 c/deg, 3 octaves
     * wide. None of these is a grid point, so the estimates cannot be exact. */
    truth[0] = log10(200.0);
    truth[1] = log10(2.5);
    truth[2] = 3.0;
    truth[3] = 0.6;

    memset(&d, 0, sizeof(d));
    d.pf = PSYQ_PF_CUSTOM;
    d.pf_batch = qcsf_pf_batch;   /* the batch entry point; see above */
    d.n_outcomes = 2;
    d.stim[0] = psyq_values(g_freq, NFREQ);      /* log10 cycles/degree */
    d.stim[1] = psyq_values(g_con, NCON);        /* log10 contrast      */
    d.n_stim = 2;
    d.param[0] = psyq_values(g_gmax, N_GMAX);
    d.param[1] = psyq_values(g_fmax, N_FMAX);
    d.param[2] = psyq_values(g_bw, N_BW);
    d.param[3] = psyq_values(g_delta, N_DELTA);
    d.n_param = 4;
    d.stop_trials = trials;

    dcell = d;
    dcell.pf_batch = NULL;
    dcell.pf_fn = qcsf_pf;        /* the same model, one cell per call */

    bytes = psyq_memory_size(&d);
    bytes_cell = psyq_memory_size(&dcell);
    printf("quest_qcsf: stimulus grid %d x %d = %d, parameter grid %d x %d x %d x %d = %d,\n",
           NFREQ, NCON, NFREQ * NCON, N_GMAX, N_FMAX, N_BW, N_DELTA,
           N_GMAX * N_FMAX * N_BW * N_DELTA);
    /* The table is one float per cell, plus one more per (stimulus,
     * parameter) for the tabulated outcome entropy, since no axis here is
     * flagged nuisance. psyq_memory_size() counts all of it. */
    printf("            2 outcomes, %d table cells (%d floats),"
           " psyq_memory_size = %llu bytes (%.2f MB)\n",
           NFREQ * NCON * N_GMAX * N_FMAX * N_BW * N_DELTA * 2,
           NFREQ * NCON * N_GMAX * N_FMAX * N_BW * N_DELTA * 3,
           (unsigned long long)bytes, (double)bytes / (1024.0 * 1024.0));
    printf("            %d pf_batch calls at open (one per stimulus), or %d pf_fn calls\n",
           NFREQ * NCON, NFREQ * NCON * N_GMAX * N_FMAX * N_BW * N_DELTA);
    printf("            psyq_memory_size with pf_fn instead = %llu bytes; the batch path\n"
           "            also carries the P x 4 parameter matrix and a P x 2 float staging row\n",
           (unsigned long long)bytes_cell);
    if (bytes == 0) { fputs("quest_qcsf: the desc is invalid\n", stderr); return 0; }

    if (!psyq_open(&q, &d)) { fputs(psyq_error(&q), stderr); return 0; }

    /* The two entry points are the same model, so they must give the same
     * selection landscape. Checked here rather than asserted in the manual. */
    if (!psyq_open(&qcell, &dcell)) { fputs(psyq_error(&qcell), stderr); psyq_close(&q); return 0; }
    for (i = 0; i < NFREQ * NCON; i++) {
        double a = psyq_expected_entropy(&q, i), b = psyq_expected_entropy(&qcell, i);
        double dd = (a > b) ? a - b : b - a;
        if (dd > worst) worst = dd;
    }
    printf("            batch vs per-cell: same first stimulus (%d vs %d), "
           "worst score difference %.3g bits\n",
           psyq_next(&q), psyq_next(&qcell), worst);
    psyq_close(&qcell);
    printf("truth: peak gain %.3f (sensitivity %.0f), peak frequency %.3f (%.2f c/deg),"
           " bandwidth %.2f oct, truncation %.2f\n\n",
           truth[0], pow(10.0, truth[0]), truth[1], pow(10.0, truth[1]), truth[2], truth[3]);

    printf("trial   c/deg  contrast   k   gain   fpeak     bw  trunc   entropy\n");
    g_rng = seed;
    for (t = 0; t < trials; t++) {
        int s = psyq_next(&q);
        int k = psyq_simulate(&q, s, truth, next_u());
        psyq_update(&q, s, k);
        if (t < 10 || (t + 1) % 10 == 0) {
            psyq_estimate(&q, PSYQ_EST_MEAN, est);
            printf("%5d %7.2f %9.4f %3d %6.3f %7.3f %6.2f %6.2f %9.4f\n",
                   t + 1, pow(10.0, psyq_stim_value(&q, s, 0)),
                   pow(10.0, psyq_stim_value(&q, s, 1)), k,
                   est[0], est[1], est[2], est[3], psyq_entropy(&q));
        }
    }

    psyq_estimate(&q, PSYQ_EST_MEAN, est);
    printf("\nfinal: peak gain %.3f (truth %.3f), peak frequency %.3f (truth %.3f),\n"
           "       bandwidth %.2f (truth %.2f), truncation %.2f (truth %.2f)\n",
           est[0], truth[0], est[1], truth[1], est[2], truth[2], est[3], truth[3]);
    printf("       posterior sd: gain %.3f, frequency %.3f, bandwidth %.2f, truncation %.2f\n",
           psyq_sd(&q, 0), psyq_sd(&q, 1), psyq_sd(&q, 2), psyq_sd(&q, 3));

    printf("\nrecovered sensitivity, posterior mean parameters against the truth:\n");
    printf("   c/deg    estimate      truth\n");
    for (i = 0; i < NFREQ; i++) {
        double lf = g_freq[i];
        printf("%8.2f %11.1f %10.1f\n", pow(10.0, lf),
               pow(10.0, qcsf_log_s(lf, est)), pow(10.0, qcsf_log_s(lf, truth)));
    }
    psyq_close(&q);
    return 0;
}
