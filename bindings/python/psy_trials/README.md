# psy.trials Python binding

A CPython extension for the [psy_trials.h](../../../psy_trials.h)
single-header library: the layer above the adaptive methods. It decides which
trial comes next (conditions and repetitions, sequential, random and
constrained orders, interleaved adaptive tracks, blocks, practice, warmup and
re-queued trials) and records what happened. It uses the plain CPython C API
and has no dependencies. It is built against the **Limited API / stable ABI**
(`Py_LIMITED_API = 0x03080000`), so one `psy/trials.abi3.so` works on CPython
3.8 and later.

The distribution is `psy-trials` and the module is `psy.trials`. `psy` is a
[PEP 420](https://peps.python.org/pep-0420/) implicit namespace package (no
`__init__.py`), so `psy-trials` installs alone or next to the other `psy-*`
distributions. A `psy.stair.Staircase`, `psy.quest.Quest` or `psy.gp.GP` is a
track as it is; none of those packages is required.

## How to install

From this directory:

```sh
uv pip install .                    # build and install an abi3 wheel
# or, in place for development:
python setup.py build_ext --inplace
```

In a development tree, setup.py finds `psy_trials.h` at the repository root.
An isolated build (an sdist or a cibuildwheel run) sees only this directory.
Copy the header next to `setup.py` first:

```sh
cp ../../../psy_trials.h .
uv build --sdist
```

The copy is gitignored, and the repository root wins when it is present.
psy_trials.h includes no other header of the collection.

## How to run the method of constant stimuli

```python
import psy.trials as pt

t = pt.Trials(factors=[("orientation", 2), ("contrast", 5)],   # 10 conditions
              reps=20,                                           # 200 trials
              order=pt.ORDER_CONSTRAINED,
              constraints=[pt.max_run("orientation", pt.ANY_LEVEL, 3)],
              rng=20260923)                                      # log the seed

while (ti := t.next()) is not None:
    ori = t.level(ti.condition, 0)
    con = t.level(ti.condition, 1)
    t.update(run_trial(ori, con))          # 1 = correct, 0 = not

for c in range(t.n_conditions):
    print(t.levels(c), t.proportion(c))
```

A constraint names a factor by index or by name, or `CONDITION` for the row.
An impossible design (for example, two rows with unequal counts and
`max_run(CONDITION, ANY_LEVEL, 1)`) fails at construction with `Error` and
the header's message, not at trial 190.

## How to interleave adaptive tracks

```python
import struct
import psy.stair as st

stairs = [st.Staircase(start=0.3, n_down=k, step_type=st.STEP_LOG,
                       steps=[0.2, 0.1], min=0.001, max=1.0, stop_reversals=8)
          for k in (2, 3, 4)]
t = pt.Trials(n_conditions=1, reps=10,                 # a catch condition
              order=pt.ORDER_CONSTRAINED,
              constraints=[pt.min_gap(pt.CONDITION, 0, 1)],
              tracks=stairs, track_rate=0.9, rng=seed, record_size=8)

while (ti := t.next()) is not None:
    if ti.track >= 0:
        s = stairs[ti.track]
        level = s.next()
        r = run_trial(level)
        s.update(level, r)
        t.update(r, struct.pack("<d", level))          # the level as the record
    else:
        t.update(run_catch_trial())                    # a zeroed record
```

A track is any object with a callable `is_done()`, or with a `done`
attribute (a property or a method). Give `(track, weight)` for a weighted
random interleave. The sequencer never calls anything else on a track; you
ask your own handle for the level. See [example.py](example.py).

## How to save and resume a session

```python
snap, state = t.save(), t.rng_state          # write both to disk
...
t = pt.Trials.load(snap, rng=state, **same_desc)
```

`load` needs the same desc as the saved session (every number is checked, and
a mismatch raises `Error` naming the first field that differs). It
re-supplies what a snapshot cannot carry: the tracks, the generator and
`record_size`. With an int seed, pass the `rng_state` you saved as `rng`.
With a callable generator, restore its state yourself.

Replay is the other route: open a fresh session with the same desc and seed,
then call `restore([h.outcome for h in old.history()], records)`. With
tracks, a replay needs each track to answer `is_done()` as it did in the
original run, so use a snapshot for those sessions (psy_trials.h, SAVING,
REPLAY AND RESUME).

## Reference

### `Trials(**desc)`

All arguments are keyword-only `psytr_desc` fields; a zero means the library
default.

| Argument | Meaning |
|---|---|
| `n_conditions` | plain rows; 0 with `factors` |
| `factors` | a list of `(name, n_levels)`; `name` may be None. Rows are the product, last factor fastest. |
| `reps`, `cond_reps` | repetitions of every row, or a list with one count per row (a weighted design) |
| `order` | `ORDER_SEQUENTIAL` (default), `ORDER_RANDOM`, `ORDER_FULL_RANDOM`, `ORDER_CONSTRAINED` |
| `constraints`, `max_swaps` | constraints from the helpers below; the repair budget (0 = 100000) |
| `tracks` | a list of tracks, or of `(track, weight)` |
| `interleave` | `INTERLEAVE_RANDOM` (default) or `INTERLEAVE_ROUND_ROBIN` |
| `track_rate` | probability of a track trial while a track runs; required with both conditions and tracks |
| `block_size`, `constraints_span_blocks` | main trials per block; whether runs count across blocks and breaks |
| `n_practice`, `n_warmup`, `warmup_conditions` | practice trials before the schedule; warmup trials after each break; the rows to draw them from |
| `requeue_gap` | a re-queued trial goes at least this many slots later; 0 = at the end |
| `rng` | a callable that returns a float in [0, 1), or an int seed for the header's splitmix64 |
| `record_size` | bytes of caller data per trial; the binding owns the buffer |

