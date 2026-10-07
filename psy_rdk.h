/* psy_rdk.h - v0.1.0 - public domain single-header random dot kinematograms
 *
 *   The motion of a random dot kinematogram (RDK) on the CPU: coherence,
 *   direction, speed, density, dot lifetime, the noise rule and the signal
 *   rule, each by a named published algorithm; a circular or rectangular
 *   aperture with an edge rule that keeps the density uniform; motion by
 *   the predicted onset of each frame, so a dropped frame moves dots by the
 *   time that passed; interleaved sets (Movshon/Newsome); and a
 *   reproducible sequence: the state is integers and every random draw is
 *   a counter-based function of (seed, stream, dot, update, slot), so a
 *   trial regenerates bit for bit on any platform from what MODEL lists.
 *   Output goes into caller arrays: x, y pairs as psy_gfx.h's DOTS read
 *   them, or strided into psy_gfx.h's instance records (a gabor array whose
 *   elements ride the dots).
 *
 *   Written in the single-header style of the stb / sokol libraries. Pure
 *   computation: no OS calls, no threads, no I/O, one allocation at open
 *   (none when the caller gives the memory). Needs nothing but the C
 *   standard library (<math.h> for frexp, sqrt and llround). It does not
 *   include psy_gfx.h or psy_timeline.h. C99 is the floor: it builds as
 *   C99, C11 and C++17, and in the C dialect MSVC compiles by default.
 *
 *   ---------------------------------------------------------------------
 *   CHANGELOG
 *   ---------------------------------------------------------------------
 *   v0.1.0 - first version.
 *
 *   STATUS: v0.1.0. See docs/psy_rdk.md for the tables.
 *   Built with MSVC 19.44 (/W4 /WX: the default C dialect, C11, C++17),
 *   MinGW-w64 gcc 16.1 (C99, C11, C++17) and gcc 11.4 on WSL2 (C99, C11,
 *   C++17; the test under ASan and UBSan, at -O3 -march=native and at
 *   -Ofast -march=native). tests/adapt/psy_rdk_test.c checks statistics,
 *   not pixels: the realized coherence counted from the dots'
 *   displacements (EXACT: round(c n) on every update; BERNOULLI: the
 *   binomial mean and variance), density against uniform by chi-square in
 *   equal-area cells and in the band along the edge for 36 configurations
 *   (0.42 to 0.79 of the p = 1e-7 critical value; REPLOT 18.6 to 21.2
 *   times it), lifetimes (exact under the frame clock, the mean within
 *   1.4e-5 under the onset clock, no pulse), the displacement of signal
 *   dots against speed x elapsed time under jittered and dropped onsets
 *   (4.7e-7 units on a 5-unit radius, the float output's resolution), the
 *   noise rules' direction and position distributions, frozen noise
 *   across coherence levels, dropped frames under both clocks, interleaved
 *   sets, LEAST_RECENT's rule, the edge invariant under stalls of up to
 *   1000 s; and the exact pieces: Philox against Random123's known
 *   answers, the angle conversion against exact rationals, sincos within
 *   2.6e-9 of libm, and 8 golden digests that tests/compare/rdk_ref.py, an
 *   independent Python model written from REPRODUCIBILITY, reproduces.
 *   26 of 26 non-equivalent mutations of the header caught (one by the
 *   digests only), 2 equivalent (tests/mutate/rdk.toml).
 *   Cost on the Iris Xe laptop (AC, the lock held; examples/rdk_bench.c,
 *   MSVC 19.44 and gcc 16.1; docs/psy_rdk.md has the tables):
 *   psyrdk_update() for 1000 dots 8.9 to 16 us (SAME with DIRECTION
 *   noise, which draws nothing) and 18 to 43 us (the rules that draw for
 *   every dot), for 10,000 dots 102 to 172 us and 183 to 443 us. The bars
 *   set in the design, 10 and 100 us, are missed by every rule that draws
 *   and, at 10,000 dots, by every rule. With psy_gfx.h
 *   (examples/gfx_rdk_bench.c, 1920 x 1200): 10,000 dots updated and
 *   drawn in 0.30 ms of CPU and 0.32 to 0.38 ms of GPU over the empty
 *   frame; 10,000 gabors riding the dots 0.41 ms of CPU and 1.36 to 1.48
 *   ms of GPU. No number is a measurement of light.
 *
 *   ---------------------------------------------------------------------
 *   USAGE
 *   ---------------------------------------------------------------------
 *   Do this:
 *
 *       #define PSY_RDK_IMPLEMENTATION
 *
 *   in *one* C or C++ file before including this header. Every other file
 *   just includes it.
 *
 *   A classic RDK drawn with psy_gfx.h (examples/gfx_rdk.c runs it):
 *
 *       static psyrdk_field rdk;
 *       if (psyrdk_open(&rdk, &(psyrdk_desc){ .w = 400, .count = 200,
 *               .coherence = 0.25f, .direction = 0, .speed = 120 }) < 0)
 *           fail(psyrdk_error(&rdk));
 *       psygfx_buf buf = psygfx_buffer(&gfx, (size_t)rdk.n * 8);
 *       psygfx_stim dots = psygfx_dots(&(psygfx_dots_desc){ .buf = buf,
 *           .count = (uint32_t)rdk.n, .dot_size = 4,
 *           .aperture = PSYGFX_CIRCLE, .w = 400, .color = { 1, 1, 1 } });
 *       static float xy[2 * 200];
 *
 *       psyscr_frame f;
 *       psyscr_begin(&scr, &f);
 *       psyrdk_start(&rdk, trial_seed, f.onset);       // the trial's first frame
 *       do {
 *           psyrdk_update(&rdk, f.onset);              // dots at this frame's onset
 *           psyrdk_xy(&rdk, xy);
 *           psygfx_buffer_update(&gfx, buf, 0, xy, (size_t)rdk.n * 8);
 *           psygfx_begin(&gfx, &f);
 *           psygfx_draw(&gfx, &dots);
 *           psygfx_end(&gfx);
 *           psyscr_flip(&scr);
 *       } while (psyscr_begin(&scr, &f) == PSYSCR_OK);
 *
 *   coherence, direction and speed are plain floats of the field, read at
 *   each update, so a psy_timeline.h channel drives them through
 *   psygfx_bind.field, as a GSAP timeline drives a property:
 *
 *       static const psygfx_bind binds[] = {
 *           { .stim = &dots, .param = PSYGFX_P_VISIBLE, .channel = DOTS_ON },
 *           { .field = &rdk.coherence, .channel = COH },
 *           { .field = &rdk.direction, .channel = DIR },
 *       };
 *       psytl_seq q = psytl_seq_on(&tl, TRIAL);        // the trial, in order
 *       psytl_set_value(&q, COH, 0.256f);
 *       psytl_on(&q, DOTS_ON);
 *       psytl_wait(&q, PSYTL_MS(700));                 // DIR is a track: a tween
 *       psytl_then(&q, DIR, &(psytl_tween_desc){ .from = 0, .from_set = true,
 *                                                 .to = 90, .duration = PSYTL_MS(300) });
 *       psytl_off(&q, DOTS_ON);
 *       ...
 *       psytl_evaluate(&tl, &(psytl_frame){ f.onset, f.period, f.index }, fired, 8);
 *       psygfx_apply(binds, 3, psytl_values(&tl));
 *       psyrdk_update(&rdk, f.onset);
 *       log_step(&rdk.last);                           // 24 bytes: what a replay needs
 *
 *   A global-motion gabor array: the elements are written straight into
 *   psy_gfx.h's instance records, with no copy:
 *
 *       psyrdk_write(&rdk, &(psyrdk_out){
 *           .xy  = &items[0].x,   .xy_stride  = sizeof items[0],
 *           .dir = &items[0].ori, .dir_stride = sizeof items[0] });
 *
 *   Transparent motion is two fields (two streams, two directions) drawn
 *   as two DOTS stimuli.
 *
 *   PsychoPy and Psychtoolbox, for comparison:
 *
 *       dots = visual.DotStim(win, nDots=200, fieldSize=400, fieldShape='circle',
 *                             coherence=0.25, dir=0, speed=2, dotLife=-1,
 *                             signalDots='same', noiseDots='direction')
 *       % Psychtoolbox: DotDemo.m moves dots by hand, xy = xy + dxdy, and
 *       % replots the dots that leave or are killed (f_kill) at random.
 *
 *   The differences: PsychoPy's speed is units per frame and its dir is
 *   counterclockwise from +x with y up; here speed is units per second
 *   (CLOCK_FRAMES gives per-frame steps) and direction turns clockwise on
 *   the screen, y down, as psy_gfx.h's ori. PsychoPy truncates the signal
 *   count (int(coherence * nDots)); EXACT rounds it. Both replot a dot that
 *   leaves at a random position, which is not density-preserving (EDGES);
 *   WRAP is the default here. Neither can regenerate a trial on another
 *   machine bit for bit.
 *
 *   ---------------------------------------------------------------------
 *   MODEL
 *   ---------------------------------------------------------------------
 *   THE FIELD
 *     A field holds sets x count dots (count shown per update; sets 1 but
 *     for MN) in an aperture centered on 0, 0 in the caller's units (px or
 *     deg), x right, y down. psyrdk_start(seed, t) places every dot at
 *     random and starts the trial's clock at t; each psyrdk_update(t)
 *     advances one set to time t; psyrdk_xy() and psyrdk_write() give the
 *     set shown. Fields are independent values: two of them make
 *     transparent motion.
 *   SIGNAL AND NOISE
 *     On each update a dot is signal or noise. A signal dot moves by
 *     speed x elapsed time along direction. A noise dot follows the noise
 *     rule (desc.noise; Scase, Braddick and Raymond 1996):
 *       PSYRDK_NOISE_DIRECTION (0)  its own direction, drawn at birth, at
 *                                   the signal speed ("random direction";
 *                                   PsychoPy's noiseDots='direction')
 *       PSYRDK_NOISE_POSITION       a random position each update ("random
 *                                   position"; the "white noise" of
 *                                   Britten, Shadlen, Newsome and Movshon
 *                                   1992)
 *       PSYRDK_NOISE_WALK           a direction drawn afresh each update,
 *                                   at the signal speed ("random walk";
 *                                   Pilly and Seitz's "Brownian")
 *     Which dots are signal (desc.signal):
 *       PSYRDK_SIGNAL_SAME (0)      the same dots all trial (Williams and
 *                                   Sekuler 1984 "same"): those with the
 *                                   smallest keys drawn at start, so a
 *                                   coherence change converts the dots at
 *                                   the boundary and a reborn dot keeps its
 *                                   role
 *       PSYRDK_SIGNAL_DIFFERENT     chosen afresh each update (Williams and
 *                                   Sekuler 1984 "different"; Britten et al.
 *                                   1992)
 *       PSYRDK_SIGNAL_LEAST_RECENT  afresh each update, but the dots that
 *                                   have been signal longest are the first
 *                                   to become noise (Law and Gold 2008, as
 *                                   Pilly and Seitz 2009 describe it;
 *                                   Snowden and Braddick 1989): below
 *                                   coherence 0.5 no dot is signal on two
 *                                   updates of its set in a row
 *     How many (desc.select):
 *       PSYRDK_EXACT (0)            round(coherence x count) every update:
 *                                   the realized coherence is the stated one
 *       PSYRDK_BERNOULLI            each dot with probability coherence, so
 *                                   the count is binomial (the Shadlen lab's
 *                                   dotsX.m: L = rand(ndots,1) < coh)
 *     In both, a dot's key is a random 32-bit word; EXACT takes the dots
 *     with the smallest keys and BERNOULLI those below coherence x 2^32.
 *     So the signal dots at a higher coherence include those at a lower
 *     one, and the noise dots of both draw the same words (FROZEN NOISE).
 *   PRESETS (desc.algorithm; Pilly and Seitz 2009's names)
 *       PSYRDK_CUSTOM (0)  the rule fields as given
 *       PSYRDK_WN    white noise       DIFFERENT, BERNOULLI, POSITION, 1 set
 *       PSYRDK_MN    Movshon/Newsome   DIFFERENT, BERNOULLI, POSITION, 3 sets
 *       PSYRDK_LL    limited lifetime  LEAST_RECENT, EXACT, POSITION, 3 sets
 *       PSYRDK_BM    Brownian motion   DIFFERENT, EXACT, WALK, 1 set
 *     A preset with any rule field (signal, select, noise, sets) set is
 *     refused, so a preset never overrides a field silently.
 *   INTERLEAVED SETS (desc.sets, 0 = 1, at most PSYRDK_MAX_SETS)
 *     Update k (1, 2, ...) moves set (k - 1) mod sets, by the time since
 *     that set's own last update, and shows only that set. With 3 sets
 *     (MN) a dot is shown every third frame and a signal dot jumps three
 *     frames' distance at the same speed (Pilly and Seitz 2009).
 *   LIFETIME (desc.lifetime in s, or desc.lifetime_frames x frame_ns)
 *     A fixed lifetime L: a dot dies at a scheduled time, and the first
 *     update at or after it places the dot at random (and draws its
 *     direction under NOISE_DIRECTION); it does not move on that update.
 *     The next death is the old one plus L (plus as many L as the update
 *     passed), not the update's time plus L, so the mean life is exactly L
 *     even when L is not a whole number of frames: lives then take the
 *     two neighboring frame counts. At start the remaining lives are
 *     uniform over (0, L]: equal ages would make every dot die on the same
 *     update, a pulse every L. 0 = unlimited.
 *     Not offered: a death probability per update (Psychtoolbox's DotDemo
 *     f_kill, geometric lives). It is defined per update, not per second,
 *     so a dropped frame or another refresh rate changes the lifetime.
 *     To replicate DotDemo, a fixed lifetime of 1 / f_kill frames under
 *     CLOCK_FRAMES has the same mean life (not the same distribution);
 *     the geometric distribution itself is not supported.
 *   EDGES (desc.edge)
 *     PSYRDK_WRAP (0) keeps a uniform density uniform, exactly:
 *       RECT    a torus: x modulo the width, y modulo the height. Each axis
 *               is a translation modulo a length, which preserves length.
 *       CIRCLE  a dot is translated modulo the length of its own chord:
 *               the line of its motion cuts the disc in a chord of length
 *               2 sqrt(R^2 - s^2), s its distance from the center, and a
 *               dot that leaves goes back along that line by the chord's
 *               length (by a multiple of it for a longer step). On each
 *               chord the motion is a rotation of a circle of that length;
 *               the chords tile the disc, so area is preserved, for every
 *               direction, per dot and per update.
 *     Wraps that are NOT density-preserving, measured in docs/psy_rdk.md:
 *       p -> -p (the point reflection) puts the dot outside for an update,
 *       and short chords lose more of their time, so density falls toward
 *       the sides parallel to the motion; re-entry at a uniform point of
 *       the circumference ignores the flux (|cos| of the angle to the
 *       motion), so dots pile up at those sides. The Shadlen lab's code
 *       moves dots in the bounding square and masks a circle: density is
 *       kept, but the shown count varies from frame to frame (RECT here and
 *       a CIRCLE aperture on the psygfx_dots stimulus give that display).
 *     PSYRDK_REPLOT places a dot that leaves at a random position (PsychoPy
 *     DotStim, Psychtoolbox DotDemo). Not density-preserving: nothing
 *     enters across the trailing edge but replots, so long-lived signal
 *     dots thin out toward it, over about speed x lifetime (the whole
 *     chord with no lifetime). At low coherence that gradient is a cue to
 *     the direction. Kept to replicate those programs.
 *     With WRAP and no lifetime a signal dot keeps its identity all trial
 *     and can be tracked: set a lifetime to prevent it.
 *   TIME (desc.clock)
 *     PSYRDK_CLOCK_ONSET (0)  an update moves its set by t minus that set's
 *              last t. Pass f.onset, the predicted onset of the frame being
 *              drawn: a dropped frame (the next onset two periods on) moves
 *              dots two periods' distance, so speed, duration and lifetimes
 *              hold in seconds. The frame that was late shows its positions
 *              a period late; nothing can repair it. Any int64 clock works:
 *              a psy_timeline.h base time runs the field at that base's rate.
 *     PSYRDK_CLOCK_FRAMES     each update counts frame_ns (desc; f.period
 *              is the usual value) whatever t says: the displacement per
 *              shown frame is fixed, which paradigms defined by displacement
 *              need (Dmax; Pilly and Seitz's spatial displacement; MN's
 *              Delta-x). A dropped frame then delays the rest of the
 *              trajectory by a frame: the trial is short of distance and the
 *              speed is wrong over that interval. t is still checked and
 *              logged.
 *     t before the last update is refused (PSYRDK_ERR_ORDER): a field is
 *     a stochastic history, so going back is a restart and a replay
 *     (psyrdk_replay), or a restore of a snapshot. An update at the same t
 *     moves nothing but still runs the selection and the noise rule (an
 *     update is an update). coherence, direction and speed hold over
 *     (last t, t]. A negative speed moves the other way; a non-finite one
 *     moves nothing; a non-finite direction is 0.
 *   FROZEN NOISE
 *     Every random word is a pure function of (seed, stream, dot, update,
 *     slot) (REPRODUCIBILITY), never of what other dots did. With the same
 *     seed at two coherence levels, a dot that is noise on an update in
 *     both runs draws the same words there: under NOISE_POSITION it lands
 *     on the same point; under WALK and DIRECTION it takes the same
 *     direction (its position also carries its history, which differs
 *     where it was signal in one run). Reverse correlation and motion
 *     energy analyses can therefore reuse one noise sequence across trials.
 *
 *   ---------------------------------------------------------------------
 *   REPRODUCIBILITY: the exact rules
 *   ---------------------------------------------------------------------
 *   Any language with 64-bit integers reproduces a field from this section
 *   (tests/compare/rdk_ref.py is such a model, in Python). No floating
 *   point takes part in the state: a float input is read as the exact
 *   rational it is, and rnd(r) = floor(r + 1/2) of that rational.
 *   LATTICE   S = 2^30. Rmax = the larger half-size (CIRCLE w / 2; RECT
 *             max(w, h) / 2) is S lattice steps. CIRCLE: x^2 + y^2 <= S^2.
 *             RECT: -hx <= x < hx, -hy <= y < hy, hx = rnd(S w / max(w, h)),
 *             hy = rnd(S h / max(w, h)). A position is two int32.
 *   ANGLES    a direction is a uint32, 2^32 per turn, from +x toward +y:
 *             angle(deg) = rnd(deg 2^32 / 360) mod 2^32. sincos(a) is Q30
 *             from the literal tables below by angle addition: quadrant a
 *             >> 30; coarse i = bits 29..22, fine j = bits 21..14, rest e =
 *             bits 13..0; sAB = rnd2(sC cF + cC sF), cAB = rnd2(cC cF - sC
 *             sF) with sC = coarse[i], cC = coarse[256 - i], sF = fine_sin[j],
 *             cF = fine_cos[j]; te = (e PIH) >> 32; s = sAB + rnd2(cAB te),
 *             c = cAB - rnd2(sAB te), where rnd2(z) = floor((z + 2^29) /
 *             2^30); then by quadrant 1: (c, -s), 2: (-s, -c), 3: (-c, s).
 *   STEP      D = rnd(|speed| dt 2^30 / (Rmax 10^9)), dt in ns, at most
 *             2^62; a step along angle a is (rnd2(D c), rnd2(D s)).
 *   COUNTS    count = desc.count, or llround(density x area) in double,
 *             area = 3.141592653589793 x r x r (CIRCLE, r = w / 2, left to
 *             right) or w x h. Lifetime L = lifetime_frames x frame_ns, or
 *             llround(lifetime x 1e9) in double. EXACT: m = rnd(c count);
 *             BERNOULLI: threshold floor(c 2^32), c clamped to 0..1.
 *   WORDS     W(dot, u, slot) = Philox4x32-10 (Salmon, Moraes, Dror and
 *             Shaw 2011) with key (seed low 32 bits, seed high 32 bits) and
 *             counter (dot, u, slot, stream): four words w0..w3. dot is the
 *             index in the field (0 .. sets x count - 1), u the update
 *             number (0 for start).
 *   PLACING   P(dot, u): tries k = 0..32; try 0 is (w2, w3) of slot 0, try
 *             k >= 1 is (w0, w1) or (w2, w3) (k odd, even) of slot
 *             (k + 1) / 2. A try (a, b) is x = ((a 2hx) >> 32) - hx, y =
 *             ((b 2hy) >> 32) - hy (hx = hy = S for CIRCLE); RECT takes the
 *             first try, CIRCLE the first with x^2 + y^2 <= S^2. If all 33
 *             fail (probability 9.7e-23 per placement) the dot goes to
 *             0, 0, counted in stats.exhausted.
 *   START     u = 0; for each dot: P(dot, 0); its direction w1 of slot 0;
 *             with a lifetime, its death t + 1 + ((w0 + 2^32 w1 of slot
 *             17) x L) >> 64. Every set's last time is t, last update 0.
 *   UPDATE    u += 1; set s = (u - 1) mod sets, its dots in index order.
 *             The clock: now = t, dt = t - T_s (ONSET); now = start t + u
 *             frame_ns, dt = (u - U_s) frame_ns (FRAMES). The
 *             signal angle is angle(direction), + 2^31 for a negative speed.
 *             Keys: SAME w0 of (dot, 0, slot 0); else w0 of (dot, u, 0).
 *             Signal: BERNOULLI key < threshold; EXACT the m dots of the set
 *             smallest by (key, index), LEAST_RECENT by (run, key, index),
 *             run = the dot's consecutive signal updates (at most 65535),
 *             then run = signal ? run + 1 : 0. Then for each dot of the set,
 *             in index order: if its death <= now: P(dot, u), a new direction
 *             (NOISE_DIRECTION: w1 of slot 0), death += L until > now, no
 *             motion. Else a signal dot steps along the signal angle; a
 *             noise dot: POSITION P(dot, u); WALK direction = w1 of slot 0,
 *             then steps; DIRECTION steps along its own. A step leaves the
 *             aperture when the point lands outside, or when D > 2S (CIRCLE).
 *             Then: REPLOT P(dot, u); WRAP RECT the torus, x' = ((x + hx +
 *             dx) mod 2hx) - hx and the same for y; WRAP
 *             CIRCLE: along = rnd2(x c + y s), e = max(0, x^2 + y^2 -
 *             along^2), h = isqrt(S^2 - e), L = 2h; u0 = clamp(along + h, 0,
 *             L), u1 = (u0 + D) mod L, p' = p + (rnd2((u1 - u0) c), rnd2((u1
 *             - u0) s)); if p' is outside, u1 is clamped to 2..L - 2 and p'
 *             computed again, and if it is still outside (or L < 4) the dot
 *             stays. T_s = t, U_s = u.
 *   DIGEST    FNV-1a 64 over, per dot: x, y, direction (uint32 little-
 *             endian), flags (one byte), run (uint16, LEAST_RECENT only),
 *             death (int64, with a lifetime only); then u, then each set's
 *             T_s and U_s (int64).
 *   Log per trial, to regenerate it: PSYRDK_VERSION_STRING (the rules are
 *   versioned), the desc (every field), seed and stream, the t of
 *   psyrdk_start, and rdk.last after each update (t, coherence, direction,
 *   speed). With fixed parameters the update times suffice; under the
 *   onset clock with t = f.onset they are the flips' targets in
 *   psy_screen.h's ring. Log psyrdk_digest() at the trial's end: a replay
 *   that gives the same digest is the same trial. Positions integrate their
 *   history, so a replay from update k needs the state at k: keep
 *   psyrdk_snapshot() at k, or run from start (a replay costs what the
 *   updates cost: docs/psy_rdk.md, "Cost").
 *
 *   ---------------------------------------------------------------------
 *   OUTPUT
 *   ---------------------------------------------------------------------
 *   psyrdk_xy() writes the shown set as x, y float pairs (psygfx_dots's
 *   buffer). psyrdk_write() writes any of x, y; direction (deg, the way the
 *   dot moved on its last update, or its own direction); age (s since its
 *   last birth by lifetime, or since start); signal (0 or 1); event
 *   (PSYRDK_EV_WRAPPED, PSYRDK_EV_PLACED: the dot jumped on its last
 *   update, so an element riding it restarts its carrier), each with a
 *   byte stride (0 = packed). x = (float)(x_lattice x Rmax 2^-30) in
 *   double. Outputs are never read back.
 *
 *   ---------------------------------------------------------------------
 *   MEMORY AND THREADS
 *   ---------------------------------------------------------------------
 *   open() allocates once, psyrdk_bytes(desc) bytes, or uses desc.mem
 *   (mem_bytes at least that, 64-byte aligned is best): per dot 13 bytes
 *   (x, y, direction, flags), plus 8 under NOISE_DIRECTION (its sine and
 *   cosine, kept), 2 with LEAST_RECENT and 8 with a lifetime; plus 20 per
 *   shown dot of scratch (the update's words and the selection's indices).
 *   10,000 dots of WN take about 330 KB. Nothing after open() allocates. Define PSYRDK_MALLOC and PSYRDK_FREE to replace malloc.
 *   A field is used by one thread at a time; two fields are independent.
 *
 *   ---------------------------------------------------------------------
 *   BUILDING
 *   ---------------------------------------------------------------------
 *   Link libm on Linux. Define PSYRDK_API to change the linkage. The state
 *   needs two's complement int64 with arithmetic right shifts (checked at
 *   compile time) and nothing of the floating-point environment: no flag
 *   (-ffast-math, -ffp-contract, /fp:fast) changes a result (the test
 *   passes built with gcc -Ofast -march=native; the finite checks read
 *   the bits).
 *
 *   LICENSE: public domain / MIT-0; the notice is at the end of the file.
 */
