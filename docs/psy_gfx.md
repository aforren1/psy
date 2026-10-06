# psy_gfx.h design notes

This note explains why `psy_gfx.h` has its current form and records what
was measured. The manual in the header tells you how to use it. The plan
that asked for the header is section 4.3 of [rig_spec.md](rig_spec.md).

All numbers come from one machine: a Windows 11 25H2 laptop with an Intel
Iris Xe (driver 32.0.101.7088) that drives a 1920 x 1200 panel at 60 Hz,
and an NVIDIA RTX A500 with no outputs. ANGLE is the copy in Docker
Desktop's Electron folder, `2.1.23876 git hash: fffbc739779a`, unless a
row says otherwise. No photometer, spectroradiometer, photodiode or Bits#
was used, so no number here is a measurement of light.

## v0.2 status

This section is the running record of v0.2. Update it when an item
changes, so that a restart loses nothing.

| Item | State |
|---|---|
| 1. Strokes, MITER, MASK_TEX, `psygfx_sdf_from_mask()` | Done, tested on 4 renderers. Fixed 2026-10-05: ring dots lost their band when the field had no aperture; RECT and CROSS now bevel below a limit of 1.414 (they never did). Stroke GPU time 1.05 to 1.17 times the fill (bar 1.3). |
| 2. Sprites (`src`), nearest and linear | Done, tested. Linear is a hand bilinear from four clamped `texelFetch`, not the sampler (departure, see Sprites and tint). |
| 3. Tint | Done, tested (color, modulation, ADD). |
| 4. Groups | Done, tested. |
| 5. Render targets, `PSYGFX_STIM_ADD`, `PSYGFX_ADD` | Done, tested, with the OVER alpha check and the 16-pass limit. ADD kept: 0.43 ms against 9.1 to 9.2 ms. Composite 0.43 ms GPU (bar 1 ms); pass switch 4.7 us CPU (bar 50 us). |
| 6. GL epoch | Done, tested (`psygfx__sync()`). |
| 7. Contract | Done: `psy_look.w` is the band's center offset, parameters 61 to 70 appended, 336-byte stimulus on 64-bit targets, 8 built-in programs plus the output stage. Open time 1.3 to 1.4 s after the SDF got one call site (3.2 to 5.0 s before). |
| Mutations | 18 of 18 caught (after one test addition). |
| Build matrix | MSVC 19.44 (CMake) and 19.51 C11 and C++17; MinGW gcc 16.1 C99, C11, C++17; WSL gcc 11.4 C99, C11, C++17, ASan and UBSan, llvmpipe pixel test; emcc 6.0.10 C11 run in node, C++17 compiled. All pass. |
| Left | v0.2 frame-loop runs in a window and an allocation count with target passes; timing on renderers other than the Iris Xe. |

## v0.3 status

The running record of v0.3 (SDF shapes, compounds, effects, dashes,
gradients, MSDF). The design is in the session notes of 2026-10-05;
decisions taken from it: the vector program compiles at every open (no
flag); smooth operations are gradient-normalized only; no CSG stack; the
ellipse's iteration count follows its aspect ratio; one shadow offset; no
batching of stimuli with extension blocks; the Gaussian reach is 5 SD; ADD's
gamut check is v0.2's.

