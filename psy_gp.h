/* psy_gp.h - v0.2.1 - public domain single-header Gaussian-process adaptive library
 *
 *   Nonparametric and semiparametric Bayesian adaptive psychophysics: a
 *   Gaussian-process model over a multidimensional stimulus space with
 *   binary, ordinal, categorical or continuous outcomes, and acquisition
 *   functions for threshold (level-set) estimation and for the whole
 *   psychometric field. The subset of AEPsych (Owen et al. 2021; Letham
 *   et al. 2022; Keeley et al. 2023) that fits in one C file with no
 *   dependencies: Laplace inference instead of variational, a candidate
 *   set instead of a gradient optimizer, the semiparametric model as a
 *   kernel, and the look-ahead acquisitions through a rank-one update.
 *
 *   Written in the single-header style of the stb / sokol libraries. Pure
 *   computation: no OS calls, no threads. One allocation at open, none
 *   after. Needs nothing but libm. Not a transport header, so it does not
 *   include psy_rt.h, unless PSYGP_ASYNC asks for the optional thread that
 *   runs the inference off the frame loop; see ASYNC.
 *
 *   Targets every platform the compiler does. C++17, C11, or the pre-C11 C
 *   dialect MSVC compiles with by default.
 *
 *   ---------------------------------------------------------------------
 *   CHANGELOG
 *   ---------------------------------------------------------------------
 *   v0.2.1 - version macros (PSYGP_VERSION_MAJOR / MINOR / PATCH / STRING)
 *          and psygp_version(), so a program can log which header it was
 *          built from and a binding can read its version from the header;
 *          and desc.refine_steps, a golden-section refinement of the grid
 *          winner off the candidate set (see CANDIDATES). refine_steps is the
 *          LAST field of psygp_desc, so every earlier field keeps its offset,
 *          but the struct is larger and a binding that mirrors it must add the
 *          field. At its default of 0 nothing computes differently from v0.2.
 *   v0.2 - the optional async layer, PSYGP_ASYNC: psy_rt.h's psyrt_pump with
 *          psygp_update(), psygp_next() and psygp_threshold() on one thread, a
 *          queue of responses, a published snapshot a frame loop polls, and
 *          desc.fit_in_idle, which spends the gaps between trials on
 *          psygp_fit_step(). Nothing changed in the synchronous header, which
 *          still includes nothing but the C standard library and is bit for bit
 *          what v0.1 computed; see ASYNC.
 *   v0.1 - the implementation. All four likelihoods, both kernels, all six
 *          acquisitions, the analytic marginal-likelihood gradient (numerical
 *          under CATEGORICAL), the threshold search, and five things v0.0 did
 *          not plan: psygp_fit_step(), which makes a hyperparameter fit
 *          resumable one evaluation at a time, and desc.refit_every with
 *          psygp_refit(), which trades the exact Newton refit of every trial
 *          for an O(N^2) rank-one update, and desc.guess / desc.lapse, a
 *          floor and a ceiling on the Bernoulli and ordinal links so a
 *          two-interval task states its chance level instead of faking it.
 *          PSYGP_REAL makes the kernel matrix, the Laplace factors and the
 *          look-ahead covariance float on request; see PRECISION for what that
 *          buys, which is memory rather than time.
 *          It also fits a MAP rather than a type-II maximum likelihood: weak
 *          log-normal priors on the lengthscales and the output scales
 *          (desc.no_hyper_prior turns them off), which is what stops a nearly
 *          separable run from interpolating its own trials; psygp_next()
 *          guards against an acquisition that has stopped discriminating;
 *          and psygp_predict_p_many() / psygp_predict_f_many() evaluate a
 *          field in one blocked pass. Three changes to what
 *          v0.0 specified: EAVC measures the level set with the EXPECTED
 *          count of candidates above the level rather than the count of
 *          candidates whose mean is above it (the count is integer-valued,
 *          and it pins the acquisition to one stimulus as soon as no
 *          candidate can flip); the semiparametric kernel measures the
 *          intensity from the middle of the box; and the hyperparameter
 *          gradient under CATEGORICAL is central differences, not analytic.
 *   v0.0 - specification. Declarations and the manual, no implementation.
 *
 *   STATUS: v0.2.1. Implemented and checked by tests/adapt/psy_gp_test.c,
 *   which builds and passes as C11, C99 and C++17 under gcc with -Wall
 *   -Wextra -Wpedantic -Wshadow -Werror, with and without PSYGP_ASYNC, as
 *   MSVC's default C dialect with /W4 /WX both ways, and is clean under
 *   -fsanitize=address,undefined and, with the async layer, under
 *   -fsanitize=thread.
 *   What it verifies, with the numbers it measured: the Cholesky and the
 *   triangular solves against a hand-built SPD matrix (1e-12); the kernel
 *   symmetric and positive definite on random points for both kernels; the
 *   20-node Gauss-Hermite rule integrating x^0 to x^6 against the Gaussian
 *   to 1e-11; the exact Gaussian posterior, its variance and its log
 *   marginal likelihood against a dense Gauss-Jordan solve (1e-9); the
 *   Bernoulli probit Laplace mode satisfying f = m + K grad log p(y|f) at
 *   six points (1e-8) and its predictive variance against a dense
 *   (K^-1 + W)^-1 (1e-9); the softmax mode satisfying the same condition
 *   per class with three classes (1e-7); the analytic gradient of the
 *   Laplace log
 *   marginal likelihood against central differences for every free
 *   hyperparameter under RBF and SEMIP crossed with Bernoulli, ordinal and
 *   Gaussian, and again with the logit link, worst relative error below
 *   1e-4; the rank-one look-ahead
 *   covariance against a brute-force refit with W frozen (1e-10);
 *   psygp_memory_size() exact to the byte; the Halton points; the desc
 *   validation; a caller-owned candidate list, the random acquisition and
 *   the threshold-width stop rule running a session; psygp_predict_p_many()
 *   and psygp_predict_f_many() agreeing with the one-point calls to the last
 *   bit over a 900-point grid; and a reported
 *   Cholesky failure leaving the previous
 *   posterior in force. On a simulated 1-D probit observer (slope 8,
 *   50% point 0.40, box [0, 1]) over 8 response streams of 100 trials, the
 *   mean absolute error of the 75% threshold is 0.011 for LSE, 0.010 for
 *   EAVC and 0.037 for BALD, against a binomial noise floor near 0.02 for
 *   a hundred trials; the tolerance the test enforces is 0.08. Over 20
 *   further streams outside the test the same three come out at 0.013,
 *   0.013 and 0.032, worst case 0.05, 0.04 and 0.12, with no run failing to
 *   find a crossing. The other
 *   three likelihoods recover their simulated field: rms error 0.003 for
 *   GAUSSIAN over 60 trials, 0.076 for the ordinal P(y >= 2) with four
 *   categories over 150 trials, and 0.069 for the three softmax class
 *   probabilities over 120 trials.
 *
 *   The floor and the ceiling are checked the same way: the analytic
 *   gradient still matches central differences to better than 1e-4 with
 *   guess = 0.5 and lapse = 0.02 under both links, both kernels, and both
 *   Bernoulli and ordinal; and a simulated two-interval observer
 *   (p = 0.5 + 0.48 Phi(8 (x - 0.4))) recovers its 75% threshold with a mean
 *   absolute error of 0.022 over 5 streams of 120 trials.
 *
 *   The weak priors of HYPERPARAMETERS are in the same comparison: without
 *   them the same eight streams give 0.050 for LSE and 0.043 for BALD, and
 *   two of the eight collapse onto a diffuse fit whose threshold is 0.14
 *   out. The gradient check covers them, since they are part of the
 *   objective the finite differences see, and desc.no_hyper_prior is how the
 *   comparison was run.
 *
 *   examples/gp_audiometric.c is the harder test, and the numbers that justify
 *   the priors and the acquisition guard come from it: the audiometric observer
 *   of Owen et al. 2021, four phenotypes by 20 replications of 150 trials, same
 *   seeds throughout. Pooled over the phenotypes at trial 150, threshold error
 *   in dB at beta = 2 then beta = 0.5: LOCALMI 1.74 / 1.75, LSE 2.04 / 2.28,
 *   EAVC 2.14 / 2.03, BALV 3.76 / 3.40, BALD 5.66 / 5.28, RANDOM 5.42 / 5.45,
 *   SEMIP with BALV 3.41 / 3.46, SEMIP with LSE 8.05 / 8.14, and eight
 *   interleaved staircases given the frequencies and the psychometric width
 *   2.72 / 2.07. Surface error MAE(p): BALV 0.0558 / 0.0621, which is the best
 *   of the nine in all eight phenotype-by-beta cells, then EAVC 0.0668, BALD
 *   0.0672, LOCALMI 0.0733, RANDOM 0.0763, LSE 0.0824; the staircase reads
 *   0.0186 there, with beta known for free. So the level-set acquisitions win
 *   the threshold and the variance-based ones win the field, which is the
 *   paper's own finding, and a classical staircase that is told the frequencies
 *   and the width is still competitive on both.
 *
 *   That run is also the before-and-after for the two v0.1 fixes. The weak
 *   priors: LSE was 3.20 / 3.07 dB without them and BALD 8.48 / 8.91, against
 *   2.04 / 2.28 and 5.66 / 5.28 with them, and the output scale sat at a bound
 *   in 130 of 1600 runs rather than 1580, all 130 in the BALV, LOCALMI or
 *   beta = 0.5 cells where the latent really is steep. They cost early
 *   accuracy: at trial 25, MAE(p) is 0.03 to 0.11 worse with the prior than
 *   without. The acquisition guard: SEMIP with a level-set acquisition used to
 *   separate its own data and then propose one candidate forever, and its
 *   threshold contour left the box in 7 to 19 of 30 context columns in every
 *   cell; now no cell of the 24 has a single non-crossing column. Over all 1600
 *   runs, PSYGP_ERR_NUMERIC 0 and a multi-crossing threshold 0. SEMIP with LSE
 *   at 8 dB is still the worst configuration measured, and the reason is in the
 *   model rather than the fit: see SEMIP below, and ACQUISITION for what the
 *   guard does and does not repair.
 *
 *   The two cost knobs are checked too: psygp_fit_step() run to completion
 *   lands on the same hyperparameters (1e-12) and the same log marginal
 *   likelihood (1e-9) as psygp_fit(), and desc.refit_every = 5 replayed against
 *   refit_every = 1 on one 78-trial response stream differs by at most
 *   1.7e-4 of probability, which psygp_refit() then erases to 6e-15.
 *
 *   The Laplace log marginal likelihood itself is checked against an
 *   independent dense evaluation of R&W eq. 3.32 (1e-8), which matters
 *   because it is the larger half of what the fit maximizes. The weak priors
 *   are the other half, and a fit that ran away in spite of them still shows
 *   up as a log marginal far closer to zero than the responses allow;
 *   HYPERPARAMETERS says how that looks and what else to do about it.
 *
 *   The async layer of v0.2 is checked by the same test, in a PSYGP_ASYNC
 *   block: a 60-trial session driven through psygp_async_submit() reproduces
 *   the synchronous session bit for bit (memcmp of the history and of the
 *   proposal stream, and the same log marginal likelihood and threshold to the
 *   last bit) with fit_in_idle off; with it on, the log marginal improved in an
 *   idle gap on 27 of 60 trials while the proposals kept up, and
 *   desc.fit_every was parked at 0 for the session and restored at stop; and a
 *   burst that outruns the thread gives PSYGP_ERR_BUSY with nothing copied
 *   (8 accepted, 3 refused, 8 still pending at a stop that drained them). That
 *   test is clean under -fsanitize=thread with no warnings, and the rest of the
 *   test's output is byte-identical between the PSYGP_ASYNC build and the plain
 *   one. examples/gp_async.c runs 200 EAVC trials through a 16 ms frame loop in
 *   11.2 s: with fit_in_idle on and a 225-candidate grid the proposal was ready
 *   by the next frame on 28 trials and late on 172, 2.4 frames of waiting on
 *   average and 5 at worst, the queue never filled, and the threshold at three
 *   contexts came out 0.007 to 0.060 from the truth.
 *
 *   desc.refine_steps is checked there too: the point score it climbs equals
 *   the candidate score to the last bit at every candidate for LSE, BALV,
 *   BALD and LOCALMI, including under CATEGORICAL; every refined proposal stays
 *   within one grid step of the winner it started from and never scores below
 *   it, under all five acquisitions; and the eight LSE streams above give a
 *   mean threshold error of 0.0111 with refine_steps = 2 against 0.0112
 *   without, which on a 65-point grid is what refinement should change: next
 *   to nothing. CANDIDATES has the audiometric numbers, where the grid is
 *   coarse and it matters.
 *
 *   The float build of PSYGP_REAL passes the same test with the tolerances
 *   PRECISION lists, measured rather than assumed, with PSYGP_LIK_GAUSSIAN
 *   rejected at open and its checks skipped.
 *
 *   NOT verified: nothing is compared against AEPsych yet, so the
 *   agreement claim of docs/psy_adapt.md is still a plan, and the Laplace
 *   band's 95% coverage is unmeasured. The categorical one-against-the-rest
 *   acquisitions have no Monte Carlo check. The timings come from one run on
 *   one idle x86-64 desktop and vary by tens of percent between runs of the
 *   same binary, so the frame-budget trial counts are an order of magnitude and
 *   not a specification (examples/gp_bench.c prints its own numbers;
 *   MEMORY, COST AND THREADS quotes them), and the async layer's late counts
 *   are a property of that machine and that grid, not of the header. No
 *   platform other than x86-64 has been built or run. The async layer has run
 *   on Linux (pthreads) and on Windows (MSVC, native threads, and the OS
 *   granted the below-normal drop); psy_rt.h's macOS path is untested here.
 *
 *   ---------------------------------------------------------------------
 *   USAGE
 *   ---------------------------------------------------------------------
 *   Do this:
 *
 *       #define PSY_GP_IMPLEMENTATION
 *
 *   in *one* C or C++ file before including this header to create the
 *   implementation. Every other file just includes the header normally.
 *
 *   A detection threshold as a function of one context variable (say,
 *   contrast threshold across spatial frequency), estimated at 75%:
 *
 *       #define PSY_GP_IMPLEMENTATION
 *       #include "psy_gp.h"
 *
 *       psygp_desc d = {0};
 *       d.n_dims        = 2;
 *       d.lo[0] = 0.0;  d.hi[0] = 1.5;      // log10 spatial frequency
 *       d.lo[1] = -3.0; d.hi[1] = 0.0;      // log10 contrast: intensity
 *       d.intensity_dim = 1;
 *       d.kernel        = PSYGP_KERNEL_SEMIP;  // linear in contrast
 *       d.acq           = PSYGP_ACQ_EAVC;      // look-ahead level set
 *       d.target_p      = 0.75;
 *       d.n_init        = 10;                // Halton trials first
 *       d.fit           = true;              // fit the hyperparameters
 *       d.fit_every     = 10;
 *       d.stop_trials   = 150;
 *
 *       psygp_gp g;
 *       if (!psygp_open(&g, &d)) { fputs(psygp_error(&g), stderr); return 1; }
 *
 *       while (!psygp_done(&g)) {
 *           double x[2];
 *           psygp_next(&g, x);                        // fills x[0], x[1]
 *           int seen = run_trial(x);                  // 1 = yes, 0 = no
 *           psygp_update(&g, x, seen);
 *       }
 *       double ctx[1] = { 0.7 }, thr, lo, hi;
 *       psygp_threshold(&g, ctx, 0.75, &thr, &lo, &hi); // contrast at 0.7
 *       psygp_close(&g);
 *
 *   The same with a four-point confidence rating instead of yes/no is
 *   d.lik = PSYGP_LIK_ORDINAL, d.n_outcomes = 4, d.target_outcome = 2
 *   ("rated 2 or above"), and psygp_update() gets the rating. A "which of
 *   three intervals" task is PSYGP_LIK_CATEGORICAL with n_outcomes = 3;
 *   a matching task with a continuous setting is PSYGP_LIK_GAUSSIAN and
 *   psygp_update_real().
 *
 *   ---------------------------------------------------------------------
 *   MODEL
 *   ---------------------------------------------------------------------
 *   A latent field f ~ GP(mean, kernel) over a box [lo, hi] of n_dims
 *   dimensions, observed through one of four likelihoods (desc.lik):
 *
 *     PSYGP_LIK_BERNOULLI   p(y = 1 | f) = link(f), or with a floor and a
 *                           ceiling desc.guess + (1 - guess - lapse)
 *                           link(f). Detection, 2AFC,
 *                           yes/no. K = 2. AEPsych's default, and this
 *                           header's.
 *     PSYGP_LIK_ORDINAL     K ordered categories: a confidence rating, a
 *                           Likert scale, "less / same / more".
 *                           p(y = k | f) = link(c_k - f) - link(c_{k-1} - f)
 *                           with K - 1 increasing cutpoints c_1..c_{K-1},
 *                           c_0 = -inf, c_K = +inf. One latent. The
 *                           cutpoints are hyperparameters, fixed in
 *                           desc.hyper.cutpoint or fit; c_1 is pinned to
 *                           0 when fitting, else the mean and the
 *                           cutpoints trade off. AEPsych's
 *                           OrdinalLikelihood.
 *     PSYGP_LIK_CATEGORICAL K unordered categories: which of K intervals,
 *                           which of K named percepts, which direction.
 *                           K latent GPs sharing one kernel and
 *                           p(y = k | f) = softmax_k(f_1..f_K) (Rasmussen
 *                           & Williams 2006 sec. 3.5). Costs K times the
 *                           Bernoulli Laplace. Not in AEPsych.
 *     PSYGP_LIK_GAUSSIAN    a continuous y: a matching setting, a
 *                           magnitude estimate, a log reaction time.
 *                           y = f + noise, noise sd a hyperparameter.
 *                           Exact Gaussian posterior, no Newton. AEPsych's
 *                           GaussianLikelihood.
 *
 *   The link (desc.link) is the probit by default, the logit on request;
 *   it applies to BERNOULLI and ORDINAL.
 *
 *   FLOOR AND CEILING (desc.guess, desc.lapse)
 *     A two-interval task cannot score below 0.5 and no observer is right
 *     every time, so the built-in links take a floor and a ceiling:
 *
 *       p(y = 1 | f) = guess + (1 - guess - lapse) link(f)
 *
 *     under BERNOULLI, and under ORDINAL the same squeeze on every
 *     cumulative probability P(y >= k), which moves `lapse` of the mass into
 *     the bottom category and `guess` into the top and leaves each category
 *     probability a difference of two squeezed terms. Both default to 0,
 *     both are FIXED (never fitted, they are task structure and not model
 *     slack), and psygp_open() rejects a nonzero value under CATEGORICAL or
 *     GAUSSIAN. Set desc.guess = 0.5 for a two-interval task and the model
 *     no longer has to bend its prior mean to fake the chance level; a lapse
 *     of 0.02 to 0.05 is the usual allowance for attention.
 *     Every level-set quantity follows: the target of the LSE, EAVC and
 *     LOCALMI family and of psygp_threshold() is this same p, so the latent
 *     level is f* = link^-1((target_p - guess) / (1 - guess - lapse)), and
 *     target_p has to lie inside (guess, 1 - lapse) or open() refuses it.
 *     Two costs. First, the log likelihood is no longer log-concave in f
 *     once either is nonzero, so the Laplace posterior is a weaker
 *     approximation: the Newton step clamps a negative Hessian entry to zero
 *     (the treatment R&W give the non-log-concave case) and the line search
 *     guards the step, but the posterior variance at a site the clamp
 *     touched is the prior's, and the approximation degrades as the lapse
 *     grows. Second, a two-interval trial carries less information than a
 *     yes/no trial, and an unregularized hyperparameter fit on a handful of
 *     them can settle on "the whole field is at chance", which is
 *     self-confirming: hyper.mean walks to its lower bound, the output scale
 *     to its lower, the log marginal likelihood sits at N log(guess), and a
 *     level-set acquisition has no gradient left to follow. The remedies are
 *     a longer init phase and a later first fit (n_init 16 and fit_every 20
 *     rather than 8 and 10 for a hundred-trial run), or fixing hyper.mean so
 *     the fit cannot go there.
 *
 *   The header keeps a Gaussian
 *   approximation to the posterior of f given the trials so far, and the
 *   acquisition function picks the next stimulus from a candidate set to
 *   shrink the uncertainty that matters for the question asked. This is
 *   AEPsych's model with two substitutions:
 *
 *     - Inference is the LAPLACE approximation (Williams & Barber 1998;
 *       Rasmussen & Williams 2006 ch. 3): Newton's method to the mode of
 *       the latent posterior, a Gaussian around it. AEPsych uses
 *       variational inference through GPyTorch. Laplace is one Cholesky
 *       per Newton step on an N x N matrix, N being the trial count, and
 *       needs no optimizer. For a probit likelihood on a few hundred
 *       binary trials the two agree to within the noise of the trials
 *       themselves; docs/psy_adapt.md has the comparison plan and the
 *       caveats (Laplace underestimates the variance where the posterior
 *       is skewed, i.e. early and at the edges).
 *     - The next stimulus is chosen from a CANDIDATE SET, a grid or a
 *       Halton set over the box, not by optimizing the acquisition with
 *       gradients. Every candidate is scored, the best wins; a grid of a
 *       few hundred to a few thousand points is what AEPsych's optimizer
 *       starts from anyway.
 *
 *   KERNELS (desc.kernel)
 *     PSYGP_KERNEL_RBF    squared exponential with one lengthscale per
 *                         dimension (ARD) and an output scale. AEPsych's
 *                         default. Smooth, no assumption of monotonicity.
 *                         k(x, x') = outputscale *
 *                                    exp(-1/2 sum_d ((x_d - x'_d) / l_d)^2),
 *                         so the output scale is a VARIANCE: the prior sd of
 *                         the latent is its square root.
 *     PSYGP_KERNEL_SEMIP  the semiparametric model (Keeley et al. 2023):
 *                         f(c, x) = a(c) + b(c) * x, where x is the
 *                         intensity dimension (desc.intensity_dim) measured
 *                         from the MIDDLE of the box, c the
 *                         other dimensions, and a and b are independent
 *                         RBF GPs over c. Every slice along the intensity
 *                         is a straight line in latent space, i.e. a
 *                         cumulative Gaussian (probit) or logistic (logit)
 *                         psychometric function whose location and slope
 *                         vary smoothly with context. The kernel is
 *                         k_a(c, c') + x x' k_b(c, c'), so it is the same
 *                         Laplace machinery with a different covariance.
 *                         The x of that product is the intensity minus the
 *                         middle of the box, because the slope term's prior
 *                         variance is x^2 k_b(c, c) and the point where the
 *                         slope stops contributing has to be inside the
 *                         stimulus range, not wherever zero falls in the
 *                         caller's units. lengthscale[intensity_dim] is
 *                         unused here, and with n_dims = 1 there is no
 *                         context at all: the model is then plain probit or
 *                         logistic regression with a Gaussian prior on the
 *                         intercept and the slope.
 *                         THE SCALE OF THE INTERCEPT is the trap. a(c) has to
 *                         carry (box middle - threshold) / width of the
 *                         psychometric rise in latent units, so an observer
 *                         whose rise is narrow against the box needs a huge
 *                         intercept: on the audiometric observer of
 *                         examples/gp_audiometric.c, 140 dB of intensity and a
 *                         probit width of 0.5 dB put a(c) over tens of latent
 *                         units and its output scale over a thousand, where
 *                         the default ceiling is 10 and the weak prior of
 *                         HYPERPARAMETERS is centered on 1. Measured there
 *                         with the level-set acquisition, 20 replications of
 *                         150 trials: 8.97 dB of threshold error with the
 *                         prior, 7.07 dB without it, 6.10 dB without it and
 *                         with the two output-scale ceilings raised to 4000
 *                         and 100, against 1.98 dB for the plain RBF kernel on
 *                         the same observer. So the semiparametric kernel is
 *                         the wrong model for that combination, not a badly
 *                         fitted one, and the way to use it on a steep
 *                         observer is to scale the intensity axis so the
 *                         psychometric rise is of order 1 (which puts the
 *                         intercept in range), or to set hyper_max and
 *                         desc.no_hyper_prior for the scale the problem needs.
 *                         Keeley's slope is constrained positive; this
 *                         kernel's is not, and a run with too few trials at
 *                         one context can put a negative slope there. The
 *                         HYPERPARAMETERS and ESTIMATES sections say how
 *                         to see it. A positive-slope variant is in the
 *                         plan. Under CATEGORICAL each latent gets its own
 *                         a and b.
 *     Monotonicity along the intensity dimension is not enforced by either
 *     kernel (AEPsych's monotonic projection is not here). The threshold
 *     search treats the posterior mean as monotonic and reports where it
 *     is not.
 *
 *   HYPERPARAMETERS (desc.hyper, desc.fit)
 *     lengthscale[d], outputscale and mean for RBF; a second lengthscale
 *     set and outputscale for the slope GP under SEMIP; cutpoint[k] under
 *     ORDINAL; noise_sd under GAUSSIAN. A NONZERO field is fixed at that
 *     value. A zero field takes a default and is the set psygp_fit() fits:
 *     lengthscale (hi - lo) / 4, outputscale 1, mean 0, cutpoints 0, 1, 2,
 *     ... (c_1 is pinned to 0 while they are fitted, so the mean stays
 *     identifiable), noise_sd 0.1. Two consequences of "zero means fit":
 *     the mean cannot be pinned to exactly 0 while it is being fitted (set
 *     it to 1e-12, or leave desc.fit off), and setting cutpoint[0] fixes
 *     the whole cutpoint vector. A fixed mean is how the prior is centered
 *     somewhere other than p = 0.5: link^-1(p0), e.g. -1.28 for p0 = 0.1 in
 *     a yes/no task with rare false alarms.
 *     Fitting maximizes the log marginal likelihood plus the weak priors
 *     below: the Laplace
 *     approximation to it (R&W eq. 3.32) for the discrete likelihoods,
 *     the exact one (R&W eq. 2.30) for GAUSSIAN. The GRADIENT is analytic
 *     (R&W eqs. 5.22 to 5.24 for Laplace, including the implicit term
 *     through the mode; eq. 5.9 for Gaussian), so a fit is gradient ascent
 *     with a backtracking line search, bounded by [hyper_min, hyper_max],
 *     from the values in force. Lengthscales, output scales, cutpoint gaps
 *     and the noise are fitted as logarithms, the mean as itself. Bounds
 *     default to 0.05 (hi - lo) to 2 (hi - lo) for a lengthscale, 0.1 to 10
 *     for an output scale, -5 to +5 for the mean, 0.01 to 10 for a cutpoint
 *     gap and 1e-3 to 10 for the noise sd; a zero bound field means the
 *     default, so a bound of exactly zero cannot be asked for.
 *     One objective-plus-gradient evaluation is one Laplace refit plus
 *     O(N^2) per hyperparameter, and a fit stops after 40 of them or when
 *     no uphill step is left. The step is monotone: a fit never leaves the
 *     model with a lower marginal likelihood than it started with.
 *     CATEGORICAL is the exception: its gradient is central differences on
 *     the same objective (2 P + 1 refits per gradient), because the
 *     implicit term of eq. 5.23 needs third derivatives of a coupled block
 *     Hessian. It is correct but slow, and it is the one part of the fit
 *     that is not analytic.
     *
 *     WEAK PRIORS (desc.no_hyper_prior). AEPsych puts gamma priors on the
 *     lengthscales and optimizes the variational ELBO with autodiff. This is
 *     the same gradient by hand, with priors of the same purpose:
 *     log-normal on every lengthscale, centered on the default (hi - lo) / 4
 *     with an sd of 1 in logarithms, and log-normal on the output scales,
 *     centered at 1 with an sd of 0.7. Nothing on the mean or the cutpoints.
 *     They are what stops the fit from interpolating its own trials. Type-II
 *     maximum likelihood on a run whose responses nearly separate keeps
 *     improving as the output scale grows and the lengthscale shrinks, so it
 *     ends at whatever bound it hits; the posterior is then diffuse or
 *     interpolating, and a level-set acquisition follows the variance to the
 *     edges of the box instead of the threshold. The likelihood is flat near
 *     that bound, so a prior this weak moves the answer a long way there and
 *     hardly at all once the data have something to say.
 *     What it bought, measured by examples/gp_audiometric.c on the metabolic
 *     phenotype at beta = 2 over 20 replications of 150 trials with the
 *     level-set acquisition (the same seeds, priors off then on): the
 *     threshold error at trial 150 fell from 3.14 +- 0.30 dB to
 *     1.98 +- 0.32 dB and the surface error from 0.115 to 0.077, the output
 *     scale stopped sitting on its upper bound (20 runs out of 20 before,
 *     none after), and the Laplace log marginal likelihood went from -20.6 to
 *     -58.7 nats, where 150 binary trials at a 75% level cannot honestly beat
 *     about -84. The early surface error is the one thing that got worse
 *     (0.153 to 0.256 at trial 25): a conservative fit starts further from
 *     the truth and ends closer. On the 1-D observer of the STATUS block the
 *     priors cut the mean absolute threshold error of LSE from 0.050 to 0.011
 *     and of BALD from 0.043 to 0.037 over eight streams.
 *     desc.no_hyper_prior turns them off and fits type-II maximum likelihood,
 *     which is what the numbers above call the "before".
 *     The fit therefore maximizes a posterior, not a likelihood.
 *     psygp_log_marginal() still reports the likelihood alone, and the
 *     symptom of a fit that ran away anyway is a log marginal much closer
 *     to zero than the responses allow: a hundred binary trials at a 75%
 *     level cannot be explained better than about -56 nats, so a fit
 *     reporting -5 has interpolated, and its threshold band will be wide and
 *     its threshold estimate unreliable. What is left to do about that is to
 *     raise hyper_min.lengthscale, lower hyper_max.outputscale, or fix the
 *     mean.
 *     A lengthscale at its UPPER bound, on the other hand, is usually not a
 *     complaint: a field that really is flat along a dimension wants an
 *     infinite lengthscale there, and the bound is the only thing stopping
 *     it. On the audiometric observer's flat phenotype the frequency
 *     lengthscale sat on its ceiling in 15 to 20 runs out of 20 with no cost
 *     to either metric. Read it as "this dimension carries no structure",
 *     and only worry when the field plainly does vary along it.
 *     Fits run every desc.fit_every trials once the init phase is over and
 *     on psygp_fit(). Log psygp_get_hyper() with your data: a lengthscale
 *     that hits its bound is a warning, a slope output scale that hits
 *     its upper bound under SEMIP is the negative-slope case above.
 *     A whole fit is one call and tens of refits, which is more than an
 *     inter-trial interval holds. psygp_fit_step() is the same ascent one
 *     evaluation at a time: it returns 1 while another step would help, 0
 *     when it is done, and leaves the best hyperparameters found so far in
 *     force every time it returns, so the caller can spend one step per
 *     trial and read estimates in between. Recording a trial throws the
 *     stepping state away, since the data it was fitting have changed; a
 *     caller that wants to interleave fitting with trials should run the
 *     steps to 0 in the gaps of one trial, or accept that a long fit
 *     restarts. psygp_fit() is a loop over psygp_fit_step().
 *
 *   ACQUISITION (desc.acq)
 *     The first desc.n_init trials are a Halton sequence over the box
 *     (AEPsych's Sobol phase; Halton because it is ten lines and
 *     deterministic). After that the candidates are scored and the best
 *     wins. The level-set family (LSE, EAVC, LOCALMI) needs a TARGET, and
 *     psygp_open() rejects a desc without one: for BERNOULLI the level
 *     P(y = 1) = target_p; for ORDINAL P(y >= target_outcome) = target_p;
 *     for CATEGORICAL P(y = target_outcome) = target_p; for GAUSSIAN
 *     E[y] = target_value. f* below is the latent value that meets it, the
 *     level of the latent field itself and not of its posterior average.
 *     Note that PSYGP_ACQ_LSE is the default acq, so a zeroed desc needs a
 *     target_p even when the level set is not what is wanted; BALV, BALD
 *     and RANDOM need none.
 *
 *     PSYGP_ACQ_LSE      level-set straddle (Bryan et al. 2005; Gotovos
 *                        et al. 2013; AEPsych "LevelSetEstimation"):
 *                        beta * sd_f(x) - |mean_f(x) - f*|. Cheap, myopic,
 *                        and drawn to the edges of the box. beta =
 *                        desc.acq_beta, default 1.96 (AEPsych's 3.84 is on
 *                        the variance and is the same number).
 *     PSYGP_ACQ_EAVC     expected absolute volume change of the level set
 *                        (Letham et al. 2022; AEPsych "EAVC"): the
 *                        candidate whose outcome is expected to move the
 *                        most of the box across the target. The one
 *                        AEPsych's authors recommend for thresholds, and
 *                        this header's recommendation. Needs the LOOK-AHEAD
 *                        below. The volume is sum over candidates of
 *                        P(f(x) > f*), the expected NUMBER of candidates in
 *                        the level set, so the score is continuous in every
 *                        candidate's mean and variance; the score of a
 *                        candidate is E_y |volume - volume after y|.
 *     PSYGP_ACQ_LOCALMI  mutual information between the outcome at x and
 *                        the level-set membership of x, the Bernoulli
 *                        1{f(x) > f*} (Letham et al. 2022; AEPsych
 *                        "LocalMI"). Look-ahead, but only at the candidate
 *                        itself, so it needs no M x M covariance and costs
 *                        one quadrature per candidate.
 *     PSYGP_ACQ_BALV     posterior variance of the target probability, or
 *                        of E[y] under GAUSSIAN (AEPsych
 *                        "MCPosteriorVariance"): global estimation of the
 *                        whole field.
 *     PSYGP_ACQ_BALD     mutual information between the outcome and f
 *                        (Houlsby et al. 2011; AEPsych
 *                        "BernoulliMCMutualInformation"): global, and less
 *                        drawn to the edges than BALV. Defined for every
 *                        likelihood.
 *     PSYGP_ACQ_RANDOM   Halton for every trial, or draws from desc.rng
 *                        when it is set. A baseline for a simulation, or a
 *                        space-filling schedule you want reproducible. The
 *                        point is not a candidate (psygp_next() returns -1);
 *                        under psygp_next_subset() it is the subset
 *                        candidate nearest the drawn point.
 *
 *     Expectations over one latent are Gauss-Hermite quadrature with
 *     PSYGP_QUAD_N (20) nodes. Under CATEGORICAL the latents are coupled
 *     by the softmax, and BALV, BALD and the look-aheads score each class
 *     latent one-against-the-rest (its mean minus the log-sum-exp of the
 *     other class means, through a logistic link) and sum; that is an
 *     approximation, and the manual says so here so the comparison against
 *     a Monte Carlo score (which desc.rng makes possible, see
 *     docs/psy_adapt.md) is not a surprise. The look-ahead covariance under
 *     CATEGORICAL drops the between-class term of R&W eq. 3.41 as well.
 *     Ties break toward the lowest candidate index, unless desc.rng is set,
 *     in which case a tie is a draw. Two scores tie when they are within
 *     1e-12 of the best, relative.
 *     TWO DEGENERACIES, and the guard against them. A posterior so diffuse
 *     that every candidate scores alike, and a posterior so confident that
 *     one candidate wins forever, both stop a run from learning: the first
 *     hands every trial to the lowest index, the second to the same index.
 *     psygp_next() therefore falls back to a Halton point (returning -1, as
 *     in the init phase) when the best score is no better than the average of
 *     the scores by more than 1e-9 relative, or when the same candidate has
 *     won PSYGP__REPEAT_MAX (3) trials in a row. psygp_next_subset() does
 *     not do this, because a Halton point is not in the subset. Measured on
 *     the semiparametric kernel with a level-set acquisition on the
 *     audiometric observer, where the second degeneracy was reliable: without
 *     the guard the threshold error froze at 23.4 dB, identical across all 20
 *     seeds, and the contour left the box in 14 of 30 context columns; with it,
 *     and with the priors above, the error falls to 8.1 dB pooled over the four
 *     phenotypes and no context column fails to cross.
 *
 *     What the guard does NOT do, in that one configuration: restore the run's
 *     dependence on the responses. Instrumented, SEMIP with LSE on the
 *     audiometric observer trips the repeat rule on every fourth trial for the
 *     whole session (145 trials, 35 fallbacks, and the flat test never fires),
 *     the acquisition returns to the same candidate immediately afterwards, and
 *     the candidate scores are bit-identical between two runs with different
 *     seeds. So the guard is a rate limiter there, worth 15 dB, and the run is
 *     still driven by the design rather than by the observer: the fit puts the
 *     intercept output scale at its ceiling, the latent variance swamps the
 *     responses, and the level-set score becomes a function of the candidate
 *     set alone. 8 dB of threshold error across 20 seeds takes only three
 *     distinct values for that reason. A fallback that proposes the best
 *     candidate NOT yet visited instead of a Halton point was measured and is
 *     not the answer: on one phenotype at beta = 2 it takes SEMIP with LSE from
 *     13.9 to 9.8 dB, but it takes SEMIP with BALV from 2.7 to 5.3 dB and RBF
 *     with LSE from 1.7 to 2.3, because a fallback that never repeats a
 *     candidate spends trials away from the threshold in the runs that were
 *     working. The repair for the SEMIP cell is in the model, not the
 *     acquisition: see SEMIP.
 *
 *   LOOK-AHEAD
 *     EAVC needs, for every candidate x*, the posterior at every other
 *     candidate AFTER one more observation at x*. With the
 *     Laplace Hessian W held at its current value, that posterior is the
 *     current one minus a rank-one term (the Woodbury identity on one new
 *     row), so the look-ahead latent variance at all M candidates for
 *     every one of the M candidates comes from the M x M posterior
 *     covariance among the candidates: O(M^2 N) to build it once per
 *     trial and O(M^2) per acquisition. The new observation enters the mean
 *     through one Newton step at the frozen W, and the outcome enters
 *     through its expectation, one quadrature per candidate. This is Letham
 *     et al.'s construction; the approximation is that W does not move on
 *     one trial, which is exactly the assumption the warm-started Newton
 *     step relies on anyway. Memory: the M x M covariance and the M x N
 *     solved kernel block it is built from, 4 MB together at M = N = 512.
 *     Only EAVC takes them; LOCALMI uses the variance at the candidate
 *     itself, which the ordinary prediction already gives.
 *     M is capped at 4096 under EAVC and 65536 otherwise.
 *     Two things make the M^2 scoring loop affordable. The level-set
 *     membership of a candidate is one normal cdf per candidate and outcome,
 *     and the loop uses a rational approximation of it (Abramowitz & Stegun
 *     26.2.17, 1e-7 absolute) rather than erfc, which costs 30 to 150 ns
 *     depending on the library and was most of a trial's time; predictions and
 *     thresholds are unaffected and still use erfc. And the sum runs over the
 *     CHANGE from the current volume, which lets it skip a candidate the new
 *     observation cannot move across the level. Together they took the scoring
 *     loop at M = 256 from 6 to 16 ms down to 3.5 to 3.9 ms, and a whole EAVC
 *     psygp_next() from 15 to 9 ms at N = 100 and from 101 to 28 ms at
 *     N = 400, measured as CPU time on the machine of the FRAME BUDGET table.
 *     What is left is the algorithm rather than the implementation: the M x M
 *     covariance costs M^2 N / 2 and the prediction M N^2 / 2, so halving M
 *     is what halves the first and quarters nothing else.
 *
 *   CANDIDATES (desc.candidates, desc.grid, desc.n_candidates,
 *   desc.refine_steps)
 *     desc.candidates: an explicit M x n_dims array the caller owns and
 *     must keep alive for the life of the handle, for the stimuli the
 *     display can actually produce. Else desc.grid[d]
 *     points per dimension for a product grid, last dimension fastest,
 *     endpoints included (every grid[d] must be at least 1 once one of them
 *     is set). Else a Halton set of
 *     desc.n_candidates points (default 512). psygp_next_subset()
 *     restricts one trial to some of the candidates. The candidate set is
 *     also the set the look-ahead volume is measured on and the spacing
 *     psygp_threshold() scans with, so keep it fine enough along the
 *     intensity dimension.
 *     Finer is better, up to the cost. A coarse candidate set does act as a
 *     regularizer on a fit that has no prior, and on three replications of the
 *     audiometric observer it looked like a large one; on 20 replications with
 *     the weak priors of HYPERPARAMETERS in place the effect reverses, and the
 *     finer set wins: 1.46 +- 0.26 dB of threshold error at trial 150 on a
 *     15 x 25 grid against 1.98 +- 0.32 dB on 11 x 21, same seeds, same
 *     observer. Treat the candidate count as the cost knob it is, not as a
 *     way to regularize the model.
 *
 *     desc.refine_steps (default 0, off; at most 32) takes the grid winner off
 *     the grid. A proposal on a candidate set is quantized to it, and on the
 *     audiometric grid of 11 x 21 that is 7 dB along the intensity axis, where
 *     AEPsych optimizes the acquisition continuously. With refine_steps = n,
 *     psygp_next() runs n rounds of coordinate-wise golden-section search
 *     (10 iterations per dimension per round) on the acquisition, each
 *     dimension searched between the winner's two neighbors, one candidate
 *     spacing either side, clipped to the box; for a Halton or caller-owned
 *     set the spacing is what M points would have on a regular grid. The
 *     bracket stays on the winner's cell for every round, a round that moves
 *     nothing ends the search, and a move is taken only if it scores strictly
 *     higher, so the result never scores below the candidate it started from.
 *     A proposal that moved returns -1, like a Halton point.
 *     The objective is the acquisition at one point, which takes one
 *     prediction: exact for LSE, BALV, BALD and LOCALMI, whose candidate
 *     scores are that same arithmetic (the test checks the two agree to the
 *     last bit). EAVC is a sum over the OTHER candidates and has no value off
 *     the candidate set, so an EAVC winner is refined on the LSE straddle,
 *     which aims at the same level set: the look-ahead picks the cell and the
 *     straddle places the point inside it. RANDOM and psygp_next_subset() never
 *     refine. The cost is at most 1 + 12 n D predictions, 49 for n = 2 in two
 *     dimensions, each a kernel row and a triangular solve, which at M = 231
 *     is a fifth of the candidate sweep and inside the run-to-run noise of a
 *     whole trial.
 *     Measured with examples/gp_audiometric.c's protocol (metabolic phenotype,
 *     beta = 2, 20 replications, same seeds, trial 150):
 *
 *       refine_steps               0              2              4
 *       LSE   threshold, dB   1.98 +- 0.32   1.50 +- 0.13   1.53 +- 0.12
 *             field MAE(p)    0.077          0.089          0.085
 *             ms per trial    1.8            2.0            1.6
 *       EAVC  threshold, dB   1.98 +- 0.16   1.37 +- 0.15   1.38 +- 0.15
 *             field MAE(p)    0.058          0.073          0.073
 *             ms per trial    4.3            4.2            4.1
 *
 *     So refinement buys a quarter to a third of the threshold error and costs
 *     the field about as much: points placed exactly on the level set say more
 *     about the level set and less about everything else, and the early
 *     threshold is worse too (trial 25: 6.8 to 8.6 dB for LSE, 7.4 to 10.0 for
 *     EAVC). 4 rounds buy nothing over 2, because the search has almost always
 *     converged by then. The default stays 0, which keeps every earlier result
 *     reproducible; set 2 for a session whose deliverable is the threshold, and
 *     leave it 0 when it is the field.
 *
 *   THE LOOP
 *     psygp_next(g, x)             fills x[n_dims] with the next stimulus
 *     psygp_update(g, x, k)        what was shown, and the outcome; refits
 *     psygp_update_real(g, x, y)   the same under PSYGP_LIK_GAUSSIAN
 *     psygp_done(g)                a stop criterion has fired
 *     psygp_threshold(...)         level set along the intensity dimension
 *     psygp_predict_*(...)         the field and its uncertainty anywhere
 *     psygp_fit_step(g)            one step of a hyperparameter fit
 *     psygp_refit(g)               the exact refit desc.refit_every skips
 *
 *   next() and update() are separate, as in the other adaptive headers:
 *   show what you can, tell update() what you showed, at any point of the
 *   box, candidate or not. A proposal that has not been answered is handed
 *   out again by the next psygp_next(), and psygp_history() marks a trial
 *   `proposed` only when the stimulus given to update() is exactly the one
 *   proposed, so a display that quantized it leaves a record.
 *
 *   ESTIMATES
 *     psygp_predict_f(g, x, k, &mu, &sd)  latent k's mean and sd at x
 *                                         (k = 0 except CATEGORICAL)
 *     psygp_predict_p(g, x)               the target quantity at x:
 *                                         E[P(y = 1)], E[P(y >= k*)] or
 *                                         E[P(y = k*)]; E[y] under
 *                                         GAUSSIAN. The probit Bernoulli
 *                                         case is closed-form,
 *                                         Phi(mu / sqrt(1 + sd^2)); the
 *                                         rest are quadrature.
 *     psygp_predict_p_many(g, xs, n, p)   the target quantity at n points, and
 *     psygp_predict_f_many(g, xs, n, k, mu, sd)
 *                                         latent k at n points. One blocked
 *                                         pass rather than n passes, which is
 *                                         what a field plot per trial needs.
 *                                         The saving is cache, not work: a
 *                                         point costs a kernel row and a
 *                                         triangular solve either way, so at
 *                                         N = 150 a 900-point field measured
 *                                         22 us per point one at a time and
 *                                         11 us per point batched, and the
 *                                         gap grows with N as the factor
 *                                         stops fitting in cache. A field on
 *                                         the CANDIDATE set is cheaper still,
 *                                         because the acquisition has already
 *                                         computed it.
 *     psygp_predict_outcomes(g, x, p)     all K outcome probabilities.
 *                                         Under CATEGORICAL these are K
 *                                         one-against-the-rest quadratures
 *                                         renormalized to sum to 1, not the
 *                                         exact K-dimensional softmax
 *                                         expectation.
 *     psygp_threshold(g, ctx, target, &x, &lo, &hi)
 *       Bisection along the intensity dimension at context ctx (the other
 *       n_dims - 1 coordinates, in order) for the level where
 *       psygp_predict_p() crosses target (0 = the desc's target). lo and hi
 *       are the crossings of the mu - 1.96 sd and mu + 1.96 sd latent
 *       curves, sorted, so a falling curve gives the same band as a rising
 *       one: a credible band from the latent posterior, not from posterior
 *       samples of the threshold. It is a 95% interval for the threshold
 *       only as far as the latent curve is straight over its width, and it
 *       is as wide as the latent posterior at the crossing divided by the
 *       slope there, so a loose fit shows up as a wide band.
 *       A band edge with no crossing inside the
 *       box is clamped to the box. Returns PSYGP_ERR_NOCROSS when the
 *       middle curve has no crossing inside the box, and
 *       psygp_threshold_multi_cross() reports when there was more than one
 *       (the non-monotone case), in which case the crossing at the lowest
 *       intensity is the one reported. The search scans the dimension
 *       before it bisects, at the candidate spacing (the grid count along
 *       that dimension, or M^(1/n_dims) clamped to 9..65 points), which is
 *       what lets it see a second crossing rather than bisect it away.
 *     psygp_get_hyper(g, out) and psygp_log_marginal(g) for the fit.
 *
 *   STOPPING (desc.stop_trials, desc.stop_threshold_sd)
 *     stop_trials ends after that many trials. stop_threshold_sd ends when
 *     the width (hi - lo) of psygp_threshold() at desc.stop_context is
 *     below it; with n_dims = 1 the context is empty. At least one is
 *     required, and psygp_open() rejects a desc with neither.
 *     PSYGP_MAX_TRIALS (or desc.max_trials) is the hard ceiling.
 *     psygp_done() runs a threshold search every call while
 *     stop_threshold_sd is on, so call it once per trial, not in a poll.
 *
 *   ---------------------------------------------------------------------
 *   RETURN VALUES AND ERRORS
 *   ---------------------------------------------------------------------
 *   psygp_open() returns bool and fills psygp_error(). Every other function
 *   returns a value or a code and never touches the message buffer.
 *   psygp_strerror() names a code.
 *
 *     PSYGP_ERR_ARG        null handle, x outside the box, outcome out of
 *                          range, update() under GAUSSIAN or
 *                          update_real() under a discrete likelihood
 *     PSYGP_ERR_CLOSED     the handle is not open
 *     PSYGP_ERR_FULL       max_trials recorded
 *     PSYGP_ERR_MEMORY     buffer too small or allocation failed (open)
 *     PSYGP_ERR_NOCROSS    no threshold inside the box at that context
 *     PSYGP_ERR_NUMERIC    a Cholesky failed; the trial is still recorded
 *                          and the posterior of the trials that did factor
 *                          stays in force, so predictions keep working from
 *                          one trial less. The next update tries again.
 *                          Happens with duplicate
 *                          stimuli at extreme outcomes and an output scale
 *                          fit too large; see desc.jitter.
 *
 *   psygp_next() is the exception to the code scheme: it returns a
 *   candidate index of 0 or more, -1 for a point that is not a candidate,
 *   and a code below -1 for a failure. PSYGP_ERR_ARG is -1 and therefore
 *   unusable there, so a null handle, a null x, a closed handle and a bad
 *   subset all come back as PSYGP_ERR_CLOSED, and a full handle as
 *   PSYGP_ERR_FULL.
 *
 *   ---------------------------------------------------------------------
 *   MEMORY, COST AND THREADS
 *   ---------------------------------------------------------------------
 *   psygp_memory_size(desc) returns the bytes open() takes from
 *   desc.memory or from PSYGP_MALLOC (malloc by default; define
 *   PSYGP_MALLOC/PSYGP_FREE before the include), exactly once. With N =
 *   max_trials, M candidates and K latents (1 except CATEGORICAL):
 *   N*N*8 for the kernel matrix, K*N*N*8 for the Laplace factor, another
 *   N*N*8 of workspace for the hyperparameter gradient (taken whether or
 *   not desc.fit is on, because psygp_fit() is callable at any time), one
 *   more N*N*8 under CATEGORICAL for the coupling factor, the M x M
 *   look-ahead covariance and its M x N staging block under EAVC,
 *   a handful of N*8 vectors, M*n_dims*8 for a generated candidate set,
 *   and the history. Measured: Bernoulli at N = 512, M = 512 is 6.44 MB
 *   with LSE and 10.44 MB with EAVC; N = 200, M = 512 with LSE is 1.11 MB;
 *   a 3-class categorical at N = M = 512 with LSE is 12.54 MB. The handle
 *   itself is 42 KB at the default ceilings, nearly all of it the 512-trial
 *   history. Nothing allocates after open.
 *
 *   Per update: a Laplace refit warm-started from the last mode, typically
 *   2 to 4 Newton steps, each an N x N Cholesky (N^3 / 3 flops) plus two
 *   triangular solves; under CATEGORICAL a step is K such factorizations
 *   plus K triangular inversions and one more Cholesky for the coupling
 *   term, so about 2.5 K times the Bernoulli cost; none under
 *   GAUSSIAN, where the kernel Cholesky grows by one row per trial in
 *   O(N^2). A hyperparameter fit
 *   is up to 40 refits with gradients and is the expensive call: run it
 *   in the inter-trial interval one psygp_fit_step() at a time, or set
 *   fit_every to 0 and call psygp_fit() when you choose.
 *
 *   desc.refit_every buys the update down to O(N^2). The exact Laplace
 *   factor cannot grow by a row, because the Hessian W changes in every
 *   entry with every trial; but if W is FROZEN at the values the last exact
 *   refit left, then adding a trial borders B with one row, its Cholesky
 *   gains one row by a triangular solve and a square root, and one
 *   quasi-Newton step moves the mode: five matrix-vector products in place
 *   of a factorization. That is the same rank-one construction the
 *   look-ahead acquisitions already use, applied to the training set.
 *   refit_every = k does that on every trial and an exact Newton refit
 *   every k trials; k larger than the session does no exact refit at all
 *   inside the loop, leaving psygp_refit() to the caller between blocks.
 *   The cost is drift: on a replayed 78-trial run, three cheap updates
 *   after the last exact refit put the posterior 1.7e-4 of probability
 *   away from the exact one, and psygp_refit() erases it. BERNOULLI and
 *   ORDINAL only; GAUSSIAN is already incremental and CATEGORICAL always
 *   refits. Past this, a preconditioned conjugate
 *   gradient solve using the previous factor, which makes an EXACT Newton
 *   step O(N^2) times a handful of iterations, is planned for a later version
 *   and is written up in docs/psy_adapt.md.
 *
 *   Per next: M candidates, each a kernel row (N) and a triangular solve
 *   (N^2 / 2), so M*N^2/2 flops: 256 candidates at N = 200 is 5 M, at
 *   N = 400 20 M; EAVC adds M^2 N / 2 once and M^2 per candidate and
 *   outcome. Predictions are computed in blocks of 32 candidates so the
 *   factor is read once per block rather than once per candidate.
 *
 *   FRAME BUDGET
 *   A trial's psygp_next() plus psygp_update() should fit in one display
 *   frame, 16 ms, so an experiment can call them between trials with no
 *   thread and no pacing. examples/gp_bench.c measures exactly that and
 *   prints the largest trial count that stays inside it. On one x86-64
 *   desktop core under WSL2, gcc -O2, Bernoulli probit RBF over a 2-D box
 *   with M = 256 candidates, next + update, median of a ten-trial window, in
 *   ms, from one run on an otherwise idle machine:
 *
 *     N     LSE      LSE no      EAVC     EAVC no
 *           exact    refit       exact    refit
 *     50     0.55     0.30        4.8      6.9
 *     100    1.6      0.81        7.3      7.7
 *     200    6.7      3.2        16.3     13.2
 *     400   43.8     14.4        49.0     25.7
 *
 *   and the largest N whose next + update still fits the frame:
 *
 *     LSE, exact refit every trial      311
 *     LSE, no refit inside the loop     past 400
 *     EAVC, exact refit every trial     216
 *     EAVC, no refit inside the loop    265
 *
 *   A hyperparameter fit does not fit a frame at any useful N, which is what
 *   psygp_fit_step() is for: on the same machine at N = 400 one step is 43 to
 *   74 ms and a whole fit is 40 steps, 1.8 to 2.3 s, so a fit spread one step
 *   to a trial costs a session 40 trials of latency instead of one trial of
 *   two seconds.
 *
 *   ("no refit" is refit_every past the session length.) Read those as an
 *   order of magnitude, not as a specification: the run-to-run spread is tens
 *   of percent even quiet, the largest-N figures moved between 180 and 380 over
 *   runs of the same binary on a loaded machine, and anything else running
 *   inflates all of them. Measure your own configuration with gp_bench.
 *
 *   What to do when
 *   a configuration does not fit: fewer candidates (next is linear in M,
 *   and EAVC is quadratic), a cheaper acquisition (LSE and LOCALMI need no
 *   M x M covariance), desc.refit_every with psygp_refit() between blocks,
 *   psygp_fit_step() instead of psygp_fit() so the fit is spread over
 *   trials, a lower max_trials, or another thread: PSYGP_ASYNC is that thread,
 *   written and tested, and ASYNC says what it does and does not buy. A handle
 *   is not thread-safe, and nothing in the header is for the frame loop
 *   itself: these calls belong between trials.
 *
 *   Every function is deterministic unless desc.rng is set: the same desc
 *   and the same history give the same posterior, the same proposals and
 *   the same fit. A saved history replayed through psygp_update()
 *   restores a run, except that the fit schedule is by trial count, so
 *   replay with the same fit_every.
 *
 *   ---------------------------------------------------------------------
 *   SIMULATION
 *   ---------------------------------------------------------------------
 *   psygp_simulate_outcome(p, K, u) draws an outcome from K probabilities
 *   with the uniform variate u in [0, 1) that the caller supplies; K = 2
 *   with p = {1 - p1, p1} is the Bernoulli case. The header draws nothing
 *   itself; desc.rng is the caller's generator lent to the header for the
 *   two places randomness helps (ties, random acquisition). examples/gp_sim.c
 *   runs the LSE, EAVC and BALV configurations against a 2-D observer with
 *   a known threshold curve and prints the error of psygp_threshold() per
 *   trial; that is the harness the comparison with AEPsych will run on, through
 *   the Python binding, in one process, on one response stream.
 *   tests/adapt/psy_gp_test.c is the self-checking test the STATUS block
 *   quotes; examples/gp_bench.c is where the cost numbers come from.
 *
 *   ---------------------------------------------------------------------
 *   PRECISION (PSYGP_REAL)
 *   ---------------------------------------------------------------------
 *   The matrices that dominate the cost and the memory hold psygp_real,
 *   which is double unless PSYGP_REAL says otherwise: the kernel matrix, the
 *   Laplace factors, the hyperparameter gradient's workspace and the
 *   look-ahead covariance. Every vector, every accumulation and every scalar
 *   is double in both builds, the dot products accumulate in double, and the
 *   Newton tolerance, the smallest accepted fit step and the default jitter
 *   move with the working precision (1e-11, 1e-9 and 1e-6 in double;
 *   1e-5, 1e-5 and 1e-4 in float; a Newton step in float cannot resolve a
 *   mode better than about 1e-5 of the latent, and asking for more only runs
 *   the iteration to its cap).
 *
 *   -DPSYGP_REAL=float halves those matrices: 10.44 MB becomes 5.44 MB for
 *   Bernoulli at N = M = 512 with EAVC. What it costs, measured by
 *   tests/adapt/psy_gp_test.c, which prints the worst error it saw in either
 *   build (double, then float): Cholesky and triangular inverse 6e-17 and
 *   5e-7, the Laplace stationarity residual 3e-16 and 9e-5, the predictive
 *   variance against a dense (K^-1 + W)^-1 9e-16 and 2e-5, the Laplace log
 *   marginal likelihood 0 and 1e-5, the rank-one look-ahead covariance 2e-12
 *   and 9e-6, the analytic hyperparameter gradient against central
 *   differences 9e-5 and 2.9e-2 relative (and the difference step has to grow
 *   from 1e-5 to 3e-3, because a float objective is only good to about 1e-7).
 *   psygp_refit() restores the exact posterior to 6e-15 in double and 6e-8 in
 *   float.
 *   What it does not cost: the answers an experiment reads. Over 20 response
 *   streams of a 100-trial simulated observer the mean absolute threshold
 *   error is 0.0130 against 0.0134 for LSE, 0.0128 against 0.0139 for EAVC
 *   and 0.0323 against 0.0429 for BALD, and neither build reported a single
 *   PSYGP_ERR_NUMERIC in 6000 updates.
 *
 *   What float does NOT buy is time. The same 6000 updates took 42.3 s in
 *   double and 41.0 s in float, three percent; and per phase, measured as CPU
 *   time at M = 256 (candidate prediction, covariance build, scoring loop and
 *   one Laplace refit, ms), double against float:
 *
 *     N = 50    0.5 2.4 3.4 0.04   against   0.6 3.1 3.8 0.05
 *     N = 100   0.9 2.6 2.6 0.14   against   1.6 7.4 8.1 0.29
 *     N = 200   4.0 7.3 3.5 1.02   against   4.8 10.3 3.0 1.48
 *     N = 400  19.3 32.9 3.8 9.0   against  19.1 34.4 3.7 14.5
 *
 *   which is a wash or slightly worse. The reason is that every loop here is
 *   scalar at -O2, so a float multiply costs exactly what a double multiply
 *   costs, and the matrices still reach the same cache level. Float would pay
 *   where the loops vectorize or where the working set crosses a cache
 *   boundary; measure it with examples/gp_bench.c before believing in it.
 *
 *   PSYGP_LIK_GAUSSIAN is not available in a float build and psygp_open()
 *   says so: its exact posterior factors the raw kernel matrix rather than
 *   I + W^1/2 K W^1/2, and that matrix is too ill-conditioned for float.
 *   Double remains the default on those numbers.
 *
 *   ---------------------------------------------------------------------
 *   ASYNC (PSYGP_ASYNC)
 *   ---------------------------------------------------------------------
 *   Everything above runs on the thread that calls it, and FRAME BUDGET says
 *   what that costs: some configurations fit a 16 ms frame and the interesting
 *   ones do not. A look-ahead acquisition at a few hundred trials is tens of
 *   milliseconds and a hyperparameter fit is hundreds, so the knobs there
 *   (fewer candidates, a cheaper acquisition, refit_every, fit_step) all trade
 *   something away to buy the frame back. The other answer is to not do the
 *   work on the frame loop's thread at all. Define PSYGP_ASYNC before the
 *   include and this header gains one:
 *
 *       #define PSYGP_ASYNC
 *       #define PSY_GP_IMPLEMENTATION
 *       #include "psy_gp.h"             // brings psy_rt.h with it
 *
 *       psygp_gp g;  psygp_async a;  psygp_async_desc ad = {0};
 *       psygp_open(&g, &desc);          // as usual, on this thread
 *       ad.gp          = &g;
 *       ad.fit_in_idle = true;          // the fit lives in the gaps
 *       ad.below_normal = true;         // never above the frame loop
 *       ad.context[0]  = 0.5;           // where the snapshot's threshold is
 *       if (!psygp_async_start(&a, &ad)) die(psygp_async_error(&a));
 *
 *       psygp_snapshot s;
 *       psygp_async_poll(&a, &s);       // the first proposal, already there
 *       for (;;) {
 *           int k   = run_trial(s.x);             // your frames
 *           int seq = psygp_async_submit(&a, s.x, k);
 *           while (drawing_frames())              // the interval
 *               if (psygp_async_poll(&a, &s) >= seq) break;
 *           if (s.done) break;
 *       }
 *       psygp_async_stop(&a);           // drains; the handle is yours again
 *
 *   WHAT IT IS. psy_rt.h's psyrt_pump with psygp_update(), psygp_next() and
 *   psygp_threshold() in its on_msg: one thread, a queue of PSYGP_ASYNC_QUEUE
 *   responses (8 by default) inline in the handle, no heap, normal or
 *   below-normal priority and never above it. Read psy_rt.h's PUMP section for
 *   the queue, the seq and the lock; this layer is a few hundred lines on top
 *   of it and adds no concurrency of its own. psy_quest.h's PSYQ_ASYNC is the
 *   same layer over QUEST+ and reads the same.
 *
 *   THE FIT IN THE GAPS is what the GP has that QUEST+ does not.
 *   desc.fit_in_idle puts one psygp_fit_step() in the pump's on_idle, which
 *   runs whenever the response queue is empty, and takes over from
 *   desc.fit_every, which the layer parks at 0 for the duration and restores at
 *   stop(). The effect is that the hyperparameters keep improving between
 *   trials and cost the trial loop nothing, and that a response never waits
 *   behind a whole fit: on_idle returns after one evaluation, the queue is
 *   checked, and the response goes first. A snapshot published from an idle
 *   step carries the same seq as the last response with new hypers, a new log
 *   marginal and `fitting` still true; it does not move the proposal, which
 *   belongs to the responses. Leave fit_in_idle off and desc.fit_every is
 *   honored as usual, on the pump thread, which is then one slow trial in
 *   every fit_every rather than a spread cost.
 *
 *   OWNERSHIP, which is the rule to get right. The caller opens the gp handle
 *   and psygp_async_start() takes it over: from then until psygp_async_stop()
 *   returns, NO psygp_* call on that handle is allowed from any thread,
 *   including the const ones (psygp_predict_p() writes the handle's prediction
 *   scratch, so even a read of the posterior is a write). Everything a trial
 *   loop needs is in the snapshot instead: the proposal, the trial count, the
 *   stop flags, the threshold with its band, the hyperparameters, the log
 *   marginal likelihood and how many updates have returned PSYGP_ERR_NUMERIC.
 *   After stop the handle is the caller's again and every psygp_* call is
 *   legal, which is how a session ends: stop, then read the history, the
 *   posterior, the field and the estimates at leisure.
 *
 *   THE SEQ. psygp_async_submit() returns a positive, increasing seq for the
 *   response it copied. psygp_async_poll() returns the seq the snapshot
 *   accounts for: every response through it has been applied AND the proposal
 *   in the snapshot was computed after them. So `poll() >= seq` is the "is the
 *   next stimulus ready?" test, it is one atomic load and a struct copy, and a
 *   frame loop can afford it every frame. psygp_async_wait() is the blocking
 *   form with a relative timeout, for the end of an interval.
 *
 *   Seq 0 is the proposal psygp_async_start() computed before the thread
 *   existed, so a snapshot always exists once start() has returned true and the
 *   first trial reads its stimulus from a poll that returns 0. That is why
 *   there is no "nothing published yet" state to handle.
 *
 *   A FULL QUEUE is PSYGP_ERR_BUSY, nothing was copied, and the caller still
 *   owns the response: retry it on the next frame, which examples/gp_async.c
 *   does. A queue that grew instead would trade a visible error for an
 *   invisible unbounded latency. With the default queue of 8 it takes eight
 *   trials submitted faster than the thread drains to get there.
 *
 *   WHAT IT DOES NOT DO. It does not make a slow configuration fast, it moves
 *   it: a proposal that takes four frames still takes four frames, and what the
 *   loop gains is that it is drawing during them instead of blocked. The cost
 *   of being late is one more frame of the same stimulus, which an experiment
 *   has to be written for; examples/gp_async.c counts how often it happened.
 *   It does not touch determinism either: the thread runs the same functions in
 *   submit order, so the same responses give the same posterior bit for bit
 *   whether they went through the queue or not, and
 *   tests/adapt/psy_gp_test.c checks exactly that with memcmp. (With
 *   fit_in_idle on the fit sees a different schedule, so the posterior is a
 *   different one, reproducible only for the same timing. Bit-for-bit replay of
 *   an async session means replaying the history through psygp_update() with
 *   fit_in_idle off.)
 *
 *   COST. The handle is 6160 bytes on x86-64 at the default ceilings: mostly
 *   psyrt_pump's inline ring, which this layer does not use
 *   (PSYRT_PUMP_INLINE_BYTES can be set to 1 if nothing else in the program
 *   needs it), plus 80 bytes a queued response and a 360-byte snapshot.
 *   A submit is a mutex, an 80-byte copy and a condition-variable signal. A
 *   poll is an atomic load and a 360-byte copy under a lock nothing else holds
 *   for long. Nothing allocates, at start or after.
 *
 *   ---------------------------------------------------------------------
 *   BUILDING
 *   ---------------------------------------------------------------------
 *   Nothing to link but libm, unless PSYGP_ASYNC is defined: then psy_gp.h
 *   includes psy_rt.h, which must sit beside it, and POSIX builds link
 *   -pthread. Copy both headers in that case, as a transport header's user
 *   does. PSY_GP_IMPLEMENTATION then also compiles psy_rt.h's implementation,
 *   unless the same translation unit already defined PSY_RT_IMPLEMENTATION or
 *   implemented a transport header; either order gives exactly one copy.
 *   PSYGP_ASYNC together with PSYRT_NO_THREADS is a #error: the layer IS a
 *   thread, so there is nothing sensible to compile.
 *
 *   Define PSYGP_API to override the default
 *   `extern` linkage (definitions too, so static works). Define
 *   PSYGP_MAX_DIMS (8), PSYGP_MAX_OUTCOMES (8), PSYGP_MAX_TRIALS (512),
 *   PSYGP_REAL (double) and PSYGP_ASYNC_QUEUE (8, the response queue, which
 *   sizes the psygp_async handle)
 *   before the include to resize the handle and the ceilings; define them the
 *   same in every translation unit. Every matrix
 *   is row-major and dense, every loop is scalar: there is no BLAS and no
 *   thread, so -O2 and a compiler that vectorizes a dot product are the
 *   whole performance story.
 *
 *       cc -O2 -I. -o gp_sim examples/gp_sim.c -lm
 *       cl /O2 /I. examples\gp_sim.c
 *       cc -O2 -pthread -I. -o gp_async examples/gp_async.c -lm
 *
 *   ---------------------------------------------------------------------
 *   LICENSE: public domain / MIT-0, see end of file.
 */