#ifndef PSY_RDK_H_INCLUDED
#define PSY_RDK_H_INCLUDED

#define PSYRDK_VERSION_MAJOR 0
#define PSYRDK_VERSION_MINOR 1
#define PSYRDK_VERSION_PATCH 0
#define PSYRDK_VERSION_STRING "0.1.0"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PSYRDK_API
#define PSYRDK_API extern
#endif

#define PSYRDK_OK          0
#define PSYRDK_ERR_ARG   (-1)   /* a desc field or argument; psyrdk_error() names it */
#define PSYRDK_ERR_ORDER (-2)   /* t before the last update, or no start            */
#define PSYRDK_ERR_MEM   (-3)   /* mem_bytes too small, or the allocation failed    */

#define PSYRDK_MAX_SETS 8
#define PSYRDK_MAX_DOTS (1 << 24)   /* sets x count */

/* psyrdk_write()'s event bits; also the low bits of a dot's flags. */
#define PSYRDK_EV_SIGNAL  0x1u   /* signal on its last update                   */
#define PSYRDK_EV_WRAPPED 0x2u   /* wrapped at the edge on its last update      */
#define PSYRDK_EV_PLACED  0x4u   /* placed at random: start, birth, replot, or
                                  * NOISE_POSITION                              */

typedef enum psyrdk_algorithm { PSYRDK_CUSTOM = 0, PSYRDK_WN, PSYRDK_MN, PSYRDK_LL, PSYRDK_BM } psyrdk_algorithm;
typedef enum psyrdk_signal { PSYRDK_SIGNAL_SAME = 0, PSYRDK_SIGNAL_DIFFERENT, PSYRDK_SIGNAL_LEAST_RECENT } psyrdk_signal;
typedef enum psyrdk_select { PSYRDK_EXACT = 0, PSYRDK_BERNOULLI } psyrdk_select;
typedef enum psyrdk_noise { PSYRDK_NOISE_DIRECTION = 0, PSYRDK_NOISE_POSITION, PSYRDK_NOISE_WALK } psyrdk_noise;
typedef enum psyrdk_shape { PSYRDK_CIRCLE = 0, PSYRDK_RECT } psyrdk_shape;
typedef enum psyrdk_edge { PSYRDK_WRAP = 0, PSYRDK_REPLOT } psyrdk_edge;
typedef enum psyrdk_clock { PSYRDK_CLOCK_ONSET = 0, PSYRDK_CLOCK_FRAMES } psyrdk_clock;

