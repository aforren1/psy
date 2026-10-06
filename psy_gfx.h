/* psy_gfx.h - v0.3.0 - public domain single-header stimulus graphics library
 *   (with MIT-licensed parts: see below)
 *
 *   Stimuli on GL ES 3.0, on top of psy_screen.h: signed-distance shapes
 *   with a stated edge profile, gratings, gabors, dot fields, image
 *   textures and hash noise, drawn in linear light into a float scene,
 *   then one output stage (a lookup table per channel from a calibration,
 *   dithering, 8-bit quantization) into the screen's back buffer. Also the
 *   calibration math (cone fundamentals, cone contrast, DKL), a canonical
 *   calibration file, a user shader contract, timeline bindings and a
 *   parameter table. Vector shapes from closed-form distance functions
 *   (v0.3): rounded and partial shapes, paths of up to 224 points,
 *   compounds, effects, dashes, gradients, blend modes, MSDF atlases and
 *   glyph runs. No compute stage, no path rasterizer, no SVG: artwork goes
 *   through the pack tool.
 *
 *   REQUIRES psy_screen.h and psy_rt.h beside it, and what psy_screen.h
 *   needs (SDL3 to link; ANGLE's DLLs at run time on Windows). Nothing
 *   ANGLE or Khronos is needed to compile: GL is loaded at run time.
 *
 *   Written in the single-header style of the stb / sokol libraries. C99 is
 *   the floor: it builds as C99, C11 and C++17, and in the C dialect MSVC
 *   compiles by default.
 *
 *   MIT-LICENSED PARTS: the vector program's distance functions follow
 *   Inigo Quilez's (box, rounded box, uneven capsule, arc, pie, star,
 *   smooth minimum) and 0xfaded's ellipse. They are in psygfx__glsl_vec0,
 *   psygfx__glsl_vec1 and their CPU forms (psygfx__vp_*, psygfx__vsmin).
 *   Each such function names its source in a comment; the MIT notices are
 *   in the license block at the end of the file. The rest is public domain
 *   / MIT-0.
 *
 *   ---------------------------------------------------------------------
 *   CHANGELOG
 *   ---------------------------------------------------------------------
 *   v0.3.0 - the vector program (VECTOR SHAPES): SHAPE kinds RRECT, ARC,
 *          PIE, CAPSULE, NGON, STAR, ELLIPSE, QBEZIER, POLYLINE and
 *          COMPOUND (psygfx_compound(), up to 56 primitives folded by
 *          UNION, INTERSECT, SUBTRACT, XOR and smooth variants, onion);
 *          POLYGON fillets; offset (dilation) on every SHAPE; dashes,
 *          dash_offset, dash_snap, trim and caps; effects in one pass
 *          (drop shadow, glow, inner shadow, two bands, an exact blur for a
 *          sharp RECT's shadow); paint (LINEAR, RADIAL, ANGULAR, VERTEX;
 *          mixed in RGB, OKLAB or DKL_POLAR); blend modes OVER, ADD and
 *          MULTIPLY on color stimuli; psygfx_length(); the parameter tables
 *          psygfx_prim_params(), psygfx_fx_params(), psygfx_paint_params()
 *          and psygfx_bind.field. DISTANCE TEXTURES: MSDF and MTSDF masks
 *          with sdf_range and the range rule; a mask's src rectangle;
 *          psygfx_glyphs(), an instanced glyph run. The edge model's limits
 *          are stated and measured (EDGES, EXACTNESS). Backend version 2:
 *          deferred pipelines and pipeline_finish (ANGLE compiles the
 *          programs in parallel), bindings.blend, PSYGFX_BLEND_MULTIPLY,
 *          two vec4 instance attributes. Breaking: a backend must set
 *          version 2 and may leave pipeline_finish NULL; max_draws counts
 *          uniform blocks (a vector stimulus with data takes more than
 *          one); a USER stimulus with a MASK_TEX aperture loses p[28..31]
 *          to the mask's rectangle.
 *   v0.2.0 - strokes on every SDF shape and aperture (CENTER, INSIDE or
 *          OUTSIDE, the same edge profile on both sides of the band; ROUND
 *          or MITER joins, MITER on RECT, CROSS and convex POLYGON with a
 *          miter limit and a bevel beyond it); PSYGFX_MASK_TEX, a shape from
 *          a signed-distance texture, and psygfx_sdf_from_mask(); sprites:
 *          an IMAGE's source rectangle, nearest or linear, with no bleed;
 *          tint; flat groups (psygfx_group: place, anchor, box, x, y, ori,
 *          scale, opacity) with bindings; render targets
 *          (psygfx_target(), psygfx_begin_target(), psygfx_end_target(),
 *          psygfx_read_target()) and PSYGFX_STIM_ADD images; the PSYGFX_ADD
 *          shader mode; the GL state cache follows psyscr_gl_epoch().
 *          Breaking: psy_look.w in the shader contract is the stroke band's
 *          center offset, not visible; psygfx_stim and psygfx_bind grew
 *          (zero a psygfx_bind you fill field by field; an initializer list
 *          gets a NULL group for gcc -Wextra); the OVER blend now
 *          accumulates alpha (the scene's output is unchanged); IMAGE's
 *          linear sampling is by hand from four texelFetch, clamped to the
 *          source rectangle, not the sampler's.
 *   v0.1.0 - first release.
 *
 *   STATUS: v0.3.0. Built and run on one Windows 11 laptop (Intel Iris Xe,
 *   a 1920 x 1200 panel at 60 Hz) with the ANGLE in Docker Desktop's
 *   Electron folder (2.1.23876, git fffbc739779a) and with CI's, from
 *   Electron v38.8.6 (2.1.25848, git 85cc0e0b9cc3): the same numbers. No
 *   photometer, spectroradiometer, photodiode or Bits# has been used: no
 *   number here is a measurement of light.
 *   Pixels (tests/adapt/psy_gfx_test.c, offscreen, every stimulus read back
 *   from a float32 scene against a CPU reference in double at pixel
 *   centers, positions through the inverse of the one coordinate transform)
 *   on four GL ES 3.0 renderers: ANGLE on D3D11 (Iris Xe), on D3D11 WARP,
 *   on Vulkan SwiftShader, and Mesa 23.2 llvmpipe (WSL2). Largest errors
 *   (Iris Xe, WARP, llvmpipe / SwiftShader): soft edges 1.7e-5 / 9.5e-5,
 *   grating 1.7e-5 / 1.4e-4, gabor 1.2e-6 / 8.8e-5, dots 1.5e-5 / 9.5e-5,
 *   strokes 1.6e-5 / 9.5e-5, MASK_TEX 1.4e-5 / 9.5e-5, linear sprites
 *   8.4e-7, tint 6.3e-8, group members against the same stimuli placed
 *   alone 6.0e-8 of full scale; images, noise, hard edges and hard bands,
 *   MITER and bevels against their CPU form, sprite borders (no bleed),
 *   shapes through a target against direct drawing, the output stage's
 *   codes (all 256, a CLUT, rounding at 0.005 above each halfway point, a
 *   CLUT through a target), the 8 x 8 ordered dither, the scene with v0.2's
 *   OVER alpha against v0.1's, the anchor's invariance and batched against
 *   single draws: exact. psygfx_hit() agrees with the GPU's coverage >= 0.5
 *   on every pixel not within 1e-3 px of an edge, strokes and groups
 *   included. psygfx_sdf_from_mask() equals a brute-force search. A present
 *   callback that changes GL state leaves the next frame exact (without
 *   the epoch, 901 values differ). docs/psy_gfx.md has the tables and the
 *   tolerances. 14 deliberate faults in v0.1 and 18 in v0.2 were each
 *   caught.
 *   Calibration math against published values: sRGB primaries to XYZ
 *   within 3.9e-5 of IEC 61966-2-1's printed matrix; the CLUT of a display
 *   with sRGB's transfer function within 1.3e-3 (17 levels) and 3.6e-5 (64
 *   levels) of sRGB's encoding; the embedded cone fundamentals equal CVRL's
 *   linss2_10e_1; RGB to LMS, the DKL matrix and its unit axes within 7e-8
 *   (relative) of Psychtoolbox's ComputeDKL_M run in MATLAB R2023a on its
 *   own B_monitor spectra.
 *   Cost on the Iris Xe at 1920 x 1200 (examples/gfx_bench.c, GPU time from
 *   frames between two glFinish calls, AC and battery runs): an empty frame
 *   (scene clear and the output stage) 0.42 to 1.34 ms of GPU; 1000 gabors
 *   of 256 x 256 about 9.3 to 11 ms of GPU and 0.2 to 0.5 ms of CPU
 *   (batched; 2.1 to 5.0 ms one draw each); 100000 dots uploaded each frame
 *   1.5 to 2.0 ms of GPU and 0.65 to 1.3 ms of CPU; a full-frame RGBA8
 *   upload 1.1 to 2.6 ms of CPU. end()'s own CPU time for an empty frame is
 *   56 to 260 us mean, which misses the 10 us planned; the time is in
 *   ANGLE's calls, not broken down further. psygfx_open() 1.96 to 2.14 s
 *   in v0.3 (three runs; eleven programs, the vector program always
 *   included; ANGLE compiles them in parallel when every link starts
 *   before any status is read: a probe of four programs went from 7.3 s
 *   to 3.0 s. v0.2's nine took 1.3 to 1.4 s).
 *   v0.2 (AC, two runs): a stroke costs 1.05 to 1.17 times its fill's GPU
 *   time; a full-screen target drawn as an image 0.43 ms of GPU, so 1000
 *   gabors rendered once and added each frame cost 0.43 ms against 9.1 to
 *   9.2 ms drawn each frame; a target pass 4.7 us of CPU; 1000 sprites
 *   from one atlas 0.49 ms of GPU and 155 us of CPU.
 *   v0.3 pixels (the same method; docs/psy_gfx.md has the tables, Iris Xe
 *   / WARP / SwiftShader / llvmpipe): the new kinds, filled and stroked,
 *   soft, against their CPU form 1.7e-5 / 8.2e-6 / 9.6e-5 / 8.2e-6; with
 *   dashes or trim 4.0e-4 / 4.0e-4 / 2.5e-3 / 5.8e-5 (the arc length's
 *   float error at a dash end); the exact kinds against F(brute-force
 *   Euclidean distance) 4.3e-6 / 4.9e-6 / 4.9e-6 / 4.9e-6; QBEZIER as
 *   chords 0.0050 px from the curve; effects in one pass against the same
 *   layers drawn apart 1.9e-5 / 5.7e-6 / 1.2e-4 / 5.7e-6; EXACT_BLUR
 *   against the erf product 1.5e-7 to 1.7e-7; paint RGB 1.2e-6, OKLAB
 *   against Ottosson's matrices through the calibration 1.8e-6, DKL_POLAR
 *   against psygfx_cal_dir_dkl() 1.5e-5 / 6.3e-6 / 1.6e-4 / 1.3e-6,
 *   VERTEX against barycentric 1.1e-7; MSDF against the CPU's bilinear
 *   median 3.5e-6; glyph runs, batching and MULTIPLY: exact or 4.8e-8. The
 *   edge model against a supersampled Gaussian blur: 0.2500 at a right
 *   angle, 0.0406 and 0.0100 on discs of 5 and 20 SD, a union's crease
 *   0.0822 (0.0830 predicted), a MITER corner 0.0800, on all four. 26
 *   deliberate faults in v0.3: 24 caught on llvmpipe, one (the star's
 *   reflection) only on D3D11, one equivalent (EXACTNESS).
 *   v0.3 cost (Iris Xe, AC, GPU over an empty frame, full screen): v0.2
 *   rounded rect 0.40 ms, RRECT 0.93 ms (2.3x, the bar was 1.3x), with RGB
 *   paint 1.38 ms, OKLAB 1.54 ms (1.65x solid, bar 1.1x), STAR 0.86 ms,
 *   ELLIPSE 1.32 ms; an 8 px stroke 0.27 ms, dashed 0.90 ms; a compound of
 *   1, 2, 8 and 32 circles 0.78, 1.22, 3.53 and 12.4 ms (0.163 ms per
 *   primitive per Mpx, bar 0.15); a compound of 8 in a 600 x 400 box 0.62
 *   ms, with every effect 1.62 ms (bar 1.0); 100 compounds of 4, 120 us of
 *   CPU (bar 600); a run of 200 MSDF glyphs 0.12 ms of GPU and 18 us of CPU
 *   (bars 0.2 ms, 20 us).
 *   In an 800 x 600 window on the composition swapchain, 1 minute each
 *   (v0.1): DRAW p99 0.48 to 0.79 ms with up to 100 gabors or 10000 dots, 0
 *   to 2 late targets per minute. No C runtime heap call in 566 frames of
 *   an MSVC debug build (v0.1; a control with one malloc per frame counted
 *   569).
 *   Builds, warnings as errors: MSVC 19.44 (CMake) and 19.51 (C11 and
 *   C++17), MinGW-w64 gcc 16.1 (C99, C11, C++17), gcc 11.4 on WSL2 (C99,
 *   C11, C++17, ASan and UBSan), emcc 6.0.10 (C11 run in node; C++17
 *   compiled); v0.3 again on each. Under ASan the
 *   test leaves GL out unless PSYGFX_TEST_DEVICES names it: Mesa leaks 112
 *   bytes from a module it unloads.
 *   NOT done: 10-bit, Mono++ and Color++ output (refused: psy_screen.h makes
 *   RGBA8 back buffers, this panel's link is 8 bits per color, and no
 *   device was there to verify); stereo (refused); text layout (a glyph
 *   run draws placed glyphs; there is no atlas tool yet); cubic Beziers;
 *   filtered noise on the GPU; a program binary cache; fullscreen runs;
 *   any other GPU, macOS, X11 or Wayland (psy_screen.h's stubs), WebGL.
 *   Outside this block and docs/psy_gfx.md, a number in this header is a
 *   measurement only where the text says "measured".
 *
 *   ---------------------------------------------------------------------
 *   USAGE
 *   ---------------------------------------------------------------------
 *   Do this:
 *
 *       #define PSY_GFX_IMPLEMENTATION
 *
 *   in *one* C or C++ file before including this header. psy_screen.h's and
 *   psy_rt.h's implementations come with it, once.
 *
 *   A gabor in the middle of a mid-gray window (examples/gfx_hello.c):
 *
 *       #define PSY_GFX_IMPLEMENTATION
 *       #include "psy_gfx.h"
 *
 *       static psyscr_screen scr;
 *       static psygfx_gfx gfx;
 *
 *       int main(void) {
 *           if (!psyscr_open(&scr, &(psyscr_desc){ .windowed = true })) return 1;
 *           if (!psygfx_open(&gfx, &(psygfx_desc){ .screen = &scr,
 *                                   .background = { 0.5f, 0.5f, 0.5f } })) return 1;
 *           psygfx_stim g = psygfx_gabor(&(psygfx_gabor_desc){
 *               .sf = 1 / 32.0f, .sigma = 32, .contrast = 0.5f });
 *           psyscr_frame f;
 *           while (psyscr_begin(&scr, &f) == PSYSCR_OK) {   // Esc ends it
 *               psygfx_begin(&gfx, &f);
 *               psygfx_draw(&gfx, &g);
 *               psygfx_end(&gfx);
 *               psyscr_flip(&scr);
 *           }
 *           psygfx_close(&gfx);
 *           psyscr_close(&scr);
 *       }
 *
 *   PsychoPy and Psychtoolbox, for comparison:
 *
 *       win = visual.Window(units='pix', color=0)          # PsychoPy
 *       g = visual.GratingStim(win, tex='sin', mask='gauss', sf=1/32,
 *                              size=256, contrast=0.5)
 *       while not event.getKeys(['escape']):
 *           g.draw(); win.flip()
 *
 *       PsychImaging('PrepareConfiguration');               % Psychtoolbox
 *       PsychImaging('AddTask', 'General', 'FloatingPoint32BitIfPossible');
 *       win = PsychImaging('OpenWindow', 0, 0.5);
 *       tex = CreateProceduralGabor(win, 256, 256, 0, [0.5 0.5 0.5 0]);
 *       while ~KbCheck
 *           Screen('DrawTexture', win, tex, [], [], 0, [], [], [], [], ...
 *                  kPsychDontDoRotation, [180, 1/32, 32, 50, 1, 0, 0, 0]);
 *           Screen('Flip', win);
 *       end
 *
 *   The differences: PsychoPy's 'gauss' mask has SD = size / 6 (42.7 px
 *   here); here sigma is stated. PTB scales its procedural gabor's contrast
 *   by its normalization options. Coordinates: here the origin is the
 *   top-left corner, y down, and ori turns clockwise on the screen;
 *   PsychoPy's origin is the center with y up, and its ori is clockwise
 *   from vertical; PTB's origin is top-left, y down, angles clockwise. Both
 *   draw for "the next flip"; here a frame is drawn for f.onset, the
 *   predicted onset of its own flip.
 *
 *   A trial with timeline bindings (examples/gfx_trial.c runs it):
 *
 *       static psygfx_stim grating;
 *       static const psygfx_bind binds[] = {
 *           { &grating, PSYGFX_P_VISIBLE,  GRATING_ON, NULL },  // onset, offset
 *           { &grating, PSYGFX_P_CONTRAST, CONTRAST,   NULL },  // a tween or keys
 *       };
 *       ...
 *       psyscr_begin(&scr, &f);
 *       psytl_evaluate(&tl, &(psytl_frame){ f.onset, f.period, f.index }, fired, 8);
 *       psygfx_apply(binds, 2, psytl_values(&tl));
 *       psyscr_mark(&scr, PSYSCR_PHASE_EVALUATE);
 *       psygfx_texture_update(&gfx, noise, 0, 0, w, h, next_noise, 0);  // if any
 *       psyscr_mark(&scr, PSYSCR_PHASE_UPLOAD);
 *       psygfx_begin(&gfx, &f);
 *       psygfx_draw(&gfx, &grating);
 *       psygfx_end(&gfx);
 *       psyscr_flip(&scr);                    // unmarked time goes to DRAW
 *
 *   A drifting carrier takes its phase from the predicted onset, computed
 *   in double on the CPU, so no shader needs absolute time:
 *
 *       g.phase = (float)fmod(tf_hz * (f.onset - t0) * 1e-9, 1.0);
 *
 *   ---------------------------------------------------------------------
 *   MODEL
 *   ---------------------------------------------------------------------
 *   TWO STAGES
 *     Stimuli draw into a float scene target (RGBA16F: 11 bits of mantissa,
 *     a quarter of an 8-bit code at worst; an RGBA32F scene measured 1.4 to
 *     1.7 times the GPU time on blend-heavy frames) in linear device RGB:
 *     per gun, 0..1, proportional to that gun's light after linearization.
 *     The output stage then reads the scene once per pixel, looks each
 *     channel up in the CLUT, dithers if asked, quantizes to 8 bits and
 *     writes framebuffer 0, the screen's back buffer. Gamma lives
 *     only there: SDL3 has no gamma ramps.
 *   TWO BLENDS
 *     A modulation stimulus (GRATING, GABOR, NOISE, an IMAGE with
 *     modulation, a MODULATION user shader) adds
 *         contrast * gate * g(p) * aperture(p) * dir
 *     to the scene, g in [-1, 1]. Overlapping gabors add in linear light.
 *     dir all 0 means the background, so contrast is Michelson luminance
 *     contrast around it: L = Lbg (1 + contrast g). The calibration gives
 *     dir for a cone-contrast or DKL direction. A color stimulus (SHAPE,
 *     DOTS, a color IMAGE, a COLOR user shader) is blended over the scene
 *     with coverage * opacity * gate.
 *   DRAW ORDER
 *     Call order. No depth buffer, no sorting. visible < 0.5 skips a draw.
 *   NOTHING READS A CLOCK
 *     What a frame shows is a function of the psyscr_frame given to begin():
 *     f.onset, f.index, f.vblank. No call in the frame waits for the GPU
 *     (no glFinish, no glGet, no readback), so drawing cannot move a flip;
 *     a GPU that cannot keep up shows as SWAP time and drops in the flip
 *     record. The reads (psygfx_read_scene, _output) block, for tests.
 *   COST
 *     draw() packs the stimulus into a 256-byte uniform block and queues
 *     it. end() uploads the frame's blocks in one call, then draws each run
 *     of queued stimuli that share a program and textures as one instanced
 *     draw, up to 16 per draw, in order (measured: 1000 gabors cost 0.2 to
 *     0.5 ms of CPU this way, 2.1 to 5.0 ms one draw each; a longer batch
 *     makes ANGLE's compile slower: 64 took 2.2 s to open, 256 took 19 s).
 *     A vector stimulus whose data fills more than its block (VECTOR
 *     SHAPES) takes the blocks after it and draws alone.
 *
 *   ---------------------------------------------------------------------
 *   COORDINATES
 *   ---------------------------------------------------------------------
 *   Screen pixels: the origin is the top-left corner of the screen (or of
 *   the headless target), x right, y down, pixel centers at + 0.5. Units
 *   are pixels, or degrees (desc.units = PSYGFX_DEG): one scale factor,
 *   the pixels per degree at the screen center from desc.view, with the
 *   same origin and axes.
 *   ori is in degrees and turns +x toward +y: clockwise on the screen. ori
 *   0 puts a carrier along x (vertical bars).
 *   Two fields place a stimulus:
 *     place     the screen point x, y are measured from: one of nine
 *               (PSYGFX_CENTER, the default, PSYGFX_TOP_LEFT, ... ,
 *               PSYGFX_BOTTOM_RIGHT). x, y are along the screen axes, so +y
 *               is down from it. .x = 100 is 100 px right of the center;
 *               .place = PSYGFX_TOP_LEFT, .x = 10, .y = 10 is 10 px in from
 *               that corner. Not bindable.
 *     anchor    the point of the stimulus's own box that sits at x, y, and
 *               the point it turns about: ax, ay as fractions of the box,
 *               0, 0 its top-left corner and 1, 1 its bottom-right (y
 *               down). The same idea as Phaser's setOrigin and Pixi's
 *               sprite.anchor; the default here is the center, 0.5, 0.5, as
 *               in Phaser (Pixi's default is the top-left). A desc takes it
 *               as one of the nine names; the stimulus holds ax, ay as
 *               bindable floats.
 *   The box is w x h for shapes, gratings, noise, images and user shaders,
 *   the 8-sigma quad for a gabor unless w, h are set, and the field's
 *   aperture box for DOTS (the anchor moves the field, not each dot).
 *   Shapes, apertures and carriers are defined about the box's own
 *   center, so moving the anchor moves and turns a stimulus but does not
 *   change its pattern.
 *   psygfx_resolve() gives the anchor's screen position, psygfx_bounds()
 *   the turned box's axis-aligned bounds, psygfx_hit() whether a screen
 *   pixel is inside (where the GPU's coverage is 0.5 or more, by the same
 *   SDF on the CPU), psygfx_center() and psygfx_size() the screen's.
 *   SDL's mouse events are in window coordinates. At a pixel density of 1
 *   they are these pixels; at another density, multiply them by
 *   SDL_GetWindowPixelDensity() first (STATUS gives what this machine
 *   reported).
 *   The convention lives in one function of the implementation,
 *   psygfx__place() and its helpers; the shaders take what it computes.
 *
 *   ---------------------------------------------------------------------
 *   GROUPS
 *   ---------------------------------------------------------------------
 *   A psygfx_group is a plain value, like a stimulus: place, anchor (ax, ay
 *   of its box), box w x h (units; 0 x 0 is a point), x, y, ori, scale,
 *   opacity and visible. A stimulus joins one with .group = &group (NULL is
 *   none). For a member:
 *     its place is a point of the group's box, not of the screen, and its
 *       x, y and ori are in the group's frame;
 *     the group's frame is its anchor at its own place point plus (x, y),
 *       turned by its ori and scaled by its scale;
 *     scale applies to everything in units (positions, sizes, sigma, shape
 *       parameters, dot positions; sf is divided by it), never to what is
 *       in px (edge_width, stroke, dot_size, check): a px value sets the
 *       stimulus's spatial-frequency content at the panel. Pixi's
 *       container.scale zooms everything; this one does not;
 *     opacity multiplies each member's gate (a color member's alpha and a
 *       modulation member's contrast). It is not a flattened layer: two
 *       overlapping members at 0.5 do not look like one layer at 0.5. A
 *       flattened layer is a target (TARGETS) drawn with opacity.
 *   One level: a group has no group. A tree would need a walk per draw and a
 *   rule for cycles, and a bound channel on a group would then move the
 *   screen by an amount that depends on its ancestors. Deeper composition
 *   goes through a target. A psygfx_bind with .group set binds a group
 *   field (PSYGFX_G_*), so one channel moves every member on the next draw.
 *   psygfx_resolve(), _bounds(), _local() and _hit() go through the group.
 *
 *   ---------------------------------------------------------------------
 *   TARGETS
 *   ---------------------------------------------------------------------
 *   psygfx_target() makes a render target at setup (RGBA16F, or RGBA8),
 *   cleared to transparent black. Between psygfx_begin() and psygfx_end(),
 *   psygfx_begin_target(t, clear) and psygfx_end_target() draw into it with
 *   the normal calls (clear NULL keeps what it holds, so "draw once, sample
 *   every frame" works); then psygfx_image() draws it like any texture. The
 *   rules:
 *     coordinates in a target are its own: origin its top-left corner, y
 *       down, place points of its box, the same px per unit;
 *     its rows are top first, as an uploaded image's, so it is sampled and
 *       read (psygfx_read_target) with no flip;
 *     it holds scene-space linear values: no CLUT, dither or quantization
 *       runs in it; the CLUT applies once, in the output stage. RGBA8 keeps
 *       linear values in 1/255 steps, coarse in the dark: use it for masks
 *       and flat colors; RGBA16F holds negative increments too;
 *     color is premultiplied: draws accumulate alpha, and psygfx_image() on
 *       a target sets PSYGFX_STIM_PREMULTIPLIED;
 *     a target filled with modulation stimuli is a layer of increments:
 *       draw it with .add (PSYGFX_STIM_ADD), which adds texel.rgb scaled by
 *       contrast, gate, tint and aperture;
 *     a draw that samples the target being drawn is refused
 *       (PSYGFX_ERR_ORDER): GL calls that a feedback loop. A target sampled
 *       before the pass that writes it in the same frame shows the previous
 *       frame: call order decides;
 *     at most PSYGFX_MAX_PASSES (16) target passes per frame, not nested;
 *       nothing is allocated for them in the frame loop.
 *
 *   ---------------------------------------------------------------------
 *   STIMULI
 *   ---------------------------------------------------------------------
 *   One plain struct, psygfx_stim, for every kind; set fields directly.
 *   Constructors take a desc whose zero fields mean defaults, and return a
 *   stimulus with real values in every field (visible, gate, contrast,
 *   opacity, aspect 1; anchor 0.5, 0.5). After construction a 0 is a 0.
 *   SHAPE    an SDF shape in a solid color: RECT (corner radius shape_p[1]),
 *            CIRCLE (diameter w), ANNULUS (inner radius shape_p[0]), LINE
 *            (length w, width shape_p[0], round caps), POLYGON (shape_p[0]
 *            vertices, 3 to 16, x, y pairs in p[] from the box center), CROSS
 *            (arm width shape_p[0]). The v0.3 kinds, compounds, offsets,
 *            dashes, effects, paint and blend modes: VECTOR SHAPES.
 *   GRATING  sine, or square (wave 1), cos(2 pi (sf x + phase)) along the
 *            box's x from its center, in the shape as an aperture.
 *   GABOR    cos(2 pi (sf x + phase)) exp(-(x^2 + (aspect y)^2) / 2 sigma^2);
 *            the quad is 8 sigma unless w, h are set: the envelope is
 *            3.4e-4 at the cut, 0.04 of an 8-bit code at contrast 1.
 *   DOTS     count dots, each an SDF circle of dot_size px with the edge
 *            profile, at x, y float pairs (units, from the field's center)
 *            in a psygfx_buffer; drawn when the dot's center is in the
 *            field's aperture (RECT, CIRCLE or none). One instanced draw.
 *   IMAGE    a texture, or its source rectangle src (texels from the
 *            top-left; a sprite from an atlas), 1:1 by texelFetch unless
 *            linear; color, the red channel as g (modulation), or rgb as an
 *            increment (add). Nearest reads one texel by its integer
 *            address, clamped to the rectangle; linear interpolates by hand
 *            from four texelFetch whose addresses are clamped to the
 *            rectangle: neither can read a neighbor, so an atlas needs no
 *            padding (the outer half texel repeats the edge texel, as
 *            clamping to the edge does). No mipmaps: draw sprites at 1:1 or
 *            larger. tint multiplies linear light: color rgb by tint.rgb,
 *            coverage by tint.a; for a modulation, dir by tint.rgb and the
 *            amplitude by tint.a (the same as dir and contrast); for add,
 *            the increment by tint.rgb and tint.a. 16-bit planes are R16UI,
 *            read as k / 65535. Values are linear: an 8-bit sRGB photograph
 *            is the pack tool's to linearize.
 *   NOISE    per check of `check` px from the box's top-left corner:
 *            uniform in (-1, 1) or binary +-1 from an integer hash of the
 *            check and seed, bit for bit what psygfx_noise_value() gives.
 *   USER     a pipeline from psygfx_pipeline() (SHADER CONTRACT).
 *   TEXT     no layout here: a glyph run (GLYPH RUNS) draws glyphs that
 *            the caller placed; the atlas comes from the pack tool.
 *   MASK_TEX (any kind's shape) the signed-distance texture `mask` (R16F
 *            or R32F, distance in texels, negative inside, with padding),
 *            fitted to the box: px per texel = box width / texture width.
 *            A box of another aspect ratio than the texture's is refused.
 *            Nothing outside the box is drawn: the padding holds what an
 *            outer stroke or a soft edge reaches. psygfx_sdf_from_mask()
 *            makes one from a mask; psygfx_hit() does not see masks. MSDF
 *            and MTSDF atlases and a mask's src rectangle: DISTANCE
 *            TEXTURES.
 *   EDGES (edge, edge_width in px), centered on the nominal boundary, so
 *   the coverage is 0.5 at the stated size:
 *     HARD      1 where the pixel center is inside, else 0. No smoothing.
 *     COSINE    raised cosine from 1 to 0 over edge_width.
 *     GAUSSIAN  the hard edge blurred by a Gaussian of SD edge_width:
 *               0.5 erfc(d / (edge_width sqrt 2)).
 *   The edge sets the stimulus's spatial-frequency content, so state it.
 *   What the profile is, exactly: coverage = F(d), F the profile and d the
 *   signed distance to the boundary. Two limits (measured in the test
 *   against references that share none of this arithmetic; docs/psy_gfx.md):
 *     F(d) is the Gaussian blur of the shape only on a straight edge. At a
 *       vertex of interior angle a the blur is a / 360 and F(0) is 0.5: 0.25
 *       of the step at a right angle, 0.333 at 60 degrees, above 1/255 of
 *       the step within 3.75 SD of a right-angled vertex. On a disc of
 *       radius R the blur is about 0.2 SD / R below F(d) at the boundary
 *       (0.041 at R = 5 SD, 0.010 at 20 SD). The same holds for COSINE.
 *     d is the Euclidean distance for every v0.2 shape, with one
 *       exception (the v0.3 kinds and operations: EXACTNESS): MITER's
 *       field (RECT, CROSS, POLYGON) is the largest distance to the
 *       edges' lines, Euclidean inside, but outside a corner a lower bound,
 *       so the outer edge is up to 0.083 of the step too high there (at 1.2
 *       SD from a right-angled corner along its bisector; above 1/255
 *       within 3.75 SD), whatever the stroke or edge width.
 *   A HARD edge needs only the sign of d: it is exact for every shape.
 *   STROKES (stroke px wide; stroke_align): the band between two offsets
 *   a1 < a2 of the boundary: CENTER -s/2..s/2, INSIDE -s..0, OUTSIDE 0..s.
 *   Its coverage is the fill's at a2 minus the fill's at a1: the hard band
 *   blurred by the same profile, so both sides get the same edge, and a
 *   band narrower than the edge never reaches 1 (as a blurred thin line
 *   does not). A stroke replaces the aperture of every kind: an outline, a
 *   grating or noise in a ring, ring dots. Joins: the SDF's own (ROUND)
 *   gives a round corner on the outside of a turn and a sharp one inside
 *   (a 4 px OUTSIDE stroke on a square has round outer corners of radius
 *   4 px). MITER keeps outer corners sharp on RECT (no corner radius), CROSS
 *   and a convex POLYGON, with a bevel where the miter would pass
 *   miter_limit times the stroke width (0 means 4, SVG's default). A right
 *   angle's miter is sqrt 2 times the width, so RECT and CROSS bevel only
 *   below a limit of 1.414.
 *   MITER on a curve, a rounded rect or a concave polygon is refused.
 *   psygfx_hit() tests the hard band.
 *
 *   ---------------------------------------------------------------------
 *   VECTOR SHAPES (v0.3)
 *   ---------------------------------------------------------------------
 *   One more built-in program, the vector program, draws a SHAPE from
 *   closed-form distance functions with their gradients and arc lengths.
 *   It is always compiled. A SHAPE goes to it when it has a v0.3 kind, a
 *   POLYGON with fillets, fx, a paint other than SOLID, dashes or trim;
 *   other SHAPEs stay on v0.2's program and draw as before. Sizes are in
 *   units unless the text says px.
 *     RRECT     w x h; shape_p = the corner radii top-left, top-right,
 *               bottom-right, bottom-left.
 *     ARC       a ring segment with round ends: outer diameter w;
 *               shape_p[0] sweep (degrees), [1] thickness, [2] start
 *               (degrees, +x toward +y).
 *     PIE       a disc sector: diameter w; [0] sweep, [2] start.
 *     CAPSULE   length w, end to end; [0], [1] the radii of the left and
 *               right ends (an uneven capsule fits its box).
 *     NGON      a regular polygon: circumdiameter w; [0] sides (3 to 64),
 *               [1] corner rounding radius.
 *     STAR      outer diameter w; [0] points (3 to 64), [1] the inner
 *               radius over the outer, [2] corner rounding radius.
 *     ELLIPSE   w x h, aspect up to 20. An iteration (0xfaded's): 3, 6 or 8
 *               steps for aspects up to 2, 4.5 and 20 (distance error under
 *               1e-5 px, measured at 2:1 and 9:1).
 *     QBEZIER   path = 3 points (x, y pairs from the box center); [0] the
 *               stroke width. Drawn as chords within 0.005 px of the curve
 *               (at most 223; a curve that needs more is refused).
 *     POLYLINE  path = 2 to 224 points; [0] the width; join ROUND, MITER
 *               (with miter_limit) or BEVEL; cap BUTT, ROUND or SQUARE.
 *     POLYGON   shape_p[1] > 0 rounds every corner with a fillet of that
 *               radius, convex or concave.
 *   offset (px, every SHAPE, v0.2 kinds and glyph runs included) moves the
 *   boundary out (+) or in (-) before the stroke and the edge: a dilated
 *   or eroded shape. The quad grows with it.
 *   The block: a stimulus packs into its 256-byte block and, when its data
 *   needs it, the blocks after it (up to 247 vec4 of data: polygon
 *   vertices, path points, primitives, effects, paint). Such a stimulus is
 *   one instanced draw of its own; a vector stimulus with one block batches
 *   with its neighbors. desc.max_draws counts blocks, not draws.
 *   psygfx_hit(), _bounds() and _local() use the same field on the CPU.
 *
 *   COMPOUNDS
 *   psygfx_compound() makes a SHAPE of kind COMPOUND from an array of
 *   psygfx_prim (48 bytes each, 1 to PSYGFX_MAX_PRIMS = 56). Each primitive
 *   has a kind (CIRCLE, ANNULUS, RECT, RRECT, LINE, CAPSULE, CROSS, ARC,
 *   PIE, NGON, STAR, ELLIPSE), a center, w, h, ori and p[4] as shape_p, in
 *   the compound's box frame (units from the box center). The fold runs
 *   left to right: primitive i meets everything before it by its op,
 *   UNION, INTERSECT, SUBTRACT (removes primitive i), XOR, or a SMOOTH_
 *   op with blend radius k (Quilez's quadratic smooth minimum). After a
 *   smooth op the field is divided by its gradient's length, so the edge
 *   keeps its stated width through the blend (EXACTNESS). onion > 0 makes
 *   a primitive a shell |d| - onion; desc.onion does it to the result. The
 *   box places and anchors the compound; a primitive outside the box is
 *   still drawn (the quad grows to it). Keep the array alive: the stimulus
 *   points at it. Bind a primitive's float with psygfx_bind.field and the
 *   offsets in psygfx_prim_params(). There is no CSG stack: a fold is
 *   linear. A compound has no length, so no dashes or trim.
 *
 *   EFFECTS
 *   stim.fx points at a psygfx_fx (px; NULL = none). From the same field,
 *   in one pass, back to front: drop (a shadow at dx, dy screen px, behind
 *   the shape), glow (the same, at no offset), the fill or stroke, inner
 *   (a shadow inside the fill, from dx, dy), then band[0] and band[1]
 *   (outlines between offsets a1 < a2 of the boundary, on top). A shadow
 *   is the silhouette moved out by spread and blurred by sigma (0 = a
 *   hard silhouette): coverage F at offset spread, with a Gaussian F of SD
 *   sigma. It is a true blur only on straight edges (EXACTNESS).
 *   PSYGFX_FX_EXACT_BLUR draws a sharp RECT's (or zero-radius RRECT's) drop
 *   shadow as the exact Gaussian blur of the rect, the product of two erf.
 *   An effect with opacity 0 is off. Bind its floats with psygfx_bind.field
 *   and psygfx_fx_params(). The quad grows by each effect's reach (5 SD).
 *
 *   DASHES AND TRIM
 *   Dashes and trim run along the arc length s of a closed boundary (a
 *   stroke is then required) or along the center line of a LINE, CAPSULE,
 *   ARC, QBEZIER or POLYLINE. s runs from a start point fixed per kind,
 *   +x toward +y (clockwise on the screen); psygfx_length() gives the
 *   total in px. dash[0] and dash[1] are the dash and gap in px;
 *   dash_offset (px) moves the pattern along s, so a bound channel marches
 *   it; dash_snap fits a whole number of periods to a closed boundary.
 *   trim[0], trim[1] keep the part from start to end, as fractions of the
 *   length (0, 0 or 0, 1 = all). cap shapes the ends of dashes, of a
 *   trimmed outline and of an open path: BUTT (flat, at the end), ROUND
 *   or SQUARE (half the band width past it). A dash's soft edge across s
 *   uses the same profile as the boundary. CROSS, masks and compounds have
 *   no length: dashes or trim on them are refused.
 *
 *   PAINT
 *   stim.paint points at a psygfx_paint (NULL or SOLID = color). LINEAR
 *   runs t from x0, y0 to x1, y1; RADIAL from the center x0, y0 to the
 *   radius x1; ANGULAR around x0, y0 from x1 degrees, clockwise; all in
 *   units of the box frame. 2 to 6 stops, t rising; repeat wraps t, else
 *   it is clamped. VERTEX gives each vertex of a convex POLYGON a color,
 *   mixed by Wachspress coordinates (barycentric on a triangle). The
 *   space is where colors mix: RGB (linear device RGB; XYZ, LMS and
 *   Cartesian DKL are linear maps of it and mix the same), OKLAB
 *   (Ottosson's Oklab from the calibration's XYZ, white at Y = 1; needs a
 *   calibration with chromaticities) or DKL_POLAR (elevation, azimuth and
 *   radius about the background; needs a calibration with spectra; the
 *   azimuth takes the shorter turn between stops). Stops convert on the
 *   CPU; the shader mixes in the space and converts back per pixel. A
 *   path between in-gamut stops can leave the gamut in a nonlinear space:
 *   the CPU samples 64 points between neighbors and counts it as clipped.
 *
 *   BLEND
 *   stim.blend sets how a color stimulus meets the scene, in linear light:
 *   OVER (the default, premultiplied: an occluder), ADD (rgb += color x
 *   coverage x opacity: superposed light; alpha unchanged) or MULTIPLY
 *   (rgb = dst (1 - a + a color), a = coverage x opacity: a filter).
 *   Modulation stimuli add already: blend on them, and on an ADD image, is
 *   refused. A stimulus with another blend does not batch with its
 *   neighbors. The gamut check is v0.2's: per draw, overlaps not counted.
 *
 *   DISTANCE TEXTURES
 *   texture_desc.sdf marks a texture as a distance atlas for MASK_TEX:
 *   DIST (R16F or R32F, distance in texels, as v0.2), MSDF (msdfgen's
 *   multi-channel field: the median of the bilinear r, g, b; 0.5 on the
 *   edge, above it inside) or MTSDF (MSDF in rgb and the true distance in
 *   alpha, which offsets and round strokes read). MSDF and MTSDF need RGBA8,
 *   RGBA16F or RGBA32F and sdf_range, the atlas's full distance range in
 *   texels (msdf-atlas-gen's pxrange). An MSDF's offsets are mitered: a
 *   stroke with ROUND joins on it is refused (use JOIN_MITER, or MTSDF).
 *   The range rule: the atlas holds distances to range / 2 texels. What
 *   the edge (5 SD of a Gaussian, half the width of a cosine), the outer
 *   or inner part of the stroke and the offset reach must lie within
 *   (range / 2 - 1.5) texels, 1.5 texels kept for the bilinear neighbors.
 *   A draw that breaks it is refused with PSYGFX_ERR_RANGE and a message
 *   that gives the range it needs. src (x, y, w, h, texels from the
 *   top-left) picks one rectangle of the atlas for a mask; addresses are
 *   clamped to it, so no neighbor bleeds. The box must have the
 *   rectangle's aspect ratio: px per texel = box width / rectangle width.
 *
 *   GLYPH RUNS
 *   psygfx_glyphs() draws count glyphs from one atlas in one instanced
 *   draw. The buffer holds psygfx_glyph records (32 bytes): x, y, the
 *   glyph box's top-left in units from the run's box top-left, and sx, sy,
 *   sw, sh, its atlas rectangle in texels, padding included. scale is the
 *   units per atlas texel for every glyph. Edge, stroke, offset, color,
 *   opacity and blend apply to each glyph; the run places, turns and
 *   anchors as one box. The range rule holds per run. A run equals the
 *   same glyphs drawn one by one (bit for bit, measured). Layout,
 *   shaping and kerning are the caller's; bounds and hits come from the
 *   layout's metrics, not from the atlas. Coverage is an area fraction
 *   blended in linear light, so text looks thinner than text blended in
 *   sRGB; nothing corrects it.
 *
 *   EXACTNESS
 *   Two questions. Distance: is d the Euclidean signed distance? Then the
 *   coverage is exactly F(d), the stated profile. Blur: is the coverage
 *   the hard shape blurred by the kernel? For any shape, only on straight
 *   edges. The rule:
 *     1. A HARD edge needs only the sign of d: it is exact for every shape
 *        and operation (ELLIPSE, chords and textures to their stated
 *        error).
 *     2. A soft edge is exactly F(Euclidean d) for one exact primitive,
 *        filled, stroked with ROUND joins, offset or onioned; for a union
 *        of parts that stay apart by more than the edge's reach; and for
 *        dashes on straight runs.
 *     3. A soft edge is the Gaussian blur of the shape only on straight
 *        edges, and for a sharp RECT's shadow with EXACT_BLUR.
 *     4. Smooth merges never have an exact soft edge; the deviation is
 *        largest where two parts connect.
 *   Measured (llvmpipe, Iris Xe, WARP and SwiftShader agree), as the
 *   largest |coverage - reference| in units of the edge's step:
 *     exact kinds (RRECT, ARC, PIE, CAPSULE, NGON, STAR, POLYGON
 *       fillets, ELLIPSE) against F(brute-force Euclidean d)    4.9e-6
 *     QBEZIER (chords) against the curve                0.0050 px; 1.3e-3
 *     a UNION's reflex 90 degree crease (inside, a bound)     0.082
 *     a MITER corner, outside (a bound; v0.2)                 0.080
 *     SMOOTH_UNION, normalized, settled neck                  0.015
 *     SMOOTH_UNION at the moment two parts connect            0.122
 *     F(d) against the true blur: a 90 degree vertex          0.250
 *       a disc of radius 5 SD, 20 SD                          0.041, 0.010
 *       a drop shadow (F of the moved field) at a corner      0.248
 *     EXACT_BLUR against the erf product                      1.7e-7
 *   INTERSECT and SUBTRACT are bounds outside near a convex crease, XOR at
 *   every crease, and POLYLINE MITER and BEVEL near each turn, each about
 *   0.083 at 90 degrees (not measured separately). A non-smooth fold of
 *   exact fields already has a unit gradient, so dividing by it changes
 *   nothing there.
 *
 *   ---------------------------------------------------------------------
 *   OUTPUT STAGE
 *   ---------------------------------------------------------------------
 *   CLUT     n entries per channel from linear to device value (2 to
 *            4096), interpolated linearly by hand from two texel fetches,
 *            so no filtering extension is involved. Identity (n = 2) without
 *            a calibration: then a scene value is a device value.
 *            psygfx_set_lut() replaces it between frames.
 *   DITHER   code = floor(v * 255 + t), t = 0.5 (NONE, rounding), a Bayer
 *            8 x 8 threshold (ORDERED), or a hash of pixel, frame index and
 *            seed (NOISE, a new pattern each frame). The pattern is the
 *            scene's pixel grid from its bottom-left corner. Its spectrum
 *            is part of the stimulus; the default is NONE.
 *   GAMUT    the scene is clamped to 0..1. Each draw is checked on the CPU
 *            (bg +- contrast * gate * |dir| for modulations, the color for
 *            color stimuli); a draw that may leave 0..1 is counted
 *            (psygfx_clipped) and a frame with one pushes a CLIPPED record.
 *            Overlaps are not counted.
 *   10 BIT, MONO++, COLOR++, STEREO
 *            declared and refused in v0.1 (STATUS).
 *
 *   ---------------------------------------------------------------------
 *   CALIBRATION
 *   ---------------------------------------------------------------------
 *   psygfx_cal is the canonical form, in memory and on disk (61 KB, fixed
 *   offsets, little-endian, CRC-32). Fill it:
 *       psygfx_cal_init(&c);
 *       psygfx_cal_add(&c, PSYGFX_GUN_BLACK, 0, Y, x, y);     // all guns 0
 *       psygfx_cal_add(&c, gun, level, Y, x, y);              // per gun, to 1
 *       psygfx_cal_add(&c, PSYGFX_GUN_WHITE, 1, Y, x, y);     // additivity
 *       psygfx_cal_set_spectra(&c, 380, 4, n, r, g, b, black); // optional
 *       psygfx_cal_derive(&c, err, sizeof err);
 *   derive() linearizes each gun, Y' = (Y - Yblack) / (Y(1) - Yblack), by a
 *   monotone cubic (Fritsch and Carlson) inverted into the CLUT; refuses
 *   readings whose luminance does not rise, naming the level; gives RGB
 *   to XYZ (CIE 1931) from the full-level chromaticities, and RGB to LMS
 *   from the spectra with the Stockman and Sharpe (2000) 2-degree cone
 *   fundamentals (CVRL linss2_10e_1, embedded), the spectra interpolated
 *   linearly onto 1 nm. Black is removed from the matrices and kept apart
 *   (black_xyz, black_lms). Luminance in cone space is 0.68990272 L +
 *   0.34832189 M (the CIE 2006 2-degree function). Without spectra there
 *   is no cone space: the cone and DKL calls refuse (no 1931-to-cone
 *   approximation is made for you).
 *   psygfx_cal_dir_cone() gives the dir for cone contrasts at a background;
 *   psygfx_cal_dkl_matrix() is Psychtoolbox's ComputeDKL_M (Brainard 1996)
 *   at a background, and psygfx_cal_dir_dkl() the dir for a DKL vector
 *   (luminance, L-M, S); psygfx_dkl_from_sph() takes elevation and azimuth.
 *   psygfx_max_contrast() is the largest contrast that stays in gamut.
 *   save() sets the CRC and copies the bytes; load() refuses another size,
 *   magic or version, a bad CRC, and a file whose stored matrices or CLUT
 *   differ from what its readings give: a calibration can always be
 *   rebuilt from its readings. psygfx_cal_nominal() makes one from stated
 *   primaries and a gamma (an EDID's, sRGB's), flagged NOMINAL: not a
 *   measurement.
 *
 *   ---------------------------------------------------------------------
 *   SHADER CONTRACT
 *   ---------------------------------------------------------------------
 *   A user shader is GLSL ES 3.00: one function and its helpers.
 *       float psy_main(vec2 p)   MODULATION: g(p) in [-1, 1]
 *       vec4  psy_main(vec2 p)   COLOR: linear rgb and coverage
 *       vec3  psy_main(vec2 p)   ADD: an rgb increment
 *   p is the fragment's point in the stimulus's own frame, pixels from its
 *   box center, along its axes (x along ori, y a quarter turn clockwise
 *   from it on the screen), computed from gl_FragCoord and the exact
 *   center: interpolated coordinates moved with the rasterizer's subpixel
 *   snapping, up to 2e-2 of full scale on SwiftShader (docs/psy_gfx.md).
 *   The wrapper applies the aperture and edge, contrast, gate, dir or
 *   opacity, and the blend. Available to the body:
 *       psy_param(k)    the stimulus's p[k], k 0..31
 *       psy_size        half box w, h (px), pixels per unit, quad margin
 *       psy_look        contrast, opacity, gate, the stroke band's center
 *                       offset px (psy_shape.w is its half width)
 *       psy_color, psy_dir, psy_xf, psy_edge, psy_shape, psy_misc
 *       psy_target      target w, h, 1/w, 1/h
 *       psy_time        onset s since open (float: 0.24 ms steps at 1 h),
 *                       period s, frame index, 0
 *       psy_seed        uvec4: frame index low, high, desc.seed, vblank
 *       psy_hash(uint)  the integer hash of NOISE
 *       psy_tex0..2, psy_utex0 (unsigned), psy_PI, psy_sdf, psy_sdf_at,
 *       psy_coverage, psy_aperture, psy_bilinear (four texelFetch,
 *       clamped to a rectangle)
 *   Identifiers with two underscores in a row are reserved in GLSL (ANGLE
 *   warns): the contract has none.
 *   The wrapper puts "#line 1" before the body, so the compiler's log
 *   names the body's lines. psygfx_shader_wrap() gives the fragment
 *   shader text without GL: the pack tool validates exactly that.
 *   A time-dependent pattern takes its phase in psy_param() from the CPU,
 *   not from psy_time. A slow shader is its writer's timing problem, and
 *   the flip record's phases are where it shows.
 *
 *   ---------------------------------------------------------------------
 *   BACKENDS
 *   ---------------------------------------------------------------------
 *   psygfx_backend (version 2) is a table of 18 calls (pipelines from
 *   shader text, textures, buffers, passes, apply, draw, present, read,
 *   reset, pipeline_finish): the gfx backend of the rig's extensions.
 *   open() starts every built-in program with pipeline_src.deferred set,
 *   then calls pipeline_finish on each: a driver that compiles in parallel
 *   (ANGLE does) then overlaps them. A backend without it sets
 *   pipeline_finish NULL and completes each pipeline_make. bindings.blend
 *   overrides a pipeline's blend for one draw (BLEND). The built-in one is GL ES 3.0 through
 *   psyscr_gl_proc() (or desc.gl_proc, headless); on a screen with no GL
 *   (SIM) the null backend runs everything and draws nothing. A backend
 *   gets and returns rows bottom-up, GL's order.
 *   On DXGI_FLIP and COMPOSITION, ANGLE's client-buffer pbuffer puts GL's
 *   row 0 at the top of the screen (measured); the output stage writes
 *   the rows in that order. desc.rows overrides it for a custom presenter.
 *   GL state is cached and set only where it changes. Another program's GL
 *   calls between frames make the cache wrong: call psygfx_reset_state()
 *   after them. A psy_screen.h present callback is followed for you: the
 *   cache is dropped whenever psyscr_gl_epoch() has moved (checked on entry
 *   to every call that issues GL). When psyscr_gl_generation() has moved,
 *   the context is new and every GL object is gone: that call returns
 *   PSYGFX_ERR_LOST and the handle closes without deleting anything (the
 *   old names could name the new context's objects). Call psygfx_close(),
 *   then psygfx_open() and make the resources again.
 *
 *   ---------------------------------------------------------------------
 *   THE RING
 *   ---------------------------------------------------------------------
 *   With desc.ring, source PSYRT_SRC_GFX:
 *     PSYGFX_EV_OPEN     at open: aux scene format; u.u32[0..6] CLUT n,
 *                        dither, output, calibration CRC, its flags, w, h
 *     PSYGFX_EV_LUT      at psygfx_set_lut(): u.u32[0] n, [1] CRC-32 of it
 *     PSYGFX_EV_CLIPPED  a frame with clipped draws only: t_ns its onset,
 *                        aux the count, u.i64[0] the frame index
 *
 *   ---------------------------------------------------------------------
 *   MEMORY AND THREADS
 *   ---------------------------------------------------------------------
 *   open() allocates once (the frame's uniform staging and queue,
 *   max_draws x 296 bytes plus 4 KB of slack for one stimulus's extension
 *   blocks and 4.25 KB of frame blocks, one per target pass, 304 KB by
 *   default, and the shader text while it compiles); nothing after it
 *   (measured in v0.1: no C runtime heap call in the frame loop).
 *   The handle is about 10.8 KB. A target pass takes no memory of its own;
 *   psygfx_target() makes its texture and framebuffer at setup. One thread
 *   calls everything, the thread that owns the screen's context. Textures,
 *   buffers and pipelines are made and updated between frames, never
 *   between begin() and end().
 *
 *   ---------------------------------------------------------------------
 *   BUILDING
 *   ---------------------------------------------------------------------
 *   As psy_screen.h: link SDL3, put ANGLE's libEGL.dll and libGLESv2.dll
 *   beside the program on Windows. With PSYSCR_NO_SDL it builds without SDL
 *   and runs headless on a GL ES 3.0 context you make (desc.gl_proc) or on
 *   a custom presenter (tests/adapt/psy_gfx_headless.h makes one on EGL:
 *   ANGLE's D3D11, WARP or SwiftShader on Windows, Mesa on Linux). Link
 *   libm on Linux. Define PSYGFX_API to change the linkage.
 *   Zones: psygfx.begin, psygfx.draw, psygfx.end, psygfx.output,
 *   psygfx.upload.
 *
 *   LICENSE: public domain / MIT-0, with the MIT-licensed parts named at
 *   the top; all notices are at the end of the file.
 */
#ifndef PSY_GFX_H_INCLUDED
#define PSY_GFX_H_INCLUDED

#define PSYGFX_VERSION_MAJOR 0
#define PSYGFX_VERSION_MINOR 3
#define PSYGFX_VERSION_PATCH 0
#define PSYGFX_VERSION_STRING "0.3.0"

#include "psy_screen.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PSYGFX_API
#define PSYGFX_API extern
#endif

/* --- codes -------------------------------------------------------------- */

#define PSYGFX_OK                    0
#define PSYGFX_ERR_ARG             (-1)
#define PSYGFX_ERR_CLOSED          (-2)  /* not open                          */
#define PSYGFX_ERR_ORDER           (-3)  /* begin, draw, end or an update out
                                          * of turn                          */
#define PSYGFX_ERR_FULL            (-4)  /* a pool or the frame's draws       */
#define PSYGFX_ERR_GL              (-5)  /* the backend failed                */
#define PSYGFX_ERR_NOT_IMPLEMENTED (-6)
#define PSYGFX_ERR_REFUSED         (-7)  /* a request done only on purpose    */
#define PSYGFX_ERR_FORMAT          (-8)  /* not the canonical form            */
#define PSYGFX_ERR_RANGE           (-9)  /* readings or values out of range   */
#define PSYGFX_ERR_LOST            (-10) /* the GL context is new: every GL
                                          * object is gone; the handle is now
                                          * closed (psygfx_close, then open) */

/* Ring record kinds under PSYRT_SRC_GFX. */
#define PSYGFX_EV_OPEN    1u
#define PSYGFX_EV_LUT     2u
#define PSYGFX_EV_CLIPPED 3u

/* --- resources ------------------------------------------------------------ */

typedef struct psygfx_tex  { uint32_t id; } psygfx_tex;    /* 0 = none          */
typedef struct psygfx_buf  { uint32_t id; } psygfx_buf;    /* 0 = none          */
typedef struct psygfx_pipe { uint32_t id; } psygfx_pipe;   /* 0 = none          */

typedef enum psygfx_format {
    PSYGFX_FORMAT_NONE = 0,
    PSYGFX_R8,       /* uint8_t planes, 0..255 read as 0..1                     */
    PSYGFX_RG8,
    PSYGFX_RGBA8,
    PSYGFX_R16F,     /* float input, stored as half                             */
    PSYGFX_RGBA16F,
    PSYGFX_R32F,     /* float                                                   */
    PSYGFX_RGBA32F,
    PSYGFX_R16UI,    /* uint16_t planes, read as k / 65535 (16-bit images)      */
    PSYGFX_FORMAT_COUNT
} psygfx_format;

/* A render target (TARGETS): drawn with the normal API between
 * psygfx_begin_target() and psygfx_end_target(), then drawn as an IMAGE. */
typedef struct psygfx_target_desc {
    int32_t       w, h;          /* required, pixels                             */
    psygfx_format format;        /* 0 = PSYGFX_RGBA16F; or PSYGFX_RGBA8          */
    bool          linear;        /* linear filtering when drawn scaled           */
} psygfx_target_desc;

/* What a texture used as a PSYGFX_MASK_TEX holds (DISTANCE TEXTURES). */
typedef enum psygfx_sdf_kind {
    PSYGFX_SDF_NONE = 0,         /* not a distance texture; as a mask, DIST      */
    PSYGFX_SDF_DIST,             /* R16F or R32F: distance in texels, negative
                                  * inside (psygfx_sdf_from_mask)                */
    PSYGFX_SDF_MSDF,             /* msdfgen multi-channel: the median of rgb,
                                  * 0.5 on the edge, above 0.5 inside           */
    PSYGFX_SDF_MTSDF             /* MSDF in rgb, the true distance in alpha      */
} psygfx_sdf_kind;

typedef struct psygfx_texture_desc {
    int32_t       w, h;          /* required                                     */
    psygfx_format format;        /* required                                     */
    bool          linear;        /* linear filtering; default nearest (1:1)      */
    const void*   data;          /* NULL = zeros; row 0 is the top row           */
    size_t        stride;        /* bytes per row; 0 = tight                     */
    /* v0.3 */
    psygfx_sdf_kind sdf;         /* MSDF, MTSDF: RGBA8, RGBA16F or RGBA32F       */
    float         sdf_range;     /* MSDF, MTSDF: the atlas's distance range, the
                                  * full width in texels (msdf-atlas-gen's
                                  * pxrange); required for them                 */
} psygfx_texture_desc;

/* --- open ------------------------------------------------------------------ */

typedef enum psygfx_units  { PSYGFX_PX = 0, PSYGFX_DEG = 1 } psygfx_units;
typedef enum psygfx_dither {
    PSYGFX_DITHER_NONE = 0,      /* round to nearest                             */
    PSYGFX_DITHER_ORDERED = 1,   /* Bayer 8 x 8, the same every frame            */
    PSYGFX_DITHER_NOISE = 2      /* white, from a hash of pixel, frame and seed  */
} psygfx_dither;
typedef enum psygfx_stereo {
    PSYGFX_MONO = 0, PSYGFX_SIDE_BY_SIDE, PSYGFX_ROW_INTERLEAVED, PSYGFX_FRAME_SEQUENTIAL
} psygfx_stereo;
typedef enum psygfx_output {
    PSYGFX_OUT_8 = 0,            /* 8 bits per channel                           */
    PSYGFX_OUT_10,               /* refused in v0.1                              */
    PSYGFX_OUT_MONO_PP,          /* Bits# Mono++, refused in v0.1                */
    PSYGFX_OUT_COLOR_PP          /* Bits# Color++, refused in v0.1               */
} psygfx_output;
/* The row order of framebuffer 0, which the output stage writes. Not the
 * coordinate convention: that is COORDINATES in the manual. */
typedef enum psygfx_rows {
    PSYGFX_ROWS_AUTO = 0,        /* from the screen's backend                    */
    PSYGFX_ROWS_BOTTOM_UP,       /* row 0 is the bottom (GL's own)               */
    PSYGFX_ROWS_TOP_DOWN         /* row 0 is the top (ANGLE's client buffers)    */
} psygfx_rows;

/* Nine points of a box: of the screen for `place`, of the stimulus's own box
 * for `anchor`. CENTER is the zero default for both. */
typedef enum psygfx_align {
    PSYGFX_CENTER = 0, PSYGFX_TOP_LEFT, PSYGFX_TOP, PSYGFX_TOP_RIGHT, PSYGFX_LEFT,
    PSYGFX_RIGHT, PSYGFX_BOTTOM_LEFT, PSYGFX_BOTTOM, PSYGFX_BOTTOM_RIGHT
} psygfx_align;

/* Viewing geometry for PSYGFX_DEG: one scale factor, the pixels per degree at
 * the screen center. */
typedef struct psygfx_view { float distance_mm, width_mm; } psygfx_view;

struct psygfx_cal;
struct psygfx_backend;

typedef struct psygfx_desc {
    psyscr_screen*  screen;         /* required, unless gl_proc is set          */
    psyscr_proc   (*gl_proc)(void* ctx, const char* name);  /* headless: a GL ES
                                     * 3.0 context current on this thread       */
    void*           gl_ctx;
    int32_t         width, height;  /* headless only: framebuffer 0's size      */
    float           background[3];  /* linear device RGB, 0..1; 0 = black       */
    psygfx_units    units;          /* 0 = PX                                   */
    psygfx_view     view;           /* DEG only                                 */
    const struct psygfx_cal* cal;   /* NULL = identity: scene value = device value */
    psygfx_dither   dither;         /* 0 = NONE                                 */
    uint32_t        seed;           /* NOISE dither                             */
    psygfx_output   output;         /* only OUT_8 opens in v0.1                 */
    psygfx_stereo   stereo;         /* only MONO opens in v0.1                  */
    psygfx_rows     rows;           /* 0 = AUTO                                 */
    int32_t         max_draws;      /* per frame; 0 = 1024                      */
    psyrt_ring*     ring;           /* PSYRT_SRC_GFX records; NULL = none       */
    const struct psygfx_backend* backend;   /* NULL = the built-in GL ES 3.0
                                     * backend, or the null backend on a screen
                                     * without GL                               */
    void*           backend_ctx;
} psygfx_desc;

/* --- stimuli -------------------------------------------------------------- */

typedef enum psygfx_kind {
    PSYGFX_KIND_NONE = 0,
    PSYGFX_SHAPE,        /* SDF shape, solid color                              */
    PSYGFX_GRATING,      /* sine or square carrier in an SDF aperture           */
    PSYGFX_GABOR,        /* cosine carrier x Gaussian envelope                  */
    PSYGFX_DOTS,         /* SDF dots at positions from a buffer                 */
    PSYGFX_IMAGE,        /* a texture, color or modulation                      */
    PSYGFX_NOISE,        /* hash noise per check, modulation                    */
    PSYGFX_TEXT,         /* not implemented in v0.1                             */
    PSYGFX_USER,         /* a pipeline from psygfx_pipeline()                   */
    PSYGFX_KIND_COUNT
} psygfx_kind;

typedef enum psygfx_shape_kind {
    PSYGFX_RECT = 0,     /* w x h; shape_p[1] = corner radius                   */
    PSYGFX_CIRCLE,       /* diameter w                                          */
    PSYGFX_ANNULUS,      /* outer diameter w; shape_p[0] = inner radius         */
    PSYGFX_LINE,         /* length w; shape_p[0] = width; round caps            */
    PSYGFX_POLYGON,      /* shape_p[0] = n (3..16); vertices x, y in p[]        */
    PSYGFX_CROSS,        /* arms w and h long; shape_p[0] = arm width           */
    PSYGFX_NO_APERTURE,  /* the quad itself (gabor default)                     */
    PSYGFX_MASK_TEX,     /* the signed-distance texture `mask`, fitted to the box */
    /* v0.3, SHAPE only (the vector program; SHAPES in the manual) */
    PSYGFX_RRECT,        /* w x h; shape_p = corner radii TL, TR, BR, BL        */
    PSYGFX_ARC,          /* outer diameter w; [0] sweep deg, [1] thickness,
                          * [2] start deg (clockwise from +x); round ends       */
    PSYGFX_PIE,          /* diameter w; [0] sweep deg, [2] start deg            */
    PSYGFX_CAPSULE,      /* length w; [0], [1] radii of the left, right end     */
    PSYGFX_NGON,         /* circumdiameter w; [0] sides n (3..64), [1] rounding */
    PSYGFX_STAR,         /* outer diameter w; [0] points n (3..64), [1] inner
                          * radius / outer, [2] rounding                         */
    PSYGFX_ELLIPSE,      /* w x h, aspect up to 20                               */
    PSYGFX_QBEZIER,      /* a quadratic Bezier, path = 3 points; [0] width       */
    PSYGFX_POLYLINE,     /* an open path of 2..224 points; [0] width; join, cap  */
    PSYGFX_COMPOUND      /* prims, folded by their ops (COMPOUNDS)               */
} psygfx_shape_kind;

/* A stroke: the band between two offsets of the boundary, `stroke` px wide. */
typedef enum psygfx_stroke_align {
    PSYGFX_STROKE_CENTER = 0,   /* the band straddles the boundary             */
    PSYGFX_STROKE_INSIDE,       /* the band lies inside it                      */
    PSYGFX_STROKE_OUTSIDE       /* the band lies outside it                     */
} psygfx_stroke_align;
typedef enum psygfx_join {
    PSYGFX_JOIN_ROUND = 0,      /* the SDF's own: round outside a turn          */
    PSYGFX_JOIN_MITER,          /* sharp; RECT (no corner radius), CROSS and
                                 * convex POLYGON only; POLYLINE                */
    PSYGFX_JOIN_BEVEL           /* POLYLINE only                                */
} psygfx_join;

/* The ends of a dash, of a trimmed outline and of an open path (POLYLINE,
 * QBEZIER). LINE, CAPSULE and ARC have round ends of their own. */
typedef enum psygfx_cap {
    PSYGFX_CAP_BUTT = 0, PSYGFX_CAP_ROUND, PSYGFX_CAP_SQUARE
} psygfx_cap;

/* How a color stimulus meets the scene, in linear light (BLEND). */
typedef enum psygfx_blend_mode {
    PSYGFX_BLEND_MODE_OVER = 0,  /* premultiplied over: an occluder             */
    PSYGFX_BLEND_MODE_ADD,       /* rgb += color x coverage: superposed light   */
    PSYGFX_BLEND_MODE_MULTIPLY   /* rgb *= 1 - a + a color: a filter            */
} psygfx_blend_mode;

/* A compound's operations: each primitive meets everything before it. */
typedef enum psygfx_op {
    PSYGFX_OP_UNION = 0, PSYGFX_OP_INTERSECT, PSYGFX_OP_SUBTRACT, PSYGFX_OP_XOR,
    PSYGFX_OP_SMOOTH_UNION, PSYGFX_OP_SMOOTH_INTERSECT, PSYGFX_OP_SMOOTH_SUBTRACT
} psygfx_op;

#ifndef PSYGFX_MAX_PRIMS
#define PSYGFX_MAX_PRIMS 56            /* primitives per compound               */
#endif
#define PSYGFX_MAX_PATH  224           /* points per POLYLINE                   */

/* One primitive of a compound: a plain value, 48 bytes. Its fields are in
 * the gfx's units, in the compound's box frame (pixels from the box center
 * along its axes). */
typedef struct psygfx_prim {
    uint8_t shape;          /* CIRCLE, ANNULUS, RECT, RRECT, LINE, CAPSULE,
                             * CROSS, ARC, PIE, NGON, STAR, ELLIPSE              */
    uint8_t op;             /* psygfx_op with everything before it; the first
                             * primitive's is ignored                           */
    uint8_t reserved_[2];
    float   x, y;           /* center                                           */
    float   w, h;           /* its box, as the shape's own w, h                 */
    float   ori;            /* degrees, +x toward +y                            */
    float   k;              /* SMOOTH_*: blend radius                           */
    float   onion;          /* > 0: the shell |d| - onion of this primitive     */
    float   p[4];           /* as shape_p                                       */
} psygfx_prim;

/* Effects drawn from the same distance field, in one pass (EFFECTS). px. */
typedef struct psygfx_shadow {
    float sigma;            /* Gaussian SD; 0 = a hard silhouette                */
    float spread;           /* the silhouette moved out (+) or in (-)            */
    float color[3];         /* linear device RGB                                 */
    float opacity;          /* 0 = off                                           */
} psygfx_shadow;

typedef struct psygfx_band {
    float a1, a2;           /* offsets of the boundary, a1 < a2; -2, 0 = a 2 px
                             * inner outline                                     */
    float color[3];
    float opacity;          /* 0 = off                                           */
} psygfx_band;

#define PSYGFX_FX_EXACT_BLUR 0x1u  /* drop shadow of a sharp RECT: the exact
                                    * Gaussian blur of the rect (separable erf) */

typedef struct psygfx_fx {
    float         dx, dy;   /* screen px: the offset of drop and inner shadow    */
    psygfx_shadow drop;     /* behind the shape, at the offset                   */
    psygfx_shadow glow;     /* behind the shape, at no offset                    */
    psygfx_shadow inner;    /* inside the fill, from the offset                  */
    psygfx_band   band[2];  /* outlines on top                                   */
    uint32_t      flags;    /* PSYGFX_FX_*                                       */
} psygfx_fx;

/* Fill colors that vary over the shape (PAINT). */
typedef enum psygfx_paint_kind {
    PSYGFX_PAINT_SOLID = 0,
    PSYGFX_PAINT_LINEAR,    /* along x0, y0 -> x1, y1                            */
    PSYGFX_PAINT_RADIAL,    /* center x0, y0; radius x1                          */
    PSYGFX_PAINT_ANGULAR,   /* center x0, y0; start x1 degrees, clockwise        */
    PSYGFX_PAINT_VERTEX     /* a color per vertex of a convex POLYGON            */
} psygfx_paint_kind;

typedef enum psygfx_space {
    PSYGFX_SPACE_RGB = 0,   /* linear device RGB (also XYZ, LMS, Cartesian DKL:
                             * linear maps of it give the same mix)             */
    PSYGFX_SPACE_OKLAB,     /* Ottosson's Oklab from the calibration's XYZ      */
    PSYGFX_SPACE_DKL_POLAR  /* elevation, azimuth, radius about the background  */
} psygfx_space;

typedef struct psygfx_stop { float t; float color[3]; } psygfx_stop;

typedef struct psygfx_paint {
    uint8_t kind, space;    /* psygfx_paint_kind, psygfx_space                   */
    uint8_t repeat;         /* t past 0..1 wraps; else it is clamped             */
    uint8_t n;              /* stops, 2..6                                       */
    float   x0, y0, x1, y1; /* units, the box frame (from its center)            */
    psygfx_stop stops[6];   /* t rising 0..1; colors in linear device RGB        */
    const float* vertex_colors;   /* VERTEX: rgb per POLYGON vertex             */
} psygfx_paint;

/* One glyph of a run: an instance (TEXT, GLYPH RUNS). 32 bytes. */
typedef struct psygfx_glyph {
    float x, y;             /* the glyph box's top-left, units from the run's
                             * box top-left                                      */
    float sx, sy, sw, sh;   /* its atlas rectangle, texels from the top-left,
                             * padding included                                  */
    float reserved[2];
} psygfx_glyph;

struct psygfx_group;

typedef enum psygfx_edge {
    PSYGFX_EDGE_HARD = 0,    /* 1 where the pixel center is inside, else 0      */
    PSYGFX_EDGE_COSINE,      /* raised cosine, edge_width = full 0-to-1 width   */
    PSYGFX_EDGE_GAUSSIAN     /* hard edge blurred by a Gaussian, SD edge_width  */
} psygfx_edge;

typedef enum psygfx_noise_dist { PSYGFX_UNIFORM = 0, PSYGFX_BINARY = 1 } psygfx_noise_dist;

/* One stimulus. A plain value: copy it, keep arrays of it, set fields
 * directly. The float fields are the parameter table's (psygfx_params) and
 * can be bound to timeline channels (psygfx_apply). Spatial fields are in
 * the units of the gfx (pixels, or degrees as a scale factor), edges and dot
 * sizes in pixels. Screen axes: origin top-left, x right, y down; ori turns
 * +x toward +y, clockwise on the screen (COORDINATES). */
typedef struct psygfx_stim {
    uint16_t    kind, shape, edge, wave;   /* psygfx_kind, _shape_kind, _edge;
                                            * wave 0 sine, 1 square           */
    uint32_t    flags;                     /* PSYGFX_STIM_*                     */
    uint8_t     place;                     /* psygfx_align: the screen point x,
                                            * y are measured from             */
    uint8_t     reserved_[3];
    float       visible;      /* < 0.5: not drawn                               */
    float       x, y;         /* the anchor's offset from the place point       */
    float       ax, ay;       /* anchor: the point of the box at x, y and the
                               * center of rotation, as fractions of the box
                               * (0, 0 top-left; 0.5, 0.5 the center)          */
    float       w, h;         /* size; GABOR: 0 = 8 sigma                       */
    float       ori;          /* degrees, +x toward +y (clockwise on screen)    */
    float       contrast;     /* modulation: amplitude along dir                */
    float       opacity;      /* color stimuli                                  */
    float       gate;         /* multiplier, for flicker tracks                 */
    float       color[3];     /* color stimuli: linear device RGB               */
    float       dir[3];       /* modulation per unit contrast, linear RGB;
                               * 0, 0, 0 = the background (luminance contrast)  */
    float       sf;           /* cycles per unit                                */
    float       phase;        /* cycles                                         */
    float       sigma;        /* GABOR envelope SD                              */
    float       aspect;       /* GABOR envelope: y extent = sigma / aspect      */
    float       edge_width;   /* px                                             */
    float       shape_p[4];   /* see psygfx_shape_kind                          */
    float       dot_size;     /* DOTS diameter, px                              */
    float       seed;         /* NOISE: an integer below 2^24                   */
    float       check;        /* NOISE check size, px; 0 = 1                    */
    float       p[32];        /* USER parameters; POLYGON vertices              */
    psygfx_tex  tex;          /* IMAGE                                          */
    psygfx_buf  buf;          /* DOTS: x, y float pairs, units, from x, y       */
    uint32_t    count;        /* DOTS                                           */
    psygfx_pipe pipe;         /* USER                                           */
    /* v0.2 */
    float       stroke;       /* band width, px; 0 = fill                       */
    float       miter_limit;  /* MITER: miter length / stroke width; 0 = 4       */
    float       tint[4];      /* IMAGE: linear rgb and alpha factors; 1 each      */
    float       src[4];       /* IMAGE: texel rect x, y, w, h from the top-left;
                               * w or h 0 = the whole texture                    */
    uint8_t     stroke_align, join;   /* psygfx_stroke_align, psygfx_join          */
    uint8_t     reserved2_[2];
    psygfx_tex  mask;         /* PSYGFX_MASK_TEX: signed distance in texels      */
    const struct psygfx_group* group;   /* NULL = none (GROUPS)                  */
    /* v0.3 */
    float       offset;       /* px: the boundary moved out (+) or in (-); SHAPE */
    float       dash[2];      /* px: dash and gap along the boundary; 0 = none   */
    float       dash_offset;  /* px along the boundary: march it                 */
    float       trim[2];      /* start, end as fractions of the length; 0, 0 =
                               * the whole                                       */
    uint8_t     cap;          /* psygfx_cap                                      */
    uint8_t     blend;        /* psygfx_blend_mode, color stimuli                */
    uint8_t     dash_snap;    /* closed boundaries: a whole number of periods    */
    uint8_t     reserved3_;
    uint32_t    n_path;       /* POLYLINE, QBEZIER points                        */
    uint32_t    n_prims;      /* COMPOUND                                        */
    const float*        path; /* x, y pairs, units, from the box center          */
    const psygfx_prim*  prims;
    const psygfx_fx*    fx;   /* NULL = none                                     */
    const psygfx_paint* paint;   /* NULL = color                                 */
} psygfx_stim;

#define PSYGFX_STIM_MODULATION    0x1u  /* IMAGE: the red channel modulates (g) */
#define PSYGFX_STIM_LINEAR        0x2u  /* IMAGE: sample with the texture filter */
#define PSYGFX_STIM_ADD           0x4u  /* IMAGE: add texel.rgb (a layer of
                                         * increments, from a target)           */
#define PSYGFX_STIM_PREMULTIPLIED 0x8u  /* IMAGE: rgb already times alpha (set
                                         * for a target by psygfx_image)        */

/* A flat group (GROUPS): place, anchor and box on the screen; its members'
 * place, x, y and ori are in its frame. One level: a group has no group. */
typedef struct psygfx_group {
    uint8_t place;            /* psygfx_align, on the screen or target          */
    uint8_t reserved_[3];
    float   visible;          /* < 0.5: no member is drawn                       */
    float   x, y;             /* units, along the screen axes, from place        */
    float   ax, ay;           /* anchor: fractions of the group's box            */
    float   w, h;             /* the group's box, units; 0 x 0 = a point         */
    float   ori;              /* degrees, +x toward +y, about the anchor         */
    float   scale;            /* multiplies members' sizes in units, not in px   */
    float   opacity;          /* multiplies every member's gate                  */
} psygfx_group;

typedef struct psygfx_group_desc {
    psygfx_align place, anchor;                                /* 0 = CENTER */
    float x, y, w, h, ori;
    float scale, opacity;                                      /* 0 = 1 */
} psygfx_group_desc;

enum {
    PSYGFX_G_VISIBLE, PSYGFX_G_X, PSYGFX_G_Y, PSYGFX_G_AX, PSYGFX_G_AY, PSYGFX_G_W, PSYGFX_G_H,
    PSYGFX_G_ORI, PSYGFX_G_SCALE, PSYGFX_G_OPACITY, PSYGFX_G_COUNT
};

typedef struct psygfx_shape_desc {
    psygfx_align place, anchor;                                /* 0 = CENTER */
    psygfx_shape_kind shape; psygfx_edge edge; float edge_width;
    float x, y, w, h, ori; float color[3]; float opacity;     /* 0 = 1 */
    float shape_p[4]; const float* vertices;                   /* POLYGON: n x, y */
    float stroke; psygfx_stroke_align stroke_align; psygfx_join join; float miter_limit;
    psygfx_tex mask; const psygfx_group* group;
    /* v0.3 */
    float offset; float dash[2]; float dash_offset; float trim[2];
    psygfx_cap cap; psygfx_blend_mode blend; bool dash_snap;
    const float* path; int n_path;                             /* POLYLINE, QBEZIER */
    const psygfx_fx* fx; const psygfx_paint* paint;
} psygfx_shape_desc;

/* A compound (COMPOUNDS): its box places and anchors it, its prims draw it. */
typedef struct psygfx_compound_desc {
    psygfx_align place, anchor;                                /* 0 = CENTER */
    const psygfx_prim* prims; int n;                           /* 1..PSYGFX_MAX_PRIMS */
    float x, y, w, h, ori;
    float onion;                                               /* units, on the result */
    psygfx_edge edge; float edge_width;
    float color[3]; float opacity;                             /* 0 = 1 */
    float stroke; psygfx_stroke_align stroke_align; float offset;
    psygfx_blend_mode blend;
    const psygfx_fx* fx; const psygfx_paint* paint;
    const psygfx_group* group;
} psygfx_compound_desc;

/* A glyph run (GLYPH RUNS): one draw of count glyphs from an atlas. */
typedef struct psygfx_glyphs_desc {
    psygfx_align place, anchor;                                /* 0 = CENTER */
    psygfx_tex atlas;                                          /* MSDF, MTSDF or DIST */
    psygfx_buf buf; uint32_t count;                            /* psygfx_glyph records */
    float scale;                                               /* units per atlas texel */
    float x, y, w, h, ori;                                     /* the run's box */
    psygfx_edge edge; float edge_width;
    float color[3]; float opacity;                             /* 0 = 1 */
    float stroke; psygfx_stroke_align stroke_align; psygfx_join join; float offset;
    psygfx_blend_mode blend;
    const psygfx_group* group;
} psygfx_glyphs_desc;

typedef struct psygfx_grating_desc {
    psygfx_align place, anchor;                                /* 0 = CENTER */
    float x, y, w, h, ori, sf, phase; float contrast;          /* 0 = 1 */
    int square;                                                /* square wave */
    psygfx_shape_kind aperture;                                /* 0 = RECT */
    psygfx_edge edge; float edge_width; float dir[3];
    float stroke; psygfx_stroke_align stroke_align; psygfx_tex mask; const psygfx_group* group;
} psygfx_grating_desc;

typedef struct psygfx_gabor_desc {
    psygfx_align place, anchor;                                /* 0 = CENTER */
    float x, y, ori, sf, phase, sigma; float contrast;         /* 0 = 1 */
    float aspect;                                              /* 0 = 1 */
    float size;                                                /* 0 = 8 sigma */
    float dir[3];
    const psygfx_group* group;
} psygfx_gabor_desc;

typedef struct psygfx_dots_desc {
    psygfx_align place, anchor;                                /* 0 = CENTER */
    psygfx_buf buf; uint32_t count; float x, y, ori;
    float dot_size; psygfx_edge edge; float edge_width;
    float color[3]; float opacity;                             /* 0 = 1 */
    psygfx_shape_kind aperture; float w, h;                    /* field; RECT,
                                                * CIRCLE or NO_APERTURE (default) */
    float stroke; psygfx_stroke_align stroke_align;                 /* rings */
    const psygfx_group* group;
} psygfx_dots_desc;

typedef struct psygfx_image_desc {
    psygfx_align place, anchor;                                /* 0 = CENTER */
    psygfx_tex tex; float x, y, w, h, ori;                     /* w, h 0 = texels */
    bool modulation; bool linear; float contrast; float opacity; /* 0 = 1 */
    float dir[3];
    float src[4];                                              /* w 0 = all */
    float tint[4];                                             /* all 0 = 1 */
    bool add;                                                  /* PSYGFX_STIM_ADD */
    const psygfx_group* group;
} psygfx_image_desc;

typedef struct psygfx_noise_desc {
    psygfx_align place, anchor;                                /* 0 = CENTER */
    float x, y, w, h, ori; float check; uint32_t seed;
    psygfx_noise_dist dist; float contrast;                    /* 0 = 1 */
    psygfx_shape_kind aperture; psygfx_edge edge; float edge_width; float dir[3];
    float stroke; psygfx_stroke_align stroke_align; psygfx_tex mask; const psygfx_group* group;
} psygfx_noise_desc;

typedef struct psygfx_user_desc {
    psygfx_align place, anchor;                                /* 0 = CENTER */
    psygfx_pipe pipe; float x, y, w, h, ori; float contrast, opacity; /* 0 = 1 */
    psygfx_shape_kind aperture; psygfx_edge edge; float edge_width;
    float color[3]; float dir[3]; const float* p; int n_p;
    float stroke; psygfx_stroke_align stroke_align; psygfx_tex mask; const psygfx_group* group;
} psygfx_user_desc;

/* --- user shaders ---------------------------------------------------------- */

typedef enum psygfx_shader_mode {
    PSYGFX_MODULATION = 0,  /* float psy_main(vec2 p): g in [-1, 1], added as
                             * contrast * gate * g * aperture * dir           */
    PSYGFX_COLOR = 1,       /* vec4 psy_main(vec2 p): linear rgb and coverage,
                             * blended over with opacity * gate * aperture    */
    PSYGFX_ADD = 2          /* vec3 psy_main(vec2 p): an rgb increment, added
                             * as contrast * gate * aperture * it             */
} psygfx_shader_mode;

typedef struct psygfx_pipeline_desc {
    const char*        body;   /* GLSL ES 3.00: psy_main and its helpers        */
    psygfx_shader_mode mode;
    const char*        name;   /* for messages                                  */
} psygfx_pipeline_desc;

/* --- timeline binding ------------------------------------------------------ */

/* Indices into the parameter table: the bindable float fields of
 * psygfx_stim, in order. */
enum {
    PSYGFX_P_VISIBLE, PSYGFX_P_X, PSYGFX_P_Y, PSYGFX_P_AX, PSYGFX_P_AY, PSYGFX_P_W, PSYGFX_P_H, PSYGFX_P_ORI,
    PSYGFX_P_CONTRAST, PSYGFX_P_OPACITY, PSYGFX_P_GATE,
    PSYGFX_P_COLOR_R, PSYGFX_P_COLOR_G, PSYGFX_P_COLOR_B,
    PSYGFX_P_DIR_R, PSYGFX_P_DIR_G, PSYGFX_P_DIR_B,
    PSYGFX_P_SF, PSYGFX_P_PHASE, PSYGFX_P_SIGMA, PSYGFX_P_ASPECT, PSYGFX_P_EDGE_WIDTH,
    PSYGFX_P_SHAPE_P0, PSYGFX_P_SHAPE_P1, PSYGFX_P_SHAPE_P2, PSYGFX_P_SHAPE_P3,
    PSYGFX_P_DOT_SIZE, PSYGFX_P_SEED, PSYGFX_P_CHECK,
    PSYGFX_P_P0,
    /* v0.2, appended so v0.1's indices stay */
    PSYGFX_P_STROKE = PSYGFX_P_P0 + 32, PSYGFX_P_MITER_LIMIT,
    PSYGFX_P_TINT_R, PSYGFX_P_TINT_G, PSYGFX_P_TINT_B, PSYGFX_P_TINT_A,
    PSYGFX_P_SRC_X, PSYGFX_P_SRC_Y, PSYGFX_P_SRC_W, PSYGFX_P_SRC_H,
    /* v0.3 */
    PSYGFX_P_OFFSET, PSYGFX_P_DASH, PSYGFX_P_GAP, PSYGFX_P_DASH_OFFSET,
    PSYGFX_P_TRIM_START, PSYGFX_P_TRIM_END,
    PSYGFX_P_COUNT
};

typedef struct psygfx_bind {
    psygfx_stim*  stim;
    uint16_t      param;    /* PSYGFX_P_*, or PSYGFX_G_* with group              */
    uint16_t      channel;  /* index into the values (psytl_values())           */
    psygfx_group* group;    /* non-NULL: param is a field of this group          */
    float*        field;    /* v0.3, non-NULL: values[channel] goes here; stim,
                             * param and group are not read. Any float: a
                             * primitive's x, a stop's t, a shadow's sigma       */
} psygfx_bind;

/* One field of psygfx_stim a designer or a script sets. */
typedef struct psygfx_param {
    const char* name;     /* "contrast", "p7"                                    */
    const char* type;     /* "f32"                                               */
    double      min, max;
    double      def;      /* the value a constructor gives when its desc is 0   */
    const char* unit;     /* "", "u" (the gfx unit), "px", "deg", "cyc/u", "cyc" */
    const char* doc;
    uint32_t    offset;   /* in psygfx_stim                                      */
    uint32_t    kinds;    /* bit (1 << psygfx_kind) for each kind that reads it  */
} psygfx_param;

/* --- calibration ------------------------------------------------------------ */

#define PSYGFX_CAL_MAX_READINGS 256
#define PSYGFX_CAL_MAX_WL       471   /* 360 to 830 nm at 1 nm                  */
#define PSYGFX_CAL_MAX_LUT      4096

#define PSYGFX_CAL_NOMINAL     0x1u   /* made from stated primaries and a gamma,
                                       * not from a measurement                 */
#define PSYGFX_CAL_HAS_XY      0x2u   /* the readings carry chromaticities      */
#define PSYGFX_CAL_HAS_SPECTRA 0x4u   /* spd[] holds the primaries' spectra     */

#define PSYGFX_GUN_BLACK (-1)
#define PSYGFX_GUN_WHITE 3

typedef struct psygfx_cal_reading {
    int32_t gun;       /* 0, 1, 2; PSYGFX_GUN_BLACK (all 0); PSYGFX_GUN_WHITE    */
    float   level;     /* device value of that gun, 0..1                          */
    float   Y;         /* cd/m2                                                   */
    float   x, y;      /* CIE 1931 chromaticity; 0, 0 = not measured             */
} psygfx_cal_reading;

/* A display calibration: the canonical in-memory and on-disk form (about 61
 * KB, little-endian, every offset fixed). Fill the identity, the readings
 * and the spectra, then psygfx_cal_derive() fills the rest. */
typedef struct psygfx_cal {
    char     magic[8];             /* "PSYCAL\0\1"                               */
    uint32_t version;              /* 1                                          */
    uint32_t bytes;                /* sizeof(psygfx_cal)                          */
    uint32_t flags;                /* PSYGFX_CAL_*                                */
    int32_t  mode_w, mode_h, refresh_num, refresh_den;
    int32_t  n_readings;
    int64_t  date;                 /* seconds since 1970, UTC                     */
    char     display[64];
    char     serial[64];
    char     instrument[64];
    char     screen[256];          /* psyscr_describe() at measurement            */
    psygfx_cal_reading readings[PSYGFX_CAL_MAX_READINGS];
    int32_t  n_wl;                 /* spectra samples                             */
    float    wl_start, wl_step;    /* nm                                          */
    int32_t  lut_n;                /* CLUT entries, 2..4096; 0 = 4096             */
    float    spd[4][PSYGFX_CAL_MAX_WL];  /* R, G, B at full output, and black;
                                    * W sr-1 m-2 nm-1                             */
    /* derived by psygfx_cal_derive() */
    double   rgb_to_xyz[9];        /* CIE 1931, row-major, black removed          */
    double   rgb_to_lms[9];        /* Stockman-Sharpe 2 deg, black removed        */
    double   black_xyz[3];
    double   black_lms[3];
    double   white_err;            /* white minus the sum of the guns, percent    */
    float    lut[3][PSYGFX_CAL_MAX_LUT];   /* linear to device value, per gun     */
    uint32_t reserved;
    uint32_t crc;                  /* CRC-32 of every byte before it              */
} psygfx_cal;

/* --- backend (the gfx backend interface) ------------------------------------ */

#define PSYGFX_BACKEND_VERSION 2   /* 2: v0.3, psygfx_bindings.blend and the
                                    * 2-vec4 instance layout                    */

typedef struct psygfx_backend_open {
    psyscr_proc (*gl_proc)(void* ctx, const char* name);
    void*       gl_ctx;
    int32_t     w, h;              /* framebuffer 0                              */
} psygfx_backend_open;

typedef struct psygfx_backend_caps {
    bool     color_buffer_float;   /* float render targets                       */
    bool     float_blend;          /* blending into 32-bit float targets          */
    bool     float_linear;         /* linear filtering of 32-bit float textures   */
    int32_t  ubo_align;
    int32_t  max_ubo;
    char     renderer[160];
} psygfx_backend_caps;

#define PSYGFX_BLEND_NONE  0
#define PSYGFX_BLEND_ADD   1       /* rgb += src, alpha kept                      */
#define PSYGFX_BLEND_OVER  2       /* premultiplied over, alpha accumulates       */
#define PSYGFX_BLEND_MULTIPLY 3    /* rgb = dst (1 - a) + dst src.rgb, alpha kept  */

typedef struct psygfx_pipeline_src {
    const char* vs;
    const char* fs;
    int32_t     blend;             /* PSYGFX_BLEND_*                              */
    int32_t     instance_attr;     /* 1: attribute 0 is a vec2 per instance; 2:
                                    * attributes 0 and 1 are vec4s, 32 bytes   */
    int32_t     deferred;          /* 1: return once the link is under way; the
                                    * status comes from pipeline_finish (when
                                    * the backend has it), so the driver can
                                    * compile several programs at once        */
} psygfx_pipeline_src;

typedef struct psygfx_texture_src {
    int32_t       w, h;
    psygfx_format format;
    bool          linear;
    bool          target;          /* render target                               */
    const void*   data;
    size_t        stride;
} psygfx_texture_src;

#define PSYGFX_BUFFER_UNIFORM  1
#define PSYGFX_BUFFER_INSTANCE 2

typedef struct psygfx_bindings {
    uint32_t pipeline;
    uint32_t ubo;                  /* frame block at frame_off, stims at stim_off */
    uint32_t frame_off, frame_size, stim_off, stim_size;
    uint32_t tex[4];               /* units 0..3; 0 = none                        */
    uint32_t instances;            /* buffer for attribute 0; 0 = none            */
    int32_t  blend;                /* PSYGFX_BLEND_*; 0 = the pipeline's own      */
} psygfx_bindings;

#define PSYGFX_READ_FLOAT 1        /* RGBA float                                  */
#define PSYGFX_READ_UNORM 2        /* RGBA uint8_t                                */

/* Every call runs on the thread that owns the context. Ids are the
 * backend's, never 0. Rows are given and read bottom-up, GL's order. */
typedef struct psygfx_backend {
    uint32_t    version;           /* PSYGFX_BACKEND_VERSION                      */
    const char* name;
    int  (*open)(void* c, const psygfx_backend_open* in, psygfx_backend_caps* caps, char* err, size_t cap);
    void (*close)(void* c);
    int  (*pipeline_make)(void* c, const psygfx_pipeline_src* d, uint32_t* id, char* err, size_t cap);
    void (*pipeline_free)(void* c, uint32_t id);
    int  (*texture_make)(void* c, const psygfx_texture_src* d, uint32_t* id);
    int  (*texture_update)(void* c, uint32_t id, int x, int y, int w, int h, const void* data, size_t stride);
    void (*texture_free)(void* c, uint32_t id);
    int  (*buffer_make)(void* c, int kind, size_t bytes, uint32_t* id);
    int  (*buffer_update)(void* c, uint32_t id, size_t off, const void* data, size_t n);
    void (*buffer_free)(void* c, uint32_t id);
    void (*pass_begin)(void* c, uint32_t target, int w, int h, const float* clear);  /* target 0 = framebuffer 0 */
    void (*apply)(void* c, const psygfx_bindings* b);
    void (*draw)(void* c, int vertices, int instances);
    void (*pass_end)(void* c);
    void (*present)(void* c);
    int  (*read)(void* c, uint32_t target, int x, int y, int w, int h, int format, void* out);
    void (*reset)(void* c);        /* forget cached state                          */
    /* version 2: waits for a deferred pipeline and checks it; NULL = every
     * pipeline_make is complete when it returns */
    int  (*pipeline_finish)(void* c, uint32_t id, char* err, size_t cap);
} psygfx_backend;

/* --- the handle ------------------------------------------------------------ */

#ifndef PSYGFX_MAX_TEXTURES
#define PSYGFX_MAX_TEXTURES  64
#endif
#ifndef PSYGFX_MAX_BUFFERS
#define PSYGFX_MAX_BUFFERS   32
#endif
#ifndef PSYGFX_MAX_PIPELINES
#define PSYGFX_MAX_PIPELINES 32
#endif
#define PSYGFX__N_BUILTIN     10
#define PSYGFX__BACKEND_WORDS 512
#define PSYGFX__UBO_RING      3
#ifndef PSYGFX_MAX_PASSES
#define PSYGFX_MAX_PASSES    16          /* target passes per frame              */
#endif

typedef struct psygfx__seg {
    uint32_t target;               /* backend id; 0 = the scene                   */
    int32_t  w, h, topdown;
    int32_t  first, end;           /* commands                                    */
    int32_t  frame_slot;           /* its frame block                             */
    int32_t  has_clear;
    float    clear[4];
} psygfx__seg;

typedef struct psygfx__res {
    uint32_t used, bid;
    int32_t  w, h, format, flags;
    int32_t  sdf;                  /* psygfx_sdf_kind                             */
    float    sdf_range;            /* texels                                      */
} psygfx__res;

typedef struct psygfx__cmd {
    uint32_t pipe;                 /* backend id                                  */
    uint32_t tex[4];
    uint32_t inst;                 /* DOTS buffer                                 */
    uint32_t count;                /* DOTS instances                              */
    uint32_t block;                /* index of its stim block                      */
    uint32_t nblk;                 /* blocks it uses: 1, or 1 + its extension      */
    int32_t  blend;                /* PSYGFX_BLEND_*, 0 = the pipeline's          */
} psygfx__cmd;

/* The gfx. Caller-allocated and zeroed; every field is private. */
typedef struct psygfx_gfx {
    int                    open;
    int                    in_frame;
    const psygfx_backend*  be;
    void*                  bctx;
    psyscr_screen*         screen;
    psygfx_backend_caps    caps;
    int32_t                w, h;
    int                    origin_top;
    float                  bg[3];
    float                  ppu;
    psygfx_format          scene_format;
    psygfx_dither          dither;
    uint32_t               seed;
    int32_t                lut_n;
    uint32_t               cal_crc;
    uint32_t               scene, lut;              /* backend texture ids       */
    uint32_t               builtin[PSYGFX__N_BUILTIN];
    uint32_t               output_pipe;
    uint32_t               ubo[PSYGFX__UBO_RING];
    int                    ubo_i;
    uint32_t               ubo_bytes;
    unsigned char*         staging;                 /* frame block, stim blocks  */
    psygfx__cmd*           cmds;
    int32_t                max_draws, n_cmds, n_blocks;
    int32_t                max_batch;               /* a test seam               */
    psyscr_frame           frame;
    psygfx__seg            segs[2 * PSYGFX_MAX_PASSES + 1];
    int32_t                n_segs, n_targets, in_target;
    uint32_t               cur_target_tex;          /* the pool id being drawn  */
    int32_t                cur_w, cur_h, cur_top;   /* the pass being recorded  */
    uint32_t               epoch;
    uint32_t               generation;              /* psyscr_gl_generation at open */
    int64_t                t_open;
    uint32_t               clipped;                 /* this frame                */
    uint64_t               clipped_total, frames, draws;
    psyrt_ring*            ring;
    /* from the calibration at open, for PAINT's spaces: Oklab's cone
     * response (cubed) to device RGB and back; DKL about the background */
    int32_t                has_oklab, has_dkl;
    double                 ok_lms2rgb[9], ok_rgb2lms[9], dkl2rgb[9], rgb2dkl[9];
    psygfx__res            tex[PSYGFX_MAX_TEXTURES];
    psygfx__res            buf[PSYGFX_MAX_BUFFERS];
    psygfx__res            pipe[PSYGFX_MAX_PIPELINES];
    char                   error[512];
    uint64_t               backend_mem[PSYGFX__BACKEND_WORDS];
} psygfx_gfx;

/* --- API ------------------------------------------------------------------- */

PSYGFX_API const char* psygfx_version(void);
PSYGFX_API const char* psygfx_strerror(int code);

/* Opens on desc->screen (or the headless context of desc->gl_proc): loads GL,
 * compiles the built-in programs, makes the scene target, the CLUT and the
 * uniform buffers, and allocates the per-frame staging once (max_draws x 256
 * bytes). Nothing is allocated after it. False with psygfx_error() set:
 * no EXT_color_buffer_float, an output, stereo or scene format v0.1 does not
 * have, a calibration that fails psygfx_cal_check(). On a screen without GL
 * (SIM) it opens the null backend: everything runs, nothing is drawn. */
PSYGFX_API bool        psygfx_open(psygfx_gfx* g, const psygfx_desc* d);
PSYGFX_API void        psygfx_close(psygfx_gfx* g);
PSYGFX_API const char* psygfx_error(const psygfx_gfx* g);
PSYGFX_API bool        psygfx_is_open(const psygfx_gfx* g);
/* One line for the log: backend, renderer, size, scene format, CLUT, dither,
 * units. Returns snprintf's count. */
PSYGFX_API int         psygfx_describe(const psygfx_gfx* g, char* buf, size_t cap);

/* Stimuli. Zero desc fields take defaults; the result has real values in
 * every field (visible 1, gate 1, contrast and opacity 1 unless set). */
PSYGFX_API psygfx_stim psygfx_shape(const psygfx_shape_desc* d);
PSYGFX_API psygfx_stim psygfx_grating(const psygfx_grating_desc* d);
PSYGFX_API psygfx_stim psygfx_gabor(const psygfx_gabor_desc* d);
PSYGFX_API psygfx_stim psygfx_dots(const psygfx_dots_desc* d);
PSYGFX_API psygfx_stim psygfx_image(const psygfx_gfx* g, const psygfx_image_desc* d);
PSYGFX_API psygfx_stim psygfx_noise(const psygfx_noise_desc* d);
PSYGFX_API psygfx_stim psygfx_user(const psygfx_user_desc* d);
/* v0.3. A compound: a SHAPE of kind PSYGFX_COMPOUND whose prims point at
 * the desc's array (keep it alive; bind its fields with psygfx_bind.field).
 * A glyph run: a SHAPE with MASK_TEX on the atlas and buf, count glyphs. */
PSYGFX_API psygfx_stim psygfx_compound(const psygfx_compound_desc* d);
PSYGFX_API psygfx_stim psygfx_glyphs(const psygfx_glyphs_desc* d);
/* The length in px of what dashes and trim run along: the boundary, or the
 * center line of a LINE, CAPSULE, ARC, QBEZIER or POLYLINE. Negative: a
 * code (no length: a CROSS, a mask, a compound, a stimulus that is not a
 * shape). */
PSYGFX_API double psygfx_length(const psygfx_gfx* g, const psygfx_stim* s);

/* The frame. begin() takes the frame psyscr_begin() filled (or one you fill,
 * headless) and records nothing on the GPU; draw() packs the stimulus and
 * queues it; end() uploads the frame's uniforms once, draws the queue into the
 * scene target in order, runs the output stage into framebuffer 0, and
 * returns. Nothing waits for the GPU. Call psyscr_flip() after end(). */
PSYGFX_API int  psygfx_begin(psygfx_gfx* g, const psyscr_frame* f);
PSYGFX_API int  psygfx_draw(psygfx_gfx* g, const psygfx_stim* s);
PSYGFX_API int  psygfx_draw_n(psygfx_gfx* g, const psygfx_stim* s, int n);
PSYGFX_API int  psygfx_end(psygfx_gfx* g);
/* After your own GL calls between frames: forget the cached GL state. */
PSYGFX_API void psygfx_reset_state(psygfx_gfx* g);
/* Draws that may have left 0..1 since open (a CPU bound per draw). */
PSYGFX_API uint64_t psygfx_clipped(const psygfx_gfx* g);

/* Groups (GROUPS). Zero desc fields take defaults (scale, opacity 1; anchor
 * the center); the parameter table of PSYGFX_G_* fields. */
PSYGFX_API psygfx_group psygfx_group_make(const psygfx_group_desc* d);
PSYGFX_API const psygfx_param* psygfx_group_params(int* n);

/* Render targets (TARGETS). psygfx_target() at setup only. begin_target()
 * and end_target() inside begin..end, not nested; clear NULL keeps what the
 * target holds. read_target() reads RGBA float, rows top first. */
PSYGFX_API psygfx_tex psygfx_target(psygfx_gfx* g, const psygfx_target_desc* d);
PSYGFX_API int psygfx_begin_target(psygfx_gfx* g, psygfx_tex t, const float* clear);
PSYGFX_API int psygfx_end_target(psygfx_gfx* g);
PSYGFX_API int psygfx_read_target(psygfx_gfx* g, psygfx_tex t, int x, int y, int w, int h, float* rgba);

/* A signed-distance texture for PSYGFX_MASK_TEX from a mask (texels >= 128
 * inside): out is (w + 2 pad) x (h + 2 pad) floats, rows top first, the
 * distance in texels to the boundary halfway between inside and outside
 * texel centers, negative inside. An exact Euclidean distance transform
 * (Felzenszwalb and Huttenlocher 2012). At setup: allocates its scratch. */
PSYGFX_API int psygfx_sdf_from_mask(float* out, const uint8_t* mask, int w, int h, int pad);

/* Resources. Outside begin..end only (PSYGFX_ERR_ORDER otherwise): a queued
 * draw is submitted at end() and must see the data it was queued with. */
PSYGFX_API psygfx_tex psygfx_texture(psygfx_gfx* g, const psygfx_texture_desc* d);
PSYGFX_API int        psygfx_texture_update(psygfx_gfx* g, psygfx_tex t, int x, int y, int w, int h,
                                            const void* data, size_t stride);
PSYGFX_API void       psygfx_texture_free(psygfx_gfx* g, psygfx_tex t);
PSYGFX_API psygfx_buf psygfx_buffer(psygfx_gfx* g, size_t bytes);
PSYGFX_API int        psygfx_buffer_update(psygfx_gfx* g, psygfx_buf b, size_t off, const void* data, size_t n);
PSYGFX_API void       psygfx_buffer_free(psygfx_gfx* g, psygfx_buf b);
/* A user shader: the body wrapped in the contract (SHADER CONTRACT), compiled
 * now. On failure id 0 and psygfx_error() holds the compiler's log, whose
 * line numbers are the body's. */
PSYGFX_API psygfx_pipe psygfx_pipeline(psygfx_gfx* g, const psygfx_pipeline_desc* d);
PSYGFX_API void        psygfx_pipeline_free(psygfx_gfx* g, psygfx_pipe p);

/* The fragment shader psygfx_pipeline() compiles for a body, without GL: the
 * pack tool validates the same text. Returns the length, or a negative code
 * when cap is too small (the length needed is then -return - 1000). */
PSYGFX_API int psygfx_shader_wrap(const char* body, psygfx_shader_mode mode, char* out, size_t cap);

/* The output stage's table: lut holds n values per channel, red first, linear
 * to device value 0..1, 2 <= n <= 4096. Between frames. */
PSYGFX_API int psygfx_set_lut(psygfx_gfx* g, const float* lut, int n);

/* Read back for tests and checks; blocks on the GPU, never in a trial. x, y
 * from the top-left, rows top first. scene: RGBA float in linear device RGB;
 * output: RGBA8 codes of framebuffer 0 after end(). */
PSYGFX_API int psygfx_read_scene(psygfx_gfx* g, int x, int y, int w, int h, float* rgba);
PSYGFX_API int psygfx_read_output(psygfx_gfx* g, int x, int y, int w, int h, uint8_t* rgba);

/* Screen pixels: from the top-left corner, x right, y down, pixel centers at
 * + 0.5; the space of psygfx_read_output()'s rows and of SDL's mouse events
 * at pixel density 1 (COORDINATES). The screen's center and size; a
 * stimulus's anchor point; the axis-aligned bounds of its turned box; the
 * point of its own frame (pixels from its box center, along its axes) under
 * a screen pixel; and whether a screen pixel is inside it, where the GPU's
 * coverage is 0.5 or more (SHAPE, and the aperture of GRATING, GABOR, NOISE,
 * IMAGE and USER; false for the rest). */
PSYGFX_API void psygfx_center(const psygfx_gfx* g, float* x, float* y);
PSYGFX_API void psygfx_size(const psygfx_gfx* g, float* w, float* h);
PSYGFX_API void psygfx_resolve(const psygfx_gfx* g, const psygfx_stim* s, float* x, float* y);
PSYGFX_API void psygfx_bounds(const psygfx_gfx* g, const psygfx_stim* s, float* x0, float* y0, float* x1, float* y1);
PSYGFX_API void psygfx_local(const psygfx_gfx* g, const psygfx_stim* s, float px, float py, float* lx, float* ly);
PSYGFX_API bool psygfx_hit(const psygfx_gfx* g, const psygfx_stim* s, float px, float py);

/* Timeline binding: values[b[i].channel] goes to field b[i].param of
 * *b[i].stim, for each i. A loop of stores. */
PSYGFX_API int psygfx_apply(const psygfx_bind* b, int n, const float* values);

/* The parameter table; *n gets PSYGFX_P_COUNT. */
PSYGFX_API const psygfx_param* psygfx_params(int* n);
/* v0.3: the float fields of psygfx_prim, psygfx_fx and psygfx_paint, for a
 * designer; offset is within that struct, kinds is 0. Bind them with
 * psygfx_bind.field. */
PSYGFX_API const psygfx_param* psygfx_prim_params(int* n);
PSYGFX_API const psygfx_param* psygfx_fx_params(int* n);
PSYGFX_API const psygfx_param* psygfx_paint_params(int* n);
/* The desc fields a designer sets (psyscr_param's shape). */
PSYGFX_API const psyscr_param* psygfx_desc_params(int* n);

/* Noise, bit for bit what the NOISE stimulus draws for check (cx, cy) from
 * its top-left corner. psygfx_noise_fill() writes w x h checks, rows top
 * first. psygfx_noise_gauss() is CPU only: N(0, 1) by Box-Muller from the
 * same hash; reproducible for one compiler and C library. */
PSYGFX_API float psygfx_noise_value(int32_t cx, int32_t cy, uint32_t seed, psygfx_noise_dist dist);
PSYGFX_API void  psygfx_noise_fill(float* out, int w, int h, uint32_t seed, psygfx_noise_dist dist);
PSYGFX_API void  psygfx_noise_gauss(float* out, int w, int h, uint32_t seed);
PSYGFX_API uint32_t psygfx_hash(uint32_t v);   /* pcg_hash, Jarzynski and Olano 2020 */

/* Calibration (CALIBRATION in the manual). */
PSYGFX_API void  psygfx_cal_init(psygfx_cal* c);   /* zero, magic, version, size  */
PSYGFX_API int   psygfx_cal_add(psygfx_cal* c, int gun, float level, float Y, float x, float y);
PSYGFX_API int   psygfx_cal_set_spectra(psygfx_cal* c, float wl_start, float wl_step, int n,
                                        const float* r, const float* g, const float* b, const float* black);
PSYGFX_API int   psygfx_cal_derive(psygfx_cal* c, char* err, size_t cap);
PSYGFX_API int   psygfx_cal_check(const psygfx_cal* c, char* err, size_t cap);
PSYGFX_API int   psygfx_cal_save(psygfx_cal* c, void* out, size_t cap);
PSYGFX_API int   psygfx_cal_load(psygfx_cal* c, const void* bytes, size_t n, char* err, size_t cap);
PSYGFX_API int   psygfx_cal_nominal(psygfx_cal* c, const float xy[4][2], float white_Y, double gamma);
PSYGFX_API int   psygfx_cal_lms(const psygfx_cal* c, const float rgb[3], double lms[3]);
PSYGFX_API int   psygfx_cal_dir_cone(const psygfx_cal* c, const float bg[3], const float cc[3], float dir[3]);
PSYGFX_API int   psygfx_cal_dkl_matrix(const psygfx_cal* c, const float bg[3], double m[9]);
PSYGFX_API int   psygfx_cal_dir_dkl(const psygfx_cal* c, const float bg[3], const float dkl[3], float dir[3]);
PSYGFX_API void  psygfx_dkl_from_sph(float elevation_deg, float azimuth_deg, float radius, float dkl[3]);
PSYGFX_API float psygfx_max_contrast(const float bg[3], const float dir[3]);
/* Stockman and Sharpe (2000) 2-degree cone fundamentals, linear energy, from
 * CVRL (linss2_10e_1), 390 to 830 nm at 1 nm; S is 0 above 615 nm, as CVRL
 * gives it. Linear interpolation between samples, 0 outside. */
PSYGFX_API void  psygfx_cone_fundamentals(double nm, double lms[3]);

#ifdef __cplusplus
}
#endif

#endif /* PSY_GFX_H_INCLUDED */

/* ======================================================================= *
 *                            IMPLEMENTATION                               *
 * ======================================================================= */
#ifdef PSY_GFX_IMPLEMENTATION
#ifndef PSY_GFX_IMPLEMENTATION_GUARD
#define PSY_GFX_IMPLEMENTATION_GUARD

#ifndef PSY_SCREEN_IMPLEMENTATION_GUARD
    #define PSY_SCREEN_IMPLEMENTATION
    #include "psy_screen.h"
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- small helpers ------------------------------------------------------- */

static void psygfx__set_error(char* buf, size_t cap, const char* fmt, ...) {
    va_list ap;
    if (!buf || cap == 0) return;
    va_start(ap, fmt);
    vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
}


/* v0.3: the vector program's packing and CPU form, further down */
static int psygfx__is_vector(const psygfx_stim* s);
static int psygfx__vpack(const psygfx_gfx* g, const psygfx_stim* s, float* b, double ext[2], int* clipped, const char** err);
static int psygfx__vhard(const float* b, double px, double py);
static double psygfx__edge_reach(const psygfx_stim* s);

PSYGFX_API const char* psygfx_version(void) { return PSYGFX_VERSION_STRING; }

PSYGFX_API const char* psygfx_strerror(int code) {
    switch (code) {
    case PSYGFX_OK:                  return "ok";
    case PSYGFX_ERR_ARG:             return "bad argument";
    case PSYGFX_ERR_CLOSED:          return "gfx not open";
    case PSYGFX_ERR_ORDER:           return "begin, draw, end or an update out of turn";
    case PSYGFX_ERR_FULL:            return "pool or frame full";
    case PSYGFX_ERR_GL:              return "the backend failed";
    case PSYGFX_ERR_NOT_IMPLEMENTED: return "not implemented";
    case PSYGFX_ERR_REFUSED:         return "refused";
    case PSYGFX_ERR_FORMAT:          return "not the canonical form";
    case PSYGFX_ERR_RANGE:           return "out of range";
    default:                         return code > 0 ? "ok" : "unknown error";
    }
}

/* Bytes per texel of the canonical in-memory form. */
static int psygfx__texel_bytes(psygfx_format f) {
    switch (f) {
    case PSYGFX_R8:      return 1;
    case PSYGFX_RG8:     return 2;
    case PSYGFX_RGBA8:   return 4;
    case PSYGFX_R16F:    return 4;     /* float in, half stored */
    case PSYGFX_RGBA16F: return 16;
    case PSYGFX_R32F:    return 4;
    case PSYGFX_RGBA32F: return 16;
    case PSYGFX_R16UI:   return 2;
    default:             return 0;
    }
}

/* --- GL ES 3.0 backend ----------------------------------------------------- */

#if defined(_WIN32)
    #define PSYGFX__APIENTRY __stdcall
#else
    #define PSYGFX__APIENTRY
#endif

typedef unsigned int  psygfx__GLenum;
typedef unsigned int  psygfx__GLuint;
typedef int           psygfx__GLint;
typedef int           psygfx__GLsizei;
typedef unsigned char psygfx__GLboolean;
typedef ptrdiff_t     psygfx__GLintptr;
typedef ptrdiff_t     psygfx__GLsizeiptr;
typedef char          psygfx__GLchar;

/* The entry points the backend uses, loaded by name at open. */
#define PSYGFX__GL_FUNCS(X) \
    X(const unsigned char*, GetString, (psygfx__GLenum)) \
    X(const unsigned char*, GetStringi, (psygfx__GLenum, psygfx__GLuint)) \
    X(void, GetIntegerv, (psygfx__GLenum, psygfx__GLint*)) \
    X(psygfx__GLenum, GetError, (void)) \
    X(void, Enable, (psygfx__GLenum)) \
    X(void, Disable, (psygfx__GLenum)) \
    X(void, BlendFuncSeparate, (psygfx__GLenum, psygfx__GLenum, psygfx__GLenum, psygfx__GLenum)) \
    X(void, BlendEquation, (psygfx__GLenum)) \
    X(void, Viewport, (psygfx__GLint, psygfx__GLint, psygfx__GLsizei, psygfx__GLsizei)) \
    X(void, ClearColor, (float, float, float, float)) \
    X(void, Clear, (unsigned int)) \
    X(void, ColorMask, (psygfx__GLboolean, psygfx__GLboolean, psygfx__GLboolean, psygfx__GLboolean)) \
    X(psygfx__GLuint, CreateShader, (psygfx__GLenum)) \
    X(void, ShaderSource, (psygfx__GLuint, psygfx__GLsizei, const psygfx__GLchar* const*, const psygfx__GLint*)) \
    X(void, CompileShader, (psygfx__GLuint)) \
    X(void, GetShaderiv, (psygfx__GLuint, psygfx__GLenum, psygfx__GLint*)) \
    X(void, GetShaderInfoLog, (psygfx__GLuint, psygfx__GLsizei, psygfx__GLsizei*, psygfx__GLchar*)) \
    X(void, DeleteShader, (psygfx__GLuint)) \
    X(psygfx__GLuint, CreateProgram, (void)) \
    X(void, AttachShader, (psygfx__GLuint, psygfx__GLuint)) \
    X(void, LinkProgram, (psygfx__GLuint)) \
    X(void, GetProgramiv, (psygfx__GLuint, psygfx__GLenum, psygfx__GLint*)) \
    X(void, GetProgramInfoLog, (psygfx__GLuint, psygfx__GLsizei, psygfx__GLsizei*, psygfx__GLchar*)) \
    X(void, DeleteProgram, (psygfx__GLuint)) \
    X(void, UseProgram, (psygfx__GLuint)) \
    X(psygfx__GLuint, GetUniformBlockIndex, (psygfx__GLuint, const psygfx__GLchar*)) \
    X(void, UniformBlockBinding, (psygfx__GLuint, psygfx__GLuint, psygfx__GLuint)) \
    X(psygfx__GLint, GetUniformLocation, (psygfx__GLuint, const psygfx__GLchar*)) \
    X(void, Uniform1i, (psygfx__GLint, psygfx__GLint)) \
    X(void, GenTextures, (psygfx__GLsizei, psygfx__GLuint*)) \
    X(void, DeleteTextures, (psygfx__GLsizei, const psygfx__GLuint*)) \
    X(void, BindTexture, (psygfx__GLenum, psygfx__GLuint)) \
    X(void, ActiveTexture, (psygfx__GLenum)) \
    X(void, TexStorage2D, (psygfx__GLenum, psygfx__GLsizei, psygfx__GLenum, psygfx__GLsizei, psygfx__GLsizei)) \
    X(void, TexSubImage2D, (psygfx__GLenum, psygfx__GLint, psygfx__GLint, psygfx__GLint, psygfx__GLsizei, psygfx__GLsizei, psygfx__GLenum, psygfx__GLenum, const void*)) \
    X(void, TexParameteri, (psygfx__GLenum, psygfx__GLenum, psygfx__GLint)) \
    X(void, PixelStorei, (psygfx__GLenum, psygfx__GLint)) \
    X(void, GenFramebuffers, (psygfx__GLsizei, psygfx__GLuint*)) \
    X(void, DeleteFramebuffers, (psygfx__GLsizei, const psygfx__GLuint*)) \
    X(void, BindFramebuffer, (psygfx__GLenum, psygfx__GLuint)) \
    X(void, FramebufferTexture2D, (psygfx__GLenum, psygfx__GLenum, psygfx__GLenum, psygfx__GLuint, psygfx__GLint)) \
    X(psygfx__GLenum, CheckFramebufferStatus, (psygfx__GLenum)) \
    X(void, ReadPixels, (psygfx__GLint, psygfx__GLint, psygfx__GLsizei, psygfx__GLsizei, psygfx__GLenum, psygfx__GLenum, void*)) \
    X(void, GenBuffers, (psygfx__GLsizei, psygfx__GLuint*)) \
    X(void, DeleteBuffers, (psygfx__GLsizei, const psygfx__GLuint*)) \
    X(void, BindBuffer, (psygfx__GLenum, psygfx__GLuint)) \
    X(void, BufferData, (psygfx__GLenum, psygfx__GLsizeiptr, const void*, psygfx__GLenum)) \
    X(void, BufferSubData, (psygfx__GLenum, psygfx__GLintptr, psygfx__GLsizeiptr, const void*)) \
    X(void, BindBufferRange, (psygfx__GLenum, psygfx__GLuint, psygfx__GLuint, psygfx__GLintptr, psygfx__GLsizeiptr)) \
    X(void, GenVertexArrays, (psygfx__GLsizei, psygfx__GLuint*)) \
    X(void, DeleteVertexArrays, (psygfx__GLsizei, const psygfx__GLuint*)) \
    X(void, BindVertexArray, (psygfx__GLuint)) \
    X(void, EnableVertexAttribArray, (psygfx__GLuint)) \
    X(void, VertexAttribPointer, (psygfx__GLuint, psygfx__GLint, psygfx__GLenum, psygfx__GLboolean, psygfx__GLsizei, const void*)) \
    X(void, VertexAttribDivisor, (psygfx__GLuint, psygfx__GLuint)) \
    X(void, DrawArraysInstanced, (psygfx__GLenum, psygfx__GLint, psygfx__GLsizei, psygfx__GLsizei)) \
    X(void, Flush, (void)) \
    X(void, Finish, (void))

#define PSYGFX__GL_FIELD(ret, name, args) ret (PSYGFX__APIENTRY *name) args;
typedef struct psygfx__glf { PSYGFX__GL_FUNCS(PSYGFX__GL_FIELD) } psygfx__glf;
#undef PSYGFX__GL_FIELD

#define PSYGFX__GL_NO_ERROR             0
#define PSYGFX__GL_EXTENSIONS           0x1F03
#define PSYGFX__GL_RENDERER             0x1F01
#define PSYGFX__GL_VERSION              0x1F02
#define PSYGFX__GL_NUM_EXTENSIONS       0x821D
#define PSYGFX__GL_UBO_ALIGN            0x8A34
#define PSYGFX__GL_MAX_UBO_SIZE         0x8A30
#define PSYGFX__GL_BLEND                0x0BE2
#define PSYGFX__GL_SCISSOR_TEST         0x0C11
#define PSYGFX__GL_DEPTH_TEST           0x0B71
#define PSYGFX__GL_CULL_FACE            0x0B44
#define PSYGFX__GL_STENCIL_TEST         0x0B90
#define PSYGFX__GL_DITHER               0x0BD0
#define PSYGFX__GL_RASTERIZER_DISCARD   0x8C89
#define PSYGFX__GL_ZERO                 0
#define PSYGFX__GL_ONE                  1
#define PSYGFX__GL_ONE_MINUS_SRC_ALPHA  0x0303
#define PSYGFX__GL_DST_COLOR            0x0306
#define PSYGFX__GL_FUNC_ADD             0x8006
#define PSYGFX__GL_COLOR_BUFFER_BIT     0x4000u
#define PSYGFX__GL_VERTEX_SHADER        0x8B31
#define PSYGFX__GL_FRAGMENT_SHADER      0x8B30
#define PSYGFX__GL_COMPILE_STATUS       0x8B81
#define PSYGFX__GL_LINK_STATUS          0x8B82
#define PSYGFX__GL_INFO_LOG_LENGTH      0x8B84
#define PSYGFX__GL_INVALID_INDEX        0xFFFFFFFFu
#define PSYGFX__GL_TEXTURE_2D           0x0DE1
#define PSYGFX__GL_TEXTURE0             0x84C0
#define PSYGFX__GL_TEXTURE_MIN_FILTER   0x2801
#define PSYGFX__GL_TEXTURE_MAG_FILTER   0x2800
#define PSYGFX__GL_TEXTURE_WRAP_S       0x2802
#define PSYGFX__GL_TEXTURE_WRAP_T       0x2803
#define PSYGFX__GL_NEAREST              0x2600
#define PSYGFX__GL_LINEAR               0x2601
#define PSYGFX__GL_CLAMP_TO_EDGE        0x812F
#define PSYGFX__GL_UNPACK_ALIGNMENT     0x0CF5
#define PSYGFX__GL_UNPACK_ROW_LENGTH    0x0CF2
#define PSYGFX__GL_PACK_ALIGNMENT       0x0D05
#define PSYGFX__GL_FRAMEBUFFER          0x8D40
#define PSYGFX__GL_READ_FRAMEBUFFER     0x8CA8
#define PSYGFX__GL_COLOR_ATTACHMENT0    0x8CE0
#define PSYGFX__GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define PSYGFX__GL_UNSIGNED_BYTE        0x1401
#define PSYGFX__GL_UNSIGNED_SHORT       0x1403
#define PSYGFX__GL_FLOAT                0x1406
#define PSYGFX__GL_RED                  0x1903
#define PSYGFX__GL_RG                   0x8227
#define PSYGFX__GL_RGBA                 0x1908
#define PSYGFX__GL_RED_INTEGER          0x8D94
#define PSYGFX__GL_R8                   0x8229
#define PSYGFX__GL_RG8                  0x822B
#define PSYGFX__GL_RGBA8                0x8058
#define PSYGFX__GL_R16F                 0x822D
#define PSYGFX__GL_RGBA16F              0x881A
#define PSYGFX__GL_R32F                 0x822E
#define PSYGFX__GL_RGBA32F              0x8814
#define PSYGFX__GL_R16UI                0x8234
#define PSYGFX__GL_ARRAY_BUFFER         0x8892
#define PSYGFX__GL_UNIFORM_BUFFER       0x8A11
#define PSYGFX__GL_DYNAMIC_DRAW         0x88E8
#define PSYGFX__GL_TRIANGLE_STRIP       0x0005
#define PSYGFX__GL_TRIANGLES            0x0004

typedef struct psygfx__gltex { psygfx__GLuint tex, fbo; int32_t w, h, format; } psygfx__gltex;
typedef struct psygfx__glbuf { psygfx__GLuint buf; psygfx__GLenum target; uint32_t bytes; } psygfx__glbuf;
typedef struct psygfx__glpipe { psygfx__GLuint prog, vao, vs, fs; int32_t blend, inst, pending; } psygfx__glpipe;

#define PSYGFX__GL_TEX  (PSYGFX_MAX_TEXTURES + 4)
#define PSYGFX__GL_BUF  (PSYGFX_MAX_BUFFERS + PSYGFX__UBO_RING + 1)
#define PSYGFX__GL_PIPE (PSYGFX_MAX_PIPELINES + PSYGFX__N_BUILTIN + 2)

typedef struct psygfx__gl {
    psygfx__glf    f;
    int32_t        w, h;
    psygfx__gltex  tex[PSYGFX__GL_TEX];
    psygfx__glbuf  buf[PSYGFX__GL_BUF];
    psygfx__glpipe pipe[PSYGFX__GL_PIPE];
    /* the state cache: what GL has bound, so apply() skips a call that
     * would not change anything; -1 = unknown */
    int64_t        c_prog, c_vao, c_blend, c_fbo, c_inst;
    int64_t        c_tex[4];
    int64_t        c_range_buf[2], c_range_off[2];
} psygfx__gl;

static int psygfx__gl_ext(const psygfx__gl* gl, const char* name) {
    psygfx__GLint n = 0, i;
    gl->f.GetIntegerv(PSYGFX__GL_NUM_EXTENSIONS, &n);
    for (i = 0; i < n; i++) {
        const char* e = (const char*)gl->f.GetStringi(PSYGFX__GL_EXTENSIONS, (psygfx__GLuint)i);
        if (e && strcmp(e, name) == 0) return 1;
    }
    return 0;
}

static void psygfx__gl_reset(void* c) {
    psygfx__gl* gl = (psygfx__gl*)c;
    int i;
    gl->c_prog = gl->c_vao = gl->c_blend = gl->c_fbo = gl->c_inst = -1;
    for (i = 0; i < 4; i++) gl->c_tex[i] = -1;
    for (i = 0; i < 2; i++) gl->c_range_buf[i] = gl->c_range_off[i] = -1;
}

static int psygfx__gl_open(void* c, const psygfx_backend_open* in, psygfx_backend_caps* caps, char* err, size_t cap) {
    psygfx__gl* gl = (psygfx__gl*)c;
    const char* missing = NULL;
    const unsigned char* r;
    memset(gl, 0, sizeof *gl);
    if (!in->gl_proc) { psygfx__set_error(err, cap, "psy_gfx: no GL loader"); return PSYGFX_ERR_ARG; }
#define PSYGFX__GL_LOAD(ret, name, args) \
    gl->f.name = (ret (PSYGFX__APIENTRY *) args)in->gl_proc(in->gl_ctx, "gl" #name); \
    if (!gl->f.name && !missing) missing = "gl" #name;
    PSYGFX__GL_FUNCS(PSYGFX__GL_LOAD)
#undef PSYGFX__GL_LOAD
    if (missing) {
        psygfx__set_error(err, cap, "psy_gfx: the GL ES 3.0 context has no %s", missing);
        return PSYGFX_ERR_GL;
    }
    gl->w = in->w; gl->h = in->h;
    caps->color_buffer_float = psygfx__gl_ext(gl, "GL_EXT_color_buffer_float") != 0;
    caps->float_blend = psygfx__gl_ext(gl, "GL_EXT_float_blend") != 0;
    caps->float_linear = psygfx__gl_ext(gl, "GL_OES_texture_float_linear") != 0;
    gl->f.GetIntegerv(PSYGFX__GL_UBO_ALIGN, &caps->ubo_align);
    gl->f.GetIntegerv(PSYGFX__GL_MAX_UBO_SIZE, &caps->max_ubo);
    r = gl->f.GetString(PSYGFX__GL_RENDERER);
    snprintf(caps->renderer, sizeof caps->renderer, "%s", r ? (const char*)r : "?");
    psygfx__gl_reset(gl);
    while (gl->f.GetError() != PSYGFX__GL_NO_ERROR) {}
    return PSYGFX_OK;
}

static void psygfx__gl_close(void* c) {
    psygfx__gl* gl = (psygfx__gl*)c;
    int i;
    if (!gl->f.DeleteTextures) return;
    for (i = 0; i < PSYGFX__GL_TEX; i++) {
        if (gl->tex[i].fbo) gl->f.DeleteFramebuffers(1, &gl->tex[i].fbo);
        if (gl->tex[i].tex) gl->f.DeleteTextures(1, &gl->tex[i].tex);
    }
    for (i = 0; i < PSYGFX__GL_BUF; i++) if (gl->buf[i].buf) gl->f.DeleteBuffers(1, &gl->buf[i].buf);
    for (i = 0; i < PSYGFX__GL_PIPE; i++) {
        if (gl->pipe[i].vs) gl->f.DeleteShader(gl->pipe[i].vs);
        if (gl->pipe[i].fs) gl->f.DeleteShader(gl->pipe[i].fs);
        if (gl->pipe[i].prog) gl->f.DeleteProgram(gl->pipe[i].prog);
        if (gl->pipe[i].vao) gl->f.DeleteVertexArrays(1, &gl->pipe[i].vao);
    }
    memset(gl, 0, sizeof *gl);
}

/* The link's status, the blocks' and samplers' bindings (ES 3.0 has no
 * layout(binding)); then the shaders go. */
static int psygfx__gl_pipeline_finish(void* c, uint32_t id, char* err, size_t cap) {
    psygfx__gl* gl = (psygfx__gl*)c;
    static const char* const samplers[] = { "psy_tex0", "psy_tex1", "psy_tex2", "psy_utex0" };
    psygfx__glpipe* p;
    psygfx__GLuint idx;
    psygfx__GLint ok = 0, loc;
    int i;
    if (id < 1 || id > PSYGFX__GL_PIPE) return PSYGFX_ERR_ARG;
    p = &gl->pipe[id - 1];
    if (!p->pending) return PSYGFX_OK;
    p->pending = 0;
    gl->f.GetProgramiv(p->prog, PSYGFX__GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[400];
        psygfx__GLsizei n = 0;
        psygfx__GLint sok = 1;
        log[0] = '\0';
        gl->f.GetShaderiv(p->fs, PSYGFX__GL_COMPILE_STATUS, &sok);
        if (!sok) {
            gl->f.GetShaderInfoLog(p->fs, (psygfx__GLsizei)sizeof log, &n, log);
            psygfx__set_error(err, cap, "psy_gfx: fragment shader: %s", log);
        } else {
            gl->f.GetShaderiv(p->vs, PSYGFX__GL_COMPILE_STATUS, &sok);
            if (!sok) {
                gl->f.GetShaderInfoLog(p->vs, (psygfx__GLsizei)sizeof log, &n, log);
                psygfx__set_error(err, cap, "psy_gfx: vertex shader: %s", log);
            } else {
                gl->f.GetProgramInfoLog(p->prog, (psygfx__GLsizei)sizeof log, &n, log);
                psygfx__set_error(err, cap, "psy_gfx: link: %s", log);
            }
        }
        gl->f.DeleteShader(p->vs);
        gl->f.DeleteShader(p->fs);
        gl->f.DeleteProgram(p->prog);
        if (p->vao) gl->f.DeleteVertexArrays(1, &p->vao);
        memset(p, 0, sizeof *p);
        return PSYGFX_ERR_GL;
    }
    gl->f.DeleteShader(p->vs);
    gl->f.DeleteShader(p->fs);
    p->vs = p->fs = 0;
    idx = gl->f.GetUniformBlockIndex(p->prog, "psy_frame_block");
    if (idx != PSYGFX__GL_INVALID_INDEX) gl->f.UniformBlockBinding(p->prog, idx, 0);
    idx = gl->f.GetUniformBlockIndex(p->prog, "psy_stim_block");
    if (idx != PSYGFX__GL_INVALID_INDEX) gl->f.UniformBlockBinding(p->prog, idx, 1);
    gl->f.UseProgram(p->prog);
    for (i = 0; i < 4; i++) {
        loc = gl->f.GetUniformLocation(p->prog, samplers[i]);
        if (loc >= 0) gl->f.Uniform1i(loc, i);
    }
    gl->c_prog = (int64_t)p->prog;
    return PSYGFX_OK;
}

/* Submits the compile and link. ANGLE compiles on worker threads, so the
 * open submits every program before it asks for any status (measured: 4
 * programs 3.0 s, against 7.3 s one after another). */
static int psygfx__gl_pipeline_make(void* c, const psygfx_pipeline_src* d, uint32_t* id, char* err, size_t cap) {
    psygfx__gl* gl = (psygfx__gl*)c;
    psygfx__glpipe* p;
    int slot;
    for (slot = 0; slot < PSYGFX__GL_PIPE && gl->pipe[slot].prog; slot++) {}
    if (slot == PSYGFX__GL_PIPE) return PSYGFX_ERR_FULL;
    p = &gl->pipe[slot];
    p->vs = gl->f.CreateShader(PSYGFX__GL_VERTEX_SHADER);
    gl->f.ShaderSource(p->vs, 1, &d->vs, NULL);
    gl->f.CompileShader(p->vs);
    p->fs = gl->f.CreateShader(PSYGFX__GL_FRAGMENT_SHADER);
    gl->f.ShaderSource(p->fs, 1, &d->fs, NULL);
    gl->f.CompileShader(p->fs);
    p->prog = gl->f.CreateProgram();
    gl->f.AttachShader(p->prog, p->vs);
    gl->f.AttachShader(p->prog, p->fs);
    gl->f.LinkProgram(p->prog);
    p->blend = d->blend;
    p->inst = d->instance_attr;
    p->pending = 1;
    gl->f.GenVertexArrays(1, &p->vao);
    *id = (uint32_t)slot + 1;
    if (d->deferred) return PSYGFX_OK;
    {
        int rc = psygfx__gl_pipeline_finish(c, *id, err, cap);
        if (rc < 0) *id = 0;
        return rc;
    }
}

static void psygfx__gl_pipeline_free(void* c, uint32_t id) {
    psygfx__gl* gl = (psygfx__gl*)c;
    psygfx__glpipe* p;
    if (id < 1 || id > PSYGFX__GL_PIPE) return;
    p = &gl->pipe[id - 1];
    if (p->vs) gl->f.DeleteShader(p->vs);
    if (p->fs) gl->f.DeleteShader(p->fs);
    if (p->prog) gl->f.DeleteProgram(p->prog);
    if (p->vao) gl->f.DeleteVertexArrays(1, &p->vao);
    memset(p, 0, sizeof *p);
    psygfx__gl_reset(gl);
}

/* Internal format, format, type for each psygfx_format. */
static void psygfx__gl_fmt(psygfx_format f, psygfx__GLenum* ifmt, psygfx__GLenum* fmt, psygfx__GLenum* type) {
    switch (f) {
    case PSYGFX_R8:      *ifmt = PSYGFX__GL_R8;      *fmt = PSYGFX__GL_RED;  *type = PSYGFX__GL_UNSIGNED_BYTE; break;
    case PSYGFX_RG8:     *ifmt = PSYGFX__GL_RG8;     *fmt = PSYGFX__GL_RG;   *type = PSYGFX__GL_UNSIGNED_BYTE; break;
    case PSYGFX_RGBA8:   *ifmt = PSYGFX__GL_RGBA8;   *fmt = PSYGFX__GL_RGBA; *type = PSYGFX__GL_UNSIGNED_BYTE; break;
    case PSYGFX_R16F:    *ifmt = PSYGFX__GL_R16F;    *fmt = PSYGFX__GL_RED;  *type = PSYGFX__GL_FLOAT; break;
    case PSYGFX_RGBA16F: *ifmt = PSYGFX__GL_RGBA16F; *fmt = PSYGFX__GL_RGBA; *type = PSYGFX__GL_FLOAT; break;
    case PSYGFX_R32F:    *ifmt = PSYGFX__GL_R32F;    *fmt = PSYGFX__GL_RED;  *type = PSYGFX__GL_FLOAT; break;
    case PSYGFX_RGBA32F: *ifmt = PSYGFX__GL_RGBA32F; *fmt = PSYGFX__GL_RGBA; *type = PSYGFX__GL_FLOAT; break;
    case PSYGFX_R16UI:   *ifmt = PSYGFX__GL_R16UI;   *fmt = PSYGFX__GL_RED_INTEGER; *type = PSYGFX__GL_UNSIGNED_SHORT; break;
    default:             *ifmt = *fmt = *type = 0; break;
    }
}

static void psygfx__gl_bind_tex(psygfx__gl* gl, int unit, psygfx__GLuint tex) {
    gl->f.ActiveTexture(PSYGFX__GL_TEXTURE0 + (psygfx__GLenum)unit);
    gl->f.BindTexture(PSYGFX__GL_TEXTURE_2D, tex);
    gl->c_tex[unit] = (int64_t)tex;
}

static int psygfx__gl_upload(psygfx__gl* gl, const psygfx__gltex* t, int x, int y, int w, int h,
                             const void* data, size_t stride) {
    psygfx__GLenum ifmt, fmt, type;
    int bpp = psygfx__texel_bytes((psygfx_format)t->format);
    psygfx__gl_fmt((psygfx_format)t->format, &ifmt, &fmt, &type);
    psygfx__gl_bind_tex(gl, 0, t->tex);
    gl->f.PixelStorei(PSYGFX__GL_UNPACK_ALIGNMENT, 1);
    gl->f.PixelStorei(PSYGFX__GL_UNPACK_ROW_LENGTH, stride ? (psygfx__GLint)(stride / (size_t)bpp) : 0);
    gl->f.TexSubImage2D(PSYGFX__GL_TEXTURE_2D, 0, x, y, w, h, fmt, type, data);
    gl->f.PixelStorei(PSYGFX__GL_UNPACK_ROW_LENGTH, 0);
    return PSYGFX_OK;
}

static int psygfx__gl_texture_make(void* c, const psygfx_texture_src* d, uint32_t* id) {
    psygfx__gl* gl = (psygfx__gl*)c;
    psygfx__GLenum ifmt, fmt, type;
    psygfx__gltex* t;
    int slot;
    psygfx__GLint filt;
    for (slot = 0; slot < PSYGFX__GL_TEX && gl->tex[slot].tex; slot++) {}
    if (slot == PSYGFX__GL_TEX) return PSYGFX_ERR_FULL;
    psygfx__gl_fmt(d->format, &ifmt, &fmt, &type);
    if (!ifmt) return PSYGFX_ERR_ARG;
    t = &gl->tex[slot];
    gl->f.GenTextures(1, &t->tex);
    psygfx__gl_bind_tex(gl, 0, t->tex);
    gl->f.TexStorage2D(PSYGFX__GL_TEXTURE_2D, 1, ifmt, d->w, d->h);
    filt = (d->linear && d->format != PSYGFX_R16UI) ? PSYGFX__GL_LINEAR : PSYGFX__GL_NEAREST;
    gl->f.TexParameteri(PSYGFX__GL_TEXTURE_2D, PSYGFX__GL_TEXTURE_MIN_FILTER, filt);
    gl->f.TexParameteri(PSYGFX__GL_TEXTURE_2D, PSYGFX__GL_TEXTURE_MAG_FILTER, filt);
    gl->f.TexParameteri(PSYGFX__GL_TEXTURE_2D, PSYGFX__GL_TEXTURE_WRAP_S, PSYGFX__GL_CLAMP_TO_EDGE);
    gl->f.TexParameteri(PSYGFX__GL_TEXTURE_2D, PSYGFX__GL_TEXTURE_WRAP_T, PSYGFX__GL_CLAMP_TO_EDGE);
    t->w = d->w; t->h = d->h; t->format = d->format;
    if (d->data) psygfx__gl_upload(gl, t, 0, 0, d->w, d->h, d->data, d->stride);
    if (d->target) {
        gl->f.GenFramebuffers(1, &t->fbo);
        gl->f.BindFramebuffer(PSYGFX__GL_FRAMEBUFFER, t->fbo);
        gl->f.FramebufferTexture2D(PSYGFX__GL_FRAMEBUFFER, PSYGFX__GL_COLOR_ATTACHMENT0, PSYGFX__GL_TEXTURE_2D, t->tex, 0);
        gl->c_fbo = (int64_t)t->fbo;
        if (gl->f.CheckFramebufferStatus(PSYGFX__GL_FRAMEBUFFER) != PSYGFX__GL_FRAMEBUFFER_COMPLETE) {
            gl->f.DeleteFramebuffers(1, &t->fbo);
            gl->f.DeleteTextures(1, &t->tex);
            memset(t, 0, sizeof *t);
            psygfx__gl_reset(gl);
            return PSYGFX_ERR_GL;
        }
    }
    if (gl->f.GetError() != PSYGFX__GL_NO_ERROR) {
        if (t->fbo) gl->f.DeleteFramebuffers(1, &t->fbo);
        gl->f.DeleteTextures(1, &t->tex);
        memset(t, 0, sizeof *t);
        psygfx__gl_reset(gl);
        return PSYGFX_ERR_GL;
    }
    *id = (uint32_t)slot + 1;
    return PSYGFX_OK;
}

static int psygfx__gl_texture_update(void* c, uint32_t id, int x, int y, int w, int h, const void* data, size_t stride) {
    psygfx__gl* gl = (psygfx__gl*)c;
    if (id < 1 || id > PSYGFX__GL_TEX || !gl->tex[id - 1].tex) return PSYGFX_ERR_ARG;
    return psygfx__gl_upload(gl, &gl->tex[id - 1], x, y, w, h, data, stride);
}

static void psygfx__gl_texture_free(void* c, uint32_t id) {
    psygfx__gl* gl = (psygfx__gl*)c;
    psygfx__gltex* t;
    if (id < 1 || id > PSYGFX__GL_TEX) return;
    t = &gl->tex[id - 1];
    if (t->fbo) gl->f.DeleteFramebuffers(1, &t->fbo);
    if (t->tex) gl->f.DeleteTextures(1, &t->tex);
    memset(t, 0, sizeof *t);
    psygfx__gl_reset(gl);
}

static int psygfx__gl_buffer_make(void* c, int kind, size_t bytes, uint32_t* id) {
    psygfx__gl* gl = (psygfx__gl*)c;
    psygfx__glbuf* b;
    int slot;
    for (slot = 0; slot < PSYGFX__GL_BUF && gl->buf[slot].buf; slot++) {}
    if (slot == PSYGFX__GL_BUF) return PSYGFX_ERR_FULL;
    b = &gl->buf[slot];
    b->target = kind == PSYGFX_BUFFER_UNIFORM ? PSYGFX__GL_UNIFORM_BUFFER : PSYGFX__GL_ARRAY_BUFFER;
    b->bytes = (uint32_t)bytes;
    gl->f.GenBuffers(1, &b->buf);
    gl->f.BindBuffer(b->target, b->buf);
    gl->f.BufferData(b->target, (psygfx__GLsizeiptr)bytes, NULL, PSYGFX__GL_DYNAMIC_DRAW);
    if (gl->f.GetError() != PSYGFX__GL_NO_ERROR) {
        gl->f.DeleteBuffers(1, &b->buf);
        memset(b, 0, sizeof *b);
        return PSYGFX_ERR_GL;
    }
    *id = (uint32_t)slot + 1;
    return PSYGFX_OK;
}

static int psygfx__gl_buffer_update(void* c, uint32_t id, size_t off, const void* data, size_t n) {
    psygfx__gl* gl = (psygfx__gl*)c;
    psygfx__glbuf* b;
    if (id < 1 || id > PSYGFX__GL_BUF || !gl->buf[id - 1].buf) return PSYGFX_ERR_ARG;
    b = &gl->buf[id - 1];
    if (off + n > b->bytes) return PSYGFX_ERR_ARG;
    gl->f.BindBuffer(b->target, b->buf);
    gl->f.BufferSubData(b->target, (psygfx__GLintptr)off, (psygfx__GLsizeiptr)n, data);
    return PSYGFX_OK;
}

static void psygfx__gl_buffer_free(void* c, uint32_t id) {
    psygfx__gl* gl = (psygfx__gl*)c;
    if (id < 1 || id > PSYGFX__GL_BUF || !gl->buf[id - 1].buf) return;
    gl->f.DeleteBuffers(1, &gl->buf[id - 1].buf);
    memset(&gl->buf[id - 1], 0, sizeof gl->buf[0]);
    psygfx__gl_reset(gl);
}

static void psygfx__gl_pass_begin(void* c, uint32_t target, int w, int h, const float* clear) {
    psygfx__gl* gl = (psygfx__gl*)c;
    psygfx__GLuint fbo = (target >= 1 && target <= PSYGFX__GL_TEX) ? gl->tex[target - 1].fbo : 0;
    if (gl->c_fbo != (int64_t)fbo) {
        gl->f.BindFramebuffer(PSYGFX__GL_FRAMEBUFFER, fbo);
        gl->c_fbo = (int64_t)fbo;
    }
    gl->f.Viewport(0, 0, w, h);
    if (clear) {
        gl->f.ColorMask(1, 1, 1, 1);
        gl->f.ClearColor(clear[0], clear[1], clear[2], clear[3]);
        gl->f.Clear(PSYGFX__GL_COLOR_BUFFER_BIT);
    }
}

static void psygfx__gl_apply(void* c, const psygfx_bindings* b) {
    psygfx__gl* gl = (psygfx__gl*)c;
    psygfx__glpipe* p;
    int i;
    if (b->pipeline < 1 || b->pipeline > PSYGFX__GL_PIPE) return;
    p = &gl->pipe[b->pipeline - 1];
    if (gl->c_prog != (int64_t)p->prog) { gl->f.UseProgram(p->prog); gl->c_prog = (int64_t)p->prog; }
    if (gl->c_vao != (int64_t)p->vao) { gl->f.BindVertexArray(p->vao); gl->c_vao = (int64_t)p->vao; gl->c_inst = -1; }
    {
        int32_t bl = b->blend ? b->blend : p->blend;
        if (gl->c_blend != bl) {
            if (bl == PSYGFX_BLEND_NONE) {
                gl->f.Disable(PSYGFX__GL_BLEND);
            } else {
                gl->f.Enable(PSYGFX__GL_BLEND);
                gl->f.BlendEquation(PSYGFX__GL_FUNC_ADD);
                /* OVER accumulates premultiplied alpha, which a target drawn
                 * as an image needs; ADD and MULTIPLY leave alpha alone: an
                 * increment and a filter cover nothing. The scene's alpha is
                 * never read. */
                if (bl == PSYGFX_BLEND_ADD)
                    gl->f.BlendFuncSeparate(PSYGFX__GL_ONE, PSYGFX__GL_ONE, PSYGFX__GL_ZERO, PSYGFX__GL_ONE);
                else if (bl == PSYGFX_BLEND_MULTIPLY)
                    gl->f.BlendFuncSeparate(PSYGFX__GL_DST_COLOR, PSYGFX__GL_ONE_MINUS_SRC_ALPHA,
                                            PSYGFX__GL_ZERO, PSYGFX__GL_ONE);
                else
                    gl->f.BlendFuncSeparate(PSYGFX__GL_ONE, PSYGFX__GL_ONE_MINUS_SRC_ALPHA,
                                            PSYGFX__GL_ONE, PSYGFX__GL_ONE_MINUS_SRC_ALPHA);
            }
            gl->c_blend = bl;
        }
    }
    if (b->ubo >= 1 && b->ubo <= PSYGFX__GL_BUF) {
        psygfx__GLuint ub = gl->buf[b->ubo - 1].buf;
        if (b->frame_size && (gl->c_range_buf[0] != (int64_t)ub || gl->c_range_off[0] != (int64_t)b->frame_off)) {
            gl->f.BindBufferRange(PSYGFX__GL_UNIFORM_BUFFER, 0, ub, (psygfx__GLintptr)b->frame_off, (psygfx__GLsizeiptr)b->frame_size);
            gl->c_range_buf[0] = (int64_t)ub; gl->c_range_off[0] = (int64_t)b->frame_off;
        }
        if (b->stim_size && (gl->c_range_buf[1] != (int64_t)ub || gl->c_range_off[1] != (int64_t)b->stim_off)) {
            gl->f.BindBufferRange(PSYGFX__GL_UNIFORM_BUFFER, 1, ub, (psygfx__GLintptr)b->stim_off, (psygfx__GLsizeiptr)b->stim_size);
            gl->c_range_buf[1] = (int64_t)ub; gl->c_range_off[1] = (int64_t)b->stim_off;
        }
    }
    for (i = 0; i < 4; i++) {
        psygfx__GLuint t = (b->tex[i] >= 1 && b->tex[i] <= PSYGFX__GL_TEX) ? gl->tex[b->tex[i] - 1].tex : 0;
        if (t && gl->c_tex[i] != (int64_t)t) psygfx__gl_bind_tex(gl, i, t);
    }
    if (p->inst && b->instances >= 1 && b->instances <= PSYGFX__GL_BUF && gl->c_inst != (int64_t)b->instances) {
        gl->f.BindBuffer(PSYGFX__GL_ARRAY_BUFFER, gl->buf[b->instances - 1].buf);
        gl->f.EnableVertexAttribArray(0);
        if (p->inst == 2) {   /* glyphs: two vec4 per instance */
            gl->f.VertexAttribPointer(0, 4, PSYGFX__GL_FLOAT, 0, 32, NULL);
            gl->f.EnableVertexAttribArray(1);
            gl->f.VertexAttribPointer(1, 4, PSYGFX__GL_FLOAT, 0, 32, (const void*)(size_t)16);
            gl->f.VertexAttribDivisor(1, 1);
        } else {
            gl->f.VertexAttribPointer(0, 2, PSYGFX__GL_FLOAT, 0, 8, NULL);
        }
        gl->f.VertexAttribDivisor(0, 1);
        gl->c_inst = (int64_t)b->instances;
    }
}

static void psygfx__gl_draw(void* c, int vertices, int instances) {
    psygfx__gl* gl = (psygfx__gl*)c;
    gl->f.DrawArraysInstanced(vertices == 3 ? PSYGFX__GL_TRIANGLES : PSYGFX__GL_TRIANGLE_STRIP, 0, vertices, instances);
}

static void psygfx__gl_pass_end(void* c) { (void)c; }

/* psy_screen's presenter flushes inside the present; a headless caller reads
 * back, which waits anyway. */
static void psygfx__gl_present(void* c) { (void)c; }

static int psygfx__gl_read(void* c, uint32_t target, int x, int y, int w, int h, int format, void* out) {
    psygfx__gl* gl = (psygfx__gl*)c;
    psygfx__GLuint fbo = (target >= 1 && target <= PSYGFX__GL_TEX) ? gl->tex[target - 1].fbo : 0;
    gl->f.BindFramebuffer(PSYGFX__GL_FRAMEBUFFER, fbo);
    gl->c_fbo = (int64_t)fbo;
    gl->f.PixelStorei(PSYGFX__GL_PACK_ALIGNMENT, 1);
    gl->f.ReadPixels(x, y, w, h, PSYGFX__GL_RGBA, format == PSYGFX_READ_FLOAT ? PSYGFX__GL_FLOAT : PSYGFX__GL_UNSIGNED_BYTE, out);
    return gl->f.GetError() == PSYGFX__GL_NO_ERROR ? PSYGFX_OK : PSYGFX_ERR_GL;
}

static const psygfx_backend psygfx__gl_backend = {
    PSYGFX_BACKEND_VERSION, "gles3",
    psygfx__gl_open, psygfx__gl_close, psygfx__gl_pipeline_make, psygfx__gl_pipeline_free,
    psygfx__gl_texture_make, psygfx__gl_texture_update, psygfx__gl_texture_free,
    psygfx__gl_buffer_make, psygfx__gl_buffer_update, psygfx__gl_buffer_free,
    psygfx__gl_pass_begin, psygfx__gl_apply, psygfx__gl_draw, psygfx__gl_pass_end,
    psygfx__gl_present, psygfx__gl_read, psygfx__gl_reset, psygfx__gl_pipeline_finish
};

/* --- null backend: a screen without GL (SIM), and the interface's second
 *     implementation ---------------------------------------------------------- */

typedef struct psygfx__null { uint32_t next; } psygfx__null;

static int psygfx__null_open(void* c, const psygfx_backend_open* in, psygfx_backend_caps* caps, char* err, size_t cap) {
    (void)in; (void)err; (void)cap;
    ((psygfx__null*)c)->next = 1;
    caps->color_buffer_float = caps->float_blend = caps->float_linear = true;
    caps->ubo_align = 256;
    caps->max_ubo = 65536;
    snprintf(caps->renderer, sizeof caps->renderer, "null (nothing is drawn)");
    return PSYGFX_OK;
}
static void psygfx__null_close(void* c) { (void)c; }
static int psygfx__null_pipeline_make(void* c, const psygfx_pipeline_src* d, uint32_t* id, char* err, size_t cap) {
    (void)d; (void)err; (void)cap; *id = ((psygfx__null*)c)->next++; return PSYGFX_OK;
}
static void psygfx__null_free(void* c, uint32_t id) { (void)c; (void)id; }
static int psygfx__null_texture_make(void* c, const psygfx_texture_src* d, uint32_t* id) {
    (void)d; *id = ((psygfx__null*)c)->next++; return PSYGFX_OK;
}
static int psygfx__null_texture_update(void* c, uint32_t id, int x, int y, int w, int h, const void* data, size_t stride) {
    (void)c; (void)id; (void)x; (void)y; (void)w; (void)h; (void)data; (void)stride; return PSYGFX_OK;
}
static int psygfx__null_buffer_make(void* c, int kind, size_t bytes, uint32_t* id) {
    (void)kind; (void)bytes; *id = ((psygfx__null*)c)->next++; return PSYGFX_OK;
}
static int psygfx__null_buffer_update(void* c, uint32_t id, size_t off, const void* data, size_t n) {
    (void)c; (void)id; (void)off; (void)data; (void)n; return PSYGFX_OK;
}
static void psygfx__null_pass_begin(void* c, uint32_t t, int w, int h, const float* clear) {
    (void)c; (void)t; (void)w; (void)h; (void)clear;
}
static void psygfx__null_apply(void* c, const psygfx_bindings* b) { (void)c; (void)b; }
static void psygfx__null_draw(void* c, int v, int n) { (void)c; (void)v; (void)n; }
static void psygfx__null_void(void* c) { (void)c; }
static int psygfx__null_read(void* c, uint32_t t, int x, int y, int w, int h, int f, void* out) {
    (void)c; (void)t; (void)x; (void)y; (void)w; (void)h; (void)f; (void)out; return PSYGFX_ERR_NOT_IMPLEMENTED;
}

static const psygfx_backend psygfx__null_backend = {
    PSYGFX_BACKEND_VERSION, "null",
    psygfx__null_open, psygfx__null_close, psygfx__null_pipeline_make, psygfx__null_free,
    psygfx__null_texture_make, psygfx__null_texture_update, psygfx__null_free,
    psygfx__null_buffer_make, psygfx__null_buffer_update, psygfx__null_free,
    psygfx__null_pass_begin, psygfx__null_apply, psygfx__null_draw, psygfx__null_void,
    psygfx__null_void, psygfx__null_read, psygfx__null_void, NULL
};

typedef char psygfx__gl_fits[sizeof(psygfx__gl) <= sizeof(((psygfx_gfx*)0)->backend_mem) ? 1 : -1];

/* --- shaders --------------------------------------------------------------- */

/* Stimuli per instanced draw, as vec4s of the stim block (16 each). Private:
 * the bench overrides it to measure. */
#ifndef PSYGFX__BATCH_VEC4
#define PSYGFX__BATCH_VEC4 256
#endif
#define PSYGFX__BATCH (PSYGFX__BATCH_VEC4 / 16)
#define PSYGFX__STR2(x) #x
#define PSYGFX__STR(x) PSYGFX__STR2(x)

/* The contract every stimulus program is built on: the frame block, the
 * stim blocks, the texture slots and the accessors. Built-in stimuli are
 * bodies against it, the same as a user's. */
static const char psygfx__glsl_common[] =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "precision highp sampler2D;\n"
    "precision highp usampler2D;\n"
    "layout(std140) uniform psy_frame_block {\n"
    "    vec4  psy_target;\n"     /* w, h, 1/w, 1/h                         */
    "    vec4  psy_time;\n"       /* onset s since open, period s, index, eye */
    "    uvec4 psy_seed;\n"       /* index low, index high, seed, vblank low */
    "    ivec4 psy_outp;\n"       /* output stage: lut n, max code, dither, flip h */
    "    vec4  psy_axes;\n"       /* x: the local y axis sign in the pass's GL
                                 * axes (psygfx__place)                     */
    "};\n"
    /* PSYGFX__BATCH stimuli of 16 vec4: xf, size, look, color, dir, edge,
     * shape, misc, p[8]. ANGLE's compile time through D3D11 grows with this
     * array's length (docs/psy_gfx.md), so the batch is short. */
    "layout(std140) uniform psy_stim_block { vec4 psy_v[" PSYGFX__STR(PSYGFX__BATCH_VEC4) "]; };\n";

static const char psygfx__glsl_access[] =
    "#define psy_xf    psy_v[psy_i * 16]\n"
    "#define psy_size  psy_v[psy_i * 16 + 1]\n"
    "#define psy_look  psy_v[psy_i * 16 + 2]\n"
    "#define psy_color psy_v[psy_i * 16 + 3]\n"
    "#define psy_dir   psy_v[psy_i * 16 + 4]\n"
    "#define psy_edge  psy_v[psy_i * 16 + 5]\n"
    "#define psy_shape psy_v[psy_i * 16 + 6]\n"
    "#define psy_misc  psy_v[psy_i * 16 + 7]\n"
    "#define psy_param(k) (psy_v[psy_i * 16 + 8 + (k) / 4][(k) % 4])\n";

/* Quads: one stimulus per instance, from the range bound at its first block.
 * Dots: one dot per instance, the stimulus is block 0 of the range. The
 * fragment shader gets the center, not interpolated coordinates: a
 * rasterizer snaps vertices to its subpixel grid, and interpolated local
 * coordinates then ran up to 2e-2 px off on SwiftShader (docs/psy_gfx.md). */
static const char psygfx__glsl_vs_body[] =
    "flat out vec2 psy_c;\n"
    "flat out int psy_i;\n"
    "#ifdef PSY_GLYPHS\n"
    "layout(location = 0) in vec4 psy_g0;\n"     /* x, y units; atlas x, y */
    "layout(location = 1) in vec4 psy_g1;\n"     /* atlas w, h            */
    "flat out vec2 psy_gh;\n"
    "flat out vec4 psy_gr;\n"
    "#endif\n"
    "#ifdef PSY_DOTS\n"
    "layout(location = 0) in vec2 psy_dot;\n"
    "float psy_field_(vec2 q, vec4 f) {\n"     /* hx, hy, shape */
    "    if (f.z == 0.0) { vec2 d = abs(q) - f.xy; return max(d.x, d.y); }\n"
    "    if (f.z == 1.0) return length(q) - f.x;\n"
    "    return -1.0;\n"
    "}\n"
    "#endif\n"
    "void main() {\n"
    "#if defined(PSY_DOTS) || defined(PSY_GLYPHS)\n"
    "    int i = 0;\n"
    "#else\n"
    "    int i = gl_InstanceID;\n"
    "#endif\n"
    "    vec4 xf = psy_v[i * 16];\n"
    "    vec4 sz = psy_v[i * 16 + 1];\n"
    "    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1)) * 2.0 - 1.0;\n"
    "    vec2 ax = xf.zw, ay = psy_axes.x * vec2(-xf.w, xf.z);\n"
    "    vec2 local = c * (sz.xy + sz.ww);\n"
    "    vec2 o = vec2(0.0);\n"
    "#ifdef PSY_DOTS\n"
    "    o = psy_dot * sz.z;\n"
    "    if (psy_field_(o, psy_v[i * 16 + 6]) > 0.0) local = vec2(1e9);\n"
    "#endif\n"
    /* a glyph's box from its atlas rectangle at the run's px per texel
     * (misc.y), from the run box's top-left */
    "#ifdef PSY_GLYPHS\n"
    "    vec2 gh = 0.5 * psy_g1.xy * psy_v[7].y;\n"
    "    o = psy_g0.xy * sz.z + gh - sz.xy;\n"
    "    local = c * (gh + sz.ww);\n"
    "    psy_gh = gh;\n"
    "    psy_gr = vec4(psy_g0.zw, psy_g1.xy);\n"
    "#endif\n"
    "    psy_c = xf.xy + o.x * ax + o.y * ay;\n"
    "    vec2 w = psy_c + local.x * ax + local.y * ay;\n"
    "    gl_Position = vec4(w * psy_target.zw * 2.0 - 1.0, 0.0, 1.0);\n"
    "    psy_i = i;\n"
    "}\n";

/* The fragment library: hash, SDFs, strokes, edge profiles, the aperture. */
static const char psygfx__glsl_fs_lib[] =
    "uniform sampler2D psy_tex0;\n"
    "uniform sampler2D psy_tex1;\n"
    "uniform sampler2D psy_tex2;\n"
    "uniform usampler2D psy_utex0;\n"
    "flat in vec2 psy_c;\n"
    "flat in int psy_i;\n"
    "#ifdef PSY_GLYPHS\n"
    "flat in vec2 psy_gh;\n"
    "flat in vec4 psy_gr;\n"
    "#endif\n"
    "out vec4 psy_out;\n"
    "vec2 psy_p;\n"
    "const float psy_PI = 3.14159265358979;\n"
    "uint psy_hash(uint v) {\n"
    "    uint s = v * 747796405u + 2891336453u;\n"
    "    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;\n"
    "    return (w >> 22u) ^ w;\n"
    "}\n"
    /* Bilinear by hand from four texelFetch, the indices clamped to the
     * rectangle sr (texels): no texel outside it can contribute, and the
     * weights are float on every renderer, not the sampler's 8-bit ones. */
    "vec4 psy_bilinear(sampler2D t, vec2 x, vec4 sr) {\n"
    "    vec2 f = x - 0.5, i0 = floor(f), w = f - i0;\n"
    "    ivec2 lo = ivec2(sr.xy), hi = ivec2(sr.xy + sr.zw) - 1;\n"
    "    ivec2 a = clamp(ivec2(i0), lo, hi), b = clamp(ivec2(i0) + 1, lo, hi);\n"
    "    vec4 c00 = texelFetch(t, a, 0), c10 = texelFetch(t, ivec2(b.x, a.y), 0);\n"
    "    vec4 c01 = texelFetch(t, ivec2(a.x, b.y), 0), c11 = texelFetch(t, b, 0);\n"
    "    return mix(mix(c00, c10, w.x), mix(c01, c11, w.x), w.y);\n"
    "}\n"
    "#ifndef PSY_VECTOR\n"
    "float psy_sd_box(vec2 p, vec2 b) {\n"
    "    vec2 d = abs(p) - b;\n"
    "    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);\n"
    "}\n"
    /* A box with sharp corners: the max of its axis distances. Its corners
     * turn 90 degrees, so their miter ratio is sqrt 2: below that limit a
     * bevel plane per corner at the level a cuts them (as on a polygon). */
    "float psy_mbox_(vec2 p, vec2 b, float L, float a) {\n"
    "    vec2 d = abs(p) - b;\n"
    "    float m = max(d.x, d.y);\n"
    "    return L < 1.41421356 ? max(m, (d.x + d.y) * 0.70710678 + a * 0.29289322) : m;\n"
    "}\n"
    "vec2 psy_vtx_(int k) { vec4 v = psy_v[psy_i * 16 + 8 + k / 2]; return (k % 2 == 0) ? v.xy : v.zw; }\n"
    "float psy_sd_polygon(vec2 p, int n) {\n"
    "    vec2 v0 = psy_vtx_(0);\n"
    "    float d = dot(p - v0, p - v0), s = 1.0;\n"
    "    for (int k = 0, j = n - 1; k < n; j = k, k++) {\n"
    "        vec2 vi = psy_vtx_(k), vj = psy_vtx_(j), e = vj - vi, w = p - vi;\n"
    "        vec2 b = w - e * clamp(dot(w, e) / dot(e, e), 0.0, 1.0);\n"
    "        d = min(d, dot(b, b));\n"
    "        bvec3 c = bvec3(p.y >= vi.y, p.y < vj.y, e.x * w.y > e.y * w.x);\n"
    "        if (all(c) || all(not(c))) s = -s;\n"
    "    }\n"
    "    return s * sqrt(d);\n"
    "}\n"
    /* A convex polygon with sharp corners: the max of the edges' line
     * distances (unit gradients, so the edge profile keeps its width), and
     * past the miter limit L a bevel plane per corner whose level is the
     * bevel of the offset a. Only the bevels depend on the level, so one
     * loop over the edges gives both of a stroke's levels (a.x, a.y), not
     * two. o: the orientation's sign. */
    "vec2 psy_sd_polygon_miter(vec2 p, int n, float o, float L, vec2 a) {\n"
    "    vec2 v = psy_vtx_(0), e = v - psy_vtx_(n - 1);\n"
    "    vec2 n1 = o * normalize(vec2(e.y, -e.x));\n"
    "    float d = -1e30;\n"
    "    vec2 bv = vec2(-1e30);\n"
    "    for (int k = 0; k < n; k++) {\n"
    "        vec2 vn = psy_vtx_(k + 1 < n ? k + 1 : 0), e2 = vn - v;\n"
    "        vec2 n2 = o * normalize(vec2(e2.y, -e2.x)), w = p - v;\n"
    "        d = max(d, dot(w, n2));\n"
    "        vec2 b = normalize(n1 + n2);\n"
    "        float c = dot(b, n1);\n"
    "        if (c * L < 1.0) bv = max(bv, dot(w, b) + a * (1.0 - c));\n"
    "        v = vn; n1 = n2;\n"
    "    }\n"
    "    return max(vec2(d), bv);\n"
    "}\n"
    "#endif\n";

/* The library's second half (ISO C caps a string literal at 4095 bytes). */
static const char psygfx__glsl_fs_lib2[] =
    /* edge.x = shape: 0 rect, 1 circle, 2 annulus, 3 line, 4 polygon,
     * 5 cross, 6 none, 7 mask; shape = (p0, p1, n, stroke half width) in px;
     * dir.a = the miter limit (0: round), its sign the polygon's orientation.
     * a: the two offset levels asked for, which only a bevel needs. Both
     * levels come from one call, and the library has one call site: ANGLE's
     * D3D11 compiler inlines an SDF this size whole at every call site. */
    "#ifndef PSY_VECTOR\n"
    "vec2 psy_sdf2_(vec2 p, vec2 a) {\n"
    "    int k = int(psy_edge.x);\n"
    "    vec2 hs = psy_size.xy;\n"
    "    vec4 sp = psy_shape;\n"
    "    float ml = psy_dir.a;\n"
    "    if (k == 0) {\n"
    "        if (ml != 0.0) return vec2(psy_mbox_(p, hs, ml, a.x), psy_mbox_(p, hs, ml, a.y));\n"
    "        float r = min(sp.y, min(hs.x, hs.y));\n"
    "        return vec2(psy_sd_box(p, hs - r) - r);\n"
    "    }\n"
    "    if (k == 1) return vec2(length(p) - hs.x);\n"
    "    if (k == 2) return vec2(abs(length(p) - 0.5 * (hs.x + sp.x)) - 0.5 * (hs.x - sp.x));\n"
    "    if (k == 3) return vec2(length(vec2(max(abs(p.x) - (hs.x - hs.y), 0.0), p.y)) - hs.y);\n"
    "    if (k == 4) return ml != 0.0 ? psy_sd_polygon_miter(p, int(sp.z), sign(ml), abs(ml), a)\n"
    "                                 : vec2(psy_sd_polygon(p, int(sp.z)));\n"
    "    if (k == 5) {\n"
    "        vec2 b1 = vec2(hs.x, 0.5 * sp.x), b2 = vec2(0.5 * sp.x, hs.y);\n"
    "        if (ml != 0.0) return vec2(min(psy_mbox_(p, b1, ml, a.x), psy_mbox_(p, b2, ml, a.x)),\n"
    "                                   min(psy_mbox_(p, b1, ml, a.y), psy_mbox_(p, b2, ml, a.y)));\n"
    "        return vec2(min(psy_sd_box(p, b1), psy_sd_box(p, b2)));\n"
    "    }\n"
    "    if (k == 7) {\n"
    "#ifdef PSY_GLYPHS\n"
    "        hs = psy_gh;\n"
    "        vec4 mr = psy_gr;\n"
    "#else\n"
    "        vec4 mr = psy_v[psy_i * 16 + 15];\n"
    "#endif\n"
    "        vec2 r = vec2(p.x + hs.x, hs.y + p.y) / (2.0 * hs);\n"
    "        vec4 t = psy_bilinear(psy_tex1, mr.xy + r * mr.zw, mr);\n"
    "        if (sp.x < 1.5) return vec2(t.r * (2.0 * hs.x / mr.z));\n"
    "        float m = sp.z > 0.5 ? t.a : max(min(t.r, t.g), min(max(t.r, t.g), t.b));\n"
    "        return vec2((0.5 - m) * sp.y * (2.0 * hs.x / mr.z));\n"
    "    }\n"
    "    return vec2(-1e30);\n"
    "}\n"
    "float psy_sdf_at(vec2 p, float a) { return psy_sdf2_(p, vec2(a)).x; }\n"
    "float psy_sdf(vec2 p) { return psy_sdf2_(p, vec2(0.0)).x; }\n"
    "#endif\n"
    /* the fragment's point in the stimulus frame, from its exact center */
    "vec2 psy_local_() {\n"
    "    vec2 d = gl_FragCoord.xy - psy_c;\n"
    "    vec2 ax = psy_xf.zw, ay = psy_axes.x * vec2(-psy_xf.w, psy_xf.z);\n"
    "    return vec2(dot(d, ax), dot(d, ay));\n"
    "}\n"
    /* Abramowitz and Stegun 7.1.26, abs error below 1.5e-7 */
    "float psy_erfc(float x) {\n"
    "    float z = abs(x);\n"
    "    float t = 1.0 / (1.0 + 0.3275911 * z);\n"
    "    float y = t * (0.254829592 + t * (-0.284496736 + t * (1.421413741 + t * (-1.453152027 + t * 1.061405429)))) * exp(-z * z);\n"
    "    return x >= 0.0 ? y : 2.0 - y;\n"
    "}\n"
    "float psy_coverage(float d) {\n"
    "    int prof = int(psy_edge.y);\n"
    "    float w = psy_edge.z;\n"
    "    if (prof == 0 || w <= 0.0) return d <= 0.0 ? 1.0 : 0.0;\n"
    "    if (prof == 1) {\n"
    "        if (d <= -0.5 * w) return 1.0;\n"
    "        if (d >= 0.5 * w) return 0.0;\n"
    "        return 0.5 + 0.5 * cos(psy_PI * (d + 0.5 * w) / w);\n"
    "    }\n"
    "    return 0.5 * psy_erfc(d / (w * 1.4142135623731));\n"
    "}\n"
    /* A stroke is the band between the offsets a1 and a2 of the boundary:
     * the fill's coverage at a2 minus at a1, the hard band convolved with
     * the profile's kernel. look.w: the band's center offset. */
    "#ifndef PSY_VECTOR\n"
    "float psy_aperture(vec2 p) {\n"
    "    int k = int(psy_edge.x);\n"
    "    if (k == 6) return all(lessThanEqual(abs(p), psy_size.xy)) ? 1.0 : 0.0;\n"
    "    float hw = psy_shape.w;\n"
    "    vec2 a = hw > 0.0 ? psy_look.w + vec2(-hw, hw) : vec2(0.0);\n"
    /* SHAPE's offset (misc.x): the field at the level a + offset, so a
     * bevel is the offset outline's */
    "#ifdef PSY_OFFSET\n"
    "    vec2 d = psy_sdf2_(p, a + psy_misc.x) - psy_misc.x;\n"
    "#else\n"
    "    vec2 d = psy_sdf2_(p, a);\n"
    "#endif\n"
    "    if (hw <= 0.0) return psy_coverage(d.x);\n"
    "    return psy_coverage(d.y - a.y) - psy_coverage(d.x - a.x);\n"
    "}\n"
    "#endif\n";

static const char psygfx__glsl_main_mod[] =
    "void main() {\n"
    "    psy_p = psy_local_();\n"
    "    float g = psy_main(psy_p);\n"
    "    float a = psy_aperture(psy_p);\n"
    "    psy_out = vec4(psy_dir.rgb * (psy_look.x * psy_look.z * g * a), 0.0);\n"
    "}\n";

/* misc.z > 0.5: the body returns premultiplied color (a target). */
static const char psygfx__glsl_main_color[] =
    "void main() {\n"
    "    psy_p = psy_local_();\n"
    "    vec4 c = psy_main(psy_p);\n"
    "    float k = psy_aperture(psy_p) * psy_look.y * psy_look.z;\n"
    "    psy_out = psy_misc.z > 0.5 ? c * k : vec4(c.rgb * (c.a * k), c.a * k);\n"
    "}\n";

static const char psygfx__glsl_main_add[] =
    "void main() {\n"
    "    psy_p = psy_local_();\n"
    "    vec3 c = psy_main(psy_p);\n"
    "    psy_out = vec4(c * (psy_look.x * psy_look.z * psy_aperture(psy_p)), 0.0);\n"
    "}\n";

/* Built-in bodies. misc = (sf cyc/px, phase, sigma px, aspect) for gratings
 * and gabors; (check px, seed, dist, 0) for noise; (channels, mode,
 * premultiplied, 0) for images, whose source rectangle is p[0..3] and tint
 * color. Local y points down the box, so a row from the top is hs.y + p.y. */
static const char psygfx__body_shape[] =
    "vec4 psy_main(vec2 p) { return vec4(psy_color.rgb, 1.0); }\n";
static const char psygfx__body_grating[] =
    "float psy_main(vec2 p) {\n"
    "    float x = fract(psy_misc.x * p.x + psy_misc.y);\n"
    "    if (psy_edge.w > 0.5) return (x < 0.25 || x >= 0.75) ? 1.0 : -1.0;\n"
    "    return cos(2.0 * psy_PI * x);\n"
    "}\n";
static const char psygfx__body_gabor[] =
    "float psy_main(vec2 p) {\n"
    "    float x = fract(psy_misc.x * p.x + psy_misc.y);\n"
    "    float ay = psy_misc.w * p.y;\n"
    "    return cos(2.0 * psy_PI * x) * exp(-(p.x * p.x + ay * ay) / (2.0 * psy_misc.z * psy_misc.z));\n"
    "}\n";
static const char psygfx__body_noise[] =
    "float psy_main(vec2 p) {\n"
    "    vec2 q = vec2(p.x + psy_size.x, psy_size.y + p.y) / psy_misc.x;\n"
    "    uvec2 c = uvec2(ivec2(floor(q)));\n"
    "    uint h = psy_hash(c.x ^ psy_hash(c.y ^ psy_hash(uint(psy_misc.y))));\n"
    "    if (psy_misc.z > 0.5) return (h & 0x80000000u) != 0u ? 1.0 : -1.0;\n"
    "    return float(int(((h >> 9u) << 1u) + 1u) - 8388608) * (1.0 / 8388608.0);\n"
    "}\n";
/* The source rectangle's texel under p: nearest by texelFetch, clamped to
 * the rectangle; linear by hand, clamped the same way. */
static const char psygfx__body_image_fetch[] =
    "vec4 psy_texel_(vec2 p) {\n"
    "    int mode = int(psy_misc.y);\n"
    "    vec2 r = vec2(p.x + psy_size.x, psy_size.y + p.y) / (2.0 * psy_size.xy);\n"
    "    vec4 sr = psy_v[psy_i * 16 + 8];\n"
    "    ivec2 t = ivec2(sr.xy) + clamp(ivec2(floor(r * sr.zw)), ivec2(0), ivec2(sr.zw) - 1);\n"
    "    if (mode == 2) return vec4(vec3(float(texelFetch(psy_utex0, t, 0).r) * (1.0 / 65535.0)), 1.0);\n"
    "    vec4 v = mode == 1 ? psy_bilinear(psy_tex0, sr.xy + r * sr.zw, sr) : texelFetch(psy_tex0, t, 0);\n"
    "    int ch = int(psy_misc.x);\n"
    "    if (ch == 1) return vec4(v.rrr, 1.0);\n"
    "    if (ch == 2) return vec4(v.rrr, v.g);\n"
    "    return v;\n"
    "}\n";
/* Tint: linear rgb and alpha factors, in color. */
static const char psygfx__body_image_color[] =
    "vec4 psy_main(vec2 p) {\n"
    "    vec4 c = psy_texel_(p);\n"
    "    return psy_misc.z > 0.5 ? c * vec4(psy_color.rgb * psy_color.a, psy_color.a)\n"
    "                            : vec4(c.rgb * psy_color.rgb, c.a * psy_color.a);\n"
    "}\n";
/* The tint is folded into dir on the CPU. */
static const char psygfx__body_image_mod[] =
    "float psy_main(vec2 p) { return psy_texel_(p).r; }\n";
static const char psygfx__body_image_add[] =
    "vec3 psy_main(vec2 p) { return psy_texel_(p).rgb * psy_color.rgb * psy_color.a; }\n";
/* A dot: a circle of radius size.x with the stimulus's edge. */
static const char psygfx__body_dots[] =
    "vec4 psy_main(vec2 p) { return vec4(psy_color.rgb, 1.0); }\n";

/* The vector program (v0.3): SDF primitives with gradients and arc
 * lengths, the compound fold, dashes, trim, paint and effects. Its own
 * main; the CPU form is psygfx__vfield() and the functions it calls.
 * Primitives from Inigo Quilez's 2D distance functions (MIT) and
 * 0xfaded's ellipse (MIT): the notices are at the end of the file. */

static const char psygfx__glsl_vec0[] =
    "vec4 psy_vd_(int k) { return psy_v[psy_i * 16 + 9 + k]; }\n"
    "const float psy_TAU = 6.28318530717959;\n"
    "float psy_ang_(vec2 q) { if (q.x == 0.0 && q.y == 0.0) return 0.0; float a = atan(q.y, q.x); return a < 0.0 ? a + psy_TAU : a; }\n"
    "float psy_wrap_(float a) { return a - psy_TAU * floor(a / psy_TAU); }\n"
    "vec2 psy_nrm_(vec2 v, vec2 f) { float l = length(v); return l > 1e-20 ? v / l : f; }\n"
    "float psy_sgn_(float x) { return x >= 0.0 ? 1.0 : -1.0; }\n"
    "vec4 psy_p_circle_(vec2 q, float R, bool ws) {\n"
    "    return vec4(length(q) - R, psy_nrm_(q, vec2(1.0, 0.0)), ws ? R * psy_ang_(q) : 0.0);\n"
    "}\n"
    "vec4 psy_p_annulus_(vec2 q, float R, float ri, bool ws) {\n"
    "    float l = length(q), sg = psy_sgn_(l - 0.5 * (R + ri));\n"
    "    float s = !ws ? 0.0 : (sg > 0.0 ? R * psy_ang_(q) : psy_TAU * R + ri * psy_ang_(q));\n"
    "    return vec4(abs(l - 0.5 * (R + ri)) - 0.5 * (R - ri), sg * psy_nrm_(q, vec2(1.0, 0.0)), s);\n"
    "}\n"
    /* Inigo Quilez, sdBox with its gradient (iquilezles.org, "2D distance
     * functions"; MIT) */
    "vec4 psy_box_(vec2 q, vec2 b) {\n"
    "    vec2 w = abs(q) - b, sg = vec2(psy_sgn_(q.x), psy_sgn_(q.y));\n"
    "    if (w.x > 0.0 && w.y > 0.0) { float l = length(w); return vec4(l, sg * w / l, 0.0); }\n"
    "    return w.x >= w.y ? vec4(w.x, sg.x, 0.0, 0.0) : vec4(w.y, 0.0, sg.y, 0.0);\n"
    "}\n"
    /* after Inigo Quilez, sdRoundBox (Shadertoy 4llXD7; MIT): a radius per
     * corner; the gradient and arc length are ours */
    "vec4 psy_p_rrect_(vec2 q, vec2 b, vec4 r, bool ws) {\n"
    "    bool rt = q.x > 0.0, bt = q.y > 0.0;\n"
    "    float rr = rt ? (bt ? r.z : r.y) : (bt ? r.w : r.x);\n"
    "    vec2 sg = vec2(rt ? 1.0 : -1.0, bt ? 1.0 : -1.0), w = abs(q) - b + rr;\n"
    "    bool cn = w.x > 0.0 && w.y > 0.0;\n"
    "    vec4 e;\n"
    "    if (cn) { float l = length(w); e = vec4(l - rr, sg * w / l, 0.0); }\n"
    "    else if (w.x >= w.y) e = vec4(w.x - rr, sg.x, 0.0, 0.0);\n"
    "    else e = vec4(w.y - rr, 0.0, sg.y, 0.0);\n"
    "    if (ws) {\n"
    "        float h = 1.5707963267949, lt = 2.0 * b.x - r.x - r.y, lr = 2.0 * b.y - r.y - r.z;\n"
    "        float lb = 2.0 * b.x - r.z - r.w, ll = 2.0 * b.y - r.w - r.x;\n"
    "        float sR = lt + h * r.y, sB = sR + lr + h * r.z, sL = sB + lb + h * r.w;\n"
    "        if (cn) {\n"
    "            if (rt && !bt) e.w = lt + rr * atan(w.x, w.y);\n"
    "            else if (rt) e.w = sR + lr + rr * atan(w.y, w.x);\n"
    "            else if (bt) e.w = sB + lb + rr * atan(w.x, w.y);\n"
    "            else e.w = sL + ll + rr * atan(w.y, w.x);\n"
    "        } else if (w.x >= w.y) {\n"
    "            e.w = rt ? sR + clamp(q.y + b.y - r.y, 0.0, lr) : sL + clamp(b.y - r.w - q.y, 0.0, ll);\n"
    "        } else {\n"
    "            e.w = bt ? sB + clamp(b.x - r.z - q.x, 0.0, lb) : clamp(q.x + b.x - r.x, 0.0, lt);\n"
    "        }\n"
    "    }\n"
    "    return e;\n"
    "}\n"
    /* after Inigo Quilez, sdUnevenCapsule (Shadertoy 4lcBWn; MIT) */
    "vec4 psy_p_capsule_(vec2 q, float hx, float r0, float r1, bool ws) {\n"
    "    float x1 = r0 - hx, D = 2.0 * hx - r0 - r1, sn = (r0 - r1) / D, cs = sqrt(max(1.0 - sn * sn, 0.0));\n"
    "    float al = q.x - x1, k = -sn * abs(q.y) + cs * al;\n"
    "    vec4 e;\n"
    "    if (k < 0.0) { vec2 v = vec2(al, q.y); e = vec4(length(v) - r0, psy_nrm_(v, vec2(-1.0, 0.0)), 0.0); }\n"
    "    else if (k > cs * D) { vec2 v = vec2(al - D, q.y); e = vec4(length(v) - r1, psy_nrm_(v, vec2(1.0, 0.0)), 0.0); }\n"
    "    else e = vec4(cs * abs(q.y) + sn * al - r0, sn, psy_sgn_(q.y) * cs, 0.0);\n"
    "    if (ws) e.w = clamp(al, 0.0, D);\n"
    "    return e;\n"
    "}\n"
    "float psy_sweep_(float pv, float pu, float sw) {\n"
    "    return clamp(0.5 * sw + ((pv == 0.0 && pu == 0.0) ? 0.0 : atan(pv, pu)), 0.0, sw);\n"
    "}\n"
    /* after Inigo Quilez, sdArc (Shadertoy wl23RK; MIT); the trig comes from
     * the CPU, as D3D's sin and cos are coarse */
    "vec4 psy_p_arc_(vec2 q, vec2 u, float ra, float rb, float sw, vec2 sc, bool ws) {\n"
    "    vec2 v = vec2(-u.y, u.x);\n"
    "    float pv = dot(q, v), d;\n"
    "    vec2 p = vec2(abs(pv), dot(q, u)), gi;\n"
    "    if (sc.y * p.x > sc.x * p.y) { vec2 w = p - sc * ra; d = length(w) - rb; gi = psy_nrm_(w, sc); }\n"
    "    else { float l = length(p); d = abs(l - ra) - rb; gi = psy_sgn_(l - ra) * psy_nrm_(p, vec2(0.0, 1.0)); }\n"
    "    float s = ws ? ra * psy_sweep_(pv, p.y, sw) : 0.0;\n"
    "    return vec4(d, gi.x * psy_sgn_(pv) * v + gi.y * u, s);\n"
    "}\n"
    /* after Inigo Quilez, sdPie (Shadertoy 3l23RK; MIT) */
    "vec4 psy_p_pie_(vec2 q, float R, vec2 u, float sw, vec2 c, bool ws) {\n"
    "    vec2 v = vec2(-u.y, u.x);\n"
    "    float pv = dot(q, v);\n"
    "    vec2 p = vec2(abs(pv), dot(q, u));\n"
    "    float l = length(p) - R, pr = clamp(dot(p, c), 0.0, R);\n"
    "    vec2 mv = p - c * pr, gi;\n";

static const char psygfx__glsl_vec1[] =
    "    float sg = psy_sgn_(c.y * p.x - c.x * p.y), mm = length(mv) * sg, d;\n"
    "    bool arc = l > mm;\n"
    "    if (arc) { d = l; gi = psy_nrm_(p, vec2(0.0, 1.0)); } else { d = mm; gi = sg * psy_nrm_(mv, vec2(c.y, -c.x)); }\n"
    "    float s = 0.0;\n"
    "    if (ws) s = arc ? R + R * psy_sweep_(pv, p.y, sw) : (pv < 0.0 ? pr : 2.0 * R + R * sw - pr);\n"
    "    return vec4(d, gi.x * psy_sgn_(pv) * v + gi.y * u, s);\n"
    "}\n"
    /* after Inigo Quilez, sdStar (Shadertoy 3tSGDy; MIT): the sector fold;
     * the fold by a complex power and the reflection guard against D3D's
     * coarse atan, and the fillets, are ours */
    "vec4 psy_p_star_(vec2 q, vec4 C, vec4 D, bool ws) {\n"
    "    float R = C.x, n = C.y, rho = C.z, rf = C.w, an = psy_PI / n;\n"
    "    float al = (q.x == 0.0 && q.y == 0.0) ? 0.0 : atan(q.x, q.y), kf = floor((al + an) / (2.0 * an));\n"
    "    vec2 Th = D.xy, z = vec2(Th.x * Th.x - Th.y * Th.y, 2.0 * Th.x * Th.y), w2 = vec2(1.0, 0.0);\n"
    "    for (int kk = int(abs(kf)); kk > 0; kk /= 2) {\n"
    "        if ((kk & 1) != 0) w2 = vec2(w2.x * z.x - w2.y * z.y, w2.x * z.y + w2.y * z.x);\n"
    "        z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y);\n"
    "    }\n"
    "    if (kf < 0.0) w2.y = -w2.y;\n"
    "    float fys = dot(q, vec2(w2.x, -w2.y)), sb = psy_sgn_(fys);\n"
    "    vec2 f = vec2(dot(q, w2.yx), abs(fys));\n"
    "    bool rfl = f.x * Th.y - f.y * Th.x < 0.0;\n"
    "    if (rfl) f = 2.0 * dot(f, Th) * Th - f;\n"
    "    vec2 I = vec2(R * rho, 0.0), T = R * Th, e = normalize(T - I), ne = vec2(e.y, -e.x);\n"
    "    vec2 PI = I, PT = T, gf = ne;\n"
    "    float best = 1e30, dv = 0.0, sh = 0.0, lI = D.w, H = D.z;\n"
    "    if (rf > 0.0) {\n"
    "        float cb = dot(e, Th), sbe = abs(e.x * Th.y - e.y * Th.x);\n"
    "        vec2 CT = T - Th * (rf / sbe);\n"
    "        PT = T - e * (rf * cb / sbe);\n"
    "        if (abs(e.x) > 1e-5) {\n"
    "            float cv = e.x > 0.0 ? -1.0 : 1.0;\n"
    "            vec2 CI = I - vec2(cv * rf / abs(e.y), 0.0);\n"
    "            PI = I + e * (rf * abs(e.x) / abs(e.y));\n"
    "            vec2 w = f - CI, a1 = vec2(cv, 0.0), a2 = (PI - CI) / rf;\n"
    "            float cr = a1.x * a2.y - a1.y * a2.x, lw = length(w);\n"
    "            if ((a1.x * w.y - a1.y * w.x) * cr >= -1e-5 * lw && (w.x * a2.y - w.y * a2.x) * cr >= -1e-5 * lw && lw > 0.0) {\n"
    "                dv = cv * (lw - rf); best = abs(dv); gf = cv * w / lw;\n"
    "                sh = rf * abs(atan(a1.x * w.y - a1.y * w.x, dot(a1, w)));\n"
    "            }\n"
    "        }\n"
    "        vec2 w = f - CT;\n"
    "        float lw = length(w), cr = ne.x * Th.y - ne.y * Th.x;\n"
    "        if ((ne.x * w.y - ne.y * w.x) * cr >= -1e-5 * lw && (w.x * Th.y - w.y * Th.x) * cr >= -1e-5 * lw && lw > 0.0 && abs(lw - rf) < best) {\n"
    "            dv = lw - rf; best = abs(dv); gf = w / lw;\n"
    "            sh = lI + length(PT - PI) + rf * abs(atan(ne.x * w.y - ne.y * w.x, dot(ne, w)));\n"
    "        }\n"
    "    }\n"
    "    float LP = length(PT - PI), t = clamp(dot(f - PI, e), 0.0, LP);\n"
    "    vec2 w = f - PI - e * t;\n"
    "    float l = length(w);\n"
    "    if (l < best) { float sg = dot(w, ne) >= 0.0 ? 1.0 : -1.0; dv = sg * l; gf = l > 0.0 ? sg * w / l : ne; sh = lI + t; }\n"
    "    if (rfl) { gf = 2.0 * dot(gf, Th) * Th - gf; sh = 2.0 * H - sh; }\n"
    "    vec2 g = gf.x * w2.yx + gf.y * sb * vec2(w2.x, -w2.y);\n"
    "    float s = 0.0;\n"
    "    if (ws) { float P = 2.0 * H; s = mod(n - kf, n) * P + (sb < 0.0 ? sh : -sh); s -= n * P * floor(s / (n * P)); }\n"
    "    return vec4(dv, g, s);\n"
    "}\n"
    "const float psy_GLX[8] = float[8](0.0950125098376374, 0.2816035507792589, 0.4580167776572274, 0.6178762444026438,\n"
    "                                  0.7554044083550030, 0.8656312023878318, 0.9445750230732326, 0.9894009349916499);\n"
    "const float psy_GLW[8] = float[8](0.1894506104550685, 0.1826034150449236, 0.1691565193950025, 0.1495959888165767,\n"
    "                                  0.1246289712555339, 0.0951585116824928, 0.0622535239386479, 0.0271524594117541);\n"
    /* 0xfaded (Carl Chatfield), the trig-free closest point on an ellipse
     * (github.com/0xfaded/ellipse_demo; MIT) */
    "vec4 psy_p_ellipse_(vec2 q, vec2 ab, float it, float Lq, bool ws) {\n"
    "    vec2 p = abs(q), t = vec2(0.70710678118654752);\n"
    "    vec2 k2 = vec2(ab.x * ab.x - ab.y * ab.y, ab.y * ab.y - ab.x * ab.x) / ab;\n"
    "    for (int i = 0; i < int(it); i++) {\n"
    "        vec2 ev = k2 * t * t * t, r = ab * t - ev, qq = p - ev;\n"
    "        t = clamp((qq * (length(r) / max(length(qq), 1e-20)) + ev) / ab, 0.0, 1.0);\n";

static const char psygfx__glsl_vec2[] =
    "        t /= max(length(t), 1e-20);\n"
    "    }\n"
    "    vec2 w = p - ab * t;\n"
    "    float sg = dot(p / ab, p / ab) < 1.0 ? -1.0 : 1.0;\n"
    "    vec2 g = sg * psy_nrm_(w, sg * normalize(t / ab)) * vec2(psy_sgn_(q.x), psy_sgn_(q.y));\n"
    "    float s = 0.0;\n"
    "    if (ws) {\n"
    "        float t1 = atan(t.y, t.x), h = 0.5 * t1, S = 0.0;\n"
    "        for (int i = 0; i < int(psy_axes.y); i++) {\n"
    "            float x = psy_GLX[i / 2] * (i % 2 == 0 ? 1.0 : -1.0), u = h * (1.0 + x);\n"
    "            S += psy_GLW[i / 2] * length(ab * vec2(sin(u), cos(u)));\n"
    "        }\n"
    "        S *= h;\n"
    "        s = q.x >= 0.0 ? (q.y >= 0.0 ? S : 4.0 * Lq - S) : (q.y >= 0.0 ? 2.0 * Lq - S : 2.0 * Lq + S);\n"
    "    }\n"
    "    return vec4(sg * length(w), g, s);\n"
    "}\n"
    "vec4 psy_p_polygon_(vec2 q, int g0, int n, bool ws) {\n"
    "    float best = 1e30, dv = 0.0, s = 0.0, par = 1.0, o = psy_vd_(g0 + 2).y;\n"
    "    bool sharp = abs(o) > 1.5;\n"
    "    o = psy_sgn_(o);\n"
    "    vec2 gb = vec2(1.0, 0.0);\n"
    "    for (int i = 0; i < n; i++) {\n"
    "        int j = i + 1 < n ? i + 1 : 0;\n"
    "        vec4 A = psy_vd_(g0 + 3 * i), B = psy_vd_(g0 + 3 * i + 1), C = psy_vd_(g0 + 3 * i + 2), An = psy_vd_(g0 + 3 * j);\n"
    "        if (B.z != 0.0) {\n"
    "            float r = abs(B.z), cv = psy_sgn_(B.z);\n"
    "            vec2 w = q - B.xy, a1 = (A.xy - B.xy) / r, a2 = (A.zw - B.xy) / r;\n"
    "            float cr = a1.x * a2.y - a1.y * a2.x, lw = length(w);\n"
    "            if ((a1.x * w.y - a1.y * w.x) * cr >= -1e-5 * lw && (w.x * a2.y - w.y * a2.x) * cr >= -1e-5 * lw && lw > 0.0 && abs(lw - r) < best) {\n"
    "                dv = cv * (lw - r); best = abs(dv); gb = psy_sgn_(lw - r) * w / lw;\n"
    "                s = B.w + r * abs(atan(a1.x * w.y - a1.y * w.x, dot(a1, w)));\n"
    "            }\n"
    "        }\n"
    "        vec2 ev = An.xy - A.zw;\n"
    "        float le = length(ev);\n"
    "        vec2 e = ev / max(le, 1e-20), ne = o * vec2(e.y, -e.x);\n"
    "        float t = clamp(dot(q - A.zw, e), 0.0, le);\n"
    "        vec2 w = q - A.zw - e * t;\n"
    "        float l = length(w);\n"
    "        if (l < best) { float sg = dot(w, ne) >= 0.0 ? 1.0 : -1.0; best = l; dv = sg * l; gb = l > 0.0 ? w / l : ne; s = C.x + t; }\n"
    "        if (sharp) {\n"
    "            vec2 vi = C.zw, vj = psy_vd_(g0 + 3 * j + 2).zw, ee = vj - vi, ww = q - vi;\n"
    "            bvec3 c = bvec3(q.y >= vi.y, q.y < vj.y, ee.x * ww.y > ee.y * ww.x);\n"
    "            if (all(c) || all(not(c))) par = -par;\n"
    "        }\n"
    "    }\n"
    "    if (sharp) { dv = par * best; gb *= par; }\n"
    "    else gb *= psy_sgn_(dv);\n"
    "    return vec4(dv, gb, ws ? s : 0.0);\n"
    "}\n"
    "vec3 psy_seg2_(vec2 p, vec2 a, vec2 b, vec3 best) {\n"
    "    vec2 e = b - a, q = p - a, r = q - e * clamp(dot(q, e) / max(dot(e, e), 1e-20), 0.0, 1.0);\n"
    "    return dot(r, r) < best.x ? vec3(dot(r, r), r) : best;\n"
    "}\n"
    "vec4 psy_cpoly_(vec2 p, vec2 a, vec2 b, vec2 c, vec2 d) {\n"
    "    vec3 best = psy_seg2_(p, a, b, vec3(1e30, 0.0, 0.0));\n"
    "    best = psy_seg2_(p, b, c, best);\n"
    "    best = psy_seg2_(p, c, d, best);\n"
    "    best = psy_seg2_(p, d, a, best);\n"
    "    float ar = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) + (c.x - a.x) * (d.y - a.y) - (c.y - a.y) * (d.x - a.x);\n"
    "    bool ins = ar * ((b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x)) >= 0.0 && ar * ((c.x - b.x) * (p.y - b.y) - (c.y - b.y) * (p.x - b.x)) >= 0.0 &&\n"
    "               ar * ((d.x - c.x) * (p.y - c.y) - (d.y - c.y) * (p.x - c.x)) >= 0.0 && ar * ((a.x - d.x) * (p.y - d.y) - (a.y - d.y) * (p.x - d.x)) >= 0.0;\n"
    "    float l = sqrt(best.x), sg = ins ? -1.0 : 1.0;\n"
    "    return vec4(sg * l, l > 0.0 ? sg * best.yz / l : vec2(0.0), 0.0);\n"
    "}\n"
    "vec4 psy_p_polyline_(vec2 p, int g0, int n, float hw, int join, float lim, int cap, bool ws) {\n"
    "    vec4 best = vec4(1e30, 1.0, 0.0, 0.0);\n"
    "    float bh = 0.5;\n"
    "    int bi = 0;\n"
    "    for (int i = 0; i < n - 1; i++) {\n"
    "        vec4 A = psy_vd_(g0 + i), B = psy_vd_(g0 + i + 1);\n"
    "        vec2 tv = B.xy - A.xy;\n"
    "        float len = length(tv);\n"
    "        vec2 t = tv / max(len, 1e-20), nv = vec2(-t.y, t.x), r = p - A.xy;\n"
    "        float al = dot(r, t);\n";

static const char psygfx__glsl_vec3[] =
    "        if (join == 0) {   /* round joins: the distance to the center line, exact */\n"
    "            float h = clamp(al, 0.0, len);\n"
    "            vec2 w = r - t * h;\n"
    "            float l = length(w);\n"
    "            if (l - hw < best.x) { best = vec4(l - hw, psy_nrm_(w, nv), A.z + h); bh = al / max(len, 1e-20); bi = i; }\n"
    "            continue;\n"
    "        }\n"
    "        float e0 = (i == 0 && cap == 2) ? hw : 0.0, e1 = (i == n - 2 && cap == 2) ? hw : 0.0;\n"
    "        vec4 bx = psy_box_(vec2(al - 0.5 * (len + e1 - e0), dot(r, nv)), vec2(0.5 * (len + e0 + e1), hw));\n"
    "        if (bx.x < best.x) best = vec4(bx.x, bx.y * t + bx.z * nv, A.z + clamp(al, 0.0, len));\n"
    "        if (i < n - 2) {\n"
    "            vec2 t2 = normalize(psy_vd_(g0 + i + 2).xy - B.xy);\n"
    "            float cr = t.x * t2.y - t.y * t2.x;\n"
    "            if (abs(cr) > 1e-6 || dot(t, t2) < 0.0) {\n"
    "                float sd = cr > 0.0 ? -1.0 : 1.0;\n"
    "                vec2 na = sd * nv, nb = sd * vec2(-t2.y, t2.x), m = na + nb, Pa = B.xy + na * hw, Pb = B.xy + nb * hw;\n"
    "                float mm = dot(m, m);\n"
    "                bool mit = join == 1 && mm > 1e-12 && 2.0 / sqrt(mm) <= lim;\n"
    "                vec4 jp = psy_cpoly_(p, B.xy, Pa, mit ? B.xy + m * (2.0 * hw / mm) : Pb, Pb);\n"
    "                if (jp.x < best.x) best = vec4(jp.xyz, B.z);\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    if (join == 0 && cap != 1 && ((bi == 0 && bh <= 0.0) || (bi == n - 2 && bh >= 1.0))) {\n"
    "        /* a butt or square end: the half strip past the end segment */\n"
    "        bool st = bi == 0 && bh <= 0.0;\n"
    "        vec4 E = psy_vd_(g0 + (st ? 0 : n - 1)), F = psy_vd_(g0 + (st ? 1 : n - 2));\n"
    "        vec2 to = normalize(E.xy - F.xy), nn = vec2(-to.y, to.x), r = p - E.xy;\n"
    "        float ac = dot(r, nn);\n"
    "        vec2 qd = vec2(dot(r, to) - (cap == 2 ? hw : 0.0), abs(ac) - hw);\n"
    "        vec2 gg = (qd.x > 0.0 && qd.y > 0.0) ? normalize(qd) : (qd.x >= qd.y ? vec2(1.0, 0.0) : vec2(0.0, 1.0));\n"
    "        best.x = length(max(qd, 0.0)) + min(max(qd.x, qd.y), 0.0);\n"
    "        best.yz = gg.x * to + gg.y * psy_sgn_(ac) * nn;\n"
    "    }\n"
    "    if (join != 0 && cap == 1)\n"
    "        for (int k = 0; k < 2; k++) {\n"
    "            vec4 E = psy_vd_(g0 + (k == 0 ? 0 : n - 1));\n"
    "            vec2 v = p - E.xy;\n"
    "            float l = length(v);\n"
    "            if (l - hw < best.x) best = vec4(l - hw, psy_nrm_(v, vec2(1.0, 0.0)), E.z);\n"
    "        }\n"
    "    if (!ws) best.w = 0.0;\n"
    "    return best;\n"
    "}\n"
    "vec4 psy_prim_(vec2 q, vec4 B, vec4 C, vec4 D, bool ws) {\n"
    "    int k = int(B.x);\n"
    "    if (k == 1) return psy_p_circle_(q, C.x, ws);\n"
    "    if (k == 2) return psy_p_annulus_(q, C.x, C.z, ws);\n"
    "    if (k == 8) return psy_p_rrect_(q, C.xy, vec4(C.zw, D.xy), ws);\n"
    "    if (k == 11) return psy_p_capsule_(q, C.x, C.z, C.w, ws);\n"
    "    if (k == 9) return psy_p_arc_(q, C.xy, C.z, C.w, D.x, D.zw, ws);\n"
    "    if (k == 10) return psy_p_pie_(q, C.x, C.yz, D.x, D.zw, ws);\n"
    "    if (k == 13) return psy_p_star_(q, C, D, ws);\n"
    "    if (k == 14) return psy_p_ellipse_(q, C.xy, C.z, C.w, ws);\n"
    "    if (k == 5) { vec4 a = psy_box_(q, vec2(C.x, 0.5 * C.z)), b = psy_box_(q, vec2(0.5 * C.z, C.y)); return a.x < b.x ? a : b; }\n"
    "    if (k == 4) return psy_p_polygon_(q, int(D.z), int(D.w), ws);\n"
    "    if (k == 16) return psy_p_polyline_(q, int(D.z), int(D.w), C.z, int(C.w), D.x, int(D.y), ws);\n"
    "    return vec4(1e30, 1.0, 0.0, 0.0);\n"
    "}\n"
    /* Inigo Quilez, the quadratic polynomial smooth minimum
     * (iquilezles.org, "smooth minimum"; MIT), with its gradient */
    "vec4 psy_smin_(vec4 a, vec4 b, float k) {\n"
    "    float h = clamp(0.5 + 0.5 * (b.x - a.x) / k, 0.0, 1.0);\n"
    "    return vec4(mix(b.x, a.x, h) - k * h * (1.0 - h), mix(b.yz, a.yz, h), h > 0.5 ? a.w : b.w);\n"
    "}\n"
    "vec4 psy_op_(vec4 a, vec4 e, vec4 B) {\n"
    "    int op = int(B.y);\n"
    "    float k = max(B.z, 1e-6);\n"
    "    vec4 ne = vec4(-e.xyz, e.w), na = vec4(-a.xyz, a.w);\n"
    "    if (op == 0) return e.x < a.x ? e : a;\n"
    "    if (op == 1) return e.x > a.x ? e : a;\n"
    "    if (op == 2) return ne.x > a.x ? ne : a;\n"
    "    if (op == 3) { vec4 u = e.x < a.x ? e : a, v = e.x < a.x ? a : e; return u.x > -v.x ? u : vec4(-v.xyz, v.w); }\n"
    "    if (op == 4) return psy_smin_(a, e, k);\n";

static const char psygfx__glsl_vec4[] =
    "    vec4 r = psy_smin_(na, op == 5 ? ne : e, k);\n"
    "    return vec4(-r.xyz, r.w);\n"
    "}\n"
    "void psy_field_(vec2 p, vec2 p1, int npts, bool ws, out vec4 f0, out vec4 f1) {\n"
    "    int n = int(psy_v[psy_i * 16 + 6].w);\n"
    "    vec4 acc = vec4(1e30, 1.0, 0.0, 0.0);\n"
    "    f0 = acc;\n"
    "    f1 = acc;\n"
    "    for (int t = 0; t < n * npts; t++) {\n"
    "        int i = t < n ? t : t - n;\n"
    "        vec4 A = psy_vd_(4 * i), B = psy_vd_(4 * i + 1), C = psy_vd_(4 * i + 2), D = psy_vd_(4 * i + 3);\n"
    "        vec2 ax = A.zw, ay = vec2(-A.w, A.z), r = (t < n ? p : p1) - A.xy;\n"
    "        vec4 e = psy_prim_(vec2(dot(r, ax), dot(r, ay)), B, C, D, ws);\n"
    "        e.yz = e.y * ax + e.z * ay;\n"
    "        if (B.w > 0.0) e = vec4(abs(e.x) - B.w, psy_sgn_(e.x) * e.yz, e.w);\n"
    "        acc = i == 0 ? e : psy_op_(acc, e, B);\n"
    "        if (t == n - 1) f0 = acc;\n"
    "        if (t == 2 * n - 1) f1 = acc;\n"
    "    }\n"
    "    if (npts < 2) f1 = f0;\n"
    "}\n"
    "/* Coverage and Gaussians four at a time: D3D inlines each call site, so\n"
    " * the vector program calls these from a few sites, not many. */\n"
    "vec4 psy_erfc4_(vec4 x) {\n"
    "    vec4 z = abs(x), t = 1.0 / (1.0 + 0.3275911 * z);\n"
    "    vec4 y = t * (0.254829592 + t * (-0.284496736 + t * (1.421413741 + t * (-1.453152027 + t * 1.061405429)))) * exp(-z * z);\n"
    "    return mix(2.0 - y, y, greaterThanEqual(x, vec4(0.0)));\n"
    "}\n"
    "vec4 psy_cov4_(vec4 d) {\n"
    "    int prof = int(psy_edge.y);\n"
    "    float w = psy_edge.z;\n"
    "    vec4 hard = mix(vec4(0.0), vec4(1.0), lessThanEqual(d, vec4(0.0)));\n"
    "    if (prof == 0 || w <= 0.0) return hard;\n"
    "    if (prof == 1) {\n"
    "        vec4 c = 0.5 + 0.5 * cos(psy_PI * (d + 0.5 * w) / w);\n"
    "        return mix(mix(c, vec4(0.0), greaterThanEqual(d, vec4(0.5 * w))), vec4(1.0), lessThanEqual(d, vec4(-0.5 * w)));\n"
    "    }\n"
    "    return 0.5 * psy_erfc4_(d / (w * 1.4142135623731));\n"
    "}\n"
    "vec4 psy_gauss4_(vec4 x, vec4 sg) {\n"
    "    vec4 g = 0.5 * psy_erfc4_(x / (max(sg, vec4(1e-30)) * 1.4142135623731));\n"
    "    return mix(g, mix(vec4(0.0), vec4(1.0), lessThanEqual(x, vec4(0.0))), lessThanEqual(sg, vec4(0.0)));\n"
    "}\n"
    "float psy_along_(float cm, float nn, float hh, float s) {\n"
    "    vec4 V4 = psy_v[psy_i * 16 + 4], V6 = psy_v[psy_i * 16 + 6];\n"
    "    int fl = int(psy_v[psy_i * 16 + 5].w), cap = int(V6.z);\n"
    "    bool dsh = (fl & 1) != 0, trm = (fl & 2) != 0;\n"
    "    float P = V4.x + V4.y, u = dsh ? mod(s - V4.z, P) : 0.0, ex = cap == 2 ? hh : 0.0, a = 1.0, ga = 0.0;\n"
    "    if (dsh) {\n"
    "        a = 0.0;\n"
    "        ga = 1e30;\n"
    "        for (int k = 0; k < int(psy_axes.z); k++) {\n"
    "            float uk = u + float(k - 1) * P;\n"
    "            vec4 c = psy_cov4_(vec4(uk - V4.x - ex, uk + ex, 0.0, 0.0));\n"
    "            a += c.x - c.y;\n"
    "            ga = min(ga, max(abs(uk - 0.5 * V4.x) - 0.5 * V4.x, 0.0));\n"
    "        }\n"
    "    }\n"
    "    if (trm) ga = max(ga, max(abs(s - 0.5 * (V6.x + V6.y)) - 0.5 * (V6.y - V6.x), 0.0));\n"
    "    vec4 t = psy_cov4_(vec4(s - V6.y - ex, s - V6.x + ex, length(vec2(ga, nn)) - hh, 0.0));\n"
    "    if (cap == 1) return t.z;\n"
    "    return cm * a * (trm ? t.x - t.y : 1.0);\n"
    "}\n"
    "vec3 psy_paint_(vec2 p, vec4 f0, float d) {\n"
    "    int o = int(psy_v[psy_i * 16 + 8].y);\n"
    "    vec4 H = psy_vd_(o), G = psy_vd_(o + 1);\n"
    "    int kind = int(H.x), sp = int(H.y), n = int(H.w);\n"
    "    vec3 c = psy_vd_(o + 2).xyz;\n"
    "    if (kind == 4) {\n"
    "        int g0 = int(G.x);\n"
    "        vec2 q = (p - max(d, 0.0) * f0.yz) * G.y;\n"
    "        float ws = 0.0;\n"
    "        vec3 acc = vec3(0.0);\n"
    "        for (int i = 0; i < n; i++) {\n"
    "            int ip = i == 0 ? n - 1 : i - 1, i2 = i + 1 < n ? i + 1 : 0;\n"
    "            vec2 vp = psy_vd_(g0 + 3 * ip + 2).zw * G.y, vi = psy_vd_(g0 + 3 * i + 2).zw * G.y, vn = psy_vd_(g0 + 3 * i2 + 2).zw * G.y;\n"
    "            float w = (vi.x - vp.x) * (vn.y - vp.y) - (vi.y - vp.y) * (vn.x - vp.x);\n"
    "            for (int j = 0; j < n; j++) {\n"
    "                if (j == ip || j == i) continue;\n"
    "                vec2 a = psy_vd_(g0 + 3 * j + 2).zw * G.y - q, b = psy_vd_(g0 + 3 * (j + 1 < n ? j + 1 : 0) + 2).zw * G.y - q;\n"
    "                w *= a.x * b.y - a.y * b.x;\n"
    "            }\n"
    "            ws += w;\n";

static const char psygfx__glsl_vec5[] =
    "            acc += w * psy_vd_(o + 2 + i).xyz;\n"
    "        }\n"
    "        c = acc / ws;\n"
    "    } else {\n"
    "        float t;\n"
    "        if (kind == 1) { vec2 ab = G.zw - G.xy; t = dot(p - G.xy, ab) / dot(ab, ab); }\n"
    "        else if (kind == 2) t = length(p - G.xy) / G.z;\n"
    "        else t = psy_wrap_(psy_ang_(p - G.xy) - G.z) / psy_TAU;\n"
    "        t = H.z > 0.5 ? fract(t) : clamp(t, 0.0, 1.0);\n"
    "        for (int k = 0; k < n - 1; k++) {\n"
    "            vec4 a = psy_vd_(o + 2 + k), b = psy_vd_(o + 3 + k);\n"
    "            if (t >= a.w) c = b.w > a.w ? mix(a.xyz, b.xyz, clamp((t - a.w) / (b.w - a.w), 0.0, 1.0)) : b.xyz;\n"
    "        }\n"
    "    }\n"
    "    if (sp == 1) {\n"
    "        vec3 l = vec3(c.x + 0.3963377774 * c.y + 0.2158037573 * c.z, c.x - 0.1055613458 * c.y - 0.0638541728 * c.z,\n"
    "                      c.x - 0.0894841775 * c.y - 1.2914855480 * c.z);\n"
    "        l = l * l * l;\n"
    "        c = vec3(dot(psy_vd_(o + 18).xyz, l), dot(psy_vd_(o + 19).xyz, l), dot(psy_vd_(o + 20).xyz, l));\n"
    "    } else if (sp == 2) {\n"
    "        vec3 k3 = c.z * vec3(sin(c.x), cos(c.x) * cos(c.y), cos(c.x) * sin(c.y));\n"
    "        vec4 r0 = psy_vd_(o + 18), r1 = psy_vd_(o + 19), r2 = psy_vd_(o + 20);\n"
    "        c = vec3(r0.w + dot(r0.xyz, k3), r1.w + dot(r1.xyz, k3), r2.w + dot(r2.xyz, k3));\n"
    "    }\n"
    "    return c;\n"
    "}\n"
    "vec4 psy_over_(vec4 t, vec4 b) { return t + b * (1.0 - t.a); }\n"
    "void main() {\n"
    "    psy_p = psy_local_();\n"
    "    vec4 V2 = psy_v[psy_i * 16 + 2], V3 = psy_v[psy_i * 16 + 3], V5 = psy_v[psy_i * 16 + 5];\n"
    "    vec4 V7 = psy_v[psy_i * 16 + 7], V8 = psy_v[psy_i * 16 + 8];\n"
    "    int fl = int(V5.w);\n"
    "    vec4 f0, f1;\n"
    "    psy_field_(psy_p, psy_p - V7.xy, int(V7.w), (fl & 8) != 0, f0, f1);\n"
    "    if ((fl & 4) != 0) { f0.x /= max(length(f0.yz), 1e-3); f1.x /= max(length(f1.yz), 1e-3); }\n"
    "    if (V7.z > 0.0) { f0.x = abs(f0.x) - V7.z; f1.x = abs(f1.x) - V7.z; }\n"
    "    float d = f0.x - V2.z, d1 = f1.x - V2.z, hw = V3.w;\n"
    "    vec4 c0 = psy_cov4_(vec4(d - V2.w - hw, d - V2.w + hw, d, 0.0));\n"
    "    float cm = hw > 0.0 ? c0.x - c0.y : c0.z;\n"
    "    if ((fl & 3) != 0) cm = psy_along_(cm, hw > 0.0 ? d - V2.w : d + V8.w, hw > 0.0 ? hw : V8.w, f0.w);\n"
    "    vec4 acc = vec4((fl & 16) != 0 ? psy_paint_(psy_p, f0, d) : V3.rgb, 1.0) * cm;\n"
    "    if ((fl & 32) != 0) {\n"
    "        int o = int(V8.x);\n"
    "        vec4 Dp = psy_vd_(o), Gp = psy_vd_(o + 2), Ip = psy_vd_(o + 4), B0 = psy_vd_(o + 6), B1 = psy_vd_(o + 8);\n"
    "        vec4 g = psy_gauss4_(vec4(d1 - Dp.y, d - Gp.y, -(d1 + Ip.y), 0.0), vec4(Dp.x, Gp.x, Ip.x, 1.0));\n"
    "        vec4 bd = psy_cov4_(vec4(d - B0.y, d - B0.x, d - B1.y, d - B1.x));\n"
    "        if ((fl & 64) != 0) {   /* the exact blur of a sharp rect: four erf in one */\n"
    "            vec2 q = psy_p - V7.xy, hb = psy_vd_(2).xy + Dp.y;\n"
    "            vec4 e = 0.5 * psy_erfc4_(-vec4(hb - q, -hb - q) / (Dp.x * 1.4142135623731));\n"
    "            g.x = (e.x - e.z) * (e.y - e.w);\n"
    "        }\n"
    "        vec4 r = vec4(psy_vd_(o + 1).rgb, 1.0) * (g.x * Dp.z);\n"
    "        r = psy_over_(vec4(psy_vd_(o + 3).rgb, 1.0) * (g.y * Gp.z), r);\n"
    "        r = psy_over_(acc, r);\n"
    "        r = psy_over_(vec4(psy_vd_(o + 5).rgb, 1.0) * (c0.z * g.z * Ip.z), r);\n"
    "        r = psy_over_(vec4(psy_vd_(o + 7).rgb, 1.0) * ((bd.x - bd.y) * B0.z), r);\n"
    "        r = psy_over_(vec4(psy_vd_(o + 9).rgb, 1.0) * ((bd.z - bd.w) * B1.z), r);\n"
    "        acc = r;\n"
    "    }\n"
    "    psy_out = acc * (V2.x * V2.y);\n"
    "}\n";

static const char* const psygfx__glsl_vec[] = { psygfx__glsl_vec0, psygfx__glsl_vec1, psygfx__glsl_vec2, psygfx__glsl_vec3, psygfx__glsl_vec4, psygfx__glsl_vec5 };

static const char psygfx__glsl_output[] =
    "uniform sampler2D psy_tex0;\n"   /* the scene                          */
    "uniform sampler2D psy_tex1;\n"   /* the CLUT, n x 3                    */
    "out vec4 psy_out;\n"
    "uint psy_hash(uint v) {\n"
    "    uint s = v * 747796405u + 2891336453u;\n"
    "    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;\n"
    "    return (w >> 22u) ^ w;\n"
    "}\n"
    "void main() {\n"
    "    ivec2 q = ivec2(gl_FragCoord.xy);\n"
    "    if (psy_outp.w > 0) q.y = psy_outp.w - 1 - q.y;\n"
    "    vec3 v = clamp(texelFetch(psy_tex0, q, 0).rgb, 0.0, 1.0);\n"
    "    int n = psy_outp.x;\n"
    "    float mx = float(psy_outp.y);\n"
    "    vec3 d;\n"
    "    for (int c = 0; c < 3; c++) {\n"
    "        float x = v[c] * float(n - 1);\n"
    "        int i0 = min(int(x), n - 2);\n"
    "        float a = texelFetch(psy_tex1, ivec2(i0, c), 0).r;\n"
    "        float b = texelFetch(psy_tex1, ivec2(i0 + 1, c), 0).r;\n"
    "        d[c] = a + (b - a) * (x - float(i0));\n"
    "    }\n"
    "    float t = 0.5;\n"
    "    if (psy_outp.z == 1) {\n"   /* Bayer 8 x 8 by bit interleaving */
    "        int x = q.x & 7, y = q.y & 7, z = x ^ y;\n"
    "        int b = ((z & 1) << 5) | ((x & 1) << 4) | ((z & 2) << 2) | ((x & 2) << 1) | ((z & 4) >> 1) | ((x & 4) >> 2);\n"
    "        t = (float(b) + 0.5) / 64.0;\n"
    "    } else if (psy_outp.z == 2) {\n"
    "        uint h = psy_hash(uint(q.x) ^ psy_hash(uint(q.y) ^ psy_hash(psy_seed.x ^ psy_hash(psy_seed.z))));\n"
    "        t = (float(h >> 8u) + 0.5) * (1.0 / 16777216.0);\n"
    "    }\n"
    "    vec3 code = clamp(floor(d * mx + t), 0.0, mx);\n"
    "    psy_out = vec4(code / mx, 1.0);\n"
    "}\n";

static const char psygfx__glsl_vs_output[] =
    "void main() {\n"
    "    vec2 c = vec2(float((gl_VertexID & 1) << 2), float((gl_VertexID & 2) << 1)) - 1.0;\n"
    "    gl_Position = vec4(c, 0.0, 1.0);\n"
    "}\n";

/* Appends to a buffer; the total length is counted even when it overflows. */
typedef struct psygfx__sb { char* p; size_t cap, n; } psygfx__sb;
static void psygfx__sb_add(psygfx__sb* b, const char* s) {
    size_t k = strlen(s);
    if (b->p && b->n + k < b->cap) memcpy(b->p + b->n, s, k + 1);
    b->n += k;
}

/* defs go after the common header (#version must come first); a mode
 * below 0: the body has its own main (the vector program). */
static size_t psygfx__fs_text2(const char* defs, const char* body, int mode, char* out, size_t cap) {
    psygfx__sb b;
    b.p = out; b.cap = cap; b.n = 0;
    if (out && cap) out[0] = '\0';
    psygfx__sb_add(&b, psygfx__glsl_common);
    psygfx__sb_add(&b, defs);
    psygfx__sb_add(&b, psygfx__glsl_access);
    psygfx__sb_add(&b, psygfx__glsl_fs_lib);
    psygfx__sb_add(&b, psygfx__glsl_fs_lib2);
    /* The compiler's log then gives the body's own line numbers. */
    psygfx__sb_add(&b, "#line 1\n");
    psygfx__sb_add(&b, body);
    psygfx__sb_add(&b, "\n");
    if (mode >= 0)
        psygfx__sb_add(&b, mode == PSYGFX_COLOR ? psygfx__glsl_main_color
                           : (mode == PSYGFX_ADD ? psygfx__glsl_main_add : psygfx__glsl_main_mod));
    return b.n;
}

static size_t psygfx__fs_text(const char* body, psygfx_shader_mode mode, char* out, size_t cap) {
    return psygfx__fs_text2("", body, (int)mode, out, cap);
}

PSYGFX_API int psygfx_shader_wrap(const char* body, psygfx_shader_mode mode, char* out, size_t cap) {
    size_t n;
    if (!body) return PSYGFX_ERR_ARG;
    n = psygfx__fs_text(body, mode, out, cap);
    if (!out || n >= cap) return -1000 - (int)n;
    return (int)n;
}

/* --- core ------------------------------------------------------------------ */

enum {
    PSYGFX__B_SHAPE, PSYGFX__B_GRATING, PSYGFX__B_GABOR, PSYGFX__B_NOISE,
    PSYGFX__B_IMAGE_COLOR, PSYGFX__B_IMAGE_MOD, PSYGFX__B_DOTS, PSYGFX__B_IMAGE_ADD,
    PSYGFX__B_VECTOR, PSYGFX__B_GLYPHS
};

/* Test seam, not API: the pixel test sets it before open to read the scene
 * in float32. */
static int psygfx__test_scene32 = 0;

#define PSYGFX__BLOCK 256u
#define PSYGFX__RES_TARGET 0x1     /* psygfx__res.flags: a render target */
/* The local y axis sign the coordinate convention gives (psygfx__place). */
#define PSYGFX__Y_SIGN (-1.0f)
/* Frame blocks at the start of the staging: the scene's, then one per
 * target pass; the stim blocks follow. */
#define PSYGFX__FRAME_SLOTS (1 + PSYGFX_MAX_PASSES)
#define PSYGFX__FRAME_BYTES ((uint32_t)PSYGFX__FRAME_SLOTS * PSYGFX__BLOCK)
#define PSYGFX__STIM_RANGE ((uint32_t)PSYGFX__BATCH_VEC4 * 16u)   /* the block as declared */

/* inst: 0 none, 1 dots (a vec2 per instance), 2 glyphs (two vec4); mode
 * below 0: the body has its own main and blends OVER. */
static int psygfx__make_pipe(psygfx_gfx* g, const char* defs, const char* body, int mode, int inst, int deferred,
                             uint32_t* id, const char* name) {
    psygfx_pipeline_src src;
    size_t nv, nf;
    char* vs;
    char* fs;
    int rc;
    nv = strlen(psygfx__glsl_common) + strlen(defs) + strlen(psygfx__glsl_vs_body) + 1;
    nf = psygfx__fs_text2(defs, body, mode, NULL, 0) + 1;
    vs = (char*)malloc(nv + nf);
    if (!vs) { psygfx__set_error(g->error, sizeof g->error, "psy_gfx: out of memory"); return PSYGFX_ERR_FULL; }
    fs = vs + nv;
    /* #version must come first, so the defines go after the common header. */
    snprintf(vs, nv, "%s%s%s", psygfx__glsl_common, defs, psygfx__glsl_vs_body);
    psygfx__fs_text2(defs, body, mode, fs, nf);
    memset(&src, 0, sizeof src);
    src.vs = vs;
    src.fs = fs;
    src.blend = (mode == PSYGFX_COLOR || mode < 0) ? PSYGFX_BLEND_OVER : PSYGFX_BLEND_ADD;   /* ADD and MODULATION add */
    src.instance_attr = inst;
    src.deferred = deferred && g->be->pipeline_finish ? 1 : 0;
    rc = g->be->pipeline_make(g->bctx, &src, id, g->error, sizeof g->error);
    free(vs);
    if (rc < 0 && name) {
        char tmp[sizeof g->error];
        memcpy(tmp, g->error, sizeof tmp);
        psygfx__set_error(g->error, sizeof g->error, "%s (%s)", tmp, name);
    }
    return rc;
}

static int psygfx__make_output(psygfx_gfx* g) {
    psygfx_pipeline_src src;
    size_t nv = strlen(psygfx__glsl_common) + strlen(psygfx__glsl_vs_output) + 1;
    size_t nf = strlen(psygfx__glsl_common) + strlen(psygfx__glsl_output) + 1;
    char* vs = (char*)malloc(nv + nf);
    int rc;
    if (!vs) return PSYGFX_ERR_FULL;
    snprintf(vs, nv, "%s%s", psygfx__glsl_common, psygfx__glsl_vs_output);
    snprintf(vs + nv, nf, "%s%s", psygfx__glsl_common, psygfx__glsl_output);
    memset(&src, 0, sizeof src);
    src.vs = vs;
    src.fs = vs + nv;
    src.blend = PSYGFX_BLEND_NONE;
    src.deferred = g->be->pipeline_finish ? 1 : 0;
    rc = g->be->pipeline_make(g->bctx, &src, &g->output_pipe, g->error, sizeof g->error);
    free(vs);
    return rc;
}

static void psygfx__push(psygfx_gfx* g, uint16_t kind, uint64_t t, uint32_t aux, const uint32_t* u, int n, int64_t i64) {
    psyrt_event ev;
    int i;
    if (!g->ring) return;
    memset(&ev, 0, sizeof ev);
    ev.source = (uint16_t)PSYRT_SRC_GFX;
    ev.kind = kind;
    ev.t_ns = t;
    ev.aux = aux;
    if (u) for (i = 0; i < n && i < 16; i++) ev.u.u32[i] = u[i];
    else ev.u.i64[0] = i64;
    psyrt_ring_push(g->ring, &ev);
}

static uint32_t psygfx__crc32(const void* data, size_t n) {
    const unsigned char* p = (const unsigned char*)data;
    uint32_t c = 0xFFFFFFFFu;
    size_t i;
    int k;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return c ^ 0xFFFFFFFFu;
}

static int psygfx__lut_upload(psygfx_gfx* g, const float* lut, int n) {
    return g->be->texture_update(g->bctx, g->lut, 0, 0, n, 3, lut, (size_t)n * sizeof(float));
}

static int psygfx__inv3(const double* m, double* out);
static void psygfx__mul3(const double* m, const double* v, double* out);
static int psygfx__has_lms(const psygfx_cal* c);

static void psygfx__mm3(const double* a, const double* b, double* out) {
    int i, j, k;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) {
            out[i * 3 + j] = 0.0;
            for (k = 0; k < 3; k++) out[i * 3 + j] += a[i * 3 + k] * b[k * 3 + j];
        }
}

/* PAINT's spaces: Oklab's cone response (before the cube root) from device
 * RGB, through the calibration's XYZ scaled to a white of Y = 1 (Ottosson's
 * M1, no chromatic adaptation); DKL about desc.background. */
static void psygfx__paint_spaces(psygfx_gfx* g, const psygfx_cal* c) {
    static const double m1[9] = { 0.8189330101, 0.3618667424, -0.1288597137, 0.0329845436, 0.9293118715, 0.0361456387,
                                  0.0482003018, 0.2643662691, 0.6338517070 };
    double yw = c->rgb_to_xyz[3] + c->rgb_to_xyz[4] + c->rgb_to_xyz[5], xn[9], m[9];
    int k;
    if (yw > 0.0 && c->rgb_to_xyz[4] != 0.0) {
        for (k = 0; k < 9; k++) xn[k] = c->rgb_to_xyz[k] / yw;
        psygfx__mm3(m1, xn, g->ok_rgb2lms);
        g->has_oklab = psygfx__inv3(g->ok_rgb2lms, g->ok_lms2rgb) ? 1 : 0;
    }
    if (psygfx__has_lms(c) && psygfx_cal_dkl_matrix(c, g->bg, m) == PSYGFX_OK) {
        psygfx__mm3(m, c->rgb_to_lms, g->rgb2dkl);
        g->has_dkl = psygfx__inv3(g->rgb2dkl, g->dkl2rgb) ? 1 : 0;
    }
}

PSYGFX_API bool psygfx_open(psygfx_gfx* g, const psygfx_desc* d) {
    psygfx_backend_open in;
    psygfx_texture_src ts;
    int rc, i;
    static const float identity[6] = { 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f };
    /* the defines change what a program holds, not what it computes:
     * SHAPE's offset is 0 for a v0.2 stimulus, and d - 0 is d */
    struct { const char* defs; const char* body; int mode; int inst; const char* name; } builtins[] = {
        { "#define PSY_OFFSET 1\n", psygfx__body_shape, PSYGFX_COLOR, 0, "shape" },
        { "", psygfx__body_grating, PSYGFX_MODULATION, 0, "grating" },
        { "", psygfx__body_gabor,   PSYGFX_MODULATION, 0, "gabor" },
        { "", psygfx__body_noise,   PSYGFX_MODULATION, 0, "noise" },
        { "", NULL,                 PSYGFX_COLOR,      0, "image" },
        { "", NULL,                 PSYGFX_MODULATION, 0, "image modulation" },
        { "#define PSY_DOTS 1\n", psygfx__body_dots, PSYGFX_COLOR, 1, "dots" },
        { "", NULL,                 PSYGFX_ADD,        0, "image add" },
        { "#define PSY_VECTOR 1\n", NULL, -1, 0, "vector" },
        { "#define PSY_GLYPHS 1\n#define PSY_OFFSET 1\n", psygfx__body_shape, PSYGFX_COLOR, 2, "glyphs" },
    };
    if (!g) return false;
    if (g->open) { psygfx__set_error(g->error, sizeof g->error, "psy_gfx: already open"); return false; }
    memset(g, 0, sizeof *g);
    if (!d) { psygfx__set_error(g->error, sizeof g->error, "psy_gfx: no desc"); return false; }
    if (d->output != PSYGFX_OUT_8) {
        psygfx__set_error(g->error, sizeof g->error, "psy_gfx: only 8-bit output in v0.1: 10-bit needs an "
                          "RGB10A2 back buffer from psy_screen, Mono++ and Color++ need a device to verify");
        return false;
    }
    if (d->stereo != PSYGFX_MONO) {
        psygfx__set_error(g->error, sizeof g->error, "psy_gfx: only MONO in v0.1");
        return false;
    }
    if (d->units == PSYGFX_DEG && (d->view.distance_mm <= 0 || d->view.width_mm <= 0)) {
        psygfx__set_error(g->error, sizeof g->error, "psy_gfx: PSYGFX_DEG needs view.distance_mm and view.width_mm");
        return false;
    }
    for (i = 0; i < 3; i++) {
        if (!(d->background[i] >= 0.0f && d->background[i] <= 1.0f)) {
            psygfx__set_error(g->error, sizeof g->error, "psy_gfx: background outside 0..1");
            return false;
        }
    }
    if (d->cal) {
        char e[200];
        if (psygfx_cal_check(d->cal, e, sizeof e) < 0) {
            psygfx__set_error(g->error, sizeof g->error, "psy_gfx: calibration: %s", e);
            return false;
        }
    }
    memset(&in, 0, sizeof in);
    g->screen = d->screen;
    if (d->screen) {
        psyscr_caps sc;
        if (!psyscr_is_open(d->screen)) { psygfx__set_error(g->error, sizeof g->error, "psy_gfx: the screen is not open"); return false; }
        psyscr_get_caps(d->screen, &sc);
        g->w = sc.mode.w; g->h = sc.mode.h;
        /* The simulated display has no size: desc.width, height, or 800 x 600. */
        if (g->w <= 0 || g->h <= 0) { g->w = d->width > 0 ? d->width : 800; g->h = d->height > 0 ? d->height : 600; }
        /* ANGLE's client-buffer pbuffer puts GL's row 0 at the top of the
         * D3D texture (probe, docs/psy_gfx.md). */
        g->origin_top = sc.backend == PSYSCR_BACKEND_DXGI_FLIP || sc.backend == PSYSCR_BACKEND_COMPOSITION;
        if (psyscr_gl_proc(d->screen, "glGetString")) {
            in.gl_proc = (psyscr_proc (*)(void*, const char*))psyscr_gl_proc;
            in.gl_ctx = d->screen;
        }
    } else if (d->gl_proc) {
        in.gl_proc = d->gl_proc;
        in.gl_ctx = d->gl_ctx;
        g->w = d->width; g->h = d->height;
    } else {
        psygfx__set_error(g->error, sizeof g->error, "psy_gfx: desc.screen or desc.gl_proc is required");
        return false;
    }
    if (d->rows == PSYGFX_ROWS_BOTTOM_UP) g->origin_top = 0;
    if (d->rows == PSYGFX_ROWS_TOP_DOWN) g->origin_top = 1;
    if (g->w <= 0 || g->h <= 0) { psygfx__set_error(g->error, sizeof g->error, "psy_gfx: no framebuffer size"); return false; }
    in.w = g->w; in.h = g->h;
    if (d->backend) { g->be = d->backend; g->bctx = d->backend_ctx; }
    else if (in.gl_proc) { g->be = &psygfx__gl_backend; g->bctx = g->backend_mem; }
    else { g->be = &psygfx__null_backend; g->bctx = g->backend_mem; }
    if (g->be->version != PSYGFX_BACKEND_VERSION) {
        psygfx__set_error(g->error, sizeof g->error, "psy_gfx: backend version %u, want %u", g->be->version, PSYGFX_BACKEND_VERSION);
        return false;
    }
    rc = g->be->open(g->bctx, &in, &g->caps, g->error, sizeof g->error);
    if (rc < 0) { if (!g->error[0]) psygfx__set_error(g->error, sizeof g->error, "psy_gfx: backend open failed"); return false; }
    g->open = 1;   /* from here psygfx_close() undoes what was made */
#if defined(PSYSCR_HAS_GL_EPOCH)
    if (g->screen) { g->epoch = psyscr_gl_epoch(g->screen); g->generation = psyscr_gl_generation(g->screen); }
#endif
    if (!g->caps.color_buffer_float) {
        psygfx__set_error(g->error, sizeof g->error, "psy_gfx: GL_EXT_color_buffer_float is missing (%s)", g->caps.renderer);
        goto fail;
    }
    /* RGBA16F: an RGBA32F scene took 1.4 to 1.7 times the GPU time on
     * blend-heavy frames and gives nothing at 8-bit output (docs/psy_gfx.md).
     * The test reads RGBA32F to measure shader error below half precision. */
    g->scene_format = psygfx__test_scene32 ? PSYGFX_RGBA32F : PSYGFX_RGBA16F;
    if (g->scene_format == PSYGFX_RGBA32F && !g->caps.float_blend) {
        psygfx__set_error(g->error, sizeof g->error, "psy_gfx: an RGBA32F scene needs GL_EXT_float_blend (%s)", g->caps.renderer);
        goto fail;
    }
    if (g->caps.ubo_align > (int32_t)PSYGFX__BLOCK || g->caps.max_ubo < (int32_t)PSYGFX__STIM_RANGE) {
        psygfx__set_error(g->error, sizeof g->error, "psy_gfx: uniform buffer alignment %d or size %d not supported",
                          g->caps.ubo_align, g->caps.max_ubo);
        goto fail;
    }
    for (i = 0; i < 3; i++) g->bg[i] = d->background[i];
    g->ppu = 1.0f;
    if (d->units == PSYGFX_DEG)
        g->ppu = (float)((double)g->w / d->view.width_mm * d->view.distance_mm * tan(3.14159265358979323846 / 180.0));
    g->dither = d->dither;
    g->seed = d->seed;
    g->ring = d->ring;
    g->max_draws = d->max_draws > 0 ? d->max_draws : 1024;
    g->max_batch = PSYGFX__BATCH;
    g->t_open = (int64_t)psyrt_now_ns();
    /* The one allocation: the frame's uniform staging and its draw queue. */
    g->staging = (unsigned char*)calloc((size_t)PSYGFX__FRAME_BYTES + ((size_t)g->max_draws + 16) * PSYGFX__BLOCK, 1);
    g->cur_w = g->w; g->cur_h = g->h; g->cur_top = 0;
    g->cmds = (psygfx__cmd*)calloc((size_t)g->max_draws, sizeof(psygfx__cmd));
    if (!g->staging || !g->cmds) { psygfx__set_error(g->error, sizeof g->error, "psy_gfx: out of memory"); goto fail; }
    /* A range starts at any block and is as long as the declared array. */
    g->ubo_bytes = PSYGFX__FRAME_BYTES + ((uint32_t)g->max_draws + 16) * PSYGFX__BLOCK + PSYGFX__STIM_RANGE;
    for (i = 0; i < PSYGFX__UBO_RING; i++) {
        if (g->be->buffer_make(g->bctx, PSYGFX_BUFFER_UNIFORM, g->ubo_bytes, &g->ubo[i]) < 0) {
            psygfx__set_error(g->error, sizeof g->error, "psy_gfx: uniform buffer of %u bytes", g->ubo_bytes);
            goto fail;
        }
    }
    memset(&ts, 0, sizeof ts);
    ts.w = g->w; ts.h = g->h; ts.format = g->scene_format; ts.target = true;
    if (g->be->texture_make(g->bctx, &ts, &g->scene) < 0) {
        psygfx__set_error(g->error, sizeof g->error, "psy_gfx: scene target %dx%d", g->w, g->h);
        goto fail;
    }
    memset(&ts, 0, sizeof ts);
    ts.w = PSYGFX_CAL_MAX_LUT; ts.h = 3; ts.format = PSYGFX_R32F;
    if (g->be->texture_make(g->bctx, &ts, &g->lut) < 0) {
        psygfx__set_error(g->error, sizeof g->error, "psy_gfx: CLUT texture");
        goto fail;
    }
    if (d->cal) {
        int n = d->cal->lut_n ? d->cal->lut_n : PSYGFX_CAL_MAX_LUT, c;
        float* tmp = (float*)malloc((size_t)n * 3 * sizeof(float));
        if (!tmp) { psygfx__set_error(g->error, sizeof g->error, "psy_gfx: out of memory"); goto fail; }
        for (c = 0; c < 3; c++) memcpy(tmp + c * n, d->cal->lut[c], (size_t)n * sizeof(float));
        rc = psygfx__lut_upload(g, tmp, n);
        free(tmp);
        g->lut_n = n;
        g->cal_crc = d->cal->crc;
    } else {
        rc = psygfx__lut_upload(g, identity, 2);
        g->lut_n = 2;
    }
    if (rc < 0) { psygfx__set_error(g->error, sizeof g->error, "psy_gfx: CLUT upload"); goto fail; }
    if (d->cal) psygfx__paint_spaces(g, d->cal);
    for (i = 0; i < (int)(sizeof builtins / sizeof builtins[0]); i++) {
        char body[sizeof psygfx__body_image_fetch + sizeof psygfx__body_image_color + 16];
        const char* src = builtins[i].body;
        char* vec = NULL;
        if (!src && builtins[i].mode < 0) {   /* the vector program, from its pieces */
            size_t n = 1, k;
            for (k = 0; k < sizeof psygfx__glsl_vec / sizeof psygfx__glsl_vec[0]; k++) n += strlen(psygfx__glsl_vec[k]);
            vec = (char*)malloc(n);
            if (!vec) { psygfx__set_error(g->error, sizeof g->error, "psy_gfx: out of memory"); goto fail; }
            n = 0;
            for (k = 0; k < sizeof psygfx__glsl_vec / sizeof psygfx__glsl_vec[0]; k++) {
                size_t m = strlen(psygfx__glsl_vec[k]);
                memcpy(vec + n, psygfx__glsl_vec[k], m);
                n += m;
            }
            vec[n] = '\0';
            src = vec;
        } else if (!src) {
            snprintf(body, sizeof body, "%s%s", psygfx__body_image_fetch,
                     builtins[i].mode == PSYGFX_COLOR ? psygfx__body_image_color
                     : (builtins[i].mode == PSYGFX_ADD ? psygfx__body_image_add : psygfx__body_image_mod));
            src = body;
        }
        rc = psygfx__make_pipe(g, builtins[i].defs, src, builtins[i].mode, builtins[i].inst, 1, &g->builtin[i], builtins[i].name);
        free(vec);
        if (rc < 0) goto fail;
    }
    if (psygfx__make_output(g) < 0) goto fail;
    if (g->be->pipeline_finish) {   /* the statuses, once every program is under way */
        for (i = 0; i < (int)(sizeof builtins / sizeof builtins[0]); i++) {
            if (g->be->pipeline_finish(g->bctx, g->builtin[i], g->error, sizeof g->error) < 0) {
                char tmp[sizeof g->error];
                g->builtin[i] = 0;
                memcpy(tmp, g->error, sizeof tmp);
                psygfx__set_error(g->error, sizeof g->error, "%s (%s)", tmp, builtins[i].name);
                goto fail;
            }
        }
        if (g->be->pipeline_finish(g->bctx, g->output_pipe, g->error, sizeof g->error) < 0) { g->output_pipe = 0; goto fail; }
    }
    g->be->reset(g->bctx);
    g->error[0] = '\0';
    {
        uint32_t u[7];
        u[0] = (uint32_t)g->lut_n; u[1] = (uint32_t)g->dither; u[2] = (uint32_t)d->output;
        u[3] = g->cal_crc; u[4] = d->cal ? d->cal->flags : 0u; u[5] = (uint32_t)g->w; u[6] = (uint32_t)g->h;
        psygfx__push(g, (uint16_t)PSYGFX_EV_OPEN, 0, (uint32_t)g->scene_format, u, 7, 0);
    }
    return true;
fail:
    {
        char keep[sizeof g->error];
        memcpy(keep, g->error, sizeof keep);
        psygfx_close(g);
        memcpy(g->error, keep, sizeof keep);
    }
    return false;
}

PSYGFX_API void psygfx_close(psygfx_gfx* g) {
    if (!g) return;
    if (g->open && g->be) {
        if (g->screen) psyscr_bind(g->screen);
        g->be->close(g->bctx);
    }
    free(g->staging);
    free(g->cmds);
    {
        char keep[sizeof g->error];
        memcpy(keep, g->error, sizeof keep);
        memset(g, 0, sizeof *g);
        memcpy(g->error, keep, sizeof keep);
    }
}

PSYGFX_API const char* psygfx_error(const psygfx_gfx* g) { return g ? g->error : "psy_gfx: no handle"; }
PSYGFX_API bool psygfx_is_open(const psygfx_gfx* g) { return g && g->open; }
PSYGFX_API uint64_t psygfx_clipped(const psygfx_gfx* g) { return g ? g->clipped_total : 0; }

PSYGFX_API int psygfx_describe(const psygfx_gfx* g, char* buf, size_t cap) {
    static const char* const dith[] = { "none", "ordered", "noise" };
    if (!g || !g->open) return snprintf(buf, cap, "psy_gfx: closed");
    return snprintf(buf, cap, "psy_gfx %s: %s, %s, %dx%d, scene %s, CLUT %d%s, dither %s, %.4g px/unit, origin %s",
                    PSYGFX_VERSION_STRING, g->be->name, g->caps.renderer, g->w, g->h,
                    g->scene_format == PSYGFX_RGBA32F ? "RGBA32F" : "RGBA16F", g->lut_n,
                    g->cal_crc ? " (calibration)" : " (identity)", dith[g->dither <= 2 ? g->dither : 0],
                    (double)g->ppu, g->origin_top ? "top" : "bottom");
}

PSYGFX_API void psygfx_reset_state(psygfx_gfx* g) {
    if (g && g->open) g->be->reset(g->bctx);
}

/* --- stimuli ---------------------------------------------------------------- */

static void psygfx__stim_defaults(psygfx_stim* s, int kind) {
    memset(s, 0, sizeof *s);
    s->kind = (uint16_t)kind;
    s->ax = 0.5f;
    s->ay = 0.5f;
    s->visible = 1.0f;
    s->contrast = 1.0f;
    s->opacity = 1.0f;
    s->gate = 1.0f;
    s->aspect = 1.0f;
    s->check = 1.0f;
    s->tint[0] = s->tint[1] = s->tint[2] = s->tint[3] = 1.0f;
}

static void psygfx__copy3(float* dst, const float* src) { dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; }

/* A desc's place and anchor into the stimulus. */
static void psygfx__align(psygfx_stim* s, psygfx_align place, psygfx_align anchor) {
    static const float fx[9] = { 0.5f, 0.0f, 0.5f, 1.0f, 0.0f, 1.0f, 0.0f, 0.5f, 1.0f };
    static const float fy[9] = { 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f };
    int a = (int)anchor >= 0 && (int)anchor < 9 ? (int)anchor : 0;
    s->place = (uint8_t)((int)place >= 0 && (int)place < 9 ? (int)place : 0);
    s->ax = fx[a];
    s->ay = fy[a];
}

PSYGFX_API psygfx_stim psygfx_shape(const psygfx_shape_desc* d) {
    psygfx_stim s;
    int i;
    psygfx__stim_defaults(&s, PSYGFX_SHAPE);
    if (!d) return s;
    psygfx__align(&s, d->place, d->anchor);
    s.shape = (uint16_t)d->shape; s.edge = (uint16_t)d->edge; s.edge_width = d->edge_width;
    s.x = d->x; s.y = d->y; s.w = d->w; s.h = d->h ? d->h : d->w; s.ori = d->ori;
    psygfx__copy3(s.color, d->color);
    if (d->opacity != 0.0f) s.opacity = d->opacity;
    for (i = 0; i < 4; i++) s.shape_p[i] = d->shape_p[i];
    if (d->shape == PSYGFX_POLYGON && d->vertices) {
        int n = (int)d->shape_p[0];
        if (n > 16) n = 16;
        for (i = 0; i < 2 * n; i++) s.p[i] = d->vertices[i];
    }
    s.stroke = d->stroke; s.stroke_align = (uint8_t)d->stroke_align; s.join = (uint8_t)d->join;
    s.miter_limit = d->miter_limit; s.mask = d->mask; s.group = d->group;
    s.offset = d->offset; s.dash[0] = d->dash[0]; s.dash[1] = d->dash[1]; s.dash_offset = d->dash_offset;
    s.trim[0] = d->trim[0]; s.trim[1] = d->trim[1];
    s.cap = (uint8_t)d->cap; s.blend = (uint8_t)d->blend; s.dash_snap = (uint8_t)(d->dash_snap ? 1 : 0);
    s.path = d->path; s.n_path = d->n_path > 0 ? (uint32_t)d->n_path : 0u;
    s.fx = d->fx; s.paint = d->paint;
    if (d->shape == PSYGFX_STAR && d->shape_p[1] == 0.0f) s.shape_p[1] = 0.5f;
    if (d->shape == PSYGFX_CAPSULE && d->h == 0.0f) s.h = 2.0f * (d->shape_p[0] > d->shape_p[1] ? d->shape_p[0] : d->shape_p[1]);
    return s;
}

PSYGFX_API psygfx_stim psygfx_compound(const psygfx_compound_desc* d) {
    psygfx_stim s;
    psygfx__stim_defaults(&s, PSYGFX_SHAPE);
    s.shape = PSYGFX_COMPOUND;
    if (!d) return s;
    psygfx__align(&s, d->place, d->anchor);
    s.prims = d->prims; s.n_prims = d->n > 0 ? (uint32_t)d->n : 0u;
    s.x = d->x; s.y = d->y; s.w = d->w; s.h = d->h ? d->h : d->w; s.ori = d->ori;
    s.shape_p[3] = d->onion;   /* a compound's onion on the result */
    s.edge = (uint16_t)d->edge; s.edge_width = d->edge_width;
    psygfx__copy3(s.color, d->color);
    if (d->opacity != 0.0f) s.opacity = d->opacity;
    s.stroke = d->stroke; s.stroke_align = (uint8_t)d->stroke_align; s.offset = d->offset;
    s.blend = (uint8_t)d->blend; s.fx = d->fx; s.paint = d->paint; s.group = d->group;
    return s;
}

PSYGFX_API psygfx_stim psygfx_glyphs(const psygfx_glyphs_desc* d) {
    psygfx_stim s;
    psygfx__stim_defaults(&s, PSYGFX_SHAPE);
    s.shape = PSYGFX_MASK_TEX;
    if (!d) return s;
    psygfx__align(&s, d->place, d->anchor);
    s.mask = d->atlas; s.buf = d->buf; s.count = d->count;
    s.shape_p[0] = d->scale;   /* units per atlas texel */
    s.x = d->x; s.y = d->y; s.w = d->w; s.h = d->h; s.ori = d->ori;
    s.edge = (uint16_t)d->edge; s.edge_width = d->edge_width;
    psygfx__copy3(s.color, d->color);
    if (d->opacity != 0.0f) s.opacity = d->opacity;
    s.stroke = d->stroke; s.stroke_align = (uint8_t)d->stroke_align; s.join = (uint8_t)d->join; s.offset = d->offset;
    s.blend = (uint8_t)d->blend; s.group = d->group;
    return s;
}

PSYGFX_API psygfx_stim psygfx_grating(const psygfx_grating_desc* d) {
    psygfx_stim s;
    psygfx__stim_defaults(&s, PSYGFX_GRATING);
    if (!d) return s;
    psygfx__align(&s, d->place, d->anchor);
    s.x = d->x; s.y = d->y; s.w = d->w; s.h = d->h ? d->h : d->w; s.ori = d->ori;
    s.sf = d->sf; s.phase = d->phase;
    if (d->contrast != 0.0f) s.contrast = d->contrast;
    s.wave = (uint16_t)(d->square ? 1 : 0);
    s.shape = (uint16_t)d->aperture; s.edge = (uint16_t)d->edge; s.edge_width = d->edge_width;
    psygfx__copy3(s.dir, d->dir);
    s.stroke = d->stroke; s.stroke_align = (uint8_t)d->stroke_align; s.mask = d->mask; s.group = d->group;
    return s;
}

PSYGFX_API psygfx_stim psygfx_gabor(const psygfx_gabor_desc* d) {
    psygfx_stim s;
    psygfx__stim_defaults(&s, PSYGFX_GABOR);
    s.shape = PSYGFX_NO_APERTURE;
    if (!d) return s;
    psygfx__align(&s, d->place, d->anchor);
    s.x = d->x; s.y = d->y; s.ori = d->ori; s.sf = d->sf; s.phase = d->phase; s.sigma = d->sigma;
    if (d->contrast != 0.0f) s.contrast = d->contrast;
    if (d->aspect != 0.0f) s.aspect = d->aspect;
    s.w = d->size;
    s.h = d->size;
    psygfx__copy3(s.dir, d->dir);
    s.group = d->group;
    return s;
}

PSYGFX_API psygfx_stim psygfx_dots(const psygfx_dots_desc* d) {
    psygfx_stim s;
    psygfx__stim_defaults(&s, PSYGFX_DOTS);
    s.shape = PSYGFX_NO_APERTURE;
    if (!d) return s;
    psygfx__align(&s, d->place, d->anchor);
    s.buf = d->buf; s.count = d->count; s.x = d->x; s.y = d->y; s.ori = d->ori;
    s.dot_size = d->dot_size; s.edge = (uint16_t)d->edge; s.edge_width = d->edge_width;
    psygfx__copy3(s.color, d->color);
    if (d->opacity != 0.0f) s.opacity = d->opacity;
    s.shape = (uint16_t)(d->aperture == PSYGFX_RECT || d->aperture == PSYGFX_CIRCLE ? d->aperture : PSYGFX_NO_APERTURE);
    s.w = d->w; s.h = d->h ? d->h : d->w;
    s.stroke = d->stroke; s.stroke_align = (uint8_t)d->stroke_align; s.group = d->group;
    return s;
}

PSYGFX_API psygfx_stim psygfx_image(const psygfx_gfx* g, const psygfx_image_desc* d) {
    psygfx_stim s;
    int i;
    psygfx__stim_defaults(&s, PSYGFX_IMAGE);
    s.shape = PSYGFX_NO_APERTURE;
    if (!d) return s;
    psygfx__align(&s, d->place, d->anchor);
    s.tex = d->tex; s.x = d->x; s.y = d->y; s.ori = d->ori;
    s.w = d->w; s.h = d->h;
    for (i = 0; i < 4; i++) s.src[i] = d->src[i];
    if (d->tint[0] != 0.0f || d->tint[1] != 0.0f || d->tint[2] != 0.0f || d->tint[3] != 0.0f)
        for (i = 0; i < 4; i++) s.tint[i] = d->tint[i];
    if (g && d->tex.id >= 1 && d->tex.id <= PSYGFX_MAX_TEXTURES) {
        const psygfx__res* r = &g->tex[d->tex.id - 1];
        float sw = s.src[2] > 0 && s.src[3] > 0 ? s.src[2] : (float)r->w;
        float sh = s.src[2] > 0 && s.src[3] > 0 ? s.src[3] : (float)r->h;
        if (s.w == 0) s.w = sw / g->ppu;
        if (s.h == 0) s.h = sh / g->ppu;
        if (r->flags & PSYGFX__RES_TARGET) s.flags |= PSYGFX_STIM_PREMULTIPLIED;
    }
    if (d->modulation) s.flags |= PSYGFX_STIM_MODULATION;
    if (d->add) s.flags |= PSYGFX_STIM_ADD;
    if (d->linear) s.flags |= PSYGFX_STIM_LINEAR;
    if (d->contrast != 0.0f) s.contrast = d->contrast;
    if (d->opacity != 0.0f) s.opacity = d->opacity;
    psygfx__copy3(s.dir, d->dir);
    s.group = d->group;
    return s;
}

PSYGFX_API psygfx_stim psygfx_noise(const psygfx_noise_desc* d) {
    psygfx_stim s;
    psygfx__stim_defaults(&s, PSYGFX_NOISE);
    if (!d) return s;
    psygfx__align(&s, d->place, d->anchor);
    s.x = d->x; s.y = d->y; s.w = d->w; s.h = d->h ? d->h : d->w; s.ori = d->ori;
    s.check = d->check > 0 ? d->check : 1.0f;
    s.seed = (float)(d->seed & 0xFFFFFFu);
    s.wave = (uint16_t)d->dist;
    if (d->contrast != 0.0f) s.contrast = d->contrast;
    s.shape = (uint16_t)d->aperture; s.edge = (uint16_t)d->edge; s.edge_width = d->edge_width;
    psygfx__copy3(s.dir, d->dir);
    s.stroke = d->stroke; s.stroke_align = (uint8_t)d->stroke_align; s.mask = d->mask; s.group = d->group;
    return s;
}

PSYGFX_API psygfx_stim psygfx_user(const psygfx_user_desc* d) {
    psygfx_stim s;
    int i;
    psygfx__stim_defaults(&s, PSYGFX_USER);
    if (!d) return s;
    psygfx__align(&s, d->place, d->anchor);
    s.pipe = d->pipe; s.x = d->x; s.y = d->y; s.w = d->w; s.h = d->h ? d->h : d->w; s.ori = d->ori;
    if (d->contrast != 0.0f) s.contrast = d->contrast;
    if (d->opacity != 0.0f) s.opacity = d->opacity;
    s.shape = (uint16_t)d->aperture; s.edge = (uint16_t)d->edge; s.edge_width = d->edge_width;
    psygfx__copy3(s.color, d->color);
    psygfx__copy3(s.dir, d->dir);
    if (d->p) for (i = 0; i < d->n_p && i < 32; i++) s.p[i] = d->p[i];
    s.stroke = d->stroke; s.stroke_align = (uint8_t)d->stroke_align; s.mask = d->mask; s.group = d->group;
    return s;
}

PSYGFX_API psygfx_group psygfx_group_make(const psygfx_group_desc* d) {
    static const float fx[9] = { 0.5f, 0.0f, 0.5f, 1.0f, 0.0f, 1.0f, 0.0f, 0.5f, 1.0f };
    static const float fy[9] = { 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f };
    psygfx_group gr;
    memset(&gr, 0, sizeof gr);
    gr.visible = 1.0f; gr.ax = 0.5f; gr.ay = 0.5f; gr.scale = 1.0f; gr.opacity = 1.0f;
    if (!d) return gr;
    gr.place = (uint8_t)((int)d->place >= 0 && (int)d->place < 9 ? (int)d->place : 0);
    if ((int)d->anchor > 0 && (int)d->anchor < 9) { gr.ax = fx[d->anchor]; gr.ay = fy[d->anchor]; }
    gr.x = d->x; gr.y = d->y; gr.w = d->w; gr.h = d->h; gr.ori = d->ori;
    if (d->scale != 0.0f) gr.scale = d->scale;
    if (d->opacity != 0.0f) gr.opacity = d->opacity;
    return gr;
}

/* --- frame ------------------------------------------------------------------ */

/* A pass's std140 frame block, slot 0 for the scene: 5 x 16 bytes. A target
 * is drawn with its rows top first (GL's row 0 its top row), so its local y
 * axis keeps its sign in GL's axes; the scene's flips (PSYGFX__Y_SIGN). */
static void psygfx__frame_block(psygfx_gfx* g, int slot, int w, int h, int topdown) {
    unsigned char* base = g->staging + (size_t)slot * PSYGFX__BLOCK;
    const psyscr_frame* f = &g->frame;
    float* fb;
    uint32_t* fu;
    int32_t* fi;
    memset(base, 0, PSYGFX__BLOCK);
    fb = (float*)base;
    fb[0] = (float)w; fb[1] = (float)h; fb[2] = 1.0f / (float)w; fb[3] = 1.0f / (float)h;
    fb[4] = (float)((double)(f->onset - g->t_open) * 1e-9);
    fb[5] = (float)((double)f->period * 1e-9);
    fb[6] = (float)(f->index & 0xFFFFFF);
    fu = (uint32_t*)(base + 32);
    fu[0] = (uint32_t)((uint64_t)f->index & 0xFFFFFFFFu);
    fu[1] = (uint32_t)((uint64_t)f->index >> 32);
    fu[2] = g->seed;
    fu[3] = (uint32_t)((uint64_t)f->vblank & 0xFFFFFFFFu);
    fi = (int32_t*)(base + 48);
    fi[0] = g->lut_n;
    fi[1] = 255;
    fi[2] = (int32_t)g->dither;
    fi[3] = g->origin_top ? g->h : 0;
    fb = (float*)(base + 64);
    fb[0] = topdown ? 1.0f : PSYGFX__Y_SIGN;
    /* the vector program's loop bounds: data, so D3D's compiler cannot
     * unroll them (unrolled, they doubled its compile time) */
    fb[1] = 16.0f;   /* Gauss-Legendre nodes */
    fb[2] = 3.0f;    /* dashes about a pixel  */
}

/* GL state another program, or a psy_screen.h hook, may have changed: the
 * cached state goes when the screen's GL epoch moves. A new context means
 * every name the handle holds is gone or, worse, names another object:
 * the handle closes without deleting anything, since deleting those names
 * would delete the new context's objects. Called on entry to every
 * function that issues GL; returns PSYGFX_ERR_LOST then. */
#if defined(PSYSCR_HAS_GL_EPOCH)
    #define PSYGFX__EPOCH 1
#else
    #define PSYGFX__EPOCH 0
#endif
static int psygfx__sync(psygfx_gfx* g) {
#if PSYGFX__EPOCH
    if (g->screen) {
        uint32_t e = psyscr_gl_epoch(g->screen);
        if (psyscr_gl_generation(g->screen) != g->generation) {
            g->open = 0;
            g->in_frame = 0;
            psygfx__set_error(g->error, sizeof g->error,
                              "psy_gfx: the GL context is new; every GL object is gone (close and open again)");
            return PSYGFX_ERR_LOST;
        }
        if (e != g->epoch) { g->be->reset(g->bctx); g->epoch = e; }
    }
#else
    (void)g;
#endif
    return PSYGFX_OK;
}

PSYGFX_API int psygfx_begin(psygfx_gfx* g, const psyscr_frame* f) {
    float* fb;
    uint32_t* fu;
    int32_t* fi;
    PSYRT_ZONE(z, "psygfx.begin");
    if (!g || !g->open) { PSYRT_ZONE_END(z); return PSYGFX_ERR_CLOSED; }
    if (g->in_frame) { PSYRT_ZONE_END(z); return PSYGFX_ERR_ORDER; }
    if (!f) { PSYRT_ZONE_END(z); return PSYGFX_ERR_ARG; }
    g->frame = *f;
    g->in_frame = 1;
    g->n_cmds = 0;
    g->n_blocks = 0;
    g->clipped = 0;
    g->n_targets = 0;
    g->in_target = 0;
    g->cur_target_tex = 0;
    g->cur_w = g->w; g->cur_h = g->h; g->cur_top = 0;
    g->n_segs = 1;
    memset(&g->segs[0], 0, sizeof g->segs[0]);
    g->segs[0].w = g->w; g->segs[0].h = g->h;
    psygfx__frame_block(g, 0, g->w, g->h, 0);
    (void)fb; (void)fu; (void)fi;
    PSYRT_ZONE_END(z);
    return PSYGFX_OK;
}

/* --- coordinates ------------------------------------------------------------- */

/* A group's scale applies to what is in units, never to what is in px. */
static float psygfx__scale(const psygfx_stim* s) {
    return s->group && s->group->scale > 0.0f ? s->group->scale : 1.0f;
}

/* The quad's half size in px, the shape the shader evaluates, and its
 * parameters in px. DOTS: the field's box and aperture. */
static int psygfx__geom(const psygfx_gfx* g, const psygfx_stim* s, float* hx, float* hy, float sp[3]) {
    const float ppu = g->ppu * psygfx__scale(s);
    int shape = s->shape;
    *hx = 0.5f * s->w * ppu;
    *hy = 0.5f * s->h * ppu;
    if (s->kind == PSYGFX_SHAPE && shape == PSYGFX_NO_APERTURE) shape = PSYGFX_RECT;
    if (s->kind == PSYGFX_GABOR) {
        float sg = s->sigma > 0 ? s->sigma : 0.0f;
        if (s->w <= 0) *hx = 4.0f * sg * ppu;
        if (s->h <= 0) *hy = 4.0f * sg * ppu / (s->aspect > 0 ? s->aspect : 1.0f);
    }
    if (shape == PSYGFX_LINE) *hy = 0.5f * s->shape_p[0] * ppu;
    sp[0] = s->shape_p[0] * ppu;
    sp[1] = s->shape_p[1] * ppu;
    sp[2] = shape == PSYGFX_POLYGON ? s->shape_p[0] : 0.0f;
    return shape;
}

/* The coordinate convention lives here and nowhere else.
 * Pixels: origin at the top-left corner of the screen (or of the target
 * being drawn), x right, y down. A stimulus's anchor point is its `place`
 * point plus (x, y) units along the axes. Its anchor, (ax, ay) as fractions
 * of its own box with (0, 0) the top-left corner, sits there, and the box
 * turns about it by ori degrees, +x toward +y (clockwise on the screen).
 * Everything a stimulus draws (SDF, aperture, carrier) is defined about the
 * box's own center, so the anchor moves and turns a pattern but never
 * changes it.
 * In a group, the member's place point is one of its group's box and its x,
 * y, ori are in the group's frame; the group's frame is its anchor at its
 * own place point plus (x, y), turned by its ori and scaled by its scale.
 * psygfx__place() hands the shaders the box center in the pass's GL pixels
 * and the local x axis in GL's axes; the local y axis is that axis turned a
 * quarter turn counterclockwise in GL's axes, times the frame block's sign. */

static void psygfx__place_point(int place, double w, double h, double* px, double* py) {
    switch (place) {
    case PSYGFX_TOP_LEFT:     *px = 0.0;     *py = 0.0;     break;
    case PSYGFX_TOP:          *px = 0.5 * w; *py = 0.0;     break;
    case PSYGFX_TOP_RIGHT:    *px = w;       *py = 0.0;     break;
    case PSYGFX_LEFT:         *px = 0.0;     *py = 0.5 * h; break;
    case PSYGFX_RIGHT:        *px = w;       *py = 0.5 * h; break;
    case PSYGFX_BOTTOM_LEFT:  *px = 0.0;     *py = h;       break;
    case PSYGFX_BOTTOM:       *px = 0.5 * w; *py = h;       break;
    case PSYGFX_BOTTOM_RIGHT: *px = w;       *py = h;       break;
    default:                  *px = 0.5 * w; *py = 0.5 * h; break;
    }
}

/* The anchor's position and the box center in the pixels of a W x H pass,
 * and the box's turn. */
static void psygfx__frame_px(const psygfx_gfx* g, const psygfx_stim* s, double W, double H,
                             double anchor[2], double center[2], double* c, double* sn) {
    const double deg = 3.14159265358979323846 / 180.0;
    const psygfx_group* gr = s->group;
    double ppu = g->ppu, rad = (double)s->ori * deg, px, py, ox, oy;
    float hx, hy, sp[3];
    if (!gr) {
        psygfx__place_point(s->place, W, H, &px, &py);
        anchor[0] = px + (double)s->x * ppu;
        anchor[1] = py + (double)s->y * ppu;
    } else {
        double gw = (double)gr->w * ppu, gh = (double)gr->h * ppu, k = gr->scale > 0 ? gr->scale : 1.0;
        double gc = cos((double)gr->ori * deg), gs = sin((double)gr->ori * deg), qx, qy, gx, gy;
        /* the member's anchor in the group's frame, from the box's pivot */
        psygfx__place_point(s->place, gw, gh, &px, &py);
        qx = (px + (double)s->x * ppu - (double)gr->ax * gw) * k;
        qy = (py + (double)s->y * ppu - (double)gr->ay * gh) * k;
        psygfx__place_point(gr->place, W, H, &gx, &gy);
        anchor[0] = gx + (double)gr->x * ppu + gc * qx - gs * qy;
        anchor[1] = gy + (double)gr->y * ppu + gs * qx + gc * qy;
        rad += (double)gr->ori * deg;
    }
    psygfx__geom(g, s, &hx, &hy, sp);
    *c = cos(rad);
    *sn = sin(rad);
    /* the center relative to the anchor, in the box's own axes, then
     * turned: local x is (cos, sin) and local y is (-sin, cos) on screen */
    ox = (0.5 - (double)s->ax) * 2.0 * hx;
    oy = (0.5 - (double)s->ay) * 2.0 * hy;
    center[0] = anchor[0] + *c * ox - *sn * oy;
    center[1] = anchor[1] + *sn * ox + *c * oy;
}

/* For the pass being recorded: the scene is GL's bottom-up rows, a target's
 * rows are top first. */
static void psygfx__place(const psygfx_gfx* g, const psygfx_stim* s, float out[4]) {
    double a[2], m[2], c, sn;
    psygfx__frame_px(g, s, (double)g->cur_w, (double)g->cur_h, a, m, &c, &sn);
    out[0] = (float)m[0];
    out[1] = g->cur_top ? (float)m[1] : (float)((double)g->cur_h - m[1]);
    out[2] = (float)c;
    out[3] = g->cur_top ? (float)sn : (float)-sn;
}

PSYGFX_API void psygfx_center(const psygfx_gfx* g, float* x, float* y) {
    if (x) *x = g ? 0.5f * (float)g->w : 0.0f;
    if (y) *y = g ? 0.5f * (float)g->h : 0.0f;
}

PSYGFX_API void psygfx_size(const psygfx_gfx* g, float* w, float* h) {
    if (w) *w = g ? (float)g->w : 0.0f;
    if (h) *h = g ? (float)g->h : 0.0f;
}

PSYGFX_API void psygfx_resolve(const psygfx_gfx* g, const psygfx_stim* s, float* x, float* y) {
    double a[2] = { 0, 0 }, m[2], c, sn;
    if (g && s) psygfx__frame_px(g, s, g->w, g->h, a, m, &c, &sn);
    if (x) *x = (float)a[0];
    if (y) *y = (float)a[1];
}

/* How far a level `a` px out of the boundary reaches from the box, px: a
 * mitered corner pushes it out by up to the miter limit (a polygon) or
 * sqrt 2 (a rect or cross). */
static float psygfx__reach(const psygfx_stim* s, int shape, float a) {
    float lim = s->miter_limit > 0.0f ? s->miter_limit : 4.0f;
    if (lim < 1.0f) lim = 1.0f;
    if (s->join == PSYGFX_JOIN_MITER && shape == PSYGFX_POLYGON) return a * lim;
    if (s->join == PSYGFX_JOIN_MITER && (shape == PSYGFX_RECT || shape == PSYGFX_CROSS)) return a * 1.4142136f;
    return a;
}

/* The outer offset of a stroke, px; 0 for a fill. */
static float psygfx__outer(const psygfx_stim* s, int shape) {
    float a2;
    if (s->stroke <= 0.0f) return 0.0f;
    a2 = s->stroke_align == PSYGFX_STROKE_INSIDE ? 0.0f
       : (s->stroke_align == PSYGFX_STROKE_OUTSIDE ? s->stroke : 0.5f * s->stroke);
    return psygfx__reach(s, shape, a2);
}

PSYGFX_API void psygfx_bounds(const psygfx_gfx* g, const psygfx_stim* s, float* x0, float* y0, float* x1, float* y1) {
    double a[2] = { 0, 0 }, m[2] = { 0, 0 }, c = 1, sn = 0, ex = 0, ey = 0;
    float hx = 0, hy = 0, sp[3];
    if (g && s) {
        int shape;
        psygfx__frame_px(g, s, g->w, g->h, a, m, &c, &sn);
        shape = psygfx__geom(g, s, &hx, &hy, sp);
        if (psygfx__is_vector(s)) {
            float blk[16 * 64];
            double e2[2];
            const char* err;
            int clip;
            if (psygfx__vpack(g, s, blk, e2, &clip, &err) >= 0) { hx = (float)e2[0]; hy = (float)e2[1]; }
        } else if (s->kind == PSYGFX_DOTS) {
            float r = 0.5f * s->dot_size + psygfx__outer(s, PSYGFX_CIRCLE);
            hx += r; hy += r;
        } else if (shape != PSYGFX_NO_APERTURE && shape != PSYGFX_MASK_TEX) {
            float o = psygfx__outer(s, shape) + (s->kind == PSYGFX_SHAPE && s->offset > 0.0f ? s->offset : 0.0f);
            hx += o; hy += o;
        }
        ex = fabs(c) * hx + fabs(sn) * hy;
        ey = fabs(sn) * hx + fabs(c) * hy;
    }
    if (x0) *x0 = (float)(m[0] - ex);
    if (y0) *y0 = (float)(m[1] - ey);
    if (x1) *x1 = (float)(m[0] + ex);
    if (y1) *y1 = (float)(m[1] + ey);
}

PSYGFX_API void psygfx_local(const psygfx_gfx* g, const psygfx_stim* s, float px, float py, float* lx, float* ly) {
    double a[2] = { 0, 0 }, m[2] = { 0, 0 }, c = 1, sn = 0, dx, dy;
    if (g && s) psygfx__frame_px(g, s, g->w, g->h, a, m, &c, &sn);
    dx = (double)px - m[0];
    dy = (double)py - m[1];
    if (lx) *lx = (float)(dx * c + dy * sn);
    if (ly) *ly = (float)(-dx * sn + dy * c);
}

/* The shader's psy_sdf_at() on the CPU, in double. */
static double psygfx__sd_box(double px, double py, double bx, double by) {
    double dx = fabs(px) - bx, dy = fabs(py) - by;
    double ox = dx > 0 ? dx : 0, oy = dy > 0 ? dy : 0;
    double in = dx > dy ? dx : dy;
    return sqrt(ox * ox + oy * oy) + (in < 0 ? in : 0);
}

static double psygfx__mbox(double px, double py, double bx, double by, double L, double a) {
    double dx = fabs(px) - bx, dy = fabs(py) - by, m = dx > dy ? dx : dy, b;
    if (!(L < 1.41421356)) return m;
    b = (dx + dy) * 0.70710678118654752 + a * (1.0 - 0.70710678118654752);
    return b > m ? b : m;
}

/* A polygon's orientation in the local frame: +1 counterclockwise in the
 * math sense (y up), -1 the other way; 0 when not strictly convex. */
static int psygfx__convex(const float* v, int n, double scale) {
    int k, sgn = 0;
    for (k = 0; k < n; k++) {
        const float* a = v + 2 * k;
        const float* b = v + 2 * ((k + 1) % n);
        const float* c = v + 2 * ((k + 2) % n);
        double cr = ((double)b[0] - a[0]) * ((double)c[1] - b[1]) - ((double)b[1] - a[1]) * ((double)c[0] - b[0]);
        int sk = cr > 0 ? 1 : (cr < 0 ? -1 : 0);
        if (sk == 0) return 0;
        if (sgn == 0) sgn = sk;
        else if (sk != sgn) return 0;
    }
    (void)scale;
    return sgn;
}

static double psygfx__sd_polygon_miter(double px, double py, const float* verts, int n, double ppu,
                                       double o, double L, double a) {
    double d = -1e30;
    int k;
    for (k = 0; k < n; k++) {
        int kp = (k + n - 1) % n, kn = (k + 1) % n;
        double vx = verts[2 * k] * ppu, vy = verts[2 * k + 1] * ppu;
        double e1x = vx - verts[2 * kp] * ppu, e1y = vy - verts[2 * kp + 1] * ppu;
        double e2x = verts[2 * kn] * ppu - vx, e2y = verts[2 * kn + 1] * ppu - vy;
        double l1 = sqrt(e1x * e1x + e1y * e1y), l2 = sqrt(e2x * e2x + e2y * e2y);
        double n1x = o * e1y / l1, n1y = -o * e1x / l1, n2x = o * e2y / l2, n2y = -o * e2x / l2;
        double bx = n1x + n2x, by = n1y + n2y, bl = sqrt(bx * bx + by * by), c, t;
        t = (px - vx) * n2x + (py - vy) * n2y;
        if (t > d) d = t;
        bx /= bl; by /= bl;
        c = bx * n1x + by * n1y;
        if (c * L < 1.0) {
            t = (px - vx) * bx + (py - vy) * by + a * (1.0 - c);
            if (t > d) d = t;
        }
    }
    return d;
}

static double psygfx__sdf(int shape, double px, double py, double hx, double hy, const float sp[3],
                          const float* verts, double ppu, double ml, double a) {
    switch (shape) {
    case PSYGFX_RECT: {
        double r = sp[1];
        if (ml != 0.0) return psygfx__mbox(px, py, hx, hy, ml, a);
        if (r > hx) r = hx;
        if (r > hy) r = hy;
        return psygfx__sd_box(px, py, hx - r, hy - r) - r;
    }
    case PSYGFX_CIRCLE:  return sqrt(px * px + py * py) - hx;
    case PSYGFX_ANNULUS: return fabs(sqrt(px * px + py * py) - 0.5 * (hx + sp[0])) - 0.5 * (hx - sp[0]);
    case PSYGFX_LINE: {
        double ex = fabs(px) - (hx - hy);
        if (ex < 0) ex = 0;
        return sqrt(ex * ex + py * py) - hy;
    }
    case PSYGFX_POLYGON: {
        int n = (int)sp[2], k, j;
        double d, sg = 1.0;
        if (n < 3 || n > 16) return 1e30;
        if (ml != 0.0) return psygfx__sd_polygon_miter(px, py, verts, n, ppu, ml > 0 ? 1.0 : -1.0, fabs(ml), a);
        d = (px - verts[0] * ppu) * (px - verts[0] * ppu) + (py - verts[1] * ppu) * (py - verts[1] * ppu);
        for (k = 0, j = n - 1; k < n; j = k, k++) {
            double vix = verts[2 * k] * ppu, viy = verts[2 * k + 1] * ppu;
            double vjx = verts[2 * j] * ppu, vjy = verts[2 * j + 1] * ppu;
            double ex = vjx - vix, ey = vjy - viy, wx = px - vix, wy = py - viy;
            double t = (wx * ex + wy * ey) / (ex * ex + ey * ey);
            double bx, by;
            int c1 = py >= viy, c2 = py < vjy, c3 = ex * wy > ey * wx;
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            bx = wx - ex * t;
            by = wy - ey * t;
            if (bx * bx + by * by < d) d = bx * bx + by * by;
            if ((c1 && c2 && c3) || (!c1 && !c2 && !c3)) sg = -sg;
        }
        return sg * sqrt(d);
    }
    case PSYGFX_CROSS: {
        double a1, b1;
        if (ml != 0.0) {
            a1 = psygfx__mbox(px, py, hx, 0.5 * sp[0], ml, a);
            b1 = psygfx__mbox(px, py, 0.5 * sp[0], hy, ml, a);
        } else {
            a1 = psygfx__sd_box(px, py, hx, 0.5 * sp[0]);
            b1 = psygfx__sd_box(px, py, 0.5 * sp[0], hy);
        }
        return a1 < b1 ? a1 : b1;
    }
    default:
        return -1e30;   /* no aperture: the quad alone clips */
    }
}

/* The band's offsets a1 < a2, px, of a stroke; a fill has none. */
static void psygfx__band(const psygfx_stim* s, float* a1, float* a2) {
    float w = s->stroke;
    if (s->stroke_align == PSYGFX_STROKE_INSIDE) { *a1 = -w; *a2 = 0.0f; }
    else if (s->stroke_align == PSYGFX_STROKE_OUTSIDE) { *a1 = 0.0f; *a2 = w; }
    else { *a1 = -0.5f * w; *a2 = 0.5f * w; }
}

/* The miter limit the shader gets in *ml: 0 round; else the limit, its sign
 * the polygon's orientation (the sign of its cross products in the local
 * frame, as the shader sees them). Returns -1 when MITER is asked where it
 * cannot be: a curve, a rounded rect, a concave polygon. */
static int psygfx__miter(const psygfx_stim* s, int shape, float* ml) {
    float lim = s->miter_limit > 0.0f ? s->miter_limit : 4.0f;
    *ml = 0.0f;
    if (s->join != PSYGFX_JOIN_MITER) return 0;
    if (lim < 1.0f) lim = 1.0f;
    if ((shape == PSYGFX_RECT && s->shape_p[1] <= 0.0f) || shape == PSYGFX_CROSS) { *ml = lim; return 0; }
    if (shape == PSYGFX_POLYGON) {
        int n = (int)s->shape_p[0], o;
        if (n < 3 || n > 16) return -1;
        o = psygfx__convex(s->p, n, 1.0);
        if (o == 0) return -1;
        *ml = (float)o * lim;
        return 0;
    }
    return -1;
}


static int psygfx__trimmed(const psygfx_stim* s) {
    return !(s->trim[0] == 0.0f && s->trim[1] == 0.0f) && !(s->trim[0] <= 0.0f && s->trim[1] >= 1.0f);
}

/* A glyph run: a SHAPE on a mask with glyph instances. */
static int psygfx__is_glyphs(const psygfx_stim* s) {
    return s->kind == PSYGFX_SHAPE && s->shape == PSYGFX_MASK_TEX && s->buf.id != 0 && s->count > 0;
}

PSYGFX_API bool psygfx_hit(const psygfx_gfx* g, const psygfx_stim* s, float px, float py) {
    float lx, ly, hx, hy, sp[3], ml;
    int shape;
    if (!g || !g->open || !s || !(s->visible >= 0.5f)) return false;
    if (s->group && !(s->group->visible >= 0.5f)) return false;
    if (s->kind != PSYGFX_SHAPE && s->kind != PSYGFX_GRATING && s->kind != PSYGFX_GABOR &&
        s->kind != PSYGFX_IMAGE && s->kind != PSYGFX_NOISE && s->kind != PSYGFX_USER) return false;
    psygfx_local(g, s, px, py, &lx, &ly);
    if (psygfx__is_vector(s)) {   /* the vector program's field, on the CPU */
        float blk[16 * 64];
        double ext[2];
        const char* err;
        int clip;
        if (psygfx__vpack(g, s, blk, ext, &clip, &err) < 0) return false;
        return psygfx__vhard(blk, lx, ly) != 0;
    }
    shape = psygfx__geom(g, s, &hx, &hy, sp);
    if (shape == PSYGFX_MASK_TEX) return false;   /* its distances live on the GPU */
    if (psygfx__miter(s, shape, &ml) < 0) return false;
    /* Coverage >= 0.5 is d <= 0 for every edge profile, which are centered
     * on the boundary; with no aperture, the quad is the boundary. A stroke
     * is its hard band. */
    if (shape == PSYGFX_NO_APERTURE) return fabsf(lx) <= hx && fabsf(ly) <= hy;
    if (s->stroke > 0.0f) {
        float a1, a2;
        double d1, d2;
        const double k = g->ppu * psygfx__scale(s);
        psygfx__band(s, &a1, &a2);
        d2 = psygfx__sdf(shape, lx, ly, hx, hy, sp, s->p, k, ml, a2 + s->offset) - s->offset;
        d1 = psygfx__sdf(shape, lx, ly, hx, hy, sp, s->p, k, ml, a1 + s->offset) - s->offset;
        return d2 <= a2 && d1 > a1;
    }
    return psygfx__sdf(shape, lx, ly, hx, hy, sp, s->p, g->ppu * psygfx__scale(s), ml, s->offset) - s->offset <= 0.0;
}

/* Quad margin in px outside the nominal boundary that a stroke and an edge
 * profile reach: 1 px for the pixel centers on it, and their extent. A
 * mask is clipped to its box: its padding holds what reaches out. */
static float psygfx__margin(const psygfx_stim* s, int shape, float ew) {
    float m;
    if ((shape == PSYGFX_NO_APERTURE && s->kind != PSYGFX_DOTS) || shape == PSYGFX_MASK_TEX) return 1.0f;
    m = s->stroke_align == PSYGFX_STROKE_INSIDE || s->stroke <= 0.0f ? 0.0f
      : (s->stroke_align == PSYGFX_STROKE_OUTSIDE ? s->stroke : 0.5f * s->stroke);
    if (s->kind == PSYGFX_SHAPE && s->offset > 0.0f) m += s->offset;
    if (s->edge == PSYGFX_EDGE_COSINE && ew > 0) m += 0.5f * ew;
    if (s->edge == PSYGFX_EDGE_GAUSSIAN && ew > 0) m += 5.0f * ew;
    return psygfx__reach(s, shape, m) + 1.0f;
}

/* Packs one stimulus into its 256-byte std140 block (the 16 vec4 of the
 * contract). Returns the pipeline, or 0 when the stimulus cannot be drawn,
 * with *why the code. */
/* A refusal names its reason: draw() runs on the owning thread only. */
static uint32_t psygfx__refuse(psygfx_gfx* g, int* why, int code, const char* msg) {
    *why = code;
    psygfx__set_error(g->error, sizeof g->error, "psy_gfx: %s", msg);
    return 0;
}

/* The backend blend of a color stimulus; -1 when s->blend is not one. */
static int32_t psygfx__blend_of(const psygfx_stim* s) {
    switch (s->blend) {
    case PSYGFX_BLEND_MODE_OVER:     return 0;
    case PSYGFX_BLEND_MODE_ADD:      return PSYGFX_BLEND_ADD;
    case PSYGFX_BLEND_MODE_MULTIPLY: return PSYGFX_BLEND_MULTIPLY;
    default:                         return -1;
    }
}

static uint32_t psygfx__pack(psygfx_gfx* g, const psygfx_stim* s, float* b, psygfx__cmd* cmd, int* why) {
    const float k = psygfx__scale(s), ppu = g->ppu * k;
    float hx, hy, sp[3], ew = s->edge_width, ml = 0.0f, mrect[4] = { 0, 0, 0, 0 };
    float dir[3], gate = s->gate * (s->group ? s->group->opacity : 1.0f);
    int i, mod = 0, shape, mask_kind = 0;
    uint32_t pipe = 0;
    memset(b, 0, PSYGFX__BLOCK);
    memset(cmd, 0, sizeof *cmd);
    cmd->nblk = 1;
    *why = PSYGFX_ERR_ARG;
    if (s->edge == PSYGFX_EDGE_HARD || ew < 0) ew = 0;
    cmd->blend = psygfx__blend_of(s);
    if (cmd->blend < 0) return psygfx__refuse(g, why, PSYGFX_ERR_ARG, "blend is not a psygfx_blend_mode");
    if (s->kind != PSYGFX_SHAPE && (s->offset != 0.0f || s->dash[0] > 0.0f || psygfx__trimmed(s) || s->fx || s->paint || s->prims))
        return psygfx__refuse(g, why, PSYGFX_ERR_ARG, "offset, dashes, trim, fx, paint and prims are SHAPE fields");
    if (psygfx__is_vector(s)) {   /* the vector program: its own block layout */
        double ext[2];
        const char* err = "";
        int clip = 0, n;
        psygfx__place(g, s, b);
        n = psygfx__vpack(g, s, b, ext, &clip, &err);
        if (n < 0) return psygfx__refuse(g, why, n, err);
        cmd->nblk = (uint32_t)n;
        g->clipped += (uint32_t)clip;
        if (!s->paint || s->paint->kind == PSYGFX_PAINT_SOLID)
            for (i = 0; i < 3; i++)
                if (s->color[i] < 0.0f || s->color[i] > 1.0f) { g->clipped++; break; }
        *why = PSYGFX_OK;
        return g->builtin[PSYGFX__B_VECTOR];
    }
    if (s->shape > PSYGFX_MASK_TEX)
        return psygfx__refuse(g, why, PSYGFX_ERR_ARG, "the v0.3 kinds are SHAPE kinds, not apertures");
    shape = psygfx__geom(g, s, &hx, &hy, sp);
    switch (s->kind) {
    case PSYGFX_SHAPE:
        if (psygfx__is_glyphs(s)) {
            if (s->buf.id > PSYGFX_MAX_BUFFERS || !g->buf[s->buf.id - 1].used ||
                (size_t)s->count * sizeof(psygfx_glyph) > (size_t)g->buf[s->buf.id - 1].w)
                return psygfx__refuse(g, why, PSYGFX_ERR_ARG, "a glyph run's buffer holds fewer than count glyphs");
            pipe = g->builtin[PSYGFX__B_GLYPHS];
            cmd->inst = g->buf[s->buf.id - 1].bid;
            cmd->count = s->count;
        } else {
            pipe = g->builtin[PSYGFX__B_SHAPE];
        }
        break;
    case PSYGFX_GRATING: pipe = g->builtin[PSYGFX__B_GRATING]; mod = 1; break;
    case PSYGFX_GABOR:
        if (s->sigma <= 0) return 0;
        pipe = g->builtin[PSYGFX__B_GABOR];
        mod = 1;
        break;
    case PSYGFX_NOISE:   pipe = g->builtin[PSYGFX__B_NOISE]; mod = 1; break;
    case PSYGFX_IMAGE: {
        const psygfx__res* r;
        float sx, sy, sw, sh;
        if (s->tex.id < 1 || s->tex.id > PSYGFX_MAX_TEXTURES || !g->tex[s->tex.id - 1].used) return 0;
        r = &g->tex[s->tex.id - 1];
        mod = (s->flags & PSYGFX_STIM_MODULATION) != 0;
        pipe = g->builtin[(s->flags & PSYGFX_STIM_ADD) ? PSYGFX__B_IMAGE_ADD
                          : (mod ? PSYGFX__B_IMAGE_MOD : PSYGFX__B_IMAGE_COLOR)];
        if (r->format == PSYGFX_R16UI) cmd->tex[3] = r->bid; else cmd->tex[0] = r->bid;
        b[28] = (r->format == PSYGFX_R8 || r->format == PSYGFX_R16F || r->format == PSYGFX_R32F ||
                 r->format == PSYGFX_R16UI) ? 1.0f : (r->format == PSYGFX_RG8 ? 2.0f : 4.0f);
        b[29] = r->format == PSYGFX_R16UI ? 2.0f : ((s->flags & PSYGFX_STIM_LINEAR) ? 1.0f : 0.0f);
        b[30] = (s->flags & PSYGFX_STIM_PREMULTIPLIED) ? 1.0f : 0.0f;
        /* the source rectangle, whole texels inside the texture */
        sx = s->src[0]; sy = s->src[1]; sw = s->src[2]; sh = s->src[3];
        if (!(sw > 0.0f && sh > 0.0f)) { sx = 0; sy = 0; sw = (float)r->w; sh = (float)r->h; }
        sx = floorf(sx); sy = floorf(sy); sw = floorf(sw); sh = floorf(sh);
        if (sx < 0 || sy < 0 || sw < 1 || sh < 1 || sx + sw > (float)r->w || sy + sh > (float)r->h) return 0;
        b[32] = sx; b[33] = sy; b[34] = sw; b[35] = sh;
        break;
    }
    case PSYGFX_DOTS:
        if (s->buf.id < 1 || s->buf.id > PSYGFX_MAX_BUFFERS || !g->buf[s->buf.id - 1].used || s->count == 0) return 0;
        if ((size_t)s->count * 8u > (size_t)g->buf[s->buf.id - 1].w) return 0;
        pipe = g->builtin[PSYGFX__B_DOTS];
        cmd->inst = g->buf[s->buf.id - 1].bid;
        cmd->count = s->count;
        break;
    case PSYGFX_USER:
        if (s->pipe.id < 1 || s->pipe.id > PSYGFX_MAX_PIPELINES || !g->pipe[s->pipe.id - 1].used) return 0;
        pipe = g->pipe[s->pipe.id - 1].bid;
        mod = g->pipe[s->pipe.id - 1].flags != PSYGFX_COLOR;
        break;
    default:
        return 0;
    }
    if (mod && s->blend != PSYGFX_BLEND_MODE_OVER)
        return psygfx__refuse(g, why, PSYGFX_ERR_ARG, "a modulation adds: blend is for color stimuli");
    if (s->kind == PSYGFX_IMAGE && (s->flags & PSYGFX_STIM_ADD) && s->blend != PSYGFX_BLEND_MODE_OVER)
        return psygfx__refuse(g, why, PSYGFX_ERR_ARG, "an ADD image adds already");
    if (shape == PSYGFX_MASK_TEX) {
        const psygfx__res* m;
        double pxpt, reach, out_r, in_r, er = psygfx__edge_reach(s);
        if (s->kind == PSYGFX_DOTS || s->mask.id < 1 || s->mask.id > PSYGFX_MAX_TEXTURES || !g->tex[s->mask.id - 1].used) return 0;
        m = &g->tex[s->mask.id - 1];
        mask_kind = m->sdf ? m->sdf : PSYGFX_SDF_DIST;
        if (mask_kind == PSYGFX_SDF_DIST && m->format != PSYGFX_R16F && m->format != PSYGFX_R32F) return 0;
        if (mask_kind != PSYGFX_SDF_DIST && !(m->sdf_range > 0.0f && (m->format == PSYGFX_RGBA8 || m->format == PSYGFX_RGBA16F ||
                                                                    m->format == PSYGFX_RGBA32F)))
            return psygfx__refuse(g, why, PSYGFX_ERR_ARG, "an MSDF or MTSDF mask is RGBA with an sdf_range");
        if (mask_kind == PSYGFX_SDF_MSDF && s->stroke > 0.0f && s->join == PSYGFX_JOIN_ROUND)
            return psygfx__refuse(g, why, PSYGFX_ERR_ARG, "an MSDF's offsets are mitered: a stroke on it needs JOIN_MITER, "
                                                         "or an MTSDF atlas for round joins");
        if (psygfx__is_glyphs(s)) {
            pxpt = (double)s->shape_p[0] * ppu;
            if (!(pxpt > 0.0)) return psygfx__refuse(g, why, PSYGFX_ERR_ARG, "a glyph run needs a scale (units per texel)");
            b[29] = (float)pxpt;
        } else {
            /* the mask's rectangle: src on every kind but IMAGE (whose src is
             * its texture's); clamped addresses, so no neighbor bleeds */
            mrect[2] = (float)m->w; mrect[3] = (float)m->h;
            if (s->kind != PSYGFX_IMAGE && s->src[2] > 0.0f && s->src[3] > 0.0f) {
                mrect[0] = floorf(s->src[0]); mrect[1] = floorf(s->src[1]); mrect[2] = floorf(s->src[2]); mrect[3] = floorf(s->src[3]);
                if (mrect[0] < 0 || mrect[1] < 0 || mrect[2] < 1 || mrect[3] < 1 || mrect[0] + mrect[2] > (float)m->w ||
                    mrect[1] + mrect[3] > (float)m->h)
                    return psygfx__refuse(g, why, PSYGFX_ERR_ARG, "the mask's src rectangle is not inside its texture");
            }
            /* one scale on both axes, or the distance would be wrong */
            if (fabs((double)hx / hy - (double)mrect[2] / mrect[3]) > 1e-3 * ((double)mrect[2] / mrect[3])) return 0;
            pxpt = 2.0 * hx / mrect[2];
        }
        /* The range rule: the atlas encodes distances to range / 2 texels;
         * what the edge, stroke and offset reach must lie inside it, 1.5
         * texels in for the bilinear neighbors. A Gaussian reaches 5 SD. */
        out_r = (s->stroke > 0.0f && s->stroke_align != PSYGFX_STROKE_INSIDE ?
                 (s->stroke_align == PSYGFX_STROKE_OUTSIDE ? s->stroke : 0.5 * s->stroke) : 0.0) + s->offset + er;
        in_r = (s->stroke > 0.0f && s->stroke_align != PSYGFX_STROKE_OUTSIDE ?
                (s->stroke_align == PSYGFX_STROKE_INSIDE ? s->stroke : 0.5 * s->stroke) : 0.0) - s->offset + er;
        reach = out_r > in_r ? out_r : in_r;
        if (mask_kind != PSYGFX_SDF_DIST && reach > (0.5 * m->sdf_range - 1.5) * pxpt) {
            char msg[240];
            snprintf(msg, sizeof msg, "the atlas range %.3g texels is too small: the edge, stroke and offset reach %.3g px "
                     "at %.4g px per texel, which needs a range of %.3g", (double)m->sdf_range, reach, pxpt, 2.0 * (reach / pxpt + 1.5));
            return psygfx__refuse(g, why, PSYGFX_ERR_RANGE, msg);
        }
        cmd->tex[1] = m->bid;
    }
    if (s->kind != PSYGFX_DOTS && shape != PSYGFX_NO_APERTURE && shape != PSYGFX_MASK_TEX) {
        if (psygfx__miter(s, shape, &ml) < 0) return 0;
    }
    /* a target pass must not sample its own target */
    if (g->in_target)
        for (i = 0; i < 4; i++)
            if (cmd->tex[i] && cmd->tex[i] == g->tex[g->cur_target_tex - 1].bid) { *why = PSYGFX_ERR_ORDER; return 0; }
    psygfx__place(g, s, b);                 /* xf */
    if (s->kind == PSYGFX_DOTS) {
        float r = 0.5f * s->dot_size;
        b[4] = r; b[5] = r; b[6] = ppu; b[7] = psygfx__margin(s, PSYGFX_CIRCLE, ew);
        b[20] = (float)PSYGFX_CIRCLE;
        /* the field's aperture, which the vertex shader tests each dot
         * center against: half box, 0 rect, 1 circle, 2 none */
        b[24] = hx; b[25] = hy;
        b[26] = (float)(shape == PSYGFX_RECT ? 0 : (shape == PSYGFX_CIRCLE ? 1 : 2));
    } else {
        b[4] = hx; b[5] = hy; b[6] = ppu; b[7] = psygfx__margin(s, shape, ew);
        b[20] = (float)shape;
        b[24] = sp[0]; b[25] = sp[1]; b[26] = sp[2];
    }
    /* look: contrast, opacity, gate, the stroke band's center offset;
     * shape.w: the band's half width (0: a fill) */
    b[8] = s->contrast; b[9] = s->opacity; b[10] = gate;
    /* DOTS: the band is each dot's ring, whatever the field's aperture */
    if (s->stroke > 0.0f && (s->kind == PSYGFX_DOTS || shape != PSYGFX_NO_APERTURE)) {
        float a1, a2;
        psygfx__band(s, &a1, &a2);
        b[11] = 0.5f * (a1 + a2);
        b[27] = 0.5f * (a2 - a1);
    }
    psygfx__copy3(b + 12, s->color);
    dir[0] = s->dir[0]; dir[1] = s->dir[1]; dir[2] = s->dir[2];
    if (dir[0] == 0.0f && dir[1] == 0.0f && dir[2] == 0.0f) psygfx__copy3(dir, g->bg);
    if (s->kind == PSYGFX_IMAGE) {
        /* tint: in color for the color and ADD paths, folded into dir for
         * a modulation */
        for (i = 0; i < 4; i++) b[12 + i] = s->tint[i];
        for (i = 0; i < 3; i++) dir[i] *= s->tint[i] * s->tint[3];
    }
    psygfx__copy3(b + 16, dir);
    b[19] = ml;
    b[21] = (float)s->edge; b[22] = ew; b[23] = (float)s->wave;
    if (s->kind == PSYGFX_GRATING || s->kind == PSYGFX_GABOR) {
        b[28] = s->sf / ppu; b[29] = s->phase; b[30] = s->sigma * ppu; b[31] = s->aspect;
    } else if (s->kind == PSYGFX_NOISE) {
        b[28] = s->check > 0 ? s->check : 1.0f; b[29] = s->seed; b[30] = (float)s->wave;
    }
    if (s->kind != PSYGFX_IMAGE) {
        if (shape == PSYGFX_POLYGON && s->kind != PSYGFX_USER) {
            for (i = 0; i < 32; i++) b[32 + i] = s->p[i] * ppu;
        } else {
            for (i = 0; i < 32; i++) b[32 + i] = s->p[i];
        }
    }
    if (s->kind == PSYGFX_SHAPE) b[28] = s->offset;   /* misc.x, the shape program's PSY_OFFSET */
    if (shape == PSYGFX_MASK_TEX) {
        /* shape.x the kind (1 distance, 2 MSDF, 3 MTSDF), y the range, z 1
         * to read MTSDF's true distance (round offsets); p[28..31] the
         * rectangle (a USER body loses those four parameters) */
        b[24] = (float)mask_kind; b[25] = mask_kind == PSYGFX_SDF_DIST ? 0.0f : g->tex[s->mask.id - 1].sdf_range;
        b[26] = mask_kind == PSYGFX_SDF_MTSDF && ((s->stroke > 0.0f && s->join == PSYGFX_JOIN_ROUND) || s->offset != 0.0f) ? 1.0f : 0.0f;
        for (i = 0; i < 4; i++) b[60 + i] = mrect[i];
    }
    /* A CPU bound on leaving 0..1, so a clip is counted without a readback. */
    if (s->kind == PSYGFX_IMAGE && (s->flags & PSYGFX_STIM_ADD)) {
        /* an increment layer's range is the texture's: not bounded here */
    } else if (mod) {
        float kk = fabsf(s->contrast * gate);
        for (i = 0; i < 3; i++) {
            float lo = g->bg[i] - kk * fabsf(dir[i]), hi = g->bg[i] + kk * fabsf(dir[i]);
            if (lo < -1e-6f || hi > 1.0f + 1e-6f) { g->clipped++; break; }
        }
    } else if (s->kind != PSYGFX_IMAGE) {
        for (i = 0; i < 3; i++)
            if (s->color[i] < 0.0f || s->color[i] > 1.0f) { g->clipped++; break; }
    }
    *why = PSYGFX_OK;
    return pipe;
}

PSYGFX_API int psygfx_draw(psygfx_gfx* g, const psygfx_stim* s) {
    psygfx__cmd* cmd;
    uint32_t pipe;
    int why;
    PSYRT_ZONE(z, "psygfx.draw");
    if (!g || !g->open) { PSYRT_ZONE_END(z); return PSYGFX_ERR_CLOSED; }
    if (!g->in_frame) { PSYRT_ZONE_END(z); return PSYGFX_ERR_ORDER; }
    if (!s) { PSYRT_ZONE_END(z); return PSYGFX_ERR_ARG; }
    if (!(s->visible >= 0.5f) || (s->group && !(s->group->visible >= 0.5f))) { PSYRT_ZONE_END(z); return PSYGFX_OK; }
    if (g->n_cmds >= g->max_draws || g->n_blocks >= g->max_draws) { PSYRT_ZONE_END(z); return PSYGFX_ERR_FULL; }
    cmd = &g->cmds[g->n_cmds];
    /* the staging has 16 blocks of slack, so a vector stimulus packs
     * before its blocks are counted */
    pipe = psygfx__pack(g, s, (float*)(g->staging + PSYGFX__FRAME_BYTES + (size_t)g->n_blocks * PSYGFX__BLOCK), cmd, &why);
    if (!pipe) {
        PSYRT_ZONE_END(z);
        return s->kind == PSYGFX_TEXT ? PSYGFX_ERR_NOT_IMPLEMENTED : why;
    }
    if (g->n_blocks + (int32_t)cmd->nblk > g->max_draws) { PSYRT_ZONE_END(z); return PSYGFX_ERR_FULL; }
    cmd->pipe = pipe;
    cmd->block = (uint32_t)g->n_blocks;
    g->n_blocks += (int32_t)cmd->nblk;
    g->n_cmds++;
    PSYRT_ZONE_END(z);
    return PSYGFX_OK;
}

PSYGFX_API int psygfx_draw_n(psygfx_gfx* g, const psygfx_stim* s, int n) {
    int i, rc, first = PSYGFX_OK;
    if (!s || n < 0) return PSYGFX_ERR_ARG;
    for (i = 0; i < n; i++) {
        rc = psygfx_draw(g, &s[i]);
        if (rc < 0 && first == PSYGFX_OK) first = rc;
        if (rc == PSYGFX_ERR_CLOSED || rc == PSYGFX_ERR_FULL) return rc;
    }
    return first;
}

static int psygfx__same_batch(const psygfx__cmd* a, const psygfx__cmd* b) {
    return a->pipe == b->pipe && a->inst == 0 && b->inst == 0 && a->nblk == 1 && b->nblk == 1 && a->blend == b->blend &&
           a->tex[0] == b->tex[0] && a->tex[1] == b->tex[1] && a->tex[2] == b->tex[2] && a->tex[3] == b->tex[3];
}

/* Closes the segment being recorded and opens the next. */
static void psygfx__seg_next(psygfx_gfx* g, uint32_t target, int w, int h, int topdown, int slot, const float* clear) {
    psygfx__seg* sg;
    g->segs[g->n_segs - 1].end = g->n_cmds;
    sg = &g->segs[g->n_segs++];
    memset(sg, 0, sizeof *sg);
    sg->target = target; sg->w = w; sg->h = h; sg->topdown = topdown;
    sg->first = g->n_cmds; sg->frame_slot = slot;
    if (clear) { sg->has_clear = 1; memcpy(sg->clear, clear, sizeof sg->clear); }
}

PSYGFX_API int psygfx_begin_target(psygfx_gfx* g, psygfx_tex t, const float* clear) {
    psygfx__res* r;
    if (!g || !g->open) return PSYGFX_ERR_CLOSED;
    if (!g->in_frame || g->in_target || g->n_targets >= PSYGFX_MAX_PASSES) return PSYGFX_ERR_ORDER;
    if (t.id < 1 || t.id > PSYGFX_MAX_TEXTURES || !g->tex[t.id - 1].used || !(g->tex[t.id - 1].flags & PSYGFX__RES_TARGET))
        return PSYGFX_ERR_ARG;
    r = &g->tex[t.id - 1];
    g->n_targets++;
    g->in_target = 1;
    g->cur_target_tex = t.id;
    g->cur_w = r->w; g->cur_h = r->h; g->cur_top = 1;
    psygfx__frame_block(g, g->n_targets, r->w, r->h, 1);
    psygfx__seg_next(g, r->bid, r->w, r->h, 1, g->n_targets, clear);
    return PSYGFX_OK;
}

PSYGFX_API int psygfx_end_target(psygfx_gfx* g) {
    if (!g || !g->open) return PSYGFX_ERR_CLOSED;
    if (!g->in_frame || !g->in_target) return PSYGFX_ERR_ORDER;
    g->in_target = 0;
    g->cur_target_tex = 0;
    g->cur_w = g->w; g->cur_h = g->h; g->cur_top = 0;
    psygfx__seg_next(g, 0, g->w, g->h, 0, 0, NULL);
    return PSYGFX_OK;
}

PSYGFX_API int psygfx_end(psygfx_gfx* g) {
    psygfx_bindings bd;
    uint32_t ubo;
    float clear[4];
    int i, j, k, rc, scene_cleared = 0;
    PSYRT_ZONE(z, "psygfx.end");
    if (!g || !g->open) { PSYRT_ZONE_END(z); return PSYGFX_ERR_CLOSED; }
    if (!g->in_frame) { PSYRT_ZONE_END(z); return PSYGFX_ERR_ORDER; }
    if (g->in_target) psygfx_end_target(g);
    g->in_frame = 0;
    g->segs[g->n_segs - 1].end = g->n_cmds;
    if (psygfx__sync(g) < 0) { PSYRT_ZONE_END(z); return PSYGFX_ERR_LOST; }
    ubo = g->ubo[g->ubo_i];
    g->ubo_i = (g->ubo_i + 1) % PSYGFX__UBO_RING;
    /* One upload for the frame: the frame blocks and every stim block. */
    rc = g->be->buffer_update(g->bctx, ubo, 0, g->staging, PSYGFX__FRAME_BYTES + (size_t)g->n_blocks * PSYGFX__BLOCK);
    if (rc < 0) { PSYRT_ZONE_END(z); return PSYGFX_ERR_GL; }
    clear[0] = g->bg[0]; clear[1] = g->bg[1]; clear[2] = g->bg[2]; clear[3] = 1.0f;
    memset(&bd, 0, sizeof bd);
    bd.ubo = ubo;
    bd.frame_size = PSYGFX__BLOCK;
    bd.stim_size = PSYGFX__STIM_RANGE;
    /* The segments in call order: the scene is cleared at its first, a
     * target only when its begin asked. An empty scene segment is skipped
     * unless the scene still needs its clear. */
    for (k = 0; k < g->n_segs; k++) {
        const psygfx__seg* sg = &g->segs[k];
        if (sg->target == 0) {
            if (sg->first == sg->end && scene_cleared) continue;
            g->be->pass_begin(g->bctx, g->scene, g->w, g->h, scene_cleared ? NULL : clear);
            scene_cleared = 1;
        } else {
            g->be->pass_begin(g->bctx, sg->target, sg->w, sg->h, sg->has_clear ? sg->clear : NULL);
        }
        bd.frame_off = (uint32_t)sg->frame_slot * PSYGFX__BLOCK;
        for (i = sg->first; i < sg->end; i = j) {
            const psygfx__cmd* c = &g->cmds[i];
            for (j = i + 1; j < sg->end && j - i < g->max_batch && psygfx__same_batch(c, &g->cmds[j]); j++) {}
            bd.pipeline = c->pipe;
            bd.stim_off = PSYGFX__FRAME_BYTES + c->block * PSYGFX__BLOCK;
            memcpy(bd.tex, c->tex, sizeof bd.tex);
            bd.instances = c->inst;
            bd.blend = c->blend;
            g->be->apply(g->bctx, &bd);
            g->be->draw(g->bctx, 4, c->inst ? (int)c->count : j - i);
            g->draws++;
        }
        g->be->pass_end(g->bctx);
    }
    {
        PSYRT_ZONE(zo, "psygfx.output");
        g->be->pass_begin(g->bctx, 0, g->w, g->h, NULL);
        memset(&bd, 0, sizeof bd);
        bd.pipeline = g->output_pipe;
        bd.ubo = ubo;
        bd.frame_size = PSYGFX__BLOCK;
        bd.tex[0] = g->scene;
        bd.tex[1] = g->lut;
        g->be->apply(g->bctx, &bd);
        g->be->draw(g->bctx, 3, 1);
        g->be->pass_end(g->bctx);
        g->be->present(g->bctx);
        PSYRT_ZONE_END(zo);
    }
    g->frames++;
    if (g->clipped) {
        g->clipped_total += g->clipped;
        psygfx__push(g, (uint16_t)PSYGFX_EV_CLIPPED, (uint64_t)g->frame.onset, g->clipped, NULL, 0, g->frame.index);
    }
    PSYRT_ZONE_END(z);
    return PSYGFX_OK;
}

/* --- resources ---------------------------------------------------------------- */

static int psygfx__slot(psygfx__res* pool, int n) {
    int i;
    for (i = 0; i < n; i++) if (!pool[i].used) return i;
    return -1;
}

PSYGFX_API psygfx_tex psygfx_texture(psygfx_gfx* g, const psygfx_texture_desc* d) {
    psygfx_tex t;
    psygfx_texture_src src;
    int slot;
    t.id = 0;
    if (!g || !g->open || !d) return t;
    if (g->in_frame) { psygfx__set_error(g->error, sizeof g->error, "psy_gfx: texture made inside begin..end"); return t; }
    if (d->w <= 0 || d->h <= 0 || psygfx__texel_bytes(d->format) == 0) {
        psygfx__set_error(g->error, sizeof g->error, "psy_gfx: texture size or format");
        return t;
    }
    if ((d->sdf == PSYGFX_SDF_MSDF || d->sdf == PSYGFX_SDF_MTSDF) &&
        (!(d->sdf_range > 0.0f) || (d->format != PSYGFX_RGBA8 && d->format != PSYGFX_RGBA16F && d->format != PSYGFX_RGBA32F))) {
        psygfx__set_error(g->error, sizeof g->error, "psy_gfx: an MSDF or MTSDF texture is RGBA8, RGBA16F or RGBA32F with sdf_range > 0");
        return t;
    }
    if (d->sdf > PSYGFX_SDF_MTSDF) { psygfx__set_error(g->error, sizeof g->error, "psy_gfx: sdf kind"); return t; }
    if (d->linear && (d->format == PSYGFX_R32F || d->format == PSYGFX_RGBA32F) && !g->caps.float_linear) {
        psygfx__set_error(g->error, sizeof g->error, "psy_gfx: linear filtering of a 32-bit float texture needs GL_OES_texture_float_linear");
        return t;
    }
    slot = psygfx__slot(g->tex, PSYGFX_MAX_TEXTURES);
    if (slot < 0) { psygfx__set_error(g->error, sizeof g->error, "psy_gfx: %d textures in use", PSYGFX_MAX_TEXTURES); return t; }
    if (psygfx__sync(g) < 0) return t;
    memset(&src, 0, sizeof src);
    src.w = d->w; src.h = d->h; src.format = d->format; src.linear = d->linear;
    src.data = d->data; src.stride = d->stride;
    if (g->be->texture_make(g->bctx, &src, &g->tex[slot].bid) < 0) {
        psygfx__set_error(g->error, sizeof g->error, "psy_gfx: texture %dx%d", d->w, d->h);
        return t;
    }
    g->tex[slot].used = 1;
    g->tex[slot].w = d->w; g->tex[slot].h = d->h; g->tex[slot].format = d->format;
    g->tex[slot].sdf = (int32_t)d->sdf; g->tex[slot].sdf_range = d->sdf_range;
    t.id = (uint32_t)slot + 1;
    return t;
}

PSYGFX_API int psygfx_texture_update(psygfx_gfx* g, psygfx_tex t, int x, int y, int w, int h,
                                     const void* data, size_t stride) {
    const psygfx__res* r;
    int rc;
    PSYRT_ZONE(z, "psygfx.upload");
    if (!g || !g->open) { PSYRT_ZONE_END(z); return PSYGFX_ERR_CLOSED; }
    if (g->in_frame) { PSYRT_ZONE_END(z); return PSYGFX_ERR_ORDER; }
    if (t.id < 1 || t.id > PSYGFX_MAX_TEXTURES || !g->tex[t.id - 1].used || !data) { PSYRT_ZONE_END(z); return PSYGFX_ERR_ARG; }
    r = &g->tex[t.id - 1];
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > r->w || y + h > r->h) { PSYRT_ZONE_END(z); return PSYGFX_ERR_ARG; }
    if (psygfx__sync(g) < 0) { PSYRT_ZONE_END(z); return PSYGFX_ERR_LOST; }
    rc = g->be->texture_update(g->bctx, r->bid, x, y, w, h, data, stride);
    PSYRT_ZONE_END(z);
    return rc;
}

PSYGFX_API void psygfx_texture_free(psygfx_gfx* g, psygfx_tex t) {
    if (!g || !g->open || g->in_frame || t.id < 1 || t.id > PSYGFX_MAX_TEXTURES || !g->tex[t.id - 1].used) return;
    if (psygfx__sync(g) < 0) return;
    g->be->texture_free(g->bctx, g->tex[t.id - 1].bid);
    memset(&g->tex[t.id - 1], 0, sizeof g->tex[0]);
}

PSYGFX_API psygfx_buf psygfx_buffer(psygfx_gfx* g, size_t bytes) {
    psygfx_buf b;
    int slot;
    b.id = 0;
    if (!g || !g->open || bytes == 0 || bytes > 0x7FFFFFFFu) return b;
    if (g->in_frame) { psygfx__set_error(g->error, sizeof g->error, "psy_gfx: buffer made inside begin..end"); return b; }
    slot = psygfx__slot(g->buf, PSYGFX_MAX_BUFFERS);
    if (slot < 0) { psygfx__set_error(g->error, sizeof g->error, "psy_gfx: %d buffers in use", PSYGFX_MAX_BUFFERS); return b; }
    if (psygfx__sync(g) < 0) return b;
    if (g->be->buffer_make(g->bctx, PSYGFX_BUFFER_INSTANCE, bytes, &g->buf[slot].bid) < 0) {
        psygfx__set_error(g->error, sizeof g->error, "psy_gfx: buffer of %lu bytes", (unsigned long)bytes);
        return b;
    }
    g->buf[slot].used = 1;
    g->buf[slot].w = (int32_t)bytes;
    b.id = (uint32_t)slot + 1;
    return b;
}

PSYGFX_API int psygfx_buffer_update(psygfx_gfx* g, psygfx_buf b, size_t off, const void* data, size_t n) {
    int rc;
    PSYRT_ZONE(z, "psygfx.upload");
    if (!g || !g->open) { PSYRT_ZONE_END(z); return PSYGFX_ERR_CLOSED; }
    if (g->in_frame) { PSYRT_ZONE_END(z); return PSYGFX_ERR_ORDER; }
    if (b.id < 1 || b.id > PSYGFX_MAX_BUFFERS || !g->buf[b.id - 1].used || !data || off + n > (size_t)g->buf[b.id - 1].w) {
        PSYRT_ZONE_END(z);
        return PSYGFX_ERR_ARG;
    }
    if (psygfx__sync(g) < 0) { PSYRT_ZONE_END(z); return PSYGFX_ERR_LOST; }
    rc = g->be->buffer_update(g->bctx, g->buf[b.id - 1].bid, off, data, n);
    PSYRT_ZONE_END(z);
    return rc;
}

PSYGFX_API void psygfx_buffer_free(psygfx_gfx* g, psygfx_buf b) {
    if (!g || !g->open || g->in_frame || b.id < 1 || b.id > PSYGFX_MAX_BUFFERS || !g->buf[b.id - 1].used) return;
    if (psygfx__sync(g) < 0) return;
    g->be->buffer_free(g->bctx, g->buf[b.id - 1].bid);
    memset(&g->buf[b.id - 1], 0, sizeof g->buf[0]);
}

PSYGFX_API psygfx_pipe psygfx_pipeline(psygfx_gfx* g, const psygfx_pipeline_desc* d) {
    psygfx_pipe p;
    int slot;
    p.id = 0;
    if (!g || !g->open || !d || !d->body) return p;
    if (g->in_frame) { psygfx__set_error(g->error, sizeof g->error, "psy_gfx: pipeline made inside begin..end"); return p; }
    slot = psygfx__slot(g->pipe, PSYGFX_MAX_PIPELINES);
    if (slot < 0) { psygfx__set_error(g->error, sizeof g->error, "psy_gfx: %d pipelines in use", PSYGFX_MAX_PIPELINES); return p; }
    if (psygfx__sync(g) < 0) return p;
    if (psygfx__make_pipe(g, "", d->body, (int)d->mode, 0, 0, &g->pipe[slot].bid, d->name ? d->name : "user") < 0) return p;
    g->be->reset(g->bctx);
    g->pipe[slot].used = 1;
    g->pipe[slot].flags = (int32_t)d->mode;
    g->error[0] = '\0';
    p.id = (uint32_t)slot + 1;
    return p;
}

PSYGFX_API void psygfx_pipeline_free(psygfx_gfx* g, psygfx_pipe p) {
    if (!g || !g->open || g->in_frame || p.id < 1 || p.id > PSYGFX_MAX_PIPELINES || !g->pipe[p.id - 1].used) return;
    if (psygfx__sync(g) < 0) return;
    g->be->pipeline_free(g->bctx, g->pipe[p.id - 1].bid);
    memset(&g->pipe[p.id - 1], 0, sizeof g->pipe[0]);
}

PSYGFX_API int psygfx_set_lut(psygfx_gfx* g, const float* lut, int n) {
    int rc;
    PSYRT_ZONE(z, "psygfx.upload");
    if (!g || !g->open) { PSYRT_ZONE_END(z); return PSYGFX_ERR_CLOSED; }
    if (g->in_frame) { PSYRT_ZONE_END(z); return PSYGFX_ERR_ORDER; }
    if (!lut || n < 2 || n > PSYGFX_CAL_MAX_LUT) { PSYRT_ZONE_END(z); return PSYGFX_ERR_ARG; }
    if (psygfx__sync(g) < 0) { PSYRT_ZONE_END(z); return PSYGFX_ERR_LOST; }
    rc = psygfx__lut_upload(g, lut, n);
    if (rc >= 0) {
        uint32_t u[2];
        g->lut_n = n;
        u[0] = (uint32_t)n;
        u[1] = psygfx__crc32(lut, (size_t)n * 3 * sizeof(float));
        psygfx__push(g, (uint16_t)PSYGFX_EV_LUT, 0, 0, u, 2, 0);
    }
    PSYRT_ZONE_END(z);
    return rc;
}

/* Reads bottom-up from GL, then turns the rows over in place. */
static void psygfx__flip_rows(unsigned char* p, int w, int h, size_t bpp) {
    size_t row = (size_t)w * bpp;
    int y;
    for (y = 0; y < h / 2; y++) {
        unsigned char* a = p + (size_t)y * row;
        unsigned char* b = p + (size_t)(h - 1 - y) * row;
        size_t k;
        for (k = 0; k < row; k++) { unsigned char t = a[k]; a[k] = b[k]; b[k] = t; }
    }
}

PSYGFX_API int psygfx_read_scene(psygfx_gfx* g, int x, int y, int w, int h, float* rgba) {
    int rc;
    if (!g || !g->open) return PSYGFX_ERR_CLOSED;
    if (g->in_frame) return PSYGFX_ERR_ORDER;
    if (!rgba || x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > g->w || y + h > g->h) return PSYGFX_ERR_ARG;
    if (psygfx__sync(g) < 0) return PSYGFX_ERR_LOST;
    rc = g->be->read(g->bctx, g->scene, x, g->h - y - h, w, h, PSYGFX_READ_FLOAT, rgba);
    g->be->reset(g->bctx);
    if (rc >= 0) psygfx__flip_rows((unsigned char*)rgba, w, h, 4 * sizeof(float));
    return rc;
}

PSYGFX_API int psygfx_read_output(psygfx_gfx* g, int x, int y, int w, int h, uint8_t* rgba) {
    int rc;
    if (!g || !g->open) return PSYGFX_ERR_CLOSED;
    if (g->in_frame) return PSYGFX_ERR_ORDER;
    if (!rgba || x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > g->w || y + h > g->h) return PSYGFX_ERR_ARG;
    if (psygfx__sync(g) < 0) return PSYGFX_ERR_LOST;
    rc = g->be->read(g->bctx, 0, x, g->origin_top ? y : g->h - y - h, w, h, PSYGFX_READ_UNORM, rgba);
    g->be->reset(g->bctx);
    if (rc >= 0 && !g->origin_top) psygfx__flip_rows(rgba, w, h, 4);
    return rc;
}

PSYGFX_API psygfx_tex psygfx_target(psygfx_gfx* g, const psygfx_target_desc* d) {
    psygfx_tex t;
    psygfx_texture_src src;
    psygfx_format fmt;
    int slot;
    t.id = 0;
    if (!g || !g->open || !d) return t;
    if (g->in_frame) { psygfx__set_error(g->error, sizeof g->error, "psy_gfx: a target made inside begin..end"); return t; }
    fmt = d->format ? d->format : PSYGFX_RGBA16F;
    if (d->w <= 0 || d->h <= 0 || (fmt != PSYGFX_RGBA16F && fmt != PSYGFX_RGBA8)) {
        psygfx__set_error(g->error, sizeof g->error, "psy_gfx: a target is RGBA16F or RGBA8, with a size");
        return t;
    }
    slot = psygfx__slot(g->tex, PSYGFX_MAX_TEXTURES);
    if (slot < 0) { psygfx__set_error(g->error, sizeof g->error, "psy_gfx: %d textures in use", PSYGFX_MAX_TEXTURES); return t; }
    if (psygfx__sync(g) < 0) return t;
    memset(&src, 0, sizeof src);
    src.w = d->w; src.h = d->h; src.format = fmt; src.linear = d->linear; src.target = true;
    if (g->be->texture_make(g->bctx, &src, &g->tex[slot].bid) < 0) {
        psygfx__set_error(g->error, sizeof g->error, "psy_gfx: target %dx%d", d->w, d->h);
        return t;
    }
    {   /* a new target starts transparent black */
        static const float zero[4] = { 0, 0, 0, 0 };
        g->be->pass_begin(g->bctx, g->tex[slot].bid, d->w, d->h, zero);
        g->be->pass_end(g->bctx);
        g->be->reset(g->bctx);
    }
    g->tex[slot].used = 1;
    g->tex[slot].w = d->w; g->tex[slot].h = d->h; g->tex[slot].format = fmt;
    g->tex[slot].flags = PSYGFX__RES_TARGET;
    t.id = (uint32_t)slot + 1;
    return t;
}

/* A target's rows are top first already: no turn. */
PSYGFX_API int psygfx_read_target(psygfx_gfx* g, psygfx_tex t, int x, int y, int w, int h, float* rgba) {
    const psygfx__res* r;
    int rc;
    if (!g || !g->open) return PSYGFX_ERR_CLOSED;
    if (g->in_frame) return PSYGFX_ERR_ORDER;
    if (t.id < 1 || t.id > PSYGFX_MAX_TEXTURES || !g->tex[t.id - 1].used || !(g->tex[t.id - 1].flags & PSYGFX__RES_TARGET))
        return PSYGFX_ERR_ARG;
    r = &g->tex[t.id - 1];
    if (!rgba || x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > r->w || y + h > r->h) return PSYGFX_ERR_ARG;
    if (psygfx__sync(g) < 0) return PSYGFX_ERR_LOST;
    if (r->format == PSYGFX_RGBA8) {
        /* GL ES reads a fixed-point target as bytes only: read them into the
         * front of the caller's floats, then widen from the back. */
        unsigned char* b8 = (unsigned char*)rgba;
        size_t n = (size_t)w * (size_t)h * 4, i;
        rc = g->be->read(g->bctx, r->bid, x, y, w, h, PSYGFX_READ_UNORM, b8);
        if (rc >= 0) for (i = n; i-- > 0;) rgba[i] = (float)b8[i] * (1.0f / 255.0f);
    } else {
        rc = g->be->read(g->bctx, r->bid, x, y, w, h, PSYGFX_READ_FLOAT, rgba);
    }
    g->be->reset(g->bctx);
    return rc;
}
/* Stockman and Sharpe (2000) 2-degree cone fundamentals, linear energy, normalized
 * to peak 1: CVRL linss2_10e_1 (cvrl.org), 390 to 830 nm at 1 nm, L, M, S. CVRL
 * leaves S blank above 615 nm; it is 0 here. */
static const float psygfx__ss2[441][3] = {
    /* 390 */ {4.15003E-04f, 3.68349E-04f, 9.54729E-03f}, {5.02650E-04f, 4.48015E-04f, 1.14794E-02f}, {6.07367E-04f, 5.43965E-04f, 1.37986E-02f},
    /* 393 */ {7.31850E-04f, 6.58983E-04f, 1.65746E-02f}, {8.79012E-04f, 7.96121E-04f, 1.98869E-02f}, {1.05192E-03f, 9.58658E-04f, 2.38250E-02f},
    /* 396 */ {1.25373E-03f, 1.15002E-03f, 2.84877E-02f}, {1.48756E-03f, 1.37367E-03f, 3.39832E-02f}, {1.75633E-03f, 1.63296E-03f, 4.04274E-02f},
    /* 399 */ {2.06261E-03f, 1.93089E-03f, 4.79417E-02f}, {2.40836E-03f, 2.26991E-03f, 5.66498E-02f}, {2.79522E-03f, 2.65210E-03f, 6.66757E-02f},
    /* 402 */ {3.22640E-03f, 3.08110E-03f, 7.81479E-02f}, {3.70617E-03f, 3.56156E-03f, 9.11925E-02f}, {4.23972E-03f, 4.09900E-03f, 1.05926E-01f},
    /* 405 */ {4.83339E-03f, 4.70010E-03f, 1.22451E-01f}, {5.49335E-03f, 5.37186E-03f, 1.40844E-01f}, {6.21933E-03f, 6.11757E-03f, 1.61140E-01f},
    /* 408 */ {7.00631E-03f, 6.93795E-03f, 1.83325E-01f}, {7.84503E-03f, 7.83144E-03f, 2.07327E-01f}, {8.72127E-03f, 8.79369E-03f, 2.33008E-01f},
    /* 411 */ {9.61879E-03f, 9.81865E-03f, 2.60183E-01f}, {1.05324E-02f, 1.09044E-02f, 2.88723E-01f}, {1.14620E-02f, 1.20509E-02f, 3.18512E-01f},
    /* 414 */ {1.24105E-02f, 1.32582E-02f, 3.49431E-01f}, {1.33837E-02f, 1.45277E-02f, 3.81363E-01f}, {1.43870E-02f, 1.58600E-02f, 4.14141E-01f},
    /* 417 */ {1.54116E-02f, 1.72496E-02f, 4.47350E-01f}, {1.64424E-02f, 1.86878E-02f, 4.80439E-01f}, {1.74614E-02f, 2.01638E-02f, 5.12767E-01f},
    /* 420 */ {1.84480E-02f, 2.16649E-02f, 5.43618E-01f}, {1.93852E-02f, 2.31801E-02f, 5.72399E-01f}, {2.02811E-02f, 2.47139E-02f, 5.99284E-01f},
    /* 423 */ {2.11545E-02f, 2.62785E-02f, 6.24786E-01f}, {2.20286E-02f, 2.78905E-02f, 6.49576E-01f}, {2.29317E-02f, 2.95714E-02f, 6.74474E-01f},
    /* 426 */ {2.38896E-02f, 3.13438E-02f, 7.00186E-01f}, {2.49026E-02f, 3.32151E-02f, 7.26460E-01f}, {2.59631E-02f, 3.51889E-02f, 7.52726E-01f},
    /* 429 */ {2.70619E-02f, 3.72683E-02f, 7.78333E-01f}, {2.81877E-02f, 3.94566E-02f, 8.02555E-01f}, {2.93303E-02f, 4.17546E-02f, 8.24818E-01f},
    /* 432 */ {3.04898E-02f, 4.41544E-02f, 8.45422E-01f}, {3.16694E-02f, 4.66432E-02f, 8.64961E-01f}, {3.28731E-02f, 4.92050E-02f, 8.84100E-01f},
    /* 435 */ {3.41054E-02f, 5.18199E-02f, 9.03573E-01f}, {3.53670E-02f, 5.44645E-02f, 9.23844E-01f}, {3.66400E-02f, 5.71131E-02f, 9.44055E-01f},
    /* 438 */ {3.78988E-02f, 5.97369E-02f, 9.62920E-01f}, {3.91148E-02f, 6.23038E-02f, 9.79057E-01f}, {4.02563E-02f, 6.47782E-02f, 9.91020E-01f},
    /* 441 */ {4.12989E-02f, 6.71329E-02f, 9.97765E-01f}, {4.22582E-02f, 6.93844E-02f, 9.99982E-01f}, {4.31627E-02f, 7.15658E-02f, 9.98861E-01f},
    /* 444 */ {4.40444E-02f, 7.37164E-02f, 9.95628E-01f}, {4.49380E-02f, 7.58812E-02f, 9.91515E-01f}, {4.58729E-02f, 7.80980E-02f, 9.87377E-01f},
    /* 447 */ {4.68459E-02f, 8.03538E-02f, 9.82619E-01f}, {4.78446E-02f, 8.26198E-02f, 9.76301E-01f}, {4.88555E-02f, 8.48644E-02f, 9.67513E-01f},
    /* 450 */ {4.98639E-02f, 8.70524E-02f, 9.55393E-01f}, {5.08618E-02f, 8.91640E-02f, 9.39499E-01f}, {5.18731E-02f, 9.12523E-02f, 9.20807E-01f},
    /* 453 */ {5.29317E-02f, 9.33950E-02f, 9.00592E-01f}, {5.40746E-02f, 9.56775E-02f, 8.80040E-01f}, {5.53418E-02f, 9.81934E-02f, 8.60240E-01f},
    /* 456 */ {5.67734E-02f, 1.01031E-01f, 8.42031E-01f}, {5.83973E-02f, 1.04229E-01f, 8.25572E-01f}, {6.02409E-02f, 1.07814E-01f, 8.10860E-01f},
    /* 459 */ {6.23353E-02f, 1.11817E-01f, 7.97902E-01f}, {6.47164E-02f, 1.16272E-01f, 7.86704E-01f}, {6.74131E-02f, 1.21204E-01f, 7.77119E-01f},
    /* 462 */ {7.04040E-02f, 1.26573E-01f, 7.68365E-01f}, {7.36489E-02f, 1.32311E-01f, 7.59538E-01f}, {7.70978E-02f, 1.38334E-01f, 7.49777E-01f},
    /* 465 */ {8.06894E-02f, 1.44541E-01f, 7.38268E-01f}, {8.43613E-02f, 1.50828E-01f, 7.24389E-01f}, {8.80904E-02f, 1.57146E-01f, 7.08113E-01f},
    /* 468 */ {9.18630E-02f, 1.63457E-01f, 6.89572E-01f}, {9.56637E-02f, 1.69721E-01f, 6.68927E-01f}, {9.94755E-02f, 1.75893E-01f, 6.46359E-01f},
    /* 471 */ {1.03286E-01f, 1.81938E-01f, 6.22112E-01f}, {1.07103E-01f, 1.87872E-01f, 5.96591E-01f}, {1.10947E-01f, 1.93731E-01f, 5.70216E-01f},
    /* 474 */ {1.14838E-01f, 1.99557E-01f, 5.43373E-01f}, {1.18802E-01f, 2.05398E-01f, 5.16411E-01f}, {1.22863E-01f, 2.11302E-01f, 4.89647E-01f},
    /* 477 */ {1.27026E-01f, 2.17283E-01f, 4.63406E-01f}, {1.31293E-01f, 2.23347E-01f, 4.37965E-01f}, {1.35666E-01f, 2.29501E-01f, 4.13549E-01f},
    /* 480 */ {1.40145E-01f, 2.35754E-01f, 3.90333E-01f}, {1.44731E-01f, 2.42108E-01f, 3.68394E-01f}, {1.49415E-01f, 2.48545E-01f, 3.47582E-01f},
    /* 483 */ {1.54189E-01f, 2.55037E-01f, 3.27724E-01f}, {1.59038E-01f, 2.61554E-01f, 3.08676E-01f}, {1.63952E-01f, 2.68063E-01f, 2.90322E-01f},
    /* 486 */ {1.68932E-01f, 2.74561E-01f, 2.72626E-01f}, {1.74063E-01f, 2.81180E-01f, 2.55772E-01f}, {1.79457E-01f, 2.88097E-01f, 2.39945E-01f},
    /* 489 */ {1.85241E-01f, 2.95508E-01f, 2.25280E-01f}, {1.91556E-01f, 3.03630E-01f, 2.11867E-01f}, {1.98532E-01f, 3.12649E-01f, 1.99719E-01f},
    /* 492 */ {2.06182E-01f, 3.22565E-01f, 1.88674E-01f}, {2.14485E-01f, 3.33319E-01f, 1.78557E-01f}, {2.23411E-01f, 3.44844E-01f, 1.69217E-01f},
    /* 495 */ {2.32926E-01f, 3.57061E-01f, 1.60526E-01f}, {2.42992E-01f, 3.69895E-01f, 1.52368E-01f}, {2.53616E-01f, 3.83355E-01f, 1.44620E-01f},
    /* 498 */ {2.64810E-01f, 3.97467E-01f, 1.37173E-01f}, {2.76587E-01f, 4.12260E-01f, 1.29937E-01f}, {2.88959E-01f, 4.27764E-01f, 1.22839E-01f},
    /* 501 */ {3.01934E-01f, 4.44001E-01f, 1.15834E-01f}, {3.15508E-01f, 4.60947E-01f, 1.08922E-01f}, {3.29673E-01f, 4.78562E-01f, 1.02118E-01f},
    /* 504 */ {3.44416E-01f, 4.96795E-01f, 9.54374E-02f}, {3.59716E-01f, 5.15587E-01f, 8.88965E-02f}, {3.75550E-01f, 5.34868E-01f, 8.25343E-02f},
    /* 507 */ {3.91895E-01f, 5.54575E-01f, 7.64613E-02f}, {4.08722E-01f, 5.74640E-01f, 7.07769E-02f}, {4.25998E-01f, 5.94984E-01f, 6.55491E-02f},
    /* 510 */ {4.43683E-01f, 6.15520E-01f, 6.08210E-02f}, {4.61731E-01f, 6.36162E-01f, 5.65919E-02f}, {4.80094E-01f, 6.56872E-01f, 5.27659E-02f},
    /* 513 */ {4.98717E-01f, 6.77624E-01f, 4.92440E-02f}, {5.17539E-01f, 6.98393E-01f, 4.59470E-02f}, {5.36494E-01f, 7.19154E-01f, 4.28123E-02f},
    /* 516 */ {5.55493E-01f, 7.39844E-01f, 3.98014E-02f}, {5.74386E-01f, 7.60235E-01f, 3.69227E-02f}, {5.92995E-01f, 7.80039E-01f, 3.41909E-02f},
    /* 519 */ {6.11124E-01f, 7.98941E-01f, 3.16158E-02f}, {6.28561E-01f, 8.16610E-01f, 2.92033E-02f}, {6.45139E-01f, 8.32783E-01f, 2.69536E-02f},
    /* 522 */ {6.60907E-01f, 8.47550E-01f, 2.48575E-02f}, {6.75993E-01f, 8.61113E-01f, 2.29047E-02f}, {6.90542E-01f, 8.73698E-01f, 2.10855E-02f},
    /* 525 */ {7.04720E-01f, 8.85550E-01f, 1.93912E-02f}, {7.18662E-01f, 8.96873E-01f, 1.78145E-02f}, {7.32323E-01f, 9.07638E-01f, 1.63514E-02f},
    /* 528 */ {7.45606E-01f, 9.17757E-01f, 1.49980E-02f}, {7.58410E-01f, 9.27137E-01f, 1.37497E-02f}, {7.70630E-01f, 9.35687E-01f, 1.26013E-02f},
    /* 531 */ {7.82208E-01f, 9.43362E-01f, 1.15466E-02f}, {7.93290E-01f, 9.50309E-01f, 1.05766E-02f}, {8.04084E-01f, 9.56733E-01f, 9.68268E-03f},
    /* 534 */ {8.14813E-01f, 9.62844E-01f, 8.85746E-03f}, {8.25711E-01f, 9.68858E-01f, 8.09453E-03f}, {8.36943E-01f, 9.74919E-01f, 7.38890E-03f},
    /* 537 */ {8.48349E-01f, 9.80850E-01f, 6.73799E-03f}, {8.59676E-01f, 9.86388E-01f, 6.13947E-03f}, {8.70656E-01f, 9.91267E-01f, 5.59073E-03f},
    /* 540 */ {8.81011E-01f, 9.95217E-01f, 5.08900E-03f}, {8.90496E-01f, 9.98007E-01f, 4.63120E-03f}, {8.99052E-01f, 9.99595E-01f, 4.21366E-03f},
    /* 543 */ {9.06669E-01f, 9.99982E-01f, 3.83286E-03f}, {9.13341E-01f, 9.99177E-01f, 3.48559E-03f}, {9.19067E-01f, 9.97193E-01f, 3.16893E-03f},
    /* 546 */ {9.23898E-01f, 9.94102E-01f, 2.88021E-03f}, {9.28099E-01f, 9.90204E-01f, 2.61698E-03f}, {9.31995E-01f, 9.85855E-01f, 2.37701E-03f},
    /* 549 */ {9.35916E-01f, 9.81404E-01f, 2.15829E-03f}, {9.40198E-01f, 9.77193E-01f, 1.95896E-03f}, {9.45076E-01f, 9.73441E-01f, 1.77737E-03f},
    /* 552 */ {9.50367E-01f, 9.69881E-01f, 1.61218E-03f}, {9.55775E-01f, 9.66133E-01f, 1.46215E-03f}, {9.61000E-01f, 9.61823E-01f, 1.32606E-03f},
    /* 555 */ {9.65733E-01f, 9.56583E-01f, 1.20277E-03f}, {9.69744E-01f, 9.50167E-01f, 1.09118E-03f}, {9.73133E-01f, 9.42773E-01f, 9.90135E-04f},
    /* 558 */ {9.76086E-01f, 9.34709E-01f, 8.98564E-04f}, {9.78793E-01f, 9.26271E-01f, 8.15524E-04f}, {9.81445E-01f, 9.17750E-01f, 7.40174E-04f},
    /* 561 */ {9.84186E-01f, 9.09339E-01f, 6.71772E-04f}, {9.86965E-01f, 9.00895E-01f, 6.09693E-04f}, {9.89678E-01f, 8.92197E-01f, 5.53372E-04f},
    /* 564 */ {9.92220E-01f, 8.83035E-01f, 5.02292E-04f}, {9.94486E-01f, 8.73205E-01f, 4.55979E-04f}, {9.96386E-01f, 8.62565E-01f, 4.13997E-04f},
    /* 567 */ {9.97895E-01f, 8.51173E-01f, 3.75940E-04f}, {9.99004E-01f, 8.39132E-01f, 3.41439E-04f}, {9.99706E-01f, 8.26545E-01f, 3.10160E-04f},
    /* 570 */ {9.99993E-01f, 8.13509E-01f, 2.81800E-04f}, {9.99837E-01f, 8.00082E-01f, 2.56084E-04f}, {9.99123E-01f, 7.86166E-01f, 2.32764E-04f},
    /* 573 */ {9.97719E-01f, 7.71635E-01f, 2.11616E-04f}, {9.95491E-01f, 7.56376E-01f, 1.92436E-04f}, {9.92310E-01f, 7.40291E-01f, 1.75039E-04f},
    /* 576 */ {9.88146E-01f, 7.23369E-01f, 1.59257E-04f}, {9.83345E-01f, 7.05890E-01f, 1.44940E-04f}, {9.78342E-01f, 6.88184E-01f, 1.31948E-04f},
    /* 579 */ {9.73564E-01f, 6.70554E-01f, 1.20157E-04f}, {9.69429E-01f, 6.53274E-01f, 1.09454E-04f}, {9.66211E-01f, 6.36524E-01f, 9.97367E-05f},
    /* 582 */ {9.63630E-01f, 6.20217E-01f, 9.09125E-05f}, {9.61273E-01f, 6.04211E-01f, 8.28976E-05f}, {9.58732E-01f, 5.88375E-01f, 7.56160E-05f},
    /* 585 */ {9.55602E-01f, 5.72597E-01f, 6.89991E-05f}, {9.51569E-01f, 5.56783E-01f, 6.29846E-05f}, {9.46654E-01f, 5.40896E-01f, 5.75163E-05f},
    /* 588 */ {9.40962E-01f, 5.24912E-01f, 5.25432E-05f}, {9.34600E-01f, 5.08817E-01f, 4.80192E-05f}, {9.27673E-01f, 4.92599E-01f, 4.39024E-05f},
    /* 591 */ {9.20264E-01f, 4.76268E-01f, 4.01551E-05f}, {9.12391E-01f, 4.59893E-01f, 3.67431E-05f}, {9.04050E-01f, 4.43551E-01f, 3.36353E-05f},
    /* 594 */ {8.95243E-01f, 4.27314E-01f, 3.08037E-05f}, {8.85969E-01f, 4.11246E-01f, 2.82228E-05f}, {8.76242E-01f, 3.95396E-01f, 2.58697E-05f},
    /* 597 */ {8.66117E-01f, 3.79782E-01f, 2.37235E-05f}, {8.55658E-01f, 3.64410E-01f, 2.17653E-05f}, {8.44926E-01f, 3.49289E-01f, 1.99779E-05f},
    /* 600 */ {8.33982E-01f, 3.34429E-01f, 1.83459E-05f}, {8.22859E-01f, 3.19843E-01f, 1.68551E-05f}, {8.11491E-01f, 3.05564E-01f, 1.54929E-05f},
    /* 603 */ {7.99794E-01f, 2.91625E-01f, 1.42476E-05f}, {7.87689E-01f, 2.78053E-01f, 1.31087E-05f}, {7.75103E-01f, 2.64872E-01f, 1.20667E-05f},
    /* 606 */ {7.61996E-01f, 2.52099E-01f, 1.11131E-05f}, {7.48425E-01f, 2.39747E-01f, 1.02399E-05f}, {7.34470E-01f, 2.27822E-01f, 9.43999E-06f},
    /* 609 */ {7.20208E-01f, 2.16330E-01f, 8.70695E-06f}, {7.05713E-01f, 2.05273E-01f, 8.03488E-06f}, {6.91044E-01f, 1.94650E-01f, 7.41844E-06f},
    /* 612 */ {6.76212E-01f, 1.84448E-01f, 6.85279E-06f}, {6.61220E-01f, 1.74654E-01f, 6.33352E-06f}, {6.46072E-01f, 1.65256E-01f, 5.85662E-06f},
    /* 615 */ {6.30773E-01f, 1.56243E-01f, 5.41843E-06f}, {6.15349E-01f, 1.47602E-01f, 0.0f}, {5.99888E-01f, 1.39329E-01f, 0.0f},
    /* 618 */ {5.84489E-01f, 1.31416E-01f, 0.0f}, {5.69240E-01f, 1.23856E-01f, 0.0f}, {5.54224E-01f, 1.16641E-01f, 0.0f},
    /* 621 */ {5.39469E-01f, 1.09766E-01f, 0.0f}, {5.24827E-01f, 1.03226E-01f, 0.0f}, {5.10124E-01f, 9.70205E-02f, 0.0f},
    /* 624 */ {4.95206E-01f, 9.11430E-02f, 0.0f}, {4.79941E-01f, 8.55872E-02f, 0.0f}, {4.64270E-01f, 8.03428E-02f, 0.0f},
    /* 627 */ {4.48338E-01f, 7.53913E-02f, 0.0f}, {4.32329E-01f, 7.07136E-02f, 0.0f}, {4.16406E-01f, 6.62924E-02f, 0.0f},
    /* 630 */ {4.00711E-01f, 6.21120E-02f, 0.0f}, {3.85355E-01f, 5.81595E-02f, 0.0f}, {3.70377E-01f, 5.44276E-02f, 0.0f},
    /* 633 */ {3.55793E-01f, 5.09098E-02f, 0.0f}, {3.41618E-01f, 4.75991E-02f, 0.0f}, {3.27864E-01f, 4.44879E-02f, 0.0f},
    /* 636 */ {3.14541E-01f, 4.15659E-02f, 0.0f}, {3.01662E-01f, 3.88152E-02f, 0.0f}, {2.89239E-01f, 3.62181E-02f, 0.0f},
    /* 639 */ {2.77278E-01f, 3.37599E-02f, 0.0f}, {2.65784E-01f, 3.14282E-02f, 0.0f}, {2.54740E-01f, 2.92176E-02f, 0.0f},
    /* 642 */ {2.44054E-01f, 2.71403E-02f, 0.0f}, {2.33634E-01f, 2.52084E-02f, 0.0f}, {2.23399E-01f, 2.34286E-02f, 0.0f},
    /* 645 */ {2.13284E-01f, 2.18037E-02f, 0.0f}, {2.03257E-01f, 2.03285E-02f, 0.0f}, {1.93370E-01f, 1.89779E-02f, 0.0f},
    /* 648 */ {1.83688E-01f, 1.77272E-02f, 0.0f}, {1.74263E-01f, 1.65560E-02f, 0.0f}, {1.65141E-01f, 1.54480E-02f, 0.0f},
    /* 651 */ {1.56354E-01f, 1.43924E-02f, 0.0f}, {1.47916E-01f, 1.33896E-02f, 0.0f}, {1.39834E-01f, 1.24414E-02f, 0.0f},
    /* 654 */ {1.32111E-01f, 1.15488E-02f, 0.0f}, {1.24749E-01f, 1.07120E-02f, 0.0f}, {1.17744E-01f, 9.92994E-03f, 0.0f},
    /* 657 */ {1.11081E-01f, 9.20057E-03f, 0.0f}, {1.04747E-01f, 8.52126E-03f, 0.0f}, {9.87277E-02f, 7.88945E-03f, 0.0f},
    /* 660 */ {9.30085E-02f, 7.30255E-03f, 0.0f}, {8.75769E-02f, 6.75820E-03f, 0.0f}, {8.24219E-02f, 6.25474E-03f, 0.0f},
    /* 663 */ {7.75328E-02f, 5.79045E-03f, 0.0f}, {7.28989E-02f, 5.36347E-03f, 0.0f}, {6.85100E-02f, 4.97179E-03f, 0.0f},
    /* 666 */ {6.43554E-02f, 4.61300E-03f, 0.0f}, {6.04242E-02f, 4.28339E-03f, 0.0f}, {5.67057E-02f, 3.97940E-03f, 0.0f},
    /* 669 */ {5.31896E-02f, 3.69803E-03f, 0.0f}, {4.98661E-02f, 3.43667E-03f, 0.0f}, {4.67258E-02f, 3.19326E-03f, 0.0f},
    /* 672 */ {4.37598E-02f, 2.96653E-03f, 0.0f}, {4.09594E-02f, 2.75543E-03f, 0.0f}, {3.83165E-02f, 2.55896E-03f, 0.0f},
    /* 675 */ {3.58233E-02f, 2.37617E-03f, 0.0f}, {3.34725E-02f, 2.20618E-03f, 0.0f}, {3.12583E-02f, 2.04809E-03f, 0.0f},
    /* 678 */ {2.91751E-02f, 1.90110E-03f, 0.0f}, {2.72173E-02f, 1.76442E-03f, 0.0f}, {2.53790E-02f, 1.63734E-03f, 0.0f},
    /* 681 */ {2.36541E-02f, 1.51916E-03f, 0.0f}, {2.20336E-02f, 1.40913E-03f, 0.0f}, {2.05092E-02f, 1.30655E-03f, 0.0f},
    /* 684 */ {1.90735E-02f, 1.21078E-03f, 0.0f}, {1.77201E-02f, 1.12128E-03f, 0.0f}, {1.64451E-02f, 1.03766E-03f, 0.0f},
    /* 687 */ {1.52510E-02f, 9.59892E-04f, 0.0f}, {1.41404E-02f, 8.87951E-04f, 0.0f}, {1.31137E-02f, 8.21729E-04f, 0.0f},
    /* 690 */ {1.21701E-02f, 7.61051E-04f, 0.0f}, {1.13063E-02f, 7.05623E-04f, 0.0f}, {1.05131E-02f, 6.54874E-04f, 0.0f},
    /* 693 */ {9.78127E-03f, 6.08240E-04f, 0.0f}, {9.10300E-03f, 5.65240E-04f, 0.0f}, {8.47170E-03f, 5.25457E-04f, 0.0f},
    /* 696 */ {7.88227E-03f, 4.88550E-04f, 0.0f}, {7.33213E-03f, 4.54277E-04f, 0.0f}, {6.81928E-03f, 4.22436E-04f, 0.0f},
    /* 699 */ {6.34173E-03f, 3.92839E-04f, 0.0f}, {5.89749E-03f, 3.65317E-04f, 0.0f}, {5.48444E-03f, 3.39710E-04f, 0.0f},
    /* 702 */ {5.09978E-03f, 3.15856E-04f, 0.0f}, {4.74086E-03f, 2.93607E-04f, 0.0f}, {4.40538E-03f, 2.72834E-04f, 0.0f},
    /* 705 */ {4.09129E-03f, 2.53417E-04f, 0.0f}, {3.79703E-03f, 2.35265E-04f, 0.0f}, {3.52187E-03f, 2.18331E-04f, 0.0f},
    /* 708 */ {3.26520E-03f, 2.02574E-04f, 0.0f}, {3.02632E-03f, 1.87948E-04f, 0.0f}, {2.80447E-03f, 1.74402E-04f, 0.0f},
    /* 711 */ {2.59882E-03f, 1.61878E-04f, 0.0f}, {2.40849E-03f, 1.50305E-04f, 0.0f}, {2.23259E-03f, 1.39612E-04f, 0.0f},
    /* 714 */ {2.07024E-03f, 1.29734E-04f, 0.0f}, {1.92058E-03f, 1.20608E-04f, 0.0f}, {1.78269E-03f, 1.12176E-04f, 0.0f},
    /* 717 */ {1.65540E-03f, 1.04373E-04f, 0.0f}, {1.53762E-03f, 9.71383E-05f, 0.0f}, {1.42839E-03f, 9.04202E-05f, 0.0f},
    /* 720 */ {1.32687E-03f, 8.41716E-05f, 0.0f}, {1.23238E-03f, 7.83538E-05f, 0.0f}, {1.14456E-03f, 7.29425E-05f, 0.0f},
    /* 723 */ {1.06308E-03f, 6.79164E-05f, 0.0f}, {9.87592E-04f, 6.32542E-05f, 0.0f}, {9.17777E-04f, 5.89349E-05f, 0.0f},
    /* 726 */ {8.53264E-04f, 5.49360E-05f, 0.0f}, {7.93589E-04f, 5.12291E-05f, 0.0f}, {7.38306E-04f, 4.77872E-05f, 0.0f},
    /* 729 */ {6.87018E-04f, 4.45862E-05f, 0.0f}, {6.39373E-04f, 4.16049E-05f, 0.0f}, {5.95057E-04f, 3.88245E-05f, 0.0f},
    /* 732 */ {5.53797E-04f, 3.62302E-05f, 0.0f}, {5.15350E-04f, 3.38086E-05f, 0.0f}, {4.79496E-04f, 3.15474E-05f, 0.0f},
    /* 735 */ {4.46035E-04f, 2.94354E-05f, 0.0f}, {4.14809E-04f, 2.74634E-05f, 0.0f}, {3.85749E-04f, 2.56268E-05f, 0.0f},
    /* 738 */ {3.58792E-04f, 2.39217E-05f, 0.0f}, {3.33861E-04f, 2.23432E-05f, 0.0f}, {3.10869E-04f, 2.08860E-05f, 0.0f},
    /* 741 */ {2.89699E-04f, 1.95424E-05f, 0.0f}, {2.70145E-04f, 1.82990E-05f, 0.0f}, {2.52007E-04f, 1.71423E-05f, 0.0f},
    /* 744 */ {2.35117E-04f, 1.60611E-05f, 0.0f}, {2.19329E-04f, 1.50458E-05f, 0.0f}, {2.04535E-04f, 1.40893E-05f, 0.0f},
    /* 747 */ {1.90692E-04f, 1.31899E-05f, 0.0f}, {1.77771E-04f, 1.23462E-05f, 0.0f}, {1.65736E-04f, 1.15569E-05f, 0.0f},
    /* 750 */ {1.54549E-04f, 1.08200E-05f, 0.0f}, {1.44167E-04f, 1.01334E-05f, 0.0f}, {1.34528E-04f, 9.49367E-06f, 0.0f},
    /* 753 */ {1.25574E-04f, 8.89736E-06f, 0.0f}, {1.17251E-04f, 8.34135E-06f, 0.0f}, {1.09508E-04f, 7.82271E-06f, 0.0f},
    /* 756 */ {1.02300E-04f, 7.33865E-06f, 0.0f}, {9.55828E-05f, 6.88612E-06f, 0.0f}, {8.93161E-05f, 6.46228E-06f, 0.0f},
    /* 759 */ {8.34631E-05f, 6.06462E-06f, 0.0f}, {7.79912E-05f, 5.69093E-06f, 0.0f}, {7.28730E-05f, 5.33942E-06f, 0.0f},
    /* 762 */ {6.80921E-05f, 5.00929E-06f, 0.0f}, {6.36342E-05f, 4.69984E-06f, 0.0f}, {5.94841E-05f, 4.41034E-06f, 0.0f},
    /* 765 */ {5.56264E-05f, 4.13998E-06f, 0.0f}, {5.20430E-05f, 3.88772E-06f, 0.0f}, {4.87063E-05f, 3.65186E-06f, 0.0f},
    /* 768 */ {4.55900E-05f, 3.43070E-06f, 0.0f}, {4.26710E-05f, 3.22278E-06f, 0.0f}, {3.99295E-05f, 3.02683E-06f, 0.0f},
    /* 771 */ {3.73509E-05f, 2.84192E-06f, 0.0f}, {3.49326E-05f, 2.66795E-06f, 0.0f}, {3.26730E-05f, 2.50491E-06f, 0.0f},
    /* 774 */ {3.05690E-05f, 2.35267E-06f, 0.0f}, {2.86163E-05f, 2.21100E-06f, 0.0f}, {2.68077E-05f, 2.07946E-06f, 0.0f},
    /* 777 */ {2.51286E-05f, 1.95700E-06f, 0.0f}, {2.35645E-05f, 1.84258E-06f, 0.0f}, {2.21026E-05f, 1.73528E-06f, 0.0f},
    /* 780 */ {2.07321E-05f, 1.63433E-06f, 0.0f}, {1.94444E-05f, 1.53910E-06f, 0.0f}, {1.82351E-05f, 1.44932E-06f, 0.0f},
    /* 783 */ {1.71008E-05f, 1.36478E-06f, 0.0f}, {1.60380E-05f, 1.28526E-06f, 0.0f}, {1.50432E-05f, 1.21054E-06f, 0.0f},
    /* 786 */ {1.41127E-05f, 1.14038E-06f, 0.0f}, {1.32420E-05f, 1.07446E-06f, 0.0f}, {1.24263E-05f, 1.01247E-06f, 0.0f},
    /* 789 */ {1.16618E-05f, 9.54130E-07f, 0.0f}, {1.09446E-05f, 8.99170E-07f, 0.0f}, {1.02716E-05f, 8.47379E-07f, 0.0f},
    /* 792 */ {9.64036E-06f, 7.98635E-07f, 0.0f}, {9.04897E-06f, 7.52832E-07f, 0.0f}, {8.49535E-06f, 7.09857E-07f, 0.0f},
    /* 795 */ {7.97750E-06f, 6.69594E-07f, 0.0f}, {7.49341E-06f, 6.31907E-07f, 0.0f}, {7.04079E-06f, 5.96604E-07f, 0.0f},
    /* 798 */ {6.61744E-06f, 5.63495E-07f, 0.0f}, {6.22133E-06f, 5.32408E-07f, 0.0f}, {5.85057E-06f, 5.03187E-07f, 0.0f},
    /* 801 */ {5.50333E-06f, 4.75686E-07f, 0.0f}, {5.17756E-06f, 4.49753E-07f, 0.0f}, {4.87135E-06f, 4.25249E-07f, 0.0f},
    /* 804 */ {4.58300E-06f, 4.02050E-07f, 0.0f}, {4.31102E-06f, 3.80046E-07f, 0.0f}, {4.05422E-06f, 3.59156E-07f, 0.0f},
    /* 807 */ {3.81213E-06f, 3.39356E-07f, 0.0f}, {3.58438E-06f, 3.20633E-07f, 0.0f}, {3.37053E-06f, 3.02966E-07f, 0.0f},
    /* 810 */ {3.17009E-06f, 2.86329E-07f, 0.0f}, {2.98248E-06f, 2.70687E-07f, 0.0f}, {2.80691E-06f, 2.55980E-07f, 0.0f},
    /* 813 */ {2.64257E-06f, 2.42147E-07f, 0.0f}, {2.48873E-06f, 2.29130E-07f, 0.0f}, {2.34468E-06f, 2.16878E-07f, 0.0f},
    /* 816 */ {2.20974E-06f, 2.05338E-07f, 0.0f}, {2.08315E-06f, 1.94449E-07f, 0.0f}, {1.96419E-06f, 1.84155E-07f, 0.0f},
    /* 819 */ {1.85222E-06f, 1.74407E-07f, 0.0f}, {1.74666E-06f, 1.65158E-07f, 0.0f}, {1.64705E-06f, 1.56373E-07f, 0.0f},
    /* 822 */ {1.55307E-06f, 1.48031E-07f, 0.0f}, {1.46448E-06f, 1.40117E-07f, 0.0f}, {1.38100E-06f, 1.32615E-07f, 0.0f},
    /* 825 */ {1.30241E-06f, 1.25508E-07f, 0.0f}, {1.22844E-06f, 1.18781E-07f, 0.0f}, {1.15888E-06f, 1.12416E-07f, 0.0f},
    /* 828 */ {1.09348E-06f, 1.06398E-07f, 0.0f}, {1.03203E-06f, 1.00711E-07f, 0.0f}, {9.74306E-07f, 9.53411E-08f, 0.0f},

};

/* --- noise ------------------------------------------------------------------- */

PSYGFX_API uint32_t psygfx_hash(uint32_t v) {
    uint32_t s = v * 747796405u + 2891336453u;
    uint32_t w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}

/* The NOISE body's arithmetic, step for step: an odd 24-bit integer over
 * 2^23 is exact in a float, so the GPU and the CPU agree to the bit. */
PSYGFX_API float psygfx_noise_value(int32_t cx, int32_t cy, uint32_t seed, psygfx_noise_dist dist) {
    uint32_t h = psygfx_hash((uint32_t)cx ^ psygfx_hash((uint32_t)cy ^ psygfx_hash(seed & 0xFFFFFFu)));
    if (dist == PSYGFX_BINARY) return (h & 0x80000000u) ? 1.0f : -1.0f;
    return (float)((int32_t)(((h >> 9) << 1) + 1u) - 8388608) * (1.0f / 8388608.0f);
}

PSYGFX_API void psygfx_noise_fill(float* out, int w, int h, uint32_t seed, psygfx_noise_dist dist) {
    int x, y;
    if (!out) return;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) out[(size_t)y * (size_t)w + (size_t)x] = psygfx_noise_value(x, y, seed, dist);
}

PSYGFX_API void psygfx_noise_gauss(float* out, int w, int h, uint32_t seed) {
    size_t n = (size_t)w * (size_t)h, i;
    uint32_t s1, s2;
    if (!out || w <= 0 || h <= 0) return;
    s1 = psygfx_hash(seed);
    s2 = psygfx_hash(seed ^ 0x9E3779B9u);
    for (i = 0; i < n; i += 2) {
        uint32_t a = psygfx_hash((uint32_t)i ^ s1), b = psygfx_hash((uint32_t)i ^ s2);
        double u1 = ((double)(a >> 8) + 0.5) / 16777216.0, u2 = ((double)(b >> 8) + 0.5) / 16777216.0;
        double r = sqrt(-2.0 * log(u1)), t = 6.283185307179586 * u2;
        out[i] = (float)(r * cos(t));
        if (i + 1 < n) out[i + 1] = (float)(r * sin(t));
    }
}

/* --- signed distance from a mask ----------------------------------------------- */

/* One row of the squared Euclidean distance transform (Felzenszwalb and
 * Huttenlocher 2012): f in, d out, n samples; v and z are scratch. */
static void psygfx__edt1(const double* f, double* d, int n, int* v, double* z) {
    int k = 0, q;
    v[0] = 0; z[0] = -1e300; z[1] = 1e300;
    for (q = 1; q < n; q++) {
        double sq;
        for (;;) {
            int r = v[k];
            sq = ((f[q] + (double)q * q) - (f[r] + (double)r * r)) / (2.0 * q - 2.0 * r);
            if (sq <= z[k] && k > 0) { k--; continue; }
            if (sq <= z[k]) { /* k == 0 and z[0] is -inf: never */ }
            break;
        }
        k++;
        v[k] = q; z[k] = sq; z[k + 1] = 1e300;
    }
    k = 0;
    for (q = 0; q < n; q++) {
        while (z[k + 1] < q) k++;
        d[q] = ((double)q - v[k]) * ((double)q - v[k]) + f[v[k]];
    }
}

/* Squared distance from every texel to the nearest texel where in[] is set. */
static void psygfx__edt2(const unsigned char* in, double* out, int w, int h, double* f, double* d, int* v, double* z) {
    int x, y, n = w > h ? w : h;
    (void)n;
    for (x = 0; x < w; x++) {
        for (y = 0; y < h; y++) f[y] = in[(size_t)y * w + x] ? 0.0 : 1e20;
        psygfx__edt1(f, d, h, v, z);
        for (y = 0; y < h; y++) out[(size_t)y * w + x] = d[y];
    }
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) f[x] = out[(size_t)y * w + x];
        psygfx__edt1(f, d, w, v, z);
        for (x = 0; x < w; x++) out[(size_t)y * w + x] = d[x];
    }
}

PSYGFX_API int psygfx_sdf_from_mask(float* out, const uint8_t* mask, int w, int h, int pad) {
    int W = w + 2 * pad, H = h + 2 * pad, n, x, y;
    size_t N;
    unsigned char* in;
    double *din, *dout, *f, *d, *z;
    int* v;
    if (!out || !mask || w <= 0 || h <= 0 || pad < 0 || W > 16384 || H > 16384) return PSYGFX_ERR_ARG;
    N = (size_t)W * (size_t)H;
    n = W > H ? W : H;
    in = (unsigned char*)malloc(N);
    din = (double*)malloc(2 * N * sizeof(double));
    f = (double*)malloc(((size_t)n * 3 + 2) * sizeof(double));
    v = (int*)malloc((size_t)n * sizeof(int));
    if (!in || !din || !f || !v) { free(in); free(din); free(f); free(v); return PSYGFX_ERR_FULL; }
    dout = din + N;
    d = f + n;
    z = d + n;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            int mx = x - pad, my = y - pad;
            in[(size_t)y * W + x] = (unsigned char)(mx >= 0 && my >= 0 && mx < w && my < h && mask[(size_t)my * w + mx] >= 128);
        }
    psygfx__edt2(in, din, W, H, f, d, v, z);          /* to the nearest inside texel */
    for (x = 0; x < (int)N; x++) in[x] = (unsigned char)!in[x];
    psygfx__edt2(in, dout, W, H, f, d, v, z);         /* to the nearest outside texel */
    for (x = 0; x < (int)N; x++) {
        /* the boundary lies halfway between an inside and an outside texel
         * center; a texel with none of one kind gets the other side's 0.5 */
        double a = din[x] < 1e19 ? sqrt(din[x]) : 0.0, b = dout[x] < 1e19 ? sqrt(dout[x]) : 0.0;
        out[x] = in[x] ? (float)(a - 0.5) : (float)(0.5 - b);
    }
    free(in); free(din); free(f); free(v);
    return PSYGFX_OK;
}

/* --- calibration ------------------------------------------------------------ */

PSYGFX_API void psygfx_cone_fundamentals(double nm, double lms[3]) {
    int i0;
    double f;
    lms[0] = lms[1] = lms[2] = 0.0;
    if (!(nm >= 390.0 && nm <= 830.0)) return;
    i0 = (int)(nm - 390.0);
    if (i0 >= 440) i0 = 439;
    f = nm - 390.0 - i0;
    lms[0] = psygfx__ss2[i0][0] + (psygfx__ss2[i0 + 1][0] - psygfx__ss2[i0][0]) * f;
    lms[1] = psygfx__ss2[i0][1] + (psygfx__ss2[i0 + 1][1] - psygfx__ss2[i0][1]) * f;
    lms[2] = psygfx__ss2[i0][2] + (psygfx__ss2[i0 + 1][2] - psygfx__ss2[i0][2]) * f;
}

static const char psygfx__cal_magic[8] = { 'P', 'S', 'Y', 'C', 'A', 'L', '\0', '\1' };

/* The canonical layout is fixed: every offset, on every compiler. */
#define PSYGFX__AT(f, off) typedef char psygfx__cal_at_##f[offsetof(psygfx_cal, f) == (off) ? 1 : -1]
PSYGFX__AT(version, 8);
PSYGFX__AT(n_readings, 36);
PSYGFX__AT(date, 40);
PSYGFX__AT(display, 48);
PSYGFX__AT(screen, 240);
PSYGFX__AT(readings, 496);
PSYGFX__AT(n_wl, 5616);
PSYGFX__AT(lut_n, 5628);
PSYGFX__AT(spd, 5632);
PSYGFX__AT(rgb_to_xyz, 13168);
PSYGFX__AT(white_err, 13360);
PSYGFX__AT(lut, 13368);
PSYGFX__AT(crc, 62524);
typedef char psygfx__cal_size[sizeof(psygfx_cal) == 62528 ? 1 : -1];
typedef char psygfx__reading_size[sizeof(psygfx_cal_reading) == 20 ? 1 : -1];
#undef PSYGFX__AT

PSYGFX_API void psygfx_cal_init(psygfx_cal* c) {
    if (!c) return;
    memset(c, 0, sizeof *c);
    memcpy(c->magic, psygfx__cal_magic, 8);
    c->version = 1;
    c->bytes = (uint32_t)sizeof *c;
}

PSYGFX_API int psygfx_cal_add(psygfx_cal* c, int gun, float level, float Y, float x, float y) {
    psygfx_cal_reading* r;
    if (!c) return PSYGFX_ERR_ARG;
    if (c->n_readings < 0 || c->n_readings >= PSYGFX_CAL_MAX_READINGS) return PSYGFX_ERR_FULL;
    if (gun < PSYGFX_GUN_BLACK || gun > PSYGFX_GUN_WHITE || !(level >= 0.0f && level <= 1.0f) || !(Y >= 0.0f))
        return PSYGFX_ERR_ARG;
    r = &c->readings[c->n_readings++];
    r->gun = gun; r->level = level; r->Y = Y; r->x = x; r->y = y;
    if (x > 0.0f && y > 0.0f) c->flags |= PSYGFX_CAL_HAS_XY;
    return PSYGFX_OK;
}

PSYGFX_API int psygfx_cal_set_spectra(psygfx_cal* c, float wl_start, float wl_step, int n,
                                      const float* r, const float* g, const float* b, const float* black) {
    int i;
    if (!c || !r || !g || !b || n < 2 || n > PSYGFX_CAL_MAX_WL || !(wl_step > 0.0f)) return PSYGFX_ERR_ARG;
    memset(c->spd, 0, sizeof c->spd);
    for (i = 0; i < n; i++) {
        c->spd[0][i] = r[i]; c->spd[1][i] = g[i]; c->spd[2][i] = b[i];
        c->spd[3][i] = black ? black[i] : 0.0f;
    }
    c->n_wl = n; c->wl_start = wl_start; c->wl_step = wl_step;
    c->flags |= PSYGFX_CAL_HAS_SPECTRA;
    return PSYGFX_OK;
}

static int psygfx__inv3(const double* m, double* out) {
    double a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
    double A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    double det = a * A + b * B + c * C;
    if (!(fabs(det) > 1e-300)) return 0;
    out[0] = A / det; out[1] = -(b * i - c * h) / det; out[2] = (b * f - c * e) / det;
    out[3] = B / det; out[4] = (a * i - c * g) / det;  out[5] = -(a * f - c * d) / det;
    out[6] = C / det; out[7] = -(a * h - b * g) / det; out[8] = (a * e - b * d) / det;
    return 1;
}

static void psygfx__mul3(const double* m, const double* v, double* out) {
    double t0 = m[0] * v[0] + m[1] * v[1] + m[2] * v[2];
    double t1 = m[3] * v[0] + m[4] * v[1] + m[5] * v[2];
    double t2 = m[6] * v[0] + m[7] * v[1] + m[8] * v[2];
    out[0] = t0; out[1] = t1; out[2] = t2;
}

/* Monotone cubic Hermite (Fritsch and Carlson 1980) through (x[i], y[i]),
 * x and y strictly increasing. */
static double psygfx__hermite(const double* x, const double* y, const double* m, int n, double t) {
    int lo = 0, hi = n - 1;
    double h, s, s2, s3;
    if (t <= x[0]) return y[0];
    if (t >= x[n - 1]) return y[n - 1];
    while (hi - lo > 1) { int mid = (lo + hi) / 2; if (x[mid] <= t) lo = mid; else hi = mid; }
    h = x[hi] - x[lo];
    s = (t - x[lo]) / h; s2 = s * s; s3 = s2 * s;
    return (2 * s3 - 3 * s2 + 1) * y[lo] + (s3 - 2 * s2 + s) * h * m[lo] + (-2 * s3 + 3 * s2) * y[hi] + (s3 - s2) * h * m[hi];
}

/* Everything psygfx_cal_derive() computes. verify != 0: compare with what is
 * stored instead of writing, and fail when they differ beyond rounding. */
static int psygfx__cal_derive(psygfx_cal* c, int verify, char* err, size_t cap) {
    double lx[3][PSYGFX_CAL_MAX_READINGS + 1], ly[3][PSYGFX_CAL_MAX_READINGS + 1], lm[PSYGFX_CAL_MAX_READINGS + 1];
    int np[3] = { 0, 0, 0 }, i, k, gun, lut_n, nblack = 0, full[3] = { -1, -1, -1 }, white = -1;
    double Ybk = 0.0, Y1[3], xyz[9], lms[9], bxyz[3] = { 0, 0, 0 }, blms[3] = { 0, 0, 0 }, werr = 0.0;
    const psygfx_cal_reading* black = NULL;
    double maxd = 0.0;
    if (memcmp(c->magic, psygfx__cal_magic, 8) != 0 || c->version != 1 || c->bytes != (uint32_t)sizeof *c) {
        psygfx__set_error(err, cap, "not a canonical calibration (magic, version 1, %u bytes); the pack tool writes one",
                          (unsigned)sizeof *c);
        return PSYGFX_ERR_FORMAT;
    }
    if (c->n_readings < 1 || c->n_readings > PSYGFX_CAL_MAX_READINGS) {
        psygfx__set_error(err, cap, "%d readings; a calibration is rebuilt from its readings", c->n_readings);
        return PSYGFX_ERR_RANGE;
    }
    lut_n = c->lut_n ? c->lut_n : PSYGFX_CAL_MAX_LUT;
    if (lut_n < 2 || lut_n > PSYGFX_CAL_MAX_LUT) { psygfx__set_error(err, cap, "lut_n %d outside 2..4096", c->lut_n); return PSYGFX_ERR_RANGE; }
    for (i = 0; i < c->n_readings; i++) {
        const psygfx_cal_reading* r = &c->readings[i];
        if (r->gun == PSYGFX_GUN_BLACK) { black = r; nblack++; }
        else if (r->gun == PSYGFX_GUN_WHITE) white = i;
        else if (r->gun < 0 || r->gun > 2) { psygfx__set_error(err, cap, "reading %d: gun %d", i, r->gun); return PSYGFX_ERR_RANGE; }
    }
    if (nblack != 1) { psygfx__set_error(err, cap, "%d black readings; one is needed", nblack); return PSYGFX_ERR_RANGE; }
    Ybk = black->Y;
    for (gun = 0; gun < 3; gun++) {
        lx[gun][0] = 0.0; ly[gun][0] = Ybk; np[gun] = 1;
        for (i = 0; i < c->n_readings; i++) {
            const psygfx_cal_reading* r = &c->readings[i];
            int j;
            if (r->gun != gun) continue;
            /* insertion by level */
            for (j = np[gun]; j > 1 && lx[gun][j - 1] > r->level; j--) { lx[gun][j] = lx[gun][j - 1]; ly[gun][j] = ly[gun][j - 1]; }
            lx[gun][j] = r->level; ly[gun][j] = r->Y;
            np[gun]++;
            if (r->level == 1.0f) full[gun] = i;
        }
        if (full[gun] < 0) { psygfx__set_error(err, cap, "gun %d has no reading at level 1", gun); return PSYGFX_ERR_RANGE; }
        for (k = 1; k < np[gun]; k++) {
            if (!(lx[gun][k] > lx[gun][k - 1]) || !(ly[gun][k] > ly[gun][k - 1])) {
                psygfx__set_error(err, cap, "gun %d: luminance does not rise at level %.6g (%.6g cd/m2 after %.6g); "
                                  "non-monotone readings are refused", gun, lx[gun][k], ly[gun][k], ly[gun][k - 1]);
                return PSYGFX_ERR_RANGE;
            }
        }
        Y1[gun] = ly[gun][np[gun] - 1];
        for (k = 0; k < np[gun]; k++) ly[gun][k] = (ly[gun][k] - Ybk) / (Y1[gun] - Ybk);
    }
    if (white >= 0) {
        double sum = Ybk + (Y1[0] - Ybk) + (Y1[1] - Ybk) + (Y1[2] - Ybk);
        werr = (c->readings[white].Y - sum) / c->readings[white].Y * 100.0;
    }
    /* XYZ (1931) from the chromaticities of the full-level readings */
    memset(xyz, 0, sizeof xyz);
    {
        int have = 1;
        for (gun = 0; gun < 3; gun++) {
            const psygfx_cal_reading* r = &c->readings[full[gun]];
            if (!(r->x > 0.0f && r->y > 0.0f)) have = 0;
        }
        if (have && Ybk > 0.0) {
            if (black->x > 0.0f && black->y > 0.0f) {
                bxyz[0] = black->x / black->y * Ybk; bxyz[1] = Ybk; bxyz[2] = (1.0 - black->x - black->y) / black->y * Ybk;
            } else {
                have = 0;   /* a black with light needs its chromaticity */
            }
        }
        if (have) {
            for (gun = 0; gun < 3; gun++) {
                const psygfx_cal_reading* r = &c->readings[full[gun]];
                double Y = r->Y, x = r->x, y = r->y;
                xyz[0 * 3 + gun] = x / y * Y - bxyz[0];
                xyz[1 * 3 + gun] = Y - bxyz[1];
                xyz[2 * 3 + gun] = (1.0 - x - y) / y * Y - bxyz[2];
            }
        }
    }
    /* LMS from the spectra: the 1-nm cone table against each spectrum
     * interpolated linearly onto it, 0 outside the measured range. */
    memset(lms, 0, sizeof lms);
    if ((c->flags & PSYGFX_CAL_HAS_SPECTRA) && c->n_wl >= 2 && c->n_wl <= PSYGFX_CAL_MAX_WL && c->wl_step > 0) {
        double acc[4][3];
        int j, nm;
        memset(acc, 0, sizeof acc);
        for (nm = 390; nm <= 830; nm++) {
            double t = ((double)nm - c->wl_start) / c->wl_step, f;
            int i0 = (int)floor(t);
            if (t < 0.0 || t > (double)(c->n_wl - 1)) continue;
            if (i0 >= c->n_wl - 1) i0 = c->n_wl - 2;
            f = t - i0;
            for (j = 0; j < 4; j++) {
                double v = c->spd[j][i0] + (c->spd[j][i0 + 1] - c->spd[j][i0]) * f;
                acc[j][0] += psygfx__ss2[nm - 390][0] * v;
                acc[j][1] += psygfx__ss2[nm - 390][1] * v;
                acc[j][2] += psygfx__ss2[nm - 390][2] * v;
            }
        }
        for (k = 0; k < 3; k++) {
            blms[k] = acc[3][k];
            for (j = 0; j < 3; j++) lms[k * 3 + j] = acc[j][k] - acc[3][k];
        }
    }
    if (!verify) {
        memcpy(c->rgb_to_xyz, xyz, sizeof xyz);
        memcpy(c->rgb_to_lms, lms, sizeof lms);
        memcpy(c->black_xyz, bxyz, sizeof bxyz);
        memcpy(c->black_lms, blms, sizeof blms);
        c->white_err = werr;
        c->lut_n = lut_n;
    } else {
        for (k = 0; k < 9; k++) {
            double s1 = fabs(xyz[k] - c->rgb_to_xyz[k]) / (fabs(xyz[k]) + 1e-30);
            double s2 = fabs(lms[k] - c->rgb_to_lms[k]) / (fabs(lms[k]) + 1e-30);
            if ((xyz[k] != c->rgb_to_xyz[k] && s1 > 1e-9) || (lms[k] != c->rgb_to_lms[k] && s2 > 1e-9)) {
                psygfx__set_error(err, cap, "the stored matrices differ from the ones its readings give");
                return PSYGFX_ERR_FORMAT;
            }
        }
        if (c->lut_n != lut_n) { psygfx__set_error(err, cap, "lut_n differs"); return PSYGFX_ERR_FORMAT; }
    }
    /* The CLUT: entry k is the level whose normalized luminance is k/(n-1). */
    for (gun = 0; gun < 3; gun++) {
        int n = np[gun];
        const double* x = lx[gun];
        const double* y = ly[gun];
        double dk[PSYGFX_CAL_MAX_READINGS + 1];
        dk[0] = 1.0;   /* n >= 2 always: black and the full level */
        for (k = 0; k < n - 1; k++) dk[k] = (y[k + 1] - y[k]) / (x[k + 1] - x[k]);
        lm[0] = dk[0];
        lm[n - 1] = dk[n - 2];
        for (k = 1; k < n - 1; k++) lm[k] = 0.5 * (dk[k - 1] + dk[k]);
        for (k = 0; k < n - 1; k++) {
            double a = lm[k] / dk[k], b = lm[k + 1] / dk[k], s = a * a + b * b;
            if (s > 9.0) { double t = 3.0 / sqrt(s); lm[k] = t * a * dk[k]; lm[k + 1] = t * b * dk[k]; }
        }
        for (k = 0; k < lut_n; k++) {
            double target = (double)k / (double)(lut_n - 1), lo = 0.0, hi = 1.0, v;
            int it;
            if (k == 0) v = 0.0;
            else if (k == lut_n - 1) v = 1.0;
            else {
                for (it = 0; it < 64; it++) {
                    double mid = 0.5 * (lo + hi);
                    if (psygfx__hermite(x, y, lm, n, mid) < target) lo = mid; else hi = mid;
                }
                v = 0.5 * (lo + hi);
            }
            if (!verify) c->lut[gun][k] = (float)v;
            else if (fabs((double)c->lut[gun][k] - v) > maxd) maxd = fabs((double)c->lut[gun][k] - v);
        }
    }
    if (verify && maxd > 1e-6) {
        psygfx__set_error(err, cap, "the stored CLUT differs from the one its readings give by %.3g", maxd);
        return PSYGFX_ERR_FORMAT;
    }
    return PSYGFX_OK;
}

PSYGFX_API int psygfx_cal_derive(psygfx_cal* c, char* err, size_t cap) {
    if (!c) return PSYGFX_ERR_ARG;
    if (err && cap) err[0] = '\0';
    return psygfx__cal_derive(c, 0, err, cap);
}

PSYGFX_API int psygfx_cal_check(const psygfx_cal* c, char* err, size_t cap) {
    if (!c) return PSYGFX_ERR_ARG;
    if (err && cap) err[0] = '\0';
    if (c->crc && c->crc != psygfx__crc32(c, offsetof(psygfx_cal, crc))) {
        psygfx__set_error(err, cap, "CRC mismatch");
        return PSYGFX_ERR_FORMAT;
    }
    /* verify mode reads only; the cast does not write */
    return psygfx__cal_derive((psygfx_cal*)c, 1, err, cap);
}

PSYGFX_API int psygfx_cal_save(psygfx_cal* c, void* out, size_t cap) {
    if (!c) return PSYGFX_ERR_ARG;
    c->crc = psygfx__crc32(c, offsetof(psygfx_cal, crc));
    if (!out) return (int)sizeof *c;
    if (cap < sizeof *c) return PSYGFX_ERR_FULL;
    memcpy(out, c, sizeof *c);
    return (int)sizeof *c;
}

PSYGFX_API int psygfx_cal_load(psygfx_cal* c, const void* bytes, size_t n, char* err, size_t cap) {
    int rc;
    if (err && cap) err[0] = '\0';
    if (!c || !bytes) return PSYGFX_ERR_ARG;
    if (n != sizeof *c || memcmp(bytes, psygfx__cal_magic, 8) != 0) {
        psygfx__set_error(err, cap, "not a canonical calibration (%lu bytes, want %u with magic PSYCAL); "
                          "the pack tool writes one", (unsigned long)n, (unsigned)sizeof *c);
        return PSYGFX_ERR_FORMAT;
    }
    memcpy(c, bytes, sizeof *c);
    if (c->crc != psygfx__crc32(c, offsetof(psygfx_cal, crc))) {
        memset(c, 0, sizeof *c);
        psygfx__set_error(err, cap, "CRC mismatch: the file was changed or truncated");
        return PSYGFX_ERR_FORMAT;
    }
    rc = psygfx__cal_derive(c, 1, err, cap);
    if (rc < 0) memset(c, 0, sizeof *c);
    return rc;
}

PSYGFX_API int psygfx_cal_nominal(psygfx_cal* c, const float xy[4][2], float white_Y, double gamma) {
    double m[9], inv[9], w[3], Yp[3];
    int gun, k, rc;
    if (!c || !xy || !(white_Y > 0.0f) || !(gamma > 0.0)) return PSYGFX_ERR_ARG;
    for (k = 0; k < 4; k++) if (!(xy[k][1] > 0.0f)) return PSYGFX_ERR_ARG;
    psygfx_cal_init(c);
    /* luminance of each primary so that the three sum to the white point */
    for (gun = 0; gun < 3; gun++) {
        m[0 * 3 + gun] = xy[gun][0] / xy[gun][1];
        m[1 * 3 + gun] = 1.0;
        m[2 * 3 + gun] = (1.0 - xy[gun][0] - xy[gun][1]) / xy[gun][1];
    }
    w[0] = xy[3][0] / xy[3][1] * white_Y; w[1] = white_Y; w[2] = (1.0 - xy[3][0] - xy[3][1]) / xy[3][1] * white_Y;
    if (!psygfx__inv3(m, inv)) return PSYGFX_ERR_RANGE;
    psygfx__mul3(inv, w, Yp);
    psygfx_cal_add(c, PSYGFX_GUN_BLACK, 0.0f, 0.0f, 0.0f, 0.0f);
    for (gun = 0; gun < 3; gun++) {
        if (!(Yp[gun] > 0.0)) return PSYGFX_ERR_RANGE;
        for (k = 1; k <= 32; k++) {
            double l = k / 32.0;
            psygfx_cal_add(c, gun, (float)l, (float)(Yp[gun] * pow(l, gamma)), xy[gun][0], xy[gun][1]);
        }
    }
    psygfx_cal_add(c, PSYGFX_GUN_WHITE, 1.0f, white_Y, xy[3][0], xy[3][1]);
    c->flags |= PSYGFX_CAL_NOMINAL;
    snprintf(c->instrument, sizeof c->instrument, "nominal: stated primaries, gamma %.4g", gamma);
    rc = psygfx_cal_derive(c, NULL, 0);
    if (rc >= 0) psygfx_cal_save(c, NULL, 0);
    return rc;
}

static int psygfx__has_lms(const psygfx_cal* c) {
    return c && (c->flags & PSYGFX_CAL_HAS_SPECTRA) && (c->rgb_to_lms[0] != 0.0 || c->rgb_to_lms[4] != 0.0);
}

PSYGFX_API int psygfx_cal_lms(const psygfx_cal* c, const float rgb[3], double out[3]) {
    double v[3];
    if (!psygfx__has_lms(c) || !rgb || !out) return PSYGFX_ERR_REFUSED;
    v[0] = rgb[0]; v[1] = rgb[1]; v[2] = rgb[2];
    psygfx__mul3(c->rgb_to_lms, v, out);
    out[0] += c->black_lms[0]; out[1] += c->black_lms[1]; out[2] += c->black_lms[2];
    return PSYGFX_OK;
}

PSYGFX_API int psygfx_cal_dir_cone(const psygfx_cal* c, const float bg[3], const float cc[3], float dir[3]) {
    double lb[3], d[3], inv[9], r[3];
    int k;
    if (!bg || !cc || !dir) return PSYGFX_ERR_ARG;
    if (psygfx_cal_lms(c, bg, lb) < 0) return PSYGFX_ERR_REFUSED;
    if (!psygfx__inv3(c->rgb_to_lms, inv)) return PSYGFX_ERR_RANGE;
    for (k = 0; k < 3; k++) d[k] = (double)cc[k] * lb[k];
    psygfx__mul3(inv, d, r);
    for (k = 0; k < 3; k++) dir[k] = (float)r[k];
    return PSYGFX_OK;
}

/* Psychtoolbox's ComputeDKL_M (Brainard 1996, the appendix in Kaiser and
 * Boynton), with luminance V = 0.68990272 L + 0.34832189 M. */
PSYGFX_API int psygfx_cal_dkl_matrix(const psygfx_cal* c, const float bg[3], double m[9]) {
    static const double w0 = 0.68990272, w1 = 0.34832189;
    double b[3], raw[9], inv[9], col[3], pooled[3], resp[3];
    int k, j;
    if (!bg || !m) return PSYGFX_ERR_ARG;
    if (psygfx_cal_lms(c, bg, b) < 0) return PSYGFX_ERR_REFUSED;
    if (!(b[0] > 0 && b[1] > 0 && b[2] > 0)) return PSYGFX_ERR_RANGE;
    raw[0] = w0;  raw[1] = w1;            raw[2] = 0.0;
    raw[3] = 1.0; raw[4] = -b[0] / b[1];  raw[5] = 0.0;
    raw[6] = -w0; raw[7] = -w1;           raw[8] = (w0 * b[0] + w1 * b[1]) / b[2];
    if (!psygfx__inv3(raw, inv)) return PSYGFX_ERR_RANGE;
    /* each isolating stimulus, scaled to unit pooled cone contrast, must
     * give a unit response: rescale the rows */
    for (j = 0; j < 3; j++) {
        double s = 0.0;
        for (k = 0; k < 3; k++) { col[k] = inv[k * 3 + j]; s += (col[k] / b[k]) * (col[k] / b[k]); }
        pooled[j] = sqrt(s);
        for (k = 0; k < 3; k++) col[k] /= pooled[j];
        psygfx__mul3(raw, col, resp);
        for (k = 0; k < 3; k++) m[j * 3 + k] = raw[j * 3 + k] / resp[j];
    }
    return PSYGFX_OK;
}

PSYGFX_API int psygfx_cal_dir_dkl(const psygfx_cal* c, const float bg[3], const float dkl[3], float dir[3]) {
    double m[9], inv[9], v[3], cone[3], linv[9], r[3];
    int k, rc;
    if (!dkl || !dir) return PSYGFX_ERR_ARG;
    rc = psygfx_cal_dkl_matrix(c, bg, m);
    if (rc < 0) return rc;
    if (!psygfx__inv3(m, inv) || !psygfx__inv3(c->rgb_to_lms, linv)) return PSYGFX_ERR_RANGE;
    v[0] = dkl[0]; v[1] = dkl[1]; v[2] = dkl[2];
    psygfx__mul3(inv, v, cone);
    psygfx__mul3(linv, cone, r);
    for (k = 0; k < 3; k++) dir[k] = (float)r[k];
    return PSYGFX_OK;
}

PSYGFX_API void psygfx_dkl_from_sph(float elevation_deg, float azimuth_deg, float radius, float dkl[3]) {
    const double e = (double)elevation_deg * 3.14159265358979323846 / 180.0;
    const double a = (double)azimuth_deg * 3.14159265358979323846 / 180.0;
    if (!dkl) return;
    dkl[0] = (float)(radius * sin(e));
    dkl[1] = (float)(radius * cos(e) * cos(a));
    dkl[2] = (float)(radius * cos(e) * sin(a));
}

PSYGFX_API float psygfx_max_contrast(const float bg[3], const float dir[3]) {
    float best = 3.4e38f;
    int k;
    if (!bg || !dir) return 0.0f;
    for (k = 0; k < 3; k++) {
        float a = fabsf(dir[k]), room = bg[k] < 1.0f - bg[k] ? bg[k] : 1.0f - bg[k];
        if (a > 0.0f && room / a < best) best = room / a;
    }
    return best;
}

/* --- v0.3: the vector program's data and its CPU form ----------------------- */

/* The vector program's block, past the vertex shader's two vec4:
 *   2  opacity, gate, offset px, the band's center offset px
 *   3  color rgb, the band's half width px (0: a fill)
 *   4  dash px, gap px, dash offset px, the boundary's length L px
 *   5  kind, edge profile, edge width px, flags (PSYGFX__VF_*)
 *   6  trim start px, trim end px, cap, primitives n
 *   7  the second point's offset px (local), onion px, points 1 or 2
 *   8  fx offset, paint offset (vec4 into the data region), -, path half width
 *   9  the data region: 4 vec4 per primitive, then the path, fx and paint;
 *      it runs on into the extension blocks (16 vec4 each, at most 15)
 * A primitive: (center px, cos, sin) (kind, op, k px, onion px)
 * (C: 4 parameters px) (D: 2 parameters, geometry offset, count). */
#define PSYGFX__VDATA 247
#define PSYGFX__VF_DASH   1
#define PSYGFX__VF_TRIM   2
#define PSYGFX__VF_SMOOTH 4
#define PSYGFX__VF_S      8
#define PSYGFX__VF_PAINT  16
#define PSYGFX__VF_FX     32
#define PSYGFX__VF_RBLUR  64
#define PSYGFX__PI  3.14159265358979323846
#define PSYGFX__TAU 6.28318530717958647692

/* The quadrature of the shader's arc lengths: 16-point Gauss-Legendre. */
static const double psygfx__glx[8] = { 0.0950125098376374, 0.2816035507792589, 0.4580167776572274, 0.6178762444026438,
                                       0.7554044083550030, 0.8656312023878318, 0.9445750230732326, 0.9894009349916499 };
static const double psygfx__glw[8] = { 0.1894506104550685, 0.1826034150449236, 0.1691565193950025, 0.1495959888165767,
                                       0.1246289712555339, 0.0951585116824928, 0.0622535239386479, 0.0271524594117541 };

static double psygfx__sgn(double x) { return x >= 0.0 ? 1.0 : -1.0; }
static double psygfx__clampd(double x, double a, double b) { return x < a ? a : (x > b ? b : x); }
static double psygfx__ang(double x, double y) {
    double a;
    if (x == 0.0 && y == 0.0) return 0.0;
    a = atan2(y, x);
    return a < 0.0 ? a + PSYGFX__TAU : a;
}
static void psygfx__nrm(double vx, double vy, double fx, double fy, double* ox, double* oy) {
    double l = sqrt(vx * vx + vy * vy);
    if (l > 1e-300) { *ox = vx / l; *oy = vy / l; } else { *ox = fx; *oy = fy; }
}
static void psygfx__set4(double e[4], double a, double b, double c, double d) { e[0] = a; e[1] = b; e[2] = c; e[3] = d; }

/* Each primitive in its own frame: e = distance, gradient x, y, arc length.
 * The CPU forms of the shader's psy_p_*_: the sources are named there
 * (Inigo Quilez, MIT: box, rounded box, uneven capsule, arc, pie, star,
 * smooth minimum; 0xfaded, MIT: ellipse). */
static void psygfx__vp_box(double qx, double qy, double bx, double by, double e[4]) {
    double wx = fabs(qx) - bx, wy = fabs(qy) - by, sx = psygfx__sgn(qx), sy = psygfx__sgn(qy);
    if (wx > 0.0 && wy > 0.0) { double l = sqrt(wx * wx + wy * wy); psygfx__set4(e, l, sx * wx / l, sy * wy / l, 0.0); return; }
    if (wx >= wy) psygfx__set4(e, wx, sx, 0.0, 0.0); else psygfx__set4(e, wy, 0.0, sy, 0.0);
}

/* after Inigo Quilez, sdRoundBox (MIT) */
static void psygfx__vp_rrect(double qx, double qy, double bx, double by, const double r[4], int ws, double e[4]) {
    int rt = qx > 0.0, bt = qy > 0.0, cn;
    double rr = rt ? (bt ? r[2] : r[1]) : (bt ? r[3] : r[0]);
    double sx = rt ? 1.0 : -1.0, sy = bt ? 1.0 : -1.0, wx = fabs(qx) - bx + rr, wy = fabs(qy) - by + rr;
    cn = wx > 0.0 && wy > 0.0;
    if (cn) { double l = sqrt(wx * wx + wy * wy); psygfx__set4(e, l - rr, sx * wx / l, sy * wy / l, 0.0); }
    else if (wx >= wy) psygfx__set4(e, wx - rr, sx, 0.0, 0.0);
    else psygfx__set4(e, wy - rr, 0.0, sy, 0.0);
    if (ws) {
        double h = 0.5 * PSYGFX__PI, lt = 2.0 * bx - r[0] - r[1], lr = 2.0 * by - r[1] - r[2];
        double lb = 2.0 * bx - r[2] - r[3], ll = 2.0 * by - r[3] - r[0];
        double sR = lt + h * r[1], sB = sR + lr + h * r[2], sL = sB + lb + h * r[3];
        if (cn) {
            if (rt && !bt) e[3] = lt + rr * atan2(wx, wy);
            else if (rt) e[3] = sR + lr + rr * atan2(wy, wx);
            else if (bt) e[3] = sB + lb + rr * atan2(wx, wy);
            else e[3] = sL + ll + rr * atan2(wy, wx);
        } else if (wx >= wy) {
            e[3] = rt ? sR + psygfx__clampd(qy + by - r[1], 0.0, lr) : sL + psygfx__clampd(by - r[3] - qy, 0.0, ll);
        } else {
            e[3] = bt ? sB + psygfx__clampd(bx - r[2] - qx, 0.0, lb) : psygfx__clampd(qx + bx - r[0], 0.0, lt);
        }
    }
}

/* after Inigo Quilez, sdUnevenCapsule (MIT) */
static void psygfx__vp_capsule(double qx, double qy, double hx, double r0, double r1, int ws, double e[4]) {
    double x1 = r0 - hx, D = 2.0 * hx - r0 - r1, sn = (r0 - r1) / D, cs = sqrt(fmax(1.0 - sn * sn, 0.0));
    double al = qx - x1, k = -sn * fabs(qy) + cs * al, gx, gy;
    if (k < 0.0) { psygfx__nrm(al, qy, -1.0, 0.0, &gx, &gy); psygfx__set4(e, sqrt(al * al + qy * qy) - r0, gx, gy, 0.0); }
    else if (k > cs * D) { psygfx__nrm(al - D, qy, 1.0, 0.0, &gx, &gy); psygfx__set4(e, sqrt((al - D) * (al - D) + qy * qy) - r1, gx, gy, 0.0); }
    else psygfx__set4(e, cs * fabs(qy) + sn * al - r0, sn, psygfx__sgn(qy) * cs, 0.0);
    if (ws) e[3] = psygfx__clampd(al, 0.0, D);
}

/* The arc and the pie are folded about their middle direction u; their
 * trig comes from the CPU (u, and sin, cos of the half sweep), because a
 * D3D shader's sin and cos may be coarse (measured: 0.01 px off at radius
 * 75 on WARP). IQ's sdArc and sdPie in that frame. */
/* The clockwise angle from the start, measured from the middle direction
 * so that no angle wraps through 2 pi; past either end, that end. */
static double psygfx__sweep(double pv, double pu, double sw) {
    return psygfx__clampd(0.5 * sw + ((pv == 0.0 && pu == 0.0) ? 0.0 : atan2(pv, pu)), 0.0, sw);
}

/* after Inigo Quilez, sdArc (MIT) */
static void psygfx__vp_arc(double qx, double qy, double ux, double uy, double ra, double rb, double sw, double scx, double scy,
                           int ws, double e[4]) {
    double vx = -uy, vy = ux, pv = qx * vx + qy * vy, px = fabs(pv), py = qx * ux + qy * uy, d, gx, gy;
    if (scy * px > scx * py) { double wx = px - scx * ra, wy = py - scy * ra; d = sqrt(wx * wx + wy * wy) - rb; psygfx__nrm(wx, wy, scx, scy, &gx, &gy); }
    else { double l = sqrt(px * px + py * py), sg = psygfx__sgn(l - ra); d = fabs(l - ra) - rb; psygfx__nrm(px, py, 0.0, 1.0, &gx, &gy); gx *= sg; gy *= sg; }
    gx *= psygfx__sgn(pv);
    psygfx__set4(e, d, gx * vx + gy * ux, gx * vy + gy * uy,
                 ws ? ra * psygfx__sweep(pv, py, sw) : 0.0);
}

/* after Inigo Quilez, sdPie (MIT) */
static void psygfx__vp_pie(double qx, double qy, double R, double ux, double uy, double sw, double cx, double cy, int ws, double e[4]) {
    double vx = -uy, vy = ux, pv = qx * vx + qy * vy, px = fabs(pv), py = qx * ux + qy * uy;
    double l = sqrt(px * px + py * py) - R, pr = psygfx__clampd(px * cx + py * cy, 0.0, R);
    double mx = px - cx * pr, my = py - cy * pr, sg = psygfx__sgn(cy * px - cx * py), mm = sqrt(mx * mx + my * my) * sg, d, gx, gy;
    int arc = l > mm;
    if (arc) { d = l; psygfx__nrm(px, py, 0.0, 1.0, &gx, &gy); }
    else { d = mm; psygfx__nrm(mx, my, cy, -cx, &gx, &gy); gx *= sg; gy *= sg; }
    gx *= psygfx__sgn(pv);
    psygfx__set4(e, d, gx * vx + gy * ux, gx * vy + gy * uy, 0.0);
    if (ws) e[3] = arc ? R + R * psygfx__sweep(pv, py, sw) : (pv < 0.0 ? pr : 2.0 * R + R * sw - pr);
}

/* A star's constants in its folded half sector, in double: half a
 * sector's boundary length H and the inner fillet's arc length lI. */
static void psygfx__star_consts(double R, double n, double rho, double rf, double* H, double* lI) {
    double an = PSYGFX__PI / n, Tx = R * cos(an), Ty = R * sin(an), Ix = R * rho;
    double LE = sqrt((Tx - Ix) * (Tx - Ix) + Ty * Ty), ex = (Tx - Ix) / LE, ey = Ty / LE;
    double cb = ex * cos(an) + ey * sin(an), sbe = fabs(ex * sin(an) - ey * cos(an)), tI = 0.0;
    *lI = 0.0;
    if (rf <= 0.0) { *H = LE; return; }
    if (fabs(ex) > 1e-5) { tI = rf * fabs(ex) / fabs(ey); *lI = rf * atan2(fabs(ex), fabs(ey)); }
    *H = *lI + (LE - tI - rf * cb / sbe) + rf * atan2(cb, sbe);
}

/* A star or a regular polygon (rho = cos(pi / n)) with fillets of radius rf
 * at every corner, folded into half a sector: the inner corner I on the
 * folded x axis, the tip T at angle pi / n (cos, sin in D[0], D[1]). The
 * sector's rotation is a power of a unit complex number, not a sin and cos
 * per pixel; a point the rounded sector index puts past the tip line is
 * reflected back across it. The fold is after Inigo Quilez's sdStar (MIT). */
static void psygfx__vp_star(double qx, double qy, const float* C, const float* D, int ws, double e[4]) {
    double R = C[0], n = C[1], rho = C[2], rf = C[3], an = PSYGFX__PI / n, Thx = D[0], Thy = D[1];
    double al = (qx == 0.0 && qy == 0.0) ? 0.0 : atan2(qx, qy), kf = floor((al + an) / (2.0 * an));
    double zx = Thx * Thx - Thy * Thy, zy = 2.0 * Thx * Thy, wx2 = 1.0, wy2 = 0.0, fys, sb, fx, fy;
    double Ix, Tx, Ty, ex, ey, nex, ney, LE, PIx, PIy, PTx, PTy, gfx, gfy, best = 1e300, dv = 0.0, sh = 0.0, lI = D[3], H = D[2];
    double LP, t, wx, wy, l;
    int kk = (int)fabs(kf), b, rfl;
    for (b = 0; b < 7; b++) {
        if (kk & 1) { double tx = wx2 * zx - wy2 * zy; wy2 = wx2 * zy + wy2 * zx; wx2 = tx; }
        { double tz = zx * zx - zy * zy; zy = 2.0 * zx * zy; zx = tz; }
        kk /= 2;
    }
    if (kf < 0.0) wy2 = -wy2;
    fys = qx * wx2 - qy * wy2; sb = psygfx__sgn(fys);
    fx = qx * wy2 + qy * wx2; fy = fabs(fys);
    rfl = fx * Thy - fy * Thx < 0.0;
    if (rfl) { double k2 = 2.0 * (fx * Thx + fy * Thy); fx = k2 * Thx - fx; fy = k2 * Thy - fy; }
    Ix = R * rho; Tx = R * Thx; Ty = R * Thy;
    LE = sqrt((Tx - Ix) * (Tx - Ix) + Ty * Ty); ex = (Tx - Ix) / LE; ey = Ty / LE; nex = ey; ney = -ex;
    PIx = Ix; PIy = 0.0; PTx = Tx; PTy = Ty; gfx = nex; gfy = ney;
    if (rf > 0.0) {
        double cb = ex * Thx + ey * Thy, sbe = fabs(ex * Thy - ey * Thx), CTx, CTy, lw, cr;
        CTx = Tx - Thx * rf / sbe; CTy = Ty - Thy * rf / sbe;
        PTx = Tx - ex * rf * cb / sbe; PTy = Ty - ey * rf * cb / sbe;
        if (fabs(ex) > 1e-5) {
            double cv = ex > 0.0 ? -1.0 : 1.0, CIx = Ix - cv * rf / fabs(ey), CIy = 0.0, a1x = cv, a1y = 0.0, a2x, a2y;
            PIx = Ix + ex * rf * fabs(ex) / fabs(ey); PIy = ey * rf * fabs(ex) / fabs(ey);
            a2x = (PIx - CIx) / rf; a2y = (PIy - CIy) / rf;
            wx = fx - CIx; wy = fy - CIy; lw = sqrt(wx * wx + wy * wy);
            cr = a1x * a2y - a1y * a2x;
            if ((a1x * wy - a1y * wx) * cr >= -1e-5 * lw && (wx * a2y - wy * a2x) * cr >= -1e-5 * lw && lw > 0.0) {
                dv = cv * (lw - rf); best = fabs(dv); gfx = cv * wx / lw; gfy = cv * wy / lw;
                sh = rf * fabs(atan2(a1x * wy - a1y * wx, a1x * wx + a1y * wy));
            }
        }
        wx = fx - CTx; wy = fy - CTy; lw = sqrt(wx * wx + wy * wy);
        cr = nex * Thy - ney * Thx;
        if ((nex * wy - ney * wx) * cr >= -1e-5 * lw && (wx * Thy - wy * Thx) * cr >= -1e-5 * lw && lw > 0.0 && fabs(lw - rf) < best) {
            dv = lw - rf; best = fabs(dv); gfx = wx / lw; gfy = wy / lw;
            sh = lI + sqrt((PTx - PIx) * (PTx - PIx) + (PTy - PIy) * (PTy - PIy)) + rf * fabs(atan2(nex * wy - ney * wx, nex * wx + ney * wy));
        }
    }
    LP = sqrt((PTx - PIx) * (PTx - PIx) + (PTy - PIy) * (PTy - PIy));
    t = psygfx__clampd((fx - PIx) * ex + (fy - PIy) * ey, 0.0, LP);
    wx = fx - PIx - ex * t; wy = fy - PIy - ey * t; l = sqrt(wx * wx + wy * wy);
    if (l < best) {
        double sg = wx * nex + wy * ney >= 0.0 ? 1.0 : -1.0;
        dv = sg * l; sh = lI + t;
        if (l > 0.0) { gfx = sg * wx / l; gfy = sg * wy / l; } else { gfx = nex; gfy = ney; }
    }
    if (rfl) { double k2 = 2.0 * (gfx * Thx + gfy * Thy); gfx = k2 * Thx - gfx; gfy = k2 * Thy - gfy; sh = 2.0 * H - sh; }
    e[0] = dv;
    e[1] = gfx * wy2 + gfy * sb * wx2;
    e[2] = gfx * wx2 - gfy * sb * wy2;
    e[3] = 0.0;
    if (ws) {
        double P = 2.0 * H, s2 = fmod(fmod(n - kf, n) + n, n) * P + (sb < 0.0 ? sh : -sh);
        e[3] = s2 - n * P * floor(s2 / (n * P));
    }
}

/* 0xfaded's trig-free closest point on an ellipse (MIT, see the end of the
 * file), in the first quadrant; the arc length by the shader's quadrature. */
static void psygfx__vp_ellipse(double qx, double qy, double a, double b, int it, double Lq, int ws, double e[4]) {
    double px = fabs(qx), py = fabs(qy), tx = 0.70710678118654752, ty = 0.70710678118654752;
    double k2x = (a * a - b * b) / a, k2y = (b * b - a * a) / b, wx, wy, sg, nx, ny, gx, gy;
    int i;
    for (i = 0; i < it && i < 8; i++) {
        double evx = k2x * tx * tx * tx, evy = k2y * ty * ty * ty, rx = a * tx - evx, ry = b * ty - evy;
        double qqx = px - evx, qqy = py - evy, rl = sqrt(rx * rx + ry * ry), ql = fmax(sqrt(qqx * qqx + qqy * qqy), 1e-300), tl;
        tx = psygfx__clampd((qqx * rl / ql + evx) / a, 0.0, 1.0);
        ty = psygfx__clampd((qqy * rl / ql + evy) / b, 0.0, 1.0);
        tl = fmax(sqrt(tx * tx + ty * ty), 1e-300);
        tx /= tl; ty /= tl;
    }
    wx = px - a * tx; wy = py - b * ty;
    sg = (px / a) * (px / a) + (py / b) * (py / b) < 1.0 ? -1.0 : 1.0;
    psygfx__nrm(tx / a, ty / b, 1.0, 0.0, &nx, &ny);
    psygfx__nrm(wx, wy, sg * nx, sg * ny, &gx, &gy);
    psygfx__set4(e, sg * sqrt(wx * wx + wy * wy), sg * gx * psygfx__sgn(qx), sg * gy * psygfx__sgn(qy), 0.0);
    if (ws) {
        double t1 = atan2(ty, tx), h = 0.5 * t1, S = 0.0;
        int j;
        for (i = 0; i < 8; i++)
            for (j = 0; j < 2; j++) {
                double u = h * (1.0 + (j == 0 ? psygfx__glx[i] : -psygfx__glx[i]));
                S += psygfx__glw[i] * sqrt(a * a * sin(u) * sin(u) + b * b * cos(u) * cos(u));
            }
        S *= h;
        e[3] = qx >= 0.0 ? (qy >= 0.0 ? S : 4.0 * Lq - S) : (qy >= 0.0 ? 2.0 * Lq - S : 2.0 * Lq + S);
    }
}

/* The quarter of an ellipse's perimeter, in double. */
static double psygfx__ell_quarter(double a, double b) {
    int k, i, j;
    double s = 0.0;
    for (k = 0; k < 64; k++) {
        double t0 = k * (0.5 * PSYGFX__PI) / 64.0, h = 0.5 * (0.5 * PSYGFX__PI) / 64.0;
        for (i = 0; i < 8; i++)
            for (j = 0; j < 2; j++) {
                double u = t0 + h * (1.0 + (j == 0 ? psygfx__glx[i] : -psygfx__glx[i]));
                s += h * psygfx__glw[i] * sqrt(a * a * sin(u) * sin(u) + b * b * cos(u) * cos(u));
            }
    }
    return s;
}

/* The data region as floats: vec4 k at vd + 4 k. */
#define PSYGFX__VD(vd, k, c) ((double)(vd)[4 * (k) + (c)])

static void psygfx__vp_polygon(double qx, double qy, const float* vd, int g0, int n, int ws, double e[4]) {
    double best = 1e300, dv = 0.0, s = 0.0, par = 1.0, o = PSYGFX__VD(vd, g0 + 2, 1), gbx = 1.0, gby = 0.0;
    int sharp = fabs(o) > 1.5, i;
    o = psygfx__sgn(o);
    for (i = 0; i < n && i < 16; i++) {
        int j = i + 1 < n ? i + 1 : 0;
        const float* A = vd + 4 * (g0 + 3 * i);
        const float* B = A + 4;
        const float* C = A + 8;
        const float* An = vd + 4 * (g0 + 3 * j);
        double evx, evy, le, ex, ey, nex, ney, t, wx, wy, l;
        if (B[2] != 0.0f) {
            double r = fabs((double)B[2]), cv = psygfx__sgn(B[2]);
            double ww_x = qx - B[0], ww_y = qy - B[1], a1x = (A[0] - B[0]) / r, a1y = (A[1] - B[1]) / r;
            double a2x = (A[2] - B[0]) / r, a2y = (A[3] - B[1]) / r, cr = a1x * a2y - a1y * a2x, lw = sqrt(ww_x * ww_x + ww_y * ww_y);
            if ((a1x * ww_y - a1y * ww_x) * cr >= -1e-5 * lw && (ww_x * a2y - ww_y * a2x) * cr >= -1e-5 * lw && lw > 0.0 && fabs(lw - r) < best) {
                double sg2 = psygfx__sgn(lw - r);
                dv = cv * (lw - r); best = fabs(dv); gbx = sg2 * ww_x / lw; gby = sg2 * ww_y / lw;
                s = B[3] + r * fabs(atan2(a1x * ww_y - a1y * ww_x, a1x * ww_x + a1y * ww_y));
            }
        }
        evx = (double)An[0] - A[2]; evy = (double)An[1] - A[3];
        le = sqrt(evx * evx + evy * evy);
        ex = evx / fmax(le, 1e-300); ey = evy / fmax(le, 1e-300); nex = o * ey; ney = -o * ex;
        t = psygfx__clampd((qx - A[2]) * ex + (qy - A[3]) * ey, 0.0, le);
        wx = qx - A[2] - ex * t; wy = qy - A[3] - ey * t; l = sqrt(wx * wx + wy * wy);
        if (l < best) {
            double sg = wx * nex + wy * ney >= 0.0 ? 1.0 : -1.0;
            best = l; dv = sg * l; s = C[0] + t;
            if (l > 0.0) { gbx = wx / l; gby = wy / l; } else { gbx = nex; gby = ney; }
        }
        if (sharp) {
            double vix = C[2], viy = C[3], vjx = vd[4 * (g0 + 3 * j + 2) + 2], vjy = vd[4 * (g0 + 3 * j + 2) + 3];
            double eex = vjx - vix, eey = vjy - viy, wwx = qx - vix, wwy = qy - viy;
            int c1 = qy >= viy, c2 = qy < vjy, c3 = eex * wwy > eey * wwx;
            if ((c1 && c2 && c3) || (!c1 && !c2 && !c3)) par = -par;
        }
    }
    if (sharp) { dv = par * best; gbx *= par; gby *= par; }
    else { gbx *= psygfx__sgn(dv); gby *= psygfx__sgn(dv); }
    psygfx__set4(e, dv, gbx, gby, ws ? s : 0.0);
}

/* A convex polygon of n = 3 or 4 vertices, exact. */
static void psygfx__vp_cpoly(double px, double py, const double* v, int n, double e[4]) {
    double ar = 0.0, best = 1e300, bx = 0.0, by = 0.0, l, sg;
    int i, ins = 1;
    for (i = 0; i < n; i++) { int j = i + 1 < n ? i + 1 : 0; ar += v[2 * i] * v[2 * j + 1] - v[2 * i + 1] * v[2 * j]; }
    for (i = 0; i < n; i++) {
        int j = i + 1 < n ? i + 1 : 0;
        double ux = v[2 * i], uy = v[2 * i + 1], ex = v[2 * j] - ux, ey = v[2 * j + 1] - uy, qx = px - ux, qy = py - uy;
        double h = psygfx__clampd((qx * ex + qy * ey) / fmax(ex * ex + ey * ey, 1e-300), 0.0, 1.0), rx = qx - ex * h, ry = qy - ey * h;
        if (rx * rx + ry * ry < best) { best = rx * rx + ry * ry; bx = rx; by = ry; }
        if (ar * (ex * qy - ey * qx) < 0.0) ins = 0;
    }
    l = sqrt(best); sg = ins ? -1.0 : 1.0;
    psygfx__set4(e, sg * l, l > 0.0 ? sg * bx / l : 0.0, l > 0.0 ? sg * by / l : 0.0, 0.0);
}

/* Round joins: the distance to the center line, exact (a union of short
 * boxes is only a bound inside, and a chain of Bezier chords is short
 * boxes). Miter and bevel joins: boxes and join polygons, a bound inside
 * near each turn's inner corner. */
static void psygfx__vp_polyline(double px, double py, const float* vd, int g0, int n, double hw, int join, double lim,
                                int cap, int ws, double best[4]) {
    int i, bi = 0;
    double bh = 0.5;
    psygfx__set4(best, 1e300, 1.0, 0.0, 0.0);
    for (i = 0; i < n - 1 && i < PSYGFX_MAX_PATH - 1; i++) {
        const float* A = vd + 4 * (g0 + i);
        const float* B = A + 4;
        double tvx = (double)B[0] - A[0], tvy = (double)B[1] - A[1], len = sqrt(tvx * tvx + tvy * tvy);
        double tx = tvx / fmax(len, 1e-300), ty = tvy / fmax(len, 1e-300), nx = -ty, ny = tx, rx = px - A[0], ry = py - A[1];
        double al = rx * tx + ry * ty, e0, e1, bx[4];
        if (join == 0) {
            double h = psygfx__clampd(al, 0.0, len), wx = rx - tx * h, wy = ry - ty * h, l = sqrt(wx * wx + wy * wy), gx, gy;
            if (l - hw < best[0]) {
                psygfx__nrm(wx, wy, nx, ny, &gx, &gy);
                psygfx__set4(best, l - hw, gx, gy, A[2] + h);
                bh = al / fmax(len, 1e-300); bi = i;
            }
            continue;
        }
        e0 = (i == 0 && cap == 2) ? hw : 0.0; e1 = (i == n - 2 && cap == 2) ? hw : 0.0;
        psygfx__vp_box(al - 0.5 * (len + e1 - e0), rx * nx + ry * ny, 0.5 * (len + e0 + e1), hw, bx);
        if (bx[0] < best[0]) psygfx__set4(best, bx[0], bx[1] * tx + bx[2] * nx, bx[1] * ty + bx[2] * ny, A[2] + psygfx__clampd(al, 0.0, len));
        if (i < n - 2) {
            const float* Cn = A + 8;
            double t2x = (double)Cn[0] - B[0], t2y = (double)Cn[1] - B[1], tl = sqrt(t2x * t2x + t2y * t2y), cr;
            t2x /= tl; t2y /= tl;
            cr = tx * t2y - ty * t2x;
            if (fabs(cr) > 1e-6 || tx * t2x + ty * t2y < 0.0) {
                double sd = cr > 0.0 ? -1.0 : 1.0, nax = sd * nx, nay = sd * ny, nbx = -sd * t2y, nby = sd * t2x;
                double mx = nax + nbx, my = nay + nby, mm = mx * mx + my * my, v[8], jp[4];
                int mit = join == 1 && mm > 1e-12 && 2.0 / sqrt(mm) <= lim;
                v[0] = B[0]; v[1] = B[1]; v[2] = B[0] + nax * hw; v[3] = B[1] + nay * hw;
                v[6] = B[0] + nbx * hw; v[7] = B[1] + nby * hw;
                if (mit) { v[4] = B[0] + mx * (2.0 * hw / mm); v[5] = B[1] + my * (2.0 * hw / mm); }
                else { v[4] = v[6]; v[5] = v[7]; }
                psygfx__vp_cpoly(px, py, v, 4, jp);
                if (jp[0] < best[0]) psygfx__set4(best, jp[0], jp[1], jp[2], B[2]);
            }
        }
    }
    if (join == 0 && cap != 1 && ((bi == 0 && bh <= 0.0) || (bi == n - 2 && bh >= 1.0))) {
        int st = bi == 0 && bh <= 0.0;
        const float* E = vd + 4 * (g0 + (st ? 0 : n - 1));
        const float* F = vd + 4 * (g0 + (st ? 1 : n - 2));
        double tox, toy, nx, ny, rx = px - E[0], ry = py - E[1], ac, qdx, qdy, ggx, ggy;
        psygfx__nrm((double)E[0] - F[0], (double)E[1] - F[1], 1.0, 0.0, &tox, &toy);
        nx = -toy; ny = tox;
        ac = rx * nx + ry * ny;
        qdx = rx * tox + ry * toy - (cap == 2 ? hw : 0.0); qdy = fabs(ac) - hw;
        if (qdx > 0.0 && qdy > 0.0) { double q = sqrt(qdx * qdx + qdy * qdy); ggx = qdx / q; ggy = qdy / q; }
        else if (qdx >= qdy) { ggx = 1.0; ggy = 0.0; } else { ggx = 0.0; ggy = 1.0; }
        best[0] = sqrt(fmax(qdx, 0.0) * fmax(qdx, 0.0) + fmax(qdy, 0.0) * fmax(qdy, 0.0)) + fmin(fmax(qdx, qdy), 0.0);
        best[1] = ggx * tox + ggy * psygfx__sgn(ac) * nx;
        best[2] = ggx * toy + ggy * psygfx__sgn(ac) * ny;
    }
    if (join != 0 && cap == 1) {
        int k;
        for (k = 0; k < 2; k++) {
            const float* E = vd + 4 * (g0 + (k == 0 ? 0 : n - 1));
            double vx = px - E[0], vy = py - E[1], l = sqrt(vx * vx + vy * vy), gx, gy;
            if (l - hw < best[0]) { psygfx__nrm(vx, vy, 1.0, 0.0, &gx, &gy); psygfx__set4(best, l - hw, gx, gy, E[2]); }
        }
    }
    if (!ws) best[3] = 0.0;
}

/* The shader's psy_prim_, in double. */
static void psygfx__vprim(double qx, double qy, const float* B, const float* C, const float* D, const float* vd, int ws, double e[4]) {
    int k = (int)B[0];
    switch (k) {
    case 1: {
        double R = C[0], l = sqrt(qx * qx + qy * qy), gx, gy;
        psygfx__nrm(qx, qy, 1.0, 0.0, &gx, &gy);
        psygfx__set4(e, l - R, gx, gy, ws ? R * psygfx__ang(qx, qy) : 0.0);
        return;
    }
    case 2: {
        double R = C[0], ri = C[2], l = sqrt(qx * qx + qy * qy), sg = psygfx__sgn(l - 0.5 * (R + ri)), gx, gy;
        psygfx__nrm(qx, qy, 1.0, 0.0, &gx, &gy);
        psygfx__set4(e, fabs(l - 0.5 * (R + ri)) - 0.5 * (R - ri), sg * gx, sg * gy,
                     !ws ? 0.0 : (sg > 0.0 ? R * psygfx__ang(qx, qy) : PSYGFX__TAU * R + ri * psygfx__ang(qx, qy)));
        return;
    }
    case 8: {
        double r[4];
        r[0] = C[2]; r[1] = C[3]; r[2] = D[0]; r[3] = D[1];
        psygfx__vp_rrect(qx, qy, C[0], C[1], r, ws, e);
        return;
    }
    case 11: psygfx__vp_capsule(qx, qy, C[0], C[2], C[3], ws, e); return;
    case 9:  psygfx__vp_arc(qx, qy, C[0], C[1], C[2], C[3], D[0], D[2], D[3], ws, e); return;
    case 10: psygfx__vp_pie(qx, qy, C[0], C[1], C[2], D[0], D[2], D[3], ws, e); return;
    case 13: psygfx__vp_star(qx, qy, C, D, ws, e); return;
    case 14: psygfx__vp_ellipse(qx, qy, C[0], C[1], (int)C[2], C[3], ws, e); return;
    case 5: {
        double a[4], b[4];
        psygfx__vp_box(qx, qy, C[0], 0.5 * C[2], a);
        psygfx__vp_box(qx, qy, 0.5 * C[2], C[1], b);
        if (a[0] < b[0]) memcpy(e, a, sizeof a); else memcpy(e, b, sizeof b);
        return;
    }
    case 4:  psygfx__vp_polygon(qx, qy, vd, (int)D[2], (int)D[3], ws, e); return;
    case 16: psygfx__vp_polyline(qx, qy, vd, (int)D[2], (int)D[3], C[2], (int)C[3], D[0], (int)D[1], ws, e); return;
    default: psygfx__set4(e, 1e300, 1.0, 0.0, 0.0); return;
    }
}

/* Inigo Quilez's quadratic smooth minimum (MIT), with its gradient */
static void psygfx__vsmin(const double a[4], const double b[4], double k, double r[4]) {
    double h = psygfx__clampd(0.5 + 0.5 * (b[0] - a[0]) / k, 0.0, 1.0);
    r[0] = b[0] + (a[0] - b[0]) * h - k * h * (1.0 - h);
    r[1] = b[1] + (a[1] - b[1]) * h;
    r[2] = b[2] + (a[2] - b[2]) * h;
    r[3] = h > 0.5 ? a[3] : b[3];
}

static void psygfx__vop(double a[4], const double e[4], const float* B) {
    int op = (int)B[1], i;
    double k = B[2] > 1e-6f ? (double)B[2] : 1e-6, ne[4], na[4], r[4];
    ne[0] = -e[0]; ne[1] = -e[1]; ne[2] = -e[2]; ne[3] = e[3];
    na[0] = -a[0]; na[1] = -a[1]; na[2] = -a[2]; na[3] = a[3];
    switch (op) {
    case 0: if (e[0] < a[0]) memcpy(a, e, 4 * sizeof(double)); return;
    case 1: if (e[0] > a[0]) memcpy(a, e, 4 * sizeof(double)); return;
    case 2: if (ne[0] > a[0]) memcpy(a, ne, sizeof ne); return;
    case 3: {
        double u[4], v[4];
        memcpy(u, e[0] < a[0] ? e : a, sizeof u);
        memcpy(v, e[0] < a[0] ? a : e, sizeof v);
        if (u[0] > -v[0]) memcpy(a, u, sizeof u);
        else { a[0] = -v[0]; a[1] = -v[1]; a[2] = -v[2]; a[3] = v[3]; }
        return;
    }
    case 4: psygfx__vsmin(a, e, k, r); memcpy(a, r, sizeof r); return;
    default:
        psygfx__vsmin(na, op == 5 ? ne : e, k, r);
        for (i = 0; i < 3; i++) a[i] = -r[i];
        a[3] = r[3];
        return;
    }
}

/* The shader's field at local point p (and p minus the second point's
 * offset), folded, normalized, onioned: f[0] is what the edge sees before
 * the offset. b is the stimulus's block, its data region following. */
static void psygfx__vfield(const float* b, double px, double py, double f0[4], double f1[4]) {
    const float* vd = b + 36;
    int n = (int)b[27], np = (int)b[31], ws = ((int)b[23] & PSYGFX__VF_S) != 0, i, j;
    psygfx__set4(f0, 1e300, 1.0, 0.0, 0.0);
    psygfx__set4(f1, 1e300, 1.0, 0.0, 0.0);
    for (i = 0; i < n && i < PSYGFX_MAX_PRIMS; i++) {
        const float* A = vd + 16 * i;
        double ax = A[2], ay = A[3];
        for (j = 0; j < np && j < 2; j++) {
            double rx = (j == 0 ? px : px - b[28]) - A[0], ry = (j == 0 ? py : py - b[29]) - A[1], e[4], gx, gy;
            psygfx__vprim(rx * ax + ry * ay, -rx * ay + ry * ax, A + 4, A + 8, A + 12, vd, ws, e);
            gx = e[1] * ax - e[2] * ay;
            gy = e[1] * ay + e[2] * ax;
            e[1] = gx; e[2] = gy;
            if (A[7] > 0.0f) { double sg = psygfx__sgn(e[0]); e[0] = fabs(e[0]) - A[7]; e[1] *= sg; e[2] *= sg; }
            if (i == 0) memcpy(j == 0 ? f0 : f1, e, sizeof e);
            else psygfx__vop(j == 0 ? f0 : f1, e, A + 4);
        }
    }
    if (np < 2) memcpy(f1, f0, 4 * sizeof(double));
    if ((int)b[23] & PSYGFX__VF_SMOOTH) {
        f0[0] /= fmax(sqrt(f0[1] * f0[1] + f0[2] * f0[2]), 1e-3);
        f1[0] /= fmax(sqrt(f1[1] * f1[1] + f1[2] * f1[2]), 1e-3);
    }
    if (b[30] > 0.0f) { f0[0] = fabs(f0[0]) - b[30]; f1[0] = fabs(f1[0]) - b[30]; }
}

/* The main layer's hard coverage (where the GPU's is 0.5 or more), on the
 * CPU: the fill or the band, times the dashes and the trim. */
static int psygfx__vhard(const float* b, double px, double py) {
    double f0[4], f1[4], d, hw = b[15], c = b[11], nn, hh, s;
    int fl;
    psygfx__vfield(b, px, py, f0, f1);
    d = f0[0] - b[10];
    fl = (int)b[23];
    if (hw > 0.0 ? !(d - c - hw <= 0.0 && d - c + hw > 0.0) : !(d <= 0.0)) return 0;
    if (!(fl & (PSYGFX__VF_DASH | PSYGFX__VF_TRIM))) return 1;
    s = f0[3];
    nn = hw > 0.0 ? d - c : d + b[35];
    hh = hw > 0.0 ? hw : b[35];
    {
        double P = (double)b[16] + b[17], u = (fl & PSYGFX__VF_DASH) ? s - b[18] - P * floor((s - b[18]) / P) : 0.0;
        int cap = (int)b[26];
        double ex = cap == 2 ? hh : 0.0;
        if (cap != 1) {
            if (fl & PSYGFX__VF_DASH) {
                int k, in = 0;
                for (k = -1; k <= 1; k++) in |= u + k * P >= -ex && u + k * P <= b[16] + ex;
                if (!in) return 0;
            }
            if ((fl & PSYGFX__VF_TRIM) && !(s >= b[24] - ex && s <= b[25] + ex)) return 0;
            return 1;
        } else {
            double ga = 0.0;
            if (fl & PSYGFX__VF_DASH) {
                int k;
                ga = 1e300;
                for (k = -1; k <= 1; k++) ga = fmin(ga, fmax(fabs(u + k * P - 0.5 * b[16]) - 0.5 * b[16], 0.0));
            }
            if (fl & PSYGFX__VF_TRIM) ga = fmax(ga, fmax(fabs(s - 0.5 * ((double)b[24] + b[25])) - 0.5 * ((double)b[25] - b[24]), 0.0));
            return sqrt(ga * ga + nn * nn) - hh <= 0.0;
        }
    }
}

/* A primitive's kind and parameters into the shader's, px. sp: its four
 * shape parameters (units, degrees, counts). *L: the length dashes run
 * along (px; -1: none), *rad: a bounding radius about its center, *phw:
 * the half width of a path-like kind (dashes then follow its center line). */
static int psygfx__vkind(int shape, double w, double h, const float* sp, double ppu, float C[4], float D[4],
                         double* L, double* rad, double* phw, const char** err) {
    const double hx = 0.5 * w * ppu, hy = 0.5 * h * ppu;
    int i;
    for (i = 0; i < 4; i++) C[i] = D[i] = 0.0f;
    *L = -1.0; *phw = 0.0; *rad = sqrt(hx * hx + hy * hy);
    switch (shape) {
    case PSYGFX_RECT: case PSYGFX_RRECT: {
        double r[4], m = hx < hy ? hx : hy, sum = 0.0;
        if (!(hx > 0.0 && hy > 0.0)) { *err = "a rect needs w, h"; return PSYGFX_ERR_ARG; }
        for (i = 0; i < 4; i++) {
            r[i] = shape == PSYGFX_RECT ? (double)sp[1] * ppu : (double)sp[i] * ppu;
            r[i] = r[i] < 0.0 ? 0.0 : (r[i] > m ? m : r[i]);
            sum += r[i];
        }
        C[0] = (float)hx; C[1] = (float)hy; C[2] = (float)r[0]; C[3] = (float)r[1]; D[0] = (float)r[2]; D[1] = (float)r[3];
        *L = 4.0 * hx + 4.0 * hy - 2.0 * sum + 0.5 * PSYGFX__PI * sum;
        return 8;
    }
    case PSYGFX_CIRCLE:
        if (!(hx > 0.0)) { *err = "a circle needs w"; return PSYGFX_ERR_ARG; }
        C[0] = (float)hx; *L = PSYGFX__TAU * hx; *rad = hx;
        return 1;
    case PSYGFX_ANNULUS: {
        double ri = (double)sp[0] * ppu;
        if (!(ri >= 0.0 && ri < hx)) { *err = "an annulus needs 0 <= inner radius < w / 2"; return PSYGFX_ERR_ARG; }
        C[0] = (float)hx; C[2] = (float)ri; *L = PSYGFX__TAU * (hx + ri); *rad = hx;
        return 2;
    }
    case PSYGFX_LINE: case PSYGFX_CAPSULE: {
        double r0 = shape == PSYGFX_LINE ? 0.5 * sp[0] * ppu : (double)sp[0] * ppu;
        double r1 = shape == PSYGFX_LINE ? r0 : (double)sp[1] * ppu, Dl = 2.0 * hx - r0 - r1;
        if (!(r0 > 0.0 && r1 > 0.0 && Dl > fabs(r0 - r1))) {
            *err = "a line or capsule needs radii > 0 and a length past both ends"; return PSYGFX_ERR_ARG;
        }
        C[0] = (float)hx; C[2] = (float)r0; C[3] = (float)r1;
        *L = Dl; *phw = 0.5 * (r0 + r1); *rad = hx;
        return 11;
    }
    case PSYGFX_CROSS:
        if (!(sp[0] > 0.0f)) { *err = "a cross needs an arm width"; return PSYGFX_ERR_ARG; }
        C[0] = (float)hx; C[1] = (float)hy; C[2] = (float)(sp[0] * ppu);
        return 5;
    case PSYGFX_ARC: case PSYGFX_PIE: {
        double sw = (double)sp[0] * PSYGFX__PI / 180.0, a0 = (double)sp[2] * PSYGFX__PI / 180.0, rb = 0.5 * sp[1] * ppu;
        if (!(hx > 0.0 && sw > 0.0)) { *err = "an arc or pie needs w and a sweep"; return PSYGFX_ERR_ARG; }
        if (sw > PSYGFX__TAU) sw = PSYGFX__TAU;
        /* u: the middle direction; sin, cos of the half sweep */
        D[0] = (float)sw; D[1] = (float)a0; D[2] = (float)sin(0.5 * sw); D[3] = (float)cos(0.5 * sw); *rad = hx;
        if (shape == PSYGFX_PIE) {
            C[0] = (float)hx; C[1] = (float)cos(a0 + 0.5 * sw); C[2] = (float)sin(a0 + 0.5 * sw);
            *L = 2.0 * hx + hx * sw;
            return 10;
        }
        if (!(rb > 0.0 && rb < hx)) { *err = "an arc needs 0 < thickness < w"; return PSYGFX_ERR_ARG; }
        C[0] = (float)cos(a0 + 0.5 * sw); C[1] = (float)sin(a0 + 0.5 * sw); C[2] = (float)(hx - rb); C[3] = (float)rb;
        *L = (hx - rb) * sw; *phw = rb;
        return 9;
    }
    case PSYGFX_NGON: case PSYGFX_STAR: {
        double n = floor((double)sp[0] + 0.5), rho, rf, an, Tx, Ty, Ix, ex, ey, LE, be, tb, e4[4], H;
        if (!(n >= 3.0 && n <= 64.0 && hx > 0.0)) { *err = "an ngon or star needs w and 3 to 64 sides"; return PSYGFX_ERR_ARG; }
        an = PSYGFX__PI / n;
        rho = shape == PSYGFX_NGON ? cos(an) : (double)sp[1];
        rf = (shape == PSYGFX_NGON ? (double)sp[1] : (double)sp[2]) * ppu;
        if (!(rho > 0.0 && rho <= 1.0) || rf < 0.0) { *err = "a star needs 0 < inner ratio <= 1 and rounding >= 0"; return PSYGFX_ERR_ARG; }
        Tx = hx * cos(an); Ty = hx * sin(an); Ix = hx * rho;
        LE = sqrt((Tx - Ix) * (Tx - Ix) + Ty * Ty); ex = (Tx - Ix) / LE; ey = Ty / LE;
        be = acos(psygfx__clampd(ex * cos(an) + ey * sin(an), -1.0, 1.0));
        tb = 1.0 / tan(be) + (fabs(ex) > 1e-5 ? 1.0 / tan(acos(fabs(ex))) : 0.0);
        if (rf * tb > LE * (1.0 + 1e-9)) { *err = "the rounding is larger than the edges allow"; return PSYGFX_ERR_ARG; }
        {
            double lI;
            psygfx__star_consts(hx, n, rho, rf, &H, &lI);
            C[0] = (float)hx; C[1] = (float)n; C[2] = (float)rho; C[3] = (float)rf;
            D[0] = (float)cos(an); D[1] = (float)sin(an); D[2] = (float)H; D[3] = (float)lI;
        }
        *L = 2.0 * n * H; *rad = hx;
        (void)e4;
        return 13;
    }
    case PSYGFX_ELLIPSE: {
        double asp = hx > hy ? hx / hy : hy / hx;
        if (!(hx > 0.0 && hy > 0.0) || asp > 20.0) { *err = "an ellipse needs w, h with an aspect ratio up to 20"; return PSYGFX_ERR_ARG; }
        C[0] = (float)hx; C[1] = (float)hy;
        /* iterations from the aspect: below 1e-4 px measured up to 10:1,
         * 6.6e-4 px at 20:1 (docs/psy_gfx.md) */
        C[2] = asp <= 2.0 ? 3.0f : (asp <= 4.5 ? 6.0f : 8.0f);
        C[3] = (float)psygfx__ell_quarter(hx, hy);
        *L = 4.0 * (double)C[3]; *rad = hx > hy ? hx : hy;
        return 14;
    }
    default:
        *err = "not a primitive of the vector program";
        return PSYGFX_ERR_ARG;
    }
}

/* Writes vec4 v of the data region, if it fits. */
static int psygfx__vput(float* vd, int k, double a, double b, double c, double d) {
    if (k < 0 || k >= PSYGFX__VDATA) return 0;
    vd[4 * k] = (float)a; vd[4 * k + 1] = (float)b; vd[4 * k + 2] = (float)c; vd[4 * k + 3] = (float)d;
    return 1;
}

static double psygfx__edge_reach(const psygfx_stim* s) {
    if (s->edge == PSYGFX_EDGE_COSINE && s->edge_width > 0.0f) return 0.5 * s->edge_width;
    if (s->edge == PSYGFX_EDGE_GAUSSIAN && s->edge_width > 0.0f) return 5.0 * s->edge_width;
    return 0.0;
}

static int psygfx__is_vector(const psygfx_stim* s) {
    int sh = s->shape;
    if (s->kind != PSYGFX_SHAPE || sh == PSYGFX_MASK_TEX) return 0;
    if (sh >= PSYGFX_RRECT) return 1;
    if (sh == PSYGFX_POLYGON && s->shape_p[1] > 0.0f) return 1;
    if (s->fx || (s->paint && s->paint->kind != PSYGFX_PAINT_SOLID) || s->dash[0] > 0.0f) return 1;
    return !(s->trim[0] == 0.0f && s->trim[1] == 0.0f) && !(s->trim[0] <= 0.0f && s->trim[1] >= 1.0f);
}

/* A POLYGON's boundary for the vector program: per vertex, the fillet's
 * tangent points, center, signed radius and arc lengths (3 vec4). */
static int psygfx__vpolygon(const psygfx_stim* s, double ppu, float* vd, int g0, double* L, double* ext, const char** err) {
    int n = (int)s->shape_p[0], i;
    double rf = (double)s->shape_p[1] * ppu, area = 0.0, v[32], td[16], sacc = 0.0, o;
    if (n < 3 || n > 16) { *err = "a polygon needs 3 to 16 vertices"; return PSYGFX_ERR_ARG; }
    for (i = 0; i < 2 * n; i++) v[i] = (double)s->p[i] * ppu;
    for (i = 0; i < n; i++) { int j = (i + 1) % n; area += v[2 * i] * v[2 * j + 1] - v[2 * j] * v[2 * i + 1]; }
    if (area == 0.0) { *err = "a polygon with no area"; return PSYGFX_ERR_ARG; }
    o = area > 0.0 ? 1.0 : -1.0;
    ext[0] = ext[1] = 0.0;
    for (i = 0; i < n; i++) {
        int ip = (i + n - 1) % n, in = (i + 1) % n;
        double u1x = v[2 * i] - v[2 * ip], u1y = v[2 * i + 1] - v[2 * ip + 1], u2x = v[2 * in] - v[2 * i], u2y = v[2 * in + 1] - v[2 * i + 1];
        double l1 = sqrt(u1x * u1x + u1y * u1y), l2 = sqrt(u2x * u2x + u2y * u2y), dl;
        if (l1 == 0.0 || l2 == 0.0) { *err = "a polygon with a repeated vertex"; return PSYGFX_ERR_ARG; }
        dl = acos(psygfx__clampd(-(u1x * u2x + u1y * u2y) / (l1 * l2), -1.0, 1.0));
        td[i] = (rf > 0.0 && dl < PSYGFX__PI - 1e-9) ? rf / tan(0.5 * dl) : 0.0;
        if (fabs(v[2 * i]) > ext[0]) ext[0] = fabs(v[2 * i]);
        if (fabs(v[2 * i + 1]) > ext[1]) ext[1] = fabs(v[2 * i + 1]);
    }
    for (i = 0; i < n; i++) {
        int in = (i + 1) % n;
        double ex = v[2 * in] - v[2 * i], ey = v[2 * in + 1] - v[2 * i + 1];
        if (td[i] + td[in] > sqrt(ex * ex + ey * ey) * (1.0 + 1e-9)) { *err = "the fillets are larger than the edges allow"; return PSYGFX_ERR_ARG; }
    }
    for (i = 0; i < n; i++) {
        int ip = (i + n - 1) % n, in = (i + 1) % n;
        double u1x = v[2 * i] - v[2 * ip], u1y = v[2 * i + 1] - v[2 * ip + 1], u2x = v[2 * in] - v[2 * i], u2y = v[2 * in + 1] - v[2 * i + 1];
        double l1 = sqrt(u1x * u1x + u1y * u1y), l2 = sqrt(u2x * u2x + u2y * u2y);
        double pinx, piny, poutx, pouty, cx = 0.0, cy = 0.0, rs = 0.0, arc = 0.0, sin_ = sacc;
        u1x /= l1; u1y /= l1; u2x /= l2; u2y /= l2;
        pinx = v[2 * i] - u1x * td[i]; piny = v[2 * i + 1] - u1y * td[i];
        poutx = v[2 * i] + u2x * td[i]; pouty = v[2 * i + 1] + u2y * td[i];
        if (td[i] > 0.0) {
            double dl = acos(psygfx__clampd(-(u1x * u2x + u1y * u2y), -1.0, 1.0)), bx = -u1x + u2x, by = -u1y + u2y, bl = sqrt(bx * bx + by * by);
            cx = v[2 * i] + bx / bl * rf / sin(0.5 * dl); cy = v[2 * i + 1] + by / bl * rf / sin(0.5 * dl);
            rs = (u1x * u2y - u1y * u2x) * o > 0.0 ? rf : -rf;
            arc = rf * (PSYGFX__PI - dl);
        }
        sacc += arc;
        if (!psygfx__vput(vd, g0 + 3 * i, pinx, piny, poutx, pouty) ||
            !psygfx__vput(vd, g0 + 3 * i + 1, cx, cy, rs, sin_) ||
            !psygfx__vput(vd, g0 + 3 * i + 2, sacc, rf > 0.0 ? o : 2.0 * o, v[2 * i], v[2 * i + 1])) return PSYGFX_ERR_FULL;
        {   /* the edge to the next vertex's fillet */
            double nx = v[2 * in] - u2x * td[in], ny = v[2 * in + 1] - u2y * td[in];
            sacc += sqrt((nx - poutx) * (nx - poutx) + (ny - pouty) * (ny - pouty));
        }
    }
    *L = sacc;
    return 0;
}

/* The fx block (10 vec4) of a stimulus. */
static int psygfx__vfx(const psygfx_fx* fx, float* vd, int o) {
    const psygfx_shadow* sh[3];
    int k, ok = 1;
    sh[0] = &fx->drop; sh[1] = &fx->glow; sh[2] = &fx->inner;
    for (k = 0; k < 3; k++) {
        ok &= psygfx__vput(vd, o + 2 * k, sh[k]->sigma, sh[k]->spread, sh[k]->opacity > 0.0f ? sh[k]->opacity : 0.0f, 0.0);
        ok &= psygfx__vput(vd, o + 2 * k + 1, sh[k]->color[0], sh[k]->color[1], sh[k]->color[2], 0.0);
    }
    for (k = 0; k < 2; k++) {
        const psygfx_band* b = &fx->band[k];
        ok &= psygfx__vput(vd, o + 6 + 2 * k, b->a1, b->a2, b->opacity > 0.0f && b->a2 > b->a1 ? b->opacity : 0.0f, 0.0);
        ok &= psygfx__vput(vd, o + 7 + 2 * k, b->color[0], b->color[1], b->color[2], 0.0);
    }
    return ok ? 0 : PSYGFX_ERR_FULL;
}

/* A color in linear device RGB into the paint's space. */
static void psygfx__to_space(const psygfx_gfx* g, int space, const float rgb[3], double out[3]) {
    int k;
    if (space == PSYGFX_SPACE_OKLAB) {
        static const double m2[9] = { 0.2104542553, 0.7936177850, -0.0040720468, 1.9779984951, -2.4285922050, 0.4505937099,
                                      0.0259040371, 0.7827717662, -0.8086757660 };
        double v[3], l[3];
        for (k = 0; k < 3; k++) v[k] = rgb[k];
        psygfx__mul3(g->ok_rgb2lms, v, l);
        for (k = 0; k < 3; k++) l[k] = cbrt(l[k]);
        psygfx__mul3(m2, l, out);
    } else if (space == PSYGFX_SPACE_DKL_POLAR) {
        double v[3], d[3], r;
        for (k = 0; k < 3; k++) v[k] = (double)rgb[k] - g->bg[k];
        psygfx__mul3(g->rgb2dkl, v, d);
        r = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        out[0] = r > 0.0 ? asin(psygfx__clampd(d[0] / r, -1.0, 1.0)) : 0.0;
        out[1] = atan2(d[2], d[1]);
        out[2] = r;
    } else {
        for (k = 0; k < 3; k++) out[k] = rgb[k];
    }
}

/* The paint's space back to device RGB, on the CPU (the shader's last step). */
static void psygfx__from_space(const psygfx_gfx* g, int space, const double c[3], double rgb[3]) {
    int k;
    if (space == PSYGFX_SPACE_OKLAB) {
        double l[3];
        l[0] = c[0] + 0.3963377774 * c[1] + 0.2158037573 * c[2];
        l[1] = c[0] - 0.1055613458 * c[1] - 0.0638541728 * c[2];
        l[2] = c[0] - 0.0894841775 * c[1] - 1.2914855480 * c[2];
        for (k = 0; k < 3; k++) l[k] = l[k] * l[k] * l[k];
        psygfx__mul3(g->ok_lms2rgb, l, rgb);
    } else if (space == PSYGFX_SPACE_DKL_POLAR) {
        double d[3];
        d[0] = c[2] * sin(c[0]); d[1] = c[2] * cos(c[0]) * cos(c[1]); d[2] = c[2] * cos(c[0]) * sin(c[1]);
        psygfx__mul3(g->dkl2rgb, d, rgb);
        for (k = 0; k < 3; k++) rgb[k] += g->bg[k];
    } else {
        for (k = 0; k < 3; k++) rgb[k] = c[k];
    }
}

/* The paint block: header, geometry, colors from vec4 2 (stops or the
 * vertices'), and from vec4 18 the space's matrix rows. */
static int psygfx__vpaint(const psygfx_gfx* g, const psygfx_stim* s, const psygfx_paint* pt, double ppu, float* vd, int o,
                          int poly_g0, int* used, int* clipped, const char** err) {
    int n = pt->n, k, j, clip = 0;
    double c[3], prev_az = 0.0;
    if (pt->kind < PSYGFX_PAINT_LINEAR || pt->kind > PSYGFX_PAINT_VERTEX || pt->space > PSYGFX_SPACE_DKL_POLAR) {
        *err = "paint kind or space"; return PSYGFX_ERR_ARG;
    }
    if (pt->space == PSYGFX_SPACE_OKLAB && !g->has_oklab) { *err = "OKLAB paint needs a calibration with chromaticities"; return PSYGFX_ERR_REFUSED; }
    if (pt->space == PSYGFX_SPACE_DKL_POLAR && !g->has_dkl) { *err = "DKL_POLAR paint needs a calibration with spectra"; return PSYGFX_ERR_REFUSED; }
    if (pt->kind == PSYGFX_PAINT_VERTEX) {
        n = (int)s->shape_p[0];
        if (s->shape != PSYGFX_POLYGON || !pt->vertex_colors || psygfx__convex(s->p, n, 1.0) == 0) {
            *err = "VERTEX paint needs a convex POLYGON and vertex_colors"; return PSYGFX_ERR_ARG;
        }
    } else {
        if (n < 2 || n > 6) { *err = "a paint has 2 to 6 stops"; return PSYGFX_ERR_ARG; }
        for (k = 1; k < n; k++) if (pt->stops[k].t < pt->stops[k - 1].t) { *err = "paint stops must rise"; return PSYGFX_ERR_ARG; }
    }
    if (!psygfx__vput(vd, o, pt->kind, pt->space, pt->repeat ? 1 : 0, n)) return PSYGFX_ERR_FULL;
    if (pt->kind == PSYGFX_PAINT_VERTEX) {
        double r = 0.0;
        for (k = 0; k < n; k++) r = fmax(r, sqrt((double)s->p[2 * k] * s->p[2 * k] + (double)s->p[2 * k + 1] * s->p[2 * k + 1]) * ppu);
        if (!psygfx__vput(vd, o + 1, poly_g0, r > 0.0 ? 1.0 / r : 1.0, 0, 0)) return PSYGFX_ERR_FULL;
    } else if (pt->kind == PSYGFX_PAINT_RADIAL) {
        if (!(pt->x1 > 0.0f)) { *err = "a radial paint needs a radius"; return PSYGFX_ERR_ARG; }
        if (!psygfx__vput(vd, o + 1, pt->x0 * ppu, pt->y0 * ppu, pt->x1 * ppu, 0)) return PSYGFX_ERR_FULL;
    } else if (pt->kind == PSYGFX_PAINT_ANGULAR) {
        if (!psygfx__vput(vd, o + 1, pt->x0 * ppu, pt->y0 * ppu, (double)pt->x1 * PSYGFX__PI / 180.0, 0)) return PSYGFX_ERR_FULL;
    } else {
        if (pt->x0 == pt->x1 && pt->y0 == pt->y1) { *err = "a linear paint needs two points"; return PSYGFX_ERR_ARG; }
        if (!psygfx__vput(vd, o + 1, pt->x0 * ppu, pt->y0 * ppu, pt->x1 * ppu, pt->y1 * ppu)) return PSYGFX_ERR_FULL;
    }
    for (k = 0; k < n; k++) {
        const float* rgb = pt->kind == PSYGFX_PAINT_VERTEX ? pt->vertex_colors + 3 * k : pt->stops[k].color;
        psygfx__to_space(g, pt->space, rgb, c);
        if (pt->space == PSYGFX_SPACE_DKL_POLAR && k > 0) {   /* the shorter turn from the stop before */
            while (c[1] - prev_az > PSYGFX__PI) c[1] -= PSYGFX__TAU;
            while (c[1] - prev_az < -PSYGFX__PI) c[1] += PSYGFX__TAU;
        }
        prev_az = c[1];
        if (!psygfx__vput(vd, o + 2 + k, c[0], c[1], c[2], pt->kind == PSYGFX_PAINT_VERTEX ? 0.0 : pt->stops[k].t)) return PSYGFX_ERR_FULL;
    }
    if (pt->space != PSYGFX_SPACE_RGB) {
        const double* m = pt->space == PSYGFX_SPACE_OKLAB ? g->ok_lms2rgb : g->dkl2rgb;
        for (k = 0; k < 3; k++)
            if (!psygfx__vput(vd, o + 18 + k, m[3 * k], m[3 * k + 1], m[3 * k + 2], pt->space == PSYGFX_SPACE_DKL_POLAR ? g->bg[k] : 0.0))
                return PSYGFX_ERR_FULL;
        *used = 21;
    } else {
        *used = 2 + n;
    }
    /* A path between colors in gamut can leave it in a nonlinear space:
     * sampled, 64 points between each pair of neighbors, and counted. */
    for (k = 0; k + 1 < n && !clip; k++) {
        double a[3], b[3], rgb[3];
        int i2;
        for (j = 0; j < 3; j++) { a[j] = vd[4 * (o + 2 + k) + j]; b[j] = vd[4 * (o + 3 + k) + j]; }
        for (i2 = 0; i2 <= 64 && !clip; i2++) {
            double t = i2 / 64.0, m[3];
            for (j = 0; j < 3; j++) m[j] = a[j] + (b[j] - a[j]) * t;
            psygfx__from_space(g, pt->space, m, rgb);
            for (j = 0; j < 3; j++) if (rgb[j] < -1e-6 || rgb[j] > 1.0 + 1e-6) clip = 1;
        }
    }
    if (clip) *clipped = 1;
    return 0;
}

/* Packs a vector stimulus: its block from vec4 1 (vec4 0, the placement,
 * is the caller's) and the data region after it. Returns the blocks used,
 * or a negative code with *err set. ext gets the content's half extents
 * px (the bounds), before edges and effects. */
static int psygfx__vpack(const psygfx_gfx* g, const psygfx_stim* s, float* b, double ext[2], int* clipped, const char** err) {
    const double ppu = (double)g->ppu * psygfx__scale(s), er = psygfx__edge_reach(s);
    float* vd = b + 36;
    int nd = 0, np = 0, fl = 0, i, rc, pathlike = 0, kindv = 0, poly_g0 = -1;
    double L = -1.0, qx = 0.0, qy = 0.0, phw = 0.0, kmax = 0.0, a1 = 0.0, a2 = 0.0, outer, reach, gate;
    *err = "";
    *clipped = 0;
    memset(b + 4, 0, (size_t)(16 * 64 - 4) * sizeof(float));
    if (s->join != PSYGFX_JOIN_ROUND && s->shape != PSYGFX_POLYLINE) {
        *err = "MITER and BEVEL in the vector program: POLYLINE only"; return PSYGFX_ERR_ARG;
    }
    if (s->shape == PSYGFX_COMPOUND) {
        double R = 0.0;
        np = (int)s->n_prims;
        if (!s->prims || np < 1 || np > PSYGFX_MAX_PRIMS) { *err = "a compound needs 1 to PSYGFX_MAX_PRIMS prims"; return PSYGFX_ERR_ARG; }
        for (i = 0; i < np; i++) {
            const psygfx_prim* pr = &s->prims[i];
            float C[4], D[4];
            double l, rad, h2, cx = (double)pr->x * ppu, cy = (double)pr->y * ppu, an = (double)pr->ori * PSYGFX__PI / 180.0;
            int k = psygfx__vkind(pr->shape, pr->w, pr->h > 0.0f ? pr->h : pr->w, pr->p, ppu, C, D, &l, &rad, &h2, err);
            if (k < 0) return k;
            if (pr->op > PSYGFX_OP_SMOOTH_SUBTRACT) { *err = "a prim's op"; return PSYGFX_ERR_ARG; }
            if (i > 0 && pr->op >= PSYGFX_OP_SMOOTH_UNION) fl |= PSYGFX__VF_SMOOTH;
            if (pr->k * ppu > kmax) kmax = pr->k * ppu;
            psygfx__vput(vd, 4 * i, cx, cy, cos(an), sin(an));
            psygfx__vput(vd, 4 * i + 1, k, i ? pr->op : 0, pr->k * ppu, pr->onion > 0.0f ? pr->onion * ppu : 0.0);
            psygfx__vput(vd, 4 * i + 2, C[0], C[1], C[2], C[3]);
            psygfx__vput(vd, 4 * i + 3, D[0], D[1], D[2], D[3]);
            rad = sqrt(cx * cx + cy * cy) + rad + (pr->onion > 0.0f ? pr->onion * ppu : 0.0);
            if (rad > R) R = rad;
        }
        R += kmax + (s->shape_p[3] > 0.0f ? s->shape_p[3] * ppu : 0.0);
        qx = qy = R;
        nd = 4 * np;
        kindv = PSYGFX_COMPOUND;
    } else {
        float C[4], D[4];
        double rad;
        int sh = s->shape;
        np = 1;
        if (sh == PSYGFX_POLYGON) {
            double e2[2];
            rc = psygfx__vpolygon(s, ppu, vd, 4, &L, e2, err);
            if (rc < 0) return rc;
            C[0] = C[1] = C[2] = C[3] = 0.0f; D[0] = D[1] = 0.0f;
            D[2] = 4.0f; D[3] = (float)(int)s->shape_p[0];
            kindv = 4; poly_g0 = 4; nd = 4 + 3 * (int)s->shape_p[0];
            qx = e2[0]; qy = e2[1];
        } else if (sh == PSYGFX_POLYLINE || sh == PSYGFX_QBEZIER) {
            int n = (int)s->n_path, k;
            double hw = 0.5 * s->shape_p[0] * ppu, sacc = 0.0, lim = s->miter_limit > 0.0f ? s->miter_limit : 4.0;
            if (!s->path || hw <= 0.0 || (sh == PSYGFX_QBEZIER ? n != 3 : (n < 2 || n > PSYGFX_MAX_PATH))) {
                *err = "a path needs points (QBEZIER 3, POLYLINE 2 to 224) and a width"; return PSYGFX_ERR_ARG;
            }
            for (k = 0; k < n; k++) {
                double x = (double)s->path[2 * k] * ppu, y = (double)s->path[2 * k + 1] * ppu;
                if (fabs(x) > qx) qx = fabs(x);
                if (fabs(y) > qy) qy = fabs(y);
            }
            if (sh == PSYGFX_POLYLINE) {
                for (k = 0; k < n; k++) {
                    double x = (double)s->path[2 * k] * ppu, y = (double)s->path[2 * k + 1] * ppu;
                    if (k > 0) {
                        double dx = x - (double)s->path[2 * k - 2] * ppu, dy = y - (double)s->path[2 * k - 1] * ppu, l = sqrt(dx * dx + dy * dy);
                        if (l == 0.0) { *err = "a polyline with a repeated point"; return PSYGFX_ERR_ARG; }
                        sacc += l;
                    }
                    psygfx__vput(vd, 4 + k, x, y, sacc, 0.0);
                }
                C[0] = C[1] = 0.0f; C[2] = (float)hw; C[3] = (float)s->join;
                D[0] = (float)lim; D[1] = (float)s->cap; D[2] = 4.0f; D[3] = (float)n;
                kindv = 16; nd = 4 + n;
                hw *= s->join == PSYGFX_JOIN_MITER ? lim : 1.0;
            } else {
                double Ax = s->path[0] * ppu, Ay = s->path[1] * ppu, Bx = s->path[2] * ppu, By = s->path[3] * ppu;
                double Cx = s->path[4] * ppu, Cy = s->path[5] * ppu, bx = Ax - 2.0 * Bx + Cx, by = Ay - 2.0 * By + Cy;
                double px = Ax, py = Ay;
                int q, m;
                /* Chords on the CPU, drawn as a polyline with round joins: a
                 * chord over dt leaves the curve by at most |b| dt^2 / 4, b =
                 * A - 2B + C, so m chords keep it within 0.005 px. (IQ's
                 * closed form plus a Newton step held 4e-5 px but doubled the
                 * vector program's compile time through ANGLE.) */
                m = (int)ceil(sqrt(sqrt(bx * bx + by * by) / (4.0 * 0.005)));
                if (m < 1) m = 1;
                if (m > PSYGFX_MAX_PATH - 1) { *err = "a Bezier this curved needs more than 223 chords at 0.005 px"; return PSYGFX_ERR_ARG; }
                if ((Bx - Ax) * (Bx - Ax) + (By - Ay) * (By - Ay) == 0.0 && (Cx - Bx) * (Cx - Bx) + (Cy - By) * (Cy - By) == 0.0) {
                    *err = "a Bezier with no length"; return PSYGFX_ERR_ARG;
                }
                for (q = 0; q <= m; q++) {
                    double t = (double)q / m, x = (1 - t) * (1 - t) * Ax + 2 * (1 - t) * t * Bx + t * t * Cx;
                    double y = (1 - t) * (1 - t) * Ay + 2 * (1 - t) * t * By + t * t * Cy;
                    if (q > 0) sacc += sqrt((x - px) * (x - px) + (y - py) * (y - py));
                    psygfx__vput(vd, 4 + q, x, y, sacc, 0.0);
                    px = x; py = y;
                }
                C[0] = C[1] = 0.0f; C[2] = (float)hw; C[3] = (float)PSYGFX_JOIN_ROUND;
                D[0] = 4.0f; D[1] = (float)s->cap; D[2] = 4.0f; D[3] = (float)(m + 1);
                kindv = 16; nd = 5 + m;
            }
            L = sacc; phw = 0.5 * s->shape_p[0] * ppu; pathlike = 1;
            qx += hw + (s->cap == PSYGFX_CAP_SQUARE ? phw : 0.0);
            qy += hw + (s->cap == PSYGFX_CAP_SQUARE ? phw : 0.0);
        } else {
            double hx, hy;
            float sp[4];
            int k;
            for (k = 0; k < 4; k++) sp[k] = s->shape_p[k];
            kindv = psygfx__vkind(sh, s->w, s->h, sp, ppu, C, D, &L, &rad, &phw, err);
            if (kindv < 0) return kindv;
            pathlike = sh == PSYGFX_LINE || sh == PSYGFX_CAPSULE || sh == PSYGFX_ARC;
            hx = 0.5 * s->w * ppu; hy = 0.5 * s->h * ppu;
            if (sh == PSYGFX_CIRCLE || sh == PSYGFX_ANNULUS || sh == PSYGFX_ARC || sh == PSYGFX_PIE ||
                sh == PSYGFX_NGON || sh == PSYGFX_STAR) hy = hx;
            if (sh == PSYGFX_LINE) hy = 0.5 * s->shape_p[0] * ppu;
            if (sh == PSYGFX_CAPSULE) hy = fmax(s->shape_p[0], s->shape_p[1]) * ppu;
            qx = hx; qy = hy;
            nd = 4;
        }
        psygfx__vput(vd, 0, 0.0, 0.0, 1.0, 0.0);
        psygfx__vput(vd, 1, kindv, 0.0, 0.0, 0.0);
        psygfx__vput(vd, 2, C[0], C[1], C[2], C[3]);
        psygfx__vput(vd, 3, D[0], D[1], D[2], D[3]);
    }
    /* the main layer: a fill, or the stroke's band */
    if (s->stroke > 0.0f) {
        float f1, f2;
        psygfx__band(s, &f1, &f2);
        a1 = f1; a2 = f2;
    }
    gate = s->gate * (s->group ? s->group->opacity : 1.0f);
    b[8] = s->opacity; b[9] = (float)gate; b[10] = s->offset;
    b[11] = (float)(0.5 * (a1 + a2)); b[15] = (float)(0.5 * (a2 - a1));
    psygfx__copy3(b + 12, s->color);
    b[20] = (float)s->shape; b[21] = (float)s->edge; b[22] = s->edge == PSYGFX_EDGE_HARD || s->edge_width < 0.0f ? 0.0f : s->edge_width;
    b[27] = (float)np;
    b[30] = s->shape == PSYGFX_COMPOUND && s->shape_p[3] > 0.0f ? (float)(s->shape_p[3] * ppu) : 0.0f;
    b[35] = (float)phw;
    /* dashes and trim run along the boundary, or a path-like kind's center
     * line; on a closed boundary they need a stroke */
    {
        int dsh = s->dash[0] > 0.0f;
        int trm = !(s->trim[0] == 0.0f && s->trim[1] == 0.0f) && !(s->trim[0] <= 0.0f && s->trim[1] >= 1.0f);
        if (dsh || trm) {
            double P, dash = s->dash[0], gap = s->dash[1] > 0.0f ? s->dash[1] : 0.0;
            if (L <= 0.0 || s->shape == PSYGFX_COMPOUND) { *err = "dashes and trim need a length (not a cross, mask or compound)"; return PSYGFX_ERR_ARG; }
            if (!(s->stroke > 0.0f) && !pathlike) { *err = "dashes and trim on a closed boundary need a stroke"; return PSYGFX_ERR_ARG; }
            if (trm && !(s->trim[0] >= 0.0f && s->trim[0] <= s->trim[1] && s->trim[1] <= 1.0f)) { *err = "trim: 0 <= start <= end <= 1"; return PSYGFX_ERR_ARG; }
            P = dash + gap;
            if (dsh && s->dash_snap && !pathlike) {
                double m = floor(L / P + 0.5);
                if (m < 1.0) m = 1.0;
                dash *= L / (m * P); gap *= L / (m * P);
            }
            b[16] = (float)dash; b[17] = (float)gap; b[18] = s->dash_offset;
            b[24] = (float)(s->trim[0] * L); b[25] = (float)(s->trim[1] * L);
            fl |= (dsh ? PSYGFX__VF_DASH : 0) | (trm ? PSYGFX__VF_TRIM : 0) | PSYGFX__VF_S;
        }
        b[19] = (float)L; b[26] = (float)s->cap;
    }
    /* reach past the boundary, px: the quad's margin */
    outer = s->offset + (s->stroke > 0.0f ? (a2 > 0.0 ? a2 : 0.0) : 0.0);
    if (outer < 0.0) outer = 0.0;
    reach = outer + er;
    b[31] = 1.0f;
    b[32] = b[33] = -1.0f;
    if (s->fx) {
        const psygfx_fx* fx = s->fx;
        double rad = ((double)s->ori + (s->group ? s->group->ori : 0.0f)) * PSYGFX__PI / 180.0, c = cos(rad), sn = sin(rad), r;
        int k;
        rc = psygfx__vfx(fx, vd, nd);
        if (rc < 0) { *err = "the data region is full"; return rc; }
        b[32] = (float)nd;
        nd += 10;
        fl |= PSYGFX__VF_FX;
        b[28] = (float)(fx->dx * c + fx->dy * sn);
        b[29] = (float)(-fx->dx * sn + fx->dy * c);
        if ((fx->drop.opacity > 0.0f || fx->inner.opacity > 0.0f) && (fx->dx != 0.0f || fx->dy != 0.0f)) b[31] = 2.0f;
        if (fx->flags & PSYGFX_FX_EXACT_BLUR) {
            if (!(kindv == 8 && vd[10] == 0.0f && vd[11] == 0.0f && vd[12] == 0.0f && vd[13] == 0.0f)) {
                *err = "PSYGFX_FX_EXACT_BLUR needs a RECT or RRECT with no corner radius"; return PSYGFX_ERR_ARG;
            }
            fl |= PSYGFX__VF_RBLUR;
        }
        if (fx->drop.opacity > 0.0f) {
            r = sqrt((double)fx->dx * fx->dx + (double)fx->dy * fx->dy) + fx->drop.spread + s->offset + 5.0 * fx->drop.sigma;
            if (r > reach) reach = r;
        }
        if (fx->glow.opacity > 0.0f) {
            r = (double)fx->glow.spread + s->offset + 5.0 * fx->glow.sigma;
            if (r > reach) reach = r;
        }
        for (k = 0; k < 2; k++)
            if (fx->band[k].opacity > 0.0f) {
                r = (double)fx->band[k].a2 + s->offset + er;
                if (r > reach) reach = r;
            }
    }
    if (s->paint && s->paint->kind != PSYGFX_PAINT_SOLID) {
        int used = 0;
        rc = psygfx__vpaint(g, s, s->paint, ppu, vd, nd, poly_g0, &used, clipped, err);
        if (rc < 0) return rc;
        b[33] = (float)nd;
        nd += used;
        fl |= PSYGFX__VF_PAINT;
    }
    if (nd > PSYGFX__VDATA) { *err = "the data region is full (247 vec4)"; return PSYGFX_ERR_FULL; }
    b[23] = (float)fl;
    b[4] = (float)qx; b[5] = (float)qy; b[6] = (float)ppu;
    b[7] = (float)(reach + 1.0);
    ext[0] = qx + outer; ext[1] = qy + outer;
    return nd <= 7 ? 1 : 1 + (nd - 7 + 15) / 16;
}

/* --- parameter tables --------------------------------------------------------- */

#define PSYGFX__K(k) (1u << (k))
#define PSYGFX__ALL  0x1FEu
#define PSYGFX__MOD  (PSYGFX__K(PSYGFX_GRATING) | PSYGFX__K(PSYGFX_GABOR) | PSYGFX__K(PSYGFX_NOISE) | \
                      PSYGFX__K(PSYGFX_IMAGE) | PSYGFX__K(PSYGFX_USER))
#define PSYGFX__COL  (PSYGFX__K(PSYGFX_SHAPE) | PSYGFX__K(PSYGFX_DOTS) | PSYGFX__K(PSYGFX_IMAGE) | PSYGFX__K(PSYGFX_USER))
#define PSYGFX__APT  (PSYGFX__K(PSYGFX_SHAPE) | PSYGFX__K(PSYGFX_GRATING) | PSYGFX__K(PSYGFX_NOISE) | \
                      PSYGFX__K(PSYGFX_USER) | PSYGFX__K(PSYGFX_DOTS))
#define PSYGFX__F(name, field, lo, hi, def, unit, doc, kinds) \
    { name, "f32", lo, hi, def, unit, doc, (uint32_t)offsetof(psygfx_stim, field), kinds }
#define PSYGFX__P(i) PSYGFX__F("p" #i, p[i], -1e30, 1e30, 0, "", "user parameter; POLYGON vertex coordinate", \
                               PSYGFX__K(PSYGFX_USER) | PSYGFX__K(PSYGFX_SHAPE))

static const psygfx_param psygfx__params[PSYGFX_P_COUNT] = {
    PSYGFX__F("visible", visible, 0, 1, 1, "", "drawn when 0.5 or more; onset and offset events", PSYGFX__ALL),
    PSYGFX__F("x", x, -1e6, 1e6, 0, "u", "anchor offset from the place point, +x right", PSYGFX__ALL),
    PSYGFX__F("y", y, -1e6, 1e6, 0, "u", "anchor offset from the place point, +y down", PSYGFX__ALL),
    PSYGFX__F("ax", ax, -10, 10, 0.5, "", "anchor x, fraction of the box from its left edge", PSYGFX__ALL),
    PSYGFX__F("ay", ay, -10, 10, 0.5, "", "anchor y, fraction of the box from its top edge", PSYGFX__ALL),
    PSYGFX__F("w", w, 0, 1e6, 0, "u", "box width; GABOR 0 = 8 sigma", PSYGFX__ALL),
    PSYGFX__F("h", h, 0, 1e6, 0, "u", "box height; 0 = w in constructors", PSYGFX__ALL),
    PSYGFX__F("ori", ori, -1e6, 1e6, 0, "deg", "rotation about the anchor, +x toward +y (clockwise)", PSYGFX__ALL),
    PSYGFX__F("contrast", contrast, -1e3, 1e3, 1, "", "modulation amplitude along dir", PSYGFX__MOD),
    PSYGFX__F("opacity", opacity, 0, 1, 1, "", "alpha of a color stimulus", PSYGFX__COL),
    PSYGFX__F("gate", gate, 0, 1, 1, "", "multiplier for flicker tracks", PSYGFX__ALL),
    PSYGFX__F("color_r", color[0], 0, 1, 0, "rgb", "linear device red", PSYGFX__COL),
    PSYGFX__F("color_g", color[1], 0, 1, 0, "rgb", "linear device green", PSYGFX__COL),
    PSYGFX__F("color_b", color[2], 0, 1, 0, "rgb", "linear device blue", PSYGFX__COL),
    PSYGFX__F("dir_r", dir[0], -1e3, 1e3, 0, "rgb", "modulation per unit contrast, red; all 0 = background", PSYGFX__MOD),
    PSYGFX__F("dir_g", dir[1], -1e3, 1e3, 0, "rgb", "modulation per unit contrast, green", PSYGFX__MOD),
    PSYGFX__F("dir_b", dir[2], -1e3, 1e3, 0, "rgb", "modulation per unit contrast, blue", PSYGFX__MOD),
    PSYGFX__F("sf", sf, 0, 1e6, 0, "cyc/u", "spatial frequency", PSYGFX__K(PSYGFX_GRATING) | PSYGFX__K(PSYGFX_GABOR)),
    PSYGFX__F("phase", phase, -1e9, 1e9, 0, "cyc", "carrier phase at the box center", PSYGFX__K(PSYGFX_GRATING) | PSYGFX__K(PSYGFX_GABOR)),
    PSYGFX__F("sigma", sigma, 0, 1e6, 0, "u", "Gaussian envelope SD", PSYGFX__K(PSYGFX_GABOR)),
    PSYGFX__F("aspect", aspect, 0, 1e6, 1, "", "envelope: y SD = sigma / aspect", PSYGFX__K(PSYGFX_GABOR)),
    PSYGFX__F("edge_width", edge_width, 0, 1e6, 0, "px", "COSINE full width, GAUSSIAN SD", PSYGFX__APT),
    PSYGFX__F("shape_p0", shape_p[0], 0, 1e6, 0, "u", "annulus inner radius, line and cross arm width, polygon n", PSYGFX__APT),
    PSYGFX__F("shape_p1", shape_p[1], 0, 1e6, 0, "u", "rect corner radius", PSYGFX__APT),
    PSYGFX__F("shape_p2", shape_p[2], 0, 1e6, 0, "u", "reserved", PSYGFX__APT),
    PSYGFX__F("shape_p3", shape_p[3], 0, 1e6, 0, "u", "reserved", PSYGFX__APT),
    PSYGFX__F("dot_size", dot_size, 0, 1e6, 0, "px", "dot diameter", PSYGFX__K(PSYGFX_DOTS)),
    PSYGFX__F("seed", seed, 0, 16777215, 0, "", "noise seed, an integer", PSYGFX__K(PSYGFX_NOISE)),
    PSYGFX__F("check", check, 1, 1e6, 1, "px", "noise check size", PSYGFX__K(PSYGFX_NOISE)),
    PSYGFX__P(0),  PSYGFX__P(1),  PSYGFX__P(2),  PSYGFX__P(3),  PSYGFX__P(4),  PSYGFX__P(5),  PSYGFX__P(6),  PSYGFX__P(7),
    PSYGFX__P(8),  PSYGFX__P(9),  PSYGFX__P(10), PSYGFX__P(11), PSYGFX__P(12), PSYGFX__P(13), PSYGFX__P(14), PSYGFX__P(15),
    PSYGFX__P(16), PSYGFX__P(17), PSYGFX__P(18), PSYGFX__P(19), PSYGFX__P(20), PSYGFX__P(21), PSYGFX__P(22), PSYGFX__P(23),
    PSYGFX__P(24), PSYGFX__P(25), PSYGFX__P(26), PSYGFX__P(27), PSYGFX__P(28), PSYGFX__P(29), PSYGFX__P(30), PSYGFX__P(31),
    PSYGFX__F("stroke", stroke, 0, 1e6, 0, "px", "stroke band width; 0 = fill", PSYGFX__APT),
    PSYGFX__F("miter_limit", miter_limit, 0, 1e6, 4, "", "MITER: miter length over stroke width before a bevel", PSYGFX__APT),
    PSYGFX__F("tint_r", tint[0], 0, 1e3, 1, "", "image tint, linear red factor", PSYGFX__K(PSYGFX_IMAGE)),
    PSYGFX__F("tint_g", tint[1], 0, 1e3, 1, "", "image tint, linear green factor", PSYGFX__K(PSYGFX_IMAGE)),
    PSYGFX__F("tint_b", tint[2], 0, 1e3, 1, "", "image tint, linear blue factor", PSYGFX__K(PSYGFX_IMAGE)),
    PSYGFX__F("tint_a", tint[3], 0, 1e3, 1, "", "image tint, alpha factor", PSYGFX__K(PSYGFX_IMAGE)),
    PSYGFX__F("src_x", src[0], 0, 65535, 0, "texel", "source rectangle, left", PSYGFX__K(PSYGFX_IMAGE)),
    PSYGFX__F("src_y", src[1], 0, 65535, 0, "texel", "source rectangle, top", PSYGFX__K(PSYGFX_IMAGE)),
    PSYGFX__F("src_w", src[2], 0, 65535, 0, "texel", "source rectangle width; 0 = the whole texture", PSYGFX__K(PSYGFX_IMAGE)),
    PSYGFX__F("src_h", src[3], 0, 65535, 0, "texel", "source rectangle height", PSYGFX__K(PSYGFX_IMAGE)),
    PSYGFX__F("offset", offset, -1e6, 1e6, 0, "px", "boundary moved out (+) or in (-)", PSYGFX__K(PSYGFX_SHAPE)),
    PSYGFX__F("dash", dash[0], 0, 1e6, 0, "px", "dash length along the boundary; 0 = none", PSYGFX__K(PSYGFX_SHAPE)),
    PSYGFX__F("gap", dash[1], 0, 1e6, 0, "px", "gap between dashes", PSYGFX__K(PSYGFX_SHAPE)),
    PSYGFX__F("dash_offset", dash_offset, -1e9, 1e9, 0, "px", "dash pattern shift along the boundary", PSYGFX__K(PSYGFX_SHAPE)),
    PSYGFX__F("trim_start", trim[0], 0, 1, 0, "", "drawn from this fraction of the length", PSYGFX__K(PSYGFX_SHAPE)),
    PSYGFX__F("trim_end", trim[1], 0, 1, 0, "", "drawn to this fraction; 0, 0 = all", PSYGFX__K(PSYGFX_SHAPE)),
};

#define PSYGFX__GF(name, field, lo, hi, def, unit, doc) \
    { name, "f32", lo, hi, def, unit, doc, (uint32_t)offsetof(psygfx_group, field), 0u }
static const psygfx_param psygfx__group_params[PSYGFX_G_COUNT] = {
    PSYGFX__GF("visible", visible, 0, 1, 1, "", "members drawn when 0.5 or more"),
    PSYGFX__GF("x", x, -1e6, 1e6, 0, "u", "anchor offset from the place point, +x right"),
    PSYGFX__GF("y", y, -1e6, 1e6, 0, "u", "anchor offset from the place point, +y down"),
    PSYGFX__GF("ax", ax, -10, 10, 0.5, "", "anchor x, fraction of the group's box"),
    PSYGFX__GF("ay", ay, -10, 10, 0.5, "", "anchor y, fraction of the group's box"),
    PSYGFX__GF("w", w, 0, 1e6, 0, "u", "the group's box width"),
    PSYGFX__GF("h", h, 0, 1e6, 0, "u", "the group's box height"),
    PSYGFX__GF("ori", ori, -1e6, 1e6, 0, "deg", "rotation about the anchor, +x toward +y (clockwise)"),
    PSYGFX__GF("scale", scale, 0, 1e6, 1, "", "members' sizes in units; px quantities unscaled"),
    PSYGFX__GF("opacity", opacity, 0, 1, 1, "", "multiplies every member's gate"),
};

#define PSYGFX__XF(T, name, field, lo, hi, def, unit, doc) \
    { name, "f32", lo, hi, def, unit, doc, (uint32_t)offsetof(T, field), 0u }
static const psygfx_param psygfx__prim_params[] = {
    PSYGFX__XF(psygfx_prim, "x", x, -1e6, 1e6, 0, "u", "center in the compound's box frame, +x right"),
    PSYGFX__XF(psygfx_prim, "y", y, -1e6, 1e6, 0, "u", "center, +y down"),
    PSYGFX__XF(psygfx_prim, "w", w, 0, 1e6, 0, "u", "the primitive's box width, as the shape's w"),
    PSYGFX__XF(psygfx_prim, "h", h, 0, 1e6, 0, "u", "box height; 0 = w"),
    PSYGFX__XF(psygfx_prim, "ori", ori, -1e6, 1e6, 0, "deg", "rotation, +x toward +y (clockwise)"),
    PSYGFX__XF(psygfx_prim, "k", k, 0, 1e6, 0, "u", "SMOOTH_* blend radius"),
    PSYGFX__XF(psygfx_prim, "onion", onion, 0, 1e6, 0, "u", "half thickness of the shell |d| - onion; 0 = none"),
    PSYGFX__XF(psygfx_prim, "p0", p[0], -1e6, 1e6, 0, "", "shape parameter 0, as shape_p"),
    PSYGFX__XF(psygfx_prim, "p1", p[1], -1e6, 1e6, 0, "", "shape parameter 1"),
    PSYGFX__XF(psygfx_prim, "p2", p[2], -1e6, 1e6, 0, "", "shape parameter 2"),
    PSYGFX__XF(psygfx_prim, "p3", p[3], -1e6, 1e6, 0, "", "shape parameter 3"),
};
#define PSYGFX__SH(base, doc) \
    PSYGFX__XF(psygfx_fx, base ".sigma", base_.sigma, 0, 1e6, 0, "px", doc " Gaussian SD; 0 = hard"), \
    PSYGFX__XF(psygfx_fx, base ".spread", base_.spread, -1e6, 1e6, 0, "px", doc " silhouette moved out"), \
    PSYGFX__XF(psygfx_fx, base ".color_r", base_.color[0], 0, 1, 0, "rgb", doc " linear device red"), \
    PSYGFX__XF(psygfx_fx, base ".color_g", base_.color[1], 0, 1, 0, "rgb", doc " linear device green"), \
    PSYGFX__XF(psygfx_fx, base ".color_b", base_.color[2], 0, 1, 0, "rgb", doc " linear device blue"), \
    PSYGFX__XF(psygfx_fx, base ".opacity", base_.opacity, 0, 1, 0, "", doc " opacity; 0 = off")
#define PSYGFX__BD(base, i) \
    PSYGFX__XF(psygfx_fx, base ".a1", band[i].a1, -1e6, 1e6, 0, "px", "band inner offset of the boundary"), \
    PSYGFX__XF(psygfx_fx, base ".a2", band[i].a2, -1e6, 1e6, 0, "px", "band outer offset"), \
    PSYGFX__XF(psygfx_fx, base ".color_r", band[i].color[0], 0, 1, 0, "rgb", "band linear device red"), \
    PSYGFX__XF(psygfx_fx, base ".color_g", band[i].color[1], 0, 1, 0, "rgb", "band linear device green"), \
    PSYGFX__XF(psygfx_fx, base ".color_b", band[i].color[2], 0, 1, 0, "rgb", "band linear device blue"), \
    PSYGFX__XF(psygfx_fx, base ".opacity", band[i].opacity, 0, 1, 0, "", "band opacity; 0 = off")
#define base_ drop
static const psygfx_param psygfx__fx_params_drop[] = { PSYGFX__SH("drop", "drop shadow") };
#undef base_
#define base_ glow
static const psygfx_param psygfx__fx_params_glow[] = { PSYGFX__SH("glow", "outer glow") };
#undef base_
#define base_ inner
static const psygfx_param psygfx__fx_params_inner[] = { PSYGFX__SH("inner", "inner shadow") };
#undef base_
static const psygfx_param psygfx__fx_params_rest[] = {
    PSYGFX__XF(psygfx_fx, "dx", dx, -1e6, 1e6, 0, "px", "drop and inner shadow offset, screen +x right"),
    PSYGFX__XF(psygfx_fx, "dy", dy, -1e6, 1e6, 0, "px", "the offset, screen +y down"),
    PSYGFX__BD("band0", 0), PSYGFX__BD("band1", 1),
};
static psygfx_param psygfx__fx_params[2 + 3 * 6 + 12];
static const psygfx_param psygfx__paint_params[] = {
    PSYGFX__XF(psygfx_paint, "x0", x0, -1e6, 1e6, 0, "u", "LINEAR start, RADIAL and ANGULAR center x"),
    PSYGFX__XF(psygfx_paint, "y0", y0, -1e6, 1e6, 0, "u", "LINEAR start, the center y"),
    PSYGFX__XF(psygfx_paint, "x1", x1, -1e6, 1e6, 0, "", "LINEAR end x (u), RADIAL radius (u), ANGULAR start (deg)"),
    PSYGFX__XF(psygfx_paint, "y1", y1, -1e6, 1e6, 0, "u", "LINEAR end y"),
#define PSYGFX__ST(i) \
    PSYGFX__XF(psygfx_paint, "stop" #i ".t", stops[i].t, 0, 1, 0, "", "stop position"), \
    PSYGFX__XF(psygfx_paint, "stop" #i ".color_r", stops[i].color[0], 0, 1, 0, "rgb", "stop linear device red"), \
    PSYGFX__XF(psygfx_paint, "stop" #i ".color_g", stops[i].color[1], 0, 1, 0, "rgb", "stop linear device green"), \
    PSYGFX__XF(psygfx_paint, "stop" #i ".color_b", stops[i].color[2], 0, 1, 0, "rgb", "stop linear device blue")
    PSYGFX__ST(0), PSYGFX__ST(1), PSYGFX__ST(2), PSYGFX__ST(3), PSYGFX__ST(4), PSYGFX__ST(5),
};

PSYGFX_API const psygfx_param* psygfx_prim_params(int* n) {
    if (n) *n = (int)(sizeof psygfx__prim_params / sizeof psygfx__prim_params[0]);
    return psygfx__prim_params;
}

PSYGFX_API const psygfx_param* psygfx_fx_params(int* n) {
    /* one table from four: the shadows' share a macro */
    if (!psygfx__fx_params[0].name) {
        int k = 0, i;
        for (i = 0; i < 2; i++) psygfx__fx_params[k++] = psygfx__fx_params_rest[i];
        for (i = 0; i < 6; i++) psygfx__fx_params[k++] = psygfx__fx_params_drop[i];
        for (i = 0; i < 6; i++) psygfx__fx_params[k++] = psygfx__fx_params_glow[i];
        for (i = 0; i < 6; i++) psygfx__fx_params[k++] = psygfx__fx_params_inner[i];
        for (i = 2; i < 14; i++) psygfx__fx_params[k++] = psygfx__fx_params_rest[i];
    }
    if (n) *n = (int)(sizeof psygfx__fx_params / sizeof psygfx__fx_params[0]);
    return psygfx__fx_params;
}

PSYGFX_API const psygfx_param* psygfx_paint_params(int* n) {
    if (n) *n = (int)(sizeof psygfx__paint_params / sizeof psygfx__paint_params[0]);
    return psygfx__paint_params;
}

PSYGFX_API double psygfx_length(const psygfx_gfx* g, const psygfx_stim* s) {
    float blk[16 * 64];
    double ext[2];
    const char* err;
    int clip, n;
    psygfx_stim t;
    if (!g || !g->open || !s) return PSYGFX_ERR_ARG;
    if (s->kind != PSYGFX_SHAPE || s->shape == PSYGFX_MASK_TEX || s->shape == PSYGFX_COMPOUND || s->shape == PSYGFX_CROSS ||
        s->shape == PSYGFX_NO_APERTURE) return PSYGFX_ERR_ARG;
    t = *s;
    t.fx = NULL; t.paint = NULL; t.dash[0] = 0.0f; t.trim[0] = t.trim[1] = 0.0f;
    t.join = PSYGFX_JOIN_ROUND; t.shape_p[1] = t.shape == PSYGFX_POLYGON && t.shape_p[1] <= 0.0f ? 0.0f : t.shape_p[1];
    t.dash[0] = 1.0f; t.dash[1] = 1.0f; t.dash_snap = 0;
    if (t.stroke <= 0.0f) t.stroke = 1.0f;   /* a length needs no stroke; the pack asks for one */
    n = psygfx__vpack(g, &t, blk, ext, &clip, &err);
    if (n < 0) return n;
    return (double)blk[19];
}

PSYGFX_API const psygfx_param* psygfx_group_params(int* n) {
    if (n) *n = PSYGFX_G_COUNT;
    return psygfx__group_params;
}

PSYGFX_API const psygfx_param* psygfx_params(int* n) {
    if (n) *n = PSYGFX_P_COUNT;
    return psygfx__params;
}

PSYGFX_API int psygfx_apply(const psygfx_bind* b, int n, const float* values) {
    int i;
    if ((!b && n > 0) || n < 0 || !values) return PSYGFX_ERR_ARG;
    /* all or nothing: a bad entry changes no field */
    for (i = 0; i < n; i++) {
        if (b[i].field) continue;
        if (b[i].group ? b[i].param >= PSYGFX_G_COUNT : (!b[i].stim || b[i].param >= PSYGFX_P_COUNT))
            return PSYGFX_ERR_ARG;
    }
    for (i = 0; i < n; i++) {
        if (b[i].field) *b[i].field = values[b[i].channel];
        else if (b[i].group) *(float*)((char*)b[i].group + psygfx__group_params[b[i].param].offset) = values[b[i].channel];
        else *(float*)((char*)b[i].stim + psygfx__params[b[i].param].offset) = values[b[i].channel];
    }
    return n;
}

static const psyscr_param psygfx__desc_params[] = {
    { "background.r", "f32", 0, 1, 0, "rgb", "background, linear device red" },
    { "background.g", "f32", 0, 1, 0, "rgb", "background, linear device green" },
    { "background.b", "f32", 0, 1, 0, "rgb", "background, linear device blue" },
    { "units", "enum", 0, 1, 0, "", "0 pixels, 1 degrees (one scale factor at the screen center)" },
    { "view.distance_mm", "f32", 0, 1e5, 0, "mm", "viewing distance, degrees only" },
    { "view.width_mm", "f32", 0, 1e5, 0, "mm", "width of the drawable area, degrees only" },
    { "dither", "enum", 0, 2, 0, "", "0 none (round), 1 ordered 8x8, 2 noise per frame" },
    { "seed", "u32", 0, 4294967295.0, 0, "", "noise dither seed" },
    { "output", "enum", 0, 0, 0, "", "0 8-bit (the only one in v0.1)" },
    { "stereo", "enum", 0, 0, 0, "", "0 mono (the only one in v0.1)" },
    { "max_draws", "i32", 0, 65536, 1024, "", "draws per frame" },
};

PSYGFX_API const psyscr_param* psygfx_desc_params(int* n) {
    if (n) *n = (int)(sizeof psygfx__desc_params / sizeof psygfx__desc_params[0]);
    return psygfx__desc_params;
}

#ifdef __cplusplus
}
#endif

#endif /* PSY_GFX_IMPLEMENTATION_GUARD */
#endif /* PSY_GFX_IMPLEMENTATION */

/* ------------------------------------------------------------------------
 * This software is available under the MIT-0 (MIT No Attribution) license.
 *
 * Copyright (c) 2026 psy contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY.
 * ------------------------------------------------------------------------
 * Parts under the MIT license: the vector program's distance functions
 * (psygfx__glsl_vec0 and _vec1) and their CPU forms (psygfx__vp_*,
 * psygfx__vsmin). Each such function names its source in a comment.
 *
 *   Inigo Quilez, 2D distance functions and the smooth minimum
 *   (iquilezles.org/articles/distfunctions2d, /articles/smin), with the
 *   notice of each Shadertoy source:
 *     The MIT License
 *     Copyright (c) 2015 Inigo Quilez   (rounded box, Shadertoy 4llXD7)
 *     Copyright (c) 2018 Inigo Quilez   (uneven capsule, Shadertoy 4lcBWn)
 *     Copyright (c) 2019 Inigo Quilez   (arc, Shadertoy wl23RK)
 *     Copyright (c) 2019 Inigo Quilez   (pie, Shadertoy 3l23RK)
 *     Copyright (c) 2019 Inigo Quilez   (star, Shadertoy 3tSGDy)
 *   The box and the smooth minimum come from the articles, which carry no
 *   per-function year; they are under the same terms.
 *
 *   0xfaded, the trig-free closest point on an ellipse
 *   (github.com/0xfaded/ellipse_demo):
 *     MIT License
 *     Copyright (c) 2017 Carl Chatfield
 *
 * Both under these terms:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 * ------------------------------------------------------------------------ */