#ifndef PSY_GP_H_INCLUDED
#define PSY_GP_H_INCLUDED

/* The version of this header, for a log line or a compile-time check. The
 * string is the three numbers, and the test asserts that it stays so. */
#define PSYGP_VERSION_MAJOR  0
#define PSYGP_VERSION_MINOR  2
#define PSYGP_VERSION_PATCH  1
#define PSYGP_VERSION_STRING "0.2.1"

/* The one optional dependency, and it comes FIRST: psy_rt.h sets a
 * feature-test macro for the Linux clock calls and can only do that before the
 * first system header. Without PSYGP_ASYNC this header includes nothing but the
 * C standard library, which is what the rest of the manual assumes; with it,
 * psy_gp.h gains the async layer and psy_rt.h comes with it. See ASYNC and
 * BUILDING. */
#ifdef PSYGP_ASYNC
    #ifdef PSYRT_NO_THREADS
        #error "psy_gp.h: PSYGP_ASYNC is a thread, and PSYRT_NO_THREADS removes threads. Define one or the other, not both."
    #endif
    #include "psy_rt.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PSYGP_API
#define PSYGP_API extern
#endif

/* The element type of the matrices that dominate the cost: the kernel matrix,
 * the Laplace factors, the gradient workspace and the look-ahead covariance.
 * Define PSYGP_REAL to float before the include to halve their memory and the
 * traffic through it; every vector, every accumulation and every scalar stays
 * double either way. Define it the same in every translation unit, because it
 * changes the layout of psygp_gp. See PRECISION in the manual for what the
 * float build costs and what it does not support. */