/* Zero fields take the defaults in brackets in MODEL. */
typedef struct psyrdk_desc {
    psyrdk_algorithm algorithm;          /* 0: the rule fields below          */
    psyrdk_shape     aperture;           /* 0 = CIRCLE                         */
    float            w, h;               /* units; CIRCLE: diameter w          */
    int              count;              /* dots shown per update, or ...      */
    float            density;            /* ... per unit^2 (exactly one)       */
    float            coherence;          /* first values of the field's floats */
    float            direction;          /* deg, +x toward +y (clockwise)      */
    float            speed;              /* units per second                   */
    psyrdk_signal    signal;
    psyrdk_select    select;
    psyrdk_noise     noise;
    psyrdk_edge      edge;
    int              sets;               /* interleaved sets; 0 = 1            */
    float            lifetime;           /* s; 0 = unlimited                   */
    int              lifetime_frames;    /* or this many frame_ns              */
    psyrdk_clock     clock;
    int64_t          frame_ns;           /* CLOCK_FRAMES, lifetime_frames      */
    uint64_t         seed;               /* psyrdk_start() takes its own       */
    uint32_t         stream;             /* a field's own words: two fields of
                                          * one seed differ by stream          */
    void*            mem;                /* NULL: one allocation in open()     */
    size_t           mem_bytes;
} psyrdk_desc;

/* The inputs of one update: what a log needs to replay it. 24 bytes. */
typedef struct psyrdk_step {
    int64_t t;
    float   coherence, direction, speed;
    int32_t set;                         /* the set it moved (out only)        */
} psyrdk_step;

/* Counts since start, for tests and logs. */
typedef struct psyrdk_stats {
    uint64_t updates;
    uint64_t signal;      /* signal dots summed over updates                 */
    uint64_t wraps;       /* WRAP at the edge                                */
    uint64_t replots;     /* REPLOT at the edge                              */
    uint64_t deaths;      /* lifetime ends                                   */
    uint64_t placed;      /* every P(): start, deaths, replots, POSITION     */
    uint64_t exhausted;   /* placements that used every try (see PLACING)    */
    uint64_t stuck;       /* chord wraps that kept the dot in place          */
} psyrdk_stats;

typedef struct psyrdk_field {
    /* Read at each update: set them, or bind them (psygfx_bind.field). */
    float coherence;                     /* 0..1                               */
    float direction;                     /* deg, +x toward +y: clockwise on the
                                          * screen (psy_gfx.h's ori)           */
    float speed;                         /* units per second                   */
    /* Read only. */
    int          n;                      /* dots shown per update              */
    int          total;                  /* n x sets                           */
    psyrdk_step  last;                   /* the last update's inputs           */
    psyrdk_stats stats;
    /* Private. */
    uint32_t magic_;
    uint8_t  algorithm_, signal_, select_, noise_, aperture_, edge_, clock_, sets_;
    uint32_t stream_;
    int      started_, shown_;
    uint64_t seed_;
    int64_t  frame_ns_, life_ns_;
    float    w_, h_, rmax_;
    int32_t  hx_, hy_;
    uint64_t u_;
    int64_t  set_t_[PSYRDK_MAX_SETS];
    int64_t  set_u_[PSYRDK_MAX_SETS];
    int64_t  t_start_;
    int64_t  now_;                       /* the clock of the last update: t, or
                                          * start + u frame_ns (FRAMES)        */
    uint64_t same_sel_[PSYRDK_MAX_SETS]; /* SAME: the m or threshold in force   */
    uint32_t dir_bits_, dir_angle_;      /* the last direction and its angle   */
    int      dir_valid_;
    uint32_t sig_angle_;                 /* the last update's signal angle     */
    int32_t* xy_;
    uint32_t* ang_;
    int32_t* cs_;                        /* NOISE_DIRECTION: sincos of ang_    */
    uint8_t* flags_;
    uint16_t* run_;
    int64_t* death_;
    uint32_t* words_;                    /* slot 0 of each shown dot, 4 words  */
    uint32_t* idx_;
    uint32_t* hist_;
    void*    mem_;
    size_t   bytes_;
    int      owns_;
    char     err_[160];
} psyrdk_field;

/* Strided outputs for the shown dots; NULL skips one; stride 0 = packed. */
typedef struct psyrdk_out {
    float*   xy;     size_t xy_stride;     /* x, y units from the center       */
    float*   dir;    size_t dir_stride;    /* deg                              */
    float*   age;    size_t age_stride;    /* s                                */
    uint8_t* signal; size_t signal_stride; /* 0 or 1                           */
    uint8_t* event;  size_t event_stride;  /* PSYRDK_EV_* bits                 */
} psyrdk_out;

/* One float of psyrdk_field a designer or a binding sets. */
typedef struct psyrdk_param {
    const char* name, *type;
    double      min, max, def;
    const char* unit, *doc;
    uint32_t    offset;                  /* in psyrdk_field                    */
} psyrdk_param;

PSYRDK_API const char* psyrdk_version(void);
PSYRDK_API const char* psyrdk_strerror(int code);
/* Bytes open() needs for desc (0 when the desc is refused). */
PSYRDK_API size_t      psyrdk_bytes(const psyrdk_desc* d);
/* Checks the desc and lays the field out; 0 or a code, psyrdk_error() says
 * why. Not started: call psyrdk_start(). */
PSYRDK_API int         psyrdk_open(psyrdk_field* f, const psyrdk_desc* d);
PSYRDK_API void        psyrdk_close(psyrdk_field* f);
PSYRDK_API const char* psyrdk_error(const psyrdk_field* f);
/* A trial: every dot placed at random from seed, the clock at t. */
PSYRDK_API int         psyrdk_start(psyrdk_field* f, uint64_t seed, int64_t t);
/* The next set at time t (ns, on any clock). Returns the dots shown (n) or
 * a code. */
PSYRDK_API int         psyrdk_update(psyrdk_field* f, int64_t t);
/* The shown set as x, y pairs (2 n floats). Returns n or a code. */
PSYRDK_API int         psyrdk_xy(const psyrdk_field* f, float* xy);
PSYRDK_API int         psyrdk_write(const psyrdk_field* f, const psyrdk_out* o);
/* FNV-1a 64 of the state (REPRODUCIBILITY, DIGEST). */
PSYRDK_API uint64_t    psyrdk_digest(const psyrdk_field* f);
/* start(seed, t0), then for each step: its coherence, direction and speed
 * into the field, update(step.t). Returns 0 or the first code. */
PSYRDK_API int         psyrdk_replay(psyrdk_field* f, uint64_t seed, int64_t t0, const psyrdk_step* steps, int n);
/* The state after an update, and back into a field opened with the same
 * desc. Bytes in, bytes out; refused for another desc. */
PSYRDK_API size_t      psyrdk_snapshot_bytes(const psyrdk_field* f);
PSYRDK_API int         psyrdk_snapshot(const psyrdk_field* f, void* out, size_t cap);
PSYRDK_API int         psyrdk_restore(psyrdk_field* f, const void* in, size_t n);
/* coherence, direction, speed; *n gets 3. */
PSYRDK_API const psyrdk_param* psyrdk_params(int* n);

/* The exact pieces (REPRODUCIBILITY), public for bindings and tests. */
PSYRDK_API void     psyrdk_philox(uint64_t seed, const uint32_t ctr[4], uint32_t out[4]);
PSYRDK_API uint32_t psyrdk_angle(float deg);
PSYRDK_API void     psyrdk_sincos(uint32_t angle, int32_t* s, int32_t* c);

#ifdef __cplusplus
}
#endif
#endif /* PSY_RDK_H_INCLUDED */

/* ======================================================================== */

#ifdef PSY_RDK_IMPLEMENTATION
#ifndef PSY_RDK_IMPLEMENTATION_GUARD
#define PSY_RDK_IMPLEMENTATION_GUARD

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef PSYRDK_MALLOC
#include <stdlib.h>
#define PSYRDK_MALLOC(n) malloc(n)
#define PSYRDK_FREE(p) free(p)
#endif

/* The state's rounding (rnd2) and the floor of negative steps rely on an
 * arithmetic right shift of int64; C leaves it to the implementation. */
typedef char psyrdk__sar_check[((int64_t)-8 >> 1) == (int64_t)-4 ? 1 : -1];

#define PSYRDK__MAGIC 0x52444b31u   /* "RDK1" */
#define PSYRDK__S     ((int64_t)1 << 30)
#define PSYRDK__NOSIG ((uint8_t)0xfeu)   /* clears PSYRDK_EV_SIGNAL */

/* Q30 sine tables (REPRODUCIBILITY, ANGLES), floor(v 2^30 + 1/2) at 50
 * digits by tests/compare/rdk_tables.py: sin(i pi/512), i = 0..256, and
 * sin, cos(j pi/131072), j = 0..255. PIH = floor(pi/2 2^32 + 1/2). */
