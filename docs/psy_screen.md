# psy_screen.h design notes

This note explains why `psy_screen.h` has its current form and records
what was measured. The manual in the header tells you how to use it. The
plan that asked for the header is section 4.2 of
[rig_spec.md](rig_spec.md).

All numbers come from one machine: a Windows 11 25H2 laptop (build 26200)
with an Intel Iris Xe that drives the internal 1920 x 1200 panel and an
NVIDIA RTX A500 with no outputs. The panel has only 60 Hz modes; SDL3
reports the refresh as 15413000/256880 Hz, 60.0008 Hz, and the vblank
times agree with that to 1 ppm. ANGLE is the copy that ships with Docker
Desktop's Electron front end, `2.1.23876 git hash: fffbc739779a`, not a
pinned build. SDL3 is 3.4.0. The frame thread ran at
`THREAD_PRIORITY_TIME_CRITICAL`. No photodiode was attached, so every
onset below is a software timestamp: the vblank time DXGI reports, not
light on the panel.

## Windows: the header owns the swapchain

The spec first said "EGL through ANGLE via SDL3". That path was measured
and dropped.

With `SDL_HINT_OPENGL_ES_DRIVER` and `SDL_HINT_VIDEO_FORCE_EGL`, SDL3 makes
an ES 3.0 context through ANGLE on the window. ANGLE then makes the
window surface's swapchain itself, and its source (`NativeWindow11Win32.cpp`)
shows that an HWND surface is `DXGI_SWAP_EFFECT_SEQUENTIAL` with one
buffer, a blt-model swapchain. A blt-model swapchain has no frame
statistics in a window, no frame latency control, and can never get an
overlay plane or independent flip. Measured, fullscreen:

| | Result |
|---|---|
| `eglGetSyncValuesCHROMIUM` (advertised) | failed on 180 of 180 frames |
| 180 swaps, swap interval 1 | 3.6 s, about 50 Hz |
| one swap call | 0.4 to 54 ms |

