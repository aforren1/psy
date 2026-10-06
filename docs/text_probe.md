# Text and blur probes, 2026-10-06

This note records two probes that decided the text plan in
[rig_spec.md](rig_spec.md) section 5.2 (Font row). The probe code was in
a session scratchpad and is lost; the numbers below are the record. When
Slug glyph runs and the blur pass go into `psy_gfx.h`, their tables move
to [psy_gfx.md](psy_gfx.md) and are measured again.

Conditions for every number: Windows 11 laptop, Intel Iris Xe, ANGLE
2.1.23876 (Docker Desktop's Electron copy) on D3D11, AC power, the
shared measurement lock held for timing, rows interleaved. Correctness
also ran on WARP, SwiftShader and Mesa llvmpipe. Only one GPU was timed.

## Decision

- Scalable text is drawn from glyph outlines by the Slug algorithm
  (Lengyel, JCGT 6(2), 2017; patent dedicated to the public domain
  2026-03-17; reference shaders "MIT OR Apache-2.0", credit required).
- MSDF atlases are not a pack format. The MSDF path in `psy_gfx.h` can
  stay.
- Value-exact text is an exact-size alpha atlas from an exact-area
  rasterizer in the pack tool, drawn on the pixel grid only.
- Outlines and faux bold: stroke expansion of the curves, filled with
  Slug coverage. Hard shadows: an offset copy. Soft shadows, glow and
  blurred text: a separable Gaussian blur pass on a render target.

## Why MSDF was dropped

- A distance soft edge is a true Gaussian blur only on straight edges
  (0.25 off at a corner, psy_gfx.h EXACTNESS). Blurred-letter stimuli
  need a real blur, which a distance field cannot give.
- MSDF failed on dense CJK at large sizes (holes and blobs) and on
  overlapping contours (holes at joints), and left corner wedges.
- A whole CJK font is 212 to 762 MB as MSDF atlases and about 65 MB as
  Slug data, so with Slug the pack holds whole fonts and the player
  rasterizes nothing at run time.

## Slug probe

Reference: exact box-filter coverage per pixel in double (adaptive
Gauss-Kronrod per row, nonzero rule), checked against Green's theorem to
5e-13. Glyph sets: Segoe UI (Latin), Segoe UI Light (thin stems),
Bahnschrift (overlapping contours), Arial (Arabic), Nirmala UI
(Devanagari), Microsoft YaHei (CJK, TrueType), Source Han Sans JP (CJK,
CFF; cubics converted to quadratics within 1e-4 em). MSDF A is 32
texels/em with pxrange 8; MSDF B is 64 texels/em with pxrange 16.

Coverage error on edge pixels, rotation 0, max / mean:

| px | Slug f32 | MSDF A, psy_gfx cosine 1 px | MSDF B, linear | Skribidi rasterizer | stb_truetype |
|---|---|---|---|---|---|
| 8 | 0.48 / 0.049 | 0.53 / 0.076 | 0.44 / 0.060 | 0.26 / 0.023 | 0.28 / 0.0085 |
| 12 | 0.53 / 0.037 | 0.46 / 0.062 | 0.41 / 0.041 | 0.27 / 0.026 | 0.34 / 0.012 |
| 24 | 0.56 / 0.018 | 0.57 / 0.047 | 0.43 / 0.018 | 0.24 / 0.024 | 0.36 / 0.013 |
| 48 | 0.36 / 0.010 | 1.0 / 0.046 | 0.43 / 0.010 | 0.23 / 0.022 | 0.39 / 0.013 |
| 200 | 0.29 / 0.007 | 1.0 / 0.082 | 1.0 / 0.009 | 0.32 / 0.019 | 0.42 / 0.014 |

- At 200 px, pixels off by more than 0.25: Slug 5, MSDF A 28,800, MSDF
  B 1,310.
- Slug's remaining error is at convex corners, by design: it averages a
  horizontal and a vertical ray.
- At 45 degrees Slug blurs (mean 0.055 against MSDF B's 0.024). A
  variant with screen-aligned rays was not tested.
- On the D3D11 hardware, Slug differs from the software renderers by up
  to 0.49 on 20 to 50 pixels per rotated condition, where both ray
  weights are near zero. A test tolerance or a shader fix is needed.
- An f16 curve texture is exact for Latin TrueType; Arabic, Devanagari
  and CFF glyphs need f32 or glyph-relative coordinates.

GPU time, ms per frame, 1920 x 1200 RGBA16F:

| workload | Slug f32 | MSDF A | MSDF B |
|---|---|---|---|
| 12 px Latin, 21,763 glyphs | 3.95 | 1.19 | 1.25 |
| 48 px Latin, 1,439 glyphs | 1.55 | 0.43 | 0.45 |
| 16 px CJK, 6,783 glyphs | 6.08 | 0.39 | 0.68 |
| 200 px CJK, 45 glyphs | 2.52 | 0.41 | 0.40 |

Slug compiles in 45 to 56 ms on ANGLE D3D11. Building Slug data costs
19 to 66 us per glyph (1.24 s for all 30,204 YaHei glyphs).

Exact distance from the same curves ("Slug-distance") is within 6e-5 px
on TrueType, but its cost grows with the effect's reach: 142 ms for a
full page of 12 px text with a Gaussian of SD 1. It also needs overlap
removal at build time. Not adopted.

Skribidi's rasterizer has errors of 0.24 to 0.32 of full scale at every
size, so value-exact atlases need an exact-area rasterizer (the probe's
took 0.3 to 6 ms per glyph, unoptimized). A native-size atlas drawn off
the pixel grid has a mean edge error of 0.085 to 0.109, so off-grid
drawing is refused.

## Blur probe

Reference: the exact shape convolved with a Gaussian, in double (closed
form in x with erfc, adaptive in y), checked against psy_gfx's
EXACT_BLUR erf product to 1.7e-15. Blur kernel: direct taps,
2 ceil(5 s') + 1 per pass, with s' = sqrt(s^2 - 1/12) to remove the box
filter's variance. Shader accumulation is f32 in every variant.

Max error against the exact blurred shape:

| sigma, px | 0.5 | 1 | 2 | 4 | 8 | 16 |
|---|---|---|---|---|---|---|
| exact source, f32 target | 0.20 | 0.040 | 0.0089 | 0.0017 | 6.1e-4 | 1.4e-4 |
| exact source, f16 target | 0.20 | 0.040 | 0.0082 | 0.0021 | 1.2e-3 | 8.7e-4 |
| Slug source, f16 target | 0.28 | 0.10 | 0.035 | 0.014 | 4.0e-3 | 1.3e-3 |
| 2x supersampled exact source, f32 | 0.023 | 0.010 | 0.0034 | 5.7e-4 | 1.4e-4 | 3.1e-5 |

- The floor is the 1x sampling of the source, not the blur: a
  box-filtered edge is not band-limited, and the error falls as
  1/sigma^2. 2x supersampling lowers it 2 to 8 times.
- 8-bit exactness (3.9e-3) is reached at sigma 4 and up at 1x, and at
  sigma 2 and up with a 2x supersampled source. 10-bit (9.8e-4) needs
  sigma 8 and up with f32 targets at 1x.
- Against psy_gfx's distance soft edge: the blur pass errs everywhere,
  straight edges included (a rect at sigma 1: 0.025), but less than the
  distance soft edge at corners (0.25).

GPU time, ms per frame, direct taps with weights computed in the shader:

| region | sigma 0.5 | 1 | 2 | 4 |
|---|---|---|---|---|
| full 1920 x 1200, R16F | 1.21 | 1.86 | 3.37 | 6.26 |
| word 400 x 200, R16F | 0.021 | 0.015 | 0.072 | 0.169 |

- A word-sized region meets a 0.2 ms bar up to sigma 4. A full-screen
  blur misses a 1 ms bar at every sigma.
- Above sigma 8, a downsampling pyramid costs under 1 ms full screen,
  with an error of about 0.012 at every sigma; opt-in only.
- Bilinear tap pairing was no faster and less consistent across
  renderers; deleted. Pixel-integrated weights gave nothing; deleted.
- The blur program compiles in 26 to 40 ms.

Stroke expansion (Tiller-Hanson offsets, round joins by a union of
round-capped segments, within 1e-4 em): 23 to 52 us per Latin glyph and
100 to 170 us per CJK glyph, plus 150 to 930 us to rebuild the Slug
bands. A stroke has about 6 times the curves of its glyph. Inner offsets
fold where the curvature radius is below the stroke width (137 wrong
pixel centers of 122k at 0.08 em); not fixed.

## Open

- Whether a full-screen blur at 1.2 to 3.4 ms (sigma 2 and below) is
  acceptable. There is no compute stage in GL ES 3.0.
- The cost of 2x supersampling and of f32 targets: not timed.
- An exact GPU blur by per-pixel integration of the curves against the
  Gaussian: not tested.
- The stroke fold fix.