static const int32_t psyrdk__sin_coarse[257] = {
    0, 6588356, 13176464, 19764076, 26350943, 32936819, 39521455, 46104602,
    52686014, 59265442, 65842639, 72417357, 78989349, 85558366, 92124163, 98686491,
    105245103, 111799753, 118350194, 124896179, 131437462, 137973796, 144504935, 151030634,
    157550647, 164064728, 170572633, 177074115, 183568930, 190056834, 196537583, 203010932,
    209476638, 215934457, 222384147, 228825464, 235258165, 241682010, 248096755, 254502159,
    260897982, 267283981, 273659918, 280025552, 286380643, 292724951, 299058239, 305380268,
    311690799, 317989595, 324276419, 330551034, 336813204, 343062693, 349299266, 355522689,
    361732726, 367929144, 374111709, 380280190, 386434353, 392573967, 398698801, 404808624,
    410903207, 416982319, 423045732, 429093217, 435124548, 441139496, 447137835, 453119340,
    459083786, 465030947, 470960600, 476872522, 482766489, 488642281, 494499676, 500338453,
    506158392, 511959275, 517740883, 523502998, 529245404, 534967884, 540670223, 546352205,
    552013618, 557654248, 563273883, 568872310, 574449320, 580004702, 585538248, 591049748,
    596538995, 602005783, 607449906, 612871159, 618269338, 623644239, 628995660, 634323400,
    639627258, 644907034, 650162530, 655393548, 660599890, 665781362, 670937767, 676068911,
    681174602, 686254647, 691308855, 696337036, 701339000, 706314559, 711263525, 716185713,
    721080937, 725949013, 730789757, 735602987, 740388522, 745146182, 749875788, 754577161,
    759250125, 763894504, 768510122, 773096806, 777654384, 782182683, 786681534, 791150767,
    795590213, 799999706, 804379079, 808728167, 813046808, 817334838, 821592095, 825818421,
    830013654, 834177638, 838310216, 842411232, 846480531, 850517961, 854523370, 858496606,
    862437520, 866345964, 870221790, 874064853, 877875009, 881652112, 885396022, 889106597,
    892783698, 896427186, 900036924, 903612776, 907154608, 910662286, 914135678, 917574653,
    920979082, 924348837, 927683790, 930983817, 934248793, 937478595, 940673101, 943832191,
    946955747, 950043650, 953095785, 956112036, 959092290, 962036435, 964944360, 967815955,
    970651112, 973449725, 976211688, 978936898, 981625251, 984276646, 986890984, 989468165,
    992008094, 994510675, 996975812, 999403415, 1001793390, 1004145648, 1006460100, 1008736660,
    1010975242, 1013175761, 1015338134, 1017462281, 1019548121, 1021595575, 1023604567, 1025575020,
    1027506862, 1029400018, 1031254418, 1033069992, 1034846671, 1036584389, 1038283080, 1039942680,
    1041563127, 1043144360, 1044686319, 1046188946, 1047652185, 1049075980, 1050460278, 1051805027,
    1053110176, 1054375676, 1055601479, 1056787540, 1057933813, 1059040255, 1060106826, 1061133483,
    1062120190, 1063066909, 1063973603, 1064840240, 1065666786, 1066453210, 1067199483, 1067905576,
    1068571464, 1069197120, 1069782521, 1070327646, 1070832474, 1071296985, 1071721163, 1072104991,
    1072448455, 1072751542, 1073014240, 1073236540, 1073418433, 1073559913, 1073660973, 1073721611,
    1073741824,
};
static const int32_t psyrdk__sin_fine[256] = {
    0, 25736, 51472, 77208, 102944, 128680, 154416, 180151,
    205887, 231623, 257359, 283095, 308831, 334567, 360303, 386039,
    411775, 437511, 463247, 488983, 514719, 540454, 566190, 591926,
    617662, 643398, 669134, 694870, 720606, 746342, 772078, 797814,
    823550, 849286, 875021, 900757, 926493, 952229, 977965, 1003701,
    1029437, 1055173, 1080909, 1106645, 1132381, 1158116, 1183852, 1209588,
    1235324, 1261060, 1286796, 1312532, 1338268, 1364004, 1389740, 1415476,
    1441211, 1466947, 1492683, 1518419, 1544155, 1569891, 1595627, 1621363,
    1647099, 1672835, 1698570, 1724306, 1750042, 1775778, 1801514, 1827250,
    1852986, 1878722, 1904458, 1930193, 1955929, 1981665, 2007401, 2033137,
    2058873, 2084609, 2110345, 2136081, 2161816, 2187552, 2213288, 2239024,
    2264760, 2290496, 2316232, 2341968, 2367703, 2393439, 2419175, 2444911,
    2470647, 2496383, 2522119, 2547854, 2573590, 2599326, 2625062, 2650798,
    2676534, 2702269, 2728005, 2753741, 2779477, 2805213, 2830949, 2856685,
    2882420, 2908156, 2933892, 2959628, 2985364, 3011100, 3036835, 3062571,
    3088307, 3114043, 3139779, 3165514, 3191250, 3216986, 3242722, 3268458,
    3294193, 3319929, 3345665, 3371401, 3397137, 3422872, 3448608, 3474344,
    3500080, 3525816, 3551551, 3577287, 3603023, 3628759, 3654495, 3680230,
    3705966, 3731702, 3757438, 3783173, 3808909, 3834645, 3860381, 3886116,
    3911852, 3937588, 3963324, 3989060, 4014795, 4040531, 4066267, 4092002,
    4117738, 4143474, 4169210, 4194945, 4220681, 4246417, 4272153, 4297888,
    4323624, 4349360, 4375095, 4400831, 4426567, 4452303, 4478038, 4503774,
    4529510, 4555245, 4580981, 4606717, 4632452, 4658188, 4683924, 4709660,
    4735395, 4761131, 4786867, 4812602, 4838338, 4864074, 4889809, 4915545,
    4941281, 4967016, 4992752, 5018487, 5044223, 5069959, 5095694, 5121430,
    5147166, 5172901, 5198637, 5224373, 5250108, 5275844, 5301579, 5327315,
    5353051, 5378786, 5404522, 5430257, 5455993, 5481729, 5507464, 5533200,
    5558935, 5584671, 5610407, 5636142, 5661878, 5687613, 5713349, 5739084,
    5764820, 5790556, 5816291, 5842027, 5867762, 5893498, 5919233, 5944969,
    5970704, 5996440, 6022175, 6047911, 6073646, 6099382, 6125117, 6150853,
    6176588, 6202324, 6228059, 6253795, 6279530, 6305266, 6331001, 6356737,
    6382472, 6408208, 6433943, 6459679, 6485414, 6511150, 6536885, 6562621,
};
static const int32_t psyrdk__cos_fine[256] = {
    1073741824, 1073741824, 1073741823, 1073741821, 1073741819, 1073741816, 1073741813, 1073741809,
    1073741804, 1073741799, 1073741793, 1073741787, 1073741780, 1073741772, 1073741764, 1073741755,
    1073741745, 1073741735, 1073741724, 1073741713, 1073741701, 1073741688, 1073741675, 1073741661,
    1073741646, 1073741631, 1073741616, 1073741599, 1073741582, 1073741565, 1073741546, 1073741528,
    1073741508, 1073741488, 1073741467, 1073741446, 1073741424, 1073741402, 1073741379, 1073741355,
    1073741331, 1073741306, 1073741280, 1073741254, 1073741227, 1073741199, 1073741171, 1073741143,
    1073741113, 1073741083, 1073741053, 1073741022, 1073740990, 1073740958, 1073740925, 1073740891,
    1073740857, 1073740822, 1073740786, 1073740750, 1073740714, 1073740676, 1073740638, 1073740600,
    1073740561, 1073740521, 1073740481, 1073740439, 1073740398, 1073740356, 1073740313, 1073740269,
    1073740225, 1073740180, 1073740135, 1073740089, 1073740043, 1073739995, 1073739948, 1073739899,
    1073739850, 1073739800, 1073739750, 1073739699, 1073739648, 1073739596, 1073739543, 1073739490,
    1073739436, 1073739381, 1073739326, 1073739270, 1073739213, 1073739156, 1073739099, 1073739040,
    1073738982, 1073738922, 1073738862, 1073738801, 1073738740, 1073738678, 1073738615, 1073738552,
    1073738488, 1073738424, 1073738359, 1073738293, 1073738227, 1073738160, 1073738092, 1073738024,
    1073737955, 1073737886, 1073737816, 1073737745, 1073737674, 1073737602, 1073737529, 1073737456,
    1073737383, 1073737308, 1073737233, 1073737158, 1073737082, 1073737005, 1073736927, 1073736849,
    1073736771, 1073736692, 1073736612, 1073736531, 1073736450, 1073736368, 1073736286, 1073736203,
    1073736119, 1073736035, 1073735950, 1073735865, 1073735779, 1073735692, 1073735605, 1073735517,
    1073735429, 1073735339, 1073735250, 1073735159, 1073735068, 1073734977, 1073734884, 1073734792,
    1073734698, 1073734604, 1073734509, 1073734414, 1073734318, 1073734222, 1073734124, 1073734027,
    1073733928, 1073733829, 1073733730, 1073733629, 1073733529, 1073733427, 1073733325, 1073733222,
    1073733119, 1073733015, 1073732911, 1073732805, 1073732700, 1073732593, 1073732486, 1073732378,
    1073732270, 1073732161, 1073732052, 1073731942, 1073731831, 1073731720, 1073731608, 1073731495,
    1073731382, 1073731268, 1073731154, 1073731039, 1073730923, 1073730807, 1073730690, 1073730572,
    1073730454, 1073730335, 1073730216, 1073730096, 1073729976, 1073729854, 1073729733, 1073729610,
    1073729487, 1073729363, 1073729239, 1073729114, 1073728989, 1073728862, 1073728736, 1073728608,
    1073728480, 1073728352, 1073728222, 1073728093, 1073727962, 1073727831, 1073727699, 1073727567,
    1073727434, 1073727301, 1073727166, 1073727032, 1073726896, 1073726760, 1073726624, 1073726486,
    1073726348, 1073726210, 1073726071, 1073725931, 1073725791, 1073725650, 1073725508, 1073725366,
    1073725223, 1073725080, 1073724936, 1073724791, 1073724646, 1073724500, 1073724354, 1073724206,
    1073724059, 1073723910, 1073723761, 1073723612, 1073723462, 1073723311, 1073723159, 1073723007,
    1073722855, 1073722701, 1073722547, 1073722393, 1073722238, 1073722082, 1073721926, 1073721769,
};
#define PSYRDK__PIH 6746518852ull

/* --- 128-bit helpers: the exact rational conversions ---------------------- */

typedef struct psyrdk__u128 { uint64_t hi, lo; } psyrdk__u128;

static psyrdk__u128 psyrdk__mul64(uint64_t a, uint64_t b) {
    uint64_t a0 = a & 0xffffffffu, a1 = a >> 32, b0 = b & 0xffffffffu, b1 = b >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = (p00 >> 32) + (p01 & 0xffffffffu) + (p10 & 0xffffffffu);
    psyrdk__u128 r;
    r.lo = (mid << 32) | (p00 & 0xffffffffu);
    r.hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
    return r;
}

static int psyrdk__bits128(psyrdk__u128 a) {
    int n = 0;
    uint64_t v = a.hi ? a.hi : a.lo;
    while (v) { n++; v >>= 1; }
    return a.hi ? n + 64 : n;
}

static psyrdk__u128 psyrdk__shl128(psyrdk__u128 a, int k) {
    psyrdk__u128 r;
    if (k <= 0) return a;
    if (k >= 128) { r.hi = r.lo = 0; return r; }
    if (k >= 64) { r.hi = a.lo << (k - 64); r.lo = 0; return r; }
    r.hi = (a.hi << k) | (a.lo >> (64 - k));
    r.lo = a.lo << k;
    return r;
}

static int psyrdk__ge128(psyrdk__u128 a, psyrdk__u128 b) { return a.hi > b.hi || (a.hi == b.hi && a.lo >= b.lo); }

static psyrdk__u128 psyrdk__sub128(psyrdk__u128 a, psyrdk__u128 b) {
    psyrdk__u128 r;
    r.lo = a.lo - b.lo;
    r.hi = a.hi - b.hi - (a.lo < b.lo);
    return r;
}

static psyrdk__u128 psyrdk__add128(psyrdk__u128 a, psyrdk__u128 b) {
    psyrdk__u128 r;
    r.lo = a.lo + b.lo;
    r.hi = a.hi + b.hi + (r.lo < a.lo);
    return r;
}

/* floor(n / d), d > 0, by shift and subtract: the reference, and the path
 * for a divisor of more than 64 bits. */
static psyrdk__u128 psyrdk__div128_slow(psyrdk__u128 n, psyrdk__u128 d) {
    psyrdk__u128 q = { 0, 0 }, r = { 0, 0 };
    int i, nb = psyrdk__bits128(n);
    for (i = nb - 1; i >= 0; i--) {
        uint64_t bit = i >= 64 ? (n.hi >> (i - 64)) & 1u : (n.lo >> i) & 1u;
        r = psyrdk__shl128(r, 1);
        r.lo |= bit;
        if (psyrdk__ge128(r, d)) {
            r = psyrdk__sub128(r, d);
            if (i >= 64) q.hi |= (uint64_t)1 << (i - 64); else q.lo |= (uint64_t)1 << i;
        }
    }
    return q;
}

/* floor((u1 2^64 + u0) / v) for u1 < v (Hacker's Delight, divlu: two
 * 32-bit digits after normalizing v). */
