/* Self-checking tests for psy_gp.h. No framework: every check prints a line
 * and bumps a counter, main() returns 0 only when the counter is zero.
 *
 * The file defines PSY_GP_IMPLEMENTATION, so the header's private helpers are
 * in scope. Several checks need them: the numerics under test are the Cholesky,
 * the quadrature, the Laplace mode and the analytic marginal-likelihood
 * gradient, and none of those has a public entry point of its own.
 *
 *   cc -O2 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Werror -I. \
 *      -o gp_test tests/adapt/psy_gp_test.c -lm && ./gp_test
 */
#define PSY_GP_IMPLEMENTATION
#include "psy_gp.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

static int psygp_test_fails = 0;

/* Every quantity that passes through the header's psygp_real matrices loses
 * accuracy in a float build. The factor is not a guess: it is what the checks
 * below were measured to need, and the numbers they actually reach are printed
 * so the two builds can be compared. */
static const double tolx = (sizeof(psygp_real) == sizeof(double)) ? 1.0 : 2e7;
static const int real_is_double = (sizeof(psygp_real) == sizeof(double));

/* Worst observed errors, printed at the end. */
static double worst_chol = 0.0, worst_stat = 0.0, worst_var = 0.0;
static double worst_look = 0.0, worst_lm = 0.0, worst_grad = 0.0;

static double track(double* slot, double v) {
    if (v > *slot) *slot = v;
    return v;
}

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        if (!(cond)) {                                                        \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                       \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
            psygp_test_fails++;                                               \
        }                                                                     \
    } while (0)

#define CLOSE(a, b, tol, ...)                                                 \
    do {                                                                      \
        double aa_ = (a), bb_ = (b);                                          \
        if (!(fabs(aa_ - bb_) <= (tol))) {                                    \
            printf("FAIL %s:%d: %.17g vs %.17g (tol %g): ", __FILE__,         \
                   __LINE__, aa_, bb_, (double)(tol));                        \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
            psygp_test_fails++;                                               \
        }                                                                     \
    } while (0)

/* --- test-side linear algebra, independent of the header's -------------- */

/* Gauss-Jordan inverse with partial pivoting, n x n row-major. Deliberately a
 * different algorithm from the header's Cholesky so a shared mistake cannot
 * pass both sides of a comparison. */
static void tinv(double* A, int n) {
    double* I = (double*)calloc((size_t)n * n, sizeof(double));
    for (int i = 0; i < n; i++) I[i * n + i] = 1.0;
    for (int c = 0; c < n; c++) {
        int piv = c;
        double best = fabs(A[c * n + c]), d;
        for (int r = c + 1; r < n; r++)
            if (fabs(A[r * n + c]) > best) { best = fabs(A[r * n + c]); piv = r; }
        if (piv != c) {
            for (int k = 0; k < n; k++) {
                double t = A[c * n + k]; A[c * n + k] = A[piv * n + k]; A[piv * n + k] = t;
                t = I[c * n + k]; I[c * n + k] = I[piv * n + k]; I[piv * n + k] = t;
            }
        }
        d = A[c * n + c];
        for (int k = 0; k < n; k++) { A[c * n + k] /= d; I[c * n + k] /= d; }
        for (int r = 0; r < n; r++) {
            double f = A[r * n + c];
            if (r == c || f == 0.0) continue;
            for (int k = 0; k < n; k++) {
                A[r * n + k] -= f * A[c * n + k];
                I[r * n + k] -= f * I[c * n + k];
            }
        }
    }
    memcpy(A, I, (size_t)n * n * sizeof(double));
    free(I);
}

/* log|det A| by Gaussian elimination with partial pivoting, again on purpose
 * not the header's Cholesky. Destroys A. */
static double tlogdet(double* A, int n) {
    double acc = 0.0;
    for (int c = 0; c < n; c++) {
        int piv = c;
        double best = fabs(A[c * n + c]);
        for (int r = c + 1; r < n; r++)
            if (fabs(A[r * n + c]) > best) { best = fabs(A[r * n + c]); piv = r; }
        if (piv != c)
            for (int k = 0; k < n; k++) {
                double t = A[c * n + k]; A[c * n + k] = A[piv * n + k];
                A[piv * n + k] = t;
            }
        acc += log(fabs(A[c * n + c]));
        for (int r = c + 1; r < n; r++) {
            double f = A[r * n + c] / A[c * n + c];
            for (int k = c; k < n; k++) A[r * n + k] -= f * A[c * n + k];
        }
    }
    return acc;
}

/* --- deterministic uniforms --------------------------------------------- */

static uint64_t rng_state = 0x243F6A8885A308D3ull;

static double rng_u(void) {
    uint64_t z = (rng_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z = z ^ (z >> 31);
    return (double)(z >> 11) * (1.0 / 9007199254740992.0);
}

static double rng_cb(void* ctx) { (void)ctx; return rng_u(); }

/* --- 1: Cholesky and triangular solves ---------------------------------- */

/* The binding reads its __version__ from psygp_version(), so the string must be
 * the three macros and nothing else. Compared at run time through strcmp
 * because a constant-condition CHECK is MSVC warning C4127 under /W4 /WX. */
static void test_version(void) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d.%d.%d", PSYGP_VERSION_MAJOR,
             PSYGP_VERSION_MINOR, PSYGP_VERSION_PATCH);
    CHECK(strcmp(psygp_version(), "0.2.1") == 0,
          "psygp_version() is %s, expected 0.2.1", psygp_version());
    CHECK(strcmp(psygp_version(), PSYGP_VERSION_STRING) == 0,
          "psygp_version() %s disagrees with PSYGP_VERSION_STRING %s",
          psygp_version(), PSYGP_VERSION_STRING);
    CHECK(strcmp(buf, PSYGP_VERSION_STRING) == 0,
          "PSYGP_VERSION_STRING %s is not MAJOR.MINOR.PATCH %s",
          PSYGP_VERSION_STRING, buf);
    printf("  version %s\n", psygp_version());
}

static void test_chol(void) {
    double A[9] = { 4.0, 2.0, 1.0,
                    2.0, 5.0, 3.0,
                    1.0, 3.0, 6.0 };
    psygp_real L[9];
    double b[3] = { 1.0, -2.0, 0.5 }, x[3], Ai[9], xref[3];
    for (int i = 0; i < 9; i++) L[i] = (psygp_real)A[i];
    CHECK(psygp__chol(L, 3, 3), "chol of an SPD matrix failed");
    for (int i = 0; i < 3; i++)
        for (int j = 0; j <= i; j++) {
            double s = 0.0;
            for (int k = 0; k <= j; k++) s += (double)L[i * 3 + k] * L[j * 3 + k];
            track(&worst_chol, fabs(s - A[i * 3 + j]));
            CLOSE(s, A[i * 3 + j], 1e-13 * tolx, "L L' at (%d,%d)", i, j);
        }
    memcpy(x, b, sizeof(b));
    psygp__tri_fwd(L, 3, 3, x);
    psygp__tri_bwd(L, 3, 3, x);
    memcpy(Ai, A, sizeof(A));
    tinv(Ai, 3);
    for (int i = 0; i < 3; i++) {
        xref[i] = 0.0;
        for (int j = 0; j < 3; j++) xref[i] += Ai[i * 3 + j] * b[j];
        track(&worst_chol, fabs(x[i] - xref[i]));
        CLOSE(x[i], xref[i], 1e-12 * tolx, "solve component %d", i);
    }
    /* A matrix that is not positive definite must be reported, not fixed. */
    {
        psygp_real B[4] = { 1.0, 2.0, 2.0, 1.0 };
        CHECK(!psygp__chol(B, 2, 2), "chol accepted an indefinite matrix");
    }
    /* The triangular inverse and its square, used by the hyper gradient. */
    {
        psygp_real C[9];
        double P[9];
        for (int i = 0; i < 9; i++) C[i] = (psygp_real)A[i];
        psygp__chol(C, 3, 3);
        psygp__tri_inv(C, 3, 3);
        {
            double tmp[3];
            psygp__tri_sqr(C, 3, 3, tmp);
        }
        memcpy(P, A, sizeof(A));
        tinv(P, 3);
        for (int i = 0; i < 9; i++) {
            track(&worst_chol, fabs((double)C[i] - P[i]));
            CLOSE((double)C[i], P[i], 1e-12 * tolx, "A^-1 entry %d", i);
        }
    }
}

/* --- 2: kernel symmetry and positive definiteness ----------------------- */

static void test_kernel(int kernel) {
    psygp_desc d;
    psygp_gp g;
    double X[16 * 3];
    psygp_real G[16 * 16];
    int n = 16, nd = 3;
    rng_state = 0x243F6A8885A308D3ull + 0x11ull;
    memset(&d, 0, sizeof(d));
    d.n_dims = nd;
    for (int i = 0; i < nd; i++) { d.lo[i] = -1.0; d.hi[i] = 2.0; }
    d.intensity_dim = 2;
    d.kernel = (psygp_kernel)kernel;
    d.target_p = 0.75;
    d.stop_trials = 10;
    CHECK(psygp_open(&g, &d), "open for the kernel test: %s", psygp_error(&g));
    for (int i = 0; i < n * nd; i++) X[i] = -1.0 + 3.0 * rng_u();
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            G[i * n + j] = (psygp_real)psygp__kernel(&g, X + i * nd, X + j * nd);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < i; j++)
            CLOSE((double)G[i * n + j], (double)G[j * n + i], 1e-15,
                  "kernel symmetry (%d,%d)", i, j);
        CHECK(G[i * n + i] > 0.0, "kernel diagonal %d is not positive", i);
    }
    for (int i = 0; i < n; i++) G[i * n + i] += (psygp_real)1e-6;
    CHECK(psygp__chol(G, n, n), "kernel %d Gram matrix is not positive definite",
          kernel);
    psygp_close(&g);
}

/* --- 3: Gauss-Hermite nodes --------------------------------------------- */

static void test_quad(void) {
    double x[PSYGP_QUAD_N], w[PSYGP_QUAD_N];
    double want[7] = { 1.0, 0.0, 1.0, 0.0, 3.0, 0.0, 15.0 };
    psygp__gh_init(x, w, PSYGP_QUAD_N);
    for (int k = 0; k <= 6; k++) {
        double acc = 0.0;
        for (int i = 0; i < PSYGP_QUAD_N; i++)
            acc += w[i] * pow(PSYGP__SQRT2 * x[i], (double)k);
        acc /= PSYGP__SQRTPI;
        CLOSE(acc, want[k], 1e-11, "Gaussian moment %d", k);
    }
}

/* --- 4: exact Gaussian regression vs a dense solve ---------------------- */

