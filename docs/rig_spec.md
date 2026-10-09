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

1. **One clock.** Every timestamp in the system is on the `ysp/rt.h`
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
    +-- ysp/screen.h   window, display modes, flip at time      [SDL3, ANGLE]
    +-- ysp/gfx.h      stimuli on GL ES 3.0, output stage       [ysp/screen.h]
    +-- ysp/audio.h    device, scheduled playback, synthesis    [miniaudio]
    +-- ysp/video.h    decode-ahead, scheduler, capture         [ysp/screen.h, SDL3]
    +-- ysp/timeline.h events and tracks on the clock           [nothing]
    +-- ysp/trials.h, ysp/stair.h, ysp/quest.h, ysp/aep.h        [as today]
    +-- ysp/rt.h       clock, waits, worker, pump, event ring   [OS]
    +-- ysp/parallel.h, ysp/serial.h                            [ysp/rt.h]
```

The headers stay usable without the player, the pack or the designer.
The player is a program that uses them. The designer is a program that
produces packs and talks to the player.

## 3. Dependency decisions

| Component | Decision | Reason |
|---|---|---|
| SDL3 | Accepted, for windowing, input, display modes, camera capture | Windowing is the hairiest OS surface. Stable ABI, packaged everywhere, Emscripten support, exposes native handles so present-timing code can go under it. Camera API since 3.2 covers Media Foundation, AVFoundation, V4L2 and getUserMedia. |
| ANGLE | Accepted, for GL ES 3.0 on Windows and macOS | Three browser vendors maintain it. One shader language end to end. Same code path as WebGL2 in the browser. Reliable on Intel integrated GPUs where vendor GL is not. Exposes vblank timestamps on its D3D11 backend. Binaries, not a source build (decided 2026-10-06): CI takes ANGLE from an Electron release zip, checked by checksum; development machines use whatever Electron copy is present. A source build at a pinned commit (depot_tools, about 10 GB) waits until an ANGLE bug or a missing feature needs a patch. Each measurement names the ANGLE version it ran on, and program binaries are keyed per ANGLE build. |
| miniaudio | Accepted, for audio devices | Single file, public domain, WASAPI shared and exclusive, CoreAudio, ALSA, PulseAudio, Web Audio. Same implementation-macro mechanic as `ysp/rt.h`. |
| Emscripten | Accepted, for the web player | Required by SDL3's web backend. `ysp/rt.h` already has the branch. |
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
| nanovg | Rejected | Vector UI drawing with a fixed anti-aliasing fringe and its own font atlas. A stimulus edge is a parameter; see the signed-distance shapes in `ysp/gfx.h`. Unmaintained since 2019. |
| wasm3 | Deferred | Hosts modules from any language in the native player if someone needs Rust or C++ logic. |
| minicoro, tina | Deferred | Native stackful coroutines. Luau's VM-level coroutines cover the scripting model with no native stack switch, so they run in the web player and under the sanitizers. Only the eject path could want native coroutines for sequential C. If that demand appears, minicoro, player only, never in a header, and not in the web player. |
| Tracy | Accepted, opt-in, through a macro layer in `ysp/rt.h` | The headers see only C macros that compile to nothing by default. Tracy's C++ client is compiled into the player or the user's program. On-demand mode in a deployed player. |

## 4. Headers

### 4.1 ysp/rt.h additions

- **Event ring.** A wait-free fixed-record ring that any thread pushes
  into without a lock or an allocation, drained by a normal thread. The
  record is a timestamp, a source, a kind and a small payload. The
  screen, audio and video headers push their flip, onset and frame
  records into a ring the caller supplies. Export format is the
  caller's business; examples show a CSV drain.
- **Clock correlation.** A call that correlates the `ysp/rt.h` clock
  with another clock read back to back, for SDL's tick clock, the
  Windows interrupt-time clock and audio device positions.
- **Instrumentation macros.** Zone, frame mark, plot and message
  macros. By default they compile to nothing. With `YRT_TRACY` they
  map to Tracy's C API. With `YRT_TRACE_RING` they write zones to
  the event ring, so a rig without Tracy still records them. Every
  header instruments its hot paths through these macros: the frame
  loop phases, the pump callbacks, the audio callback, the decode-ahead,
  the serial reader. Frame marks go at the flip. Plots carry the flip
  residual, the audio fill level and the decode-ahead depth. Messages
  mirror trial and event boundaries. A message with the `ysp/rt.h` time
  at each frame mark correlates Tracy's clock with the rig's. A thread
  that will run a real-time callback emits one zone at open to
  allocate its queue before it matters.

### 4.2 ysp/screen.h (`yscr_`, `YSCR_`)

Needs SDL3. Owns the window, the GL ES 3.0 context, the display mode,
the swap path and the input restamping.

- **Context.** EGL through ANGLE on Windows and macOS, the native driver
  on Linux, WebGL2 under Emscripten. Confirm SDL3's EGL path on Cocoa
  with ANGLE's Metal backend before the first macOS build. On Windows,
  SDL3 owns only the window: the header makes the D3D11 device and the
  DXGI swapchain and gives ANGLE a client-buffer pbuffer on the back
  buffer, because SDL's EGL path gets ANGLE's blt-model window surface,
  which has no vblank statistics and no frame latency control
  ([screen.md](screen.md) has the probe numbers).
- **Display modes.** Enumerate the chosen display's modes. Offer "the
  mode whose refresh is an integer multiple of this rate" for video and
  report what was obtained.
- **Flip at a time.** `yscr_flip_at(t)` presents the drawn frame at
  target time `t` on the `ysp/rt.h` clock. It does not wait for the flip:
  the record of the flip, with the estimated onset, the residual and a
  dropped-frame count, arrives with the next frame's begin call and in the
  event ring, and `yscr_wait_flip()` blocks for it as Psychtoolbox's
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
- **Input.** SDL events restamped onto the `ysp/rt.h` clock. The manual
  states that keyboard and mouse timestamps are at best
  millisecond-granular. On the Windows message path they count in the
  system tick: posted key events were stamped up to 15.4 ms early
  (docs/screen.md). The manual points at `ysp/serial.h` response
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
  untimed screen is a `ysp/screen.h` handle opened with a kind that
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

### 4.3 ysp/gfx.h (`ygfx_`, `YGFX_`)

Includes `ysp/screen.h`. GL ES 3.0 only. No compute stage.

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
  Artwork goes through the pack tool as Slug paths, the same form as
  glyph outlines (5.2). Changed 2026-10-06: "as a distance texture
  (MASK_TEX, MSDF or MTSDF)".
- **Output stage.** Gamma, CLUT, dithering for 10-bit and Bits#-style
  modes, in the final shader. SDL3 has no gamma ramps, so this is the
  only place gamma lives. Stereo on one display is an output mode
  here: side by side, interlaced, or frame-sequential with shutter
  glasses at 120 Hz. Quad-buffer stereo is a workstation GL feature
  that ES 3.0 does not have.
- **Calibration.** Measured primaries and gamma from a photometer become
  a 3x3 matrix into cone space (Stockman-Sharpe fundamentals), DKL and
  cone contrast, and a table from linear to device values. Changed
  2026-10-06: the calibration and the color math are `ysp/color.h`'s
  (`ycol_`, pure computation), which `ysp/gfx.h` includes from v0.5. It
  is a color header for calibrated displays, not a general color library:
  every conversion goes through a calibration, a color outside the gamut
  comes back unclipped with its distance, and gamut questions are calls of
  their own. No ICC profiles, appearance models or color-difference
  formulas (docs/color.md). Was: "No general color library".
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
  stated encoding, and texture import for `ysp/video.h`'s GPU paths;
  instanced stimuli (a template and up to 16384 elements a frame, with a
  per-element hit test); draws reordered across kinds where they do not
  overlap, with identical pixels.
- **Added in v0.6.0** (2026-10-06): curve sets (the Slug format, version 1,
  that the pack tool's outline builder writes) and curve runs (glyphs and
  artwork layers as instanced items, exact area on resolved glyphs, rays
  elsewhere); a separable Gaussian blur pass for soft shadows, glow and
  blurred-letter stimuli (R16F, RGBA16F, RGBA32F, opt-in 2x). The pack no
  longer produces MSDF atlases; the MSDF path stays.
- **Added in v0.7.0** (2026-10-06): a rays-only option for pages of small
  text; static text cached in a target with the drawn run's coverage, bit
  for bit; Gaussian NOISE from a quantile table with an exact CPU twin;
  specialized vector programs for large rounded rects, dashed circles and
  circle compounds.
- **Added in v0.8.0** (2026-10-07): setup passes (targets rendered outside a
  screen frame, at setup); the rays program picked by itself for turned
  curve runs.
- **Changed in v0.9.0** (2026-10-07): the noise hash (triple32, one hash
  of a point's index under seed keys); every noise and dither pattern
  changed.
- **Added in v0.10.0** (2026-10-07): gradient noise (3D simplex fBm) with
  an exact CPU twin; not band-limited (filtered noise or gratings give a
  stated band).

### 4.4 ysp/audio.h (`yau_`, `YAU_`)

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
- **Schedule.** `yau_play_at(buffer, t)` on the `ysp/rt.h` clock. The
  callback counts frames from an anchor correlated to the clock and
  starts the buffer on the right frame.
- **Master clock.** The `ysp/rt.h` clock is the master. The header fits
  the device clock against it and plans every onset in device frames from
  the fit. It never resamples and never inserts frames. A long buffer's
  end follows the device clock and its record says where it ended.
  Changed 2026-10-05: the first plan nudged the stream by resampling or
  frame insertion, against principle 5. Locking a movie's soundtrack to
  its video goes to `ysp/video.h`, which follows the audio fit with the
  movie base.
- **Synthesis.** Tones, noise, envelopes, at the project rate.
- **Latency.** Reported only as measured by the line-out to line-in
  loopback test.
- **Per-onset record** into the event ring.
- **Streams.** Added 2026-10-06 (v0.2.0): a ring-fed voice that is
  played, planned, recorded and confirmed as a buffer is. Its sample s
  plays on stream frame origin + s. A late start or a ring underrun skips
  samples and does not move origin. A movie's soundtrack is a stream, so
  movie sound takes the same path as every other sound, and `ysp/video.h`
  follows origin. WAV files (RIFF, RF64, BW64) stream through it.

### 4.5 ysp/timeline.h (`ytl_`, `YTL_`)

Includes nothing. Caller-sized fixed arrays, no heap.

- **Events.** Onset, offset, trigger, at a time on a chosen time base:
  the `ysp/rt.h` clock, a trial clock, or a movie clock.
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
- Composes with `ysp/trials.h`: the trial handler picks the condition,
  the timeline runs it. At movie scale the arrays hold thousands of
  annotations.

#### 4.5.1 C authoring API

Status: built in `ysp/timeline.h` v0.4.0 (2026-10-06), revised from the
2026-10-04 draft after a review (docs/timeline.md, "Sequences"). The
draft lowered each `ytl_to` to `ytl_tween()`. A channel holds one
waiting tween, so the draft trial below lost its first ramp, and a
re-anchor did not replay tweens. The goal is for C code to be about as
short as a JS animation library, GSAP or anime.js, with the timeline's
timing unchanged. The builder is in the same header. A C trial and a Luau
trial read alike (section 6.1).

The trial from the `ysp/timeline.h` USAGE section, in GSAP:

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
ytl_seq q = ytl_seq_on(&tl, TRIAL);          /* cursor at base time 0 */
ytl_on(&q, FIX);
ytl_wait(&q, YTL_MS(500));
ytl_off(&q, FIX);
ytl_on(&q, GRATING);
ytl_trigger(&q, 12);
ytl_to(&q, CONTRAST, &(ytl_tween_desc){
    .to = 0.5f, .duration = YTL_MS(100), .ease = YTL_EASE_COSINE });
ytl_wait(&q, YTL_MS(600));
ytl_then(&q, CONTRAST, &(ytl_tween_desc){
    .to = 0.0f, .duration = YTL_MS(100), .ease = YTL_EASE_COSINE });
ytl_off(&q, GRATING);
if (q.err < 0) report(ytl_strerror(q.err), q.err_call);
```

