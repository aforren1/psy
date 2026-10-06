# Rig specification: display, audio, video, pack, player, designer

Status: proposal of 2026-09-24, revised 2026-10-06. It records the
decisions made while planning the move from the transport and adaptive
headers into graphics, audio, video and the tooling above them. Steps 1 to
6 of section 11 have started; section 11 gives the state of each step.
Each header's design note in `docs/` records what was built and measured.
When a note and this document disagree, the note is newer.

The existing headers and their conventions in [README.md](../README.md)
stay as they are. This document adds to them.

## 1. Principles

These extend the conventions in the README. Every new header, tool and
format follows them.

1. **One clock.** Every timestamp in the system is on the `psy_rt.h`
   clock. A backend that reports time on another clock converts it at
   the boundary and records the correlation.
2. **Present at a time, not at an interval.** The display, audio and
   video APIs take a target time and report the actual time. The
   difference is data and goes in the log.
3. **A measurement or nothing.** A latency number in a header is a
   measurement from a loopback test, or a bound, or a pointer to the
   test. The STATUS block says which.
4. **One canonical format per resource.** The runtime accepts exactly
   one in-memory and one on-disk form of each resource type. It rejects
   everything else at open with a message that names the tool that
   produces the canonical form. There is no on-the-fly fallback.
5. **No implicit conversion.** No resampling at play, no resizing at
   upload, no color format coercion, no frame rate conversion. A
   mismatch is a refusal.
6. **Data over code.** Everything an experiment changes often is data in
   the pack: conditions, timelines, parameters, resources. Code is the
   player, or an eject path for the few who need it.
7. **One external dependency per device header, at most.** Pure
   computation headers depend on nothing. Device headers may depend on
   one library each, named in a "Needs" column in the README table.
8. **Introspectable descriptors.** Every parameter a designer can set is
   a plain value in a `desc` struct, and every new header exports a
   table that enumerates its parameters with type, range, unit and
   default.

## 2. Architecture

```
designer (web app, static, offline PWA)
    |  runner protocol: JSON over WebSocket, localhost, token
    v
player (prebuilt per platform, and wasm)   <--  pack (zip, store-only)
    |
    +-- psy_screen.h   window, display modes, flip at time      [SDL3, ANGLE]
    +-- psy_gfx.h      stimuli on GL ES 3.0, output stage       [psy_screen.h]
    +-- psy_audio.h    device, scheduled playback, synthesis    [miniaudio]
    +-- psy_video.h    decode-ahead, scheduler, capture         [psy_screen.h, SDL3]
    +-- psy_timeline.h events and tracks on the clock           [nothing]
    +-- psy_trials.h, psy_stair.h, psy_quest.h, psy_gp.h        [as today]
    +-- psy_rt.h       clock, waits, worker, pump, event ring   [OS]
    +-- psy_parallel.h, psy_serial.h                            [psy_rt.h]
```

The headers stay usable without the player, the pack or the designer.
The player is a program that uses them. The designer is a program that
produces packs and talks to the player.

## 3. Dependency decisions

| Component | Decision | Reason |
|---|---|---|
| SDL3 | Accepted, for windowing, input, display modes, camera capture | Windowing is the hairiest OS surface. Stable ABI, packaged everywhere, Emscripten support, exposes native handles so present-timing code can go under it. Camera API since 3.2 covers Media Foundation, AVFoundation, V4L2 and getUserMedia. |
| ANGLE | Accepted, for GL ES 3.0 on Windows and macOS | Three browser vendors maintain it. One shader language end to end. Same code path as WebGL2 in the browser. Reliable on Intel integrated GPUs where vendor GL is not. Exposes vblank timestamps on its D3D11 backend. Binaries, not a source build (decided 2026-10-06): CI takes ANGLE from an Electron release zip, checked by checksum; development machines use whatever Electron copy is present. A source build at a pinned commit (depot_tools, about 10 GB) waits until an ANGLE bug or a missing feature needs a patch. Each measurement names the ANGLE version it ran on, and program binaries are keyed per ANGLE build. |
| miniaudio | Accepted, for audio devices | Single file, public domain, WASAPI shared and exclusive, CoreAudio, ALSA, PulseAudio, Web Audio. Same implementation-macro mechanic as `psy_rt.h`. |
| Emscripten | Accepted, for the web player | Required by SDL3's web backend. `psy_rt.h` already has the branch. |
| zig cc | Accepted, for the eject path and custom modules | One directory, cross-compiles to Windows and Linux with a pinned glibc. Driven as a compiler, version pinned, shipped with the designer. |
| glslang, SPIRV-Cross | Accepted, in the tool only | Compile the user shader contract offline. Both have WebAssembly builds, so the designer runs them in the browser. Never in the runtime. |
| FFmpeg | Accepted, opt-in, as a video backend on Linux and as the tool's transcoder | The tool shells out to the command line. The Linux hardware-decode backend links the libraries, off by default. |
| pl_mpeg | Accepted, as the zero-install compressed video backend | Single header, MPEG-1 and MP2. |
| QOI | Accepted, for lossless frame and texture compression | Single header, fast, exact. |
| bgfx | Rejected | Hides the swap chain, runs a render thread, precompiled shaders per backend for every user, heavy build. |
| sokol_gfx | Rejected | Good design, single author. The ES 3.0 plan needs no second graphics abstraction. Reference for the backend interface shape. |
| SDL_GPU | Rejected for now | No GL, no browser backend shipped, SDL owns the swap chain. Revisit if ANGLE fails on a platform. |
| WebGPU via Dawn | Deferred | The consistent choice if compute becomes a must. Larger binary, WGSL, no present timing. |
| GStreamer | Rejected | Hundreds of megabytes, runtime plugin discovery, platform install failures. |
| Magnum | Rejected | C++ engine, single author, GL-centric, nothing ES 3.0 does not do for this surface. |
| Dear ImGui | Accepted for the operator window in examples, C++ only. Rejected for stimuli | UI batcher, packed 8-bit color, no gamma or bit depth control. |
| alwan | Not a dependency | Color science library, explicitly no device characterization. The rig needs measured primaries, not standard ones. |
| Basis Universal | Opt-in only | Lossy in exactly the values a calibrated stimulus is made of. |
| Luau | Proposed, in the player only, for scripted logic | Sandboxed by default, fast interpreter, incremental collector, a type checker and language server that build to WebAssembly for the designer's editor. C++, so never in a header. Plain Lua 5.4 is the fallback if C matters more than tooling. |
| QuickJS | Rejected for scripting | Familiar language, but larger, a less predictable collector, and the web player would have to choose between QuickJS in WebAssembly for parity and the browser engine for speed. |
| nanovg | Rejected | Vector UI drawing with a fixed anti-aliasing fringe and its own font atlas. A stimulus edge is a parameter; see the signed-distance shapes in `psy_gfx.h`. Unmaintained since 2019. |
| wasm3 | Deferred | Hosts modules from any language in the native player if someone needs Rust or C++ logic. |
| minicoro, tina | Deferred | Native stackful coroutines. Luau's VM-level coroutines cover the scripting model with no native stack switch, so they run in the web player and under the sanitizers. Only the eject path could want native coroutines for sequential C. If that demand appears, minicoro, player only, never in a header, and not in the web player. |
| Tracy | Accepted, opt-in, through a macro layer in `psy_rt.h` | The headers see only C macros that compile to nothing by default. Tracy's C++ client is compiled into the player or the user's program. On-demand mode in a deployed player. |

## 4. Headers

### 4.1 psy_rt.h additions

- **Event ring.** A wait-free fixed-record ring that any thread pushes
  into without a lock or an allocation, drained by a normal thread. The
  record is a timestamp, a source, a kind and a small payload. The
  screen, audio and video headers push their flip, onset and frame
  records into a ring the caller supplies. Export format is the
  caller's business; examples show a CSV drain.
- **Clock correlation.** A call that correlates the `psy_rt.h` clock
  with another clock read back to back, for SDL's tick clock, the
  Windows interrupt-time clock and audio device positions.
- **Instrumentation macros.** Zone, frame mark, plot and message
  macros. By default they compile to nothing. With `PSYRT_TRACY` they
  map to Tracy's C API. With `PSYRT_TRACE_RING` they write zones to
  the event ring, so a rig without Tracy still records them. Every
  header instruments its hot paths through these macros: the frame
  loop phases, the pump callbacks, the audio callback, the decode-ahead,
  the serial reader. Frame marks go at the flip. Plots carry the flip
  residual, the audio fill level and the decode-ahead depth. Messages
  mirror trial and event boundaries. A message with the `psy_rt.h` time
  at each frame mark correlates Tracy's clock with the rig's. A thread
  that will run a real-time callback emits one zone at open to
  allocate its queue before it matters.

### 4.2 psy_screen.h (`psyscr_`, `PSYSCR_`)

Needs SDL3. Owns the window, the GL ES 3.0 context, the display mode,
the swap path and the input restamping.

