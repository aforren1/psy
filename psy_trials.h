/* psy_trials.h - v0.1.1 - public domain single-header trial sequencing library
 *
 *   The layer above the adaptive methods: which trial comes next, and what
 *   happened on it. Conditions and repetitions (the method of constant
 *   stimuli), sequential, random and constrained-random orders ("no more
 *   than three of the same orientation in a row", "a catch trial at most
 *   once in any six trials"), interleaved adaptive tracks (staircases,
 *   QUEST+, GP handles owned by the caller), blocks, practice trials, catch
 *   trials, re-queued trials, per-condition tallies, and a history that
 *   replays. PsychoPy's TrialHandler and MultiStairHandler, as one C
 *   header with no heap.
 *
 *   Written in the single-header style of the stb / sokol libraries. Pure
 *   computation: no OS calls, no threads, no heap, no I/O. Needs nothing
 *   but the C standard library (snprintf for the format functions). It
 *   does not include the method headers: a track is an opaque pointer and
 *   an is-done callback, so this header interleaves anything and knows
 *   nothing about stimuli or levels.
 *
 *   Targets every platform the compiler does. C++17, C11, or the pre-C11 C
 *   dialect MSVC compiles with by default.
 *
 *   ---------------------------------------------------------------------
 *   CHANGELOG
 *   ---------------------------------------------------------------------
 *   v0.1.1 - hardening for newer compilers (gcc 13 and 16, clang 18 at -O3
 *          with warnings as errors), no change to any order, draw or
 *          snapshot byte. Every loop over a fixed-size array is bounded by
 *          the array's size as well as its count, every memmove size is
 *          visibly nonnegative and inside the arrays, and open() copies
 *          desc.cond_reps and desc.warmup_conditions into the handle,
 *          reading each entry once, so neither needs to outlive open().
 *          open() now rejects n_warmup_conditions above
 *          PSYTR_MAX_CONDITIONS. The handle grew by 4 KB.
 *   v0.1.0 - first implementation. Defining PSY_TRIALS_IMPLEMENTATION now
 *          compiles the library instead of failing. The manual gained what
 *          a specification could leave open and an implementation cannot:
 *          the exact generator draw order (RANDOMNESS), the snapshot byte
 *          layout, how blocks are counted and where practice sits, what a
 *          constraint window does at a block boundary (it is clipped),
 *          what psytr_requeue() does to the schedule and the block count,
 *          and the rest of the descs psytr_open() rejects. Behavior that
 *          differs from the v0.0 text:
 *          - Constraints hold on the MAIN sequence (every trial but
 *            practice and warmup, track trials included as trials with no
 *            level), and next() enforces them at run time as well as at
 *            open: a scheduled trial that would break one gives way to a
 *            track trial, or to a later scheduled trial that fits. Without
 *            that, the second USAGE example (one catch condition and three
 *            tracks) could not be expressed. With tracks, a repair that
 *            runs out of swaps is therefore not an open() failure.
 *          - The repair swaps the offending trial with the first of up to
 *            64 slots, from a random start, that leaves both slots legal,
 *            not with a blind random later slot: the blind swap failed
 *            about 15% of easy designs (3 rows x 10, no immediate repeat)
 *            by getting trapped at the end of the schedule.
 *          - PSYTR_ANY_LEVEL works for max_in_window and min_gap as well
 *            as max_run. no_transition is clipped at block boundaries like
 *            the others; first_not is about the session's first main
 *            trial only.
 *          - psytr_requeue() records the new outcome PSYTR_REQUEUE (-2),
 *            and psytr_restore() re-queues on PSYTR_REQUEUE, not on
 *            PSYTR_INVALID, so update(PSYTR_INVALID) and requeue() replay
 *            differently and the history's outcomes feed restore directly.
 *            update() rejects outcomes below PSYTR_INVALID.
 *          - Practice and warmup trials cannot be re-queued (PSYTR_ERR_ARG).
 *          - Two history flags: PSYTR_FLAG_FIRST_IN_BLOCK and
 *            PSYTR_FLAG_VIOLATION.
 *          - psytr_format_row() and psytr_format_header() add warmup and
 *            after_break columns, and end the line with a newline.
 *          - A record pointer with no records buffer is ignored rather
 *            than an error, and a NULL record zero-fills the trial's slot.
 *          - psytr_condition_at() indexes the practice slots too.
 *   v0.0 - specification. Declarations and the manual, no implementation.
 *
 *   STATUS: v0.1.1. Built and run on Linux (WSL2) with gcc 11.4 as C99,
 *   C11 and C++17 under -Wall -Wextra -Wpedantic -Wshadow -Werror, and
 *   under -fsanitize=address,undefined -fno-sanitize-recover=all with no
 *   diagnostic; on Windows 11 with MSVC 19.44 under /W4 /WX, in its
 *   default C dialect and as /std:c++17. The header, its test and both
 *   examples also build at -O2 and -O3 under gcc 11.4 with
 *   -Warray-bounds=2 -Wstringop-overflow=4 -Wmaybe-uninitialized
 *   -Waggressive-loop-optimizations -Wnull-dereference added, and under
 *   clang 11.1 as C11, C99 and C++17, all with -Werror; the newer gcc 13,
 *   gcc 16 and clang 18 of CI were not available locally, so their verdict
 *   is CI's. Both examples print byte for
 *   byte the same on the two compilers, and the test prints the same
 *   apart from its timings.
 *   tests/adapt/psy_trials_test.c runs at PSYTR_MAX_TRIALS 256 and at the
 *   default 4096 and checks: the factorial index round trip; the orders'
 *   properties over 400 seeds (SEQUENTIAL identical to PsychoPy's order,
 *   RANDOM one of each row per repetition, FULL_RANDOM exact counts and a
 *   first slot near uniform, weighted repetitions) and the Fisher-Yates
 *   draw order against a hand trace; every constraint rule on every
 *   repaired order of eight designs x 200 seeds, by a checker written
 *   separately from the header's, including block-clipped windows and
 *   runs; the failure message on impossible designs; track interleaving
 *   at rates 0.5 and 1 and 0 (rejected with conditions), round robin
 *   against a hand trace, weights, and that a finished track is never
 *   picked or asked again; practice and warmup flags, blocks and tallies;
 *   re-queues with and without a gap, and 760 random re-queues under a
 *   no-repeat rule with no unflagged violation in the realized order;
 *   psytr_mark_break() ending a run; tallies against a hand count;
 *   psytr_restore() reproducing history, schedule, tallies, records and
 *   the generator's position bit for bit, with and without tracks;
 *   psytr_save() at 42 cut points (with and without a pending trial)
 *   then psytr_load() into a garbage-filled handle and finishing,
 *   compared bit for bit with the uninterrupted run, snapshot bytes
 *   included; every load refusal; every rejected desc; PSYTR_ERR_FULL;
 *   the format functions against fixed strings.
 *   What is NOT done: no comparison against psychopy.data.TrialHandler
 *   (the Python binding and tests/compare/ script in docs/psy_trials.md
 *   do not exist yet); no run on macOS or big-endian hardware, so the
 *   snapshot's portability is by construction, not by test; no loading
 *   of a snapshot written by another compiler; the constraint repair's
 *   success rate is measured only on the designs in the test, and a
 *   tight design can still need a larger desc.max_swaps. Nothing here is
 *   a timing measurement of a real session. A -DPSYTR_API=static build
 *   needs a translation unit that calls every function, or
 *   -Wno-unused-function.
 *
 *   ---------------------------------------------------------------------
 *   USAGE
 *   ---------------------------------------------------------------------
 *   Do this:
 *
 *       #define PSY_TRIALS_IMPLEMENTATION
 *
 *   in *one* C or C++ file before including this header to create the
 *   implementation. Every other file just includes the header normally.
 *
 *   Method of constant stimuli, 2 orientations x 5 contrasts, 20
 *   repetitions, random order, never the same orientation four times
 *   running (examples/trials_mocs.c runs this):
 *
 *       #define PSY_TRIALS_IMPLEMENTATION
 *       #include "psy_trials.h"
 *
 *       uint64_t seed = 20260923u;                  // the caller owns it
 *       psytr_desc d = {0};
 *       d.factors[0] = psytr_factor("orientation", 2);
 *       d.factors[1] = psytr_factor("contrast", 5);
 *       d.n_factors  = 2;                           // 10 conditions
 *       d.reps       = 20;                          // 200 trials
 *       d.order      = PSYTR_ORDER_CONSTRAINED;
 *       d.constraints[0] = psytr_max_run(0, PSYTR_ANY_LEVEL, 3);
 *       d.n_constraints  = 1;
 *       d.rng = psytr_splitmix; d.rng_ctx = &seed;
 *
 *       static psytr_trials t;                      // 89 KB; not the stack
 *       if (!psytr_open(&t, &d)) { fputs(psytr_error(&t), stderr); return 1; }
 *
 *       psytr_trial_info ti;
 *       while (psytr_next(&t, &ti) >= 0) {
 *           int ori = psytr_level(&t, ti.condition, 0);
 *           int con = psytr_level(&t, ti.condition, 1);
 *           int correct = run_trial(ori, con);      // the caller's business
 *           psytr_update(&t, correct, NULL);
 *       }
 *       for (int c = 0; c < psytr_n_conditions(&t); c++)
 *           printf("%d %.2f\n", c, psytr_proportion(&t, c, 1));
 *
 *   Three interleaved staircases with a catch condition on about one trial
 *   in ten, never two catch trials in a row (examples/trials_interleave.c
 *   runs this):
 *
 *       static double levels[PSYTR_MAX_TRIALS];     // one record per trial
 *       psyst_stair s[3];                           // opened by the caller
 *       psytr_desc d = {0};
 *       d.n_conditions = 1;                         // the catch trial
 *       d.reps         = 24;
 *       d.order        = PSYTR_ORDER_CONSTRAINED;
 *       d.constraints[0] = psytr_min_gap(PSYTR_CONDITION, 0, 1);
 *       d.n_constraints  = 1;
 *       for (int i = 0; i < 3; i++)
 *           d.tracks[i] = psytr_track(&s[i], psytr_stair_done);
 *       d.n_tracks    = 3;
 *       d.track_rate  = 0.9;                        // 9 in 10 while any runs
 *       d.records     = levels;
 *       d.record_size = sizeof(double);
 *       d.rng = psytr_splitmix; d.rng_ctx = &seed;
 *       ...
 *       while (psytr_next(&t, &ti) >= 0) {
 *           if (ti.track >= 0) {
 *               double level = psyst_next(&s[ti.track]);
 *               int r = run_trial(level);
 *               psyst_update(&s[ti.track], level, r);
 *               psytr_update(&t, r, &level);        // the level as the record
 *           } else {
 *               int r = run_catch_trial();
 *               psytr_update(&t, r, NULL);          // a zeroed record
 *           }
 *       }
 *
 *   where psytr_stair_done is the caller's three-line adapter:
 *
 *       static bool psytr_stair_done(void* ctx) {
 *           return psyst_done((const psyst_stair*)ctx);
 *       }
 *
 *   The schedule there is 24 identical catch trials, so no order of it
 *   keeps them apart; the track trials do. See CONSTRAINTS, AT RUN TIME.
 *
 *   ---------------------------------------------------------------------
 *   MODEL
 *   ---------------------------------------------------------------------
 *   A session is a sequence of trials. Each trial comes from one of two
 *   sources: a CONDITION, one row of a table the caller defines, scheduled
 *   in advance and ordered at open; or a TRACK, an adaptive procedure the
 *   caller owns, chosen at run time while it is not done. The header
 *   decides which source and which condition; the caller decides what to
 *   show. It records the outcome and a fixed-size caller record per trial,
 *   tallies outcomes per condition, and hands the history back.
 *   Practice trials run first and warmup trials open the later blocks
 *   (BLOCKS, BREAKS, ...). Every other trial, scheduled or track, is a
 *   MAIN trial; blocks and constraints count main trials only.
 *
 *   CONDITIONS
 *     Either desc.n_conditions rows with no structure (the caller knows
 *     what row 3 means), or a FACTORIAL design: desc.factors[] gives
 *     n_levels per factor and the rows are their Cartesian product, last
 *     factor fastest, so psytr_level(t, condition, factor) returns the
 *     level index of each factor. A factor's name is for the row formatter
 *     only. With factors, n_conditions is 0 or the product.
 *     PSYTR_MAX_FACTORS (8) factors, and the product must fit
 *     PSYTR_MAX_CONDITIONS (1024).
 *     desc.reps repetitions of every condition, or desc.cond_reps[] per
 *     condition (a weighted design; PsychoPy's TrialHandlerExt), which
 *     overrides reps when it is not NULL; an entry may be 0, the sum may
 *     not. The practice trials plus the scheduled trials must fit
 *     PSYTR_MAX_TRIALS (4096; a compile-time size of the handle), and so
 *     must the warmup trials when the session has no tracks, since their
 *     number is then known at open.
 *     open() copies cond_reps and warmup_conditions (at most
 *     PSYTR_MAX_CONDITIONS entries) into the handle, reading each entry
 *     once, so those two arrays may go away after open(). The other
 *     pointers (records, factor names, track contexts, rng and rng_ctx)
 *     are used after open() returns and must stay valid while the handle
 *     is in use.
 *
 *   ORDER (desc.order)
 *     PSYTR_ORDER_SEQUENTIAL   repetition-major, as PsychoPy does it: rep
 *                              0 of every row in row order, then rep 1, so
 *                              the rows cycle and the repetitions of one
 *                              row are spread over the session. With
 *                              cond_reps, repetition r holds the rows that
 *                              have more than r repetitions.
 *     PSYTR_ORDER_RANDOM       each repetition is a block; the rows are
 *                              shuffled within every block independently
 *                              (PsychoPy "random"). Every condition appears
 *                              exactly once per block (with cond_reps, once
 *                              in each block it has a repetition in). These
 *                              repetition blocks are not block_size blocks.
 *     PSYTR_ORDER_FULL_RANDOM  one shuffle of all reps x rows (PsychoPy
 *                              "fullRandom"). Runs of a condition are
 *                              possible.
 *     PSYTR_ORDER_CONSTRAINED  FULL_RANDOM followed by repair until every
 *                              constraint holds; see CONSTRAINTS.
 *     The shuffle is Fisher-Yates on the caller's generator (RANDOMNESS
 *     gives the draws). Two sessions with the same desc and the same seed
 *     get the same order, and the order is fixed at open, so
 *     psytr_condition_at(t, i) answers for a trial not yet run (a caller
 *     that preloads stimuli needs that). The schedule it indexes is the
 *     practice slots, then the main schedule; warmup trials are drawn at
 *     run time and are not in it. The answer for a slot not yet run holds
 *     unless a re-queue inserts a copy before it or the run-time check
 *     (CONSTRAINTS) moves a trial forward, which a schedule that passed the
 *     repair at open needs only after a re-queue.
 *     The rep of a scheduled trial counts the row's earlier slots in the
 *     main schedule, so under every order it numbers that row's
 *     presentations from 0; a re-queued copy keeps the rep of the trial it
 *     replaces. Practice, warmup and track trials have rep -1.
 *
 *   CONSTRAINTS (desc.constraints[], PSYTR_ORDER_CONSTRAINED only)
 *     Each constraint names a FACTOR (or PSYTR_CONDITION for the row
 *     itself), a LEVEL of it (or PSYTR_ANY_LEVEL: whatever level the trial
 *     at hand has), and a rule:
 *       psytr_max_run(f, l, n)          no more than n consecutive trials
 *                                       with level l of factor f
 *       psytr_max_in_window(f, l, w, n) at most n trials with level l in
 *                                       any w consecutive trials
 *       psytr_min_gap(f, l, g)          at least g other trials between two
 *                                       trials with level l
 *       psytr_no_transition(f, a, b)    level a is never directly followed
 *                                       by level b (a may equal b)
 *       psytr_first_not(f, l)           the session's first main trial
 *                                       does not have level l
 *     PSYTR_ANY_LEVEL is accepted by the first three. n, w and g are at
 *     least 1. PSYTR_MAX_CONSTRAINTS (16). A constraint under any other
 *     order is rejected at open.
 *     THE SEQUENCE. Constraints hold on the main sequence in run order.
 *     Practice and warmup trials are not in it. A track trial is, with no
 *     level of any factor: it breaks a run, widens a gap and thins a
 *     window, and never breaks a rule itself. A trial that was re-queued
 *     is in it too: the observer saw it. Unless
 *     desc.constraints_span_blocks is set, the sequence is cut into
 *     SEGMENTS at the start of every block (desc.block_size) and at every
 *     break marked with psytr_mark_break(), and every rule but first_not
 *     looks back only within the segment: a run, a gap and a transition
 *     end at the boundary, and a window that straddles it is clipped to
 *     the trials on this side, so "at most n in any w" counts fewer than w
 *     trials just after a rest. A rest between blocks ends a run as far as
 *     the observer is concerned; set constraints_span_blocks for the
 *     stricter reading, under which marked breaks do not cut either.
 *     AT OPEN. The main schedule is repaired so the rules hold on it
 *     alone, as though no track trial were interleaved. The repair is a
 *     swap search: scan the slots in order for the first whose trial
 *     breaks a rule (each rule looks back from the trial at hand), then
 *     look for a partner to swap it with, starting at a random other slot
 *     and trying up to 64 slots in turn (wrapping around, skipping trials
 *     of the same row); the first swap after which both slots obey every
 *     rule looking back is taken, and when none of the tries gives one,
 *     the swap with the random slot is taken anyway. Rescan from the lower
 *     of the two slots; at most desc.max_swaps swaps (0 = 100000). Without
 *     tracks,
 *     slot i of the main schedule is main trial i, so the segments are the
 *     blocks; with tracks the blocks' slots are not known at open and the
 *     repair counts across them, which is stricter. Adding track trials or
 *     boundaries to a sequence that obeys the rules never breaks one, so
 *     the repaired schedule holds however the run interleaves it.
 *     Without tracks, a repair that runs out of swaps FAILS open() with a
 *     message naming the first constraint still broken, its slot and the
 *     swap count, so an impossible design (two levels, max run 1, unequal
 *     counts) is found at open, not at trial 190. With tracks it does not
 *     fail: the design may rely on track trials to separate its scheduled
 *     trials, as the second USAGE example does, and the run-time check
 *     enforces that. The search is deterministic from the seed and it is
 *     not uniform over the orders that satisfy the rules; no practical
 *     method is.
 *     AT RUN TIME. Before next() hands out a scheduled trial it checks the
 *     trial against the main trials already run. If the trial would break
 *     a rule: while any track is not done, a track trial runs instead;
 *     otherwise the first later slot whose trial fits moves forward to run
 *     now, the rest keeping their order; when none fits, the trial runs
 *     anyway and its history entry carries PSYTR_FLAG_VIOLATION. That is
 *     the only way a session breaks a rule, and it happens only when the
 *     tracks end before the schedule does, or after a re-queue the rest of
 *     the schedule cannot absorb.
 *     Not in v0.1: transition balancing (every level following every other
 *     equally often) and counterbalancing across sessions. docs/psy_trials.md
 *     says why.
 *
 *   TRACKS (desc.tracks[], desc.n_tracks, desc.interleave, desc.track_rate)
 *     A track is {ctx, is_done(ctx), weight}. While a track is not done and
 *     a scheduled trial is left, next() runs a track trial with
 *     probability desc.track_rate and otherwise the next scheduled trial.
 *     track_rate defaults to 1 with no conditions and to 0 with no tracks;
 *     with both it is required, in (0, 1], and 1 runs the tracks to their
 *     end before the schedule. When the schedule is exhausted the tracks
 *     run alone; when every track is done the schedule runs alone. A
 *     scheduled trial that would break a constraint gives way to a track
 *     trial, with no draw (CONSTRAINTS). Among tracks:
 *     PSYTR_INTERLEAVE_RANDOM draws by weight among those not done (weight
 *     0 means 1), PSYTR_INTERLEAVE_ROUND_ROBIN cycles through them in index
 *     order, skipping any that are done. Once is_done() has returned true
 *     for a track it is never called on it or picked again. next() calls
 *     is_done() once on each track not yet done each time it chooses a
 *     main or warmup trial, never more than once per track per call, and
 *     not when it hands out a practice trial or returns the pending trial
 *     again; psytr_done() calls it too. The header never calls anything
 *     else on a track: the caller asks its own handle for the level, as
 *     the USAGE example shows. PSYTR_MAX_TRACKS (16).
 *
 *   BLOCKS, BREAKS, PRACTICE, WARMUP, CATCH TRIALS, RE-QUEUES
 *     desc.block_size splits the main sequence into blocks of that many
 *     main trials (0 = one block); a trial later re-queued counts, since
 *     the observer spent the time. Practice trials are block -1 and the
 *     main trials blocks 0, 1, ...; psytr_trial_info.block and
 *     .first_in_block tell the caller where a rest screen goes.
 *     first_in_block is set on the first practice trial, on the first main
 *     trial, and on the first trial of every later block, which is its
 *     first warmup trial when there are warmups; that trial also has
 *     after_break. The header has no clock: a break by elapsed time is the
 *     caller's check between trials, and psytr_mark_break(t) records that
 *     one happened. Called between trials it flags the next trial handed
 *     out; called with a trial pending it flags that one; either way a
 *     constraint segment starts at the next main trial (the pending one,
 *     when that is a main trial). It does not start a block or run warmup
 *     trials, and it changes nothing in the order: a boundary only relaxes
 *     a rule the schedule already meets. So a break taken on the caller's
 *     own schedule leaves the same flag in the history as a scheduled one.
 *     desc.n_practice trials run before the schedule, drawn at open at
 *     random from desc.warmup_conditions[] (any condition when that list is
 *     empty), or cycled through that list in order under SEQUENTIAL;
 *     flagged practice, excluded from the tallies and the constraints.
 *     desc.n_warmup trials run at the start of every block after the
 *     first, when a main trial is left to follow them, drawn at run time
 *     the same way (under SEQUENTIAL the cycle restarts in each block) and
 *     flagged warmup: a reintroduction after a rest. n_warmup needs
 *     block_size. Both are condition trials, never track trials: the
 *     header does not know what an easy level is, so a warmup for a
 *     staircase is the caller's, which sees the flag, shows what it likes,
 *     and decides whether the track hears about it.
 *     A catch trial is a condition like any other, placed by the
 *     constraints; the second USAGE example is that.
 *     psytr_requeue(t), called INSTEAD of psytr_update() for a scheduled
 *     trial (not a practice, warmup or track trial: PSYTR_ERR_ARG), records
 *     PSYTR_REQUEUE as its outcome and inserts a copy (same row, same rep,
 *     flagged requeued) into the schedule: at the end when
 *     desc.requeue_gap is 0; otherwise at a random slot with at least
 *     requeue_gap scheduled trials before it, or at the end when fewer are
 *     left. For a missed response or a fixation break. The copy is tallied
 *     when it runs. When the history could not hold it (trials handed out
 *     + slots left + 1 > PSYTR_MAX_TRIALS) nothing is inserted, the trial
 *     is recorded as PSYTR_INVALID, and requeue returns PSYTR_ERR_FULL. The
 *     copy is placed without a constraint check; the run-time check keeps
 *     the realized order within the rules where it can.
 *
 *   THE LOOP
 *     psytr_next(t, &info)        the next trial: its index, or PSYTR_DONE
 *     psytr_update(t, outcome, rec) what happened, plus the caller's record
 *     psytr_done(t)               nothing pending or scheduled, tracks done
 *     psytr_requeue(t)            do this one again later
 *   next() without an update() in between returns the same trial again
 *   (info may be NULL); update() or requeue() without a pending trial is
 *   PSYTR_ERR_ORDER. `outcome` is any int >= 0 the caller chooses (1 =
 *   correct, a key code, a category), or PSYTR_INVALID (-1) for a trial
 *   that produced no usable response, excluded from the tallies. Lower
 *   values are PSYTR_ERR_ARG: PSYTR_REQUEUE (-2) is what requeue()
 *   records. `rec` copies desc.record_size bytes into the trial's slot of
 *   desc.records; NULL zero-fills the slot; with no records buffer (or
 *   record_size 0) it is ignored.
 *
 *   TALLIES AND HISTORY
 *     psytr_count(t, c, outcome) and psytr_proportion(t, c, outcome) over
 *     the completed, valid (outcome >= 0), non-practice, non-warmup trials
 *     of condition c; psytr_n_valid(t, c) is how many. count() also
 *     counts the PSYTR_INVALID or PSYTR_REQUEUE trials of c when asked;
 *     proportion() is NaN for those and when n_valid is 0. That is the
 *     method-of-constant-stimuli estimate; a psychometric fit is an
 *     offline job on the history, as everywhere in this collection.
 *     psytr_history(t, &n) is the trial array, a pending trial included
 *     (without PSYTR_FLAG_DONE, outcome PSYTR_INVALID); psytr_record(t, i)
 *     the caller's bytes for trial i. psytr_format_row(t, i, buf, cap)
 *     writes one newline-terminated CSV line:
 *       index,block,rep,condition,track,practice,warmup,requeued,
 *       after_break,outcome
 *     then one column per factor level (empty for a track trial); the
 *     outcome is empty while the trial is pending. psytr_format_header()
 *     writes the matching header, with the factor names (factor<i> when a
 *     name is NULL, quoted when it holds a comma, a quote or a line break).
 *     No file is touched.
 *
 *   SAVING, REPLAY AND RESUME
 *     The header's own state is small and scalar: the desc's numbers, the
 *     schedule, the history (outcomes and flags), the tallies. Three ways
 *     out, none of them a file:
 *       psytr_format_header / psytr_format_row   CSV lines for analysis
 *       psytr_format_meta                        one line of the desc's
 *                                                numbers and the counts, as
 *                                                key=value pairs, to head a
 *                                                data file
 *       psytr_save / psytr_load                  a versioned byte snapshot
 *                                                of the whole state, for
 *                                                resuming after a crash
 *     psytr_save() writes psytr_save_size(t) bytes; psytr_load() rebuilds a
 *     handle from them plus a desc that re-supplies what a snapshot cannot
 *     carry (the pointers: rng and its state, records, cond_reps, the
 *     warmup list, factor names, tracks), checks that the desc's numbers
 *     match the saved ones, and continues at the pending trial when one
 *     was pending. The generator's state is the caller's: save it beside
 *     the snapshot (for psytr_splitmix, the uint64_t) and put it back
 *     before the next draw. psytr_load() draws nothing and calls no track.
 *     SNAPSHOT LAYOUT, format 1. Every integer is little-endian two's
 *     complement, written and read byte by byte, so a snapshot moves
 *     between compilers and platforms; an f64 is the IEEE 754 bit pattern
 *     as a u64. Defaults are written resolved (max_swaps 0 as 100000,
 *     weight 0 as 1, track_rate as used).
 *       magic     4 bytes "PSTR", then u32 format (1)
 *       desc      i32 n_conditions, i32 n_factors, i32 n_levels per
 *                 factor, i32 reps, u8 has_cond_reps and then i32 per
 *                 condition when set, i32 order, i32 n_constraints and
 *                 per constraint i32 rule, factor, level, level2, n,
 *                 window; i32 max_swaps, i32 n_tracks, f64 weight per
 *                 track, i32 interleave, f64 track_rate, i32 block_size,
 *                 u8 constraints_span_blocks, i32 n_practice, i32
 *                 n_warmup, i32 n_warmup_conditions and i32 per entry,
 *                 i32 requeue_gap, u64 record_size
 *       state     i32 n_scheduled, n_run, n_done, current, schedule_pos,
 *                 rr_next; u32 track-done mask; i32 break_pending, n_main,
 *                 seg_start, warm_block, warm_done, swaps
 *       schedule  n_scheduled x i16 condition, then n_scheduled x i16 rep,
 *                 then n_scheduled x u8 flags
 *       main      n_main x i16 condition (-1 for a track trial)
 *       history   n_run x (i16 condition, i8 track, u8 flags, i16 rep,
 *                 i16 block, i32 outcome)
 *       tallies   n_conditions x i32 valid, then n_conditions x i32
 *                 outcome-1 count
 *       records   n_run x record_size bytes, when record_size > 0
 *     psytr_load() compares the desc section with the desc it is given and
 *     names the first field that differs, then range-checks every number
 *     it reads, so a wrong, truncated or corrupt snapshot fails the load
 *     with a message instead of indexing outside the handle. When
 *     record_size > 0 the records are copied into desc.records.
 *     Replay is the other route: the order is a function of desc and the
 *     generator's state at open, and every run-time draw consumes the
 *     generator in a fixed order, so psytr_restore(t, outcomes, records,
 *     n) after a fresh open() with the same desc and seed rebuilds the
 *     same run: for each trial it calls next(), then requeue() when the
 *     outcome is PSYTR_REQUEUE and update() otherwise, so the outcomes in
 *     psytr_history() feed it directly. Two limits: a break marked with
 *     psytr_mark_break() is not in the outcomes (drive the loop by hand
 *     and mark it again, or use a snapshot); and next() asks the tracks
 *     is_done(), so with tracks each must answer at every trial as it did
 *     in the original run, which a track restored to its final state does
 *     not. For a session with tracks, drive the loop by hand, feeding each
 *     track its own recorded trial, or use a snapshot. Track state is the
 *     caller's either way; the method headers replay from their own
 *     histories.
 *     The caller's per-trial data is deliberately NOT the header's
 *     problem beyond desc.record_size bytes per trial. Large or variable
 *     data (an eye trace, an EEG epoch, audio) belongs in its own
 *     container under the trial index, and the fixed record holds a
 *     reference to it if one is needed. The sequencer's table stays narrow
 *     and joins on trial index.
 *
 *   RANDOMNESS
 *     desc.rng(ctx) returns a uniform variate in [0, 1). Required for every
 *     order but SEQUENTIAL, for random interleaving of more than one
 *     track, for a track_rate below 1 with both conditions and tracks, for
 *     practice and warmup draws except under SEQUENTIAL, and for a
 *     requeue_gap; open() says which if it is missing. The header owns no
 *     generator and seeds nothing. psytr_splitmix() is a documented
 *     convenience: a splitmix64 step on a uint64_t the CALLER owns, so
 *     `d.rng = psytr_splitmix; d.rng_ctx = &seed;` is the whole setup and
 *     the seed is the caller's to log.
 *     DRAW ORDER. This is a contract: replay and resume depend on it. An
 *     "index draw below m" is floor(u * m) clamped to [0, m - 1], so a
 *     variate outside [0, 1) is clamped, not rejected. A "raw draw" is u
 *     itself.
 *     In psytr_open(), in this order:
 *       1. practice: one index draw per practice trial, in trial order,
 *          below the warmup list's length (or n_conditions). None under
 *          SEQUENTIAL.
 *       2. order: the main schedule starts in SEQUENTIAL order. RANDOM
 *          shuffles each repetition block in turn; FULL_RANDOM and
 *          CONSTRAINED shuffle the whole main schedule once. A shuffle of
 *          m items is Fisher-Yates from the top: for i = m-1 down to 1,
 *          j = an index draw below i + 1, swap items i and j (m - 1
 *          draws).
 *       3. repair (CONSTRAINED): one index draw per swap, s below n - 1
 *          for a main schedule of n slots; the partner search for the
 *          trial in slot k starts at slot (k + 1 + s) mod n.
 *     In psytr_next(), when it chooses a new trial:
 *       4. a practice trial: nothing.
 *       5. a warmup trial: one index draw into the list. None under
 *          SEQUENTIAL.
 *       6. a main trial: when a scheduled trial and a live track are both
 *          available, the scheduled trial breaks no rule, and track_rate
 *          is below 1: one raw draw u, a track trial when u < track_rate.
 *          Then, for a track trial under RANDOM interleave with more than
 *          one live track: one raw draw u, and the track is the first, in
 *          index order, whose running sum of live weights exceeds u times
 *          the live weights' total.
 *     In psytr_requeue() with requeue_gap > 0:
 *       7. one index draw for the slot, even when fewer than requeue_gap
 *          slots are left and the copy goes at the end.
 *     Nothing else draws: not update(), mark_break(), load(), done(), the
 *     format functions or the tallies.
 *
 *   ---------------------------------------------------------------------
 *   RETURN VALUES AND ERRORS
 *   ---------------------------------------------------------------------
 *   psytr_open() and psytr_load() return bool and fill psytr_error().
 *   Everything else returns a value or a code and never touches the
 *   message. psytr_next() returns the trial index (>= 0) or PSYTR_DONE
 *   (-1) or an error below -1; psytr_strerror() names a code.
 *
 *     PSYTR_ERR_ARG     null handle, condition or factor or trial out of
 *                       range, an outcome below PSYTR_INVALID, a requeue of
 *                       a practice, warmup or track trial, a restore with
 *                       more outcomes than the session has trials, a save
 *                       buffer smaller than psytr_save_size()
 *     PSYTR_ERR_CLOSED  the handle is not open
 *     PSYTR_ERR_ORDER   update() or requeue() with no pending trial (next()
 *                       twice is fine), restore() on a handle that has
 *                       already handed out a trial
 *     PSYTR_ERR_FULL    next() with PSYTR_MAX_TRIALS trials in the history
 *                       and something left to run; a re-queue that will not
 *                       fit (refused, the outcome recorded as PSYTR_INVALID)
 *
 *   On a closed or null handle the counts (n_conditions, n_factors,
 *   n_scheduled, n_run, n_done, n_valid) are 0, the lookups (level,
 *   condition_at, condition_from_levels, count) and the loop functions
 *   return the error, proportion is NaN, history and record are NULL,
 *   done and is_open are false, and save_size is 0.
 *
 *   ---------------------------------------------------------------------
 *   MEMORY AND THREADS
 *   ---------------------------------------------------------------------
 *   The handle holds the condition table, the schedule and the history
 *   inline: 19 bytes per trial at PSYTR_MAX_TRIALS (4096) plus 12 per
 *   condition at PSYTR_MAX_CONDITIONS (1024) plus the desc, 91480
 *   bytes on a 64-bit ABI. Nothing allocates. The caller's per-trial
 *   records live in desc.records, a buffer of at least record_size x
 *   PSYTR_MAX_TRIALS bytes the caller provides. Define PSYTR_MAX_TRIALS or
 *   PSYTR_MAX_CONDITIONS before the include to resize; both must stay
 *   below 32768, since trial and condition numbers are stored in 16 bits.
 *   A handle is not thread-safe; one thread runs one session.
 *   Costs: open() is O(trials) plus, per repair swap, up to 128 slot
 *   checks for the partner and a rescan from the lower slot, each check
 *   O(constraints x rule span), so an impossible
 *   design spends the whole swap budget (tests/adapt/psy_trials_test.c
 *   prints what that costs: 60 ms for a 4000-trial impossible design at
 *   the default budget, gcc -O2 on a desktop x64). next() is O(tracks +
 *   constraints x rule
 *   span), plus O(slots left x constraints x rule span) when a scheduled
 *   trial has to be moved forward. update() is O(1) plus the record copy;
 *   requeue() is O(slots left); count() for an outcome other than 1 is
 *   O(trials). "Rule span" is n for max_run, w for max_in_window, g for
 *   min_gap and 1 for the rest.
 *
 *   ---------------------------------------------------------------------
 *   BUILDING
 *   ---------------------------------------------------------------------
 *   Nothing to link. Define PSYTR_API to override the default `extern`
 *   linkage (definitions too, so static works; a -DPSYTR_API=static build
 *   needs a translation unit that calls every function, or
 *   -Wno-unused-function).
 *
 *       cc -O2 -I. -o trials_mocs examples/trials_mocs.c
 *       cl /O2 /I. examples\trials_mocs.c
 *
 *   ---------------------------------------------------------------------
 *   LICENSE: public domain / MIT-0, see end of file.
 */
