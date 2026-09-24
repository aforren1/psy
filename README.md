# psy

Single-header C libraries for psychophysics and neuroscience rigs, in the
style of [stb](https://github.com/nothings/stb) and
[sokol](https://github.com/floooh/sokol). A header needs nothing but the OS
and, for a transport, the shared `psy_rt.h`. Every header compiles as C99,
C11 or C++17, and in the C dialect MSVC compiles by default. Each header
carries its own documentation in its top comment.

## Libraries

| Header | Purpose | Platforms | Status |
|---|---|---|---|
| [psy_rt.h](psy_rt.h) | The shared base: monotonic clock, deadline waits, thread elevation, a one-shot deadline worker, and a background pump for inference between trials. The transport headers and the async layers of the adaptive headers use it. | Windows, Linux, macOS, WebAssembly (Emscripten, no scheduling ladder) | v0.4.0. Pump built and run on Windows, Linux and under node (Emscripten), ThreadSanitizer clean including a wait-versus-stop hammer; the deadline worker as before: Windows measured with [rt_jitter](examples/rt_jitter.c); Linux built and run under WSL2, worker clean under ThreadSanitizer; CI compiles the macOS backend, which has not run on a Mac. No test host grants CAP_SYS_NICE, so the SCHED_DEADLINE, SCHED_FIFO and Mach time-constraint rungs are unexercised. |
| [psy_parallel.h](psy_parallel.h) | Parallel port (LPT) trigger output and line input, blocking and async pulses. | Windows, Linux | v0.3, on psy_rt.h. Linux tested; Windows backend compiled but not run on hardware. |
| [psy_serial.h](psy_serial.h) | Serial port (RS-232, USB-serial, USB-CDC) byte I/O for trigger and response boxes, blocking and async pulses. | Windows, Linux, macOS | v0.4, on psy_rt.h. Linux tested against a virtual port pair; the Windows and macOS backends compile in CI but have never run on a device. [Design notes](docs/psy_serial.md). |
| [psy_trials.h](psy_trials.h) | Trial sequencing above the adaptive methods: conditions and repetitions (constant stimuli), sequential, random and constrained-random orders, interleaved adaptive tracks, blocks, practice and catch trials, re-queues, tallies, a replayable history. No heap, no I/O. | any | v0.1.1. Order properties over hundreds of seeds, every constraint rule checked by an independent checker, save/load and restore bit for bit; gcc and MSVC, sanitizer clean. Sequential order identical to PsychoPy's TrialHandler and the random orders' block properties matched over 100 seeds ([tests/compare/](tests/compare/)). [Design notes](docs/psy_trials.md). |
| [psy_stair.h](psy_stair.h) | Adaptive staircases: transformed and weighted up/down, accelerated stochastic approximation. No heap, no threads. | any | v0.1.2. Hand-derived tracks and a simulated observer in [tests/adapt/](tests/adapt/); gcc and MSVC, sanitizer clean; replays PsychoPy's StairHandler and Palamedes' PAL_AMUD trial for trial ([tests/compare/](tests/compare/)). [Design notes](docs/psy_adapt.md), [comparison of the methods](docs/adapt_comparison.md). |
| [psy_quest.h](psy_quest.h) | QUEST+ on a grid, with QUEST, Psi and Psi-marginal as configurations; built-in psychometric functions, per-cell and batch callbacks for custom models (quick CSF); optional async layer on psy_rt.h. | any | v0.5.2. Snapshots the size of the posterior, byte-identical between gcc and MSVC; 14770 checks against an independent reference; a Psi-marginal selection in about 1 ms; gcc and MSVC, sanitizer and ThreadSanitizer clean; identical selections to questplus on 180 of 180 trials, to Watson's own QUEST+ notebook on all 17 of its saved runs, to mQUESTPlus on 424 of 424 (ties included) and to Palamedes' PAL_AMPM ([tests/compare/](tests/compare/)). [Design notes](docs/psy_adapt.md), [comparison of the methods](docs/adapt_comparison.md). |
| [psy_gp.h](psy_gp.h) | Gaussian-process adaptive estimation (the AEPsych feature set and Keeley et al.'s psychometric model): Laplace GP with binary, ordinal, categorical, pairwise or continuous outcomes, RBF and semiparametric kernels, a two-latent threshold-and-slope model, integer and categorical dimensions, look-ahead level-set and global acquisitions on a candidate set, priors on the hyperparameters; optional async layer on psy_rt.h. | any | v0.14.0. Two-latent psychometric model, optimization and pairwise acquisitions, mixed parameters, monotonic projection, conjugate-gradient updates past 512 trials and, opt-in, in the fits; runtime priors; snapshots. Numerics checked against dense references; the Owen et al. 2021 audiometric benchmark reproduced by [gp_audiometric](examples/gp_audiometric.c) and run beside AEPsych on one response stream, where it matches or beats AEPsych's threshold error with EAVC and BALV and halves its field error ([tests/compare/](tests/compare/)); gcc and MSVC, sanitizer clean. [Design notes](docs/psy_adapt.md), [comparison of the methods](docs/adapt_comparison.md). |

## Quick start

1. Copy `psy_rt.h` and the header you need into your project.
2. In exactly one `.c` or `.cpp` file, define the implementation macro before
   the include:

   ```c
   #define PSY_PARALLEL_IMPLEMENTATION
   #include "psy_parallel.h"
   ```

3. Include the header normally everywhere else.
4. Read the header's top comment. It is the manual: usage, platform setup,
   timing notes, build flags, and the API reference.

To build the examples and compile checks in this repository:

```sh
cmake -B build
cmake --build build
ctest --test-dir build
```

Or without CMake, from the repository root:

```sh
cc -O2 -pthread -I. -o parallel_trigger examples/parallel_trigger.c   # Linux
cl /O2 /I. examples\parallel_trigger.c                                # Windows (MSVC)
```

CMake consumers can `add_subdirectory(psy)` and link `psy::psy`. It adds the
include path and the libraries an implementation file needs: pthreads on
POSIX, `setupapi` on Windows, and `libm` on Linux.

## Layout

```
psy_rt.h                  the shared base: clock, waits, scheduling, deadline worker, pump
psy_<name>.h              one library per header, at the root; the header is the documentation
examples/<name>_*.c       runnable demos, one or more per library
tests/compile/            per-header compile checks (C11, C++17, no-threads, async)
tests/adapt/              self-checking tests for psy_rt.h's pump, the adaptive-method
                          headers and psy_trials.h, run by ctest
tests/compare/            side-by-side runs against PsychoPy, questplus, AEPsych,
                          mQUESTPlus and Palamedes; by hand, not CI
tests/loopback/           opt-in hardware tests (-DPSY_BUILD_LOOPBACK=ON), run by hand
docs/psy_serial.md        design notes for psy_serial.h
docs/psy_adapt.md         design notes and verification for psy_stair.h, psy_quest.h, psy_gp.h
docs/adapt_comparison.md  the adaptive methods compared on published test problems
docs/psy_trials.md        design notes and verification for psy_trials.h
bindings/python/<name>/   one pip package per library
bindings/mex/             psy_parallel.c, psy_serial.c, psy_stair.c, psy_quest.c, psy_gp.c,
                          psy_trials.c, psy_mex_util.h, build.m, test_mex.m, example_<name>.m
CMakeLists.txt            builds examples, compile checks and tests; registers libraries
```

## Conventions

- **Dependencies.** `psy_rt.h` is the one header the others use. It holds
  the clock, the deadline waits, the thread elevation, the deadline worker
  and the pump. A transport header includes it, so a user copies `psy_rt.h`
  and the transport header. The adaptive-method headers (`psy_stair.h`,
  `psy_quest.h`, `psy_gp.h`) and `psy_trials.h` are pure computation and
  include nothing but the C standard library. The exception is the opt-in
  async layer of `psy_quest.h` and `psy_gp.h` (`PSYQ_ASYNC`, `PSYGP_ASYNC`),
  which includes `psy_rt.h` for the pump. There are no other dependencies
  between headers. Two mechanics
  follow from this. First, the transport's implementation block also
  compiles the implementation of `psy_rt.h`, unless the same translation unit
  already defined `PSY_RT_IMPLEMENTATION` and included `psy_rt.h` before it.
  Either order gives exactly one copy of the implementation. Second,
  `PSY<X>_NO_THREADS` also sets `PSYRT_NO_THREADS`. If you include `psy_rt.h`
  first, it reads its own macro before the transport can set it, so define
  both macros or neither.
- **Names.** `psy_<name>.h` uses a short, unique function prefix `psy<x>_`
  and macro prefix `PSY<X>_`, registered in the table above: `psyp_`/`PSYP_`
  for parallel, `psys_`/`PSYS_` for serial, `psyrt_`/`PSYRT_` for the
  real-time timing primitives in `psy_rt.h`, `psyst_`/`PSYST_` for
  staircases, `psyq_`/`PSYQ_` for QUEST+, `psygp_`/`PSYGP_` for the
  Gaussian-process methods, `psytr_`/`PSYTR_` for trial sequencing. One letter while it stays
  unique; a later `psy_screen.h` picks something like `psyscr_`. The
  implementation macro is `PSY_<NAME>_IMPLEMENTATION`. Private symbols use a
  double underscore (`psyp__set_error`).
- **Handles.** The caller allocates the handle struct, zeroed or closed.
  `psy<x>_open(p, desc)` fills it. A zero field in `desc` means "default";
  set fields with designated initializers (`psys_desc d = { .device =
  "COM3" };`, C99 or C++20) so everything unset is zero. Some headers have a
  required field (`psys_desc.device`); the header says so.
- **Errors.** A function that cannot run while another thread uses the handle
  returns `bool` and leaves a message in the handle, cleared on entry;
  `psy<x>_error(p)` reads it. Every function a second thread may call returns
  an `int` count or a negative code and never touches the message buffer.
  Where the line falls is per header: `psy_parallel.h` puts all its setup
  calls in the first group, `psy_serial.h` only `psys_open`, because its
  shutdown recipe has a reader, a writer and a main thread on one handle.
- **Threads.** Headers that spawn a worker thread honor `PSY<X>_NO_THREADS`
  to drop it, and link `-pthread` on POSIX otherwise.
- **Platforms.** A header `#error`s on platforms it does not support, so an
  unsupported build fails at compile time, not at run time.
- **Documentation.** sokol-style. The header's top comment holds the manual
  in labeled sections (USAGE, platform setup, TIMING, BUILDING, ...), and
  every declaration carries its reference comment. `docs/` holds design notes
  and comparisons: why a header looks the way it does and how it measured
  against other toolboxes, never what the header already says. Code comments
  say why, not how.
- **Timing.** Anything that claims a latency number measures it. Each header
  says how.

## Add a library

1. Write `psy_<name>.h` with the top-comment manual, the API reference on
   each declaration, and the MIT-0 block, following the conventions above.
   Build on `psy_rt.h` for the clock, the waits, the thread scheduling and
   the deadline worker. Do not write those again.
2. Add `tests/compile/psy_<name>.c` and `.cpp` (copy an existing pair and
   change the name).
3. Register it in `CMakeLists.txt`: append to `PSY_LIBS`, set
   `PSY_PREFIX_<name>`, `PSY_PLATFORMS_<name>`, and `PSY_THREADS_<name>`,
   and `PSY_ASYNC_<name>` if it has an opt-in async layer. Compile checks,
   examples, a `tests/adapt/psy_<name>_test.c` and a
   `tests/loopback/psy_<name>_loopback.c` if there are ones are found by name
   from there.
4. Add at least one `examples/<name>_*.c`. It must exit with a documented
   code when no hardware is present; add that expectation to the "Run
   examples" step in `.github/workflows/ci.yml`.
5. Add a row to the table above.
6. Bindings are optional: `bindings/python/<name>/` (add it to the CI wheel
   matrix) and `bindings/mex/psy_<name>.c` (add a platform gate to
   `build.m` if the header does not support every OS).

CI builds every registered header on Windows, Linux, and macOS, so step 3 is
what puts a header under test.

## Bindings

- **Python**: one distribution per library under
  [bindings/python/](bindings/python/), each a dependency-free CPython
  extension on the Limited API (one abi3 wheel per platform for CPython
  3.8+). Each one compiles its header into the extension, plus `psy_rt.h`
  where the header uses it. They share the `psy` namespace (PEP 420, no `__init__.py`), so
  `pip install psy-parallel psy-serial` gives `import psy.parallel` and
  `import psy.serial`, and either installs alone. The adaptive headers have
  the same: `psy-stair`, `psy-quest`, `psy-gp` and `psy-trials` give
  `psy.stair`, `psy.quest`, `psy.gp` and `psy.trials`, with an `Async`
  class in quest and gp that runs inference on a C thread the interpreter
  never sees, and `psy.trials` taking any of the three as a track. `tests/compare/`
  runs them beside PsychoPy, questplus and AEPsych on one response stream.
  CI builds the wheels with cibuildwheel on Linux, Windows and macOS, except
  `psy-parallel` on macOS, which its header does not support.
- **MATLAB / Octave**: [bindings/mex/](bindings/mex/), one MEX function per
  library (`psy_parallel`, `psy_serial`, `psy_stair`, `psy_quest`,
  `psy_gp`, `psy_trials`) with ppdev-mex-style command dispatch, built and
  tested in MATLAB R2023a and Octave 10. `tests/compare/` runs `psy_quest`
  beside mQUESTPlus and Palamedes' PAL_AMPM, and `psy_stair` beside
  PAL_AMUD, on one response stream; the MEX README has the results.

## License

MIT-0 (public domain equivalent). See [LICENSE](LICENSE) and the end of each
header.