static uint64_t psyrdk__divlu(uint64_t u1, uint64_t u0, uint64_t v) {
    const uint64_t b = (uint64_t)1 << 32;
    uint64_t vn1, vn0, un1, un0, q1, q0, un32, un21, rhat;
    int sh = 0;
    while (!(v & ((uint64_t)1 << 63))) { v <<= 1; sh++; }
    un32 = sh ? (u1 << sh) | (u0 >> (64 - sh)) : u1;
    u0 <<= sh;
    vn1 = v >> 32; vn0 = v & 0xffffffffu;
    un1 = u0 >> 32; un0 = u0 & 0xffffffffu;
    q1 = un32 / vn1;
    rhat = un32 - q1 * vn1;
    while (q1 >= b || q1 * vn0 > b * rhat + un1) { q1--; rhat += vn1; if (rhat >= b) break; }
    un21 = un32 * b + un1 - q1 * v;
    q0 = un21 / vn1;
    rhat = un21 - q0 * vn1;
    while (q0 >= b || q0 * vn0 > b * rhat + un0) { q0--; rhat += vn1; if (rhat >= b) break; }
    return q1 * b + q0;
}

static psyrdk__u128 psyrdk__div128(psyrdk__u128 n, psyrdk__u128 d) {
    psyrdk__u128 q;
    if (d.hi || !d.lo) return psyrdk__div128_slow(n, d);
    if (d.lo == 1) return n;
    q.hi = n.hi / d.lo;
    q.lo = psyrdk__divlu(n.hi % d.lo, n.lo, d.lo);
    return q;
}

/* floor(n / d + 1/2) for n >= 0, d > 0. */
static psyrdk__u128 psyrdk__rnd128(psyrdk__u128 n, psyrdk__u128 d) {
    return psyrdk__div128(psyrdk__add128(psyrdk__shl128(n, 1), d), psyrdk__shl128(d, 1));
}

/* A finite float as m 2^e exactly, |m| < 2^24. */
static void psyrdk__split(float v, int64_t* m, int* e) {
    int ex = 0;
    double fr = frexp((double)v, &ex);
    *m = (int64_t)ldexp(fr, 24);
    *e = ex - 24;
}

/* By the bits, so -ffast-math cannot fold it away. */
static int psyrdk__finite(float v) {
    uint32_t b;
    memcpy(&b, &v, sizeof b);
    return (b & 0x7f800000u) != 0x7f800000u;
}

/* floor(a / b) for b > 0. */
static int64_t psyrdk__fdiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}

static int64_t psyrdk__fmod(int64_t a, int64_t b) {
    int64_t r = a % b;
    return r < 0 ? r + b : r;
}

static uint64_t psyrdk__mulmod(uint64_t a, uint64_t b, uint64_t m) {
    psyrdk__u128 p = psyrdk__mul64(a, b), q, mm;
    mm.hi = 0; mm.lo = m;
    q = psyrdk__div128(p, mm);
    p = psyrdk__sub128(p, psyrdk__mul64(q.lo, m));
    return p.lo;
}

PSYRDK_API uint32_t psyrdk_angle(float deg) {
    /* rnd(deg 2^32 / 360) mod 2^32 = floor((2 X + 360) / 720) mod 2^32 with
     * X = m 2^(e + 32); reduced modulo K = 720 2^32, so any float works. */
    const uint64_t K = (uint64_t)720 << 32;
    int64_t m;
    int e, p;
    if (!psyrdk__finite(deg)) return 0;
    psyrdk__split(deg, &m, &e);
    p = e + 33;
    if (p >= 0) {
        uint64_t pw = 1, r;
        int i;
        for (i = 0; i < p; i++) pw = (pw << 1) % K;
        r = psyrdk__mulmod((uint64_t)(m < 0 ? -m : m), pw, K);
        if (m < 0 && r) r = K - r;
        r = (r + 360) % K;
        return (uint32_t)(r / 720);
    } else {
        int k = -p;
        int64_t q;
        if (k > 40) return 0;
        q = psyrdk__fdiv(m + ((int64_t)360 << k), (int64_t)720 << k);
        return (uint32_t)(uint64_t)q;
    }
}

PSYRDK_API void psyrdk_sincos(uint32_t a, int32_t* s_out, int32_t* c_out) {
    uint32_t r = a & 0x3fffffffu;
    int i = (int)(r >> 22), j = (int)((r >> 14) & 255u);
    int64_t e = (int64_t)(r & 16383u);
    int64_t sC = psyrdk__sin_coarse[i], cC = psyrdk__sin_coarse[256 - i];
    int64_t sF = psyrdk__sin_fine[j], cF = psyrdk__cos_fine[j];
    int64_t sAB = (sC * cF + cC * sF + ((int64_t)1 << 29)) >> 30;
    int64_t cAB = (cC * cF - sC * sF + ((int64_t)1 << 29)) >> 30;
    int64_t te = (int64_t)(((uint64_t)e * PSYRDK__PIH) >> 32);
    int64_t s = sAB + ((cAB * te + ((int64_t)1 << 29)) >> 30);
    int64_t c = cAB - ((sAB * te + ((int64_t)1 << 29)) >> 30);
    switch (a >> 30) {
    case 0: *s_out = (int32_t)s; *c_out = (int32_t)c; break;
    case 1: *s_out = (int32_t)c; *c_out = (int32_t)-s; break;
    case 2: *s_out = (int32_t)-s; *c_out = (int32_t)-c; break;
    default: *s_out = (int32_t)-c; *c_out = (int32_t)s; break;
    }
}

/* Philox4x32-10 (Salmon, Moraes, Dror and Shaw 2011; Random123's constants). */
static void psyrdk__philox(uint32_t k0, uint32_t k1, uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3, uint32_t out[4]) {
    int r;
    for (r = 0; r < 10; r++) {
        uint64_t p0 = (uint64_t)0xD2511F53u * c0, p1 = (uint64_t)0xCD9E8D57u * c2;
        uint32_t n0 = (uint32_t)(p1 >> 32) ^ c1 ^ k0, n2 = (uint32_t)(p0 >> 32) ^ c3 ^ k1;
        c1 = (uint32_t)p1; c3 = (uint32_t)p0; c0 = n0; c2 = n2;
        k0 += 0x9E3779B9u; k1 += 0xBB67AE85u;
    }
    out[0] = c0; out[1] = c1; out[2] = c2; out[3] = c3;
}


PSYRDK_API void psyrdk_philox(uint64_t seed, const uint32_t ctr[4], uint32_t out[4]) {
    psyrdk__philox((uint32_t)seed, (uint32_t)(seed >> 32), ctr[0], ctr[1], ctr[2], ctr[3], out);
}

/* --- errors and the desc -------------------------------------------------- */

PSYRDK_API const char* psyrdk_version(void) { return PSYRDK_VERSION_STRING; }

PSYRDK_API const char* psyrdk_strerror(int code) {
    switch (code) {
    case PSYRDK_OK: return "ok";
    case PSYRDK_ERR_ARG: return "bad argument";
    case PSYRDK_ERR_ORDER: return "out of order (no start, or t before the last update)";
    case PSYRDK_ERR_MEM: return "not enough memory";
    default: return "unknown code";
    }
}

PSYRDK_API const char* psyrdk_error(const psyrdk_field* f) { return f ? f->err_ : "no field"; }

static int psyrdk__fail(psyrdk_field* f, int code, const char* msg) {
    if (f) {
        size_t n = strlen(msg);
        if (n >= sizeof f->err_) n = sizeof f->err_ - 1;
        memcpy(f->err_, msg, n);
        f->err_[n] = 0;
    }
    return code;
}

/* The resolved desc: rules after the preset, sizes, counts. */
typedef struct psyrdk__plan {
    int signal, select, noise, sets, aperture, edge, clock, n, total;
    float w, h, rmax;
    int32_t hx, hy;
    int64_t life_ns, frame_ns;
    size_t off_xy, off_ang, off_cs, off_flags, off_run, off_death, off_key, off_idx, off_hist, bytes;
} psyrdk__plan;

static int64_t psyrdk__half_lattice(float a, float b) {
    /* rnd(S a / b), 0 < a <= b */
    int64_t ma, mb;
    int ea, eb, p;
    psyrdk__u128 num, den;
    psyrdk__split(a, &ma, &ea);
    psyrdk__split(b, &mb, &eb);
    p = 30 + ea - eb;
    num.hi = 0; num.lo = (uint64_t)ma;
    den.hi = 0; den.lo = (uint64_t)mb;
    if (p >= 0) num = psyrdk__shl128(num, p); else if (-p < 100) den = psyrdk__shl128(den, -p); else return 0;
    return (int64_t)psyrdk__rnd128(num, den).lo;
}

static size_t psyrdk__up64(size_t v) { return (v + 63) & ~(size_t)63; }

static int psyrdk__plan_desc(const psyrdk_desc* d, psyrdk__plan* p, char* why, size_t cap) {
    double area;
    size_t o;
#define PSYRDK__NO(msg) do { snprintf(why, cap, "%s", msg); return PSYRDK_ERR_ARG; } while (0)
    memset(p, 0, sizeof *p);
    if (!d) PSYRDK__NO("desc is NULL");
    if ((int)d->algorithm < 0 || (int)d->algorithm > PSYRDK_BM) PSYRDK__NO("algorithm: not a psyrdk_algorithm");
    if ((int)d->signal < 0 || (int)d->signal > PSYRDK_SIGNAL_LEAST_RECENT) PSYRDK__NO("signal: not a psyrdk_signal");
    if ((int)d->select < 0 || (int)d->select > PSYRDK_BERNOULLI) PSYRDK__NO("select: not a psyrdk_select");
    if ((int)d->noise < 0 || (int)d->noise > PSYRDK_NOISE_WALK) PSYRDK__NO("noise: not a psyrdk_noise");
    if ((int)d->aperture < 0 || (int)d->aperture > PSYRDK_RECT) PSYRDK__NO("aperture: CIRCLE or RECT");
    if ((int)d->edge < 0 || (int)d->edge > PSYRDK_REPLOT) PSYRDK__NO("edge: WRAP or REPLOT");
    if ((int)d->clock < 0 || (int)d->clock > PSYRDK_CLOCK_FRAMES) PSYRDK__NO("clock: ONSET or FRAMES");
    if (d->algorithm != PSYRDK_CUSTOM) {
        if (d->signal || d->select || d->noise || d->sets)
            PSYRDK__NO("a preset sets signal, select, noise and sets: leave them 0 (or use PSYRDK_CUSTOM)");
        switch (d->algorithm) {
        case PSYRDK_WN: p->signal = PSYRDK_SIGNAL_DIFFERENT; p->select = PSYRDK_BERNOULLI; p->noise = PSYRDK_NOISE_POSITION; p->sets = 1; break;
        case PSYRDK_MN: p->signal = PSYRDK_SIGNAL_DIFFERENT; p->select = PSYRDK_BERNOULLI; p->noise = PSYRDK_NOISE_POSITION; p->sets = 3; break;
        case PSYRDK_LL: p->signal = PSYRDK_SIGNAL_LEAST_RECENT; p->select = PSYRDK_EXACT; p->noise = PSYRDK_NOISE_POSITION; p->sets = 3; break;
        default: p->signal = PSYRDK_SIGNAL_DIFFERENT; p->select = PSYRDK_EXACT; p->noise = PSYRDK_NOISE_WALK; p->sets = 1; break;
        }
    } else {
        p->signal = d->signal; p->select = d->select; p->noise = d->noise;
        p->sets = d->sets ? d->sets : 1;
    }
    if (p->sets < 1 || p->sets > PSYRDK_MAX_SETS) PSYRDK__NO("sets: 0 (one set) to PSYRDK_MAX_SETS");
    if (p->signal == PSYRDK_SIGNAL_LEAST_RECENT && p->select == PSYRDK_BERNOULLI)
        PSYRDK__NO("LEAST_RECENT chooses an exact count: select must be EXACT");
    p->aperture = d->aperture; p->edge = d->edge; p->clock = d->clock;
    if (!psyrdk__finite(d->w) || d->w <= 0) PSYRDK__NO("w: the aperture's width (CIRCLE: diameter) must be > 0");
    if (d->w < 1e-30f || d->w > 1e30f) PSYRDK__NO("w: out of range (1e-30 to 1e30)");
    if (d->aperture == PSYRDK_RECT) {
        if (!psyrdk__finite(d->h) || d->h <= 0) PSYRDK__NO("h: a RECT aperture needs a height > 0");
        if (d->h < 1e-30f || d->h > 1e30f) PSYRDK__NO("h: out of range (1e-30 to 1e30)");
        p->w = d->w; p->h = d->h;
    } else {
        if (d->h != 0 && d->h != d->w) PSYRDK__NO("h: a CIRCLE has a diameter w only (leave h 0)");
        p->w = p->h = d->w;
    }
    p->rmax = (p->w > p->h ? p->w : p->h) * 0.5f;
    if (p->aperture == PSYRDK_RECT) {
        float mx = p->w > p->h ? p->w : p->h;
        p->hx = (int32_t)psyrdk__half_lattice(p->w, mx);
        p->hy = (int32_t)psyrdk__half_lattice(p->h, mx);
        if (p->hx < 1 || p->hy < 1) PSYRDK__NO("w, h: the aspect ratio is beyond 2^30");
    } else {
        p->hx = p->hy = (int32_t)PSYRDK__S;
    }
    if ((d->count != 0) == (d->density != 0)) PSYRDK__NO("count or density: set exactly one");
    if (d->count) {
        if (d->count < 1) PSYRDK__NO("count: must be > 0");
        p->n = d->count;
    } else {
        if (!psyrdk__finite(d->density) || d->density < 0) PSYRDK__NO("density: must be > 0");
        if (p->aperture == PSYRDK_CIRCLE) {
            double r = (double)(p->w * 0.5f);
            area = 3.141592653589793 * r * r;
        } else {
            area = (double)p->w * (double)p->h;
        }
        area = (double)d->density * area;
        if (!(area >= 0.5) || area > (double)PSYRDK_MAX_DOTS) PSYRDK__NO("density: gives no dot, or more than PSYRDK_MAX_DOTS");
        p->n = (int)llround(area);
    }
    if ((int64_t)p->n * p->sets > PSYRDK_MAX_DOTS) PSYRDK__NO("count x sets: more than PSYRDK_MAX_DOTS");
    p->total = p->n * p->sets;
    if (d->frame_ns < 0) PSYRDK__NO("frame_ns: must be >= 0");
    p->frame_ns = d->frame_ns;
    if (p->clock == PSYRDK_CLOCK_FRAMES && d->frame_ns <= 0) PSYRDK__NO("frame_ns: CLOCK_FRAMES needs the time per update");
    if (d->lifetime != 0 && d->lifetime_frames != 0) PSYRDK__NO("lifetime or lifetime_frames: set at most one");
    if (d->lifetime_frames) {
        if (d->lifetime_frames < 0) PSYRDK__NO("lifetime_frames: must be > 0");
        if (d->frame_ns <= 0) PSYRDK__NO("lifetime_frames: needs frame_ns");
        if (d->frame_ns > ((int64_t)1 << 62) / d->lifetime_frames) PSYRDK__NO("lifetime_frames x frame_ns: too long");
        p->life_ns = (int64_t)d->lifetime_frames * d->frame_ns;
    } else if (d->lifetime != 0) {
        if (!psyrdk__finite(d->lifetime) || d->lifetime < 0 || d->lifetime > 1e9f) PSYRDK__NO("lifetime: seconds, > 0");
        p->life_ns = (int64_t)llround((double)d->lifetime * 1e9);
        if (p->life_ns < 1) PSYRDK__NO("lifetime: shorter than 1 ns");
    }
    /* SoA, each array on its own 64-byte line. */
    o = 0;
    p->off_xy = o;    o = psyrdk__up64(o + (size_t)p->total * 8);
    p->off_ang = o;   o = psyrdk__up64(o + (size_t)p->total * 4);
    p->off_cs = o;    if (p->noise == PSYRDK_NOISE_DIRECTION) o = psyrdk__up64(o + (size_t)p->total * 8);
    p->off_flags = o; o = psyrdk__up64(o + (size_t)p->total);
    p->off_run = o;   if (p->signal == PSYRDK_SIGNAL_LEAST_RECENT) o = psyrdk__up64(o + (size_t)p->total * 2);
    p->off_death = o; if (p->life_ns) o = psyrdk__up64(o + (size_t)p->total * 8);
    p->off_key = o;   o = psyrdk__up64(o + (size_t)p->n * 16);
    p->off_idx = o;   o = psyrdk__up64(o + (size_t)p->n * 4);
    p->off_hist = o;  o = psyrdk__up64(o + 256 * 4);
    p->bytes = o + 64;   /* room to align a caller's block */
    return PSYRDK_OK;
#undef PSYRDK__NO
}