- **Context.** EGL through ANGLE on Windows and macOS, the native driver
  on Linux, WebGL2 under Emscripten. Confirm SDL3's EGL path on Cocoa
  with ANGLE's Metal backend before the first macOS build. On Windows,
  SDL3 owns only the window: the header makes the D3D11 device and the
  DXGI swapchain and gives ANGLE a client-buffer pbuffer on the back
  buffer, because SDL's EGL path gets ANGLE's blt-model window surface,
  which has no vblank statistics and no frame latency control
  ([psy_screen.md](psy_screen.md) has the probe numbers).
- **Display modes.** Enumerate the chosen display's modes. Offer "the
  mode whose refresh is an integer multiple of this rate" for video and
  report what was obtained.
- **Flip at a time.** `psyscr_flip_at(t)` presents the drawn frame at
  target time `t` on the `psy_rt.h` clock. It does not wait for the flip:
  the record of the flip, with the estimated onset, the residual and a
  dropped-frame count, arrives with the next frame's begin call and in the
  event ring, and `psyscr_wait_flip()` blocks for it as Psychtoolbox's
  Flip does. The capability query reports one of three kinds:
  - fixed grid, with the period, where `t` snaps to the nearest vblank by
    the timeline's rule (the first vblank at or after `t` minus half a
    period), so a target with a little prediction noise does not go a
    frame late; a never-early lead is an option;
  - continuous, with a minimum and maximum interval, for variable
    refresh;
  - callback-driven, with the grid the compositor gives, for the
    browser and macOS display links.
- **Queue depth.** At most one frame in flight. Where the platform gives
  no control, the header issues a trivial draw and waits on a fence
  after the swap.
- **Swap path backends**, behind one interface:
  - Windows 11: the composition swapchain API, with present-at-time,
    displayed time and duration, and skipped-present statistics. ANGLE
    renders into the D3D11 presentation buffer through a client-buffer
    surface.
  - Windows 10: DXGI flip model, the swapchain the header's own, ANGLE
    rendering into its back buffer through a client-buffer surface, vblank
    timestamps from DXGI's frame statistics. ANGLE's sync-control extension
    failed on every frame of its own window surface (blt model).
  - Linux X11: GLX sync control on the native driver.
  - Linux Wayland: presentation-time through SDL's native handles.
  - macOS: whatever ANGLE's Metal backend exposes, measured before it
    is claimed.
- **Variable refresh.** A capability, never a default. Enabled only
  after a photodiode interval sweep and a luminance-versus-interval
  sweep on that panel. The header re-presents on purpose to stay inside
  the panel's range and logs each re-present, so the driver's
  low-framerate compensation never fires silently.
- **Input.** SDL events restamped onto the `psy_rt.h` clock. The manual
  states that keyboard and mouse timestamps are at best
  millisecond-granular. On the Windows message path they count in the
  system tick: posted key events were stamped up to 15.4 ms early
  (docs/psy_screen.md). The manual points at `psy_serial.h` response
  boxes for reaction times.
- **Photodiode patch.** A corner patch the frame loop can toggle for the
  loopback test.
- **Multiple displays.** One handle per display, each with its own
  context, swap path, capability kind and flip record. A group present
  submits to every handle and then waits on every handle, on one
  thread per display where the swap blocks, so waits never serialize
  into a dropped frame. The log records each display's onset. Two
  displays flipping together is a hardware claim: only genlock on a
  workstation GPU guarantees it, and two photodiodes measure it.
  - Windows: one fullscreen window per display in independent flip,
    each composed at its own refresh. The composition swapchain
    presents to more than one display.
  - Linux X11: one window per output with its own sync-control
    counters, or one window spanning two outputs in the same mode with
    a single swap, which works only when the CRTCs are in step and is
    the one path to a synchronous dual display without genlock.
    Separate X screens (`:0.0`, `:0.1`, one per display) are a
    supported configuration too, and the recommended one for a stimulus
    display beside an operator display, as in Psychtoolbox: each screen
    has its own page flips and vblank counter, and the operator
    screen's compositor cannot touch the stimulus screen. The GLX
    backend takes the X screen by name (`desc.x_display`). Whether
    SDL3 creates a window and context on a screen other than the
    default must be checked on a real two-screen setup (reports from
    the SDL 2.0 era say it did not). If it does not, the backend opens
    its own X connection to that screen and owns the window and the
    GLX context there, as the Windows backends own their swapchain.
    Input on that screen then comes from XInput2 on the same
    connection, or only from the operator screen; reaction times use
    response boxes either way. Wayland has no equivalent: a display is
    chosen by going fullscreen on a `wl_output`, with presentation-time
    per output.
  - macOS: one window per display on its own display link, nothing
    synchronizes them.
  - Web: one window. A second display is out of scope.
- **Untimed screens** (proposal, 2026-10-06), for an operator view, a
  mirror of what the participant sees, or an eye-tracker overlay. An
  untimed screen is a `psy_screen.h` handle opened with a kind that
  makes no timing claim:
  - it never blocks the frame thread: it presents without waiting
    (DXGI: `DXGI_PRESENT_DO_NOT_WAIT`; GLX: swap interval 0 on its own
    X screen) and drops frames freely, counting them;
  - it never joins a group's wait, and a group refuses it;
  - its records carry no onset and no tier;
  - it shows a scaled copy of a timed screen's scene (a texture shared
    on the same device, sampled after that screen's draw) or its own
    content, such as a UI.

  A second swapchain on the same GPU can disturb the timed display:
  on Windows the operator window adds composition work for DWM on a
  shared GPU. So before it is built, a measurement on two physical
  displays decides it: the timed display fullscreen in independent
  flip, the untimed window updating at the desktop rate on the other
  display, and the timed display's flip records (drops, residuals,
  path) with and without it. If the untimed screen disturbs the timed
  one, the manual names the condition, and the browser operator
  console (section 6) stays the recommended operator display.
- **Per-flip record** into the event ring: target, estimate, residual,
  dropped count, mode, and the phase durations of the frame: timeline
  evaluate, script callback, draw submission, texture upload, swap
  wait, and GPU time where the disjoint timer query extension exists.
  A dropped frame names the phase that ate the budget without any
  tool attached.

### 4.3 psy_gfx.h (`psygfx_`, `PSYGFX_`)

Includes `psy_screen.h`. GL ES 3.0 only. No compute stage.

Coordinates (decided 2026-10-05; the script API of section 6.1 uses the
same): screen pixels from the top-left corner, x right, y down; positive
angles turn +x toward +y, clockwise on the screen; degrees are one scale
factor with the same origin and axes. A stimulus has a `place` (the screen
point x and y are measured from, the center by default) and an anchor (the
point of its own box at x, y that it turns about, the center by default).

- **Backend interface** of about fifteen calls: create a pipeline from
  precompiled shaders, create and update a texture, set a uniform
  block, draw a quad, draw an instanced set, begin and end a pass to a
  float render target, present. Small enough that a second
  implementation is possible if ANGLE fails somewhere.
- **Stimulus set.** Rect, grating and gabor by shader, dot fields by
  instancing, image textures, text from glyph outlines (Slug, 5.2), noise
  and filtered noise by fragment passes on float render targets.
- **Shapes by signed distance.** Circle, annulus, rect, line, polygon,
  cross, aperture and mask as signed-distance functions in one
  fragment shader, with the edge profile as a parameter: hard,
  raised-cosine or Gaussian, with its width in pixels. The edge
  profile sets the spatial-frequency content, so the caller states it.
  Complex vector artwork is rasterized by the pack tool at the display
  resolution and drawn as a texture. No vector drawing library.
  The boundary (v0.3, 2026-10-05): closed-form distance functions
  (rounded and partial shapes, regular polygons and stars, ellipses), a
  bounded fold of at most 56 primitives, and paths of at most 224 points
  are inside it, with effects, dashes and gradients drawn from the same
  field. SVG, fill rules and a general path rasterizer are outside it.
  Artwork still goes through the pack tool, as a distance texture
  (MASK_TEX, MSDF or MTSDF).
- **Output stage.** Gamma, CLUT, dithering for 10-bit and Bits#-style
  modes, in the final shader. SDL3 has no gamma ramps, so this is the
  only place gamma lives. Stereo on one display is an output mode
  here: side by side, interlaced, or frame-sequential with shutter
  glasses at 120 Hz. Quad-buffer stereo is a workstation GL feature
  that ES 3.0 does not have.
- **Calibration.** Measured primaries and gamma from a photometer become
  a 3x3 matrix into cone space (Stockman-Sharpe fundamentals), DKL and
  cone contrast, and a table from linear to device values. Changed
  2026-10-06: the calibration and the color math are `psy_color.h`'s
  (`psycol_`, pure computation), which `psy_gfx.h` includes from v0.5. It
  is a color header for calibrated displays, not a general color library:
  every conversion goes through a calibration, a color outside the gamut
  comes back unclipped with its distance, and gamut questions are calls of
  their own. No ICC profiles, appearance models or color-difference
  formulas (docs/psy_color.md). Was: "No general color library".
