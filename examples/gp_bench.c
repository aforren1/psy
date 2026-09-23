/* gp_bench - what psy_gp.h costs per trial, measured against one display frame.
 *
 * The header's manual states operation counts, not times: a prediction over M
 * candidates is M triangular solves of N x N, an update is a few Cholesky
 * factorizations, a look-ahead adds an M x M covariance. This program runs
 * Bernoulli RBF sessions to 400 trials under four configurations, times every
 * psygp_next() and psygp_update(), and reports them next to the counts and
 * against a 16 ms budget: the trial loop of an experiment that calls them
 * between trials with no thread and no pacing has about that much room. For
 * each configuration it prints the largest trial count whose next plus update
 * still fits, which is the number to design a session around. It also times
 * one whole psygp_fit() and one psygp_fit_step(), the resumable form.
 *
 * This is the only file in the repository that uses psy_rt.h with psy_gp.h. The
 * header does no timing and includes nothing; the clock belongs to the caller,
 * so the caller brings it.
 *
 *   cc -O2 -I. -o gp_bench examples/gp_bench.c -lm       # Linux: -pthread too
 *   cl /O2 /I. examples\gp_bench.c
 */
#define PSY_RT_IMPLEMENTATION
#include "psy_rt.h"

#define PSY_GP_IMPLEMENTATION
#include "psy_gp.h"

#include <stdio.h>
#include <math.h>
#include <string.h>

#define N_MAX_TRIALS 400
#define N_CAND       256
#define FRAME_MS     16.0
#define WINDOW       10     /* trials averaged before a row is believed */

static const int marks[4] = { 50, 100, 200, 400 };

static uint64_t rng_state;

static double rng_u(void) {
    uint64_t z = (rng_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z = z ^ (z >> 31);
    return (double)(z >> 11) * (1.0 / 9007199254740992.0);
}

/* A 2-D detection observer; the model is what is being timed, not the observer. */
static int observe(const double* x) {
    double p[2];
    p[1] = 0.5 * erfc(-(5.0 * (x[1] - 0.4 * x[0])) / sqrt(2.0));
    p[0] = 1.0 - p[1];
    return psygp_simulate_outcome(p, 2, rng_u());
}

/* The median (want = 0) or the largest (want = 1) next+update of the WINDOW
 * trials ending at n. Ten values, so an insertion sort is the whole of it. */
static double window_stat(const double* a, const double* b, int n, int want) {
    double v[WINDOW];
    for (int i = 0; i < WINDOW; i++) v[i] = a[n - WINDOW + 1 + i] + b[n - WINDOW + 1 + i];
    for (int i = 1; i < WINDOW; i++) {
        double key = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; j--; }
        v[j + 1] = key;
    }
    return want ? v[WINDOW - 1] : v[WINDOW / 2];
}