static void test_gaussian_exact(void) {
    psygp_desc d;
    psygp_gp g;
    int n = 9, nd = 2;
    double xs[9 * 2], ys[9];
    double Ky[81], ks[9], mu, sd, mref, vref, kxx;
    double probe[2] = { 0.31, -0.22 };
    rng_state = 0x243F6A8885A308D3ull + 0x22ull;
    memset(&d, 0, sizeof(d));
    d.n_dims = nd;
    d.lo[0] = -1.0; d.hi[0] = 1.0;
    d.lo[1] = -1.0; d.hi[1] = 1.0;
    d.lik = PSYGP_LIK_GAUSSIAN;
    d.hyper.lengthscale[0] = 0.7;
    d.hyper.lengthscale[1] = 0.4;
    d.hyper.outputscale = 1.3;
    d.hyper.mean = 0.25;
    d.hyper.noise_sd = 0.3;
    d.stop_trials = n;
    CHECK(psygp_open(&g, &d), "open GAUSSIAN: %s", psygp_error(&g));
    for (int i = 0; i < n; i++) {
        xs[i * 2 + 0] = -1.0 + 2.0 * rng_u();
        xs[i * 2 + 1] = -1.0 + 2.0 * rng_u();
        ys[i] = sin(3.0 * xs[i * 2]) + 0.2 * rng_u();
        CHECK(psygp_update_real(&g, xs + i * 2, ys[i]) == PSYGP_OK,
              "update_real %d", i);
    }
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            Ky[i * n + j] = psygp__kernel(&g, xs + i * 2, xs + j * 2) +
                            (i == j ? 0.3 * 0.3 + 1e-6 : 0.0);
    tinv(Ky, n);
    for (int i = 0; i < n; i++) ks[i] = psygp__kernel(&g, xs + i * 2, probe);
    kxx = psygp__kernel(&g, probe, probe);
    mref = 0.25;
    vref = kxx;
    for (int i = 0; i < n; i++) {
        double row = 0.0;
        for (int j = 0; j < n; j++) row += Ky[i * n + j] * (ys[j] - 0.25);
        mref += ks[i] * row;
        row = 0.0;
        for (int j = 0; j < n; j++) row += Ky[i * n + j] * ks[j];
        vref -= ks[i] * row;
    }
    CHECK(psygp_predict_f(&g, probe, 0, &mu, &sd) == PSYGP_OK, "predict_f");
    CLOSE(mu, mref, 1e-10, "Gaussian posterior mean");
    CLOSE(sd * sd, vref, 1e-10, "Gaussian posterior variance");
    /* The exact log marginal likelihood, from the same dense inverse. */
    {
        double quad = 0.0, ldet, Kc[81];
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                Kc[i * n + j] = psygp__kernel(&g, xs + i * 2, xs + j * 2) +
                                (i == j ? 0.3 * 0.3 + 1e-6 : 0.0);
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                quad += (ys[i] - 0.25) * Ky[i * n + j] * (ys[j] - 0.25);
        ldet = 0.5 * tlogdet(Kc, n);   /* the test's own elimination: log|Ky| */
        CLOSE(psygp_log_marginal(&g),
              -0.5 * quad - ldet - 0.5 * n * PSYGP__LOG2PI, 1e-9,
              "Gaussian log marginal");
    }
    psygp_close(&g);
}

/* --- 5: the Laplace mode and its predictive variance -------------------- */

static void test_laplace_probit(void) {
    psygp_desc d;
    psygp_gp g;
    int n = 6;
    double xs[6] = { -0.9, -0.5, -0.1, 0.2, 0.6, 0.95 };
    int ys[6] = { 0, 0, 1, 0, 1, 1 };
    double K[36], A[36], ks[6], mu, sd, vref, kxx;
    double probe = 0.35;
    memset(&d, 0, sizeof(d));
    d.n_dims = 1;
    d.lo[0] = -1.0; d.hi[0] = 1.0;
    d.hyper.lengthscale[0] = 0.5;
    d.hyper.outputscale = 2.0;
    d.hyper.mean = -0.3;
    d.target_p = 0.75;
    d.stop_trials = n;
    CHECK(psygp_open(&g, &d), "open BERNOULLI: %s", psygp_error(&g));
    for (int i = 0; i < n; i++)
        CHECK(psygp_update(&g, xs + i, ys[i]) == PSYGP_OK, "update %d", i);
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            K[i * n + j] = psygp__kernel(&g, xs + i, xs + j) +
                           (i == j ? 1e-6 : 0.0);
    /* Stationarity: fhat = mean + K grad log p(y | fhat), with the gradient
     * recomputed here from the probit rather than read from the handle. */
    for (int i = 0; i < n; i++) {
        double acc = -0.3;
        for (int j = 0; j < n; j++) {
            double s = ys[j] ? 1.0 : -1.0, z = s * g.f[j];
            double gr = s * exp(-0.5 * z * z) / (2.5066282746310005 *
                                                 0.5 * erfc(-z / sqrt(2.0)));
            acc += K[i * n + j] * gr;
        }
        track(&worst_stat, fabs(g.f[i] - acc));
        CLOSE(g.f[i], acc, 1e-8 * tolx, "stationarity at site %d", i);
    }
    /* Predictive variance against a dense (K^-1 + W)^-1. */
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) A[i * n + j] = K[i * n + j];
    tinv(A, n);
    for (int i = 0; i < n; i++) A[i * n + i] += g.W[i];
    tinv(A, n);   /* A = (K^-1 + W)^-1, the posterior covariance at the sites */
    for (int i = 0; i < n; i++) ks[i] = psygp__kernel(&g, xs + i, &probe);
    kxx = psygp__kernel(&g, &probe, &probe);
    {
        /* var = k** - k*' K^-1 k* + k*' K^-1 A K^-1 k*, which is the
         * same thing as k** - k*' (K + W^-1)^-1 k* written through A. */
        double Ki[36], v1[6], v2[6], t1 = 0.0, t2 = 0.0;
        memcpy(Ki, K, sizeof(K));
        tinv(Ki, n);
        for (int i = 0; i < n; i++) {
            v1[i] = 0.0;
            for (int j = 0; j < n; j++) v1[i] += Ki[i * n + j] * ks[j];
        }
        for (int i = 0; i < n; i++) {
            v2[i] = 0.0;
            for (int j = 0; j < n; j++) v2[i] += A[i * n + j] * v1[j];
        }
        for (int i = 0; i < n; i++) { t1 += ks[i] * v1[i]; t2 += v1[i] * v2[i]; }
        vref = kxx - t1 + t2;
    }
    CHECK(psygp_predict_f(&g, &probe, 0, &mu, &sd) == PSYGP_OK, "predict_f");
    track(&worst_var, fabs(sd * sd - vref));
    CLOSE(sd * sd, vref, 1e-9 * tolx, "Laplace predictive variance");
    /* The probit closed form is what psygp_predict_p must return. */
    CLOSE(psygp_predict_p(&g, &probe),
          0.5 * erfc(-(mu / sqrt(1.0 + sd * sd)) / sqrt(2.0)), 1e-13,
          "predict_p closed form");   /* both sides read the same mu and sd */
    /* The Laplace log marginal likelihood (R&W eq. 3.32) is what the
     * hyperparameter fit maximizes, so it is worth one independent
     * evaluation: -1/2 a'(fhat - m) + log p(y | fhat) - 1/2 log|B|. */
    {
        double Ki[36], B[36], av[6], quad = 0.0, ll = 0.0, ldet;
        memcpy(Ki, K, sizeof(K));
        tinv(Ki, n);
        for (int i = 0; i < n; i++) {
            av[i] = 0.0;
            for (int j = 0; j < n; j++) av[i] += Ki[i * n + j] * (g.f[j] + 0.3);
        }
        for (int i = 0; i < n; i++) {
            double s = ys[i] ? 1.0 : -1.0;
            quad += av[i] * (g.f[i] + 0.3);
            ll += log(0.5 * erfc(-(s * g.f[i]) / sqrt(2.0)));
        }
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                B[i * n + j] = (i == j ? 1.0 : 0.0) +
                               sqrt(g.W[i]) * K[i * n + j] * sqrt(g.W[j]);
        ldet = tlogdet(B, n);
        track(&worst_lm, fabs(psygp_log_marginal(&g) -
                              (-0.5 * quad + ll - 0.5 * ldet)));
        CLOSE(psygp_log_marginal(&g), -0.5 * quad + ll - 0.5 * ldet, 1e-8 * tolx,
              "Laplace log marginal likelihood");
    }
    psygp_close(&g);
}

/* --- 6: the analytic gradient against central differences --------------- */

static psygp_link psygp_link_gradient_probe = PSYGP_LINK_PROBIT;
static double psygp_guess_gradient_probe = 0.0;
static double psygp_lapse_gradient_probe = 0.0;

static void test_gradient(psygp_lik lik, psygp_kernel kernel, const char* name) {
    psygp_desc d;
    psygp_gp g;
    psygp__param p[PSYGP__NTHETA];
    double th[PSYGP__NTHETA], ga[PSYGP__NTHETA], gn[PSYGP__NTHETA];
    double v0, vp, vm;
    int np, n = 12, nd = 2;
    rng_state = 0x243F6A8885A308D3ull + 0x33ull;
    memset(&d, 0, sizeof(d));
    d.n_dims = nd;
    d.lo[0] = -1.0; d.hi[0] = 1.0;
    d.lo[1] = -2.0; d.hi[1] = 2.0;
    d.intensity_dim = 1;
    d.kernel = kernel;
    d.lik = lik;
    d.link = psygp_link_gradient_probe;
    if (lik == PSYGP_LIK_ORDINAL) { d.n_outcomes = 4; d.target_outcome = 2; }
    if (lik != PSYGP_LIK_GAUSSIAN) {
        d.guess = psygp_guess_gradient_probe;
        d.lapse = psygp_lapse_gradient_probe;
    }
    d.target_p = 0.75;
    d.stop_trials = n;
    CHECK(psygp_open(&g, &d), "open %s: %s", name, psygp_error(&g));
    for (int i = 0; i < n; i++) {
        double x[2];
        x[0] = -1.0 + 2.0 * rng_u();
        x[1] = -2.0 + 4.0 * rng_u();
        if (lik == PSYGP_LIK_GAUSSIAN) {
            CHECK(psygp_update_real(&g, x, 0.7 * x[1] + 0.3 * rng_u()) == PSYGP_OK,
                  "%s update", name);
        } else {
            int k = (int)(rng_u() * (lik == PSYGP_LIK_ORDINAL ? 4.0 : 2.0));
            CHECK(psygp_update(&g, x, k) == PSYGP_OK, "%s update", name);
        }
    }
    np = psygp__params(&g, p);
    CHECK(np > 0, "%s has no free hyperparameters", name);
    psygp__theta_get(&g, p, np, th);
    CHECK(psygp__fit_eval(&g, p, np, th, &v0, ga) == PSYGP_OK, "%s fit_eval", name);
    for (int i = 0; i < np; i++) {
        /* The step has to clear the resolution of the objective: a float build
         * computes it to about 1e-7 relative, so a 1e-5 step would differentiate
         * the noise. */
        double step = (real_is_double ? 1e-5 : 3e-3) * (1.0 + fabs(th[i]));
        double save = th[i], rel;
        th[i] = save + step;
        CHECK(psygp__fit_eval(&g, p, np, th, &vp, NULL) == PSYGP_OK, "%s +h", name);
        th[i] = save - step;
        CHECK(psygp__fit_eval(&g, p, np, th, &vm, NULL) == PSYGP_OK, "%s -h", name);
        th[i] = save;
        gn[i] = (vp - vm) / (2.0 * step);
        rel = fabs(ga[i] - gn[i]) / (fabs(gn[i]) > 1e-6 ? fabs(gn[i]) : 1e-6);
        track(&worst_grad, rel);
        CHECK(rel < (real_is_double ? 1e-4 : 6e-2),
              "%s gradient %d (kind %d dim %d): analytic %.12g, "
              "finite difference %.12g, relative error %.3g",
              name, i, p[i].kind, p[i].dim, ga[i], gn[i], rel);
    }
    psygp_close(&g);
}

/* --- 7: the rank-one look-ahead ----------------------------------------- */

/* The look-ahead variance at every candidate after one more observation, with W
 * frozen, is a rank-one downdate of the candidate covariance. The brute force
 * on the other side builds the (N + 1)-point posterior from scratch with the
 * same frozen W and the new site's precision appended. */
