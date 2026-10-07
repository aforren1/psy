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
| [psy_trials.h](psy_trials.h) | Trial sequencing above the adaptive methods: conditions and repetitions (constant stimuli), sequential, random and constrained-random orders, interleaved adaptive tracks, blocks, practice and catch trials, re-queues, tallies, a replayable history. From v0.2: conditions files and trial lists as tables, order lists, draws with replacement, subsets, blocked and alternating groups in Latin-square order, units ("b always follows a"), exact first-order transition balance, Latin and Williams squares by participant, and a rules text with one statement per C call. No heap, no I/O. | any | psy_table.h | v0.2.0. The hashes of 600 v0.1.1 sessions match on gcc, MSVC and Linux; each v0.2 order checked by chi-square against its stated distribution, units and balance uniform over every valid order of small designs; 9.7 million fuzz inputs and 29 of 29 mutations caught; gcc and MSVC, sanitizer clean. Sequential order identical to PsychoPy's TrialHandler and the random orders' block properties matched over 100 seeds ([tests/compare/](tests/compare/)). open() of a 10,000-trial design 0.02 to 7 ms. [Design notes](docs/psy_trials.md). |
| [psy_table.h](psy_table.h) | Typed tables as one position-independent, hashed block: an RFC 4180 CSV parser with hard bounds (32767 rows, 64 columns, 4096-byte fields), errors that name the line, row and column, int32, number and string columns with levels, and a correctly rounded number syntax no locale can change. The pack stores the same block, and a view checks it in place. No heap; file I/O only with `PSYTB_STDIO`. | any | nothing | v0.1.0. Numbers equal to strtod bit for bit on 10 million strings per compiler; every single-bit flip of a block refused; 5.9 million fuzz inputs; 24 of 25 mutations caught, the other equivalent. Parse 97 to 218 MB/s. [Design notes](docs/psy_table.md). |
| [psy_stair.h](psy_stair.h) | Adaptive staircases: transformed and weighted up/down, accelerated stochastic approximation. No heap, no threads. | any | nothing | v0.1.2. Hand-derived tracks and a simulated observer in [tests/adapt/](tests/adapt/); gcc and MSVC, sanitizer clean; replays PsychoPy's StairHandler and Palamedes' PAL_AMUD trial for trial ([tests/compare/](tests/compare/)). [Design notes](docs/psy_adapt.md), [comparison of the methods](docs/adapt_comparison.md). |
| [psy_quest.h](psy_quest.h) | QUEST+ on a grid, with QUEST, Psi and Psi-marginal as configurations; built-in psychometric functions, per-cell and batch callbacks for custom models (quick CSF); optional async layer on psy_rt.h. | any | nothing; psy_rt.h for the async layer | v0.5.2. Snapshots the size of the posterior, byte-identical between gcc and MSVC; 14770 checks against an independent reference; a Psi-marginal selection in about 1 ms; gcc and MSVC, sanitizer and ThreadSanitizer clean; identical selections to questplus on 180 of 180 trials, to Watson's own QUEST+ notebook on all 17 of its saved runs, to mQUESTPlus on 424 of 424 (ties included) and to Palamedes' PAL_AMPM ([tests/compare/](tests/compare/)). [Design notes](docs/psy_adapt.md), [comparison of the methods](docs/adapt_comparison.md). |
| [psy_gp.h](psy_gp.h) | Gaussian-process adaptive estimation (the AEPsych feature set and Keeley et al.'s psychometric model): Laplace GP with binary, ordinal, categorical, pairwise or continuous outcomes, RBF and semiparametric kernels, a two-latent threshold-and-slope model, integer and categorical dimensions, look-ahead level-set and global acquisitions on a candidate set, priors on the hyperparameters; optional async layer on psy_rt.h. | any | nothing; psy_rt.h for the async layer | v0.14.0. Two-latent psychometric model, optimization and pairwise acquisitions, mixed parameters, monotonic projection, conjugate-gradient updates past 512 trials and, opt-in, in the fits; runtime priors; snapshots. Numerics checked against dense references; the Owen et al. 2021 audiometric benchmark reproduced by [gp_audiometric](examples/gp_audiometric.c) and run beside AEPsych on one response stream, where it matches or beats AEPsych's threshold error with EAVC and BALV and halves its field error ([tests/compare/](tests/compare/)); gcc and MSVC, sanitizer clean. [Design notes](docs/psy_adapt.md), [comparison of the methods](docs/adapt_comparison.md). |
| [psy_timeline.h](psy_timeline.h) | Events and tracks on the clock: onsets, offsets, value changes, triggers and annotations on a trial, movie or RT time base; float channels driven by keyframes with easing (Bezier included), by sampled trajectories (a 10-minute sum-of-sines target in 288 KB), by repeating tracks (flicker) or by tweens; and one call per frame that takes the frame's predicted onset, fills the channel values and reports which events landed on the frame and their residuals. Rewind, late events, a seek that skips what it passes over instead of firing it late, a pure look-ahead of the events due in an RT window (for sound), exact rational base rates (slow motion for a movie base), the frame window as a query, a record of where every event landed. A sequence builder that reads like a GSAP timeline, and the same trial as a table of ops, checked all or none. No heap, no I/O, no clock. | any | nothing | v0.4.0. A test written from the manual alone (skip, peek, rates and sequences by the implementer), with its own model and 32 random model runs; rates against an exact 128-bit reference, sequences against hand-built tracks over 600 random tables; 21 of 24 rate and 19 of 19 sequence mutations of the header caught (3 cannot change a result); gcc, MinGW, MSVC, sanitizer clean; evaluate under 1.6 us p99 on a two-hour movie with 10,000 annotations. [Design notes](docs/psy_timeline.md). |
| [psy_color.h](psy_color.h) | Color on a calibrated display: the display calibration (photometer readings and spectra in, a lookup table and the matrices into CIE 1931 XYZ and Stockman-Sharpe 2 or 10 degree cone space out, the canonical .psycal file), a conversion context, values tagged with their space, and conversions both ways between linear device RGB, device codes, XYZ, xyY, CIELAB, CIELUV, Oklab, OkLCh, sRGB, Display P3, Rec. 2020, LMS, cone contrast, DKL (spherical and Cartesian) and MacLeod-Boynton. A conversion the calibration cannot support is refused with the reason; a color outside the gamut comes back unclipped with its distance. Gamut questions (the largest contrast along a direction, the boundary in a plane), gamut mapping by a stated method, and a participant's luminance from flicker photometry for the DKL isoluminant plane. No heap, no I/O. | any | nothing | v0.1.0. The calibration is psy_gfx.h v0.4's, bit for bit (52 calibrations, 2100 raw calls, PAINT's and VIDEO's matrices); conversions within 1e-12 of colour-science 0.4.7 (OkLCh hue of near-grays 3.3e-10); round trips through every space within 1.4e-13; Psychtoolbox's ComputeDKL_M within 6.2e-16, LMSToMacBoyn 1.2e-7, SensorToPrimary 1.5e-8 with the same out-of-gamut flags, MaximizeGamutContrast 7.3e-16 (MATLAB R2023a); 32 of 32 mutations caught; MSVC, MinGW, gcc with ASan and UBSan. A DKL direction 22 to 27 ns a call (MSVC), an OkLCh gamut ring of 360 hues 0.8 to 1.0 ms. No number is a measurement of light. [Design notes](docs/psy_color.md). |
| [psy_rdk.h](psy_rdk.h) | Random dot kinematograms: coherence, direction, speed, density, dot lifetime, the noise rule (random position, random walk, random direction: Scase, Braddick and Raymond 1996) and the signal rule (same, different, least recent), the presets of Pilly and Seitz 2009 (white noise, Movshon/Newsome with 3 interleaved sets, limited lifetime, Brownian); circular or rectangular apertures with a wrap that keeps the density uniform (a dot moves modulo its own chord); motion by the predicted onset of each frame or by a fixed step per frame; transparent motion as two fields. The state is integers and every draw is Philox4x32-10 of (seed, stream, dot, update, slot), so a trial regenerates bit for bit on any platform and the noise is frozen across coherence levels. Output into psy_gfx.h's dot buffers or straight into its instance records (gabor arrays). No I/O; one allocation at open, or none. | any | nothing | v0.1.0. Statistical tests: the realized coherence from the dots' displacements, density by chi-square in equal-area cells and along the edge (36 configurations), lifetimes, displacement against speed x elapsed time with dropped frames, the noise rules' distributions, frozen noise; 8 golden digests reproduced by an independent Python model and by MSVC, MinGW, gcc (-Ofast too, ASan, UBSan); 26 of 26 non-equivalent mutations caught. 10,000 dots: 102 to 443 us of CPU a frame by rule; with psy_gfx.h 0.3 ms of CPU and 0.32 to 0.38 ms of GPU. The Python binding `psy.rdk` replays a C-logged trial into equal NumPy arrays bit for bit. [Design notes](docs/psy_rdk.md). |
| [psy_outline.h](psy_outline.h) | The outline builder, the CPU side of text and artwork: paths of quadratic Bezier curves from C calls (move, line, quadratic, cubic, SVG arcs, rectangles, ellipses, transforms, nonzero and even-odd), glyph outlines from a bounds-checked TrueType and CFF (CID-keyed included) reader for fonts from untrusted packs, an SVG subset reader (paths, basic shapes, transforms, solid fills and strokes; everything else refused by name), overlap removal (winding 0 or 1, every vertex shared), strokes expanded to fills with Nehab's evolute elements so curves tighter than the half width do not fold (round, miter, bevel; butt, round, square; faux bold and inset), exact box-filter alpha per pixel in double, and curve sets for psy_gfx.h's Slug runs (format v1, the RESOLVED flag). No heap after warm-up, or a fixed arena; no I/O. | any | nothing | v0.2.1. TrueType glyphs placed as HarfBuzz places them: every glyph of Arial and Times New Roman equal to HarfBuzz 14.6.0's outline, and the curve sets equal word for word. Curve sets equal the format's test vector and pass psy_gfx.h's own check and winding; resolve within 2e-13 and exact alpha within 1.8e-13 of an adaptive Gauss-Kronrod reference; strokes 0 wrong of 1254027 pixel centers on 400 random curves (half bands alone: 186 wrong on 30); 30000 mutated fonts and 20000 mutated SVG files without a crash, also under ASan and UBSan; every glyph of seven Windows fonts builds; 64 of 67 mutants killed, 3 equivalent. A whole Microsoft YaHei into a set 1.7 to 1.9 s; exact alpha for 3000 CJK glyphs at 32 px 0.37 to 0.40 s; a CJK stroke 0.73 to 0.80 ms. Missed: a Latin stroke 289 to 303 us against a 200 us bar, and a CFF CJK set 94 to 103 us a glyph against 66. [Design notes](docs/psy_outline.md). |
| [psy_screen.h](psy_screen.h) | The stimulus display: window, GL ES 3.0 context, display modes and the swap path, with flip at a time. Each frame gets the predicted onset of its flip for the timeline, presents for a target time on the psy_rt.h clock, and gets a record of the vblank it was shown on, the drops, the composition path and where its time went, in psy_rt.h's event ring. Also the mode whose refresh is a multiple of a video's rate, a photodiode patch, input restamping and several displays. | Windows 10 and 11 (DXGI flip through ANGLE), Windows 11 x64 (the composition swapchain, present at a target time); Linux and macOS: the simulated display only | psy_rt.h, SDL3; ANGLE at run time on Windows | v0.3.1: Shift+Esc (not Esc alone), a close request, Alt+F4 or a request from any thread makes begin() report an abort once; the caller decides how to stop. An opt-in panic watchdog: Shift+Esc 3 times while the frame loop reports nothing puts back the gamma ramp and ends a hung program (28 to 85 ms after the third press here). A window icon (Escher's impossible cube). A D3D11 device with video support and multithread protection for psy_video.h, at no measured cost per frame. v0.3.0. Flip hooks: exact device codes in the frame (VPixx Pixel Mode and pixel sync presets, self-tested and read back), triggers at the planned onset on a deadline worker that move when a flip will be late, and an after-flip callback. Software timestamps only, on one Windows 11 laptop at 60 Hz (Iris Xe, Electron's ANGLE 2.1.23876). Fullscreen on a quiet machine, 1 minute each: onset within 2.33 us of the prediction at p99 on DXGI flip and 2.36 us on the composition swapchain, no drop, every frame on the first vblank after the present call (15.96 and 15.63 ms p50 to scanout start). In a window kept on top, the composition swapchain stayed on independent flip while DXGI flip moved between the overlay and the composed path: 2.1 to 2.5 us p99 against 4.5 to 5.4 us, and 0 to 24 drops per minute against 27 to 67 (runs made before the v0.2 depth rule and early-flip fix). Every injected overrun flagged. Each flip carries a timing tier; a composed onset on the composition swapchain is the compositor's plan (tier 3). AUTO stays DXGI flip. No photodiode has run the loopback test. A core test with a scripted swap path on a virtual clock (no SDL, no display) on MSVC, MinGW, gcc and clang, sanitizer clean, 41 mutations of the header caught. [Design notes](docs/psy_screen.md). |
| [psy_audio.h](psy_audio.h) | Sound at a time on the psy_rt.h clock: `psyau_play_at(buf, t)` plans the first sample on the device frame t falls on, through a fit of the device clock against the psy_rt clock, and records where it landed (stream frame, its time from that fit, residual, tier) in the handle and in the event ring. The time is the fit's, confirmed by position reports, never observed: tier 2 at best. A strict open that refuses any rate, channel or format conversion; cancel, stop and gain at a time; loops, routing, clipping flags; tones, noise and clicks at the project rate; the OS's latency claim, kept out of every onset; the Audio device and Audio source extension interfaces. Never resamples, never inserts frames. | Windows (WASAPI shared; exclusive opens but did not work here); CoreAudio, ALSA, PulseAudio and Web Audio compiled through miniaudio, never run | psy_rt.h, miniaudio 0.11.25 | v0.1.0. Software timestamps only, on one Windows 11 laptop, Realtek speakers, WASAPI shared at 48 kHz: 0 underruns in 10 minutes idle and 3 minutes under load; every burst at its planned frame in WASAPI's own loopback capture (700 of 700 idle, 500 of 500 under load); onset time against the capture 67 us p99 from its median idle (one frame, 20.8 us, was the bar: missed) and 20 us under load in one run, so tier 2 with those conditions; 18.7 us per callback with 32 voices; no heap call after open. No loopback cable: the delay to sound is unmeasured. A core test with a scripted device on a virtual clock on MSVC, MinGW, gcc and emcc, sanitizer and ThreadSanitizer clean, fifteen mutations caught. [Design notes](docs/psy_audio.md). |
| [psy_gfx.h](psy_gfx.h) | Stimuli on GL ES 3.0 over psy_screen.h: signed-distance shapes with a stated edge profile (hard, raised cosine, Gaussian), gratings, gabors, dot fields, image textures and sprites with tint, and hash noise, drawn in linear light into a float scene, then one output stage (a lookup table per channel, ordered or noise dithering, 8-bit codes). Strokes on every shape (inside, centered or outside; round or mitered joins), shapes from signed-distance textures, flat groups and render targets. Vector shapes from closed-form distance functions: rounded rects, arcs, pies, capsules, regular polygons, stars, ellipses, Bezier and polyline paths, compounds of up to 56 primitives with smooth merges, drop shadows, glows and outlines in one pass, dashes and trim, gradients in RGB, Oklab or DKL, add and multiply blends, MSDF atlases and instanced glyph runs; text and artwork from Slug curve sets (exact area per pixel on resolved glyphs), a separable Gaussian blur pass for glow, soft shadows and blurred letters; MIT notices for the SDF functions (Inigo Quilez, 0xfaded). Screen coordinates from the top-left corner, a place and an anchor per stimulus, hit tests by the shader's own SDF. The calibration, its file and the color math are psy_color.h's (v0.5); `psygfx_color()` gives the context PAINT uses; a user shader contract; timeline bindings; a 19-call backend interface. | Windows 11 through psy_screen.h (DXGI flip, composition swapchain); headless on any GL ES 3.0 context (tested: ANGLE, Mesa); Linux and macOS: the null backend on the simulated display | psy_screen.h, psy_color.h (SDL3; ANGLE at run time on Windows) | v0.10.0: gradient noise (3D simplex fBm: scale, octaves, lacunarity, gain, z to evolve) in integers, bit for bit equal to its CPU twin on four renderers; not band-limited, as the manual states; 1.8 ms full screen for one octave. v0.9.0: new noise and dither patterns: the hash is triple32 and a point's key one hash of its index under seed keys; v0.8's nested hash gave 29 % of full-screen frames two rows that were copies (PractRand failed it at 256 MB; the new key passes 16 GB). v0.8.0: setup passes render text, artwork or a blur layer into a target outside a frame (the same bits as in a frame); turned and aliased curve runs take the rays program by themselves (the same bits; a turned 12 px page 5.1 ms, was 5.9). v0.7.0: curve runs with `.rays` on their own program (a 12 px page 5.3 ms against 7.5), and a page cached in a target equal to the drawn run bit for bit (0.5 ms a frame); Gaussian NOISE from a quantile table, the same bits on the GPU and the CPU; three specialized vector programs (RRECT 0.95 to 0.57 ms, a dashed circle 0.93 to 0.51, a compound of 8 with effects 1.6 to 0.96); ELLIPSE packs 600 times faster; the default test runs in under a minute. v0.6.0: curve runs within 1.8e-5 of the exact pixel coverage on resolved glyphs (rays elsewhere: mean edge error 0.019 at 12 px per em), the same on three renderers to 6.5e-5 but one pixel; a 12 px page of 26205 glyphs 5.5 to 8.2 ms of GPU, a 40-glyph line 0.09 ms; the blur within 0.0087 of the exact blur at sigma 2 (2x: 0.0021), 0.18 ms for a word at sigma 2; NV12 video 1.15 times RGBA8 (was 1.64); 29 of 29 v0.6 mutations caught. v0.5.0: the calibration moved to psy_color.h with every readback of the test bit-identical on three renderers; OKLAB paint on absolute XYZ, the display's black included (5.2e-3 of full scale on a test display with 0.5 cd/m2 of black); `desc.cones`, `desc.lum`. v0.4.0: a program binary cache (open 11 to 35 ms warm, against 2 s or more), NV12 and I420 video converted to linear light (within 3.1e-7 of a double reference), texture import (GL; D3D11 RGBA and NV12 through ANGLE, shared handles included), instanced stimuli (10000 gabors in 0.18 ms of CPU and 1.3 ms of GPU) with a per-element hit test, and draws reordered where they do not overlap (bit-identical; 1000 interleaved stimuli 7.2 to 1.1 ms of GPU); 35 of 35 v0.4 mutations caught. v0.3.0: pixels checked offscreen against a CPU reference on four renderers (Iris Xe through ANGLE, WARP, SwiftShader, Mesa llvmpipe): largest error 1.7e-5 of full scale (1.4e-4 on SwiftShader), and exact for images, noise, hard edges and bands, sprite borders, targets against direct drawing, output codes and ordered dither; the v0.3 shapes within 4.9e-6 of the profile of the brute-force Euclidean distance, and the edge model's limits (corners, creases, smooth merges) measured against a true blur; 32 mutations in v0.1 and v0.2 and 25 of 26 in v0.3 caught (one equivalent). A stroke costs 1.05 to 1.17 times its fill; a full-screen target composite 0.43 ms of GPU. Calibration math within 3.9e-5 of IEC 61966-2-1's sRGB matrix and 7e-8 of Psychtoolbox's DKL matrix. On the Iris Xe at 1920 x 1200: an empty frame 0.42 to 1.34 ms of GPU, 1000 gabors 0.2 to 0.5 ms of CPU; no heap call per frame. No photometer: no number is a measurement of light. Open takes about 2 s (eleven programs). 10-bit, Bits#, stereo and text layout refused. [Design notes](docs/psy_gfx.md). |
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
                          headers, psy_trials.h, psy_table.h and psy_timeline.h, run by ctest
tests/compare/            side-by-side runs against PsychoPy, questplus, AEPsych,
                          mQUESTPlus and Palamedes; by hand, not CI
tests/fuzz/               libFuzzer targets for psy_table.h and psy_trials.h; by hand, not CI
tests/loopback/           opt-in hardware tests (-DPSY_BUILD_LOOPBACK=ON), run by hand
docs/psy_serial.md        design notes for psy_serial.h
docs/psy_adapt.md         design notes and verification for psy_stair.h, psy_quest.h, psy_gp.h
docs/adapt_comparison.md  the adaptive methods compared on published test problems
docs/psy_trials.md        design notes and verification for psy_trials.h
docs/psy_table.md         design notes, number-parser verification and costs for psy_table.h
docs/psy_timeline.md      design notes and measured costs for psy_timeline.h
docs/psy_screen.md        design notes and the swap-path measurements for psy_screen.h
docs/psy_audio.md         design notes and the clock and loopback measurements for psy_audio.h
docs/psy_gfx.md           design notes, pixel errors per renderer and frame costs for psy_gfx.h
docs/psy_color.md         design notes, references, Psychtoolbox comparison and costs for psy_color.h
docs/psy_rdk.md           design notes, statistics, mutations and costs for psy_rdk.h
docs/psy_outline.md       design notes, references, mutations and build costs for psy_outline.h
docs/psy_video.md         design notes, decode and upload costs for psy_video.h
docs/psy_rt.md            design notes and measured costs for psy_rt.h's event ring, correlation, macros
bindings/python/<name>/   one pip package per library
bindings/mex/             psy_parallel.c, psy_serial.c, psy_stair.c, psy_quest.c, psy_gp.c,
                          psy_trials.c, psy_color.c, psy_mex_util.h, build.m, test_mex.m,
                          test_mex_color.m, example_<name>.m
CMakeLists.txt            builds examples, compile checks and tests; registers libraries
```

## Conventions

- **Dependencies.** `psy_rt.h` is the one header the others use. It holds
  the clock, the deadline waits, the thread elevation, the deadline worker
  and the pump. A transport header includes it, so a user copies `psy_rt.h`
  and the transport header. The adaptive-method headers (`psy_stair.h`,
  `psy_quest.h`, `psy_gp.h`), `psy_trials.h`, `psy_table.h`, `psy_timeline.h`, `psy_color.h` and `psy_outline.h` are pure computation and
  include nothing but the C standard library. The exceptions are the opt-in
  async layer of `psy_quest.h` and `psy_gp.h` (`PSYQ_ASYNC`, `PSYGP_ASYNC`),
  which includes `psy_rt.h` for the pump, and `psy_trials.h`, which requires
  `psy_table.h` (v0.2) and compiles its implementation with its own. The rig headers build on each
  other: `psy_screen.h` on `psy_rt.h`, `psy_gfx.h` on `psy_screen.h` and
  `psy_color.h`, `psy_video.h` on `psy_gfx.h` and `psy_timeline.h`, and
  `psy_audio.h` on `psy_rt.h`. There are no other dependencies
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
  audio, `psygfx_`/`PSYGFX_` for stimulus drawing, `psycol_`/`PSYCOL_` for color, `psyol_`/`PSYOL_` for outlines. One letter while it
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
  never sees, and `psy.trials` taking any of the three as a track.
  `psy-color` gives `psy.color` (calibration, conversions, gamut, a
  participant's luminance). `tests/compare/`
  runs them beside PsychoPy, questplus and AEPsych on one response stream.
  CI builds the wheels with cibuildwheel on Linux, Windows and macOS, except
  `psy-parallel` on macOS, which its header does not support.
- **MATLAB / Octave**: [bindings/mex/](bindings/mex/), one MEX function per
  library (`psy_parallel`, `psy_serial`, `psy_stair`, `psy_quest`,
  `psy_gp`, `psy_trials`, `psy_color`) with ppdev-mex-style command dispatch, built and
  tested in MATLAB R2023a and Octave 10. `tests/compare/` runs `psy_quest`
  beside mQUESTPlus and Palamedes' PAL_AMPM, and `psy_stair` beside
  PAL_AMUD, on one response stream; the MEX README has the results.
  `tests/compare/psy_color_ptb.m` runs `psy_color` beside Psychtoolbox's
  colorimetric code (docs/psy_color.md).

## License

MIT-0 (public domain equivalent). See [LICENSE](LICENSE) and the end of each
header.