#ifndef PSY_TRIALS_H_INCLUDED
#define PSY_TRIALS_H_INCLUDED

/* The version of this header, for a log line or a compile-time check. The
 * string is the three numbers, and the test asserts that it stays so. */
#define PSYTR_VERSION_MAJOR 0
#define PSYTR_VERSION_MINOR 1
#define PSYTR_VERSION_PATCH 1
#define PSYTR_VERSION_STRING "0.1.1"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PSYTR_API
#define PSYTR_API extern
#endif

#ifndef PSYTR_MAX_TRIALS
#define PSYTR_MAX_TRIALS 4096
#endif
#ifndef PSYTR_MAX_CONDITIONS
#define PSYTR_MAX_CONDITIONS 1024
#endif
#define PSYTR_MAX_FACTORS      8
#define PSYTR_MAX_CONSTRAINTS 16
#define PSYTR_MAX_TRACKS      16

/* The history and the schedule store trial and condition numbers in 16
 * bits to keep the handle small. */
#if PSYTR_MAX_TRIALS < 1 || PSYTR_MAX_TRIALS > 32767
#error "PSYTR_MAX_TRIALS must be in [1, 32767]"
#endif
#if PSYTR_MAX_CONDITIONS < 1 || PSYTR_MAX_CONDITIONS > 32767
#error "PSYTR_MAX_CONDITIONS must be in [1, 32767]"
#endif

