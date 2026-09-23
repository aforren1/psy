/* quest_bench.c - time psyq_next() and psyq_update() on this machine.
 *
 * psy_quest.h's manual states operation counts, not times: one psyq_next()
 * under PSYQ_SELECT_ENTROPY sweeps the whole likelihood table, S*P*K cells,
 * with a multiply-add per cell and a logarithm per stimulus and outcome; one
 * psyq_update() reads one outcome's row, P cells, twice. What those counts
 * cost depends on the machine, the compiler and whether the table fits in
 * cache, so this program measures them and prints the counts beside the
 * times. The numbers this prints are the numbers to put in a log, not the
 * ones in the manual.
 *
 * Two yardsticks come with them. The memory floor is the same dot product
 * over the same bytes with nothing else in it, so the gap between it and a
 * selection is what the selection costs beyond moving the table. The frame
 * budget is 16 ms: a psyq_next() plus a psyq_update() that fit in one display
 * frame can run between trials on the experiment's own thread, with no
 * pacing and no worker.
 *
 * The desc benchmarked is Psi-marginal (Prins 2013): 31 stimuli by a
 * 61 x 12 x 5 x 5 parameter grid, guess and lapse flagged nuisance so the
 * selection entropy is the entropy of the marginal. Five configurations run:
 * plain Psi (guess and lapse fixed, a 24-times smaller grid), the full grid
 * without the nuisance flags, the full grid with them, the no-table path,
 * and a random 8-stimulus subset per trial.
 *
 * psy_quest.h itself includes no clock: it is pure computation and does no
 * OS calls. The clock here is psy_rt.h's, which is what an experiment would
 * bracket the call with anyway. PSYRT_NO_THREADS drops psy_rt.h's deadline
 * worker, so nothing but the clock is compiled in and no -pthread is needed.
 *
 * Build (from the repository root):
 *     cc -O2 -I. -o quest_bench examples/quest_bench.c -lm
 *     cl /O2 /I. examples\quest_bench.c
 *
 * Usage: quest_bench [trials]
 *
 * Exit code: 0 always.
 */
#define PSYRT_NO_THREADS
#define PSY_RT_IMPLEMENTATION
#include "psy_rt.h"

#define PSY_QUEST_IMPLEMENTATION
#include "psy_quest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NS 31
#define NA 61
#define NB 12
#define NG 5
#define NL 5

static double g_stim[NS], g_alpha[NA], g_beta[NB], g_guess[NG], g_lapse[NL];

static void build_axes(void) {
    int i;
    for (i = 0; i < NS; i++) g_stim[i]  = -3.0 + 3.0 * (double)i / (NS - 1);
    for (i = 0; i < NA; i++) g_alpha[i] = -3.0 + 3.0 * (double)i / (NA - 1);
    for (i = 0; i < NB; i++) g_beta[i]  = 0.5 + 5.5 * (double)i / (NB - 1);
    for (i = 0; i < NG; i++) g_guess[i] = 0.45 + 0.10 * (double)i / (NG - 1);
    for (i = 0; i < NL; i++) g_lapse[i] = 0.00 + 0.06 * (double)i / (NL - 1);
}

static uint64_t g_rng = 0x1234567890ABCDEFull;

