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
| dots x 1000, 4 px, upload each frame | 66 to 288 | 0.37 to 0.61 | CPU 0.1 ms | in 2 of 5 runs |
| dots x 10000 | 87 to 243 | 0.45 to 0.59 | CPU 0.3 ms, GPU 0.5 ms over the empty frame | yes |
| dots x 100000 | 654 to 1271 | 1.45 to 1.99 | CPU 2 ms, GPU 3 ms | yes |
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
| 10000 dots, uploaded each frame | 3567 | 3 | 1 | 268 / 643 | 215 |

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
would cut the open from 0.8 to 1.4 s to a few ms, but it changes start-up,
not timing, so it is left for later.

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

## Next

In this order, decided 2026-10-06:

1. **Program binary cache.** Every program adds 0.15 to 0.75 s to open
   time, and open is at its 2.0 s bar. The cache brings open time down and
   makes kind-specialized programs affordable. Those fix the missed v0.3
   GPU bars (RRECT 2.3x, dashes 3.3x, OKLAB 1.65x, compound of 8 at
   1.6 ms).
2. **Planar YUV and texture import.** NV12 and I420 converted in the IMAGE
   shader, plus import, rebind and per-plane update of textures. psy_video
   needs both: at 1080p, converting on the pump costs more than decoding,
   and the zero-copy Media Foundation path imports D3D11 textures.
3. **Instanced stimuli.** One template stimulus (any kind but USER with
   extension blocks) and a per-instance attribute buffer, drawn in one
   call. The attributes come from a fixed set: position, ori, phase,
   contrast, size scale, color and gate. Reason: a gabor array of N
   elements is N blocks of 256 bytes today, uploaded every frame and drawn
   16 per call. That is fine near 1000 elements and costly at 10,000
   (2.5 MB of uniforms, 625 draws). Instances need 16 to 32 bytes each.
   Open: how timeline bindings address one element or all of them, and a
   hit test per element (a target in a search array).

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