- **Shader contract.** A user writes a fragment function body in GLSL ES
  against a fixed interface: a uniform block with time, resolution, the
  frame's predicted onset and a parameter array, plus fixed texture
  slots. The tool wraps it and precompiles it. ANGLE's own translator
  compiles it at runtime in dev mode. A user shader is the user's
  timing problem, and the flip log is where they find out.
- **Determinism.** GPU float arithmetic is not identical across vendors.
  A stimulus whose pixel values must be reproducible is generated on
  the CPU on the pump, or in the pack, seeded, and uploaded.
- **Parameter table** for the designer.
- **Added in v0.4.0** (2026-10-06): a program binary cache in storage the
  caller owns; NV12 and I420 textures converted to linear light by a
  stated encoding, and texture import for `psy_video.h`'s GPU paths;
  instanced stimuli (a template and up to 16384 elements a frame, with a
  per-element hit test); draws reordered across kinds where they do not
  overlap, with identical pixels.

### 4.4 psy_audio.h (`psyau_`, `PSYAU_`)

Needs miniaudio. The implementation block defines
`MINIAUDIO_IMPLEMENTATION` unless the translation unit already did.

Two halves behind one API. The scheduler and the synthesis render
into any callback that supplies a frame clock. The device backend is
miniaudio by default, and an audio device extension (section 12) can
replace it, for hardware miniaudio cannot open, such as a 64-channel
ASIO array. The scheduler then runs as a source inside the extension's
callback.

- **Project format.** The caller declares one rate, one channel count,
  one sample format and a channel map. 48 kHz is the default. 44.1, 96
  and 192 kHz and 24-bit are equal choices. The device opens at the
  declared format or the open fails.
- **Strict open.** Disable miniaudio's automatic sample rate conversion,
  read back the device's internal rate after init, and fail on a
  mismatch with a message that names the OS rate. Exclusive mode is a
  `desc` field.
  Changed 2026-10-05 (from the miniaudio 0.11.25 source): with a nonzero
  rate, miniaudio asks WASAPI shared mode to resample and then reports
  the requested rate, so a read-back cannot see it; the header asks for
  no format, rate or channel count and refuses what the OS gives when it
  differs. Exclusive mode does not fix a rate mismatch: miniaudio opens
  it at the endpoint's Default Format, the same rate as shared mode.
  Another exclusive rate needs a WASAPI Audio device extension.
- **Schedule.** `psyau_play_at(buffer, t)` on the `psy_rt.h` clock. The
  callback counts frames from an anchor correlated to the clock and
  starts the buffer on the right frame.
- **Master clock.** The `psy_rt.h` clock is the master. The header fits
  the device clock against it and plans every onset in device frames from
  the fit. It never resamples and never inserts frames. A long buffer's
  end follows the device clock and its record says where it ended.
  Changed 2026-10-05: the first plan nudged the stream by resampling or
  frame insertion, against principle 5. Locking a movie's soundtrack to
  its video goes to `psy_video.h`, which follows the audio fit with the
  movie base.
- **Synthesis.** Tones, noise, envelopes, at the project rate.
- **Latency.** Reported only as measured by the line-out to line-in
  loopback test.
- **Per-onset record** into the event ring.
- **Streams.** Added 2026-10-06 (v0.2.0): a ring-fed voice that is
  played, planned, recorded and confirmed as a buffer is. Its sample s
  plays on stream frame origin + s. A late start or a ring underrun skips
  samples and does not move origin. A movie's soundtrack is a stream, so
  movie sound takes the same path as every other sound, and `psy_video.h`
  follows origin. WAV files (RIFF, RF64, BW64) stream through it.

### 4.5 psy_timeline.h (`psytl_`, `PSYTL_`)

Includes nothing. Caller-sized fixed arrays, no heap.

- **Events.** Onset, offset, trigger, at a time on a chosen time base:
  the `psy_rt.h` clock, a trial clock, or a movie clock.
- **Tracks.** Keyframes with easing on any float channel: position,
  contrast, opacity, spatial frequency.
- **Evaluate at the predicted onset.** One call takes the predicted
  onset of the frame being drawn, fills a state struct and reports
  which events fired. It never samples the current time.
- **Quantization.** An event lands on a frame, by default the nearest
  frame to its time. The evaluate call reports the frame and the
  residual.
- **Replay.** Deterministic from the same inputs and the same frame
  times, with a record of where every event landed.
- Composes with `psy_trials.h`: the trial handler picks the condition,
  the timeline runs it. At movie scale the arrays hold thousands of
  annotations.

#### 4.5.1 C authoring API

Status: built in `psy_timeline.h` v0.4.0 (2026-10-06), revised from the
2026-10-04 draft after a review (docs/psy_timeline.md, "Sequences"). The
draft lowered each `psytl_to` to `psytl_tween()`. A channel holds one
waiting tween, so the draft trial below lost its first ramp, and a
re-anchor did not replay tweens. The goal is for C code to be about as
short as a JS animation library, GSAP or anime.js, with the timeline's
timing unchanged. The builder is in the same header. A C trial and a Luau
trial read alike (section 6.1).

The trial from the `psy_timeline.h` USAGE section, in GSAP:

```js
gsap.timeline()
  .set(fix, {visible: 1})
  .set(fix, {visible: 0}, "+=0.5")
  .set(grating, {visible: 1}, "<")
  .call(trigger, [12], "<")
  .to(grating, {contrast: 0.5, duration: 0.1, ease: "sine.inOut"}, "<")
  .to(grating, {contrast: 0, duration: 0.1, ease: "sine.inOut"}, "+=0.5")
  .set(grating, {visible: 0});
```

The same trial with the builder (C99):

```c
psytl_seq q = psytl_seq_on(&tl, TRIAL);          /* cursor at base time 0 */
psytl_on(&q, FIX);
psytl_wait(&q, PSYTL_MS(500));
psytl_off(&q, FIX);
psytl_on(&q, GRATING);
psytl_trigger(&q, 12);
psytl_to(&q, CONTRAST, &(psytl_tween_desc){
    .to = 0.5f, .duration = PSYTL_MS(100), .ease = PSYTL_EASE_COSINE });
psytl_wait(&q, PSYTL_MS(600));
psytl_then(&q, CONTRAST, &(psytl_tween_desc){
    .to = 0.0f, .duration = PSYTL_MS(100), .ease = PSYTL_EASE_COSINE });
psytl_off(&q, GRATING);
if (q.err < 0) report(psytl_strerror(q.err), q.err_call);
```

The rules:
- **A cursor.** `psytl_seq` holds a base and a cursor in base time.
  `psytl_seq_on()` puts the cursor at 0. `psytl_seq_now()` puts it at the
  base's "now", the time a tween desc uses when `start_set` is false.
  `psytl_wait(&q, dt)` moves the cursor by `dt`, and `psytl_at(&q, t)`
  sets it. `q.t` is the cursor: store it in a variable to use it as a
  label.
- **Events and tweens at the cursor.** `psytl_on`, `psytl_off`,
  `psytl_set_value`, `psytl_trigger` and `psytl_mark(&q, code, user)` add
  an event at the cursor. `psytl_to` starts a tween at the cursor plus the
  desc's `delay`, and the cursor does not move. That is the script's
  non-blocking `tween()`. A desc with `start_set` is refused: the cursor
  is the start.
- **One keyed track per channel.** A sequence's tweens on a channel
  become one keyed track in the key arena, which grows with each call.
  The track waits for its first start and then takes over from the old
  driver, as `psytl_tween()` does. After that it is a function of base
  time, so re-anchoring a trial replays it. A tween that starts inside
  the channel's previous one is refused, and so is `PSYTL_FOREVER`.
- **Sequencing.** `psytl_then` is `psytl_to` and then a wait to the
  tween's end, which is the default placement of GSAP's `.to()`.
  `psytl_to` returns the tween's end time, so
  `psytl_at(&q, psytl_to(...))` does the same thing explicitly.
- **A sticky error, as in a stream.** The first failing call stores its
  code in `q.err` and its index in `q.err_call`. Every call after that
  does nothing. So a sequence needs no check after each line, only one
  at the end. That is the C analog of a chained JS call. The builder is
  not atomic: the calls before the error stay. `psytl_seq_cancel(&q)`
  removes what the sequence added.
- **Keyframes and stagger:** `psytl_keyframes(&q, ch, values, offsets, n,
  ease)` joins the channel's track in the arena, and
  `psytl_on_n(&q, chans, n, stagger)` adds onsets all or none.
- **Nothing here changes timing.** Every call lowers to events and keys at
  absolute base times. Frame placement, sampling at the onset and replay
  are the core's.

##### The same sequence as data