static void test_lookahead(void) {
    psygp_desc d;
    psygp_gp g;
    int n = 7, M, ja = 3, jb = 5, jc = 2;
    double xs[7] = { -0.85, -0.6, -0.2, 0.05, 0.4, 0.7, 0.9 };
    int ys[7] = { 0, 0, 1, 0, 1, 1, 1 };
    double A[64], cw, cov_ab, cov_aj, cov_jb, cov_jj, want, got;
    double ca[1], cb[1], cj[1];
    psygp__scr s;
    memset(&d, 0, sizeof(d));
    d.n_dims = 1;
    d.lo[0] = -1.0; d.hi[0] = 1.0;
    d.acq = PSYGP_ACQ_EAVC;
    d.target_p = 0.75;
    d.n_candidates = 9;
    d.hyper.lengthscale[0] = 0.45;
    d.hyper.outputscale = 1.5;
    d.stop_trials = n;
    CHECK(psygp_open(&g, &d), "open look-ahead: %s", psygp_error(&g));
    for (int i = 0; i < n; i++)
        CHECK(psygp_update(&g, xs + i, ys[i]) == PSYGP_OK, "update %d", i);
    M = psygp_n_candidates(&g);
    CHECK(M == 9, "candidate count %d", M);
    psygp__cand_cov_build(&g, 0);
    psygp__scr_of(&g, &s);
    psygp_candidate(&g, ja, ca);
    psygp_candidate(&g, jb, cb);
    psygp_candidate(&g, jc, cj);
    /* The candidate covariance itself, against k** - k*' (K + W^-1)^-1 k*. */
    {
        double Kw[64], kia[8], kib[8], v[8], acc = 0.0;
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                Kw[i * n + j] = psygp__kernel(&g, xs + i, xs + j) +
                                (i == j ? 1e-6 + 1.0 / g.W[i] : 0.0);
        tinv(Kw, n);
        for (int i = 0; i < n; i++) {
            kia[i] = psygp__kernel(&g, xs + i, ca);
            kib[i] = psygp__kernel(&g, xs + i, cb);
        }
        for (int i = 0; i < n; i++) {
            v[i] = 0.0;
            for (int j = 0; j < n; j++) v[i] += Kw[i * n + j] * kib[j];
            acc += 0.0;
        }
        for (int i = 0; i < n; i++) acc += kia[i] * v[i];
        track(&worst_look, fabs((double)g.cand_cov[ja * M + jb] -
                                (psygp__kernel(&g, ca, cb) - acc)));
        CLOSE((double)g.cand_cov[ja * M + jb], psygp__kernel(&g, ca, cb) - acc,
              1e-10 * tolx, "candidate covariance (%d,%d)", ja, jb);
    }
    cov_ab = g.cand_cov[ja * M + jb];
    cov_aj = g.cand_cov[ja * M + jc];
    cov_jb = g.cand_cov[jc * M + jb];
    cov_jj = g.cand_cov[jc * M + jc];
    /* An arbitrary but fixed site precision, as the look-ahead would use. */
    cw = 0.37;
    want = cov_ab - cov_aj * cov_jb * cw / (1.0 + cw * cov_jj);
    {
        /* Brute force: the same posterior with the candidate appended as an
         * (n + 1)-th site of precision cw. */
        int m = n + 1;
        double kia[8], kib[8], v[8], acc = 0.0;
        double xall[8];
        for (int i = 0; i < n; i++) xall[i] = xs[i];
        xall[n] = cj[0];
        for (int i = 0; i < m; i++)
            for (int j = 0; j < m; j++)
                A[i * m + j] = psygp__kernel(&g, xall + i, xall + j) +
                               (i == j ? 1e-6 + (i < n ? 1.0 / g.W[i] : 1.0 / cw)
                                       : 0.0);
        tinv(A, m);
        for (int i = 0; i < m; i++) {
            kia[i] = psygp__kernel(&g, xall + i, ca);
            kib[i] = psygp__kernel(&g, xall + i, cb);
        }
        for (int i = 0; i < m; i++) {
            v[i] = 0.0;
            for (int j = 0; j < m; j++) v[i] += A[i * m + j] * kib[j];
        }
        for (int i = 0; i < m; i++) acc += kia[i] * v[i];
        got = psygp__kernel(&g, ca, cb) - acc;
    }
    track(&worst_look, fabs(want - got));
    CLOSE(want, got, 1e-10 * tolx, "rank-one look-ahead covariance");
    /* The diagonal case is the look-ahead variance the acquisitions use. */
    want = cov_jj - cov_jj * cov_jj * cw / (1.0 + cw * cov_jj);
    CLOSE(want, cov_jj / (1.0 + cw * cov_jj), 1e-13, "look-ahead variance form");
    psygp_close(&g);
}

/* --- 8: memory_size is exact -------------------------------------------- */

static void test_memory(void) {
    psygp_desc d;
    psygp_gp g;
    size_t need;
    void* buf;
    memset(&d, 0, sizeof(d));
    d.n_dims = 2;
    d.lo[0] = 0.0; d.hi[0] = 1.0;
    d.lo[1] = 0.0; d.hi[1] = 1.0;
    d.acq = PSYGP_ACQ_EAVC;
    d.target_p = 0.75;
    d.n_candidates = 64;
    d.max_trials = 40;
    d.stop_trials = 40;
    need = psygp_memory_size(&d);
    CHECK(need > 0, "memory_size returned 0 for a valid desc");
    buf = malloc(need);
    d.memory = buf;
    d.memory_size = need;
    CHECK(psygp_open(&g, &d), "open with exactly psygp_memory_size() bytes: %s",
          psygp_error(&g));
    psygp_close(&g);
    d.memory_size = need - 1;
    CHECK(!psygp_open(&g, &d), "open accepted one byte less than the size");
    free(buf);
    /* A desc the header rejects has no size. */
    d.memory = NULL;
    d.memory_size = 0;
    d.n_dims = 0;
    CHECK(psygp_memory_size(&d) == 0, "memory_size of an invalid desc");
}

/* --- 9: Halton ---------------------------------------------------------- */

static void test_halton(void) {
    CLOSE(psygp__radinv(1, 2), 0.5, 1e-15, "halton base 2 point 1");
    CLOSE(psygp__radinv(2, 2), 0.25, 1e-15, "halton base 2 point 2");
    CLOSE(psygp__radinv(3, 2), 0.75, 1e-15, "halton base 2 point 3");
    CLOSE(psygp__radinv(1, 3), 1.0 / 3.0, 1e-15, "halton base 3 point 1");
    CLOSE(psygp__radinv(2, 3), 2.0 / 3.0, 1e-15, "halton base 3 point 2");
    CLOSE(psygp__radinv(3, 3), 1.0 / 9.0, 1e-15, "halton base 3 point 3");
    CHECK(psygp__nth_prime(0) == 2 && psygp__nth_prime(1) == 3 &&
          psygp__nth_prime(2) == 5 && psygp__nth_prime(7) == 19,
          "primes: %u %u %u %u", psygp__nth_prime(0), psygp__nth_prime(1),
          psygp__nth_prime(2), psygp__nth_prime(7));
}

/* --- 10: open rejects bad descs ----------------------------------------- */

static void test_open_reject(void) {
    psygp_gp g;
    psygp_desc ok;
    memset(&ok, 0, sizeof(ok));
    ok.n_dims = 2;
    ok.lo[0] = 0.0; ok.hi[0] = 1.0;
    ok.lo[1] = 0.0; ok.hi[1] = 1.0;
    ok.target_p = 0.75;
    ok.stop_trials = 20;
    CHECK(psygp_open(&g, &ok), "the reference desc must open: %s", psygp_error(&g));
    psygp_close(&g);
#define REJECT(mut, what)                                                     \
    do {                                                                      \
        psygp_desc d = ok;                                                    \
        mut;                                                                  \
        CHECK(!psygp_open(&g, &d), "open accepted %s", what);                 \
        psygp_close(&g);                                                      \
    } while (0)
    REJECT(d.n_dims = 0, "n_dims = 0");
    REJECT(d.n_dims = PSYGP_MAX_DIMS + 1, "n_dims over the ceiling");
    REJECT(d.hi[1] = d.lo[1], "an empty box dimension");
    REJECT(d.intensity_dim = 2, "intensity_dim out of range");
    REJECT(d.stop_trials = 0, "no stop criterion");
    REJECT(d.max_trials = PSYGP_MAX_TRIALS + 1, "max_trials over the ceiling");
    REJECT(d.lik = PSYGP_LIK_ORDINAL, "ORDINAL without n_outcomes");
    REJECT((d.lik = PSYGP_LIK_CATEGORICAL, d.n_outcomes = 2),
           "CATEGORICAL with 2 outcomes");
    REJECT((d.lik = PSYGP_LIK_ORDINAL, d.n_outcomes = 4, d.target_outcome = 4),
           "target_outcome out of range");
    REJECT((d.acq = PSYGP_ACQ_EAVC, d.target_p = 0.0),
           "a level-set acquisition without target_p");
    REJECT(d.target_p = 1.5, "target_p above 1");
    REJECT(d.hyper.outputscale = -1.0, "a negative output scale");
    REJECT(d.grid[1] = 3, "a product grid with a zero dimension");
    REJECT((d.candidates = ok.lo, d.n_candidates = 0),
           "an explicit candidate set with no count");
    REJECT((d.acq = PSYGP_ACQ_EAVC, d.target_p = 0.75, d.n_candidates = 100000),
           "more look-ahead candidates than the cap");
    REJECT(d.n_candidates = -1, "a negative candidate count");
    REJECT(d.refit_every = -1, "a negative refit_every");
#undef REJECT
    CHECK(!psygp_open(NULL, &ok), "open accepted a null handle");
    memset(&g, 0, sizeof(g));
    CHECK(!psygp_open(&g, NULL), "open accepted a null desc");
    CHECK(psygp_n_trials(&g) == PSYGP_ERR_CLOSED, "n_trials on a closed handle");
    CHECK(psygp_update(&g, ok.lo, 1) == PSYGP_ERR_CLOSED, "update when closed");
}

/* --- 11: a simulated 1-D probit observer -------------------------------- */

/* The observer: p(yes) = Phi(slope (x - x50)), so the 75% point is at
 * x50 + 0.6744898 / slope. */
#define OBS_SLOPE 8.0
#define OBS_X50   0.40
#define OBS_THR75 (OBS_X50 + 0.67448975019608171 / OBS_SLOPE)

static int observe(double x) {
    double p[2];
    p[1] = 0.5 * erfc(-(OBS_SLOPE * (x - OBS_X50)) / sqrt(2.0));
    p[0] = 1.0 - p[1];
    return psygp_simulate_outcome(p, 2, rng_u());
}

/* desc.refine_steps for run_observer(), so the refinement test can rerun the
 * same streams through it. */
static int obs_refine = 0;

static double run_observer(psygp_acq acq, int trials, const char* name, int rep) {
    psygp_desc d;
    psygp_gp g;
    double thr = 0.0, lo, hi;
    int rc;
    rng_state = 0x243F6A8885A308D3ull + (uint64_t)rep * 0x1000193ull;
    memset(&d, 0, sizeof(d));
    d.n_dims = 1;
    d.lo[0] = 0.0; d.hi[0] = 1.0;
    d.acq = acq;
    d.target_p = 0.75;
    d.n_candidates = 65;
    d.grid[0] = 65;
    d.n_init = 8;
    d.fit = true;
    d.fit_every = 10;
    d.stop_trials = trials;
    d.refine_steps = obs_refine;
    if (!psygp_open(&g, &d)) {
        CHECK(0, "open %s: %s", name, psygp_error(&g));
        return 1e9;
    }
    while (!psygp_done(&g)) {
        double x[1];
        int idx = psygp_next(&g, x);
        CHECK(idx >= -1, "%s next returned %d", name, idx);
        rc = psygp_update(&g, x, observe(x[0]));
        CHECK(rc == PSYGP_OK, "%s update: %s", name, psygp_strerror(rc));
    }
    CHECK(psygp_n_trials(&g) == trials, "%s ran %d trials", name,
          psygp_n_trials(&g));
    rc = psygp_threshold(&g, NULL, 0.0, &thr, &lo, &hi);
    CHECK(rc == PSYGP_OK, "%s threshold: %s", name, psygp_strerror(rc));
    CHECK(lo <= thr + 1e-9 && thr <= hi + 1e-9,
          "%s band [%g, %g] does not contain %g", name, lo, hi, thr);
    printf("  %-5s stream %d: threshold %.4f (true %.4f, error %+.4f), "
           "band %.3f wide, log marginal %.2f\n", name, rep, thr,
           OBS_THR75, thr - OBS_THR75, hi - lo, psygp_log_marginal(&g));
    psygp_close(&g);
    return fabs(thr - OBS_THR75);
}

