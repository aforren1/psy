/* psy_quest.h - v0.5.2 - public domain single-header Bayesian adaptive library
 *
 *   Parametric Bayesian adaptive estimation on a grid: QUEST+ (Watson 2017),
 *   which contains QUEST (Watson & Pelli 1983), the Psi method (Kontsevich
 *   & Tyler 1999) and Psi-marginal (Prins 2013) as configurations. Any
 *   number of stimulus dimensions, any number of parameters, any number of
 *   outcomes, a built-in catalog of psychometric functions and a callback
 *   for the rest (a CSF model, a matching task, a three-alternative task).
 *
 *   Written in the single-header style of the stb / sokol libraries. Pure
 *   computation: no OS calls, no threads. One allocation at open, none
 *   after. Needs nothing but libm, and includes nothing but the C standard
 *   library. Define PSYQ_ASYNC and it also gets an opt-in layer that runs the
 *   inference on a background thread, which is the one thing here that needs
 *   psy_rt.h and an OS; see ASYNC.
 *
 *   Targets every platform the compiler does. C++17, C11, or the pre-C11 C
 *   dialect MSVC compiles with by default.
 *
 *   ---------------------------------------------------------------------
 *   CHANGELOG
 *   ---------------------------------------------------------------------
 *   v0.5.2 - documentation only: STATUS records the comparisons against
 *          mQUESTPlus, Palamedes' PAL_AMPM and Watson's own notebook runs,
 *          which are now done. No code changed.
 *   v0.5.1 - builds clean on gcc 16 (MinGW, -O3) and current clang, which
 *          CI found and this machine's compilers do not. No behavior change,
 *          and the default build's arithmetic is bit for bit v0.5.0. gcc 16
 *          constant-propagated a test's deliberately bad arguments (axis 9,
 *          a three-element parameter array for a three-parameter model) into
 *          paths that open()'s invariants make impossible, and with
 *          -Werror=aggressive-loop-optimizations and -Werror=array-bounds it
 *          refused them. Neither was an over-read at run time. The header now
 *          makes the invariants visible instead of assuming them: every axis a
 *          caller passes is checked against the array it will index as well as
 *          against the handle, every loop over the stimulus or parameter axes
 *          is bounded by the array it indexes, and every array a caller hands
 *          in or gets back is copied once, exactly n_stim / n_param / K
 *          elements, to or from a local of the compile-time maximum, so no
 *          caller's array is ever indexed past what the handle says it holds on
 *          any path an optimizer can build. The test initializes every array
 *          a header call fills and fills its deliberately bad desc with 0xA5
 *          instead of leaving it uninitialized.
 *   v0.5.0 - snapshots: psyq_save_size(), psyq_save() and psyq_load(), in the
 *          shape of psy_trials.h's psytr_save / psytr_load. A session saved
 *          at any trial resumes without replay and continues bit for bit as
 *          the uninterrupted one would have, which the test checks at four
 *          cut points in five configurations. And psyq_async_desc.queue_depth:
 *          how far the trial loop may run ahead of the inference is now a
 *          session's choice, under PSYQ_ASYNC_QUEUE as the capacity, where it
 *          used to be the macro itself. That was the one compile-time knob a
 *          binding could not reach; see BUILDING for the ones that stay.
 *   v0.4.1 - two small things a binding asked for: version macros
 *          (PSYQ_VERSION_MAJOR / MINOR / PATCH / STRING) and psyq_version(),
 *          so a program can log which header it was built from, and
 *          psyq_stim_axis_n() / psyq_param_axis_n(), so a caller can read an
 *          axis's size back from the handle instead of keeping its own copy
 *          of the desc. No behavior changed.
 *   v0.4 - an opt-in async layer, compiled only under PSYQ_ASYNC: psyq_async,
 *          a background thread that owns a psyq_quest between start and stop,
 *          takes responses through a fixed-size queue and publishes a snapshot
 *          (the next stimulus, an estimate, the entropy, the stop state) that
 *          the frame loop polls or waits on. It is psy_rt.h's psyrt_pump with
 *          psyq_update() and psyq_next() in its on_msg, so an async run and a
 *          synchronous replay of the same responses agree bit for bit, which
 *          the test asserts. Without PSYQ_ASYNC the header is unchanged, still
 *          includes nothing but the C standard library, and produces the same
 *          numbers bit for bit; examples/quest_async.c is a 16 ms frame loop
 *          driving it. See ASYNC and docs/psy_adapt.md, "Inference on a
 *          thread".
 *   v0.3.1 - no code in the header changed, and the default build is bit for
 *          bit v0.3. Two lower-precision variants were built and measured to
 *          answer whether they are worth having: a float posterior and
 *          accumulators, and a 16-bit likelihood table. Neither earned its
 *          place, both were removed, and PRECISION records the numbers so the
 *          question does not have to be reopened. examples/quest_bench.c lost
 *          a measurement bug in the process: its memory-floor pass did the
 *          same arithmetic every trial, so at -O3 the compiler hoisted it out
 *          of the timing loop and the floor came out 30 times too fast. The
 *          pass now perturbs the posterior first.
 *   v0.3 - a batch entry point for the custom psychometric function,
 *          psyq_pf_batch_fn and desc.pf_batch, beside the per-cell
 *          desc.pf_fn. It fills a whole stimulus's worth of cells in one
 *          call, which is what a binding needs: a vectorized Python callable
 *          is entered once per stimulus instead of S*P times (140 times
 *          instead of 156,800 for examples/quest_qcsf.c, which now uses it
 *          and keeps the per-cell version beside it to check against).
 *          PSYQ_PF_CUSTOM takes exactly one of the two; open() rejects both
 *          or neither. The two paths produce tables that are equal bit for
 *          bit, which the test asserts. The cell entropy row is now computed
 *          from the likelihoods AS STORED, so the decomposition is exact
 *          against the floats the selection reads.
 *   v0.2 - psyq_next() made fast, and measured. Same API; two changes
 *          inside, both of them visible only in memory and in the clock:
 *            - the expected entropy is computed from its decomposition,
 *              E[H] = H(theta) - H(y | s) + sum_theta post h(s, theta), with
 *              the cell outcome entropy h tabulated at open as one more table
 *              row. That takes the logarithm out of the per-cell loop: a
 *              joint selection is now S*P*(K+1) multiply-adds and S*K
 *              logarithms, where v0.1 needed S*P*K logarithms. The row costs
 *              S*P floats, 50% more table at K = 2, and is only built when
 *              it will be used: a desc with a nuisance axis takes its
 *              logarithms on the marginal and does not pay for it.
 *            - the table is stored transposed, L[S][K][P] with the parameter
 *              index fastest, and the parameter axes are permuted internally
 *              so the nuisance ones are the slowest. Every sweep is then a
 *              contiguous dot product or a contiguous multiply-accumulate
 *              over the parameter grid, which gcc and MSVC vectorize. The
 *              public index order is unchanged; see LAYOUT.
 *          Measured on one laptop, a Psi-marginal selection went from 6.3 ms
 *          to 0.95 ms and an update from 30 us to 19 us; the numbers and the
 *          memory floor they are measured against are under MEMORY, COST AND
 *          THREADS, and examples/quest_bench.c prints them for your machine.
 *          No SIMD intrinsics: at the measured rate the sweep is bound by
 *          memory, not by arithmetic, and -mavx2 changes nothing.
 *   v0.1 - the implementation. The API and the declarations are v0.0's, and
 *          the handle gained three private fields (the selection scores and
 *          the nuisance marginal's strides). Everywhere the specification
 *          was optimistic, silent or self-contradictory, this manual now
 *          says what the code does:
 *            - a selection costs one logarithm per table CELL, S*P*K of
 *              them, not S*K, and that is what a selection costs. Flagging
 *              nuisance axes makes it cheaper, not dearer. See MEMORY, COST
 *              AND THREADS, which also has measured times.
 *            - psyq_memory_size() covers the arena, not the trial history:
 *              the history is inline in the handle. It also carries seven
 *              bytes of slack so an unaligned caller buffer works.
 *            - desc.tie_tolerance is in the units of the score. Those are
 *              bits only under PSYQ_SELECT_ENTROPY; under the placement
 *              rules they are the units of stimulus axis 0.
 *            - a stop criterion, once fired, stays fired, and the criteria
 *              are tested at open() as well as after every update.
 *            - a quantile is a grid point, not an interpolation, and the
 *              placement rules read the marginal of desc.select_param.
 *            - an update whose outcome is impossible under every parameter
 *              point returns PSYQ_ERR_ARG and changes nothing.
 *            - psyq_update_values() records stim_index = -1 even on a grid
 *              point, and agrees with psyq_update() to float precision.
 *            - psyq_next() caches its proposal; psyq_next_subset() does not,
 *              and overwrites that cache.
 *            - PSYQ_PF_WEIBULL needs a positive stimulus axis 0 and a
 *              positive alpha axis; open() rejects anything else.
 *            - open() checks a custom pf in every cell it tabulates, and in
 *              nine cells under desc.no_table.
 *            - RETURN VALUES AND ERRORS now lists every desc open() refuses.
 *   v0.0 - specification. Declarations and the manual, no implementation.
 *
 *   STATUS: v0.5.2. Implemented and tested against an independent
 *   double-precision reference written from the definitions in
 *   tests/adapt/psy_quest_test.c: 14770 checks over the posterior update, the
 *   expected-entropy selection (joint and marginalized over nuisance axes),
 *   every built-in psychometric function, every placement rule, every
 *   tiebreak rule, every stop criterion, every estimator, a three-outcome
 *   custom model, the no_table and off-grid paths, a nuisance axis in the
 *   middle of the parameter list (which makes the internal and the public
 *   axis orders differ), both custom-callback entry points, the arena
 *   accounting, and every desc the manual says open() rejects. desc.pf_batch
 *   and desc.pf_fn, given the same model, fill the table equal BIT FOR BIT
 *   and make identical selections, with and without the permutation and at
 *   K = 2 and K = 3; the batch path's off-grid and single-cell results agree
 *   with the per-cell path's to float precision, as the manual says they
 *   will. The posterior and the selection scores agree with the
 *   reference to 1e-12 under desc.no_table (both in double) and to 1e-6
 *   through the float table, and the selected stimulus is identical. The
 *   decomposed selection score agrees with the definition to 1.3e-15 in
 *   double; through the float table the worst difference is 5.3e-8, which is
 *   the rounding of the tabulated cell entropy to float, nothing else, and is
 *   below the table's own error. A 20-replication simulated Psi run of 100 trials
 *   recovers a threshold of -1.72 log10 units with a mean absolute error of
 *   0.032 and a bias of -0.003. A snapshot taken at trials 0, 1, 7 and 23 of a
 *   24-trial run, both between an update and the next selection and between a
 *   selection and its update, resumes to the uninterrupted run BIT FOR BIT
 *   (proposals, posterior, history, estimates, entropy, and the caller's
 *   generator state) in five configurations: joint, a nuisance axis last and
 *   in the middle, pf_batch, and a random subset with random ties; and a load
 *   refuses, with the field named and the handle left closed, a desc that
 *   differs in any compared number, the other callback kind, a wrong magic or
 *   format, a truncated or trailing snapshot, and corrupt counters, posterior
 *   or history.
 *
 *   The async layer has its own block in that test, built and run both ways:
 *   14770 checks without PSYQ_ASYNC and 15121 with it, the extra ones covering
 *   an async run against a bit-for-bit synchronous replay, the initial
 *   proposal at seq 0, a poll that is never ahead of what was submitted nor
 *   behind a previous poll, a queue driven until it returns PSYQ_ERR_BUSY with
 *   the replay still matching afterwards, a stop that drains, an off-grid
 *   submit, every call on a zeroed or stopped handle, and queue_depth 1
 *   pushing back on a second submit.
 *
 *   Clean under gcc 11.4 -fsanitize=address,undefined, and with PSYQ_ASYNC
 *   also under -fsanitize=thread. Compiles and runs warning-free as C99, C11
 *   and C++17 under gcc 11.4 with -Wall -Wextra -Wpedantic -Wshadow -Werror,
 *   at -O2 and at -O3, with and without PSYQ_ASYNC. Also built and run on
 *   Windows 11 with MSVC 19.44 (VS 2022) under /W4 /WX, in its default C
 *   dialect and as C++, both ways: the test prints the same passes and the
 *   same simulation numbers as gcc, and the four examples build and exit 0.
 *
 *   The cost numbers under MEMORY, COST AND THREADS, including the two
 *   lower-precision variants under PRECISION that were measured and then
 *   removed, come from examples/quest_bench.c on one x86-64 laptop, and they
 *   are the only measurement of cost there is.
 *
 *   Compared cell by cell against the reference implementations, through the
 *   MEX binding in MATLAB R2023a (tests/compare/compare_quest_mquestplus.m).
 *   Against mQUESTPlus, which drives each run so that both are updated with
 *   the stimulus it chose, on the paper's figure 2 threshold (32 trials),
 *   figure 3 threshold, slope and lapse (200), figure 4's normal through
 *   desc.pf_batch on qpPFNormal (128), and a marginalized case with the
 *   nuisance axis in the middle (64): no selection differs. 32 selections are
 *   ties within 6.9e-10 bits, which this header resolves to the lowest index
 *   under desc.tie_tolerance where mQUESTPlus takes the strict argmin, and in
 *   all of them the two picked the same stimulus. The posteriors agree within
 *   4.3e-7, which is the float table (LAYOUT). Against Palamedes' PAL_AMPM,
 *   Psi and Psi-marginal: identical selections over 60 trials each, posteriors
 *   within 5.7e-8. And Watson's own QUEST+ notebook runs replay identically, 17
 *   of 17 (tests/compare/methods_compare.py --replay).
 *
 *   NOT yet verified: Psychtoolbox's Quest, the one reference implementation
 *   not compared; any platform but x86-64 (the header has no intrinsics, so
 *   vectorization elsewhere is the compiler's business, but it is untested on
 *   ARM).
 *
 *   ---------------------------------------------------------------------
 *   USAGE
 *   ---------------------------------------------------------------------
 *   Do this:
 *
 *       #define PSY_QUEST_IMPLEMENTATION
 *
 *   in *one* C or C++ file before including this header to create the
 *   implementation. Every other file just includes the header normally.
 *
 *   A 2AFC contrast threshold with a Weibull in log10 contrast (this is the
 *   Psi method: threshold and slope free, guess and lapse fixed):
 *
 *       #define PSY_QUEST_IMPLEMENTATION
 *       #include "psy_quest.h"
 *
 *       psyq_desc d = {0};
 *       d.pf = PSYQ_PF_GUMBEL;                       // Weibull in log units
 *       d.stim[0]   = psyq_linspace(-3.0, 0.0, 31); // log10 contrast
 *       d.n_stim    = 1;
 *       d.param[0]  = psyq_linspace(-3.0, 0.0, 61); // threshold (log10)
 *       d.param[1]  = psyq_linspace( 0.5, 6.0, 12); // slope
 *       d.param[2]  = psyq_fixed(0.5);              // guess: 2AFC
 *       d.param[3]  = psyq_fixed(0.02);             // lapse
 *       d.n_param   = 4;
 *       d.stop_trials = 60;
 *
 *       psyq_quest q;
 *       if (!psyq_open(&q, &d)) { fputs(psyq_error(&q), stderr); return 1; }
 *
 *       while (!psyq_done(&q)) {
 *           int    si = psyq_next(&q);              // stimulus grid index
 *           double x  = psyq_stim_value(&q, si, 0); // its log10 contrast
 *           int correct = run_trial(pow(10.0, x));  // 1 = correct, 0 = not
 *           psyq_update(&q, si, correct);
 *       }
 *       double est[4];
 *       psyq_estimate(&q, PSYQ_EST_MEAN, est);      // est[0] = threshold
 *       psyq_close(&q);
 *
 *   Classic QUEST (Watson & Pelli 1983) is the same with param[1] fixed at
 *   3.5, param[3] fixed at 0.01, a Gaussian prior on param[0]
 *   (psyq_desc.param[0].prior) and desc.select = PSYQ_SELECT_QUANTILE.
 *   Psi-marginal is the same with param[2] and param[3] given a few grid
 *   points each and flagged nuisance. A CSF or any other model is
 *   PSYQ_PF_CUSTOM with desc.pf_fn; see PSYCHOMETRIC FUNCTIONS. So is any
 *   task with more than two outcomes: a "less / same / more" judgment
 *   with two criteria, a 4AFC scored by which interval was chosen, a
 *   categorical response on a circular stimulus (mQUESTPlus's
 *   qpPFCircular). The custom function fills K probabilities per cell and
 *   the rest of the header does not care what K is.
 *
 *   ---------------------------------------------------------------------
 *   MODEL
 *   ---------------------------------------------------------------------
 *   Everything lives on grids. The STIMULUS grid is the Cartesian product
 *   of n_stim axes (S points); the PARAMETER grid is the product of n_param
 *   axes (P points); a trial has one of K OUTCOMES. The psychometric
 *   function gives p(outcome | stimulus, parameters) for every cell, and
 *   Bayes' rule keeps a posterior over the P parameter points. Before each
 *   trial the header picks the stimulus whose outcome is expected to leave
 *   the posterior with the least entropy (Watson 2017, eq. 8-11), or the
 *   stimulus a QUEST-style placement rule asks for. This is exactly what
 *   mQUESTPlus, questplus (Python) and PsychoPy's QuestPlusHandler do; the
 *   grid layout below is chosen so their published examples reproduce.
 *
 *   AXES
 *     A psyq_axis is either an explicit array of values (values, n) or a
 *     linspace (lo, hi, n) when values is NULL. psyq_linspace() and
 *     psyq_fixed() build one. n = 1 is a FIXED parameter: it costs nothing
 *     and is what turns QUEST+ into QUEST or Psi. Grids need not be
 *     uniform; a log-spaced threshold axis is an explicit array. The handle
 *     keeps every axis's size, so psyq_stim_axis_n() and psyq_param_axis_n()
 *     read them back and a caller never has to carry the desc around.
 *
 *   LAYOUT
 *     Both joint grids are row-major with the LAST axis fastest, so
 *     flat = ((i0 * n1 + i1) * n2 + i2) ... . psyq_stim_index() and
 *     psyq_param_index() convert. That order is what every index, the
 *     posterior psyq_posterior() hands back, every marginal and every
 *     history entry is in.
 *
 *     Inside, two things differ, because the per-trial sweep is the whole
 *     cost of this header and it wants long contiguous runs.
 *
 *     First, the likelihood table is TRANSPOSED: L[s][k][theta], stimulus
 *     major, then outcome, with the parameter index fastest. One trial's
 *     selection reads it once front to back as K+1 dot products of a
 *     P-long float row against the posterior, and psyq_update() reads the
 *     one row of the outcome that happened. Both are contiguous, so a
 *     compiler that vectorizes will.
 *
 *     Second, when any parameter axis is flagged nuisance, the axes are
 *     PERMUTED internally so the nuisance ones vary slowest. The marginal
 *     the selection needs is then the fastest-varying part of the posterior,
 *     and marginalizing is a multiply-accumulate into one contiguous vector
 *     instead of a strided gather. The caller never sees this: indices,
 *     marginals, estimates, the history and psyq_posterior() are all in the
 *     order above, and psyq_posterior() permutes into a second buffer when
 *     the two orders differ (they do not when the nuisance axes are already
 *     last, which is how psyq_desc is usually written).
 *
 *     The table holds floats; every accumulation is in double; the posterior
 *     is double. That halves the table against a double table for about 1e-7
 *     of posterior mass, because a float carries seven digits and a
 *     likelihood is multiplied into a posterior that is renormalized every
 *     trial, so the error is replaced rather than compounded. Set
 *     desc.no_table for a run in double throughout, at the price of
 *     evaluating the model instead of reading it.
 *
 *   OUTCOMES
 *     K = 2 for the built-in functions: outcome 1 is "correct" / "yes" /
 *     "detected", outcome 0 the other. A custom function sets K itself
 *     (desc.n_outcomes) and defines what each index means; a 3-outcome
 *     "less / same / more" judgment, or a 4AFC with the chosen interval as
 *     the outcome, are ordinary configurations.
 *
 *   THE LOOP
 *     psyq_next(q)                 stimulus grid index to show next
 *     psyq_update(q, s, k)         what was shown (index) and the outcome
 *     psyq_update_values(q, x, k)  the same for a stimulus OFF the grid
 *     psyq_done(q)                 a stop criterion has fired
 *     psyq_estimate(q, how, out)   parameter estimates from the posterior
 *
 *   next() and update() are separate, as in psy_stair.h: show what you can,
 *   tell update() what you showed. An off-grid stimulus (a display that
 *   quantized the contrast, an interleaved design, a catch trial) goes
 *   through psyq_update_values(), which evaluates the psychometric function
 *   at that stimulus for all P parameter points instead of reading the
 *   table. The posterior is exact either way; only the cost differs. Two
 *   details follow from that. The trial is recorded with stim_index = -1 even
 *   when the value lands exactly on a grid point, because the value, not the
 *   index, is what was shown. And the two paths agree to float precision
 *   rather than exactly, since the table path rounds the likelihood to float
 *   and this one does not: about 1e-7 per update on a renormalized posterior,
 *   which is the trade LAYOUT describes.
 *
 *   SELECTION (desc.select)
 *     PSYQ_SELECT_ENTROPY   (default) minimum expected posterior entropy
 *                           over the candidate stimuli. With nuisance
 *                           parameters flagged (psyq_axis.nuisance), the
 *                           entropy is that of the posterior MARGINALIZED
 *                           over them (Watson 2017 sec. 2.5; Prins 2013),
 *                           so trials are spent on the parameters you want.
 *     PSYQ_SELECT_QUANTILE  the QUEST placement: the stimulus on axis 0
 *                           nearest the posterior quantile
 *                           desc.select_quantile of parameter
 *                           desc.select_param (a threshold in the same
 *                           units as stimulus axis 0). 0.5 is Watson &
 *                           Pelli's default; Pelli's 1987 "ideal"
 *                           placement is a different quantile per beta.
 *     PSYQ_SELECT_MEAN,     the same with the marginal posterior mean or the
 *     PSYQ_SELECT_MODE      marginal posterior mode of that parameter.
 *     psyq_next_subset() restricts the candidates for one trial to the
 *     stimuli the display can produce right now.
 *     The three placement rules read only stimulus axis 0, so every stimulus
 *     that shares an axis-0 value is tied and the tiebreak rule below picks
 *     among them.
 *     psyq_next() caches: it recomputes only after an update, so calling it
 *     twice costs one selection and draws from desc.rng once.
 *     psyq_next_subset() always computes, and its answer becomes the cached
 *     proposal, so a psyq_next() after it repeats the subset's choice rather
 *     than scoring the whole grid.
 *
 *   TIES (desc.tiebreak, desc.tie_tolerance)
 *     Two stimuli are tied when their scores differ by less than
 *     tie_tolerance (default 1e-9). The score is the expected entropy in bits
 *     under PSYQ_SELECT_ENTROPY, where expected entropies of stimuli far from
 *     the posterior mass are equal to many more digits than that, so ties are
 *     common early in a run and at the edges. Under the three placement rules
 *     the score is the distance from stimulus axis 0 to the point estimate,
 *     in the units of that axis, and the tolerance is in those units too. The
 *     rule is set at open and applied every trial:
 *       PSYQ_TIE_LOWEST    (default) the lowest stimulus index. Fully
 *                          deterministic, and biased toward one end of
 *                          the grid.
 *       PSYQ_TIE_NEAREST   the tied stimulus nearest, in grid index, to
 *                          the last stimulus shown (the lowest tied index
 *                          before the first trial, and after a
 *                          psyq_update_values() trial, which has no grid
 *                          index). Deterministic; keeps the run from
 *                          jumping across the grid on a tie.
 *       PSYQ_TIE_ALTERNATE the lowest tied index on one tied trial, the
 *                          highest on the next. Deterministic and
 *                          unbiased over a run. Only a trial that really
 *                          was tied advances the alternation.
 *       PSYQ_TIE_RANDOM    a uniform draw among the tied, from desc.rng:
 *                          one variate per tied trial, choosing tied
 *                          candidate floor(u * count). open() fails
 *                          without a generator.
 *
 *   RANDOM SUBSET (desc.subset_size, desc.rng)
 *     Watson 2017 sec. 4.2 recommends scoring a random subset of the
 *     stimuli each trial rather than all of them: it breaks the tendency
 *     of pure entropy selection to sit on one stimulus, and it divides
 *     the cost of next() by S / subset_size. With desc.rng set and
 *     subset_size > 0, psyq_next() draws that many distinct stimulus
 *     indices from desc.rng each trial and scores only those. The
 *     generator is the caller's; the header never seeds or owns one, so a
 *     run is reproducible from the caller's seed. psyq_next_subset() is
 *     the same with the caller's own list, of 1 to S indices.
 *     The draw is a rejection draw, so it costs at least subset_size
 *     variates and more when one repeats: keep subset_size well under S,
 *     which is the point of it. A subset_size of S or more scores every
 *     stimulus and draws nothing; open() clamps it.
 *
 *   PRIORS
 *     Uniform on every axis by default. psyq_axis.prior points at n
 *     unnormalized weights for that axis; the joint prior is the product.
 *     desc.joint_prior points at P weights for a prior that does not
 *     factor. Either is normalized at open. A Gaussian prior on a log
 *     threshold with a given sd is what QUEST assumes; build it with
 *     psyq_prior_normal().
 *
 *   STOPPING (desc.stop_trials, desc.stop_entropy, desc.stop_sd)
 *     Any subset; the first to fire ends the run, and psyq_stop_reason()
 *     keeps naming that one: a criterion that has fired stays fired, even if
 *     a later trial would raise the entropy or the sd back over its bound.
 *     stop_entropy is a bound on the posterior entropy in bits (of the
 *     marginal over non-nuisance parameters); stop_sd is a bound on the
 *     posterior sd of parameter desc.stop_sd_param. 0 disables a criterion;
 *     open() rejects all zero. The criteria are tested at open() and after
 *     every update, so an entropy or sd bound the prior already satisfies
 *     makes psyq_done() true before the first trial. Trials after done() are
 *     still accepted and recorded, until PSYQ_MAX_TRIALS of them are, which
 *     is its own stop reason (PSYQ_STOP_FULL) and the point at which
 *     psyq_update() starts returning PSYQ_ERR_FULL and changing nothing.
 *
 *   ESTIMATES
 *     psyq_estimate(q, how, out) fills out[n_param] from the joint posterior:
 *       PSYQ_EST_MEAN    marginal posterior mean of each parameter
 *       PSYQ_EST_MODE    the joint posterior mode (one grid point)
 *       PSYQ_EST_MEDIAN  marginal posterior median of each parameter
 *     psyq_quantile(q, i, p), psyq_sd(q, i) and psyq_marginal(q, i, out)
 *     give a single parameter's marginal quantile, sd and full marginal.
 *     A quantile on a grid is a grid point, not an interpolation: it is the
 *     lowest point of axis i whose cumulative marginal mass reaches p. The
 *     median estimator is psyq_quantile(q, i, 0.5), so both step from grid
 *     point to grid point as trials come in. Psychtoolbox's QuestQuantile
 *     interpolates; this does not, because the posterior only exists on the
 *     grid.
 *     psyq_posterior(q) is the joint posterior itself, P doubles in LAYOUT
 *     order, for a plot or a fit. psyq_entropy(q) is its entropy in bits,
 *     the number QUEST+ is driving down.
 *     mQUESTPlus additionally refits the parameters by maximum likelihood
 *     off the grid (qpFit); psy_quest.h does not, and says so, because a
 *     fit off the grid is an offline job and this header's job is the next
 *     trial. The history is there for it.
 *
 *   ---------------------------------------------------------------------
 *   PSYCHOMETRIC FUNCTIONS
 *   ---------------------------------------------------------------------
 *   Every built-in is p(correct) = gamma + (1 - gamma - lambda) * F(x),
 *   with the parameter axes in the fixed order
 *
 *       param[0] = alpha   threshold, in the units of stimulus axis 0
 *       param[1] = beta    slope
 *       param[2] = gamma   guess rate (0.5 for 2AFC, 0 for yes/no)
 *       param[3] = lambda  lapse rate
 *
 *   and n_param = 4; fix the ones you do not estimate with psyq_fixed().
 *   They use stimulus axis 0 only; the other stimulus axes, if any, are
 *   ignored (a built-in with n_stim > 1 is a way to test selection, not a
 *   model). The forms follow Palamedes (PAL_Weibull etc.), so a Palamedes
 *   or psignifit fit of the same data agrees on the parameters:
 *
 *     PSYQ_PF_WEIBULL   F = 1 - exp(-(x / alpha)^beta)            x > 0
 *                       open() rejects a stimulus axis 0 or an alpha axis
 *                       with a point at or below zero, where the form has
 *                       no value; an off-grid psyq_update_values() below
 *                       zero reads F = 0.
 *     PSYQ_PF_GUMBEL    F = 1 - exp(-10^(beta (x - alpha)))        log-Weibull:
 *                       the Weibull with x and alpha in log10 units. This is
 *                       QUEST's function (Watson & Pelli's log10 intensity)
 *                       and, with x in dB (20 log10), QUEST+'s Watson 2017
 *                       form once beta is scaled by 1/20.
 *     PSYQ_PF_LOGISTIC  F = 1 / (1 + exp(-beta (x - alpha)))
 *     PSYQ_PF_NORMAL    F = Phi(beta (x - alpha))     cumulative Gaussian
 *     PSYQ_PF_HYPSEC    F = (2 / pi) atan(exp((pi / 2) beta (x - alpha)))
 *     PSYQ_PF_CUSTOM    desc.pf_fn, below.
 *
 *   PSYQ_PF_CUSTOM takes the model as a callback, in one of two shapes.
 *   Set exactly one of them; open() rejects both or neither.
 *
 *   desc.pf_fn, one cell per call:
 *
 *       void pf(void* ctx, const double* stim,   // n_stim values
 *                          const double* params, // n_param values
 *                          double* p);           // out: n_outcomes values
 *
 *   desc.pf_batch, one stimulus's worth of cells per call:
 *
 *       void pf_batch(void* ctx,
 *                     const double* stims, int S,   // S rows of n_stim
 *                     const double* params, int P,  // P rows of n_param
 *                     float* out);                  // (s*P + i)*K + k
 *
 *   Both get desc.pf_ctx and must be pure functions of their arguments. Both
 *   must write K non-negative values per cell that sum to 1: within 1e-9 for
 *   pf_fn, which writes doubles, and within 1e-6 for pf_batch, which writes
 *   floats and so cannot land closer than about K times a float's last digit.
 *   open() checks every cell while it builds the table, and under
 *   desc.no_table the nine cells at the ends and the middle of both grids,
 *   and fails with the offending cell and the callback's name in the message.
 *
 *   Which to write. pf_fn is the one to write in C: it is a formula, it says
 *   what it means, and S*P calls to a C function are S*P multiply-adds' worth
 *   of overhead. pf_batch is the one to write in a binding, or wherever a
 *   call is expensive and array arithmetic is not: the header calls it once
 *   per stimulus with the whole parameter grid as a P x n_param matrix, so a
 *   vectorized callable is entered S times at open rather than S*P times, and
 *   once per psyq_update_values() or psyq_p_values() rather than P times. The
 *   rows of `params` are the header's own walk of the parameter grid, which is
 *   the LAYOUT order unless a nuisance axis made it permute the grid
 *   internally (see LAYOUT), so a callback must treat them as a list of P
 *   parameter points, in which order does not matter, and answer row for row.
 *   The header calls it with S = 1; the S argument is part of the contract so
 *   that the shape is a batch, not so that the header can subdivide it.
 *
 *   Two things follow from pf_batch writing floats. Its likelihoods carry
 *   float precision, so psyq_p(), psyq_p_values(), psyq_simulate() and an
 *   off-grid psyq_update_values() are float-exact rather than double-exact
 *   under it, which is the same precision the likelihood TABLE has had all
 *   along. And the two entry points, given the same model, fill the table
 *   identically bit for bit, which is what tests/adapt/psy_quest_test.c
 *   checks, so a binding can develop against pf_fn and ship pf_batch.
 *
 *   desc.no_table with pf_batch is legal and works, and it is the wrong
 *   configuration for a binding: the header then calls the callback once per
 *   stimulus on EVERY psyq_next(), which moves S*P*K floats across the
 *   language boundary per trial. That is exactly the table no_table declined
 *   to store, rebuilt every trial. In C, with a cheap formula, it is a fair
 *   trade; through an interpreter it is not.
 *
 *   Watson's quick CSF (Lesmes 2010): stim = (log spatial frequency, log
 *   contrast), params = (peak gain, peak frequency, bandwidth,
 *   low-frequency truncation), a log-parabola for the sensitivity and a
 *   Weibull on the distance to it. examples/quest_qcsf.c writes it both ways
 *   and checks them against each other.
 *
 *   ---------------------------------------------------------------------
 *   RETURN VALUES AND ERRORS
 *   ---------------------------------------------------------------------
 *   psyq_open() returns bool and fills psyq_error() on failure. Every other
 *   function returns a value or a code and never touches the message
 *   buffer. Index-returning functions return >= 0 or a negative PSYQ_ERR_*;
 *   the counting functions return how many values they wrote; the functions
 *   that return a double return NaN on a bad argument; psyq_strerror() names
 *   a code.
 *
 *     PSYQ_ERR_ARG      null handle, index out of range, outcome >= K,
 *                       non-finite value; also an update whose outcome has
 *                       zero probability under EVERY parameter point, which
 *                       no posterior can absorb. Such an update changes
 *                       nothing and is not recorded.
 *     PSYQ_ERR_CLOSED   the handle is not open
 *     PSYQ_ERR_FULL     PSYQ_MAX_TRIALS trials recorded; the update changes
 *                       nothing
 *     PSYQ_ERR_MEMORY   the caller's buffer is too small, or the allocator
 *                       returned NULL (open only, with a message)
 *
 *   psyq_open() rejects, with the reason in psyq_error(): n_stim or n_param
 *   outside 1..PSYQ_MAX_STIM_DIMS / PSYQ_MAX_PARAMS; an axis with n < 1 or a
 *   non-finite point; a negative or non-finite prior weight, or a prior or
 *   joint_prior whose weights sum to zero; a prior or a nuisance flag on a
 *   stimulus axis, where neither means anything; a
 *   built-in pf with n_param != 4 or n_outcomes neither 0 nor 2;
 *   PSYQ_PF_WEIBULL with a non-positive stimulus or alpha point;
 *   PSYQ_PF_CUSTOM with neither pf_fn nor pf_batch, or with both, or with
 *   n_outcomes outside 2..PSYQ_MAX_OUTCOMES, or whose outcomes do not sum to
 *   1; a pf, select or
 *   tiebreak enum out of range; select_param or stop_sd_param outside
 *   0..n_param-1; select_quantile outside (0, 1) unless it is 0 for the
 *   default of 0.5; a negative tie_tolerance, stop_trials, stop_entropy,
 *   stop_sd or subset_size; PSYQ_TIE_RANDOM or subset_size > 0 without rng;
 *   no stop criterion at all; memory set with memory_size 0 or below
 *   psyq_memory_size(); and a grid too large for this machine's size_t.
 *
 *   ---------------------------------------------------------------------
 *   MEMORY, COST AND THREADS
 *   ---------------------------------------------------------------------
 *   psyq_memory_size(desc) returns the bytes one handle's arena needs: the
 *   float table, the posterior (P*8), the likelihood scratch that the
 *   off-grid and no-table paths use (P*K*8), the selection scores (S*8), a
 *   marginal workspace, the axis values, the random subset, and seven spare
 *   bytes so an unaligned caller buffer works. It does NOT cover the
 *   trial history: that is PSYQ_MAX_TRIALS entries inline in the handle,
 *   which the caller allocates as one object. It returns 0 for a desc open()
 *   would reject, so it is also a way to validate one.
 *
 *   With desc.pf_batch the arena also holds the parameter matrix the callback
 *   is handed, P*n_param doubles, and one stimulus's worth of its output,
 *   P*K floats. Both are built once and reused, so the batch path allocates
 *   and frees nothing per call; for the qCSF example that is 45 KB on top of
 *   1.9 MB.
 *
 *   The table is S*P*K floats, plus S*P more UNLESS an axis is flagged
 *   nuisance. The extra row per stimulus is the outcome entropy of each
 *   cell, which is what lets a selection run without a logarithm per cell
 *   (see the cost paragraph below). At K = 2 that is 50% more table, and it
 *   is the one place where this header trades memory for time. A desc with a
 *   nuisance axis takes its logarithms on the marginal instead, so it does
 *   not build the row and does not pay for it, and neither does desc.
 *   no_table. A desc with a nuisance axis that is not already last also
 *   carries P more doubles, for psyq_posterior() to permute into.
 *
 *   open() takes that many bytes from desc.memory when it is set, else from
 *   PSYQ_MALLOC (malloc by default; define PSYQ_MALLOC/PSYQ_FREE before the
 *   include for an arena of your own) exactly once. Nothing allocates after
 *   open, and close() frees only what open() took. open() also copies the
 *   axis values into the arena and consumes the priors into the posterior, so
 *   no psyq_axis.values, psyq_axis.prior or desc.joint_prior array has to
 *   outlive the call; desc.pf_ctx and desc.rng_ctx do, because the callbacks
 *   keep using them. The handle itself is about 1 KB plus the history and
 *   lives where the caller puts it.
 *
 *   Table sizes, so you can check before you open (the middle column is
 *   S*P*K cells; the bytes include the entropy row where there is one):
 *     Psi, 31 x (61 x 12 x 1 x 1) x 2            45 K cells      270 KB
 *     Psi-marginal, 31 x (61 x 12 x 5 x 5) x 2   1.1 M cells     4.5 MB
 *     qCSF, (12 x 30) x (20 x 20 x 10 x 10) x 2  29 M cells      173 MB
 *   The last is what desc.no_table is for: no table, the callback runs
 *   S*P times per psyq_next() instead, and the handle needs a few
 *   multiples of P*8 bytes. mQUESTPlus makes the same trade with
 *   qpQuestPlusInitialize's precompute flags; questplus (Python) keeps the
 *   table in an xarray. A grid that big is also a grid that will not
 *   converge in an experiment's worth of trials; Watson 2017 sec. 4 says
 *   what grain is worth having.
 *
 *   Per trial, psyq_next() under PSYQ_SELECT_ENTROPY is one pass over the
 *   table, front to back. The expected entropy is not summed cell by cell as
 *   it is written; it is decomposed,
 *
 *     E[H] = H(theta) - H(y | s) + sum_theta post(theta) h(s, theta),
 *
 *   where h(s, theta) = -sum_k L log2 L is the outcome entropy of one cell.
 *   h belongs to the table, so it is tabulated at open, H(theta) is the same
 *   for every stimulus, and what is left per stimulus is K+1 dot products of
 *   length P and K logarithms: S*P*(K+1) multiply-adds and S*K logarithms for
 *   the sweep. With nuisance axes flagged the entropy is that of the
 *   marginal, which does not decompose, so that path instead accumulates K
 *   marginals (S*P*K multiply-adds, no entropy row) and takes S*K*(points in
 *   the marginal) logarithms. Either way there is no logarithm per cell, and
 *   both sweeps are contiguous. psyq_update() reads the one row of the
 *   outcome that happened, P cells, twice. qCSF without a table is 29 M
 *   callback evaluations per selection, hundreds of milliseconds, and belongs
 *   in the inter-trial interval or on a thread of the caller's.
 *
 *   Every count above is an operation count. For times, run
 *   examples/quest_bench.c: it prints the counts next to measured nanoseconds
 *   for four configurations of the Psi-marginal grid, and next to a MEMORY
 *   FLOOR, which is the same dot product over the same bytes with nothing
 *   else in it. What it prints on your machine is what to log. On one x86-64
 *   laptop (WSL2, gcc 11.4 -O2, a 4.5 MB table, so well out of cache), median
 *   of 40 calls, one core:
 *
 *     memory floor                          0.37 ns/cell
 *     Psi, 61 x 12 grid    0.03 ms next     0.59 ns/cell, 0.7 us update
 *     joint entropy        1.07 ms next     0.94 ns/cell,  23 us update
 *     marginal entropy     1.09 ms next     0.96 ns/cell,  19 us update
 *     random subset of 8   0.41 ms next     1.41 ns/cell,  25 us update
 *     no table at all     21.6  ms next    19.0  ns/cell, 624 us update
 *
 *   The table sweeps are within about twice the floor, and the rest is the
 *   entropy row, the logarithms and the per-stimulus bookkeeping; the Psi row
 *   is faster per cell than the others because its whole table is 272 KB and
 *   stays in cache. They are bound by memory, not by arithmetic: building
 *   with -mavx2 -mfma, which doubles the vector width, does not change the
 *   numbers. There are no SIMD intrinsics in this header for that reason.
 *   Under MSVC 19.44 /O2 the same grid measured 1.19 and 0.96 ns/cell against
 *   a 0.70 ns/cell floor. Run to run on a loaded machine these numbers move
 *   by a factor of two, which is the reason to measure on the rig rather than
 *   to quote a number from here.
 *
 *   FRAME BUDGET
 *   One psyq_next() plus one psyq_update() that fit inside a 60 Hz frame,
 *   16 ms, can run between trials on the experiment's own thread, with no
 *   pacing, no worker and no thread of any kind. Measured on the machine
 *   above, next + update against the budget:
 *
 *     Psi, 31 x (61 x 12 x 1 x 1) x 2         0.03 ms   fits, 580x over
 *     Psi-marginal, 31 x (61 x 12 x 5 x 5)    1.11 ms   fits, 14x over
 *     the same grid, nothing flagged          1.09 ms   fits, 15x over
 *     the same grid, subset_size 8            0.44 ms   fits, 37x over
 *     the same grid, desc.no_table           22.2  ms   DOES NOT FIT
 *     qCSF with a table, 29 M cells          ~27 ms     DOES NOT FIT
 *     qCSF with desc.no_table                ~550 ms    DOES NOT FIT
 *
 *   So every configuration that keeps a table and is not of qCSF size is a
 *   fraction of a frame, and examples/quest_qcsf.c, whose grid is ninety times
 *   smaller than the qCSF row, is one too (0.3 ms there). When a configuration does not fit,
 *   in this order: set desc.subset_size, which divides the sweep by
 *   S / subset_size and is what Watson 2017 sec. 4.2 recommends anyway (8 of
 *   31 stimuli above, 37x under the budget); coarsen the grid, which Watson
 *   2017 sec. 4 argues for on statistical grounds as well; or run psyq_next()
 *   on a thread of your own during the inter-trial interval, which this
 *   header allows because it does no I/O and touches nothing but its own
 *   handle. Do not split one selection across frames: the posterior must not
 *   change underneath it. examples/quest_bench.c prints this line for every
 *   configuration it times, so a desc of your own can be checked against the
 *   budget before it is trusted with an experiment.
 *
 *   PRECISION
 *   The likelihood table is float; the posterior, every accumulator, the
 *   entropies and every public number are double; the psychometric function is
 *   evaluated in double. LAYOUT says why the table is the one narrow thing.
 *   Two ways of narrowing the rest were implemented, measured and then taken
 *   out again. The numbers are here so nobody has to do it twice.
 *
 *   A float posterior, scratch and accumulators (the table already being
 *   float, this makes the whole sweep single precision). The sweep turns out
 *   to be neither purely bandwidth-bound nor purely arithmetic-bound, so
 *   halving the arithmetic width helps only where the compiler vectorizes.
 *   Best of several interleaved runs on one core, Psi-marginal grid:
 *
 *     -O2 (gcc 11, no vectorizer)   1.06 to 1.17x faster
 *     -O3 (SSE2, 2 -> 4 lanes)      1.34 to 1.48x faster
 *     the small 61 x 12 Psi grid    1.2 to 2.0x, psyq_update 1.2 to 1.7x
 *     desc.no_table                 no gain, and 0.87 to 0.89x in every run
 *
 *   Accuracy was never what decided it: over 90 simulated trials on three
 *   grids not one selected stimulus or simulated outcome changed, the
 *   threshold estimate moved by at most 3.6e-6 log units against a posterior
 *   sd of 0.06, the posterior by 1.2e-6 relative, the entropy by 1.1e-4 bits,
 *   and the mode and the quantiles were identical. It was dropped because
 *   1.1 to 1.5x off a selection that already fits a display frame fourteen
 *   times over buys the experiment nothing, while a second precision would
 *   have to be documented, tested and supported for good; and because the one
 *   configuration that does NOT fit the frame, desc.no_table, is where it was
 *   consistently slower.
 *
 *   An IEEE half (binary16) table, converted to float on load. It halves the
 *   table as advertised, 4.89 MB to 2.73 MB of arena on the Psi-marginal grid,
 *   and it is 2 to 8 times SLOWER: the memory floor goes from 0.44 to 1.4-2.2
 *   ns per cell and one selection from 1.3 to 11-14 ms, because the
 *   conversion costs more arithmetic than the halved bandwidth saves. The
 *   F16C hardware conversion, one instruction but not in the x86-64 baseline,
 *   narrows it to about 2.2x slower, which is still a loss. Its accuracy
 *   would have been usable (threshold estimate within 1.4e-3 log units, no
 *   selection changes in 90 trials, a 20-run Psi threshold error of 0.0342
 *   against 0.0324 for the float table, and 4.2e-4 on the decomposed
 *   selection score); it simply is not faster. When a table does not fit, the
 *   answer is still desc.no_table, or a coarser grid.
 *
 *   One sentence explains both results. At about 0.9 ns per cell the sweep is
 *   balanced between the bytes it moves and the work it does per byte, so
 *   cutting bytes at the price of operations loses, and cutting operations at
 *   constant bytes wins a little. That is also why there are no SIMD
 *   intrinsics here: see the note above about -mavx2 changing nothing.
 *
 *   A handle is not thread-safe, and the way to use one from a frame loop
 *   without paying for it on that thread is ASYNC, not a lock of your own.
 *   Every function is deterministic unless
 *   desc.rng is set: the same desc and the same history give the same
 *   posterior and the same proposals, bit for bit on one platform, so a
 *   saved history replayed through psyq_update() restores the run. With
 *   rng set, the posterior is still a function of the history alone; only
 *   the proposals depend on the caller's seed.
 *
 *   ---------------------------------------------------------------------
 *   SIMULATION
 *   ---------------------------------------------------------------------
 *   psyq_p(q, stim_index, params, p_out) evaluates the model at any
 *   parameter values (not only grid points); psyq_simulate(q, stim_index,
 *   params, u) draws an outcome from it with the uniform variate u in
 *   [0, 1) that the caller supplies. The header has no random generator.
 *   examples/quest_sim.c runs Psi and QUEST configurations against a
 *   simulated observer and prints the posterior sd per trial next to the
 *   truth. examples/quest_qcsf.c does the same for a four-parameter quick CSF
 *   through desc.pf_fn, and tests/adapt/psy_quest_test.c replays a fixed
 *   response sequence against a reference posterior computed from the
 *   definitions. Checking those same streams against mQUESTPlus's
 *   qpQuestPlusPaperSimpleExamplesDemo is what the bindings are for, and is
 *   not done yet; see docs/psy_adapt.md.
 *
 *   ---------------------------------------------------------------------
 *   SNAPSHOTS
 *   ---------------------------------------------------------------------
 *   A session can be saved at any trial and resumed later, after a crash or a
 *   break, WITHOUT replaying it:
 *
 *       size_t n = psyq_save_size(&q);
 *       psyq_save(&q, buf, n);                    // write buf to disk
 *       ...
 *       psyq_load(&q, &desc, buf, n);             // q resumes where it was
 *
 *   What is saved: the posterior, the history, the pending proposal (a
 *   psyq_next() that was answered by no psyq_update() yet is not made again,
 *   so it draws nothing from desc.rng), the tie state (PSYQ_TIE_NEAREST's last
 *   stimulus and PSYQ_TIE_ALTERNATE's parity), the stop state, and the desc's
 *   numbers. What is not: the likelihood table, which is a function of the
 *   desc and is rebuilt at load, and anything behind a pointer.
 *
 *   psyq_load() is psyq_open() and then a restore. It takes a desc because a
 *   snapshot cannot carry what the desc points at: the axis arrays, the
 *   callbacks and their contexts, desc.rng and its context, desc.memory. That
 *   desc must agree with the snapshot on every number the resumed session
 *   depends on: every axis's points (as open() resolved them, so a linspace
 *   and the same values as an explicit array agree), the nuisance flags, the
 *   psychometric function and whether a custom one is pf_fn or pf_batch, the
 *   outcome count, the selection rule and its parameter and quantile, the
 *   tiebreak and its tolerance, whether there is a generator, the subset
 *   size, the stop criteria and no_table. A mismatch fails the load and names
 *   the first field that differs. The priors are the exception, and on
 *   purpose: open() consumes them into the posterior and keeps no copy, and
 *   the posterior in the snapshot replaces what they would have produced, so
 *   no difference in them could change the resumed session.
 *
 *   The GENERATOR is the caller's, as everywhere in this header: save its
 *   state beside the snapshot and put it back before the next psyq_next().
 *   With that, a resumed session is the uninterrupted one bit for bit: same
 *   proposals, same posterior, same history, same generator state at the end.
 *   tests/adapt/psy_quest_test.c checks exactly that, cutting a 24-trial run
 *   at trials 0, 1, 7 and 23, both between an update and the next selection
 *   and between a selection and its update, in five configurations: joint,
 *   a nuisance axis last, a nuisance axis in the middle (which the header
 *   stores permuted), pf_batch, and a random subset with random ties through
 *   desc.rng.
 *
 *   COST, measured on the Psi-marginal grid of MEMORY, COST AND THREADS: a
 *   snapshot after 100 trials is 149 KB, nearly all of it the posterior (P
 *   doubles; the history is 9 bytes plus 8 per stimulus dimension a trial).
 *   psyq_save() took under a millisecond. psyq_load() took what psyq_open()
 *   takes, 24 to 38 ms against 26 to 30 ms for the open, because both are the
 *   table rebuild: S*P evaluations of the model. That is the trade: the
 *   snapshot is the size of the posterior instead of the size of the table,
 *   and a resume pays one open. Under desc.no_table there is no table and a
 *   load is a millisecond.
 *
 *   LAYOUT, format 1. Every integer is little-endian two's complement, written
 *   and read one byte at a time, so a snapshot moves between compilers and
 *   platforms; an f64 is the IEEE 754 bit pattern as a u64.
 *     magic     4 bytes "PSYQ", then u32 format (1)
 *     desc      i32 n_stim; per stimulus axis i32 n and n f64 points;
 *               i32 n_param; per parameter axis i32 n, u8 nuisance and n
 *               f64 points; i32 pf, u8 model kind (0 built-in, 1 pf_fn,
 *               2 pf_batch), i32 K, i32 select, i32 select_param, f64
 *               select_quantile, i32 tiebreak, f64 tie_tolerance, u8 has
 *               rng, i32 subset_size, i32 stop_trials, f64 stop_entropy,
 *               f64 stop_sd, i32 stop_sd_param, u8 no_table, i32 S, i32 P.
 *               Defaults are written as used (select_quantile 0 as 0.5,
 *               tie_tolerance 0 as 1e-9, subset_size clamped to S).
 *     state     i32 n_trials, proposed, last_shown, tie_parity, stop
 *     posterior P f64 in the CALLER'S axis order (LAYOUT), not renormalized
 *     history   n_trials x (n_stim f64 stim, i32 stim_index, i32
 *               proposed_index, u8 outcome)
 *   Every number is range-checked as it is read, and the posterior is checked
 *   for finite, non-negative mass, so a wrong, truncated or corrupt snapshot
 *   fails the load with a message instead of indexing outside the handle, and
 *   a failed load leaves the handle closed. The format number changes when
 *   this layout does.
 *
 *   Under PSYQ_ASYNC the handle belongs to the thread between start and stop,
 *   so save after psyq_async_stop(), and start a new async session on the
 *   handle psyq_load() rebuilt.
 *
 *   ---------------------------------------------------------------------
 *   ASYNC (PSYQ_ASYNC)
 *   ---------------------------------------------------------------------
 *   Everything above runs on the thread that calls it, and FRAME BUDGET says
 *   what that costs: every configuration with a table fits a 16 ms frame many
 *   times over, and desc.no_table does not. The other half of the answer, for
 *   the configuration that does not fit and for a caller that would rather not
 *   spend the millisecond at all, is to not do the work on the frame loop's
 *   thread. Define PSYQ_ASYNC before the include and this header gains one:
 *
 *       #define PSYQ_ASYNC
 *       #define PSY_QUEST_IMPLEMENTATION
 *       #include "psy_quest.h"          // brings psy_rt.h with it
 *
 *       psyq_quest q;  psyq_async a;  psyq_async_desc ad = {0};
 *       psyq_open(&q, &desc);           // as usual, on this thread
 *       ad.quest = &q;
 *       if (!psyq_async_start(&a, &ad)) die(psyq_async_error(&a));
 *
 *       psyq_snapshot s;
 *       psyq_async_poll(&a, &s);        // the first proposal, already there
 *       for (;;) {
 *           int k = run_trial(s.stim[0]);         // your frames
 *           int seq = psyq_async_submit(&a, s.proposed, k);
 *           while (drawing_frames())              // the interval
 *               if (psyq_async_poll(&a, &s) >= seq) break;
 *           psyq_async_wait(&a, (uint32_t)seq, 20000000ull, &s);  // or block
 *           if (s.done) break;
 *       }
 *       psyq_async_stop(&a);            // drains; the handle is yours again
 *
 *   WHAT IT IS. psy_rt.h's psyrt_pump with psyq_update() and psyq_next() in
 *   its on_msg: one thread, a queue of PSYQ_ASYNC_QUEUE responses (8 by
 *   default) inline in the handle, no heap, normal or below-normal priority
 *   and never above it. on_idle is NULL, because QUEST+ has nothing to do
 *   between trials; that hook is there for psy_gp.h's background fitting.
 *   Read psy_rt.h's PUMP section for the queue, the seq and the lock; this
 *   layer is a hundred lines on top of it and adds no concurrency of its own.
 *
 *   OWNERSHIP, which is the rule to get right. The caller opens the quest
 *   handle and psyq_async_start() takes it over: from then until
 *   psyq_async_stop() returns, NO psyq_* call on that handle is allowed from
 *   any thread, including the const ones and including psyq_simulate() (a
 *   custom pf_batch model writes to the handle's staging row, so even
 *   evaluating the model is a write). Everything a trial loop needs is in the
 *   snapshot instead. After stop the handle is the caller's again and every
 *   psyq_* call is legal, which is how a session ends: stop, then read the
 *   history, the posterior and the estimates at leisure.
 *
 *   THE SEQ. psyq_async_submit() returns a positive, increasing seq for the
 *   response it copied. psyq_async_poll() returns the seq the snapshot
 *   accounts for: every response through it has been applied AND the proposal
 *   in the snapshot was computed after them. So `poll() >= seq` is the "is the
 *   next stimulus ready?" test, it is one atomic load and a struct copy, and a
 *   frame loop can afford it every frame. psyq_async_wait() is the blocking
 *   form with a relative timeout, for the end of an interval.
 *
 *   Seq 0 is the proposal psyq_async_start() computed before the thread
 *   existed, so a snapshot always exists once start() has returned true and
 *   the first trial reads its stimulus from a poll that returns 0. That is why
 *   there is no "nothing published yet" state to handle: the alternatives were
 *   a sentinel or a flag, and having start() publish is simpler than either.
 *
 *   THE QUEUE's capacity is PSYQ_ASYNC_QUEUE, which sizes the handle; how
 *   much of it a session uses is psyq_async_desc.queue_depth (0 for all of
 *   it). That is a policy, how far the trial loop may run ahead of the
 *   inference, and 1 keeps the two in lockstep: a second response submitted
 *   before the first is finished comes back PSYQ_ERR_BUSY.
 *
 *   A FULL QUEUE is PSYQ_ERR_BUSY, nothing was copied, and the caller still
 *   owns the response: retry it on the next frame. A queue that grew instead
 *   would trade a visible error for an invisible unbounded latency. With the
 *   default queue of 8 it takes eight trials submitted faster than the thread
 *   drains to get there, which for a one-millisecond selection means a trial
 *   loop that is not waiting for responses at all.
 *
 *   WHAT IT DOES NOT DO. It does not make a slow configuration fast, it moves
 *   it: the counts under MEMORY, COST AND THREADS still decide how many trials
 *   a session can afford, and a no_table qCSF selection is still 550 ms, now
 *   on another core. It does not touch the posterior's determinism either: the
 *   thread runs the same functions in submit order, so the same responses give
 *   the same posterior bit for bit whether they went through the queue or not,
 *   and tests/adapt/psy_quest_test.c checks exactly that with memcmp.
 *
 *   COST. The handle is about 5.5 KB (most of it psyrt_pump's inline ring,
 *   which this layer does not use: PSYRT_PUMP_INLINE_BYTES can be set to 1 if
 *   nothing else in the program needs it) plus the queue, 40 bytes a response.
 *   A submit is a mutex, a 40-byte copy and a condition-variable signal. A
 *   poll is an atomic load and a 136-byte copy under a mutex nothing else
 *   holds for long. Nothing allocates.
 *
 *   ---------------------------------------------------------------------
 *   BUILDING
 *   ---------------------------------------------------------------------
 *   Nothing to link but libm, unless PSYQ_ASYNC is defined: then psy_quest.h
 *   includes psy_rt.h, which must sit beside it, and POSIX builds link
 *   -pthread. Copy both headers in that case, as a transport header's user
 *   does. PSY_QUEST_IMPLEMENTATION then also compiles psy_rt.h's
 *   implementation, unless the same translation unit already defined
 *   PSY_RT_IMPLEMENTATION or implemented a transport header; either order
 *   gives exactly one copy. PSYQ_ASYNC together with PSYRT_NO_THREADS is a
 *   #error: the layer IS a thread, so there is nothing sensible to compile.
 *   Define PSYQ_ASYNC_QUEUE (8) to resize the response queue's capacity,
 *   which sizes the psyq_async handle; the depth a session uses is
 *   psyq_async_desc.queue_depth.
 *
 *   Every macro in this header either sizes a handle (PSYQ_MAX_STIM_DIMS,
 *   PSYQ_MAX_PARAMS, PSYQ_MAX_OUTCOMES, PSYQ_MAX_TRIALS, PSYQ_ASYNC_QUEUE),
 *   selects a build (PSYQ_ASYNC, PSYQ_API, PSYQ_MALLOC / PSYQ_FREE), or names
 *   a version. Nothing a session might tune is a macro, because a binding
 *   compiles the header once for every script that uses it: what a session
 *   chooses is a desc field, with zero as the default. Two constants look like
 *   knobs and are deliberately not. The sum-to-1 check on a custom model's
 *   outcomes (1e-9 for pf_fn, 1e-6 for pf_batch) is a precondition, not a
 *   preference: the decomposed selection score assumes each cell's outcomes
 *   sum to 1, so a looser check would let a biased selection through
 *   silently; normalize the model instead. And the random subset's 64 retries
 *   before it falls back to a linear probe only bound a loop against a
 *   degenerate generator, and change nothing a working one produces.
 *
 *   Nothing to link but libm. The per-trial sweeps are written as
 *   contiguous dot products with four independent accumulators, which is
 *   what a compiler needs to vectorize a floating-point reduction without
 *   being told it may reassociate. gcc does it at -O3 (or -O2
 *   -ftree-vectorize; gcc 11 at plain -O2 does not vectorize at all), MSVC at
 *   /O2, both with 16-byte vectors. Nothing here needs -ffast-math, and
 *   -ffast-math is not recommended: it would let the compiler reorder the
 *   summations and the results would stop being reproducible.
 *
 *   Define PSYQ_API to override the default
 *   `extern` linkage (definitions too, so static works). Define
 *   PSYQ_MAX_STIM_DIMS (4), PSYQ_MAX_PARAMS (8), PSYQ_MAX_OUTCOMES (8) and
 *   PSYQ_MAX_TRIALS (2048) before the include to resize the handle: the
 *   history is 2048 trials inline, which is most of the handle's 100 KB, so a
 *   session of 300 trials should say so. Define PSYQ_MALLOC and PSYQ_FREE
 *   (both, or neither) to replace malloc for the one allocation at open.
 *
 *       cc -O2 -I. -o quest_sim examples/quest_sim.c -lm
 *       cl /O2 /I. examples\quest_sim.c
 *
 *   ---------------------------------------------------------------------
 *   LICENSE: public domain / MIT-0, see end of file.
 */