#ifndef PSYGP_REAL
#define PSYGP_REAL double
#endif
typedef PSYGP_REAL psygp_real;

#ifndef PSYGP_MAX_DIMS
#define PSYGP_MAX_DIMS 8
#endif
#ifndef PSYGP_MAX_OUTCOMES
#define PSYGP_MAX_OUTCOMES 8
#endif
#ifndef PSYGP_MAX_TRIALS
#define PSYGP_MAX_TRIALS 512
#endif
#define PSYGP_QUAD_N 20   /* Gauss-Hermite nodes for expectations over f */

/* --- codes ------------------------------------------------------------- */

#define PSYGP_OK            0
#define PSYGP_ERR_ARG      (-1)
#define PSYGP_ERR_CLOSED   (-2)
#define PSYGP_ERR_FULL     (-3)
#define PSYGP_ERR_MEMORY   (-4)
#define PSYGP_ERR_NOCROSS  (-5)  /* no level-set crossing inside the box    */
#define PSYGP_ERR_NUMERIC  (-6)  /* Cholesky failed; posterior unchanged    */

/* Static description of a PSYGP_ERR_* code ("ok" for values >= 0). */
PSYGP_API const char* psygp_strerror(int code);

/* PSYGP_VERSION_STRING, as compiled into the implementation. Beside the macro,
 * which is what the CALLER was compiled against, it tells a program that links
 * a prebuilt implementation which one it got. */
PSYGP_API const char* psygp_version(void);

/* --- description ------------------------------------------------------- */

typedef enum psygp_lik {
    PSYGP_LIK_BERNOULLI = 0, /* y in {0, 1}, p = link(f) (default)          */
    PSYGP_LIK_ORDINAL,       /* y in 0..K-1 ordered, K - 1 cutpoints        */
    PSYGP_LIK_CATEGORICAL,   /* y in 0..K-1 unordered, K latents, softmax   */
    PSYGP_LIK_GAUSSIAN       /* y real, y = f + noise                       */
} psygp_lik;

typedef enum psygp_kernel {
    PSYGP_KERNEL_RBF = 0,  /* ARD squared exponential (default)            */
    PSYGP_KERNEL_SEMIP     /* a(c) + b(c) x along intensity_dim            */
} psygp_kernel;

typedef enum psygp_link {
    PSYGP_LINK_PROBIT = 0, /* p = Phi(f) (default)                          */
    PSYGP_LINK_LOGIT       /* p = 1 / (1 + exp(-f))                         */
} psygp_link;

typedef enum psygp_acq {
    PSYGP_ACQ_LSE = 0,     /* level-set straddle (default)                  */
    PSYGP_ACQ_EAVC,        /* look-ahead expected absolute volume change    */
    PSYGP_ACQ_LOCALMI,     /* look-ahead local mutual information           */
    PSYGP_ACQ_BALV,        /* posterior variance of the target quantity     */
    PSYGP_ACQ_BALD,        /* outcome / latent mutual information           */
    PSYGP_ACQ_RANDOM       /* Halton, or desc.rng, every trial              */
} psygp_acq;

typedef enum psygp_stop {
    PSYGP_STOP_NONE = 0,
    PSYGP_STOP_TRIALS,
    PSYGP_STOP_THRESHOLD_SD,
    PSYGP_STOP_FULL
} psygp_stop;

/* A uniform variate in [0, 1) from the caller's generator. Optional; see
 * SIMULATION for what the header does with it. */
typedef double (*psygp_rng_fn)(void* ctx);

/* Model hyperparameters. A zero field means "default, or fit when
 * desc.fit is on"; see HYPERPARAMETERS. The `_b` fields apply to the slope
 * GP under PSYGP_KERNEL_SEMIP only, and lengthscale[intensity_dim] is
 * ignored there. cutpoint[] is ORDINAL only (K - 1 entries, increasing);
 * noise_sd is GAUSSIAN only. */
typedef struct psygp_hyper {
    double lengthscale[PSYGP_MAX_DIMS];
    double outputscale;
    double mean;
    double lengthscale_b[PSYGP_MAX_DIMS];
    double outputscale_b;
    double cutpoint[PSYGP_MAX_OUTCOMES - 1];
    double noise_sd;
} psygp_hyper;

/* Run description. Zero-initialize it and set only what you need.
 * Required: n_dims in [1, PSYGP_MAX_DIMS], lo[d] < hi[d] for each, and at
 * least one stop criterion. ORDINAL and CATEGORICAL need n_outcomes >= 2
 * (>= 3 for CATEGORICAL). A level-set acquisition needs target_p (or
 * target_value under GAUSSIAN). */
typedef struct psygp_desc {
    int          n_dims;
    double       lo[PSYGP_MAX_DIMS];
    double       hi[PSYGP_MAX_DIMS];
    int          intensity_dim;   /* the dimension thresholds are along;
                                   * SEMIP's x. Default 0.                  */
    psygp_lik    lik;             /* BERNOULLI (0) by default               */
    int          n_outcomes;      /* K; ORDINAL / CATEGORICAL only          */
    psygp_kernel kernel;
    psygp_link   link;
    double       guess;           /* gamma: the floor on p, e.g. 0.5 for a
                                   * two-interval task. BERNOULLI and
                                   * ORDINAL only; fixed, never fit         */
    double       lapse;           /* lambda: the gap below 1                */
    psygp_hyper  hyper;           /* fixed values, or 0 = default / fit     */
    bool         fit;             /* fit the zero hyperparameters           */
    int          fit_every;       /* refit every this many trials after
                                   * n_init; 0 = only on psygp_fit()        */
    bool         no_hyper_prior;  /* drop the weak priors of HYPERPARAMETERS
                                   * and fit type-II maximum likelihood       */
    int          refit_every;     /* 0 or 1 = an exact Newton refit on every
                                   * update; k > 1 = an O(N^2) rank-one
                                   * update per trial and an exact refit
                                   * every k trials. BERNOULLI and ORDINAL
                                   * only; see MEMORY, COST AND THREADS     */
    psygp_hyper  hyper_min;       /* fit bounds; 0 = 0.05 (hi - lo) for
                                   * lengthscales, 0.1 for output scales,
                                   * -5 for the mean, 0.01 for a cutpoint
                                   * gap, 1e-3 for noise_sd                 */
    psygp_hyper  hyper_max;       /* 0 = 2 (hi - lo), 10, +5, 10, 10        */
    double       jitter;          /* added to the kernel diagonal for the
                                   * Cholesky; 0 = 1e-6                     */
    psygp_acq    acq;
    double       target_p;        /* level-set probability (discrete liks) */
    double       target_value;    /* level-set value (GAUSSIAN)             */
    int          target_outcome;  /* ORDINAL: y >= this; CATEGORICAL: y ==
                                   * this. Default 1.                       */
    double       acq_beta;        /* LSE straddle width; 0 = 1.96           */
    int          n_init;          /* Halton trials before the acquisition
                                   * runs; 0 = 2 * n_dims + 2               */
    psygp_rng_fn rng;             /* optional generator: ties, RANDOM acq   */
    void*        rng_ctx;
    const double* candidates;     /* M x n_dims, caller-owned, or NULL; it
                                   * must outlive the handle                */
    int          n_candidates;    /* M for candidates / Halton; 0 = 512     */
    int          grid[PSYGP_MAX_DIMS]; /* product grid instead; 0 = unused,
                                        * else every dimension needs >= 1   */
    int          stop_trials;     /* 0 = off                                */
    double       stop_threshold_sd; /* 0 = off                              */
    double       stop_context[PSYGP_MAX_DIMS]; /* context for the above     */
    int          max_trials;      /* 0 = PSYGP_MAX_TRIALS                   */
    void*        memory;          /* caller's buffer, or NULL for the
                                   * allocator                              */
    size_t       memory_size;
    int          refine_steps;    /* rounds of golden-section refinement of
                                   * the grid winner; 0 = off. See
                                   * CANDIDATES. Last in the struct so the
                                   * fields before it keep their offsets.   */
} psygp_desc;

/* --- handle ------------------------------------------------------------ */

typedef struct psygp_trial {
    double  x[PSYGP_MAX_DIMS];
    double  y;          /* the outcome index, or the value under GAUSSIAN   */
    uint8_t proposed;   /* 1 when x is exactly what psygp_next() proposed  */
    uint8_t init;       /* 1 when the trial fell in the init phase          */
} psygp_trial;

/* Handle. The caller allocates it and treats every field as opaque. Must
 * be zeroed or closed before psygp_open(). */
typedef struct psygp_gp {
    psygp_desc  desc;
    psygp_hyper hyper;         /* the values in force (fit or default)     */
    int         N;             /* trials so far                            */
    int         N_fit;         /* trials the factor in L covers: N, or less
                                * after PSYGP_ERR_NUMERIC                  */
    int         N_max;
    int         M;             /* candidates                               */
    int         K;             /* latents: 1, or n_outcomes if CATEGORICAL */
    void*       mem;
    size_t      mem_size;
    bool        mem_owned;
    double*     X;             /* N_max x n_dims                           */
    double*     y;             /* N_max                                    */
    psygp_real* Kmat;          /* N_max x N_max kernel matrix              */
    psygp_real* L;             /* K x N_max x N_max Laplace factors, or the
                                * growing kernel Cholesky under GAUSSIAN   */
    double*     f;             /* K x N_max latent mode                    */
    double*     W;             /* K x N_max Hessian diagonal               */
    double*     grad;          /* K x N_max d log p(y|f) / df at the mode  */
    double*     cand;          /* M x n_dims, generated candidates         */
    double*     cand_mu;       /* K x M latent means at the candidates     */
    psygp_real* cand_cov;      /* M x M latent covariance, look-ahead only */
    double*     scratch;       /* Newton, gradient and prediction space    */
    double      log_marginal;
    int         halton_index;
    int         proposed;      /* candidate index of the last next(), -1  */
    int         last_index;    /* the candidate the last next() proposed   */
    int         repeats;       /* how many trials in a row it has won      */
    int         fit_state;     /* psygp_fit_step()'s place in a fit        */
    int         fit_evals;     /* objective evaluations spent in it        */
    int         fit_back;      /* consecutive line-search backtracks       */
    double      fit_step;      /* the line search's current step           */
    double      fit_best;      /* the best log marginal it has reached     */
    bool        fit_valid;     /* the Laplace state matches X, y, hyper    */
    bool        cand_valid;    /* cand_mu / cand_cov match the fit         */
    bool        multi_cross;   /* last threshold search crossed > once     */
    psygp_stop  stop;
    bool        open;
    psygp_trial history[PSYGP_MAX_TRIALS];
    char        error[256];
} psygp_gp;

/* --- lifecycle --------------------------------------------------------- */

/* Bytes psygp_open() needs for `desc`, or 0 when the desc is invalid. */
PSYGP_API size_t psygp_memory_size(const psygp_desc* desc);

/* Validate `desc`, take memory, generate the candidate set. Returns false
 * with psygp_error() set on failure. The only function that writes the
 * message buffer. */
PSYGP_API bool psygp_open(psygp_gp* g, const psygp_desc* desc);

/* Release what open() allocated. Safe on a zeroed or closed handle. */
PSYGP_API void psygp_close(psygp_gp* g);

PSYGP_API const char* psygp_error(const psygp_gp* g);
PSYGP_API bool        psygp_is_open(const psygp_gp* g);

/* --- the loop ---------------------------------------------------------- */

/* Fill x[n_dims] with the next stimulus: a Halton point during the init
 * phase, else the best candidate under desc.acq, refined off the candidate set
 * when desc.refine_steps asks for it. Returns the candidate index (>= 0), or
 * -1 for a Halton, rng or refined point, or a PSYGP_ERR_* below -1 (a bad
 * argument is reported as PSYGP_ERR_CLOSED, since -1 is taken).
 * Calling it twice without an update returns the same point. psygp_next_subset()
 * never refines, since the caller asked for one of the listed candidates. */
PSYGP_API int psygp_next(psygp_gp* g, double* x);

/* psygp_next() restricted to the `n` candidate indices in `subset`. Skips
 * the init phase. */
PSYGP_API int psygp_next_subset(psygp_gp* g, const int* subset, int n, double* x);

/* The acquisition score of candidate `index`, for a plot. The look-ahead
 * scores are defined on candidates only, which is why this takes an
 * index and not a point. NaN during the init phase or on a bad argument. */
PSYGP_API double psygp_acq_score(const psygp_gp* g, int index);

/* Record outcome `outcome` in 0..K-1 at stimulus x[n_dims], refit the
 * Laplace posterior, and refit hyperparameters when the schedule says so.
 * Returns 0, or a negative PSYGP_ERR_*; on PSYGP_ERR_NUMERIC the trial is
 * recorded and the previous posterior stays in force. PSYGP_ERR_ARG under
 * PSYGP_LIK_GAUSSIAN. */
PSYGP_API int psygp_update(psygp_gp* g, const double* x, int outcome);

/* The same for a continuous outcome under PSYGP_LIK_GAUSSIAN. */
PSYGP_API int psygp_update_real(psygp_gp* g, const double* x, double y);

PSYGP_API bool       psygp_done(const psygp_gp* g);
PSYGP_API psygp_stop psygp_stop_reason(const psygp_gp* g);

/* --- estimates --------------------------------------------------------- */

/* Latent `k`'s posterior mean and sd at x (k = 0 except CATEGORICAL).
 * Returns 0 or PSYGP_ERR_*. */
PSYGP_API int psygp_predict_f(const psygp_gp* g, const double* x, int k, double* mu, double* sd);

/* The target quantity at x under the posterior (see ESTIMATES); NaN on a
 * bad argument. */
PSYGP_API double psygp_predict_p(const psygp_gp* g, const double* x);

/* The same for `n` points at once, xs being n * n_dims coordinates and p an
 * n-element output. One blocked pass over the factor instead of n passes, which
 * is what makes a field plot per trial affordable: see ESTIMATES. Returns 0 or
 * PSYGP_ERR_*; psygp_predict_f_many() is the latent underneath, with k = 0
 * except under CATEGORICAL, and either output pointer may be NULL. */
PSYGP_API int psygp_predict_p_many(const psygp_gp* g, const double* xs, int n,
                                   double* p);
PSYGP_API int psygp_predict_f_many(const psygp_gp* g, const double* xs, int n,
                                   int k, double* mu, double* sd);

/* Posterior variance of the target quantity at x, by quadrature; the BALV
 * score. */
PSYGP_API double psygp_predict_p_var(const psygp_gp* g, const double* x);

/* All K outcome probabilities at x under the posterior (2 for BERNOULLI;
 * PSYGP_ERR_ARG under GAUSSIAN, where psygp_predict_f is the answer). */
PSYGP_API int psygp_predict_outcomes(const psygp_gp* g, const double* x, double* p);

/* Threshold along the intensity dimension at context ctx[n_dims - 1] (the
 * coordinates of every other dimension, in order; NULL when n_dims == 1).
 * `target` = 0 uses the desc's target. Writes the crossing of
 * psygp_predict_p() to *x, and the crossings of the +-1.96 sd latent
 * curves to *lo and *hi (either may be NULL). Returns 0,
 * PSYGP_ERR_NOCROSS, or another PSYGP_ERR_*. */
PSYGP_API int  psygp_threshold(const psygp_gp* g, const double* ctx, double target,
                               double* x, double* lo, double* hi);
PSYGP_API bool psygp_threshold_multi_cross(const psygp_gp* g);

/* --- hyperparameters --------------------------------------------------- */

/* One step of the fit above, for a caller that cannot give it a whole
 * inter-trial interval: at most two Laplace refits, one of them with the
 * gradient. Returns 1 while another step would help, 0 when the fit has
 * converged or run out of its evaluation budget, or a negative PSYGP_ERR_*.
 * The hyperparameters in force after a call are always the best found so far,
 * so estimates read between steps are consistent. psygp_update() discards the
 * state, since the data it was fitting have changed. */
PSYGP_API int psygp_fit_step(psygp_gp* g);

/* Force the exact Newton refit that desc.refit_every may be skipping. Does
 * nothing under GAUSSIAN or CATEGORICAL, which never skip one. Returns 0 or
 * PSYGP_ERR_*. */
PSYGP_API int psygp_refit(psygp_gp* g);

/* Fit the hyperparameters left zero in desc.hyper, from the values in force;
 * see HYPERPARAMETERS. Works whether or not desc.fit is on, and returns 0 with
 * nothing done when fewer than two trials are recorded or no field is free.
 * Returns 0 or PSYGP_ERR_*. Expensive; see MEMORY, COST AND THREADS. */
PSYGP_API int psygp_fit(psygp_gp* g);

/* The hyperparameters in force, and the log marginal likelihood of the
 * current fit (Laplace-approximate, or exact under GAUSSIAN). */
PSYGP_API int    psygp_get_hyper(const psygp_gp* g, psygp_hyper* out);
PSYGP_API double psygp_log_marginal(const psygp_gp* g);

/* --- candidates and history -------------------------------------------- */

PSYGP_API int psygp_n_candidates(const psygp_gp* g);
PSYGP_API int psygp_candidate(const psygp_gp* g, int index, double* x);

PSYGP_API int                psygp_n_trials(const psygp_gp* g);
PSYGP_API const psygp_trial* psygp_history(const psygp_gp* g, int* n);

/* --- simulation -------------------------------------------------------- */

/* Draw an outcome from the K probabilities p[0..K-1] with the uniform
 * variate u in [0, 1): the smallest k with cumulative p > u. */
PSYGP_API int psygp_simulate_outcome(const double* p, int K, double u);

/* -----------------------------------------------------------------------
 *  ASYNC
 *  Compiled only under PSYGP_ASYNC. See ASYNC in the manual.
 * ----------------------------------------------------------------------- */
#ifdef PSYGP_ASYNC

/* Two more codes, which only the async calls return. */
#define PSYGP_ERR_BUSY    (-7)  /* the queue is full; the caller keeps its
                                 * response and retries next frame          */
#define PSYGP_ERR_TIMEOUT (-8)  /* psygp_async_wait ran out of time          */

/* Responses the queue holds. One trial each; a caller that submits more than
 * this many before the thread drains gets PSYGP_ERR_BUSY. The ring lives
 * inside psygp_async, so this sizes the handle. */
#ifndef PSYGP_ASYNC_QUEUE
#define PSYGP_ASYNC_QUEUE 8
#endif

/* What the thread publishes after every update, and psygp_async_poll() and
 * psygp_async_wait() hand back. A plain struct, copied out under the pump's
 * lock; nothing in it points anywhere. */
typedef struct psygp_snapshot {
    uint32_t    seq;           /* the submit this accounts for: every update
                                * through this seq is applied and `x` was
                                * computed after them. 0 is the proposal made
                                * at psygp_async_start(), before any response */
    double      x[PSYGP_MAX_DIMS];  /* the next stimulus                     */
    int         proposed;      /* its candidate index, -1 for a Halton, rng
                                * or refined point, or a PSYGP_ERR_* below -1
                                * when psygp_next() refused                  */
    int         update_rc;     /* what psygp_update() returned for this
                                * response: 0, or the code that made it
                                * refuse. Not part of the seq contract; it is
                                * here so a refusal on the thread cannot pass
                                * unnoticed                                  */
    int         n_trials;
    bool        done;
    psygp_stop  stop;
    double      threshold;     /* psygp_threshold() at desc.context and
                                * desc.target, with its band               */
    double      threshold_lo;
    double      threshold_hi;
    int         threshold_rc;  /* 0, PSYGP_ERR_NOCROSS, or another code     */
    bool        multi_cross;   /* that search crossed more than once        */
    psygp_hyper hyper;         /* the values in force                       */
    double      log_marginal;
    int         numeric;       /* updates that returned PSYGP_ERR_NUMERIC so
                                * far in this session                       */
    bool        fitting;       /* desc.fit_in_idle and the fit has not
                                * converged yet, so the hyperparameters and
                                * the log marginal are still moving         */
} psygp_snapshot;

/* Async description. Zero-initialize it and set only what you need; `gp` is
 * required and must already be open. */
typedef struct psygp_async_desc {
    psygp_gp* gp;              /* the handle the thread drives. The layer
                                * OWNS it from start() to stop(): no psygp_*
                                * call on it meanwhile, from any thread.    */
    double    context[PSYGP_MAX_DIMS]; /* the n_dims - 1 coordinates the
                                * snapshot's threshold is taken at; unused
                                * when n_dims is 1                          */
    double    target;          /* the level for it; 0 = the desc's target   */
    bool      fit_in_idle;     /* run one psygp_fit_step() whenever the queue
                                * is empty. Takes over from desc.fit_every,
                                * which is then ignored; see ASYNC          */
    bool      below_normal;    /* run the thread below normal priority      */
    int       pin_cpu;         /* logical CPU to pin it to; 0 = no pinning,
                                * as psyrt_pump_desc.pin_cpu                */
} psygp_async_desc;

/* One trial's response on its way to the thread. Opaque; sized here because
 * the ring is inline. */
typedef struct psygp_async_msg {
    double x[PSYGP_MAX_DIMS];
    double y;                  /* the outcome index, or the value under
                                * PSYGP_LIK_GAUSSIAN                        */
    int    real;               /* 1 when it came from submit_real()          */
} psygp_async_msg;

/* Async handle. The caller allocates it and treats every field as opaque. It
 * owns no heap: the pump, the response ring and the snapshot are all inline.
 * Must be zeroed or stopped before psygp_async_start(). */
typedef struct psygp_async {
    psyrt_pump      pump;
    psygp_gp*       gp;
    psygp_async_msg ring[PSYGP_ASYNC_QUEUE];
    psygp_snapshot  snap;      /* guarded by psyrt_pump_lock()              */
    double          context[PSYGP_MAX_DIMS];
    double          target;
    int             fit_every; /* the caller's, parked while fit_in_idle     */
    int             numeric;
    bool            fit_in_idle;
    bool            fit_active; /* pump thread only: the fit has more to do  */
    bool            published;  /* a snapshot exists; under the pump's lock  */
    bool            running;
    char            error[256];
} psygp_async;

/* Start the thread and publish the first proposal. Returns true with a
 * snapshot already available (psygp_async_poll() returns 0 and fills it),
 * false with psygp_async_error() set: a null handle or desc, a gp that is not
 * open, a context outside the box, or a pump the OS would not start. The only
 * async call that writes the message, and the only one that must not race
 * another call on the same handle. Cost: one psygp_next() and one
 * psygp_threshold() on the calling thread, before the thread exists. */
PSYGP_API bool psygp_async_start(psygp_async* a, const psygp_async_desc* desc);

/* Deliver every response already queued, then join the thread. After it the gp
 * handle is the caller's again and every psygp_* call on it is legal. Safe on
 * a zeroed or already-stopped handle. Must not race another call on the same
 * handle, except a psygp_async_wait() already blocked, which comes out with
 * PSYGP_ERR_CLOSED. */
PSYGP_API void psygp_async_stop(psygp_async* a);

/* Hand one trial to the thread: what was shown and the outcome. Returns a
 * POSITIVE seq, increasing, which psygp_async_poll() and psygp_async_wait()
 * compare against. Negative on failure: PSYGP_ERR_ARG (null handle or x, x
 * outside the box, outcome out of range, the wrong submit for the likelihood),
 * PSYGP_ERR_CLOSED (not started), PSYGP_ERR_BUSY (the queue is full: NOTHING
 * was copied, the caller still owns the response and should retry it on the
 * next frame). Callable from any thread. Copies the response and returns; it
 * does not wait for the inference. */
PSYGP_API int psygp_async_submit(psygp_async* a, const double* x, int outcome);
PSYGP_API int psygp_async_submit_real(psygp_async* a, const double* x, double y);

/* Copy the newest snapshot into `out` (which may be NULL to ask only for the
 * seq) and return the seq it accounts for: every response through that seq is
 * applied and its proposal is the one in the snapshot. 0 means the proposal
 * made at start(), before any response had landed, so a caller that wants the
 * first stimulus reads it from a poll that returns 0. Negative on error. One
 * atomic load plus a struct copy under the pump's publish lock: cheap enough
 * for every frame. */
PSYGP_API int psygp_async_poll(const psygp_async* a, psygp_snapshot* out);

/* The same, after blocking until the thread has finished the response `seq` or
 * `timeout_ns` of monotonic time has passed. Returns the seq accounted for
 * (>= seq on success), PSYGP_ERR_TIMEOUT if the time ran out first,
 * PSYGP_ERR_CLOSED if the thread stopped without reaching it. timeout_ns is
 * relative; 0 polls. This is the end-of-interval call; poll() is the frame
 * loop's. */
PSYGP_API int psygp_async_wait(psygp_async* a, uint32_t seq, uint64_t timeout_ns,
                               psygp_snapshot* out);

/* Responses queued and not yet applied (the one being worked on is not
 * counted), or a negative PSYGP_ERR_*. For a log line, not a frame loop: it
 * takes the queue's mutex. */
PSYGP_API int psygp_async_pending(const psygp_async* a);

/* Last psygp_async_start() message for this handle ("" if none). */
PSYGP_API const char* psygp_async_error(const psygp_async* a);

/* True while the thread is running. */
PSYGP_API bool psygp_async_is_running(const psygp_async* a);

/* What the thread runs at: PSYRT_POLICY_NORMAL, or PSYRT_POLICY_BELOW_NORMAL
 * when desc.below_normal was set AND the OS granted the drop, which is not an
 * error either way. PSYRT_POLICY_NONE before a start and after a stop. Log it:
 * "the inference was ready in time" means something different at each rung. */
PSYGP_API psyrt_policy psygp_async_policy(const psygp_async* a);

#endif /* PSYGP_ASYNC */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PSY_GP_H_INCLUDED */

/* ======================================================================= *
 *                             IMPLEMENTATION                              *
 * ======================================================================= */
#ifdef PSY_GP_IMPLEMENTATION
#ifndef PSY_GP_IMPLEMENTATION_GUARD
#define PSY_GP_IMPLEMENTATION_GUARD

/* Under PSYGP_ASYNC, psy_rt.h's implementation too, unless this translation
 * unit already has it. Its implementation block sits outside its header guard
 * and carries a guard of its own, so a unit that also defines
 * PSY_RT_IMPLEMENTATION, or that also implements a transport header, still ends
 * up with exactly one copy. */
#ifdef PSYGP_ASYNC
    #ifndef PSY_RT_IMPLEMENTATION_GUARD
        #define PSY_RT_IMPLEMENTATION
        #include "psy_rt.h"
    #endif
#endif

#include <math.h>
#include <string.h>
#include <stdio.h>   /* snprintf, for psygp_open's one message buffer */
#include <stdarg.h>

#if !defined(PSYGP_MALLOC) || !defined(PSYGP_FREE)
    #include <stdlib.h>
#endif
#ifndef PSYGP_MALLOC
#define PSYGP_MALLOC(sz) malloc(sz)
#endif
#ifndef PSYGP_FREE
#define PSYGP_FREE(p) free(p)
#endif

/* Candidates are predicted in blocks, so one pass of the triangular solve
 * streams the Cholesky factor once for PSYGP__BLK right-hand sides instead of
 * once per candidate. That is the difference between a factor read M times and
 * a factor read M / 32 times. */
#define PSYGP__BLK 32

/* Ceiling on the free hyperparameters: a lengthscale per dimension for each of
 * the two GPs, a cutpoint per outcome, and the four scalars. */
#define PSYGP__NTHETA (2 * PSYGP_MAX_DIMS + PSYGP_MAX_OUTCOMES + 4)

#define PSYGP__SQRT2   1.4142135623730950488
#define PSYGP__SQRT2PI 2.5066282746310005024
#define PSYGP__LOG2PI  1.8378770664093454836
#define PSYGP__SQRTPI  1.7724538509055160273

#define PSYGP__NEWTON_MAX 50

/* Newton stops when its step is this small, relative. It has to be reachable
 * in the working precision: a float build resolves a latent to about 1e-7, so
 * asking for 1e-11 there would run every Newton to its iteration cap and every
 * line search into the noise. The same reason sets the smallest improvement a
 * hyperparameter step has to show. */
#define PSYGP__NEWTON_TOL (sizeof(psygp_real) == sizeof(double) ? 1e-11 : 1e-5)
#define PSYGP__FIT_TOL    (sizeof(psygp_real) == sizeof(double) ? 1e-9 : 1e-5)

/* Where a Newton run starts: from the prior mean, from the last mode with one
 * trial added, or from the last mode as it stands. */
#define PSYGP__START_COLD 0
#define PSYGP__START_ROW  1
#define PSYGP__START_KEEP 2

/* Cumulative-link arguments are clamped here before the tail cancels the
 * category probability to nothing. Beyond +-8.5 the probit adds 1e-17 of
 * probability per category, so the clamp costs nothing a double can see, and
 * it keeps the third derivative of an empty category finite. */
#define PSYGP__PROBIT_CLAMP 8.5
#define PSYGP__LOGIT_CLAMP  36.0

/* --- scalar math -------------------------------------------------------- */

static double psygp__phi(double z) {
    return exp(-0.5 * z * z) / PSYGP__SQRT2PI;
}

static double psygp__Phi(double z) {
    return 0.5 * erfc(-z / PSYGP__SQRT2);
}

/* log Phi(z), by the asymptotic series where erfc has already underflowed. */
static double psygp__log_Phi(double z) {
    if (z > -20.0) {
        double p = psygp__Phi(z);
        return p > 0.0 ? log(p) : -7.0e2;
    } else {
        double zi = 1.0 / (z * z);
        double s = 1.0 - zi * (1.0 - 3.0 * zi * (1.0 - 5.0 * zi));
        return -0.5 * z * z - log(-z * PSYGP__SQRT2PI) + log(s);
    }
}

/* The inverse Mills ratio phi(z) / Phi(z). The ratio tends to -z in the left
 * tail, where both parts have underflowed, so it gets the same series. */