The rules:
- **A cursor.** `ytl_seq` holds a base and a cursor in base time.
  `ytl_seq_on()` puts the cursor at 0. `ytl_seq_now()` puts it at the
  base's "now", the time a tween desc uses when `start_set` is false.
  `ytl_wait(&q, dt)` moves the cursor by `dt`, and `ytl_at(&q, t)`
  sets it. `q.t` is the cursor: store it in a variable to use it as a
  label.
- **Events and tweens at the cursor.** `ytl_on`, `ytl_off`,
  `ytl_set_value`, `ytl_trigger` and `ytl_mark(&q, code, user)` add
  an event at the cursor. `ytl_to` starts a tween at the cursor plus the
  desc's `delay`, and the cursor does not move. That is the script's
  non-blocking `tween()`. A desc with `start_set` is refused: the cursor
  is the start.
- **One keyed track per channel.** A sequence's tweens on a channel
  become one keyed track in the key arena, which grows with each call.
  The track waits for its first start and then takes over from the old
  driver, as `ytl_tween()` does. After that it is a function of base
  time, so re-anchoring a trial replays it. A tween that starts inside
  the channel's previous one is refused, and so is `YTL_FOREVER`.
- **Sequencing.** `ytl_then` is `ytl_to` and then a wait to the
  tween's end, which is the default placement of GSAP's `.to()`.
  `ytl_to` returns the tween's end time, so
  `ytl_at(&q, ytl_to(...))` does the same thing explicitly.
- **A sticky error, as in a stream.** The first failing call stores its
  code in `q.err` and its index in `q.err_call`. Every call after that
  does nothing. So a sequence needs no check after each line, only one
  at the end. That is the C analog of a chained JS call. The builder is
  not atomic: the calls before the error stay. `ytl_seq_cancel(&q)`
  removes what the sequence added.
- **Keyframes and stagger:** `ytl_keyframes(&q, ch, values, offsets, n,
  ease)` joins the channel's track in the arena, and
  `ytl_on_n(&q, chans, n, stagger)` adds onsets all or none.
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
Thin macros over designated initializers build the ops. `ytl_run()`
gives each op to the same builder calls, in order, with a sticky error:

```c
static const ytl_op trial[] = {
    YTL_ON(FIX),
    YTL_WAIT(YTL_MS(500)),
    YTL_OFF(FIX), YTL_ON(GRATING), YTL_TRIGGER(12),
    YTL_TO(CONTRAST, .to = 0.5f, .duration = YTL_MS(100), .ease = YTL_EASE_COSINE),
    YTL_WAIT(YTL_MS(600)),
    YTL_THEN(CONTRAST, .to = 0.0f, .duration = YTL_MS(100), .ease = YTL_EASE_COSINE),
    YTL_OFF(GRATING),
};

ytl_seq q = ytl_seq_on(&tl, TRIAL);
ytl_run(&q, trial, YTL_COUNT(trial));
if (q.err < 0) report(ytl_strerror(q.err), q.err_call);   /* the op's index */
```

An op is a tagged struct:

```c
typedef struct ytl_op {
    int              op;      /* YTL_OP_ON, _OFF, _SET, _TRIGGER, _MARK,
                               * _WAIT, _AT, _TO, _THEN                      */
    int              ch;
    int64_t          t;       /* WAIT: dt; AT: base time                    */
    int32_t          code;    /* TRIGGER, MARK                              */
    float            value;   /* SET_VALUE                                  */
    uint64_t         user;    /* MARK                                       */
    ytl_tween_desc tween;   /* TO, THEN                                   */
} ytl_op;                   /* 96 bytes                                   */

#define YTL_ON(c)      { .op = YTL_OP_ON, .ch = (c) }
#define YTL_WAIT(dt)     { .op = YTL_OP_WAIT, .t = (dt) }
#define YTL_TO(c, ...)   { .op = YTL_OP_TO, .ch = (c), .tween = { __VA_ARGS__ } }
```

Why this form, and not only the builder:
- **It is data.** The table can be `static const`, so it can live in
  read-only memory. It can also be logged and compared with a diff. It
  has the shape of the pack's experiment definition (principle 6), so
  the eject path emits it directly, and a table from the pack can be
  given to `ytl_run()` without a compile step.
- **No new semantics.** `ytl_run()` is a loop over the builder calls,
  so the two forms mix freely in one seq. A static table can set up a
  trial, and builder calls can then add the computed parts, such as a
  staircase level.
- **Precedent.** Modern C single-header libraries use this style: sokol
  passes descs as compound literals, and the Clay layout library builds
  whole UIs from macros over designated initializers.

Measured (a stand-in for `ytl_op` with the real `ytl_tween_desc`,
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
  `ytl_tween_desc`. C does not require this.
- C++17 users use the builder.

The header's own compile checks are `tests/compile/timeline.c` (the
table as C) and `tests/compile/timeline_ops.cpp` (as C++20, with
`-Wno-missing-field-initializers` on g++; g++ 16.1 still warns there).

Decisions (2026-10-05):
- **`ytl_on` and `ytl_off`, not show and hide.** The header does not
  know what a channel means. The script layer keeps `show` and `hide`
  for stimuli, and they lower to on and off. The ops are `YTL_ON` and
  `YTL_OFF`, as in the examples above.
- **Audio is not frame-quantized.** An on or off on a channel that the
  player maps to a sound goes to `yau_play_at()` at the event's own
  time, sample-accurate. It must also be handed over before that time,
  because the audio path needs lead time. An evaluate fires an event on
  the frame that shows it. That is too late and too coarse for sound. So
  the timeline needs a look-ahead query: give the pending events of a
  base whose RT time falls in a window, without firing them:
  `ytl_peek(tl, base, from_rt, to_rt, out, cap)`, built in
  ysp/timeline.h v0.3.0 (2026-10-05). The
  player's audio path schedules from it, and the video path keeps
  evaluate. The event's landing record stays the video frame's. The
  audio onset record comes from `ysp/audio.h` (section 4.4). Which base
  and channel kinds are audio is the player's choice, not the header's.