/* --- codes ------------------------------------------------------------- */

#define PSYTR_DONE        (-1)  /* psytr_next(): nothing left to run        */
#define PSYTR_ERR_ARG     (-2)
#define PSYTR_ERR_CLOSED  (-3)
#define PSYTR_ERR_ORDER   (-4)
#define PSYTR_ERR_FULL    (-5)

#define PSYTR_INVALID     (-1)  /* an outcome that is no response           */
#define PSYTR_REQUEUE     (-2)  /* the outcome psytr_requeue() records, and
                                 * what psytr_restore() reads as a re-queue */
#define PSYTR_CONDITION   (-1)  /* "the condition row" as a constraint's factor */
#define PSYTR_ANY_LEVEL   (-1)  /* "whatever level it is" in a constraint    */

/* Static description of a PSYTR_ERR_* code ("ok" for values >= 0). */
PSYTR_API const char* psytr_strerror(int code);

/* PSYTR_VERSION_STRING as compiled into the implementation. */
PSYTR_API const char* psytr_version(void);

/* --- description ------------------------------------------------------- */

typedef enum psytr_order {
    PSYTR_ORDER_SEQUENTIAL = 0,
    PSYTR_ORDER_RANDOM,        /* shuffled within each repetition          */
    PSYTR_ORDER_FULL_RANDOM,   /* one shuffle over everything              */
    PSYTR_ORDER_CONSTRAINED    /* FULL_RANDOM repaired to the constraints  */
} psytr_order;

typedef enum psytr_interleave {
    PSYTR_INTERLEAVE_RANDOM = 0, /* by weight                              */
    PSYTR_INTERLEAVE_ROUND_ROBIN
} psytr_interleave;

typedef enum psytr_rule {
    PSYTR_RULE_MAX_RUN = 0,
    PSYTR_RULE_MAX_IN_WINDOW,
    PSYTR_RULE_MIN_GAP,
    PSYTR_RULE_NO_TRANSITION,
    PSYTR_RULE_FIRST_NOT
} psytr_rule;

/* A uniform variate in [0, 1) from the caller's generator. */
typedef double (*psytr_rng_fn)(void* ctx);

/* One splitmix64 step on the uint64_t at `state`, returned as a double in
 * [0, 1) built from the top 53 bits. The state is the caller's; pass its
 * address as rng_ctx. */
PSYTR_API double psytr_splitmix(void* state);

typedef struct psytr_factor_desc {
    const char* name;    /* for psytr_format_header(); may be NULL         */
    int         n_levels;
} psytr_factor_desc;

/* One ordering constraint; build with the helpers below. */
typedef struct psytr_constraint {
    psytr_rule rule;
    int factor;   /* factor index, or PSYTR_CONDITION                      */
    int level;    /* level index, or PSYTR_ANY_LEVEL (MAX_RUN, MAX_IN_WINDOW,
                   * MIN_GAP); NO_TRANSITION: the level that comes first    */
    int level2;   /* NO_TRANSITION: the level that may not follow          */
    int n;        /* MAX_RUN: run length; MAX_IN_WINDOW: count; MIN_GAP: gap */
    int window;   /* MAX_IN_WINDOW: window length                          */
} psytr_constraint;

PSYTR_API psytr_factor_desc psytr_factor(const char* name, int n_levels);
PSYTR_API psytr_constraint  psytr_max_run(int factor, int level, int max_run);
PSYTR_API psytr_constraint  psytr_max_in_window(int factor, int level, int window, int max_count);
PSYTR_API psytr_constraint  psytr_min_gap(int factor, int level, int gap);
PSYTR_API psytr_constraint  psytr_no_transition(int factor, int from_level, int to_level);
PSYTR_API psytr_constraint  psytr_first_not(int factor, int level);

/* Is the track finished? Called on the caller's handle; TRACKS says when. */
typedef bool (*psytr_done_fn)(void* ctx);

typedef struct psytr_track_desc {
    void*         ctx;
    psytr_done_fn is_done;   /* required                                   */
    double        weight;    /* RANDOM interleave; 0 = 1                   */
} psytr_track_desc;

PSYTR_API psytr_track_desc psytr_track(void* ctx, psytr_done_fn is_done);

/* Session description. Zero-initialize it and set only what you need.
 * Required: n_conditions >= 1 or n_factors >= 1 or n_tracks >= 1; reps >= 1
 * (or cond_reps) when there are conditions; track_rate when there are both
 * conditions and tracks; rng whenever RANDOMNESS says a draw is needed.
 * psytr_open() rejects, with a message: a null desc; a negative count;
 * more of anything than its PSYTR_MAX_*; a factor with no levels; a factor
 * product above PSYTR_MAX_CONDITIONS or unequal to a nonzero
 * n_conditions; no conditions and no tracks; conditions with reps < 1 and
 * no cond_reps, a negative cond_reps entry, or a zero sum; an order,
 * interleave or rule out of range; constraints under any order but
 * CONSTRAINED; a constraint whose factor or level is out of range, that
 * uses PSYTR_ANY_LEVEL where its rule takes none, or whose n, window or
 * gap is below 1; a track without is_done or with a weight that is
 * negative or not finite; a track_rate outside [0, 1], or 0 with both
 * conditions and tracks; practice or warmup with no conditions; n_warmup
 * without block_size; a warmup list entry out of range, a list longer than
 * PSYTR_MAX_CONDITIONS, or a count without a list; record_size without records; a missing rng; practice plus
 * scheduled (plus, without tracks, warmup) trials above PSYTR_MAX_TRIALS;
 * and, without tracks, constraints the repair cannot meet. */
typedef struct psytr_desc {
    int               n_conditions;  /* plain rows; 0 with factors         */
    psytr_factor_desc factors[PSYTR_MAX_FACTORS];
    int               n_factors;
    int               reps;          /* repetitions of every condition     */
    const int*        cond_reps;     /* per-condition repetitions, or NULL */
    psytr_order       order;
    psytr_constraint  constraints[PSYTR_MAX_CONSTRAINTS];
    int               n_constraints;
    int               max_swaps;     /* repair budget; 0 = 100000          */
    psytr_track_desc  tracks[PSYTR_MAX_TRACKS];
    int               n_tracks;
    psytr_interleave  interleave;
    double            track_rate;    /* P(track trial) while any runs      */
    int               block_size;    /* main trials per block; 0 = one     */
    bool              constraints_span_blocks; /* count runs across blocks */
    int               n_practice;    /* before the schedule                */
    int               n_warmup;      /* at the start of each later block   */
    const int*        warmup_conditions; /* rows to draw those from, or   */
    int               n_warmup_conditions; /* NULL / 0 = any condition     */
    int               requeue_gap;   /* 0 = append at the end              */
    psytr_rng_fn      rng;
    void*             rng_ctx;
    void*             records;       /* record_size x PSYTR_MAX_TRIALS     */
    size_t            record_size;   /* 0 = no records                     */
} psytr_desc;

/* --- handle ------------------------------------------------------------ */

/* What psytr_next() reports about the trial it chose. */
typedef struct psytr_trial_info {
    int  index;          /* trial number, from 0                          */
    int  condition;      /* row, or -1 for a track trial                   */
    int  track;          /* track, or -1 for a condition trial             */
    int  rep;            /* repetition of a scheduled trial, else -1       */
    int  block;          /* -1 for practice                                */
    bool first_in_block;
    bool after_break;    /* a scheduled or marked break precedes it        */
    bool practice;
    bool warmup;
    bool requeued;       /* a re-run of a re-queued trial                  */
} psytr_trial_info;

/* One recorded trial. psytr_history() hands out the array. */
typedef struct psytr_trial {
    int16_t condition;   /* -1 for a track trial                           */
    int8_t  track;       /* -1 for a condition trial                       */
    uint8_t flags;       /* PSYTR_FLAG_*                                   */
    int16_t rep;         /* -1 for practice, warmup and track trials       */
    int16_t block;       /* -1 for practice                                */
    int32_t outcome;     /* PSYTR_INVALID until update(), or as recorded   */
} psytr_trial;

#define PSYTR_FLAG_PRACTICE        1
#define PSYTR_FLAG_REQUEUED        2  /* a re-run of a re-queued trial      */
#define PSYTR_FLAG_DONE            4  /* update() or requeue() has run      */
#define PSYTR_FLAG_WARMUP          8  /* a reintroduction trial after a break */
#define PSYTR_FLAG_AFTER_BREAK    16  /* a break preceded it (scheduled, or
                                       * psytr_mark_break)                  */