/* The mean absolute threshold error over a few response streams. One stream
 * says nothing: the binomial noise of a hundred trials near the 75% point is
 * worth about 0.02 of stimulus here, so a single run lands anywhere inside 0.1
 * and a tolerance tight enough to be interesting would only be measuring the
 * seed. */
static double mean_error(psygp_acq acq, int trials, const char* name, int reps) {
    double acc = 0.0;
    for (int r = 0; r < reps; r++) acc += run_observer(acq, trials, name, r);
    return acc / (double)reps;
}

/* --- 12: the other three likelihoods recover their field ---------------- */

static void test_gaussian_field(void) {
    psygp_desc d;
    psygp_gp g;
    double rms = 0.0;
    int n = 0;
    rng_state = 0x243F6A8885A308D3ull + 0x44ull;
    memset(&d, 0, sizeof(d));
    d.n_dims = 1;
    d.lo[0] = 0.0; d.hi[0] = 1.0;
    d.lik = PSYGP_LIK_GAUSSIAN;
    d.acq = PSYGP_ACQ_BALV;
    d.target_value = 0.5;
    d.grid[0] = 33;
    d.n_init = 6;
    d.fit = true;
    d.fit_every = 20;
    d.stop_trials = 60;
    CHECK(psygp_open(&g, &d), "open GAUSSIAN field: %s", psygp_error(&g));
    while (!psygp_done(&g)) {
        double x[1];
        psygp_next(&g, x);
        CHECK(psygp_update_real(&g, x, sin(4.0 * x[0]) + 0.05 * (rng_u() - 0.5))
              == PSYGP_OK, "gaussian field update");
    }
    for (int i = 0; i <= 20; i++) {
        double x = (double)i / 20.0, e;
        e = psygp_predict_p(&g, &x) - sin(4.0 * x);
        rms += e * e;
        n++;
    }
    rms = sqrt(rms / n);
    printf("  gaussian field rms error %.4f\n", rms);
    CHECK(rms < 0.05, "GAUSSIAN field rms error %.4f", rms);
    psygp_close(&g);
}

static void test_ordinal_field(void) {
    psygp_desc d;
    psygp_gp g;
    double cut[3] = { 0.0, 1.0, 2.0 };
    double rms = 0.0;
    int n = 0;
    rng_state = 0x243F6A8885A308D3ull + 0x55ull;
    memset(&d, 0, sizeof(d));
    d.n_dims = 1;
    d.lo[0] = 0.0; d.hi[0] = 1.0;
    d.lik = PSYGP_LIK_ORDINAL;
    d.n_outcomes = 4;
    d.target_outcome = 2;
    d.target_p = 0.5;
    d.acq = PSYGP_ACQ_BALD;
    d.grid[0] = 33;
    d.n_init = 8;
    d.fit = true;
    d.fit_every = 20;
    d.hyper.cutpoint[0] = cut[0];
    d.hyper.cutpoint[1] = cut[1];
    d.hyper.cutpoint[2] = cut[2];
    d.stop_trials = 150;
    CHECK(psygp_open(&g, &d), "open ORDINAL: %s", psygp_error(&g));
    while (!psygp_done(&g)) {
        double x[1], p[4], f;
        int k;
        psygp_next(&g, x);
        f = 4.0 * (x[0] - 0.5);   /* the simulated latent */
        {
            double prev = 0.0;
            for (int j = 0; j < 3; j++) {
                double c = 0.5 * erfc(-(cut[j] - f) / sqrt(2.0));
                p[j] = c - prev;
                prev = c;
            }
            p[3] = 1.0 - prev;
        }
        k = psygp_simulate_outcome(p, 4, rng_u());
        CHECK(psygp_update(&g, x, k) == PSYGP_OK, "ordinal update");
    }
    for (int i = 0; i <= 20; i++) {
        double x = (double)i / 20.0, e, want;
        want = 1.0 - 0.5 * erfc(-(cut[1] - 4.0 * (x - 0.5)) / sqrt(2.0));
        e = psygp_predict_p(&g, &x) - want;   /* P(y >= 2) */
        rms += e * e;
        n++;
    }
    rms = sqrt(rms / n);
    printf("  ordinal P(y >= 2) rms error %.4f\n", rms);
    CHECK(rms < 0.12, "ORDINAL field rms error %.4f", rms);
    psygp_close(&g);
}

static void test_categorical_field(void) {
    psygp_desc d;
    psygp_gp g;
    double rms = 0.0;
    int n = 0;
    rng_state = 0x243F6A8885A308D3ull + 0x66ull;
    memset(&d, 0, sizeof(d));
    d.n_dims = 1;
    d.lo[0] = 0.0; d.hi[0] = 1.0;
    d.lik = PSYGP_LIK_CATEGORICAL;
    d.n_outcomes = 3;
    d.target_outcome = 1;
    d.target_p = 0.5;
    d.acq = PSYGP_ACQ_BALV;
    d.grid[0] = 25;
    d.n_init = 10;
    d.stop_trials = 120;
    CHECK(psygp_open(&g, &d), "open CATEGORICAL: %s", psygp_error(&g));
    while (!psygp_done(&g)) {
        double x[1], p[3], fs[3], m;
        int k;
        psygp_next(&g, x);
        fs[0] = 2.0 - 4.0 * x[0];
        fs[1] = 0.0;
        fs[2] = -2.0 + 4.0 * x[0];
        m = fs[0] > fs[2] ? fs[0] : fs[2];
        {
            double sum = 0.0;
            for (int c = 0; c < 3; c++) { p[c] = exp(fs[c] - m); sum += p[c]; }
            for (int c = 0; c < 3; c++) p[c] /= sum;
        }
        k = psygp_simulate_outcome(p, 3, rng_u());
        CHECK(psygp_update(&g, x, k) == PSYGP_OK, "categorical update");
    }
    for (int i = 0; i <= 20; i++) {
        double x = (double)i / 20.0, p[3], want[3], fs[3], sum = 0.0;
        CHECK(psygp_predict_outcomes(&g, &x, p) == PSYGP_OK, "predict_outcomes");
        fs[0] = 2.0 - 4.0 * x;
        fs[1] = 0.0;
        fs[2] = -2.0 + 4.0 * x;
        for (int c = 0; c < 3; c++) { want[c] = exp(fs[c]); sum += want[c]; }
        for (int c = 0; c < 3; c++) {
            double e = p[c] - want[c] / sum;
            rms += e * e;
            n++;
        }
    }
    rms = sqrt(rms / n);
    printf("  categorical class probability rms error %.4f\n", rms);
    CHECK(rms < 0.15, "CATEGORICAL field rms error %.4f", rms);
    /* The softmax mode satisfies the same stationarity condition as the
     * one-latent case, one block per class: f_c = m + K (y_c - pi_c). It is
     * the one cheap check on the whole of R&W Algorithm 3.3. */
    {
        int nt = psygp_n_trials(&g), ld = g.N_max;
        const psygp_trial* h = psygp_history(&g, &nt);
        double* Kd = (double*)malloc((size_t)nt * nt * sizeof(double));
        for (int i = 0; i < nt; i++)
            for (int j = 0; j < nt; j++)
                Kd[i * nt + j] = psygp__kernel(&g, h[i].x, h[j].x) +
                                 (i == j ? 1e-6 : 0.0);
        for (int c = 0; c < 3; c++)
            for (int i = 0; i < nt; i++) {
                double acc = g.hyper.mean;
                for (int j = 0; j < nt; j++) {
                    double fs[3], m2, sum = 0.0, pic;
                    for (int cc = 0; cc < 3; cc++) fs[cc] = g.f[cc * ld + j];
                    m2 = fs[0] > fs[1] ? fs[0] : fs[1];
                    if (fs[2] > m2) m2 = fs[2];
                    for (int cc = 0; cc < 3; cc++) sum += exp(fs[cc] - m2);
                    pic = exp(fs[c] - m2) / sum;
                    acc += Kd[i * nt + j] *
                           (((int)(h[j].y + 0.5) == c ? 1.0 : 0.0) - pic);
                }
                track(&worst_stat, fabs(g.f[c * ld + i] - acc));
                CLOSE(g.f[c * ld + i], acc, 1e-7 * tolx,
                      "softmax stationarity at class %d site %d", c, i);
            }
        free(Kd);
    }
    psygp_close(&g);
}

/* --- 13: the hyperparameter fit raises the marginal likelihood ---------- */

static void test_fit(void) {
    psygp_desc d;
    psygp_gp g;
    double before, after;
    int rc;
    rng_state = 0x243F6A8885A308D3ull + 0x77ull;
    memset(&d, 0, sizeof(d));
    d.n_dims = 1;
    d.lo[0] = 0.0; d.hi[0] = 1.0;
    d.n_candidates = 33;
    d.grid[0] = 33;
    d.target_p = 0.75;
    d.n_init = 30;
    d.stop_trials = 30;
    CHECK(psygp_open(&g, &d), "open fit: %s", psygp_error(&g));
    while (!psygp_done(&g)) {
        double x[1];
        psygp_next(&g, x);
        CHECK(psygp_update(&g, x, observe(x[0])) == PSYGP_OK, "fit update");
    }
    before = psygp_log_marginal(&g);
    rc = psygp_fit(&g);
    CHECK(rc == PSYGP_OK, "psygp_fit: %s", psygp_strerror(rc));
    after = psygp_log_marginal(&g);
    printf("  fit raised the log marginal from %.4f to %.4f\n", before, after);
    /* The fit maximizes the posterior, so the likelihood alone is allowed to
     * give a little back to the priors; it must not collapse, and it must move. */
    CHECK(after > before - 1.0, "the fit cost %.3f nats of likelihood",
          before - after);
    CHECK(fabs(after - before) > 1e-6, "the fit did not move the log marginal");
    {
        psygp_hyper h;
        memset(&h, 0, sizeof(h));
        CHECK(psygp_get_hyper(&g, &h) == PSYGP_OK, "get_hyper");
        CHECK(h.lengthscale[0] > 0.0 && h.outputscale > 0.0,
              "fitted hyperparameters are not positive");
    }
    psygp_close(&g);
}

/* A fit on the schedule must not break the loop, and ties drawn from the
 * caller's generator must stay inside the candidate set. */