static double next_u(void) {
    uint64_t z = (g_rng += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z = z ^ (z >> 31);
    return (double)(z >> 11) * (1.0 / 9007199254740992.0);
}

static double bench_rng(void* ctx) { (void)ctx; return next_u(); }

static void base_desc(psyq_desc* d, bool nuisance, bool no_table, int subset, bool psi) {
    memset(d, 0, sizeof(*d));
    d->pf = PSYQ_PF_GUMBEL;
    d->stim[0] = psyq_values(g_stim, NS);
    d->n_stim = 1;
    d->param[0] = psyq_values(g_alpha, NA);
    d->param[1] = psyq_values(g_beta, NB);
    if (psi) {
        d->param[2] = psyq_fixed(0.5);
        d->param[3] = psyq_fixed(0.02);
    } else {
        d->param[2] = psyq_values(g_guess, NG);
        d->param[3] = psyq_values(g_lapse, NL);
    }
    d->param[2].nuisance = nuisance;
    d->param[3].nuisance = nuisance;
    d->n_param = 4;
    d->no_table = no_table;
    d->stop_trials = 100000;
    if (subset > 0) {
        d->rng = bench_rng;
        d->subset_size = subset;
    }
}

static psyq_quest g_q;

/* The floor. psyq_next() under PSYQ_SELECT_ENTROPY is S*K dot products of a
 * contiguous float row against the posterior, so the fastest that pattern can
 * run on this machine, with nothing but the loads and the multiply-adds in
 * it, is what the table sweeps should be compared against. Same shape as the
 * header's kernel, same sizes, no entropy bookkeeping, and the same accumulator
 * type. */
static double floor_dot(const double* a, const float* b, int n) {
    double s0 = 0.0, s1 = 0.0, s2 = 0.0, s3 = 0.0;
    int i, m = n & ~3;
    for (i = 0; i < m; i += 4) {
        s0 += a[i]     * (double)b[i];
        s1 += a[i + 1] * (double)b[i + 1];
        s2 += a[i + 2] * (double)b[i + 2];
        s3 += a[i + 3] * (double)b[i + 3];
    }
    for (; i < n; i++) s0 += a[i] * (double)b[i];
    return (s0 + s1) + (s2 + s3);
}

static void run_floor(int trials) {
    size_t cells = (size_t)NS * (size_t)NA * NB * NG * NL * 2;
    int P = NA * NB * NG * NL;
    float* tab = (float*)malloc(cells * sizeof(float));
    double* post = (double*)malloc((size_t)P * sizeof(double));
    uint64_t best = (uint64_t)-1;
    double sink = 0.0;
    size_t i;
    int t, s, k;
    if (!tab || !post) { free(tab); free(post); return; }
    for (i = 0; i < cells; i++) tab[i] = 0.5f;
    for (t = 0; t < P; t++) post[t] = 1.0 / (double)P;
    for (t = 0; t < trials; t++) {
        uint64_t t0, dt;
        /* Perturb the posterior first: without this the pass is the same
         * computation every time and an optimizer is free to hoist it out of
         * the loop, which measures nothing. */
        post[t % P] = 1.0 / (double)P + (double)t * 1e-9;
        t0 = psyrt_now_ns();
        for (s = 0; s < NS; s++)
            for (k = 0; k < 2; k++)
                sink += floor_dot(post, tab + ((size_t)s * 2 + (size_t)k) * (size_t)P, P);
        dt = psyrt_now_ns() - t0;
        if (dt < best) best = dt;
    }
    printf("%-22s pass: %8.3f ms best     %10llu cells, %6.2f ns/cell  (sink %.1f)\n",
           "memory floor", (double)best / 1e6, (unsigned long long)cells,
           (double)best / (double)cells, sink);
    free(tab);
    free(post);
}

/* Median of the per-call times: a mean over a handful of calls hides the one
 * that took a page fault, and the median is what a trial will usually see. */
static void sort_u64(uint64_t* v, int n) {
    int gap, i, j;
    for (gap = n / 2; gap > 0; gap /= 2)
        for (i = gap; i < n; i++) {
            uint64_t tmp = v[i];
            j = i;
            while (j >= gap && v[j - gap] > tmp) { v[j] = v[j - gap]; j -= gap; }
            v[j] = tmp;
        }
}

#define MAX_TRIALS 512
static uint64_t g_next_ns[MAX_TRIALS];
static uint64_t g_upd_ns[MAX_TRIALS];

#define FRAME_NS 16000000.0

static void run_one(const char* name, bool nuisance, bool no_table, int subset,
                    int trials, bool psi) {
    psyq_desc d;
    double truth[4];
    long long cells, sweep;
    int t;
    uint64_t sum_next = 0, sum_upd = 0;
    double frame, per_cell;

    base_desc(&d, nuisance, no_table, subset, psi);
    if (!psyq_open(&g_q, &d)) { fprintf(stderr, "%s: %s\n", name, psyq_error(&g_q)); return; }
    truth[0] = -1.7; truth[1] = 3.1; truth[2] = 0.5; truth[3] = 0.02;

    for (t = 0; t < trials; t++) {
        uint64_t t0, t1, t2;
        int s, k;
        t0 = psyrt_now_ns();
        s = psyq_next(&g_q);
        t1 = psyrt_now_ns();
        k = psyq_simulate(&g_q, s, truth, next_u());
        t2 = psyrt_now_ns();
        psyq_update(&g_q, s, k);
        g_next_ns[t] = t1 - t0;
        g_upd_ns[t] = psyrt_now_ns() - t2;
        sum_next += g_next_ns[t];
        sum_upd += g_upd_ns[t];
    }
    sort_u64(g_next_ns, trials);
    sort_u64(g_upd_ns, trials);

    sweep = (subset > 0 ? subset : psyq_n_stim(&g_q));
    cells = sweep * (long long)psyq_n_param(&g_q) * psyq_n_outcomes(&g_q);
    printf("%-22s next: %8.3f ms median, %8.3f ms mean   %10lld cells, %6.2f ns/cell\n",
           name,
           (double)g_next_ns[trials / 2] / 1e6, (double)sum_next / (double)trials / 1e6,
           cells, (double)g_next_ns[trials / 2] / (double)cells);
    printf("%-22s upd:  %8.3f us median, %8.3f us mean   %10d cells, %6.2f ns/cell\n",
           "", (double)g_upd_ns[trials / 2] / 1e3, (double)sum_upd / (double)trials / 1e3,
           psyq_n_param(&g_q),
           (double)g_upd_ns[trials / 2] / (double)psyq_n_param(&g_q));

    /* The frame budget: one next plus one update, against 16 ms. */
    frame = (double)(g_next_ns[trials / 2] + g_upd_ns[trials / 2]);
    per_cell = (double)g_next_ns[trials / 2] / (double)cells;
    printf("%-22s frame:%8.3f ms next+update, %s 16 ms (%.2fx), "
           "one frame holds %.1f M cells\n",
           "", frame / 1e6, (frame <= FRAME_NS) ? "FITS  " : "OVER  ",
           (frame > 0.0) ? FRAME_NS / frame : 0.0,
           (per_cell > 0.0) ? FRAME_NS / per_cell / 1e6 : 0.0);
    psyq_close(&g_q);
}

int main(int argc, char** argv) {
    int trials = (argc > 1) ? atoi(argv[1]) : 40;
    psyq_desc d;
    psyrt_clock_info clk;

    if (trials < 3) trials = 3;
    if (trials > MAX_TRIALS) trials = MAX_TRIALS;
    build_axes();
    psyrt_get_clock_info(&clk);

    base_desc(&d, true, false, 0, false);
    printf("quest_bench: Psi-marginal, %d stimuli x (%d x %d x %d x %d) parameters x 2 outcomes\n",
           NS, NA, NB, NG, NL);
    printf("             %d parameter points, %lld table cells, psyq_memory_size = %.2f MB\n",
           NA * NB * NG * NL, (long long)NS * NA * NB * NG * NL * 2,
           (double)psyq_memory_size(&d) / (1024.0 * 1024.0));
    printf("             %d trials per configuration, clock resolution %llu ns\n\n",
           trials, (unsigned long long)clk.resolution_ns);
    printf("Counts are what the manual states: one next() sweeps S*P*K table cells,\n"
           "one update() runs twice down one P-cell row. The frame line is next+update\n"
           "against a 16 ms display frame, and how big a table that rate fills in one.\n\n");

    run_floor(trials);
    run_one("Psi, 61x12 grid", false, false, 0, trials, true);
    run_one("joint entropy", false, false, 0, trials, false);
    run_one("marginal entropy", true, false, 0, trials, false);
    run_one("marginal, no_table", true, true, 0, trials, false);
    run_one("marginal, subset 8", true, false, 8, trials, false);

    printf("\nno_table trades %lld floats of table (%.2f MB) for %d psychometric function\n"
           "evaluations per next(); the subset trades %d of the %d stimuli for a random\n"
           "draw per trial (Watson 2017 sec. 4.2). A configuration that does not fit the\n"
           "frame has three ways out, in this order: desc.subset_size, a coarser grid,\n"
           "and a thread of the caller's own.\n",
           (long long)NS * NA * NB * NG * NL * 2,
           (double)((long long)NS * NA * NB * NG * NL * 2 * 4) / (1024.0 * 1024.0),
           NS * NA * NB * NG * NL, NS - 8, NS);
    return 0;
}