#define PSYTR_FLAG_FIRST_IN_BLOCK 32
#define PSYTR_FLAG_VIOLATION      64  /* ran against a constraint because
                                       * nothing else could; see CONSTRAINTS */

/* Handle. The caller allocates it and treats every field as opaque.
 * psytr_open() and psytr_load() reset it, so it may be reused. Sized by
 * PSYTR_MAX_TRIALS and PSYTR_MAX_CONDITIONS; see MEMORY AND THREADS. */
typedef struct psytr_trials {
    psytr_desc  desc;                         /* defaults resolved          */
    int         n_cond;                       /* rows                       */
    int         level_stride[PSYTR_MAX_FACTORS];
    int         n_scheduled;                  /* slots: practice, then main */
    int         n_run;                        /* trials handed out          */
    int         n_done;                       /* trials updated or re-queued */
    int         current;                      /* trial index of the pending
                                               * next(), or -1             */
    int         schedule_pos;                 /* next slot to hand out      */
    int         rr_next;                      /* round-robin cursor         */
    uint32_t    track_done;                   /* bit i: track i said done   */
    int         break_pending;                /* psytr_mark_break() seen   */
    int         n_main;                       /* main trials handed out     */
    int         seg_start;                    /* main index starting the
                                               * constraint segment         */
    int         warm_block;                   /* block warm_done counts for */
    int         warm_done;
    int         swaps;                        /* repair swaps used at open  */
    bool        open;
    bool        has_cond_reps;                /* desc.cond_reps was given   */
    int16_t     cond_reps[PSYTR_MAX_CONDITIONS]; /* copied once at open     */
    int16_t     warm_list[PSYTR_MAX_CONDITIONS]; /* desc.warmup_conditions */
    int16_t     schedule[PSYTR_MAX_TRIALS];   /* condition per slot         */
    int16_t     schedule_rep[PSYTR_MAX_TRIALS];
    uint8_t     schedule_flags[PSYTR_MAX_TRIALS];
    int16_t     main_seq[PSYTR_MAX_TRIALS];   /* condition per main trial,
                                               * -1 for a track trial       */
    psytr_trial history[PSYTR_MAX_TRIALS];
    int32_t     tally_valid[PSYTR_MAX_CONDITIONS];
    int32_t     tally_pos[PSYTR_MAX_CONDITIONS]; /* outcome == 1, the common
                                                  * case, kept O(1)         */
    char        error[256];
} psytr_trials;

/* --- lifecycle --------------------------------------------------------- */

/* Validate `desc`, build the condition table, draw the practice trials,
 * order the schedule and repair it to the constraints. Returns false with
 * psytr_error() set on any failure, including (without tracks) an
 * unsatisfiable constraint set. It and psytr_load() are the only functions
 * that write the message buffer. */
PSYTR_API bool psytr_open(psytr_trials* t, const psytr_desc* desc);

/* The last open() or load() message ("" after a success). */
PSYTR_API const char* psytr_error(const psytr_trials* t);
PSYTR_API bool        psytr_is_open(const psytr_trials* t);

/* --- conditions -------------------------------------------------------- */

PSYTR_API int psytr_n_conditions(const psytr_trials* t);
PSYTR_API int psytr_n_factors(const psytr_trials* t);

/* Level index of `factor` in condition row `condition`, or PSYTR_ERR_ARG.
 * PSYTR_CONDITION as the factor returns the row itself. */
PSYTR_API int psytr_level(const psytr_trials* t, int condition, int factor);

/* Condition row from per-factor level indices `levels[n_factors]`, or
 * PSYTR_ERR_ARG. */
PSYTR_API int psytr_condition_from_levels(const psytr_trials* t, const int* levels);

/* Condition in slot `i` of the schedule, or PSYTR_ERR_ARG. Slots
 * 0..n_practice-1 are the practice trials, then the main schedule with any
 * re-queued copies. Not the trial index: track and warmup trials are not
 * scheduled. For preloading; ORDER says when an answer can change. */
PSYTR_API int psytr_condition_at(const psytr_trials* t, int i);
PSYTR_API int psytr_n_scheduled(const psytr_trials* t);

/* --- the loop ---------------------------------------------------------- */

/* Choose the next trial and describe it in `info` (may be NULL). Returns
 * its index, PSYTR_DONE, or a negative error. Idempotent until
 * psytr_update() or psytr_requeue(). */
PSYTR_API int psytr_next(psytr_trials* t, psytr_trial_info* info);

/* Record `outcome` (>= 0, or PSYTR_INVALID) for the pending trial and copy
 * record_size bytes from `rec` (NULL zero-fills the slot). Returns 0 or
 * PSYTR_ERR_*. */
PSYTR_API int psytr_update(psytr_trials* t, int outcome, const void* rec);

/* Record that a break was taken: before the pending trial when there is
 * one, else before the next trial handed out. Flags that trial and starts a
 * constraint segment; see BLOCKS, BREAKS, ... Returns 0 or PSYTR_ERR_*. */
PSYTR_API int psytr_mark_break(psytr_trials* t);

/* Record PSYTR_REQUEUE for the pending trial and schedule its condition
 * again; see BLOCKS, BREAKS, PRACTICE, WARMUP, CATCH TRIALS, RE-QUEUES.
 * Returns 0 or PSYTR_ERR_*. Call it INSTEAD of psytr_update() for that
 * trial. A track trial cannot be re-queued (the track decides), nor a
 * practice or warmup trial: PSYTR_ERR_ARG, and the trial stays pending. */
PSYTR_API int psytr_requeue(psytr_trials* t);

/* True when no trial is pending, the schedule is exhausted and every track
 * is done. Calls is_done() on each track not yet known to be done. */
PSYTR_API bool psytr_done(const psytr_trials* t);

/* Trials handed out (a pending one included), and trials completed. */
PSYTR_API int psytr_n_run(const psytr_trials* t);
PSYTR_API int psytr_n_done(const psytr_trials* t);

/* --- tallies ----------------------------------------------------------- */

/* Valid (outcome >= 0), non-practice, non-warmup trials of `condition` so
 * far; the number of its completed non-practice, non-warmup trials with
 * `outcome`; and that count over n_valid (NaN when n_valid is 0 or the
 * outcome is negative). psytr_count() for an outcome other than 1 scans
 * the history: O(trials). */
PSYTR_API int    psytr_n_valid(const psytr_trials* t, int condition);
PSYTR_API int    psytr_count(const psytr_trials* t, int condition, int outcome);
PSYTR_API double psytr_proportion(const psytr_trials* t, int condition, int outcome);

/* --- history ----------------------------------------------------------- */

/* The trial array and its length (a pending trial included). The pointer
 * is into the handle. NULL with *n = 0 on a closed handle. */
PSYTR_API const psytr_trial* psytr_history(const psytr_trials* t, int* n);

/* The caller's record for trial `i` (i < psytr_n_run()), or NULL. */
PSYTR_API const void* psytr_record(const psytr_trials* t, int i);

/* One newline-terminated CSV line for trial `i`: index,block,rep,
 * condition,track,practice,warmup,requeued,after_break,outcome, then one
 * column per factor level. Returns the length written or needed (snprintf
 * semantics: the line is complete when the return is below cap), or a
 * negative PSYTR_ERR_*. psytr_format_header() writes the matching header
 * using the factor names. */
PSYTR_API int psytr_format_row(const psytr_trials* t, int i, char* buf, size_t cap);
PSYTR_API int psytr_format_header(const psytr_trials* t, char* buf, size_t cap);

/* One newline-terminated line of space-separated key=value pairs: the
 * version, the desc's numbers, the constraints in helper-call form, the
 * repair's swap count, and trials scheduled, run and done. For the head of
 * a data file next to the seed the caller logs. snprintf semantics. */
PSYTR_API int psytr_format_meta(const psytr_trials* t, char* buf, size_t cap);

/* Snapshot: bytes needed (0 on a closed handle); write them (returns the
 * bytes written, or PSYTR_ERR_ARG when cap is too small); and rebuild from
 * them. `desc` re-supplies the pointer fields and must agree with the
 * snapshot on every number; a mismatch, a wrong magic or format, or a
 * truncated or corrupt snapshot fails the load with a message in
 * psytr_error(). SAVING, REPLAY AND RESUME gives the layout. */
PSYTR_API size_t psytr_save_size(const psytr_trials* t);
PSYTR_API int    psytr_save(const psytr_trials* t, void* buf, size_t cap);
PSYTR_API bool   psytr_load(psytr_trials* t, const psytr_desc* desc, const void* buf, size_t len);

/* Re-apply `n` outcomes (and records, record_size bytes each, or NULL) to a
 * freshly opened handle with the same desc and seed, advancing the schedule
 * as the original run did. An outcome of PSYTR_REQUEUE re-queues. Returns 0
 * or PSYTR_ERR_*; SAVING, REPLAY AND RESUME gives the limits. */
PSYTR_API int psytr_restore(psytr_trials* t, const int* outcomes, const void* records, int n);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PSY_TRIALS_H_INCLUDED */

/* ======================================================================= *
 *                             IMPLEMENTATION                              *
 * ======================================================================= */
#ifdef PSY_TRIALS_IMPLEMENTATION
#ifndef PSY_TRIALS_IMPLEMENTATION_GUARD
#define PSY_TRIALS_IMPLEMENTATION_GUARD

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef NAN
    #define PSYTR__NAN ((double)NAN)
#else
    /* No NAN macro means a pre-C99 library; inf - inf is the portable
     * spelling and this branch never compiles on the supported toolchains. */
    #define PSYTR__NAN (HUGE_VAL - HUGE_VAL)
#endif

#define PSYTR__DEFAULT_SWAPS 100000
#define PSYTR__SNAP_FORMAT   1u

/* break_pending bits: the flag goes on the next trial of any kind, the
 * segment starts at the next MAIN trial, and those can differ when the next
 * trial is a warmup. */
#define PSYTR__BREAK_FLAG 1
#define PSYTR__BREAK_SEG  2

/* isfinite() is C99 and MSVC's default C dialect does not declare it, so the
 * test is written in comparisons a NaN loses on its own. */
static bool psytr__finite(double x) {
    return x > -HUGE_VAL && x < HUGE_VAL;
}

/* --- codes, version, helpers ------------------------------------------- */

PSYTR_API const char* psytr_strerror(int code) {
    switch (code) {
    case PSYTR_DONE:       return "no trial left";
    case PSYTR_ERR_ARG:    return "invalid argument";
    case PSYTR_ERR_CLOSED: return "session is not open";
    case PSYTR_ERR_ORDER:  return "call out of order";
    case PSYTR_ERR_FULL:   return "trial history is full";
    default:               return code >= 0 ? "ok" : "unknown error";
    }
}

PSYTR_API const char* psytr_version(void) { return PSYTR_VERSION_STRING; }

PSYTR_API double psytr_splitmix(void* state) {
    uint64_t* s = (uint64_t*)state;
    uint64_t z;
    *s += 0x9E3779B97F4A7C15ULL;
    z = *s;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z = z ^ (z >> 31);
    return (double)(z >> 11) * (1.0 / 9007199254740992.0);
}

PSYTR_API psytr_factor_desc psytr_factor(const char* name, int n_levels) {
    psytr_factor_desc f;
    f.name = name;
    f.n_levels = n_levels;
    return f;
}

static psytr_constraint psytr__constraint(psytr_rule rule, int factor, int level,
                                          int level2, int n, int window) {
    psytr_constraint c;
    c.rule = rule;
    c.factor = factor;
    c.level = level;
    c.level2 = level2;
    c.n = n;
    c.window = window;
    return c;
}

PSYTR_API psytr_constraint psytr_max_run(int factor, int level, int max_run) {
    return psytr__constraint(PSYTR_RULE_MAX_RUN, factor, level, 0, max_run, 0);
}

PSYTR_API psytr_constraint psytr_max_in_window(int factor, int level, int window,
                                               int max_count) {
    return psytr__constraint(PSYTR_RULE_MAX_IN_WINDOW, factor, level, 0, max_count, window);
}

PSYTR_API psytr_constraint psytr_min_gap(int factor, int level, int gap) {
    return psytr__constraint(PSYTR_RULE_MIN_GAP, factor, level, 0, gap, 0);
}

PSYTR_API psytr_constraint psytr_no_transition(int factor, int from_level, int to_level) {
    return psytr__constraint(PSYTR_RULE_NO_TRANSITION, factor, from_level, to_level, 0, 0);
}

PSYTR_API psytr_constraint psytr_first_not(int factor, int level) {
    return psytr__constraint(PSYTR_RULE_FIRST_NOT, factor, level, 0, 0, 0);
}

PSYTR_API psytr_track_desc psytr_track(void* ctx, psytr_done_fn is_done) {
    psytr_track_desc d;
    d.ctx = ctx;
    d.is_done = is_done;
    d.weight = 0.0;
    return d;
}

/* --- text -------------------------------------------------------------- */

/* An appender with snprintf semantics over a whole line built in pieces:
 * `len` keeps counting past `cap`, so the caller learns the size it needs. */
typedef struct psytr__str {
    char*  buf;
    size_t cap;
    size_t len;
} psytr__str;

static void psytr__cat(psytr__str* s, const char* fmt, ...) {
    va_list ap;
    char* dst = NULL;
    size_t room = 0;
    int n;
    if (s->buf && s->len < s->cap) {
        dst = s->buf + s->len;
        room = s->cap - s->len;
    }
    va_start(ap, fmt);
    n = vsnprintf(dst, room, fmt, ap);
    va_end(ap);
    if (n > 0) s->len += (size_t)n;
}

static void psytr__str_init(psytr__str* s, char* buf, size_t cap) {
    s->buf = buf;
    s->cap = buf ? cap : 0;
    s->len = 0;
    if (s->buf && s->cap > 0) s->buf[0] = '\0';
}

static int psytr__str_done(const psytr__str* s) {
    return s->len > 0x7fffffff ? 0x7fffffff : (int)s->len;
}

static const char* psytr__rule_name(psytr_rule r) {
    switch (r) {
    case PSYTR_RULE_MAX_RUN:       return "max_run";
    case PSYTR_RULE_MAX_IN_WINDOW: return "max_in_window";
    case PSYTR_RULE_MIN_GAP:       return "min_gap";
    case PSYTR_RULE_NO_TRANSITION: return "no_transition";
    case PSYTR_RULE_FIRST_NOT:     return "first_not";
    default:                       return "?";
    }
}

/* A constraint in the form of the helper call that builds it, so a message
 * or a meta line can be pasted back into code. */
static void psytr__describe(psytr__str* s, const psytr_constraint* c) {
    char f[16], l[16];
    if (c->factor == PSYTR_CONDITION) snprintf(f, sizeof(f), "cond");
    else snprintf(f, sizeof(f), "%d", c->factor);
    if (c->level == PSYTR_ANY_LEVEL) snprintf(l, sizeof(l), "any");
    else snprintf(l, sizeof(l), "%d", c->level);
    switch (c->rule) {
    case PSYTR_RULE_MAX_RUN:
    case PSYTR_RULE_MIN_GAP:
        psytr__cat(s, "%s(%s,%s,%d)", psytr__rule_name(c->rule), f, l, c->n);
        break;
    case PSYTR_RULE_MAX_IN_WINDOW:
        psytr__cat(s, "%s(%s,%s,%d,%d)", psytr__rule_name(c->rule), f, l, c->window, c->n);
        break;
    case PSYTR_RULE_NO_TRANSITION:
        psytr__cat(s, "%s(%s,%s,%d)", psytr__rule_name(c->rule), f, l, c->level2);
        break;
    default:
        psytr__cat(s, "%s(%s,%s)", psytr__rule_name(c->rule), f, l);
        break;
    }
}