static void test_fit_every_and_rng(void) {
    psygp_desc d;
    psygp_gp g;
    rng_state = 0x243F6A8885A308D3ull + 0x88ull;
    memset(&d, 0, sizeof(d));
    d.n_dims = 2;
    d.lo[0] = 0.0; d.hi[0] = 1.0;
    d.lo[1] = 0.0; d.hi[1] = 1.0;
    d.intensity_dim = 1;
    d.kernel = PSYGP_KERNEL_SEMIP;
    d.acq = PSYGP_ACQ_LOCALMI;
    d.target_p = 0.75;
    d.n_candidates = 128;
    d.n_init = 6;
    d.fit = true;
    d.fit_every = 5;
    d.rng = rng_cb;
    d.stop_trials = 40;
    CHECK(psygp_open(&g, &d), "open fit_every: %s", psygp_error(&g));
    while (!psygp_done(&g)) {
        double x[2];
        int idx = psygp_next(&g, x);
        int rc;
        CHECK(idx >= -1 && idx < psygp_n_candidates(&g), "next index %d", idx);
        rc = psygp_update(&g, x, observe(x[1]));
        CHECK(rc == PSYGP_OK, "fit_every update: %s", psygp_strerror(rc));
    }
    CHECK(psygp_n_trials(&g) == 40, "fit_every trials %d", psygp_n_trials(&g));
    {
        double ctx[1] = { 0.5 }, thr, lo, hi;
        int rc = psygp_threshold(&g, ctx, 0.0, &thr, &lo, &hi);
        CHECK(rc == PSYGP_OK || rc == PSYGP_ERR_NOCROSS, "semip threshold: %s",
              psygp_strerror(rc));
    }
    /* A subset call must return one of the indices it was given. psygp_done()
     * has fired, which does not stop the loop functions from working. */
    {
        int sub[3] = { 4, 9, 17 };
        double x[2];
        int idx;
        idx = psygp_next_subset(&g, sub, 3, x);
        CHECK(idx == 4 || idx == 9 || idx == 17, "next_subset returned %d", idx);
        CHECK(!(psygp_acq_score(&g, 4) != psygp_acq_score(&g, 4)),
              "acq_score is not a number");
    }
    psygp_close(&g);
}

/* --- the loop bookkeeping ----------------------------------------------- */

/* The same gradient check with the logistic link, which has its own
 * derivative chain for both the Bernoulli and the ordinal likelihood, and
 * again with a floor and a ceiling on the link, which add terms to all three
 * f-derivatives and to the cutpoint partials. */
static void test_gradient_logit(void) {
    psygp_link_gradient_probe = PSYGP_LINK_LOGIT;
    test_gradient(PSYGP_LIK_BERNOULLI, PSYGP_KERNEL_RBF, "bernoulli/logit");
    test_gradient(PSYGP_LIK_ORDINAL, PSYGP_KERNEL_RBF, "ordinal/logit");
    psygp_link_gradient_probe = PSYGP_LINK_PROBIT;
    psygp_guess_gradient_probe = 0.5;
    psygp_lapse_gradient_probe = 0.02;
    test_gradient(PSYGP_LIK_BERNOULLI, PSYGP_KERNEL_RBF, "bernoulli/2afc");
    test_gradient(PSYGP_LIK_BERNOULLI, PSYGP_KERNEL_SEMIP, "bernoulli/2afc/semip");
    test_gradient(PSYGP_LIK_ORDINAL, PSYGP_KERNEL_RBF, "ordinal/2afc");
    psygp_link_gradient_probe = PSYGP_LINK_LOGIT;
    test_gradient(PSYGP_LIK_BERNOULLI, PSYGP_KERNEL_RBF, "bernoulli/2afc/logit");
    test_gradient(PSYGP_LIK_ORDINAL, PSYGP_KERNEL_RBF, "ordinal/2afc/logit");
    psygp_link_gradient_probe = PSYGP_LINK_PROBIT;
    psygp_guess_gradient_probe = 0.0;
    psygp_lapse_gradient_probe = 0.0;
}

/* A two-interval task: the observer cannot score below 0.5, and the 75% point
 * of p = 0.5 + 0.5 Phi(slope (x - x50)) sits exactly at x50, so the true
 * threshold is known without inverting anything. */
static double afc_slope = 8.0, afc_x50 = 0.40;
/* p(correct) = 0.75 at 0.5 + 0.48 link = 0.75, i.e. link = 0.5208..., so the
 * true 75% threshold is a little above x50. */
#define AFC_THR75 (0.40 + 0.052248 / 8.0)

static int observe_afc(double x) {
    double p[2];
    p[1] = 0.5 + 0.48 * 0.5 * erfc(-(afc_slope * (x - afc_x50)) / sqrt(2.0));
    p[0] = 1.0 - p[1];
    return psygp_simulate_outcome(p, 2, rng_u());
}

static void test_2afc(void) {
    double acc = 0.0;
    int reps = 5;
    printf("two-interval observer (guess 0.5, lapse 0.02), 120 trials:\n");
    for (int r = 0; r < reps; r++) {
        psygp_desc d;
        psygp_gp g;
        double thr = 0.0, lo = 0.0, hi = 0.0;
        int rc;
        rng_state = 0x243F6A8885A308D3ull + 0xcc00ull + (uint64_t)r * 0x1000193ull;
        memset(&d, 0, sizeof(d));
        d.n_dims = 1;
        d.lo[0] = 0.0; d.hi[0] = 1.0;
        d.guess = 0.5;
        d.lapse = 0.02;
        d.acq = PSYGP_ACQ_EAVC;
        d.target_p = 0.75;      /* = 0.5 + 0.48 * 0.5208..., i.e. x50 */
        d.n_candidates = 65;
        d.grid[0] = 65;
        /* A longer init phase and a later first fit than the yes/no runs get:
         * two-interval trials carry less information, and an unregularized fit
         * on eight of them can decide the whole field is at chance. MODEL in
         * the header says what that looks like and what else to do about it. */
        d.n_init = 16;
        d.fit = true;
        d.fit_every = 20;
        d.stop_trials = 120;
        CHECK(psygp_open(&g, &d), "open 2afc: %s", psygp_error(&g));
        while (!psygp_done(&g)) {
            double x[1];
            psygp_next(&g, x);
            CHECK(psygp_update(&g, x, observe_afc(x[0])) == PSYGP_OK, "2afc update");
        }
        rc = psygp_threshold(&g, NULL, 0.0, &thr, &lo, &hi);
        CHECK(rc == PSYGP_OK, "2afc threshold: %s", psygp_strerror(rc));
        printf("  stream %d: threshold %.4f (true %.4f, error %+.4f), "
               "band %.3f wide\n", r, thr, AFC_THR75, thr - AFC_THR75, hi - lo);
        acc += fabs(thr - AFC_THR75);
        /* The floor has to show up in the predictions themselves. */
        {
            double x0[1] = { 0.0 }, p[2];
            CHECK(psygp_predict_p(&g, x0) >= 0.5,
                  "the target quantity fell below the floor");
            CHECK(psygp_predict_outcomes(&g, x0, p) == PSYGP_OK, "2afc outcomes");
            CLOSE(p[0] + p[1], 1.0, 1e-12, "2afc probabilities sum to 1");
            CHECK(p[1] >= 0.5 && p[1] <= 0.98 + 1e-12,
                  "2afc p(correct) = %g is outside the floor and ceiling", p[1]);
        }
        psygp_close(&g);
    }
    acc /= reps;
    printf("  mean absolute error %.4f\n", acc);
    CHECK(acc < 0.08, "2afc mean threshold error %.4f", acc);
}

static void test_loop_rules(void) {
    psygp_desc d;
    psygp_gp g;
    double x1[1], x2[1];
    int i1, i2, n;
    const psygp_trial* h;
    memset(&d, 0, sizeof(d));
    d.n_dims = 1;
    d.lo[0] = 0.0; d.hi[0] = 1.0;
    d.n_candidates = 17;
    d.target_p = 0.75;
    d.n_init = 2;
    d.max_trials = 4;
    d.stop_trials = 4;
    CHECK(psygp_open(&g, &d), "open loop rules: %s", psygp_error(&g));
    i1 = psygp_next(&g, x1);
    i2 = psygp_next(&g, x2);
    CHECK(i1 == i2 && x1[0] == x2[0], "next twice without an update moved");
    CHECK(psygp_update(&g, x1, 1) == PSYGP_OK, "update");
    h = psygp_history(&g, &n);
    CHECK(n == 1, "history length %d", n);
    CHECK(h[0].proposed == 1, "the trial was not marked as proposed");
    CHECK(h[0].init == 1, "the first trial is an init trial");
    /* A stimulus the caller changed is recorded, and marked as not proposed. */
    psygp_next(&g, x1);
    x2[0] = x1[0] * 0.5;
    CHECK(psygp_update(&g, x2, 0) == PSYGP_OK, "update with a shifted stimulus");
    h = psygp_history(&g, &n);
    CHECK(h[1].proposed == 0, "a changed stimulus was marked as proposed");
    CLOSE(h[1].x[0], x2[0], 0.0, "the history records what was shown");
    /* Argument checks. */
    CHECK(psygp_update(&g, x1, 5) == PSYGP_ERR_ARG, "outcome out of range");
    CHECK(psygp_update_real(&g, x1, 0.5) == PSYGP_ERR_ARG,
          "update_real under a discrete likelihood");
    x2[0] = 2.0;
    CHECK(psygp_update(&g, x2, 1) == PSYGP_ERR_ARG, "a stimulus outside the box");
    /* Fill to max_trials and check the ceiling. */
    x2[0] = 0.5;
    CHECK(psygp_update(&g, x2, 1) == PSYGP_OK, "update 3");
    CHECK(psygp_update(&g, x2, 0) == PSYGP_OK, "update 4");
    CHECK(psygp_stop_reason(&g) == PSYGP_STOP_FULL, "stop reason %d",
          (int)psygp_stop_reason(&g));
    CHECK(psygp_update(&g, x2, 1) == PSYGP_ERR_FULL, "update past max_trials");
    CHECK(psygp_next(&g, x1) == PSYGP_ERR_FULL, "next past max_trials");
    psygp_close(&g);
    CHECK(!psygp_is_open(&g), "the handle is still open after close");
}

/* The configurations the checks above do not reach: a caller-owned candidate
 * list, the random acquisition, the logit link, the threshold-width stop rule,
 * and the caller's own memory. */