#ifndef PSY_QUEST_H_INCLUDED
#define PSY_QUEST_H_INCLUDED

/* The version of this header, for a log line or a compile-time check. The
 * string is the three numbers, and the test asserts that it stays so. */
#define PSYQ_VERSION_MAJOR  0
#define PSYQ_VERSION_MINOR  5
#define PSYQ_VERSION_PATCH  2
#define PSYQ_VERSION_STRING "0.5.2"

/* The one optional dependency, and it comes FIRST: psy_rt.h sets a
 * feature-test macro for the Linux clock calls and can only do that before the
 * first system header. Without PSYQ_ASYNC this header includes nothing but the
 * C standard library, which is what the rest of the manual assumes; with it,
 * psy_quest.h gains the async layer and psy_rt.h comes with it. See ASYNC and
 * BUILDING. */
#ifdef PSYQ_ASYNC
    #ifdef PSYRT_NO_THREADS
        #error "psy_quest.h: PSYQ_ASYNC is a thread, and PSYRT_NO_THREADS removes threads. Define one or the other, not both."
    #endif
    #include "psy_rt.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PSYQ_API
#define PSYQ_API extern
#endif

/* Handle capacities; compile-time so the handle has a fixed size. */
#ifndef PSYQ_MAX_STIM_DIMS
#define PSYQ_MAX_STIM_DIMS 4
#endif
#ifndef PSYQ_MAX_PARAMS
#define PSYQ_MAX_PARAMS 8
#endif
#ifndef PSYQ_MAX_OUTCOMES
#define PSYQ_MAX_OUTCOMES 8
#endif
#ifndef PSYQ_MAX_TRIALS
#define PSYQ_MAX_TRIALS 2048
#endif