There is no method chaining. In C, a call that returns its receiver
chains only by nesting, which reads from the inside out. Function
pointers in the struct still repeat the receiver, prevent inlining and
are hard to follow in a debugger. Chaining in JS gives three things, and
C gets each one in another way:
- One error path for the sequence: the sticky `q.err` above.
- No repeated receiver: `&q,` on each line is the cost, and that is
  ordinary C (sokol, raylib).
- A whole timeline as one readable unit: an op array, below.

A sequence that is known when the code is written is a table of ops.
Thin macros over designated initializers build the ops. `psytl_run()`
gives each op to the same builder calls, in order, with a sticky error:

```c
static const psytl_op trial[] = {
    PSYTL_ON(FIX),
    PSYTL_WAIT(PSYTL_MS(500)),
    PSYTL_OFF(FIX), PSYTL_ON(GRATING), PSYTL_TRIGGER(12),
    PSYTL_TO(CONTRAST, .to = 0.5f, .duration = PSYTL_MS(100), .ease = PSYTL_EASE_COSINE),
    PSYTL_WAIT(PSYTL_MS(600)),
    PSYTL_THEN(CONTRAST, .to = 0.0f, .duration = PSYTL_MS(100), .ease = PSYTL_EASE_COSINE),
    PSYTL_OFF(GRATING),
};

psytl_seq q = psytl_seq_on(&tl, TRIAL);
psytl_run(&q, trial, PSYTL_COUNT(trial));
if (q.err < 0) report(psytl_strerror(q.err), q.err_call);   /* the op's index */
```

An op is a tagged struct:

```c
typedef struct psytl_op {
    int              op;      /* PSYTL_OP_ON, _OFF, _SET, _TRIGGER, _MARK,
                               * _WAIT, _AT, _TO, _THEN                      */
    int              ch;
    int64_t          t;       /* WAIT: dt; AT: base time                    */
    int32_t          code;    /* TRIGGER, MARK                              */
    float            value;   /* SET_VALUE                                  */
    uint64_t         user;    /* MARK                                       */
    psytl_tween_desc tween;   /* TO, THEN                                   */
} psytl_op;                   /* 96 bytes                                   */

#define PSYTL_ON(c)      { .op = PSYTL_OP_ON, .ch = (c) }
#define PSYTL_WAIT(dt)     { .op = PSYTL_OP_WAIT, .t = (dt) }
#define PSYTL_TO(c, ...)   { .op = PSYTL_OP_TO, .ch = (c), .tween = { __VA_ARGS__ } }
```

Why this form, and not only the builder:
- **It is data.** The table can be `static const`, so it can live in
  read-only memory. It can also be logged and compared with a diff. It
  has the shape of the pack's experiment definition (principle 6), so
  the eject path emits it directly, and a table from the pack can be
  given to `psytl_run()` without a compile step.
- **No new semantics.** `psytl_run()` is a loop over the builder calls,
  so the two forms mix freely in one seq. A static table can set up a
  trial, and builder calls can then add the computed parts, such as a
  staircase level.
- **Precedent.** Modern C single-header libraries use this style: sokol
  passes descs as compound literals, and the Clay layout library builds
  whole UIs from macros over designated initializers.

Measured (a stand-in for `psytl_op` with the real `psytl_tween_desc`,
in a scratch program, 2026-10-05):

| Build | Result |
|---|---|
| gcc 11.4, C99, `-Wall -Wextra -Wpedantic -Werror` | Clean; the table is a constant initializer |
| MSVC 19.44, default C dialect, `/W4 /WX` | Clean |
| MSVC 19.44, `/std:c++20 /W4 /WX` | Clean |
| g++ 11.4, `-std=c++20 -Wall -Wextra -Werror` | 44 `-Wmissing-field-initializers` errors: g++ 11 warns about omitted fields even with designated initializers. Clean with `-Wno-missing-field-initializers` |
| g++ 11.4, `-std=c++17` | Fails: no designated initializers before C++20 |

The costs:
- A mistake inside a macro's `__VA_ARGS__` gives a worse compiler
  message than the same mistake in a plain call. One thin macro for
  each op and no logic in the macros keep the message short.
- In C++20, designated initializers must follow declaration order:
  `.to` before `.duration` before `.ease`, the order of
  `psytl_tween_desc`. C does not require this.
- C++17 users use the builder.

The header's own compile checks are `tests/compile/psy_timeline.c` (the
table as C) and `tests/compile/psy_timeline_ops.cpp` (as C++20, with
`-Wno-missing-field-initializers` on g++; g++ 16.1 still warns there).

Decisions (2026-10-05):
- **`psytl_on` and `psytl_off`, not show and hide.** The header does not
  know what a channel means. The script layer keeps `show` and `hide`
  for stimuli, and they lower to on and off. The ops are `PSYTL_ON` and
  `PSYTL_OFF`, as in the examples above.
- **Audio is not frame-quantized.** An on or off on a channel that the
  player maps to a sound goes to `psyau_play_at()` at the event's own
  time, sample-accurate. It must also be handed over before that time,
  because the audio path needs lead time. An evaluate fires an event on
  the frame that shows it. That is too late and too coarse for sound. So
  the timeline needs a look-ahead query: give the pending events of a
  base whose RT time falls in a window, without firing them:
  `psytl_peek(tl, base, from_rt, to_rt, out, cap)`, built in
  psy_timeline.h v0.3.0 (2026-10-05). The
  player's audio path schedules from it, and the video path keeps
  evaluate. The event's landing record stays the video frame's. The
  audio onset record comes from `psy_audio.h` (section 4.4). Which base
  and channel kinds are audio is the player's choice, not the header's.
- **The key arena is in the timeline handle.** `desc.keys` and
  `desc.key_capacity` give caller-owned storage, as `desc.events` does,
  with no default (NULL: a sequence tween is `PSYTL_ERR_FULL`). Each
  tweened channel has one block, and the channel stores an offset, not a
  pointer. A replaced block, or one whose base `psytl_clear()` cleared, is
  garbage until a call needs its room; then one pass compacts the arena.
  A call that does not fit is refused and adds nothing. The arena is not
  in the seq: a seq is a short-lived cursor, often on the stack, and the
  keys must live until the track is replaced or its base is cleared,
  which only the timeline knows.
- **No C++ sugar for now.** C++17 users use the builder. A chained C++
  wrapper is not planned.
- **g++ and `-Wmissing-field-initializers`.** The C++20 compile check of
  the op macros builds with `-Wno-missing-field-initializers`. The
  macros keep zero means default.
- **`psytl_run()` checks every table again.** A table that the designer
  checked at import can still be wrong in the player: the player can be
  a different version from the designer, an extension that an op needs
  may not be installed, the parameter tables and channel counts may have
  changed, or the rig's display or audio rate may make an op invalid,
  for example a flicker rate. The check runs once per run, not per frame.
  Binding is checked against the first earlier op naming the channel,
  O(n^2) compares with no scratch table. It is all or nothing, as
  `psytl_add_n()` is: the whole table is checked before the first op is
  applied, so a bad op at index 37 does not leave half a trial.
  `psytl_check_ops(tl, base, ops, n, &bad)` does the same check without
  applying, so the player can reject a pack when it loads, before the
  session starts, not at trial 190.

Open questions:
- How far ahead must `psytl_peek()` look for audio? That is the audio
  path's measured scheduling lead plus one frame. psy_audio.h v0.1.0
  measured the shortest lead with no late onset at 43.5 to 54.5 ms
  (WASAPI shared, 10 ms periods, this laptop; docs/psy_audio.md). The
  line-in loopback test is still needed for the output latency itself.
- Answered (v0.4.0): the arena has no default. An inline arena would grow
  every handle, and the handle is already 64 KB.

### 4.6 psy_video.h (`psyvid_`, `PSYVID_`)

Includes `psy_screen.h`. Uses SDL3 for capture.

- **Scheduler.** At each flip, pick the frame whose presentation
  timestamp is due against the `psy_rt.h` clock. Drop or repeat as
  needed. Never count frames. Log every decision.
- **Decode-ahead** on the `psy_rt.h` pump, N frames ahead, texture
  upload on the frame thread.
- **Backend interface:** open, decode the next frame with its
  timestamp into caller-owned planes, seek.
- **Backends**, in order of implementation:
  1. Frame sequence: a flat container of raw or QOI frames with an
     index. Read and write. The value-exact path.
  2. pl_mpeg: MPEG-1 and MP2, software decode.
  3. Media Foundation source reader (Windows) and AVFoundation asset
     reader (macOS): hardware H.264 and HEVC to NV12 with timestamps.
     Required by feature-length naturalistic paradigms.
  4. FFmpeg: opt-in, the Linux route for the same.
- **Seek and resume.** Seek to the previous keyframe and decode forward
  silently. Frame-accurate, checked against the log.
- **YUV to RGB** in the output shader, with the manual stating that
  codecs are lossy in luminance and chroma and that value-exact
  stimuli use the frame sequence.
