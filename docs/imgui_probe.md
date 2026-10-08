# Dear ImGui operator console probe, 2026-10-07

This note records a probe that asked whether Dear ImGui, called from C
through cimgui, can be the operator console of a rig without disturbing
the timed stimulus screen. The plan that asked for it is
[rig_spec.md](rig_spec.md) section 4.2 ("Untimed screens", the proposal
of 2026-10-06), section 6 (runner protocol, operator console) and
section 13 (the open question on an untimed screen beside a timed one).

The probe code is in `C:\tmp\psy-work\imgui\` on the development machine
and is not kept in the repo: `src/imgui_probe.c` (about 1080 lines of
C), a 7-line C++ file that compiles the two stock backends with C
linkage, a 147-line generated `GLES3/gl3.h` shim that loads GL from
ANGLE at run time, two build scripts and the run logs in `runs/`. The
numbers below are the record.

## Recommendation

1. **Dear ImGui through cimgui can serve the operator console as a
   second window (configuration a).** On this machine, with the console
   in its own window on its own D3D11 device, swapchain and ANGLE
   context, drawn on the frame thread after the timed flip and presented
   without waiting, the timed window showed the same flip records as
   without it: one flip interval over 1.5 periods in 72,000 measured
   frames of windowed console runs (4 runs of 3 minutes and one of 8),
   against none in 50,400 frames without ImGui; the prediction error
   p99 within 0.1 us of the run without the console; independent flip
   on every COMPOSITION frame with or without it, and on DXGI_FLIP 68
   to 122 composed frames (tier 2) in a 3-minute run with the console
   against 68 without; the header's own cost unchanged. The
   one long interval was a 32 ms GPU stall that both devices saw
   (section 2.2); it did not come back in an 8-minute rerun (section
   2.3). The console never blocked: 0 `DXGI_ERROR_WAS_STILL_DRAWING`, at
   most 4 frames a run skipped because its swapchain had no free slot.
2. **This is a one-display result.** The machine has one display. The
   two windows were side by side on it, the timed window kept on top in
   a window, not fullscreen on a display of its own. The claim that
   rig_spec 4.2 gates (a timed display in independent flip beside an
   untimed one on a second display) is still not measured. Run the
   probe on a two-display rig before the untimed screen is built.
3. **Fix the IME routing before any ImGui text field ships.** The stock
   SDL3 backend starts SDL text input on whichever window has the
   keyboard focus, not on the ImGui window. When the operator leaves a
   console text field active and the stimulus window gets the focus, the
   stimulus window's text input turns on: its keys leave the raw path
   (stamped 11.4 ms after they were sent instead of about 0.25 ms), and
   `psy_screen.h` records nothing, because the backend calls SDL
   directly and not `psyscr_text_input()` (section 5). A 15-line
   replacement for `platform_io.Platform_SetImeDataFn` fixes it: the
   console window only, or `psyscr_text_input()` for an overlay.
4. **Use the overlay (configuration b) for development only.** ImGui in
   the stimulus window did not cost a flip either, but it adds 0.1 to
   0.3 ms of CPU inside `psyscr_flip_at()` and 0.1 to 1.1 ms of GPU time
   to the timed frame, its pixels are device values that bypass the
   calibration (section 4), and a participant sees it.
5. **Put the console on the frame thread only while it stays this
   small.** It cost the frame thread 0.52 ms mean (idle) and 0.88 to
   0.93 ms mean (busy) per frame, at most 3.9 ms, after the flip, where
   the thread otherwise waits for the next slot. A console thread or a
   separate process (a runner-protocol client, as rig_spec section 6
   plans) would take that time off the frame thread; neither was built
   (Not tested).
6. **The player needs a C++ compiler for ImGui, not a C++ runtime.**
   Six C++ files (Dear ImGui, cimgui, the two backends) build with
   exceptions and RTTI off. MinGW gcc links the program with `gcc`, no
   libstdc++, after `-fno-threadsafe-statics`; MSVC links against
   VCRUNTIME140 and the UCRT only, no MSVCP140. Luau, the proposed
   script engine, is C++ too, so the player needs a C++ compiler
   either way. The headers stay C.

## Conditions

- Windows 11 25H2 laptop, Intel Iris Xe driving the internal 1920 x
  1200 panel at 60.0008 Hz, an NVIDIA RTX A500 with no outputs. One
  display: a second display was not attached (checked with WMI and
  `System.Windows.Forms.Screen`).
- AC power. Auto color management on (`color=wcg` in the describe line).
  DWM composes the desktop; the timed window is kept on top.
- ANGLE 2.1.23876 (git fffbc739779a), the copy in Docker Desktop's
  front end. SDL 3.4.0. `psy_screen.h` v0.3.2, `psy_gfx.h` at the
  tree of 2026-10-07.
- cimgui master `125f397` (2026-09-14) with its Dear ImGui submodule at
  v1.92.9b, docking branch (`b48d1af`). cimgui's own `cimconfig.h`, which
  keeps `IM_ASSERT` on in release builds. Khronos `GLES3/gl3.h` from
  OpenGL-Registry `6af574a`, `KHR/khrplatform.h` from EGL-Registry
  `db3425b`.
- Toolchains: MSVC 19.4 (VS 2022), `/O2 /MD`, C11 for the probe and
  C++ with `/EHs-c- /GR-` for ImGui; MinGW-w64 gcc 16.1 (winlibs),
  `-O2`, C11, and C++ with `-fno-exceptions -fno-rtti
  -fno-threadsafe-statics`. Every timing run used the MSVC build. The
  MinGW build ran the console smoke test.
- Every timing run went through the shared measurement lock
  (`psy-guard.sh time`), which waits for a quiet machine (no compiler or
  test process, CPU load at most 20 %). Other programs were open (VS
  Code, Docker Desktop, other workers' idle windows).
- The frame thread ran at `THREAD_PRIORITY_TIME_CRITICAL`.

## Method

### The timed window

`psy_screen.h` and `psy_gfx.h` as a rig uses them: an 800 x 600 window at
(40, 120), kept on top and in the foreground, a 0.30 gray background
and four drifting gabors at contrast 0.3 (the console's sliders drive
them). Each frame: `psyscr_begin()`, `psygfx_begin/draw/end`,
`psyscr_flip_at(f.onset)`. The flip records come from `f.done`; the
header's cost from its trace zones, as in `examples/screen_flipstats.c`.
GPU time is a pair of D3D11 timestamp queries on the screen's own device
(`psyscr_native()`): one after `psyscr_begin()`, one in the present
callback, read 8 frames later without a flush. These are D3D11 calls
beside ANGLE, not ANGLE's `GL_EXT_disjoint_timer_query`, which cost 115
to 140 us a frame in docs/psy_screen.md; their own CPU cost was not
measured apart, but the header's zones were the same as in earlier
runs without them. The first 120 frames are warm-up and are not in the
tables.

Backends: COMPOSITION and DXGI_FLIP in a window, DXGI_FLIP fullscreen
(AUTO). In a window kept on top, COMPOSITION got independent flip on
every measured frame; DXGI_FLIP got the overlay path on 99.4 % of
frames.

### Configuration (a): the console window

A second SDL window, 960 x 1000 at (900, 60), beside the timed window,
not over it. The probe makes its own D3D11 device on the timed screen's
adapter (by LUID), a `DXGI_SWAP_EFFECT_FLIP_DISCARD` swapchain with 2
buffers, a frame latency waitable object and a maximum latency of 1, and
an ANGLE context on a client-buffer pbuffer, the same way
`psy_screen.h`'s DXGI_FLIP backend does (the probe calls the header's
private EGL loader). After `psyscr_flip_at()` returns, the frame thread
does one console frame:

1. `WaitForSingleObject(waitable, 0)`; if no slot is free, skip the
   frame and count it;
2. `eglMakeCurrent` the console context;
3. `ImGui_ImplOpenGL3_NewFrame`, `ImGui_ImplSDL3_NewFrame`, `igNewFrame`,
   the panels, `igRender`;
4. flip the draw lists to GL's row order (below), clear,
   `ImGui_ImplOpenGL3_RenderDrawData`, `glFlush`;
5. `Present(1, DXGI_PRESENT_DO_NOT_WAIT)`.

The next `psyscr_begin()` makes the timed context current again (its
acquire does that every frame). Events: the frame loop reads every event
with `psyscr_poll()` and passes the ones whose window id is the
console's to `ImGui_ImplSDL3_ProcessEvent()`.

With the timed window fullscreen, the console window was behind it
(covered, still drawing and presenting). That puts the console's GPU
work and presents beside a fullscreen timed window, but not its
composition on a second display.

### Configuration (b): the overlay

The same panels, scaled to the window, drawn into the timed window: the
UI is built after `psygfx_end()` (so it counts in the DRAW phase), and
`ImGui_ImplOpenGL3_RenderDrawData()` runs in `psyscr_on_present()`,
after `psy_gfx.h`'s output stage and before the header's flush,
photodiode patch and codes. `psy_gfx.h` drops its GL state cache when
`psyscr_gl_epoch()` moves, so the backend's state changes do not leak
into the next frame.

### The panels

- **Trials**: a table with 5 columns and a list clipper, 200 rows (idle)
  or 10,000 rows (busy).
- **Flips**: the timed screen's drops and late targets, and two
  `igPlotLines` of the last 600 flip intervals and DRAW phases, from the
  flip records.
- **Parameters**: six sliders bound to the gabors, a check box, a text
  field.
- **Log**: the last 1024 lines with a clipper, scrolled to the end.
- **Busy only**: the table scrolled 7 px every frame, 10 log lines a
  frame, a window with a 10,000-point polyline (`ImDrawList_AddPolyline`,
  no downsampling, as ImPlot draws a line), and a thread that posts
  `WM_MOUSEMOVE` (and every 8th time `WM_MOUSEWHEEL`) to the ImGui window
  about 60 times a second. Posted messages go through SDL's message
  path on the frame thread like real input; the real cursor does not
  move.

All panels update every frame (60 Hz).

### Row order

ANGLE's client-buffer pbuffer puts GL row 0 at the top of the screen
(psy_screen.h, GL STATE). The stock OpenGL3 backend assumes the
opposite. The probe mirrors the vertex y and the clip rectangles of the
draw data before `RenderDrawData` (2 us idle, 13 to 23 us mean busy on
the console; up to 2.1 ms once). A screenshot confirmed upright text.
Changing two lines in the backend (the projection and the scissor)
should do the same with no CPU pass; not tried.

## 1. Build

| | MinGW gcc 16.1 | MSVC 19.4 |
|---|---|---|
| C++ files | `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`, `imgui_demo.cpp`, `cimgui.cpp`, and one file that includes `imgui_impl_sdl3.cpp` and `imgui_impl_opengl3.cpp` with `IMGUI_IMPL_API` = `extern "C"` | same |
| C++ runtime symbols | `__cxa_guard_acquire/release` from function-local statics; none with `-fno-threadsafe-statics` | none outside the CRT |
| Link | `gcc`, not `g++`: SDL3, imm32 | `link`: SDL3, imm32, user32 |
| DLLs the program needs | SDL3, KERNEL32, msvcrt, libwinpthread-1 (the toolchain's), SHELL32, USER32 | SDL3, IMM32, USER32, KERNEL32, SHELL32, VCRUNTIME140, UCRT |
| Library size | 2.26 MB (`libcimgui.a`) | 6.48 MB (`cimgui.lib`) |
| Warnings in the C file | none under `-Wall` | C5287 (mixed enum types) from `cimgui.h` under `/W4` |

The renderer is the stock `imgui_impl_opengl3.cpp` with
`IMGUI_IMPL_OPENGL_ES3` on ANGLE's GL ES 3.0, through a shim
`GLES3/gl3.h` that declares 63 function pointers and loads them with
`psyscr_gl_proc()`. The pointers serve both contexts, because they are
ANGLE's context-dispatching entry points. ES 3.0 has no
`glDrawElementsBaseVertex`, so the backend does not set
`RendererHasVtxOffset`: a single ImGui window must stay under 65,536
vertices with 16-bit indices. The 10,000-point polyline went in a
window of its own for that reason.

cimgui notes for a C player:

- cimgui's constructors heap-allocate (`ImGuiListClipper_ImGuiListClipper()`
  is `IM_NEW`): two clippers a frame were two allocations a frame.
  The C++ constructor is a `memset`, so the probe zeroes a struct on the
  stack.
- Vector arguments are `ImVec2_c` values; C has no default arguments, so
  every call passes every argument.

An ImGui renderer written on `psy_gfx.h` calls was not built: ImGui needs
indexed textured triangles with per-vertex color and a scissor per draw
command, and `psy_gfx.h` draws quads, instances and curve runs. On one
GL context the stock backend works, because the present callback and
`psyscr_gl_epoch()` already isolate a foreign renderer.

## 2. Configurations against the timed window

Windowed, 3 minutes (10,800 measured frames) each. "Long" is the number
of measured flip intervals over 1.5 periods. "Pred p99" is the p99 of
|onset - predicted onset| for on-time frames. CPU and GPU are mean / p99
/ max in microseconds. The timed window's "CPU begin to flip" includes
the overlay's UI build in configuration (b); "flip_at" includes the
present call and, in (b), the overlay render.

| Run | Long | Pred p99 us | Path | Timed CPU begin to flip | Timed CPU in flip_at | Timed GPU |
|---|---|---|---|---|---|---|
| COMPOSITION, none | 0 | 1.85 | independent | 126 / 308 / 1957 | 441 / 788 / 2640 | 409 / 423 / 2338 |
| COMPOSITION, console idle | 0 | 1.84 | independent | 121 / 247 / 581 | 422 / 692 / 2473 | 412 / 431 / 3182 |
| COMPOSITION, console busy | 0 | 1.82 | independent | 176 / 374 / 1199 | 422 / 663 / 1060 | 417 / 436 / 3782 |
| COMPOSITION, overlay idle | 0 | 1.92 | independent | 228 / 429 / 2190 | 506 / 1174 / 2294 | 504 / 522 / 1348 |
| COMPOSITION, overlay busy | 0 | 1.85 | independent | 387 / 735 / 1594 | 715 / 1309 / 2564 | 878 / 927 / 1829 |
| COMPOSITION, overlay busy, again | 0 | 1.84 | independent | 395 / 795 / 2593 | 712 / 1596 / 4296 | 876 / 925 / 2512 |
| COMPOSITION, overlay idle, again | 0 | 1.88 | independent | 270 / 764 / 2309 | 543 / 1238 / 2729 | 509 / 527 / 2549 |
| DXGI_FLIP, none | 0 | 1.92 | overlay 99.4 % | 227 / 562 / 1864 | 415 / 612 / 2624 | 466 / 489 / 2619 |
| DXGI_FLIP, console idle | 0 | 1.92 | overlay 99.4 % | 216 / 421 / 1690 | 401 / 584 / 894 | 466 / 491 / 2357 |
| DXGI_FLIP, console busy | 1 | 1.92 | overlay 98.9 % | 296 / 874 / 2097 | 359 / 559 / 16928 | 474 / 490 / 31922 |
| DXGI_FLIP, overlay idle | 0 | 1.92 | overlay 99.4 % | 360 / 1061 / 2189 | 366 / 720 / 2549 | 570 / 935 / 1945 |
| DXGI_FLIP, overlay busy | 0 | 1.87 | overlay 99.4 % | 538 / 1469 / 2351 | 526 / 1110 / 3865 | 1186 / 2043 / 3642 |

Fullscreen (DXGI_FLIP through AUTO, 1920 x 1200), 2 minutes (7,200
measured frames) each:

| Run | Long | Pred p99 / max us | Path changes | Composed frames | Timed CPU begin to flip | Timed GPU |
|---|---|---|---|---|---|---|
| none | 0 | 1.98 / 4.1 | 1 | 0 | 221 / 580 / 1903 | 1422 / 1460 / 2189 |
| overlay idle | 0 | 1.92 / 5.0 | 2 | 5 | 394 / 888 / 2576 | 1721 / 1993 / 2773 |
| overlay busy | 0 | 3.19 / 43.0 | 1 | 0 | 510 / 1389 / 2432 | 2486 / 3135 / 4557 |
| console busy, covered | 0 | 2.02 / 6.9 | 25 | 107 | 282 / 644 / 2224 | 1444 / 1481 / 4981 |

Over whole runs, warm-up included, the counts of records with a drop
were 1 on every COMPOSITION run (one record of over 700 vblanks, from
the open), 4 to 5 on every
windowed DXGI_FLIP run with or without ImGui, and 0 fullscreen. Early
flips (3 to 6) and estimated records (1 to 2) on windowed DXGI_FLIP were
the same with and without ImGui.

The header's own cost did not change: `psyscr_begin()` minus its wait
was 6.2 to 10.6 us mean and 14.8 to 23.4 us p99 in every run;
`psyscr_flip_at()` minus the present call was 1.9 to 2.2 us mean
without the overlay (with it, the callback's render is in that zone).

### 2.1 What the covered console did to a fullscreen window

With the busy console covered by the fullscreen timed window, the timed
window left the overlay path 25 times in 2 minutes and was composed on
107 frames (tier 2), against 1 change and 0 composed frames without it.
No flip was late. The likely cause is that a covered window that keeps
presenting makes DWM compose now and then; the cause was not traced. On a second display this would not apply in the
same form, but it shows that the untimed window's presents reach the
timed window's path decision. The two-display measurement must count
path changes, not only drops.

### 2.2 The 32 ms stall

In the DXGI_FLIP console-busy run, one frame's GPU time was 31.9 ms on
the timed device and, on the same frame, 31.5 ms on the console's
device; that frame was a vblank late (prediction error 16.7 ms), and
`psyscr_flip_at()` took 16.9 ms once. A stall that both devices see is
GPU-wide: the console's own work was 0.9 ms mean on that run. One event
in 3 minutes does not say whether the console caused it. Section 2.3
has the longer pair.

### 2.3 Eight-minute pair, DXGI_FLIP

The busy console and the control again, 8 minutes (28,800 measured
frames) each, in separate locked sessions about 13 minutes apart:

| Run | Long | Pred p99 / max us | Composed frames | Header begin mean / p99 us | Timed GPU mean / p99 / max us | Console GPU mean / p99 / max us |
|---|---|---|---|---|---|---|
| none | 0 | 1.82 / 8.8 | 64 | 9.2 / 21.0 | 466 / 488 / 2740 | |
| console busy | 0 | 1.83 / 8.6 | 101 | 10.6 / 22.3 | 469 / 491 / 3754 | 889 / 1485 / 3438 |

No stall like that of section 2.2 came back in 8 minutes with the busy
console. The console run had 101 composed frames (tier 2) against 64;
each run had 2 path changes. No measured frame was dropped in either
run; the console run had 1 late target, the control none.

## 3. The cost of ImGui

Configuration (a), on the frame thread after the flip, mean / p99 / max
in microseconds:

| Part | COMPOSITION idle | COMPOSITION busy | DXGI_FLIP idle | DXGI_FLIP busy | fullscreen, covered, busy |
|---|---|---|---|---|---|
| All, after `psyscr_flip_at()` returns | 518 / 1127 / 2560 | 881 / 1735 / 3228 | 516 / 950 / 2671 | 928 / 1754 / 3900 | 872 / 1457 / 2814 |
| UI build (NewFrame to igRender) | 138 / 287 / 662 | 257 / 542 / 2072 | 135 / 263 / 2291 | 278 / 663 / 2379 | 256 / 470 / 978 |
| `Present(1, DO_NOT_WAIT)` | 124 / 288 / 539 | 120 / 280 / 604 | 132 / 270 / 524 | 130 / 322 / 1574 | 135 / 240 / 2036 |
| Console GPU | 415 / 690 / 1851 | 1030 / 1559 / 4261 | 303 / 572 / 1475 | 893 / 1544 / 31502 | 779 / 1155 / 2013 |
| Frames drawn / skipped (no slot) | 10919 / 1 | 10920 / 0 | 10918 / 2 | 10916 / 4 | 7319 / 1 |

`eglMakeCurrent` to the console context: 7 to 11 us mean, at most 113
us. `RenderDrawData` and `glFlush`: 260 us mean idle, 475 to 489 us
busy. `DXGI_ERROR_WAS_STILL_DRAWING`: 0 in every run.

Configuration (b), in the timed window: UI build 112 to 147 us mean idle
and 233 to 252 us busy (p99 at most 0.51 ms); the render in the present
callback 101 to 130 us mean idle and 261 to 286 us busy (p99 at most
0.58 ms). The overlay added 0.1 ms (idle) and 0.4 to 1.1 ms (busy) of GPU
time per frame to the timed window.

### 3.1 Allocations

ImGui's allocator was replaced (`igSetAllocatorFunctions`) by one that
counts calls and live bytes.

| Run | At init and warm-up | In the measured frames | Frames with any | Peak live |
|---|---|---|---|---|
| console idle (3 min) | about 490 | 20 | 13 of 10,800 | 597 KB |
| console busy (3 min) | about 530 | 19 to 21 | 5 to 6 | 1906 KB |
| overlay idle (3 min) | about 490 | 18 | 8 | 489 KB |
| overlay busy (3 min) | about 545 | 5 | 4 | 1827 KB |

ImGui allocates while a window or a buffer grows (new windows, longer
draw lists, the log, glyphs it has not baked yet in the 1.92 dynamic
atlas), then reuses its vectors. In steady state it is a few
allocations a minute. To bound them: route `igSetAllocatorFunctions` to
an arena sized from the warm-up peak (under 2 MB here), turn off
compaction (`io.ConfigMemoryCompactTimer = -1`, default 60 s, frees idle
windows' buffers and so causes reallocations later), and pre-bake the
glyphs the console uses. Not done in the probe.

### 3.2 Memory

Process private bytes in MB. "Warm-up" is after 120 frames.

| Run | Before ImGui | After init | After warm-up |
|---|---|---|---|
| windowed, none | 68.5 | 68.5 | 82.5 |
| windowed, overlay idle / busy | 67.4 to 69.7 | same | 85.2 to 86.1 / 88.0 to 90.0 |
| windowed, console idle / busy | 67.0 to 68.2 | 94.8 to 95.4 | 118.7 to 120.2 / 123.0 to 123.8 |
| fullscreen, none / overlay busy / console busy | 68.4 / 68.1 / 68.6 | 68.4 / 68.1 / 96.0 | 111.3 / 115.1 / 152.2 |

The overlay costs 3 to 8 MB. The console window costs about 27 MB at
open (its D3D11 device and ANGLE display) and 37 to 41 MB after warm-up;
ImGui's own heap is at most 1.9 MB of that.

## 4. Color

ImGui writes 8-bit display values: its style colors and vertex colors
are meant as the sRGB code values the display shows.

- **(a) needs nothing.** The console's swapchain is RGBA8 UNORM, which
  DWM shows as sRGB, as it shows every other window. The calibration of
  the stimulus display does not apply to it.
- **(b) bypasses the calibration.** Drawn in the present callback, ImGui
  writes framebuffer 0 after `psy_gfx.h`'s output stage, so its values
  reach the display as device codes. Measured with `psygfx_read_output()`
  on one frame: a `psy_gfx.h` rect at scene value 0.5 and an ImGui window
  at color 0.5 both gave code 128 with the identity CLUT; with a CLUT of
  v^(1/2.2) the rect gave 186 and the ImGui window still 128.
  This is the correct place for a UI: drawn into the float scene, ImGui's
  sRGB codes would be read as linear light and go through the CLUT, too
  bright. It also means that an overlay frame's pixels under the panels
  are neither the stimulus nor calibrated, and the dither, 10-bit and
  stereo modes do not cover them. The photodiode patch and the codes are
  drawn after the callback, so the overlay cannot cover them.

## 5. Input

SDL keeps one event queue per process and pumps it on the thread that
made the windows. `psyscr_begin()` and `psyscr_poll()` pump it, so the
console's events arrive on the frame thread with the others; the frame
loop passes the console's (by window id) to ImGui. Keys go to the window
with the keyboard focus. SDL 3.4 turns the raw keyboard path off per
window: a key-down comes through the raw path unless the focus window
has text input on (`SDL_windowsevents.c`, `WIN_HandleRawKeyboardInput`).

Scripted test: F24 sent with `SendInput` 20 times per phase, the
windows focused with `SetForegroundWindow`, an ImGui text field made
active with `igSetKeyboardFocusHere`. Keys stamped minus sent, ms:

| Phase | Keys went to | Text input on (timed / console) | Stock IME handler: p50 / max | Routed IME handler: p50 / max |
|---|---|---|---|---|
| timed window focused | timed, 20 of 20 | off / off | 0.22 / 0.41 | 0.23 / 0.40 |
| console focused | console, 20 of 20 | off / off | 0.25 / 0.43 | 0.32 / 0.52 |
| console focused, text field active | console, 20 of 20 | off / on | 11.45 / 16.80 | 11.28 / 16.70 |
| timed focused again, text field still active | timed, 20 of 20 | stock: **on** / off; routed: off / on | **11.42 / 16.85** | 0.33 / 0.59 |

- While the console has the focus, the stimulus window gets no keys at
  all. On a one-keyboard rig the participant's keys go wherever the
  operator last clicked; reaction times belong on a response box
  (`psy_serial.h`) for that reason too.
- The stock backend's `ImGui_ImplSDL3_UpdateIme()` runs in every
  `ImGui_ImplSDL3_NewFrame()` and starts text input on
  `SDL_GetKeyboardFocus()`, whichever window that is. With an active
  ImGui text field, a focus change to the stimulus window turned that
  window's text input on: its keys went through the message path and
  were stamped 11.4 ms after they were sent (p50), not 0.2 ms. No
  `PSYSCR_EV_TEXT_INPUT` record and no `PSYSCR_FLIP_TEXT_INPUT` flag
  showed it.
- The fix is a replacement `Platform_SetImeDataFn`, set after
  `ImGui_ImplSDL3_InitForOther()` and before the first frame (the
  backend keeps the last IME data it saw, so swapping later keeps the
  bug): for the console it starts and stops text input on the console's
  window only; for the overlay it calls `psyscr_text_input()`. With
  the overlay, the stock handler left text input on with 0 ring records
  and 0 flagged flips; the routed one gave 1 `PSYSCR_EV_TEXT_INPUT`
  record and 278 flips flagged `PSYSCR_FLIP_TEXT_INPUT`.
- `psyscr_text_input()` (v0.3.2) acts on a `psy_screen.h` window. The
  console window is not one, so the console handler calls
  `SDL_StartTextInput(console)` itself. An untimed screen kind in
  `psy_screen.h` could give the console the same call and records.
- Focus changes moved the timed window between paths (13 path changes
  in each 10-second input test on COMPOSITION).
- `psy_screen.h` sees Shift+Esc through an SDL event watch, which sees
  the events of every window, so by its manual the abort combination
  also works with the console focused. Not tested.

## Not tested

- Two physical displays: the timed display fullscreen in independent
  flip on one, the console on the other. This is the measurement that
  rig_spec 4.2 asks for, and it is still open.
- A photodiode: every onset is the OS's vblank time.
- The console on a thread of its own, or in a separate process. SDL's
  window calls (the backend's cursor, mouse capture and IME calls)
  belong on the thread that made the window, so a console thread needs
  its own small platform layer; a process needs the runner protocol.
- The console sharing the timed screen's D3D11 device (one device, two
  swapchains), and the composition swapchain for the console.
- Operator input from a real mouse and keyboard during timing runs (the
  busy runs posted mouse messages), drag operations, and docking.
- ImGui's multi-viewport mode, ImPlot, and fonts other than the default.
- Windows 10, other GPUs, Linux X11 (a separate X screen for the
  console), macOS, and the browser.
- Emscripten builds of cimgui.
- Battery power.
