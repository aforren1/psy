# psy

Single-header C libraries for psychophysics and neuroscience rigs, in the
style of [stb](https://github.com/nothings/stb) and
[sokol](https://github.com/floooh/sokol). A header needs nothing but the OS
and, for a transport, the shared `psy_rt.h`. Every header compiles as C99,
C11 or C++17, and in the C dialect MSVC compiles by default. Each header
carries its own documentation in its top comment.

## Libraries

| Header | Purpose | Platforms | Needs | Status |
|---|---|---|---|---|
| [psy_rt.h](psy_rt.h) | The shared base: monotonic clock, deadline waits, thread elevation, a one-shot deadline worker, a background pump for inference between trials, a wait-free event ring for the rig's log, clock correlation, and instrumentation macros (nothing, Tracy, or the ring). The transport headers and the async layers of the adaptive headers use it. | Windows, Linux, macOS, WebAssembly (Emscripten, no scheduling ladder) | OS only; Tracy, opt-in | v0.5.0. Event ring stress-tested with 2 to 8 producers (no torn or lost record unaccounted for), ThreadSanitizer clean; a push costs 11 to 37 ns natively and a TIME_CRITICAL producer's p99.9 stays near 2 us while 7 others push, against 0.1 to 0.5 ms behind a lock; Tracy mode built against Tracy v0.14.1 ([design notes and tables](docs/psy_rt.md)). Pump built and run on Windows, Linux and under node (Emscripten); on MinGW the worker and pump threads inherit the x87 control word so a thread computes the same bits as the caller, ThreadSanitizer clean including a wait-versus-stop hammer; the deadline worker as before: Windows measured with [rt_jitter](examples/rt_jitter.c); Linux built and run under WSL2, worker clean under ThreadSanitizer; CI compiles the macOS backend, which has not run on a Mac. No test host grants CAP_SYS_NICE, so the SCHED_FIFO and Mach time-constraint rungs are unexercised. |
| [psy_parallel.h](psy_parallel.h) | Parallel port (LPT) trigger output and line input, blocking and async pulses. | Windows, Linux | psy_rt.h; InpOut driver on Windows | v0.3, on psy_rt.h. Linux tested; Windows backend compiled but not run on hardware. |
| [psy_serial.h](psy_serial.h) | Serial port (RS-232, USB-serial, USB-CDC) byte I/O for trigger and response boxes, blocking and async pulses. | Windows, Linux, macOS | psy_rt.h | v0.4, on psy_rt.h. Linux tested against a virtual port pair; the Windows and macOS backends compile in CI but have never run on a device. [Design notes](docs/psy_serial.md). |
| [psy_trials.h](psy_trials.h) | Trial sequencing above the adaptive methods: conditions and repetitions (constant stimuli), sequential, random and constrained-random orders, interleaved adaptive tracks, blocks, practice and catch trials, re-queues, tallies, a replayable history. No heap, no I/O. | any | nothing | v0.1.1. Order properties over hundreds of seeds, every constraint rule checked by an independent checker, save/load and restore bit for bit; gcc and MSVC, sanitizer clean. Sequential order identical to PsychoPy's TrialHandler and the random orders' block properties matched over 100 seeds ([tests/compare/](tests/compare/)). [Design notes](docs/psy_trials.md). |
| [psy_stair.h](psy_stair.h) | Adaptive staircases: transformed and weighted up/down, accelerated stochastic approximation. No heap, no threads. | any | nothing | v0.1.2. Hand-derived tracks and a simulated observer in [tests/adapt/](tests/adapt/); gcc and MSVC, sanitizer clean; replays PsychoPy's StairHandler and Palamedes' PAL_AMUD trial for trial ([tests/compare/](tests/compare/)). [Design notes](docs/psy_adapt.md), [comparison of the methods](docs/adapt_comparison.md). |
| [psy_quest.h](psy_quest.h) | QUEST+ on a grid, with QUEST, Psi and Psi-marginal as configurations; built-in psychometric functions, per-cell and batch callbacks for custom models (quick CSF); optional async layer on psy_rt.h. | any | nothing; psy_rt.h for the async layer | v0.5.2. Snapshots the size of the posterior, byte-identical between gcc and MSVC; 14770 checks against an independent reference; a Psi-marginal selection in about 1 ms; gcc and MSVC, sanitizer and ThreadSanitizer clean; identical selections to questplus on 180 of 180 trials, to Watson's own QUEST+ notebook on all 17 of its saved runs, to mQUESTPlus on 424 of 424 (ties included) and to Palamedes' PAL_AMPM ([tests/compare/](tests/compare/)). [Design notes](docs/psy_adapt.md), [comparison of the methods](docs/adapt_comparison.md). |
| [psy_gp.h](psy_gp.h) | Gaussian-process adaptive estimation (the AEPsych feature set and Keeley et al.'s psychometric model): Laplace GP with binary, ordinal, categorical, pairwise or continuous outcomes, RBF and semiparametric kernels, a two-latent threshold-and-slope model, integer and categorical dimensions, look-ahead level-set and global acquisitions on a candidate set, priors on the hyperparameters; optional async layer on psy_rt.h. | any | nothing; psy_rt.h for the async layer | v0.14.0. Two-latent psychometric model, optimization and pairwise acquisitions, mixed parameters, monotonic projection, conjugate-gradient updates past 512 trials and, opt-in, in the fits; runtime priors; snapshots. Numerics checked against dense references; the Owen et al. 2021 audiometric benchmark reproduced by [gp_audiometric](examples/gp_audiometric.c) and run beside AEPsych on one response stream, where it matches or beats AEPsych's threshold error with EAVC and BALV and halves its field error ([tests/compare/](tests/compare/)); gcc and MSVC, sanitizer clean. [Design notes](docs/psy_adapt.md), [comparison of the methods](docs/adapt_comparison.md). |
| [psy_timeline.h](psy_timeline.h) | Events and tracks on the clock: onsets, offsets, value changes, triggers and annotations on a trial, movie or RT time base; float channels driven by keyframes with easing (Bezier included), by sampled trajectories (a 10-minute sum-of-sines target in 288 KB), by repeating tracks (flicker) or by tweens; and one call per frame that takes the frame's predicted onset, fills the channel values and reports which events landed on the frame and their residuals. Rewind, late events, a seek that skips what it passes over instead of firing it late, a pure look-ahead of the events due in an RT window (for sound), a record of where every event landed. No heap, no I/O, no clock. | any | nothing | v0.3.0. A test written from the manual alone (skip and peek by the implementer), with its own model and a 20-seed random model check, 20 of 23 header mutations caught (3 cannot change a result); gcc, MinGW, MSVC and emcc, sanitizer clean; evaluate under 1.6 us p99 on a two-hour movie with 10,000 annotations. [Design notes](docs/psy_timeline.md). |
| [psy_screen.h](psy_screen.h) | The stimulus display: window, GL ES 3.0 context, display modes and the swap path, with flip at a time. Each frame gets the predicted onset of its flip for the timeline, presents for a target time on the psy_rt.h clock, and gets a record of the vblank it was shown on, the drops, the composition path and where its time went, in psy_rt.h's event ring. Also the mode whose refresh is a multiple of a video's rate, a photodiode patch, input restamping and several displays. | Windows 10 and 11 (DXGI flip through ANGLE), Windows 11 x64 (the composition swapchain, present at a target time); Linux and macOS: the simulated display only | psy_rt.h, SDL3; ANGLE at run time on Windows | v0.3.0. Flip hooks: exact device codes in the frame (VPixx Pixel Mode and pixel sync presets, self-tested and read back), triggers at the planned onset on a deadline worker that move when a flip will be late, and an after-flip callback. Software timestamps only, on one Windows 11 laptop at 60 Hz (Iris Xe, Electron's ANGLE 2.1.23876). Fullscreen on a quiet machine, 1 minute each: onset within 2.33 us of the prediction at p99 on DXGI flip and 2.36 us on the composition swapchain, no drop, every frame on the first vblank after the present call (15.96 and 15.63 ms p50 to scanout start). In a window kept on top, the composition swapchain stayed on independent flip while DXGI flip moved between the overlay and the composed path: 2.1 to 2.5 us p99 against 4.5 to 5.4 us, and 0 to 24 drops per minute against 27 to 67 (runs made before the v0.2 depth rule and early-flip fix). Every injected overrun flagged. Each flip carries a timing tier; a composed onset on the composition swapchain is the compositor's plan (tier 3). AUTO stays DXGI flip. No photodiode has run the loopback test. A core test with a scripted swap path on a virtual clock (no SDL, no display) on MSVC, MinGW, gcc and clang, sanitizer clean, 29 mutations of the header caught. [Design notes](docs/psy_screen.md). |
| [psy_audio.h](psy_audio.h) | Sound at a time on the psy_rt.h clock: `psyau_play_at(buf, t)` plans the first sample on the device frame t falls on, through a fit of the device clock against the psy_rt clock, and records where it landed (stream frame, its time from that fit, residual, tier) in the handle and in the event ring. The time is the fit's, confirmed by position reports, never observed: tier 2 at best. A strict open that refuses any rate, channel or format conversion; cancel, stop and gain at a time; loops, routing, clipping flags; tones, noise and clicks at the project rate; the OS's latency claim, kept out of every onset; the Audio device and Audio source extension interfaces. Never resamples, never inserts frames. | Windows (WASAPI shared; exclusive opens but did not work here); CoreAudio, ALSA, PulseAudio and Web Audio compiled through miniaudio, never run | psy_rt.h, miniaudio 0.11.25 | v0.1.0. Software timestamps only, on one Windows 11 laptop, Realtek speakers, WASAPI shared at 48 kHz: 0 underruns in 10 minutes idle and 3 minutes under load; every burst at its planned frame in WASAPI's own loopback capture (700 of 700 idle, 500 of 500 under load); onset time against the capture 67 us p99 from its median idle (one frame, 20.8 us, was the bar: missed) and 20 us under load in one run, so tier 2 with those conditions; 18.7 us per callback with 32 voices; no heap call after open. No loopback cable: the delay to sound is unmeasured. A core test with a scripted device on a virtual clock on MSVC, MinGW, gcc and emcc, sanitizer and ThreadSanitizer clean, fifteen mutations caught. [Design notes](docs/psy_audio.md). |
| [psy_gfx.h](psy_gfx.h) | Stimuli on GL ES 3.0 over psy_screen.h: signed-distance shapes with a stated edge profile (hard, raised cosine, Gaussian), gratings, gabors, dot fields, image textures and sprites with tint, and hash noise, drawn in linear light into a float scene, then one output stage (a lookup table per channel, ordered or noise dithering, 8-bit codes). Strokes on every shape (inside, centered or outside; round or mitered joins), shapes from signed-distance textures, flat groups and render targets. Vector shapes from closed-form distance functions: rounded rects, arcs, pies, capsules, regular polygons, stars, ellipses, Bezier and polyline paths, compounds of up to 56 primitives with smooth merges, drop shadows, glows and outlines in one pass, dashes and trim, gradients in RGB, Oklab or DKL, add and multiply blends, MSDF atlases and instanced glyph runs; MIT notices for the SDF functions (Inigo Quilez, 0xfaded). Screen coordinates from the top-left corner, a place and an anchor per stimulus, hit tests by the shader's own SDF. Calibration math (Stockman-Sharpe cone fundamentals, cone contrast, DKL) and a canonical calibration file; a user shader contract; timeline bindings; an 18-call backend interface. | Windows 11 through psy_screen.h (DXGI flip, composition swapchain); headless on any GL ES 3.0 context (tested: ANGLE, Mesa); Linux and macOS: the null backend on the simulated display | psy_screen.h (SDL3; ANGLE at run time on Windows) | v0.3.0. Pixels checked offscreen against a CPU reference on four renderers (Iris Xe through ANGLE, WARP, SwiftShader, Mesa llvmpipe): largest error 1.7e-5 of full scale (1.4e-4 on SwiftShader), and exact for images, noise, hard edges and bands, sprite borders, targets against direct drawing, output codes and ordered dither; the v0.3 shapes within 4.9e-6 of the profile of the brute-force Euclidean distance, and the edge model's limits (corners, creases, smooth merges) measured against a true blur; 32 mutations in v0.1 and v0.2 and 25 of 26 in v0.3 caught (one equivalent). A stroke costs 1.05 to 1.17 times its fill; a full-screen target composite 0.43 ms of GPU. Calibration math within 3.9e-5 of IEC 61966-2-1's sRGB matrix and 7e-8 of Psychtoolbox's DKL matrix. On the Iris Xe at 1920 x 1200: an empty frame 0.42 to 1.34 ms of GPU, 1000 gabors 0.2 to 0.5 ms of CPU; no heap call per frame. No photometer: no number is a measurement of light. Open takes about 2 s (eleven programs). 10-bit, Bits#, stereo and text layout refused. [Design notes](docs/psy_gfx.md). |
| [psy_video.h](psy_video.h) | A movie as a stimulus: on each display frame the frame due at the predicted onset by psy_timeline.h's lead rule, never by counting frames; a cadence that judders (3:2) is shown and named, slips against the display grid are found and named, and every display frame gets a record (frame, due time, flip onset, decision and reason). The movie clock is a psy_timeline base, so annotations fire on the frame that shows them; it can follow the audio fit. Decode-ahead on a psy_rt.h pump, upload into a psy_gfx.h IMAGE; seek, loop, pause, manual frame index. A frame sequence container (raw or QOI, value-exact, read and write), MPEG-1 through pl_mpeg with a hashed index, and the Video decoder extension interface. | Windows through psy_gfx.h; any platform on the simulated display and CPU decode | psy_gfx.h, psy_timeline.h; pl_mpeg (optional) | v0.1.0. A core test with a scripted decoder and display on a virtual clock (cadences, slips against an exact model, stalls, seeks, the timeline agreement under 20 us of noise); 20 mutations caught. In a 640 x 360 window on the Iris Xe: `psyvid_update()` with a 1920 x 1080 RGBA8 upload 0.95 ms mean, 1.8 ms p99; no heap call per frame. pl_mpeg 1080p 5.5 ms a frame plus 7 ms of I420 to RGBA8 on the decode thread. No Media Foundation, no GPU zero-copy, no capture yet. [Design notes](docs/psy_video.md). |

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
psy_rt.h                  the shared base: clock, waits, scheduling, worker, pump, event ring, trace macros
psy_<name>.h              one library per header, at the root; the header is the documentation
examples/<name>_*.c       runnable demos, one or more per library
tests/compile/            per-header compile checks (C11, C++17, no-threads, async)
tests/adapt/              self-checking tests for psy_rt.h's pump, the adaptive-method
                          headers, psy_trials.h and psy_timeline.h, run by ctest
tests/compare/            side-by-side runs against PsychoPy, questplus, AEPsych,
                          mQUESTPlus and Palamedes; by hand, not CI
tests/loopback/           opt-in hardware tests (-DPSY_BUILD_LOOPBACK=ON), run by hand
docs/psy_serial.md        design notes for psy_serial.h
docs/psy_adapt.md         design notes and verification for psy_stair.h, psy_quest.h, psy_gp.h
docs/adapt_comparison.md  the adaptive methods compared on published test problems
docs/psy_trials.md        design notes and verification for psy_trials.h
docs/psy_timeline.md      design notes and measured costs for psy_timeline.h
docs/psy_screen.md        design notes and the swap-path measurements for psy_screen.h
docs/psy_audio.md         design notes and the clock and loopback measurements for psy_audio.h
docs/psy_gfx.md           design notes, pixel errors per renderer and frame costs for psy_gfx.h
docs/psy_video.md         design notes, decode and upload costs for psy_video.h
docs/psy_rt.md            design notes and measured costs for psy_rt.h's event ring, correlation, macros
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
  `psy_quest.h`, `psy_gp.h`), `psy_trials.h` and `psy_timeline.h` are pure computation and
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
  Gaussian-process methods, `psytr_`/`PSYTR_` for trial sequencing, `psytl_`/`PSYTL_`
  for the timeline, `psyscr_`/`PSYSCR_` for the display, `psyau_`/`PSYAU_` for
  audio, `psygfx_`/`PSYGFX_` for stimulus drawing. One letter while it
  stays unique. The
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