| Method or property | Returns |
|---|---|
| `next()` | a `TrialInfo` named tuple, or None when the session is done. Returns the pending trial again until `update()` or `requeue()`. |
| `update(outcome, rec=None)` | records the outcome (an int >= 0, or `INVALID`) and `record_size` bytes (None zero-fills) |
| `requeue()` | instead of `update()`: records `REQUEUE` and schedules the row again (scheduled trials only) |
| `mark_break()` | a break was taken; starts a constraint segment |
| `done` | True when nothing is pending or scheduled and every track is done |
| `level(c, f)`, `levels(c)`, `condition_from_levels(levels)` | the factorial table |
| `condition_at(slot)`, `schedule()`, `n_scheduled` | the schedule, for preloading stimuli |
| `n_valid(c)`, `count(c, outcome=1)`, `proportion(c, outcome=1)` | tallies over valid, non-practice, non-warmup trials |
| `history()` | a list of `Trial` named tuples: `index, condition, track, rep, block, outcome, flags`, and one bool per flag (`practice, warmup, requeued, after_break, first_in_block, violation, done`) |
| `record(i)` | trial `i`'s record as bytes |
| `format_header()`, `format_row(i)`, `format_meta()` | CSV lines and a `key=value` line, as str with the newline |
| `save()`, `Trials.load(data, **desc)`, `restore(outcomes, records=None)` | snapshot, resume, replay. `records` is one bytes object or a list of records. |
| `rng_state` | the splitmix state for an int seed (read and write); None for a callable |
| `n_conditions`, `n_factors`, `n_run`, `n_done`, `swaps`, `is_open`, `record_size` | counts and state |

`TrialInfo` has `index, condition, track, rep, block, first_in_block,
after_break, practice, warmup, requeued`. `condition` is -1 on a track trial
and `track` is -1 on a condition trial.

### Module functions and constants

| Function | Constraint |
|---|---|
| `max_run(factor, level, n)` | no more than `n` consecutive trials with that level |
| `max_in_window(factor, level, window, n)` | at most `n` in any `window` consecutive trials |
| `min_gap(factor, level, gap)` | at least `gap` other trials between two |
| `no_transition(factor, from_level, to_level)` | `from_level` never directly followed by `to_level` |
| `first_not(factor, level)` | the first main trial does not have that level |

`splitmix(state)` returns `(u, new_state)`: one step of the header's
generator. `strerror(code)` names an `ERR_*` code. Enums (standard-library
`IntEnum`): `Order`, `Interleave`, `Rule`, also as `ORDER_*`, `INTERLEAVE_*`,
`RULE_*`. Constants: `INVALID` (-1), `REQUEUE` (-2), `CONDITION`,
`ANY_LEVEL`, `FLAG_*`, `ERR_*`, `MAX_TRIALS` (4096), `MAX_CONDITIONS`,
`MAX_FACTORS`, `MAX_CONSTRAINTS`, `MAX_TRACKS`.

`__version__` is `psytr_version()` from the compiled header, and the tests
check that `pyproject.toml` carries the same string.

### Errors

| Exception | Raised when |
|---|---|
| `Error` | base class; also a constraint set the repair cannot meet, a failed `load`, or a Trials already in a call |
| `ArgumentError` | an invalid desc, an index out of range, an outcome below `INVALID`, a re-queue of a practice, warmup or track trial. Also a `ValueError`. |
| `Closed` | the session was never opened |
| `OutOfOrder` | `update()` or `requeue()` with no pending trial; `restore()` after trials ran |
| `Full` | `MAX_TRIALS` trials are in the history |

## Explanation: callbacks, the GIL and randomness

A track's `is_done` and a callable `rng` are Python, and the header calls
them from `next()`, `done`, `restore()`, and (the generator) the constructor
and `requeue()`. Those calls keep the GIL. The constructor and `load()`
release it when the generator is the binding's splitmix, because
construction calls no track and the constraint repair of a tight design can
take tens of milliseconds. An exception in a callback is raised from the call
that ran it. The callbacks after it in that call get a neutral answer (not
done; u = 0). If the call was `next()`, the trial it chose stays pending, and
the next `next()` returns it. A callback that calls back into the same
`Trials` gets `Error`.

The header owns no generator and seeds nothing. An int `rng` is the initial
splitmix64 state, which the binding holds for you. Two sessions with the same
desc and seed get the same order, and every draw happens in the order that
the manual's RANDOMNESS section fixes. That is why `restore` and `load` can
reproduce a run bit for bit.

## Verification

[tests/compare/compare_trials_psychopy.py](../../../tests/compare/compare_trials_psychopy.py)
checks the orders against `psychopy.data.TrialHandler` and
`TrialHandlerExt` over 100 seeds. The two use different generators, so the
random orders are compared by the properties each promises. Sequential
orders are identical. Under `random`, each repetition is one block that
holds every condition once, on both sides. Under `fullRandom`, weighted or
not, the counts per condition are exact on both sides. The weighted
sequential orders differ by design: PsychoPy runs a row's copies back to
back, and this header cycles the rows. The script reports that difference
and does not fail on it.