static double psygp__mills(double z) {
    if (z > -20.0) {
        double p = psygp__Phi(z);
        return p > 1e-300 ? psygp__phi(z) / p : -z;
    } else {
        double zi = 1.0 / (z * z);
        double s = 1.0 - zi * (1.0 - 3.0 * zi * (1.0 - 5.0 * zi));
        return -z / s;
    }
}

/* Phi^-1 by Acklam's rational approximation plus one Halley step, which takes
 * it to full double precision. Used for the latent level of a target
 * probability, never in a hot loop. */
static double psygp__Phi_inv(double p) {
    static const double a[6] = { -3.969683028665376e+01,  2.209460984245205e+02,
                                 -2.759285104469687e+02,  1.383577518672690e+02,
                                 -3.066479806614716e+01,  2.506628277459239e+00 };
    static const double b[5] = { -5.447609879822406e+01,  1.615858368580409e+02,
                                 -1.556989798598866e+02,  6.680131188771972e+01,
                                 -1.328068155288572e+01 };
    static const double c[6] = { -7.784894002430293e-03, -3.223964580411365e-01,
                                 -2.400758277161838e+00, -2.549732539343734e+00,
                                  4.374664141464968e+00,  2.938163982698783e+00 };
    static const double d[4] = {  7.784695709041462e-03,  3.224671290700398e-01,
                                  2.445134137142996e+00,  3.754408661907416e+00 };
    double x, q, r, e, u;
    if (!(p > 0.0)) return -HUGE_VAL;
    if (!(p < 1.0)) return HUGE_VAL;
    if (p < 0.02425) {
        q = sqrt(-2.0 * log(p));
        x = (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
            ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    } else if (p <= 1.0 - 0.02425) {
        q = p - 0.5; r = q * q;
        x = (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5]) * q /
            (((((b[0]*r+b[1])*r+b[2])*r+b[3])*r+b[4])*r+1.0);
    } else {
        q = sqrt(-2.0 * log(1.0 - p));
        x = -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
             ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    }
    e = psygp__Phi(x) - p;
    u = e * PSYGP__SQRT2PI * exp(0.5 * x * x);
    return x - u / (1.0 + 0.5 * x * u);
}

static double psygp__sigmoid(double z) {
    if (z >= 0.0) return 1.0 / (1.0 + exp(-z));
    { double e = exp(z); return e / (1.0 + e); }
}

/* log sigmoid(z), without the cancellation of log(1 / (1 + exp(-z))). */
static double psygp__log_sigmoid(double z) {
    if (z >= 0.0) return -log1p(exp(-z));
    return z - log1p(exp(z));
}

/* The link and its first three derivatives at u. The ordinal likelihood needs
 * all four at both of the cutpoints that bound a category, and every f and
 * cutpoint derivative of that likelihood is a sum of them. */
static void psygp__link_all(int link, double u, double* L0, double* L1,
                            double* L2, double* L3) {
    if (link == PSYGP_LINK_LOGIT) {
        double s = psygp__sigmoid(u), d = s * (1.0 - s);
        *L0 = s;
        *L1 = d;
        *L2 = d * (1.0 - 2.0 * s);
        *L3 = d * (1.0 - 6.0 * s * (1.0 - s));
    } else {
        double d = psygp__phi(u);
        *L0 = psygp__Phi(u);
        *L1 = d;
        *L2 = -u * d;
        *L3 = (u * u - 1.0) * d;
    }
}

static double psygp__link(int link, double z) {
    return link == PSYGP_LINK_LOGIT ? psygp__sigmoid(z) : psygp__Phi(z);
}

static double psygp__link_inv(int link, double p) {
    if (link == PSYGP_LINK_LOGIT) {
        if (p <= 0.0) return -HUGE_VAL;
        if (p >= 1.0) return HUGE_VAL;
        return log(p / (1.0 - p));
    }
    return psygp__Phi_inv(p);
}

static double psygp__clamp(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* log(sum(exp(v))) over n entries, shifted by the largest. */
static double psygp__logsumexp(const double* v, int n) {
    double m = v[0], s = 0.0;
    for (int i = 1; i < n; i++) if (v[i] > m) m = v[i];
    for (int i = 0; i < n; i++) s += exp(v[i] - m);
    return m + log(s);
}

/* --- Gauss-Hermite ------------------------------------------------------ */

/* Nodes and weights for int f(x) exp(-x^2) dx, by Newton's method on the
 * normalized Hermite polynomials (Numerical Recipes' gauher). Computed at
 * open, not tabulated, so PSYGP_QUAD_N can change without a new table. The
 * expectation of g under N(mu, s^2) is then sum w_i g(mu + sqrt(2) s x_i) /
 * sqrt(pi). */
static void psygp__gh_init(double* x, double* w, int n) {
    const double pim4 = 0.7511255444649425;   /* pi^-1/4 */
    int m = (n + 1) / 2;
    double z = 0.0, pp = 1.0;
    for (int i = 1; i <= m; i++) {
        if (i == 1) {
            double an = 2.0 * n + 1.0;
            z = sqrt(an) - 1.85575 * pow(an, -0.16667);
        } else if (i == 2) {
            z -= 1.14 * pow((double)n, 0.426) / z;
        } else if (i == 3) {
            z = 1.86 * z - 0.86 * x[0];
        } else if (i == 4) {
            z = 1.91 * z - 0.91 * x[1];
        } else {
            z = 2.0 * z - x[i - 3];
        }
        for (int it = 0; it < 100; it++) {
            double p1 = pim4, p2 = 0.0, p3, z1;
            for (int j = 1; j <= n; j++) {
                p3 = p2; p2 = p1;
                p1 = z * sqrt(2.0 / (double)j) * p2 -
                     sqrt((double)(j - 1) / (double)j) * p3;
            }
            pp = sqrt(2.0 * (double)n) * p2;
            z1 = z;
            z = z1 - p1 / pp;
            if (fabs(z - z1) <= 1e-14 * (1.0 + fabs(z))) break;
        }
        x[i - 1] = z;
        x[n - i] = -z;
        w[i - 1] = 2.0 / (pp * pp);
        w[n - i] = w[i - 1];
    }
}

/* --- Halton ------------------------------------------------------------- */

/* The n-th prime (n = 0 gives 2), by trial division. n is at most
 * PSYGP_MAX_DIMS, so the loop is shorter than a table would be to maintain. */
static unsigned psygp__nth_prime(int n) {
    unsigned p = 2;
    for (int found = 0;; p++) {
        unsigned d;
        for (d = 2; d * d <= p; d++) if (p % d == 0) break;
        if (d * d > p) {
            if (found == n) return p;
            found++;
        }
    }
}

/* The radical inverse of i in the given base: index 1 gives 1/base. Index 0
 * would be the corner of the box, which is why the sequences start at 1. */
static double psygp__radinv(unsigned i, unsigned base) {
    double f = 1.0 / (double)base, r = 0.0;
    while (i > 0u) {
        r += f * (double)(i % base);
        i /= base;
        f /= (double)base;
    }
    return r;
}

/* Phi to about 1e-7 absolute with one exponential instead of erfc
 * (Abramowitz & Stegun 26.2.17). The look-ahead evaluates M^2 of these per
 * trial and erfc costs 30 to 150 ns depending on the library, so this is the
 * difference between a look-ahead that fits a display frame and one that does
 * not. Nothing outside the look-ahead uses it: predictions and thresholds want
 * the full-precision psygp__Phi. */
static double psygp__Phi_fast(double z) {
    static const double b0 = 0.319381530, b1 = -0.356563782, b2 = 1.781477937,
                        b3 = -1.821255978, b4 = 1.330274429;
    double az = fabs(z), tt, poly, p;
    if (az > 8.0) return z > 0.0 ? 1.0 : 0.0;
    tt = 1.0 / (1.0 + 0.2316419 * az);
    poly = tt * (b0 + tt * (b1 + tt * (b2 + tt * (b3 + tt * b4))));
    p = exp(-0.5 * az * az) * 0.39894228040143268 * poly;
    return z > 0.0 ? 1.0 - p : p;
}

/* --- dense linear algebra ----------------------------------------------- */

/* Sum of a[i] b[i]. Four accumulators, because this loop is the inner loop of
 * the Cholesky, of every triangular solve and of the candidate covariance, and
 * one accumulator serializes it on the latency of the addition: a compiler may
 * not reassociate floating point on its own, and at -O2 gcc 11 does not
 * vectorize it either. */
static double psygp__dot(const double* a, const double* b, int n) {
    double s0 = 0.0, s1 = 0.0, s2 = 0.0, s3 = 0.0;
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        s0 += a[i] * b[i];
        s1 += a[i + 1] * b[i + 1];
        s2 += a[i + 2] * b[i + 2];
        s3 += a[i + 3] * b[i + 3];
    }
    for (; i < n; i++) s0 += a[i] * b[i];
    return (s0 + s1) + (s2 + s3);
}

/* The same over a matrix row and a vector, and over two matrix rows. Separate
 * functions rather than one on psygp_real, because the vectors stay double
 * whatever PSYGP_REAL is, and the accumulation is double either way. */
static double psygp__dotr(const psygp_real* a, const double* b, int n) {
    double s0 = 0.0, s1 = 0.0, s2 = 0.0, s3 = 0.0;
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        s0 += a[i] * b[i];
        s1 += a[i + 1] * b[i + 1];
        s2 += a[i + 2] * b[i + 2];
        s3 += a[i + 3] * b[i + 3];
    }
    for (; i < n; i++) s0 += a[i] * b[i];
    return (s0 + s1) + (s2 + s3);
}

static double psygp__dotrr(const psygp_real* a, const psygp_real* b, int n) {
    double s0 = 0.0, s1 = 0.0, s2 = 0.0, s3 = 0.0;
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        s0 += (double)a[i] * b[i];
        s1 += (double)a[i + 1] * b[i + 1];
        s2 += (double)a[i + 2] * b[i + 2];
        s3 += (double)a[i + 3] * b[i + 3];
    }
    for (; i < n; i++) s0 += (double)a[i] * b[i];
    return (s0 + s1) + (s2 + s3);
}
/* Everything is row-major with an explicit leading dimension, so an N x N
 * matrix keeps its stride while N grows with the trial count. */

/* In-place lower Cholesky of the lower triangle. The upper triangle is left
 * alone. Returns false on a non-positive pivot, which is the one numeric
 * failure the API reports. */
static bool psygp__chol(psygp_real* A, int n, int ld) {
    for (int j = 0; j < n; j++) {
        psygp_real* rj = A + (size_t)j * ld;
        double dv = rj[j] - psygp__dotrr(rj, rj, j), inv;
        if (!(dv > 0.0)) return false;
        dv = sqrt(dv);
        rj[j] = (psygp_real)dv;
        inv = 1.0 / dv;
        for (int i = j + 1; i < n; i++) {
            psygp_real* ri = A + (size_t)i * ld;
            ri[j] = (psygp_real)((ri[j] - psygp__dotrr(ri, rj, j)) * inv);
        }
    }
    return true;
}

/* Solve L z = b in place. */
static void psygp__tri_fwd(const psygp_real* L, int n, int ld, double* b) {
    for (int i = 0; i < n; i++) {
        const psygp_real* ri = L + (size_t)i * ld;
        b[i] = (b[i] - psygp__dotr(ri, b, i)) / ri[i];
    }
}

/* Solve L^T x = z in place. */
static void psygp__tri_bwd(const psygp_real* L, int n, int ld, double* b) {
    for (int i = n - 1; i >= 0; i--) {
        const psygp_real* ri = L + (size_t)i * ld;
        double s = b[i] / ri[i];
        b[i] = s;
        for (int k = 0; k < i; k++) b[k] -= ri[k] * s;
    }
}

/* y = A x for a matrix stored in full. */
static void psygp__gemv(const psygp_real* A, int n, int ld, const double* x,
                        double* y) {
    for (int i = 0; i < n; i++) y[i] = psygp__dotr(A + (size_t)i * ld, x, n);
}

/* Invert a lower-triangular factor in place. Column order matters: row i is
 * read for columns j..i-1 before column j of it is overwritten, so j must run
 * upward. */
static void psygp__tri_inv(psygp_real* L, int n, int ld) {
    for (int i = 0; i < n; i++) {
        psygp_real* ri = L + (size_t)i * ld;
        double inv = 1.0 / ri[i];
        for (int j = 0; j < i; j++) {
            double s = 0.0;
            for (int k = j; k < i; k++) s += (double)ri[k] * L[(size_t)k * ld + j];
            ri[j] = (psygp_real)(-s * inv);
        }
        ri[i] = (psygp_real)inv;
    }
}

/* LU with partial pivoting, row-major, in place; piv records the row swaps as
 * doubles because the one arena this header owns holds doubles. The implicit
 * term of the hyperparameter gradient needs (I + K W)^-1 with the TRUE Hessian,
 * and that Hessian has negative entries as soon as desc.guess or desc.lapse
 * takes the likelihood out of log-concavity, so there is no Cholesky to lean
 * on. Nothing else in the header needs a general solve. */
static bool psygp__lu(psygp_real* A, int n, int ld, double* piv) {
    for (int c = 0; c < n; c++) {
        int p = c;
        double best = fabs(A[(size_t)c * ld + c]);
        for (int r = c + 1; r < n; r++) {
            double v = fabs(A[(size_t)r * ld + c]);
            if (v > best) { best = v; p = r; }
        }
        piv[c] = (double)p;
        if (p != c) {
            for (int k = 0; k < n; k++) {
                psygp_real tmp = A[(size_t)c * ld + k];
                A[(size_t)c * ld + k] = A[(size_t)p * ld + k];
                A[(size_t)p * ld + k] = tmp;
            }
        }
        if (!(fabs(A[(size_t)c * ld + c]) > 0.0)) return false;
        for (int r = c + 1; r < n; r++) {
            double f = A[(size_t)r * ld + c] / A[(size_t)c * ld + c];
            A[(size_t)r * ld + c] = (psygp_real)f;
            for (int k = c + 1; k < n; k++)
                A[(size_t)r * ld + k] -= (psygp_real)(f * A[(size_t)c * ld + k]);
        }
    }
    return true;
}

static void psygp__lu_solve(const psygp_real* A, int n, int ld, const double* piv,
                            double* b) {
    for (int c = 0; c < n; c++) {
        int p = (int)piv[c];
        if (p != c) { double tmp = b[c]; b[c] = b[p]; b[p] = tmp; }
        for (int r = c + 1; r < n; r++) b[r] -= A[(size_t)r * ld + c] * b[c];
    }
    for (int i = n - 1; i >= 0; i--) {
        for (int k = i + 1; k < n; k++) b[i] -= A[(size_t)i * ld + k] * b[k];
        b[i] /= A[(size_t)i * ld + i];
    }
}

/* A := A^T A for a lower-triangular A, in place, both triangles filled. Row i
 * of the result needs rows >= i of A, so rows are finished from the top and
 * each one is staged in `tmp` before it overwrites the row it was read from. */
static void psygp__tri_sqr(psygp_real* A, int n, int ld, double* tmp) {
    for (int i = 0; i < n; i++) {
        for (int j = 0; j <= i; j++) {
            double s = 0.0;
            for (int k = i; k < n; k++)
                s += (double)A[(size_t)k * ld + i] * A[(size_t)k * ld + j];
            tmp[j] = s;
        }
        for (int j = 0; j <= i; j++) {
            A[(size_t)i * ld + j] = (psygp_real)tmp[j];
            A[(size_t)j * ld + i] = (psygp_real)tmp[j];
        }
    }
}

/* --- memory layout ------------------------------------------------------ */

/* Scratch areas, carved out of the one allocation. The blocks above `blk` keep
 * their contents between calls; the ones below are temporaries. */
typedef struct psygp__scr {
    double* alpha;    /* K x Nmax: K^-1 (fhat - mean), the predictive weights */
    double* sW;       /* K x Nmax: sqrt of the Laplace Hessian diagonal       */
    double* cand_sd;  /* K x M: latent sd at the candidates                   */
    double* cand_t;   /* M: the latent level of the level set at a candidate */
    double* cand_dv;  /* M: the look-ahead covariance diagonal                */
    double* cand_p0;  /* M: P(f > f*) now, the look-ahead's baseline          */
    double* cand_isd; /* M: 1 / sd at a candidate, for the look-ahead's skip   */
    psygp_real* Rm;       /* Nmax x Nmax: (K + W^-1)^-1 for the hyper gradient,
                       *    and the per-class workspace of the categorical
                       *    Newton step                                       */
    psygp_real* Echol;    /* Nmax x Nmax: chol(sum_c E_c), CATEGORICAL only       */
    double* cat;      /* 4 x K x Nmax: the categorical Newton step, CAT only  */
    psygp_real* V;        /* M x Nmax: L^-1 W^1/2 k(X, cand), look-ahead only     */
    double* blk;      /* 2 x PSYGP__BLK x Nmax: blocked prediction            */
    double* kg;       /* PSYGP__NTHETA x Nmax: (dK/dtheta) grad               */
    double* score;    /* M: acquisition scores                               */
    double* gh_x;     /* PSYGP_QUAD_N                                         */
    double* gh_w;     /* PSYGP_QUAD_N                                         */
    double* th;       /* 6 x PSYGP__NTHETA: the fit's vectors                 */
    double* t1;       /* Nmax                                                 */
    double* t2;
    double* t3;
    double* t4;
    double* t5;
    double* t6;
    double* t7;
    double* pv;       /* 4 x PSYGP_MAX_OUTCOMES: outcome probabilities        */
} psygp__scr;

/* One walk of the arena for both jobs: psygp_memory_size() calls it with a
 * null base to add up the bytes, psygp_open() and every hot function call it
 * with the real base to get their pointers. One function, so the size and the
 * layout cannot drift apart. */
static size_t psygp__layout(const psygp_desc* d, int Nmax, int M, int K,
                            unsigned char* base, psygp_gp* g, psygp__scr* s) {
    size_t off = 0;
    int nd = d->n_dims;
    bool look = (d->acq == PSYGP_ACQ_EAVC);
    bool cat  = (d->lik == PSYGP_LIK_CATEGORICAL);

#define PSYGP__TAKE(pp, count) do {                                  \
        double** pp_ = (pp);                                         \
        if (base && pp_) *pp_ = (double*)(base + off);               \
        off += (size_t)(count) * sizeof(double);                      \
    } while (0)
/* The same for a psygp_real block, its size rounded up to a whole double so
 * that the blocks after it stay aligned in a float build. */
#define PSYGP__TAKER(pp, count) do {                                 \
        psygp_real** ppr_ = (pp);                                    \
        if (base && ppr_) *ppr_ = (psygp_real*)(base + off);         \
        off += (((size_t)(count) * sizeof(psygp_real) + sizeof(double) - 1) \
                / sizeof(double)) * sizeof(double);                  \
    } while (0)

    PSYGP__TAKE(g ? &g->X : NULL,       (size_t)Nmax * nd);
    PSYGP__TAKE(g ? &g->y : NULL,       (size_t)Nmax);
    PSYGP__TAKER(g ? &g->Kmat : NULL,   (size_t)Nmax * Nmax);
    PSYGP__TAKER(g ? &g->L : NULL,      (size_t)K * Nmax * Nmax);
    PSYGP__TAKE(g ? &g->f : NULL,       (size_t)K * Nmax);
    PSYGP__TAKE(g ? &g->W : NULL,       (size_t)K * Nmax);
    PSYGP__TAKE(g ? &g->grad : NULL,    (size_t)K * Nmax);
    PSYGP__TAKE(g ? &g->cand : NULL,    d->candidates ? 0 : (size_t)M * nd);
    PSYGP__TAKE(g ? &g->cand_mu : NULL, (size_t)K * M);
    PSYGP__TAKER(g ? &g->cand_cov : NULL, look ? (size_t)M * M : 0);
    if (g) g->scratch = (double*)(base ? base + off : NULL);
    PSYGP__TAKE(s ? &s->alpha : NULL,   (size_t)K * Nmax);
    PSYGP__TAKE(s ? &s->sW : NULL,      (size_t)K * Nmax);
    PSYGP__TAKE(s ? &s->cand_sd : NULL, (size_t)K * M);
    PSYGP__TAKE(s ? &s->cand_t : NULL,  (size_t)M);
    PSYGP__TAKE(s ? &s->cand_dv : NULL, (size_t)M);
    PSYGP__TAKE(s ? &s->cand_p0 : NULL, (size_t)M);
    PSYGP__TAKE(s ? &s->cand_isd : NULL, (size_t)M);
    PSYGP__TAKER(s ? &s->Rm : NULL,     (size_t)Nmax * Nmax);
    PSYGP__TAKER(s ? &s->Echol : NULL,  cat ? (size_t)Nmax * Nmax : 0);
    PSYGP__TAKE(s ? &s->cat : NULL,     cat ? (size_t)4 * K * Nmax : 0);
    PSYGP__TAKER(s ? &s->V : NULL,      look ? (size_t)M * Nmax : 0);
    PSYGP__TAKE(s ? &s->blk : NULL,     (size_t)2 * PSYGP__BLK * Nmax);
    PSYGP__TAKE(s ? &s->kg : NULL,      (size_t)PSYGP__NTHETA * Nmax);
    PSYGP__TAKE(s ? &s->score : NULL,   (size_t)M);
    PSYGP__TAKE(s ? &s->gh_x : NULL,    (size_t)PSYGP_QUAD_N);
    PSYGP__TAKE(s ? &s->gh_w : NULL,    (size_t)PSYGP_QUAD_N);
    PSYGP__TAKE(s ? &s->th : NULL,      (size_t)6 * PSYGP__NTHETA);
    PSYGP__TAKE(s ? &s->t1 : NULL,      (size_t)Nmax);
    PSYGP__TAKE(s ? &s->t2 : NULL,      (size_t)Nmax);
    PSYGP__TAKE(s ? &s->t3 : NULL,      (size_t)Nmax);
    PSYGP__TAKE(s ? &s->t4 : NULL,      (size_t)Nmax);
    PSYGP__TAKE(s ? &s->t5 : NULL,      (size_t)Nmax);
    PSYGP__TAKE(s ? &s->t6 : NULL,      (size_t)Nmax);
    PSYGP__TAKE(s ? &s->t7 : NULL,      (size_t)Nmax);
    PSYGP__TAKE(s ? &s->pv : NULL,      (size_t)4 * PSYGP_MAX_OUTCOMES);
#undef PSYGP__TAKE
#undef PSYGP__TAKER
    return off;
}

/* The scratch pointers of an open handle. Recomputed rather than stored: the
 * walk is a few dozen adds and the handle stays the size the manual says. */
static void psygp__scr_of(const psygp_gp* g, psygp__scr* s) {
    psygp__layout(&g->desc, g->N_max, g->M, g->K, (unsigned char*)g->mem, NULL, s);
}

/* --- desc validation ---------------------------------------------------- */

/* Candidate counts above this would make the M x M look-ahead covariance the
 * whole address space; the plain count is capped so M * M * 8 cannot overflow
 * a 32-bit size_t either. */
#define PSYGP__M_CAP      65536
/* More rounds than this cannot move a point that has converged, and a typo of
 * 1000 would otherwise cost a thousand predictions per dimension per trial. */
#define PSYGP__REFINE_MAX 32
#define PSYGP__M_CAP_LOOK 4096

static void psygp__err(char* buf, size_t cap, const char* fmt, ...) {
    va_list ap;
    if (!buf || cap == 0) return;
    va_start(ap, fmt);
    vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
}

/* Shared by psygp_memory_size() (which returns 0 on failure) and psygp_open()
 * (which reports why). Fills the three sizes the layout needs. */
static bool psygp__validate(const psygp_desc* d, char* err, size_t cap,
                            int* Nmax_out, int* M_out, int* K_out) {
    int nd, Nmax, M, K, nout;
    if (!d) { psygp__err(err, cap, "null desc"); return false; }
    nd = d->n_dims;
    if (nd < 1 || nd > PSYGP_MAX_DIMS) {
        psygp__err(err, cap, "n_dims = %d, need 1 to %d", nd, PSYGP_MAX_DIMS);
        return false;
    }
    for (int i = 0; i < nd; i++) {
        if (!(d->lo[i] < d->hi[i])) {
            psygp__err(err, cap, "lo[%d] = %g is not below hi[%d] = %g",
                       i, d->lo[i], i, d->hi[i]);
            return false;
        }
    }
    if (d->intensity_dim < 0 || d->intensity_dim >= nd) {
        psygp__err(err, cap, "intensity_dim = %d, need 0 to %d",
                   d->intensity_dim, nd - 1);
        return false;
    }
    if (d->lik < PSYGP_LIK_BERNOULLI || d->lik > PSYGP_LIK_GAUSSIAN) {
        psygp__err(err, cap, "lik = %d is not a psygp_lik", (int)d->lik);
        return false;
    }
    if (d->kernel != PSYGP_KERNEL_RBF && d->kernel != PSYGP_KERNEL_SEMIP) {
        psygp__err(err, cap, "kernel = %d is not a psygp_kernel", (int)d->kernel);
        return false;
    }
    if (d->link != PSYGP_LINK_PROBIT && d->link != PSYGP_LINK_LOGIT) {
        psygp__err(err, cap, "link = %d is not a psygp_link", (int)d->link);
        return false;
    }
    if (d->acq < PSYGP_ACQ_LSE || d->acq > PSYGP_ACQ_RANDOM) {
        psygp__err(err, cap, "acq = %d is not a psygp_acq", (int)d->acq);
        return false;
    }
    nout = d->n_outcomes;
    if (d->lik == PSYGP_LIK_ORDINAL) {
        if (nout < 2 || nout > PSYGP_MAX_OUTCOMES) {
            psygp__err(err, cap, "ORDINAL n_outcomes = %d, need 2 to %d",
                       nout, PSYGP_MAX_OUTCOMES);
            return false;
        }
    } else if (d->lik == PSYGP_LIK_CATEGORICAL) {
        if (nout < 3 || nout > PSYGP_MAX_OUTCOMES) {
            psygp__err(err, cap, "CATEGORICAL n_outcomes = %d, need 3 to %d",
                       nout, PSYGP_MAX_OUTCOMES);
            return false;
        }
    } else if (nout != 0 && nout != 2) {
        psygp__err(err, cap, "n_outcomes = %d, but this likelihood sets it",
                   nout);
        return false;
    }
    if (d->lik == PSYGP_LIK_ORDINAL) {
        int t = d->target_outcome == 0 ? 1 : d->target_outcome;
        if (t < 1 || t > nout - 1) {
            psygp__err(err, cap, "target_outcome = %d, need 1 to %d", t, nout - 1);
            return false;
        }
        /* Fixed cutpoints must be usable as they stand. */
        if (d->hyper.cutpoint[0] != 0.0) {
            for (int i = 1; i < nout - 1; i++) {
                if (!(d->hyper.cutpoint[i] > d->hyper.cutpoint[i - 1])) {
                    psygp__err(err, cap, "hyper.cutpoint is not increasing at %d", i);
                    return false;
                }
            }
        }
    } else if (d->lik == PSYGP_LIK_CATEGORICAL) {
        if (d->target_outcome < 0 || d->target_outcome > nout - 1) {
            psygp__err(err, cap, "target_outcome = %d, need 0 to %d",
                       d->target_outcome, nout - 1);
            return false;
        }
    }
    if (d->lik == PSYGP_LIK_GAUSSIAN && sizeof(psygp_real) != sizeof(double)) {
        psygp__err(err, cap,
                   "PSYGP_LIK_GAUSSIAN needs the double build: the exact "
                   "posterior factors the raw kernel matrix, which is too "
                   "ill-conditioned for %u-byte reals",
                   (unsigned)sizeof(psygp_real));
        return false;
    }
    if (d->guess < 0.0 || d->lapse < 0.0 || d->guess + d->lapse >= 1.0) {
        psygp__err(err, cap,
                   "guess = %g and lapse = %g: both need to be at least 0 and "
                   "sum below 1", d->guess, d->lapse);
        return false;
    }
    if ((d->guess != 0.0 || d->lapse != 0.0) &&
        (d->lik == PSYGP_LIK_CATEGORICAL || d->lik == PSYGP_LIK_GAUSSIAN)) {
        psygp__err(err, cap,
                   "guess and lapse apply to BERNOULLI and ORDINAL only");
        return false;
    }
    if (d->acq == PSYGP_ACQ_LSE || d->acq == PSYGP_ACQ_EAVC ||
        d->acq == PSYGP_ACQ_LOCALMI) {
        if (d->lik != PSYGP_LIK_GAUSSIAN &&
            !(d->target_p > d->guess && d->target_p < 1.0 - d->lapse)) {
            psygp__err(err, cap,
                       "a level-set acquisition needs target_p in (%g, %g), got %g",
                       d->guess, 1.0 - d->lapse, d->target_p);
            return false;
        }
    }
    for (int i = 0; i < nd; i++) {
        if (d->hyper.lengthscale[i] < 0.0 || d->hyper.lengthscale_b[i] < 0.0) {
            psygp__err(err, cap, "hyper.lengthscale[%d] is negative", i);
            return false;
        }
    }
    if (d->hyper.outputscale < 0.0 || d->hyper.outputscale_b < 0.0 ||
        d->hyper.noise_sd < 0.0 || d->jitter < 0.0) {
        psygp__err(err, cap, "an output scale, the noise sd or the jitter is negative");
        return false;
    }
    if (d->stop_trials < 0 || d->stop_threshold_sd < 0.0) {
        psygp__err(err, cap, "a stop criterion is negative");
        return false;
    }
    if (d->stop_trials == 0 && d->stop_threshold_sd == 0.0) {
        psygp__err(err, cap,
                   "no stop criterion: set stop_trials or stop_threshold_sd");
        return false;
    }
    if (d->max_trials < 0 || d->max_trials > PSYGP_MAX_TRIALS) {
        psygp__err(err, cap, "max_trials = %d, need 0 to %d (PSYGP_MAX_TRIALS)",
                   d->max_trials, PSYGP_MAX_TRIALS);
        return false;
    }
    if (d->n_init < 0) { psygp__err(err, cap, "n_init is negative"); return false; }
    if (d->fit_every < 0 || d->refit_every < 0) {
        psygp__err(err, cap, "fit_every or refit_every is negative");
        return false;
    }
    if (d->n_candidates < 0) {
        psygp__err(err, cap, "n_candidates is negative");
        return false;
    }
    if (d->refine_steps < 0 || d->refine_steps > PSYGP__REFINE_MAX) {
        psygp__err(err, cap, "refine_steps = %d, need 0 to %d", d->refine_steps,
                   PSYGP__REFINE_MAX);
        return false;
    }
    Nmax = d->max_trials > 0 ? d->max_trials : PSYGP_MAX_TRIALS;
    K = d->lik == PSYGP_LIK_CATEGORICAL ? nout : 1;
    /* The candidate set: the caller's list, a product grid, or Halton. */
    if (d->candidates) {
        if (d->n_candidates <= 0) {
            psygp__err(err, cap, "desc.candidates needs n_candidates > 0");
            return false;
        }
        M = d->n_candidates;
    } else {
        int any = 0;
        for (int i = 0; i < nd; i++) if (d->grid[i] != 0) any = 1;
        if (any) {
            double prod = 1.0;
            for (int i = 0; i < nd; i++) {
                if (d->grid[i] < 1) {
                    psygp__err(err, cap,
                               "grid[%d] = %d: every dimension needs at least 1 point",
                               i, d->grid[i]);
                    return false;
                }
                prod *= (double)d->grid[i];
            }
            if (prod > (double)PSYGP__M_CAP) {
                psygp__err(err, cap, "the product grid has %.0f points, cap is %d",
                           prod, PSYGP__M_CAP);
                return false;
            }
            M = 1;
            for (int i = 0; i < nd; i++) M *= d->grid[i];
        } else {
            M = d->n_candidates > 0 ? d->n_candidates : 512;
        }
    }
    if (M > PSYGP__M_CAP) {
        psygp__err(err, cap, "n_candidates = %d, cap is %d", M, PSYGP__M_CAP);
        return false;
    }
    if (d->acq == PSYGP_ACQ_EAVC && M > PSYGP__M_CAP_LOOK) {
        psygp__err(err, cap,
                   "EAVC holds an M x M covariance: M = %d, cap is %d", M,
                   PSYGP__M_CAP_LOOK);
        return false;
    }
    if (Nmax_out) *Nmax_out = Nmax;
    if (M_out) *M_out = M;
    if (K_out) *K_out = K;
    return true;
}

/* --- kernel ------------------------------------------------------------- */

/* The intensity enters the semiparametric kernel through x x', so the slope
 * GP's prior variance is x^2 k_b(c, c) and the point where the slope term
 * vanishes is x = 0. Measuring x from the middle of the box puts that point in
 * the middle of the stimulus range instead of wherever zero happens to fall in
 * the caller's units, which is the difference between a usable prior and one
 * that pins the latent to its mean at a box edge. */
static double psygp__xt(const psygp_gp* g, const double* x) {
    int id = g->desc.intensity_dim;
    return x[id] - 0.5 * (g->desc.lo[id] + g->desc.hi[id]);
}

/* k_a and the slope term x x' k_b of the kernel, separately: the hyperparameter
 * gradient needs the two parts, and the kernel value is their sum. */