PSYRDK_API size_t psyrdk_bytes(const psyrdk_desc* d) {
    psyrdk__plan p;
    char why[160];
    return psyrdk__plan_desc(d, &p, why, sizeof why) < 0 ? 0 : p.bytes;
}

PSYRDK_API int psyrdk_open(psyrdk_field* f, const psyrdk_desc* d) {
    psyrdk__plan p;
    char why[160];
    unsigned char* base;
    int rc;
    if (!f) return PSYRDK_ERR_ARG;
    memset(f, 0, sizeof *f);
    rc = psyrdk__plan_desc(d, &p, why, sizeof why);
    if (rc < 0) return psyrdk__fail(f, rc, why);
    if (d->mem) {
        if (d->mem_bytes < p.bytes) return psyrdk__fail(f, PSYRDK_ERR_MEM, "mem_bytes: less than psyrdk_bytes(desc)");
        f->mem_ = d->mem;
        f->owns_ = 0;
    } else {
        f->mem_ = PSYRDK_MALLOC(p.bytes);
        if (!f->mem_) return psyrdk__fail(f, PSYRDK_ERR_MEM, "the allocation failed");
        f->owns_ = 1;
    }
    f->bytes_ = p.bytes;
    base = (unsigned char*)f->mem_;
    base += (64 - (size_t)((uintptr_t)base & 63)) & 63;
    memset(base, 0, p.bytes - 64);
    f->xy_ = (int32_t*)(void*)(base + p.off_xy);
    f->ang_ = (uint32_t*)(void*)(base + p.off_ang);
    f->cs_ = p.noise == PSYRDK_NOISE_DIRECTION ? (int32_t*)(void*)(base + p.off_cs) : NULL;
    f->flags_ = base + p.off_flags;
    f->run_ = p.signal == PSYRDK_SIGNAL_LEAST_RECENT ? (uint16_t*)(void*)(base + p.off_run) : NULL;
    f->death_ = p.life_ns ? (int64_t*)(void*)(base + p.off_death) : NULL;
    f->words_ = (uint32_t*)(void*)(base + p.off_key);
    f->idx_ = (uint32_t*)(void*)(base + p.off_idx);
    f->hist_ = (uint32_t*)(void*)(base + p.off_hist);
    f->algorithm_ = (uint8_t)d->algorithm;
    f->signal_ = (uint8_t)p.signal; f->select_ = (uint8_t)p.select; f->noise_ = (uint8_t)p.noise;
    f->aperture_ = (uint8_t)p.aperture; f->edge_ = (uint8_t)p.edge; f->clock_ = (uint8_t)p.clock;
    f->sets_ = (uint8_t)p.sets;
    f->stream_ = d->stream;
    f->seed_ = d->seed;
    f->frame_ns_ = p.frame_ns; f->life_ns_ = p.life_ns;
    f->w_ = p.w; f->h_ = p.h; f->rmax_ = p.rmax;
    f->hx_ = p.hx; f->hy_ = p.hy;
    f->n = p.n; f->total = p.total;
    f->coherence = d->coherence; f->direction = d->direction; f->speed = d->speed;
    f->magic_ = PSYRDK__MAGIC;
    return PSYRDK_OK;
}

PSYRDK_API void psyrdk_close(psyrdk_field* f) {
    if (!f) return;
    if (f->owns_ && f->mem_) PSYRDK_FREE(f->mem_);
    memset(f, 0, sizeof *f);
}

/* --- the motion ------------------------------------------------------------ */

static void psyrdk__words(const psyrdk_field* f, uint32_t dot, uint32_t u, uint32_t slot, uint32_t w[4]) {
    psyrdk__philox((uint32_t)f->seed_, (uint32_t)(f->seed_ >> 32), dot, u, slot, f->stream_, w);
}

/* Slot 0 of dots lo .. lo + n - 1 at update u into out (4 words a dot). */
static void psyrdk__words_n(const psyrdk_field* f, uint32_t lo, int n, uint32_t u, uint32_t* out) {
    uint32_t k0 = (uint32_t)f->seed_, k1 = (uint32_t)(f->seed_ >> 32);
    int i;
    /* One call at a time: four chains interleaved in one loop and an SSE2
     * form were measured no faster (docs/psy_rdk.md). */
    for (i = 0; i < n; i++) psyrdk__philox(k0, k1, lo + (uint32_t)i, u, 0, f->stream_, out + 4 * i);
}

/* P(dot, u); w0 holds slot 0's words when the caller has them. */
static void psyrdk__place(psyrdk_field* f, uint32_t dot, uint32_t u, const uint32_t w0[4]) {
    int64_t hx = f->hx_, hy = f->hy_;
    int32_t* p = f->xy_ + 2 * (size_t)dot;
    uint32_t w[4];
    int k;
    f->stats.placed++;
    for (k = 0; k <= 32; k++) {
        uint32_t a, b;
        int64_t x, y;
        if (k == 0) { a = w0[2]; b = w0[3]; }
        else {
            if (k & 1) psyrdk__words(f, dot, u, (uint32_t)((k + 1) / 2), w);
            a = (k & 1) ? w[0] : w[2];
            b = (k & 1) ? w[1] : w[3];
        }
        x = (int64_t)(((uint64_t)a * (uint64_t)(2 * hx)) >> 32) - hx;
        y = (int64_t)(((uint64_t)b * (uint64_t)(2 * hy)) >> 32) - hy;
        if (f->aperture_ == PSYRDK_RECT || (uint64_t)(x * x) + (uint64_t)(y * y) <= (uint64_t)PSYRDK__S * (uint64_t)PSYRDK__S) {
            p[0] = (int32_t)x; p[1] = (int32_t)y;
            return;
        }
    }
    f->stats.exhausted++;
    p[0] = 0; p[1] = 0;
}

/* rnd2(a c) for any |a| <= 2^62, |c| <= 2^30. */
static int64_t psyrdk__mulq30(int64_t a, int32_t c) {
    if (a < ((int64_t)1 << 32) && a > -((int64_t)1 << 32)) return (a * c + ((int64_t)1 << 29)) >> 30;
    if (c == 0) return 0;
    {
        int neg = (a < 0) != (c < 0);
        uint64_t ua = (uint64_t)(a < 0 ? -a : a), uc = (uint64_t)(c < 0 ? -(int64_t)c : (int64_t)c);
        psyrdk__u128 pr = psyrdk__mul64(ua, uc), half = { 0, (uint64_t)1 << 29 };
        uint64_t q;
        if (neg) {
            /* floor((-P + 2^29) / 2^30) = -ceil((P - 2^29) / 2^30) */
            psyrdk__u128 t = psyrdk__sub128(pr, half);
            uint64_t fl = (t.hi << 34) | (t.lo >> 30);
            q = fl + ((t.lo & 0x3fffffffu) != 0);
            return -(int64_t)q;
        }
        pr = psyrdk__add128(pr, half);
        q = (pr.hi << 34) | (pr.lo >> 30);
        return (int64_t)q;
    }
}

static uint64_t psyrdk__isqrt(uint64_t n) {
    uint64_t r = (uint64_t)sqrt((double)n);
    while (r > 0 && r * r > n) r--;
    while ((r + 1) * (r + 1) <= n) r++;
    return r;
}

static void psyrdk__replot(psyrdk_field* f, uint32_t i, uint32_t u, const uint32_t* w0) {
    uint32_t w[4];
    if (!w0) { psyrdk__words(f, i, u, 0, w); w0 = w; }
    psyrdk__place(f, i, u, w0);
    f->flags_[i] |= PSYRDK_EV_PLACED;
    f->stats.replots++;
}

/* Steps dot i by D along (c, s) under the edge rule; w0 is its slot 0 at
 * update u, or NULL. */
