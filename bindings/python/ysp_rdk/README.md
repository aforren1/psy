# ysp.rdk Python binding

A CPython extension for the [ysp/rdk.h](../../../include/ysp/rdk.h) single-header
library: random dot kinematograms whose dots regenerate bit for bit from
a seed and the logged inputs of each update. Use it in an analysis to get
back the exact dots a participant saw, for reverse correlation, motion
energy or a check of the stimulus. The header's state is integers and its
random words are Philox4x32-10 of (seed, stream, dot, update, slot), so
the rig (C, MSVC) and the analysis (Python, any platform) compute the same
bits.

The extension uses the **Limited API / stable ABI**
(`Py_LIMITED_API = 0x03080000`), so one `ysp/rdk.abi3.so` works on
CPython 3.8 and later. It needs NumPy at run time: the outputs are NumPy
arrays (`numpy.frombuffer` over bytes that the C code fills, with no
Python work per dot).

The distribution is `ysp-rdk` and the module is `ysp.rdk`. `ysp` is a
[PEP 420](https://peps.python.org/pep-0420/) implicit namespace package (no
`__init__.py`), so `ysp-rdk` installs alone or next to the other `ysp-*`
distributions.

## How to install

From this directory:

```sh
uv pip install .                    # build and install an abi3 wheel
```

In a development tree, setup.py finds `ysp/rdk.h` in the repository's
`include/` directory.
An isolated build (an sdist or a cibuildwheel run) sees only this
directory. Copy the header into `include/ysp/` next to `setup.py` first:

```sh
mkdir -p include/ysp && cp ../../../include/ysp/rdk.h include/ysp/
uv build --sdist
```

## How to regenerate a logged trial

The rig logs what the header's manual lists (REPRODUCIBILITY): the
version, the desc, the seed, the time of `yrdk_start()`, and `rdk.last`
after each update (t, coherence, direction, speed). Open a field with the
same desc as keyword arguments, then replay:

```python
import ysp.rdk as rdk

f = rdk.Field(algorithm="MN", w=10, count=100, coherence=0.256, speed=5)
steps = [(t, c, d, s) for t, c, d, s in log]   # rdk.last of each update
xy, direction, signal = f.trajectory(seed, t0, steps)
# xy: (updates, n, 2) float32, units from the center, y down
# direction: (updates, n) float32, degrees, clockwise on the screen
# signal: (updates, n) uint8, 1 for a signal dot on that update
assert f.digest() == logged_digest              # the same trial
```

`trajectory()` runs the whole trial in one C loop. `replay(seed, t0,
steps)` does the same without the arrays. A step is any row of at least
four values: a tuple, a `Field.last` record (`Step`), or a row of a NumPy
structured array.

## How to run a field step by step

```python
f = rdk.Field(w=10, count=200, coherence=0.5, direction=0, speed=7.5)
f.start(seed, t0)                    # every dot placed from the seed
for t in onsets:                     # ns, the frames' predicted onsets
    f.coherence = 0.5                # read at each update, as float32
    f.update(t)
    xy = f.xy()                      # (n, 2) float32
    log.append(f.last)               # Step(t, coherence, direction, speed, set)
snap = f.snapshot()                  # bytes: resume later with restore()
```

`directions()`, `ages()`, `signals()` and `events()` give the other outputs
of the shown set. `digest()` is the header's FNV-1a 64 of the state.

## Reference

| Name | What it is |
|---|---|
| `Field(**desc)` | `yrdk_desc`'s fields as keyword arguments. An enum takes its name, in any case, or its value: `algorithm` "custom", "WN", "MN", "LL", "BM"; `aperture` "circle", "rect"; `signal` "same", "different", "least_recent"; `select` "exact", "bernoulli"; `noise` "direction", "position", "walk"; `edge` "wrap", "replot"; `clock` "onset", "frames". A float is rounded to float32, as a C float literal is. |
| `start(seed, t)` | `yrdk_start()`. |
| `update(t)` | `yrdk_update()`; returns the dots shown. |
| `replay(seed, t0, steps)` | `yrdk_replay()`. |
| `trajectory(seed, t0, steps)` | the replay, with the shown set's x, y, direction and signal after each step. |
| `xy()`, `directions()`, `ages()`, `signals()`, `events()` | `yrdk_write()` of the shown set, as NumPy arrays. |
| `digest()`, `snapshot()`, `restore(bytes)` | the header's calls of the same names. |
| `coherence`, `direction`, `speed` | the field's floats, read at each update. |
| `n`, `total`, `last`, `stats` | dots shown, dots in the field, the last update's inputs, counts since start. |
| `philox(seed, ctr)`, `angle(deg)`, `sincos(angle)` | the exact pieces of REPRODUCIBILITY. |
| `Error`, `ArgumentError`, `OrderError` | the header's codes; `ArgumentError` and `OrderError` are also `ValueError`. |

## Tests

```sh
uv build --wheel -o dist
uv run --no-project --with pytest --with numpy --with dist/ysp_rdk-0.1.0-cp38-abi3-<platform>.whl pytest tests
```

The tests check the 8 golden digests of `tests/adapt/rdk_test.c` and
`tests/compare/rdk_ref.py`, a trial logged by the C header
(`tests/c_trial.bin`, written by `tests/make_c_trial.c`) replayed into
equal arrays bit for bit, frozen noise across coherence levels,
snapshots, the outputs and the refusals. CI runs them on Linux, macOS
arm64 and Windows.