static void psygp__kparts(const psygp_gp* g, const double* xa, const double* xb,
                          double* ka, double* kb, double* xx) {
    const psygp_hyper* h = &g->hyper;
    int nd = g->desc.n_dims, id = g->desc.intensity_dim;
    double qa = 0.0;
    if (g->desc.kernel == PSYGP_KERNEL_RBF) {
        for (int i = 0; i < nd; i++) {
            double dd = (xa[i] - xb[i]) / h->lengthscale[i];
            qa += dd * dd;
        }
        *ka = h->outputscale * exp(-0.5 * qa);
        *kb = 0.0;
        *xx = 0.0;
    } else {
        double qb = 0.0;
        for (int i = 0; i < nd; i++) {
            double dl;
            if (i == id) continue;
            dl = (xa[i] - xb[i]) / h->lengthscale[i];
            qa += dl * dl;
            dl = (xa[i] - xb[i]) / h->lengthscale_b[i];
            qb += dl * dl;
        }
        *ka = h->outputscale * exp(-0.5 * qa);
        *kb = h->outputscale_b * exp(-0.5 * qb);
        *xx = psygp__xt(g, xa) * psygp__xt(g, xb);
    }
}

static double psygp__kernel(const psygp_gp* g, const double* xa, const double* xb) {
    double ka, kb, xx;
    psygp__kparts(g, xa, xb, &ka, &kb, &xx);
    return ka + xx * kb;
}

/* --- hyperparameter vector ---------------------------------------------- */

enum {
    PSYGP__P_LS = 0, PSYGP__P_OS, PSYGP__P_MEAN,
    PSYGP__P_LSB, PSYGP__P_OSB, PSYGP__P_CUT, PSYGP__P_NOISE
};

typedef struct psygp__param {
    int    kind;
    int    dim;      /* dimension, or cutpoint array index */
    double lo, hi;   /* bounds in the fitted coordinate */
    bool   logsp;    /* fitted as log(value) */
} psygp__param;

static double psygp__pick(double v, double dflt) { return v != 0.0 ? v : dflt; }

/* The free hyperparameters, in a fixed order, with their bounds. A field the
 * caller set to something nonzero is fixed there; a zero field is fitted (and
 * carries a default until it is). That is why desc.hyper, not the values in
 * force, decides. */
static int psygp__params(const psygp_gp* g, psygp__param* p) {
    const psygp_desc* d = &g->desc;
    const psygp_hyper* h0 = &d->hyper;
    int nd = d->n_dims, id = d->intensity_dim, np = 0;
    bool semip = (d->kernel == PSYGP_KERNEL_SEMIP);
    for (int i = 0; i < nd; i++) {
        double range = d->hi[i] - d->lo[i];
        if (semip && i == id) continue;   /* no lengthscale along x */
        if (h0->lengthscale[i] != 0.0) continue;
        p[np].kind = PSYGP__P_LS; p[np].dim = i; p[np].logsp = true;
        p[np].lo = log(psygp__pick(d->hyper_min.lengthscale[i], 0.05 * range));
        p[np].hi = log(psygp__pick(d->hyper_max.lengthscale[i], 2.0 * range));
        np++;
    }
    if (h0->outputscale == 0.0) {
        p[np].kind = PSYGP__P_OS; p[np].dim = 0; p[np].logsp = true;
        p[np].lo = log(psygp__pick(d->hyper_min.outputscale, 0.1));
        p[np].hi = log(psygp__pick(d->hyper_max.outputscale, 10.0));
        np++;
    }
    if (h0->mean == 0.0) {
        p[np].kind = PSYGP__P_MEAN; p[np].dim = 0; p[np].logsp = false;
        p[np].lo = psygp__pick(d->hyper_min.mean, -5.0);
        p[np].hi = psygp__pick(d->hyper_max.mean, 5.0);
        np++;
    }
    if (semip) {
        for (int i = 0; i < nd; i++) {
            double range = d->hi[i] - d->lo[i];
            if (i == id) continue;
            if (h0->lengthscale_b[i] != 0.0) continue;
            p[np].kind = PSYGP__P_LSB; p[np].dim = i; p[np].logsp = true;
            p[np].lo = log(psygp__pick(d->hyper_min.lengthscale_b[i], 0.05 * range));
            p[np].hi = log(psygp__pick(d->hyper_max.lengthscale_b[i], 2.0 * range));
            np++;
        }
        if (h0->outputscale_b == 0.0) {
            p[np].kind = PSYGP__P_OSB; p[np].dim = 0; p[np].logsp = true;
            p[np].lo = log(psygp__pick(d->hyper_min.outputscale_b, 0.1));
            p[np].hi = log(psygp__pick(d->hyper_max.outputscale_b, 10.0));
            np++;
        }
    }
    if (d->lik == PSYGP_LIK_ORDINAL && h0->cutpoint[0] == 0.0) {
        /* c_1 is pinned to 0 and the rest are fitted as log gaps, which keeps
         * them increasing and keeps hyper.mean identifiable. */
        for (int m = 1; m < d->n_outcomes - 1; m++) {
            p[np].kind = PSYGP__P_CUT; p[np].dim = m; p[np].logsp = true;
            p[np].lo = log(psygp__pick(d->hyper_min.cutpoint[m], 0.01));
            p[np].hi = log(psygp__pick(d->hyper_max.cutpoint[m], 10.0));
            np++;
        }
    }
    if (d->lik == PSYGP_LIK_GAUSSIAN && h0->noise_sd == 0.0) {
        p[np].kind = PSYGP__P_NOISE; p[np].dim = 0; p[np].logsp = true;
        p[np].lo = log(psygp__pick(d->hyper_min.noise_sd, 1e-3));
        p[np].hi = log(psygp__pick(d->hyper_max.noise_sd, 10.0));
        np++;
    }
    return np;
}

static double psygp__param_value(const psygp_gp* g, const psygp__param* p) {
    const psygp_hyper* h = &g->hyper;
    switch (p->kind) {
        case PSYGP__P_LS:   return h->lengthscale[p->dim];
        case PSYGP__P_OS:   return h->outputscale;
        case PSYGP__P_MEAN: return h->mean;
        case PSYGP__P_LSB:  return h->lengthscale_b[p->dim];
        case PSYGP__P_OSB:  return h->outputscale_b;
        case PSYGP__P_CUT:  return h->cutpoint[p->dim] - h->cutpoint[p->dim - 1];
        default:            return h->noise_sd;
    }
}

static void psygp__theta_get(const psygp_gp* g, const psygp__param* p, int np,
                             double* th) {
    for (int i = 0; i < np; i++) {
        double v = psygp__param_value(g, &p[i]);
        th[i] = psygp__clamp(p[i].logsp ? log(v) : v, p[i].lo, p[i].hi);
    }
}

static void psygp__theta_set(psygp_gp* g, const psygp__param* p, int np,
                             const double* th) {
    psygp_hyper* h = &g->hyper;
    for (int i = 0; i < np; i++) {
        double v = psygp__clamp(th[i], p[i].lo, p[i].hi);
        if (p[i].logsp) v = exp(v);
        switch (p[i].kind) {
            case PSYGP__P_LS:   h->lengthscale[p[i].dim] = v; break;
            case PSYGP__P_OS:   h->outputscale = v; break;
            case PSYGP__P_MEAN: h->mean = v; break;
            case PSYGP__P_LSB:  h->lengthscale_b[p[i].dim] = v; break;
            case PSYGP__P_OSB:  h->outputscale_b = v; break;
            case PSYGP__P_CUT:  h->cutpoint[p[i].dim] = h->cutpoint[p[i].dim - 1] + v;
                                break;
            default:            h->noise_sd = v; break;
        }
    }
}

/* dk(xa, xb)/dtheta for every parameter at once: one pass over the matrix
 * instead of one pass per hyperparameter, which is what keeps a gradient
 * evaluation at O(N^2) per hyperparameter with the constant of a single
 * exponential. */
static void psygp__kernel_grad(const psygp_gp* g, const psygp__param* p, int np,
                               const double* xa, const double* xb, double* dk) {
    const psygp_hyper* h = &g->hyper;
    double ka, kb, xx;
    psygp__kparts(g, xa, xb, &ka, &kb, &xx);
    for (int i = 0; i < np; i++) {
        int dm = p[i].dim;
        double dd;
        switch (p[i].kind) {
            case PSYGP__P_LS:
                dd = (xa[dm] - xb[dm]) / h->lengthscale[dm];
                dk[i] = ka * dd * dd;
                break;
            case PSYGP__P_OS:
                dk[i] = ka;
                break;
            case PSYGP__P_LSB:
                dd = (xa[dm] - xb[dm]) / h->lengthscale_b[dm];
                dk[i] = xx * kb * dd * dd;
                break;
            case PSYGP__P_OSB:
                dk[i] = xx * kb;
                break;
            default:
                dk[i] = 0.0;   /* mean, cutpoints and noise are not in K */
                break;
        }
    }
}

/* --- likelihoods -------------------------------------------------------- */

/* The span the link is squeezed into: p = guess + span * link(f). One for a
 * likelihood the floor and ceiling do not apply to, so every formula below can
 * use it unconditionally. */
static double psygp__span(const psygp_gp* g) {
    return 1.0 - g->desc.guess - g->desc.lapse;
}


/* The cumulative link writes category j as Lambda(u1) - Lambda(u0) with
 * u0 = c_j - f and u1 = c_{j+1} - f, so every derivative in f and in the two
 * cutpoints is a combination of the partials of log(Lambda(u1) - Lambda(u0)).
 * Filling them all once is what lets the f-derivatives and the cutpoint
 * gradient share one code path.
 * G = { g0, g1, g00, g01, g11, g000, g001, g011, g111, log p }. */
static void psygp__ord_G(const psygp_gp* g, double fv, int j, double* G) {
    int link = (int)g->desc.link;
    int nout = g->desc.n_outcomes;
    double clamp = link == PSYGP_LINK_LOGIT ? PSYGP__LOGIT_CLAMP : PSYGP__PROBIT_CLAMP;
    double A0 = 0.0, l0 = 0.0, m0 = 0.0, n0 = 0.0;
    double A1 = 1.0, l1 = 0.0, m1 = 0.0, n1 = 0.0;
    double span = psygp__span(g), off = 0.0;
    double P, iP, iP2, iP3;
    if (j > 0) {
        double u0 = psygp__clamp(g->hyper.cutpoint[j - 1] - fv, -clamp, clamp);
        psygp__link_all(link, u0, &A0, &l0, &m0, &n0);
    }
    if (j < nout - 1) {
        double u1 = psygp__clamp(g->hyper.cutpoint[j] - fv, -clamp, clamp);
        psygp__link_all(link, u1, &A1, &l1, &m1, &n1);
    }
    /* desc.guess and desc.lapse move mass to the top and bottom categories:
     * P(y >= k) becomes guess + span P(y >= k) for every inner k, so a
     * category probability is span times the old difference, plus the lapse
     * for the bottom category and the guess for the top. The partials keep
     * their shape, with the densities scaled by the span. */
    if (j == 0) off = g->desc.lapse;
    if (j == nout - 1) off += g->desc.guess;
    l0 *= span; l1 *= span;
    m0 *= span; m1 *= span;
    n0 *= span; n1 *= span;
    P = off + span * (A1 - A0);
    if (P < 1e-14) P = 1e-14;
    iP = 1.0 / P; iP2 = iP * iP; iP3 = iP2 * iP;
    G[0] = -l0 * iP;
    G[1] =  l1 * iP;
    G[2] = -m0 * iP - l0 * l0 * iP2;
    G[3] =  l0 * l1 * iP2;
    G[4] =  m1 * iP - l1 * l1 * iP2;
    G[5] = -n0 * iP - 3.0 * l0 * m0 * iP2 - 2.0 * l0 * l0 * l0 * iP3;
    G[6] =  m0 * l1 * iP2 + 2.0 * l0 * l0 * l1 * iP3;
    G[7] =  l0 * m1 * iP2 - 2.0 * l0 * l1 * l1 * iP3;
    G[8] =  n1 * iP - 3.0 * l1 * m1 * iP2 + 2.0 * l1 * l1 * l1 * iP3;
    G[9] = log(P);
}

/* log p(y | f) and its first three derivatives in f, for the one-latent
 * likelihoods. */
static void psygp__ll(const psygp_gp* g, double fv, double yv,
                      double* lp, double* d1, double* d2, double* d3) {
    if (g->desc.lik == PSYGP_LIK_BERNOULLI &&
        (g->desc.guess > 0.0 || g->desc.lapse > 0.0)) {
        /* p(y = 1) = guess + span link(f). The tail-stable forms below do not
         * apply once there is a floor, so this path works from the link and
         * its three derivatives and the generic derivatives of a logarithm,
         * with the link argument clamped where the density underflows. */
        int link = (int)g->desc.link;
        double clamp = link == PSYGP_LINK_LOGIT ? PSYGP__LOGIT_CLAMP
                                                : PSYGP__PROBIT_CLAMP;
        double span = psygp__span(g);
        double L0, L1, L2, L3, q, q1, q2, q3, r;
        psygp__link_all(link, psygp__clamp(fv, -clamp, clamp), &L0, &L1, &L2, &L3);
        if (yv > 0.5) {
            q = g->desc.guess + span * L0;
            q1 = span * L1; q2 = span * L2; q3 = span * L3;
        } else {
            q = g->desc.lapse + span * (1.0 - L0);
            q1 = -span * L1; q2 = -span * L2; q3 = -span * L3;
        }
        if (q < 1e-300) q = 1e-300;
        r = q1 / q;
        *lp = log(q);
        *d1 = r;
        *d2 = q2 / q - r * r;
        *d3 = q3 / q - 3.0 * (q2 / q) * r + 2.0 * r * r * r;
    } else if (g->desc.lik == PSYGP_LIK_BERNOULLI) {
        if (g->desc.link == PSYGP_LINK_LOGIT) {
            double t = yv > 0.5 ? 1.0 : 0.0;
            double pr = psygp__sigmoid(fv), q = pr * (1.0 - pr);
            *lp = t > 0.5 ? psygp__log_sigmoid(fv) : psygp__log_sigmoid(-fv);
            *d1 = t - pr;
            *d2 = -q;
            *d3 = -q * (1.0 - 2.0 * pr);
        } else {
            double s = yv > 0.5 ? 1.0 : -1.0;
            double z = s * fv, r = psygp__mills(z);
            *lp = psygp__log_Phi(z);
            *d1 = s * r;
            *d2 = -(r * r + z * r);
            *d3 = s * (2.0 * r * r * r + 3.0 * z * r * r + (z * z - 1.0) * r);
        }
    } else {
        double G[10];
        int j = (int)(yv + 0.5);
        psygp__ord_G(g, fv, j, G);
        *lp = G[9];
        *d1 = -(G[0] + G[1]);
        *d2 = G[2] + 2.0 * G[3] + G[4];
        *d3 = -(G[5] + 3.0 * G[6] + 3.0 * G[7] + G[8]);
    }
}

/* The cutpoint partials the hyperparameter gradient needs: d log p / dc_m,
 * d2 log p / df dc_m and d3 log p / df^2 dc_m, where m indexes the cutpoint
 * array (c_{m+1} of the manual). Only the two cutpoints that bound the
 * observed category contribute. */
static void psygp__ll_cut(const psygp_gp* g, double fv, double yv, int m,
                          double* e0, double* e1, double* e2) {
    double G[10];
    int j = (int)(yv + 0.5);
    *e0 = *e1 = *e2 = 0.0;
    if (m != j - 1 && m != j) return;
    psygp__ord_G(g, fv, j, G);
    if (m == j - 1) {
        *e0 = G[0];
        *e1 = -(G[2] + G[3]);
        *e2 = G[5] + 2.0 * G[6] + G[7];
    } else {
        *e0 = G[1];
        *e1 = -(G[3] + G[4]);
        *e2 = G[6] + 2.0 * G[7] + G[8];
    }
}

/* All K outcome probabilities at an exact latent value, for the one-latent
 * likelihoods. */
static void psygp__probs_of_f(const psygp_gp* g, double fv, double* p) {
    int link = (int)g->desc.link;
    double span = psygp__span(g);
    if (g->desc.lik == PSYGP_LIK_BERNOULLI) {
        p[1] = g->desc.guess + span * psygp__link(link, fv);
        p[0] = 1.0 - p[1];
    } else {
        int nout = g->desc.n_outcomes;
        double prev = 0.0;
        for (int j = 0; j < nout - 1; j++) {
            double c = psygp__link(link, g->hyper.cutpoint[j] - fv);
            p[j] = span * (c - prev) + (j == 0 ? g->desc.lapse : 0.0);
            if (p[j] < 0.0) p[j] = 0.0;
            prev = c;
        }
        p[nout - 1] = span * (1.0 - prev) + g->desc.guess;
        if (p[nout - 1] < 0.0) p[nout - 1] = 0.0;
    }
}

/* The target quantity at an exact latent value: P(y = 1), P(y >= k*),
 * one-against-the-rest P(y = k*), or y itself. `other` carries the log-sum-exp
 * of the competing latents under CATEGORICAL and is ignored elsewhere. */
static double psygp__q_of_f(const psygp_gp* g, double fv, double other) {
    int link = (int)g->desc.link;
    int k = g->desc.target_outcome == 0 && g->desc.lik == PSYGP_LIK_ORDINAL
            ? 1 : g->desc.target_outcome;
    switch (g->desc.lik) {
        case PSYGP_LIK_BERNOULLI:
            return g->desc.guess + psygp__span(g) * psygp__link(link, fv);
        case PSYGP_LIK_ORDINAL:
            return g->desc.guess + psygp__span(g) *
                   (1.0 - psygp__link(link, g->hyper.cutpoint[k - 1] - fv));
        case PSYGP_LIK_CATEGORICAL: return psygp__sigmoid(fv - other);
        default:                    return fv;
    }
}

/* --- the kernel matrix -------------------------------------------------- */

static const double* psygp__cand(const psygp_gp* g, int i) {
    int nd = g->desc.n_dims;
    return g->desc.candidates ? g->desc.candidates + (size_t)i * nd
                              : g->cand + (size_t)i * nd;
}

static double psygp__jitter(const psygp_gp* g) {
    if (g->desc.jitter > 0.0) return g->desc.jitter;
    /* The default has to cover the rounding the factorization itself adds,
     * which is about N times the working epsilon. */
    return sizeof(psygp_real) == sizeof(double) ? 1e-6 : 1e-4;
}

/* Row and column n of the kernel matrix, for a trial just recorded. */
static void psygp__kmat_row(psygp_gp* g, int n) {
    int nd = g->desc.n_dims, ld = g->N_max;
    const double* xn = g->X + (size_t)n * nd;
    for (int i = 0; i < n; i++) {
        double v = psygp__kernel(g, g->X + (size_t)i * nd, xn);
        g->Kmat[(size_t)n * ld + i] = (psygp_real)v;
        g->Kmat[(size_t)i * ld + n] = (psygp_real)v;
    }
    g->Kmat[(size_t)n * ld + n] = (psygp_real)(psygp__kernel(g, xn, xn) + psygp__jitter(g));
}

/* The whole matrix, after a hyperparameter change. */
static void psygp__kmat_build(psygp_gp* g) {
    for (int n = 0; n < g->N; n++) psygp__kmat_row(g, n);
}

/* --- Laplace, one latent ------------------------------------------------ */

/* Newton's method to the mode of the latent posterior, in the form of
 * Rasmussen & Williams Algorithm 3.1: the step goes through
 * B = I + W^1/2 K W^1/2, whose Cholesky is the only factorization, and the
 * objective Psi = -1/2 a'(f - m) + log p(y|f) guards it with a backtracking
 * line search. Leaves L, W, sqrt(W), the log-likelihood gradient and
 * alpha = K^-1 (fhat - m) in place for prediction, and the Laplace log
 * marginal likelihood (eq. 3.32) in g->log_marginal.
 *
 * `mode` is the start: PSYGP__START_COLD from the prior mean,
 * PSYGP__START_ROW to reuse the last mode with one trial added, and
 * PSYGP__START_KEEP to reuse it as it stands, which is what a hyperparameter
 * step does. */
static int psygp__laplace(psygp_gp* g, int mode) {
    psygp__scr s;
    int n = g->N, ld = g->N_max;
    double mean = g->hyper.mean;
    double *fv, *W, *d1, *sW, *a, *h, *b, *cv, *an, *hn;
    double psi = 0.0, logdet = 0.0, ll = 0.0;
    psygp__scr_of(g, &s);
    fv = g->f; W = g->W; d1 = g->grad; sW = s.sW; a = s.alpha;
    h = s.t1; b = s.t2; cv = s.t3; an = s.t4; hn = s.t5;

    /* A start has to be a consistent (alpha, f) pair or Psi is not comparable
     * across steps, so f is always recomputed from alpha rather than carried
     * over. Any alpha gives a consistent pair, which is what makes a warm start
     * safe after the kernel has changed under it. */
    if (mode == PSYGP__START_COLD) for (int i = 0; i < n; i++) a[i] = 0.0;
    else if (mode == PSYGP__START_ROW) a[n - 1] = 0.0;
    psygp__gemv(g->Kmat, n, ld, a, h);
    for (int i = 0; i < n; i++) fv[i] = mean + h[i];

    for (int it = 0; it < PSYGP__NEWTON_MAX; it++) {
        double ah = 0.0, step = 1.0;
        bool moved = false;
        ll = 0.0;
        for (int i = 0; i < n; i++) {
            double lp, dd1, dd2, dd3, w;
            psygp__ll(g, fv[i], g->y[i], &lp, &dd1, &dd2, &dd3);
            ll += lp;
            d1[i] = dd1;
            w = -dd2;                       /* log-concave: w >= 0 */
            if (!(w > 0.0)) w = 0.0;        /* rounding, or a saturated site */
            if (w > 1e8) w = 1e8;
            W[i] = w;
            sW[i] = sqrt(w);
        }
        /* B = I + W^1/2 K W^1/2, lower triangle only: the Cholesky reads no
         * more than that. */
        for (int i = 0; i < n; i++) {
            const psygp_real* kr = g->Kmat + (size_t)i * ld;
            psygp_real* br = g->L + (size_t)i * ld;
            double si = sW[i];
            for (int j = 0; j <= i; j++) br[j] = (psygp_real)(si * kr[j] * sW[j]);
            br[i] += 1.0;
        }
        if (!psygp__chol(g->L, n, ld)) return PSYGP_ERR_NUMERIC;
        logdet = 0.0;
        for (int i = 0; i < n; i++) logdet += log(g->L[(size_t)i * ld + i]);
        for (int i = 0; i < n; i++) ah += a[i] * h[i];
        psi = -0.5 * ah + ll;

        for (int i = 0; i < n; i++) b[i] = W[i] * h[i] + d1[i];
        psygp__gemv(g->Kmat, n, ld, b, cv);
        for (int i = 0; i < n; i++) cv[i] *= sW[i];
        psygp__tri_fwd(g->L, n, ld, cv);
        psygp__tri_bwd(g->L, n, ld, cv);
        for (int i = 0; i < n; i++) an[i] = b[i] - sW[i] * cv[i];
        psygp__gemv(g->Kmat, n, ld, an, hn);

        /* Convergence on the step, not on the objective: the objective is
         * stationary at the mode, so it stops moving while the mode is still
         * micrometers away, and W, L and the log determinant all carry that
         * error into the marginal likelihood and its gradient. */
        {
            double dmax = 0.0, hmax = 0.0;
            for (int i = 0; i < n; i++) {
                double dd = fabs(hn[i] - h[i]);
                if (dd > dmax) dmax = dd;
                if (fabs(h[i]) > hmax) hmax = fabs(h[i]);
            }
            if (dmax <= PSYGP__NEWTON_TOL * (1.0 + hmax)) break;
        }

        for (int ls = 0; ls < 20; ls++) {
            double ah2 = 0.0, ll2 = 0.0, psi2;
            for (int i = 0; i < n; i++) {
                double at = a[i] + step * (an[i] - a[i]);
                double ht = h[i] + step * (hn[i] - h[i]);
                double lp, x1, x2, x3;
                ah2 += at * ht;
                psygp__ll(g, mean + ht, g->y[i], &lp, &x1, &x2, &x3);
                ll2 += lp;
            }
            psi2 = -0.5 * ah2 + ll2;
            if (psi2 > psi - 1e-13 * (1.0 + fabs(psi))) {
                for (int i = 0; i < n; i++) {
                    a[i] += step * (an[i] - a[i]);
                    h[i] += step * (hn[i] - h[i]);
                    fv[i] = mean + h[i];
                }
                moved = true;
                break;
            }
            step *= 0.5;
        }
        if (!moved) break;   /* no uphill step left; L still matches f */
    }
    g->log_marginal = psi - logdet;
    g->N_fit = n;
    g->fit_valid = true;
    return PSYGP_OK;
}

/* --- Laplace, K softmax latents ----------------------------------------- */

/* Rasmussen & Williams Algorithm 3.3. The Hessian is no longer diagonal:
 * W = diag(pi) - Pi Pi', so the step needs one Cholesky per class of
 * B_c = I + D_c^1/2 K D_c^1/2 plus one of sum_c E_c with
 * E_c = (K + D_c^-1)^-1, and sum_c E_c is what the class blocks couple
 * through. g->W holds pi, which is the diagonal part. */
static int psygp__laplace_cat(psygp_gp* g, int mode) {
    psygp__scr s;
    int n = g->N, ld = g->N_max, K = g->K;
    size_t bs = (size_t)ld * ld, kn = (size_t)K * ld;
    double mean = g->hyper.mean;
    double *pi, *d1, *sW, *a, *h, *b, *cv, *an, *hn, *r, *z;
    double psi = 0.0, logdet = 0.0, ll = 0.0;
    psygp__scr_of(g, &s);
    pi = g->W; d1 = g->grad; sW = s.sW; a = s.alpha;
    h = s.cat; b = s.cat + kn; an = s.cat + 2 * kn; hn = s.cat + 3 * kn;
    cv = s.t1; r = s.t2; z = s.t3;

    for (int c = 0; c < K; c++) {
        if (mode == PSYGP__START_COLD)
            for (int i = 0; i < n; i++) a[(size_t)c * ld + i] = 0.0;
        else if (mode == PSYGP__START_ROW) a[(size_t)c * ld + n - 1] = 0.0;
        psygp__gemv(g->Kmat, n, ld, a + (size_t)c * ld, h + (size_t)c * ld);
        for (int i = 0; i < n; i++)
            g->f[(size_t)c * ld + i] = mean + h[(size_t)c * ld + i];
    }

    for (int it = 0; it < PSYGP__NEWTON_MAX; it++) {
        double ah = 0.0, step = 1.0;
        bool moved = false;
        ll = 0.0;
        for (int i = 0; i < n; i++) {
            double fs[PSYGP_MAX_OUTCOMES] = { 0.0 }, lse;
            int yi = (int)(g->y[i] + 0.5);
            for (int c = 0; c < K; c++) fs[c] = g->f[(size_t)c * ld + i];
            lse = psygp__logsumexp(fs, K);
            ll += fs[yi] - lse;
            for (int c = 0; c < K; c++) {
                double p = exp(fs[c] - lse);
                if (p < 1e-10) p = 1e-10;
                pi[(size_t)c * ld + i] = p;
                sW[(size_t)c * ld + i] = sqrt(p);
                d1[(size_t)c * ld + i] = (c == yi ? 1.0 : 0.0) - p;
            }
        }
        /* One B_c per class, then sum_c E_c and its factor. */
        for (int i = 0; i < n; i++) z[i] = 0.0;
        for (int c = 0; c < K; c++) {
            psygp_real* Lc = g->L + (size_t)c * bs;
            const double* sc = sW + (size_t)c * ld;
            for (int i = 0; i < n; i++) {
                const psygp_real* kr = g->Kmat + (size_t)i * ld;
                psygp_real* br = Lc + (size_t)i * ld;
                double si = sc[i];
                for (int j = 0; j <= i; j++)
                    br[j] = (psygp_real)(si * kr[j] * sc[j]);
                br[i] += (psygp_real)1.0;
            }
            if (!psygp__chol(Lc, n, ld)) return PSYGP_ERR_NUMERIC;
            /* E_c = Z'Z with Z = L_c^-1 D_c^1/2. */
            for (int i = 0; i < n; i++)
                memcpy(s.Rm + (size_t)i * ld, Lc + (size_t)i * ld,
                       (size_t)(i + 1) * sizeof(psygp_real));
            psygp__tri_inv(s.Rm, n, ld);
            for (int i = 0; i < n; i++)
                for (int j = 0; j <= i; j++) s.Rm[(size_t)i * ld + j] = (psygp_real)(s.Rm[(size_t)i * ld + j] * sc[j]);
            psygp__tri_sqr(s.Rm, n, ld, cv);
            if (c == 0) {
                for (int i = 0; i < n; i++)
                    memcpy(s.Echol + (size_t)i * ld, s.Rm + (size_t)i * ld,
                           (size_t)(i + 1) * sizeof(psygp_real));
            } else {
                for (int i = 0; i < n; i++)
                    for (int j = 0; j <= i; j++)
                        s.Echol[(size_t)i * ld + j] += s.Rm[(size_t)i * ld + j];
            }
        }
        if (!psygp__chol(s.Echol, n, ld)) return PSYGP_ERR_NUMERIC;
        logdet = 0.0;
        for (int i = 0; i < n; i++) logdet += log(s.Echol[(size_t)i * ld + i]);
        for (int c = 0; c < K; c++) {
            const psygp_real* Lc = g->L + (size_t)c * bs;
            for (int i = 0; i < n; i++) logdet += log(Lc[(size_t)i * ld + i]);
        }
        for (size_t i = 0; i < kn; i++) ah += a[i] * h[i];
        psi = -0.5 * ah + ll;

        /* b = (D - Pi Pi') h + (y - pi) */
        for (int i = 0; i < n; i++) {
            double ph = 0.0;
            for (int c = 0; c < K; c++)
                ph += pi[(size_t)c * ld + i] * h[(size_t)c * ld + i];
            for (int c = 0; c < K; c++) {
                size_t o = (size_t)c * ld + i;
                b[o] = pi[o] * (h[o] - ph) + d1[o];
            }
        }
        /* c_c = E_c K b_c and r = sum_c c_c */
        for (int i = 0; i < n; i++) r[i] = 0.0;
        for (int c = 0; c < K; c++) {
            const psygp_real* Lc = g->L + (size_t)c * bs;
            const double* sc = sW + (size_t)c * ld;
            psygp__gemv(g->Kmat, n, ld, b + (size_t)c * ld, cv);
            for (int i = 0; i < n; i++) cv[i] *= sc[i];
            psygp__tri_fwd(Lc, n, ld, cv);
            psygp__tri_bwd(Lc, n, ld, cv);
            for (int i = 0; i < n; i++) {
                cv[i] *= sc[i];
                an[(size_t)c * ld + i] = cv[i];   /* park c_c */
                r[i] += cv[i];
            }
        }
        psygp__tri_fwd(s.Echol, n, ld, r);
        psygp__tri_bwd(s.Echol, n, ld, r);
        /* a_c = b_c - c_c + E_c (sum E)^-1 r */
        for (int c = 0; c < K; c++) {
            const psygp_real* Lc = g->L + (size_t)c * bs;
            const double* sc = sW + (size_t)c * ld;
            for (int i = 0; i < n; i++) z[i] = sc[i] * r[i];
            psygp__tri_fwd(Lc, n, ld, z);
            psygp__tri_bwd(Lc, n, ld, z);
            for (int i = 0; i < n; i++) {
                size_t o = (size_t)c * ld + i;
                an[o] = b[o] - an[o] + sc[i] * z[i];
            }
            psygp__gemv(g->Kmat, n, ld, an + (size_t)c * ld, hn + (size_t)c * ld);
        }
        /* Converge on the step; see psygp__laplace. */
        {
            double dmax = 0.0, hmax = 0.0;
            for (size_t i = 0; i < kn; i++) {
                double dd = fabs(hn[i] - h[i]);
                if (dd > dmax) dmax = dd;
                if (fabs(h[i]) > hmax) hmax = fabs(h[i]);
            }
            if (dmax <= PSYGP__NEWTON_TOL * (1.0 + hmax)) break;
        }

        for (int ls = 0; ls < 20; ls++) {
            double ah2 = 0.0, ll2 = 0.0, psi2;
            for (int c = 0; c < K; c++)
                for (int i = 0; i < n; i++) {
                    size_t o = (size_t)c * ld + i;
                    ah2 += (a[o] + step * (an[o] - a[o])) *
                           (h[o] + step * (hn[o] - h[o]));
                }
            for (int i = 0; i < n; i++) {
                double fs[PSYGP_MAX_OUTCOMES] = { 0.0 };
                int yi = (int)(g->y[i] + 0.5);
                for (int c = 0; c < K; c++) {
                    size_t o = (size_t)c * ld + i;
                    fs[c] = mean + h[o] + step * (hn[o] - h[o]);
                }
                ll2 += fs[yi] - psygp__logsumexp(fs, K);
            }
            psi2 = -0.5 * ah2 + ll2;
            if (psi2 > psi - 1e-13 * (1.0 + fabs(psi))) {
                for (int c = 0; c < K; c++)
                    for (int i = 0; i < n; i++) {
                        size_t o = (size_t)c * ld + i;
                        a[o] += step * (an[o] - a[o]);
                        h[o] += step * (hn[o] - h[o]);
                        g->f[o] = mean + h[o];
                    }
                moved = true;
                break;
            }
            step *= 0.5;
        }
        if (!moved) break;
    }
    g->log_marginal = psi - logdet;
    g->N_fit = n;
    g->fit_valid = true;
    return PSYGP_OK;
}