static bool psytr__fail(psytr_trials* t, const char* fmt, ...) {
    va_list ap;
    int n;
    n = snprintf(t->error, sizeof(t->error), "psy_trials: ");
    if (n < 0) n = 0;
    va_start(ap, fmt);
    vsnprintf(t->error + n, sizeof(t->error) - (size_t)n, fmt, ap);
    va_end(ap);
    t->open = false;
    return false;
}

/* --- conditions -------------------------------------------------------- */

/* The counts, clamped to the arrays they index. open() and load() already
 * guarantee every count fits its array, so at run time these are the counts
 * themselves. They exist for the optimizer, which cannot see what open()
 * checked: without a bound it can carry a constant from a caller's
 * deliberately bad argument into a path open() makes impossible and then
 * warn about it (gcc 16 does, for loops like these in psy_quest.h). Every
 * loop and index below that touches a fixed-size array is bounded by one of
 * these, or by the array's size directly. */
static int psytr__clamp(int n, int cap) {
    return (n < 0) ? 0 : (n > cap ? cap : n);
}
static int psytr__nf(const psytr_trials* t) { return psytr__clamp(t->desc.n_factors, PSYTR_MAX_FACTORS); }
static int psytr__nci(const psytr_trials* t) { return psytr__clamp(t->desc.n_constraints, PSYTR_MAX_CONSTRAINTS); }
static int psytr__ntr(const psytr_trials* t) { return psytr__clamp(t->desc.n_tracks, PSYTR_MAX_TRACKS); }
static int psytr__nc(const psytr_trials* t) { return psytr__clamp(t->n_cond, PSYTR_MAX_CONDITIONS); }
static int psytr__nwl(const psytr_trials* t) { return psytr__clamp(t->desc.n_warmup_conditions, PSYTR_MAX_CONDITIONS); }
static int psytr__nsch(const psytr_trials* t) { return psytr__clamp(t->n_scheduled, PSYTR_MAX_TRIALS); }
static int psytr__nrun(const psytr_trials* t) { return psytr__clamp(t->n_run, PSYTR_MAX_TRIALS); }
static int psytr__nmain(const psytr_trials* t) { return psytr__clamp(t->n_main, PSYTR_MAX_TRIALS); }

/* Level of factor `f` (or the row, for PSYTR_CONDITION) in the trial with
 * row `c`; -1 for a track trial, which has no level of anything. */
static int psytr__lv(const psytr_trials* t, int c, int f) {
    if (c < 0) return -1;
    if (f == PSYTR_CONDITION) return c;
    if (f < 0 || f >= PSYTR_MAX_FACTORS) return -1;
    return (c / t->level_stride[f]) % t->desc.factors[f].n_levels;
}

static int psytr__reps_of(const psytr_trials* t, int c) {
    if (!t->has_cond_reps) return t->desc.reps;
    return (c >= 0 && c < PSYTR_MAX_CONDITIONS) ? t->cond_reps[c] : 0;
}

/* --- the generator ----------------------------------------------------- */

/* An index below m; see DRAW ORDER. The comparisons are written so a NaN
 * lands on 0 and nothing reaches the (int) conversion out of range. */
static int psytr__draw(psytr_trials* t, int m) {
    double u = t->desc.rng(t->desc.rng_ctx);
    int j;
    if (!(u > 0.0)) return 0;
    if (!(u < 1.0)) return m - 1;
    j = (int)(u * (double)m);
    return j < m ? j : m - 1;
}

static void psytr__shuffle(psytr_trials* t, int16_t* a, int n) {
    int i, j;
    int16_t tmp;
    for (i = n - 1; i > 0; i--) {
        j = psytr__draw(t, i + 1);
        tmp = a[i]; a[i] = a[j]; a[j] = tmp;
    }
}

/* Practice and warmup rows: the k-th from the list in order, or a draw. */
static int psytr__pick_easy(psytr_trials* t, int k) {
    int nl = psytr__nwl(t);
    int m = nl > 0 ? nl : psytr__nc(t);
    int i;
    if (m < 1) return 0;
    i = (t->desc.order == PSYTR_ORDER_SEQUENTIAL) ? k % m : psytr__draw(t, m);
    return nl > 0 ? t->warm_list[i] : i;
}

/* --- constraints ------------------------------------------------------- */

/* The first constraint the trial at seq[k] breaks, looking back no further
 * than seg (the segment's first index); -1 when it breaks none. Every rule
 * is phrased as "the trial at hand completes a violation", so a scan in
 * order finds the first violated slot and a trial never has to look ahead.
 * The same test serves the repair at open (seq = the main schedule) and the
 * check at run time (seq = the main trials so far plus the candidate). */
static int psytr__violation(const psytr_trials* t, const int16_t* seq, int seg, int k) {
    int ci, j, cnt, lo, own, target, f, nci = psytr__nci(t);
    for (ci = 0; ci < nci; ci++) {
        const psytr_constraint* c = &t->desc.constraints[ci];
        f = c->factor;
        own = psytr__lv(t, seq[k], f);
        if (own < 0) continue;
        target = (c->level == PSYTR_ANY_LEVEL) ? own : c->level;
        switch (c->rule) {
        case PSYTR_RULE_MAX_RUN:
            if (own != target) break;
            cnt = 1;
            for (j = k - 1; j >= seg && cnt <= c->n; j--) {
                if (psytr__lv(t, seq[j], f) != target) break;
                cnt++;
            }
            if (cnt > c->n) return ci;
            break;
        case PSYTR_RULE_MAX_IN_WINDOW:
            if (own != target) break;
            lo = k - c->window + 1;
            if (lo < seg) lo = seg;
            cnt = 0;
            for (j = lo; j <= k; j++)
                if (psytr__lv(t, seq[j], f) == target) cnt++;
            if (cnt > c->n) return ci;
            break;
        case PSYTR_RULE_MIN_GAP:
            if (own != target) break;
            lo = k - c->n;
            if (lo < seg) lo = seg;
            for (j = lo; j < k; j++)
                if (psytr__lv(t, seq[j], f) == target) return ci;
            break;
        case PSYTR_RULE_NO_TRANSITION:
            if (own == c->level2 && k - 1 >= seg && psytr__lv(t, seq[k - 1], f) == c->level)
                return ci;
            break;
        case PSYTR_RULE_FIRST_NOT:
            if (k == 0 && own == c->level) return ci;
            break;
        default:
            break;
        }
    }
    return -1;
}

/* The swap search of CONSTRAINTS, AT OPEN, on seq[0..n). bs > 0 cuts
 * segments every bs slots. Returns -1 when every rule holds, else the
 * constraint still broken, with its slot in *at. */
#define PSYTR__REPAIR_TRIES 64

static int psytr__repair(psytr_trials* t, int16_t* seq, int n, int bs, int* at) {
    int k = 0, v = -1, j = 0, s, step, tries;
    int16_t tmp;
    bool fits;
    t->swaps = 0;
    for (;;) {
        for (; k < n; k++) {
            v = psytr__violation(t, seq, bs > 0 ? k - k % bs : 0, k);
            if (v >= 0) break;
        }
        if (k >= n) return -1;
        if (t->swaps >= t->desc.max_swaps || n < 2) { *at = k; return v; }
        /* A blind random partner gets trapped at the end of the schedule,
         * where few slots are left to trade with (measured: 15% failures on
         * 3 rows x 10 with no immediate repeat). So the one draw only picks
         * where the search for a partner starts; the first partner that
         * leaves both slots legal looking back wins, and if none of the
         * tries does, the drawn one is taken anyway to move the search on. */
        s = psytr__draw(t, n - 1);
        tries = n - 1 < PSYTR__REPAIR_TRIES ? n - 1 : PSYTR__REPAIR_TRIES;
        fits = false;
        for (step = 0; step < tries; step++) {
            j = (k + 1 + (s + step) % (n - 1)) % n;
            if (seq[j] == seq[k]) continue;
            tmp = seq[k]; seq[k] = seq[j]; seq[j] = tmp;
            fits = psytr__violation(t, seq, bs > 0 ? k - k % bs : 0, k) < 0 &&
                   psytr__violation(t, seq, bs > 0 ? j - j % bs : 0, j) < 0;
            if (fits) break;
            tmp = seq[k]; seq[k] = seq[j]; seq[j] = tmp;
        }
        if (!fits) {
            j = (k + 1 + s) % n;
            tmp = seq[k]; seq[k] = seq[j]; seq[j] = tmp;
        }
        t->swaps++;
        /* Only slots at or after the lower of the two changed, and every
         * rule looks back, so the slots before it are still good. */
        if (j < k) k = j;
    }
}

/* --- lifecycle --------------------------------------------------------- */

static bool psytr__check_constraint(psytr_trials* t, const psytr_desc* d, int ci, int n_cond) {
    const psytr_constraint* c = &d->constraints[ci];
    int n_lev;
    bool any_ok;
    if ((int)c->rule < (int)PSYTR_RULE_MAX_RUN || (int)c->rule > (int)PSYTR_RULE_FIRST_NOT)
        return psytr__fail(t, "desc.constraints[%d].rule is not a psytr_rule", ci);
    if (c->factor != PSYTR_CONDITION &&
        (c->factor < 0 || c->factor >= d->n_factors || c->factor >= PSYTR_MAX_FACTORS))
        return psytr__fail(t, "desc.constraints[%d].factor is not a factor or PSYTR_CONDITION", ci);
    n_lev = (c->factor == PSYTR_CONDITION) ? n_cond : d->factors[c->factor].n_levels;
    any_ok = c->rule == PSYTR_RULE_MAX_RUN || c->rule == PSYTR_RULE_MAX_IN_WINDOW ||
             c->rule == PSYTR_RULE_MIN_GAP;
    if (c->level == PSYTR_ANY_LEVEL) {
        if (!any_ok)
            return psytr__fail(t, "desc.constraints[%d]: %s takes no PSYTR_ANY_LEVEL", ci,
                               psytr__rule_name(c->rule));
    } else if (c->level < 0 || c->level >= n_lev) {
        return psytr__fail(t, "desc.constraints[%d].level is out of range", ci);
    }
    switch (c->rule) {
    case PSYTR_RULE_MAX_RUN:
    case PSYTR_RULE_MIN_GAP:
        if (c->n < 1) return psytr__fail(t, "desc.constraints[%d].n must be at least 1", ci);
        break;
    case PSYTR_RULE_MAX_IN_WINDOW:
        if (c->n < 1 || c->window < 1)
            return psytr__fail(t, "desc.constraints[%d]: window and count must be at least 1", ci);
        break;
    case PSYTR_RULE_NO_TRANSITION:
        if (c->level2 < 0 || c->level2 >= n_lev)
            return psytr__fail(t, "desc.constraints[%d].level2 is out of range", ci);
        break;
    default:
        break;
    }
    return true;
}

/* Everything open() and load() share: validate, copy the desc with its
 * defaults resolved, size the schedule. No generator draw and no track
 * call, so load() can use it. */