| Item | State |
|---|---|
| 0. v0.2 edge model against truth (manual EDGES, this file, test `gl_edge_truth`) | Done on 4 renderers: vertex 0.2500, disc 0.0406 / 0.0100, MITER 0.0800 on each. |
| 1. Types, API, parameter tables, `psygfx_bind.field` | Done. `psygfx_prim_params()` 11 rows, `psygfx_fx_params()` 32, `psygfx_paint_params()` 28; P_OFFSET to P_TRIM_END appended to the stimulus table. |
| 2. Vector program: kinds, compounds, effects, dashes, trim, paint | Done, tested on 4 renderers. Departures: QBEZIER is chords within 0.005 px, not exact to 4e-5 px; dashes on CROSS refused; MITER and BEVEL in the vector program on POLYLINE only. |
| 3. Blend modes (MULTIPLY; ADD for shapes), backend version 2 | Done, tested (MULTIPLY within 4.8e-8 of the product). Backend v2 adds deferred pipelines and `pipeline_finish`. |
| 4. MSDF and MTSDF, mask source rectangles, range rule, glyph runs | Done, tested (MSDF 3.5e-6; glyph runs equal single draws bit for bit). |
| 5. Tests T1 to T12, mutations | Done. 26 mutations: 24 caught on llvmpipe, 1 caught only on D3D11 (the star's reflection guard), 1 equivalent (normalizing a non-smooth fold). Two test additions were needed (an Oklab white check; the MSDF and glyph checks). |
| 6. Bench (under the lock), open time | Done: open 1.96 to 2.14 s (bar 2.0 s, met on 1 of 3 runs). Four bars missed: see "v0.3 cost". |
| 7. Manual, README, rig_spec 4.3, license block | Done. The year of each Inigo Quilez notice was copied from the Shadertoy pages by hand (2015, 2018, 2019, 2019, 2019), because the pages refused automated reads. |
| 8. Build matrix | See "v0.3 builds". |

## v0.4 status

The running record of v0.4 (program cache, video, instances, the
gallery's fixes). The design is in the session notes of 2026-10-06; the
coordinator's conditions are listed with the item each belongs to.

| Item | State |
|---|---|
| 1. Program cache | Done, tested (11 GL cases on 3 renderers, a CPU half with a fake backend). Warm open 11.4 to 35.0 ms on the Iris Xe against 2.3 to 5.3 s without a cache (bar 100 ms: met); a cold open adds 32 to 120 ms of stores (bar 5 %: met). |
| 1b. Kind-specialized vector programs | Not built: the coordinator asked for the harness numbers first; see "Next". |
| 2. Gallery fixes (calibration CRC, straight-alpha sprites, DIST range rule, first, shape_p); the dots' zero-default bug and the no-pixel audit | Done, tested. |
| 3. Planar YUV, encodings, import, rebind | Done, tested on 3 renderers; D3D11 NV12 import (array slice, shared handles) on the Iris Xe and WARP. Bars: update CPU met (30 to 38 % of RGBA8); GPU 1.0 ms met except DEVICE (1.12 ms); GPU 1.5x RGBA8 missed (1.63 to 1.91). |
| 4. Instances | Done, tested on 3 renderers. Every bar met: 10000 gabors 177 to 191 us of CPU, 1.26 to 1.33 ms of GPU (5.5 ms one stimulus each). |
| 5. Overlap-aware reordering in end() | Done, tested bit for bit against call order on 3 renderers. 1000 interleaved stimuli of 4 kinds: 7.2 to 1.1 ms of GPU, 3.3 to 0.47 ms of CPU. |
| 6. Bench on AC | Done ("v0.4 cost"); a build ran during parts of it, named there. |
| Mutations | 35 of 35 caught ("v0.4 mutations"). |

## What the GPU and ANGLE report

Queries made before the design, offscreen:

| Question | Answer |
|---|---|
| The panel's link | `IDXGIOutput6::GetDesc1`: 8 bits per color, SDR (G22, P709), 270 cd/m2. EDID primaries R (0.600, 0.370), G (0.356, 0.550), B (0.155, 0.110), white (0.3135, 0.3291): a narrow gamut. |
| 10-bit swapchain | `R10G10B10A2_UNORM`: a flip-model swapchain is made; the sRGB color space can present and use an overlay. |
| ANGLE's 10-bit and float surfaces (spec 13) | On the D3D11 device: config 101 is RGBA 10/10/10/2, config 76 is RGBA16F. A client-buffer pbuffer on an `R10G10B10A2` texture works: a clear of 513/1023 reads back as 513 through D3D. |
| So, 10-bit output here | The GPU side works. The link is 8 bits per color, so the driver truncates or dithers. Which one cannot be found without a photometer. v0.1 refuses `PSYGFX_OUT_10`. |
| Float targets | `EXT_color_buffer_float`, `EXT_float_blend`, `OES_texture_float_linear` on Iris Xe (D3D11), WARP, SwiftShader, NVIDIA (Vulkan) and Mesa 23.2 llvmpipe. A float32 clear of 0.123456789 reads back as 0.123456791. |
| Float to 8-bit and 10-bit codes | Every code k written as k/255 or k/1023 reads back as k, on all four ANGLE renderers. |
| Program binaries | One format, ANGLE's own (0x93A6), valid for one ANGLE build on one adapter and driver. There is no portable binary for GL ES 3.0. |
| Software GL for CI | ANGLE on D3D11 WARP and on Vulkan SwiftShader; Mesa llvmpipe through EGL's surfaceless platform. No window and no Khronos header. |

## The row order of the back buffer

ANGLE's pbuffer on a D3D11 texture (`EGL_ANGLE_d3d_texture_client_buffer`)
puts GL's row 0 at D3D's row 0, which is the top of the screen. A clear of
GL's upper half read back through D3D as the bottom rows. So on DXGI_FLIP
and COMPOSITION the output stage writes the rows the other way, and a
headless context keeps GL's own order. A visible check in an 800 x 600
window confirmed it: a rect placed at the top-left showed at the window's
top-left through GDI, with the same codes as `psygfx_read_output()`
(140, 115, 102 for 0.55, 0.45, 0.40).

## Coordinates

The convention lives in one function, `psygfx__place()`, and its helpers.
Screen pixels have the origin at the top-left corner, x right, y down;
positive angles turn +x toward +y, which is clockwise on the screen. A
stimulus has a `place`, the screen point x, y are measured from (nine
choices, the center by default), and an anchor, the point of its own box
that sits at x, y and that it turns about (`ax`, `ay`, the center by
default, as in Phaser). The pattern is defined about the box's own center,
so the anchor moves a pattern without changing it. The test checks that a
gabor placed by its center and by its top-left corner gives the same bits.

SDL3 reported a pixel density of 1.000 and a display content scale of
1.000 for a window on this panel, so mouse coordinates are pixels here.

## Local coordinates come from gl_FragCoord

The first version interpolated each fragment's point in the stimulus frame
from the quad's vertices. Against a CPU reference in double, the errors
were large on the software renderers and grew with spatial frequency:

| Renderer | Grating max error, interpolated | Grating max error, from gl_FragCoord |
|---|---|---|
| Iris Xe (D3D11) | 1.7e-5 (gabor 1.6e-4) | 1.7e-5 (gabor 1.2e-6) |
| WARP | 1.1e-3 | 1.2e-5 |
| SwiftShader | 2.1e-2 | 1.4e-4 |
| llvmpipe | 3.2e-5 | 1.2e-5 |

A rasterizer snaps vertex positions to its subpixel grid, and the
interpolated coordinates move with the snapped vertices. The vertex shader
now hands the fragment shader the stimulus's exact center (flat), and the
fragment shader computes its point from `gl_FragCoord`, which is the exact
pixel center. The quad only has to cover the stimulus. Shapes with no
aperture are clipped to the box in the shader, so snapping cannot move an
edge either.

## Compile time and the batch length

`psygfx_open()` first took 7.4 to 8.7 s through ANGLE on D3D11 (12 to 20 s
on WARP). Per program, measured:

| Stim block in the shader | Compile and link per program |
|---|---|
| `vec4[16]` (one stimulus) | 27 to 48 ms |
| `vec4[256]` (16 stimuli) | 69 to 88 ms |
| `vec4[1024]` (64 stimuli) | 226 to 265 ms |
| `vec4[4096]` (256 stimuli) | 940 to 1280 ms |
| an array of 256 structs | not timed per program; the open of eight took 7.4 to 8.1 s, as with `vec4[4096]` (7.6 to 8.7 s) |

The polygon loop made no difference (835 to 1126 ms without it). The time
grows with the length of the dynamically indexed uniform array. Batch
length against open time and CPU time per frame (bench, 1000 gabors,
CPU in `begin()` to `end()`):

| Batch | `psygfx_open()` | 1000 gabors, CPU mean |
|---|---|---|
| 16 | 0.8 s | 387 us |
| 64 | 2.2 s | 508 us |
| 256 | 18.8 s | 132 us |

16 is kept: a whole frame of 1000 stimuli stays under 0.5 ms of CPU, and
the open stays under 1.5 s.

## Measured, then deleted

| Choice | Variants | Measured | Kept |
|---|---|---|---|
| Draw calls | batched (one instanced draw per run of 16 like stimuli) against one draw call each | 1000 gabors: 0.20 to 0.50 ms against 2.1 to 5.0 ms of CPU; GPU the same | batched. One draw each remains only as a test seam (`max_batch`), which checks that both give the same bits |
| Uniforms per stimulus | `glUniform4fv` per draw against one buffer upload per frame and a bound range per draw (raw GL, 16 vec4 each) | 1000 draws: 1.36 to 1.74 ms against 0.82 to 2.15 ms of CPU, no winner; batched ranges beat both | one upload per frame and batched ranges; per-draw uniforms were never put in the header |
| Scene format | RGBA16F against RGBA32F | empty frames: no difference (0.46 to 1.26 ms both); 1000 gabors 9.9 to 11.2 ms against 16.1 to 17.4 ms of GPU; full-screen noise 0.9 to 1.0 against 1.7 to 2.3 ms | RGBA16F. RGBA32F remains only as a test seam, because the pixel test needs float32 to measure shader error below half precision |
| Full-frame texture upload | `glTexSubImage2D` from memory against a pixel unpack buffer | 1.1 to 2.6 ms against 9.4 to 19.2 ms of CPU | `glTexSubImage2D`; no PBO path was added |
| A direct path into the back buffer, without the scene | an empty frame against a bare clear of framebuffer 0 | 0.42 to 1.2 ms against 0.12 to 0.38 ms of GPU | the scene path only: its cost is under the 1.5 ms planned, so a second path is not worth keeping |

## Frame cost

`examples/gfx_bench.c` on the Iris Xe at 1920 x 1200, RGBA16F scene,
batch 16. "GPU" is the time per frame of 120 to 300 frames run back to back
between two `glFinish()` calls; it includes the empty frame's cost. Five
runs, Balanced plan: the batch study's (power state not recorded), the two
scene-format runs (the empty frames of that session on AC), and two final
runs on battery; some rows ran in fewer of them. The bars are the ones the
design proposed against the 16.7 ms frame.

| Workload | CPU mean, us | GPU, ms | Bar | Met |
|---|---|---|---|---|
| empty frame: scene clear + output stage, identity CLUT | 113 to 259 | 0.46 to 1.15 | GPU 1.0 ms, CPU 10 us | GPU in 5 of 7 runs (the two on battery missed: 0.89 and 1.15); CPU no |
| empty frame, CLUT 4096 + ordered dither | 56 to 171 | 0.42 to 1.34 | GPU 1.0 ms | in 4 of 7 runs |
| gabors 256 x 256, x 1 | 182 to 248 | 0.49 to 1.13 | | |
| gabors x 10 | 78 to 189 | 0.57 to 0.62 | | |
| gabors x 100 | 75 to 213 | 1.33 to 1.72 | CPU 100 us | in 1 of 5 runs |
| gabors x 1000 | 196 to 502 | 9.3 to 11.2 | CPU 1 ms, GPU 10 ms | CPU yes; GPU in 4 of 5 runs |
| dots x 1000, 4 px, upload each frame | 66 to 288 | 0.37 to 0.61 | CPU 0.1 ms | void: drew nothing (v0.4, "the gallery's findings") |
| dots x 10000 | 87 to 243 | 0.45 to 0.59 | CPU 0.3 ms, GPU 0.5 ms over the empty frame | void: drew nothing |
| dots x 100000 | 654 to 1271 | 1.45 to 1.99 | CPU 2 ms, GPU 3 ms | void: drew nothing; re-measured in "v0.4 cost" |
| noise, full screen, GPU hash | 63 to 285 | 0.83 to 1.01 | GPU 1 ms over the empty frame | yes |
| `psygfx_noise_fill()` 1920 x 1200 on the CPU | 8 500 to 26 600, once | | off the frame thread | on the pump |
| noise, CPU R32F upload + draw | 1 708 to 3 259 | 2.99 to 4.79 | CPU 3 ms | in 2 of 3 runs |
| image RGBA8 full frame, upload + draw | 1 450 to 2 782 | 3.49 to 4.21 | CPU 3 ms | mean yes, p99 (4.4 to 5.8 ms) no |

The one bar missed in every run is the CPU time of `end()` for an empty
frame: about 15 GL calls take 56 to 260 us through ANGLE. It was not
broken down further. psy_screen.h's present call through ANGLE costs 380 to
1230 us on the same machine, so this is the same order. A frame with up to
1000 stimuli still stays under 0.5 ms of CPU for `begin()` to `end()`.

The variation between runs is the machine's: the same binary moved up to
2x between runs, with the power state and another agent's work on the
machine as the likely causes. The runs were not repeated enough to
separate them. In the runs at batch 64 and 256 (not kept) the GPU rows
varied as much.

### In a real frame loop

`examples/gfx_load.c`, an 800 x 600 window on the panel, the composition
swapchain, the window kept on top, 1 minute each, on battery. Phases from
psy_screen.h's flip records.

| Load | Flips | Dropped vblanks | Late targets | DRAW p50 / p99, us | UPLOAD p99, us |
|---|---|---|---|---|---|
| nothing (empty frames) | 3554 | 43 | 0 | 188 / 476 | 0.7 |
| 1 gabor | 2565 (1) | 21 | 2 | 286 / 654 | 0.7 |
| 1 gabor, again | 3600 | 1 | 0 | 255 / 590 | 0.6 |
| 100 gabors | 3595 | 5 | 1 | 332 / 788 | 0.8 |
| 10000 dots, uploaded each frame (void: they drew nothing; not re-measured) | 3567 | 3 | 1 | 268 / 643 | 215 |

(1) The run ended before its minute; the window was most likely covered or
closed. The rerun below it is complete.

The drops were the system's: no late target came with a DRAW or UPLOAD
phase near the 16.7 ms budget (DRAW p99 under 0.8 ms in every run), and the
empty-frame run had the most drops. psy_screen.md describes the drops of
windowed runs.

### Allocations

MSVC debug build, `gfx_load --allocs` with 100 gabors and 10000 dots,
10 s: 0 C runtime heap calls in the 566 frames after frame 30. The control
(`--allocs-control`, one `malloc` per frame) counted 569. Allocations
inside ANGLE's DLLs are not seen by this hook.

## Pixel tests: measured error and tolerance

`tests/adapt/psy_gfx_test.c` renders each stimulus into an RGBA32F scene
(the test seam), reads it back, and compares it with a CPU reference in
double at each pixel center, found through `psygfx_local()`, the inverse
of the coordinate transform. Errors are in scene units (full scale 1).

| Check | Iris Xe (D3D11) | WARP | SwiftShader | llvmpipe | Tolerance |
|---|---|---|---|---|---|
| soft edges (cosine, Gaussian) | 1.69e-5 | 2.93e-6 | 9.49e-5 | 2.40e-6 | 5e-5; SwiftShader 3e-4 |
| hard edges, wrong pixels | 0 | 0 | 0 | 0 | 0 (pixels within 1e-3 px of an edge skipped) |
| `psygfx_hit()` against GPU coverage >= 0.5 | 0 | 0 | 0 | 0 | 0 (same skip) |
| grating, sine | 1.72e-5 | 1.20e-5 | 1.41e-4 | 1.20e-5 | 5e-5; SwiftShader 4e-4 |
| grating, square | 7.15e-6 | 1.22e-6 | 4.73e-5 | 1.16e-6 | 2e-5; SwiftShader 1.5e-4 |
| gabor | 1.23e-6 | 1.11e-6 | 8.77e-5 | 4.85e-7 | 4e-6; SwiftShader 3e-4 |
| dots | 1.47e-5 | 7.70e-6 | 9.49e-5 | 7.70e-6 | 5e-5; SwiftShader 3e-4 |
| images, all eight formats, 1:1 | 2.97e-8 | 2.97e-8 | 7.43e-8 | 7.43e-8 | 2.2e-7 |
| noise against `psygfx_noise_value()` | 0 | 0 | 0 | 0 | 0 (bit for bit) |
| output codes: identity, rounding, CLUT | 0 | 0 | 0 | 0 | 0 |
| ordered dither against the 8 x 8 pattern | 0 | 0 | 0 | 0 | 0 |
| noise dither, mean minus target, in codes | +0.0008 | +0.0008 | +0.0008 | +0.0008 | 0.005 (v0.1: -0.0015; the frame index, and so the pattern, moved when v0.2 added frames before this check) |
| two gabors against the sum of each alone | 1.19e-7 | 1.19e-7 | 1.19e-7 | 1.19e-7 | 4e-7 |
| user shader against the built-in grating | 0 | 0 | 0 | 0 | 1e-6 |
| anchor moved with the position | 0 | 0 | 0 | 0 | 1e-6 |
| batched against one draw each | 0 | 0 | 0 | 0 | 0 |

Each tolerance is about 3x the largest error measured on a renderer of
its kind. SwiftShader's transcendentals are about ten times coarser than
the others' and get their own column. The design's probe had put SwiftShader's
`cos` error at 1.9e-4; with the point from `gl_FragCoord`, the largest
grating error is 1.4e-4.

Calibration checks, against published values:

| Check | Reference | Measured | Tolerance |
|---|---|---|---|
| sRGB primaries and D65 to XYZ | IEC 61966-2-1:1999, the printed matrix | max 3.9e-5 | 1.2e-4 |
| CLUT of a display with sRGB's transfer function | sRGB's encoding | 1.3e-3 from 17 levels, 3.6e-5 from 64 | 4e-3, 1.1e-4 |
| cone fundamentals, embedded | CVRL `linss2_10e_1.csv`: all 441 rows compared once; spot rows in the test | equal to float rounding | 1e-7 relative |
| RGB to LMS of Psychtoolbox's `B_monitor` | `T_cones_ss2 * B` in MATLAB R2023a | 1.3e-8 relative | 1e-6 |
| DKL matrix | Psychtoolbox's `ComputeDKL_M.m`, background RGB (0.4, 0.5, 0.3) | 3.1e-8 relative | 1e-6 |
| DKL unit axes as RGB increments | the same | 6.8e-8 relative | 1e-5 |
| axis isolation | algebra: L-M keeps S and luminance, S keeps L and M | under 1e-6 relative | 1e-5 |

Psychtoolbox's `T_cones_ss2.mat` equals CVRL's table to 1e-13. The MATLAB
script resampled `B_monitor` (380 to 780 nm at 5 nm) linearly onto 1 nm,
as the header does. Its numbers are constants in the test.

Mutations: 14 deliberate faults in a copy of the header, each built and run
against the test on llvmpipe. All 14 failed the test: the local y axis
sign, the rotation direction, an off-center cosine edge, the erfc constant,
output rounding at 0.49, the Bayer bit order, the GPU hash constant, an
uninterpolated CLUT, batches that ignore textures, image rows upside down,
the anchor's sign, cone integration one nm off, the DKL luminance weights
swapped, and a hit test that uses the margin. The first run let three
through (rounding, the Bayer bit, textures in batches), and the test grew
a check for each.

v0.2 mutations: 18 more, by a script that applies each fault to a copy of
the header and runs the test on llvmpipe. All 18 failed the test: a stroke
as F(|d - c| - s/2), INSIDE and OUTSIDE swapped, no bevel past a polygon's
limit, no bevel on a right angle below 1.414, bilinear clamped to the
texture instead of the source rectangle, tint alpha not on coverage, ADD
without tint alpha, a group's scale on the edge width, group opacity
ignored, group ori not added, target rows bottom first, no feedback
refusal, the cache kept over an epoch change, the distance transform
without its half texel, OVER keeping alpha (v0.1's blend), ring dots
without their band, a non-uniform mask fit drawn, and a target's clear
ignored. The first run let one through (ADD without tint alpha); the tint
check grew an ADD image.

## Shaders on GL ES 3.0

GL ES 3.0 has no portable binary, so the runtime takes text: the user's
body wrapped by `psygfx_shader_wrap()`, which the pack tool calls too, so
the two cannot disagree. ANGLE compiles it when the pack loads. The
compiler's log names the body's own lines: the wrapper puts `#line 1`
before the body, and the test checks that an error on the body's line 3
is reported as `0:3` (ANGLE `0:3:`, Mesa `0:3(`). A program binary cache
would cut the open from 0.8 to 1.4 s to a few ms; v0.4 has one ("v0.4: the
program cache").

## v0.2: what was added and why

### One uniform block, four free floats

The per-stimulus uniform block stays at 256 bytes (16 vec4). ANGLE's
compile time on D3D11 grows with the length of the dynamically indexed
array (the batch table above), so a longer block would make
`psygfx_open()` slower for every user. v0.2 puts its new data in the four
floats that v0.1 did not use, or derives it in the shader, or keeps it on
the CPU:

| Float | v0.2 use |
|---|---|
| `look.w` | the stroke band's center offset, px (was `visible`, which the CPU already tests) |
| `shape.w` | the band's half width, px; 0 is a fill |
| `dir.a` | the miter limit; its sign is the polygon's orientation; 0 is ROUND |
| `color.a` | IMAGE: the tint's alpha |

The tint's rgb goes into `color.rgb` (color and ADD images) or into `dir`
on the CPU (modulation images). The source rectangle goes into `p[0..3]`,
which an IMAGE did not use. A group never reaches the shader:
`psygfx__place()` composes it into the stimulus's center, axes and pixels
per unit.

### Strokes

A stroke is the band between the offsets a1 < a2 of the boundary. Its
coverage is F(d - a2) - F(d - a1), with F the fill coverage under the
stimulus's edge profile. This is the hard band convolved with the
profile's kernel. The shortcut F(|d - c| - s/2) gives a band that reaches
1 at every width; the true band does not when s is narrower than the edge.
The test checks this: a 1 px band under a Gaussian edge of SD 2 px peaks at
0.197 (2 erf(0.25 / sqrt 2)), not 1.

A round join is the SDF's own offset, so one SDF value serves both
offsets. A mitered RECT or CROSS uses the box's Chebyshev distance, whose
offsets keep sharp corners. A mitered convex POLYGON uses the max of its
edges' line distances. Past the miter limit, a bevel plane per corner cuts
the corner; the bevel depends on the offset level, so a band needs the SDF
at both levels. For a polygon, one loop gives both levels, because only the
bevel terms depend on the level. A right angle's miter is sqrt 2 times the
width, so RECT and CROSS bevel only below a limit of 1.414. MITER on a
curve, a rounded rect or a concave polygon is refused
(`PSYGFX_ERR_ARG`): a concave corner's miter is not the max of line
distances.

### MASK_TEX

`psygfx_sdf_from_mask()` runs Felzenszwalb and Huttenlocher's exact
squared Euclidean distance transform twice (to the nearest inside texel,
to the nearest outside texel) and puts the boundary halfway between the
two texel centers. The test compares it with a brute-force search on 20
random masks: equal in every texel. Against an analytic disc of radius 24
texels, the largest difference is 0.49 texel; a binary mask places its
boundary only to within half a texel's diagonal (0.71). The shader samples
the texture by the same four-texelFetch bilinear as images, so an R32F
mask needs no `OES_texture_float_linear`. A box of another aspect ratio
than the texture's is refused: a non-uniform fit would scale the distance
differently along each axis. `psygfx_hit()` returns false for a mask,
because the distances are on the GPU only.

### Sprites and tint

Nearest sampling reads one texel by `texelFetch` at an integer address
clamped to the source rectangle, so no neighbor can contribute. Linear
sampling interpolates by hand from four `texelFetch` whose addresses are
clamped to the rectangle. The design had the sampler's own linear filter
with the sample point clamped half a texel inside. The hand form was kept
because it is exact for any rectangle, it needs no padding, and its
weights are float on every renderer: the test measured the hand form
within 8.4e-7 of a double reference on all four renderers. There are no
mipmaps: draw sprites at 1:1 or larger.

The tint multiplies linear light. For a modulation image it multiplies
`dir` and the amplitude, which `dir` and `contrast` already do; the manual
says so.

### Groups

A group is one level of transform, composed on the CPU in
`psygfx__frame_px()`: S = group place point + (x, y) + R(group ori) *
scale * (P - group anchor point), with P the member's anchor in the group's
box. Scale multiplies what is in units (positions, sizes, sigma, shape
parameters, dot positions; sf is divided by it). It does not multiply what
is in px: `edge_width`, `stroke`, `dot_size` and `check`. A px value sets
the stimulus's spatial-frequency content at the panel, so a zoom must not
change it. Pixi's `container.scale` zooms everything. The test draws each
member in a group and the same stimulus placed alone from the composed
values: the largest difference is 6.0e-8 of full scale on every renderer.

### Render targets

A target pass splits the scene pass: the frame's commands are recorded in
segments, played in call order, each with its own frame block (17 fixed
slots: the scene and 16 passes). Nothing is allocated per pass. A target
holds scene-space linear values; the CLUT and dither run once, in the
output stage, which the test checks code for code against direct drawing.
A target is drawn with its rows top first, so it reads back and samples
with no flip; its local y axis keeps its sign in GL's axes.

v0.1's OVER blend kept the destination's alpha. A target drawn as a
premultiplied image needs the coverage it accumulated, so v0.2's OVER
accumulates alpha: (ONE, ONE_MINUS_SRC_ALPHA) for color and for alpha. The
scene's alpha is never read. The test draws the same frame through the
built-in backend and through a copy that puts v0.1's blend back after
each apply: the scene's rgb and the output codes are bit-identical. The
control shows that the copy took effect: in a target cleared to alpha 0,
the two blends give a different alpha at every texel drawn.

### The GL epoch

`psygfx__sync()` runs on entry to every function that issues GL, when
psy_screen.h defines `PSYSCR_HAS_GL_EPOCH`. When `psyscr_gl_epoch()` has
moved (a present callback ran), it drops the GL state cache. When
`psyscr_gl_generation()` has moved, the context is new: the call returns
`PSYGFX_ERR_LOST` and the handle closes without deleting anything,
because the old names could name the new context's objects. The test
installs a present callback that unbinds the program, the vertex array,
the uniform ranges and the textures and disables blending: the next frame
is the reference, code for code. The control makes the same changes with
no epoch: 901 output values differ.

### v0.2 pixel tests: measured error and tolerance

The same method as v0.1's table above: an RGBA32F scene read back against
a CPU reference in double at each pixel center. ANGLE rows: the Docker
copy and Electron v38.8.6's copy gave the same numbers. llvmpipe: Mesa
23.2.1 in WSL2.

| Check | Iris Xe (D3D11) | WARP | SwiftShader | llvmpipe | Tolerance |
|---|---|---|---|---|---|
| strokes, soft edges: 6 shapes x 3 alignments x 2 edges x 2 angles x both joins, ring dots, a 1 px band | 1.62e-5 | 9.12e-6 | 9.53e-5 | 8.59e-6 | 5e-5; SwiftShader 3e-4 |
| strokes, hard edges, wrong pixels | 0 | 0 | 0 | 0 | 0 (pixels within 1e-3 px of a band edge skipped) |
| `psygfx_hit()` on strokes against GPU coverage >= 0.5 | 0 | 0 | 0 | 0 | 0 (same skip) |
| alignments by hand, MITER corner pixels, bevels (polygon past its limit, RECT and CROSS below 1.414) against the CPU form | 0 | 0 | 0 | 0 | 0 |
| MASK_TEX (fill and stroke, scale 1 and 2, turned) against the CPU's bilinear of the same texture | 1.42e-5 | 1.16e-5 | 9.53e-5 | 1.10e-5 | 5e-5; SwiftShader 3e-4 |
| sprites, nearest: wrong texels; bleed from a neighbor (nearest and linear) | 0; 0 | 0; 0 | 0; 0 | 0; 0 | 0 |
| sprites, linear, against the CPU's clamped bilinear | 7.77e-7 | 7.77e-7 | 8.36e-7 | 8.36e-7 | 2.5e-6 |
| tint: color, modulation and ADD images against exact products | 4.18e-8 | 4.18e-8 | 6.26e-8 | 6.26e-8 | 2e-7 |
| group members against the same stimuli placed alone | 5.96e-8 | 5.96e-8 | 5.96e-8 | 5.96e-8 | 2e-7 |
| `psygfx_hit()` through a group | 0 | 0 | 0 | 0 | 0 |
| shapes through a target against direct drawing | 0 | 0 | 0 | 0 | 0 (bit for bit) |
| target row order, clear, persistence, RGBA8 codes | 0 | 0 | 0 | 0 | 0 |
| 30 gabors through an RGBA16F target and ADD against direct drawing | 2.78e-4 | 2.78e-4 | 1.46e-4 | 2.78e-4 | 8e-4 (half precision of the increments) |
| output codes through a target with a CLUT against direct drawing | 0 | 0 | 0 | 0 | 0 |
| scene rgb and codes, OVER with alpha against v0.1's OVER | 0 | 0 | 0 | 0 | 0 (bit for bit) |
| output codes after a present callback that changes GL state | 0 (control: 901) | 0 (901) | 0 (901) | 0 (901) | 0 |
| refusals (MITER on a curve and a concave polygon, a non-uniform mask fit, a target sampled in its own pass), the pass limit, a new context | all | all | all | all | all |

CPU only: `psygfx_sdf_from_mask()` equals brute force at every texel of
20 random masks (exact: both are square roots of the same integers), and
is within 0.49 texel of an analytic disc (bound 0.75, from the mask's half
diagonal of 0.71).

### The edge model against truth

The checks above compare the GPU with the CPU form of the same function.
They cannot see where that function is not the Euclidean distance, or where
a profile of the distance is not a blur of the shape. Both happen, by
design, and v0.2's manual did not say so. Coverage is F(d): the profile of
the signed distance to the nearest boundary point.

- **F(d) is a blur only on a straight edge.** At a vertex of interior angle
  a, the Gaussian blur of the hard shape is a / 360 of the step and F(0) is
  0.5: 0.25 off at a right angle, 0.333 at 60 degrees, 0.083 at 150
  degrees. Above 1/255 of the step within 3.75 SD of a right-angled vertex.
  On a disc of radius R, F(d) is about 0.2 SD / R above the blur at the
  boundary: 0.448 at R = SD, 0.115 (2), 0.041 (5), 0.020 (10), 0.010 (20),
  0.004 (50).
- **MITER's field is a bound outside corners.** It is the largest distance
  to the edges' lines: Euclidean inside a convex shape, below the Euclidean
  distance outside a corner. A soft outer edge there is up to 0.083 of the
  step too high for a right angle (at 1.17 SD along the bisector), more at
  sharper corners. This does not depend on the edge width: it scales with
  it.

The analytic numbers came from double-precision integrals (the disc's
exact blur is the noncentral chi-square CDF with 2 degrees of freedom).
The test `gl_edge_truth` measures them on the GPU against references that
share none of the header's arithmetic: the exact blur of a rect (a product
of erf), the blur of a disc by a 2000-panel Simpson integral, and the
Euclidean distance by brute force over a rect's four edge segments. Each
measured deviation must be at most the analytic value plus the renderer's
tolerance and at least 0.9 (vertex, disc) or 0.5 (MITER corner, sampled
off its worst point by the pixel grid) of it.

| Check (GAUSSIAN, SD 2 px) | Analytic | Iris Xe | WARP | SwiftShader | llvmpipe |
|---|---|---|---|---|---|
| 90 deg vertex on a pixel center, F(d) minus the blur | 0.2500 | 0.2500 | 0.2500 | 0.2500 | 0.2500 |
| disc, R = 5 SD, F(d) minus the blur | 0.0406 | 0.0406 | 0.0406 | 0.0406 | 0.0406 |
| disc, R = 20 SD | 0.0100 | 0.0100 | 0.0100 | 0.0100 | 0.0100 |
| MITER rect fill, F(field) minus F(Euclidean d) | 0.0830 | 0.0800 | 0.0800 | 0.0800 | 0.0800 |

A user who needs a soft edge that is exactly a blur uses a straight edge,
or for a sharp rect the exact separable blur (v0.3, section v0.3 below).

### v0.2 cost

`examples/gfx_bench.c --only v0.2`, Iris Xe at 1920 x 1200, RGBA16F
scene, on AC (Balanced plan), measurement lock held. The rows of one table
take turns in blocks of 12 frames, 25 rounds; the GPU time is per frame
between two `glFinish()` calls, as the median over the rounds; "over
empty" is the median of the per-round difference from an empty frame. Two
runs of the final shaders. Three earlier runs, before the SDF got one call
site (below), gave stroke ratios of 0.95 to 1.17 and the same target and
sprite rows within 10 %.

| Workload | GPU over empty, ms | Ratio to the fill | Bar | Met |
|---|---|---|---|---|
| circle, 1160 px, cosine edge: fill | 0.227 to 0.242 | | | |
| the same circle, 8 px stroke | 0.262 to 0.282 | 1.15 to 1.17 | 1.3 | yes |
| pentagon, 1114 px: fill | 0.693 to 0.738 | | | |
| the same, 8 px stroke, MITER | 0.738 to 0.773 | 1.05 to 1.06 | 1.3 | yes |
| the same, 8 px stroke, ROUND | 0.739 to 0.775 | 1.05 to 1.07 | 1.3 | yes |
| rect 1880 x 1160: fill | 0.401 to 0.402 | | | |
| the same, 8 px stroke, MITER, limit 1.2 (a bevel) | 0.431 to 0.436 | 1.07 to 1.08 | 1.3 | yes |
| MASK_TEX circle, 288 x 288 R16F texels fitted to 1160 px | 0.307 to 0.320 | 1.32 to 1.35 of the analytic circle | | |

A stroke's quad covers the shape's box and the band, not a ring, so the
shader runs on about the same pixels as the fill's.

| Workload | GPU over empty, ms | CPU over empty, us | Bar | Met |
|---|---|---|---|---|
| 1000 sprites of 32 x 32 from one atlas | 0.490 to 0.493 | 153 to 155 | | |
| 100 gabors of 256 x 256 | 0.959 to 0.979 | 24 to 25 | | |
| the same 100 gabors in a group, its x bound each frame | 0.955 to 0.970 | 26 to 29 | | |
| an empty target pass, cleared (1920 x 1200 RGBA16F) | 0.000 to 0.001 | -6 (noise) | | |
| full-screen target composite, COLOR | 0.430 | 9 | 1 ms GPU | yes |
| full-screen target composite, ADD | 0.427 to 0.430 | 5 to 8 | 1 ms GPU | yes |
| 1000 gabors drawn each frame | 9.13 to 9.20 | 130 to 141 | | |
| pass switch, per target pass | 9.0 to 9.1 us of frame time | 4.7 | 50 us CPU | yes |

The pass switch: 16 passes a frame, each one small shape into a target
between two scene draws, against the same 32 shapes with no pass. The
difference includes the batching that a pass breaks (32 draw calls
against 2). The first form of this bench (16 empty passes on one target)
gave 0.1 us, because consecutive passes on one target bind nothing; it did
not measure a switch and was replaced.

ADD is kept: 1000 gabors rendered once into a target and added each frame
cost 0.43 ms of GPU, against 9.1 to 9.2 ms drawn each frame (9.3 to 11.2 ms
in v0.1's runs). The target holds RGBA16F, so the increments are rounded
to half precision (2.8e-4 of full scale measured for 30 gabors at contrast
0.3; under a quarter of an 8-bit code).

### Open time and the SDF's call sites

The first v0.2 shaders called the SDF from three places in the aperture
(a fill, a band at one level, a band at two levels). `psygfx_open()` then
took 3.2 to 5.0 s on the Iris Xe (five runs) and 3.9 s on WARP, against
0.8 to 1.4 s in v0.1: ANGLE's D3D11 path inlines a function at every call
site, and this one holds two polygon loops. Now one function gives both
levels and has one call site: 1.31 to 1.43 s on the Iris Xe (five runs)
and 1.40 s on WARP, for eight built-in programs and the output stage.

## v0.3: what was added and why

### One vector program, always compiled

The v0.3 kinds, compounds, effects, dashes, trim and paint share one
built-in program. A stimulus's uniform block is 16 vec4 (256 bytes), and
the bound range is 4096 bytes. So the blocks after a stimulus's own block
can hold its data (polygon vertices, path points, primitives, effects,
paint stops and matrices, up to 247 vec4) at no compile cost: the shader
reads them with the same index arithmetic. A stimulus that uses such
"extension blocks" draws alone, as one instance; one that fits its block
batches as before. `desc.max_draws` now counts blocks. There is no
`desc.vector` flag: the program compiles at every open, which the open
time below pays for.

The block: vec4 0 the placement; 1 (half extents, px per unit, quad
margin); 2 (opacity, gate, offset, band center); 3 (rgb, band half width);
4 (dash, gap, dash offset, length); 5 (kind, profile, edge width, flags);
6 (trim start, trim end, cap, primitive count); 7 (second point's offset,
compound onion, point count); 8 (effect and paint offsets, path half
width); from 9 the data. A primitive is 4 vec4. Flags: DASH, TRIM, SMOOTH,
S (arc length needed), PAINT, FX, RBLUR.

Each function returns the distance, its gradient and the arc length s
together. The gradient serves the smooth operations' normalization and
the effects' offsets; s serves dashes and trim. The CPU has the same
functions in double (`psygfx__vfield()`), which the test compares and
`psygfx_hit()` uses.

### Compile time and GPU time on ANGLE's D3D11 path

The first vector program took 6 s to open and 4.5 ms of GPU for a
full-screen RRECT. What was found, by compiling variants in a harness
(`vharness.c`: compile time and GPU time of one GLSL file):

- **Loops that the compiler cannot bound are kept; loops it can bound are
  unrolled.** A field loop over a constant 56 primitives became 56 copies.
  The loop bounds now come from the frame block (`psy_axes.y` = 16,
  `.z` = 3), which the compiler cannot see as constants.
- **A nested loop makes FXC flatten.** An inner loop over a path's two
  points made both compile and GPU time about 8 times worse. The field is
  one flat loop (`for t < n * npts`).
- **The quadratic Bezier's closed form** (a cubic solve and Newton steps)
  was the largest single cost. QBEZIER is now drawn as a polyline of
  chords from the CPU, within 0.005 px of the curve. This is a departure:
  the design had it exact to 4e-5 px.
- **ANGLE compiles programs in parallel** when the program statuses are
  read only after every link has started. A probe with four programs went
  from 7.3 s to 3.0 s. Backend version 2 has deferred `pipeline_make` and
  a `pipeline_finish` that reads the status; `psygfx_open()` starts all
  eleven programs, then finishes each.
- A trick that turned branches into 0-or-1 loops (`PSY_IF`) made the
  compile slower and was removed.

Result: `psygfx_open()` 2143, 1957 and 2096 ms on the Iris Xe (bar 2.0 s;
v0.2: 1.3 to 1.4 s). There is no program binary cache (as decided).

### Trigonometry precision

D3D's `sin` and `cos` are coarse, and `atan` on D3D and SwiftShader is not
exact. So:

- the CPU computes every angle's sine and cosine (arc ends, pie edges, the
  star's sector) and puts them in the block;
- the star and the regular polygon fold a point into their sector by a
  power of a unit complex number, not by a `sin` and `cos` per pixel. A
  point that a coarse `atan` puts in the next sector is reflected back
  across the tip line. Removing that reflection passes on llvmpipe and
  SwiftShader but fails on both D3D11 renderers (a star's soft error
  7.3e-5 against a 5e-5 tolerance; 3.5e-5 against brute force where the
  guarded form gives 4.3e-6);
- an arc's s is measured from the middle of its sweep
  (`clamp(sw / 2 + atan(pv, pu), 0, sw)`), so the `atan` branch cut is
  outside the arc. SwiftShader put s off by the full sweep at angle 0
  before.

### Polyline interiors

A polyline as a union of boxes and join polygons is the Euclidean distance
outside, but inside it is only a bound near each inner corner. For the
Bezier's chords, that bound was 1.1 px off. With ROUND joins the field is
now the distance to the center line, minus the half width, which is
exact; a BUTT or SQUARE end adds the half strip past the last segment. The
chords then measure 0.0050 px from the curve. MITER and BEVEL keep the
box form: a bound inside near each turn.

### The quadratic smooth minimum, normalized

A raw smooth union has a gradient below 1 in the blend, so its soft edge
is up to 2.2 times wider there (0.19 to 0.20 of the step at a settled
neck, from the design's CPU numbers). After a smooth operation the field
is divided by its gradient's length: 0.015 at a settled neck. Where two
parts first connect, the field has a saddle with a zero gradient and no
first-order fix: 0.122 measured. A non-smooth fold of exact fields has a
unit gradient already; the test cannot see the division there (mutation
1 below is equivalent).

### v0.3 pixel tests: measured error and tolerance

`tests/adapt/psy_gfx_test.c`, the same method as v0.2: an RGBA32F scene,
read back, against a CPU reference in double at pixel centers. The parts
`v0.3 kinds` (243 stimuli: every kind, fill and the three strokes, soft
and hard, two angles), `v0.3 truth`, `v0.3 fx`, `v0.3 paint` and `v0.3
msdf`. Largest |error| of full scale; the tolerance is about 3 times the
worst renderer's error, with SwiftShader in its own column where it needs
one.

| Check | Tolerance (SwiftShader) | Iris Xe | WARP | SwiftShader | llvmpipe |
|---|---|---|---|---|---|
| kinds, soft, against the CPU form | 5e-5 (3e-4) | 1.7e-5 | 8.2e-6 | 9.6e-5 | 8.2e-6 |
| the same with dashes or trim | 1.5e-3 (1e-2) | 4.0e-4 | 4.0e-4 | 2.5e-3 | 5.8e-5 |
| hard pixels wrong; `psygfx_hit()` wrong | 0 | 0 | 0 | 0 | 0 |
| exact kinds against F(brute-force Euclidean d) | 6e-5 (3e-4) | 4.3e-6 | 4.9e-6 | 4.9e-6 | 4.9e-6 |
| the CPU form's distance against brute force | 1e-3 px | 9.4e-6 px | (CPU) | | |
| QBEZIER chords: coverage; distance | 2e-3; 0.0055 px | 1.3e-3; 0.0050 px | same | same | same |
| a dash pattern moved by one period against unmoved | 2e-3 (2e-2) | 3.1e-6 | 1.6e-5 | 1.6e-5 | 1.6e-5 |
| dash count on a circle; trim 0..1 against none | 0 wrong | 0 | 0 | 0 | 0 |
| effects in one pass against the layers drawn by the CPU | 5e-5 (3e-4) | 1.9e-5 | 5.7e-6 | 1.2e-4 | 5.7e-6 |
| EXACT_BLUR shadow against the erf product | 5e-5 (3e-4) | 1.5e-7 | 1.7e-7 | 1.7e-7 | 1.7e-7 |
| MULTIPLY against the product | 2e-7 | 4.8e-8 | 4.8e-8 | 4.8e-8 | 4.8e-8 |
| extension blocks batched with v0.2 stimuli, against single draws | 0 | 0 | 0 | 0 | 0 |
| paint RGB | 2e-6 | 1.2e-6 | 1.2e-6 | 6.4e-8 | 3.0e-7 |
| paint OKLAB, against Ottosson's matrices through the calibration | 2e-5 (1e-4) | 1.8e-6 | 1.8e-6 | 9.2e-7 | 9.5e-7 |
| paint DKL_POLAR, against `psygfx_cal_dir_dkl()` | 1e-4 (1e-3) | 1.5e-5 | 6.3e-6 | 1.6e-4 | 1.3e-6 |
| paint VERTEX, against barycentric | 2e-5 | 1.2e-7 | 1.1e-7 | 1.1e-7 | 1.1e-7 |
| MSDF against the CPU's bilinear median | 5e-5 (3e-4) | 3.5e-6 | 3.5e-6 | 3.5e-6 | 3.6e-6 |
| a glyph run against the same glyphs drawn alone | 1e-5 | 0 | 0 | 0 | 0 |
| refusals (range rule, MSDF with ROUND joins, blend on a modulation, ...) | all | all | all | all | all |

The dashes' error is the arc length's float error at a dash end, where
the coverage across s is steep; it is largest on SwiftShader. The MSDF
test atlas has different values per channel (R = v, G = v + 0.2, B = v -
0.15) so a mean in place of the median fails.

The CPU half also checks the parameter tables, `psygfx_bind.field` (all
or nothing), and Oklab's white (L = 1, a = b = 0 within 1e-3 through a
nominal sRGB calibration). Two development seams: `PSYGFX_TEST_PARTS`
runs named parts, `PSYGFX_TEST_VERBOSE` prints the worst pixels.

### Exactness as measured

Against references that share none of the header's arithmetic (brute-force
distance to a dense boundary sample, a supersampled blur, the erf
product). Deviation in units of the edge's step; Gaussian edge.

| Shape or operation | Distance | Measured | Predicted |
|---|---|---|---|
| RRECT, ARC, PIE, CAPSULE, NGON, STAR, POLYGON fillets, ELLIPSE 2:1 and 9:1 | exact | 4.9e-6 (float) | float only |
| QBEZIER | chords, 0.0050 px | 1.3e-3 | |
| UNION, reflex 90 degree crease (an L of two rects) | a bound inside | 0.0822 | 0.0830 |
| MITER corner, outside (v0.2) | a bound | 0.0800 | 0.0830 |
| SMOOTH_UNION normalized, settled neck | first order | 0.0150 | 0.012 to 0.015 |
| SMOOTH_UNION normalized, at the moment of connection | first order | 0.1220 | 0.12 to 0.15 |
| F(d) against the blur, 90 degree vertex | | 0.2500 | 0.25 |
| F(d) against the blur, disc of R = 5 SD, 20 SD | | 0.0406, 0.0100 | 0.0406, 0.0100 |
| drop shadow (F of the moved field) against the blur, at a corner | | 0.2484 | 0.25 |
| EXACT_BLUR | exact | 1.7e-7 | 0 |

Not measured separately: INTERSECT and SUBTRACT outside a convex crease,
XOR, POLYLINE MITER and BEVEL inside a turn (each a bound, about 0.083 at
90 degrees by the same geometry as the union's crease).

### v0.3 cost

`examples/gfx_bench.c --only v0.3`, Iris Xe, 1920 x 1200, RGBA16F scene,
AC, the measurement lock held, rows interleaved (25 rounds of 12 frames).
GPU over an empty frame, ms.

| Workload | GPU, ms | CPU over empty, us | Bar | Met |
|---|---|---|---|---|
| `psygfx_open()`, three runs | 2143, 1957, 2096 ms | | 2.0 s | 1 of 3 |
| v0.2 rounded rect, full screen, fill | 0.396 | 8 | | |
| RRECT (vector program), fill | 0.929 | 17 | 1.3x v0.2 | no: 2.3x |
| RRECT, LINEAR paint, RGB | 1.383 | 19 | | |
| RRECT, LINEAR paint, OKLAB | 1.536 | 20 | 1.1x solid | no: 1.65x |
| STAR 5, rounded | 0.860 | 20 | | |
| ELLIPSE | 1.315 | 27 | | |
| v0.2 circle, 8 px stroke | 0.271 | 8 | | |
| the same, dashed | 0.903 | 7 | 1.2x the stroke | no: 3.3x |
| compound of 1, 2, 8, 32 circles, full screen | 0.782, 1.219, 3.528, 12.433 | 9 to 21 | 0.15 ms per primitive per Mpx | no: 0.163 |
| compound of 8, 600 x 400 box | 0.617 | 15 | | |
| the same with every effect | 1.622 | 16 | 1.0 ms | no |
| 100 compounds of 4, 60 x 20, one draw each | 0.474 | 120 | 600 us CPU | yes |
| a run of 200 MSDF glyphs, 38 px | 0.121 | 18 | 0.2 ms, 20 us | yes |

The vector program's per-pixel cost is the price of one general program:
every pixel runs the fold loop, the dash and paint branches, and the
effects' tests, where v0.2's shape program has one switch. The v0.2 kinds
still draw with v0.2's program, so nothing v0.2 drew became slower. A
full-screen compound of 32 primitives (12.4 ms) does not fit a frame with
anything else; compounds are for stimuli of a few hundred pixels.

### Mutations

`mutate03.py` (scratchpad): each fault in a copy of the header, the test
built against it and run on llvmpipe; a fault must fail the test.

| # | Fault | Result |
|---|---|---|
| 0 | no gradient normalization after a smooth op | caught |
| 1 | normalized without a smooth op | equivalent: a hard fold of exact fields has a unit gradient |
| 2 | the second point without its offset | caught |
| 3 | SUBTRACT operands swapped | caught |
| 4 | onion sign | caught |
| 5 | circle arc length off its start | caught |
| 6 | trim end as a length, not a fraction | caught |
| 7 | OKLAB without the white scale | caught after the white check was added (a round trip cannot see it) |
| 8 | DKL azimuth turned the other way | caught |
| 9 | MULTIPLY without coverage | caught |
| 10 | the quad not grown for a moved primitive | caught |
| 11 | MSDF mean, not median | caught (after the test atlas got distinct channels) |
| 12 | range rule off | caught |
| 13 | extension data read at the next stimulus | caught |
| 14 | Wachspress colors shifted | caught |
| 15 | ellipse at 2 iterations | caught |
| 16 | Bezier chords at 0.5 px | caught |
| 17 | fillet convexity swapped | caught |
| 18 | exact blur with the wrong axis | caught |
| 19 | glyphs ignore the run scale | caught |
| 20 | ADD blend drawn as OVER | caught |
| 21 | one dash, not its neighbors | caught |
| 22 | deferred programs never finished | caught |
| 23 | star fold without the reflection | not on llvmpipe; caught on D3D11 hardware and WARP |
| 24 | ROUND polyline as boxes | caught |
| 25 | paint stops past the first ignored | caught |

### Departures from the design

- QBEZIER is chords within 0.005 px, not exact to 4e-5 px (compile time).
- The vector program misses four cost bars (above); open time is at the
  2.0 s bar, not under it.
- Dash ends with ROUND caps use the capsule distance across s and the
  boundary; on curves this is the design's "(1 + a / R) stretch".
- Dashes and trim on CROSS, masks and compounds are refused (no length).
- MITER and BEVEL in the vector program are on POLYLINE only; v0.2's
  MITER on RECT, CROSS and POLYGON is unchanged.
- CAPSULE's w is the full length, ends included (it fits its box).
- POLYLINE and QBEZIER take their width in shape_p[0], in units.
- A glyph record has no w, h: the run's scale and the atlas rectangle
  give the box.
- Trim and dash offset compare within tolerance, not bit for bit as the
  design asked (s is a float that the offset changes).
- A USER stimulus with a MASK_TEX aperture loses p[28..31] to the mask's
  rectangle.

### v0.3 builds

Warnings as errors everywhere (MSVC `/W4 /WX`; gcc `-Wall -Wextra
-Wpedantic -Wshadow -Werror`).

| Toolchain | Builds | Runs |
|---|---|---|
| MSVC 19.44, CMake (VS 2022), SDL3 | every target | ctest 40 of 40 pass; the gfx test on D3D11 hardware and WARP (CMake copies only `libEGL.dll` and `libGLESv2.dll`, so SwiftShader does not start there) |
| MSVC 19.51 (`cl`, VS 18) | the test as C11 and as C++17 | C++17 on SwiftShader through the Docker ANGLE folder: pass |
| MinGW-w64 gcc 16.1 | compile check and test, C99, C11, C++17 | not run |
| gcc 11.4, WSL2 | compile check and test, C99, C11, C++17 | C99 and C++17 on llvmpipe: pass |
| gcc 11.4, ASan and UBSan | the test | CPU half: pass; with llvmpipe (`detect_leaks=0`, Mesa's known 112 bytes): pass |
| WSL suite (rt, screen, audio, gfx, timeline, video) | `verify_all.sh` | all pass |
| emcc 6.0.10 (Docker) | compile check C11 and C++17, the test C11 | node: the CPU half passes |

### rig_spec 4.3

v0.3 stays inside "no vector drawing library": closed-form distance
functions, a bounded fold (56 primitives), bounded paths (224 points). No
SVG, no fill rules, no general path rasterizer. Artwork still goes
through the pack tool, now also as MSDF.

## v0.4: the program cache

### What ANGLE does with a binary

A probe on the Docker ANGLE (2.1.23876), 2026-10-06, under the lock:

| Fact | Iris Xe (D3D11) | WARP | SwiftShader |
|---|---|---|---|
| Binary formats | 1, 0x93A6 | 1, 0x93A6 | 1, 0x93A6 |
| The eleven programs' binaries | 456 KB (17 to 39 KB each, the vector program 112 KB) | 456 KB | 425 KB |
| `glProgramBinary` and the link status, all eleven | 2.8 ms | 9.3 ms | 0.4 ms |
| One byte flipped in the middle of a binary | linked: 10 of 11 programs | linked: 10 of 11 | linked: 11 of 11 |
| Half a binary under a valid header | the process ended (fast fail) | | |

GL_RENDERER carries the adapter and the driver version ("Direct3D11
vs_5_0 ps_5_0, D3D11-32.0.101.7088"), GL_VERSION the ANGLE build and git
hash. ANGLE does not check what it is given, so an entry carries its own
check, and the driver sees no byte that has not matched it.

EGL_ANDROID_blob_cache exists too. It was not used: psy_screen.h owns the
EGL display, the blob cache is set once per display, and its corruption
handling is ANGLE's. `glGetProgramBinary` is core GL ES 3.0 and keeps the
key, the check and the fallback in this header.

### The entry and the key

The key is FNV-1a 64 of the vendor, renderer, version and GLSL strings,
the binary format, and the vertex and fragment text. The 48-byte header
holds the key, a second hash of the same material (a multiply-xorshift
hash), the material's length, the binary's format and size, and FNV-1a 64
of the binary. A load is used only when every field matches. A forced
collision (a test seam sets every key to 0) shows the second hash at work:
each program finds another program's entry, rejects it, compiles, and the
frame is bit-identical.

### Tests

`v0.4 cache` (GL, each renderer): a reference frame with no cache (a
gabor, a soft circle, an RRECT, a linear image, noise, a grating in a
circle, ordered dither), then eight opens with an in-memory cache, each
followed by the same frame, compared bit for bit, and the counters:

| Case | Loaded | Compiled | Rejected | Frame |
|---|---|---|---|---|
| cold | 0 | 11 | 0 | equal |
| warm | 11 | 0 | 0 | equal |
| a byte flipped in each payload | 0 | 11 | 11 | equal |
| warm again (the entries were rewritten) | 11 (and a user pipeline) | 0 | 0 | equal |
| every key 0, cold | 0 | 11 | | equal |
| every key 0, warm | 1 | 10 | 10 | equal |
| another driver identity | 0 | 11 | 0 | equal |
| a binary of another ANGLE build under a valid header | 0 | 11 | 11 | equal |
| the file cache: cold, warm, a folder that cannot be made | as expected | | | equal |

The CPU half runs everywhere with a backend whose binary is a hash of the
program's text: a changed vendor, renderer or version is a miss; a
flipped byte, a flipped magic and a truncation are rejects; a binary the
driver refuses is compiled; with every key 0 nothing is loaded; a user
pipeline is cached. The whole GL suite runs with an in-memory cache, so
every other pixel check also runs on loaded binaries.

### Open time

The probe's 2.8 ms for eleven loads became 11 to 31 ms for the whole open
(scene, CLUT, buffers, eleven loads). The table: fresh processes, five
rounds interleaved (none, cold, warm), the lock held. Run 1 on AC; the
WARP and SwiftShader runs on AC.

| Renderer | No cache | Cold (empty cache, stores) | Warm |
|---|---|---|---|
| Iris Xe | 2410 to 4569 ms | 2289 to 4678 ms | 11.4 to 22.3 ms |
| WARP | 1808 to 2666 ms | 1829 to 2541 ms | 17.8 to 30.7 ms |
| SwiftShader | 42 to 80 ms | 155 to 176 ms | 4.0 to 4.4 ms |

What a cold open adds, timed inside open (the reads of the binaries and
the stores), three runs each: an in-memory cache 1.3 to 2.6 ms on the Iris
Xe; the file cache 39 to 121 ms (eleven files). Bar: a cold open at most
5 % slower than none: met (at most 121 ms of 2.3 s or more). SwiftShader
pays more (133 to 347 ms): reading a binary back makes ANGLE's Vulkan
backend build what it would otherwise build at the first draw.

v0.4 against v0.3.0 with no cache, to check that the version-3 backend
and the cache's code cost nothing when no cache is given: fresh
processes, eight rounds interleaved, the lock held, 2026-10-06 09:56 to
09:58. **A loaded machine:** on battery, and another worker's build ran
during rounds 2 to 4, 7 and 8.

| Header | Open, ms (8 rounds) | Median |
|---|---|---|
| v0.3.0 (git HEAD) | 7489, 4603, 3384, 4122, 4010, 4743, 3524, 3231 | 4066 |
| v0.4 | 4751, 3305, 4008, 3743, 4355, 3646, 3370, 3475 | 3694 |

No regression. Both headers moved together from v0.3's 1.96 to 2.14 s
(measured on 2026-10-05); the machine moved. PROGRAM_BINARY_RETRIEVABLE_HINT
is set only when a cache is given.

## v0.4: video

### What ANGLE imports

A probe on the Iris Xe (2026-10-06): an NV12 texture on ANGLE's D3D11
device, imported plane by plane with `eglCreateImageKHR(EGL_D3D11_TEXTURE_ANGLE,
{EGL_D3D11_TEXTURE_PLANE_ANGLE, p})` and `glEGLImageTargetTexture2DOES`:
plane 0 an R8 texture, plane 1 RG8, read back exactly; slice 1 of a
2-slice array made with D3D11_BIND_DECODER through
EGL_D3D11_TEXTURE_ARRAY_SLICE_ANGLE, exactly. RGBA8 and BGRA8 textures
import with no attributes. The imported textures cannot be rendered to
(the framebuffer is incomplete); psy_gfx only samples them.

### The conversion

Y' at the image point; chroma at the stated siting in its own plane
(texel-edge coordinates c = L / 2, plus 0.25 across for LEFT, both ways
for TOP_LEFT), bilinear by four texelFetch clamped to the plane, or
replicated; the matrix and range from constants the CPU puts in the
block; R'G'B' clamped; the transfer; the primaries' 3x3. At 1:1 the
weights are psy_video.h's sixteenths: CENTER takes 9/16, 3/16, 3/16, 1/16;
LEFT takes the co-sited sample whole and the mean of two between them.

TRC_DEVICE: at open (and at each `psygfx_set_lut()`) a 256 x 3 table holds,
per gun, the linear value the CLUT maps to each code k / 255, inverted on
the CPU. The output stage then writes code k back: all 256 codes through
a nominal calibration's CLUT came out as they went in.

The video program is compiled the first time an encoded texture is made
or imported, so an open without video compiles nothing more and the
plain IMAGE program is the same text as v0.3's (plus the straight-alpha
fix).

### Pixel tests

`v0.4 video`, an RGBA32F scene against a double reference built from the
definitions (H.273's matrices and ranges, the transfers' formulas, the
luma equation for green, chroma weights from the siting), none of the
header's arithmetic. Frames of 36 or 37 x 23 or 24 texels (odd and even),
random codes with some 0 and 255 (out of range for LIMITED, so the clamp
is exercised).

| Check | Iris Xe | WARP | SwiftShader | Tolerance |
|---|---|---|---|---|
| NV12 and I420 x 3 matrices x 2 ranges x 3 sitings x 2 chroma filters, 1:1 (72) | 2.7e-7 | 3.1e-7 | 3.0e-7 | 1e-6 |
| each transfer (DEVICE, BT1886, SRGB, LINEAR, GAMMA22) | 4.4e-7 | 6.6e-7 | 7.5e-7 | 2e-6 |
| linear sampling at 2.5x | 6.2e-6 | 5.8e-6 | 5.5e-6 | 2e-5 |
| one plane's rectangle, then all planes, updated | 5.1e-7 | 6.6e-7 | 6.3e-7 | 2e-6 |
| BT.709 and BT.2020 primaries through a calibration (EDID chromaticities, nominal) | 1.1e-6 | 1.3e-6 | 9.8e-7 | 4e-6 |
| TRC_DEVICE: the 256 gray codes through a non-identity CLUT | 0 wrong | 0 wrong | 0 wrong | 0 |
| rebind: the other's texels, its own, its own after the other is freed | 5.1e-7 | 5.0e-7 | 6.3e-7 | 2e-6 |
| GL import of a texture's planes (the source survives the import's free) | 5.1e-7 | 5.0e-7 | 6.3e-7 | 2e-6 |
| D3D11 NV12 import, slice 1 of an array on ANGLE's device | 5.1e-7 | 5.0e-7 | (no D3D11) | 2e-6 |
| NV12 from a second D3D11 device by an NT handle and a keyed mutex | 5.1e-7 | 5.2e-7 | | 2e-6 |
| the same by a legacy shared handle, after the producer's flush and an event query | 5.1e-7 | 5.2e-7 | | 2e-6 |
| refusals (no encoding; primaries without a calibration; siting NONE on 4:2:0; texture_update on a planar texture; a video as a modulation; rebind across formats) | 6 of 6 | 6 of 6 | 6 of 6 | all |

The shared-handle cases are psy_video.h's SHARED path: the producer
writes on its own device's immediate context; ANGLE's device opens the
texture by its handle; the consumer acquires key 1 before `psygfx_end()`
and releases key 0 after the frame. The test cannot show what happens
without the keyed mutex: the producer's write finishes long before ANGLE
samples. The order above is the documented contract, not a measured need.

### Cost, from a loaded machine

`gfx_bench --only "v0.4 video"`, 1920 x 1080 frames in a 1920 x 1200
scene, the lock held, 2026-10-06 09:58. **On battery, with another
worker's build running at the end.** GPU over an empty frame, the median
of 25 interleaved rounds.

| Workload | GPU, ms | CPU over empty, us |
|---|---|---|
| RGBA8, draw | 0.890 | 32 |
| NV12, BT.709, BT1886, draw | 1.227 | 48 |
| I420, the same, draw | 1.329 | 51 |
| RGBA8, upload + draw | 3.403 | 2117 |
| NV12, upload planes + draw | 2.290 | 755 |
| (second table) RGBA8, draw | 0.719 | 23 |
| NV12, transfer LINEAR | 1.125 | 32 |
| NV12, transfer BT1886 | 1.111 | 25 |
| NV12, transfer DEVICE | 1.275 | 21 |

The update call alone, 400 calls each, A/B/A/B: RGBA8 (8.3 MB) 1107 to
1253 us mean, 1848 to 2192 us p99; NV12 (3.1 MB) 360 to 362 us mean, 535
to 556 us p99; I420 367 to 402 us mean. `psygfx_texture_rebind()`: 0.001
us a call.

The knob for the GPU time. Chroma costs 8 texelFetch a pixel for NV12
and 16 for I420 (the hand bilinear, kept for exactness), and DEVICE 3
more. The sampler's own bilinear (one `texture()` a plane) would remove
most of them. Its weights: at 1:1 with the supported sitings the chroma
sample point falls on fractions 0, 1/4, 1/2 or 3/4 of a texel (CENTER: 1/4
and 3/4 both ways; LEFT: 0 and 1/2 across, 1/4 and 3/4 down; TOP_LEFT: 0
and 1/2), which a sampler's 8-bit subtexel weights hold exactly, so the
fast path would give the same weights at 1:1. Scaled, its weights are
quantized to 1/256: an error of up to 1/512 of the step between two
chroma samples, under 1/255 of a code. The filter's own arithmetic on R8
texels was not checked. Not built (the coordinator's decision, 2026-10-06).

## v0.4: instances

### Design

A template stimulus (one uniform block) and an array of 32-byte
`psygfx_inst` records, all float so `psygfx_bind.field` binds any of them,
drawn in one instanced draw. draw() copies the records into the frame's
element staging; end() uploads them in one call into one of three element
buffers (the ring of the uniform blocks) and draws with two vec4
attributes, the glyph runs' layout. The template's block is followed by
two more: the anchor in the pass's GL px and the center's offset from it,
px per unit, the field mask, and the palette.

The vertex shader computes each element's center and axes and hands the
fragment shader flat replacements for the placement, look, color or dir
and misc. A built-in body sees the template's frame at p / k (k the
element's scale) and every distance is multiplied by k: a uniform scale is
exact for any SDF, and what is in px (edge width, stroke, offset) is added
after, so it does not scale. A user body sees its own px and psy_size
scaled, as a group of that scale gives it.

D3D's `sin` and `cos` are coarse (v0.3), so an element's angle comes from
a polynomial in degrees: the turn reduced to a quarter, then degree-9 and
degree-8 Taylor terms. Run in float on the CPU, as the shader runs it,
from -720 to 720 degrees in steps of 0.001 degree: within 4.3e-7 of
double.

### Timeline bindings and the hit test

A template field moves every element (a `psygfx_bind` on the instanced
stimulus, which holds the template's fields); `psygfx_bind.field =
&items[k].gate` moves one element. No new binding kind was needed.
`psygfx_hit_index()` builds element i as its own stimulus (the template in
a one-point group at the element's anchor, scaled and turned there) and
runs the existing hit test on it, from the highest index down, skipping
gate 0: the topmost element under a pixel, by the same field the GPU
draws.

### Tests

`v0.4 inst`: each element against the same stimulus drawn alone, which
the test composes itself (a one-point group at the element's anchor with
its scale and turn, the fields applied); an RGBA32F scene, largest
difference of full scale:

| Template | Iris Xe | WARP | SwiftShader | Tolerance |
|---|---|---|---|---|
| gabor, every field (dir from the palette); two arrays in one frame | 8.3e-7 | 8.3e-7 | 8.3e-7 | 3e-5 |
| grating in a cosine-edged circle; noise | 2.9e-6 | 3.4e-6 | 3.3e-6 | 3e-5 |
| stroked CROSS (v0.2 program); vector RRECT with an offset | 9.5e-7 | 1.0e-6 | 1.0e-6 | 3e-5 |
| linear image tinted by the palette | 9.5e-7 | 9.5e-7 | 9.5e-7 | 3e-5 |
| USER at scale 0.5 to 1.5, turned, about an anchor at (0.3, 0.6) | 9.1e-6 | 9.2e-6 | 9.1e-6 | 3e-5 |
| palette: integer entries (bit for bit); fractions against double | 0 wrong; 1.1e-7 | 0; 1.1e-7 | 0; 1.1e-7 | 0; 2e-7 |
| `psygfx_hit_index()` against the GPU: 16 overlapping turned and scaled rects, one with gate 0 | 0 of 24712 wrong (4 ties) | same | same | 0 |
| bindings: a template field, one element's field | 0 wrong | | | |
| refusals: a DOTS template, PHASE on a shape, COLOR without a palette, SCALE on NOISE, a template with extension blocks, more than max_instances, 9000 + 9000 elements in one frame | 8 of 8 | 8 of 8 | 8 of 8 | all |

The USER case is the coordinator's condition: the user's shader sees the
same local coordinates whether drawn as an element or alone.

## v0.4: the draw order

### What interleaving cost

1000 stimuli, four kinds in turn (gabor, soft circle, grating in a circle,
RRECT), on a grid, no overlap, against the same stimuli grouped by kind by
the caller (2026-10-06 10:36, AC, the lock held, no build running):

| Workload | GPU over empty, ms | CPU over empty, us |
|---|---|---|
| interleaved, call order | 5.520 | 2239 |
| grouped by kind | 0.470 | 124 |
| interleaved, 10 % overlapping their neighbor | 5.517 | 2217 |

So batching by call order gave interleaved kinds about one draw each, and
about 5 us of GPU a draw through ANGLE's D3D11 backend. Built.

### The rule

In end(), within each pass, a draw joins the latest earlier batch of its
kind (the same program, textures and blend, room for one more) when its
bounds meet none of the batches between. The commands and their blocks
are then written in the new order to a second staging, the members of a
batch next to each other, and the two are swapped; nothing is allocated
in the frame. The bounds: the draw's own quad, center +- (half size +
margin) along its turned axes, plus 1 px; every fragment a draw writes is
in its quad (soft edges, strokes, offsets and effects grow the margin).
Instanced draws, dot fields and glyph runs place their pieces anywhere:
their bounds are the whole pass, so nothing passes them. A draw looks
back at most 64 batches.

Two draws with disjoint bounds write no common pixel, and blending reads
only the pixel it writes, so their order cannot change a value, for any
blend: ADD (the gabors) moves too, and the result is bit-identical (the
coordinator first excluded ADD; the argument was accepted). Target passes
keep their order: each is its own segment. A group needs no barrier: it
is composed into each draw's block on the CPU and sets no GL state.

### Tests

`v0.4 order`: each frame drawn in call order (the seam `no_reorder`) and
reordered; the scene (RGBA32F) and the output codes (ordered dither)
compared bit for bit.

| Scene | Frames | Differed |
|---|---|---|
| sparse grids of 160 stimuli of five kinds (gabor, soft circle, RRECT, grating, linear image), turned | 3 | 0 |
| dense: 400 at random places, most overlapping | 3 | 0 |
| a draw that moves past two batches to join an earlier run, and one that may not pass a gabor three draws back | 1 | 0 |
| a Gaussian-edged circle next to an RRECT, the quads from overlapping by 6.5 px to apart by 2.5 px | 9 | 0 |
| an RRECT 40 x 40 at 45 degrees whose corner meets a circle's fringe, a copy of it far away | 4 | 0 |

On all three renderers; 520 to 559 draw calls saved over the 20 frames.
The two-batch move is checked by count: 7 draws become 5, not 4.

### Cost

`gfx_bench --only "v0.4 mixed"`, 2026-10-06 11:24, AC, the lock held, no
build running during the part:

| Workload | GPU over empty, ms | CPU over empty, us |
|---|---|---|
| 1000, 4 kinds interleaved, call order | 7.197 | 3335 |
| the same, reordered | 1.133 | 467 |
| the same, grouped by kind by the caller | 0.574 | 170 |
| 10 % overlapping, call order | 7.305 | 3336 |
| 10 % overlapping, reordered | 1.158 | 450 |
| 1000 at random places (dense), call order | 6.937 | 3294 |
| the same, reordered | 1.624 | 642 |

The reorder saves 6.1 ms of GPU and 2.9 ms of CPU a frame here; its own
cost is inside the 467 us (the grouped row shows what is left to gain: a
caller who groups by kind still draws fewer, larger batches, because a
batch's union bounds grow and stop later draws). Kept.

## v0.4: the gallery's findings, and an audit

Found by the gallery worker (examples/gfx_gallery.c), fixed, each with a
test and a mutation:

| Finding | Fix |
|---|---|
| `psygfx_cal_nominal()`, `cal_set_spectra()`, `cal_derive()`, then open: "CRC mismatch" until a `cal_save()` | derive() seals the CRC; `cal_add()` and `cal_set_spectra()` unseal it. The test runs the manual's recipe as written, and refuses a reading changed after derive |
| A straight-alpha sprite drawn linear at 16x got dark fringes | the color image program filters straight-alpha RGBA and RG texels as premultiplied, then divides back; `image_desc.premultiplied` states a premultiplied texture. 1.0e-7 (Iris Xe), 6.3e-8 (WARP), 1.3e-7 (SwiftShader) from the premultiplied bilinear in double; v0.3's way was 0.196 off |
| Glyph runs and masks on a DIST atlas clipped silently | the range rule holds on DIST atlases with `texture_desc.sdf_range` (twice the padding); only the outer reach is checked (inside, a DIST atlas holds true distances). A glyph run or a src rectangle on a DIST atlas with no range is refused. A whole-texture DIST mask without a range is v0.2's, unchecked |
| A glyph run needed its own buffer | `first` on glyph and dot descs: a run from any record of a shared buffer; bit-equal to its own buffer |
| An aperture's shape_p had to be set after construction | `shape_p[4]` on the grating, noise and user descs; bit-equal to setting it by hand |

Found here, by the dots' mutation passing: **`psygfx_dots_desc.aperture`
0 is PSYGFX_RECT, so a dot field with no aperture set had a 0 x 0 RECT
field and every dot was culled**; the manual said the default was no
aperture. Since v0.1. `gfx_bench`'s dots rows (above, "Frame cost") and
`gfx_load`'s "10000 dots" run drew nothing; their numbers are void. The
gallery set its apertures and was not affected.

The audit of every constructor for the same trap (a nonempty stimulus
that draws nothing by a zero default or a zero field):

| Constructor | Zero default or field | Now |
|---|---|---|
| dots | aperture 0 = RECT, w 0 | no aperture (the manual's default); an explicit RECT or CIRCLE of no area, or dot_size 0 with dots, refused |
| dots, glyph runs | count 0 | legal, draws nothing, returns OK (v0.3: ERR_ARG with no message) |
| shape POLYGON | w, h 0 with vertices | the box is the vertices' own (they are from its center) |
| shape CIRCLE, RECT, CROSS, LINE, ANNULUS; grating, noise, user | w (or LINE's width, CROSS's arm, ANNULUS's ring) 0 | refused with a message naming the field (v0.3 drew nothing) |
| image | w, h 0 with no gfx to take the texels' size from | refused |
| gabor | sigma 0 | refused with a message (v0.3: ERR_ARG with none) |
| vector kinds, compounds, paths | w, h, widths 0 | refused already (v0.3) |
| instances | a refused `psygfx_instances()` | draws nothing (v0.3 would have drawn the template once) |

Every `return 0` in the packing now names its reason in `psygfx_error()`
(a missing texture, buffer or pipeline; a src rectangle outside its
texture; a mask's format or aspect; MITER where it does not apply). Test:
14 cases, each refused with a message or drawn as the manual says; a
polygon with no box drew 660 px, dots with no aperture 80 px.

## v0.4 cost

All on the Iris Xe at 1920 x 1200, RGBA16F scene, the lock held,
`gfx_bench`. Three sessions, each named by its conditions:

- A: 2026-10-06 10:36, AC, no build running (instances, mixed kinds).
- B: 2026-10-06 11:19 to 11:26, AC, the re-run of every bar; a build
  ran during open rounds 2 and 4 (the run paused while one was seen) and
  at the end of the instances part.
- C: 2026-10-06 11:27, AC, no build seen, but every row about 2.3 times
  B's, the empty frame included: an unknown load. Given for the ratios.

### Open

| Session | No cache | Cold | Warm |
|---|---|---|---|
| B, five rounds | 2346 to 5287 ms | 2218 to 7590 ms | 11.5 to 35.0 ms |
| earlier, AC (see "Open time") | 2410 to 4569 ms | 2289 to 4678 ms | 11.4 to 22.3 ms |

Bars: warm at most 100 ms: met. Cold at most 5 % over none: met by the
store's own time (32 to 120 ms of a 2.2 s or longer open).

### Video, 1920 x 1080 (session B)

| Workload | GPU over empty, ms | Ratio to RGBA8 | CPU over empty, us |
|---|---|---|---|
| RGBA8, draw | 0.555 | | 6 |
| NV12, BT.709, BT1886, draw | 0.908 | 1.64 | 11 |
| I420, the same | 0.935 | 1.68 | 10 |
| RGBA8, upload + draw | 1.898 | | 955 |
| NV12, upload planes + draw | 1.417 | 0.75 | 355 |
| (transfers table) RGBA8, draw | 0.585 | | 12 |
| NV12, LINEAR | 0.954 | 1.63 | 14 |
| NV12, BT1886 | 0.971 | 1.66 | 18 |
| NV12, DEVICE | 1.119 | 1.91 | 17 |

The update call alone, A/B/A/B: RGBA8 906 and 1026 us mean (1631, 2351 p99);
NV12 304 and 389 us (424, 799); I420 316 and 300 us (524, 476): 30 to 38 %
of RGBA8's. Bars: update at most 50 % of RGBA8's: met. GPU at most 1.0 ms:
met by NV12 and I420 with BT1886 and LINEAR (0.91 to 0.97 ms), missed by
DEVICE (1.12 ms). GPU at most 1.5 times RGBA8: missed (1.63 to 1.91). The
knob is the chroma fetches (above, "Cost, from a loaded machine"). A
decoded 1080p NV12 frame uploaded and drawn takes 1.4 ms of GPU and 0.36
ms of CPU: it fits a 16 ms frame with room for the rest.

### Instances

| Workload | GPU over empty, ms: A / B / C | CPU over empty, us: A / B / C |
|---|---|---|
| 10000 gabors 32 x 32, one stimulus each | 5.54 / 5.53 / 14.3 | 2804 / 3089 / 8321 |
| the same, instanced (ring of 3 element buffers) | 1.26 / 1.33 / 3.13 | 177 / 191 / 527 |
| the same, one element buffer orphaned each frame | 1.28 / 1.44 / 3.21 | 191 / 211 / 384 |
| instanced, every element's ori set each frame | 1.38 / 1.52 / 3.25 | 167 / 244 / 425 |
| 10000 LINE 16 px, instanced | 0.54 / 0.62 / 1.37 | 258 / 218 / 498 |
| 10000 RRECT 14 x 9 (vector), instanced | 1.62 / 1.70 / 4.10 | 152 / 212 / 466 |
| 1000 gabors 256 x 256, one stimulus each | 8.61 / 10.7 / 24.1 | 263 / 306 / 646 |
| the same, instanced | 4.59 / 5.87 / 11.9 | 6 / 10 / 83 |
| `psygfx_hit_index()` over 10000 RECT elements | 546 us (A), 1381 us (B, a build ran), 702 us (C) | |

Bars (A and B): CPU at most 0.2 ms for 10000: met (177, 191 us); GPU at
most 2 ms and at most one stimulus each: met (1.26 to 1.33 against 5.5);
1000 large gabors within 5 % of one stimulus each: better, 0.53 to 0.55 of
it; hit test at most 1 ms: met in A and C. The 1000 large gabors cost
about half instanced: a likely cause is that the batched path indexes
the uniform array per fragment while an element's values arrive as flat
inputs; not confirmed. The ring and orphaning were equal within the
noise in all three sessions (the ring 2 to 8 % faster on the GPU): the
ring was kept and the orphaning seam deleted. 10000 elements of any of
these kinds fit a 16 ms frame.

### Dots, re-measured (session B)

The rows of "Frame cost" drew nothing (above). Now drawn, `gfx_bench
--only dots`, 120 frames back to back, GPU per frame including the empty
frame (1.0 to 1.1 ms in the same session):

| Workload | CPU mean, us | CPU p99, us | GPU, ms |
|---|---|---|---|
| dots x 1000, 4 px, upload every frame | 198 | 370 | 0.93 |
| dots x 10000 | 171 | 393 | 1.30 |
| dots x 100000 | 1301 | 1876 | 4.98 |

Bars (v0.1's): 10000, GPU at most 0.5 ms over empty: met (about 0.2);
100000, CPU at most 2 ms: met; GPU at most 3 ms: missed (about 3.9 over
empty). `gfx_load`'s window run was not re-measured (it needs the panel,
which the photodiode setup is using).

### No regression (session B)

| Row | v0.3 (2026-10-05) | v0.4 |
|---|---|---|
| empty frame, identity CLUT (GPU per frame) | 0.46 to 1.15 ms | 1.08 to 1.11 ms |
| 1000 gabors 256 x 256, batched (GPU per frame) | 9.3 to 11.2 ms | 9.20, 9.45 ms (25.3 ms once, in a session where everything was slow) |
| v0.2 rounded rect, RRECT, RRECT with OKLAB paint, dashed stroke, compound of 8 (GPU over empty) | 0.396, 0.929, 1.536, 0.903, 3.528 ms | 0.440, 0.967, 1.629, 0.983, 3.648 ms |
| a run of 200 MSDF glyphs | 0.121 ms | 0.122 ms |

The v0.3 rows moved by 1 to 11 % together, the v0.2 rect (whose program
did not change) as much as the others: the machine, not v0.4.

## v0.4 mutations

`mutate04.pl` and `mutrun.sh` (scratchpad): each fault in a copy of the
header, the test built against it (MSVC) and run on the Iris Xe (the CPU
half alone for the CPU faults); a fault must fail the test.

| # | Fault | Result |
|---|---|---|
| 0 | the payload hash not checked | caught |
| 1 | the key without GL_RENDERER | caught (after the fake backend's renderer string was varied) |
| 2 | the key without GL_VERSION | caught (same) |
| 3 | block and sampler bindings not set after a binary load | caught |
| 4 | a binary the driver refuses not compiled | caught |
| 5 | the second hash and the length not checked | caught |
| 6 | Cb and Cr swapped | caught |
| 7 | limited range without its 16 / 219 offset | caught |
| 8 | LEFT siting as CENTER | caught |
| 9 | chroma replicated when sited | caught |
| 10 | no clamp before the transfer | caught |
| 11 | BT.709 weights for BT.601 | caught |
| 12 | the display's transfer half a code off | caught |
| 13 | rebind ignored | caught |
| 14 | the primaries' matrix in the wrong order | caught |
| 15 | an element's ori turned the other way | caught |
| 16 | an element's phase not added | caught |
| 17 | the field not rescaled at an element's scale | caught |
| 18 | a palette fraction ignored | caught |
| 19 | element offsets along the screen's axes, not the template's | caught |
| 20 | an element's gate not multiplied | caught |
| 21 | the vector program's look taken as the others' | caught |
| 22 | two element arrays read at one offset | caught (after a two-array check was added) |
| 23 | `psygfx_hit_index()` from the lowest index | caught |
| 24 | the CRC not sealed by derive | caught (after the check compared the CRC itself) |
| 25 | straight alpha filtered as v0.3 did | caught |
| 26 | the DIST range rule off | caught |
| 27 | dot fields ignore first | caught (after the dots bug was found and fixed: its test drew nothing) |
| 28 | the reorder ignores overlap | caught |
| 29 | the reorder checks only its neighbor batch | caught |
| 30 | bounds without the quad's margin | caught |
| 31 | bounds without the turn | caught (after the turned-box case was added) |
| 32 | a stimulus of no area drawn as nothing | caught |
| 33 | a dot field's zero default as a 0 x 0 RECT | caught |
| 34 | a refused element array draws its template | caught (after the scene was checked) |

35 of 35.

## v0.4 builds

Warnings as errors. MSVC 19.44 (CMake, build-gfx; and `cl` alone): the
test as C and as C++17, the compile checks, `gfx_bench`, the examples and
`test_psy_video`; the full test on the Iris Xe, WARP and SwiftShader.
MinGW-w64 gcc 16.1 (winlibs): the test as C99, C11 and C++17, the
psy_gfx compile checks (C11, C++17) against SDL3, psy_video's C11 compile
check, `gfx_bench`; the C99 and C++17 tests run on WARP and SwiftShader.
Not run for v0.4: WSL (gcc, llvmpipe, sanitizers) and emcc. llvmpipe would
cover the cache's GL half with Mesa's binary format: CI will show it.

## v0.4 departures from the design

- Element bindings: no new binding kind was needed (the template's fields and
  `psygfx_bind.field`).
- Templates: USER without extension blocks, as approved; the design had
  "any kind except USER with extension blocks".
- The palette holds 16 entries, as approved.
- The reorder, the audit and the gallery's fixes were not in the design;
  they were asked for during the work.
- No kind-specialized vector programs (below).

## v0.5: the calibration moved to psy_color.h

The calibration (`psycol_cal`, the same .psycal bytes), its calls, the
cone table and PAINT's and VIDEO's color math are psy_color.h's now;
docs/psy_color.md has the design and the stages. psy_gfx.h keeps the CLUT
and transfer textures, the output stage, dithering, every shader, the
per-draw gamut check and `psygfx_clipped()`.

| Item | State |
|---|---|
| Stage A: the code moved, aliases for callers | Readback hashes of the whole test equal v0.4.0's on the Iris Xe, WARP and SwiftShader. |
| Stage B: callers renamed, aliases deleted | Hashes equal again. psy_video.h needed no change. |
| Stage C: a `psycol_ctx` at open; `desc.cones`, `desc.lum`, `psygfx_color()`, `PSYGFX_EV_COLOR` | v0.4's parts: hashes equal to stage B's on all three. |
| OKLAB on absolute XYZ, the black included | The shader adds the row's w for OKLAB as it did for DKL_POLAR. A black without light changes no pixel. On a calibration with 0.5 cd/m2 of black under 80 cd/m2 of white, the test's gradient is within 6.98e-7 / 7.28e-7 / 7.28e-7 (Iris Xe / WARP / SwiftShader) of an independent double reference; v0.4's form would have moved it by 5.20e-3. |
| A calibration that is not sealed | Refused at open (psy_color.h's context checks the CRC); derive() and save() seal it, as since v0.4. |

The CPU form of OKLAB back to RGB (the clip sampling of a gradient) keeps
Ottosson's printed 10-digit inverse of M2, as the shader does; psy_color.h
inverts M2 exactly, 2.4e-7 away in linear RGB.

## Next

1. **Kind-specialized vector programs**, designed in the session notes of
   2026-10-06 (defines per kind, a kinds mask for compounds, dash, paint
   and space): compile time and GPU time of each variant in the harness
   first; build one only where it closes at least half of its bar's gap
   (RRECT 2.3x, dashes 3.3x, OKLAB 1.65x, compound of 8 with effects 1.6
   ms). The program cache makes the extra programs cheap at open.
2. **Sampler-filtered chroma** for the video program's 1.0 ms and 1.5x
   bars (exact weights at 1:1).
3. WSL, llvmpipe, sanitizer and emcc runs of v0.4.

## CI

The `screen` job builds psy_gfx.h with SDL3 on Windows, Linux and macOS and
runs the pixel test where software GL exists:

| Runner | Renderers | Source |
|---|---|---|
| Linux | Mesa llvmpipe | `libegl1 libegl-mesa0 libgl1-mesa-dri` from apt |
| Windows | ANGLE on WARP and on SwiftShader | Electron v38.8.6's zip, SHA-256 `366ae2b4aa9e6bc89b98c8b5831d46303e45cd49b2970c2b3921de5f2fcfc2e1`, cached; its ANGLE reports `2.1.25848 git hash: 85cc0e0b9cc3` |
| macOS | none: the CPU checks only | |

Electron v44's zip no longer has `libEGL.dll` and `libGLESv2.dll`. v38.8.6
is the newest release line that was checked and still has them. The test
gave the same errors with it as with the Docker copy. The wasm job runs the
compile check and the CPU half of the test in node. The sanitizer job runs
the CPU half: under ASan, Mesa leaves 112 bytes in 2 allocations from a
module it unloads, which a suppression cannot name, so the test runs GL
under a sanitizer only when `PSYGFX_TEST_DEVICES` asks for it. Locally,
with GL on, everything passed under ASan and UBSan apart from that leak.

## Not measured

- Light. A CLUT, a dither, a calibration: all checked as arithmetic, none
  against a photometer.
- 10-bit output, Mono++, Color++, stereo, text, GPU filtered noise.
- Fullscreen. gfx changes what is drawn, not how it is presented; the
  swap path's numbers are psy_screen.md's.
- Any GPU but the Iris Xe for timing; any OS but Windows 11 for timing.
- The GPU time of the output stage inside a real frame loop (the GPU timer
  query costs too much per frame; the bench measures it offline).
- v0.2 in a real frame loop: the window runs and the allocation count
  above are v0.1's. v0.2 allocates nothing new after open by design (fixed
  frame-block slots, segments in the handle), but no frame loop with
  target passes was counted.
- Stroke, group and target costs on WARP, SwiftShader and llvmpipe: their
  pixels are tested, their speed is not.
- v0.3 in a real frame loop: no `gfx_load` window run and no allocation
  count with vector stimuli. The vector path allocates nothing after open
  by design (the staging has 16 blocks of slack for extension blocks).
- v0.3 costs on renderers other than the Iris Xe, and on battery.
- ELLIPSE's distance error above an aspect of 9:1 (the test stops there;
  20:1 is allowed).
- v0.4 on WSL (gcc, llvmpipe, ASan, UBSan) and emcc; the cache's GL half
  on Mesa's binary format.
- `gfx_load` with dots that draw (the v0.1 window run drew none).
- D3D11 import on another GPU or driver; what leaving out the keyed
  mutex does (the test's producer finishes long before ANGLE samples).
- v0.4 in a real frame loop: no window run and no allocation count with
  instances, video or the reorder (none allocates in the frame by design).
- v0.4 costs on renderers other than the Iris Xe, and on battery (the
  battery rows above are named).