/* --- codes ------------------------------------------------------------- */

#define PSYQ_OK          0
#define PSYQ_ERR_ARG    (-1)  /* null handle, bad index, bad outcome, NaN    */
#define PSYQ_ERR_CLOSED (-2)  /* psyq_open() has not succeeded on this handle */
#define PSYQ_ERR_FULL   (-3)  /* PSYQ_MAX_TRIALS reached                     */
#define PSYQ_ERR_MEMORY (-4)  /* buffer too small or allocation failed       */

/* Static description of a PSYQ_ERR_* code ("ok" for values >= 0). */
PSYQ_API const char* psyq_strerror(int code);

/* PSYQ_VERSION_STRING, as compiled into the implementation. Beside the macro,
 * which is what the CALLER was compiled against, it tells a program that links
 * a prebuilt implementation which one it got. */
PSYQ_API const char* psyq_version(void);

/* --- description ------------------------------------------------------- */

/* One grid axis, for a stimulus dimension or a parameter. `values` (n of
 * them) when set, else linspace(lo, hi, n). n = 1 fixes a parameter. */
typedef struct psyq_axis {
    const double* values;   /* explicit grid, or NULL for a linspace        */
    double        lo, hi;   /* linspace bounds when values is NULL          */
    int           n;        /* points; >= 1                                 */
    const double* prior;    /* parameters only: n unnormalized weights, or
                             * NULL for uniform                             */
    bool          nuisance; /* parameters only: marginalize it out of the
                             * selection entropy and the stop criteria      */
} psyq_axis;

/* Axis constructors, for a designated-initializer-free desc build. */
PSYQ_API psyq_axis psyq_linspace(double lo, double hi, int n);
PSYQ_API psyq_axis psyq_values(const double* values, int n);
PSYQ_API psyq_axis psyq_fixed(double value);

/* Fill `out[n]` with an unnormalized Gaussian over the axis's points, for
 * psyq_axis.prior. Returns false when the axis is invalid. */
PSYQ_API bool psyq_prior_normal(const psyq_axis* axis, double mean, double sd, double* out);

typedef enum psyq_pf {
    PSYQ_PF_GUMBEL = 0,   /* log-Weibull; QUEST's function (default)       */
    PSYQ_PF_WEIBULL,      /* Weibull on a linear, positive axis            */
    PSYQ_PF_LOGISTIC,
    PSYQ_PF_NORMAL,       /* cumulative Gaussian                           */
    PSYQ_PF_HYPSEC,       /* hyperbolic secant                             */
    PSYQ_PF_CUSTOM        /* desc.pf_fn                                    */
} psyq_pf;

/* Custom psychometric function, one cell at a time: write p[0..n_outcomes-1].
 * `stim` has n_stim values, `params` n_param values, in axis order. Must be a
 * pure function of its arguments. */
typedef void (*psyq_pf_fn)(void* ctx, const double* stim, const double* params, double* p);

/* Custom psychometric function, many cells at a time: fill
 * out[(s * P + i) * K + k] for every stimulus s in 0..S-1, every parameter row
 * i in 0..P-1 and every outcome k. `stims` is S rows of n_stim doubles,
 * `params` is P rows of n_param doubles; the columns of both are in the
 * caller's axis order. The rows of `params` are the header's own walk of the
 * parameter grid, which is the LAYOUT order unless a nuisance axis made the
 * header permute the grid, so treat them as a list of P parameter points and
 * nothing more: row i of the output belongs to row i of the input.
 *
 * This is the entry point for a vectorized model, and the one a binding wants:
 * it turns S*P calls into one per stimulus. The header calls it with S = 1,
 * once per stimulus at open and once per off-grid update; the S argument is
 * there because the contract is a batch, not because the header splits it
 * further. See PSYCHOMETRIC FUNCTIONS for which one to write. */
typedef void (*psyq_pf_batch_fn)(void* ctx, const double* stims, int S,
                                 const double* params, int P, float* out);

typedef enum psyq_select {
    PSYQ_SELECT_ENTROPY = 0, /* minimum expected posterior entropy (QUEST+) */
    PSYQ_SELECT_QUANTILE,    /* QUEST: posterior quantile of a parameter    */
    PSYQ_SELECT_MEAN,        /* posterior mean of a parameter               */
    PSYQ_SELECT_MODE         /* posterior mode of a parameter               */
} psyq_select;

typedef enum psyq_estimator {
    PSYQ_EST_MEAN = 0,       /* marginal means                              */
    PSYQ_EST_MODE,           /* joint mode                                  */
    PSYQ_EST_MEDIAN          /* marginal medians                            */
} psyq_estimator;

typedef enum psyq_tiebreak {
    PSYQ_TIE_LOWEST = 0,     /* lowest tied stimulus index                  */
    PSYQ_TIE_NEAREST,        /* nearest tied index to the last stimulus     */
    PSYQ_TIE_ALTERNATE,      /* lowest, then highest, alternating           */
    PSYQ_TIE_RANDOM          /* uniform among the tied, from desc.rng       */
} psyq_tiebreak;

/* A uniform variate in [0, 1) from the caller's generator. Optional; used
 * for PSYQ_TIE_RANDOM and for desc.subset_size. */
typedef double (*psyq_rng_fn)(void* ctx);

typedef enum psyq_stop {
    PSYQ_STOP_NONE = 0,
    PSYQ_STOP_TRIALS,
    PSYQ_STOP_ENTROPY,
    PSYQ_STOP_SD,
    PSYQ_STOP_FULL
} psyq_stop;

/* Run description. Zero-initialize it and set only what you need.
 * Required: n_stim >= 1 with stim[0..n_stim-1], n_param >= 1 with
 * param[0..n_param-1] (exactly 4 for a built-in pf), and at least one stop
 * criterion. PSYQ_PF_CUSTOM also needs pf_fn and n_outcomes. */