/* --- exact Gaussian regression ------------------------------------------ */

/* No Newton: the posterior is Gaussian and the factor of K + noise^2 I gains
 * one row per trial in O(N^2), a triangular solve and a square root. `from` is
 * the first row that is not factored yet; 0 rebuilds. sqrt(W) is set to 1
 * because the noise is already inside this factor, which lets prediction and
 * the look-ahead share the Laplace code path. */
static int psygp__gauss_fit(psygp_gp* g, int from) {
    psygp__scr s;
    int n = g->N, ld = g->N_max;
    double nv = g->hyper.noise_sd * g->hyper.noise_sd;
    double logdet = 0.0, quad = 0.0;
    psygp__scr_of(g, &s);
    for (int i = from; i < n; i++) {
        psygp_real* Lr = g->L + (size_t)i * ld;
        double d = g->Kmat[(size_t)i * ld + i] + nv;
        for (int k = 0; k < i; k++) s.t1[k] = g->Kmat[(size_t)i * ld + k];
        psygp__tri_fwd(g->L, i, ld, s.t1);
        for (int k = 0; k < i; k++) {
            Lr[k] = (psygp_real)s.t1[k];
            d -= s.t1[k] * s.t1[k];
        }
        if (!(d > 0.0)) return PSYGP_ERR_NUMERIC;
        Lr[i] = (psygp_real)sqrt(d);
    }
    for (int i = 0; i < n; i++) {
        s.alpha[i] = g->y[i] - g->hyper.mean;
        s.sW[i] = 1.0;
        g->W[i] = nv > 0.0 ? 1.0 / nv : 1e8;   /* the look-ahead's precision */
    }
    psygp__tri_fwd(g->L, n, ld, s.alpha);
    psygp__tri_bwd(g->L, n, ld, s.alpha);
    for (int i = 0; i < n; i++) {
        logdet += log(g->L[(size_t)i * ld + i]);
        quad += (g->y[i] - g->hyper.mean) * s.alpha[i];
        g->f[i] = g->hyper.mean;   /* unused, but keeps the field defined */
        g->grad[i] = (g->y[i] - g->hyper.mean) / (nv > 0.0 ? nv : 1e-8);
    }
    g->log_marginal = -0.5 * quad - logdet - 0.5 * (double)n * PSYGP__LOG2PI;
    g->N_fit = n;
    g->fit_valid = true;
    return PSYGP_OK;
}

/* One trial's worth of posterior update in O(N^2), for desc.refit_every: the
 * same rank-one construction the look-ahead acquisitions use, applied to the
 * training set instead of the candidate set.
 *
 * With W frozen at the values the last exact refit left, adding a trial adds
 * one row and column to B = I + W^1/2 K W^1/2, and the Cholesky of a bordered
 * matrix gains one row by a triangular solve and a square root. The new site's
 * own W comes from the look-ahead value: the predictive mean there, which is
 * K a with the old alpha extended by a zero, through the likelihood. One
 * quasi-Newton step at that frozen W then moves the mode.
 *
 * What it costs in accuracy is the drift of W at the old sites, which is why
 * an exact refit still runs every desc.refit_every trials. What it saves is
 * the refactorization: five matrix-vector products instead of a Cholesky per
 * Newton step. Single-latent discrete likelihoods only. */
static int psygp__laplace_grow(psygp_gp* g) {
    psygp__scr s;
    int n = g->N, ld = g->N_max, m = n - 1;
    double mean = g->hyper.mean;
    double *fv, *W, *d1, *sW, *a, *h, *b, *cv;
    double mu_new, w_new, dd, ll = 0.0, ah = 0.0, logdet = 0.0;
    if (n < 2 || g->N_fit != m) return psygp__laplace(g, PSYGP__START_ROW);
    psygp__scr_of(g, &s);
    fv = g->f; W = g->W; d1 = g->grad; sW = s.sW; a = s.alpha;
    h = s.t1; b = s.t2; cv = s.t3;

    a[m] = 0.0;
    psygp__gemv(g->Kmat, n, ld, a, h);
    mu_new = mean + h[m];
    {   /* the new site's curvature at its predictive mean, which is the
         * look-ahead's frozen W. Its gradient is not needed here: the step
         * below reads every site's gradient at the mode it lands on. */
        double lp, dd1, dd2, dd3;
        psygp__ll(g, mu_new, g->y[m], &lp, &dd1, &dd2, &dd3);
        w_new = -dd2;
    }
    if (w_new > 1e8) w_new = 1e8;
    if (!(w_new > 0.0)) w_new = 0.0;
    W[m] = w_new;
    sW[m] = sqrt(w_new);

    for (int i = 0; i < m; i++) cv[i] = sW[i] * g->Kmat[(size_t)m * ld + i] * sW[m];
    psygp__tri_fwd(g->L, m, ld, cv);
    dd = 1.0 + sW[m] * sW[m] * g->Kmat[(size_t)m * ld + m];
    for (int i = 0; i < m; i++) {
        g->L[(size_t)m * ld + i] = (psygp_real)cv[i];
        dd -= cv[i] * cv[i];
    }
    if (!(dd > 0.0)) return PSYGP_ERR_NUMERIC;
    g->L[(size_t)m * ld + m] = (psygp_real)sqrt(dd);

    for (int i = 0; i < n; i++) {
        double lp, dd1, dd2, dd3;
        fv[i] = mean + h[i];
        psygp__ll(g, fv[i], g->y[i], &lp, &dd1, &dd2, &dd3);
        d1[i] = dd1;
        b[i] = W[i] * h[i] + dd1;
    }
    psygp__gemv(g->Kmat, n, ld, b, cv);
    for (int i = 0; i < n; i++) cv[i] *= sW[i];
    psygp__tri_fwd(g->L, n, ld, cv);
    psygp__tri_bwd(g->L, n, ld, cv);
    for (int i = 0; i < n; i++) a[i] = b[i] - sW[i] * cv[i];
    psygp__gemv(g->Kmat, n, ld, a, h);
    for (int i = 0; i < n; i++) {
        double lp, dd1, dd2, dd3;
        fv[i] = mean + h[i];
        psygp__ll(g, fv[i], g->y[i], &lp, &dd1, &dd2, &dd3);
        d1[i] = dd1;
        ll += lp;
        ah += a[i] * h[i];
    }
    for (int i = 0; i < n; i++) logdet += log(g->L[(size_t)i * ld + i]);
    g->log_marginal = -0.5 * ah + ll - logdet;
    g->N_fit = n;
    g->fit_valid = true;
    return PSYGP_OK;
}

/* Whether this configuration may take the cheap update at all. */
static bool psygp__can_grow(const psygp_gp* g) {
    return g->desc.refit_every > 1 && g->K == 1 &&
           g->desc.lik != PSYGP_LIK_GAUSSIAN;
}

/* One entry point for all four: refit the posterior at the current data and
 * hyperparameters. `grow` is the row the factor is valid up to, so an update
 * can add a row under GAUSSIAN instead of refactoring. */
static int psygp__infer(psygp_gp* g, int mode, int grow) {
    if (g->N == 0) {
        g->N_fit = 0;
        g->fit_valid = true;
        g->log_marginal = 0.0;
        return PSYGP_OK;
    }
    if (g->desc.lik == PSYGP_LIK_GAUSSIAN)
        return psygp__gauss_fit(g, mode == PSYGP__START_ROW ? grow : 0);
    if (g->K > 1) return psygp__laplace_cat(g, mode);
    return psygp__laplace(g, mode);
}

/* --- prediction --------------------------------------------------------- */

/* Latent k at x: Rasmussen & Williams Algorithm 3.2, with the extra term the
 * softmax posterior adds through sum_c E_c (eq. 3.41). w1 and w2 are N-vectors
 * of workspace the caller owns. */
static void psygp__predict_k(const psygp_gp* g, const psygp__scr* s,
                             const double* x, int k, double* mu, double* var,
                             double* w1, double* w2) {
    int n = g->N_fit, ld = g->N_max, nd = g->desc.n_dims;
    double kxx = psygp__kernel(g, x, x), m = 0.0, v;
    const double* al = s->alpha + (size_t)k * ld;
    const double* sw = s->sW + (size_t)k * ld;
    const psygp_real* Lk = g->L + (size_t)k * ld * ld;
    if (n == 0) { *mu = g->hyper.mean; *var = kxx; return; }
    for (int i = 0; i < n; i++) w1[i] = psygp__kernel(g, g->X + (size_t)i * nd, x);
    m = psygp__dot(al, w1, n);
    for (int i = 0; i < n; i++) w2[i] = sw[i] * w1[i];
    psygp__tri_fwd(Lk, n, ld, w2);
    v = kxx - psygp__dot(w2, w2, n);
    if (g->K > 1) {
        psygp__tri_bwd(Lk, n, ld, w2);
        for (int i = 0; i < n; i++) w2[i] *= sw[i];
        psygp__tri_fwd(s->Echol, n, ld, w2);
        for (int i = 0; i < n; i++) v += w2[i] * w2[i];
    }
    *mu = g->hyper.mean + m;
    *var = v > 0.0 ? v : 0.0;
}

/* Latent means and sds at every candidate, in blocks of PSYGP__BLK so the
 * triangular solve streams the factor once per block and not once per
 * candidate. When `kv` is a latent index, its L^-1 W^1/2 k(X, c) columns are
 * kept in V for the look-ahead covariance. */
/* Latent k's mean and sd at n points, in blocks of PSYGP__BLK so the triangular
 * solve streams the factor once per block instead of once per point. This is the
 * same path the candidate cache uses, which is why a field plot costs what one
 * acquisition sweep costs rather than n times a single prediction. */
static void psygp__predict_many(const psygp_gp* g, const psygp__scr* s,
                                const double* xs, int n, int k,
                                double* mu, double* sd) {
    int nf = g->N_fit, ld = g->N_max, nd = g->desc.n_dims;
    const double* al = s->alpha + (size_t)k * ld;
    const double* sw = s->sW + (size_t)k * ld;
    const psygp_real* Lk = g->L + (size_t)k * ld * ld;
    for (int b0 = 0; b0 < n; b0 += PSYGP__BLK) {
        int b1 = b0 + PSYGP__BLK < n ? b0 + PSYGP__BLK : n;
        double kxx[PSYGP__BLK];
        for (int j = b0; j < b1; j++) {
            const double* xj = xs + (size_t)j * nd;
            double* row = s->blk + (size_t)(j - b0) * ld;
            kxx[j - b0] = psygp__kernel(g, xj, xj);
            for (int i = 0; i < nf; i++)
                row[i] = psygp__kernel(g, g->X + (size_t)i * nd, xj);
        }
        for (int j = b0; j < b1; j++) {
            const double* row = s->blk + (size_t)(j - b0) * ld;
            double* wv = s->blk + (size_t)(PSYGP__BLK + j - b0) * ld;
            double v = kxx[j - b0];
            mu[j] = g->hyper.mean + psygp__dot(al, row, nf);
            if (nf == 0) { sd[j] = sqrt(v); continue; }
            for (int i = 0; i < nf; i++) wv[i] = sw[i] * row[i];
            psygp__tri_fwd(Lk, nf, ld, wv);
            v -= psygp__dot(wv, wv, nf);
            if (g->K > 1) {
                double* w2 = s->t6;
                memcpy(w2, wv, (size_t)nf * sizeof(double));
                psygp__tri_bwd(Lk, nf, ld, w2);
                for (int i = 0; i < nf; i++) w2[i] *= sw[i];
                psygp__tri_fwd(s->Echol, nf, ld, w2);
                v += psygp__dot(w2, w2, nf);
            }
            sd[j] = v > 0.0 ? sqrt(v) : 0.0;
        }
    }
}

static void psygp__cand_fill(psygp_gp* g, int kv) {
    psygp__scr s;
    int n = g->N_fit, ld = g->N_max, nd = g->desc.n_dims, M = g->M, K = g->K;
    psygp__scr_of(g, &s);
    for (int b0 = 0; b0 < M; b0 += PSYGP__BLK) {
        int b1 = b0 + PSYGP__BLK < M ? b0 + PSYGP__BLK : M;
        double kxx[PSYGP__BLK];
        for (int j = b0; j < b1; j++) {
            const double* cj = psygp__cand(g, j);
            double* row = s.blk + (size_t)(j - b0) * ld;
            kxx[j - b0] = psygp__kernel(g, cj, cj);
            for (int i = 0; i < n; i++)
                row[i] = psygp__kernel(g, g->X + (size_t)i * nd, cj);
        }
        for (int k = 0; k < K; k++) {
            const double* al = s.alpha + (size_t)k * ld;
            const double* sw = s.sW + (size_t)k * ld;
            const psygp_real* Lk = g->L + (size_t)k * ld * ld;
            for (int j = b0; j < b1; j++) {
                const double* row = s.blk + (size_t)(j - b0) * ld;
                double* wv = s.blk + (size_t)(PSYGP__BLK + j - b0) * ld;
                double v = kxx[j - b0];
                g->cand_mu[(size_t)k * M + j] = g->hyper.mean +
                                                psygp__dot(al, row, n);
                if (n == 0) { s.cand_sd[(size_t)k * M + j] = sqrt(v); continue; }
                for (int i = 0; i < n; i++) wv[i] = sw[i] * row[i];
                psygp__tri_fwd(Lk, n, ld, wv);
                v -= psygp__dot(wv, wv, n);
                if (kv == k && s.V) {
                    psygp_real* vrow = s.V + (size_t)j * ld;
                    for (int i = 0; i < n; i++) vrow[i] = (psygp_real)wv[i];
                }
                if (K > 1) {
                    double* w2 = s.t6;
                    memcpy(w2, wv, (size_t)n * sizeof(double));
                    psygp__tri_bwd(Lk, n, ld, w2);
                    for (int i = 0; i < n; i++) w2[i] *= sw[i];
                    psygp__tri_fwd(s.Echol, n, ld, w2);
                    for (int i = 0; i < n; i++) v += w2[i] * w2[i];
                }
                s.cand_sd[(size_t)k * M + j] = v > 0.0 ? sqrt(v) : 0.0;
            }
        }
    }
}

/* The M x M latent covariance among the candidates for latent kv, which is the
 * one object the look-ahead needs beyond the per-candidate marginals: with W
 * frozen, every look-ahead variance is a rank-one update of it. Under
 * CATEGORICAL the between-class term of eq. 3.41 is dropped here, on top of the
 * one-against-the-rest approximation the manual states. */
static void psygp__cand_cov_build(psygp_gp* g, int kv) {
    psygp__scr s;
    int n = g->N_fit, M = g->M, ld = g->N_max;
    psygp__scr_of(g, &s);
    psygp__cand_fill(g, kv);
    for (int i = 0; i < M; i++) {
        const double* ci = psygp__cand(g, i);
        const psygp_real* vi = s.V + (size_t)i * ld;
        for (int j = 0; j <= i; j++) {
            const psygp_real* vj = s.V + (size_t)j * ld;
            double c = psygp__kernel(g, ci, psygp__cand(g, j)) -
                       psygp__dotrr(vi, vj, n);
            g->cand_cov[(size_t)i * M + j] = (psygp_real)c;
            g->cand_cov[(size_t)j * M + i] = (psygp_real)c;
        }
    }
}

/* --- marginal likelihood and its gradient ------------------------------- */

/* The objective of a hyperparameter fit, and its derivative in the fitted
 * coordinates. The value is the Laplace approximation to the log marginal
 * likelihood (Rasmussen & Williams eq. 3.32), or the exact one under GAUSSIAN
 * (eq. 2.30). The gradient is analytic: eqs. 5.22 to 5.24 for Laplace, which
 * means an explicit part through K and an implicit part through the mode, and
 * eq. 5.9 for the Gaussian case, which has no implicit part. Every call leaves
 * the handle's posterior at `th`.
 *
 * The one-pass structure matters: the derivative of the kernel in every
 * hyperparameter is read off one visit to each matrix element, so the whole
 * gradient costs one pass and not one pass per hyperparameter. */
/* Weak priors on the two hyperparameters an unregularized type-II fit runs away
 * with: log-normal on the output scales, normal on the mean, both centered on
 * the defaults. Data that nearly separate (a run where almost every trial came
 * back the same way) let the marginal likelihood keep improving as the output
 * scale and the mean grow, and it stops only at the bounds: the posterior is
 * then diffuse, a level-set acquisition follows the variance to the edges of
 * the box instead of the threshold, and the run never recovers. The likelihood
 * near that bound is flat, so a prior this weak is enough to hold the fit at a
 * sane place while data that really do insist still move it. This is what makes
 * the fit a MAP estimate rather than type-II maximum likelihood; psygp_log_
 * marginal() still reports the likelihood alone. */
/* How many trials in a row one candidate may win before psygp_next() breaks the
 * loop with a Halton point. */
#define PSYGP__REPEAT_MAX 3

#define PSYGP__PRIOR_LS_SD 1.0
#define PSYGP__PRIOR_OS_SD 0.7

static double psygp__log_prior(const psygp_gp* g, const psygp__param* p, int np,
                               const double* th, double* grad) {
    double lp = 0.0;
    if (g->desc.no_hyper_prior) return 0.0;
    for (int i = 0; i < np; i++) {
        double sd, center, v;
        switch (p[i].kind) {
            case PSYGP__P_LS: case PSYGP__P_LSB:
                sd = PSYGP__PRIOR_LS_SD;
                center = log(0.25 * (g->desc.hi[p[i].dim] - g->desc.lo[p[i].dim]));
                break;
            case PSYGP__P_OS: case PSYGP__P_OSB:
                sd = PSYGP__PRIOR_OS_SD;
                center = 0.0;            /* log 1 */
                break;
            default:
                continue;                /* none on the mean or the cutpoints */
        }
        v = (psygp__clamp(th[i], p[i].lo, p[i].hi) - center) / sd;
        lp -= 0.5 * v * v;
        if (grad) grad[i] -= v / sd;
    }
    return lp;
}

static int psygp__fit_eval(psygp_gp* g, const psygp__param* p, int np,
                           const double* th, double* val, double* grad) {
    psygp__scr s;
    int n, ld = g->N_max, rc;
    bool gauss = (g->desc.lik == PSYGP_LIK_GAUSSIAN);
    bool clamped = false;
    double *q, *s2, *bv, *s3, *tv, *tmp, *piv;
    double aa[PSYGP__NTHETA], tr[PSYGP__NTHETA];
    psygp__scr_of(g, &s);
    psygp__theta_set(g, p, np, th);
    psygp__kmat_build(g);
    rc = psygp__infer(g, g->fit_valid && g->N_fit == g->N ? PSYGP__START_KEEP
                                                        : PSYGP__START_COLD, 0);
    if (rc != PSYGP_OK) { g->fit_valid = false; return rc; }
    *val = g->log_marginal + psygp__log_prior(g, p, np, th, NULL);
    if (!grad) return PSYGP_OK;
    if (g->K > 1) return PSYGP_ERR_ARG;   /* softmax: psygp__fit_grad_fd */
    n = g->N;
    q = s.t1; s2 = s.t2; bv = s.t3; s3 = s.t4; tv = s.t5; tmp = s.t6; piv = s.t7;

    /* R = (K + W^-1)^-1 = W^1/2 B^-1 W^1/2, dense because the trace term needs
     * every element of it. Under GAUSSIAN sqrt(W) is 1 and the factor already
     * holds K + noise^2 I, so the same lines give (K + noise^2 I)^-1. */
    for (int i = 0; i < n; i++)
        memcpy(s.Rm + (size_t)i * ld, g->L + (size_t)i * ld,
               (size_t)(i + 1) * sizeof(psygp_real));
    psygp__tri_inv(s.Rm, n, ld);
    psygp__tri_sqr(s.Rm, n, ld, tmp);
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            s.Rm[(size_t)i * ld + j] = (psygp_real)(s.Rm[(size_t)i * ld + j] *
                                                   s.sW[i] * s.sW[j]);

    if (!gauss) {
        /* diag((K^-1 + W)^-1) = diag(K) - diag(C'C), C = L^-1 W^1/2 K, one
         * block of columns at a time so the factor is read M / BLK times. */
        for (int b0 = 0; b0 < n; b0 += PSYGP__BLK) {
            int b1 = b0 + PSYGP__BLK < n ? b0 + PSYGP__BLK : n;
            for (int j = b0; j < b1; j++) {
                double* wv = s.blk + (size_t)(j - b0) * ld;
                double v = g->Kmat[(size_t)j * ld + j];
                for (int i = 0; i < n; i++)
                    wv[i] = s.sW[i] * g->Kmat[(size_t)j * ld + i];
                psygp__tri_fwd(g->L, n, ld, wv);
                q[j] = 0.5 * (v - psygp__dot(wv, wv, n));
            }
        }
        for (int i = 0; i < n; i++) {
            double lp, d1, d2, d3;
            psygp__ll(g, g->f[i], g->y[i], &lp, &d1, &d2, &d3);
            /* A site whose W the Newton step clamped to zero (which only
             * happens when desc.guess or desc.lapse has taken the log
             * likelihood out of log-concavity) contributes no dW/dtheta, so it
             * contributes nothing here either. Without this the gradient would
             * be the derivative of an objective the code does not evaluate. */
            s2[i] = g->W[i] > 0.0 ? q[i] * d3 : 0.0;
            if (!(-d2 > 0.0)) clamped = true;
        }
        if (clamped) {
            /* The mode is defined by the TRUE gradient, so the implicit term
             * goes through the true Hessian even where the Newton step used a
             * clamped one. Build I + K W_true and factor it with an LU, in the
             * Cholesky factor's buffer: nothing below this point reads the
             * factor, and the lines at the end put it back. */
            for (int i = 0; i < n; i++) {
                double lp, d1, d2, d3;
                psygp__ll(g, g->f[i], g->y[i], &lp, &d1, &d2, &d3);
                tv[i] = -d2;
            }
            for (int i = 0; i < n; i++) {
                psygp_real* ar = g->L + (size_t)i * ld;
                const psygp_real* kr = g->Kmat + (size_t)i * ld;
                for (int j = 0; j < n; j++) ar[j] = (psygp_real)(kr[j] * tv[j] + (i == j ? 1.0 : 0.0));
            }
            if (!psygp__lu(g->L, n, ld, piv)) clamped = false;
        }
    }

    /* One visit per matrix element for the explicit terms and for
     * (dK/dtheta) grad, which the implicit term needs. */
    for (int i = 0; i < np; i++) {
        aa[i] = 0.0;
        tr[i] = 0.0;
        for (int j = 0; j < n; j++) s.kg[(size_t)i * ld + j] = 0.0;
    }
    for (int i = 0; i < n; i++) {
        const double* xi = g->X + (size_t)i * g->desc.n_dims;
        for (int j = 0; j <= i; j++) {
            double dk[PSYGP__NTHETA];
            double wgt = (i == j) ? 1.0 : 2.0;
            psygp__kernel_grad(g, p, np, xi, g->X + (size_t)j * g->desc.n_dims, dk);
            for (int t = 0; t < np; t++) {
                if (dk[t] == 0.0) continue;
                aa[t] += wgt * s.alpha[i] * dk[t] * s.alpha[j];
                tr[t] += wgt * s.Rm[(size_t)i * ld + j] * dk[t];
                s.kg[(size_t)t * ld + i] += dk[t] * g->grad[j];
                if (i != j) s.kg[(size_t)t * ld + j] += dk[t] * g->grad[i];
            }
        }
    }

    for (int t = 0; t < np; t++) {
        double gv = 0.0;
        bool implicit = !gauss;
        switch (p[t].kind) {
            case PSYGP__P_LS: case PSYGP__P_OS:
            case PSYGP__P_LSB: case PSYGP__P_OSB:
                gv = 0.5 * aa[t] - 0.5 * tr[t];
                memcpy(bv, s.kg + (size_t)t * ld, (size_t)n * sizeof(double));
                break;
            case PSYGP__P_MEAN:
                for (int i = 0; i < n; i++) { gv += s.alpha[i]; bv[i] = 1.0; }
                break;
            case PSYGP__P_NOISE: {
                double nv = g->hyper.noise_sd * g->hyper.noise_sd, trc = 0.0, aa2 = 0.0;
                for (int i = 0; i < n; i++) {
                    trc += s.Rm[(size_t)i * ld + i];
                    aa2 += s.alpha[i] * s.alpha[i];
                }
                gv = nv * (aa2 - trc);
                implicit = false;
                break;
            }
            default: {   /* PSYGP__P_CUT: assembled below */
                gv = 0.0;
                implicit = false;
                break;
            }
        }
        if (implicit) {
            /* s3 = (I + K W)^-1 b, which is b - K R b while W is the Laplace
             * Hessian, and an LU solve when a clamped entry means R was built
             * from something else. The implicit term is then s2' s3 (eq. 5.24). */
            if (clamped) {
                memcpy(s3, bv, (size_t)n * sizeof(double));
                psygp__lu_solve(g->L, n, ld, piv, s3);
                for (int i = 0; i < n; i++) gv += s2[i] * s3[i];
            } else {
                psygp__gemv(s.Rm, n, ld, bv, tv);
                psygp__gemv(g->Kmat, n, ld, tv, s3);
                for (int i = 0; i < n; i++) gv += s2[i] * (bv[i] - s3[i]);
            }
        }
        grad[t] = gv;
    }

    /* Cutpoints: the same explicit-plus-implicit split, but the likelihood
     * depends on them directly, so the log-determinant term comes in through
     * dW/dc and the implicit term through K de1/dc. The fitted coordinate is
     * the log gap to the cutpoint below, which is why one gap moves every
     * cutpoint above it. */
    if (g->desc.lik == PSYGP_LIK_ORDINAL) {
        double gc[PSYGP_MAX_OUTCOMES];
        int nout = g->desc.n_outcomes;
        bool any = false;
        for (int t = 0; t < np; t++) if (p[t].kind == PSYGP__P_CUT) any = true;
        if (any) {
            for (int m = 1; m < nout - 1; m++) {
                double ex = 0.0;
                for (int i = 0; i < n; i++) {
                    double e0, e1, e2;
                    psygp__ll_cut(g, g->f[i], g->y[i], m, &e0, &e1, &e2);
                    ex += e0 + (g->W[i] > 0.0 ? q[i] * e2 : 0.0);
                    tv[i] = e1;
                }
                psygp__gemv(g->Kmat, n, ld, tv, bv);
                if (clamped) {
                    memcpy(s3, bv, (size_t)n * sizeof(double));
                    psygp__lu_solve(g->L, n, ld, piv, s3);
                    for (int i = 0; i < n; i++) ex += s2[i] * s3[i];
                } else {
                    psygp__gemv(s.Rm, n, ld, bv, tv);
                    psygp__gemv(g->Kmat, n, ld, tv, s3);
                    for (int i = 0; i < n; i++) ex += s2[i] * (bv[i] - s3[i]);
                }
                gc[m] = ex;
            }
            for (int t = 0; t < np; t++) {
                if (p[t].kind != PSYGP__P_CUT) continue;   /* the gaps below */
                {
                    double gap = g->hyper.cutpoint[p[t].dim] -
                                 g->hyper.cutpoint[p[t].dim - 1];
                    double sum = 0.0;
                    for (int m = p[t].dim; m < nout - 1; m++) sum += gc[m];
                    grad[t] = gap * sum;
                }
            }
        }
    }
    psygp__log_prior(g, p, np, th, grad);
    if (clamped) {
        /* Put the Cholesky of B back: prediction reads it, and a caller may
         * read estimates between two steps of a fit. */
        for (int i = 0; i < n; i++) {
            const psygp_real* kr = g->Kmat + (size_t)i * ld;
            psygp_real* br = g->L + (size_t)i * ld;
            double si = s.sW[i];
            for (int j = 0; j <= i; j++) br[j] = (psygp_real)(si * kr[j] * s.sW[j]);
            br[i] += 1.0;
        }
        if (!psygp__chol(g->L, n, ld)) { g->fit_valid = false; return PSYGP_ERR_NUMERIC; }
    }
    return PSYGP_OK;
}

/* The softmax likelihood's gradient, by central differences on the objective.
 * The implicit term of eq. 5.23 needs third derivatives of a coupled block
 * Hessian; until that is written and checked, a numerical gradient of a
 * verified objective is the honest option. It costs 2 P + 1 Laplace refits. */
static int psygp__fit_grad_fd(psygp_gp* g, const psygp__param* p, int np,
                              const double* th, double* val, double* grad) {
    psygp__scr s;
    double* tw;
    int rc;
    psygp__scr_of(g, &s);
    tw = s.th + 5 * PSYGP__NTHETA;
    rc = psygp__fit_eval(g, p, np, th, val, NULL);
    if (rc != PSYGP_OK) return rc;
    memcpy(tw, th, (size_t)np * sizeof(double));
    for (int i = 0; i < np; i++) {
        double step = 1e-4 * (1.0 + fabs(th[i])), vp, vm;
        double hi = th[i] + step, lo = th[i] - step;
        if (hi > p[i].hi) hi = p[i].hi;
        if (lo < p[i].lo) lo = p[i].lo;
        if (!(hi > lo)) { grad[i] = 0.0; continue; }
        tw[i] = hi;
        if (psygp__fit_eval(g, p, np, tw, &vp, NULL) != PSYGP_OK) { grad[i] = 0.0; tw[i] = th[i]; continue; }
        tw[i] = lo;
        if (psygp__fit_eval(g, p, np, tw, &vm, NULL) != PSYGP_OK) { grad[i] = 0.0; tw[i] = th[i]; continue; }
        grad[i] = (vp - vm) / (hi - lo);
        tw[i] = th[i];
    }
    return psygp__fit_eval(g, p, np, th, val, NULL);
}

static int psygp__fit_value_grad(psygp_gp* g, const psygp__param* p, int np,
                                 const double* th, double* val, double* grad) {
    if (grad && g->K > 1) return psygp__fit_grad_fd(g, p, np, th, val, grad);
    return psygp__fit_eval(g, p, np, th, val, grad);
}

/* Gradient ascent with a backtracking line search, bounded by hyper_min and
 * hyper_max in the fitted coordinates. Bounded and monotone: a step is taken
 * only when it raises the marginal likelihood, so a fit never makes the model
 * worse than the values it started from.
 *
 * The ascent is a state machine rather than a loop, because a whole fit is
 * tens of refits and an experiment between two trials may only have room for
 * one. psygp_fit_step() advances it by one evaluation and psygp_fit() runs it
 * to the end. Every exit leaves the model at the best point found, so a caller
 * that stops stepping early still has a consistent posterior. */
#define PSYGP__FIT_EVALS 40
#define PSYGP__FIT_IDLE  0
#define PSYGP__FIT_GRAD  1
#define PSYGP__FIT_TRY   2

PSYGP_API int psygp_fit_step(psygp_gp* g) {
    psygp__scr s;
    psygp__param p[PSYGP__NTHETA];
    double *th, *cand, *gr;
    double val, gmax = 0.0;
    int np, rc;
    if (!g || !g->open) return PSYGP_ERR_CLOSED;
    if (!g->fit_valid) return PSYGP_ERR_NUMERIC;
    np = psygp__params(g, p);
    if (np == 0 || g->N < 2) {   /* nothing free, or nothing to fit it to */
        g->fit_state = PSYGP__FIT_IDLE;
        return 0;
    }
    psygp__scr_of(g, &s);
    th = s.th; cand = s.th + PSYGP__NTHETA; gr = s.th + 2 * PSYGP__NTHETA;
    if (g->fit_state == PSYGP__FIT_IDLE) {
        psygp__theta_get(g, p, np, th);
        g->fit_evals = 0;
        g->fit_back = 0;
        g->fit_step = 0.0;
        g->fit_state = PSYGP__FIT_GRAD;
    }
    if (g->fit_evals >= PSYGP__FIT_EVALS) {
        g->fit_state = PSYGP__FIT_IDLE;
        return 0;
    }
    if (g->fit_state == PSYGP__FIT_GRAD) {
        rc = psygp__fit_value_grad(g, p, np, th, &val, gr);
        g->fit_evals++;
        g->fit_state = PSYGP__FIT_IDLE;
        if (rc != PSYGP_OK) {
            psygp__fit_eval(g, p, np, th, &val, NULL);   /* put the model back */
            return rc;
        }
        g->fit_best = val;
        for (int i = 0; i < np; i++) if (fabs(gr[i]) > gmax) gmax = fabs(gr[i]);
        if (!(gmax > 1e-8)) return 0;                    /* converged */
        if (!(g->fit_step > 0.0)) g->fit_step = 0.25 / gmax;
        g->fit_back = 0;
        g->fit_state = PSYGP__FIT_TRY;
        return 1;
    }
    {   /* PSYGP__FIT_TRY: one candidate along the gradient. */
        double move = 0.0;
        for (int i = 0; i < np; i++) {
            cand[i] = psygp__clamp(th[i] + g->fit_step * gr[i], p[i].lo, p[i].hi);
            move += fabs(cand[i] - th[i]);
        }
        if (move < 1e-10) { g->fit_state = PSYGP__FIT_IDLE; return 0; }
        rc = psygp__fit_eval(g, p, np, cand, &val, NULL);
        g->fit_evals++;
        if (rc == PSYGP_OK &&
            val > g->fit_best + PSYGP__FIT_TOL * (1.0 + fabs(g->fit_best))) {
            memcpy(th, cand, (size_t)np * sizeof(double));
            g->fit_best = val;
            g->fit_step *= 2.0;
            g->fit_state = PSYGP__FIT_GRAD;
            return 1;
        }
        /* Rejected, so the model is sitting at a worse point: move it back
         * before returning, because the caller may read estimates between
         * steps. That is the second refit a step can cost. */
        g->fit_step *= 0.5;
        g->fit_back++;
        if (psygp__fit_eval(g, p, np, th, &val, NULL) != PSYGP_OK) {
            g->fit_state = PSYGP__FIT_IDLE;
            return PSYGP_ERR_NUMERIC;
        }
        /* The restore is bookkeeping, not search, so it does not spend the
         * budget: what the budget counts is how much of the hyperparameter
         * space the fit is allowed to look at. */
        if (g->fit_back >= 12) { g->fit_state = PSYGP__FIT_IDLE; return 0; }
        return 1;
    }
}