- **Capture.** SDL3 camera frames restamped onto the `psy_rt.h` clock,
  with the current trial and event attached, written to the frame
  sequence container or to an opt-in encoder (minih264 with minimp4,
  Media Foundation sink writer, AVFoundation asset writer). Encoding
  runs on the pump. The manual states that the camera timestamp is
  delivery time, not exposure time, and that the LED loopback measures
  the offset.
- **Per-frame record** into the event ring: movie timestamp, flip time,
  decision.

### 4.7 psy_net.h (`psynet_`, `PSYNET_`), later

UDP triggers and clock-offset estimation between two machines. Lab
Streaming Layer markers through liblsl as an opt-in backend.

### 4.8 Not planned

- HMD virtual reality. An OpenXR runtime owns the compositor: the
  application asks for a predicted display time, renders for it and
  hands the frame back, and the runtime reprojects, drops or repeats
  as it decides. Present-at-time does not exist there and no timing
  claim can be the header's. If a `psy_xr.h` ever appears, OpenXR is
  the route and `psy_timeline.h` carries over unchanged, because
  evaluate-at-predicted-onset is the OpenXR frame loop. `psy_screen.h`,
  the swap paths, stereo rendering, tracking and controllers do not
  carry over and are a separate project.
- Offline psychometric fitting. R, Python and MATLAB do it better and
  analysis happens there.
- A logging library. The event ring in `psy_rt.h` is the primitive.
- A general random-number header. `psy_trials.h` owns the only draw.

Removed from this list 2026-10-06: "a standalone color header". It is
`psy_color.h` (4.3, Calibration).

## 5. The pack

One zip file in store-only mode, entries aligned to 4 KB, so anyone can
open it with anything, and a browser can fetch the central directory
from the tail and then only the entries it needs by HTTP range. A
two-hour film streams by byte range. Compression is per resource, never
on the container. The pack can be appended to the player executable for
a single-file deploy on Windows and Linux.

### 5.1 Manifest

A format version, and per entry: a checksum, the source file's hash,
the tool version, and the exact derivation including any ffmpeg command
line. The runtime refuses a format version it does not know. A methods
section can cite the manifest and a reviewer can rebuild the pack.

### 5.2 Canonical resource forms

| Resource | Canonical form | Notes |
|---|---|---|
| Texture | Uncompressed 8-bit, 16-bit or float planes, optionally QOI or a single-file deflate or LZ4 | Block compression opt-in, recorded in the manifest. |
| Video | Constant frame rate, fixed pixel format and resolution, closed GOPs, no B-frames, plus an index of frame to byte offset and timestamp | Produced by ffmpeg, command recorded. |
| Audio | The project rate and channel count; 16-bit, 24-bit or float samples | Resampled offline with a high-quality resampler, recorded. In memory always float, widened exactly at load (2026-10-05): one mixer path, and float holds 16- and 24-bit values exactly. |
| Font | The font file, plus the glyph outlines built from it in the Slug form (quadratic curves and bands; cubic outlines converted within a stated tolerance), and alpha coverage at the sizes the experiment uses for value-exact text. Static text is stored as laid-out glyph runs. | No platform font engine. One pinned Skribidi does layout, bidirectional text and shaping in the designer (WebAssembly, with editing), the pack tool and the player, so text is the same everywhere. Scalable text is drawn from the outlines by the Slug algorithm (Lengyel, JCGT 2017; patent dedicated to the public domain 2026-03-17; reference shaders need attribution). A whole font fits in the pack (about 65 MB for a CJK font, against 212 to 762 MB as MSDF atlases), and a glyph missing from it is built from the font's outline in 17 to 66 us, so the player rasterizes nothing at run time. Value-exact alpha coverage comes from an exact-area rasterizer in the pack tool, not Skribidi's (max error 0.24 to 0.32 of full scale), and is drawn on the pixel grid only. Effects: outlines and faux bold by stroke expansion of the curves, filled the same way; hard shadows by an offset copy; soft shadows, glow and blurred text by a Gaussian blur pass on a render target (numbers in docs/text_probe.md). Headers only draw glyph runs. Changed 2026-10-06: MSDF atlases dropped from the pack, after a probe on the Iris Xe (docs/text_probe.md; MSDF failed on dense CJK and overlapping contours, and a distance soft edge is a true blur only on straight edges). Changed 2026-10-05: "glyph atlas plus metrics; no font engine at runtime". |
| Shader | The user's fragment body wrapped in the contract (`psygfx_shader_wrap()`, the same function in the tool and the runtime), as GLSL ES 3.00 text, with reflection | glslang validates it in the tool. GL ES 3.0 has no portable binary, so ANGLE compiles the text on the rig when the pack loads; `psy_gfx.h` caches each program's binary per ANGLE build, adapter and driver (`desc.cache`, v0.4.0), checked by its own hash before the driver sees it. SPIRV-Cross is for a second backend only. Changed 2026-10-05: "D3D bytecode is finished by the runner" (docs/psy_gfx.md). |
| Table | Typed binary columns from condition files and from ELAN, Praat or BIDS events exports | Read directly by `psy_trials.h` and `psy_timeline.h`. |
| Calibration | The 3x3 matrix and the gamma table, with the photometer readings beside them | `psy_color.h`'s `.psycal` file (62528 bytes, CRC-32; the same bytes as `psy_gfx.h` v0.4's), rebuilt from its readings on load. A participant's luminance from flicker photometry is session data, not pack data: `psy_color.h`'s `.psylum` file (changed 2026-10-06). |
| Experiment | The definition the player interprets | See section 6. |

The canonical in-memory forms are the same as the on-disk forms (audio
excepted: integer samples are widened exactly to float at load), so a
stimulus generated per trial on the pump enters through the same API
as one loaded from the pack.

## 6. The player

A prebuilt program per platform, and a WebAssembly build, that loads a
pack and interprets the experiment definition. Deploy is player plus
pack. Web deploy is `player.wasm` plus pack. No compile step in the
common case.

- **Runner protocol.** A localhost WebSocket with a session token
  carrying control, resource updates, live parameters, status and the
  log stream. The designer, a future desktop shell, the operator
  console and hot reload are all clients of it.
- **Hot reload.** A resource or parameter update applies immediately in
  preview, between trials in a live session, and never mid-trial. The
  log records that a change applied and what it was.
- **Dev mode.** Runtime shader compilation through ANGLE's translator.
  Off in a deployed player.
- **Profiling.** The player is built with Tracy in on-demand mode, so
  nothing is buffered and no port is opened until a session flag turns
  it on. The operator console plots the per-flip phase durations live
  over the runner protocol. Tracy's capture and CSV export tools give
  the text form, and the loopback tests save a capture beside their
  photodiode data.
- **Trace export.** The event ring's drain writes Chrome trace event
  JSON. One file opens in Chrome DevTools, in Perfetto and in Tracy
  through its Chrome import tool, so a browser session and a native
  rig produce the same artifact.
- **Web player instrumentation.** Under Emscripten the macro layer maps
  zones to the User Timing API and frame marks to console timestamps,
  so DevTools shows them beside the compositor, the GPU, long tasks
  and garbage collection. Builds carry DWARF debug info so a zone
  links to its C line. A session self-test at start records the
  measured refresh, the clock resolution, cross-origin isolation, the
  GPU timer query's availability and fullscreen state, and the log
  records every visibility change. The web player posts its trace and
  log to the designer over the runner protocol. Timing on a
  participant's machine is measured and excluded, never controlled,
  and the exclusion rule is written before data collection.
- **Web backend settings.** A desynchronized WebGL context, the
  high-performance power preference, and Web Audio's scheduled start on
  the audio clock with its output timestamp for the correlation.
- **Scripting.** Between the data definition and the eject path, an
  embedded script (Luau, proposed) carries the logic the designer's
  vocabulary cannot: response-contingent branching, computed stimulus
  parameters, custom trial flow. Rules:
  - Scripts live in the player. The headers stay C.
  - The script API is generated from the headers' parameter tables,
    the same source as the designer's panels.
  - Sandboxed: no file access outside the pack, no network, no wall
    clock. Time comes from the `psy_rt.h` clock through the API and
    randomness from the trial handler's draw, so a replay reproduces.
    A pack from another lab is safe to run.
  - Compiled to bytecode at import, so syntax errors surface in the
    designer.
  - A per-frame callback has a stated budget, and the flip log shows
    when a script blew it. Collector pauses count against it.
  - A data-only experiment has no script. Nobody learns a language to
    run constant stimuli.