static void bench(psygp_acq acq, int refit_every, const char* name) {
    psygp_desc d;
    psygp_gp g;
    double next_ms[N_MAX_TRIALS + 1], upd_ms[N_MAX_TRIALS + 1];
    int done = 0, last_fit = 0;
    memset(&d, 0, sizeof(d));
    memset(next_ms, 0, sizeof(next_ms));
    memset(upd_ms, 0, sizeof(upd_ms));
    d.n_dims = 2;
    d.lo[0] = 0.0; d.hi[0] = 1.0;
    d.lo[1] = 0.0; d.hi[1] = 1.0;
    d.intensity_dim = 1;
    d.acq = acq;
    d.target_p = 0.75;
    d.n_candidates = N_CAND;
    d.n_init = 10;
    d.refit_every = refit_every;
    d.max_trials = N_MAX_TRIALS;
    d.stop_trials = N_MAX_TRIALS;
    rng_state = 0x243F6A8885A308D3ull;
    if (!psygp_open(&g, &d)) {
        fprintf(stderr, "gp_bench: %s\n", psygp_error(&g));
        return;
    }
    printf("%s, M = %d, refit_every %d, %.2f MB of state\n", name, N_CAND,
           refit_every, (double)psygp_memory_size(&d) / (1024.0 * 1024.0));
    printf("     N    next, mean  update, mean  sum, med.  sum, worst  "
           "vs %.0f ms  flops: next / refit\n", FRAME_MS);
    while (psygp_n_trials(&g) < N_MAX_TRIALS) {
        double x[2];
        uint64_t t0, t1, t2;
        int n;
        t0 = psyrt_now_ns();
        if (psygp_next(&g, x) < -1) break;
        t1 = psyrt_now_ns();
        if (psygp_update(&g, x, observe(x)) != PSYGP_OK) break;
        t2 = psyrt_now_ns();
        n = psygp_n_trials(&g);
        next_ms[n] = (double)(t1 - t0) / 1e6;
        upd_ms[n] = (double)(t2 - t1) / 1e6;
        /* The largest N whose MEDIAN trial in a window is inside the frame.
         * The median, not the worst: this is a desktop, not a real-time
         * kernel, and a single stolen timeslice would otherwise decide the
         * answer. The worst is printed next to it so the spread is visible. */
        if (n >= WINDOW && window_stat(next_ms, upd_ms, n, 0) <= FRAME_MS)
            last_fit = n;
        if (done < 4 && n == marks[done]) {
            double med = window_stat(next_ms, upd_ms, n, 0);
            double mx = window_stat(next_ms, upd_ms, n, 1);
            double nn = (double)marks[done];
            double pred = (double)N_CAND * nn * nn / 2.0;
            double look = acq == PSYGP_ACQ_EAVC
                          ? (double)N_CAND * N_CAND * nn / 2.0 : 0.0;
            double chol = nn * nn * nn / 3.0;
            double sn = 0.0, su = 0.0;
            for (int i = n - WINDOW + 1; i <= n; i++) {
                sn += next_ms[i];
                su += upd_ms[i];
            }
            sn /= WINDOW;
            su /= WINDOW;
            printf("   %3d   %9.3f    %9.3f  %9.3f   %9.3f   %-6s %8.3g / %8.3g\n",
                   marks[done], sn, su, med, mx,
                   med <= FRAME_MS ? "fits" : "OVER", pred + look, chol);
            done++;
        }
    }
    if (last_fit >= N_MAX_TRIALS)
        printf("   next+update stays inside %.0f ms to at least N = %d\n",
               FRAME_MS, N_MAX_TRIALS);
    else
        printf("   next+update leaves %.0f ms after N = %d\n", FRAME_MS, last_fit);
    {
        uint64_t t0 = psyrt_now_ns();
        int rc = psygp_fit_step(&g);
        double step_ms = (double)(psyrt_now_ns() - t0) / 1e6;
        double fit_ms;
        int steps = 1;
        t0 = psyrt_now_ns();
        while (psygp_fit_step(&g) > 0) steps++;
        fit_ms = (double)(psyrt_now_ns() - t0) / 1e6 + step_ms;
        printf("   psygp_fit_step() at N = %d: %.1f ms for the first step, "
               "%.0f ms for all %d (%s)\n\n", psygp_n_trials(&g), step_ms,
               fit_ms, steps, psygp_strerror(rc < 0 ? rc : 0));
    }
    psygp_close(&g);
}

int main(void) {
    printf("gp_bench: Bernoulli, probit, RBF-ARD, 2-D box, budget %.0f ms\n",
           FRAME_MS);
    printf("psyrt_now_ns() resolution check: ");
    {
        uint64_t a = psyrt_now_ns(), b = psyrt_now_ns();
        printf("two back-to-back reads differ by %llu ns\n\n",
               (unsigned long long)(b - a));
    }
    /* refit_every past the session length means no exact Newton refit ever
     * runs inside the trial loop: the caller takes one with psygp_refit()
     * between blocks instead. It is the cheapest per-trial configuration the
     * header has, and the contrast with refit_every 1 is what it buys. */
    bench(PSYGP_ACQ_LSE, 1, "LSE (straddle)");
    bench(PSYGP_ACQ_LSE, N_MAX_TRIALS + 1, "LSE (straddle)");
    bench(PSYGP_ACQ_EAVC, 1, "EAVC (look-ahead)");
    bench(PSYGP_ACQ_EAVC, N_MAX_TRIALS + 1, "EAVC (look-ahead)");
    return 0;
}