static int psygp__fit_run(psygp_gp* g) {
    int rc;
    g->fit_state = PSYGP__FIT_IDLE;
    while ((rc = psygp_fit_step(g)) > 0) { }
    g->fit_state = PSYGP__FIT_IDLE;
    return rc;
}

/* --- the target quantity under the posterior ---------------------------- */

#define PSYGP__NSCEN (PSYGP_MAX_OUTCOMES > PSYGP_QUAD_N ? PSYGP_MAX_OUTCOMES \
                                                        : PSYGP_QUAD_N)

/* The latent level at which the target quantity is met exactly, which is the
 * level set the LSE straddle and LocalMI work on. */
static double psygp__f_star(const psygp_gp* g) {
    const psygp_desc* d = &g->desc;
    int link = (int)d->link;
    double p = d->target_p;
    if (d->lik == PSYGP_LIK_GAUSSIAN) return d->target_value;
    if (!(p > 0.0 && p < 1.0)) return 0.0;   /* no level-set acquisition asked */
    if (d->lik == PSYGP_LIK_CATEGORICAL) return log(p / (1.0 - p));
    /* Undo the floor and the ceiling before inverting the link: the level is
     * on the probability the observer produces, not on link(f). */
    p = (p - d->guess) / psygp__span(g);
    if (!(p > 0.0 && p < 1.0)) return 0.0;
    if (d->lik == PSYGP_LIK_ORDINAL) {
        int k = d->target_outcome == 0 ? 1 : d->target_outcome;
        return g->hyper.cutpoint[k - 1] - psygp__link_inv(link, 1.0 - p);
    }
    return psygp__link_inv(link, p);
}

/* E[target quantity] at a latent mean and sd: closed form for the probit
 * Bernoulli case, Gauss-Hermite quadrature otherwise. */
static double psygp__q_smooth(const psygp_gp* g, const psygp__scr* s,
                              double mu, double sd, double other) {
    double acc = 0.0;
    if (g->desc.lik == PSYGP_LIK_GAUSSIAN) return mu;
    if (g->desc.lik == PSYGP_LIK_BERNOULLI && g->desc.link == PSYGP_LINK_PROBIT)
        return g->desc.guess +
               psygp__span(g) * psygp__Phi(mu / sqrt(1.0 + sd * sd));
    if (!(sd > 0.0)) return psygp__q_of_f(g, mu, other);
    for (int i = 0; i < PSYGP_QUAD_N; i++)
        acc += s->gh_w[i] * psygp__q_of_f(g, mu + PSYGP__SQRT2 * sd * s->gh_x[i],
                                         other);
    return acc / PSYGP__SQRTPI;
}

/* Var[target quantity], the BALV score. */
static double psygp__q_var(const psygp_gp* g, const psygp__scr* s,
                           double mu, double sd, double other) {
    double m1 = 0.0, m2 = 0.0;
    if (g->desc.lik == PSYGP_LIK_GAUSSIAN) return sd * sd;
    if (!(sd > 0.0)) return 0.0;
    for (int i = 0; i < PSYGP_QUAD_N; i++) {
        double q = psygp__q_of_f(g, mu + PSYGP__SQRT2 * sd * s->gh_x[i], other);
        m1 += s->gh_w[i] * q;
        m2 += s->gh_w[i] * q * q;
    }
    m1 /= PSYGP__SQRTPI;
    m2 /= PSYGP__SQRTPI;
    return m2 - m1 * m1 > 0.0 ? m2 - m1 * m1 : 0.0;
}

static double psygp__entropy(const double* p, int n) {
    double h = 0.0;
    for (int i = 0; i < n; i++) if (p[i] > 1e-300) h -= p[i] * log(p[i]);
    return h;
}

/* All outcome probabilities at an exact latent value, in the form the
 * acquisitions score: two entries under CATEGORICAL, where the class is scored
 * against the rest. */
static int psygp__scen_probs(const psygp_gp* g, double fv, double other, double* p) {
    if (g->desc.lik == PSYGP_LIK_CATEGORICAL) {
        p[1] = psygp__sigmoid(fv - other);
        p[0] = 1.0 - p[1];
        return 2;
    }
    psygp__probs_of_f(g, fv, p);
    return g->desc.lik == PSYGP_LIK_BERNOULLI ? 2 : g->desc.n_outcomes;
}

/* BALD: the mutual information between the outcome and the latent (Houlsby et
 * al. 2011), H[E p(y|f)] - E H[p(y|f)], by quadrature. Closed form under
 * GAUSSIAN, where it is the information of one noisy reading. */
static double psygp__bald(const psygp_gp* g, const psygp__scr* s,
                          double mu, double sd, double other) {
    double pm[PSYGP_MAX_OUTCOMES], pf[PSYGP_MAX_OUTCOMES];
    double eh = 0.0;
    int nout = 0;
    if (g->desc.lik == PSYGP_LIK_GAUSSIAN) {
        double nv = g->hyper.noise_sd * g->hyper.noise_sd;
        return nv > 0.0 ? 0.5 * log1p(sd * sd / nv) : 0.0;
    }
    if (!(sd > 0.0)) return 0.0;
    for (int j = 0; j < PSYGP_MAX_OUTCOMES; j++) pm[j] = 0.0;
    for (int i = 0; i < PSYGP_QUAD_N; i++) {
        double w = s->gh_w[i] / PSYGP__SQRTPI;
        nout = psygp__scen_probs(g, mu + PSYGP__SQRT2 * sd * s->gh_x[i], other, pf);
        for (int j = 0; j < nout; j++) pm[j] += w * pf[j];
        eh += w * psygp__entropy(pf, nout);
    }
    return psygp__entropy(pm, nout) - eh;
}

/* The outcomes a look-ahead has to average over, with their predictive
 * probabilities: the K outcomes of a discrete likelihood, or the quadrature
 * nodes of the predictive of y under GAUSSIAN. */
static int psygp__scenarios(const psygp_gp* g, const psygp__scr* s,
                            double mu, double sd, double other,
                            double* oy, double* op) {
    if (g->desc.lik == PSYGP_LIK_GAUSSIAN) {
        double nv = g->hyper.noise_sd * g->hyper.noise_sd;
        double t = sqrt(2.0 * (sd * sd + nv));
        for (int i = 0; i < PSYGP_QUAD_N; i++) {
            oy[i] = mu + t * s->gh_x[i];
            op[i] = s->gh_w[i] / PSYGP__SQRTPI;
        }
        return PSYGP_QUAD_N;
    } else {
        double pm[PSYGP_MAX_OUTCOMES], pf[PSYGP_MAX_OUTCOMES];
        int nout = psygp__scen_probs(g, mu, other, pf);
        for (int j = 0; j < nout; j++) pm[j] = 0.0;
        if (sd > 0.0) {
            for (int i = 0; i < PSYGP_QUAD_N; i++) {
                double w = s->gh_w[i] / PSYGP__SQRTPI;
                psygp__scen_probs(g, mu + PSYGP__SQRT2 * sd * s->gh_x[i], other, pf);
                for (int j = 0; j < nout; j++) pm[j] += w * pf[j];
            }
        } else {
            for (int j = 0; j < nout; j++) pm[j] = pf[j];
        }
        for (int j = 0; j < nout; j++) { oy[j] = (double)j; op[j] = pm[j]; }
        return nout;
    }
}

/* The new site's log-likelihood gradient and Hessian at the predictive mean,
 * which is the rank-one term of the look-ahead: W frozen means W is read here
 * and not recomputed by a Newton step. */
static void psygp__site(const psygp_gp* g, double mu, double yv, double other,
                        double* d1, double* w) {
    if (g->desc.lik == PSYGP_LIK_CATEGORICAL) {
        double pr = psygp__sigmoid(mu - other);
        *d1 = (yv > 0.5 ? 1.0 : 0.0) - pr;
        *w = pr * (1.0 - pr);
    } else if (g->desc.lik == PSYGP_LIK_GAUSSIAN) {
        double nv = g->hyper.noise_sd * g->hyper.noise_sd;
        if (!(nv > 0.0)) nv = 1e-8;
        *d1 = (yv - mu) / nv;
        *w = 1.0 / nv;
    } else {
        double lp, dd1, dd2, dd3;
        psygp__ll(g, mu, yv, &lp, &dd1, &dd2, &dd3);
        *d1 = dd1;
        *w = -dd2 > 0.0 ? -dd2 : 0.0;
    }
}

/* --- acquisition -------------------------------------------------------- */

/* The score of one latent at one point, for the acquisitions whose score needs
 * nothing but that point's predictive: the candidate sweep and the refinement
 * of desc.refine_steps both call it, so a refined point is judged by exactly
 * the arithmetic that picked the candidate it started from. */
static double psygp__score_one(const psygp_gp* g, const psygp__scr* s,
                               psygp_acq acq, double mu, double sd, double other,
                               double beta, double fstar) {
    switch (acq) {
        case PSYGP_ACQ_LSE:
            return beta * sd - fabs(mu - other - fstar);
        case PSYGP_ACQ_BALV:
            return psygp__q_var(g, s, mu, sd, other);
        case PSYGP_ACQ_BALD:
            return psygp__bald(g, s, mu, sd, other);
        case PSYGP_ACQ_LOCALMI: {
            double oy[PSYGP__NSCEN], op[PSYGP__NSCEN];
            double cjj = sd * sd, pi0, eh = 0.0, pp[2];
            int nsc;
            if (!(cjj > 0.0)) return 0.0;
            pi0 = psygp__Phi_fast((mu - other - fstar) / sd);
            pp[0] = 1.0 - pi0; pp[1] = pi0;
            nsc = psygp__scenarios(g, s, mu, sd, other, oy, op);
            for (int t = 0; t < nsc; t++) {
                double d1, w, den, mup, sdp, piy;
                if (op[t] <= 0.0) continue;
                psygp__site(g, mu, oy[t], other, &d1, &w);
                den = 1.0 + w * cjj;
                mup = mu + cjj * d1 / den;
                sdp = sqrt(cjj / den);
                piy = psygp__Phi_fast((mup - other - fstar) / sdp);
                pp[0] = 1.0 - piy; pp[1] = piy;
                eh += op[t] * psygp__entropy(pp, 2);
            }
            pp[0] = 1.0 - pi0; pp[1] = pi0;
            return psygp__entropy(pp, 2) - eh;
        }
        default:
            return 0.0;
    }
}

/* The acquisition at an arbitrary point, the objective desc.refine_steps
 * climbs. EAVC is a sum over the OTHER candidates and has no value off the
 * candidate set, so it is refined on the LSE straddle, the per-point score
 * that aims at the same level set. */
static double psygp__point_score(const psygp_gp* g, const psygp__scr* s,
                                 const double* x) {
    double mus[PSYGP_MAX_OUTCOMES], sds[PSYGP_MAX_OUTCOMES], total = 0.0;
    double beta = g->desc.acq_beta > 0.0 ? g->desc.acq_beta : 1.96;
    double fstar = psygp__f_star(g);
    psygp_acq acq = g->desc.acq == PSYGP_ACQ_EAVC ? PSYGP_ACQ_LSE : g->desc.acq;
    int K = g->K;
    for (int c = 0; c < K; c++) {
        double v;
        psygp__predict_k(g, s, x, c, &mus[c], &v, s->t1, s->t2);
        sds[c] = sqrt(v);
    }
    for (int c = 0; c < K; c++) {
        double other = 0.0;
        if (K > 1) {
            double fs[PSYGP_MAX_OUTCOMES] = { 0.0 };
            int nr = 0;
            for (int cc = 0; cc < K; cc++) if (cc != c) fs[nr++] = mus[cc];
            other = psygp__logsumexp(fs, nr);
        }
        total += psygp__score_one(g, s, acq, mus[c], sds[c], other, beta, fstar);
    }
    return total;
}

/* Golden-section iterations per dimension per round: the bracket of two
 * candidate spacings shrinks to 0.618^10 of itself, under 2% of one spacing,
 * which is finer than a display quantizes a stimulus. */
#define PSYGP__GOLDEN_ITERS 10

/* Half-width of the refinement bracket along each dimension: one grid step, or
 * for a Halton or caller-owned set the spacing M points would have on a
 * regular grid over the box. 0 for a dimension the grid holds fixed. */
static void psygp__refine_step(const psygp_gp* g, double* h) {
    const psygp_desc* d = &g->desc;
    int nd = d->n_dims, grid = 0;
    for (int k = 0; k < nd; k++) if (d->grid[k] != 0) grid = 1;
    for (int k = 0; k < nd; k++) {
        double span = d->hi[k] - d->lo[k];
        if (!d->candidates && grid)
            h[k] = d->grid[k] > 1 ? span / (double)(d->grid[k] - 1) : 0.0;
        else
            h[k] = span * pow((double)g->M, -1.0 / (double)nd);
    }
}

/* desc.refine_steps rounds of coordinate-wise golden-section search on the
 * point score, starting from the grid winner in x. Every round searches the
 * same box, the winner's neighbors along each dimension, and not a box around
 * wherever the last round ended: a bracket that followed the point would let
 * EAVC's straddle objective walk a look-ahead winner several cells to the
 * straddle's own optimum, which is a different acquisition. A move is taken
 * only when it scores strictly higher, so the result is never worse than the
 * candidate by the objective it is judged on. Returns true when x moved. */
static bool psygp__refine(const psygp_gp* g, const psygp__scr* s, double* x) {
    const psygp_desc* d = &g->desc;
    const double r = 0.61803398874989484820;
    double h[PSYGP_MAX_DIMS], x0[PSYGP_MAX_DIMS];
    double fx = psygp__point_score(g, s, x);
    bool moved = false;
    psygp__refine_step(g, h);
    memcpy(x0, x, (size_t)d->n_dims * sizeof(double));
    for (int round = 0; round < d->refine_steps; round++) {
        bool moved_round = false;
        for (int k = 0; k < d->n_dims; k++) {
            double a, b, c1, c2, f1, f2, keep = x[k];
            if (!(h[k] > 0.0)) continue;
            a = x0[k] - h[k] > d->lo[k] ? x0[k] - h[k] : d->lo[k];
            b = x0[k] + h[k] < d->hi[k] ? x0[k] + h[k] : d->hi[k];
            c1 = b - r * (b - a);
            c2 = a + r * (b - a);
            x[k] = c1; f1 = psygp__point_score(g, s, x);
            x[k] = c2; f2 = psygp__point_score(g, s, x);
            for (int it = 0; it < PSYGP__GOLDEN_ITERS; it++) {
                if (f1 >= f2) {
                    b = c2; c2 = c1; f2 = f1;
                    c1 = b - r * (b - a);
                    x[k] = c1; f1 = psygp__point_score(g, s, x);
                } else {
                    a = c1; c1 = c2; f1 = f2;
                    c2 = a + r * (b - a);
                    x[k] = c2; f2 = psygp__point_score(g, s, x);
                }
            }
            if (f1 >= f2 && f1 > fx) { x[k] = c1; fx = f1; moved_round = true; }
            else if (f2 > f1 && f2 > fx) { x[k] = c2; fx = f2; moved_round = true; }
            else x[k] = keep;
        }
        if (!moved_round) break;   /* a round that moved nothing has converged */
        moved = true;
    }
    return moved;
}

/* One latent's contribution to the acquisition score of every candidate. With
 * one latent that is the whole score; under CATEGORICAL the caller sums the K
 * one-against-the-rest contributions, which is the approximation the manual
 * states. */
static void psygp__acq_class(psygp_gp* g, const psygp__scr* s, int c) {
    int M = g->M, K = g->K;
    const double* mus = g->cand_mu + (size_t)c * M;
    const double* sds = s->cand_sd + (size_t)c * M;
    double beta = g->desc.acq_beta > 0.0 ? g->desc.acq_beta : 1.96;
    double fstar = psygp__f_star(g);
    psygp_acq acq = g->desc.acq;
    double V0 = 0.0;
    if (acq == PSYGP_ACQ_EAVC) {
        /* The level set's volume, and the per-candidate levels and variances
         * the look-ahead updates. The volume is the EXPECTED number of
         * candidates above the level, sum_i P(f_i > f*), not the number whose
         * posterior mean is above it: a count changes by whole candidates or
         * not at all, and once no candidate is near enough to flip, every
         * score is zero and the acquisition stops moving. */
        for (int j = 0; j < M; j++) {
            double other = 0.0;
            if (K > 1) {
                double fs[PSYGP_MAX_OUTCOMES] = { 0.0 };
                int nr = 0;
                for (int cc = 0; cc < K; cc++)
                    if (cc != c) fs[nr++] = g->cand_mu[(size_t)cc * M + j];
                other = psygp__logsumexp(fs, nr);
            }
            s->cand_t[j] = fstar + other;
            s->cand_dv[j] = g->cand_cov[(size_t)j * M + j];
            s->cand_isd[j] = s->cand_dv[j] > 0.0 ? 1.0 / sqrt(s->cand_dv[j]) : 0.0;
            s->cand_p0[j] = s->cand_dv[j] > 0.0
                  ? psygp__Phi_fast((mus[j] - s->cand_t[j]) * s->cand_isd[j])
                  : (mus[j] > s->cand_t[j] ? 1.0 : 0.0);
            V0 += s->cand_p0[j];
        }
    }
    for (int j = 0; j < M; j++) {
        double mu = mus[j], sd = sds[j], other = 0.0, sc = 0.0;
        if (K > 1) {
            double fs[PSYGP_MAX_OUTCOMES] = { 0.0 };
            int nr = 0;
            for (int cc = 0; cc < K; cc++)
                if (cc != c) fs[nr++] = g->cand_mu[(size_t)cc * M + j];
            other = psygp__logsumexp(fs, nr);
        }
        switch (acq) {
            case PSYGP_ACQ_LSE:
            case PSYGP_ACQ_BALV:
            case PSYGP_ACQ_BALD:
            case PSYGP_ACQ_LOCALMI:
                sc = psygp__score_one(g, s, acq, mu, sd, other, beta, fstar);
                break;
            case PSYGP_ACQ_EAVC: {
                double oy[PSYGP__NSCEN], op[PSYGP__NSCEN];
                const psygp_real* row = g->cand_cov + (size_t)j * M;
                double cjj = row[j];
                int nsc = psygp__scenarios(g, s, mu, sd, other, oy, op);
                /* The sum runs over the CHANGE from the baseline volume, so
                 * a candidate the new observation cannot move across the level
                 * contributes nothing and is skipped. A baseline probability of
                 * exactly 0 or 1 means that candidate is already more than
                 * eight standard deviations from the level, and the look-ahead
                 * only ever shrinks its variance, so a mean shift below six of
                 * them leaves it there. That test costs two comparisons and
                 * saves an exponential, a square root and a division. */
                for (int t = 0; t < nsc; t++) {
                    double d1, w, coef, wd, dV = 0.0;
                    if (op[t] <= 0.0) continue;
                    psygp__site(g, mu, oy[t], other, &d1, &w);
                    coef = d1 / (1.0 + w * cjj);
                    wd = w / (1.0 + w * cjj);
                    for (int i = 0; i < M; i++) {
                        double dij = row[i], shift = dij * coef, v, z;
                        double p0 = s->cand_p0[i];
                        if ((p0 == 0.0 || p0 == 1.0) &&
                            fabs(shift) * s->cand_isd[i] < 2.0) continue;
                        v = s->cand_dv[i] - dij * dij * wd;
                        if (v < 1e-12) v = 1e-12;
                        z = (mus[i] + shift - s->cand_t[i]) / sqrt(v);
                        dV += psygp__Phi_fast(z) - p0;
                    }
                    sc += op[t] * fabs(dV);
                }
                break;
            }
            default:
                sc = 0.0;
                break;
        }
        s->score[j] += sc;
    }
}

/* Score every candidate under desc.acq. */
static void psygp__acq_fill(psygp_gp* g) {
    psygp__scr s;
    int M = g->M, K = g->K;
    bool look = (g->desc.acq == PSYGP_ACQ_EAVC);
    psygp__scr_of(g, &s);
    for (int j = 0; j < M; j++) s.score[j] = 0.0;
    if (g->desc.acq == PSYGP_ACQ_RANDOM) { psygp__cand_fill(g, -1); return; }
    for (int c = 0; c < K; c++) {
        if (look) psygp__cand_cov_build(g, c);
        else if (c == 0) psygp__cand_fill(g, -1);
        psygp__acq_class(g, &s, c);
    }
    g->cand_valid = true;
}

/* --- candidate set ------------------------------------------------------ */

static void psygp__candidates_make(psygp_gp* g) {
    const psygp_desc* d = &g->desc;
    int nd = d->n_dims, M = g->M;
    int any = 0;
    if (d->candidates) return;
    for (int i = 0; i < nd; i++) if (d->grid[i] != 0) any = 1;
    if (any) {
        /* Product grid, last dimension fastest, endpoints included. */
        for (int j = 0; j < M; j++) {
            int rest = j;
            for (int i = nd - 1; i >= 0; i--) {
                int n = d->grid[i], k = rest % n;
                rest /= n;
                g->cand[(size_t)j * nd + i] = n == 1
                    ? 0.5 * (d->lo[i] + d->hi[i])
                    : d->lo[i] + (d->hi[i] - d->lo[i]) * (double)k / (double)(n - 1);
            }
        }
    } else {
        for (int j = 0; j < M; j++)
            for (int i = 0; i < nd; i++)
                g->cand[(size_t)j * nd + i] =
                    d->lo[i] + (d->hi[i] - d->lo[i]) *
                    psygp__radinv((unsigned)j + 1u, psygp__nth_prime(i));
    }
}

/* A point from the caller's generator, or the next Halton point. Both walk the
 * same counter so a run stays reproducible from the caller's seed. */
static void psygp__free_point(psygp_gp* g, double* x) {
    const psygp_desc* d = &g->desc;
    int nd = d->n_dims;
    if (d->rng) {
        for (int i = 0; i < nd; i++) {
            double u = psygp__clamp(d->rng(d->rng_ctx), 0.0, 1.0);
            x[i] = d->lo[i] + (d->hi[i] - d->lo[i]) * u;
        }
    } else {
        g->halton_index++;
        for (int i = 0; i < nd; i++)
            x[i] = d->lo[i] + (d->hi[i] - d->lo[i]) *
                   psygp__radinv((unsigned)g->halton_index, psygp__nth_prime(i));
    }
}

/* --- prediction helpers ------------------------------------------------- */

/* The latent the target quantity is read from: latent 0, or the target class
 * under CATEGORICAL, where `other` carries the log-sum-exp of the competing
 * class means. */
static void psygp__target_latent(const psygp_gp* g, const psygp__scr* s,
                                 const double* x, double* mu, double* sd,
                                 double* other, double* w1, double* w2) {
    if (g->K > 1) {
        double fs[PSYGP_MAX_OUTCOMES] = { 0.0 };
        int kt = g->desc.target_outcome, nr = 0;
        double m, v;
        for (int c = 0; c < g->K; c++) {
            psygp__predict_k(g, s, x, c, &m, &v, w1, w2);
            if (c == kt) { *mu = m; *sd = sqrt(v); }
            else fs[nr++] = m;
        }
        *other = psygp__logsumexp(fs, nr);
    } else {
        double m, v;
        psygp__predict_k(g, s, x, 0, &m, &v, w1, w2);
        *mu = m;
        *sd = sqrt(v);
        *other = 0.0;
    }
}

static bool psygp__in_box(const psygp_gp* g, const double* x) {
    const psygp_desc* d = &g->desc;
    for (int i = 0; i < d->n_dims; i++) {
        double tol = 1e-9 * (d->hi[i] - d->lo[i]);
        if (!(x[i] >= d->lo[i] - tol && x[i] <= d->hi[i] + tol)) return false;
    }
    return true;
}

/* --- threshold ---------------------------------------------------------- */

/* Points along the intensity dimension are compared on this many samples
 * before the bisection runs, so a second crossing is seen rather than bisected
 * away. The count follows the candidate spacing along the dimension. */
static int psygp__scan_n(const psygp_gp* g) {
    const psygp_desc* d = &g->desc;
    int id = d->intensity_dim, n;
    if (!d->candidates) {
        int any = 0;
        for (int i = 0; i < d->n_dims; i++) if (d->grid[i] != 0) any = 1;
        if (any && d->grid[id] > 1) return d->grid[id];
    }
    n = (int)ceil(pow((double)g->M, 1.0 / (double)d->n_dims));
    if (n < 9) n = 9;
    if (n > 65) n = 65;
    return n;
}

/* The target quantity along the intensity dimension, either averaged over the
 * latent posterior (nsd = 0, what the manual calls psygp_predict_p) or read off
 * the latent curve shifted by nsd standard deviations, which is what the band
 * edges are. */
static double psygp__curve(const psygp_gp* g, const psygp__scr* s, double* x,
                           double v, double nsd, double* w1, double* w2) {
    double mu, sd, other;
    x[g->desc.intensity_dim] = v;
    psygp__target_latent(g, s, x, &mu, &sd, &other, w1, w2);
    if (nsd == 0.0) return psygp__q_smooth(g, s, mu, sd, other);
    return psygp__q_of_f(g, mu + nsd * sd, other);
}

/* Scan, then bisect the first bracket. Returns false when the curve never
 * crosses inside the box; `cross` counts the brackets found. */
static bool psygp__cross(const psygp_gp* g, const psygp__scr* s, double* x,
                         double target, double nsd, double* out, int* cross,
                         double* w1, double* w2) {
    int id = g->desc.intensity_dim, ns = psygp__scan_n(g);
    double lo = g->desc.lo[id], hi = g->desc.hi[id];
    double a = lo, qa = psygp__curve(g, s, x, lo, nsd, w1, w2) - target;
    double fa = qa, fb = 0.0, b = lo;
    bool have = false;
    *cross = 0;
    for (int k = 1; k < ns; k++) {
        double v = lo + (hi - lo) * (double)k / (double)(ns - 1);
        double qv = psygp__curve(g, s, x, v, nsd, w1, w2) - target;
        if ((qa <= 0.0 && qv > 0.0) || (qa >= 0.0 && qv < 0.0)) {
            (*cross)++;
            if (!have) { a = lo + (hi - lo) * (double)(k - 1) / (double)(ns - 1);
                         b = v; fa = qa; fb = qv; have = true; }
        }
        qa = qv;
    }
    if (!have) return false;
    for (int it = 0; it < 40; it++) {
        double mid = 0.5 * (a + b);
        double fm = psygp__curve(g, s, x, mid, nsd, w1, w2) - target;
        if ((fa <= 0.0 && fm > 0.0) || (fa >= 0.0 && fm < 0.0)) { b = mid; fb = fm; }
        else { a = mid; fa = fm; }
    }
    (void)fb;
    *out = 0.5 * (a + b);
    return true;
}

/* --- public: lifecycle -------------------------------------------------- */

PSYGP_API const char* psygp_version(void) { return PSYGP_VERSION_STRING; }

PSYGP_API const char* psygp_strerror(int code) {
    switch (code) {
        case PSYGP_ERR_ARG:     return "bad argument";
        case PSYGP_ERR_CLOSED:  return "handle not open";
        case PSYGP_ERR_FULL:    return "max_trials recorded";
        case PSYGP_ERR_MEMORY:  return "buffer too small or allocation failed";
        case PSYGP_ERR_NOCROSS: return "no level-set crossing inside the box";
        case PSYGP_ERR_NUMERIC: return "Cholesky failed; posterior unchanged";
#ifdef PSYGP_ASYNC
        case PSYGP_ERR_BUSY:    return "queue is full";
        case PSYGP_ERR_TIMEOUT: return "timed out";
#endif
        default:                return code >= 0 ? "ok" : "unknown error";
    }
}

PSYGP_API const char* psygp_error(const psygp_gp* g) {
    return g ? g->error : "null gp handle";
}

PSYGP_API bool psygp_is_open(const psygp_gp* g) {
    return g && g->open;
}

PSYGP_API size_t psygp_memory_size(const psygp_desc* desc) {
    int Nmax, M, K;
    if (!psygp__validate(desc, NULL, 0, &Nmax, &M, &K)) return 0;
    /* The eight bytes are the alignment slack a caller's buffer may need. */
    return psygp__layout(desc, Nmax, M, K, NULL, NULL, NULL) + sizeof(double);
}

static void psygp__hyper_defaults(psygp_gp* g) {
    psygp_hyper* h = &g->hyper;
    const psygp_desc* d = &g->desc;
    *h = d->hyper;
    for (int i = 0; i < d->n_dims; i++) {
        double q = 0.25 * (d->hi[i] - d->lo[i]);
        if (h->lengthscale[i] == 0.0) h->lengthscale[i] = q;
        if (h->lengthscale_b[i] == 0.0) h->lengthscale_b[i] = q;
    }
    if (h->outputscale == 0.0) h->outputscale = 1.0;
    if (h->outputscale_b == 0.0) h->outputscale_b = 1.0;
    if (h->noise_sd == 0.0) h->noise_sd = 0.1;
    if (d->lik == PSYGP_LIK_ORDINAL && h->cutpoint[0] == 0.0)
        for (int m = 0; m < d->n_outcomes - 1; m++) h->cutpoint[m] = (double)m;
}

static int psygp__n_init(const psygp_gp* g) {
    return g->desc.n_init > 0 ? g->desc.n_init : 2 * g->desc.n_dims + 2;
}

PSYGP_API bool psygp_open(psygp_gp* g, const psygp_desc* desc) {
    int Nmax = 0, M = 0, K = 0;
    size_t need;
    unsigned char* base;
    psygp__scr s;
    if (!g) return false;
    memset(g, 0, sizeof(*g));
    if (!psygp__validate(desc, g->error, sizeof(g->error), &Nmax, &M, &K))
        return false;
    g->desc = *desc;
    g->N_max = Nmax;
    g->M = M;
    g->K = K;
    need = psygp__layout(desc, Nmax, M, K, NULL, NULL, NULL) + sizeof(double);
    if (desc->memory) {
        if (desc->memory_size < need) {
            psygp__err(g->error, sizeof(g->error),
                       "desc.memory_size is %lu, psygp_memory_size() needs %lu",
                       (unsigned long)desc->memory_size, (unsigned long)need);
            return false;
        }
        g->mem = desc->memory;
        g->mem_owned = false;
    } else {
        g->mem = PSYGP_MALLOC(need);
        if (!g->mem) {
            psygp__err(g->error, sizeof(g->error), "allocation of %lu bytes failed",
                       (unsigned long)need);
            return false;
        }
        g->mem_owned = true;
    }
    g->mem_size = need;
    /* Align the base, which is why psygp_memory_size() asks for the slack. */
    base = (unsigned char*)g->mem;
    {
        size_t rem = (size_t)((uintptr_t)base % sizeof(double));
        if (rem) base += sizeof(double) - rem;
    }
    psygp__layout(desc, Nmax, M, K, base, g, &s);
    memset(base, 0, need - (size_t)(base - (unsigned char*)g->mem));
    psygp__hyper_defaults(g);
    psygp__gh_init(s.gh_x, s.gh_w, PSYGP_QUAD_N);
    psygp__candidates_make(g);
    g->proposed = -1;
    g->last_index = -1;
    g->repeats = 0;
    g->N_fit = 0;
    g->fit_valid = true;
    g->cand_valid = false;
    g->stop = PSYGP_STOP_NONE;
    g->open = true;
    return true;
}

PSYGP_API void psygp_close(psygp_gp* g) {
    if (!g) return;
    if (g->mem && g->mem_owned) PSYGP_FREE(g->mem);
    memset(g, 0, sizeof(*g));
}

/* --- public: stopping --------------------------------------------------- */

static psygp_stop psygp__stop_reason(const psygp_gp* g) {
    const psygp_desc* d;
    if (!g || !g->open) return PSYGP_STOP_NONE;
    d = &g->desc;
    if (g->N >= g->N_max) return PSYGP_STOP_FULL;
    if (d->stop_trials > 0 && g->N >= d->stop_trials) return PSYGP_STOP_TRIALS;
    if (d->stop_threshold_sd > 0.0 && g->N > 0) {
        double x = 0.0, lo = 0.0, hi = 0.0;
        if (psygp_threshold(g, d->stop_context, 0.0, &x, &lo, &hi) == PSYGP_OK &&
            hi - lo < d->stop_threshold_sd)
            return PSYGP_STOP_THRESHOLD_SD;
    }
    return PSYGP_STOP_NONE;
}