- **Script concurrency model.** Coroutines, no async syntax. A call
  that conceptually blocks yields the coroutine to the player's
  scheduler. Lua coroutines are stackful, so a wait works from any
  depth of ordinary function calls and helpers can wait inside
  themselves. The scheduler lives in the player, a few hundred lines,
  and follows these rules:
  - `wait(dt)` targets the previous scheduled point plus `dt`, then
    the frame whose predicted onset is nearest to it, which is the
    timeline's default half-frame lead. Waits chain from scheduled time,
    never from resume time, so sequences do not drift. "At or after" was
    the first rule. It was dropped because a predicted onset carries
    noise, and with 2 us of noise it put about 12% of frame-aligned
    targets a frame late (docs/psy_timeline.md). A wait that must never
    resume early uses the timeline's never-early lead.
    `wait_until(t)` and `wait_frames(n)` exist beside it.
  - Resumes happen at one point in the frame, on the frame thread,
    before the timeline evaluates and before drawing, with the
    predicted onset available. Script time counts against the frame
    budget in the flip record.
  - Everything awaitable is a handle the C side completes: a presented
    flip, an audio onset, a response, a video seek, a streaming load.
    `await(h)` yields until it resolves and returns its measured
    values. `race`, `all`, `timeout` and `repeat` compose handles. A
    response window is `race(input.key(...), wait(2.0))`.
  - Cancellation is `coroutine.close`. An abandoned branch of a race
    releases its handle.
  - Coroutines resume ordered by target time, then creation order, so a
    replay with the same frame times reproduces the sequence.
  - An error aborts the trial with a traceback into the log. The
    experiment definition says whether the session continues.
  - A wait is a timeline event with a coroutine attached and a show is
    an onset event, so the declarative timeline and the script are one
    mechanism. A script can post timeline events directly, a tween on
    contrast for example.
  - Luau yields from C functions but has no continuations, so resume
    values return to the Lua caller and a C wait runs no code after
    the resume. Pack resources are preloaded, so loading needs no
    await except for streaming.
- **Eject path.** The designer can emit ordinary C that calls the
  headers, for users who need custom logic, built with the bundled zig.
  Custom native modules load into the player through a defined
  interface.
- **The player is not an engine.** It is a few headers around a frame
  loop whose flip times, onsets and trigger edges are measured and
  logged.

### 6.1 Script API, draft

Status: draft, 2026-10-04. Nothing here is implemented. It proposes the
API that scripts see. Section 6 above gives the concurrency model.

The goal is the brevity of GSAP and Phaser with the timing of
`psy_timeline.h`. Every call lowers to timeline events, tracks and
tweens. The script does not have a second animation system.

#### Rules

1. **Time is the scheduled point, in seconds.** `now()` is the
   coroutine's scheduled time on the trial clock, not the time at which
   the coroutine resumed. `wait(dt)` moves it forward by `dt`. A call
   that takes no time argument acts at `now()`. So
   `show(a); wait(0.5); hide(a)` puts the offset exactly 0.5 s after the
   onset, and the timeline puts each one on its nearest frame.
2. **Trial time 0 is the first frame of the trial.** The player anchors
   the trial base there and clears it after the trial. A script never
   anchors a base. Session-level timing uses `session.now()` on the RT
   base.
3. **A tween does not block.** `tween()` returns a handle at once, as
   GSAP's `to()` does. `await(h)` blocks. If you do not await, things run
   in parallel. If you await, they run in sequence. No other sequencing
   syntax is needed.
4. **Properties come from the parameter tables.** `g.contrast`, `g.x`
   and `g.sf` are the names in `psy_gfx.h`'s table for the stimulus type,
   with the table's units. The Luau type checker knows them, so a wrong
   name fails in the designer.
5. **One driver per channel is the player's business.** Each stimulus's
   `visible` is driven by events, so each onset and offset has a record
   of the frame it landed on. Every other property is driven by a track.
   Assigning to it (`g.contrast = 0.3`) is a tween of length 0 at
   `now()`. A script never sees `PSYTL_ERR_BOUND`.
6. **Reading a property gives the value of the last evaluated frame.**
   This is what the participant saw on that frame. It is deterministic
   under replay.
7. **The script has no per-tween callbacks.** `await` replaces
   `onComplete`. A track or a sampled table replaces `onUpdate`. The
   escape hatch is `every_frame(fn)`, which has a stated time budget
   (section 6).

#### Examples

The trial from the `psy_timeline.h` USAGE section:

```lua
function trial(c)
  show(fix)
  wait(0.5)
  hide(fix); show(g); trigger(12)
  tween(g, {contrast = c.contrast}, 0.1, {ease = "cosine"})
  wait(0.6)
  await(tween(g, {contrast = 0}, 0.1, {ease = "cosine"}))
  hide(g)
end
```

A response window, with feedback that depends on the response:

```lua
function trial(c)
  show(g)
  local r = await(race(input.key{"f", "j"}, wait(2.0)))
  hide(g)
  if r.key == nil then mark("miss") return end
  local ok = (r.key == "f") == c.left
  show(ok and tick or cross); wait(0.3); hide(ok and tick or cross)
  return {key = r.key, rt = r.t - r.window_start, correct = ok}
end
```

A 10-minute tracking block with a 7.5 Hz flicker on the target:

```lua
function trial(c)
  show(target)
  follow(target, {x = pack.table("sos_x"), y = pack.table("sos_y")})
  flicker(target, {hz = 7.5, duty = 0.5, cycles = 4500})
  wait(600)
  hide(target)
end
```

A staggered onset of 50 dots, 20 ms apart, then all of them fade out
together:

```lua
show(dots, {stagger = 0.02})
wait(2.0)
await(tween(dots, {opacity = 0}, 0.25))
```

#### Reference

Time:

| Call | Effect |
|---|---|
| `now()` | Scheduled trial time, in seconds |
| `wait(dt)` | Advance `now()` by `dt` and yield until the frame nearest to it |
| `wait_until(t)` | Set `now()` to `t` and yield the same way; an error if `t` is before `now()` |
| `wait_frames(n)` | Yield for `n` frames; set `now()` to the trial time of the frame it resumes on |
| `frame()` | The current frame: `index`, `onset` (predicted, trial s) and `period` |

Stimulus events. Each call takes an optional `{at = t}`, which defaults
to `now()`. When `s` is a list, each call also takes `{stagger = dt}`.
Each call returns a handle. `await` on the handle returns the record:
`frame`, `onset` and `residual`.

| Call | Effect |
|---|---|
| `show(s)`, `hide(s)` | Onset and offset of `s.visible` |
| `trigger(code)` | A trigger code at that time; the trigger output sends it at the flip |
| `mark(name, value)` | An annotation in the log; `value` is optional |

Tweens and tracks. A target is a stimulus or a list of stimuli. `props`
maps property names to end values. Each call returns one handle for all
the channels it drives.