static void psyrdk__move(psyrdk_field* f, uint32_t i, uint32_t u, int32_t s, int32_t c, int64_t D, const uint32_t* w0) {
    int32_t* p = f->xy_ + 2 * (size_t)i;
    int64_t x = p[0], y = p[1], dx, dy, qx, qy;
    if (D == 0) return;
    dx = psyrdk__mulq30(D, c);
    dy = psyrdk__mulq30(D, s);
    if (f->aperture_ == PSYRDK_RECT) {
        int64_t hx = f->hx_, hy = f->hy_;
        qx = x + dx; qy = y + dy;
        if (qx >= -hx && qx < hx && qy >= -hy && qy < hy) { p[0] = (int32_t)qx; p[1] = (int32_t)qy; return; }
        if (f->edge_ == PSYRDK_REPLOT) { psyrdk__replot(f, i, u, w0); return; }
        p[0] = (int32_t)(psyrdk__fmod(x + hx + psyrdk__fmod(dx, 2 * hx), 2 * hx) - hx);
        p[1] = (int32_t)(psyrdk__fmod(y + hy + psyrdk__fmod(dy, 2 * hy), 2 * hy) - hy);
        f->flags_[i] |= PSYRDK_EV_WRAPPED;
        f->stats.wraps++;
        return;
    }
    {
        const int64_t S = PSYRDK__S;
        int out = 1;
        if (D <= 2 * S) {
            qx = x + dx; qy = y + dy;
            if (qx >= -S && qx <= S && qy >= -S && qy <= S && qx * qx + qy * qy <= S * S) {
                p[0] = (int32_t)qx; p[1] = (int32_t)qy;
                out = 0;
            }
        }
        if (!out) return;
        if (f->edge_ == PSYRDK_REPLOT) { psyrdk__replot(f, i, u, w0); return; }
        {
            int64_t along = (x * c + y * s + ((int64_t)1 << 29)) >> 30;
            int64_t e = x * x + y * y - along * along, h, L, u0, u1, nx, ny;
            if (e < 0) e = 0;
            h = e >= S * S ? 0 : (int64_t)psyrdk__isqrt((uint64_t)(S * S - e));
            L = 2 * h;
            f->flags_[i] |= PSYRDK_EV_WRAPPED;
            f->stats.wraps++;
            if (L < 4) { f->stats.stuck++; return; }
            u0 = along + h;
            if (u0 < 0) u0 = 0;
            if (u0 > L) u0 = L;
            u1 = (u0 + D % L) % L;
            nx = x + (((u1 - u0) * c + ((int64_t)1 << 29)) >> 30);
            ny = y + (((u1 - u0) * s + ((int64_t)1 << 29)) >> 30);
            if (nx * nx + ny * ny > S * S) {
                u1 = u1 < 2 ? 2 : (u1 > L - 2 ? L - 2 : u1);
                nx = x + (((u1 - u0) * c + ((int64_t)1 << 29)) >> 30);
                ny = y + (((u1 - u0) * s + ((int64_t)1 << 29)) >> 30);
                if (nx * nx + ny * ny > S * S) { f->stats.stuck++; return; }
            }
            p[0] = (int32_t)nx; p[1] = (int32_t)ny;
        }
    }
}

/* D = rnd(|v| dt 2^30 / (Rmax 10^9)), at most 2^62. */
static int64_t psyrdk__step(const psyrdk_field* f, float v, int64_t dt) {
    int64_t mv, mr;
    int ev, er, p;
    psyrdk__u128 num, den, q;
    if (dt <= 0 || v == 0 || !psyrdk__finite(v)) return 0;
    if (v < 0) v = -v;
    psyrdk__split(v, &mv, &ev);
    psyrdk__split(f->rmax_, &mr, &er);
    num = psyrdk__mul64((uint64_t)mv, (uint64_t)dt);
    den = psyrdk__mul64((uint64_t)mr, 1000000000u);
    p = 30 + ev - er;
    if (p > 0) {
        if (psyrdk__bits128(num) + p > 126) return (int64_t)1 << 62;
        num = psyrdk__shl128(num, p);
    } else if (p < 0) {
        if (psyrdk__bits128(den) - p > 126) return 0;
        den = psyrdk__shl128(den, -p);
    }
    q = psyrdk__rnd128(num, den);
    if (q.hi || q.lo > ((uint64_t)1 << 62)) return (int64_t)1 << 62;
    return (int64_t)q.lo;
}

/* The key of shown dot i: w0 of its slot 0 in the scratch. */
#define PSYRDK__KEY(f, i) ((f)->words_[4 * (size_t)(i)])

/* Digit d of shown dot i's sort key: run high, run low, key bytes 3..0. */
static uint32_t psyrdk__digit(const psyrdk_field* f, uint32_t lo, uint32_t i, int d) {
    if (d < 2) return (uint32_t)(f->run_[lo + i] >> (8 * (1 - d))) & 255u;
    return (PSYRDK__KEY(f, i) >> (8 * (5 - d))) & 255u;
}

/* Signal flags for the set's dots [lo, lo + n): the m smallest by (run,
 * key, index), by radix selection: a histogram of one byte of the key at a
 * time, top first, narrowed to the bucket that holds the m-th. Two passes
 * over the set and a few over the bucket, whatever the count. */
static void psyrdk__select(psyrdk_field* f, uint32_t lo, int m) {
    uint32_t* idx = f->idx_;
    uint32_t* hist = f->hist_;
    const uint16_t* run = f->run_;
    int n = f->n, i, nc = n, need = m, digit, all = 1, taken = 0;
    uint32_t prefix_run = 0, prefix_key = 0;
    if (m <= 0 || m >= n) {
        uint8_t on = m > 0 ? PSYRDK_EV_SIGNAL : 0;
        for (i = 0; i < n; i++) f->flags_[lo + (uint32_t)i] = (uint8_t)((f->flags_[lo + (uint32_t)i] & PSYRDK__NOSIG) | on);
        return;
    }
    for (digit = run ? 0 : 2; digit < 6; digit++) {
        int b, below = 0, k, w = 0;
        memset(hist, 0, 256 * sizeof hist[0]);
        if (all) for (i = 0; i < n; i++) hist[psyrdk__digit(f, lo, (uint32_t)i, digit)]++;
        else for (k = 0; k < nc; k++) hist[psyrdk__digit(f, lo, idx[k], digit)]++;
        for (b = 0; b < 255; b++) {
            if (below + (int)hist[b] >= need) break;
            below += (int)hist[b];
        }
        need -= below;
        if (digit < 2) prefix_run |= (uint32_t)b << (8 * (1 - digit));
        else prefix_key |= (uint32_t)b << (8 * (5 - digit));
        /* keep the candidates in bucket b, in index order; a digit that
         * every candidate shares (the high byte of a run) filters nothing */
        if ((int)hist[b] == nc) continue;
        if (all) {
            for (i = 0; i < n; i++) if ((int)psyrdk__digit(f, lo, (uint32_t)i, digit) == b) idx[w++] = (uint32_t)i;
            all = 0;
        } else {
            for (k = 0; k < nc; k++) if ((int)psyrdk__digit(f, lo, idx[k], digit) == b) idx[w++] = idx[k];
        }
        nc = w;
    }
    /* Below the boundary (prefix_run, prefix_key) is signal; at it, the
     * first `need` in index order. */
    for (i = 0; i < n; i++) {
        uint32_t r = run ? run[lo + (uint32_t)i] : 0u, kv = PSYRDK__KEY(f, i);
        int sig;
        if (r != prefix_run) sig = r < prefix_run;
        else if (kv != prefix_key) sig = kv < prefix_key;
        else sig = taken++ < need;
        f->flags_[lo + (uint32_t)i] = (uint8_t)((f->flags_[lo + (uint32_t)i] & PSYRDK__NOSIG) | (sig ? PSYRDK_EV_SIGNAL : 0));
    }
}

static void psyrdk__bernoulli(psyrdk_field* f, uint32_t lo, uint64_t thr) {
    int i;
    for (i = 0; i < f->n; i++) {
        uint8_t on = (uint64_t)PSYRDK__KEY(f, i) < thr ? PSYRDK_EV_SIGNAL : 0;
        f->flags_[lo + (uint32_t)i] = (uint8_t)((f->flags_[lo + (uint32_t)i] & PSYRDK__NOSIG) | on);
    }
}

PSYRDK_API int psyrdk_start(psyrdk_field* f, uint64_t seed, int64_t t) {
    int s;
    uint32_t i;
    if (!f || f->magic_ != PSYRDK__MAGIC) return PSYRDK_ERR_ARG;
    f->seed_ = seed;
    memset(&f->stats, 0, sizeof f->stats);
    for (i = 0; i < (uint32_t)f->total; i++) {
        uint32_t w[4];
        psyrdk__words(f, i, 0, 0, w);
        psyrdk__place(f, i, 0, w);
        f->ang_[i] = w[1];
        if (f->cs_) psyrdk_sincos(w[1], &f->cs_[2 * i + 1], &f->cs_[2 * i]);
        f->flags_[i] = PSYRDK_EV_PLACED;
        if (f->run_) f->run_[i] = 0;
        if (f->death_) {
            uint32_t v[4];
            psyrdk__u128 pr;
            psyrdk__words(f, i, 0, 17, v);
            pr = psyrdk__mul64((uint64_t)v[0] | ((uint64_t)v[1] << 32), (uint64_t)f->life_ns_);
            f->death_[i] = t + 1 + (int64_t)pr.hi;
        }
    }
    for (s = 0; s < PSYRDK_MAX_SETS; s++) { f->set_t_[s] = t; f->set_u_[s] = 0; f->same_sel_[s] = UINT64_MAX; }
    f->u_ = 0;
    f->t_start_ = t;
    f->now_ = t;
    f->dir_valid_ = 0;
    f->sig_angle_ = psyrdk_angle(f->direction);
    if (f->speed < 0) f->sig_angle_ += 0x80000000u;
    f->started_ = 1;
    f->shown_ = 0;
    memset(&f->last, 0, sizeof f->last);
    f->last.t = t;
    return PSYRDK_OK;
}

PSYRDK_API int psyrdk_update(psyrdk_field* f, int64_t t) {
    int s, n, m = 0, i, have_words;
    uint32_t lo, u, sig_angle, dir_bits;
    int32_t ss, sc;
    int64_t dt, D, now;
    float c, v;
    uint64_t thr = 0;
    uint32_t* words;
    if (!f || f->magic_ != PSYRDK__MAGIC) return PSYRDK_ERR_ARG;
    if (!f->started_) return psyrdk__fail(f, PSYRDK_ERR_ORDER, "update before psyrdk_start()");
    if (f->u_ >= 0xffffffffu) return psyrdk__fail(f, PSYRDK_ERR_ORDER, "2^32 - 1 updates since start: start again");
    if (t < f->last.t) return psyrdk__fail(f, PSYRDK_ERR_ORDER, "t is before the last update: start and replay to go back");
    u = (uint32_t)(++f->u_);
    s = (int)((u - 1) % f->sets_);
    n = f->n;
    lo = (uint32_t)s * (uint32_t)n;
    words = f->words_;
    if (f->clock_ == PSYRDK_CLOCK_FRAMES) {
        dt = ((int64_t)u - f->set_u_[s]) * f->frame_ns_;
        now = f->t_start_ + (int64_t)u * f->frame_ns_;
    } else {
        dt = t - f->set_t_[s];
        now = t;
    }
    v = f->speed;
    D = psyrdk__step(f, v, dt);
    memcpy(&dir_bits, &f->direction, sizeof dir_bits);
    if (!f->dir_valid_ || dir_bits != f->dir_bits_) {
        f->dir_angle_ = psyrdk_angle(f->direction);
        f->dir_bits_ = dir_bits;
        f->dir_valid_ = 1;
    }
    sig_angle = f->dir_angle_;
    if (v < 0) sig_angle += 0x80000000u;
    psyrdk_sincos(sig_angle, &ss, &sc);
    c = f->coherence;
    if (!psyrdk__finite(c) || !(c > 0)) c = 0;   /* NaN too, under -ffast-math as well */
    if (c > 1) c = 1;
    if (f->select_ == PSYRDK_BERNOULLI) thr = (uint64_t)floor((double)c * 4294967296.0);
    else m = (int)floor((double)c * (double)n + 0.5);
    /* Selection. The scratch then holds this update's slot 0 for every
     * shown dot when the rules draw for most of them. */
    if (f->signal_ == PSYRDK_SIGNAL_SAME) {
        uint64_t sel = f->select_ == PSYRDK_BERNOULLI ? thr : (uint64_t)m;
        if (f->same_sel_[s] != sel) {
            psyrdk__words_n(f, lo, n, 0, words);
            if (f->select_ == PSYRDK_BERNOULLI) psyrdk__bernoulli(f, lo, thr);
            else psyrdk__select(f, lo, m);
            f->same_sel_[s] = sel;
        }
        have_words = f->noise_ != PSYRDK_NOISE_DIRECTION;
        if (have_words) psyrdk__words_n(f, lo, n, u, words);
    } else {
        psyrdk__words_n(f, lo, n, u, words);
        have_words = 1;
        if (f->select_ == PSYRDK_BERNOULLI) psyrdk__bernoulli(f, lo, thr);
        else psyrdk__select(f, lo, m);
        if (f->run_) {
            for (i = 0; i < n; i++) {
                uint32_t j = lo + (uint32_t)i;
                if (f->flags_[j] & PSYRDK_EV_SIGNAL) { if (f->run_[j] < 65535u) f->run_[j]++; }
                else f->run_[j] = 0;
            }
        }
    }
    /* Motion. */
    for (i = 0; i < n; i++) {
        uint32_t j = lo + (uint32_t)i;
        uint8_t fl = (uint8_t)(f->flags_[j] & PSYRDK_EV_SIGNAL);
        const uint32_t* w = have_words ? words + 4 * (size_t)i : NULL;
        uint32_t tmp[4];
        f->flags_[j] = fl;
        if (f->death_ && f->death_[j] <= now) {
            int64_t L = f->life_ns_;
            if (!w) { psyrdk__words(f, j, u, 0, tmp); w = tmp; }
            psyrdk__place(f, j, u, w);
            if (f->cs_) {
                f->ang_[j] = w[1];
                psyrdk_sincos(w[1], &f->cs_[2 * j + 1], &f->cs_[2 * j]);
            }
            f->death_[j] += L * (1 + (now - f->death_[j]) / L);
            f->flags_[j] |= PSYRDK_EV_PLACED;
            f->stats.deaths++;
            if (fl) f->stats.signal++;
            continue;
        }
        if (fl) {
            f->stats.signal++;
            psyrdk__move(f, j, u, ss, sc, D, w);
        } else if (f->noise_ == PSYRDK_NOISE_POSITION) {
            psyrdk__place(f, j, u, w);
            f->flags_[j] |= PSYRDK_EV_PLACED;
        } else if (f->cs_) {   /* DIRECTION: its own, kept as sine and cosine */
            psyrdk__move(f, j, u, f->cs_[2 * j + 1], f->cs_[2 * j], D, w);
        } else {               /* WALK */
            int32_t ns, nc;
            f->ang_[j] = w[1];
            psyrdk_sincos(w[1], &ns, &nc);
            psyrdk__move(f, j, u, ns, nc, D, w);
        }
    }
    f->set_t_[s] = t;
    f->set_u_[s] = (int64_t)u;
    f->shown_ = s;
    f->now_ = now;
    f->sig_angle_ = sig_angle;
    f->last.t = t;
    f->last.coherence = f->coherence;
    f->last.direction = f->direction;
    f->last.speed = f->speed;
    f->last.set = s;
    f->stats.updates++;
    return n;
}