PSYGP_API bool psygp_done(const psygp_gp* g) {
    return psygp__stop_reason(g) != PSYGP_STOP_NONE;
}

PSYGP_API psygp_stop psygp_stop_reason(const psygp_gp* g) {
    return psygp__stop_reason(g);
}

/* --- public: the loop --------------------------------------------------- */

static int psygp__next_impl(psygp_gp* g, const int* subset, int nsub, double* x) {
    psygp__scr s;
    int nd, n_init, best = -1, ties = 1;
    double bscore = 0.0;
    if (!g || !g->open) return PSYGP_ERR_CLOSED;
    if (!x) return PSYGP_ERR_CLOSED;
    if (g->N >= g->N_max) return PSYGP_ERR_FULL;
    nd = g->desc.n_dims;
    if (subset) {
        if (nsub <= 0) return PSYGP_ERR_CLOSED;
        for (int i = 0; i < nsub; i++)
            if (subset[i] < 0 || subset[i] >= g->M) return PSYGP_ERR_CLOSED;
    }
    /* A proposal that has not been answered yet is handed out again, so a
     * caller that asks twice shows the same stimulus. */
    if (g->history[g->N].proposed) {
        memcpy(x, g->history[g->N].x, (size_t)nd * sizeof(double));
        return g->proposed;
    }
    psygp__scr_of(g, &s);
    n_init = psygp__n_init(g);
    if ((!subset && g->N < n_init) ||
        (!subset && g->desc.acq == PSYGP_ACQ_RANDOM)) {
        psygp__free_point(g, x);
        g->proposed = -1;
    } else if (subset && g->desc.acq == PSYGP_ACQ_RANDOM) {
        double p[PSYGP_MAX_DIMS], bd = 0.0;
        psygp__free_point(g, p);
        for (int i = 0; i < nsub; i++) {
            const double* c = psygp__cand(g, subset[i]);
            double dd = 0.0;
            for (int k = 0; k < nd; k++) {
                double t = (c[k] - p[k]) / (g->desc.hi[k] - g->desc.lo[k]);
                dd += t * t;
            }
            if (best < 0 || dd < bd) { bd = dd; best = subset[i]; }
        }
        memcpy(x, psygp__cand(g, best), (size_t)nd * sizeof(double));
        g->proposed = best;
    } else {
        double ssum = 0.0;
        int nscored = 0;
        if (!g->cand_valid) psygp__acq_fill(g);
        for (int i = 0; i < (subset ? nsub : g->M); i++) {
            int j = subset ? subset[i] : i;
            double sc = s.score[j];
            ssum += sc;
            nscored++;
            if (best < 0 || sc > bscore + 1e-12 * (1.0 + fabs(bscore))) {
                best = j; bscore = sc; ties = 1;
            } else if (sc > bscore - 1e-12 * (1.0 + fabs(bscore))) {
                /* A tie: the lowest index wins, or the caller's generator
                 * picks, by reservoir so no list of ties is kept. */
                ties++;
                if (g->desc.rng && g->desc.rng(g->desc.rng_ctx) < 1.0 / (double)ties)
                    best = j;
            }
        }
        if (best < 0) return PSYGP_ERR_CLOSED;
        /* Two ways an acquisition stops saying anything, both of them real: a
         * posterior so diffuse that every candidate scores the same, and a
         * posterior so confident that one candidate wins forever. Either way
         * the run stops learning, so fall back to a Halton point, which is
         * where the init phase would have put the trial anyway. */
        if (!subset) {
            double mean = nscored > 0 ? ssum / (double)nscored : 0.0;
            bool flat = bscore - mean <= 1e-9 * (1.0 + fabs(mean));
            g->repeats = (best == g->last_index) ? g->repeats + 1 : 1;
            g->last_index = best;
            if (flat || g->repeats > PSYGP__REPEAT_MAX) {
                psygp__free_point(g, x);
                g->repeats = 0;
                g->last_index = -1;
                g->proposed = -1;
                memcpy(g->history[g->N].x, x, (size_t)nd * sizeof(double));
                g->history[g->N].proposed = 1;
                g->history[g->N].init = 0;
                return -1;
            }
        }
        memcpy(x, psygp__cand(g, best), (size_t)nd * sizeof(double));
        g->proposed = best;
        /* The repeat guard above counts grid winners, not refined points, so
         * a run that keeps refining around one candidate still trips it. */
        if (!subset && g->desc.refine_steps > 0 && psygp__refine(g, &s, x))
            g->proposed = -1;
    }
    memcpy(g->history[g->N].x, x, (size_t)nd * sizeof(double));
    g->history[g->N].proposed = 1;
    g->history[g->N].init = (uint8_t)((!subset && g->N < n_init) ? 1 : 0);
    return g->proposed;
}

PSYGP_API int psygp_next(psygp_gp* g, double* x) {
    return psygp__next_impl(g, NULL, 0, x);
}

PSYGP_API int psygp_next_subset(psygp_gp* g, const int* subset, int n, double* x) {
    if (!subset) return PSYGP_ERR_CLOSED;
    return psygp__next_impl(g, subset, n, x);
}

PSYGP_API double psygp_acq_score(const psygp_gp* g, int index) {
    psygp__scr s;
    psygp_gp* m = (psygp_gp*)g;   /* the cache is the handle's own memory */
    if (!g || !g->open || index < 0 || index >= g->M) return (double)NAN;
    if (g->desc.acq == PSYGP_ACQ_RANDOM || g->N < psygp__n_init(g))
        return (double)NAN;
    if (!g->cand_valid) psygp__acq_fill(m);
    psygp__scr_of(g, &s);
    return s.score[index];
}

static int psygp__record(psygp_gp* g, const double* x, double yv) {
    int n, nd, rc, n_init;
    if (!g || !g->open) return PSYGP_ERR_CLOSED;
    if (!x) return PSYGP_ERR_ARG;
    if (g->N >= g->N_max) return PSYGP_ERR_FULL;
    if (!psygp__in_box(g, x)) return PSYGP_ERR_ARG;
    nd = g->desc.n_dims;
    n = g->N;
    n_init = psygp__n_init(g);
    {
        psygp_trial* t = &g->history[n];
        bool same = t->proposed != 0;
        for (int i = 0; i < nd && same; i++) if (t->x[i] != x[i]) same = false;
        memcpy(t->x, x, (size_t)nd * sizeof(double));
        t->y = yv;
        t->proposed = (uint8_t)(same ? 1 : 0);
        t->init = (uint8_t)(n < n_init ? 1 : 0);
    }
    memcpy(g->X + (size_t)n * nd, x, (size_t)nd * sizeof(double));
    g->y[n] = yv;
    g->N = n + 1;
    g->proposed = -1;
    g->cand_valid = false;
    if (g->N < PSYGP_MAX_TRIALS) memset(&g->history[g->N], 0, sizeof(psygp_trial));
    psygp__kmat_row(g, n);
    g->fit_state = PSYGP__FIT_IDLE;   /* a stepped fit was fitting other data */
    /* The cheap path when desc.refit_every says so, with the exact refit as
     * the fallback if the bordered Cholesky will not factor. */
    if (psygp__can_grow(g) && (g->N % g->desc.refit_every) != 0)
        rc = psygp__laplace_grow(g);
    else
        rc = psygp__infer(g, PSYGP__START_ROW, n);
    if (rc != PSYGP_OK && psygp__can_grow(g))
        rc = psygp__infer(g, PSYGP__START_ROW, n);
    if (rc != PSYGP_OK) {
        /* Put the posterior of the trials that did factor back in force. The
         * leading block of the kernel matrix is untouched by the new row, so
         * refitting at the old count reproduces it. */
        int saved = g->N;
        g->N = g->N_fit;
        if (g->N > 0) {
            if (psygp__infer(g, PSYGP__START_COLD, 0) != PSYGP_OK) g->fit_valid = false;
        } else {
            g->fit_valid = true;
            g->log_marginal = 0.0;
        }
        g->N = saved;
        g->stop = psygp__stop_reason(g);
        return PSYGP_ERR_NUMERIC;
    }
    if (g->desc.fit && g->desc.fit_every > 0 && g->N >= n_init &&
        (g->N - n_init) % g->desc.fit_every == 0) {
        rc = psygp__fit_run(g);
        if (rc != PSYGP_OK) { g->stop = psygp__stop_reason(g); return rc; }
    }
    g->stop = psygp__stop_reason(g);
    return PSYGP_OK;
}

PSYGP_API int psygp_update(psygp_gp* g, const double* x, int outcome) {
    int nout;
    if (!g || !g->open) return PSYGP_ERR_CLOSED;
    if (g->desc.lik == PSYGP_LIK_GAUSSIAN) return PSYGP_ERR_ARG;
    nout = g->desc.lik == PSYGP_LIK_BERNOULLI ? 2 : g->desc.n_outcomes;
    if (outcome < 0 || outcome >= nout) return PSYGP_ERR_ARG;
    return psygp__record(g, x, (double)outcome);
}

PSYGP_API int psygp_update_real(psygp_gp* g, const double* x, double y) {
    if (!g || !g->open) return PSYGP_ERR_CLOSED;
    if (g->desc.lik != PSYGP_LIK_GAUSSIAN) return PSYGP_ERR_ARG;
    if (!(y == y)) return PSYGP_ERR_ARG;
    return psygp__record(g, x, y);
}

/* --- public: estimates -------------------------------------------------- */

PSYGP_API int psygp_predict_f(const psygp_gp* g, const double* x, int k,
                              double* mu, double* sd) {
    psygp__scr s;
    double m, v;
    if (!g || !g->open) return PSYGP_ERR_CLOSED;
    if (!x || k < 0 || k >= g->K || !psygp__in_box(g, x)) return PSYGP_ERR_ARG;
    if (!g->fit_valid) return PSYGP_ERR_NUMERIC;
    psygp__scr_of(g, &s);
    psygp__predict_k(g, &s, x, k, &m, &v, s.t1, s.t2);
    if (mu) *mu = m;
    if (sd) *sd = sqrt(v);
    return PSYGP_OK;
}

PSYGP_API double psygp_predict_p(const psygp_gp* g, const double* x) {
    psygp__scr s;
    double mu, sd, other;
    if (!g || !g->open || !x || !psygp__in_box(g, x) || !g->fit_valid)
        return (double)NAN;
    psygp__scr_of(g, &s);
    psygp__target_latent(g, &s, x, &mu, &sd, &other, s.t1, s.t2);
    return psygp__q_smooth(g, &s, mu, sd, other);
}

PSYGP_API int psygp_predict_f_many(const psygp_gp* g, const double* xs, int n,
                                   int k, double* mu, double* sd) {
    psygp__scr s;
    int nd;
    if (!g || !g->open) return PSYGP_ERR_CLOSED;
    if (!xs || n < 0 || k < 0 || k >= g->K) return PSYGP_ERR_ARG;
    if (!g->fit_valid) return PSYGP_ERR_NUMERIC;
    nd = g->desc.n_dims;
    for (int i = 0; i < n; i++)
        if (!psygp__in_box(g, xs + (size_t)i * nd)) return PSYGP_ERR_ARG;
    psygp__scr_of(g, &s);
    /* A chunk at a time, so an output the caller did not ask for goes to the
     * stack and nothing borrows the candidate cache. */
    for (int b0 = 0; b0 < n; b0 += PSYGP__BLK) {
        int cnt = n - b0 < PSYGP__BLK ? n - b0 : PSYGP__BLK;
        double mub[PSYGP__BLK], sdb[PSYGP__BLK];
        psygp__predict_many(g, &s, xs + (size_t)b0 * nd, cnt, k, mub, sdb);
        if (mu) memcpy(mu + b0, mub, (size_t)cnt * sizeof(double));
        if (sd) memcpy(sd + b0, sdb, (size_t)cnt * sizeof(double));
    }
    return PSYGP_OK;
}

PSYGP_API int psygp_predict_p_many(const psygp_gp* g, const double* xs, int n,
                                   double* p) {
    psygp__scr s;
    int nd, rc;
    if (!g || !g->open) return PSYGP_ERR_CLOSED;
    if (!xs || !p || n < 0) return PSYGP_ERR_ARG;
    if (!g->fit_valid) return PSYGP_ERR_NUMERIC;
    nd = g->desc.n_dims;
    if (g->K > 1) {
        /* K coupled latents: one point at a time, since the target quantity of
         * a class needs the other classes at the same point. */
        for (int i = 0; i < n; i++) {
            p[i] = psygp_predict_p(g, xs + (size_t)i * nd);
            if (!(p[i] == p[i])) return PSYGP_ERR_ARG;
        }
        return PSYGP_OK;
    }
    psygp__scr_of(g, &s);
    for (int b0 = 0; b0 < n; b0 += PSYGP__BLK) {
        int cnt = n - b0 < PSYGP__BLK ? n - b0 : PSYGP__BLK;
        double mub[PSYGP__BLK], sdb[PSYGP__BLK];
        rc = psygp_predict_f_many(g, xs + (size_t)b0 * nd, cnt, 0, mub, sdb);
        if (rc != PSYGP_OK) return rc;
        for (int i = 0; i < cnt; i++)
            p[b0 + i] = psygp__q_smooth(g, &s, mub[i], sdb[i], 0.0);
    }
    return PSYGP_OK;
}

PSYGP_API double psygp_predict_p_var(const psygp_gp* g, const double* x) {
    psygp__scr s;
    double mu, sd, other;
    if (!g || !g->open || !x || !psygp__in_box(g, x) || !g->fit_valid)
        return (double)NAN;
    psygp__scr_of(g, &s);
    psygp__target_latent(g, &s, x, &mu, &sd, &other, s.t1, s.t2);
    return psygp__q_var(g, &s, mu, sd, other);
}

PSYGP_API int psygp_predict_outcomes(const psygp_gp* g, const double* x, double* p) {
    psygp__scr s;
    if (!g || !g->open) return PSYGP_ERR_CLOSED;
    if (!x || !p || !psygp__in_box(g, x)) return PSYGP_ERR_ARG;
    if (g->desc.lik == PSYGP_LIK_GAUSSIAN) return PSYGP_ERR_ARG;
    if (!g->fit_valid) return PSYGP_ERR_NUMERIC;
    psygp__scr_of(g, &s);
    if (g->K > 1) {
        /* One-latent quadrature per class, then normalized: the exact softmax
         * expectation is a K-dimensional integral. */
        double mu[PSYGP_MAX_OUTCOMES], sd[PSYGP_MAX_OUTCOMES], tot = 0.0;
        for (int c = 0; c < g->K; c++) {
            double m, v;
            psygp__predict_k(g, &s, x, c, &m, &v, s.t1, s.t2);
            mu[c] = m;
            sd[c] = sqrt(v);
        }
        for (int c = 0; c < g->K; c++) {
            double fs[PSYGP_MAX_OUTCOMES] = { 0.0 }, acc = 0.0, other;
            int nr = 0;
            for (int cc = 0; cc < g->K; cc++) if (cc != c) fs[nr++] = mu[cc];
            other = psygp__logsumexp(fs, nr);
            for (int i = 0; i < PSYGP_QUAD_N; i++)
                acc += s.gh_w[i] * psygp__sigmoid(mu[c] +
                       PSYGP__SQRT2 * sd[c] * s.gh_x[i] - other);
            p[c] = acc / PSYGP__SQRTPI;
            tot += p[c];
        }
        if (tot > 0.0) for (int c = 0; c < g->K; c++) p[c] /= tot;
    } else {
        double m, v, sd;
        int nout = g->desc.lik == PSYGP_LIK_BERNOULLI ? 2 : g->desc.n_outcomes;
        double pf[PSYGP_MAX_OUTCOMES];
        psygp__predict_k(g, &s, x, 0, &m, &v, s.t1, s.t2);
        sd = sqrt(v);
        for (int j = 0; j < nout; j++) p[j] = 0.0;
        if (sd > 0.0) {
            for (int i = 0; i < PSYGP_QUAD_N; i++) {
                double w = s.gh_w[i] / PSYGP__SQRTPI;
                psygp__probs_of_f(g, m + PSYGP__SQRT2 * sd * s.gh_x[i], pf);
                for (int j = 0; j < nout; j++) p[j] += w * pf[j];
            }
        } else {
            psygp__probs_of_f(g, m, p);
        }
    }
    return PSYGP_OK;
}

PSYGP_API int psygp_threshold(const psygp_gp* g, const double* ctx, double target,
                              double* x, double* lo, double* hi) {
    psygp__scr s;
    psygp_gp* mg = (psygp_gp*)g;
    double pt[PSYGP_MAX_DIMS];
    double v, a, b;
    int nd, id, cross = 0, ca = 0, cb = 0;
    bool ha, hb;
    if (!g || !g->open) return PSYGP_ERR_CLOSED;
    if (!g->fit_valid) return PSYGP_ERR_NUMERIC;
    nd = g->desc.n_dims;
    id = g->desc.intensity_dim;
    if (nd > 1 && !ctx) return PSYGP_ERR_ARG;
    if (target == 0.0)
        target = g->desc.lik == PSYGP_LIK_GAUSSIAN ? g->desc.target_value
                                                   : g->desc.target_p;
    if (g->desc.lik != PSYGP_LIK_GAUSSIAN &&
        !(target > g->desc.guess && target < 1.0 - g->desc.lapse))
        return PSYGP_ERR_ARG;   /* no such level exists with this floor */
    for (int i = 0, k = 0; i < nd; i++) {
        if (i == id) { pt[i] = g->desc.lo[i]; continue; }
        pt[i] = ctx[k++];
    }
    for (int i = 0; i < nd; i++) {
        if (i == id) continue;
        if (!(pt[i] >= g->desc.lo[i] && pt[i] <= g->desc.hi[i])) return PSYGP_ERR_ARG;
    }
    psygp__scr_of(g, &s);
    if (!psygp__cross(g, &s, pt, target, 0.0, &v, &cross, s.t1, s.t2)) {
        mg->multi_cross = false;
        return PSYGP_ERR_NOCROSS;
    }
    mg->multi_cross = (cross > 1);
    if (x) *x = v;
    if (lo || hi) {
        /* The band edges are where the latent curve shifted by +-1.96 sd meets
         * the same level. Which shift gives the lower intensity depends on the
         * sign of the slope, so they are sorted rather than assumed. */
        ha = psygp__cross(g, &s, pt, target, 1.96, &a, &ca, s.t1, s.t2);
        hb = psygp__cross(g, &s, pt, target, -1.96, &b, &cb, s.t1, s.t2);
        if (!ha) a = (b > v) ? g->desc.lo[id] : g->desc.hi[id];
        if (!hb) b = (a > v) ? g->desc.lo[id] : g->desc.hi[id];
        if (lo) *lo = a < b ? a : b;
        if (hi) *hi = a < b ? b : a;
    }
    return PSYGP_OK;
}

PSYGP_API bool psygp_threshold_multi_cross(const psygp_gp* g) {
    return g && g->open && g->multi_cross;
}

/* --- public: hyperparameters -------------------------------------------- */

PSYGP_API int psygp_fit(psygp_gp* g) {
    if (!g || !g->open) return PSYGP_ERR_CLOSED;
    if (g->N < 2) return PSYGP_OK;
    if (!g->fit_valid) return PSYGP_ERR_NUMERIC;
    return psygp__fit_run(g);
}

PSYGP_API int psygp_refit(psygp_gp* g) {
    if (!g || !g->open) return PSYGP_ERR_CLOSED;
    if (g->N == 0) return PSYGP_OK;
    return psygp__infer(g, PSYGP__START_KEEP, 0);
}

PSYGP_API int psygp_get_hyper(const psygp_gp* g, psygp_hyper* out) {
    if (!g || !g->open) return PSYGP_ERR_CLOSED;
    if (!out) return PSYGP_ERR_ARG;
    *out = g->hyper;
    return PSYGP_OK;
}

PSYGP_API double psygp_log_marginal(const psygp_gp* g) {
    if (!g || !g->open || !g->fit_valid) return (double)NAN;
    return g->log_marginal;
}

/* --- public: candidates and history ------------------------------------- */

PSYGP_API int psygp_n_candidates(const psygp_gp* g) {
    if (!g || !g->open) return PSYGP_ERR_CLOSED;
    return g->M;
}

PSYGP_API int psygp_candidate(const psygp_gp* g, int index, double* x) {
    if (!g || !g->open) return PSYGP_ERR_CLOSED;
    if (!x || index < 0 || index >= g->M) return PSYGP_ERR_ARG;
    memcpy(x, psygp__cand(g, index), (size_t)g->desc.n_dims * sizeof(double));
    return PSYGP_OK;
}

PSYGP_API int psygp_n_trials(const psygp_gp* g) {
    if (!g || !g->open) return PSYGP_ERR_CLOSED;
    return g->N;
}

PSYGP_API const psygp_trial* psygp_history(const psygp_gp* g, int* n) {
    if (!g || !g->open) { if (n) *n = 0; return NULL; }
    if (n) *n = g->N;
    return g->history;
}

/* --- public: simulation ------------------------------------------------- */

PSYGP_API int psygp_simulate_outcome(const double* p, int K, double u) {
    double acc = 0.0;
    if (!p || K < 1) return PSYGP_ERR_ARG;
    for (int k = 0; k < K; k++) {
        acc += p[k];
        if (u < acc) return k;
    }
    return K - 1;
}

/* ======================================================================= *
 *  ASYNC
 *
 *  A psyrt_pump with this header in its on_msg. Everything here is plumbing:
 *  the inference is the same psygp_update(), psygp_next() and
 *  psygp_threshold() a synchronous caller runs, on the same handle, in the
 *  same order, so an async run and a synchronous replay of the same responses
 *  agree bit for bit. What the layer adds is that the frame loop does not wait
 *  for it, and that the hyperparameter fit gets the gaps between trials.
 * ======================================================================= */
#ifdef PSYGP_ASYNC

/* Compute the summaries first, take the publish lock only to copy them in.
 * That is psy_rt.h's PUMP discipline: a caller may hold that lock as long as
 * it likes without stalling the inference, because the inference never holds
 * it. */
static void psygp__async_publish(psygp_async* a, uint32_t seq, int update_rc) {
    psygp_snapshot s;
    psygp_gp* g = a->gp;
    const double* ctx = g->desc.n_dims > 1 ? a->context : NULL;
    memset(&s, 0, sizeof(s));
    s.seq = seq;
    s.update_rc = update_rc;
    s.proposed = psygp_next(g, s.x);
    s.n_trials = psygp_n_trials(g);
    s.done = psygp_done(g);
    s.stop = psygp_stop_reason(g);
    s.threshold_rc = psygp_threshold(g, ctx, a->target, &s.threshold,
                                     &s.threshold_lo, &s.threshold_hi);
    s.multi_cross = psygp_threshold_multi_cross(g);
    (void)psygp_get_hyper(g, &s.hyper);
    s.log_marginal = psygp_log_marginal(g);
    s.numeric = a->numeric;
    s.fitting = a->fit_in_idle && a->fit_active;
    psyrt_pump_lock(&a->pump);
    a->snap = s;
    a->published = true;
    psyrt_pump_unlock(&a->pump);
}

/* One response, on the pump thread, in submit order. */
static void psygp__async_on_msg(void* ctx, const void* msg, uint32_t seq) {
    psygp_async* a = (psygp_async*)ctx;
    const psygp_async_msg* m = (const psygp_async_msg*)msg;
    int rc = m->real ? psygp_update_real(a->gp, m->x, m->y)
                     : psygp_update(a->gp, m->x, (int)(m->y + 0.5));
    if (rc == PSYGP_ERR_NUMERIC) a->numeric++;
    /* A new trial is new information, so the fit has something to do again.
     * psygp_update() has already thrown away the stepping state for the same
     * reason. */
    if (a->fit_in_idle) a->fit_active = true;
    psygp__async_publish(a, seq, rc);
}

/* One step of the hyperparameter fit, on the pump thread, whenever the queue
 * is empty. The fit's own hyperparameters and log marginal are republished
 * (the seq and the proposal are NOT: the proposal is recomputed when the next
 * response lands, which is the only moment the seq contract talks about). */
static bool psygp__async_on_idle(void* ctx) {
    psygp_async* a = (psygp_async*)ctx;
    int rc;
    if (!a->fit_active) return false;
    rc = psygp_fit_step(a->gp);
    if (rc <= 0) a->fit_active = false;
    psyrt_pump_lock(&a->pump);
    (void)psygp_get_hyper(a->gp, &a->snap.hyper);
    a->snap.log_marginal = psygp_log_marginal(a->gp);
    a->snap.fitting = a->fit_active;
    psyrt_pump_unlock(&a->pump);
    return a->fit_active;
}

/* psy_rt.h's codes in this header's vocabulary. */
static int psygp__async_rc(int rt_rc) {
    switch (rt_rc) {
    case PSYRT_ERR_FULL:    return PSYGP_ERR_BUSY;
    case PSYRT_ERR_TIMEOUT: return PSYGP_ERR_TIMEOUT;
    case PSYRT_ERR_STOPPED: return PSYGP_ERR_CLOSED;
    case PSYRT_ERR_ARG:     return PSYGP_ERR_ARG;
    default:                return (rt_rc < 0) ? PSYGP_ERR_ARG : rt_rc;
    }
}

PSYGP_API bool psygp_async_start(psygp_async* a, const psygp_async_desc* desc) {
    psyrt_pump_desc pd;
    if (!a) return false;
    memset(a->error, 0, sizeof(a->error));
    a->running = false;
    a->published = false;
    a->numeric = 0;
    a->fit_active = false;
    if (!desc || !desc->gp) {
        snprintf(a->error, sizeof(a->error),
                 "psy_gp: psygp_async_start needs desc.gp");
        return false;
    }
    if (!psygp_is_open(desc->gp)) {
        snprintf(a->error, sizeof(a->error), "psy_gp: desc.gp is not open");
        return false;
    }
    {   /* The context has to be inside the box, or every snapshot would carry
         * the same PSYGP_ERR_ARG and nobody would look at it twice. */
        const psygp_desc* d = &desc->gp->desc;
        for (int i = 0, k = 0; i < d->n_dims; i++) {
            if (i == d->intensity_dim) continue;
            if (!(desc->context[k] >= d->lo[i] && desc->context[k] <= d->hi[i])) {
                snprintf(a->error, sizeof(a->error),
                         "psy_gp: desc.context[%d] = %g is outside [%g, %g]",
                         k, desc->context[k], d->lo[i], d->hi[i]);
                return false;
            }
            k++;
        }
    }
    a->gp = desc->gp;
    a->target = desc->target;
    a->fit_in_idle = desc->fit_in_idle;
    memcpy(a->context, desc->context, sizeof(a->context));
    memset(&a->snap, 0, sizeof(a->snap));
    /* fit_in_idle and fit_every are two answers to the same question, and
     * running both would fit twice. The caller's value is parked here and put
     * back by stop(). */
    a->fit_every = a->gp->desc.fit_every;
    if (a->fit_in_idle) a->gp->desc.fit_every = 0;

    /* The first proposal, before the thread exists, so a poll() right after a
     * successful start always has something to return and the thread can never
     * publish seq 1 ahead of it. The pump's lock is a no-op while the pump is
     * not running, which is what makes this reuse of the publish path safe. */
    psygp__async_publish(a, 0, PSYGP_OK);
    if (a->fit_in_idle) a->fit_active = true;

    memset(&pd, 0, sizeof(pd));
    pd.msg_size = sizeof(psygp_async_msg);
    pd.capacity = (uint32_t)PSYGP_ASYNC_QUEUE;
    pd.ring = a->ring;
    pd.on_msg = psygp__async_on_msg;
    pd.on_idle = desc->fit_in_idle ? psygp__async_on_idle : NULL;
    pd.ctx = a;
    pd.below_normal = desc->below_normal;
    pd.pin_cpu = desc->pin_cpu;
    if (!psyrt_pump_start(&a->pump, &pd)) {
        snprintf(a->error, sizeof(a->error), "psy_gp: %.200s",
                 psyrt_pump_error(&a->pump));
        a->gp->desc.fit_every = a->fit_every;
        a->published = false;
        return false;
    }
    a->running = true;
    return true;
}

PSYGP_API void psygp_async_stop(psygp_async* a) {
    if (!a) return;
    psyrt_pump_stop(&a->pump);   /* drains, then joins */
    if (a->running && a->gp && a->fit_in_idle) a->gp->desc.fit_every = a->fit_every;
    a->running = false;
}

static int psygp__async_send(psygp_async* a, const psygp_async_msg* m) {
    int rc = psyrt_pump_submit(&a->pump, m);
    return (rc < 0) ? psygp__async_rc(rc) : rc;
}

/* The checks the synchronous psygp_update() would make, made here instead:
 * on the thread the only thing to do with a bad argument is to drop it
 * silently. Nothing read here changes after psygp_open(). */
static int psygp__async_fill(psygp_async* a, psygp_async_msg* m,
                             const double* x, double y, int real) {
    const psygp_desc* d;
    if (!a || !a->gp || !x) return PSYGP_ERR_ARG;
    if (!a->running) return PSYGP_ERR_CLOSED;
    d = &a->gp->desc;
    if (real != ((d->lik == PSYGP_LIK_GAUSSIAN) ? 1 : 0)) return PSYGP_ERR_ARG;
    if (!(y == y)) return PSYGP_ERR_ARG;
    for (int i = 0; i < PSYGP_MAX_DIMS; i++) m->x[i] = 0.0;
    for (int i = 0; i < d->n_dims; i++) {
        double tol = 1e-9 * (d->hi[i] - d->lo[i]);
        if (!(x[i] >= d->lo[i] - tol && x[i] <= d->hi[i] + tol))
            return PSYGP_ERR_ARG;
        m->x[i] = x[i];
    }
    m->y = y;
    m->real = real;
    return PSYGP_OK;
}

PSYGP_API int psygp_async_submit(psygp_async* a, const double* x, int outcome) {
    psygp_async_msg m;
    int rc, nout;
    if (!a || !a->gp) return PSYGP_ERR_ARG;
    nout = a->gp->desc.lik == PSYGP_LIK_BERNOULLI ? 2 : a->gp->desc.n_outcomes;
    if (outcome < 0 || outcome >= nout) return PSYGP_ERR_ARG;
    rc = psygp__async_fill(a, &m, x, (double)outcome, 0);
    if (rc != PSYGP_OK) return rc;
    return psygp__async_send(a, &m);
}

PSYGP_API int psygp_async_submit_real(psygp_async* a, const double* x, double y) {
    psygp_async_msg m;
    int rc = psygp__async_fill(a, &m, x, y, 1);
    if (rc != PSYGP_OK) return rc;
    return psygp__async_send(a, &m);
}

PSYGP_API int psygp_async_poll(const psygp_async* a, psygp_snapshot* out) {
    psygp_async* m = (psygp_async*)a;   /* the lock and the copy are not const */
    int seq;
    if (!a) return PSYGP_ERR_ARG;
    if (!a->running && !a->published) return PSYGP_ERR_CLOSED;
    psyrt_pump_lock(&m->pump);
    seq = (int)m->snap.seq;
    if (out) *out = m->snap;
    psyrt_pump_unlock(&m->pump);
    return seq;
}

PSYGP_API int psygp_async_wait(psygp_async* a, uint32_t seq, uint64_t timeout_ns,
                               psygp_snapshot* out) {
    int rc;
    if (!a) return PSYGP_ERR_ARG;
    /* Not gated on a->running: a seq the thread DID reach before a stop still
     * answers, which is psy_rt.h's rule for psyrt_pump_wait and the reason a
     * caller can wait on the last response it submitted after stopping. */
    rc = psyrt_pump_wait(&a->pump, seq, timeout_ns);
    if (rc < 0) return psygp__async_rc(rc);
    return psygp_async_poll(a, out);
}

PSYGP_API int psygp_async_pending(const psygp_async* a) {
    int rc;
    if (!a) return PSYGP_ERR_ARG;
    if (!a->running) return PSYGP_ERR_CLOSED;
    rc = psyrt_pump_pending(&a->pump);
    return (rc < 0) ? psygp__async_rc(rc) : rc;
}

PSYGP_API const char* psygp_async_error(const psygp_async* a) {
    return a ? a->error : "";
}

PSYGP_API bool psygp_async_is_running(const psygp_async* a) {
    return a && a->running && psyrt_pump_is_running(&a->pump);
}

PSYGP_API psyrt_policy psygp_async_policy(const psygp_async* a) {
    return a ? psyrt_pump_policy(&a->pump) : PSYRT_POLICY_NONE;
}

#endif /* PSYGP_ASYNC */

#endif /* PSY_GP_IMPLEMENTATION_GUARD */
#endif /* PSY_GP_IMPLEMENTATION */

/* ------------------------------------------------------------------------
 * This software is available under the MIT-0 (MIT No Attribution) license.
 *
 * Copyright (c) 2026 psy contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY.
 * ------------------------------------------------------------------------ */