static bool psytr__setup(psytr_trials* t, const psytr_desc* d) {
    int i, f, n_cond, n_main = 0;
    long prod;
    const char* why = NULL;

    memset(t, 0, sizeof(*t));
    t->current = -1;
    if (!d) return psytr__fail(t, "null desc");

    if (d->n_conditions < 0) return psytr__fail(t, "desc.n_conditions is negative");
    if (d->n_factors < 0 || d->n_factors > PSYTR_MAX_FACTORS)
        return psytr__fail(t, "desc.n_factors must be in [0, PSYTR_MAX_FACTORS]");
    n_cond = d->n_conditions;
    if (d->n_factors > 0) {
        prod = 1;
        for (f = 0; f < d->n_factors && f < PSYTR_MAX_FACTORS; f++) {
            if (d->factors[f].n_levels < 1)
                return psytr__fail(t, "desc.factors[%d].n_levels must be at least 1", f);
            if (d->factors[f].n_levels > PSYTR_MAX_CONDITIONS)
                return psytr__fail(t, "the factors' product is above PSYTR_MAX_CONDITIONS");
            prod *= d->factors[f].n_levels;
            if (prod > PSYTR_MAX_CONDITIONS)
                return psytr__fail(t, "the factors' product is above PSYTR_MAX_CONDITIONS");
        }
        if (d->n_conditions != 0 && d->n_conditions != (int)prod)
            return psytr__fail(t, "desc.n_conditions is not 0 or the factors' product");
        n_cond = (int)prod;
    }
    if (n_cond > PSYTR_MAX_CONDITIONS)
        return psytr__fail(t, "desc.n_conditions is above PSYTR_MAX_CONDITIONS");
    if (d->n_tracks < 0 || d->n_tracks > PSYTR_MAX_TRACKS)
        return psytr__fail(t, "desc.n_tracks must be in [0, PSYTR_MAX_TRACKS]");
    if (n_cond == 0 && d->n_tracks == 0)
        return psytr__fail(t, "desc needs conditions (n_conditions or factors) or tracks");
    if ((int)d->order < (int)PSYTR_ORDER_SEQUENTIAL || (int)d->order > (int)PSYTR_ORDER_CONSTRAINED)
        return psytr__fail(t, "desc.order is not a psytr_order");
    if ((int)d->interleave < (int)PSYTR_INTERLEAVE_RANDOM ||
        (int)d->interleave > (int)PSYTR_INTERLEAVE_ROUND_ROBIN)
        return psytr__fail(t, "desc.interleave is not a psytr_interleave");

    if (n_cond > 0) {
        if (d->cond_reps) {
            /* Read the caller's array exactly once, n_cond entries, into the
             * handle; nothing reads it again. */
            int rc;
            for (i = 0; i < n_cond && i < PSYTR_MAX_CONDITIONS; i++) {
                rc = d->cond_reps[i];
                if (rc < 0)
                    return psytr__fail(t, "desc.cond_reps[%d] is negative", i);
                if (rc > PSYTR_MAX_TRIALS)
                    return psytr__fail(t, "more scheduled trials than PSYTR_MAX_TRIALS");
                t->cond_reps[i] = (int16_t)rc;
                n_main += rc;
                if (n_main > PSYTR_MAX_TRIALS)
                    return psytr__fail(t, "more scheduled trials than PSYTR_MAX_TRIALS");
            }
            if (n_main < 1) return psytr__fail(t, "desc.cond_reps sums to 0");
            t->has_cond_reps = true;
        } else {
            if (d->reps < 1)
                return psytr__fail(t, "desc.reps must be at least 1 (or give desc.cond_reps)");
            if (d->reps > PSYTR_MAX_TRIALS || (long)d->reps * n_cond > PSYTR_MAX_TRIALS)
                return psytr__fail(t, "more scheduled trials than PSYTR_MAX_TRIALS");
            n_main = d->reps * n_cond;
        }
    }

    if (d->n_constraints < 0 || d->n_constraints > PSYTR_MAX_CONSTRAINTS)
        return psytr__fail(t, "desc.n_constraints must be in [0, PSYTR_MAX_CONSTRAINTS]");
    if (d->n_constraints > 0 && d->order != PSYTR_ORDER_CONSTRAINED)
        return psytr__fail(t, "constraints need desc.order = PSYTR_ORDER_CONSTRAINED");
    for (i = 0; i < d->n_constraints && i < PSYTR_MAX_CONSTRAINTS; i++)
        if (!psytr__check_constraint(t, d, i, n_cond)) return false;
    if (d->max_swaps < 0) return psytr__fail(t, "desc.max_swaps is negative");

    for (i = 0; i < d->n_tracks && i < PSYTR_MAX_TRACKS; i++) {
        if (!d->tracks[i].is_done)
            return psytr__fail(t, "desc.tracks[%d].is_done is NULL", i);
        if (!psytr__finite(d->tracks[i].weight) || d->tracks[i].weight < 0.0)
            return psytr__fail(t, "desc.tracks[%d].weight must be finite and not negative", i);
    }
    if (!psytr__finite(d->track_rate) || d->track_rate < 0.0 || d->track_rate > 1.0)
        return psytr__fail(t, "desc.track_rate must be in [0, 1]");
    if (d->n_tracks > 0 && n_cond > 0 && d->track_rate == 0.0)
        return psytr__fail(t, "desc.track_rate is required with both conditions and tracks");

    if (d->block_size < 0) return psytr__fail(t, "desc.block_size is negative");
    if (d->n_practice < 0 || d->n_warmup < 0)
        return psytr__fail(t, "desc.n_practice and desc.n_warmup must not be negative");
    if (d->n_practice > PSYTR_MAX_TRIALS || d->n_warmup > PSYTR_MAX_TRIALS)
        return psytr__fail(t, "more practice or warmup trials than PSYTR_MAX_TRIALS");
    if ((d->n_practice > 0 || d->n_warmup > 0) && n_cond == 0)
        return psytr__fail(t, "practice and warmup trials need conditions");
    if (d->n_warmup > 0 && d->block_size == 0)
        return psytr__fail(t, "desc.n_warmup needs desc.block_size");
    if (d->n_warmup_conditions < 0)
        return psytr__fail(t, "desc.n_warmup_conditions is negative");
    if (d->n_warmup_conditions > PSYTR_MAX_CONDITIONS)
        return psytr__fail(t, "desc.n_warmup_conditions is above PSYTR_MAX_CONDITIONS");
    if (d->n_warmup_conditions > 0) {
        int wc;
        if (!d->warmup_conditions)
            return psytr__fail(t, "desc.n_warmup_conditions without desc.warmup_conditions");
        for (i = 0; i < d->n_warmup_conditions && i < PSYTR_MAX_CONDITIONS; i++) {
            wc = d->warmup_conditions[i];
            if (wc < 0 || wc >= n_cond)
                return psytr__fail(t, "desc.warmup_conditions[%d] is not a condition", i);
            t->warm_list[i] = (int16_t)wc;
        }
    }
    if (d->requeue_gap < 0) return psytr__fail(t, "desc.requeue_gap is negative");
    if (d->record_size > 0 && !d->records)
        return psytr__fail(t, "desc.record_size needs desc.records");

    /* Practice and warmup draw only under an order other than SEQUENTIAL,
     * and they need conditions, so the first test covers them. */
    if (!d->rng) {
        if (n_cond > 0 && d->order != PSYTR_ORDER_SEQUENTIAL)
            why = "an order other than SEQUENTIAL (and its practice and warmup draws)";
        else if (d->n_tracks > 1 && d->interleave == PSYTR_INTERLEAVE_RANDOM)
            why = "RANDOM interleave of more than one track";
        else if (d->n_tracks > 0 && n_cond > 0 && d->track_rate < 1.0)
            why = "a track_rate below 1";
        else if (d->requeue_gap > 0)
            why = "a requeue_gap";
        if (why) return psytr__fail(t, "desc.rng is required for %s", why);
    }

    {
        long total = (long)d->n_practice + n_main;
        if (d->n_tracks == 0 && d->block_size > 0 && d->n_warmup > 0 && n_main > 0) {
            long blocks = ((long)n_main + d->block_size - 1) / d->block_size;
            total += (blocks - 1) * (long)d->n_warmup;
        }
        if (total > PSYTR_MAX_TRIALS)
            return psytr__fail(t, "practice, scheduled and warmup trials exceed PSYTR_MAX_TRIALS (%ld > %d)",
                               total, PSYTR_MAX_TRIALS);
    }

    t->desc = *d;
    /* The handle holds copies; clearing the pointers makes any later read
     * of the caller's arrays a crash in testing, not a silent dependency. */
    t->desc.cond_reps = NULL;
    t->desc.warmup_conditions = NULL;
    if (t->desc.max_swaps == 0) t->desc.max_swaps = PSYTR__DEFAULT_SWAPS;
    if (n_cond == 0) t->desc.track_rate = 1.0;
    else if (d->n_tracks == 0) t->desc.track_rate = 0.0;
    for (i = 0; i < psytr__ntr(t); i++)
        if (t->desc.tracks[i].weight == 0.0) t->desc.tracks[i].weight = 1.0;
    if (n_cond == 0) {
        t->desc.reps = 0;
        t->has_cond_reps = false;
    }

    t->n_cond = n_cond;
    f = psytr__nf(t);
    if (f > 0) {
        t->level_stride[f - 1] = 1;
        for (f = f - 2; f >= 0; f--)
            t->level_stride[f] = t->level_stride[f + 1] * t->desc.factors[f + 1].n_levels;
    }
    t->n_scheduled = d->n_practice + n_main;
    return true;
}

/* Fill the schedule: practice draws, the order, the repair, the reps. The
 * draws happen in exactly the order RANDOMNESS promises. */
static bool psytr__build(psytr_trials* t) {
    const psytr_desc* d = &t->desc;
    int np = psytr__clamp(d->n_practice, PSYTR_MAX_TRIALS);
    int n_main = psytr__clamp(psytr__nsch(t) - np, PSYTR_MAX_TRIALS - np);
    int nc = psytr__nc(t);
    int k, c, r, maxrep = 0, pos, start, at = -1, v;
    int16_t* main_sched = t->schedule + np;

    for (k = 0; k < np; k++) {
        t->schedule[k] = (int16_t)psytr__pick_easy(t, k);
        t->schedule_rep[k] = -1;
    }

    for (c = 0; c < nc; c++)
        if (psytr__reps_of(t, c) > maxrep) maxrep = psytr__reps_of(t, c);
    pos = np;
    for (r = 0; r < maxrep; r++) {
        start = pos;
        for (c = 0; c < nc && pos < np + n_main; c++)
            if (psytr__reps_of(t, c) > r) t->schedule[pos++] = (int16_t)c;
        if (d->order == PSYTR_ORDER_RANDOM) psytr__shuffle(t, t->schedule + start, pos - start);
    }
    if (d->order == PSYTR_ORDER_FULL_RANDOM || d->order == PSYTR_ORDER_CONSTRAINED)
        psytr__shuffle(t, main_sched, n_main);

    if (d->order == PSYTR_ORDER_CONSTRAINED && d->n_constraints > 0 && n_main > 0) {
        int bs = (d->n_tracks == 0 && !d->constraints_span_blocks) ? d->block_size : 0;
        v = psytr__repair(t, main_sched, n_main, bs, &at);
        if (v >= 0 && v < PSYTR_MAX_CONSTRAINTS && d->n_tracks == 0) {
            psytr__str s;
            char what[96];
            psytr__str_init(&s, what, sizeof(what));
            psytr__describe(&s, &d->constraints[v]);
            return psytr__fail(t, "constraint %d, %s, is still broken at main slot %d after %d swaps; "
                               "the design may be impossible (raise desc.max_swaps if not)",
                               v, what, at, t->swaps);
        }
    }

    /* The tallies are zero here and serve as the per-row counter. */
    for (k = 0; k < n_main; k++) {
        c = main_sched[k];
        if (c < 0 || c >= PSYTR_MAX_CONDITIONS) continue;
        t->schedule_rep[np + k] = (int16_t)t->tally_valid[c]++;
    }
    memset(t->tally_valid, 0, sizeof(t->tally_valid));
    return true;
}

PSYTR_API bool psytr_open(psytr_trials* t, const psytr_desc* desc) {
    if (!t) return false;
    if (!psytr__setup(t, desc)) return false;
    if (!psytr__build(t)) return false;
    t->error[0] = '\0';
    t->open = true;
    return true;
}

PSYTR_API const char* psytr_error(const psytr_trials* t) {
    return t ? t->error : "psy_trials: null handle";
}

PSYTR_API bool psytr_is_open(const psytr_trials* t) {
    return t != NULL && t->open;
}

/* --- conditions -------------------------------------------------------- */

PSYTR_API int psytr_n_conditions(const psytr_trials* t) {
    return (t && t->open) ? t->n_cond : 0;
}

PSYTR_API int psytr_n_factors(const psytr_trials* t) {
    return (t && t->open) ? t->desc.n_factors : 0;
}

PSYTR_API int psytr_level(const psytr_trials* t, int condition, int factor) {
    if (!t) return PSYTR_ERR_ARG;
    if (!t->open) return PSYTR_ERR_CLOSED;
    if (condition < 0 || condition >= t->n_cond) return PSYTR_ERR_ARG;
    if (factor != PSYTR_CONDITION && (factor < 0 || factor >= psytr__nf(t)))
        return PSYTR_ERR_ARG;
    return psytr__lv(t, condition, factor);
}

PSYTR_API int psytr_condition_from_levels(const psytr_trials* t, const int* levels) {
    int f, c = 0;
    if (!t || !levels) return PSYTR_ERR_ARG;
    if (!t->open) return PSYTR_ERR_CLOSED;
    if (psytr__nf(t) == 0) return PSYTR_ERR_ARG;
    for (f = 0; f < psytr__nf(t); f++) {
        if (levels[f] < 0 || levels[f] >= t->desc.factors[f].n_levels) return PSYTR_ERR_ARG;
        c += levels[f] * t->level_stride[f];
    }
    return c;
}

PSYTR_API int psytr_condition_at(const psytr_trials* t, int i) {
    if (!t) return PSYTR_ERR_ARG;
    if (!t->open) return PSYTR_ERR_CLOSED;
    if (i < 0 || i >= t->n_scheduled) return PSYTR_ERR_ARG;
    return t->schedule[i];
}

PSYTR_API int psytr_n_scheduled(const psytr_trials* t) {
    return (t && t->open) ? t->n_scheduled : 0;
}

/* --- the loop ---------------------------------------------------------- */

static void psytr__info(const psytr_trials* t, int i, psytr_trial_info* info) {
    const psytr_trial* h = &t->history[i];
    if (!info) return;
    info->index = i;
    info->condition = h->condition;
    info->track = h->track;
    info->rep = h->rep;
    info->block = h->block;
    info->first_in_block = (h->flags & PSYTR_FLAG_FIRST_IN_BLOCK) != 0;
    info->after_break = (h->flags & PSYTR_FLAG_AFTER_BREAK) != 0;
    info->practice = (h->flags & PSYTR_FLAG_PRACTICE) != 0;
    info->warmup = (h->flags & PSYTR_FLAG_WARMUP) != 0;
    info->requeued = (h->flags & PSYTR_FLAG_REQUEUED) != 0;
}

static int psytr__push(psytr_trials* t, int cond, int track, int rep, int block, int flags,
                       psytr_trial_info* info) {
    int i = t->n_run;
    psytr_trial* h;
    if (i < 0 || i >= PSYTR_MAX_TRIALS) return PSYTR_ERR_FULL;   /* callers checked */
    h = &t->history[i];
    if (t->break_pending & PSYTR__BREAK_FLAG) {
        flags |= PSYTR_FLAG_AFTER_BREAK;
        t->break_pending &= ~PSYTR__BREAK_FLAG;
    }
    h->condition = (int16_t)cond;
    h->track = (int8_t)track;
    h->flags = (uint8_t)flags;
    h->rep = (int16_t)rep;
    h->block = (int16_t)block;
    h->outcome = PSYTR_INVALID;
    t->n_run = i + 1;
    t->current = i;
    psytr__info(t, i, info);
    return i;
}

static int psytr__pick_track(psytr_trials* t, uint32_t live) {
    const psytr_desc* d = &t->desc;
    int i, k, n = 0, last = -1;
    double total = 0.0, u, acc = 0.0;
    if (d->interleave == PSYTR_INTERLEAVE_ROUND_ROBIN) {
        for (k = 0; k < psytr__ntr(t); k++) {
            i = (t->rr_next + k) % psytr__ntr(t);
            if (live & ((uint32_t)1 << i)) {
                t->rr_next = (i + 1) % psytr__ntr(t);
                return i;
            }
        }
        return -1;
    }
    for (i = 0; i < d->n_tracks && i < PSYTR_MAX_TRACKS; i++) {
        if (!(live & ((uint32_t)1 << i))) continue;
        n++;
        total += d->tracks[i].weight;
        last = i;
    }
    if (n <= 1) return last;
    u = d->rng(d->rng_ctx) * total;
    for (i = 0; i < d->n_tracks && i < PSYTR_MAX_TRACKS; i++) {
        if (!(live & ((uint32_t)1 << i))) continue;
        acc += d->tracks[i].weight;
        if (u < acc) return i;
    }
    return last;
}

/* Move slot `from` to slot `to` (< from), the slots between shifting up by
 * one, so the rest of the schedule keeps its order. */
static void psytr__move_forward(psytr_trials* t, int from, int to) {
    int16_t c, r;
    uint8_t f;
    size_t n;
    /* The size must be visibly nonnegative and inside the arrays, or gcc 13
     * reads a signed difference as a possible huge memmove. */
    if (to < 0 || from <= to || from >= PSYTR_MAX_TRIALS) return;
    n = (size_t)(unsigned)(from - to);
    c = t->schedule[from];
    r = t->schedule_rep[from];
    f = t->schedule_flags[from];
    memmove(t->schedule + to + 1, t->schedule + to, n * sizeof(t->schedule[0]));
    memmove(t->schedule_rep + to + 1, t->schedule_rep + to, n * sizeof(t->schedule_rep[0]));
    memmove(t->schedule_flags + to + 1, t->schedule_flags + to, n * sizeof(t->schedule_flags[0]));
    t->schedule[to] = c;
    t->schedule_rep[to] = r;
    t->schedule_flags[to] = f;
}