static void test_configurations(void) {
    psygp_desc d;
    psygp_gp g;
    static double cands[11];
    /* Static, not automatic: a megabyte of stack is more than MSVC's default
     * thread gets. */
    static unsigned char buf[1 << 20];
    size_t need;
    int seen_stop = 0;
    rng_state = 0x243F6A8885A308D3ull + 0x99ull;
    for (int i = 0; i < 11; i++) cands[i] = 0.1 * (double)i;
    memset(&d, 0, sizeof(d));
    d.n_dims = 1;
    d.lo[0] = 0.0; d.hi[0] = 1.0;
    d.link = PSYGP_LINK_LOGIT;
    d.acq = PSYGP_ACQ_RANDOM;
    d.candidates = cands;
    d.n_candidates = 11;
    d.n_init = 4;
    d.rng = rng_cb;
    d.fit = true;
    d.fit_every = 10;
    d.max_trials = 60;
    d.stop_trials = 0;
    d.stop_threshold_sd = 0.6;
    need = psygp_memory_size(&d);
    CHECK(need > 0 && need < sizeof(buf), "configuration memory %lu bytes",
          (unsigned long)need);
    d.memory = buf;
    d.memory_size = sizeof(buf);
    CHECK(psygp_open(&g, &d), "open configurations: %s", psygp_error(&g));
    CHECK(psygp_n_candidates(&g) == 11, "candidate count");
    {
        double c[1];
        CHECK(psygp_candidate(&g, 7, c) == PSYGP_OK, "psygp_candidate");
        CLOSE(c[0], 0.7, 1e-15, "the caller's candidate list is what is used");
        CHECK(psygp_candidate(&g, 11, c) == PSYGP_ERR_ARG, "candidate index");
    }
    while (!psygp_done(&g)) {
        double x[1];
        int idx = psygp_next(&g, x);
        CHECK(idx == -1, "RANDOM proposed candidate %d, not a free point", idx);
        CHECK(x[0] >= 0.0 && x[0] <= 1.0, "a point outside the box");
        CHECK(psygp_update(&g, x, observe(x[0])) == PSYGP_OK, "random update");
        if (psygp_n_trials(&g) >= 60) break;
    }
    seen_stop = (int)psygp_stop_reason(&g);
    CHECK(seen_stop == (int)PSYGP_STOP_THRESHOLD_SD ||
          seen_stop == (int)PSYGP_STOP_FULL,
          "stop reason %d after %d trials", seen_stop, psygp_n_trials(&g));
    printf("  logit / random / caller candidates: %d trials, stop reason %d\n",
           psygp_n_trials(&g), seen_stop);
    {   /* The outcome probabilities are a distribution. */
        double x[1] = { 0.45 }, p[2];
        CHECK(psygp_predict_outcomes(&g, x, p) == PSYGP_OK, "predict_outcomes");
        CLOSE(p[0] + p[1], 1.0, 1e-12, "Bernoulli probabilities sum to 1");
        CLOSE(p[1], psygp_predict_p(&g, x), 1e-12, "p[1] is the target quantity");
        CHECK(psygp_predict_p_var(&g, x) >= 0.0, "a negative variance");
    }
    psygp_close(&g);
    /* The same, ordinal, to check that the K probabilities are a distribution
     * and that the target quantity is their upper tail. */
    memset(&d, 0, sizeof(d));
    d.n_dims = 1;
    d.lo[0] = 0.0; d.hi[0] = 1.0;
    d.lik = PSYGP_LIK_ORDINAL;
    d.n_outcomes = 4;
    d.target_outcome = 2;
    d.target_p = 0.5;
    d.acq = PSYGP_ACQ_LOCALMI;
    d.n_candidates = 21;
    d.n_init = 6;
    d.stop_trials = 24;
    CHECK(psygp_open(&g, &d), "open ordinal outcomes: %s", psygp_error(&g));
    while (!psygp_done(&g)) {
        double x[1];
        psygp_next(&g, x);
        CHECK(psygp_update(&g, x, (int)(rng_u() * 4.0)) == PSYGP_OK,
              "ordinal outcome update");
    }
    {
        double x[1] = { 0.6 }, p[4], sum = 0.0;
        CHECK(psygp_predict_outcomes(&g, x, p) == PSYGP_OK, "ordinal outcomes");
        for (int i = 0; i < 4; i++) {
            CHECK(p[i] >= 0.0, "ordinal probability %d is negative", i);
            sum += p[i];
        }
        CLOSE(sum, 1.0, 1e-12, "ordinal probabilities sum to 1");
        CLOSE(psygp_predict_p(&g, x), p[2] + p[3], 1e-10,
              "the target quantity is P(y >= 2)");
    }
    psygp_close(&g);
}

/* The stepped fit must land where the whole fit lands, and must leave the model
 * consistent between steps. */
static void test_fit_step(void) {
    psygp_desc d;
    psygp_gp ga, gb;
    double ys[40];
    int n = 40, steps = 0, rc;
    rng_state = 0x243F6A8885A308D3ull + 0xaaull;
    memset(&d, 0, sizeof(d));
    d.n_dims = 1;
    d.lo[0] = 0.0; d.hi[0] = 1.0;
    d.n_candidates = 33;
    d.target_p = 0.75;
    d.n_init = 40;
    d.stop_trials = 40;
    CHECK(psygp_open(&ga, &d), "open fit_step a: %s", psygp_error(&ga));
    CHECK(psygp_open(&gb, &d), "open fit_step b: %s", psygp_error(&gb));
    for (int i = 0; i < n; i++) {
        double x[1];
        psygp_next(&ga, x);
        ys[i] = (double)observe(x[0]);
        CHECK(psygp_update(&ga, x, (int)ys[i]) == PSYGP_OK, "fit_step update a");
        CHECK(psygp_update(&gb, x, (int)ys[i]) == PSYGP_OK, "fit_step update b");
    }
    CHECK(psygp_fit(&ga) == PSYGP_OK, "whole fit");
    while ((rc = psygp_fit_step(&gb)) > 0) {
        double lm = psygp_log_marginal(&gb);
        CHECK(lm == lm, "the stepped fit left the model without a marginal");
        if (++steps > 200) { CHECK(0, "psygp_fit_step does not terminate"); break; }
    }
    CHECK(rc == 0, "psygp_fit_step ended with %d", rc);
    printf("  the stepped fit took %d calls to reach %.6f (whole fit %.6f)\n",
           steps, psygp_log_marginal(&gb), psygp_log_marginal(&ga));
    CLOSE(psygp_log_marginal(&gb), psygp_log_marginal(&ga), 1e-9,
          "stepped and whole fits disagree");
    {
        psygp_hyper ha, hb;
        memset(&ha, 0, sizeof(ha));
        memset(&hb, 0, sizeof(hb));
        psygp_get_hyper(&ga, &ha);
        psygp_get_hyper(&gb, &hb);
        CLOSE(ha.lengthscale[0], hb.lengthscale[0], 1e-12, "fit lengthscale");
        CLOSE(ha.outputscale, hb.outputscale, 1e-12, "fit output scale");
    }
    psygp_close(&ga);
    psygp_close(&gb);
}

/* desc.refit_every trades an exact Newton refit per trial for a rank-one
 * update. Replaying one response stream into both settings says how much that
 * costs, and psygp_refit() must erase the difference. */
static void test_refit_every(void) {
    psygp_desc d;
    psygp_gp ga, gb;
    double ys[80], worst = 0.0, after = 0.0;
    /* Not a multiple of refit_every: the run has to END on a cheap update, or
     * the comparison only sees the exact refit that closed it. */
    int n = 78;
    rng_state = 0x243F6A8885A308D3ull + 0xbbull;
    memset(&d, 0, sizeof(d));
    d.n_dims = 1;
    d.lo[0] = 0.0; d.hi[0] = 1.0;
    d.acq = PSYGP_ACQ_LSE;
    d.target_p = 0.75;
    d.n_candidates = 65;
    d.grid[0] = 65;
    d.n_init = 8;
    d.stop_trials = n;
    CHECK(psygp_open(&ga, &d), "open refit a: %s", psygp_error(&ga));
    d.refit_every = 5;
    CHECK(psygp_open(&gb, &d), "open refit b: %s", psygp_error(&gb));
    for (int i = 0; i < n; i++) {
        double x[1];
        psygp_next(&ga, x);          /* one stimulus stream for both handles */
        ys[i] = (double)observe(x[0]);
        CHECK(psygp_update(&ga, x, (int)ys[i]) == PSYGP_OK, "refit update a");
        CHECK(psygp_update(&gb, x, (int)ys[i]) == PSYGP_OK, "refit update b");
    }
    for (int i = 0; i <= 40; i++) {
        double x = (double)i / 40.0;
        double e = fabs(psygp_predict_p(&ga, &x) - psygp_predict_p(&gb, &x));
        if (e > worst) worst = e;
    }
    CHECK(psygp_refit(&gb) == PSYGP_OK, "psygp_refit");
    for (int i = 0; i <= 40; i++) {
        double x = (double)i / 40.0;
        double e = fabs(psygp_predict_p(&ga, &x) - psygp_predict_p(&gb, &x));
        if (e > after) after = e;
    }
    printf("  refit_every 5 vs 1: worst P difference %.2e, after psygp_refit "
           "%.2e\n", worst, after);
    CHECK(worst < 5e-3, "refit_every 5 drifts by %.3g in probability", worst);
    CHECK(after < 1e-8 * tolx,
          "psygp_refit did not restore the exact posterior (%.3g)", after);
    {
        double ta = 0.0, tb = 0.0;
        CHECK(psygp_threshold(&ga, NULL, 0.0, &ta, NULL, NULL) == PSYGP_OK,
              "refit threshold a");
        CHECK(psygp_threshold(&gb, NULL, 0.0, &tb, NULL, NULL) == PSYGP_OK,
              "refit threshold b");
        CLOSE(ta, tb, 1e-6, "thresholds after psygp_refit");
    }
    psygp_close(&ga);
    psygp_close(&gb);
}

/* The batched predictions must agree with the one-point calls they replace. */
static void test_predict_many(void) {
    psygp_desc d;
    psygp_gp g;
    static double xs[900 * 2], pm[900], mum[900], sdm[900];
    int n = 0, nd = 2;
    double worst_p = 0.0, worst_mu = 0.0, worst_sd = 0.0;
    rng_state = 0x243F6A8885A308D3ull + 0xddull;
    memset(&d, 0, sizeof(d));
    d.n_dims = nd;
    d.lo[0] = 0.0; d.hi[0] = 1.0;
    d.lo[1] = -1.0; d.hi[1] = 1.0;
    d.intensity_dim = 1;
    d.target_p = 0.75;
    d.n_candidates = 64;
    d.n_init = 8;
    d.fit = true;
    d.fit_every = 20;
    d.stop_trials = 40;
    CHECK(psygp_open(&g, &d), "open predict_many: %s", psygp_error(&g));
    while (!psygp_done(&g)) {
        double x[2];
        psygp_next(&g, x);
        CHECK(psygp_update(&g, x, observe(x[1])) == PSYGP_OK, "many update");
    }
    for (int i = 0; i < 30; i++)
        for (int j = 0; j < 30; j++) {
            xs[n * 2 + 0] = (double)i / 29.0;
            xs[n * 2 + 1] = -1.0 + 2.0 * (double)j / 29.0;
            n++;
        }
    CHECK(psygp_predict_p_many(&g, xs, n, pm) == PSYGP_OK, "predict_p_many");
    CHECK(psygp_predict_f_many(&g, xs, n, 0, mum, sdm) == PSYGP_OK,
          "predict_f_many");
    for (int i = 0; i < n; i++) {
        double mu1 = 0.0, sd1 = 0.0, p1;
        CHECK(psygp_predict_f(&g, xs + i * 2, 0, &mu1, &sd1) == PSYGP_OK,
              "predict_f point %d", i);
        p1 = psygp_predict_p(&g, xs + i * 2);
        if (fabs(p1 - pm[i]) > worst_p) worst_p = fabs(p1 - pm[i]);
        if (fabs(mu1 - mum[i]) > worst_mu) worst_mu = fabs(mu1 - mum[i]);
        if (fabs(sd1 - sdm[i]) > worst_sd) worst_sd = fabs(sd1 - sdm[i]);
    }
    printf("  batched vs one-point over %d points: p %.2e, mu %.2e, sd %.2e\n",
           n, worst_p, worst_mu, worst_sd);
    CHECK(worst_p < 1e-12 * tolx, "predict_p_many differs by %.3g", worst_p);
    CHECK(worst_mu < 1e-12 * tolx, "predict_f_many mean differs by %.3g", worst_mu);
    CHECK(worst_sd < 1e-12 * tolx, "predict_f_many sd differs by %.3g", worst_sd);
    /* One output at a time, and the argument checks. */
    CHECK(psygp_predict_f_many(&g, xs, n, 0, mum, NULL) == PSYGP_OK, "mu only");
    CHECK(psygp_predict_f_many(&g, xs, n, 0, NULL, sdm) == PSYGP_OK, "sd only");
    CHECK(psygp_predict_f_many(&g, xs, n, 1, mum, sdm) == PSYGP_ERR_ARG,
          "latent out of range");
    {
        double bad[2] = { 0.5, 5.0 };
        CHECK(psygp_predict_p_many(&g, bad, 1, pm) == PSYGP_ERR_ARG,
              "a point outside the box");
    }
    psygp_close(&g);
}