typedef struct psyq_desc {
    psyq_axis   stim[PSYQ_MAX_STIM_DIMS];
    int         n_stim;
    psyq_axis   param[PSYQ_MAX_PARAMS];
    int         n_param;
    const double* joint_prior;  /* P weights in LAYOUT order, or NULL       */
    psyq_pf     pf;             /* GUMBEL (0) by default                    */
    psyq_pf_fn  pf_fn;          /* PSYQ_PF_CUSTOM only: one cell per call   */
    psyq_pf_batch_fn pf_batch;  /* PSYQ_PF_CUSTOM only: a batch per call.
                                 * Set exactly one of pf_fn and pf_batch.   */
    void*       pf_ctx;         /* passed to pf_fn and to pf_batch          */
    int         n_outcomes;     /* PSYQ_PF_CUSTOM only; built-ins are 2     */
    psyq_select select;         /* ENTROPY (0) by default                   */
    int         select_param;   /* QUANTILE/MEAN/MODE: which parameter      */
    double      select_quantile;/* QUANTILE: 0 = 0.5                        */
    psyq_tiebreak tiebreak;     /* LOWEST (0) by default; see TIES          */
    double      tie_tolerance;  /* bits; 0 = 1e-9                           */
    psyq_rng_fn rng;            /* caller's generator, or NULL              */
    void*       rng_ctx;
    int         subset_size;    /* score this many random stimuli per next;
                                 * 0 = all. Needs rng.                      */
    int         stop_trials;    /* 0 = off                                  */
    double      stop_entropy;   /* bits; 0 = off                            */
    double      stop_sd;        /* 0 = off                                  */
    int         stop_sd_param;  /* which parameter stop_sd watches          */
    bool        no_table;       /* evaluate the pf per trial, keep no table */
    void*       memory;         /* caller's buffer of memory_size bytes, or
                                 * NULL to take one from PSYQ_MALLOC        */
    size_t      memory_size;
} psyq_desc;

/* --- handle ------------------------------------------------------------ */

/* One recorded trial. The stimulus is stored by value so an off-grid
 * update is recorded faithfully; stim_index is -1 for those. */
typedef struct psyq_trial {
    double  stim[PSYQ_MAX_STIM_DIMS];
    int     stim_index;
    int     proposed_index;  /* what psyq_next() had returned, or -1        */
    uint8_t outcome;
} psyq_trial;

/* Handle. The caller allocates it and treats every field as opaque. Must be
 * zeroed or closed before psyq_open(). */
typedef struct psyq_quest {
    psyq_desc  desc;
    int        S, P, K;            /* joint grid sizes and outcome count    */
    int        n_stim_axis[PSYQ_MAX_STIM_DIMS];
    int        n_param_axis[PSYQ_MAX_PARAMS];
    /* Everything below `mem` points into one block of `mem_size` bytes. */
    void*      mem;
    size_t     mem_size;
    bool       mem_owned;
    float* table;        /* L[S][rows][P], or NULL under no_table  */
    int        rows;               /* rows per stimulus: one per outcome, and
                                    * one more for the cell outcome entropy
                                    * where that row is kept                 */
    double*    cache;              /* 2 doubles: the posterior's entropy and
                                    * whether it is still valid              */
    double*    posterior;          /* P, normalized, INTERNAL axis order     */
    double*    post_public;        /* P in the caller's axis order, filled on
                                    * demand; NULL when the orders agree     */
    double*    scratch;            /* K*P likelihoods, row per outcome, for
                                    * the no-table and off-grid paths        */
    double*    param_matrix;       /* P rows of n_param, in the order the
                                    * batch callback is handed them; NULL
                                    * unless desc.pf_batch is set            */
    float*     batch_out;          /* P*K, what the batch callback writes    */
    double*    marginal_scratch;   /* the larger of n_marg and the longest
                                    * parameter axis                         */
    double*    scores;             /* S selection scores                    */
    double*    stim_values;        /* concatenated axis values              */
    double*    param_values;
    int*       subset_scratch;     /* subset_size indices                   */
    int        n_marg;             /* points in the non-nuisance marginal   */
    int        nuis_block;         /* points per nuisance block; n_marg times
                                    * this is P                              */
    int        perm[PSYQ_MAX_PARAMS];     /* internal axis -> public axis    */
    int        inv_perm[PSYQ_MAX_PARAMS]; /* public axis -> internal axis    */
    int        n_param_int[PSYQ_MAX_PARAMS]; /* axis sizes, internal order   */
    bool       permuted;           /* the two orders differ                 */
    bool       has_nuisance;
    int        proposed;           /* last psyq_next() result, or -1        */
    int        last_shown;         /* stimulus index of the last update, -1 */
    int        tie_parity;         /* PSYQ_TIE_ALTERNATE state              */
    int        n_trials;
    psyq_stop  stop;
    bool       open;
    psyq_trial history[PSYQ_MAX_TRIALS];
    char       error[256];
} psyq_quest;

/* --- lifecycle --------------------------------------------------------- */

/* Bytes psyq_open() needs for `desc`, or 0 when the desc is invalid. Use it
 * to size desc.memory, or to decide that no_table is required. The trial
 * history is not in there: it is inline in the handle. */
PSYQ_API size_t psyq_memory_size(const psyq_desc* desc);

/* Validate `desc`, take memory, build the table (unless no_table), install
 * the prior. Returns false with psyq_error() set on any failure. The only
 * function that writes the message buffer. Cost: S*P pf evaluations. */
PSYQ_API bool psyq_open(psyq_quest* q, const psyq_desc* desc);

/* Release what open() allocated (nothing when the caller gave memory). Safe
 * on a zeroed or closed handle, and on the same handle twice. Leaves the last
 * psyq_error() message readable. */
PSYQ_API void psyq_close(psyq_quest* q);

/* Last psyq_open() message for this handle ("" if none). */
PSYQ_API const char* psyq_error(const psyq_quest* q);
PSYQ_API bool        psyq_is_open(const psyq_quest* q);

/* --- grids ------------------------------------------------------------- */

/* Grid sizes, or PSYQ_ERR_CLOSED on a handle that is not open. */
PSYQ_API int psyq_n_stim(const psyq_quest* q);      /* S */
PSYQ_API int psyq_n_param(const psyq_quest* q);     /* P */
PSYQ_API int psyq_n_outcomes(const psyq_quest* q);  /* K */

/* Value of stimulus axis `axis` at stimulus grid point `index`; NaN on a bad
 * argument. psyq_stim_values() fills all n_stim of them and returns how many
 * it wrote, or a negative PSYQ_ERR_*. */
PSYQ_API double psyq_stim_value(const psyq_quest* q, int index, int axis);
PSYQ_API int    psyq_stim_values(const psyq_quest* q, int index, double* out);

/* Flat stimulus index of per-axis indices `sub[n_stim]`, or PSYQ_ERR_ARG.
 * psyq_stim_nearest() finds the grid point nearest a value on each axis. */
PSYQ_API int psyq_stim_index(const psyq_quest* q, const int* sub);
PSYQ_API int psyq_stim_nearest(const psyq_quest* q, const double* stim);

/* The same for the parameter grid; psyq_param_values() returns n_param. */
PSYQ_API double psyq_param_value(const psyq_quest* q, int index, int axis);
PSYQ_API int    psyq_param_values(const psyq_quest* q, int index, double* out);
PSYQ_API int    psyq_param_index(const psyq_quest* q, const int* sub);

/* Points on one axis, in the caller's axis order: stimulus axis `axis` in
 * 0..n_stim-1, parameter axis `axis` in 0..n_param-1. Returns n (>= 1),
 * PSYQ_ERR_ARG for a null handle or an axis out of range, or PSYQ_ERR_CLOSED on
 * a handle that is not open. The product over the stimulus axes is
 * psyq_n_stim(), over the parameter axes psyq_n_param(). */
PSYQ_API int psyq_stim_axis_n(const psyq_quest* q, int axis);
PSYQ_API int psyq_param_axis_n(const psyq_quest* q, int axis);

/* --- the loop ---------------------------------------------------------- */

/* Stimulus grid index to show next, per desc.select, or a negative
 * PSYQ_ERR_*. Cost: one pass over the table. Calling it twice without an
 * update returns the same value without recomputing. */
PSYQ_API int psyq_next(psyq_quest* q);

/* psyq_next() restricted to the `n` stimulus indices in `subset`, 1 <= n <= S.
 * Always recomputes, and its answer becomes the proposal psyq_next() would
 * repeat. Duplicates in `subset` are allowed and change nothing. */
PSYQ_API int psyq_next_subset(psyq_quest* q, const int* subset, int n);

/* Expected posterior entropy (bits) after showing stimulus `index`, the
 * quantity PSYQ_SELECT_ENTROPY minimizes, marginalized over the nuisance axes
 * when there are any. For a plot of the selection landscape; costs one pass
 * over that stimulus's P*(K+1) table cells, and, the first time after an
 * update, one pass of P logarithms for the constant the selection itself does
 * not need. NaN on a bad argument. */
PSYQ_API double psyq_expected_entropy(const psyq_quest* q, int index);

/* Record outcome `outcome` for the stimulus at grid index `index` and update
 * the posterior. Returns 0, or a negative PSYQ_ERR_*, in which case nothing
 * changed and nothing was recorded. Cost: two passes over the one contiguous
 * P-cell row of that stimulus and outcome. */
PSYQ_API int psyq_update(psyq_quest* q, int index, int outcome);

/* The same for a stimulus given by value, on or off the grid. The trial is
 * recorded with stim_index = -1 whether or not the value is a grid point.
 * Cost: P pf evaluations plus P*K. */
PSYQ_API int psyq_update_values(psyq_quest* q, const double* stim, int outcome);

/* True once a stop criterion has fired, which is tested at open() and after
 * every update and does not un-fire; psyq_stop_reason() says which one. */
PSYQ_API bool      psyq_done(const psyq_quest* q);
PSYQ_API psyq_stop psyq_stop_reason(const psyq_quest* q);

/* --- estimates --------------------------------------------------------- */

/* Fill out[n_param] per `how`: marginal means, the joint mode's coordinates,
 * or marginal medians. Returns 0 or PSYQ_ERR_*. */
PSYQ_API int psyq_estimate(const psyq_quest* q, psyq_estimator how, double* out);

/* Marginal posterior quantile `p` in (0, 1), marginal sd, and the full
 * marginal of parameter `axis`. The quantile is a grid point, the lowest whose
 * cumulative mass reaches p, not an interpolation. psyq_marginal() writes the
 * n points of that axis and returns n; the other two return NaN on a bad
 * argument. */
PSYQ_API double psyq_quantile(const psyq_quest* q, int axis, double p);
PSYQ_API double psyq_sd(const psyq_quest* q, int axis);
PSYQ_API int    psyq_marginal(const psyq_quest* q, int axis, double* out);

/* The joint posterior, P doubles in LAYOUT order. Valid until the next
 * update or close. Read-only. */
PSYQ_API const double* psyq_posterior(const psyq_quest* q);

/* Entropy of the posterior in bits, marginalized over nuisance axes. */
PSYQ_API double psyq_entropy(const psyq_quest* q);

/* --- model ------------------------------------------------------------- */

/* Evaluate the psychometric function at stimulus grid point `index` and the
 * given parameter values (any values, not only grid points). Writes K
 * probabilities to p_out. Returns 0 or PSYQ_ERR_*. */
PSYQ_API int psyq_p(const psyq_quest* q, int index, const double* params, double* p_out);

/* The same at an arbitrary stimulus value. */
PSYQ_API int psyq_p_values(const psyq_quest* q, const double* stim, const double* params, double* p_out);

/* Draw an outcome for stimulus `index` under `params` with the uniform
 * variate `u` in [0, 1): the smallest k with cumulative p > u, or a negative
 * PSYQ_ERR_* (u outside [0, 1) is PSYQ_ERR_ARG). */
PSYQ_API int psyq_simulate(const psyq_quest* q, int index, const double* params, double u);

/* --- history ----------------------------------------------------------- */

/* Trials recorded so far, and the array of them (NULL and *n = 0 on a handle
 * that is not open). The array is valid until close(). */
PSYQ_API int               psyq_n_trials(const psyq_quest* q);
PSYQ_API const psyq_trial* psyq_history(const psyq_quest* q, int* n);

/* --- snapshot ---------------------------------------------------------- */

/* Save a session and resume it later without replaying it. psyq_save_size()
 * is the bytes psyq_save() writes (0 on a handle that is not open);
 * psyq_save() writes them and returns the count, or PSYQ_ERR_ARG when `cap`
 * is too small, or PSYQ_ERR_CLOSED. psyq_load() opens `q` from `desc` exactly
 * as psyq_open() would, checks that the desc agrees with the snapshot, and
 * then puts the posterior, the history, the pending proposal and the tie
 * state back, so the next psyq_next() is the one the saved session would have
 * made. On any failure it returns false, leaves `q` closed, and says why in
 * psyq_error(). SNAPSHOTS in the manual gives the layout and the rules. */
PSYQ_API size_t psyq_save_size(const psyq_quest* q);
PSYQ_API int    psyq_save(const psyq_quest* q, void* buf, size_t cap);
PSYQ_API bool   psyq_load(psyq_quest* q, const psyq_desc* desc, const void* buf, size_t len);

/* --- async ------------------------------------------------------------- *
 *  Compiled only under PSYQ_ASYNC. See ASYNC in the manual.
 * ----------------------------------------------------------------------- */
#ifdef PSYQ_ASYNC

/* Two more codes, which only the async calls return. */
#define PSYQ_ERR_BUSY    (-5)  /* the queue is full; the caller keeps its
                                * response and retries next frame           */
#define PSYQ_ERR_TIMEOUT (-6)  /* psyq_async_wait ran out of time           */

/* Responses the queue holds. One trial each; a caller that submits more than
 * this many before the thread drains gets PSYQ_ERR_BUSY. The ring lives inside
 * psyq_async, so this sizes the handle. */
#ifndef PSYQ_ASYNC_QUEUE
#define PSYQ_ASYNC_QUEUE 8
#endif

/* What the thread publishes after every update, and psyq_async_poll() and
 * psyq_async_wait() hand back. A plain struct, copied out under the pump's
 * lock; nothing in it points anywhere. */
typedef struct psyq_snapshot {
    uint32_t  seq;         /* the submit this accounts for: every update
                            * through this seq is applied and `proposed` was
                            * computed after them. 0 is the proposal made at
                            * psyq_async_start(), before any response.      */
    int       proposed;    /* stimulus grid index for the next trial, or a
                            * negative PSYQ_ERR_* if the selection failed   */
    double    stim[PSYQ_MAX_STIM_DIMS];  /* its values, as psyq_stim_values */
    int       update_rc;   /* what psyq_update() returned for this response:
                            * 0, or the code that made it refuse (a full
                            * history, an impossible outcome). Not part of
                            * the seq contract; it is here so a refusal on
                            * the thread cannot pass unnoticed.             */
    int       n_trials;
    bool      done;
    psyq_stop stop;
    double    estimate[PSYQ_MAX_PARAMS];  /* per desc.estimator            */
    double    entropy;     /* bits, as psyq_entropy()                       */
    double    sd;          /* posterior sd of parameter 0                   */
} psyq_snapshot;

/* Async description. Zero-initialize it and set only what you need; `quest` is
 * required and must already be open. */
typedef struct psyq_async_desc {
    psyq_quest*    quest;      /* the handle the thread drives. The layer
                                * OWNS it from start() to stop(): no psyq_*
                                * call on it meanwhile, from any thread.    */
    psyq_estimator estimator;  /* which estimate the snapshot carries;
                                * PSYQ_EST_MEAN (0) by default              */
    bool           below_normal; /* run the thread below normal priority     */
    int            pin_cpu;    /* logical CPU to pin it to; 0 = no pinning,
                                * as psyrt_pump_desc.pin_cpu                */
    int            queue_depth; /* responses this session may have queued,
                                * 1..PSYQ_ASYNC_QUEUE; 0 = PSYQ_ASYNC_QUEUE.
                                * The macro is a capacity (it sizes the
                                * handle); this is a policy: how far the
                                * trial loop may run ahead of the inference.
                                * 1 keeps them in lockstep.                  */
} psyq_async_desc;

/* One trial's response on its way to the thread. Opaque; sized here because
 * the ring is inline. */
typedef struct psyq_async_msg {
    double stim[PSYQ_MAX_STIM_DIMS];
    int    stim_index;     /* -1 for a by-value submit */
    int    outcome;
} psyq_async_msg;

/* Async handle. The caller allocates it and treats every field as opaque. It
 * owns no heap: the pump, the response ring and the snapshot are all inline.
 * Must be zeroed or stopped before psyq_async_start(). */
typedef struct psyq_async {
    psyrt_pump     pump;
    psyq_quest*    quest;
    psyq_estimator estimator;
    psyq_async_msg ring[PSYQ_ASYNC_QUEUE];
    psyq_snapshot  snap;       /* guarded by psyrt_pump_lock()              */
    bool           published;  /* a snapshot exists; under the same lock    */
    bool           running;
    char           error[256];
} psyq_async;

/* Start the thread and publish the first proposal. Returns true with a
 * snapshot already available (psyq_async_poll() returns 0 and fills it), false
 * with psyq_async_error() set: a null handle or desc, a quest that is not
 * open, an estimator out of range, or a pump the OS would not start. The only
 * async call that writes the message, and the only one that must not race
 * another call on the same handle. Cost: one psyq_next() on the calling
 * thread, before the thread exists. */
PSYQ_API bool psyq_async_start(psyq_async* a, const psyq_async_desc* desc);

/* Deliver every response already queued, then join the thread. After it the
 * quest handle is the caller's again and every psyq_* call on it is legal.
 * Safe on a zeroed or already-stopped handle. Must not race another call on
 * the same handle, except a psyq_async_wait() already blocked, which comes out
 * with PSYQ_ERR_CLOSED. */
PSYQ_API void psyq_async_stop(psyq_async* a);

/* Hand one trial to the thread: the stimulus by grid index, or by value for an
 * off-grid trial, and the outcome. Returns a POSITIVE seq, increasing, which
 * psyq_async_poll() and psyq_async_wait() compare against. Negative on
 * failure: PSYQ_ERR_ARG (null handle, index or outcome out of range, a
 * non-finite value), PSYQ_ERR_CLOSED (not started), PSYQ_ERR_BUSY (the queue
 * is full: NOTHING was copied, the caller still owns the response and should
 * retry it on the next frame). Callable from any thread. Copies the response
 * and returns; it does not wait for the inference. */
PSYQ_API int psyq_async_submit(psyq_async* a, int stim_index, int outcome);
PSYQ_API int psyq_async_submit_values(psyq_async* a, const double* stim, int outcome);

/* Copy the newest snapshot into `out` (which may be NULL to ask only for the
 * seq) and return the seq it accounts for: every response through that seq is
 * applied and its proposal is the one in the snapshot. 0 means the proposal
 * made at start(), before any response had landed, so a caller that wants the
 * first stimulus reads it from a poll that returns 0. Negative on error. One
 * atomic load plus a struct copy under the pump's publish lock: cheap enough
 * for every frame. */
PSYQ_API int psyq_async_poll(const psyq_async* a, psyq_snapshot* out);

/* The same, after blocking until the thread has finished the response `seq`
 * or `timeout_ns` of monotonic time has passed. Returns the seq accounted for
 * (>= seq on success), PSYQ_ERR_TIMEOUT if the time ran out first,
 * PSYQ_ERR_CLOSED if the thread stopped without reaching it. timeout_ns is
 * relative; 0 polls. This is the end-of-interval call; poll() is the frame
 * loop's. */
PSYQ_API int psyq_async_wait(psyq_async* a, uint32_t seq, uint64_t timeout_ns,
                             psyq_snapshot* out);

/* Responses queued and not yet applied (the one being worked on is not
 * counted), or a negative PSYQ_ERR_*. For a log line, not a frame loop: it
 * takes the queue's mutex. */
PSYQ_API int psyq_async_pending(const psyq_async* a);

/* Last psyq_async_start() message for this handle ("" if none). */
PSYQ_API const char* psyq_async_error(const psyq_async* a);

/* True while the thread is running. */
PSYQ_API bool psyq_async_is_running(const psyq_async* a);

/* What the thread runs at: PSYRT_POLICY_NORMAL, or PSYRT_POLICY_BELOW_NORMAL
 * when desc.below_normal was set AND the OS granted the drop, which is not an
 * error either way. PSYRT_POLICY_NONE before a start and after a stop. Log it:
 * "the inference was ready in time" means something different at each rung. */
PSYQ_API psyrt_policy psyq_async_policy(const psyq_async* a);

#endif /* PSYQ_ASYNC */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PSY_QUEST_H_INCLUDED */

/* ======================================================================= *
 *                             IMPLEMENTATION                              *
 * ======================================================================= */
#ifdef PSY_QUEST_IMPLEMENTATION
#ifndef PSY_QUEST_IMPLEMENTATION_GUARD
#define PSY_QUEST_IMPLEMENTATION_GUARD

/* Under PSYQ_ASYNC, psy_rt.h's implementation too, unless this translation
 * unit already has it. Its implementation block sits outside its header guard
 * and carries a guard of its own, so a unit that also defines
 * PSY_RT_IMPLEMENTATION, or that also implements a transport header, still ends
 * up with exactly one copy. */
#ifdef PSYQ_ASYNC
    #ifndef PSY_RT_IMPLEMENTATION_GUARD
        #define PSY_RT_IMPLEMENTATION
        #include "psy_rt.h"
    #endif
#endif

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#if defined(PSYQ_MALLOC) != defined(PSYQ_FREE)
#error "psy_quest: define both PSYQ_MALLOC and PSYQ_FREE, or neither"
#endif
#ifndef PSYQ_MALLOC
#include <stdlib.h>
#define PSYQ_MALLOC(n) malloc(n)
#define PSYQ_FREE(p)   free(p)
#endif

/* Every block in the arena is a double, a float or an int, so one alignment
 * covers them all and the layout arithmetic stays in one place. */
#define PSYQ__ALIGN    8
#define PSYQ__LOG2E    1.4426950408889634074
#define PSYQ__SQRT1_2  0.70710678118654752440
#define PSYQ__NAN      ((double)NAN)

/* ======================================================================= *
 *  SMALL UTILITIES
 * ======================================================================= */

/* isfinite() is a macro whose spelling has moved between C and C++ standard
 * libraries; two comparisons are portable and say the same thing. */
static bool psyq__finite(double v) {
    return (v == v) && (v <= DBL_MAX) && (v >= -DBL_MAX);
}

static double psyq__wlog2(double w) {
    /* p log p at p = 0 is 0, and a likelihood of exactly zero is common on a
     * grid, so this branch has to be here. After the decomposition below it
     * is no longer on the per-cell path. */
    return (w > 0.0) ? w * log(w) * PSYQ__LOG2E : 0.0;
}

static bool psyq__mul_ok(size_t a, size_t b, size_t* out) {
    if (a != 0 && b > (size_t)-1 / a) return false;
    *out = a * b;
    return true;
}

static double psyq__axis_at(const psyq_axis* a, int i) {
    if (a->values) return a->values[i];
    if (a->n <= 1) return a->lo;
    return a->lo + (a->hi - a->lo) * ((double)i / (double)(a->n - 1));
}