- **The key arena is in the timeline handle.** `desc.keys` and
  `desc.key_capacity` give caller-owned storage, as `desc.events` does,
  with no default (NULL: a sequence tween is `YTL_ERR_FULL`). Each
  tweened channel has one block, and the channel stores an offset, not a
  pointer. A replaced block, or one whose base `ytl_clear()` cleared, is
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
- **`ytl_run()` checks every table again.** A table that the designer
  checked at import can still be wrong in the player: the player can be
  a different version from the designer, an extension that an op needs
  may not be installed, the parameter tables and channel counts may have
  changed, or the rig's display or audio rate may make an op invalid,
  for example a flicker rate. The check runs once per run, not per frame.
  Binding is checked against the first earlier op naming the channel,
  O(n^2) compares with no scratch table. It is all or nothing, as
  `ytl_add_n()` is: the whole table is checked before the first op is
  applied, so a bad op at index 37 does not leave half a trial.
  `ytl_check_ops(tl, base, ops, n, &bad)` does the same check without
  applying, so the player can reject a pack when it loads, before the
  session starts, not at trial 190.

Open questions:
- How far ahead must `ytl_peek()` look for audio? That is the audio
  path's measured scheduling lead plus one frame. ysp/audio.h v0.1.0
  measured the shortest lead with no late onset at 43.5 to 54.5 ms
  (WASAPI shared, 10 ms periods, this laptop; docs/audio.md). The
  line-in loopback test is still needed for the output latency itself.
- Answered (v0.4.0): the arena has no default. An inline arena would grow
  every handle, and the handle is already 64 KB.

### 4.6 ysp/video.h (`yvid_`, `YVID_`)

Includes `ysp/screen.h`. Uses SDL3 for capture.

- **Scheduler.** At each flip, pick the frame whose presentation
  timestamp is due against the `ysp/rt.h` clock. Drop or repeat as
  needed. Never count frames. Log every decision.
- **Decode-ahead** on the `ysp/rt.h` pump, N frames ahead, texture
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
- **Capture.** SDL3 camera frames restamped onto the `ysp/rt.h` clock,
  with the current trial and event attached, written to the frame
  sequence container or to an opt-in encoder (minih264 with minimp4,
  Media Foundation sink writer, AVFoundation asset writer). Encoding
  runs on the pump. The manual states that the camera timestamp is
  delivery time, not exposure time, and that the LED loopback measures
  the offset.
- **Per-frame record** into the event ring: movie timestamp, flip time,
  decision.

### 4.7 ysp/net.h (`ynet_`, `YNET_`), later

UDP triggers and clock-offset estimation between two machines. Lab
Streaming Layer markers through liblsl as an opt-in backend.

### 4.8 Not planned

- HMD virtual reality. An OpenXR runtime owns the compositor: the
  application asks for a predicted display time, renders for it and
  hands the frame back, and the runtime reprojects, drops or repeats
  as it decides. Present-at-time does not exist there and no timing
  claim can be the header's. If a `ysp/xr.h` ever appears, OpenXR is
  the route and `ysp/timeline.h` carries over unchanged, because
  evaluate-at-predicted-onset is the OpenXR frame loop. `ysp/screen.h`,
  the swap paths, stereo rendering, tracking and controllers do not
  carry over and are a separate project.
- Offline psychometric fitting. R, Python and MATLAB do it better and
  analysis happens there.
- A logging library. The event ring in `ysp/rt.h` is the primitive.
- A general random-number header. `ysp/trials.h` owns the only draw.