/* A Cholesky that fails must report it, record the trial anyway, and leave the
 * posterior of the trials that did factor in force. Duplicate stimuli with no
 * jitter and no noise make a singular matrix on purpose. */
static void test_numeric_failure(void) {
    psygp_desc d;
    psygp_gp g;
    double x[1] = { 0.3 }, mu1 = 0.0, sd1 = 0.0, mu2 = 0.0, sd2 = 0.0;
    memset(&d, 0, sizeof(d));
    d.n_dims = 1;
    d.lo[0] = 0.0; d.hi[0] = 1.0;
    d.lik = PSYGP_LIK_GAUSSIAN;
    d.acq = PSYGP_ACQ_BALV;
    d.hyper.noise_sd = 1e-300;
    d.jitter = 1e-300;
    d.stop_trials = 8;
    CHECK(psygp_open(&g, &d), "open numeric: %s", psygp_error(&g));
    CHECK(psygp_update_real(&g, x, 1.0) == PSYGP_OK, "first update");
    CHECK(psygp_predict_f(&g, x, 0, &mu1, &sd1) == PSYGP_OK, "predict before");
    CHECK(psygp_update_real(&g, x, 1.0) == PSYGP_ERR_NUMERIC,
          "a singular matrix was not reported");
    CHECK(psygp_n_trials(&g) == 2, "the failed trial was not recorded");
    CHECK(psygp_predict_f(&g, x, 0, &mu2, &sd2) == PSYGP_OK, "predict after");
    CLOSE(mu2, mu1, 1e-12, "the previous posterior did not stay in force");
    psygp_close(&g);
}

/* --- desc.refine_steps ------------------------------------------------- */

/* The refinement climbs psygp__point_score(), so first that the point score
 * at a candidate IS the candidate's acquisition score (the straddle, for
 * EAVC), then that a refined proposal never scores below the grid winner it
 * started from and stays within one grid step of it, then that it runs under
 * every acquisition and under CATEGORICAL. */
static void refine_session(psygp_acq acq, psygp_lik lik, const char* name) {
    static const double h = 1.0 / 8.0;   /* the 9-point grid's step */
    psygp_desc d;
    psygp_gp g;
    psygp__scr s;
    int refined = 0, rc;
    double worst_id = 0.0;
    rng_state = 0x243F6A8885A308D3ull + 0x77ull + (uint64_t)acq;
    memset(&d, 0, sizeof(d));
    d.n_dims = 1;
    d.lo[0] = 0.0; d.hi[0] = 1.0;
    d.lik = lik;
    if (lik == PSYGP_LIK_CATEGORICAL) { d.n_outcomes = 3; d.target_outcome = 1; }
    d.acq = acq;
    d.target_p = lik == PSYGP_LIK_CATEGORICAL ? 0.5 : 0.75;
    d.grid[0] = 9;
    d.n_init = 6;
    d.refine_steps = 3;
    d.stop_trials = 40;
    if (!psygp_open(&g, &d)) {
        CHECK(0, "open refine %s: %s", name, psygp_error(&g));
        return;
    }
    psygp__scr_of(&g, &s);
    while (!psygp_done(&g)) {
        double x[1];
        int idx, out;
        bool init = psygp_n_trials(&g) < d.n_init;
        idx = psygp_next(&g, x);
        CHECK(idx >= -1, "refine %s: next returned %d", name, idx);
        CHECK(x[0] >= 0.0 && x[0] <= 1.0, "refine %s: %g outside the box",
              name, x[0]);
        if (!init) {
            /* The grid winner, from the scores next() just computed. */
            int jb = 0;
            double sb = psygp_acq_score(&g, 0), sx, sj;
            for (int j = 1; j < 9; j++) {
                double sc = psygp_acq_score(&g, j);
                if (sc > sb) { sb = sc; jb = j; }
            }
            if (acq != PSYGP_ACQ_EAVC) {
                for (int j = 0; j < 9; j++) {
                    double sc = psygp_acq_score(&g, j);
                    double ps = psygp__point_score(&g, &s, g.cand + j);
                    double e = fabs(ps - sc) / (1.0 + fabs(sc));
                    if (e > worst_id) worst_id = e;
                }
            }
            /* The repeat guard's Halton point is also -1, and it clears
             * last_index; a refined point keeps the winner it came from. */
            if (idx == -1 && g.last_index >= 0) {
                int jw = g.last_index;
                sj = psygp__point_score(&g, &s, g.cand + jw);
                sx = psygp__point_score(&g, &s, x);
                refined++;
                CHECK(jw == jb || acq == PSYGP_ACQ_EAVC ||
                      psygp_acq_score(&g, jw) >= sb - 1e-12 * (1.0 + fabs(sb)),
                      "refine %s: started from %d, not the winner %d", name, jw, jb);
                CHECK(fabs(x[0] - g.cand[jw]) <= h + 1e-12,
                      "refine %s: %g is more than a grid step from %g", name,
                      x[0], g.cand[jw]);
                CHECK(sx >= sj - 1e-12 * (1.0 + fabs(sj)),
                      "refine %s: refined score %.9g below the winner's %.9g",
                      name, sx, sj);
            }
        }
        if (lik == PSYGP_LIK_CATEGORICAL) {
            double p[3];
            p[0] = 0.3; p[1] = 0.5 * erfc(-(6.0 * (x[0] - 0.5)) / sqrt(2.0)) * 0.7;
            p[2] = 1.0 - p[0] - p[1];
            out = psygp_simulate_outcome(p, 3, rng_u());
        } else {
            out = observe(x[0]);
        }
        rc = psygp_update(&g, x, out);
        CHECK(rc == PSYGP_OK, "refine %s: update %s", name, psygp_strerror(rc));
    }
    CHECK(worst_id < 1e-9 * tolx,
          "refine %s: point score differs from the candidate score by %.2e",
          name, worst_id);
    CHECK(refined > 0, "refine %s: no proposal was refined in 34 trials", name);
    printf("  %-18s %2d of 34 proposals refined off the grid, point score vs "
           "candidate score %.1e\n", name, refined, worst_id);
    psygp_close(&g);
}

static void test_refine(void) {
    psygp_desc d;
    psygp_gp g;
    printf("desc.refine_steps:\n");
    memset(&d, 0, sizeof(d));
    d.n_dims = 1;
    d.lo[0] = 0.0; d.hi[0] = 1.0;
    d.target_p = 0.75;
    d.refine_steps = -1;
    CHECK(!psygp_open(&g, &d), "open accepted refine_steps = -1");
    d.refine_steps = PSYGP__REFINE_MAX + 1;
    CHECK(!psygp_open(&g, &d), "open accepted refine_steps = %d",
          PSYGP__REFINE_MAX + 1);
    refine_session(PSYGP_ACQ_LSE, PSYGP_LIK_BERNOULLI, "LSE");
    refine_session(PSYGP_ACQ_BALV, PSYGP_LIK_BERNOULLI, "BALV");
    refine_session(PSYGP_ACQ_BALD, PSYGP_LIK_BERNOULLI, "BALD");
    refine_session(PSYGP_ACQ_LOCALMI, PSYGP_LIK_BERNOULLI, "LOCALMI");
    refine_session(PSYGP_ACQ_EAVC, PSYGP_LIK_BERNOULLI, "EAVC (straddle)");
    refine_session(PSYGP_ACQ_BALV, PSYGP_LIK_CATEGORICAL, "BALV, CATEGORICAL");
}
static void test_simulate_outcome(void) {
    double p[3] = { 0.2, 0.3, 0.5 };
    CHECK(psygp_simulate_outcome(p, 3, 0.0) == 0, "simulate at u = 0");
    CHECK(psygp_simulate_outcome(p, 3, 0.19) == 0, "simulate at u = 0.19");
    CHECK(psygp_simulate_outcome(p, 3, 0.21) == 1, "simulate at u = 0.21");
    CHECK(psygp_simulate_outcome(p, 3, 0.51) == 2, "simulate at u = 0.51");
    CHECK(psygp_simulate_outcome(p, 3, 1.0) == 2, "simulate at u = 1");
    CHECK(psygp_simulate_outcome(NULL, 3, 0.5) == PSYGP_ERR_ARG, "null probs");
}

/* --- the async layer, only when it was compiled in --------------------- */
#ifdef PSYGP_ASYNC

/* One 1-D session, driven synchronously, recording what it showed and what it
 * heard, so the async run can be handed the same responses. */
#define ASYNC_TRIALS 40

static double async_x[ASYNC_TRIALS];
static int async_y[ASYNC_TRIALS];

static void async_reference(psygp_gp* g, psygp_desc* d) {
    rng_state = 0x243F6A8885A308D3ull + 0xee00ull;
    memset(d, 0, sizeof(*d));
    d->n_dims = 1;
    d->lo[0] = 0.0; d->hi[0] = 1.0;
    d->acq = PSYGP_ACQ_LSE;
    d->target_p = 0.75;
    d->n_candidates = 33;
    d->grid[0] = 33;
    d->n_init = 6;
    d->fit = true;
    d->fit_every = 10;
    d->stop_trials = ASYNC_TRIALS;
    CHECK(psygp_open(g, d), "open async reference: %s", psygp_error(g));
    for (int i = 0; i < ASYNC_TRIALS; i++) {
        double x[1];
        psygp_next(g, x);
        async_x[i] = x[0];
        async_y[i] = observe(x[0]);
        CHECK(psygp_update(g, x, async_y[i]) == PSYGP_OK, "reference update");
    }
}

/* The same session through the pump. The proposals come out of the snapshot,
 * the responses out of the same observer with the same seed, so if the layer
 * changes nothing the two runs are the same run. */
static void test_async(void) {
    psygp_desc d;
    psygp_gp ref, g;
    psygp_async a;
    psygp_snapshot s;
    int n = 0, nref = 0;
    const psygp_trial *href, *hasync;
    async_reference(&ref, &d);
    href = psygp_history(&ref, &nref);

    memset(&a, 0, sizeof(a));
    memset(&g, 0, sizeof(g));
    CHECK(psygp_open(&g, &d), "open async gp: %s", psygp_error(&g));
    {
        psygp_async_desc ad;
        memset(&ad, 0, sizeof(ad));
        ad.gp = &g;
        CHECK(psygp_async_start(&a, &ad), "async start: %s", psygp_async_error(&a));
        CHECK(psygp_async_is_running(&a), "the thread is not running");
    }
    rng_state = 0x243F6A8885A308D3ull + 0xee00ull;
    CHECK(psygp_async_poll(&a, &s) == 0, "the first poll is seq 0");
    for (int i = 0; i < ASYNC_TRIALS; i++) {
        int seq, rc, y;
        CLOSE(s.x[0], async_x[i], 0.0, "async proposal %d differs", i);
        y = observe(s.x[0]);
        CHECK(y == async_y[i], "async response %d differs", i);
        seq = psygp_async_submit(&a, s.x, y);
        CHECK(seq > 0, "submit %d: %s", i, psygp_strerror(seq));
        rc = psygp_async_wait(&a, (uint32_t)seq, 20000000000ull, &s);
        CHECK(rc >= seq, "wait %d returned %d (%s)", i, rc, psygp_strerror(rc));
        CHECK(s.update_rc == PSYGP_OK, "update_rc %d on trial %d",
              s.update_rc, i);
        CHECK(s.n_trials == i + 1, "n_trials %d at trial %d", s.n_trials, i);
    }
    CHECK(s.done, "the async run did not reach its stop criterion");
    CHECK(s.numeric == 0, "%d numeric failures on the thread", s.numeric);
    psygp_async_stop(&a);
    CHECK(!psygp_async_is_running(&a), "still running after stop");

    /* Bit for bit: the history, the posterior mode and the log marginal. */
    hasync = psygp_history(&g, &n);
    CHECK(n == nref, "async %d trials, reference %d", n, nref);
    CHECK(memcmp(href, hasync, (size_t)n * sizeof(psygp_trial)) == 0,
          "the async history is not the reference history");
    CHECK(memcmp(ref.f, g.f, (size_t)n * sizeof(double)) == 0,
          "the async posterior mode differs");
    CLOSE(psygp_log_marginal(&g), psygp_log_marginal(&ref), 0.0,
          "the async log marginal differs");
    {
        double t1 = 0.0, t2 = 0.0;
        CHECK(psygp_threshold(&g, NULL, 0.0, &t1, NULL, NULL) == PSYGP_OK,
              "async threshold");
        CHECK(psygp_threshold(&ref, NULL, 0.0, &t2, NULL, NULL) == PSYGP_OK,
              "reference threshold");
        CLOSE(t1, t2, 0.0, "the async threshold differs");
        printf("  async run matches the synchronous run bit for bit; "
               "threshold %.4f, snapshot threshold %.4f\n", t1, s.threshold);
        CLOSE(s.threshold, t1, 1e-12, "the snapshot threshold is stale");
    }
    psygp_close(&g);
    psygp_close(&ref);
}