PSYTR_API int psytr_next(psytr_trials* t, psytr_trial_info* info) {
    const psytr_desc* d;
    uint32_t live = 0, bit;
    int i, q, bs, block, flags, cond, track, rep, seg;
    bool has_sched, starts_block, seg_new, use_track, viol;

    if (!t) return PSYTR_ERR_ARG;
    if (!t->open) return PSYTR_ERR_CLOSED;
    if (t->current >= 0 && t->current < PSYTR_MAX_TRIALS) {
        psytr__info(t, t->current, info);
        return t->current;
    }
    d = &t->desc;

    if (t->schedule_pos < d->n_practice) {
        if (t->n_run >= PSYTR_MAX_TRIALS) return PSYTR_ERR_FULL;
        flags = PSYTR_FLAG_PRACTICE;
        if (t->schedule_pos == 0) flags |= PSYTR_FLAG_FIRST_IN_BLOCK;
        cond = t->schedule[t->schedule_pos++];
        return psytr__push(t, cond, -1, -1, -1, flags, info);
    }

    for (i = 0; i < d->n_tracks && i < PSYTR_MAX_TRACKS; i++) {
        bit = (uint32_t)1 << i;
        if (t->track_done & bit) continue;
        if (d->tracks[i].is_done(d->tracks[i].ctx)) t->track_done |= bit;
        else live |= bit;
    }
    has_sched = t->schedule_pos < t->n_scheduled;
    if (!has_sched && live == 0) return PSYTR_DONE;
    if (t->n_run >= PSYTR_MAX_TRIALS) return PSYTR_ERR_FULL;

    bs = d->block_size;
    block = bs > 0 ? t->n_main / bs : 0;
    starts_block = t->n_main == 0 || (bs > 0 && t->n_main % bs == 0);

    /* n_warmup > 0 implies bs > 0, which open() checked. */
    if (starts_block && t->n_main > 0 && d->n_warmup > 0) {
        if (t->warm_block != block) {
            t->warm_block = block;
            t->warm_done = 0;
        }
        if (t->warm_done < d->n_warmup) {
            flags = PSYTR_FLAG_WARMUP;
            if (t->warm_done == 0) flags |= PSYTR_FLAG_FIRST_IN_BLOCK | PSYTR_FLAG_AFTER_BREAK;
            cond = psytr__pick_easy(t, t->warm_done);
            t->warm_done++;
            return psytr__push(t, cond, -1, -1, block, flags, info);
        }
    }

    flags = 0;
    if (starts_block && !(t->n_main > 0 && d->n_warmup > 0)) {
        flags |= PSYTR_FLAG_FIRST_IN_BLOCK;
        if (t->n_main > 0) flags |= PSYTR_FLAG_AFTER_BREAK;
    }
    seg_new = !d->constraints_span_blocks &&
              (starts_block || (t->break_pending & PSYTR__BREAK_SEG) != 0);
    if (d->constraints_span_blocks) seg = 0;
    else seg = seg_new ? t->n_main : t->seg_start;

    viol = false;
    if (has_sched && d->n_constraints > 0) {
        t->main_seq[t->n_main] = t->schedule[t->schedule_pos];
        viol = psytr__violation(t, t->main_seq, seg, t->n_main) >= 0;
    }

    if (live != 0 && has_sched) {
        if (viol || d->track_rate >= 1.0) use_track = true;
        else use_track = d->rng(d->rng_ctx) < d->track_rate;
    } else {
        use_track = live != 0;
    }

    if (use_track) {
        track = psytr__pick_track(t, live);
        cond = -1;
        rep = -1;
    } else {
        if (viol) {
            for (q = t->schedule_pos + 1; q < psytr__nsch(t); q++) {
                t->main_seq[t->n_main] = t->schedule[q];
                if (psytr__violation(t, t->main_seq, seg, t->n_main) < 0) break;
            }
            if (q < psytr__nsch(t)) psytr__move_forward(t, q, t->schedule_pos);
            else flags |= PSYTR_FLAG_VIOLATION;
        }
        track = -1;
        cond = t->schedule[t->schedule_pos];
        rep = t->schedule_rep[t->schedule_pos];
        if (t->schedule_flags[t->schedule_pos] & PSYTR_FLAG_REQUEUED) flags |= PSYTR_FLAG_REQUEUED;
        t->schedule_pos++;
    }

    t->main_seq[t->n_main] = (int16_t)cond;
    if (seg_new) t->seg_start = t->n_main;
    t->n_main++;
    t->break_pending &= ~PSYTR__BREAK_SEG;
    return psytr__push(t, cond, track, rep, block, flags, info);
}

/* The caller's slot for trial i, or NULL when there are no records. */
static unsigned char* psytr__slot(const psytr_trials* t, int i) {
    if (t->desc.record_size == 0 || !t->desc.records) return NULL;
    return (unsigned char*)t->desc.records + (size_t)i * t->desc.record_size;
}

static void psytr__finish(psytr_trials* t, int outcome) {
    psytr_trial* h = &t->history[t->current];
    h->outcome = outcome;
    h->flags |= PSYTR_FLAG_DONE;
    if (h->condition >= 0 && h->condition < PSYTR_MAX_CONDITIONS && outcome >= 0 &&
        !(h->flags & (PSYTR_FLAG_PRACTICE | PSYTR_FLAG_WARMUP))) {
        t->tally_valid[h->condition]++;
        if (outcome == 1) t->tally_pos[h->condition]++;
    }
    t->n_done++;
    t->current = -1;
}

PSYTR_API int psytr_update(psytr_trials* t, int outcome, const void* rec) {
    unsigned char* slot;
    if (!t) return PSYTR_ERR_ARG;
    if (!t->open) return PSYTR_ERR_CLOSED;
    if (t->current < 0 || t->current >= PSYTR_MAX_TRIALS) return PSYTR_ERR_ORDER;
    if (outcome < PSYTR_INVALID) return PSYTR_ERR_ARG;
    slot = psytr__slot(t, t->current);
    if (slot) {
        if (rec) memcpy(slot, rec, t->desc.record_size);
        else memset(slot, 0, t->desc.record_size);
    }
    psytr__finish(t, outcome);
    return 0;
}

PSYTR_API int psytr_mark_break(psytr_trials* t) {
    psytr_trial* h;
    if (!t) return PSYTR_ERR_ARG;
    if (!t->open) return PSYTR_ERR_CLOSED;
    if (t->current < 0 || t->current >= PSYTR_MAX_TRIALS) {
        t->break_pending |= PSYTR__BREAK_FLAG | PSYTR__BREAK_SEG;
        return 0;
    }
    h = &t->history[t->current];
    h->flags |= PSYTR_FLAG_AFTER_BREAK;
    if (h->flags & (PSYTR_FLAG_PRACTICE | PSYTR_FLAG_WARMUP)) t->break_pending |= PSYTR__BREAK_SEG;
    else t->seg_start = t->n_main - 1;
    return 0;
}

PSYTR_API int psytr_requeue(psytr_trials* t) {
    const psytr_desc* d;
    psytr_trial* h;
    unsigned char* slot;
    int p, lo, left;
    size_t n;

    if (!t) return PSYTR_ERR_ARG;
    if (!t->open) return PSYTR_ERR_CLOSED;
    if (t->current < 0 || t->current >= PSYTR_MAX_TRIALS) return PSYTR_ERR_ORDER;
    d = &t->desc;
    h = &t->history[t->current];
    if (h->track >= 0 || (h->flags & (PSYTR_FLAG_PRACTICE | PSYTR_FLAG_WARMUP)))
        return PSYTR_ERR_ARG;

    slot = psytr__slot(t, t->current);
    if (slot) memset(slot, 0, d->record_size);

    left = t->n_scheduled - t->schedule_pos;
    if (t->n_scheduled >= PSYTR_MAX_TRIALS || t->n_run + left + 1 > PSYTR_MAX_TRIALS) {
        psytr__finish(t, PSYTR_INVALID);
        return PSYTR_ERR_FULL;
    }

    p = t->n_scheduled;
    if (d->requeue_gap > 0) {
        lo = t->schedule_pos + d->requeue_gap;
        if (lo < t->n_scheduled) p = lo + psytr__draw(t, t->n_scheduled - lo + 1);
        else (void)psytr__draw(t, 1);
    }
    /* n_scheduled < PSYTR_MAX_TRIALS was checked above; restated so the size
     * is visibly inside the arrays (see psytr__move_forward). */
    if (p < 0 || p > t->n_scheduled || t->n_scheduled >= PSYTR_MAX_TRIALS) return PSYTR_ERR_FULL;
    n = (size_t)(unsigned)(t->n_scheduled - p);
    memmove(t->schedule + p + 1, t->schedule + p, n * sizeof(t->schedule[0]));
    memmove(t->schedule_rep + p + 1, t->schedule_rep + p, n * sizeof(t->schedule_rep[0]));
    memmove(t->schedule_flags + p + 1, t->schedule_flags + p, n * sizeof(t->schedule_flags[0]));
    t->schedule[p] = h->condition;
    t->schedule_rep[p] = h->rep;
    t->schedule_flags[p] = PSYTR_FLAG_REQUEUED;
    t->n_scheduled++;

    psytr__finish(t, PSYTR_REQUEUE);
    return 0;
}

PSYTR_API bool psytr_done(const psytr_trials* t) {
    int i;
    if (!t || !t->open) return false;
    if (t->current >= 0 || t->schedule_pos < t->n_scheduled) return false;
    for (i = 0; i < psytr__ntr(t); i++) {
        if (t->track_done & ((uint32_t)1 << i)) continue;
        if (!t->desc.tracks[i].is_done(t->desc.tracks[i].ctx)) return false;
    }
    return true;
}

PSYTR_API int psytr_n_run(const psytr_trials* t) {
    return (t && t->open) ? t->n_run : 0;
}

PSYTR_API int psytr_n_done(const psytr_trials* t) {
    return (t && t->open) ? t->n_done : 0;
}

/* --- tallies ----------------------------------------------------------- */

PSYTR_API int psytr_n_valid(const psytr_trials* t, int condition) {
    if (!t || !t->open || condition < 0 || condition >= t->n_cond) return 0;
    return t->tally_valid[condition];
}

PSYTR_API int psytr_count(const psytr_trials* t, int condition, int outcome) {
    int i, n = 0;
    const psytr_trial* h;
    if (!t) return PSYTR_ERR_ARG;
    if (!t->open) return PSYTR_ERR_CLOSED;
    if (condition < 0 || condition >= t->n_cond) return PSYTR_ERR_ARG;
    if (outcome == 1) return t->tally_pos[condition];
    for (i = 0; i < psytr__nrun(t); i++) {
        h = &t->history[i];
        if (h->condition != condition || !(h->flags & PSYTR_FLAG_DONE)) continue;
        if (h->flags & (PSYTR_FLAG_PRACTICE | PSYTR_FLAG_WARMUP)) continue;
        if (h->outcome == outcome) n++;
    }
    return n;
}

PSYTR_API double psytr_proportion(const psytr_trials* t, int condition, int outcome) {
    int n, k;
    if (!t || !t->open || condition < 0 || condition >= t->n_cond || outcome < 0)
        return PSYTR__NAN;
    n = t->tally_valid[condition];
    if (n == 0) return PSYTR__NAN;
    k = psytr_count(t, condition, outcome);
    return (double)k / (double)n;
}

/* --- history ----------------------------------------------------------- */

PSYTR_API const psytr_trial* psytr_history(const psytr_trials* t, int* n) {
    if (!t || !t->open) {
        if (n) *n = 0;
        return NULL;
    }
    if (n) *n = t->n_run;
    return t->history;
}

PSYTR_API const void* psytr_record(const psytr_trials* t, int i) {
    if (!t || !t->open || i < 0 || i >= t->n_run) return NULL;
    return psytr__slot(t, i);
}

/* A CSV field, quoted when it would otherwise split or break the line. */
static void psytr__csv_name(psytr__str* s, const char* name, int f) {
    const char* p;
    bool quote = false;
    if (!name) {
        psytr__cat(s, "factor%d", f);
        return;
    }
    for (p = name; *p; p++)
        if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') quote = true;
    if (!quote) {
        psytr__cat(s, "%s", name);
        return;
    }
    psytr__cat(s, "\"");
    for (p = name; *p; p++) {
        if (*p == '"') psytr__cat(s, "\"\"");
        else psytr__cat(s, "%c", *p);
    }
    psytr__cat(s, "\"");
}

PSYTR_API int psytr_format_header(const psytr_trials* t, char* buf, size_t cap) {
    psytr__str s;
    int f;
    if (!t) return PSYTR_ERR_ARG;
    if (!t->open) return PSYTR_ERR_CLOSED;
    psytr__str_init(&s, buf, cap);
    psytr__cat(&s, "index,block,rep,condition,track,practice,warmup,requeued,after_break,outcome");
    for (f = 0; f < psytr__nf(t); f++) {
        psytr__cat(&s, ",");
        psytr__csv_name(&s, t->desc.factors[f].name, f);
    }
    psytr__cat(&s, "\n");
    return psytr__str_done(&s);
}

PSYTR_API int psytr_format_row(const psytr_trials* t, int i, char* buf, size_t cap) {
    psytr__str s;
    const psytr_trial* h;
    int f;
    if (!t) return PSYTR_ERR_ARG;
    if (!t->open) return PSYTR_ERR_CLOSED;
    if (i < 0 || i >= t->n_run) return PSYTR_ERR_ARG;
    h = &t->history[i];
    psytr__str_init(&s, buf, cap);
    psytr__cat(&s, "%d,%d,%d,%d,%d,%d,%d,%d,%d,", i, (int)h->block, (int)h->rep,
               (int)h->condition, (int)h->track,
               (h->flags & PSYTR_FLAG_PRACTICE) ? 1 : 0,
               (h->flags & PSYTR_FLAG_WARMUP) ? 1 : 0,
               (h->flags & PSYTR_FLAG_REQUEUED) ? 1 : 0,
               (h->flags & PSYTR_FLAG_AFTER_BREAK) ? 1 : 0);
    if (h->flags & PSYTR_FLAG_DONE) psytr__cat(&s, "%d", (int)h->outcome);
    for (f = 0; f < psytr__nf(t); f++) {
        if (h->condition >= 0) psytr__cat(&s, ",%d", psytr__lv(t, h->condition, f));
        else psytr__cat(&s, ",");
    }
    psytr__cat(&s, "\n");
    return psytr__str_done(&s);
}

PSYTR_API int psytr_format_meta(const psytr_trials* t, char* buf, size_t cap) {
    static const char* const orders[] = { "sequential", "random", "full_random", "constrained" };
    const psytr_desc* d;
    psytr__str s;
    int i;
    if (!t) return PSYTR_ERR_ARG;
    if (!t->open) return PSYTR_ERR_CLOSED;
    d = &t->desc;
    psytr__str_init(&s, buf, cap);
    psytr__cat(&s, "psy_trials=%s conditions=%d factors=%d levels=", PSYTR_VERSION_STRING,
               t->n_cond, d->n_factors);
    for (i = 0; i < psytr__nf(t); i++)
        psytr__cat(&s, i ? "x%d" : "%d", d->factors[i].n_levels);
    if (t->has_cond_reps) {
        psytr__cat(&s, " cond_reps=");
        for (i = 0; i < psytr__nc(t); i++) psytr__cat(&s, i ? ",%d" : "%d", (int)t->cond_reps[i]);
    } else {
        psytr__cat(&s, " reps=%d", d->reps);
    }
    psytr__cat(&s, " order=%s constraints=", orders[d->order]);
    for (i = 0; i < psytr__nci(t); i++) {
        if (i) psytr__cat(&s, ";");
        psytr__describe(&s, &d->constraints[i]);
    }
    psytr__cat(&s, " max_swaps=%d swaps=%d span_blocks=%d tracks=%d interleave=%s weights=",
               d->max_swaps, t->swaps, d->constraints_span_blocks ? 1 : 0, d->n_tracks,
               d->interleave == PSYTR_INTERLEAVE_ROUND_ROBIN ? "round_robin" : "random");
    for (i = 0; i < psytr__ntr(t); i++) psytr__cat(&s, i ? ",%.15g" : "%.15g", d->tracks[i].weight);
    psytr__cat(&s, " track_rate=%.15g block_size=%d practice=%d warmup=%d warmup_conditions=",
               d->track_rate, d->block_size, d->n_practice, d->n_warmup);
    for (i = 0; i < psytr__nwl(t); i++)
        psytr__cat(&s, i ? ",%d" : "%d", (int)t->warm_list[i]);
    psytr__cat(&s, " requeue_gap=%d record_size=%lu scheduled=%d run=%d done=%d\n",
               d->requeue_gap, (unsigned long)d->record_size, t->n_scheduled, t->n_run,
               t->n_done);
    return psytr__str_done(&s);
}