Removed from this list 2026-10-06: "a standalone color header". It is
`ysp/color.h` (4.3, Calibration).

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
| Font | The font file, plus the glyph outlines built from it in the Slug form (quadratic curves and bands; cubic outlines converted within a stated tolerance), and alpha coverage at the sizes the experiment uses for value-exact text. Static text is stored as laid-out glyph runs. | No platform font engine. One pinned Skribidi does layout, bidirectional text and shaping in the designer (WebAssembly, with editing), the pack tool and the player, so text is the same everywhere. Its sources are vendored at a commit with named local patches (a bidi run-merge fix, docs/layout_probe.md; rule L1 per line, docs/layout.md), built without FetchContent; HarfBuzz is pinned by tag separately; the pack manifest records the Skribidi commit, the patch hashes and the HarfBuzz version. In the pack tool HarfBuzz is also the outline source for variable-font instances and CFF2, which ysp/outline.h refuses; its curve sets equal ysp/outline.h's on static fonts. Compared and not taken (2026-10-07, docs/layout_probe.md): kb_text_shape (no paragraph layout or editing, unsafe on untrusted fonts), HarfBuzz's hb-gpu renderer (1 to 3 times the rays' edge error unturned, 24 to 51 times turned at 200 px), and Rust layout engines (no Rust dependency). Scalable text is drawn from the outlines by the Slug algorithm (Lengyel, JCGT 2017; patent dedicated to the public domain 2026-03-17; reference shaders need attribution). A whole font fits in the pack (about 65 MB for a CJK font, against 212 to 762 MB as MSDF atlases), and a glyph missing from it is built from the font's outline in 20 to 120 us, resolved (ysp/outline.h, docs/outline.md; the probe's 17 to 66 us did not remove overlaps), so the player rasterizes nothing at run time. Value-exact alpha coverage comes from an exact-area rasterizer in the pack tool, not Skribidi's (max error 0.24 to 0.32 of full scale), and is drawn on the pixel grid only. Effects: outlines and faux bold by stroke expansion of the curves, filled the same way; hard shadows by an offset copy; soft shadows, glow and blurred text by a Gaussian blur pass on a render target (numbers in docs/text_probe.md). Headers only draw glyph runs. Changed 2026-10-06: MSDF atlases dropped from the pack, after a probe on the Iris Xe (docs/text_probe.md; MSDF failed on dense CJK and overlapping contours, and a distance soft edge is a true blur only on straight edges). Changed 2026-10-05: "glyph atlas plus metrics; no font engine at runtime". |
| Artwork | Filled paths in the Slug form, one solid color per layer, with the fill rule; strokes expanded to fills in the tool | The tool reads a subset of SVG: paths, basic shapes, transforms, solid fills and strokes. Filters, text, CSS, masks and gradients are refused by name, not ignored. Decided 2026-10-06, replacing MSDF artwork; the subset grows only when an experiment needs it. |
| Shader | The user's fragment body wrapped in the contract (`ygfx_shader_wrap()`, the same function in the tool and the runtime), as GLSL ES 3.00 text, with reflection | glslang validates it in the tool. GL ES 3.0 has no portable binary, so ANGLE compiles the text on the rig when the pack loads; `ysp/gfx.h` caches each program's binary per ANGLE build, adapter and driver (`desc.cache`, v0.4.0), checked by its own hash before the driver sees it. SPIRV-Cross is for a second backend only. Changed 2026-10-05: "D3D bytecode is finished by the runner" (docs/gfx.md). |
| Table | Typed binary columns from condition files and from ELAN, Praat or BIDS events exports | Read directly by `ysp/trials.h` and `ysp/timeline.h`. The form is `ysp/table.h`'s PSTB block (v0.1.0, 2026-10-07; docs/table.md): position independent, hashed, int32, double or string columns with levels; the CSV parser in the header builds the same bytes the pack tool stores. |
| Calibration | The 3x3 matrix and the gamma table, with the photometer readings beside them | `ysp/color.h`'s `.yspcal` file (62528 bytes, CRC-32; the same bytes as `ysp/gfx.h` v0.4's), rebuilt from its readings on load. A participant's luminance from flicker photometry is session data, not pack data: `ysp/color.h`'s `.ysplum` file (changed 2026-10-06). |
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
    clock. Time comes from the `ysp/rt.h` clock through the API and
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
    targets a frame late (docs/timeline.md). A wait that must never
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
`ysp/timeline.h`. Every call lowers to timeline events, tracks and
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
   and `g.sf` are the names in `ysp/gfx.h`'s table for the stimulus type,
   with the table's units. The Luau type checker knows them, so a wrong
   name fails in the designer.
5. **One driver per channel is the player's business.** Each stimulus's
   `visible` is driven by events, so each onset and offset has a record
   of the frame it landed on. Every other property is driven by a track.
   Assigning to it (`g.contrast = 0.3`) is a tween of length 0 at
   `now()`. A script never sees `YTL_ERR_BOUND`.
6. **Reading a property gives the value of the last evaluated frame.**
   This is what the participant saw on that frame. It is deterministic
   under replay.
7. **The script has no per-tween callbacks.** `await` replaces
   `onComplete`. A track or a sampled table replaces `onUpdate`. The
   escape hatch is `every_frame(fn)`, which has a stated time budget
   (section 6).

#### Examples

The trial from the `ysp/timeline.h` USAGE section:

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
`input.button{...}` and serial lines through `ysp/serial.h`.

#### Lowering

Each call is one or more `ysp/timeline.h` calls on the trial base. Times
go to nanoseconds by `ytl_ns()`, rounded to nearest.

| Script | Timeline |
|---|---|
| `show(s)` / `hide(s)` | `ytl_add` ONSET / OFFSET on `s.visible` |
| `trigger(code)` | `ytl_add` TRIGGER, `code` |
| `mark(name, v)` | `ytl_add` MARK, `code` = name id, `user` = `v` |
| `wait(dt)` | A scheduler entry at the target time, resumed by the window rule of the timeline; logged as a MARK |
| `tween(...)` | `ytl_tween` per channel, with a `ytl_tween_desc` |
| keyframes `{a, b, c}` | `ytl_keyframes` on a sequence, with the keys in the timeline's key arena (`desc.keys`, section 4.5.1) |
| `stagger` | The player computes each target's `start`; the timeline sees ordinary tweens and events |
| `opts.repeat`, `opts.yoyo` | `ytl_set_track` keyed, with `period` and `repeats`; `yoyo` adds a key back to the start |
| `follow(...)` | `ytl_set_track` sampled, with the table's rate from the manifest |
| `flicker(...)` | `ytl_set_track` with two STEP keys and a repeat |
| `s.prop = v` | `ytl_tween` with length 0 |
| reading `s.prop` | `ytl_value` |

#### C additions this needs

- Done in v0.2.0: `ytl_tween()` takes a `ytl_tween_desc` with
  `from`, `start` or `delay`, `cycles`, `yoyo` and `keep_velocity`, and
  takes over from the old track's value at its start.
- Done in v0.4.0: `ytl_window(tl, base, &frame, &end)` gives the base
  time that a frame's window reaches, from the same function the evaluate
  uses. The scheduler resumes a wait on the frame where `target <= end`,
  so the player does not copy the rule. `ytl_rt_time()` gives the RT
  time of a base time, to lower `now()` to an RT change point.
- Dropped (v0.4.0): constructors `ytl_onset()`, `ytl_trigger()`,
  `ytl_keys()`, `ytl_samples()`. The sequence builder of section 4.5.1
  and designated initializers serve the same users, and the builder now
  owns the name `ytl_trigger`.

#### Left out, and why

- **Easings that overshoot** (back, elastic, bounce): an overshoot of a
  stimulus value is a confound. `bezier()` with y outside [0, 1] stays
  possible on purpose.
- **Time scale, reverse, seek inside a trial:** a rate on the trial base
  scales every wait, tween and duration of the trial, which is a
  confound. `ytl_rate()` (v0.4.0) exists for a movie base: a script sets
  it through the movie (`m.rate`), never on the trial base. Reverse is
  refused at every level (docs/timeline.md). A movie seek is the video
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
  the `ysp/gfx.h` calibration, with the channels as the coordinates of
  that space? Partly answered 2026-10-06: `ysp/color.h` converts a DKL or
  cone-contrast color to a stimulus direction (`ycol_to_dir()`) through
  the context `ysp/gfx.h` uses (`ygfx_color()`), at 22 to 27 ns a call
  (MSVC; 79 ns MinGW), so three channels bound to a color in a space cost
  about 0.3 ms of CPU for 10000 elements a frame. Open: the binding form
  (three channels to one color field) and whether it belongs in
  `ygfx_bind`.
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

- The `ysp/rt.h` clock is the master for display, audio, video,
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

- `tests/loopback/` gains a photodiode test for `ysp/screen.h`, a
  line-out to line-in test for `ysp/audio.h`, an LED test for capture,
  a variable-refresh interval sweep and a luminance-versus-interval
  sweep. State on 2026-10-06: the photodiode test
  (`screen_loopback`) and the line-in mode of the audio loopback
  (`--line`) are built and have never run. Every onset measured so far
  is a software timestamp.
- `tests/compile/` gains the C and C++ pairs for each header, and a
  no-ANGLE build for Linux.
- CI on Windows takes ANGLE from the Electron release zip named in
  `ci.yml` (Electron v38.8.6, ANGLE 2.1.25848), checked by checksum and
  cached. Linux uses Mesa's EGL. macOS CI has no GL, so the ysp_gfx
  pixel checks skip there; where macOS gets ANGLE is not decided.
- No number outside a STATUS block is a measurement, as today.

## 11. Order of work

Revised 2026-10-06. The first order put Linux X11 with the photodiode
test first and Windows 10 second. The work went to Windows 11 first, on
the one development machine, and Windows 10 became best effort
(section 9).

### State on 2026-10-07

| Step | State |
|---|---|
| 1. `ysp/rt.h`: event ring, clock correlation | Done, v0.5.0, with the instrumentation macros. |
| 2. `ysp/timeline.h` | v0.4.0: events, tracks, tweens, `ytl_skip`, `ytl_lead`, `ytl_peek`, exact rational base rates (`ytl_rate`), `ytl_window`, `ytl_rt_time`, the `ytl_seq` builder with op tables, `ytl_run`, `ytl_check_ops` and the key arena (4.5.1). |
| 3. `ysp/screen.h` | v0.3.2 on Windows: DXGI_FLIP and COMPOSITION, flip hooks (codes, triggers, after-flip callbacks), the OS gamma ramp, the Shift+Esc abort and the triple-press panic, the window icon, text input for a text box with a ring record and a flip flag while keys are off the raw path, D3D11 video support on the device. X11, Wayland, macOS and web are stubs. No photodiode run. |
| 4. `ysp/audio.h` | v0.2.0 on WASAPI shared mode: onsets from a fit of device positions, tier 2. Streams (ring-fed voices, the soundtrack path) and WAV streaming. The other backends compile and have never run. No line-in run. |
| 5. `ysp/gfx.h` | v0.10.0: stimuli, the signed-distance vector program and kind-specialized programs, strokes, sprites, groups, render targets and setup passes, curve runs from Slug-form curve sets (exact area on resolved unturned glyphs, rays elsewhere and chosen automatically for turned runs), the blur pass, NOISE (uniform, Gaussian, integer simplex fBm, each with a bit-exact CPU twin; v0.9 replaced the nested noise hash, which repeated rows), the output stage; a program binary cache, NV12 and I420 video in linear light, texture import (GL, D3D11 through ANGLE), instanced stimuli, and draws reordered where they do not overlap. It requires `ysp/color.h` (v0.1.0). Text layout (Skribidi) belongs to the pack tool and the player (5.2), not to the header. Missed bars: full text pages (docs/gfx.md; a page drawn once into a target costs 0.5 ms a frame), the text program's cold compile, and some of v0.3's GPU bars. |
| 6. `ysp/video.h` | v0.2.0: scheduler, frame sequence, pl_mpeg, decode-ahead; Media Foundation (DXVA by default), planar upload to the gfx shader by default, the shared D3D11 GPU path as an option, the soundtrack on `ysp/audio.h` streams, base rates. Not built: AVFoundation, capture. |
| Also built | `ysp/outline.h` v0.2.1 (item 3), `ysp/rdk.h` v0.1.0 (random dot kinematograms, integer state, Python binding), `ysp/table.h` v0.1.0 and `ysp/trials.h` v0.2.0 (CSV and pack tables, order lists, sampling, units, transition balance, rules text). |
| 7 to 10 | Not started. |

### Next, in order

1. **Hardware verification on Windows 11.** Run the photodiode test on
   DXGI_FLIP and COMPOSITION, and the line-in test for `ysp/audio.h`.
   Re-measure the "one vblank early" flips of the windowed DXGI runs,
   which the `flip_at` fix of 2026-10-05 may have removed. Every later
   step builds on onset claims that are software timestamps until this
   step is done. It needs the photodiode and a line-out to line-in
   cable on the development machine.
2. **`ysp/gfx.h`**: done for now (v0.10.0). Slug curve runs, the blur
   pass, setup passes, kind-specialized programs and gradient noise are
   built; sampler-filtered chroma was measured and deleted. The open
   items are in docs/gfx.md "Next".
3. **The outline builder**: curves and bands in the Slug form from font
   outlines and paths, stroke expansion, the exact-area alpha
   rasterizer. It is the core of the pack tool's text and artwork
   (item 6) and runs in parallel with item 2. State on 2026-10-07:
   `ysp/outline.h` v0.2.1 (docs/outline.md): a bounded TrueType and
   CFF reader that places glyphs as HarfBuzz does, overlap removal (curve sets carry the RESOLVED flag),
   strokes without the probe's fold, exact alpha, curve sets in format
   v1, and the SVG subset reader of 5.2 (Artwork). Missed bars: a Latin
   stroke (289 to 303 us a glyph against 200; CJK meets its 1 ms) and the
   sets of the heaviest fonts (Source Han Sans 94 to 103 us a glyph
   against 66).
4. **`ysp/timeline.h`**: done for now (v0.4.0: base rates,
   `ytl_window()`, sequences). `ysp/video.h` applies the rates for any
   rate other than 1.
5. **Other platforms.** `ysp/screen.h` on Linux X11 (GLX sync control)
   and Wayland (presentation-time), then macOS through ANGLE's Metal
   backend. `ysp/audio.h` on ALSA, PulseAudio and CoreAudio. A platform
   gets its tier from a photodiode or line-in run on that platform, not
   from CI.
6. The pack tool as a library, and its CLI, with Skribidi, the outline
   builder (item 3) and the SVG subset reader (5.2, Artwork). State on
   2026-10-07: the first piece of its text half is built
   (docs/layout.md). Skribidi `dee63d6` with two local bidi patches (run merge; rule L1 per line, 2026-10-08), HarfBuzz
   14.6.0, SheenBidi, libunibreak 6.1 and budouxc are vendored in
   `third_party/` (7.7 MB, verified byte for byte by
   `tools/vendor_layout.py`); `pack/layout` v0.1.0 lays text out into
   curve-run items over on-demand curve sets and stamps each block with
   the versions and font hashes; it gives Chromium's glyph ids on all 15
   corpus lines (`tests/layout/`), on MSVC, MinGW and emcc.
   `examples/layout/gfx_layout.c` shows paragraphs in seven scripts, the bidi bug
   strings, BudouX breaking and Skribidi's editor (on `ysp/screen.h`
   v0.3.2's `yscr_text_input()`). Not started: the pack format, the
   CLI, serialized glyph runs.
7. The player, native and WebAssembly, and the runner protocol.
8. The designer.
9. Build jobs, `ysp/net.h`.

Items 2 and 3 run in parallel. Measurements on the development
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
| Video decoder | The interface in `ysp/video.h`. | A codec the built-in backends lack. |
| Script module | Exports a parameter table and typed functions. The host generates the Luau binding from the table. | Any of the above exposing parameters. |

- **Mechanics.** A shared module with one exported symbol that returns
  the vtable and its interface version, compiled by its author against
  the ysp C headers with any toolchain. The ABI is plain C structs. On
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
  for D3D11 (2026-10-05, docs/gfx.md): RGBA 10/10/10/2 and RGBA16F
  configurations exist, and a client-buffer pbuffer on an
  `R10G10B10A2` texture works. Metal: not run.
- Composition swapchain: how a target time between two vblanks is
  quantized, how far ahead the queue accepts presents, and whether a
  borderless fullscreen composition window gets independent flip on the
  lab GPU and driver. Answered on the Iris Xe (2026-10-05,
  docs/screen.md): the frame goes to the nearest vblank, a tie to
  the earlier one; the queue accepts at least 7 presents ahead; a
  fullscreen COMPOSITION window got independent flip. The lab GPU and
  driver: not run.
- Whether an untimed screen on a second display disturbs a timed
  display in independent flip on the same GPU (4.2, Untimed screens).
  One-display result (docs/imgui_probe.md, 2026-10-07): an ImGui console
  window on its own device and swapchain, presented with DO_NOT_WAIT,
  left the timed window's flip records unchanged in windowed
  COMPOSITION; but a busy console under a fullscreen timed window gave
  25 presentation-path changes and 107 composed frames in 2 minutes
  (none late). The two-display run must count path changes and
  composed frames, not only drops.
- Whether SDL3 opens a window and a GL context on an X screen other
  than the default (`:0.1`).
- Whether the composition swapchain's present-at-time removes the need
  for the spin-wait under variable refresh.
- miniaudio's exclusive-mode behavior per backend and the exact flag
  set that disables its silent rate conversion. Answered for WASAPI
  (2026-10-05, docs/audio.md): ask for no format, rate or channel
  count, so the client format is the device's; exclusive mode opens at
  the endpoint's Default Format. Other backends: not run.
- Whether SDL_GPU gains a browser backend, which would make it a
  candidate second implementation behind the `ysp/gfx.h` interface.
- The first stimulus that needs compute, which would move the plan to
  WebGPU through Dawn.

## 14. Coverage of jsPsych

Status: reference, 2026-10-07. This section maps each jsPsych plugin and
extension, and the jsPsych timeline model, to the ysp headers and to the
player of section 6. It lists what is missing and ranks the gaps.

Sources, read on 2026-10-07:
- jsPsych `main` at `3e24c16` (2026-09-16), core 8.3.0: `packages/plugin-*`,
  `packages/extension-*`, `docs/overview/` (timeline, plugins, data, events,
  experiment options, timing accuracy), `docs/plugins/`, `docs/extensions/`,
  `docs/reference/` (core, randomization, pluginAPI).
- jspsych-contrib `main` at `f5f3d15` (2026-09-16): package names and
  `package.json` descriptions. Parameters read for `plugin-rdk` only.
- jspsych-psychophysics (Kuroki, third party, MIT): parameter and object
  property names only.
- ysp: README.md, this document (sections 4 to 8, 11, 12), and the
  manuals of `ysp/trials.h`, `ysp/screen.h` (INPUT, ABORT), `ysp/gfx.h`
  (API list, glyph and curve runs, hit tests, units), `ysp/audio.h` and
  `ysp/video.h` (API lists), `ysp/rdk.h` (desc), `ysp/outline.h` (font
  metrics).

### 14.1 What ysp gives a jsPsych-style trial today

| Need | ysp today | Limit |
|---|---|---|
| Show a stimulus at a time, hide it after a duration | `ysp/timeline.h` onset and offset events (`ytl_seq`, op tables), bound to `ysp/gfx.h` stimuli (`ygfx_bind`); each event has a landing record (frame, residual) | None for shapes, gratings, gabors, dots, noise, images, video |
| Key response | `yscr_poll()`: SDL events restamped on the `ysp/rt.h` clock; raw keyboard path on Windows. `ysp/response.h` v0.1.0 (2026-10-07): choices, a window bound to the flip onset, minimum RT, release, held keys, double-report removal, RT with the onset's tier (`examples/response/trial_keyboard.c`) | Keyboards are ms-grade at best |
| RT-grade response | `ysp/serial.h` response boxes | None |
| Mouse click on a stimulus | SDL mouse events through `yscr_poll()`; `ygfx_hit()`, `ygfx_hit_index()` (instances, curve runs, artwork layers) | No drag helper, no pointer trace recorder |
| Sound at a time | `yau_play_at()`, tones, noise, clicks, WAV load and stream; onset record (fit time, tier 2 at best) | No microphone capture |
| Movie | `ysp/video.h`: frame sequence, MPEG-1, Media Foundation; seek, loop, pause; rate on the movie base | No camera capture (planned, 4.6) |
| Text | `ysp/gfx.h` v0.7 curve runs and cached static text; `ysp/outline.h` glyph outlines, `yol_font_glyph_index()`, `yol_font_hmetrics()` | Layout (Skribidi: wrap, shaping, kerning, bidi) is the pack tool's and the player's, not built. A one-line Latin label placed by advances works now (`examples/outline/font.c`) |
| Image file | `ygfx_image()` from planes; QOI decode in `ysp/video.h` (`yvid_qoi_decode()`) | No PNG or JPEG decoder in a header. The pack tool converts (5.2), not built |
| Degrees of visual angle | `ygfx_view {distance_mm, width_mm}`, `YGFX_DEG` | The participant procedure that measures them (card, blind spot) does not exist |
| Conditions, order, repetitions | `ysp/trials.h`: factorial, flat or table rows (CSV conditions files via `ysp/table.h`), SEQUENTIAL, RANDOM, FULL_RANDOM, CONSTRAINED, LIST, WITH_REPLACEMENT, subsets, blocked and alternating groups, units, transition balance, Latin squares, rules text, weighted reps, blocks, practice, warmup, re-queue, tracks (v0.2.0) | One level of nesting (groups); deeper trees belong to the player. See 14.5 |
| Adaptive levels | `ysp/stair.h`, `ysp/quest.h`, `ysp/aep.h` as tracks of `ysp/trials.h` | None |
| Triggers | `ysp/parallel.h`, `ysp/serial.h`, flip hooks of `ysp/screen.h` | None |
| Experiment flow, data file | Nothing above `ysp/trials.h`. The player (section 6) is not built | See 14.4 |

### 14.2 Classes

| Class | Meaning |
|---|---|
| Now | The headers cover it. A C program can do it today with caller code for the response loop. |
| Text | Needs multi-line or shaped text. Glyph drawing exists (`ysp/gfx.h` v0.7 curve runs). Layout is Skribidi in the pack tool and the player, not built. |
| Helper | Needs a small helper (tens to a few hundred lines) above the headers, in the player or an example. |
| Widget | Needs a widget layer: buttons, sliders, text entry, layout of controls, focus. |
| Device | Needs a device feature that is planned but not built: microphone capture, camera capture, an eye-tracker input source. |
| Out | Out of scope, with the reason given. |

"C ex." says whether the plugin makes a good C example in `examples/`.

### 14.3 Official plugins and extensions

53 plugins and 4 extensions in `packages/`. Totals: Now 14, Text 8,
Widget 11, Helper 4 (`sketchpad` counted here), Device 8, Out 8.

#### Keyboard-response trials

| Plugin | What it does | ysp parts | Missing | Class | C ex. |
|---|---|---|---|---|---|
| `html-keyboard-response` | Shows HTML, records a key | gfx shapes, curve runs; timeline on and off; `yscr_poll()` | Layout for general HTML text | Text (Now for shapes and one-line Latin labels) | Done: `examples/response/trial_keyboard.c` (fixation, stimulus, response window, RT from the flip onset) |
| `image-keyboard-response` | Shows an image, records a key | `ygfx_image()`; timeline | Image file decode for C users | Now | Yes, with QOI or procedural images |
| `canvas-keyboard-response` | Draws through a user function, records a key | Any `ysp/gfx.h` drawing, user shader contract | None (`ysp/response.h` v0.1.0) | Now | Covered by the base trial |
| `audio-keyboard-response` | Plays a sound, records a key; `trial_ends_after_audio`, `response_allowed_while_playing` | `yau_play_at()`, onset record, buffer length | None: `ysp/response.h` v0.1.0 measures RT from the audio onset fit (`yrsp_onset_audio()`, tier 2) | Now | Yes: RT from a sound onset with its tier |
| `video-keyboard-response` | Plays a video, records a key; `start`, `stop`, `rate`, `trial_ends_after_video` | `ysp/video.h` play, seek, end; rate on the movie base (`ytl_rate`) | None (`ysp/response.h` v0.1.0) | Now | Yes: extend `examples/video/play.c` |
| `animation` | Image sequence at `frame_time`, `frame_isi`, `sequence_reps`; keys during it | Images as timeline onsets; one landing record per frame | Image decode for C users; prompt text | Now | Yes: shows per-frame landing records that jsPsych cannot give |
| `categorize-image` | Image, key, feedback text by `key_answer` | Images, timeline | Feedback text (`correct_text`, `incorrect_text`) | Text (Now with symbol feedback) | After text |
| `categorize-html` | HTML, key, feedback text | Shapes | Layout | Text | No |
| `categorize-animation` | Animation, key, feedback text | Images, timeline | Feedback text | Text | No |
| `same-different-image` | Two images in sequence: `first_stim_duration`, `gap_duration`, `second_stim_duration`; same or different key | Timeline sequence, images | None (`ysp/response.h` v0.1.0) | Now | Yes: SOA on the frame grid with records |
| `same-different-html` | The same with HTML | Shapes | Layout | Text | No |
| `iat-image` | IAT: image, category labels left and right, error feedback | Images; one-line labels by advances | Layout for labels in general; `html_when_wrong` | Text | No |
| `iat-html` | IAT with an HTML stimulus | As above | Layout | Text | No |
| `serial-reaction-time` | Grid of squares, a target lights, key per position; `pre_target_duration`, `fade_duration` | `ygfx_instances()`, `ygfx_inst_grid()`, tween on opacity | None (`ysp/response.h` v0.1.0) | Now | Yes |
| `serial-reaction-time-mouse` | The same, click the target | `ygfx_hit_index()` on the grid; mouse events | Mouse times are ms-grade (state it) | Now | Yes, with the SRT above |
| `visual-search-circle` | Target and foils on a circle; present or absent key | Shapes or images at computed places; instances | None (`ysp/response.h` v0.1.0) | Now | Yes |
| `reconstruction` | Method of adjustment: keys change a parameter of `stim_function` | Any gfx parameter driven by a key; `ysp/timeline.h` tween of length 0 | Finish key instead of `button_label` | Now | Yes: method of adjustment |

#### Button and slider trials

| Plugin | What it does | ysp parts | Missing | Class | C ex. |
|---|---|---|---|---|---|
| `html-button-response` | HTML, buttons; `button_layout`, `grid_rows`, `enable_button_after` | Shapes and hit tests | Button widget, control layout, layout for labels | Widget | No |
| `image-button-response` | Image, buttons | Images, hit tests | Button widget | Widget | No |
| `canvas-button-response` | Canvas, buttons | gfx, hit tests | Button widget | Widget | No |
| `audio-button-response` | Sound, buttons | `yau_play_at()`, hit tests | Button widget | Widget | No |
| `video-button-response` | Video, buttons | `ysp/video.h`, hit tests | Button widget | Widget | No |
| `html-slider-response` | HTML, slider; `min`, `max`, `step`, `slider_start`, `labels`, `require_movement` | Shapes, hit tests | Slider widget (drag, keys, tick labels) | Widget | No |
| `image-slider-response` | Image, slider | Images | Slider widget | Widget | No |
| `canvas-slider-response` | Canvas, slider | gfx | Slider widget | Widget | No |
| `audio-slider-response` | Sound, slider | Audio | Slider widget | Widget | No |
| `video-slider-response` | Video, slider | Video | Slider widget | Widget | No |
| `cloze` | Text with blanks the participant types into | None | Text entry widget, layout. `ysp/screen.h` stops SDL text input on purpose (keys stay on the raw path), so a text-entry trial turns it on and is untimed | Widget | No |

#### Pointer, drawing and size procedures

| Plugin | What it does | ysp parts | Missing | Class | C ex. |
|---|---|---|---|---|---|
| `free-sort` | Drag images into an area; logs every move | Images, `ygfx_hit()`, mouse events | Drag helper (pointer capture, z-order, drop area test, move log); finish button (a key will do) | Helper | Later |
| `sketchpad` | Freehand drawing, undo, redo; saves strokes and PNG | Polyline paths (224 points each), capsules, render targets, `ygfx_read_target()` | Stroke accumulation into a target; buttons; PNG writer | Helper + Widget | No |
| `resize` | Participant scales a card image to measure pixels per unit | Images, key or drag adjust, `ygfx_view` | Procedure that turns the card width into `width_mm` | Helper | No; web player only |
| `virtual-chinrest` | Card resize, then blind-spot task, gives viewing distance | Shapes, tween of a moving dot, keys, `ygfx_view` | Procedure and the distance formula. A lab uses a chin rest and a tape | Helper | No; web player only |

#### Media capture and eye tracking

| Plugin | What it does | ysp parts | Missing | Class | C ex. |
|---|---|---|---|---|---|
| `html-audio-response` | Records the microphone after a stimulus | `ysp/audio.h` output only | Microphone capture on the `ysp/rt.h` clock | Device | After capture |
| `initialize-microphone` | Asks permission, picks the microphone | None | Microphone capture; device choice | Device | No |
| `html-video-response` | Records the camera after a stimulus | None | Camera capture (4.6, planned) | Device | After capture |
| `initialize-camera` | Asks permission, picks the camera | None | Camera capture | Device | No |
| `mirror-camera` | Shows the live camera | `ygfx_texture_update()` can show frames | Camera capture; the display is untimed | Device | No |
| `webgazer-init-camera` | Starts WebGazer, positions the face | None | Eye-tracker input source (12) | Device | No |
| `webgazer-calibrate` | Calibration dots | Dots and timeline are Now | Input source; calibration fit | Device | No |
| `webgazer-validate` | Validation dots, gaze offset, percent in ROI | Dots are Now | Input source; validation statistics | Device | No |

#### Flow, setup and forms

| Plugin | What it does | ysp parts | Missing | Class | C ex. |
|---|---|---|---|---|---|
| `instructions` | Pages of HTML; keys or clickable navigation | Static text in a target (v0.7), keys | Layout; buttons optional | Text | After text |
| `fullscreen` | Enters browser fullscreen | `ysp/screen.h` opens fullscreen; the web player's self-test records the state (6) | Nothing native. Web player: not built | Now | No |
| `preload` | Loads media before trials | The pack loads at open; long media streams (5, 4.4) | Nothing | Now (by design) | No |
| `browser-check` | Checks features, window size, measures refresh | `yscr_describe()`, capabilities, flip records; the web self-test (6) | Web self-test not built | Now (native) | No; `screen_flipstats.c` exists |
| `call-function` | Runs a function in the timeline | C, or a script call in the player | Nothing | Now | No |
| `external-html` | Loads an external page (often consent) | None | Out: consent and information pages are documents for a browser or the recruitment platform, before the player starts. No timing claim, and a document engine is not a stimulus engine | Out | No |
| `survey` | SurveyJS forms | None | Out: questionnaires need a full form engine with validation and accessibility. SurveyJS, REDCap and Qualtrics do this; the player links to them. No timing claim | Out | No |
| `survey-html-form` | Free HTML form | None | Out, as `survey` | Out | No |
| `survey-likert` | Likert items | None | Out, as `survey`. A single rating by key is a keyboard trial | Out | No |
| `survey-multi-choice` | Radio questions | None | Out, as `survey` | Out | No |
| `survey-multi-select` | Checkbox questions | None | Out, as `survey` | Out | No |
| `survey-text` | Text questions | None | Out, as `survey` | Out | No |
| `maxdiff` | Best and worst choice in a table of alternatives | None | Out: an untimed form for preference scaling; a survey tool does it | Out | No |

#### Extensions

| Extension | What it does | ysp parts | Missing | Class | C ex. |
|---|---|---|---|---|---|
| `extension-mouse-tracking` | Records pointer samples and target boxes per trial; `minimum_sample_time`, `targets`, `events` | Restamped mouse events, `ygfx_bounds()`, the event ring | Pointer trace recorder into the ring, with stimulus boxes at each flip | Helper | Yes: pointer trace and boxes in the ring, CSV drain |
| `extension-webgazer` | Gaze samples per trial, ROI targets | None | Input source (12) | Device | No |
| `extension-record-video` | Camera recording per trial | None | Camera capture (4.6) | Device | No |
| `extension-pipe` | Sends data to DataPipe (OSF) | None | Out for the native player: data stays on the rig. The web player has no upload path; decide that with the web deploy, not here | Out | No |

### 14.4 jspsych-contrib, paradigm-relevant

Descriptions from `package.json`. Not verified beyond that, except
`plugin-rdk`. Skipped as not paradigm-relevant: URL capture and redirect,
Nextcloud and DataPipe storage, device orientation, slide-to-continue,
result charts, countdown, the survey variants, number and numpad entry,
tangram and copying games.

| Plugin | What it does | ysp parts | Missing | Class | C ex. |
|---|---|---|---|---|---|
| `plugin-rdk` (contrib) | Random dot kinematogram, key report | `ysp/rdk.h`: `count`, `coherence`, `direction`, `lifetime`, `sets`, aperture CIRCLE and RECT, edge WRAP and REPLOT; transparent motion as two fields | `number_of_apertures` is several fields; `opposite_coherence` has no direct field. Units differ: contrib `move_distance` is px per frame, ysp `speed` is units per second (contrib angle and `dot_life` units not verified) | Now | Yes: `gfx_rdk.c` plus a response window |
| `plugin-rok` (contrib) | Random object kinematogram: oriented objects | `ysp/rdk.h` into instance records (gabor arrays) | None (`ysp/response.h`) | Now | No (RDK covers it) |
| `plugin-flanker` (contrib) | Flanker array with SOA | Arrows as polygons or paths; letters as one-line labels; timeline | None (`ysp/response.h`) | Now | Yes |
| `plugin-stop-signal` (contrib) | Stop-signal task: an animation, button responses (its description; the paradigm details not verified) | Timeline; `ysp/stair.h` for the stop-signal delay; `yau_play_at()` for an auditory signal | Covered by `ysp/response.h` (ysp uses keys or a response box, not buttons) | Now | Yes: staircase, sound and display on one clock |
| `plugin-libet-intentional-binding` (contrib) | Libet clock; participant sets the hand to report a time | Repeating rotation track, line shape, tone at a delay, key or mouse adjust | None (`ysp/response.h`) | Now | Yes: timing-critical |
| `plugin-corsi-blocks` (contrib) | Blocks flash in order; participant clicks the order | Timeline sequence, `ygfx_hit_index()` | Click log helper | Now | Yes |
| `plugin-spatial-nback` (contrib) | Grid cell lights; n-back responses | Instances grid, timeline | None (`ysp/response.h`) | Now | No |
| `plugin-visual-search-click-target` (contrib) | Search display, click the target | Instances, hit tests | None | Now | No |
| `plugin-image-array-keyboard-response` (contrib) | Many images, key response | Images | Image decode for C | Now | No |
| `plugin-image-click-response`, `plugin-image-hotspots` (contrib) | Click points or regions on an image | Image, hit tests, mouse events | Region list helper | Now | No |
| `plugin-video-several-keyboard-responses` (contrib) | Several keys during a video, with times | `ysp/video.h`, `yvid_movie_time()` | None (`ysp/response.h` keep_open) | Now | No |
| `plugin-video-hotspots` (contrib) | Video freezes, click regions | `ysp/video.h` pause, hit tests | None | Now | No |
| `plugin-vsl-animate-occlusion`, `plugin-vsl-grid-scene` (contrib) | Statistical learning: shapes behind an occluder; image grids | Tweens, images, instances | None | Now | No |
| `plugin-pursuit-rotor` (contrib) | Track a moving target with the pointer | Sampled track or tween for the target; mouse events | Pointer trace recorder | Helper | Yes, with the mouse-tracking example |
| `plugin-trail-making` (contrib) | Connect numbered circles in order | Circles, one-character labels, lines, hit tests | Click sequence helper | Helper | No |
| `plugin-tower-of-london` (contrib) | Move balls between pegs to a goal | Shapes, hit tests | Click-to-move or drag helper | Helper | No |
| `plugin-self-paced-reading`, `plugin-spr` (contrib) | Words revealed one by one on key press; RT per word | One-line Latin by advances | Layout for real text and masks | Text | After text: common in psycholinguistics |
| `plugin-circle-click-response` (contrib) | Options on a circle around a stimulus, click one | Shapes, images, hit tests | Text labels | Text | No |
| `plugin-html-multi-response`, `plugin-image-multi-response`, `plugin-audio-multi-response` (contrib) | Button or key response | As the key trials | Button widget | Widget | No |
| `plugin-html-vas-response`, `plugin-html-keyboard-slider`, `plugin-survey-vas` (contrib) | Visual analog scale | Shapes | Slider widget | Widget | No |
| `plugin-bart`, `plugin-columbia-card-task` (contrib) | Risk tasks with buttons and money counters | Shapes, hit tests | Buttons, number text | Widget | No |
| `plugin-free-recall-response` (contrib) | Typed recall, word by word | None | Text entry | Widget | No |
| `plugin-gamepad` (contrib) | Gamepad responses | SDL events through `yscr_poll()` | `ysp/screen.h` starts only `SDL_INIT_VIDEO`; gamepad events need `SDL_INIT_GAMEPAD` from the caller (not verified); gamepad timing not measured | Helper | No |
| `*-swipe-response`, `extension-touchscreen-buttons`, `extension-device-motion` (contrib) | Touch and motion input on phones | None | Out: mobile is not planned (9.1) | Out | No |
| `extension-chiasm`, `plugin-chiasm-*`, `extension-mediapipe-face-mesh` (contrib) | Webcam eye and face tracking | None | Input source (12) | Device | No |
| `plugin-html-keyboard-response-raf` (contrib) | Key trial timed by `requestAnimationFrame` | The frame loop does this by design | None (`ysp/response.h`) | Now | No |

Third party, not in contrib: jspsych-psychophysics (Kuroki 2021, MIT) is
the jsPsych route that jsPsych's own timing page recommends. Its stimulus
objects (`obj_type` rect, circle, line, cross, text, image, sound, gabor,
manual) with `show_start_time`, `show_end_time`, `motion_start_time`,
`horiz_pix_sec` and frame variants (`show_start_frame`) map directly to
`ysp/gfx.h` stimuli and timeline on, off and tween events. It is the
closest jsPsych analog of a ysp trial and the best source of names for a
designer's stimulus list.

### 14.5 Timeline model

jsPsych runs a tree of nodes. A leaf is a trial (a plugin `type` and its
parameters). A node has a `timeline` array and node parameters. ysp has a
flat session in `ysp/trials.h` and a within-trial sequence in
`ysp/timeline.h`. The tree lives in the player's experiment definition (6).

| jsPsych concept | jsPsych behavior | `ysp/trials.h` | `ysp/timeline.h` (`ytl_seq`) | Player (6, 6.1) |
|---|---|---|---|---|
| Trial object, `type` | One plugin run | One `ytr_next()` / `ytr_update()` pair | One trial base, anchored at the trial's first frame, cleared after | A trial function `trial(c)` or a data trial template from the designer's vocabulary |
| Nested `timeline` | Children inherit the node's parameters; any depth | One level: `desc.groups` (v0.2) orders the levels of one factor as blocks or a cycle; `block_size` counts otherwise | None | Needed: nodes with inherited parameters |
| `timeline_variables` | A table; each row runs the node's timeline once | Condition rows (flat or factorial); the caller maps row to values | None | Rows from a pack Table (5.2); `c.face` in a script for `jsPsych.timelineVariable('face')` |
| `randomize_order` | Shuffle rows; reshuffle each repetition | `YTR_ORDER_RANDOM` (each repetition a shuffled block): same semantics | None | Map directly |
| `repetitions` | Repeat the node | `desc.reps`; `desc.cond_reps` per row | None | Map directly |
| `sample: fixed-repetitions` | Each row `size` times, one shuffle | `YTR_ORDER_FULL_RANDOM` with `reps = size` | None | Map directly |
| `sample: with-replacement` (`size`, `weights`) | Independent draws | `YTR_ORDER_WITH_REPLACEMENT`, `desc.draws`, `desc.weights` (v0.2) | None | Map directly |
| `sample: without-replacement` (`size`) | A random subset of rows | `desc.subset` with FULL_RANDOM and 1 repetition (v0.2) | None | Map directly |
| `sample: alternate-groups` (`groups`, `randomize_group_order`) | Strict cycle through groups | `desc.groups` ALTERNATE, any group order (v0.2); unequal sizes are refused, where jsPsych cuts to the smallest | None | Map directly |
| `sample: custom` (`fn`) | User function returns the order | `YTR_ORDER_LIST`, `desc.order_list` (v0.2); `ytr_latin()` for Latin and Williams rows by participant | None | The script's function writes the list |
| `jsPsych.randomization.shuffleNoRepeats` | Shuffle, no immediate repeats | `YTR_ORDER_CONSTRAINED` with `ytr_max_run(YTR_CONDITION, YTR_ANY_LEVEL, 1)`; more rules than jsPsych has | None | ysp is ahead |
| `jsPsych.randomization.factorial` | Crossed design | `desc.factors` | None | Map directly |
| `sampleExGaussian`, `sampleExponential`, `randomInt` | Jitter for foreperiods and ITIs | Only `desc.rng`; 4.8 bars a general random header | None | Gap: the script needs draws from the trial handler's generator (uniform, exponential, truncated) so a replay reproduces |
| `conditional_function` | Run the node or skip it, decided at the node's first trial | None | None | A script `if`; a data-only form needs a condition expression on a node |
| `loop_function(data)` | Repeat the node while it returns true | `ytr_requeue()` covers "repeat this trial" only | None | A script loop; a data-only form for "practice until criterion" (accuracy over the last iteration) |
| Runtime push and pop of nodes | Branches added in `on_finish` | None | None | Do not borrow: replay needs the tree fixed at load. Use explicit branches |
| `on_start(trial)` | Edit the trial's parameters before it runs | The caller computes before drawing | Builder calls after a static op table (4.5.1) | The trial function computes from `c` |
| `on_load` | After the first display | None | The first flip record of the trial | A handle that resolves at the first flip |
| `on_finish(data)` | Edit the trial's data | `ytr_update(outcome, rec)` | None | The trial function returns its record table |
| `on_timeline_start`, `on_timeline_finish` | Once per node | None | None | Node hooks |
| `data` (node or trial) | Constant columns, inherited | `desc.records` (fixed bytes); factor columns in `ytr_format_row()` | MARK events | Named columns, inherited from nodes |
| `jsPsych.data.addProperties` | Columns on every row (subject, group) | `ytr_format_meta()` heads the file | None | Session columns |
| `save_timeline_variables` | Copies row values into the data | Factor levels are in every row already | None | Default on: every row value goes in the record |
| `record_data: false` | No data row | None | None | A flag on a trial |
| `trial_index`, `trial_type`, `time_elapsed` | Columns on every row | `index`, `block`, `rep`, `condition` columns | None | Add `trial_type` (template name) and session time on the `ysp/rt.h` clock |
| `stimulus_duration` | Hides the stimulus by `setTimeout` | None | OFFSET at onset + duration, on the nearest frame, with a landing record | `wait(d); hide(s)` |
| `trial_duration` | Ends the trial if no response | None | A wait | `race(input.key{...}, wait(d))` (6) |
| `response_ends_trial` | End at the response, or run to `trial_duration` | None | None | After the race: return, or `wait_until(start + d)` |
| `post_trial_gap`, `default_iti` | Blank screen after the trial | None | A wait on the session base | A node or session parameter |
| `rt` | From the call that registers the key listener (`performance.now()`), not from a flip (pluginAPI docs) | Caller's record | Onset record of the stimulus event | RT from the stimulus's flip onset estimate (or audio onset fit), with its tier in the row |
| `choices`, `"ALL_KEYS"`, `"NO_KEYS"` | `KeyboardEvent.key` strings, lowercased by default | None | None | Accept jsPsych key names; map to SDL keycodes; log the scancode too |
| `minimum_valid_rt`, `wait_for_key_release`, `rt_key_duration`, `allow_held_key`, `persist` | Key listener options | `ysp/response.h` v0.1.0 | None | Options of `yrsp_desc` (`allow_held_key` and `persist` semantics: docs/response.md) |
| `name`, `abortTimelineByName`, `abortCurrentTimeline`, `abortExperiment`, `pauseExperiment` | Flow control | None | None | Node names; abort and pause over the runner protocol and `yscr_request_abort()` |
| `extensions: [{type, params}]` per trial | Extension hooks per trial | None | None | Input-source extensions (12) enabled per node or trial |
| Simulation mode (`data-only`, `visual`), `simulation_options` | Runs without a participant | The SIM display; simulated observers in examples | None | A player mode: data-only runs with a simulated observer on the SIM display |

### 14.6 What the player's experiment definition should borrow

Borrow the structure and these names, so a jsPsych user can read a ysp
definition:
- Nodes: `timeline`, `timeline_variables`, `randomize_order`,
  `repetitions`, `sample` with `type` (`with-replacement`,
  `without-replacement`, `fixed-repetitions`, `alternate-groups`,
  `custom`), `size`, `weights`, `groups`, `randomize_group_order`,
  `conditional_function`, `loop_function`, `on_timeline_start`,
  `on_timeline_finish`, `name`.
- Trials: `stimulus`, `choices` (with `ALL_KEYS` and `NO_KEYS`), `prompt`,
  `stimulus_duration`, `trial_duration`, `response_ends_trial`,
  `post_trial_gap`, `data`, `record_data`, `save_timeline_variables`,
  `on_start`, `on_finish`, `extensions`.
- Response options: `minimum_valid_rt`, `wait_for_key_release`,
  `allow_held_key`, `persist`.
- Paradigm parameters, for the built-in templates: `key_answer`,
  `correct_text`, `incorrect_text`, `feedback_duration`,
  `show_stim_with_feedback`, `first_stim_duration`, `gap_duration`,
  `second_stim_duration`, `same_key`, `different_key`, `answer`,
  `trial_ends_after_audio`, `trial_ends_after_video`,
  `response_allowed_while_playing`, `frame_time`, `frame_isi`,
  `sequence_reps`, `target_present`, `set_size`.
- Data columns: `rt`, `response`, `stimulus`, `correct`, `trial_index`,
  `trial_type`, `time_elapsed`, `rt_key_duration`.
- From jspsych-psychophysics, for timed stimulus objects:
  `show_start_time`, `show_end_time`, `motion_start_time`,
  `motion_end_time`, `response_start_time`.

Keep the ysp semantics where they differ, and say so in the designer:
- `rt` starts at the flip onset estimate of the stimulus event (or the
  audio onset fit), not at listener registration. The row carries the
  onset tier and residual.
- `stimulus_duration` and every other duration land on a frame. The
  landing record is in the data.
- Rows are a fixed tree at load. No runtime push or pop.
- Keys: log scancode and keycode. Case folding does not apply.
- Randomness comes from the trial handler's generator with a logged seed.

Units, decided 2026-10-07: seconds, in the experiment definition, the
script API and the data file. Milliseconds were rejected: they add a
conversion for users who come from Psychtoolbox and PsychoPy, which use
seconds. jsPsych gives durations in ms, so a jsPsych name with a value in
seconds is a 1000x trap for a user who switches. Proposed safeguards, not
yet decided:
- A duration value may carry its unit as text (`"250 ms"`, `"0.25 s"`);
  the loader converts it. A bare number is seconds.
- The designer shows the unit beside every duration field.
- The loader warns when a bare duration is above 60 s, with the
  millisecond reading in the message.
- An importer for jsPsych timelines converts ms to s.

### 14.7 Gaps ranked by the paradigms they block

Counts are of the official plugins in 14.3, then of the contrib plugins in
14.4. "Blocks" means a C user cannot do it with the headers; "burdens"
means a C user writes the code each time.

| Rank | Gap | Where it goes | Blocks or burdens | Count (official + contrib) | Size |
|---|---|---|---|---|---|
| 1 | Experiment flow: nested nodes, timeline variables bound to a pack table, conditional and loop, named data rows | The player (11, item 7) | Blocks every paradigm for users who do not write C | All | Large; planned |
| 2 | Response helper: key set, double-report removal, RT from the onset record, window, `response_ends_trial`, key release, minimum RT | Done for C: `ysp/response.h` v0.1.0 and `examples/response/trial_keyboard.c` (2026-10-07). Left: `input.key` in the player | Burdened every key response trial | 17 + about 10 | Small |
| 3 | Text layout (Skribidi): prompts, instructions, word stimuli, feedback text | Pack tool and player (11, items 6 and 7) | Blocks text-heavy paradigms; `prompt` appears on most plugins | 8 + 3, and every `prompt` | Large; planned |
| 4 | Widget layer: button, slider, text entry, control layout | Player, above `ysp/gfx.h` | Blocks button and slider responses | 11 + 9 | Medium |
| 5 | Image file decode for C users | Pack tool; meanwhile QOI or an outside decoder in examples | Burdens image trials | 10 + 3 | Small (in the pack tool) |
| 6 | Eye-tracker input source and calibration, validation | Extension (12), Input source kind | Blocks gaze tasks and fixation control. jsPsych counts understate it: lab psychophysics uses fixation control widely | 4 + 4 | Medium per tracker |
| 7 | Capture: microphone, camera | `ysp/audio.h` input; `ysp/video.h` capture (4.6) | Blocks voice and video responses | 6 + 0 | Medium |
| 8 | Pointer helpers: drag, pointer trace, click sequence | Helper in the player; examples | Blocks drag sorting and mouse tracking | 2 + 4 | Small |
| 9 | Done in the headers: order and draws (`ysp/trials.h` v0.2.0) and jitter draws (v0.2.1, 2026-10-07: uniform, choice and truncated exponential durations per trial, snapped to frames on request, logged and replayed, per condition from table columns). Left: exposing them in the script | Player | Burdens designs with jittered foreperiods | Node-level; not counted per plugin | Small |
| 10 | Size procedures: card resize, blind spot | Web player | Blocks only web deploys without a measured display | 2 + 0 | Small |
| 11 | Freehand canvas, gamepad | Helper; caller's SDL init | Blocks sketch tasks and gamepad responses | 1 + 1 | Small |

Recommended C examples, in order: the base keyboard trial (rank 2's
helper in its first form, with RT from the flip onset and the tier),
`same-different-image` (SOA on the frame grid), `serial-reaction-time`
with its mouse variant, `audio-keyboard-response` (RT from a sound
onset), `reconstruction` (method of adjustment), the contrib
`plugin-stop-signal` (staircase, sound and display on one clock), and
`extension-mouse-tracking` (pointer trace in the event ring).

### 14.8 License

jsPsych and every jspsych-contrib package are MIT (jsPsych: "Copyright (c)
2014-2022 Joshua R. de Leeuw"; contrib: the `license` field of all 61
`package.json` files, no root LICENSE file; each contrib package has its
own authors). jspsych-psychophysics is MIT, Copyright (c) 2019-2026
Daiichiro Kuroki. Behavior, parameter names and data column names are not
code: ysp reimplements them freely under MIT-0. Code copied from any of
them keeps the MIT notice and its copyright line, as the SDF functions in
`ysp/gfx.h` do. No code is copied in this section.

### 14.9 Not verified

- jspsych-contrib plugins other than `plugin-rdk`: only their
  `package.json` descriptions were read.
- The angle convention and `dot_life` unit of contrib `plugin-rdk`.
- That SDL gamepad and touch events reach `yscr_poll()` when the caller
  starts the gamepad subsystem; their timing is not measured.
- jsPsych's internal behavior beyond its documentation (no source read).