/* With fit_in_idle the thread fits between trials: the log marginal has to
 * move without any submit, and the run still has to converge. */
static void test_async_fit_in_idle(void) {
    psygp_desc d;
    psygp_gp g;
    psygp_async a;
    psygp_async_desc ad;
    psygp_snapshot s;
    double lm_first = 0.0, lm_last = 0.0;
    int idle_gain = 0;
    rng_state = 0x243F6A8885A308D3ull + 0xef00ull;
    memset(&d, 0, sizeof(d));
    memset(&a, 0, sizeof(a));
    memset(&g, 0, sizeof(g));
    d.n_dims = 1;
    d.lo[0] = 0.0; d.hi[0] = 1.0;
    d.acq = PSYGP_ACQ_EAVC;
    d.target_p = 0.75;
    d.n_candidates = 33;
    d.grid[0] = 33;
    d.n_init = 8;
    d.fit = true;
    d.fit_every = 10;          /* ignored while fit_in_idle is set */
    d.stop_trials = 60;
    CHECK(psygp_open(&g, &d), "open fit_in_idle: %s", psygp_error(&g));
    memset(&ad, 0, sizeof(ad));
    ad.gp = &g;
    ad.fit_in_idle = true;
    ad.below_normal = true;
    CHECK(psygp_async_start(&a, &ad), "fit_in_idle start: %s",
          psygp_async_error(&a));
    CHECK(g.desc.fit_every == 0, "fit_every was not parked");
    psygp_async_poll(&a, &s);
    for (int i = 0; i < 60; i++) {
        int seq, rc;
        double lm_before, lm_after;
        int y = observe(s.x[0]);
        seq = psygp_async_submit(&a, s.x, y);
        CHECK(seq > 0, "fit_in_idle submit: %s", psygp_strerror(seq));
        rc = psygp_async_wait(&a, (uint32_t)seq, 20000000000ull, &s);
        CHECK(rc >= seq, "fit_in_idle wait: %s", psygp_strerror(rc));
        lm_before = s.log_marginal;
        if (i == 5) lm_first = lm_before;
        /* Hand the thread an idle interval, as a frame loop would. */
        psyrt_sleep_until(psyrt_now_ns() + 12000000ull, 0);
        psygp_async_poll(&a, &s);
        lm_after = s.log_marginal;
        if (lm_after > lm_before + 1e-9) idle_gain++;
        lm_last = lm_after;
    }
    printf("  fit_in_idle: the log marginal improved in an idle gap on %d of "
           "60 trials, %.3f to %.3f\n", idle_gain, lm_first, lm_last);
    CHECK(idle_gain > 0, "the idle fit never improved the log marginal");
    psygp_async_stop(&a);
    CHECK(g.desc.fit_every == 10, "fit_every was not restored");
    {
        double thr = 0.0;
        CHECK(psygp_threshold(&g, NULL, 0.0, &thr, NULL, NULL) == PSYGP_OK,
              "fit_in_idle threshold");
        printf("  fit_in_idle threshold %.4f (true %.4f)\n", thr, OBS_THR75);
        CHECK(fabs(thr - OBS_THR75) < 0.1,
              "fit_in_idle threshold error %.4f", fabs(thr - OBS_THR75));
    }
    psygp_close(&g);
}

/* A full queue must say so and copy nothing, and a stop must deliver what it
 * accepted. */
static void test_async_queue(void) {
    psygp_desc d;
    psygp_gp g;
    psygp_async a;
    psygp_async_desc ad;
    int accepted = 0, busy = 0;
    rng_state = 0x243F6A8885A308D3ull + 0xf000ull;
    memset(&d, 0, sizeof(d));
    memset(&a, 0, sizeof(a));
    memset(&g, 0, sizeof(g));
    d.n_dims = 1;
    d.lo[0] = 0.0; d.hi[0] = 1.0;
    d.acq = PSYGP_ACQ_EAVC;       /* slow enough that the queue can fill */
    d.target_p = 0.75;
    d.n_candidates = 257;
    d.n_init = 4;
    d.max_trials = 300;
    d.stop_trials = 300;
    CHECK(psygp_open(&g, &d), "open queue test: %s", psygp_error(&g));
    memset(&ad, 0, sizeof(ad));
    ad.gp = &g;
    CHECK(psygp_async_start(&a, &ad), "queue start: %s", psygp_async_error(&a));
    for (int i = 0; i < 200 && busy < 3; i++) {
        double x[1];
        int rc;
        x[0] = 0.01 + 0.98 * ((double)(i % 17) / 17.0);
        rc = psygp_async_submit(&a, x, i & 1);
        if (rc == PSYGP_ERR_BUSY) busy++;
        else { CHECK(rc > 0, "submit %d: %s", i, psygp_strerror(rc)); accepted++; }
    }
    printf("  queue: %d accepted, %d refused with PSYGP_ERR_BUSY, %d pending "
           "at stop\n", accepted, busy, psygp_async_pending(&a));
    CHECK(busy >= 1, "the queue never filled, so BUSY was never tested");
    /* Argument checks, on the calling thread. */
    {
        double bad[1] = { 2.0 }, ok[1] = { 0.5 };
        CHECK(psygp_async_submit(&a, bad, 1) == PSYGP_ERR_ARG, "x outside the box");
        CHECK(psygp_async_submit(&a, ok, 7) == PSYGP_ERR_ARG, "outcome out of range");
        CHECK(psygp_async_submit(&a, NULL, 1) == PSYGP_ERR_ARG, "null x");
        CHECK(psygp_async_submit_real(&a, ok, 0.5) == PSYGP_ERR_ARG,
              "submit_real under a discrete likelihood");
    }
    psygp_async_stop(&a);          /* drains */
    CHECK(psygp_n_trials(&g) == accepted,
          "stop dropped responses: %d trials of %d accepted",
          psygp_n_trials(&g), accepted);
    CHECK(psygp_async_submit(&a, async_x, 1) == PSYGP_ERR_CLOSED,
          "submit after stop");
    CHECK(psygp_async_pending(&a) == PSYGP_ERR_CLOSED, "pending after stop");
    psygp_close(&g);
    /* A start that cannot work says why and leaves nothing running. */
    {
        psygp_async b;
        psygp_async_desc bd;
        memset(&b, 0, sizeof(b));
        memset(&bd, 0, sizeof(bd));
        CHECK(!psygp_async_start(&b, &bd), "start with no gp");
        CHECK(psygp_async_error(&b)[0] != 0, "start left no message");
        CHECK(!psygp_async_is_running(&b), "a failed start left a thread");
        psygp_async_stop(&b);   /* safe on a handle that never started */
    }
}
#endif /* PSYGP_ASYNC */

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("psy_gp.h tests\n");
    test_version();
    test_chol();
    test_quad();
    test_halton();
    test_kernel(PSYGP_KERNEL_RBF);
    test_kernel(PSYGP_KERNEL_SEMIP);
    test_memory();
    test_open_reject();
    test_simulate_outcome();
    if (real_is_double) test_numeric_failure();
    test_configurations();
    test_fit_step();
    test_refit_every();
    test_predict_many();
    test_2afc();
    if (real_is_double) test_gaussian_exact();
    test_laplace_probit();
    test_lookahead();
    test_gradient(PSYGP_LIK_BERNOULLI, PSYGP_KERNEL_RBF, "bernoulli/rbf");
    test_gradient(PSYGP_LIK_BERNOULLI, PSYGP_KERNEL_SEMIP, "bernoulli/semip");
    test_gradient(PSYGP_LIK_ORDINAL, PSYGP_KERNEL_RBF, "ordinal/rbf");
    test_gradient(PSYGP_LIK_ORDINAL, PSYGP_KERNEL_SEMIP, "ordinal/semip");
    if (real_is_double) {
        test_gradient(PSYGP_LIK_GAUSSIAN, PSYGP_KERNEL_RBF, "gaussian/rbf");
        test_gradient(PSYGP_LIK_GAUSSIAN, PSYGP_KERNEL_SEMIP, "gaussian/semip");
    }
    test_gradient_logit();
    test_loop_rules();
    printf("simulated 1-D probit observer, 100 trials, 8 response streams:\n");
    {
        double e_lse = mean_error(PSYGP_ACQ_LSE, 100, "LSE", 8);
        double e_eavc = mean_error(PSYGP_ACQ_EAVC, 100, "EAVC", 8);
        double e_bald = mean_error(PSYGP_ACQ_BALD, 100, "BALD", 8);
        printf("  mean absolute error: LSE %.4f, EAVC %.4f, BALD %.4f\n",
               e_lse, e_eavc, e_bald);
        CHECK(e_lse < 0.08, "LSE mean threshold error %.4f", e_lse);
        CHECK(e_eavc < 0.08, "EAVC mean threshold error %.4f", e_eavc);
        CHECK(e_bald < 0.08, "BALD mean threshold error %.4f", e_bald);
    }
    test_refine();
    {
        /* The same eight LSE streams with two rounds of refinement. */
        double e_ref;
        obs_refine = 2;
        e_ref = mean_error(PSYGP_ACQ_LSE, 100, "LSE+r", 8);
        obs_refine = 0;
        printf("  mean absolute error: LSE with refine_steps = 2 %.4f\n",
               e_ref);
        CHECK(e_ref < 0.08, "refined LSE mean threshold error %.4f", e_ref);
    }
    if (real_is_double) test_gaussian_field();
    test_ordinal_field();
    test_categorical_field();
    test_fit();
    test_fit_every_and_rng();
#ifdef PSYGP_ASYNC
    printf("async layer (PSYGP_ASYNC):\n");
    test_async();
    test_async_fit_in_idle();
    test_async_queue();
#endif
    printf("worst observed error, PSYGP_REAL = %s (%u bytes):\n",
           real_is_double ? "double" : "float", (unsigned)sizeof(psygp_real));
    printf("  Cholesky and inverse %.2e, Laplace stationarity %.2e\n",
           worst_chol, worst_stat);
    printf("  predictive variance %.2e, log marginal %.2e\n",
           worst_var, worst_lm);
    printf("  look-ahead covariance %.2e, gradient vs differences %.2e (relative)\n",
           worst_look, worst_grad);
    if (psygp_test_fails == 0) {
        printf("all psy_gp.h tests passed\n");
        return 0;
    }
    printf("%d psy_gp.h check(s) failed\n", psygp_test_fails);
    return 1;
}