/* --- snapshot ---------------------------------------------------------- */

/* One writer for three jobs: count (out and cmp NULL), write (out), and
 * compare against a snapshot (cmp), which is how load() checks the desc
 * without a second description of the layout that could drift. */
typedef struct psytr__w {
    unsigned char*       out;
    const unsigned char* cmp;
    size_t               pos;
    size_t               cap;
    const char*          diff;   /* compare: the first field that differs */
} psytr__w;

static void psytr__put(psytr__w* w, uint64_t v, int nbytes, const char* name) {
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

static void psytr__put_i32(psytr__w* w, int v, const char* name) {
    psytr__put(w, (uint64_t)(uint32_t)v, 4, name);
}

static void psytr__put_f64(psytr__w* w, double v, const char* name) {
    uint64_t u;
    memcpy(&u, &v, sizeof(u));
    psytr__put(w, u, 8, name);
}

static void psytr__put_desc(psytr__w* w, const psytr_trials* t) {
    const psytr_desc* d = &t->desc;
    int i;
    psytr__put_i32(w, d->n_conditions, "n_conditions");
    psytr__put_i32(w, d->n_factors, "n_factors");
    for (i = 0; i < psytr__nf(t); i++) psytr__put_i32(w, d->factors[i].n_levels, "factors[].n_levels");
    psytr__put_i32(w, d->reps, "reps");
    psytr__put(w, t->has_cond_reps ? 1u : 0u, 1, "cond_reps");
    if (t->has_cond_reps)
        for (i = 0; i < psytr__nc(t); i++) psytr__put_i32(w, t->cond_reps[i], "cond_reps[]");
    psytr__put_i32(w, (int)d->order, "order");
    psytr__put_i32(w, d->n_constraints, "n_constraints");
    for (i = 0; i < psytr__nci(t); i++) {
        const psytr_constraint* c = &d->constraints[i];
        psytr__put_i32(w, (int)c->rule, "constraints[].rule");
        psytr__put_i32(w, c->factor, "constraints[].factor");
        psytr__put_i32(w, c->level, "constraints[].level");
        psytr__put_i32(w, c->level2, "constraints[].level2");
        psytr__put_i32(w, c->n, "constraints[].n");
        psytr__put_i32(w, c->window, "constraints[].window");
    }
    psytr__put_i32(w, d->max_swaps, "max_swaps");
    psytr__put_i32(w, d->n_tracks, "n_tracks");
    for (i = 0; i < psytr__ntr(t); i++) psytr__put_f64(w, d->tracks[i].weight, "tracks[].weight");
    psytr__put_i32(w, (int)d->interleave, "interleave");
    psytr__put_f64(w, d->track_rate, "track_rate");
    psytr__put_i32(w, d->block_size, "block_size");
    psytr__put(w, d->constraints_span_blocks ? 1u : 0u, 1, "constraints_span_blocks");
    psytr__put_i32(w, d->n_practice, "n_practice");
    psytr__put_i32(w, d->n_warmup, "n_warmup");
    psytr__put_i32(w, d->n_warmup_conditions, "n_warmup_conditions");
    for (i = 0; i < psytr__nwl(t); i++)
        psytr__put_i32(w, t->warm_list[i], "warmup_conditions[]");
    psytr__put_i32(w, d->requeue_gap, "requeue_gap");
    psytr__put(w, (uint64_t)d->record_size, 8, "record_size");
}

static void psytr__put_all(psytr__w* w, const psytr_trials* t) {
    int i;
    const psytr_trial* h;
    const unsigned char* rec;
    size_t k, nrec;

    psytr__put(w, 'P', 1, "magic");
    psytr__put(w, 'S', 1, "magic");
    psytr__put(w, 'T', 1, "magic");
    psytr__put(w, 'R', 1, "magic");
    psytr__put(w, PSYTR__SNAP_FORMAT, 4, "format");
    psytr__put_desc(w, t);

    psytr__put_i32(w, t->n_scheduled, "n_scheduled");
    psytr__put_i32(w, t->n_run, "n_run");
    psytr__put_i32(w, t->n_done, "n_done");
    psytr__put_i32(w, t->current, "current");
    psytr__put_i32(w, t->schedule_pos, "schedule_pos");
    psytr__put_i32(w, t->rr_next, "rr_next");
    psytr__put(w, t->track_done, 4, "track_done");
    psytr__put_i32(w, t->break_pending, "break_pending");
    psytr__put_i32(w, t->n_main, "n_main");
    psytr__put_i32(w, t->seg_start, "seg_start");
    psytr__put_i32(w, t->warm_block, "warm_block");
    psytr__put_i32(w, t->warm_done, "warm_done");
    psytr__put_i32(w, t->swaps, "swaps");

    for (i = 0; i < psytr__nsch(t); i++)
        psytr__put(w, (uint64_t)(uint16_t)t->schedule[i], 2, "schedule");
    for (i = 0; i < psytr__nsch(t); i++)
        psytr__put(w, (uint64_t)(uint16_t)t->schedule_rep[i], 2, "schedule_rep");
    for (i = 0; i < psytr__nsch(t); i++)
        psytr__put(w, t->schedule_flags[i], 1, "schedule_flags");
    for (i = 0; i < psytr__nmain(t); i++)
        psytr__put(w, (uint64_t)(uint16_t)t->main_seq[i], 2, "main");
    for (i = 0; i < psytr__nrun(t); i++) {
        h = &t->history[i];
        psytr__put(w, (uint64_t)(uint16_t)h->condition, 2, "history");
        psytr__put(w, (uint64_t)(uint8_t)h->track, 1, "history");
        psytr__put(w, h->flags, 1, "history");
        psytr__put(w, (uint64_t)(uint16_t)h->rep, 2, "history");
        psytr__put(w, (uint64_t)(uint16_t)h->block, 2, "history");
        psytr__put(w, (uint64_t)(uint32_t)h->outcome, 4, "history");
    }
    for (i = 0; i < psytr__nc(t); i++) psytr__put_i32(w, t->tally_valid[i], "tally_valid");
    for (i = 0; i < psytr__nc(t); i++) psytr__put_i32(w, t->tally_pos[i], "tally_pos");
    if (t->desc.record_size > 0) {
        rec = (const unsigned char*)t->desc.records;
        nrec = (size_t)psytr__nrun(t) * t->desc.record_size;
        for (k = 0; k < nrec; k++) psytr__put(w, rec[k], 1, "records");
    }
}

PSYTR_API size_t psytr_save_size(const psytr_trials* t) {
    psytr__w w;
    if (!t || !t->open) return 0;
    memset(&w, 0, sizeof(w));
    psytr__put_all(&w, t);
    return w.pos;
}

PSYTR_API int psytr_save(const psytr_trials* t, void* buf, size_t cap) {
    psytr__w w;
    size_t need;
    if (!t) return PSYTR_ERR_ARG;
    if (!t->open) return PSYTR_ERR_CLOSED;
    need = psytr_save_size(t);
    if (!buf || cap < need || need > 0x7fffffff) return PSYTR_ERR_ARG;
    memset(&w, 0, sizeof(w));
    w.out = (unsigned char*)buf;
    w.cap = cap;
    psytr__put_all(&w, t);
    return (int)w.pos;
}

typedef struct psytr__r {
    const unsigned char* in;
    size_t               pos;
    size_t               len;
    bool                 bad;
} psytr__r;

static uint64_t psytr__get(psytr__r* r, int nbytes) {
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

static int psytr__get_i32(psytr__r* r) { return (int)(int32_t)(uint32_t)psytr__get(r, 4); }
static int psytr__get_i16(psytr__r* r) { return (int)(int16_t)(uint16_t)psytr__get(r, 2); }

PSYTR_API bool psytr_load(psytr_trials* t, const psytr_desc* desc, const void* buf, size_t len) {
    psytr__w w;
    psytr__r r;
    const unsigned char* in = (const unsigned char*)buf;
    int i, v, nt;
    psytr_trial* h;
    unsigned char* rec;
    size_t k, nrec;

    if (!t) return false;
    if (!psytr__setup(t, desc)) return false;
    if (!in) return psytr__fail(t, "psytr_load: null snapshot");
    if (len < 8 || in[0] != 'P' || in[1] != 'S' || in[2] != 'T' || in[3] != 'R')
        return psytr__fail(t, "psytr_load: not a psy_trials snapshot");
    memset(&r, 0, sizeof(r));
    r.in = in;
    r.len = len;
    r.pos = 4;
    if ((uint32_t)psytr__get(&r, 4) != PSYTR__SNAP_FORMAT)
        return psytr__fail(t, "psytr_load: snapshot format is not %u", PSYTR__SNAP_FORMAT);

    memset(&w, 0, sizeof(w));
    w.cmp = in;
    w.cap = len;
    w.pos = 8;
    psytr__put_desc(&w, t);
    if (w.diff)
        return psytr__fail(t, "psytr_load: desc.%s does not match the snapshot", w.diff);
    r.pos = w.pos;

    t->n_scheduled  = psytr__get_i32(&r);
    t->n_run        = psytr__get_i32(&r);
    t->n_done       = psytr__get_i32(&r);
    t->current      = psytr__get_i32(&r);
    t->schedule_pos = psytr__get_i32(&r);
    t->rr_next      = psytr__get_i32(&r);
    t->track_done   = (uint32_t)psytr__get(&r, 4);
    t->break_pending = psytr__get_i32(&r);
    t->n_main       = psytr__get_i32(&r);
    t->seg_start    = psytr__get_i32(&r);
    t->warm_block   = psytr__get_i32(&r);
    t->warm_done    = psytr__get_i32(&r);
    t->swaps        = psytr__get_i32(&r);
    nt = t->desc.n_tracks;
    if (r.bad ||
        t->n_scheduled < t->desc.n_practice || t->n_scheduled > PSYTR_MAX_TRIALS ||
        t->n_run < 0 || t->n_run > PSYTR_MAX_TRIALS ||
        t->n_done < 0 || t->n_done > t->n_run ||
        (t->current != -1 && t->current != t->n_run - 1) ||
        t->n_done != t->n_run - (t->current >= 0 ? 1 : 0) ||
        t->schedule_pos < 0 || t->schedule_pos > t->n_scheduled ||
        t->rr_next < 0 || t->rr_next >= (nt > 0 ? nt : 1) ||
        (nt < 32 && (t->track_done >> nt) != 0) ||
        t->break_pending < 0 || t->break_pending > 3 ||
        t->n_main < 0 || t->n_main > t->n_run ||
        t->seg_start < 0 || t->seg_start > t->n_main ||
        t->warm_done < 0 || t->warm_done > t->desc.n_warmup || t->swaps < 0)
        return psytr__fail(t, "psytr_load: the snapshot's counters are corrupt or truncated");

    for (i = 0; i < psytr__nsch(t); i++) {
        v = psytr__get_i16(&r);
        if (v < 0 || v >= t->n_cond) r.bad = true;
        t->schedule[i] = (int16_t)v;
    }
    for (i = 0; i < psytr__nsch(t); i++) t->schedule_rep[i] = (int16_t)psytr__get_i16(&r);
    for (i = 0; i < psytr__nsch(t); i++) t->schedule_flags[i] = (uint8_t)psytr__get(&r, 1);
    for (i = 0; i < psytr__nmain(t); i++) {
        v = psytr__get_i16(&r);
        if (v < -1 || v >= t->n_cond) r.bad = true;
        t->main_seq[i] = (int16_t)v;
    }
    for (i = 0; i < psytr__nrun(t); i++) {
        h = &t->history[i];
        v = psytr__get_i16(&r);
        if (v < -1 || v >= t->n_cond) r.bad = true;
        h->condition = (int16_t)v;
        v = (int)(int8_t)(uint8_t)psytr__get(&r, 1);
        if (v < -1 || v >= nt || (v < 0) == (h->condition < 0)) r.bad = true;
        h->track = (int8_t)v;
        h->flags = (uint8_t)psytr__get(&r, 1);
        h->rep = (int16_t)psytr__get_i16(&r);
        h->block = (int16_t)psytr__get_i16(&r);
        h->outcome = (int32_t)psytr__get_i32(&r);
    }
    for (i = 0; i < psytr__nc(t); i++) {
        t->tally_valid[i] = psytr__get_i32(&r);
        if (t->tally_valid[i] < 0) r.bad = true;
    }
    for (i = 0; i < psytr__nc(t); i++) {
        t->tally_pos[i] = psytr__get_i32(&r);
        if (t->tally_pos[i] < 0 || t->tally_pos[i] > t->tally_valid[i]) r.bad = true;
    }
    if (t->desc.record_size > 0) {
        nrec = (size_t)psytr__nrun(t) * t->desc.record_size;
        if (r.bad || r.len - r.pos < nrec) {
            r.bad = true;
        } else {
            rec = (unsigned char*)t->desc.records;
            for (k = 0; k < nrec; k++) rec[k] = r.in[r.pos + k];
            r.pos += nrec;
        }
    }
    if (r.bad)
        return psytr__fail(t, "psytr_load: the snapshot is corrupt or truncated");
    if (r.pos != r.len)
        return psytr__fail(t, "psytr_load: %lu bytes after the snapshot's end",
                           (unsigned long)(r.len - r.pos));

    t->error[0] = '\0';
    t->open = true;
    return true;
}

PSYTR_API int psytr_restore(psytr_trials* t, const int* outcomes, const void* records, int n) {
    const unsigned char* rec = (const unsigned char*)records;
    int i, rc;
    if (!t) return PSYTR_ERR_ARG;
    if (!t->open) return PSYTR_ERR_CLOSED;
    if (n < 0 || (n > 0 && !outcomes)) return PSYTR_ERR_ARG;
    if (t->n_run != 0) return PSYTR_ERR_ORDER;
    for (i = 0; i < n; i++) {
        rc = psytr_next(t, NULL);
        if (rc == PSYTR_DONE) return PSYTR_ERR_ARG;
        if (rc < 0) return rc;
        if (outcomes[i] == PSYTR_REQUEUE) {
            /* A refused re-queue recorded PSYTR_INVALID in the original run
             * too, so FULL here is the same state, not a failure. */
            rc = psytr_requeue(t);
            if (rc < 0 && rc != PSYTR_ERR_FULL) return rc;
        } else {
            rc = psytr_update(t, outcomes[i],
                              rec ? rec + (size_t)i * t->desc.record_size : NULL);
            if (rc < 0) return rc;
        }
    }
    return 0;
}

#endif /* PSY_TRIALS_IMPLEMENTATION_GUARD */
#endif /* PSY_TRIALS_IMPLEMENTATION */

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
