/* quest_sim.c - run psy_quest.h against a simulated observer.
 *
 * Two configurations of the same header on the same task, a 2AFC contrast
 * threshold with a log-Weibull psychometric function:
 *
 *   Psi    (Kontsevich & Tyler 1999): threshold and slope free, stimulus
 *          chosen by minimum expected posterior entropy.
 *   QUEST  (Watson & Pelli 1983): slope and lapse fixed, a Gaussian prior on
 *          the log threshold, stimulus placed at the posterior median.
 *
 * Both are driven by the same simulated observer and the same random stream,
 * so the two columns of output are comparable trial by trial. Each row prints
 * the posterior mean and sd of the threshold next to the truth, which is the
 * number to watch: the sd is what the method is driving down, and the
 * distance from the truth is whether it is driving it down to the right
 * place. Nothing here needs hardware.
 *
 * Build (from the repository root):
 *     cc -O2 -I. -o quest_sim examples/quest_sim.c -lm
 *     cl /O2 /I. examples\quest_sim.c
 *
 * Usage: quest_sim [trials] [seed]
 *
 * Exit code: 0 always.
 */
#define PSY_QUEST_IMPLEMENTATION
#include "psy_quest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The observer. Truth in the same units as the grids: log10 contrast. */
static const double TRUE_THRESHOLD = -1.72;
static const double TRUE_SLOPE     = 3.10;
static const double GUESS_RATE     = 0.50;   /* 2AFC   */
static const double TRUE_LAPSE     = 0.02;

/* splitmix64, so a run replays from its seed on any platform. The header owns
 * no generator: psyq_simulate() takes the variate. */
static uint64_t g_rng;

