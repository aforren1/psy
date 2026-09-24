# psy_trials.h design

Status: **v0.1.1, implemented and compared.** Registered in
`CMakeLists.txt`, with `tests/adapt/psy_trials_test.c` under ctest and two
examples in CI; gcc and MSVC clean, sanitizer clean. The Python binding
`psy.trials` and the MEX function `psy_trials` exist, and
`tests/compare/compare_trials_psychopy.py` checks the binding against
PsychoPy's TrialHandler: identical sequential order, and the block
properties of the random orders over 100 seeds per design. The weighted
sequential order differs by design (PsychoPy runs a row's copies back to
back, this header cycles the rows), and the script reports that and does
not fail on it. This page records why the API looks the way it does and
what the implementation changed; the header says what it does.

## Goals

- The layer above the adaptive methods: which trial next, and what
  happened. The method headers refuse to own the trial loop (that is a
  stated non-goal in [psy_adapt.md](psy_adapt.md)), so every experiment
  writes the same shuffle, interleave and bookkeeping by hand. This header
  is that code, once.
- The method of constant stimuli as the base case, not a feature:
  conditions times repetitions in some order, with tallies per condition.
- Constrained random orders, stated as rules ("no more than three of the
  same orientation in a row", "a catch trial at most once in six"), checked
  at open so an impossible design fails before the session starts.
- Interleaved adaptive tracks with no dependency on the method headers: a
  track is an opaque pointer and an is-done callback.
- The same shape as the rest of the collection: one file, no heap, a
  caller-owned handle, a zeroed desc, `open` with the one message buffer,
  `next` / `update` / `done`, deterministic from the caller's generator, a
  history that replays.

## Non-goals

- Timing, stimuli, I/O. The header never touches a clock, a file or a
  device; `psytr_format_row` formats a line the caller writes.
- Fitting. Per-condition proportions are the estimate; the fit is offline.
- Counterbalancing across sessions or subjects (Latin squares, de Bruijn
  sequences, Williams designs). Those are design-time tools with their own
  literature; a session-level sequencer should not pretend to them.
- Transition balancing within a session (every level following every
  other equally often). It is a soft objective, not a constraint, and it
  interacts with every hard constraint; v0.2 may add it as a scored
  repair once the hard-constraint solver has been used in anger.
- Multi-session experiment structure (PsychoPy's ExperimentHandler). One
  handle is one session; the caller strings sessions together.

## Prior art

| Feature | PsychoPy | Here |
|---|---|---|
| Conditions x reps, sequential / random / fullRandom | `TrialHandler(trialList, nReps, method)` | `psytr_desc` with the same three orders, the same block semantics (random shuffles within each repetition) |
| Weighted conditions | `TrialHandlerExt` weights | `cond_reps[]` |
| Interleaved adaptive procedures | `MultiStairHandler(method='random'/'sequential')` | tracks with random-by-weight or round-robin interleave, plus `track_rate` to mix them with scheduled trials |
| Constraints on the order | none; users post-process `sequence` or roll their own | five rules, repaired at open, unsatisfiable sets fail at open |
| Catch trials | a condition with its own weight; spacing by hand | a condition placed by `psytr_min_gap` or `psytr_max_in_window` |
| Re-running missed trials | none built in (users append to `trialList`) | `psytr_requeue` with an optional gap |
| Practice | a separate handler | `n_practice`, flagged, untallied |
| Data | `addData`, `saveAsWideText` | a fixed-size caller record per trial, `psytr_format_row`, the history array |
| Resume | `saveAsPickle` | `psytr_save` / `psytr_load` byte snapshot, or `psytr_restore` from the outcomes plus the seed (a re-queue is its own outcome value so replay can tell it from an invalid response) |

Psychtoolbox has no equivalent; labs write their own. Palamedes has none.

## Decisions

### Tracks are opaque

The header could have included the three method headers and offered
`psytr_add_stair(&s)`. It does not, for the convention's sake (a header
includes nothing but psy_rt.h, and this one includes nothing) and because
the caller has to call the track's own `next` and `update` anyway: the
sequencer cannot know the level's type. A track is `{ctx, is_done, weight}`
and the adapter is three lines. The cost is that the history records which
track a trial came from but not its level; that is what the caller record
is for, and the second USAGE example puts the level there.

### Constraints are repaired, not sampled, and then held at run time

Rejection sampling of whole orders fails silently on tight constraints and
gives no diagnosis. The repair is deterministic from the seed, terminates,
and when it fails it names the first constraint still violated, so a
contradictory design is reported at open. It is not uniform over
satisfying orders; no practical method is, and the manual says so. The
five rules cover the constraints seen in practice: run length, density in
a window, minimum spacing, forbidden transitions, and the first trial.

Two things the specification got wrong, found by the implementation.
First, "swap the offending trial with a random later one" traps itself at
the end of the schedule and failed about 15 percent of easy designs
(three rows, ten repetitions, no immediate repeat); the shipped repair
draws a random start and tries up to 64 slots, wrapping, taking the first
swap that leaves both positions legal, which passes every property check
in a fraction of the time. Second, the claim that interleaved track trials
and re-queues "can only widen a gap or break a run" was false: a
re-queued trial is a trial the observer sees, and a schedule made of
identical catch trials (the second USAGE example) cannot be ordered to
satisfy any spacing rule at all. So constraints are also enforced while
the session runs: before handing out a scheduled trial, `next` checks it
against the trials already run; if it would break a rule, a track trial
runs instead while any track is live, else the first later slot that fits
is pulled forward, and if nothing fits the trial runs and its history
entry carries a violation flag. With tracks, a failed open-time repair is
therefore not an error; without tracks it still is.

### `track_rate`, not a merged schedule

Tracks end when their own stop rule fires, which the sequencer cannot
predict, so a merged schedule with fixed positions for track trials would
run out of track before or after the conditions. A rate keeps the mix
right on average and degrades gracefully: when the tracks finish, the
schedule runs alone; when the schedule finishes, the tracks run alone.
The default is 1 with no conditions and 0 with no tracks, and required
otherwise, so a mixed design states its intention.

### One generator, consumed in a fixed order

Every random choice (the shuffle, the repair, practice draws, track picks,
re-queue positions) draws from the caller's generator in a fixed order.
That is what makes `psytr_restore` work: a fresh open with the same seed
rebuilds the same schedule, and replaying the outcomes makes the same
run-time draws in the same order. The header owns no generator, as the
rest of the collection does not; `psytr_splitmix` is a step function on
state the caller owns.

### Breaks are the caller's, their trace is the header's

The header has no clock, so a break by elapsed time is the caller's check
between trials. What the header owns is the trace: `block_size` gives
scheduled boundaries and `psytr_mark_break` records an unscheduled one,
both flagged in the history, and both end a run for the constraints,
because a run of three orientations before a rest and three after is not
a run anyone perceives. `constraints_span_blocks` turns that off for a
design that wants the stricter reading.

### Warmup is a condition trial, never a track trial

`n_warmup` trials at the start of every later block, drawn from a
caller-named list of easy conditions, flagged and excluded from tallies
and constraints, are a reintroduction after a rest. They are never drawn
from a track: the header does not know what an easy level is. A caller
who wants a staircase warmed up sees the flag on a condition trial, shows
what it likes, and chooses whether the track hears about it.

### The header saves its state, not the experiment's data

The sequencer's state is small and scalar, so it saves three ways: CSV
rows for analysis, a key=value metadata line to head a data file, and a
versioned byte snapshot (`psytr_save`, `psytr_load`) that resumes a
crashed session without replaying the seed. No JSON parser: a header that
parses text is the wrong header. The caller's per-trial data is
deliberately bounded to `record_size` bytes. Anything large or variable
(an eye trace, an EEG epoch, audio) belongs in its own container keyed by
the trial index, with a reference in the fixed record if one is needed.
The sequencer's table stays narrow and joins on trial index, which is
how every analysis pipeline already works.

### Fixed capacity, inline

`PSYTR_MAX_TRIALS` (4096) and `PSYTR_MAX_CONDITIONS` (1024) size the
handle at compile time: 19 bytes per trial, 91480 bytes (89 KB) at the
defaults on x86-64. A session of more than 4096 trials is two handles.
The caller's per-trial record is the one variable-size
thing and lives in a buffer the caller provides, sized by
`record_size x PSYTR_MAX_TRIALS`, so nothing allocates.

### What PsychoPy replay can and cannot check

PsychoPy shuffles with NumPy's generator. The orders cannot match trial
for trial unless this header reproduces NumPy's Mersenne Twister and its
shuffle, which it will not. The comparison therefore checks properties
against PsychoPy: under `random`, every condition once per repetition
block and the blocks in order; under `fullRandom`, every condition exactly
`nReps` times; under `sequential`, the identical order. The constraint
rules are checked directly on generated orders, and the failure path is
checked with a design known to be impossible.

## Verification

The header's STATUS block has the numbers. In summary:

- Compile checks as C99, C11 and C++17 with warnings as errors, and MSVC
  /W4 /WX in its default C dialect and as C++17.
- `tests/adapt/psy_trials_test.c`, at `PSYTR_MAX_TRIALS` 256 and at the
  default 4096: the factorial index round trip; each order's properties
  over 400 seeds; each constraint rule holding on every repaired order of
  eight designs x 200 seeds, by a checker written separately from the
  header's, and the repair's failure message on an impossible set; tracks
  interleaved with `track_rate` at 0 (rejected with conditions), 0.5 and
  1, checking the mix and that a finished track is never picked; practice
  and warmup flagged and untallied; re-queues with and without a gap, and
  760 random re-queues under a no-repeat rule with no unflagged violation;
  `psytr_mark_break` ending a run; tallies against a hand count;
  `psytr_restore` reproducing a run bit for bit, with and without tracks;
  `psytr_save` at 42 cut points then `psytr_load` into a garbage-filled
  handle, identical to the uninterrupted run, snapshot bytes included;
  every load refusal and every rejected desc; the format functions against
  fixed strings.
- `examples/trials_mocs.c` (constant stimuli with a constraint, printing
  the proportions and a CSV) and `examples/trials_interleave.c` (three
  psy_stair.h staircases and a catch condition, the second USAGE example
  run for real, printing which track each trial went to). Both exit 0
  with no hardware, and print the same bytes under gcc and MSVC.
- `tests/compare/compare_trials_psychopy.py` runs `psy.trials` beside
  `psychopy.data.TrialHandler` and `TrialHandlerExt` on the same condition
  lists over 100 seeds: SEQUENTIAL is identical to PsychoPy's order, RANDOM
  holds each condition once per repetition block on both sides, and
  FULL_RANDOM, weighted or not, gives exact counts on both sides.

Not done: a run on macOS or on big-endian hardware, and a load of a
snapshot written by another compiler. The snapshot's portability is by
construction, not by test.