| Call | Effect |
|---|---|
| `tween(target, props, duration, opts)` | From the current value to each end value |
| `props` value `{from = a, to = b}` | From a given start (GSAP `fromTo`) |
| `props` value `by(d)` | To the current value plus `d` |
| `opts.ease` | `"linear"` (default), `"step"`, `"cosine"`, `"quad_in"`, `"quad_out"`, `"log"`, or `bezier(x1, y1, x2, y2)` |
| `opts.delay` | Start at `now() + delay` |
| `opts.repeat`, `opts.yoyo` | Repeat the tween; `yoyo` also plays it back to the start |
| `opts.stagger` | With a list: start each one `stagger` later than the one before. A table `{each = dt, from = "first" \| "last" \| "center" \| i, grid = {w, h}, ease = name}` orders the delays by distance from `from`, on a grid when `grid` is given, and spreads them with `ease` (anime.js's stagger) |
| `props` value `{a, b, c}` | Keyframes: a keyed track through the values, evenly spaced over `duration`, or at `opts.times` (fractions of `duration`, rising, 0 to 1) |
| `props` value `function(i, s)` | A value for each target of a list, from its index and stimulus |
| `opts.keep_velocity` | Start from the current value and the current velocity, and come to rest at the end value, with no step in velocity. For a moving target that is retargeted in mid-flight (smooth pursuit) |
| `follow(target, props, opts)` | Drive each property from a pack table (a sampled track), starting at `now()`. `opts.interp` is `"cubic"` (default), `"linear"` or `"step"` |
| `flicker(target, opts)` | A square wave on `target.gate`, a 0-or-1 multiplier in the stimulus shader. `opts.hz`, `opts.duty` (0.5 by default), and `opts.cycles` (forever by default) |
| `target.prop = v` | A tween of length 0 at `now()` |
| `stop(h)` | End the tracks of handle `h` at their current values |

Composition, as section 6 gives it: `await`, `race`, `all`, `timeout`,
`repeat`. Responses, as section 6 gives them: `input.key{...}`,
`input.button{...}` and serial lines through `psy_serial.h`.

#### Lowering

Each call is one or more `psy_timeline.h` calls on the trial base. Times
go to nanoseconds by `psytl_ns()`, rounded to nearest.

| Script | Timeline |
|---|---|
| `show(s)` / `hide(s)` | `psytl_add` ONSET / OFFSET on `s.visible` |
| `trigger(code)` | `psytl_add` TRIGGER, `code` |
| `mark(name, v)` | `psytl_add` MARK, `code` = name id, `user` = `v` |
| `wait(dt)` | A scheduler entry at the target time, resumed by the window rule of the timeline; logged as a MARK |
| `tween(...)` | `psytl_tween` per channel, with a `psytl_tween_desc` |
| keyframes `{a, b, c}` | `psytl_keyframes` on a sequence, with the keys in the timeline's key arena (`desc.keys`, section 4.5.1) |
| `stagger` | The player computes each target's `start`; the timeline sees ordinary tweens and events |
| `opts.repeat`, `opts.yoyo` | `psytl_set_track` keyed, with `period` and `repeats`; `yoyo` adds a key back to the start |
| `follow(...)` | `psytl_set_track` sampled, with the table's rate from the manifest |
| `flicker(...)` | `psytl_set_track` with two STEP keys and a repeat |
| `s.prop = v` | `psytl_tween` with length 0 |
| reading `s.prop` | `psytl_value` |

#### C additions this needs

- Done in v0.2.0: `psytl_tween()` takes a `psytl_tween_desc` with
  `from`, `start` or `delay`, `cycles`, `yoyo` and `keep_velocity`, and
  takes over from the old track's value at its start.
- Done in v0.4.0: `psytl_window(tl, base, &frame, &end)` gives the base
  time that a frame's window reaches, from the same function the evaluate
  uses. The scheduler resumes a wait on the frame where `target <= end`,
  so the player does not copy the rule. `psytl_rt_time()` gives the RT
  time of a base time, to lower `now()` to an RT change point.
- Dropped (v0.4.0): constructors `psytl_onset()`, `psytl_trigger()`,
  `psytl_keys()`, `psytl_samples()`. The sequence builder of section 4.5.1
  and designated initializers serve the same users, and the builder now
  owns the name `psytl_trigger`.

#### Left out, and why

- **Easings that overshoot** (back, elastic, bounce): an overshoot of a
  stimulus value is a confound. `bezier()` with y outside [0, 1] stays
  possible on purpose.
- **Time scale, reverse, seek inside a trial:** a rate on the trial base
  scales every wait, tween and duration of the trial, which is a
  confound. `psytl_rate()` (v0.4.0) exists for a movie base: a script sets
  it through the movie (`m.rate`), never on the trial base. Reverse is
  refused at every level (docs/psy_timeline.md). A movie seek is the video
  header's job.
- **Position parameters and labels** (`"<"`, `"+=0.5"`): `now()` is the
  label, a local variable keeps it, and `wait` is the relative offset.
- **Per-tween `onUpdate`:** it costs script time on every frame, and its
  values would not be in the timeline's record. Use a track.
- **Color tweens in device RGB:** an RGB tween is not a perceptual or
  cone-space path. See the open questions.

#### Open questions

- **Recording the flicker edges.** A `flicker` on `gate` is a track. A
  track has no landing record for each edge, and the flip log and the
  photodiode are the record. Should `flicker` also be able to expand to
  events, for an edge-by-edge log? At 7.5 Hz for 10 minutes, that is
  9000 events, 576 KB of storage.
- **Flicker rates the display cannot show evenly.** 7.5 Hz at 144 Hz is
  19.2 frames per cycle, so the edges alternate between 19 and 20
  frames. Should the designer warn about this, refuse it, or offer the
  nearest rate that divides evenly?
- **Color.** Should a tween on color run in DKL or cone space through
  the `psy_gfx.h` calibration, with the channels as the coordinates of
  that space? Partly answered 2026-10-06: `psy_color.h` converts a DKL or
  cone-contrast color to a stimulus direction (`psycol_to_dir()`) through
  the context `psy_gfx.h` uses (`psygfx_color()`), at 22 to 27 ns a call
  (MSVC; 79 ns MinGW), so three channels bound to a color in a space cost
  about 0.3 ms of CPU for 10000 elements a frame. Open: the binding form
  (three channels to one color field) and whether it belongs in
  `psygfx_bind`.
- **Units.** The parameter table gives each property's unit. Should
  `tween(g, {x = 2})` take a unit suffix (`deg(2)`, `px(2)`), or only the
  table's unit?
- **Aliases.** Should `"sine.inOut"` map to `"cosine"` for users who come
  from GSAP? The current answer is no: one name for each easing.

## 7. The designer

A static web app, offline as a PWA, with the project in local storage.
There is no server. The only job that wants one is the build job.

- **Import is normalization.** Dropping a file runs the pack tool in the
  browser, shows what it did and what it chose, exposes the choices
  that matter (lossy or lossless, project rate, exclusive audio), and
  writes the manifest. A resource that cannot be normalized fails in
  the designer with the file selected, never at run time.
- **Preview** runs the WebAssembly player in the page. It is faithful in
  logic and not in timing, and the page says so.
- **Run** talks to the native player on the rig through the runner
  protocol. A browser is never the rig.
- **Shaders** are validated in the browser with the WebAssembly build of
  glslang, through the same wrapper function the runtime uses. ANGLE
  compiles them on the rig when the pack loads (5.2).
- **Text** is edited in the designer with Skribidi compiled to
  WebAssembly: bidirectional editing, shaping and line breaking for
  complex scripts, laid out by the same pinned version the pack tool
  and the player use.
- **Operator console** is a browser tab on the runner protocol: live
  parameters, the staircase plot, the dropped-frame counter, the log.
- **Build jobs** go to a template repository with a GitHub Actions
  workflow, hosted or self-hosted runners, submitted through the
  GitHub API. Only generated code and the manifest are sent, never the
  pack. macOS builds run on a Mac. The toolchain versions from the
  build go into the manifest.
- **Parameter panels** are built from each header's parameter table.

## 8. Timing model

- The `psy_rt.h` clock is the master for display, audio, video,
  triggers and capture.
- Every present and every onset is a target time and an actual time.
  The residual is logged.
- Under variable refresh the same API gets smaller residuals. Nothing in
  the experiment definition knows which refresh model it got.
- Software timestamps are estimates. The photodiode, the line-in and the
  LED are the measurements. Each header's STATUS block reports the gap
  between the two per platform.
- On Windows, the loopback test prints whether the window is in
  independent flip, from the composition swapchain statistics or from
  PresentMon.
- Variable refresh must be off unless the panel passed both sweeps.
- **Timing tiers** (proposal, 2026-10-05). Each platform and each
  presentation path is in one tier:
  1. Timing good. The onset is an OS vblank time on a path the header
     can verify, such as independent flip or direct scanout.
  2. Timing good only under stated conditions, which the header checks
     at run time where it can and names in the describe line.
  3. Timing bad. The path runs for task development and dry runs only.

  Measurements assign a tier, never an expectation. Each per-flip record
  carries the tier of that flip, because the path can change between
  frames. The screen reports the worst tier since open. An estimated or
  planned onset is never tier 1. A run can require a minimum tier and
  then refuses or flags the frames below it. The analysis, not the
  experiment definition, decides what to do with flagged frames.

## 9. Platform matrix

| Platform | Context | Swap path | Audio | Video decode | Deploy from |
|---|---|---|---|---|---|
| Windows 11 | ANGLE, D3D11 | Composition swapchain | WASAPI, exclusive available | Media Foundation | any host, zig |
| Windows 10 | ANGLE, D3D11 | DXGI flip model | WASAPI | Media Foundation | any host, zig |
| Linux X11 | native GL | GLX sync control | ALSA or PulseAudio | FFmpeg, opt-in | any host, zig, pinned glibc |
| Linux Wayland | native GL | presentation-time | ALSA or PulseAudio | FFmpeg, opt-in | any host, zig |
| macOS | ANGLE, Metal | measured, then claimed | CoreAudio | AVFoundation | a Mac |
| Web | WebGL2 | requestAnimationFrame, fixed grid | Web Audio | frame sequence, pl_mpeg | any host, Emscripten |

Frame sequence and pl_mpeg run everywhere.

Windows 11 is the Windows target. Windows 10 is end of life: it stays
supported where that costs little, for example through run-time lookup
of newer APIs, but no design decision or measurement rests on it.

### 9.1 Android and iOS, not planned, not precluded

The stack ports: SDL3 on both, native GL ES 3.0 on Android, ANGLE's
Metal backend on iOS, AAudio and iOS CoreAudio in miniaudio, MediaCodec
and AVFoundation for video. Android exposes scheduled presentation
times and display-present timestamps through EGL extensions. iOS gives
a presented-time handler. Touch input latency runs tens of milliseconds
and varies, which caps reaction-time work.

Two cases with different answers:

- **Lab-owned tablets.** Controlled hardware, a handful of devices,
  ad-hoc or TestFlight builds, a photodiode. The case to do first if
  mobile comes.
- **Participants' own devices.** The installable web player is the
  answer. A native route needs one player app in each store that
  downloads packs. Apple's guideline 2.5.2 forbids downloaded code
  outside JavaScriptCore, so downloaded Luau bytecode is a gray area.
  If iOS to participants ever matters, the scripting language becomes
  JavaScript. Decide that before the first script binding is written.

## 10. Verification

- `tests/loopback/` gains a photodiode test for `psy_screen.h`, a
  line-out to line-in test for `psy_audio.h`, an LED test for capture,
  a variable-refresh interval sweep and a luminance-versus-interval
  sweep. State on 2026-10-06: the photodiode test
  (`psy_screen_loopback`) and the line-in mode of the audio loopback
  (`--line`) are built and have never run. Every onset measured so far
  is a software timestamp.
- `tests/compile/` gains the C and C++ pairs for each header, and a
  no-ANGLE build for Linux.
- CI on Windows takes ANGLE from the Electron release zip named in
  `ci.yml` (Electron v38.8.6, ANGLE 2.1.25848), checked by checksum and
  cached. Linux uses Mesa's EGL. macOS CI has no GL, so the psy_gfx
  pixel checks skip there; where macOS gets ANGLE is not decided.
- No number outside a STATUS block is a measurement, as today.

## 11. Order of work

Revised 2026-10-06. The first order put Linux X11 with the photodiode
test first and Windows 10 second. The work went to Windows 11 first, on
the one development machine, and Windows 10 became best effort
(section 9).

### State on 2026-10-06

| Step | State |
|---|---|
| 1. `psy_rt.h`: event ring, clock correlation | Done, v0.5.0, with the instrumentation macros. |
| 2. `psy_timeline.h` | v0.4.0: events, tracks, tweens, `psytl_skip`, `psytl_lead`, `psytl_peek`, exact rational base rates (`psytl_rate`), `psytl_window`, `psytl_rt_time`, the `psytl_seq` builder with op tables, `psytl_run`, `psytl_check_ops` and the key arena (4.5.1). |
| 3. `psy_screen.h` | v0.3.0 on Windows: DXGI_FLIP and COMPOSITION, flip hooks (codes, triggers, after-flip callbacks), the OS gamma ramp. X11, Wayland, macOS and web are stubs. No photodiode run. |
| 4. `psy_audio.h` | v0.2.0 on WASAPI shared mode: onsets from a fit of device positions, tier 2. Streams (ring-fed voices, the soundtrack path) and WAV streaming. The other backends compile and have never run. No line-in run. |
| 5. `psy_gfx.h` | v0.4.0: stimuli, the signed-distance vector program, strokes, sprites, groups, render targets, MSDF glyph runs, the output stage; a program binary cache (open 11 to 35 ms warm), NV12 and I420 video converted to linear light, texture import (GL, D3D11 through ANGLE), instanced stimuli, and draws reordered where they do not overlap. Text layout (Skribidi) and atlases belong to the pack tool and the player (5.2), not to the header. Missed bars: four of v0.3's GPU bars, and the video program's 1.5x of an RGBA8 image (docs/psy_gfx.md). |
| 6. `psy_video.h` | v0.1.0: scheduler, frame sequence, pl_mpeg, decode-ahead, YUV conversion on the pump. Not built: the planar shader path, Media Foundation, the shared GPU path, the soundtrack source, AVFoundation, capture. |
| 7 to 10 | Not started. |

### Next, in order

1. **Hardware verification on Windows 11.** Run the photodiode test on
   DXGI_FLIP and COMPOSITION, and the line-in test for `psy_audio.h`.
   Re-measure the "one vblank early" flips of the windowed DXGI runs,
   which the `flip_at` fix of 2026-10-05 may have removed. Every later
   step builds on onset claims that are software timestamps until this
   step is done. It needs the photodiode and a line-out to line-in
   cable on the development machine.
2. **`psy_gfx.h`**: the program binary cache, planar YUV and texture
   import, instanced stimuli: done (v0.4.0). Next, docs/psy_gfx.md "Next".
3. **`psy_video.h`**: the planar shader path, Media Foundation, the
   shared GPU path, the soundtrack source. Needs the planar formats and
   texture import from item 2.
4. **`psy_timeline.h`**: done for now (v0.4.0: base rates,
   `psytl_window()`, sequences). `psy_video.h` applies the rates for any
   rate other than 1.
5. **Other platforms.** `psy_screen.h` on Linux X11 (GLX sync control)
   and Wayland (presentation-time), then macOS through ANGLE's Metal
   backend. `psy_audio.h` on ALSA, PulseAudio and CoreAudio. A platform
   gets its tier from a photodiode or line-in run on that platform, not
   from CI.
6. The pack tool as a library, and its CLI, with Skribidi, the Slug
   outline builder and the exact-area alpha rasterizer. Before it:
   Slug glyph runs and the blur pass in `psy_gfx.h`, after instancing.
7. The player, native and WebAssembly, and the runner protocol.
8. The designer.
9. Build jobs, `psy_net.h`.

Items 2, 3 and 4 can run in parallel. Measurements on the development
machine run one at a time: on 2026-10-05 a second measuring job's load
showed up as a false regression in the first.

## 12. Extensions

The host owns what a timing claim depends on: the clock, the present,
the device callback's timing, the pack, the frame loop and the log.
Everything inside those boundaries is replaceable behind a small C
interface. The backends already named in this document, the swap path,
the gfx backend and the video decoder, are the same mechanism. An
extension implements one of these kinds, each a versioned C vtable:

| Kind | Contract | Example |
|---|---|---|
| Renderer | Draws into a texture the host owns. The host composites and presents. Its frame time is a phase in the flip record. | Filament, sharing the GL context and rendering to a render target, fenced on that context. |
| Presenter | Takes the window and implements the swap-path interface, flip record included. The host's timing claim ends at this boundary and the manual says so. | Code that must own the swapchain. |
| Audio source | Renders into the buffer the host's device callback hands it, under the real-time rules. | cave-audio's offline sink. |
| Audio device | Owns the device and calls the host's scheduler as a source in its callback. | cave-audio's ASIO array output. |
| Input source | Posts timestamped samples or events into the event ring and to scripts. | Eye trackers, motion capture, cave-audio's NatNet head tracking. |
| Video decoder | The interface in `psy_video.h`. | A codec the built-in backends lack. |
| Script module | Exports a parameter table and typed functions. The host generates the Luau binding from the table. | Any of the above exposing parameters. |

- **Mechanics.** A shared module with one exported symbol that returns
  the vtable and its interface version, compiled by its author against
  the psy C headers with any toolchain. The ABI is plain C structs. On
  desktop the player loads the modules the pack manifest lists, by
  hash. The web player and iOS have no dynamic loading, so an
  extension there is a custom player build through the eject path,
  from the same source and the same vtable. The designer shows a
  placeholder for what an extension renders unless the extension has
  a browser build.
- **Host API.** A second vtable passed in at load: the clock, the
  event ring push, the instrumentation macros as function pointers so
  extension zones land in the same trace, frame hooks with the
  predicted onset and the flip record, pump submission for off-thread
  work, pack resource access by name, and parameter table
  registration so the designer's panels and the script binding include
  the extension's parameters.
- **Threading contract**, stated and measured, not enforced. The host
  owns the frame thread and the audio callback. An extension may run
  its own threads. It must not block the frame thread, must not
  allocate or do I/O in the audio callback, and must report its own
  timing through the hooks. The flip record's extension phase shows
  when one does not.
- **Licenses.** The boundary does not change them. The manifest records
  each module's license beside its hash. A lab that distributes a
  player with a GPL module distributes under the GPL.

## 13. Open questions, to be measured or verified, not decided

- Does SDL3 create an EGL context on Cocoa against ANGLE's Metal
  backend, and what present timing does that backend expose?
- ANGLE's 10-bit surface configurations on D3D11 and Metal. Answered
  for D3D11 (2026-10-05, docs/psy_gfx.md): RGBA 10/10/10/2 and RGBA16F
  configurations exist, and a client-buffer pbuffer on an
  `R10G10B10A2` texture works. Metal: not run.
- Composition swapchain: how a target time between two vblanks is
  quantized, how far ahead the queue accepts presents, and whether a
  borderless fullscreen composition window gets independent flip on the
  lab GPU and driver. Answered on the Iris Xe (2026-10-05,
  docs/psy_screen.md): the frame goes to the nearest vblank, a tie to
  the earlier one; the queue accepts at least 7 presents ahead; a
  fullscreen COMPOSITION window got independent flip. The lab GPU and
  driver: not run.
- Whether an untimed screen on a second display disturbs a timed
  display in independent flip on the same GPU (4.2, Untimed screens).
- Whether SDL3 opens a window and a GL context on an X screen other
  than the default (`:0.1`).
- Whether the composition swapchain's present-at-time removes the need
  for the spin-wait under variable refresh.
- miniaudio's exclusive-mode behavior per backend and the exact flag
  set that disables its silent rate conversion. Answered for WASAPI
  (2026-10-05, docs/psy_audio.md): ask for no format, rate or channel
  count, so the client format is the device's; exclusive mode opens at
  the endpoint's Default Format. Other backends: not run.
- Whether SDL_GPU gains a browser backend, which would make it a
  candidate second implementation behind the `psy_gfx.h` interface.
- The first stimulus that needs compute, which would move the plan to
  WebGPU through Dawn.