So on Windows SDL3 makes only the window. The header makes a D3D11 device
on the adapter whose output shows the window, a
`DXGI_SWAP_EFFECT_FLIP_DISCARD` swapchain with two buffers, a frame latency
waitable object and `SetMaximumFrameLatency(1)`, and an ANGLE display on
that device (`EGL_ANGLE_device_creation_d3d11`). ANGLE draws into a pbuffer
made from swapchain buffer 0 (`EGL_ANGLE_d3d_texture_client_buffer`). In
D3D11's flip model only buffer 0 can be named, and it always aliases the
current back buffer; asking for buffer 1 gives `EGL_BAD_PARAMETER`. A
readback of the composed desktop with GDI after each flip found the frame's
own colors and the patch's own state on 40 of 40 frames, so the pbuffer
follows the rotation. (GDI read the levels scaled by about 0.55 by the
desktop's color path; the check compares states, not levels.)

The adapter matters on this laptop: the NVIDIA part has no output, so a
device on it would mean a copy between adapters and a composed path. The
header picks the adapter whose output holds the window's monitor.

DXGI's vblank time, `SyncQPCTime`, is a QueryPerformanceCounter value, the
counter `psyrt_now_ns()` reads. `psyrt_ticks_to_ns()` converts it with the
same arithmetic, so a flip's onset is on the psy_rt clock with no
correlation.

The Windows 11 composition swapchain, the one native present-at-a-time
path, reports `IsPresentationSupported() = 1` and
`IsPresentationSupportedWithIndependentFlip() = 1` on this GPU. v0.2.0
adds it as `PSYSCR_BACKEND_COMPOSITION`; see
[The composition swapchain](#the-composition-swapchain-v020).

## flip_at does not wait for the flip

A flip's onset is known only after its vblank. Psychtoolbox's Flip blocks
until then, which puts the CPU work of the next frame after the scanout of
this one. `psyscr_flip_at()` returns after the present call, and the record
completes in a later `psyscr_begin()`, with `f.last` and the ring.
`psyscr_wait_flip()` is the blocking form.

## The snap is the timeline's

`psyscr_flip_at(t)` snaps `t` to the first vblank at or after
`t - lead x period`, with lead 0.5 by default: the nearest vblank. The frame
loop passes the predicted onset as `t`, and a predicted onset carries
noise. Under "the first vblank at or after `t`", a `t` one microsecond
after a vblank goes a frame late; [psy_timeline.md](psy_timeline.md)
measured 12% of frame-aligned targets a frame late at 2 us of noise for the
same rule, which is why the timeline dropped it. With one rule in both
headers, a flip and a timeline event at one time land on one frame.

## Depth

A present shows `depth` vblanks after the vblank before the call. Measured:
1 on the overlay path (fullscreen), 2 on the composed path (a window). The
header learns it from the flips. The first version learned it only from
presents that went out at once, and a window that had just gone fullscreen
was composed for its first frames, so open() learned 2 and the first held
frames on the overlay path showed a vblank early (2 to 5 of the first
frames of a run). Two changes fixed it: every present without a target
counts (DXGI shows a frame at the first vblank it can, held or not), and
open() presents black frames until three flips in a row agree, up to 40.
After the change: 0 early flips in 1200 held fullscreen frames.

## Holding a frame for a later vblank

Two ways were built and measured, the same frame loop asking for a vblank
0 to 4 frames ahead with the target up to 0.45 frame off the grid:

| Method | Path | Held frames checked | On the vblank asked | Early | Late | Header CPU in flip_at, p50 |
|---|---|---|---|---|---|---|
| Sleep on psy_rt until just after vblank (planned - depth), then `Present(1)` | composed (window) | 2998 | 2998 | 0 | 0 | 3.3 us |
| same | overlay (fullscreen) | 1200 | 1200 | 0 | 0 | 2.8 us |
| `Present(SyncInterval = planned - last shown)` at once | composed | 2906 | 2711 | 143 | 52 | 2.7 us |
| same | overlay | 1800 | 605 | 795 | 400 | 1.8 us |

DXGI's sync interval holds the frame being presented, not the one on the
screen, so it cannot move a frame later. The sleep is the one kept; the
other was deleted.

## DXGI reports only the newest flip

`GetFrameStatistics` returns the statistics of the newest flip only, and
on the composed path it lags the vblank by about 1.8 ms. A flip that is
followed by another before the next read is never reported. In a hold run
in a window this left 358 of 3000 records without a statistic (12%). The
header now reads the statistics before each present when a flip is due,
and once per vblank, 2.5 ms after it, while it waits to hold a frame. That
brought the hold run to 2 of 3000. A frame the caller made late by 20 ms
or more still hides the flip before it on the composed path (23 of 900
records in those runs); on the overlay path none was lost. Such a record
is flagged `PSYSCR_FLIP_ESTIMATED` and gets its planned vblank as onset.

## The patch is a ClearView

The first patch was a scissored `glClear` through ANGLE, with the GL state
it touches read and restored. Measured: 6.3 us p50 for the reads and
8.5 us for the clear, 15.2 us per frame. On DXGI the patch is now
`ID3D11DeviceContext1::ClearView` with one rectangle on the back buffer,
after ANGLE's flush, inside the present: 0.1 us of header time, no GL
state touched. The GL path stays for presenters that do not paint the
patch themselves.

## Input timestamps

`SDL_GetTicksNS()` against `psyrt_now_ns()`: the tightest of 16 reads has a
width of 0.1 us (the QPC tick) in 1000 of 1000 calls, 0.9 us per call. SDL3
counts in QPC on Windows, so the restamp is exact to 0.1 us.

The event's own timestamp is not exact. SendInput reaches only the
foreground window, and Windows does not give the foreground to a program
started from the background. It does after the program sends one input
event, so `examples/screen_input.c` first sends a zero-size mouse move
(the pointer does not move) and calls `SetForegroundWindow()`. Then it
sends F24 with SendInput at known psy_rt times, first through SDL's
message loop and then on SDL's raw keyboard path:

| Path | Keys | Restamped minus sent, ms: mean | p50 | p99 | min | max |
|---|---|---|---|---|---|---|
| raw input | 199 of 200 | +0.39 | +0.37 | +0.65 | +0.18 | +0.67 |
| raw input, `timeBeginPeriod(1)` | 199 of 200 | +0.50 | +0.43 | +1.21 | +0.21 | +2.86 |
| message loop | 199 of 200 | +2.77 | +2.97 | +10.99 | -6.16 | +12.05 |
| message loop, `timeBeginPeriod(1)` | 199 of 200 | +2.42 | +2.24 | +11.13 | -6.91 | +11.14 |
| posted `WM_KEYDOWN` (v0.1) | 199 of 200 | -6.98 | | | -14.84 | +0.49 |

SDL takes a message event's time from the message time, which counts in
the system tick (about 15.6 ms), so a key can be stamped before it was
sent. On the raw path SDL reads Raw Input on its own thread and stamps
each key as it reads it; its sub-millisecond parts spread over the whole
millisecond, so the stamp is not tick-bound. So the header turns on the
raw keyboard path (`SDL_HINT_WINDOWS_RAW_KEYBOARD`, at default priority,
so the caller's own setting wins) and stops SDL text input. The times
above leave out the keyboard itself. Use `psy_serial.h` response boxes
for reaction times.

## Measurements

`examples/screen_flipstats.c` made every table here, built with MSVC
19.4 /O2 in Release. It defines `PSYRT_TRACE_RING`, so the header's zones
give the cost breakdown. The runs before the last three changes to the
header (DXGI times through `psyrt_ticks_to_ns()` instead of a correlation
with a 0.1 us width, no second event pump after `psyscr_poll()`, and the
GPU timer query taken out) were checked with one more fullscreen run of
the final build (last row of the first table).

Prediction: the onset DXGI reported minus the onset `psyscr_begin()`
predicted, for frames the caller did not make late. Bar: 50 us at p99.

| Run | Path | Frames | p50 us | p99 us | max us | Dropped | Late target | Estimated |
|---|---|---|---|---|---|---|---|---|
| fullscreen, idle, patch on, 2 min | overlay | 7200 | 0.46 | 2.33 | 3.69 | 0 | 0 | 0 |
| fullscreen, 8 threads spinning, 1 min | overlay | 3600 | 0.68 | 2.04 | 6.08 | 0 | 0 | 0 |
| window 800 x 600, idle, 10 min | composed | 36000 | 1.30 | 5.72 | 22.52 | 0 | 0 | 1 |
| fullscreen, final build, 30 s | overlay | 1800 | 0.32 | 2.56 | 6.58 | 0 | 1 | 0 |

The one late target in the last run was the frame loop's own: a
`SDL_PumpEvents()` call took 2.7 ms on that frame.

Holds (see above): every held frame on the vblank asked for, 1200 of 1200
fullscreen and 2998 of 2998 in a window.

Overruns: a busy wait of the given length before the flip in a random 1 of
30 frames. Bar: every overrun past the deadline flagged on its own frame,
with the right phase charged; no false flag.

| Overrun | Path | Frames | Injected | Flagged late target | Draw phase charged | Missed without a flag | Other frames flagged |
|---|---|---|---|---|---|---|---|
| 16 ms | composed | 900 | 24 | 24 | 24 | 0 | 0 |
| 16.5 ms | composed | 900 | 24 | 24 | 24 | 0 | 0 |
| 17 ms | composed | 900 | 24 | 24 | 24 | 0 | 0 |
| 20 ms | composed | 900 | 24 | 24 | 24 | 0 | 0 |
| 40 ms | composed | 900 | 24 | 24 | 24 | 0 | 0 |
| 20 ms | overlay | 900 | 24 | 24 | 24 | 0 | 0 |

A first 20 ms run in a window had one dropped flip and one more late
target on frames with no overrun; the rerun had none. Those were the
machine's, and the record named them.

The header's CPU time per frame, fullscreen, idle, 7200 frames. "begin" is
`psyscr_begin()` minus its wait for the swap path; "flip" is
`psyscr_flip_at()` minus the present call. Bar: 20 us mean and 50 us p99
for the two together.

| Part | mean us | p50 us | p99 us |
|---|---|---|---|
| begin, all | 22.2 | 20.3 | 60.4 |
| of which `SDL_PumpEvents()` | 7.8 | 7.0 | 24.7 |
| of which the statistics read and record completion | 11.8 | 10.7 | 39.8 |
| flip, all | 1.8 | 1.8 | 4.1 |
| of which the patch | 0.1 | 0.1 | 0.2 |
| the present call itself (DXGI and the driver, not counted) | 341.5 | 325.3 | 705.7 |

The bar is missed: 24 us mean and about 64 us p99. The header's own work
is about 4.5 us; the rest is the two OS calls. A repeated
`GetFrameStatisticsMedia()` costs 0.5 us, so the 11 us is the first read
after a flip. A frame that reads its events with `psyscr_poll()` skips
the second pump.

GPU time: a `GL_EXT_disjoint_timer_query` around each frame tracked the
GPU work (54 us for a clear, 3067 us p50 with 50 full-window clears) but
added about 140 us to begin and 115 us to flip at p50 through ANGLE, against
a bar of 5 us. It was taken out; `_GPU` is always unknown in v0.1.

Allocations: no C runtime heap call in 570 frames of an MSVC debug build
(`_CrtSetAllocHook`); with one `malloc` per frame added as a control, the
hook counted 90 of 90.

Two windows as a group, 3 minutes: 10800 flips on each, one late target
on each (the same moment), one drop and three estimated records on the
second. Both windows are on one panel, so their onsets were equal on
10799 of 10800 frames.

Builds, with warnings as errors: MSVC 19.4 (C11 and C++17, Release, and a
debug build), MinGW-w64 gcc 14.2 (C11, C99 and C++17), gcc 11.4 on WSL2
(C11, C99 -O3, C++17, ASan and UBSan), clang through emcc 6.0.10 (C11 run
in node, C++17 compile only). `ctest` passed 32 of 32 in the MSVC build.
The core test caught each of eight deliberate mutations of the header:
the snap without its lead, depth taken on one vote, drops not counted,
estimated records not flagged, the prediction one vblank late, the hold
waking a vblank early, late targets not flagged, a path change not
adopted at once.

## The composition swapchain (v0.2.0)

`PSYSCR_BACKEND_COMPOSITION` presents through the Windows 11
presentation manager (`IPresentationManager`) and a DirectComposition
visual. Each present carries a target time. The measurements in this
section come from two programs:

- a scratch probe in C++ with the SDK headers, which logs every present
  and every statistic;
- `examples/screen_flipstats.c` on the header's own C implementation.

All runs used the machine described at the top of this note. Most of
them ran in a 640 x 480 or 800 x 600 window kept on top, because a
window gets independent flip here too.

### Prior art

I read two projects before chasing the anomalies:

- [xzn/ntrviewer-hr](https://github.com/xzn/ntrviewer-hr),
  `ui_compositor_csc.c`. It is MIT-licensed, so I learned from it and
  copied no code. The headers are MIT-0, and MIT asks for its notice in
  every copy.
- FoggyBytes/StreamLight, `d3d11composition.cpp`. It is GPL-3.0, so I
  only used the research agent's notes on it.

Four of their practices agree with what we found and the header does the
same:

- set the surface's source rectangle;
- wait for a buffer through its available event;
- call `SetTargetTime` before every present (the target persists across
  presents);
- read a displayed time only from an independent-flip statistic.

ntrviewer-hr also declares `GetDisplayedTime` with an output pointer, as
our header does. Neither project checked a target time against the
hardware.

### The anomalies of the phase 1 probe

The first probe found three anomalies. Each one now has a measured
cause.

| Anomaly | Cause | Evidence |
|---|---|---|
| 217 of 630 presents CANCELED | Two queued presents whose targets were the same vblank. The older one is canceled on the independent-flip path and SKIPPED on the composed path. In the first probe, a counter that counted a present twice let two presents be in flight | Pairs of presents aimed at one vblank, 150 pairs: the older present was canceled in 147 and skipped in 3 (independent path), and skipped in 150 of 150 (composed path). The newer present was shown in all 300 pairs |
| 215 of 630 presents with no statistic in one run, 0 in the next | The window was covered: a program started from the background does not get the foreground, and a covered window gets PresentStatus Queued but no display statistic | 300 asap presents with one in flight: 266 had only a Queued status when the window was behind others (34 shown), and 300 of 300 were shown with the window on top |
| Half the presents SKIPPED with target 0 and 3 buffers | With no throttle, the loop presented every 13.9 ms, so some vblanks had two presents ready, and the system skips all but the latest | 136 of 300 presents skipped |

So the header keeps one present in flight. It also completes a present
that gets no statistic within 3 periods as ESTIMATED and OCCLUDED, so a
covered window cannot stall the frame loop.

### Clock

The SDK calls the target time and the displayed time `SystemInterruptTime`.
On this machine both are QPC time in 100 ns units, not
`QueryInterruptTimePrecise()`. Interrupt time (biased or unbiased) was a
constant 20.03 ms behind QPC, and QPC runs at 10 MHz here.

- Targets passed as interrupt time put every frame 1.2 periods early.
  That is the 20 ms difference, and it is what made the phase 1 table
  inconsistent.
- Displayed times read as interrupt time sat 3.4 ms off the vblank. Read
  as QPC, they sit on it.

The header therefore passes `psyrt_now_ns()` time divided by 100. On a
machine whose QPC does not run at 10 MHz this is unverified; the manual
says so in STATUS.

### The independent vblank source

D3DKMTWaitForVerticalBlankEvent returned early now and then, which put
extra wakes in the record. The probe instead polls `D3DKMTGetScanLine` on
a time-critical thread and stamps QPC when the scanout enters vertical
blank. Two numbers show how steady that stamp is:

- the median wake comes 0.5 to 2 us after the 10th-percentile wake;
- 4910 of 4910 stamps sat on a 16666.425 us grid.

### Quantization

Each cell targets vblank V + frac x period, with V at least `lead`
periods ahead, one present in flight, and 20 presents per cell. The
table gives where the frame was shown.

| Path, lead | frac -0.50 | -0.45 to +0.45 | +0.50 |
|---|---|---|---|
| independent, lead 0 | V 19/19 | V, 380 of 380 | V 18/20 |
| independent, leads 1, 2, 3, 5 | V-1 in 70 of 78 | V, 1520 of 1520 | V in 72 of 80 |
| composed, lead 0 | V+1 20/20 | V+1, 380 of 380 | V+1 20/20 |
| composed, leads 1, 2, 3, 5 | V-1 in 52 of 60 (lead 1: V) | V, 1520 of 1520 | V in 72 of 80 |

So the system shows a frame on the vblank nearest its target, and a
tie goes to the earlier vblank. That is psy_timeline.h's lead-0.5 rule.
The quarter-period offset proposed in the design is therefore not used:
the header passes the planned vblank's own time.

The shortest lead that still met the target:

- on independent flip, the next vblank;
- on the composed path, the vblank after next.

### Queue depth

With 1, 2, 4 and 7 presents in flight, each aimed at the next free
vblank, 120 of 120 frames were shown on their target at every depth.
The queue accepts presents several frames ahead. The header keeps one in
flight anyway, and no `queue_ahead` mode was added: one in flight
dropped nothing in these runs, so queueing showed no benefit to measure.

### Is the onset an observation? (condition C)

| Path | Reported time minus the scanline vblank | Statistic arrival minus reported time | Late present (0.3 period after its target vblank) |
|---|---|---|---|
| independent flip: DisplayedTime | p50 +12.7 us, p99 +76.7 us (sweep, 2097 frames) | p50 +270 us, p99 +577 us | shown 1 or 2 vblanks late, 60 of 60, and the reported time moved with it |
| composed: DComp presentedStats.time | p50 +25.8 us, p99 +92.6 us (2100 frames) | p50 -15.3 ms, p99 -13.9 ms | shown 1 to 2 vblanks late, 60 of 60, and the reported time moved with it |

- **Independent flip.** The displayed time sits on the scanout's own
  vblank. It arrives a quarter of a millisecond after it and follows a
  late frame. It is an observation: tier 1.
- **Composed path.** The time also sits on a hardware vblank and follows
  a late frame. But it is reported about 15 ms before that vblank comes,
  so it is DWM's plan for the vblank it composed the frame for. It is not
  an observation of the scanout.
- **DWM load.** A second window cleared a 4096 x 4096 float target 8 and
  then 40 times per frame. DWM's own counters (`cFramesMissed`,
  `cFramesDropped`, `cFramesLate`) stayed at 0, so no DWM miss could be
  produced to see whether the record shows one.
- **Result.** A composed onset on COMPOSITION is DWM's claim. It is
  recorded with PSYSCR_FLIP_ONSET_PLANNED, tier 3.

### A finding: the panel stretches frames

On the independent-flip path, a frame presented late was followed by
frames whose PresentDuration was 20.0, 19.4, 18.8 and so on, down to 16.67
ms over about ten frames. Over those frames the displayed time moved up
to 4.9 ms off the vblank grid, while the scanline's vblank entries stayed
on it. The panel held the vblank longer, as variable refresh does, which
the header does not enable. The record flags these flips GRID_UNSTABLE,
and their tier is 2.

### Path reports on DXGI_FLIP

`IDXGISwapChainMedia::GetFrameStatisticsMedia` is documented for media
swapchains, but the header calls it on an ordinary flip swapchain. Two
observations support its answer there:

- The path it reports changes when the conditions change. A window in
  the background was composed, and the same window taken to the
  foreground went to an overlay plane.
- The depth the header learns independently agrees with it: 2 vblanks
  on composed, 1 on overlay in fullscreen.

A small window put over ours did not force composition: the system kept
our frames on an overlay plane and composed only the other window. So
the forced-composed runs of COMPOSITION use the probe's own controls
instead, a premultiplied-alpha or rotated surface, which DWM composes.

### Depth on a backend that holds frames

The first COMPOSITION batch showed a fault in the core. A present with a
target waits for that target, so a flip that is on time shows only that
the depth it was given was enough. After one miss raised the depth to 2,
every flip "measured" 2, and with one frame in flight the loop ran at
half rate: 21.0 ms mean frame time in a window, and 27.4 ms after
overruns, where 16.7 ms was possible. DXGI_FLIP has no such fault,
because a DXGI present shows at the first vblank it can.

**First fix, replaced: a try at one less.** After 60 on-time flips the
header lowered the depth by one; a try that missed put it back and
doubled the wait, up to 3840 flips. A try that fails drops a frame on
purpose. In the two COMPOSITION runs with a CSV (window kept on top,
2 minutes and 10 minutes), every try worked: 7 tries, 0 failed, so 0 of
the 54 drops were tries. But a display that really needs depth 2 would
lose a frame at every try (the core test showed 2 in 280 flips), and the
slow phases before each try cost 123 and 468 vblanks with no new frame.
The 10-minute run also opened at depth 4: open() learned the depth from
frames whose path changed back and forth between composed and
independent flip.

**The rule now: lower only on evidence.** For each path the header
counts the slack of every flip, on time and missed, in bins of a 32nd of
a period. The slack is the planned vblank's time minus the return of the
present call. A present at one depth less would have had one period less
slack. The header lowers the depth when, on 4 flips in a row, that
slack has been seen on time at least 8 times as often as a miss was
seen with as much slack or more. Three misses in a row raise the depth
by one. So a short run of system misses (the case that caused the half
rate) is undone in about 4 flips, from the evidence of the flips before
it, and no frame is put at risk to find out.

The evidence can be wrong in one case: the system's deadline grew after
the evidence was taken, at the same time as the misses that raised the
depth. Then the first flip at the lower depth misses. A miss within 4
flips of a lowering clears that path's on-time counts, so the same
evidence cannot cost a second frame, and the depth stays up until new
on-time evidence at that slack comes in. Counts halve every 4096 flips.
Each path keeps its last depth, so a window that moves between composed
and independent flip does not learn it again. On these backends open()
does not learn the depth: it starts at 1.

The core test runs three cases on a scripted display that holds frames
to their target, on a virtual clock, with a seeded random wait of up to
a quarter period before each flip:

| Case | Drops after the 3 misses | Frames that waited 2 vblanks |
|---|---|---|
| 3 system misses at depth 1, then the display is as before | 0 | 7 or 14 |
| the display needs depth 2 from the misses on, buffer free at the flip | 1 (the miss that cleared the evidence) | all, as one frame in flight allows |
| the same, buffer free a vblank early (as on DXGI composed) | 1 | 0 |

On the panel, `screen_flipstats --miss 300` gives 3 frames in a row
about 25 ms of GPU work every 300 frames: they miss their vblank with
the present on time, which is the system's lateness that raised the
depth. COMPOSITION, window kept on top, 7200 frames each, on battery:

| Rule | Heavy frames dropped | Other frames dropped | Other frames 2 or more vblanks after the one before |
|---|---|---|---|
| try at one less (the first fix) | 70 of 72 | 1 | 1355 |
| evidence (now) | 72 of 72 | 1 | 69 |

The heavy frames drop by design. With the evidence rule each of the 24
episodes cost about 3 slow frames, not 60, and no frame outside them
dropped because of the rule.


### COMPOSITION against DXGI_FLIP

All runs below are after the depth fix unless the row says otherwise.
Prediction is the reported onset minus the predicted onset, for flips the
caller did not make late. In a window, "kept on top" means
`HWND_TOPMOST` and the foreground.

| Run | Backend | Frames | Paths | p50 us | p99 us | max us | Dropped | Late target | Early | No statistic |
|---|---|---|---|---|---|---|---|---|---|---|
| window on top, idle, 10 min | COMPOSITION | 36000 | independent 36000 | 0.53 | 2.27 | 8.07 | 12 | 0 | 0 | 0 |
| window on top, patch, 2 min | COMPOSITION | 7200 | independent 7200 | 0.43 | 2.57 | 19.37 | 42 | 1 | 0 | 4 |
| window on top, patch, 1 min, pair 1 | DXGI_FLIP | 3600 | composed 1943, overlay 1560 | (1) | (1) | (1) | 67 | 10 | 15 | 96 |
| same, pair 1 | COMPOSITION | 3600 | independent 3600 | 0.33 | 2.12 | 10.68 | 24 | 0 | 0 | 15 |
| same, pair 2 | DXGI_FLIP | 3600 | composed 1824, overlay 1743 | 0.60 | 5.35 | 16668 | 38 | 9 | 14 | 33 |
| same, pair 2 | COMPOSITION | 3600 | independent 3600 | 0.61 | 2.49 | 4.95 | 5 | 0 | 0 | 0 |
| same, pair 3 | DXGI_FLIP | 3600 | composed 552, overlay 3035 | 0.66 | 4.50 | 16667 | 27 | 1 | 9 | 13 |
| same, pair 3 | COMPOSITION | 3600 | independent 3600 | 0.64 | 2.20 | 4.48 | 0 | 0 | 0 | 0 |
| fullscreen, patch, 2 min (2) | COMPOSITION | 7200 | independent 7200 | 0.54 | 82.07 | 135.88 | 29 | 0 | 0 | 5 |
| fullscreen, patch, 2 min (2) | DXGI_FLIP | 7200 | overlay 7200 | 0.65 | 75.31 | 16668 | 21 | 6 | 4 | 0 |
| fullscreen, patch, 1 min | COMPOSITION | 3600 | independent 3600 | 1.73 | 110.19 | 260.50 | 16 | 0 | 0 | 8 |

(1) A swap-path timeout in this run put the example's prediction table
one frame out of step after it; the example now keeps them in step. The
counts are right.
(2) Before the depth fix, while another program was building on the
machine. Both runs ended at depth 1.

The windowed DXGI_FLIP runs moved between the composed and the overlay
path every 5 to 11 s. Each change to the overlay path showed a few flips
a vblank early, at the depth learned on the composed path, and each is
flagged EARLY. The v0.1 runs in a window not kept on top stayed composed.
Why the path moved was not found. Twice in these runs DXGI's waitable
object freed no slot for 333 ms, and `psyscr_begin()` returned
`PSYSCR_ERR_TIMEOUT`; `screen_flipstats` now counts that and goes on.

The fullscreen rows were 30 to 50 times worse at p99 than the v0.1
fullscreen runs (2.3 us). They ran while another agent ran load on the
laptop; on the quiet machine both backends are back at 2.3 us (see
[The fullscreen regression](#the-fullscreen-regression)).

Holds and overruns on COMPOSITION:

| Run | Frames | Result |
|---|---|---|
| hold 0 to 4 vblanks, window on top | 3000 | 2979 of 2998 on the vblank asked for, 19 late, 0 early |
| hold 0 to 4 vblanks, fullscreen, 30 s (2) | 1800 | 1800 of 1800 on the vblank asked for |
| overrun 20 ms in 1 of 30, window on top | 1800 | 60 of 60 flagged LATE_TARGET, draw phase charged on 60, 0 missed without a flag; 14 other flips dropped or late |
| overrun 17 ms in 1 of 30, window on top (2) | 900 | 24 of 24 flagged, 0 missed without a flag |

The header's CPU time per frame on COMPOSITION, window on top, 10 min:
`psyscr_begin()` without its wait 21.8 us mean and 75.1 us p99, of which
`SDL_PumpEvents()` 11.0 us and the statistics 7.9 us; `psyscr_flip_at()`
without the present call 1.8 us. That misses the 20 us bar, as on
DXGI_FLIP, on the two OS calls. The present call (flush, patch,
`SetBuffer`, `SetTargetTime`, `Present`) took 530 to 1230 us mean in the
alternating runs, against 380 to 730 us for DXGI_FLIP's flush, patch and
`Present`.

Allocations, MSVC debug build, window on top: the C runtime heap hook
counted 2 calls in the frame loop, in runs of 270 and of 570 frames, so
they do not repeat per frame. Both came from the debug runtime making its
per-thread data while an OS component on the frame thread raised and
caught a C++ exception (the stacks end in `RtlRaiseException` and
`_CxxFrameHandler3`). DXGI_FLIP had 0 in 570 frames. The control run
with one `malloc` per frame counted 572.

With one frame in flight the next frame waits for the statistic of the
last one, which comes about 0.3 ms after its vblank on independent flip.
In the 10-minute window run the loop used about 99% of the vblanks
(16.85 ms mean frame time).

### Latency

Latency here is from the return of the present call to the onset the
system reports: the start of scanout of the frame's vblank, NOT light.
The panel's own delay to light is not measured (no photodiode). "First
vblank" is the share of frames shown on the first vblank after the
present call returned. `screen_flipstats` takes the return time just
after `psyscr_flip_at()` returns, a few us later than the call itself.
Fullscreen runs: 1 minute each, idle, patch on, Balanced power plan.

| Power | Backend | Path | Frames | First vblank | p50 ms | p99 ms | max ms |
|---|---|---|---|---|---|---|---|
| AC | DXGI_FLIP (v0.1 control) | overlay | 3600 | 100% | 15.93 | 16.36 | 16.45 |
| AC | DXGI_FLIP | overlay | 3600 | 100% | 15.96 | 16.38 | 16.53 |
| AC | COMPOSITION | independent | 3600 | 100% | 15.63 | 16.19 | 16.39 |
| battery | DXGI_FLIP (v0.1 control) | overlay | 3600 | 100% | 16.01 | 16.33 | 16.44 |
| battery, window on top | DXGI_FLIP | overlay | 7192 | 100% | 16.02 | 16.32 | 16.45 |
| battery, window on top | DXGI_FLIP | composed | 5 | 3 of 5 | 15.68 | 45.39 | 45.39 |
| battery, window on top | COMPOSITION | independent | 7198 | 99.99% | 15.54 | 16.01 | 27.33 |
| battery, window on top | COMPOSITION | overlay | 2 | 0 of 2 | 26.85 | 26.85 | 26.85 |

So on the overlay and independent-flip paths both backends show a frame
on the next vblank after the present, and the time to it is set by when
in the frame the present comes, not by the backend. COMPOSITION's p50 is
0.3 to 0.5 ms lower only because its present call returns later (it
costs more). The composed path costs a vblank more: DXGI_FLIP composed
fullscreen (a window over ours, battery run below) took 32.61 ms p50,
0 of 251 frames on the first vblank. The windowed runs before the
latency columns existed (`r1`, `r4`, the alternating pairs) did not
record the return time, so they give no latency.

The two v0.2 fullscreen runs on battery did not hold the foreground:
DXGI_FLIP ran on the composed path with 3349 of 3600 flips without a
statistic, and COMPOSITION had no statistic on any flip (OCCLUDED). The
v0.1 control just before them was on the overlay path. What covered the
window was not found. The fullscreen limit for that power state was used
up, so the AC set takes the foreground (`--topmost`) for all three.

### The fullscreen regression

The fullscreen p99 of 75 to 110 us in the first v0.2 batch was the
machine's load, not the header. The same binaries on the quiet machine:

| Power | Build | Backend | Path | Frames | p50 us | p99 us | max us | Dropped |
|---|---|---|---|---|---|---|---|---|
| AC | v0.1 header, older psy_rt.h | DXGI_FLIP | overlay | 3600 | 0.39 | 2.32 | 3.65 | 0 |
| AC | v0.2 | DXGI_FLIP | overlay | 3600 | 0.48 | 2.33 | 4.15 | 0 |
| AC | v0.2 | COMPOSITION | independent | 3600 | 0.45 | 2.36 | 8.36 | 0 |
| battery | v0.1 header, older psy_rt.h | DXGI_FLIP | overlay | 3600 | 0.51 | 3.13 | 12.85 | 0 |
| not recorded | v0.2 (probe rule) | DXGI_FLIP | overlay | 3600 | 0.63 | 3.55 | 25.85 | 0 |
| not recorded | v0.2 (probe rule) | COMPOSITION | independent | 3600 | 0.56 | 2.29 | 4.09 | 0 |

The v0.1 control is today's `screen_flipstats.c` with the tier lines
taken out, built on the v0.1 header and the psy_rt.h from before the
P-core steering and the EcoQoS and timer opt-outs. On AC all three agree
to 0.04 us at p99, so neither the v0.2 depth changes nor the psy_rt.h
changes moved the prediction. The first batch ran while another agent
ran spinning load threads and throttle probes on this laptop. Both the
vblank time and the prediction come from the OS's vblank times, so load
can move the error only by delaying the vblank timestamp itself
(interrupt and DPC latency under load); that was not measured
separately. The "not recorded" rows ran before the power state was
logged, on the quiet machine.

### AUTO

AUTO stays DXGI_FLIP. COMPOSITION was better in a window on every
measure but the cost of the present call: fewer drops, no early flips,
fewer flips without a statistic, a lower p99. In fullscreen on the
quiet machine it did as well, not better (2.36 against 2.33 us p99, no
drop on either, the same latency to the next vblank), its present call
costs more everywhere, it needs
Windows 11, and its composed onset is DWM's plan (tier 3) where
DXGI_FLIP's composed onset is tier 2. A task that runs in a window on
Windows 11 can ask for COMPOSITION by name.

### Tiers

| Tier | Backend and path | Why, from the measurements |
|---|---|---|
| 1 | DXGI_FLIP overlay or independent flip | DXGI's vblank time on a flip the OS made; v0.1 prediction p99 2.3 us |
| 1 | COMPOSITION independent flip | DisplayedTime 13 us after the scanline vblank at p50, 77 us at p99, and it moves with a late frame |
| 2 | DXGI_FLIP composed | DWM's vblank time; not checked against another source |
| 2 | any flip off the vblank grid (GRID_UNSTABLE) | the panel stretched frames after a late frame, up to 4.9 ms off the grid |
| 3 | COMPOSITION composed or overlay frame (ONSET_PLANNED) | DWM reports the time about 15 ms before the vblank |
| 3 | any ESTIMATED flip | no OS time at all: the planned vblank |
| SIM | the simulated display | no display |

Where the scheme does not fit: tier 1 on COMPOSITION rests on 2097
frames against the scanline on one machine, and its p99 of 77 us is
larger than DXGI_FLIP's prediction error, so "tier 1" does not mean the
same error bound on both backends. `desc.min_tier` flags flips; it does
not stop a run, because a path can change in the middle of a trial and
only the caller knows what to do with that trial.


## Flip hooks (v0.3.0)

The design is in the scratchpad note `screen_hooks_design.md`; this
section records what was built and measured. All runs: one laptop, on
battery, Balanced power plan, `screen_flipstats` built with MSVC 19.4
/O2, 2026-10-05, under the shared measurement lock.

### What VPixx devices read from the frame

| Code | Pixels | Where | Bits | Source |
|---|---|---|---|---|
| Pixel Mode | 1 | the top-left pixel of each frame | digital out 0 to 7 from red, 8 to 15 green, 16 to 23 blue; modes RGB, GB, B | [VPixx, Sending Triggers with Pixel Mode](https://docs.vpixx.com/vocal/sending-triggers-with-pixel-mode), [Datapixx('EnablePixelMode')](http://psychtoolbox.org/docs/Datapixx-EnablePixelMode) |
| Pixel sync | "at least 8" recommended, caller-defined RGB | a raster line set with `DPxSetVidPsyncRasterLine`; Psychtoolbox draws it on scanline 0 from x = 10 and blanks that line | all 24 bits of each pixel compared | [VPixx, Example: sending a digital trigger on Pixel Sync](https://docs.vpixx.com/vocal/example-sending-a-digital-trigger-on-pixel-sync), [libdpx](https://www.vpixx.com/manuals/libdpx/html/libdpx.html), [PsychDataPixx.m](https://raw.githubusercontent.com/Psychtoolbox-3/Psychtoolbox-3/beta/Psychtoolbox/PsychHardware/DatapixxToolbox/DatapixxBasic/PsychDataPixx.m) |

VPixx also documents a "stereo blue line" for the VIEWPixx /3D
([Setting up a 3D display](https://docs.vpixx.com/matlab/setting-up-a-3d-display));
its pixels were not found in the pages read, so there is no preset. The
pages read do not say in which DATAPixx video modes Pixel Mode works or
whether pixel sync searches a whole line. Both presets
(`psyscr_slot_pixel_mode`, `psyscr_slot_psync`) follow the sources above.

### Codes

| Run | Backend, path | Frames | Self test | Read back | Differed | Codes zone, mean / p50 / p99 us | Code risk |
|---|---|---|---|---|---|---|---|
| ROW by ClearView per pixel, fullscreen, 1 min | DXGI_FLIP, overlay | 3600 | pass | 720 | 0 | 3.82 / 3.10 / 18.3 | 0x0002 (advanced color) |
| same | COMPOSITION, independent | 3600 | pass | 720 | 0 | 4.24 / 3.20 / 14.8 | 0x0002 |
| ROW by UpdateSubresource, fullscreen, 30 s | DXGI_FLIP, overlay | 1800 | pass | 360 | 0 | 5.31 / 3.90 / 22.5 | 0x0002 |

Each frame drew a 1-pixel Pixel Mode code and an 8-pixel pixel-sync row,
and read them back every 10 flips. The zone is the CPU time of the D3D11
calls, including the staging copy on read-back frames. One ClearView per
pixel was cheaper for 8 pixels, so rows of up to 8 pixels use it and
longer ones use UpdateSubresource; the longer case was not timed.

The read-back and the self test see the back buffer only. The describe
line on this laptop says `color=wcg`: Windows runs auto color management
on the internal panel, so DWM and the display kernel convert colors, and
every code here carries `CODE_RISK_ADVANCED_COLOR`. No device has read a
code from this laptop.

### Triggers

One trigger per frame on a TIME_CRITICAL worker; the callback did
nothing. "Moved" counts triggers armed or re-armed for another vblank;
"GPU moved" those moved by the fence check, with how many of those
frames were in fact late.

| Run | Backend, path | Frames | Fired minus deadline, p50 / p99 / max us | Moved | GPU moved (frame late) | Fired early | Fired late | Flip cost, mean us |
|---|---|---|---|---|---|---|---|---|
| fullscreen, codes, 1 min | DXGI_FLIP, overlay | 3600 | 1.57 / 58.7 / 3200 | 0 | off | 0 | 0 | 12.0 |
| fullscreen, codes, 1 min | COMPOSITION, independent | 3600 | 2.05 / 102 / 646 | 0 | off | 0 | 0 | 12.0 |
| window, `--miss 300`, no fence | DXGI_FLIP | 7200 | 1.98 / 238 / 7933 | 62 | off | 72 | 0 | 12.6 |
| window, `--miss 300`, fence | DXGI_FLIP | 7200 | 0.98 / 215 / 3240 | 137 | 71 (70) | 0 | 0 | 12.4 |
| window, `--miss 300`, fence | COMPOSITION | 7200 | 0.97 / 271 / 3655 | 70 | 70 (70) | 1 | 0 | 11.4 |
| window, idle, fence | COMPOSITION | 7200 | 1.16 / 300 / 2103 | 0 | 0 | 1 | 0 | 14.4 |
| fullscreen, idle, fence, 1 min | DXGI_FLIP, overlay | 3600 | 0.99 / 113 / 838 | 0 | 0 | 0 | 0 | 13.6 |

- `--miss 300` gives 3 frames in a row 25 ms of GPU work every 300
  frames. Without the fence every one of their triggers fired a frame
  early (72 of 72); the after-flip hook and the ring said so. With the
  fence, 140 of 142 were moved before they fired; the other 2 were frames
  whose drop the fence could not see.
- The 62 moves on DXGI_FLIP without the fence are the window's path
  changes: a flip shown a vblank early (EARLY) completes before its
  trigger fires, and the trigger moves to it.
- No false move: no trigger fired after its frame (fired late 0), and no
  GPU move in 10800 idle frames. The fence stays off by default because
  this is one GPU.
- The flip cost rose from 4.5 us (codes, no triggers) to 12 us with a
  trigger: arming takes the trigger lock and wakes the worker
  (`psyrt_worker_submit`). Begin and flip together miss the 20 us bar.
- A trigger whose record completed before it fired is reported PENDING
  by the after-flip hook: 0 to 3 in 3600 fullscreen frames, up to 131 in
  7200 windowed frames with path changes.
- The table above uses one word for two things. In it, "fired late" is
  a mismatch (PSYSCR_TRIG_FIRED_LATE: the trigger fired, then its frame
  was shown a vblank EARLIER), not the worker's lateness. The worker's
  lateness is the "fired minus deadline" column. Later tables call the
  mismatch "fired a vblank late".

### The trigger tail (v0.3.0, 2026-10-05)

Work notes, kept as the work goes, so a restart loses nothing. All runs
on AC power (`power: AC` in the flipstats output), Balanced plan, the
laptop panel only, 800 x 600 window unless the row says fullscreen.
"Late" in these notes means fired minus deadline over 20 us.

The split. `psyscr_trigger_info` has `woke_ns` (the clock as psy_rt.h's
worker entered the job, after its spin) and `lock_ns` (the wait for the
trigger lock). Fired minus deadline = wake (psy_rt.h's own lateness) +
lock + dispatch.

| Run (DXGI_FLIP, window, 1200 frames) | Fired - deadline p50 / p99 / max us | Wake p99 / max | Lock p99 / max | Dispatch p99 / max |
|---|---|---|---|---|
| default | 0.76 / 18.4 / 356 | 17.7 / 356 | 0.4 / 1.2 | 1.4 / 7.8 |
| channel offset 4 ms (deadline off the vblank) | 0.52 / 1.74 / 32.6 | 0.40 / 31.7 | 0.3 / 9.9 | 0.8 / 8.7 |
| spin window 3 ms (default 1.2 ms) | 1.10 / 48.7 / 215 | 47.3 / 215 | 0.5 / 2.4 | 1.6 / 4.8 |

- The lock is not the cause: its wait is at most 13 us in every run.
  The whole tail is in the wake.
- The spin window is not the cause: 3 ms does no better than 1.2 ms.
- The tail is tied to the vblank: with the deadline 4 ms after it, p99
  falls from 18 to 50 us to 1.7 us.
- Repeated default runs vary widely: p99 18 to 240 us, max 0.36 to 23
  ms, in 20 to 30 s windows minutes apart.

Where the time goes. A probe (now removed) armed psy_rt.h's worker 300
us early and spun to the deadline inside the job, logging every gap over
5 us between two clock reads, with the CPU before and after and the
thread's cycle count (QueryThreadCycleTime) across the gap. In 1800
frames: 962 gaps over 20 us, 740 of them starting 50 to 100 us before
the vblank time, 643 crossing the deadline, 50 to 400 us long. Across a
gap the thread ran almost no cycles and stayed on its CPU (2 of 962
moved). The thread was stopped on its CPU while something else ran
there: an interrupt or a DPC, or a thread of higher priority.

- Not a thread of priority 16 (dwm.exe has 3, csrss.exe 2): with the
  worker raised to 18 by MMCSS ("Pro Audio", critical), the tail stayed
  (p99 166 and 241 us against 108 and 223 us without, alternated).
- A DPC: `% DPC Time` per CPU (typeperf, 1 s samples) shows 2 to 3% of
  one P-core (330 to 500 us per frame at 60 Hz) during a run, on the CPU
  the worker spun on (CPUs 0 to 2 in that run). Pinned to E-core 10, the
  DPC ran on P-cores 2 and 4 and the worker fired at p99 5.4 us.
- Pinned per CPU (900 frames each): CPU 0 p50 31 us, CPU 1 p50 35 us
  (the same core), CPU 2 p99 292 us, CPU 3 p99 206 us, CPU 4 p99 120 us,
  CPU 6 p99 4.2 us, CPU 10 p99 6.1 us, CPU 15 p99 7.2 us. A pinned
  thread cannot move off a CPU that is taken: max 0.4 to 48 ms.

So the cause: psy_rt.h's worker prefers the P-cores, and on this laptop
the display's vblank DPC runs on a P-core, often the one the worker is
spinning on. Raising the priority cannot help against a DPC.

Placement trials (window, 1200 frames each, with the self-arming below):

- A soft CPU set (SetThreadSelectedCpuSets) of the E-cores did not keep
  the worker there: Windows still ran it on P-cores 0 to 6, p99 37 and
  132 us.
- A hard mask of the 8 E-cores: p99 2.1 and 2.6 us, 6 and 7 triggers over
  20 us late. A mask of 4 E-cores did as well (1.3 and 2.9 us); CPUs
  12 to 15 once gave p99 2.8 ms. The gap probe with the 8 E-core mask: 4
  and 0 gaps over 20 us before the deadline, none crossing it.
- Pinned to E-core 10: p99 1.0 and 1.7 us, but a pin cannot leave its CPU.

The fix: on Windows on a hybrid CPU, the worker's on_start sets a hard
affinity mask of the E-cores (all logical CPUs below the highest
EfficiencyClass) and drops psy_rt.h's P-core CPU sets. `trigger_cpu = -1`
keeps psy_rt.h's placement; `k > 0` still pins.

Arming cost. `psyscr.trigarm` was 3.8 us mean on AC (8 us on battery),
of which `psyscr.trigsubmit` (psyrt_worker_submit: the worker's lock, an
event and a condition variable) was 3.3 us. The trigger lock itself was
uncontended. So the arming cost is the wake-up, not the lock, and a
lock-free job slot would not remove it. The fix: after a flip trigger
fires, the worker arms itself 50 us before the next vblank's deadline
(`PSYSCR__SPEC_EARLY_NS`), and the frame thread submits only when its
wake is earlier than the one armed. In steady state the frame thread
never submits; the worker wakes 50 us early, finds nothing due, and
re-arms itself for the real deadline. With no trigger in the next frame
the worker spins one window for nothing. The race guarantees are
unchanged: every state change is still under the one lock, a FIRING job
cannot move, and the test checks it.

Lock waits. `psyscr__lock` now tries the lock first and records a
`psyscr.lockwait` zone only when it has to wait: 0 to 15 waits in 1200 to
3600 frames, at most 2.1 us.

### Before and after (v0.3.0, 2026-10-05, AC)

"Before" is the header as it was at the start of this work (worker on
the P-cores, a submit per trigger), built from a copy; "after" is the
current header. One trigger per frame, a no-op callback, the laptop panel,
Balanced plan, `power: AC` on every run. "Over 20" and "over 200" count
triggers that fired that many us or more after their deadline.

Fullscreen, 3600 frames each:

| Run | Fired - deadline p50 / p99 / max us | Over 20 / over 200 | Wake p99 | trigarm mean us | flip_at mean us | Drops |
|---|---|---|---|---|---|---|
| DXGI_FLIP, before | 0.97 / 31.5 / 369 | 152 / 2 | 30.7 | 3.83 | 5.03 | 0 |
| DXGI_FLIP, after | 0.46 / 1.66 / 406 | 4 / 1 | 0.56 | 0.94 | 2.56 | 0 |
| DXGI_FLIP, after, again | 0.45 / 2.55 / 2108 | 11 / 4 | 0.35 | 0.73 | 2.01 | 0 |
| COMPOSITION, before | 0.99 / 29.2 / 1627 | 122 / 2 | 28.2 | 4.08 | 5.28 | 0 |
| COMPOSITION, after | 0.47 / 1.76 / 9239 | 6 / 6 | 0.59 | 1.16 | 3.04 | 3 |
| COMPOSITION, after, again | 0.48 / 1.60 / 703 | 7 / 3 | 0.45 | 0.64 | 1.84 | 5 |

The fullscreen runs used the 6-minute budget. They ran between 19:15 and
19:47, inside the hour in which another agent ran GPU pixel tests and
started and stopped Docker Desktop outside the measurement lock, so their
tails may carry that load. They were not repeated in fullscreen.

Window, the final batch, repeated on a quiet machine (no docker, cl,
gcc or gfx test process in `tasklist` before each run, GPU 3D engine at
0.2%, under the lock, 19:52 to 20:00), 3600 frames each, alternated:

| Run | Fired - deadline p50 / p99 / max us | Over 20 / over 200 | trigarm mean us | flip_at mean us | Drops |
|---|---|---|---|---|---|
| DXGI_FLIP, before | 2.66 / 169 / 12563 | 1116 / 22 | 11.7 | 15.8 | 1 |
| DXGI_FLIP, after | 0.66 / 3.15 / 4460 | 18 / 8 | 1.46 | 4.34 | 1 |
| DXGI_FLIP, before | 2.37 / 148 / 4857 | 1023 / 19 | 7.88 | 10.7 | 2 |
| DXGI_FLIP, after | 0.48 / 2.40 / 31268 | 18 / 15 | 1.17 | 3.49 | 1 |
| COMPOSITION on top, before | 2.56 / 140 / 2460 | 877 / 18 | 10.7 | 13.9 | 13 |
| COMPOSITION on top, after | 0.46 / 1.08 / 861 | 12 / 2 | 0.62 | 1.71 | 0 |
| COMPOSITION on top, before | 0.98 / 38.8 / 669 | 227 / 1 | 4.03 | 5.18 | 0 |
| COMPOSITION on top, after | 0.46 / 0.96 / 347 | 6 / 1 | 0.69 | 1.87 | 0 |

The first runs of this batch had a slow frame thread in both builds
(trigarm 8 to 12 us before); the after build is faster in each pair.
An earlier window batch (19:37 to 19:43, possibly under the other agent's
load) agreed: p99 32 to 94 us before, 1.3 to 3.0 us after; maxima 0.88
to 1.9 ms before, 0.55 to 43 ms after.

The OS tail. examples/rt_jitter.c, 1800 waits of 16.667 ms per spin
window, no display involved, AC, under the lock:

| Placement | Spin 1.2 ms (default): p50 / p99 / max us | Spin 0: p50 / p99 / max us |
|---|---|---|
| E-cores (`start /affinity FF00`), 19:26 | 0.0 / 0.2 / 2603 | 448 / 902 / 2646 |
| P-cores (default), 19:28 | 0.0 / 21.2 / 2996 | 377 / 844 / 3689 |
| E-cores, quiet, 20:00 | 0.0 / 0.1 / 32559 | 338 / 663 / 1049 |
| P-cores, quiet, 20:02 | 0.0 / 0.0 / 20.5 | 319 / 634 / 746 |

On the simulated display (no GPU, no window), 7200 triggers on the
E-cores: p99 1.68 us, max 2232 us, 2 over 200 us.

What this says:

- The p99 target (20 us) is met in every after run; the before runs
  missed it in every run, by the vblank DPC.
- The max target (200 us) is missed in every run, before and after. The
  waits that make it are rare (1 to 15 a minute after, 1 to 22 before)
  and come from the OS: rt_jitter without a display, on the same cores
  and the same spin window, woke 2.6 ms and 32.6 ms late in two of four
  runs. A wider spin window covers a late timer (spin 0 shows the timer
  alone up to 3.7 ms late), not a thread kept off its CPU, at the cost
  of a spinning core.
- The E-cores carry their own rare stall: the 32.6 ms rt_jitter wake and
  the 31 and 43 ms trigger waits were on E-cores. P-cores had stalls of
  12 to 48 ms in the earlier pinned and default runs. The data do not
  show which placement has fewer such stalls; they do show the E-cores
  remove the DPC tail. `trigger_cpu = -1` restores the P-cores.
- begin() cost the frame thread more in some after runs (16 to 32 us
  mean against 14 to 24 us before, in runs minutes apart); with the
  worker pinned to E-core 10 and on the P-cores in the same minutes it
  was the same, and lock waits were at most 2.1 us, so the change is not
  the header's; whether the DPC moved to the frame thread's CPU was not
  measured.
- COMPOSITION fullscreen dropped 3 and 5 frames a minute after and 0
  before, in the window of possible outside load; in the quiet window
  batch the after runs dropped 0 and 0. Not resolved with these few runs.

Mutations. The core test catches each of these 24 changes to the header
(`gcc -std=c11 -O1` on WSL, one build per change): the snap without its
lead, the depth on one vote, drops not counted, ESTIMATED not set, the
prediction a vblank late, the hold a vblank early, LATE_TARGET not set, a
path change not adopted at once, the native depth raised on one miss,
EARLY not set, an estimated flip keeping its tier, a trigger not moved
after a late present, a taken trigger moved, a taken trigger fired again,
an armed trigger not moved to the vblank shown, the fence never moving,
the mismatch direction swapped, the channel offset ignored, the
trigger_at offset ignored, FLUSHED not set, triggers fired out of deadline
order, the frame thread submitting on every change, no self-arming, and
self-arming after the deadline. The native depth and EARLY changes were
first missed; `test_one_miss_and_early` now catches them (one drop on a
backend that holds frames leaves the depth at 1; a DXGI-like path whose
depth falls from 2 to 1 shows a flip a vblank early, and EARLY is set on
that flip and on no other). The previous agent's list of 20 was lost;
this list replaces it.

### Where the trigger worker runs: the rule and this machine

The rule is: keep the trigger worker off the CPUs that service the
display's vblank DPC. A DPC preempts every thread at any priority, so a
worker that spins on that CPU at the vblank waits until the DPC ends.

The E-cores are this machine's answer to that rule, not the rule. The
measurements that justify it here (i7-1360P, Iris Xe, Windows 11,
Balanced, AC; all above):

- The gap probe: in 1800 frames, 740 gaps over 20 us started 50 to 100
  us before the vblank, 643 crossed the deadline, 50 to 400 us long, with
  the worker's thread running almost no cycles and not changing CPU.
- `% DPC Time` per processor during a run: 2 to 3% of one P-core (330
  to 500 us per frame), on the P-cores the worker ran on; with the worker
  pinned to E-core 10, the DPC ran on P-cores 2 and 4 and the worker
  fired at p99 5.4 us.
- MMCSS priority 18 did not remove the tail, so it is not a thread.
- A hard mask of the E-cores: p99 1 to 3 us in every run after.

On another machine this can differ. A CPU of one core type has no
E-cores; the default is then psy_rt.h's placement, which can share a CPU
with the DPC. Another GPU or driver can route its interrupt elsewhere, an
administrator can set the device's MSI interrupt affinity policy, and
Windows can steer the interrupt toward a CPU that is awake. On such a
machine the E-cores may be the busy ones. To check a rig: run
`screen_flipstats --trigger` (it prints the triggers over 20 us late per
CPU) and `typeperf "\Processor(*)\% DPC Time"` during the run, and set
`desc.trigger_cpu` from what they show.

Later (not built): a diagnostic at open that samples the DPC count or DPC
time of each CPU for about 0.5 s while the swap path flips, and places
the worker on the CPUs with the least. The documented way is PDH (the
counters `\Processor(*)\DPCs Queued/sec` and `% DPC Time`, as typeperf
reads them), which needs no privilege. The undocumented ways are
NtQuerySystemInformation with SystemProcessorPerformanceInformation
(per-CPU DPC and interrupt time in fields the SDK calls Reserved1) or
SystemInterruptInformation (per-CPU DPC counts). The cost of the sample
at open, and whether 0.5 s finds the vblank DPC's CPU every time, are
not measured.

### The OS gamma ramp

`examples/screen_gamma.c`, without changing what is shown: the laptop's
ramp is the 8-bit identity; setting the same ramp again and reading it
back was exact; GetDeviceGammaRamp took 0.8 to 20 ms (the first call is
slow), SetDeviceGammaRamp 4.6 to 18 ms. Because the ramp was already the
identity, no fullscreen run set one, and the identity path of
`psyscr_open` did not run on hardware. Microsoft documents that
SetDeviceGammaRamp can report success without setting a ramp, that other
programs and the OS may overwrite it, that display events reset it, and
that it is undefined under HDR
([SetDeviceGammaRamp](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-setdevicegammaramp)).
No primary source says whether it acts on independent-flip or overlay
frames, or how it combines with Night Light, which uses the GPU color
pipeline ([display calibration pipeline](https://learn.microsoft.com/en-us/windows/win32/wcs/display-calibration-mhc));
that needs a capture card or light, and is not claimed.

### Two frames planned for one vblank (v0.3.0, 2026-10-05)

The video agent's report, windowed DXGI_FLIP (composed, 640 x 360, AC):
begin() sometimes planned two frames in a row for one vblank (their
predicted onsets 136 ns apart, after flips flagged EARLY), and f.vblank
sometimes rose by 2 before a drop was reported.

Reproduced on the virtual clock (`test_flap_prediction`): a DXGI-like
scripted presenter whose path moves between composed (depth 2) and
overlay (depth 1) every 9 presents, with a random wait of up to a third
of a period per frame. 300 frames: 16 frames planned on the vblank of the
frame before, 48 early flips in runs of 3.

The cause, in two parts:

1. A flip planned at depth 2 when the depth has fallen to 1 shows a
   vblank early and completes. begin() kept "one frame per vblank" only
   against flips still in flight and the newest vblank shown, so the next
   frame was planned for the vblank the early frame had been planned for.
   The 136 ns is the grid's period refit between the two begin() calls,
   not a different vblank.
2. After a path change the header adopts a new depth on one flip. The
   first flip on the new path was shown on the vblank after the flip
   before it: it had waited behind that flip in the queue, so its count
   matched the old depth (2). That match closed the one-flip window, and
   three flips in a row had to vote the depth down: 3 early flips per fall.

The fix: the screen keeps the vblank flip_at() last planned, and begin()
and flip_at() never plan at or before it. A flip shown on the vblank right
after the flip before it no longer closes a path change's window (for up
to 3 such flips). On the virtual clock: 0 frames on the vblank of the
frame before, 16 early flips, each alone. The price: the early frame
stays on the screen for the vblank it was planned for, so that vblank
shows it a second time, where before a second frame was planned there.
The prediction stays monotone, which psy_timeline.h and psy_video.h
assume.

The rise by 2 is not a fault. When the depth rises, the flip planned at
the old depth shows late (a drop), and the frame after it is planned 2
vblanks on; its begin() runs before the drop's record completes, because
one frame is in flight. On the virtual clock every vblank that no frame
was planned for followed an early flip or preceded a late one (the test
checks it). On a backend that holds frames (COMPOSITION), the flapping
script ran at half rate on the overlay path, because the depth there is
lowered only on evidence (DEPTH); that is the depth rule, not this bug,
and was not changed.

Windowed check (DXGI_FLIP, `--windowed --topmost`, AC, under the lock,
Docker Desktop running; flipstats counts frames planned at or before the
vblank of the frame before):

| Build | Frames | Planned at or before the frame before | Early | Dropped |
|---|---|---|---|---|
| fixes reverted | 3600 | 1 | 3 | 2 |
| fixed | 5400 | 0 | 3 | 4 |

The window stayed on the overlay path except at start (6 to 8 composed
frames), so these runs saw few depth falls; the virtual-clock test is the
evidence, the window runs only confirm that nothing else broke.

### psy_video.h's requests (v0.3.0)

- `psyscr_frame.done`, `n_done`, `done_lost`: every record completed
  since the begin() before, in ring order, including those completed in
  flip_at() during a hold and the ESTIMATED ones. In the windowed runs
  above, f.done carried 3599 of 3600 and 5399 of 5400 records (the last
  completes in close()).
- `psyscr_native()`: the D3D11 device and immediate context, ANGLE's EGL
  display, the adapter LUID. Borrowed pointers, valid until close(); the
  device is free-threaded, the context is the frame thread's and ANGLE's.
  In the windowed runs all three were set and the LUID was
  00000000:0001e8e6. Not done: a device with VIDEO_SUPPORT and
  multithread protection (psy_video.h's zero-copy request).

Mutations now caught: 29 (the 24 above, plus begin() and flip_at()
planning the last planned vblank again, a queued flip closing the
window, completed records not listed, and the list not cleared per
begin()).

## Not measured

- Light. No photodiode was attached. `tests/loopback/psy_screen_loopback.c`
  is built, not run. Every onset here is DXGI's vblank time.
- Any refresh other than 60 Hz; any GPU other than the Iris Xe; Windows 10.
- X11, Wayland, macOS and the web: stubs.
- A DWM miss on the composed path of COMPOSITION: a second window loading
  the GPU did not cause one, so whether the record shows it is unknown.
- COMPOSITION on a machine whose QPC does not run at 10 MHz, on ARM64
  (refused), and on Windows 10 (not available).
- Variable refresh: refused.
- Two physical displays. The group calls ran on two windows on one display.
- The keyboard itself: its scan, USB polling and the HID stack are not in
  the input times above.
- Any code read by a device (VPixx or Bits#), and codes on a display
  without auto color management.
- The OS gamma ramp set to identity on hardware (the ramp here was
  identity), and its effect on independent-flip and overlay frames.
- How late the after-flip hook runs, per backend (derived only, in the
  design note).