static double next_u(void) {
    uint64_t z = (g_rng += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z = z ^ (z >> 31);
    return (double)(z >> 11) * (1.0 / 9007199254740992.0);
}

static double g_stim[31];
static double g_alpha[61];
static double g_slope[15];

static void build_axes(void) {
    int i;
    for (i = 0; i < 31; i++) g_stim[i]  = -3.0 + 3.0 * (double)i / 30.0;
    for (i = 0; i < 61; i++) g_alpha[i] = -3.0 + 3.0 * (double)i / 60.0;
    for (i = 0; i < 15; i++) g_slope[i] = 0.5 + 5.5 * (double)i / 14.0;
}

static void psi_desc(psyq_desc* d, int trials) {
    memset(d, 0, sizeof(*d));
    d->pf = PSYQ_PF_GUMBEL;
    d->stim[0] = psyq_values(g_stim, 31);
    d->n_stim = 1;
    d->param[0] = psyq_values(g_alpha, 61);
    d->param[1] = psyq_values(g_slope, 15);
    d->param[2] = psyq_fixed(GUESS_RATE);
    d->param[3] = psyq_fixed(TRUE_LAPSE);
    d->n_param = 4;
    d->select = PSYQ_SELECT_ENTROPY;
    d->stop_trials = trials;
}

/* Classic QUEST: one free parameter, a Gaussian prior on it, and the quantile
 * placement rule. `prior` must outlive the open() call only. */
static void quest_desc(psyq_desc* d, int trials, double* prior) {
    psyq_axis alpha = psyq_values(g_alpha, 61);
    memset(d, 0, sizeof(*d));
    psyq_prior_normal(&alpha, -1.5, 0.8, prior);   /* the experimenter's guess */
    alpha.prior = prior;
    d->pf = PSYQ_PF_GUMBEL;
    d->stim[0] = psyq_values(g_stim, 31);
    d->n_stim = 1;
    d->param[0] = alpha;
    d->param[1] = psyq_fixed(3.5);                 /* Watson & Pelli's beta   */
    d->param[2] = psyq_fixed(GUESS_RATE);
    d->param[3] = psyq_fixed(0.01);                /* and their delta         */
    d->n_param = 4;
    d->select = PSYQ_SELECT_QUANTILE;
    d->select_param = 0;
    d->select_quantile = 0.5;
    d->stop_trials = trials;
}

int main(int argc, char** argv) {
    int trials = (argc > 1) ? atoi(argv[1]) : 60;
    uint64_t seed = (argc > 2) ? strtoull(argv[2], NULL, 0) : 0x5EEDF00Dull;
    double truth[4], prior[61];
    psyq_desc dp, dq;
    static psyq_quest psi, quest;   /* the handle carries the history inline */
    int t;

    if (trials < 1) trials = 1;
    if (trials > 2000) trials = 2000;
    build_axes();
    truth[0] = TRUE_THRESHOLD;
    truth[1] = TRUE_SLOPE;
    truth[2] = GUESS_RATE;
    truth[3] = TRUE_LAPSE;

    psi_desc(&dp, trials);
    quest_desc(&dq, trials, prior);
    printf("quest_sim: %d trials, seed 0x%llx\n", trials, (unsigned long long)seed);
    printf("truth: threshold %.3f, slope %.2f, guess %.2f, lapse %.3f\n",
           truth[0], truth[1], truth[2], truth[3]);
    printf("Psi grid  %llu bytes, QUEST grid %llu bytes\n",
           (unsigned long long)psyq_memory_size(&dp),
           (unsigned long long)psyq_memory_size(&dq));
    if (!psyq_open(&psi, &dp))   { fputs(psyq_error(&psi), stderr);   return 0; }
    if (!psyq_open(&quest, &dq)) { fputs(psyq_error(&quest), stderr); psyq_close(&psi); return 0; }

    printf("\n            Psi (threshold and slope free)      QUEST (threshold only)\n");
    printf("trial    x      k   mean      sd    err      x      k   mean      sd    err\n");
    g_rng = seed;
    for (t = 0; t < trials; t++) {
        int sp = psyq_next(&psi);
        int sq = psyq_next(&quest);
        /* One variate per handle per trial: the two runs see the same stream
         * but not the same draws, since they propose different stimuli. */
        int kp = psyq_simulate(&psi, sp, truth, next_u());
        int kq = psyq_simulate(&quest, sq, truth, next_u());
        double ep[4] = {0}, eq[4] = {0};
        psyq_update(&psi, sp, kp);
        psyq_update(&quest, sq, kq);
        psyq_estimate(&psi, PSYQ_EST_MEAN, ep);
        psyq_estimate(&quest, PSYQ_EST_MEAN, eq);
        printf("%5d %6.3f %2d %7.3f %7.4f %+6.3f  %6.3f %2d %7.3f %7.4f %+6.3f\n",
               t + 1,
               psyq_stim_value(&psi, sp, 0), kp, ep[0], psyq_sd(&psi, 0), ep[0] - truth[0],
               psyq_stim_value(&quest, sq, 0), kq, eq[0], psyq_sd(&quest, 0), eq[0] - truth[0]);
    }

    {
        double ep[4] = {0}, eq[4] = {0};
        psyq_estimate(&psi, PSYQ_EST_MEAN, ep);
        psyq_estimate(&quest, PSYQ_EST_MEAN, eq);
        printf("\nfinal Psi:   threshold %.4f (err %+.4f, sd %.4f), slope %.3f (err %+.3f, sd %.3f)\n",
               ep[0], ep[0] - truth[0], psyq_sd(&psi, 0),
               ep[1], ep[1] - truth[1], psyq_sd(&psi, 1));
        printf("final QUEST: threshold %.4f (err %+.4f, sd %.4f), slope fixed at %.2f\n",
               eq[0], eq[0] - truth[0], psyq_sd(&quest, 0), eq[1]);
        printf("stop reason: Psi %d, QUEST %d (1 = trials, 2 = entropy, 3 = sd)\n",
               (int)psyq_stop_reason(&psi), (int)psyq_stop_reason(&quest));
        printf("posterior entropy: Psi %.4f bits, QUEST %.4f bits\n",
               psyq_entropy(&psi), psyq_entropy(&quest));
        printf("\nQUEST is biased here on purpose: its slope is fixed at 3.50 and its\n"
               "lapse at 0.010, and the observer has 3.10 and 0.020. Psi estimates the\n"
               "slope instead, which is what the second free axis costs and buys.\n");
    }
    psyq_close(&psi);
    psyq_close(&quest);
    return 0;
}