/* --- output ----------------------------------------------------------------- */

PSYRDK_API int psyrdk_write(const psyrdk_field* f, const psyrdk_out* o) {
    int i, n;
    uint32_t lo;
    double scale;
    uint32_t sig_angle;
    if (!f || f->magic_ != PSYRDK__MAGIC || !o) return PSYRDK_ERR_ARG;
    if (!f->started_) return PSYRDK_ERR_ORDER;
    n = f->n;
    lo = (uint32_t)f->shown_ * (uint32_t)n;
    scale = (double)f->rmax_ * (1.0 / 1073741824.0);
    sig_angle = f->sig_angle_;
    if (o->xy) {
        size_t st = o->xy_stride ? o->xy_stride : 2 * sizeof(float);
        unsigned char* b = (unsigned char*)o->xy;
        const int32_t* p = f->xy_ + 2 * (size_t)lo;
        for (i = 0; i < n; i++) {
            float* q = (float*)(void*)(b + st * (size_t)i);
            q[0] = (float)((double)p[2 * i] * scale);
            q[1] = (float)((double)p[2 * i + 1] * scale);
        }
    }
    if (o->dir) {
        size_t st = o->dir_stride ? o->dir_stride : sizeof(float);
        unsigned char* b = (unsigned char*)o->dir;
        for (i = 0; i < n; i++) {
            uint32_t j = lo + (uint32_t)i;
            uint32_t a = (f->flags_[j] & PSYRDK_EV_SIGNAL) ? sig_angle : f->ang_[j];
            *(float*)(void*)(b + st * (size_t)i) = (float)((double)a * (360.0 / 4294967296.0));
        }
    }
    if (o->age) {
        size_t st = o->age_stride ? o->age_stride : sizeof(float);
        unsigned char* b = (unsigned char*)o->age;
        for (i = 0; i < n; i++) {
            uint32_t j = lo + (uint32_t)i;
            int64_t born = f->death_ ? f->death_[j] - f->life_ns_ : f->t_start_;
            if (born < f->t_start_) born = f->t_start_;
            *(float*)(void*)(b + st * (size_t)i) = (float)((double)(f->now_ - born) * 1e-9);
        }
    }
    if (o->signal) {
        size_t st = o->signal_stride ? o->signal_stride : 1;
        for (i = 0; i < n; i++) o->signal[st * (size_t)i] = (uint8_t)(f->flags_[lo + (uint32_t)i] & PSYRDK_EV_SIGNAL);
    }
    if (o->event) {
        size_t st = o->event_stride ? o->event_stride : 1;
        for (i = 0; i < n; i++) o->event[st * (size_t)i] = f->flags_[lo + (uint32_t)i];
    }
    return n;
}

PSYRDK_API int psyrdk_xy(const psyrdk_field* f, float* xy) {
    psyrdk_out o;
    memset(&o, 0, sizeof o);
    o.xy = xy;
    return psyrdk_write(f, &o);
}

/* --- digest, replay, snapshot ----------------------------------------------- */

static uint64_t psyrdk__fnv(uint64_t h, uint64_t v, int bytes) {
    int k;
    for (k = 0; k < bytes; k++) { h ^= (v >> (8 * k)) & 255u; h *= 0x100000001b3ull; }
    return h;
}

PSYRDK_API uint64_t psyrdk_digest(const psyrdk_field* f) {
    uint64_t h = 0xcbf29ce484222325ull;
    int i, s;
    if (!f || f->magic_ != PSYRDK__MAGIC) return 0;
    for (i = 0; i < f->total; i++) {
        h = psyrdk__fnv(h, (uint32_t)f->xy_[2 * i], 4);
        h = psyrdk__fnv(h, (uint32_t)f->xy_[2 * i + 1], 4);
        h = psyrdk__fnv(h, f->ang_[i], 4);
        h = psyrdk__fnv(h, f->flags_[i], 1);
        if (f->run_) h = psyrdk__fnv(h, f->run_[i], 2);
        if (f->death_) h = psyrdk__fnv(h, (uint64_t)f->death_[i], 8);
    }
    h = psyrdk__fnv(h, f->u_, 8);
    for (s = 0; s < f->sets_; s++) {
        h = psyrdk__fnv(h, (uint64_t)f->set_t_[s], 8);
        h = psyrdk__fnv(h, (uint64_t)f->set_u_[s], 8);
    }
    return h;
}

PSYRDK_API int psyrdk_replay(psyrdk_field* f, uint64_t seed, int64_t t0, const psyrdk_step* steps, int n) {
    int k, rc = psyrdk_start(f, seed, t0);
    if (rc < 0) return rc;
    if (n > 0 && !steps) return PSYRDK_ERR_ARG;
    for (k = 0; k < n; k++) {
        f->coherence = steps[k].coherence;
        f->direction = steps[k].direction;
        f->speed = steps[k].speed;
        rc = psyrdk_update(f, steps[k].t);
        if (rc < 0) return rc;
    }
    return PSYRDK_OK;
}

/* Header of a snapshot: the layout it was taken with, then the scalars. */
typedef struct psyrdk__snap {
    uint32_t magic, total, n;
    uint8_t  rules[8];
    uint64_t seed;
    uint64_t u;
    int64_t  set_t[PSYRDK_MAX_SETS], set_u[PSYRDK_MAX_SETS];
    uint64_t same_sel[PSYRDK_MAX_SETS];
    int64_t  t_start;
    int32_t  shown, started;
    int64_t  now;
    uint32_t sig_angle, pad_;
    psyrdk_step last;
    psyrdk_stats stats;
} psyrdk__snap;

static size_t psyrdk__arrays(const psyrdk_field* f) {
    size_t t = (size_t)f->total;
    return t * 8 + t * 4 + t + (f->run_ ? t * 2 : 0) + (f->death_ ? t * 8 : 0);
}

PSYRDK_API size_t psyrdk_snapshot_bytes(const psyrdk_field* f) {
    if (!f || f->magic_ != PSYRDK__MAGIC) return 0;
    return sizeof(psyrdk__snap) + psyrdk__arrays(f);
}

PSYRDK_API int psyrdk_snapshot(const psyrdk_field* f, void* out, size_t cap) {
    psyrdk__snap h;
    unsigned char* b = (unsigned char*)out;
    size_t t;
    if (!f || f->magic_ != PSYRDK__MAGIC || !out) return PSYRDK_ERR_ARG;
    if (cap < psyrdk_snapshot_bytes(f)) return PSYRDK_ERR_MEM;
    t = (size_t)f->total;
    memset(&h, 0, sizeof h);
    h.magic = PSYRDK__MAGIC; h.total = (uint32_t)f->total; h.n = (uint32_t)f->n;
    h.rules[0] = f->algorithm_; h.rules[1] = f->signal_; h.rules[2] = f->select_; h.rules[3] = f->noise_;
    h.rules[4] = f->aperture_; h.rules[5] = f->edge_; h.rules[6] = f->clock_; h.rules[7] = f->sets_;
    h.seed = f->seed_; h.u = f->u_;
    memcpy(h.set_t, f->set_t_, sizeof h.set_t);
    memcpy(h.set_u, f->set_u_, sizeof h.set_u);
    memcpy(h.same_sel, f->same_sel_, sizeof h.same_sel);
    h.t_start = f->t_start_; h.shown = f->shown_; h.started = f->started_; h.now = f->now_; h.sig_angle = f->sig_angle_;
    h.last = f->last; h.stats = f->stats;
    memcpy(b, &h, sizeof h); b += sizeof h;
    memcpy(b, f->xy_, t * 8); b += t * 8;
    memcpy(b, f->ang_, t * 4); b += t * 4;
    memcpy(b, f->flags_, t); b += t;
    if (f->run_) { memcpy(b, f->run_, t * 2); b += t * 2; }
    if (f->death_) memcpy(b, f->death_, t * 8);
    return PSYRDK_OK;
}

PSYRDK_API int psyrdk_restore(psyrdk_field* f, const void* in, size_t len) {
    psyrdk__snap h;
    const unsigned char* b = (const unsigned char*)in;
    size_t t;
    if (!f || f->magic_ != PSYRDK__MAGIC || !in) return PSYRDK_ERR_ARG;
    if (len != psyrdk_snapshot_bytes(f)) return psyrdk__fail(f, PSYRDK_ERR_ARG, "snapshot: another size: not this field's desc");
    memcpy(&h, b, sizeof h); b += sizeof h;
    if (h.magic != PSYRDK__MAGIC || h.total != (uint32_t)f->total || h.n != (uint32_t)f->n ||
        h.rules[0] != f->algorithm_ || h.rules[1] != f->signal_ || h.rules[2] != f->select_ || h.rules[3] != f->noise_ ||
        h.rules[4] != f->aperture_ || h.rules[5] != f->edge_ || h.rules[6] != f->clock_ || h.rules[7] != f->sets_)
        return psyrdk__fail(f, PSYRDK_ERR_ARG, "snapshot: taken from a field with another desc");
    t = (size_t)f->total;
    f->seed_ = h.seed; f->u_ = h.u;
    memcpy(f->set_t_, h.set_t, sizeof h.set_t);
    memcpy(f->set_u_, h.set_u, sizeof h.set_u);
    memcpy(f->same_sel_, h.same_sel, sizeof h.same_sel);
    f->t_start_ = h.t_start; f->shown_ = h.shown; f->started_ = h.started; f->now_ = h.now; f->sig_angle_ = h.sig_angle; f->dir_valid_ = 0;
    f->last = h.last; f->stats = h.stats;
    memcpy(f->xy_, b, t * 8); b += t * 8;
    memcpy(f->ang_, b, t * 4); b += t * 4;
    memcpy(f->flags_, b, t); b += t;
    if (f->run_) { memcpy(f->run_, b, t * 2); b += t * 2; }
    if (f->death_) memcpy(f->death_, b, t * 8);
    if (f->cs_) {
        size_t i;
        for (i = 0; i < t; i++) psyrdk_sincos(f->ang_[i], &f->cs_[2 * i + 1], &f->cs_[2 * i]);
    }
    return PSYRDK_OK;
}

PSYRDK_API const psyrdk_param* psyrdk_params(int* n) {
    static const psyrdk_param p[3] = {
        { "coherence", "f32", 0, 1, 0, "", "the signal fraction, read at each update", (uint32_t)offsetof(psyrdk_field, coherence) },
        { "direction", "f32", -1e30, 1e30, 0, "deg", "the signal direction, +x toward +y (clockwise on the screen)",
          (uint32_t)offsetof(psyrdk_field, direction) },
        { "speed", "f32", -1e30, 1e30, 0, "u/s", "units per second; negative moves the other way",
          (uint32_t)offsetof(psyrdk_field, speed) },
    };
    if (n) *n = 3;
    return p;
}

#endif /* PSY_RDK_IMPLEMENTATION_GUARD */
#endif /* PSY_RDK_IMPLEMENTATION */

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
 * ------------------------------------------------------------------------ */