/* ======================================================================= *
 *  DOT PRODUCTS
 *
 *  Every sweep in this header is a dot product of a contiguous likelihood row
 *  against the posterior, which is why the table is stored transposed. Four
 *  independent accumulators: a reduction written with one accumulator is a
 *  serial dependency chain, and neither gcc nor MSVC may reassociate it
 *  without being told that floating-point addition is associative, which it
 *  is not. The split is what lets them vectorize the loop, and where they do
 *  not it still gives four adds in flight instead of one. The summation order
 *  is fixed by the code, so the result does not depend on what the compiler
 *  chose to do.
 * ======================================================================= */

static double psyq__dot_f(const double* a, const float* b, int n) {
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

static double psyq__dot_d(const double* a, const double* b, int n) {
    double s0 = 0.0, s1 = 0.0, s2 = 0.0, s3 = 0.0;
    int i, m = n & ~3;
    for (i = 0; i < m; i += 4) {
        s0 += a[i]     * b[i];
        s1 += a[i + 1] * b[i + 1];
        s2 += a[i + 2] * b[i + 2];
        s3 += a[i + 3] * b[i + 3];
    }
    for (; i < n; i++) s0 += a[i] * b[i];
    return (s0 + s1) + (s2 + s3);
}

/* acc[i] += a[i] * b[i], the marginalized sweep's inner loop. One long
 * contiguous pass per nuisance point, which is the shape that vectorizes;
 * the accumulator is the marginal itself, so nothing is summed horizontally. */
static void psyq__fma_f(double* acc, const double* a, const float* b, int n) {
    int i;
    for (i = 0; i < n; i++) acc[i] += a[i] * (double)b[i];
}

static void psyq__fma_d(double* acc, const double* a, const double* b, int n) {
    int i;
    for (i = 0; i < n; i++) acc[i] += a[i] * b[i];
}

/* The posterior update's second pass: a[i] *= b[i] * scale, contiguous. */
static void psyq__scale_f(double* a, const float* b, double scale, int n) {
    int i;
    for (i = 0; i < n; i++) a[i] = a[i] * (double)b[i] * scale;
}

static void psyq__scale_d(double* a, const double* b, double scale, int n) {
    int i;
    for (i = 0; i < n; i++) a[i] = a[i] * b[i] * scale;
}

/* ======================================================================= *
 *  DESCRIPTION: VALIDATION AND SIZES
 * ======================================================================= */

typedef struct psyq__sizes {
    int    S, P, K, n_param_axes;
    int    rows;            /* table rows per stimulus: K, or K+1 with the
                             * tabulated outcome entropy                     */
    int    n_marg;          /* points in the non-nuisance parameter marginal */
    int    nuis_block;      /* points in one nuisance block; n_marg * this = P */
    int    n_subset;        /* subset_scratch entries                        */
    int    perm[PSYQ_MAX_PARAMS];  /* internal axis -> public axis           */
    bool   permuted;        /* perm is not the identity                      */
    bool   batch;           /* desc.pf_batch is the model                    */
    size_t n_stim_vals;     /* concatenated stimulus axis points             */
    size_t n_param_vals;
    size_t table_cells;     /* S*rows*P, or 0 under no_table                 */
    size_t marg_doubles;    /* marginal_scratch entries                      */
} psyq__sizes;

/* err may be NULL: psyq_memory_size() validates without a message buffer. */
#define PSYQ__BAD(...) \
    do { if (err && cap) snprintf(err, cap, __VA_ARGS__); return false; } while (0)

static bool psyq__axis_ok(const psyq_axis* a, bool is_param,
                          const char* what, int which, char* err, size_t cap) {
    int i;
    double s = 0.0;
    if (a->n < 1) PSYQ__BAD("psy_quest: %s axis %d has n = %d, need >= 1", what, which, a->n);
    if (a->values) {
        for (i = 0; i < a->n; i++)
            if (!psyq__finite(a->values[i]))
                PSYQ__BAD("psy_quest: %s axis %d value %d is not finite", what, which, i);
    } else if (!psyq__finite(a->lo) || !psyq__finite(a->hi)) {
        PSYQ__BAD("psy_quest: %s axis %d has a non-finite lo or hi", what, which);
    }
    if (a->prior) {
        if (!is_param) PSYQ__BAD("psy_quest: %s axis %d has a prior; only parameters do", what, which);
        for (i = 0; i < a->n; i++) {
            if (!psyq__finite(a->prior[i]) || a->prior[i] < 0.0)
                PSYQ__BAD("psy_quest: %s axis %d prior weight %d is negative or not finite", what, which, i);
            s += a->prior[i];
        }
        if (!(s > 0.0)) PSYQ__BAD("psy_quest: %s axis %d prior sums to zero", what, which);
    }
    if (a->nuisance && !is_param)
        PSYQ__BAD("psy_quest: stimulus axis %d is flagged nuisance; only parameters can be", which);
    return true;
}

static bool psyq__sizes_of(const psyq_desc* d, psyq__sizes* z, char* err, size_t cap) {
    int i, max_axis = 1;
    long long S = 1, P = 1, M = 1, B = 1;
    size_t bytes;

    memset(z, 0, sizeof(*z));
    if (!d) PSYQ__BAD("psy_quest: null desc");
    if (d->n_stim < 1 || d->n_stim > PSYQ_MAX_STIM_DIMS)
        PSYQ__BAD("psy_quest: n_stim = %d, need 1..%d", d->n_stim, PSYQ_MAX_STIM_DIMS);
    if (d->n_param < 1 || d->n_param > PSYQ_MAX_PARAMS)
        PSYQ__BAD("psy_quest: n_param = %d, need 1..%d", d->n_param, PSYQ_MAX_PARAMS);
    if ((int)d->pf < 0 || (int)d->pf > (int)PSYQ_PF_CUSTOM)
        PSYQ__BAD("psy_quest: desc.pf is out of range");

    for (i = 0; i < d->n_stim; i++) {
        if (!psyq__axis_ok(&d->stim[i], false, "stimulus", i, err, cap)) return false;
        S *= d->stim[i].n;
        z->n_stim_vals += (size_t)d->stim[i].n;
        if (S > 0x7fffffffLL) PSYQ__BAD("psy_quest: the stimulus grid exceeds 2^31 points");
    }
    for (i = 0; i < d->n_param; i++) {
        if (!psyq__axis_ok(&d->param[i], true, "parameter", i, err, cap)) return false;
        P *= d->param[i].n;
        z->n_param_vals += (size_t)d->param[i].n;
        if (d->param[i].nuisance) B *= d->param[i].n; else M *= d->param[i].n;
        if (d->param[i].n > max_axis) max_axis = d->param[i].n;
        if (P > 0x7fffffffLL) PSYQ__BAD("psy_quest: the parameter grid exceeds 2^31 points");
    }

    if (d->pf == PSYQ_PF_CUSTOM) {
        if (!d->pf_fn && !d->pf_batch)
            PSYQ__BAD("psy_quest: PSYQ_PF_CUSTOM needs desc.pf_fn or desc.pf_batch");
        if (d->pf_fn && d->pf_batch)
            PSYQ__BAD("psy_quest: set exactly one of desc.pf_fn and desc.pf_batch, not both");
        z->batch = (d->pf_batch != NULL);
        if (d->n_outcomes < 2 || d->n_outcomes > PSYQ_MAX_OUTCOMES)
            PSYQ__BAD("psy_quest: n_outcomes = %d, need 2..%d", d->n_outcomes, PSYQ_MAX_OUTCOMES);
        z->K = d->n_outcomes;
    } else {
        if (d->n_param != 4)
            PSYQ__BAD("psy_quest: a built-in psychometric function needs n_param = 4, got %d", d->n_param);
        if (d->n_outcomes != 0 && d->n_outcomes != 2)
            PSYQ__BAD("psy_quest: a built-in psychometric function has 2 outcomes, not %d", d->n_outcomes);
        z->K = 2;
        /* The Weibull is undefined off the positive axis, and a grid point
         * that cannot be evaluated is a desc error, not a run-time NaN. */
        if (d->pf == PSYQ_PF_WEIBULL) {
            for (i = 0; i < d->stim[0].n; i++)
                if (!(psyq__axis_at(&d->stim[0], i) > 0.0))
                    PSYQ__BAD("psy_quest: PSYQ_PF_WEIBULL needs stimulus axis 0 > 0 (point %d is not)", i);
            for (i = 0; i < d->param[0].n; i++)
                if (!(psyq__axis_at(&d->param[0], i) > 0.0))
                    PSYQ__BAD("psy_quest: PSYQ_PF_WEIBULL needs alpha > 0 (parameter point %d is not)", i);
        }
    }

    if (d->joint_prior) {
        double s = 0.0;
        long long t;
        for (t = 0; t < P; t++) {
            if (!psyq__finite(d->joint_prior[t]) || d->joint_prior[t] < 0.0)
                PSYQ__BAD("psy_quest: joint_prior[%lld] is negative or not finite", t);
            s += d->joint_prior[t];
        }
        if (!(s > 0.0)) PSYQ__BAD("psy_quest: joint_prior sums to zero");
    }

    if ((int)d->select < 0 || (int)d->select > (int)PSYQ_SELECT_MODE)
        PSYQ__BAD("psy_quest: desc.select is out of range");
    if (d->select != PSYQ_SELECT_ENTROPY) {
        if (d->select_param < 0 || d->select_param >= d->n_param)
            PSYQ__BAD("psy_quest: select_param = %d, need 0..%d", d->select_param, d->n_param - 1);
        if (d->select_quantile != 0.0 &&
            (!psyq__finite(d->select_quantile) || d->select_quantile <= 0.0 || d->select_quantile >= 1.0))
            PSYQ__BAD("psy_quest: select_quantile must be in (0, 1) or 0 for 0.5");
    }
    if ((int)d->tiebreak < 0 || (int)d->tiebreak > (int)PSYQ_TIE_RANDOM)
        PSYQ__BAD("psy_quest: desc.tiebreak is out of range");
    if (!psyq__finite(d->tie_tolerance) || d->tie_tolerance < 0.0)
        PSYQ__BAD("psy_quest: tie_tolerance must be finite and >= 0");
    if (d->tiebreak == PSYQ_TIE_RANDOM && !d->rng)
        PSYQ__BAD("psy_quest: PSYQ_TIE_RANDOM needs desc.rng");
    if (d->subset_size < 0) PSYQ__BAD("psy_quest: subset_size must be >= 0");
    if (d->subset_size > 0 && !d->rng)
        PSYQ__BAD("psy_quest: subset_size needs desc.rng");
    if (d->stop_trials < 0) PSYQ__BAD("psy_quest: stop_trials must be >= 0");
    if (!psyq__finite(d->stop_entropy) || d->stop_entropy < 0.0)
        PSYQ__BAD("psy_quest: stop_entropy must be finite and >= 0");
    if (!psyq__finite(d->stop_sd) || d->stop_sd < 0.0)
        PSYQ__BAD("psy_quest: stop_sd must be finite and >= 0");
    if (d->stop_trials == 0 && d->stop_entropy == 0.0 && d->stop_sd == 0.0)
        PSYQ__BAD("psy_quest: no stop criterion; set stop_trials, stop_entropy or stop_sd");
    if (d->stop_sd > 0.0 && (d->stop_sd_param < 0 || d->stop_sd_param >= d->n_param))
        PSYQ__BAD("psy_quest: stop_sd_param = %d, need 0..%d", d->stop_sd_param, d->n_param - 1);
    if (d->memory && d->memory_size == 0)
        PSYQ__BAD("psy_quest: desc.memory is set but memory_size is 0");

    z->S = (int)S;
    z->P = (int)P;
    z->n_param_axes = d->n_param;
    z->n_marg = (int)M;
    z->nuis_block = (int)B;
    z->n_subset = (d->subset_size > 0) ? ((d->subset_size < z->S) ? d->subset_size : z->S) : 0;

    /* Internal parameter axis order: the nuisance axes FIRST, so the axes
     * that survive into the marginal are the fastest-varying ones and the
     * marginalized sweep accumulates into a contiguous n_marg-long vector
     * instead of summing a short block per marginal cell. That is the shape
     * a compiler can vectorize: one long loop, no horizontal sums. A nuisance
     * axis of one point marginalizes over nothing, so a desc without a real
     * nuisance axis keeps the caller's order and pays no translation at the
     * boundary. */
    {
        int c = 0, j;
        if (B > 1) {
            for (j = 0; j < d->n_param; j++) if (d->param[j].nuisance)  z->perm[c++] = j;
            for (j = 0; j < d->n_param; j++) if (!d->param[j].nuisance) z->perm[c++] = j;
        } else {
            for (j = 0; j < d->n_param; j++) z->perm[j] = j;
        }
        for (j = 0; j < d->n_param; j++) if (z->perm[j] != j) z->permuted = true;
    }

    /* The outcome entropy of a cell is a property of the table, so tabulating
     * it takes the logarithms out of the per-trial sweep. The marginalized
     * selection takes its logarithms on the marginal instead and has no use
     * for it, so that configuration does not pay the extra row. */
    z->rows = (!d->no_table && B == 1) ? z->K + 1 : z->K;

    z->marg_doubles = (size_t)((z->n_marg > max_axis) ? z->n_marg : max_axis);

    if (!psyq__mul_ok((size_t)z->P, (size_t)z->K, &bytes) ||
        !psyq__mul_ok(bytes, sizeof(double), &bytes) || bytes > (size_t)-1 / 16)
        PSYQ__BAD("psy_quest: the parameter grid is too large for this machine");
    if (!d->no_table) {
        if (!psyq__mul_ok((size_t)z->S, (size_t)z->rows, &z->table_cells) ||
            !psyq__mul_ok(z->table_cells, (size_t)z->P, &z->table_cells) ||
            !psyq__mul_ok(z->table_cells, sizeof(float), &bytes) || bytes > (size_t)-1 / 16)
            PSYQ__BAD("psy_quest: the likelihood table is too large for this machine; set desc.no_table");
    }
    return true;
}

#undef PSYQ__BAD

static size_t psyq__bump(size_t* off, size_t bytes) {
    size_t at = (*off + (PSYQ__ALIGN - 1)) & ~(size_t)(PSYQ__ALIGN - 1);
    *off = at + bytes;
    return at;
}

/* The one place the arena layout is written down. With base == NULL it only
 * measures, which is what psyq_memory_size() reports. The float table is last
 * of the big blocks so the posterior and the scratch share the first pages
 * and the per-trial sweep runs the table front to back. */
static size_t psyq__plan(const psyq__sizes* z, unsigned char* base, psyq_quest* q) {
    size_t off = 0;
    size_t o_cache = psyq__bump(&off, 2 * sizeof(double));
    size_t o_post  = psyq__bump(&off, (size_t)z->P * sizeof(double));
    size_t o_pub   = psyq__bump(&off, (z->permuted ? (size_t)z->P : 0) * sizeof(double));
    size_t o_scr   = psyq__bump(&off, (size_t)z->P * (size_t)z->K * sizeof(double));
    size_t o_pmat  = psyq__bump(&off, (z->batch ? (size_t)z->P * (size_t)z->n_param_axes : 0)
                                      * sizeof(double));
    size_t o_bout  = psyq__bump(&off, (z->batch ? (size_t)z->P * (size_t)z->K : 0)
                                      * sizeof(float));
    size_t o_marg  = psyq__bump(&off, z->marg_doubles * sizeof(double));
    size_t o_sco   = psyq__bump(&off, (size_t)z->S * sizeof(double));
    size_t o_sv    = psyq__bump(&off, z->n_stim_vals * sizeof(double));
    size_t o_pv    = psyq__bump(&off, z->n_param_vals * sizeof(double));
    size_t o_tab   = psyq__bump(&off, z->table_cells * sizeof(float));
    size_t o_sub   = psyq__bump(&off, (size_t)z->n_subset * sizeof(int));
    if (base && q) {
        q->cache            = (double*)(void*)(base + o_cache);
        q->posterior        = (double*)(void*)(base + o_post);
        q->post_public      = z->permuted ? (double*)(void*)(base + o_pub) : NULL;
        q->scratch          = (double*)(void*)(base + o_scr);
        q->param_matrix     = z->batch ? (double*)(void*)(base + o_pmat) : NULL;
        q->batch_out        = z->batch ? (float*)(void*)(base + o_bout) : NULL;
        q->marginal_scratch = (double*)(void*)(base + o_marg);
        q->scores           = (double*)(void*)(base + o_sco);
        q->stim_values      = (double*)(void*)(base + o_sv);
        q->param_values     = (double*)(void*)(base + o_pv);
        q->table            = z->table_cells ? (float*)(void*)(base + o_tab) : NULL;
        q->subset_scratch   = z->n_subset ? (int*)(void*)(base + o_sub) : NULL;
    }
    return off;
}

/* ======================================================================= *
 *  GRIDS
 *
 *  Public index arithmetic (psyq_param_index(), psyq_param_value(), the
 *  history, the order psyq_posterior() hands back) is in the caller's axis
 *  order. The posterior itself is stored in the internal order, which differs
 *  only when a nuisance axis is not already last. The two meet in exactly
 *  three places: the prior at open, psyq_posterior(), and the mode.
 * ======================================================================= */

/* The counts, clamped to the arrays they index. open() already guarantees
 * n_stim <= PSYQ_MAX_STIM_DIMS, n_param <= PSYQ_MAX_PARAMS and K <=
 * PSYQ_MAX_OUTCOMES, so at run time these are the counts themselves. They exist
 * for the optimizer, which cannot see what open() checked: without a bound it
 * can hand a constant from a caller's call site (a deliberately bad axis in a
 * test, a three-element array for a three-parameter model) into a path that
 * open()'s invariants make impossible, and then warn, or with
 * -Werror=aggressive-loop-optimizations refuse, on the impossible path. gcc 16
 * did exactly that. Every loop and index below that touches a fixed-size array
 * is bounded by one of these, or by the array's size directly. */
static int psyq__ns(const psyq_quest* q) {
    int n = q->desc.n_stim;
    return (n < 0) ? 0 : (n > PSYQ_MAX_STIM_DIMS ? PSYQ_MAX_STIM_DIMS : n);
}

static int psyq__np(const psyq_quest* q) {
    int n = q->desc.n_param;
    return (n < 0) ? 0 : (n > PSYQ_MAX_PARAMS ? PSYQ_MAX_PARAMS : n);
}

static int psyq__nk(const psyq_quest* q) {
    int n = q->K;
    return (n < 0) ? 0 : (n > PSYQ_MAX_OUTCOMES ? PSYQ_MAX_OUTCOMES : n);
}

/* A caller's axis argument, checked against the handle AND the array it will
 * index, so the out-of-range path is provably dead. */
static bool psyq__stim_axis_ok(const psyq_quest* q, int axis) {
    return axis >= 0 && axis < psyq__ns(q);
}

static bool psyq__param_axis_ok(const psyq_quest* q, int axis) {
    return axis >= 0 && axis < psyq__np(q);
}

static const double* psyq__stim_axis(const psyq_quest* q, int axis) {
    const double* p = q->stim_values;
    int j;
    for (j = 0; j < axis && j < PSYQ_MAX_STIM_DIMS; j++) p += q->n_stim_axis[j];
    return p;
}

static const double* psyq__param_axis(const psyq_quest* q, int axis) {
    const double* p = q->param_values;
    int j;
    for (j = 0; j < axis && j < PSYQ_MAX_PARAMS; j++) p += q->n_param_axis[j];
    return p;
}

/* Stride of an axis in LAYOUT order (last axis fastest). */
static int psyq__stim_stride(const psyq_quest* q, int axis) {
    int j, s = 1, n = psyq__ns(q);
    for (j = axis + 1; j < n; j++) s *= q->n_stim_axis[j];
    return s;
}

static int psyq__param_stride(const psyq_quest* q, int axis) {
    int j, s = 1, n = psyq__np(q);
    for (j = axis + 1; j < n; j++) s *= q->n_param_axis[j];
    return s;
}

/* The same for the internal order, where the posterior lives. */
static int psyq__int_stride(const psyq_quest* q, int axis) {
    int j, s = 1, n = psyq__np(q);
    for (j = axis + 1; j < n; j++) s *= q->n_param_int[j];
    return s;
}

static void psyq__stim_vec(const psyq_quest* q, int index, double* out) {
    int j, t = index;
    for (j = psyq__ns(q) - 1; j >= 0; j--) {
        int n = q->n_stim_axis[j];
        out[j] = psyq__stim_axis(q, j)[t % n];
        t /= n;
    }
}

static void psyq__param_vec(const psyq_quest* q, int index, double* out) {
    int j, t = index;
    for (j = psyq__np(q) - 1; j >= 0; j--) {
        int n = q->n_param_axis[j];
        out[j] = psyq__param_axis(q, j)[t % n];
        t /= n;
    }
}

/* An INTERNAL parameter index to the caller's parameter vector. */
static void psyq__param_vec_int(const psyq_quest* q, int index, double* out) {
    int j, t = index;
    for (j = psyq__np(q) - 1; j >= 0; j--) {
        int n = q->n_param_int[j];
        out[q->perm[j]] = psyq__param_axis(q, q->perm[j])[t % n];
        t /= n;
    }
}

/* Internal order to public order, P values. */
static void psyq__to_public(const psyq_quest* q, const double* src, double* dst) {
    int idx[PSYQ_MAX_PARAMS], pub_stride[PSYQ_MAX_PARAMS];
    int np = psyq__np(q), j, t, pub = 0;
    for (j = 0; j < np; j++) pub_stride[j] = psyq__param_stride(q, j);
    memset(idx, 0, sizeof(idx));
    for (t = 0; t < q->P; t++) {
        dst[pub] = (double)src[t];
        for (j = np - 1; j >= 0; j--) {
            if (++idx[j] < q->n_param_int[j]) { pub += pub_stride[q->perm[j]]; break; }
            idx[j] = 0;
            pub -= (q->n_param_int[j] - 1) * pub_stride[q->perm[j]];
        }
    }
}

/* ======================================================================= *
 *  PSYCHOMETRIC FUNCTIONS
 * ======================================================================= */

static double psyq__pf_shape(psyq_pf pf, double x, double alpha, double beta) {
    double z = beta * (x - alpha);
    switch (pf) {
    case PSYQ_PF_WEIBULL:
        if (!(x > 0.0) || !(alpha > 0.0)) return 0.0;
        return 1.0 - exp(-pow(x / alpha, beta));
    case PSYQ_PF_LOGISTIC: return 1.0 / (1.0 + exp(-z));
    case PSYQ_PF_NORMAL:   return 0.5 * erfc(-z * PSYQ__SQRT1_2);
    case PSYQ_PF_HYPSEC:   return (2.0 / 3.14159265358979323846) * atan(exp(1.57079632679489661923 * z));
    case PSYQ_PF_GUMBEL:
    default:               return 1.0 - exp(-pow(10.0, z));
    }
}

/* One cell, written with `stride` between outcomes. The stride is what lets
 * the transposed table be filled without a round trip through a temporary:
 * only a custom callback, which insists on K contiguous values, needs one. */
static void psyq__eval_into(const psyq_quest* q, const double* stim, const double* params,
                            double* out, size_t stride) {
    if (q->desc.pf_batch) {
        /* One cell is a batch of one stimulus and one parameter row. */
        int k;
        q->desc.pf_batch(q->desc.pf_ctx, stim, 1, params, 1, q->batch_out);
        for (k = 0; k < q->K; k++) out[(size_t)k * stride] = (double)q->batch_out[k];
    } else if (q->desc.pf == PSYQ_PF_CUSTOM) {
        double p[PSYQ_MAX_OUTCOMES];
        int k;
        q->desc.pf_fn(q->desc.pf_ctx, stim, params, p);
        if (stride == 1) {
            for (k = 0; k < q->K; k++) out[k] = p[k];
        } else {
            for (k = 0; k < q->K; k++) out[(size_t)k * stride] = p[k];
        }
    } else {
        double f = psyq__pf_shape(q->desc.pf, stim[0], params[0], params[1]);
        double p1 = params[2] + (1.0 - params[2] - params[3]) * f;
        if (!(p1 >= 0.0)) p1 = 0.0;   /* also catches a NaN from a wild grid */
        if (p1 > 1.0) p1 = 1.0;
        out[0] = 1.0 - p1;
        out[stride] = p1;
    }
}

static void psyq__eval(const psyq_quest* q, const double* stim, const double* params,
                       double* p) {
    psyq__eval_into(q, stim, params, p, 1);
}

/* Fill out[k*P + theta] with the likelihood of every outcome at one stimulus,
 * walking the parameter grid in INTERNAL order with an odometer so no cell
 * costs a division. The transposed layout is what makes every later sweep a
 * contiguous dot product. */
static void psyq__lik_at(const psyq_quest* q, const double* stim, double* out) {
    const double* axv[PSYQ_MAX_PARAMS];
    int idx[PSYQ_MAX_PARAMS];
    double pv[PSYQ_MAX_PARAMS];
    int np = psyq__np(q), P = q->P, j, t;
    if (q->desc.pf_batch) {
        /* One call for the whole parameter grid at this stimulus, then the
         * transpose from the callback's cell-major order into the header's
         * outcome-major rows. The parameter matrix is already in the order
         * the rows of `out` are indexed by, so nothing is permuted here. */
        int k, K = q->K;
        q->desc.pf_batch(q->desc.pf_ctx, stim, 1, q->param_matrix, P, q->batch_out);
        for (t = 0; t < P; t++)
            for (k = 0; k < K; k++)
                out[(size_t)k * (size_t)P + (size_t)t] =
                    (double)q->batch_out[(size_t)t * (size_t)K + (size_t)k];
        return;
    }
    for (j = 0; j < np; j++) {
        axv[j] = psyq__param_axis(q, q->perm[j]);
        idx[j] = 0;
        pv[q->perm[j]] = axv[j][0];
    }
    for (t = 0; t < P; t++) {
        psyq__eval_into(q, stim, pv, out + t, (size_t)P);
        for (j = np - 1; j >= 0; j--) {
            if (++idx[j] < q->n_param_int[j]) { pv[q->perm[j]] = axv[j][idx[j]]; break; }
            idx[j] = 0;
            pv[q->perm[j]] = axv[j][0];
        }
    }
}

static void psyq__lik_at_index(const psyq_quest* q, int s, double* out) {
    double sv[PSYQ_MAX_STIM_DIMS];
    psyq__stim_vec(q, s, sv);
    psyq__lik_at(q, sv, out);
}

/* ======================================================================= *
 *  POSTERIOR SUMMARIES
 * ======================================================================= */

static double psyq__entropy_of(const double* v, int n) {
    double h = 0.0;
    int i;
    for (i = 0; i < n; i++) h -= psyq__wlog2(v[i]);
    return h;
}

/* The entropy of the posterior is a constant of every joint selection, so it
 * is computed once per posterior and kept in the arena. The cache is written
 * through a pointer, which is what lets the const functions fill it. */
static double psyq__h_post(const psyq_quest* q) {
    if (q->cache[1] == 0.0) {
        q->cache[0] = psyq__entropy_of(q->posterior, q->P);   /* already double */
        q->cache[1] = 1.0;
    }
    return q->cache[0];
}

/* Marginal of one PUBLIC parameter axis. */
static void psyq__marginal_axis(const psyq_quest* q, int axis, double* out) {
    const double* post = q->posterior;
    int j, n;
    if (!psyq__param_axis_ok(q, axis)) return;   /* callers pass valid axes */
    j = q->inv_perm[axis];
    n = q->n_param_int[j];
    int stride = psyq__int_stride(q, j);
    int i, b;
    size_t t = 0, P = (size_t)q->P;
    for (i = 0; i < n; i++) out[i] = 0.0;
    while (t < P) {
        for (i = 0; i < n; i++) {
            double s = 0.0;
            for (b = 0; b < stride; b++) s += post[t++];
            out[i] += s;
        }
    }
}

/* Posterior marginalized over the nuisance axes, into out[n_marg]. Those axes
 * are first internally, so the marginal is the fast axis and this is B passes
 * of one contiguous add. */
static void psyq__marginal_free(const psyq_quest* q, double* out) {
    const double* post = q->posterior;
    int B = q->nuis_block, M = q->n_marg, m, b;
    for (m = 0; m < M; m++) out[m] = post[m];
    for (b = 1; b < B; b++) {
        const double* p = post + (size_t)b * (size_t)M;
        for (m = 0; m < M; m++) out[m] += p[m];
    }
}

/* ======================================================================= *
 *  SELECTION
 * ======================================================================= */

/* The joint selection score, from the table.
 *
 * Watson's expected entropy decomposes. With w_k(theta) = post(theta) L, and
 * because the likelihoods of one cell sum to 1 over the outcomes,
 *
 *   sum_k sum_theta w log2 w = sum_theta post log2 post
 *                              + sum_theta post sum_k L log2 L,
 *
 * so  E[H] = H(theta) - H(y | s) + sum_theta post(theta) h(s, theta),  with
 * h(s, theta) = -sum_k L log2 L the outcome entropy of one cell. h depends on
 * the table alone, so open() tabulates it as the K+1-th row and the sweep has
 * no logarithm left in it: K+1 dot products and K logarithms per stimulus.
 * H(theta) is the same for every stimulus, so the selection leaves it out and
 * psyq_expected_entropy() adds it back. */
static double psyq__ee_joint_table(const psyq_quest* q, const float* base) {
    const double* post = q->posterior;
    int P = q->P, K = q->K, k;
    double e = 0.0;
    for (k = 0; k < K; k++)
        e += psyq__wlog2(psyq__dot_f(post, base + (size_t)k * (size_t)P, P));
    return e + psyq__dot_f(post, base + (size_t)K * (size_t)P, P);
}

/* The same quantity from the definition, for the no-table path, which has no
 * tabulated h to read:  E[H] = sum_k (p_k log2 p_k - sum_theta w log2 w).
 * This one is the true expected entropy, not the offset one. */
static double psyq__ee_joint_lik(const psyq_quest* q, const double* lik) {
    const double* post = q->posterior;
    int P = q->P, K = q->K, t, k;
    double e = 0.0;
    for (k = 0; k < K; k++) {
        const double* row = lik + (size_t)k * (size_t)P;
        double pk = 0.0, slw = 0.0;
        for (t = 0; t < P; t++) {
            double w = post[t] * row[t];
            pk += w;
            slw += psyq__wlog2(w);
        }
        e += psyq__wlog2(pk) - slw;
    }
    return e;
}

/* With nuisance axes flagged, the entropy is that of the marginal (Watson
 * 2017 sec. 2.5, Prins 2013), so the logarithms are taken on the marginal:
 * K per stimulus for the outcome, K*n_marg for the marginals themselves.
 * The nuisance axes are first internally, so one outcome's marginal is B
 * passes of psyq__fma over n_marg contiguous values, and the sweep still
 * reads the row front to back. */
static double psyq__ee_marg(const psyq_quest* q, const float* base, const double* lik) {
    const double* post = q->posterior;
    double* acc = q->marginal_scratch;
    int P = q->P, K = q->K, B = q->nuis_block, M = q->n_marg, k, m, b;
    double e = 0.0;
    for (k = 0; k < K; k++) {
        size_t off = (size_t)k * (size_t)P;
        double pk = 0.0, slw = 0.0;
        memset(acc, 0, (size_t)M * sizeof(double));
        if (base) {
            for (b = 0; b < B; b++)
                psyq__fma_f(acc, post + (size_t)b * (size_t)M,
                            base + off + (size_t)b * (size_t)M, M);
        } else {
            for (b = 0; b < B; b++)
                psyq__fma_d(acc, post + (size_t)b * (size_t)M,
                            lik + off + (size_t)b * (size_t)M, M);
        }
        for (m = 0; m < M; m++) { pk += acc[m]; slw += psyq__wlog2(acc[m]); }
        e += psyq__wlog2(pk) - slw;
    }
    return e;
}

static const float* psyq__row(const psyq_quest* q, int s, int k) {
    return q->table + ((size_t)s * (size_t)q->rows + (size_t)k) * (size_t)q->P;
}

/* What psyq__score_entropy() leaves out of the expected entropy: the same
 * constant for every stimulus, so it cannot change an argmin or a tie, and
 * only psyq_expected_entropy() has to add it back. */
static double psyq__score_offset(const psyq_quest* q) {
    return (q->rows > q->K) ? psyq__h_post(q) : 0.0;
}

static double psyq__score_entropy(const psyq_quest* q, int s) {
    if (q->table) {
        const float* base = psyq__row(q, s, 0);
        if (q->has_nuisance) return psyq__ee_marg(q, base, NULL);
        return psyq__ee_joint_table(q, base);
    }
    psyq__lik_at_index(q, s, q->scratch);
    if (q->has_nuisance) return psyq__ee_marg(q, NULL, q->scratch);
    return psyq__ee_joint_lik(q, q->scratch);
}

/* The QUEST-style placement target: a point estimate of one parameter, in the
 * units of stimulus axis 0. */
static double psyq__placement(const psyq_quest* q) {
    int axis = q->desc.select_param;
    int n;
    const double* v = psyq__param_axis(q, axis);
    double* m = q->marginal_scratch;
    int i;
    if (!psyq__param_axis_ok(q, axis)) return 0.0;   /* open() validated it */
    n = q->n_param_axis[axis];
    psyq__marginal_axis(q, axis, m);
    if (q->desc.select == PSYQ_SELECT_MEAN) {
        double s = 0.0;
        for (i = 0; i < n; i++) s += m[i] * v[i];
        return s;
    }
    if (q->desc.select == PSYQ_SELECT_MODE) {
        int best = 0;
        for (i = 1; i < n; i++) if (m[i] > m[best]) best = i;
        return v[best];
    }
    {   /* PSYQ_SELECT_QUANTILE */
        double c = 0.0, p = q->desc.select_quantile;
        for (i = 0; i < n; i++) {
            c += m[i];
            if (c >= p) return v[i];
        }
        return v[n - 1];
    }
}

static int psyq__draw_subset(psyq_quest* q) {
    int m = q->desc.subset_size, S = q->S, i, b;
    int* out = q->subset_scratch;
    if (m >= S) { for (i = 0; i < S; i++) out[i] = i; return S; }
    for (i = 0; i < m; i++) {
        int j = 0, tries;
        bool dup = true;
        for (tries = 0; tries < 64 && dup; tries++) {
            double u = q->desc.rng(q->desc.rng_ctx);
            if (!(u >= 0.0) || !(u < 1.0)) u = 0.0;   /* a bad variate must not index out of range */
            j = (int)(u * (double)S);
            if (j >= S) j = S - 1;
            if (j < 0) j = 0;
            dup = false;
            for (b = 0; b < i; b++) if (out[b] == j) { dup = true; break; }
        }
        if (dup) {
            /* An unlucky or degenerate generator must still terminate, so the
             * last resort is the first free index above the one drawn. */
            int step;
            for (step = 0; step < S; step++) {
                int c = (j + step) % S;
                bool used = false;
                for (b = 0; b < i; b++) if (out[b] == c) { used = true; break; }
                if (!used) { j = c; break; }
            }
        }
        out[i] = j;
    }
    return m;
}

/* Score n candidates and resolve the argmin per desc.tiebreak. `cand` is the
 * candidate list, or NULL for the whole grid. */
static int psyq__select_from(psyq_quest* q, const int* cand, int n) {
    double* sc = q->scores;
    double best = HUGE_VAL, thr, target = 0.0;
    int i, count = 0, pick = -1;
    bool entropy = (q->desc.select == PSYQ_SELECT_ENTROPY);

    if (entropy) {
        for (i = 0; i < n; i++) {
            sc[i] = psyq__score_entropy(q, cand ? cand[i] : i);
            if (sc[i] < best) best = sc[i];
        }
    } else {
        /* The placement rules score the distance from stimulus axis 0, the
         * slowest axis, so its index is the flat index over that axis's
         * stride and the other axes only make ties. */
        const double* ax0 = psyq__stim_axis(q, 0);
        int stride0 = psyq__stim_stride(q, 0);
        target = psyq__placement(q);
        for (i = 0; i < n; i++) {
            sc[i] = fabs(ax0[(cand ? cand[i] : i) / stride0] - target);
            if (sc[i] < best) best = sc[i];
        }
    }
    thr = best + q->desc.tie_tolerance;
    for (i = 0; i < n; i++) if (sc[i] <= thr) count++;
    if (count == 0) return cand ? cand[0] : 0;   /* every score was a NaN */

    switch (q->desc.tiebreak) {
    case PSYQ_TIE_NEAREST: {
        int bestd = -1;
        for (i = 0; i < n; i++) {
            if (sc[i] > thr) continue;
            {
                int s = cand ? cand[i] : i;
                int d = (q->last_shown >= 0) ? (s > q->last_shown ? s - q->last_shown
                                                                  : q->last_shown - s) : 0;
                if (bestd < 0 || d < bestd) { bestd = d; pick = i; }
                if (q->last_shown < 0) return cand ? cand[i] : i;
            }
        }
        break;
    }
    case PSYQ_TIE_ALTERNATE: {
        if (count > 1 && q->tie_parity) {
            for (i = n - 1; i >= 0; i--) if (sc[i] <= thr) { pick = i; break; }
        } else {
            for (i = 0; i < n; i++) if (sc[i] <= thr) { pick = i; break; }
        }
        if (count > 1) q->tie_parity ^= 1;
        break;
    }
    case PSYQ_TIE_RANDOM: {
        int want = 0, seen = 0;
        if (count > 1) {
            double u = q->desc.rng(q->desc.rng_ctx);
            if (!(u >= 0.0) || !(u < 1.0)) u = 0.0;
            want = (int)(u * (double)count);
            if (want >= count) want = count - 1;
            if (want < 0) want = 0;
        }
        for (i = 0; i < n; i++) {
            if (sc[i] > thr) continue;
            if (seen++ == want) { pick = i; break; }
        }
        break;
    }
    case PSYQ_TIE_LOWEST:
    default:
        for (i = 0; i < n; i++) if (sc[i] <= thr) { pick = i; break; }
        break;
    }
    if (pick < 0) pick = 0;
    return cand ? cand[pick] : pick;
}

/* ======================================================================= *
 *  STOPPING
 * ======================================================================= */

static void psyq__update_stop(psyq_quest* q) {
    const psyq_desc* d = &q->desc;
    if (q->stop != PSYQ_STOP_NONE) return;   /* the FIRST criterion to fire */
    if (d->stop_trials > 0 && q->n_trials >= d->stop_trials) { q->stop = PSYQ_STOP_TRIALS; return; }
    if (d->stop_entropy > 0.0 && psyq_entropy(q) <= d->stop_entropy) { q->stop = PSYQ_STOP_ENTROPY; return; }
    if (d->stop_sd > 0.0 && psyq_sd(q, d->stop_sd_param) <= d->stop_sd) { q->stop = PSYQ_STOP_SD; return; }
    if (q->n_trials >= PSYQ_MAX_TRIALS) q->stop = PSYQ_STOP_FULL;
}

/* ======================================================================= *
 *  PUBLIC API
 * ======================================================================= */

PSYQ_API const char* psyq_strerror(int code) {
    switch (code) {
    case PSYQ_ERR_ARG:    return "invalid argument";
    case PSYQ_ERR_CLOSED: return "handle is not open";
    case PSYQ_ERR_FULL:   return "trial history is full";
    case PSYQ_ERR_MEMORY: return "out of memory";
#ifdef PSYQ_ASYNC
    case PSYQ_ERR_BUSY:    return "queue is full";
    case PSYQ_ERR_TIMEOUT: return "timed out";
#endif
    default:              return (code >= 0) ? "ok" : "unknown error";
    }
}

PSYQ_API const char* psyq_version(void) { return PSYQ_VERSION_STRING; }

PSYQ_API psyq_axis psyq_linspace(double lo, double hi, int n) {
    psyq_axis a;
    memset(&a, 0, sizeof(a));
    a.lo = lo; a.hi = hi; a.n = n;
    return a;
}

PSYQ_API psyq_axis psyq_values(const double* values, int n) {
    psyq_axis a;
    memset(&a, 0, sizeof(a));
    a.values = values; a.n = n;
    if (values && n > 0) { a.lo = values[0]; a.hi = values[n - 1]; }
    return a;
}

PSYQ_API psyq_axis psyq_fixed(double value) {
    psyq_axis a;
    memset(&a, 0, sizeof(a));
    a.lo = a.hi = value; a.n = 1;
    return a;
}

PSYQ_API bool psyq_prior_normal(const psyq_axis* axis, double mean, double sd, double* out) {
    int i;
    if (!axis || !out || axis->n < 1) return false;
    if (!psyq__finite(mean) || !psyq__finite(sd) || sd <= 0.0) return false;
    if (!psyq__axis_ok(axis, true, "prior", 0, NULL, 0)) return false;
    for (i = 0; i < axis->n; i++) {
        double z = (psyq__axis_at(axis, i) - mean) / sd;
        out[i] = exp(-0.5 * z * z);
    }
    return true;
}

PSYQ_API size_t psyq_memory_size(const psyq_desc* desc) {
    psyq__sizes z;
    if (!psyq__sizes_of(desc, &z, NULL, 0)) return 0;
    /* PSYQ__ALIGN - 1 spare bytes: a caller's buffer need not be aligned for
     * a double, and open() aligns the base rather than refusing it. */
    return psyq__plan(&z, NULL, NULL) + (PSYQ__ALIGN - 1);
}

PSYQ_API bool psyq_open(psyq_quest* q, const psyq_desc* desc) {
    psyq__sizes z;
    size_t need, t;
    unsigned char* base;
    int i, j, k;
    double sv[PSYQ_MAX_STIM_DIMS];
    /* A batch callback writes floats, so K of them cannot sum closer to 1
     * than about K times a float's last digit; a per-cell callback writes
     * doubles and has no such excuse. */
    double sum_tol = (desc && desc->pf_batch) ? 1e-6 : 1e-9;

    if (!q) return false;
    memset(q->error, 0, sizeof(q->error));
    q->open = false;
    if (!desc) {
        snprintf(q->error, sizeof(q->error), "psy_quest: null desc");
        return false;
    }
    if (!psyq__sizes_of(desc, &z, q->error, sizeof(q->error))) return false;

    q->desc = *desc;
    q->S = z.S; q->P = z.P; q->K = z.K; q->rows = z.rows;
    q->n_marg = z.n_marg;
    q->nuis_block = z.nuis_block;
    q->has_nuisance = (z.nuis_block > 1);
    q->permuted = z.permuted;
    for (i = 0; i < desc->n_stim; i++)  q->n_stim_axis[i]  = desc->stim[i].n;
    for (i = 0; i < desc->n_param; i++) q->n_param_axis[i] = desc->param[i].n;
    for (i = 0; i < desc->n_param; i++) {
        q->perm[i] = z.perm[i];
        q->inv_perm[z.perm[i]] = i;
        q->n_param_int[i] = desc->param[z.perm[i]].n;
    }
    if (q->desc.select_quantile == 0.0) q->desc.select_quantile = 0.5;
    if (q->desc.tie_tolerance == 0.0)   q->desc.tie_tolerance = 1e-9;
    q->desc.subset_size = z.n_subset;

    need = psyq__plan(&z, NULL, NULL) + (PSYQ__ALIGN - 1);
    if (desc->memory) {
        if (desc->memory_size < need) {
            snprintf(q->error, sizeof(q->error),
                     "psy_quest: memory buffer too small, need %llu bytes, got %llu (%s)",
                     (unsigned long long)need, (unsigned long long)desc->memory_size,
                     psyq_strerror(PSYQ_ERR_MEMORY));
            return false;
        }
        q->mem = desc->memory;
        q->mem_owned = false;
    } else {
        q->mem = PSYQ_MALLOC(need);
        if (!q->mem) {
            snprintf(q->error, sizeof(q->error),
                     "psy_quest: allocation of %llu bytes failed (%s)",
                     (unsigned long long)need, psyq_strerror(PSYQ_ERR_MEMORY));
            return false;
        }
        q->mem_owned = true;
    }
    q->mem_size = need;
    base = (unsigned char*)q->mem;
    base += (size_t)((PSYQ__ALIGN - ((uintptr_t)base & (PSYQ__ALIGN - 1))) & (PSYQ__ALIGN - 1));
    psyq__plan(&z, base, q);

    /* The handle keeps no pointer into the caller's arrays: axis values are
     * copied here and the priors are consumed into the posterior below. */
    {
        double* p = q->stim_values;
        for (i = 0; i < desc->n_stim; i++)
            for (j = 0; j < desc->stim[i].n; j++) *p++ = psyq__axis_at(&desc->stim[i], j);
        p = q->param_values;
        for (i = 0; i < desc->n_param; i++)
            for (j = 0; j < desc->param[i].n; j++) *p++ = psyq__axis_at(&desc->param[i], j);
        for (i = 0; i < PSYQ_MAX_STIM_DIMS; i++) { q->desc.stim[i].values = NULL; q->desc.stim[i].prior = NULL; }
        for (i = 0; i < PSYQ_MAX_PARAMS; i++)    { q->desc.param[i].values = NULL; q->desc.param[i].prior = NULL; }
        q->desc.joint_prior = NULL;
        q->desc.memory = NULL;
        /* A built-in psychometric function ignores both callbacks, so drop
         * them here and the dispatch below has one thing to look at. */
        if (desc->pf != PSYQ_PF_CUSTOM) { q->desc.pf_fn = NULL; q->desc.pf_batch = NULL; }
    }

    /* The batch callback is handed the whole parameter grid as a matrix, in
     * the order the header walks it, so it is built once here and reused by
     * every call. */
    if (q->param_matrix) {
        double* w = q->param_matrix;
        for (t = 0; t < (size_t)q->P; t++) {
            psyq__param_vec_int(q, (int)t, w);
            w += desc->n_param;
        }
    }

    /* Prior: the product of the per-axis weights, times the joint weights.
     * The walk is in internal order and carries the caller's flat index along
     * with it, because joint_prior is in the caller's order. */
    {
        double sum = 0.0;
        int idx[PSYQ_MAX_PARAMS], pub_stride[PSYQ_MAX_PARAMS];
        int pub = 0;
        for (i = 0; i < desc->n_param; i++) pub_stride[i] = psyq__param_stride(q, i);
        memset(idx, 0, sizeof(idx));
        for (t = 0; t < (size_t)q->P; t++) {
            double w = 1.0;
            for (i = 0; i < desc->n_param; i++)
                if (desc->param[q->perm[i]].prior) w *= desc->param[q->perm[i]].prior[idx[i]];
            if (desc->joint_prior) w *= desc->joint_prior[pub];
            q->posterior[t] = w;
            sum += w;
            for (i = desc->n_param - 1; i >= 0; i--) {
                if (++idx[i] < q->n_param_int[i]) { pub += pub_stride[q->perm[i]]; break; }
                idx[i] = 0;
                pub -= (q->n_param_int[i] - 1) * pub_stride[q->perm[i]];
            }
        }
        if (!(sum > 0.0) || !psyq__finite(sum)) {
            snprintf(q->error, sizeof(q->error), "psy_quest: the prior has no mass");
            if (q->mem_owned) PSYQ_FREE(q->mem);
            q->mem = NULL;
            return false;
        }
        for (t = 0; t < (size_t)q->P; t++) q->posterior[t] /= sum;
    }

    /* The table, S*P pf evaluations, written front to back in the order the
     * selection sweep reads it: one row per outcome, then the outcome entropy
     * of each cell where that row is kept. */
    if (q->table) {
        float* w = q->table;
        for (i = 0; i < q->S; i++) {
            psyq__stim_vec(q, i, sv);
            psyq__lik_at(q, sv, q->scratch);
            if (desc->pf == PSYQ_PF_CUSTOM) {
                const char* who = desc->pf_batch ? "pf_batch" : "pf_fn";
                int c;
                for (c = 0; c < q->P; c++) {
                    double s = 0.0;
                    for (k = 0; k < q->K; k++) {
                        double v = q->scratch[(size_t)k * (size_t)q->P + (size_t)c];
                        if (!psyq__finite(v) || v < 0.0) {
                            snprintf(q->error, sizeof(q->error),
                                     "psy_quest: %s wrote %g for outcome %d at stimulus %d, parameter %d",
                                     who, v, k, i, c);
                            if (q->mem_owned) PSYQ_FREE(q->mem);
                            q->mem = NULL;
                            return false;
                        }
                        s += v;
                    }
                    if (fabs(s - 1.0) > sum_tol) {
                        snprintf(q->error, sizeof(q->error),
                                 "psy_quest: %s outcomes sum to %.12g at stimulus %d, parameter %d, need 1",
                                 who, s, i, c);
                        if (q->mem_owned) PSYQ_FREE(q->mem);
                        q->mem = NULL;
                        return false;
                    }
                }
            }
            for (t = 0; t < (size_t)q->P * (size_t)q->K; t++) *w++ = (float)q->scratch[t];
            if (q->rows > q->K) {
                /* The entropy of the row AS STORED, so the decomposition the
                 * selection uses is exact against the floats it reads and two
                 * callbacks that write the same floats give the same table. */
                const float* stored = w - (size_t)q->K * (size_t)q->P;
                int c;
                for (c = 0; c < q->P; c++) {
                    double h = 0.0;
                    for (k = 0; k < q->K; k++)
                        h -= psyq__wlog2((double)stored[(size_t)k * (size_t)q->P + (size_t)c]);
                    *w++ = (float)h;
                }
            }
        }
    } else if (desc->pf == PSYQ_PF_CUSTOM) {
        /* No table to check cell by cell, so check the corners and the middle. */
        int si[3], pi[3], a, b;
        si[0] = 0; si[1] = q->S / 2; si[2] = q->S - 1;
        pi[0] = 0; pi[1] = q->P / 2; pi[2] = q->P - 1;
        for (a = 0; a < 3; a++) {
            double pv[PSYQ_MAX_PARAMS];
            double pp[PSYQ_MAX_OUTCOMES];
            psyq__stim_vec(q, si[a], sv);
            for (b = 0; b < 3; b++) {
                double s = 0.0;
                psyq__param_vec_int(q, pi[b], pv);
                psyq__eval(q, sv, pv, pp);
                for (k = 0; k < q->K; k++) {
                    if (!psyq__finite(pp[k]) || pp[k] < 0.0) s = PSYQ__NAN;
                    else s += pp[k];
                }
                if (!(fabs(s - 1.0) <= sum_tol)) {
                    snprintf(q->error, sizeof(q->error),
                             "psy_quest: %s outcomes sum to %.12g at stimulus %d, parameter %d, need 1",
                             desc->pf_batch ? "pf_batch" : "pf_fn", s, si[a], pi[b]);
                    if (q->mem_owned) PSYQ_FREE(q->mem);
                    q->mem = NULL;
                    return false;
                }
            }
        }
    }

    q->cache[0] = 0.0;
    q->cache[1] = 0.0;
    q->proposed = -1;
    q->last_shown = -1;
    q->tie_parity = 0;
    q->n_trials = 0;
    q->stop = PSYQ_STOP_NONE;
    q->open = true;
    psyq__update_stop(q);
    return true;
}

PSYQ_API void psyq_close(psyq_quest* q) {
    if (!q) return;
    if (q->mem && q->mem_owned) PSYQ_FREE(q->mem);
    q->mem = NULL;
    q->mem_size = 0;
    q->mem_owned = false;
    q->table = NULL;
    q->cache = NULL;
    q->posterior = NULL;
    q->post_public = NULL;
    q->scratch = NULL;
    q->param_matrix = NULL;
    q->batch_out = NULL;
    q->marginal_scratch = NULL;
    q->scores = NULL;
    q->stim_values = NULL;
    q->param_values = NULL;
    q->subset_scratch = NULL;
    q->open = false;
    q->n_trials = 0;
    q->proposed = -1;
    q->last_shown = -1;
    q->stop = PSYQ_STOP_NONE;
}

PSYQ_API const char* psyq_error(const psyq_quest* q) { return q ? q->error : ""; }
PSYQ_API bool psyq_is_open(const psyq_quest* q) { return q && q->open; }

PSYQ_API int psyq_n_stim(const psyq_quest* q)     { return (q && q->open) ? q->S : PSYQ_ERR_CLOSED; }
PSYQ_API int psyq_n_param(const psyq_quest* q)    { return (q && q->open) ? q->P : PSYQ_ERR_CLOSED; }
PSYQ_API int psyq_n_outcomes(const psyq_quest* q) { return (q && q->open) ? q->K : PSYQ_ERR_CLOSED; }

PSYQ_API double psyq_stim_value(const psyq_quest* q, int index, int axis) {
    if (!q || !q->open || index < 0 || index >= q->S) return PSYQ__NAN;
    if (!psyq__stim_axis_ok(q, axis)) return PSYQ__NAN;
    return psyq__stim_axis(q, axis)[(index / psyq__stim_stride(q, axis)) % q->n_stim_axis[axis]];
}

PSYQ_API int psyq_stim_values(const psyq_quest* q, int index, double* out) {
    double sv[PSYQ_MAX_STIM_DIMS];
    int n;
    if (!q) return PSYQ_ERR_ARG;
    if (!q->open) return PSYQ_ERR_CLOSED;
    if (index < 0 || index >= q->S || !out) return PSYQ_ERR_ARG;
    n = psyq__ns(q);
    psyq__stim_vec(q, index, sv);
    memcpy(out, sv, (size_t)n * sizeof(double));
    return n;
}

PSYQ_API int psyq_stim_index(const psyq_quest* q, const int* sub) {
    int sb[PSYQ_MAX_STIM_DIMS];
    int i, n, flat = 0;
    if (!q) return PSYQ_ERR_ARG;
    if (!q->open) return PSYQ_ERR_CLOSED;
    if (!sub) return PSYQ_ERR_ARG;
    n = psyq__ns(q);
    memcpy(sb, sub, (size_t)n * sizeof(int));
    for (i = 0; i < n; i++) {
        if (sb[i] < 0 || sb[i] >= q->n_stim_axis[i]) return PSYQ_ERR_ARG;
        flat = flat * q->n_stim_axis[i] + sb[i];
    }
    return flat;
}

PSYQ_API int psyq_stim_nearest(const psyq_quest* q, const double* stim) {
    double sv[PSYQ_MAX_STIM_DIMS];
    int i, j, ns, flat = 0;
    if (!q) return PSYQ_ERR_ARG;
    if (!q->open) return PSYQ_ERR_CLOSED;
    if (!stim) return PSYQ_ERR_ARG;
    ns = psyq__ns(q);
    memcpy(sv, stim, (size_t)ns * sizeof(double));
    for (i = 0; i < ns; i++) {
        const double* v = psyq__stim_axis(q, i);
        int n = q->n_stim_axis[i], best = 0;
        double bd;
        if (!psyq__finite(sv[i])) return PSYQ_ERR_ARG;
        bd = fabs(v[0] - sv[i]);
        for (j = 1; j < n; j++) {
            double d = fabs(v[j] - sv[i]);
            if (d < bd) { bd = d; best = j; }
        }
        flat = flat * n + best;
    }
    return flat;
}

PSYQ_API double psyq_param_value(const psyq_quest* q, int index, int axis) {
    if (!q || !q->open || index < 0 || index >= q->P) return PSYQ__NAN;
    if (!psyq__param_axis_ok(q, axis)) return PSYQ__NAN;
    return psyq__param_axis(q, axis)[(index / psyq__param_stride(q, axis)) % q->n_param_axis[axis]];
}

PSYQ_API int psyq_param_values(const psyq_quest* q, int index, double* out) {
    double pv[PSYQ_MAX_PARAMS];
    int n;
    if (!q) return PSYQ_ERR_ARG;
    if (!q->open) return PSYQ_ERR_CLOSED;
    if (index < 0 || index >= q->P || !out) return PSYQ_ERR_ARG;
    n = psyq__np(q);
    psyq__param_vec(q, index, pv);
    memcpy(out, pv, (size_t)n * sizeof(double));
    return n;
}

PSYQ_API int psyq_param_index(const psyq_quest* q, const int* sub) {
    int sb[PSYQ_MAX_PARAMS];
    int i, n, flat = 0;
    if (!q) return PSYQ_ERR_ARG;
    if (!q->open) return PSYQ_ERR_CLOSED;
    if (!sub) return PSYQ_ERR_ARG;
    n = psyq__np(q);
    memcpy(sb, sub, (size_t)n * sizeof(int));
    for (i = 0; i < n; i++) {
        if (sb[i] < 0 || sb[i] >= q->n_param_axis[i]) return PSYQ_ERR_ARG;
        flat = flat * q->n_param_axis[i] + sb[i];
    }
    return flat;
}

PSYQ_API int psyq_stim_axis_n(const psyq_quest* q, int axis) {
    if (!q) return PSYQ_ERR_ARG;
    if (!q->open) return PSYQ_ERR_CLOSED;
    if (!psyq__stim_axis_ok(q, axis)) return PSYQ_ERR_ARG;
    return q->n_stim_axis[axis];
}

/* n_param_axis is in the caller's order whatever LAYOUT did to the posterior,
 * so this needs no translation. */
PSYQ_API int psyq_param_axis_n(const psyq_quest* q, int axis) {
    if (!q) return PSYQ_ERR_ARG;
    if (!q->open) return PSYQ_ERR_CLOSED;
    if (!psyq__param_axis_ok(q, axis)) return PSYQ_ERR_ARG;
    return q->n_param_axis[axis];
}

PSYQ_API int psyq_next(psyq_quest* q) {
    if (!q) return PSYQ_ERR_ARG;
    if (!q->open) return PSYQ_ERR_CLOSED;
    if (q->proposed >= 0) return q->proposed;
    if (q->desc.subset_size > 0) {
        int n = psyq__draw_subset(q);
        q->proposed = psyq__select_from(q, q->subset_scratch, n);
    } else {
        q->proposed = psyq__select_from(q, NULL, q->S);
    }
    return q->proposed;
}

PSYQ_API int psyq_next_subset(psyq_quest* q, const int* subset, int n) {
    int i;
    if (!q) return PSYQ_ERR_ARG;
    if (!q->open) return PSYQ_ERR_CLOSED;
    if (!subset || n < 1 || n > q->S) return PSYQ_ERR_ARG;
    for (i = 0; i < n; i++) if (subset[i] < 0 || subset[i] >= q->S) return PSYQ_ERR_ARG;
    q->proposed = psyq__select_from(q, subset, n);
    return q->proposed;
}

PSYQ_API double psyq_expected_entropy(const psyq_quest* q, int index) {
    if (!q || !q->open || index < 0 || index >= q->S) return PSYQ__NAN;
    return psyq__score_entropy(q, index) + psyq__score_offset(q);
}

/* One Bayes step against one outcome's likelihood row, which the transposed
 * layout makes contiguous. The mass is summed before anything is written, so
 * an outcome no parameter point can produce leaves the posterior alone
 * instead of destroying it. */
static int psyq__apply(psyq_quest* q, const float* row, const double* lik) {
    double sum = row ? psyq__dot_f(q->posterior, row, q->P)
                     : psyq__dot_d(q->posterior, lik, q->P);
    if (!(sum > 0.0) || !psyq__finite(sum)) return PSYQ_ERR_ARG;
    if (row) psyq__scale_f(q->posterior, row, 1.0 / sum, q->P);
    else     psyq__scale_d(q->posterior, lik, 1.0 / sum, q->P);
    q->cache[1] = 0.0;   /* the posterior moved, so its entropy is stale */
    return PSYQ_OK;
}

/* `stim` is always a local of PSYQ_MAX_STIM_DIMS here, never a caller's
 * array, so reading all of it is in bounds by construction. */
static void psyq__record(psyq_quest* q, const double* stim, int stim_index, int outcome) {
    psyq_trial* tr = &q->history[q->n_trials++];
    int i, n = psyq__ns(q);
    for (i = 0; i < PSYQ_MAX_STIM_DIMS; i++) tr->stim[i] = (i < n) ? stim[i] : 0.0;
    tr->stim_index = stim_index;
    tr->proposed_index = q->proposed;
    tr->outcome = (uint8_t)outcome;
    q->proposed = -1;
    q->last_shown = stim_index;
    psyq__update_stop(q);
}

PSYQ_API int psyq_update(psyq_quest* q, int index, int outcome) {
    double sv[PSYQ_MAX_STIM_DIMS];
    int rc;
    if (!q) return PSYQ_ERR_ARG;
    if (!q->open) return PSYQ_ERR_CLOSED;
    if (index < 0 || index >= q->S) return PSYQ_ERR_ARG;
    if (outcome < 0 || outcome >= q->K) return PSYQ_ERR_ARG;
    if (q->n_trials >= PSYQ_MAX_TRIALS) return PSYQ_ERR_FULL;
    if (q->table) {
        rc = psyq__apply(q, psyq__row(q, index, outcome), NULL);
    } else {
        psyq__lik_at_index(q, index, q->scratch);
        rc = psyq__apply(q, NULL, q->scratch + (size_t)outcome * (size_t)q->P);
    }
    if (rc != PSYQ_OK) return rc;
    psyq__stim_vec(q, index, sv);
    psyq__record(q, sv, index, outcome);
    return PSYQ_OK;
}

PSYQ_API int psyq_update_values(psyq_quest* q, const double* stim, int outcome) {
    double sv[PSYQ_MAX_STIM_DIMS];
    int i, n, rc;
    if (!q) return PSYQ_ERR_ARG;
    if (!q->open) return PSYQ_ERR_CLOSED;
    if (!stim || outcome < 0 || outcome >= q->K) return PSYQ_ERR_ARG;
    n = psyq__ns(q);
    for (i = 0; i < PSYQ_MAX_STIM_DIMS; i++) sv[i] = 0.0;
    memcpy(sv, stim, (size_t)n * sizeof(double));
    for (i = 0; i < n; i++) if (!psyq__finite(sv[i])) return PSYQ_ERR_ARG;
    if (q->n_trials >= PSYQ_MAX_TRIALS) return PSYQ_ERR_FULL;
    psyq__lik_at(q, sv, q->scratch);
    rc = psyq__apply(q, NULL, q->scratch + (size_t)outcome * (size_t)q->P);
    if (rc != PSYQ_OK) return rc;
    psyq__record(q, sv, -1, outcome);
    return PSYQ_OK;
}

PSYQ_API bool psyq_done(const psyq_quest* q) {
    return q && q->open && q->stop != PSYQ_STOP_NONE;
}

PSYQ_API psyq_stop psyq_stop_reason(const psyq_quest* q) {
    return (q && q->open) ? q->stop : PSYQ_STOP_NONE;
}

PSYQ_API int psyq_estimate(const psyq_quest* q, psyq_estimator how, double* out) {
    double est[PSYQ_MAX_PARAMS];
    int i, np;
    if (!q) return PSYQ_ERR_ARG;
    if (!q->open) return PSYQ_ERR_CLOSED;
    if (!out) return PSYQ_ERR_ARG;
    np = psyq__np(q);
    for (i = 0; i < PSYQ_MAX_PARAMS; i++) est[i] = 0.0;
    switch (how) {
    case PSYQ_EST_MODE: {
        int best = 0;
        for (i = 1; i < q->P; i++) if (q->posterior[i] > q->posterior[best]) best = i;
        psyq__param_vec_int(q, best, est);
        break;
    }
    case PSYQ_EST_MEDIAN:
        for (i = 0; i < np; i++) est[i] = psyq_quantile(q, i, 0.5);
        break;
    case PSYQ_EST_MEAN: {
        for (i = 0; i < np; i++) {
            const double* v = psyq__param_axis(q, i);
            double* m = q->marginal_scratch;
            double s = 0.0;
            int j;
            psyq__marginal_axis(q, i, m);
            for (j = 0; j < q->n_param_axis[i]; j++) s += (double)m[j] * v[j];
            est[i] = s;
        }
        break;
    }
    default:
        return PSYQ_ERR_ARG;
    }
    memcpy(out, est, (size_t)np * sizeof(double));
    return PSYQ_OK;
}

PSYQ_API double psyq_quantile(const psyq_quest* q, int axis, double p) {
    const double* v;
    double* m;
    double c = 0.0;
    int i, n;
    if (!q || !q->open) return PSYQ__NAN;
    if (!psyq__param_axis_ok(q, axis)) return PSYQ__NAN;
    if (!(p > 0.0) || !(p < 1.0)) return PSYQ__NAN;
    n = q->n_param_axis[axis];
    v = psyq__param_axis(q, axis);
    m = q->marginal_scratch;
    psyq__marginal_axis(q, axis, m);
    for (i = 0; i < n; i++) {
        c += m[i];
        if (c >= p) return v[i];
    }
    return v[n - 1];
}

PSYQ_API double psyq_sd(const psyq_quest* q, int axis) {
    const double* v;
    double* m;
    double s = 0.0, s2 = 0.0, var;
    int i, n;
    if (!q || !q->open) return PSYQ__NAN;
    if (!psyq__param_axis_ok(q, axis)) return PSYQ__NAN;
    n = q->n_param_axis[axis];
    v = psyq__param_axis(q, axis);
    m = q->marginal_scratch;
    psyq__marginal_axis(q, axis, m);
    for (i = 0; i < n; i++) { s += m[i] * v[i]; s2 += m[i] * v[i] * v[i]; }
    var = s2 - s * s;
    return (var > 0.0) ? sqrt(var) : 0.0;
}

PSYQ_API int psyq_marginal(const psyq_quest* q, int axis, double* out) {
    if (!q) return PSYQ_ERR_ARG;
    if (!q->open) return PSYQ_ERR_CLOSED;
    if (!psyq__param_axis_ok(q, axis) || !out) return PSYQ_ERR_ARG;
    psyq__marginal_axis(q, axis, out);
    return q->n_param_axis[axis];
}

PSYQ_API const double* psyq_posterior(const psyq_quest* q) {
    if (!q || !q->open) return NULL;
    if (!q->permuted) return q->posterior;
    /* The posterior is stored with the nuisance axes first; the caller asked
     * for their own axis order, so it is permuted into a second buffer that
     * exists only in this case. */
    psyq__to_public(q, q->posterior, q->post_public);
    return q->post_public;
}

PSYQ_API double psyq_entropy(const psyq_quest* q) {
    if (!q || !q->open) return PSYQ__NAN;
    if (!q->has_nuisance) return psyq__h_post(q);
    psyq__marginal_free(q, q->marginal_scratch);
    return psyq__entropy_of(q->marginal_scratch, q->n_marg);
}

PSYQ_API int psyq_p(const psyq_quest* q, int index, const double* params, double* p_out) {
    double sv[PSYQ_MAX_STIM_DIMS];
    if (!q) return PSYQ_ERR_ARG;
    if (!q->open) return PSYQ_ERR_CLOSED;
    if (index < 0 || index >= q->S) return PSYQ_ERR_ARG;
    psyq__stim_vec(q, index, sv);
    return psyq_p_values(q, sv, params, p_out);
}

/* The built-in functions read params[0..3], which is in bounds for them
 * because open() gives a built-in exactly four parameters; a custom model is
 * handed n_param values and reads its own. An optimizer cannot know that the
 * built-in branch is dead for a three-parameter custom model, so the caller's
 * arrays are copied, n_stim and n_param elements, into locals of the maximum
 * size, and the model only ever sees those. */
PSYQ_API int psyq_p_values(const psyq_quest* q, const double* stim, const double* params, double* p_out) {
    double sv[PSYQ_MAX_STIM_DIMS], pv[PSYQ_MAX_PARAMS], p[PSYQ_MAX_OUTCOMES];
    int i, ns, np, nk;
    if (!q) return PSYQ_ERR_ARG;
    if (!q->open) return PSYQ_ERR_CLOSED;
    if (!stim || !params || !p_out) return PSYQ_ERR_ARG;
    ns = psyq__ns(q);
    np = psyq__np(q);
    nk = psyq__nk(q);
    for (i = 0; i < PSYQ_MAX_STIM_DIMS; i++) sv[i] = 0.0;
    for (i = 0; i < PSYQ_MAX_PARAMS; i++)    pv[i] = 0.0;
    memcpy(sv, stim, (size_t)ns * sizeof(double));
    memcpy(pv, params, (size_t)np * sizeof(double));
    for (i = 0; i < ns; i++) if (!psyq__finite(sv[i])) return PSYQ_ERR_ARG;
    for (i = 0; i < np; i++) if (!psyq__finite(pv[i])) return PSYQ_ERR_ARG;
    psyq__eval(q, sv, pv, p);
    memcpy(p_out, p, (size_t)nk * sizeof(double));
    return PSYQ_OK;
}

PSYQ_API int psyq_simulate(const psyq_quest* q, int index, const double* params, double u) {
    double p[PSYQ_MAX_OUTCOMES];
    double c = 0.0;
    int k, rc;
    if (!q) return PSYQ_ERR_ARG;
    if (!q->open) return PSYQ_ERR_CLOSED;
    if (!psyq__finite(u) || u < 0.0 || u >= 1.0) return PSYQ_ERR_ARG;
    rc = psyq_p(q, index, params, p);
    if (rc != PSYQ_OK) return rc;
    for (k = 0; k < psyq__nk(q); k++) {
        c += p[k];
        if (c > u) return k;
    }
    return q->K - 1;   /* the probabilities sum to 1 only to rounding */
}

PSYQ_API int psyq_n_trials(const psyq_quest* q) {
    if (!q) return PSYQ_ERR_ARG;
    if (!q->open) return PSYQ_ERR_CLOSED;
    return q->n_trials;
}

PSYQ_API const psyq_trial* psyq_history(const psyq_quest* q, int* n) {
    if (n) *n = (q && q->open) ? q->n_trials : 0;
    return (q && q->open) ? q->history : NULL;
}

/* ======================================================================= *
 *  SNAPSHOT
 *
 *  In the shape of psy_trials.h's psytr_save / psytr_load: versioned,
 *  little-endian, written and read one byte at a time so a snapshot moves
 *  between compilers and platforms. One writer does three jobs (count, write,
 *  compare), which is how psyq_load() checks the desc against the snapshot
 *  without a second description of the layout that could drift from the first.
 * ======================================================================= */

#define PSYQ__SNAP_FORMAT 1u

typedef struct psyq__w {
    unsigned char*       out;
    const unsigned char* cmp;
    size_t               pos;
    size_t               cap;
    const char*          diff;   /* compare: the first field that differs */
} psyq__w;

static void psyq__put(psyq__w* w, uint64_t v, int nbytes, const char* name) {
    int i;
    unsigned char b;
    for (i = 0; i < nbytes; i++) {
        b = (unsigned char)((v >> (8 * i)) & 0xffu);
        if (w->out) {
            if (w->pos < w->cap) w->out[w->pos] = b;
        } else if (w->cmp && !w->diff) {
            if (w->pos >= w->cap || w->cmp[w->pos] != b) w->diff = name;
        }
        w->pos++;
    }
}

static void psyq__put_i32(psyq__w* w, int v, const char* name) {
    psyq__put(w, (uint64_t)(uint32_t)v, 4, name);
}

static void psyq__put_f64(psyq__w* w, double v, const char* name) {
    uint64_t u;
    memcpy(&u, &v, sizeof(u));
    psyq__put(w, u, 8, name);
}

/* The desc as the handle resolved it: defaults written as used, axes as the
 * values open() computed, whichever way they were given. The priors are NOT
 * here. open() consumes them into the posterior and keeps no copy, and the
 * posterior in the snapshot replaces whatever they would have produced, so
 * there is nothing a mismatch could change. */
static void psyq__put_desc(psyq__w* w, const psyq_quest* q) {
    const psyq_desc* d = &q->desc;
    int i, j, kind;
    psyq__put_i32(w, d->n_stim, "n_stim");
    for (i = 0; i < d->n_stim; i++) {
        const double* v = psyq__stim_axis(q, i);
        psyq__put_i32(w, q->n_stim_axis[i], "stim[].n");
        for (j = 0; j < q->n_stim_axis[i]; j++) psyq__put_f64(w, v[j], "stim[].values");
    }
    psyq__put_i32(w, d->n_param, "n_param");
    for (i = 0; i < d->n_param; i++) {
        const double* v = psyq__param_axis(q, i);
        psyq__put_i32(w, q->n_param_axis[i], "param[].n");
        psyq__put(w, d->param[i].nuisance ? 1u : 0u, 1, "param[].nuisance");
        for (j = 0; j < q->n_param_axis[i]; j++) psyq__put_f64(w, v[j], "param[].values");
    }
    /* Which kind of model, since a pointer cannot be compared across runs:
     * a built-in, a per-cell callback, or a batch one. The last two fill the
     * same table but differ in precision off the grid. */
    kind = (d->pf != PSYQ_PF_CUSTOM) ? 0 : (d->pf_batch ? 2 : 1);
    psyq__put_i32(w, (int)d->pf, "pf");
    psyq__put(w, (uint64_t)kind, 1, "pf_fn/pf_batch");
    psyq__put_i32(w, q->K, "n_outcomes");
    psyq__put_i32(w, (int)d->select, "select");
    psyq__put_i32(w, d->select_param, "select_param");
    psyq__put_f64(w, d->select_quantile, "select_quantile");
    psyq__put_i32(w, (int)d->tiebreak, "tiebreak");
    psyq__put_f64(w, d->tie_tolerance, "tie_tolerance");
    psyq__put(w, d->rng ? 1u : 0u, 1, "rng");
    psyq__put_i32(w, d->subset_size, "subset_size");
    psyq__put_i32(w, d->stop_trials, "stop_trials");
    psyq__put_f64(w, d->stop_entropy, "stop_entropy");
    psyq__put_f64(w, d->stop_sd, "stop_sd");
    psyq__put_i32(w, d->stop_sd_param, "stop_sd_param");
    psyq__put(w, d->no_table ? 1u : 0u, 1, "no_table");
    psyq__put_i32(w, q->S, "S");
    psyq__put_i32(w, q->P, "P");
}

static void psyq__put_all(psyq__w* w, const psyq_quest* q) {
    const double* post;
    int i, j;
    psyq__put(w, 'P', 1, "magic");
    psyq__put(w, 'S', 1, "magic");
    psyq__put(w, 'Y', 1, "magic");
    psyq__put(w, 'Q', 1, "magic");
    psyq__put(w, PSYQ__SNAP_FORMAT, 4, "format");
    psyq__put_desc(w, q);
    psyq__put_i32(w, q->n_trials, "n_trials");
    psyq__put_i32(w, q->proposed, "proposed");
    psyq__put_i32(w, q->last_shown, "last_shown");
    psyq__put_i32(w, q->tie_parity, "tie_parity");
    psyq__put_i32(w, (int)q->stop, "stop");
    /* The posterior in the CALLER'S axis order, so a snapshot does not depend
     * on how this version of the header lays its posterior out inside. */
    post = psyq_posterior(q);
    for (i = 0; i < q->P; i++) psyq__put_f64(w, post[i], "posterior");
    for (i = 0; i < q->n_trials; i++) {
        const psyq_trial* h = &q->history[i];
        for (j = 0; j < psyq__ns(q); j++) psyq__put_f64(w, h->stim[j], "history.stim");
        psyq__put_i32(w, h->stim_index, "history.stim_index");
        psyq__put_i32(w, h->proposed_index, "history.proposed_index");
        psyq__put(w, h->outcome, 1, "history.outcome");
    }
}

PSYQ_API size_t psyq_save_size(const psyq_quest* q) {
    psyq__w w;
    if (!q || !q->open) return 0;
    memset(&w, 0, sizeof(w));
    psyq__put_all(&w, q);
    return w.pos;
}

PSYQ_API int psyq_save(const psyq_quest* q, void* buf, size_t cap) {
    psyq__w w;
    size_t need;
    if (!q) return PSYQ_ERR_ARG;
    if (!q->open) return PSYQ_ERR_CLOSED;
    need = psyq_save_size(q);
    if (!buf || cap < need || need > 0x7fffffffu) return PSYQ_ERR_ARG;
    memset(&w, 0, sizeof(w));
    w.out = (unsigned char*)buf;
    w.cap = cap;
    psyq__put_all(&w, q);
    return (int)w.pos;
}

typedef struct psyq__r {
    const unsigned char* in;
    size_t               pos;
    size_t               len;
    bool                 bad;
} psyq__r;

static uint64_t psyq__get(psyq__r* r, int nbytes) {
    uint64_t v = 0;
    int i;
    if (r->bad || r->len - r->pos < (size_t)nbytes) {
        r->bad = true;
        return 0;
    }
    for (i = 0; i < nbytes; i++) v |= (uint64_t)r->in[r->pos + (size_t)i] << (8 * i);
    r->pos += (size_t)nbytes;
    return v;
}

static int psyq__get_i32(psyq__r* r) { return (int)(int32_t)(uint32_t)psyq__get(r, 4); }

static double psyq__get_f64(psyq__r* r) {
    uint64_t u = psyq__get(r, 8);
    double v;
    memcpy(&v, &u, sizeof(v));
    return v;
}

/* Public order to the internal one, P values: the inverse of
 * psyq__to_public, walked with the same odometer. */
static void psyq__from_public(const psyq_quest* q, const double* src, double* dst) {
    int idx[PSYQ_MAX_PARAMS], pub_stride[PSYQ_MAX_PARAMS];
    int np = psyq__np(q), j, t, pub = 0;
    for (j = 0; j < np; j++) pub_stride[j] = psyq__param_stride(q, j);
    memset(idx, 0, sizeof(idx));
    for (t = 0; t < q->P; t++) {
        dst[t] = src[pub];
        for (j = np - 1; j >= 0; j--) {
            if (++idx[j] < q->n_param_int[j]) { pub += pub_stride[q->perm[j]]; break; }
            idx[j] = 0;
            pub -= (q->n_param_int[j] - 1) * pub_stride[q->perm[j]];
        }
    }
}

/* Leave the handle closed and say why: a load either resumes the session or
 * leaves nothing half-restored behind. */
static bool psyq__load_fail(psyq_quest* q, const char* why) {
    char msg[sizeof(q->error)];
    snprintf(msg, sizeof(msg), "psy_quest: psyq_load: %s", why);
    psyq_close(q);
    memcpy(q->error, msg, sizeof(msg));
    return false;
}

PSYQ_API bool psyq_load(psyq_quest* q, const psyq_desc* desc, const void* buf, size_t len) {
    const unsigned char* in = (const unsigned char*)buf;
    psyq__w w;
    psyq__r r;
    double* post_in;
    double sum = 0.0;
    int i, j, v;

    if (!q) return false;
    /* The cheap checks first: the open that follows rebuilds the table. */
    if (!in || len < 8 || in[0] != 'P' || in[1] != 'S' || in[2] != 'Y' || in[3] != 'Q') {
        memset(q->error, 0, sizeof(q->error));
        snprintf(q->error, sizeof(q->error), "psy_quest: psyq_load: not a psy_quest snapshot");
        q->open = false;
        return false;
    }
    memset(&r, 0, sizeof(r));
    r.in = in;
    r.len = len;
    r.pos = 4;
    if ((uint32_t)psyq__get(&r, 4) != PSYQ__SNAP_FORMAT) {
        memset(q->error, 0, sizeof(q->error));
        snprintf(q->error, sizeof(q->error),
                 "psy_quest: psyq_load: snapshot format is not %u", PSYQ__SNAP_FORMAT);
        q->open = false;
        return false;
    }

    /* A full open, table and all: the table is a function of the desc and is
     * rebuilt rather than stored. That is S*P evaluations of the model, the
     * same as psyq_open(), and the price of a snapshot that is the size of the
     * posterior instead of the size of the table. */
    if (!psyq_open(q, desc)) return false;

    memset(&w, 0, sizeof(w));
    w.cmp = in;
    w.cap = len;
    w.pos = 8;
    psyq__put_desc(&w, q);
    if (w.diff) {
        char why[128];
        snprintf(why, sizeof(why), "desc.%s does not match the snapshot", w.diff);
        return psyq__load_fail(q, why);
    }
    r.pos = w.pos;

    q->n_trials   = psyq__get_i32(&r);
    q->proposed   = psyq__get_i32(&r);
    q->last_shown = psyq__get_i32(&r);
    q->tie_parity = psyq__get_i32(&r);
    v             = psyq__get_i32(&r);
    if (r.bad || q->n_trials < 0 || q->n_trials > PSYQ_MAX_TRIALS ||
        q->proposed < -1 || q->proposed >= q->S ||
        q->last_shown < -1 || q->last_shown >= q->S ||
        (q->tie_parity != 0 && q->tie_parity != 1) ||
        v < (int)PSYQ_STOP_NONE || v > (int)PSYQ_STOP_FULL)
        return psyq__load_fail(q, "the snapshot's counters are corrupt or truncated");
    q->stop = (psyq_stop)v;

    /* The posterior comes in the caller's order: straight into place when the
     * two orders agree, through the permutation buffer when they do not. It
     * is NOT renormalized, because the resumed session has to be the saved
     * one bit for bit. */
    post_in = q->permuted ? q->post_public : q->posterior;
    for (i = 0; i < q->P; i++) {
        double x = psyq__get_f64(&r);
        if (!psyq__finite(x) || x < 0.0) r.bad = true;
        post_in[i] = x;
        sum += x;
    }
    if (r.bad || !(sum > 0.0))
        return psyq__load_fail(q, "the snapshot's posterior is corrupt or truncated");
    if (q->permuted) psyq__from_public(q, q->post_public, q->posterior);
    q->cache[1] = 0.0;   /* the posterior's entropy is recomputed on demand */

    for (i = 0; i < q->n_trials; i++) {
        psyq_trial* h = &q->history[i];
        for (j = 0; j < PSYQ_MAX_STIM_DIMS; j++) h->stim[j] = 0.0;
        for (j = 0; j < psyq__ns(q); j++) {
            h->stim[j] = psyq__get_f64(&r);
            if (!psyq__finite(h->stim[j])) r.bad = true;
        }
        h->stim_index = psyq__get_i32(&r);
        h->proposed_index = psyq__get_i32(&r);
        v = (int)psyq__get(&r, 1);
        if (h->stim_index < -1 || h->stim_index >= q->S ||
            h->proposed_index < -1 || h->proposed_index >= q->S || v >= q->K)
            r.bad = true;
        h->outcome = (uint8_t)v;
    }
    if (r.bad) return psyq__load_fail(q, "the snapshot's history is corrupt or truncated");
    if (r.pos != r.len) {
        char why[96];
        snprintf(why, sizeof(why), "%lu bytes after the snapshot's end",
                 (unsigned long)(r.len - r.pos));
        return psyq__load_fail(q, why);
    }
    q->error[0] = '\0';
    return true;
}

/* ======================================================================= *
 *  ASYNC
 *
 *  A psyrt_pump with QUEST+ in its on_msg. Everything here is plumbing: the
 *  inference is the same psyq_update() and psyq_next() a synchronous caller
 *  runs, on the same handle, in the same order, so an async run and a
 *  synchronous replay of the same responses agree bit for bit. What the layer
 *  adds is that the frame loop does not wait for it.
 * ======================================================================= */
#ifdef PSYQ_ASYNC

/* Compute the summaries first, take the publish lock only to copy them in.
 * That is psy_rt.h's PUMP discipline: a caller may hold that lock as long as
 * it likes without stalling the inference, because the inference never holds
 * it. */
static void psyq__async_publish(psyq_async* a, uint32_t seq, int update_rc) {
    psyq_snapshot s;
    psyq_quest* q = a->quest;
    memset(&s, 0, sizeof(s));
    s.seq = seq;
    s.update_rc = update_rc;
    s.proposed = psyq_next(q);
    if (s.proposed >= 0) (void)psyq_stim_values(q, s.proposed, s.stim);
    s.n_trials = psyq_n_trials(q);
    s.done = psyq_done(q);
    s.stop = psyq_stop_reason(q);
    (void)psyq_estimate(q, a->estimator, s.estimate);
    s.entropy = psyq_entropy(q);
    s.sd = psyq_sd(q, 0);
    psyrt_pump_lock(&a->pump);
    a->snap = s;
    a->published = true;
    psyrt_pump_unlock(&a->pump);
}

/* One response, on the pump thread, in submit order. */
static void psyq__async_on_msg(void* ctx, const void* msg, uint32_t seq) {
    psyq_async* a = (psyq_async*)ctx;
    const psyq_async_msg* m = (const psyq_async_msg*)msg;
    int rc;
    if (m->stim_index >= 0) rc = psyq_update(a->quest, m->stim_index, m->outcome);
    else                    rc = psyq_update_values(a->quest, m->stim, m->outcome);
    psyq__async_publish(a, seq, rc);
}

/* psy_rt.h's codes in this header's vocabulary. */
static int psyq__async_rc(int rt_rc) {
    switch (rt_rc) {
    case PSYRT_ERR_FULL:    return PSYQ_ERR_BUSY;
    case PSYRT_ERR_TIMEOUT: return PSYQ_ERR_TIMEOUT;
    case PSYRT_ERR_STOPPED: return PSYQ_ERR_CLOSED;
    case PSYRT_ERR_ARG:     return PSYQ_ERR_ARG;
    default:                return (rt_rc < 0) ? PSYQ_ERR_ARG : rt_rc;
    }
}

PSYQ_API bool psyq_async_start(psyq_async* a, const psyq_async_desc* desc) {
    psyrt_pump_desc pd;
    if (!a) return false;
    memset(a->error, 0, sizeof(a->error));
    a->running = false;
    a->published = false;
    if (!desc || !desc->quest) {
        snprintf(a->error, sizeof(a->error), "psy_quest: psyq_async_start needs desc.quest");
        return false;
    }
    if (!psyq_is_open(desc->quest)) {
        snprintf(a->error, sizeof(a->error), "psy_quest: desc.quest is not open");
        return false;
    }
    if ((int)desc->estimator < 0 || (int)desc->estimator > (int)PSYQ_EST_MEDIAN) {
        snprintf(a->error, sizeof(a->error), "psy_quest: desc.estimator is out of range");
        return false;
    }
    if (desc->queue_depth < 0 || desc->queue_depth > PSYQ_ASYNC_QUEUE) {
        snprintf(a->error, sizeof(a->error),
                 "psy_quest: desc.queue_depth = %d, need 0..%d (PSYQ_ASYNC_QUEUE)",
                 desc->queue_depth, PSYQ_ASYNC_QUEUE);
        return false;
    }
    a->quest = desc->quest;
    a->estimator = desc->estimator;
    memset(&a->snap, 0, sizeof(a->snap));

    /* The first proposal, before the thread exists, so a poll() right after a
     * successful start always has something to return and the thread can never
     * publish seq 1 ahead of it. The pump's lock is a no-op while the pump is
     * not running, which is what makes this reuse of the publish path safe. */
    psyq__async_publish(a, 0, PSYQ_OK);

    memset(&pd, 0, sizeof(pd));
    pd.msg_size = sizeof(psyq_async_msg);
    pd.capacity = (uint32_t)(desc->queue_depth ? desc->queue_depth : PSYQ_ASYNC_QUEUE);
    pd.ring = a->ring;
    pd.on_msg = psyq__async_on_msg;
    pd.on_idle = NULL;      /* QUEST+ has nothing to do between trials */
    pd.ctx = a;
    pd.below_normal = desc->below_normal;
    pd.pin_cpu = desc->pin_cpu;
    if (!psyrt_pump_start(&a->pump, &pd)) {
        snprintf(a->error, sizeof(a->error), "psy_quest: %.200s",
                 psyrt_pump_error(&a->pump));
        a->published = false;
        return false;
    }
    a->running = true;
    return true;
}

PSYQ_API void psyq_async_stop(psyq_async* a) {
    if (!a) return;
    psyrt_pump_stop(&a->pump);   /* drains, then joins */
    a->running = false;
}

static int psyq__async_send(psyq_async* a, const psyq_async_msg* m) {
    int rc = psyrt_pump_submit(&a->pump, m);
    return (rc < 0) ? psyq__async_rc(rc) : rc;
}

PSYQ_API int psyq_async_submit(psyq_async* a, int stim_index, int outcome) {
    psyq_async_msg m;
    int i;
    if (!a || !a->quest) return PSYQ_ERR_ARG;
    if (!a->running) return PSYQ_ERR_CLOSED;
    /* Checked here rather than on the thread, where the only thing that could
     * be done with a bad argument is to drop it silently. S and K do not
     * change after psyq_open(), so reading them beside the thread is safe. */
    if (stim_index < 0 || stim_index >= a->quest->S) return PSYQ_ERR_ARG;
    if (outcome < 0 || outcome >= a->quest->K) return PSYQ_ERR_ARG;
    for (i = 0; i < PSYQ_MAX_STIM_DIMS; i++) m.stim[i] = 0.0;
    m.stim_index = stim_index;
    m.outcome = outcome;
    return psyq__async_send(a, &m);
}

PSYQ_API int psyq_async_submit_values(psyq_async* a, const double* stim, int outcome) {
    psyq_async_msg m;
    int i, n;
    if (!a || !a->quest || !stim) return PSYQ_ERR_ARG;
    if (!a->running) return PSYQ_ERR_CLOSED;
    if (outcome < 0 || outcome >= a->quest->K) return PSYQ_ERR_ARG;
    n = psyq__ns(a->quest);
    for (i = 0; i < PSYQ_MAX_STIM_DIMS; i++) m.stim[i] = 0.0;
    memcpy(m.stim, stim, (size_t)n * sizeof(double));
    for (i = 0; i < n; i++) if (!psyq__finite(m.stim[i])) return PSYQ_ERR_ARG;
    m.stim_index = -1;
    m.outcome = outcome;
    return psyq__async_send(a, &m);
}

PSYQ_API int psyq_async_poll(const psyq_async* a, psyq_snapshot* out) {
    psyq_async* m = (psyq_async*)a;   /* the lock and the copy are not const */
    int seq;
    if (!a) return PSYQ_ERR_ARG;
    if (!a->running && !a->published) return PSYQ_ERR_CLOSED;
    psyrt_pump_lock(&m->pump);
    seq = (int)m->snap.seq;
    if (out) *out = m->snap;
    psyrt_pump_unlock(&m->pump);
    return seq;
}

PSYQ_API int psyq_async_wait(psyq_async* a, uint32_t seq, uint64_t timeout_ns,
                             psyq_snapshot* out) {
    int rc;
    if (!a) return PSYQ_ERR_ARG;
    /* Not gated on a->running: a seq the thread DID reach before a stop still
     * answers, which is psy_rt.h's rule for psyrt_pump_wait and the reason a
     * caller can wait on the last response it submitted after stopping. */
    rc = psyrt_pump_wait(&a->pump, seq, timeout_ns);
    if (rc < 0) return psyq__async_rc(rc);
    return psyq_async_poll(a, out);
}

PSYQ_API int psyq_async_pending(const psyq_async* a) {
    int rc;
    if (!a) return PSYQ_ERR_ARG;
    if (!a->running) return PSYQ_ERR_CLOSED;
    rc = psyrt_pump_pending(&a->pump);
    return (rc < 0) ? psyq__async_rc(rc) : rc;
}

PSYQ_API const char* psyq_async_error(const psyq_async* a) { return a ? a->error : ""; }
PSYQ_API bool psyq_async_is_running(const psyq_async* a) { return a && a->running; }

PSYQ_API psyrt_policy psyq_async_policy(const psyq_async* a) {
    return a ? psyrt_pump_policy(&a->pump) : PSYRT_POLICY_NONE;
}

#endif /* PSYQ_ASYNC */

#endif /* PSY_QUEST_IMPLEMENTATION_GUARD */
#endif /* PSY_QUEST_IMPLEMENTATION */

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
