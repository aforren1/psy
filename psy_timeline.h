/* psy_timeline.h - v0.3.0 - public domain single-header timeline library
 *
 *   What a frame shows, as a function of when the frame appears. Events
 *   (onsets, offsets, value changes, triggers, annotations) at times on a
 *   chosen time base, keyframe tracks with easing on float channels
 *   (position, contrast, opacity, spatial frequency), and one call per frame
 *   that takes the PREDICTED ONSET of the frame being drawn, fills the
 *   channel values for it and reports which events landed on it, with the
 *   frame and the residual. It never reads a clock.
 *
 *   Written in the single-header style of the stb / sokol libraries. Pure
 *   computation: no OS calls, no threads, no heap, no I/O. Needs nothing
 *   but the C standard library (<math.h> for the cosine and log easings,
 *   snprintf for the open() message). It does not include psy_rt.h: a time
 *   is an int64_t count of nanoseconds, the unit psyrt_now_ns() returns, so
 *   the caller passes psy_rt.h times in with a cast and nothing else.
 *
 *   Targets every platform the compiler does. C99 is the floor: it builds as
 *   C99, C11 and C++17, and in the C dialect MSVC compiles by default.
 *
 *   ---------------------------------------------------------------------
 *   CHANGELOG
 *   ---------------------------------------------------------------------
 *   v0.3.0 - psytl_skip(): a seek that marks the events it passes over
 *          PSYTL_EV_SKIPPED instead of firing them late on one frame, and
 *          returns how many; their channel values still apply. A skip
 *          back is an anchor's rewind. psytl_peek(): the pending events
 *          in an RT window, with their RT times, changing nothing, for
 *          the audio path's look-ahead. psytl_lead(): the lead in use.
 *          A rewind now makes skipped events pending again, and prune
 *          deletes skipped events with the fired ones. Nothing else
 *          changes: a timeline that never skips gives the same bytes as
 *          v0.2.0.
 *   v0.2.0 - tracks grow: psytl_track with a keyed or a SAMPLED form
 *          (floats at an integer rate, LINEAR, STEP or Catmull-Rom CUBIC,
 *          for long trajectories such as a sum-of-sines target), a REPEAT
 *          (period and count, for flicker and counterphase), the
 *          PSYTL_EASE_BEZIER easing (CSS cubic-bezier, from a per-track
 *          curve table), and psytl_tween() with a psytl_tween_desc: to,
 *          from, duration, start or delay, ease, cycles, yoyo, and
 *          keep_velocity (a Hermite takeover with no velocity step); a
 *          tween waits until its start while the old track runs on, and
 *          takes the old track's value there; its keys live in the
 *          handle. PSYTL_S() and PSYTL_MS() for literals. A STEP track is
 *          read at the end of the lead window, so its changes land where
 *          events at the same times land; other tracks are read at the
 *          onset. Breaking: the default lead is now half a frame (desc.lead
 *          0 means 0.5), and PSYTL_LEAD_NONE asks for the old never-early
 *          rule, because under onset noise never-early misplaced about
 *          12% of frame-aligned changes;
 *          psytl_sample() takes a psytl_track; psytl_key's reserved bytes
 *          are now reserved_ (1) and curve (2), so a key written as
 *          { time, value, ease } still compiles.
 *   v0.1.0 - first implementation: time bases with anchor, pause, resume
 *          and rewind (by anchor only); events in a caller-sized array;
 *          keyframe tracks with six easings; evaluate at the predicted
 *          onset with a lead window; late events by the base's reach;
 *          prune, remove and clear; the record of where every event
 *          landed.
 *
 *   STATUS: v0.3.0. Skip, peek and lead: tests/adapt/psy_timeline_test.c
 *   has directed checks for each (the 30-minute seek against an anchor,
 *   skip back against an anchor back, a waiting late event, the reach and
 *   rewind after a skip, prune and remove of skipped events, tracks and a
 *   waiting tween, peek's window edges, order, cap, bases and purity by
 *   memcmp of the whole handle, and a 22 s audio look-ahead at 60 Hz), and
 *   skip and peek are ops of 10 more seeds of the random model check.
 *   Unlike the v0.1 and v0.2 checks, these were written by the writer of
 *   the implementation. Ten model mutations of skip and peek are each
 *   caught; of 23 mutations of the header, 20 are caught and 3 cannot
 *   change a result (docs/psy_timeline.md). The v0.2.0 test, unchanged,
 *   passes with the same model counts, and the examples print the same
 *   bytes but the version. Built and run with gcc 11.4 (WSL2: C99, C11,
 *   C++17, ASan and UBSan), MinGW-w64 gcc 16.1 (C99, C11, C++17), MSVC
 *   19.44 (default C, C11, C++17) and emcc 6.0.10 under node.
 *   timeline_bench at 60 Hz (gcc): trial 51 ns, movie 322 ns mean and
 *   599 ns p99, script 60 ns, tracks 246 ns: inside v0.2.0's ranges.
 *   v0.2.0: Built and run on Linux (WSL2) with gcc 11.4 as C99
 *   and C11 under -Wall -Wextra -Wpedantic -Wshadow -Werror at -O2 and
 *   -O3, and under -fsanitize=address,undefined -fno-sanitize-recover=all
 *   with no diagnostic; the compile checks also as C++17. On Windows 11
 *   with MSVC 19.44 under /W4 /WX, in its default C dialect and as
 *   /std:c++17. examples/timeline_trial.c and timeline_tracking.c print
 *   the same bytes on both compilers.
 *   tests/adapt/psy_timeline_test.c was written from this manual by
 *   writers who did not read the implementation, with its own model, and
 *   checks: every rejected desc and argument; quantization on grids of 60,
 *   144, 240, 360, 500 and 1000 Hz at lead 0, 0.25 and 0.5 against a
 *   ceiling-division model with times on the window edges, and period 0;
 *   base states, pause with a lead, base 0 refusals; channel values by
 *   (time, id), binding and PSYTL_ERR_BOUND, add_n all or none; the
 *   report's order, cap and total; late events and the reach; anchor
 *   rewinds; remove, prune and clear; next_due. Tracks: every easing
 *   against its own formulas, BEZIER against a bisection solver on ten
 *   curves; sampled placement over 600k samples at rates 1, 7, 60, 120,
 *   999983 and 1e9 in integer arithmetic, LINEAR, STEP and CUBIC with the
 *   phantom ends; REPEAT folding and the hold; the 7.5 Hz flicker against
 *   the frame count at 60 to 500 Hz under onset noise; every
 *   set_track and tween rejection alone; tweens: every desc field and
 *   default, PSYTL_S and PSYTL_MS, "now" after evaluates, anchors, pauses
 *   and resumes, the takeover's from at the start for keyed, sampled,
 *   repeating, tween and Hermite old tracks, identical across seven frame
 *   grids, no repeated position on a retarget, keep_velocity's value and
 *   slope continuity against a Hermite model, cycles, FOREVER and yoyo,
 *   cancellation, replacement, the cross-base stop; a STEP track's change landing on
 *   the same frame as an event at the same time over 300 random cases;
 *   replay of the calls into two handles, storages compared with memcmp;
 *   and a random model check, 10 seeds x 800 steps with set_track and
 *   tweens that wait and take over, comparing the whole storage and every
 *   value after each step.
 *   Deliberate mutations of the model (lead off by one, rewind bounds, the
 *   LATE flag, an evaluate-time un-fire, repeated instead of phantom end
 *   samples, a whole-ns sample step, the hold a cycle late or folded, the
 *   Bezier parameter used as x, STEP tracks read at the onset, a tween's
 *   from read at the call, keep_velocity's slope ignored, the yoyo
 *   return leg missing) are each caught, and so is the first version's
 *   rule for "now". examples/timeline_tracking.c runs a 10-minute 5 Hz
 *   sum-of-sines target at 500 Hz within 1.73e-5 of its peak and a 7.5 Hz
 *   flicker with no wrong frame in 300500. examples/timeline_bench.c
 *   measures a frame's evaluate at 46-527 ns mean and under 1.6 us p99 on
 *   the workloads docs/psy_timeline.md lists, the same at 60, 500 and
 *   1000 Hz; at 500 Hz the worst p99 is 0.04% of the 2 ms budget. Rare
 *   slow frames (up to 1.4 ms at 500 Hz) are the machine's, not the
 *   header's: two identical timelines timed in lockstep are slow on
 *   different frames (docs/psy_timeline.md). What is NOT done: no run on
 *   macOS, clang, the newer gcc of CI or under emscripten (CI covers
 *   those); nothing here is a timing measurement of a display: the header
 *   computes, psy_screen.h and the photodiode measure.

 *   ---------------------------------------------------------------------
 *   USAGE
 *   ---------------------------------------------------------------------
 *   Do this:
 *
 *       #define PSY_TIMELINE_IMPLEMENTATION
 *
 *   in *one* C or C++ file before including this header to create the
 *   implementation. Every other file just includes the header normally.
 *
 *   One trial: a fixation point from 0 to 0.5 s, then a grating whose
 *   contrast ramps up over 100 ms with a raised cosine, holds, and ramps
 *   down to end at 1.2 s, and a trigger at the grating's onset
 *   (examples/timeline_trial.c runs this against a simulated 60 Hz
 *   display):
 *
 *       #define PSY_TIMELINE_IMPLEMENTATION
 *       #include "psy_timeline.h"
 *
 *       enum { FIX_ON, GRATING_ON, CONTRAST, N_CHANNELS };   // the caller's map
 *       enum { TRIAL = 1 };                                  // a base number
 *
 *       static psytl_event storage[64];
 *       static psytl_timeline tl;                            // 60 KB
 *       psytl_desc d = { .events = storage, .event_capacity = 64,
 *                        .n_channels = N_CHANNELS };
 *       if (!psytl_open(&tl, &d)) { fputs(psytl_error(&tl), stderr); return 1; }
 *
 *       const int64_t S = PSYTL_NS_PER_S;
 *       psytl_event e[] = {
 *           { .time = 0,       .base = TRIAL, .kind = PSYTL_ONSET,  .target = FIX_ON },
 *           { .time = S/2,     .base = TRIAL, .kind = PSYTL_OFFSET, .target = FIX_ON },
 *           { .time = S/2,     .base = TRIAL, .kind = PSYTL_ONSET,  .target = GRATING_ON },
 *           { .time = S/2,     .base = TRIAL, .kind = PSYTL_TRIGGER, .code = 12 },
 *           { .time = 6*S/5,   .base = TRIAL, .kind = PSYTL_OFFSET, .target = GRATING_ON },
 *       };
 *       static const psytl_key ramp[] = {                    // must outlive use
 *           { S/2,        0.0f, PSYTL_EASE_COSINE },
 *           { S/2 + S/10, 0.5f },                            // LINEAR, a hold
 *           { 11*S/10,    0.5f, PSYTL_EASE_COSINE },
 *           { 6*S/5,      0.0f },
 *       };
 *       psytl_add_n(&tl, e, 5);
 *       psytl_set_keys(&tl, CONTRAST, TRIAL, ramp, 4);
 *
 *       // Trial time 0 is the first frame of the trial.
 *       psytl_anchor(&tl, TRIAL, next_onset_ns, 0);
 *       for (;;) {
 *           psytl_frame f = { next_onset_ns, period_ns, frame_index };
 *           psytl_event fired[8];
 *           int n = psytl_evaluate(&tl, &f, fired, 8);
 *           const float* v = psytl_values(&tl);
 *           draw(v[FIX_ON], v[GRATING_ON], v[CONTRAST]);
 *           for (int i = 0; i < n && i < 8; i++)
 *               if (fired[i].kind == PSYTL_TRIGGER) arm_trigger(fired[i].code);
 *           flip_at(next_onset_ns);   // and log fired[] with the flip record
 *           ...
 *       }
 *
 *   A 10-minute sum-of-sines tracking target on the x channel, from a
 *   table of samples at 120 per second, and a 7.5 Hz square-wave flicker
 *   on the luminance channel for the same 10 minutes
 *   (examples/timeline_tracking.c runs both at 500 Hz):
 *
 *       static float xs[600 * 120 + 1];        // from the pack, or computed
 *       psytl_track path = { 0 };
 *       path.samples = xs; path.n_samples = 600 * 120 + 1;
 *       path.rate = 120; path.interp = PSYTL_INTERP_CUBIC;
 *       psytl_set_track(&tl, TARGET_X, TRIAL, &path);
 *
 *       static const psytl_key square[] = {     // on for half a cycle
 *           { 0,              1.0f, PSYTL_EASE_STEP },
 *           { 2 * S / 30,     0.0f, PSYTL_EASE_STEP },
 *       };
 *       psytl_track flicker = { 0 };
 *       flicker.keys = square; flicker.n_keys = 2;
 *       flicker.period = 4 * S / 30;              // 133.33 ms, 7.5 Hz
 *       flicker.repeats = 4500;                   // 10 minutes, then off
 *       psytl_set_track(&tl, LUMINANCE, TRIAL, &flicker);
 *
 *   A script's fade, from wherever contrast is now to 0 over 200 ms, and
 *   a moving dot sent somewhere else in mid-flight with no velocity step
 *   (GSAP: gsap.to(g, {contrast: 0, duration: 0.2, ease: "sine.inOut"})):
 *
 *       psytl_tween(&tl, CONTRAST, TRIAL, &(psytl_tween_desc){
 *           .to = 0.0f, .duration = PSYTL_MS(200), .ease = PSYTL_EASE_COSINE });
 *       psytl_tween(&tl, DOT_X, TRIAL, &(psytl_tween_desc){
 *           .to = 4.0f, .duration = PSYTL_MS(300), .keep_velocity = true });
 *
 *   Compound literals and designated initializers are C99; in C++17 fill a
 *   psytl_tween_desc d = {} field by field.
 *
 *   The next trial clears the trial base and adds its own events, or keeps
 *   them and anchors the base again at its first frame: anchoring moves
 *   the base's time back to 0, which un-fires everything, so the same
 *   events run again (REWIND).
 *
 *   ---------------------------------------------------------------------
 *   MODEL
 *   ---------------------------------------------------------------------
 *   TIME
 *     Every time is an int64_t count of nanoseconds. The RT clock is the
 *     caller's monotonic clock, psyrt_now_ns() when psy_rt.h is in use; a
 *     frame's onset is on it. The header compares and subtracts times and
 *     never reads a clock. Times stay below 2^62 ns (146 years) in
 *     magnitude so that a sum of two cannot overflow. psytl_ns(seconds)
 *     rounds a double to the nearest nanosecond, for tables in seconds.
 *
 *   TIME BASES (psytl_anchor, psytl_pause, psytl_resume, psytl_stop)
 *     An event or a track is on one of PSYTL_MAX_BASES (8) bases. Base 0,
 *     PSYTL_BASE_RT, is the RT clock itself and always runs (anchor, pause,
 *     resume and stop on it are PSYTL_ERR_ARG). Bases 1 to 7
 *     are the caller's: a trial clock, a movie clock, a block clock. Each
 *     is in one of three states:
 *       stopped   the state after open(). Nothing on it fires, rewinds or
 *                 writes a channel; its channels hold their values.
 *       running   psytl_anchor(tl, b, rt, bt): base time bt at RT time rt,
 *                 advancing at the rate of the RT clock, so base time at
 *                 RT time t is bt + (t - rt). rt may be in the future.
 *       paused    psytl_pause(tl, b, rt): the base time at rt, frozen.
 *                 psytl_resume(tl, b, rt) runs it again from that time at
 *                 RT time rt. A paused base evaluates at its frozen time,
 *                 so its tracks hold and an event added at or before that
 *                 time still fires. A pause un-fires nothing, even when
 *                 the frozen time is before an event that a lead window
 *                 already fired (QUANTIZATION).
 *     psytl_stop(tl, b) stops a base. Anchoring a base again, from any
 *     state, is how a trial clock restarts and how a movie seeks: forward,
 *     the next evaluate fires the events the base passed over, late by
 *     their residuals; back, the events from the new time on are pending
 *     again (REWIND). psytl_skip() is the seek that does not fire what it
 *     passes over (SKIP). There is no rate other than 1;
 *     docs/psy_timeline.md has a proposal for an exact rational rate.
 *
 *   EVENTS (psytl_event, psytl_add, psytl_add_n)
 *     An event has a time on its base, a kind, and the caller's payload:
 *       PSYTL_MARK     reported only; an annotation, a coroutine to resume
 *       PSYTL_TRIGGER  reported only; `code` is the value for the port
 *       PSYTL_ONSET    channel `target` becomes 1 when it fires
 *       PSYTL_OFFSET   channel `target` becomes 0
 *       PSYTL_SET      channel `target` becomes `value`
 *       PSYTL_KIND_USER (16) to 255: reported only, the caller's meaning
 *     A zeroed event is a MARK at time 0 on the RT base. `code`, `user` and
 *     (for the reported-only kinds) `target` are the caller's and come back
 *     unchanged in the report. Kinds 5 to 15 are reserved and rejected.
 *     psytl_add() copies the event into the storage the desc gave, pending
 *     (flags 0, frame -1, onset 0, residual 0), assigns it an id (0, 1, 2, ... in add order; psytl_open() restarts it) and
 *     returns the id. psytl_add_n() adds n events, all or none, and returns
 *     the first id; the others follow in order. The storage is kept sorted
 *     by base, then time, then id, so events at one time keep their add
 *     order. An add costs a binary search and a move of the events after
 *     it in the storage: appending sorted events to the last base in use
 *     moves nothing, so a table of thousands loads in linear time.
 *
 *   CHANNELS (psytl_values, desc.n_channels, desc.initial)
 *     The state of a frame is desc.n_channels floats (at most
 *     PSYTL_MAX_CHANNELS, 256 by default), starting at desc.initial[] (all
 *     0 when NULL). The caller decides what each one means. A channel is
 *     driven either by events (ONSET, OFFSET, SET naming it) or by a track
 *     (psytl_set_track, psytl_set_keys, psytl_tween), never both, and from
 *     one base only. The first event that names a free channel binds it to
 *     the event's base; an event on another base, or a track, is then
 *     PSYTL_ERR_BOUND until the channel's last event is gone. A track binds
 *     the channel to its base and replaces the channel's track whatever
 *     base it was on; an event naming it is PSYTL_ERR_BOUND until the
 *     track is removed (set_track with NULL, set_keys with n = 0).
 *     psytl_clear() of a base frees the channels bound to it. A channel a
 *     prune left with no event is free but keeps its value, and a clear
 *     does not touch it.
 *     An event-driven channel's value is the value of its fired or skipped
 *     (SKIP) event with the latest (time, id), or its initial value when
 *     there is none. So an event that fires late (LATE EVENTS) does not
 *     overwrite a later event that fired before it, a skip leaves the
 *     channel as playing through would, and a rewind recomputes the
 *     channel from the events still fired or skipped.
 *
 *   TRACKS (psytl_track, psytl_set_track, psytl_set_keys, psytl_tween,
 *           psytl_sample)
 *     A track drives a channel from a function of its base's time. It has
 *     one of two forms, and an optional repeat.
 *     KEYED: n_keys keys (psytl_key) sorted by time, equal times allowed
 *     (a jump). For movement and envelopes with few changes: a ramp, a
 *     fade, a path through a dozen points. The value at time t: the first
 *     key's value before it, the last key's value from it on, and between
 *     key i and key i+1 (t_i <= t < t_i+1, at equal times the later key)
 *     v_i + (v_i+1 - v_i) e(u) with key i's easing e at
 *     u = (t - t_i) / (t_i+1 - t_i):
 *       PSYTL_EASE_LINEAR  (0)   e = u
 *       PSYTL_EASE_STEP          e = 0: v_i until t_i+1
 *       PSYTL_EASE_COSINE        e = (1 - cos(pi u)) / 2, a raised cosine
 *       PSYTL_EASE_QUAD_IN       e = u^2, starts slow
 *       PSYTL_EASE_QUAD_OUT      e = 1 - (1 - u)^2, ends slow
 *       PSYTL_EASE_BEZIER        e = y where x = u on the cubic Bezier
 *                                curve from (0, 0) to (1, 1) with control
 *                                points (x1, y1) and (x2, y2), CSS's
 *                                cubic-bezier(), from track.curves[key.curve]
 *       PSYTL_EASE_LOG           not of that form: v_i (v_i+1 / v_i)^u, a
 *                                geometric sweep (spatial frequency, tone
 *                                frequency); both values must be > 0
 *     A curve's x1 and x2 are in [0, 1], so x rises with the curve
 *     parameter and u has one y. y1 and y2 may leave [0, 1], and then the
 *     value overshoots its keys; for a stimulus that is usually a confound.
 *     The y is found to 1e-12 in x. Every key's ease must be a
 *     psytl_ease, the last key's too, though the last key's is never used
 *     and the LOG and BEZIER checks skip it.
 *     SAMPLED: n_samples floats at track.rate samples per second (an
 *     integer, 1 to 1e9), sample i at base time t0 + i * 1e9 / rate ns,
 *     exactly, not rounded to a whole ns. For a trajectory that few keys
 *     cannot describe: a sum-of-sines tracking target, a recorded path, a
 *     noise sequence. A sample costs 4 bytes against a key's 16, there is
 *     no time column to search, and the lookup is integer arithmetic, so a
 *     table does not drift over hours. A rate that is not an integer
 *     (59.94) is a resampling job for the pack tool. A step in whole ns
 *     would drift: at 120 samples/s it is a third of a ns short per
 *     sample, and docs/psy_timeline.md measures what that costs on a
 *     moving target. Before t0 the value is the first sample, from the last
 *     sample's time on the last sample, and between samples i and i+1
 *     by track.interp:
 *       PSYTL_INTERP_LINEAR (0)  v_i + (v_i+1 - v_i) u
 *       PSYTL_INTERP_STEP        v_i
 *       PSYTL_INTERP_CUBIC       Catmull-Rom through v_i-1 .. v_i+2, with
 *                                v_-1 = 2 v_0 - v_1 and v_n = 2 v_n-1 -
 *                                v_n-2 past the ends; passes through every
 *                                sample with a continuous velocity, and
 *                                with two samples is LINEAR
 *     with u the fraction of the way from sample i's time to sample
 *     i+1's. How fine a table needs to be is
 *     measured in docs/psy_timeline.md: for a sum of sines up to 5 Hz,
 *     CUBIC at 120 samples/s stays within 7.4e-6 of the peak, 1.7e-5 in
 *     the first and last segment (0.29 MB for 10 minutes), and reaches
 *     float rounding at 500 samples/s in the interior; LINEAR
 *     needs 1000 samples/s for 5e-6, and its velocity steps at every
 *     sample. The pack tool computes the samples once, so the runtime does
 *     no trigonometry and the trajectory is the same bits everywhere.
 *     REPEAT: track.period > 0 repeats the track every period ns from its
 *     start (the first key's time, or t0): base time t reads the track at
 *     start + ((t - start) mod period). track.repeats cycles (0: forever),
 *     after which the track holds its value at start + period, read
 *     without repeating. Before the start, no repeat applies. A square-wave
 *     flicker is two STEP keys and a period; a key past start + period is
 *     never reached while the track repeats.
 *     psytl_set_track(tl, channel, base, &tr) copies the struct; the
 *     arrays it points at (keys, curves, samples) stay the caller's and
 *     must stay valid and unchanged until the channel's track is
 *     replaced, the base is cleared, or the handle is no longer used. A
 *     pack's table works as is. tr NULL removes the track and returns the
 *     channel to its initial value. psytl_set_keys(tl, channel, base,
 *     keys, n) is set_track with only keys and n (n = 0 removes).
 *     set_track rejects with PSYTL_ERR_ARG: neither form or both; a
 *     negative count; unsorted key times; a key ease that is not a
 *     psytl_ease; a non-finite key value, sample or curve y; a curve x
 *     outside [0, 1]; a BEZIER key whose curve index is not below
 *     n_curves, or a LOG key with a value <= 0 at either end (both but
 *     the last key); n_curves > 0 with curves NULL, and any curve in the
 *     table that is bad, used or not; keys set with n_keys 0, or samples
 *     with n_samples 0 (neither form); a rate outside 1 to 1e9; a last
 *     sample at or past 2^62 (its time rounded up to a whole ns); an
 *     interp that is not a psytl_interp; a period < 0 or >= 2^62;
 *     repeats < 0, or > 0 with period 0; a time at or past +-2^62. The
 *     check reads every key and sample once. A rejected call leaves the
 *     channel's track as it was.
 *     TWEENS: psytl_tween(tl, channel, base, &d) drives a channel from one
 *     value to another, with its keys in the handle, so the caller keeps no
 *     array alive. Every field of psytl_tween_desc may be left zero:
 *       to             the end value
 *       duration       ns, >= 0; 0 jumps at the start
 *       start          the base time it starts, when start_set; otherwise
 *                      "now": the base time of the base's last evaluated
 *                      onset, unless an anchor, pause or resume came
 *                      after it, which set "now" to the anchor's base time
 *                      or the frozen time; 0 on a base never evaluated or
 *                      anchored
 *       delay          added to the start either way, so {.delay = d} is
 *                      "d after now" and {.start = t, .start_set = true,
 *                      .delay = d} is "d after t"
 *       from           the start value, when from_set; otherwise the value
 *                      the channel has at the start (below)
 *       ease, curve    a psytl_ease (0 = LINEAR), and for PSYTL_EASE_BEZIER
 *                      the curve, inline in the desc
 *       cycles         0 or 1: play once; n: n cycles; PSYTL_FOREVER. The
 *                      hold after the last cycle is as REPEAT gives it
 *       yoyo           a cycle goes from `from` to `to` and back, over
 *                      2 x duration, the return leg with the same ease
 *       keep_velocity  start at the velocity the channel has at the start
 *                      and come to rest at `to`: the shape is a cubic
 *                      Hermite segment, with no step in velocity, for a
 *                      moving target retargeted in mid-flight. With no
 *                      track at the start the velocity is 0, a smoothstep
 *     `from` is read only with from_set and `curve` only with BEZIER. The
 *     return leg of a yoyo applies the same ease again, from `to` to
 *     `from`; it is not GSAP's mirrored playback, so QUAD_IN is slow at
 *     the start of both legs.
 *     THE TAKEOVER. The tween waits until its start. Until then the
 *     channel's track runs on, as GSAP lets an overwritten tween run until
 *     the new one starts. At the start, the tween takes over the channel,
 *     and `from` (unless from_set) is the value the old track has at the
 *     start time, not at the frame that happens to see the start. So a
 *     tween posted just before a frame begins where the old motion would
 *     have been on that frame, with no repeated position, and the result
 *     does not depend on frame timing. With keep_velocity the start
 *     velocity is the old track's slope over the last microsecond before
 *     the start. A channel with no track starts from its value; a channel
 *     whose track is on another base stops that track at once, at its last
 *     value, because the two bases' times do not compare. A second tween
 *     before the first has started replaces it. psytl_set_track() on the
 *     channel cancels a waiting tween. A rewind does not undo a takeover:
 *     the takeover replaced the channel's track, which is not an event.
 *     Rejected with PSYTL_ERR_ARG: d NULL; a negative duration; a start,
 *     a delay, start + delay, or start + delay + duration (x 2 with yoyo)
 *     at or past +-2^62 (cycles do not count: a repeat folds time); a
 *     non-finite to, or from with from_set; an ease that is not a
 *     psytl_ease; a bad curve with BEZIER; LOG without from_set, or with
 *     from or to <= 0; cycles below PSYTL_FOREVER; more than one cycle, or
 *     yoyo, with duration 0; keep_velocity with an ease other than LINEAR,
 *     from_set, yoyo, more than one cycle (PSYTL_FOREVER included), or
 *     duration 0. A STEP tween is
 *     a STEP track (below). To await the end, add a MARK at start +
 *     duration (x 2 with yoyo, x cycles) on the same base.
 *     The arithmetic is double, stored as float. psytl_sample(&tr, t) is
 *     the same function without a handle, for a designer's preview, with
 *     no validation. A track writes its channel at every evaluate while
 *     its base runs or is paused, at the base time of the frame's onset:
 *     a ramp shows its value at the moment the frame appears. A STEP
 *     track is the exception. It is a sampled track with
 *     PSYTL_INTERP_STEP, or a keyed track (a tween included) whose keys
 *     but the last are all PSYTL_EASE_STEP. It changes value only at its
 *     key times, so it is read at the end of the frame's lead window, base
 *     time at (onset + lead x period), and each change lands on the frame
 *     an event at the same time would (QUANTIZATION). Under
 *     PSYTL_LEAD_NONE both are read at the onset. The reason is frame-locked flicker: a 7.5 Hz
 *     square wave on a 60 Hz display has its edges exactly on frame
 *     onsets, and an onset rounded to whole ns, or predicted with any
 *     noise, falls on either side of an edge. Read at the onset, a wave
 *     with exact edges showed the wrong state on 3000 of 36000 frames;
 *     read at the end of a half-period window, on none
 *     (docs/psy_timeline.md). A jump inside a track that is not a STEP
 *     track (a STEP key among ramps, keys at equal times, the wrap of a
 *     repeat whose cycle does not end where it starts) is read at the
 *     onset; to quantize it like an event, put it on its own STEP track
 *     or make it an event. Each frame costs one key
 *     lookup (O(1) while time moves forward a key at a time, a binary
 *     search after a jump or a repeat's wrap) or one division, plus the
 *     easing: cos() for COSINE, exp() and log() for LOG, at most 68
 *     cubic evaluations for BEZIER (a few on ordinary curves).
 *
 *   EVALUATE (psytl_frame, psytl_evaluate)
 *     Once per frame, before drawing, with the frame's PREDICTED onset on
 *     the RT clock, the frame period and the caller's frame number:
 *       psytl_frame f = { onset_ns, period_ns, index };
 *       int n = psytl_evaluate(tl, &f, fired, cap);
 *     For each base that is not stopped, in base order: fire every event
 *     the frame's window reaches that has not fired, then write the base's
 *     tracks. An evaluate never un-fires an event; only an anchor does
 *     (REWIND). Returns the number of events fired.
 *     The first min(n, cap) of them, ordered by RT target time (onset -
 *     residual) and then by id, are copied into fired[], landing fields
 *     included; the rest fired all the same and their records are in the
 *     storage (psytl_events). fired may be NULL with cap 0. Onsets must
 *     not decrease from one call to the next (PSYTL_ERR_ORDER); an equal
 *     onset is allowed and fires only what was added since. An evaluate
 *     changes nothing but the handle and the storage, and the same calls
 *     in the same order give the same bytes: that is REPLAY.
 *
 *   QUANTIZATION (desc.lead)
 *     An event lands on the first evaluated frame whose window reaches its
 *     time: on its base, time <= base time at (onset + lead x period),
 *     with lead x period truncated to whole nanoseconds. desc.lead:
 *       0                the default, 0.5: the nearest frame, up to half a
 *                        period early. A frame-aligned time lands on its
 *                        frame however the onset was rounded or predicted.
 *       (0, 1)           that fraction of a period early at most.
 *       PSYTL_LEAD_NONE  never early: the first frame whose onset is at or
 *                        after the event.
 *     The default is half a frame because a predicted onset carries noise.
 *     With 2 us of noise on a 60 Hz display, never-early put about 12% of
 *     frame-aligned changes a frame late, and half a frame put none
 *     (docs/psy_timeline.md). Use PSYTL_LEAD_NONE only when "not before"
 *     matters more than "on the right frame". A script's wait(dt) lands on
 *     the frame nearest its target under the default.
 *     psytl_lead(tl) gives the lead in use: 0.5 for a desc lead of 0, the
 *     desc's lead in (0, 1), and 0 for PSYTL_LEAD_NONE, so that a window
 *     ends lead x period after the onset in every case. A header that
 *     places its own frames by the same rule (psy_video.h) reads it.
 *     Pass period 0 on a display with no fixed period (variable refresh),
 *     and lead does nothing. When it fires, the event records:
 *       frame     f.index of the frame it landed on
 *       onset     f.onset, the predicted onset of that frame
 *       residual  base time at f.onset minus the event's time: how late
 *                 (positive) or early (negative; never under
 *                 PSYTL_LEAD_NONE) the frame shows it. On a running base this equals onset
 *                 minus the event's RT time.
 *     The onset is a prediction. The flip record of the same frame number
 *     gives the measured one; the difference is the frame's error and
 *     applies to every event that landed on it.
 *     psytl_next_due(tl, &t) gives the earliest RT time of a pending event
 *     (not fired, not skipped) on a running base, for a display that can
 *     flip at a time, or for sleeping until there is something to do.
 *
 *   LATE EVENTS
 *     A base's REACH is the furthest window end any evaluate has reached
 *     on it since it was last rewound (REWIND sets it to just before the
 *     anchor's time). An event added at or before the reach is late: it
 *     fires at the first evaluate whose window reaches it, normally the
 *     next one, with PSYTL_EV_LATE set and a residual that says how late.
 *     The flag is set when the event fires; a pending event's flags are
 *     0. A window can end behind the reach (a pause with a lead, an
 *     anchor whose RT time is after the onset); a late event past the
 *     window then waits for it.
 *     It is how a script's "now" lands: add the event at the current time
 *     on any base and the next frame carries it.
 *
 *   REWIND
 *     psytl_anchor(tl, b, rt, bt) with bt at or before the furthest base
 *     time of an onset evaluated on the base (or before the time of its
 *     last skip, SKIP) moves the base back: every fired or skipped event
 *     of the base at or after bt, the one exactly at bt included, becomes
 *     pending again. Its landing fields reset (frame -1, onset 0, residual
 *     0, flags 0), the base's event-driven channels are recomputed from
 *     the events still fired or skipped, and the events fire again when
 *     the base reaches them. The base's reach moves to just before bt.
 *     Running a trial's events twice is two anchors at base time 0.
 *     An anchor at a later bt rewinds nothing. Nothing else rewinds: an
 *     anchor at an earlier bt whose RT time is after the next onset puts
 *     that onset before bt, and the events from bt on wait for the base
 *     to reach them again, while the fired events before bt stay fired.
 *     A re-anchor that corrects a drift by moving a base back by a small
 *     amount re-fires what lies in that amount; to avoid it, correct
 *     forward only.
 *
 *   SKIP (psytl_skip)
 *     psytl_skip(tl, b, rt, bt) is a seek that does not fire what it
 *     passes over. It is psytl_anchor(tl, b, rt, bt), and then every
 *     event of the base before bt that is still pending is skipped: its
 *     flags become PSYTL_EV_SKIPPED, its frame stays -1, its onset
 *     becomes rt and its residual bt minus its time (how far before the
 *     seek's target it was). A skipped event never fires and is in no
 *     evaluate's report. The return value, the number skipped, is the
 *     report of the skip; the storage holds each one. After a skip no
 *     event of the base before bt is pending, and the event exactly at
 *     bt fires as usual on the frame that reaches it.
 *     Forward, an anchor fires on the next frame every event it passed
 *     over, late by its residual; a skip fires none. On a movie base with
 *     an annotation every second, a 30-minute seek fires 1800 events on
 *     one frame after an anchor and none after a skip.
 *     Back, a skip is the anchor's REWIND, and then the late events
 *     before bt still waiting for a window are skipped. A seek calls
 *     psytl_skip() whatever its direction; refusing a skip back would
 *     make every caller compare bt with a time it cannot read.
 *     What else it does:
 *       channels  a skipped ONSET, OFFSET or SET counts for its channel
 *                 as a fired one does (CHANNELS), so the channel shows
 *                 what playing through to bt would, with no report.
 *       tracks    nothing: a track is a function of base time, and the
 *                 next evaluate reads it at its onset's base time. A
 *                 tween waiting for a start before bt takes over at the
 *                 next evaluate, from the old track's value at its start,
 *                 as it would without the skip. "now" is bt, as after an
 *                 anchor.
 *       reach     when the base's reach is before bt, it moves to just
 *                 before bt, and so does the furthest base time of an
 *                 evaluated onset. So an event added later at a time
 *                 before bt is late (LATE EVENTS), and a later anchor back
 *                 to a time before bt makes the skipped events from there
 *                 pending again (REWIND).
 *       state     the base runs from bt at rt, as after an anchor. A seek
 *                 that stays paused is a skip and a psytl_pause() at the
 *                 same rt.
 *     A skip costs a binary search and a write per event it skips.
 *
 *   PEEK (psytl_peek)
 *     psytl_peek(tl, b, from_rt, to_rt, out, cap) lists the pending events
 *     of base b (or of every base, PSYTL_ALL_BASES) whose RT time is in
 *     [from_rt, to_rt), and changes nothing: no flag, landing, channel,
 *     reach or "now". An event's RT time is when its running base reaches
 *     its time: the anchor's rt + (time - the anchor's base time). A
 *     paused or stopped base has no RT times and gives no event; a fired
 *     or skipped event is not pending. Returns how many events are in the
 *     window; the first min(that, cap) are copied into out[] ordered by
 *     RT time, then id, each with onset set to its RT time, residual 0,
 *     frame -1 and flags 0. So onset - residual is the RT time in a peek
 *     as in a report. out may be NULL with cap 0, to count.
 *     It is the look-ahead for sound. An evaluate fires an event on the
 *     frame that shows it, at most lead x period early, and psy_audio.h
 *     needs an onset 43.5 to 54.5 ms before its time (docs/psy_audio.md).
 *     The audio path peeks [last to_rt, now + its lead) each frame and
 *     schedules by id: half-open windows that follow each other neither
 *     overlap nor leave a gap. The event still fires on its frame, and
 *     its landing record is the frame's. An event added less than the
 *     audio lead before its time can be in no peek before its frame
 *     fires it; it is in that evaluate's report, and the caller plays it
 *     late from there. An anchor, skip, pause or resume moves the RT
 *     times, so peek again after one. The cost is a binary search per
 *     base and a read per event in the window.
 *
 *   REMOVING (psytl_remove, psytl_prune, psytl_clear)
 *     psytl_remove(tl, id) deletes one event; when it had fired and its
 *     channel showed its value, the channel is recomputed.
 *     psytl_prune(tl, base) deletes every fired or skipped event of a base
 *     (PSYTL_ALL_BASES for all): for a long session on the RT base, where
 *     a script adds waits forever and the caller has logged the reports.
 *     Channels keep their values; a channel left with no events is free,
 *     and a later rewind of the base recomputes from what remains.
 *     psytl_clear(tl, base) deletes every event of a base and the keys of
 *     every channel bound to it, returns those channels to their initial
 *     values and frees them. The base keeps its state and anchor. Ids are
 *     never reused before the next open().
 *
 *   REPLAY
 *     A timeline is a function of the calls made on it. The same open(),
 *     the same adds, anchors and keys, and the same frames (onset, period,
 *     index) produce the same values, the same reports and the same
 *     storage bytes, every time and on every thread. The storage is the
 *     record of where every event landed. Two builds agree on every event
 *     field bit for bit (integer arithmetic) and on track values to the
 *     libm in use: COSINE and LOG call cos(), exp() and log(), whose last
 *     bit may differ between C libraries. The other easings, BEZIER, and
 *     the sampled interpolations are +, -, * and / in double, exact under
 *     IEEE 754 unless the compiler contracts them into fused multiply-adds
 *     (-ffp-contract=fast, or /fp:fast). So a trajectory the pack tool
 *     computed is the same bits on every platform; the same trajectory
 *     computed at run time with sin() would not be.
 *
 *   WITH psy_trials.h
 *     The trial handler picks the condition, the caller turns it into
 *     events and keys on a trial base, anchors the base at the trial's
 *     first frame, and the timeline runs it. Nothing links the two
 *     headers. At movie scale, the storage holds thousands of annotations
 *     on a movie base, anchored, paused and skipped with the player.
 *
 *   ---------------------------------------------------------------------
 *   MEMORY AND THREADS
 *   ---------------------------------------------------------------------
 *   The handle is about 60 KB at the default PSYTL_MAX_CHANNELS (256),
 *   almost all of it 232 bytes per channel: a copy of its track, a
 *   tween's three keys and curve, and a waiting tween's desc. An evaluate
 *   reads a byte per channel to
 *   find the tracked ones and touches only their records. Define
 *   PSYTL_MAX_CHANNELS (1 to 65535) before the implementation and every
 *   include to change it. Events live in the storage the desc gives
 *   (desc.events, desc.event_capacity entries of 64 bytes), owned by the
 *   caller and used while the handle is. Keys, curves and samples are the
 *   caller's arrays. Nothing is allocated.
 *   A frame's cost is a binary search per base that is not stopped, the
 *   events that fire (each a copy, plus an insertion into the report),
 *   and one key lookup per keyed channel, which is O(1) while time moves
 *   forward a segment at a time. Nothing depends on the refresh rate:
 *   the period is an argument, times are whole nanoseconds, and a faster
 *   display only moves the key cache less per frame. The per-frame cost
 *   measured the same at 60, 500 and 1000 Hz (docs/psy_timeline.md), so
 *   the share of the budget grows with the rate and nothing else does.
 *   examples/timeline_bench.c [refresh_hz] measures it.
 *   No function is safe to call on one handle from two threads at once.
 *   The intended use is the frame thread only.
 *
 *   ---------------------------------------------------------------------
 *   BUILDING
 *   ---------------------------------------------------------------------
 *   Link libm on Linux (-lm). Define PSYTL_API to change the linkage of
 *   every function (for example `static`); with `static`, a translation
 *   unit that does not call every function needs -Wno-unused-function.
 *
 *   LICENSE: public domain / MIT-0, see end of file.
 */
#ifndef PSY_TIMELINE_H_INCLUDED
#define PSY_TIMELINE_H_INCLUDED

/* The version of this header, for a log line or a compile-time check. */
#define PSYTL_VERSION_MAJOR 0
#define PSYTL_VERSION_MINOR 3
#define PSYTL_VERSION_PATCH 0
#define PSYTL_VERSION_STRING "0.3.0"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PSYTL_API
#define PSYTL_API extern
#endif

#ifndef PSYTL_MAX_CHANNELS
#define PSYTL_MAX_CHANNELS 256
#endif
#define PSYTL_MAX_BASES 8

/* A channel number is stored in an event's int32 target, and the handle
 * holds one record per channel, so the bound is about handle size. */
#if PSYTL_MAX_CHANNELS < 1 || PSYTL_MAX_CHANNELS > 65535
#error "PSYTL_MAX_CHANNELS must be in [1, 65535]"
#endif

/* --- codes ------------------------------------------------------------- */

#define PSYTL_ERR_ARG       (-1)
#define PSYTL_ERR_CLOSED    (-2)  /* the handle is not open                 */
#define PSYTL_ERR_ORDER     (-3)  /* an onset earlier than the last one     */
#define PSYTL_ERR_FULL      (-4)  /* the storage or the ids are used up     */
#define PSYTL_ERR_BOUND     (-5)  /* the channel is bound to another base,
                                   * or to the other kind of driver        */
#define PSYTL_ERR_NOT_FOUND (-6)  /* no event has that id                   */

#define PSYTL_BASE_RT     0     /* the RT clock; always running             */
#define PSYTL_ALL_BASES (-1)    /* psytl_prune, psytl_clear, psytl_events   */
#define PSYTL_LEAD_NONE (-1.0)  /* desc.lead: never early; 0 is the default,
                                 * half a frame                             */

#define PSYTL_NS_PER_S  INT64_C(1000000000)
#define PSYTL_NS_PER_MS INT64_C(1000000)

/* Seconds and milliseconds to ns, rounded to nearest, for literals in a
 * desc: .duration = PSYTL_MS(200). Constant expressions, so they work in a
 * static initializer. For a run-time double, psytl_ns() also clamps. */
#define PSYTL_S(x)  ((int64_t)((x) * 1e9 + ((x) < 0 ? -0.5 : 0.5)))
#define PSYTL_MS(x) ((int64_t)((x) * 1e6 + ((x) < 0 ? -0.5 : 0.5)))

#define PSYTL_FOREVER (-1)      /* psytl_tween_desc.cycles                  */

/* Static description of a PSYTL_ERR_* code ("ok" for values >= 0). */
PSYTL_API const char* psytl_strerror(int code);

/* PSYTL_VERSION_STRING as compiled into the implementation. */
PSYTL_API const char* psytl_version(void);

/* Seconds to nanoseconds, rounded to nearest, ties away from zero, and
 * clamped to +-2^62. NaN gives 0. */
PSYTL_API int64_t psytl_ns(double seconds);

/* --- events ------------------------------------------------------------ */

typedef enum psytl_kind {
    PSYTL_MARK      = 0,   /* reported only                                */
    PSYTL_TRIGGER   = 1,   /* reported only; code is the port value        */
    PSYTL_ONSET     = 2,   /* channel target := 1                          */
    PSYTL_OFFSET    = 3,   /* channel target := 0                          */
    PSYTL_SET       = 4,   /* channel target := value                      */
    PSYTL_KIND_USER = 16   /* 16..255: reported only, the caller's meaning */
} psytl_kind;

/* Event flags, set by the header. */
#define PSYTL_EV_FIRED   1u /* it has landed on a frame                     */
#define PSYTL_EV_LATE    2u /* it was added after its window had passed     */
#define PSYTL_EV_SKIPPED 4u /* a psytl_skip() passed over it; never fires   */

/* One event. The caller fills time, base, kind and its payload; the header
 * assigns id and writes the landing fields. 64 bytes. */
typedef struct psytl_event {
    int64_t  time;      /* on its base, ns                                  */
    int32_t  target;    /* the channel for ONSET, OFFSET and SET; the
                         * caller's for the other kinds                     */
    float    value;     /* SET's value; must be finite                      */
    int32_t  code;      /* the caller's: a trigger value, an annotation row */
    uint8_t  base;      /* 0 .. PSYTL_MAX_BASES-1                           */
    uint8_t  kind;      /* a psytl_kind                                     */
    uint16_t flags;     /* PSYTL_EV_*; ignored on add                       */
    uint64_t user;      /* the caller's: a pointer, a coroutine, a string id */
    int32_t  id;        /* assigned on add; ignored on add                  */
    int32_t  reserved_; /* zero                                             */
    /* landing, written when it fires; frame -1 while pending and when
     * skipped (SKIP), and the RT time in a psytl_peek() copy (PEEK) */
    int64_t  frame;     /* psytl_frame.index of the frame it landed on      */
    int64_t  onset;     /* that frame's predicted onset, RT ns              */
    int64_t  residual;  /* base time at onset minus time, ns                */
} psytl_event;

/* --- tracks ------------------------------------------------------------ */

typedef enum psytl_ease {
    PSYTL_EASE_LINEAR   = 0,
    PSYTL_EASE_STEP     = 1,
    PSYTL_EASE_COSINE   = 2,
    PSYTL_EASE_QUAD_IN  = 3,
    PSYTL_EASE_QUAD_OUT = 4,
    PSYTL_EASE_LOG      = 5,
    PSYTL_EASE_BEZIER   = 6    /* key.curve indexes the track's curves[]   */
} psytl_ease;

/* One key: the value at a time on the track's base, and the easing of the
 * segment that starts here. The last key's ease is not used. 16 bytes. */
typedef struct psytl_key {
    int64_t  time;
    float    value;     /* must be finite                                   */
    uint8_t  ease;      /* a psytl_ease                                     */
    uint8_t  reserved_;
    uint16_t curve;     /* PSYTL_EASE_BEZIER: index into the track's curves */
} psytl_key;

/* A cubic Bezier easing from (0, 0) to (1, 1) with control points (x1, y1)
 * and (x2, y2), as CSS cubic-bezier() takes them. x1 and x2 in [0, 1]. */
typedef struct psytl_curve {
    float x1, y1, x2, y2;
} psytl_curve;

/* How a sampled track reads between two samples. */
typedef enum psytl_interp {
    PSYTL_INTERP_LINEAR = 0,
    PSYTL_INTERP_STEP   = 1,   /* the sample at or before                   */
    PSYTL_INTERP_CUBIC  = 2    /* Catmull-Rom through the samples           */
} psytl_interp;

/* A track: keys at any times, or samples at a fixed step, and an optional
 * repeat (TRACKS). Exactly one of keys and samples is set. */
typedef struct psytl_track {
    const psytl_key*   keys;       /* keyed: n_keys keys sorted by time     */
    const psytl_curve* curves;     /* keyed: for PSYTL_EASE_BEZIER keys     */
    const float*       samples;    /* sampled: samples[i] at t0 + i/rate s  */
    int64_t            t0;
    int64_t            rate;       /* sampled: samples per second, 1..1e9   */
    int64_t            period;     /* > 0: repeat every period ns           */
    int                n_keys;
    int                n_curves;
    int                n_samples;
    int                interp;     /* sampled: a psytl_interp               */
    int                repeats;    /* with period: cycles, 0 = forever      */
} psytl_track;

/* The value of a track at time t (TRACKS), or 0 when it has neither keys
 * nor samples. No validation: the track is assumed to be one that
 * psytl_set_track() accepts. */
PSYTL_API double psytl_sample(const psytl_track* tr, int64_t t);

/* A tween (TWEENS). Every field may be zero: a zeroed desc is a linear
 * jump to 0 now. GSAP's gsap.to(g, {contrast: 0, duration: 0.2,
 * ease: "sine.inOut"}) is
 *     psytl_tween(&tl, CONTRAST, TRIAL, &(psytl_tween_desc){
 *         .to = 0.0f, .duration = PSYTL_MS(200), .ease = PSYTL_EASE_COSINE });
 */
typedef struct psytl_tween_desc {
    float       to;
    float       from;           /* with from_set; else the value at start   */
    int64_t     duration;       /* ns, >= 0; 0 jumps                        */
    int64_t     start;          /* base time, with start_set; else now      */
    int64_t     delay;          /* added to start, either way               */
    psytl_curve curve;          /* PSYTL_EASE_BEZIER only                   */
    int         ease;           /* a psytl_ease; 0 = LINEAR                 */
    int         cycles;         /* 0 or 1: once; n; PSYTL_FOREVER           */
    bool        from_set;
    bool        start_set;
    bool        yoyo;           /* a cycle goes to `to` and back            */
    bool        keep_velocity;  /* start at the track's velocity, end at rest */
} psytl_tween_desc;

/* --- description and handle ------------------------------------------- */

typedef struct psytl_desc {
    psytl_event* events;          /* storage, event_capacity entries; the
                                   * caller's, used while the handle is     */
    int          event_capacity;  /* >= 0                                   */
    int          n_channels;      /* 0 .. PSYTL_MAX_CHANNELS                */
    const float* initial;         /* n_channels initial values, copied at
                                   * open; NULL: all 0                      */
    double       lead;            /* the fraction of a period an event may
                                   * land early, (0, 1); 0: 0.5, the
                                   * nearest frame; PSYTL_LEAD_NONE: never
                                   * early. QUANTIZATION                    */
} psytl_desc;

/* The frame being drawn. */
typedef struct psytl_frame {
    int64_t onset;    /* predicted onset, RT ns; never decreasing           */
    int64_t period;   /* the frame period, ns, >= 0; 0 when there is none   */
    int64_t index;    /* the caller's frame number, recorded with landings  */
} psytl_frame;

/* Private. One time base. */
typedef struct psytl__base {
    int64_t anchor_rt;     /* running: base time anchor_bt at RT anchor_rt  */
    int64_t anchor_bt;     /* paused: the frozen base time                  */
    int64_t last_hi;       /* the furthest window end evaluated (the reach) */
    int64_t last_now;      /* the furthest base time of an evaluated onset  */
    int64_t now_bt;        /* "now" for a tween: the last evaluated onset's
                            * base time, or what a later anchor, pause or
                            * resume set; not last_now, which only moves
                            * forward between rewinds                     */
    int32_t begin;         /* first event of the base in the storage        */
    int32_t count;
    int32_t hi;            /* events (from begin) with time <= last_hi      */
    int32_t dirty;         /* lowest late event (from begin), or INT32_MAX  */
    uint8_t state;         /* 0 stopped, 1 running, 2 paused                */
    uint8_t evaluated;     /* last_hi is meaningful                         */
} psytl__base;

/* Private. One channel's driver. */
typedef struct psytl__chan {
    psytl_track tr;          /* a copy; keys and curves are the caller's,
                              * unless `own` says they are ik and ic        */
    psytl_key   ik[3];       /* a tween's keys (3 with yoyo)                */
    psytl_curve ic;          /* a tween's curve, or its start slope         */
    psytl_tween_desc pend;   /* a tween waiting for its start, resolved:
                              * pend.start is absolute, pend.delay 0        */
    int64_t  last_time;      /* (time, id) of the event the value is from   */
    int32_t  last_id;
    int32_t  seg;            /* the key before the last sample, -1 before
                              * the first                                   */
    int32_t  n_events;       /* events naming it                            */
    float    initial;
    uint8_t  base;           /* valid while n_events > 0 or tracked         */
    uint8_t  has_last;       /* last_time and last_id are set               */
    uint8_t  mark;           /* scratch: lost an event in this compaction   */
    uint8_t  own;            /* the track is a tween in ik and ic           */
    uint8_t  stepped;        /* only STEP segments: read at the lead window */
    uint8_t  has_track;      /* tr (or ik) holds a track to sample          */
    uint8_t  has_pend;       /* pend is waiting                             */
    uint8_t  pend_stepped;   /* pend will be a STEP track                   */
} psytl__chan;

/* Handle. The caller allocates it and treats every field as opaque.
 * psytl_open() resets it, so it may be reused. */
typedef struct psytl_timeline {
    psytl_event* events;
    int32_t      n_events;
    int32_t      capacity;
    int32_t      next_id;
    int32_t      n_channels;
    double       lead;
    int64_t      last_onset;
    bool         has_onset;
    bool         open;
    psytl__base  bases[PSYTL_MAX_BASES];
    float        values[PSYTL_MAX_CHANNELS];   /* contiguous, for upload    */
    uint8_t      track_base[PSYTL_MAX_CHANNELS]; /* base + 1 of the channel's
                                                * track, 0 for none: the
                                                * evaluate scans this, not
                                                * the records              */
    psytl__chan  chans[PSYTL_MAX_CHANNELS];
    char         error[192];
} psytl_timeline;

/* --- lifecycle --------------------------------------------------------- */

/* Validate `desc` and reset the handle: no events, base 0 running and the
 * others stopped, channels at their initial values. Returns false with
 * psytl_error() set on a bad or NULL desc, and the handle closed. The only function that writes the
 * message; the others return PSYTL_ERR_* codes. */
PSYTL_API bool psytl_open(psytl_timeline* tl, const psytl_desc* desc);

/* The last open() message ("" after a success). */
PSYTL_API const char* psytl_error(const psytl_timeline* tl);
PSYTL_API bool        psytl_is_open(const psytl_timeline* tl);

/* --- time bases -------------------------------------------------------- */

/* Run base `base` (1 .. PSYTL_MAX_BASES-1) with base time `base_time` at RT
 * time `rt`, from any state. A base_time at or before the furthest base
 * time evaluated on it rewinds it (REWIND). Returns 0, PSYTL_ERR_ARG (base
 * 0 or out of range, |base_time| >= 2^62) or PSYTL_ERR_CLOSED. Pause,
 * resume and stop also return PSYTL_ERR_ARG for base 0. */
PSYTL_API int psytl_anchor(psytl_timeline* tl, int base, int64_t rt, int64_t base_time);

/* Freeze a running base at its time at RT time `rt`. PSYTL_ERR_ORDER when
 * the base is not running. */
PSYTL_API int psytl_pause(psytl_timeline* tl, int base, int64_t rt);

/* Run a paused base again from its frozen time at RT time `rt`.
 * PSYTL_ERR_ORDER when the base is not paused. */
PSYTL_API int psytl_resume(psytl_timeline* tl, int base, int64_t rt);

/* Stop a base: nothing on it fires, rewinds or writes a channel until it is
 * anchored again. */
PSYTL_API int psytl_stop(psytl_timeline* tl, int base);

/* Seek base `base` to `base_time` at RT time `rt` without firing what lies
 * between (SKIP): psytl_anchor(), then every pending event of the base
 * before base_time is marked PSYTL_EV_SKIPPED and never fires. Returns the
 * number skipped (>= 0), or what psytl_anchor() returns on an error, with
 * nothing changed. */
PSYTL_API int psytl_skip(psytl_timeline* tl, int base, int64_t rt, int64_t base_time);

/* The base's time at RT time `rt` into *out, and true, when the base is
 * running or paused (a paused base gives its frozen time); false when it is
 * stopped or the arguments are bad. */
PSYTL_API bool psytl_base_time(const psytl_timeline* tl, int base, int64_t rt, int64_t* out);

/* --- events ------------------------------------------------------------ */

/* Add one event (EVENTS). Returns its id (>= 0) or PSYTL_ERR_ARG (bad base,
 * kind, channel or value), PSYTL_ERR_BOUND, PSYTL_ERR_FULL. */
PSYTL_API int psytl_add(psytl_timeline* tl, const psytl_event* e);

/* Add n events, all or none. Returns the first id (the others follow), or
 * an error as psytl_add() with nothing added. n = 0 returns the next id. */
PSYTL_API int psytl_add_n(psytl_timeline* tl, const psytl_event* e, int n);

/* Delete one event by id. Returns 0 or PSYTL_ERR_NOT_FOUND. */
PSYTL_API int psytl_remove(psytl_timeline* tl, int id);

/* Delete every fired or skipped event of `base` (or PSYTL_ALL_BASES).
 * Returns the number deleted. Channel values stay. */
PSYTL_API int psytl_prune(psytl_timeline* tl, int base);

/* Delete every event of `base` (or PSYTL_ALL_BASES) and the keys of the
 * channels bound to it; those channels return to their initial values.
 * Returns the number of events deleted. */
PSYTL_API int psytl_clear(psytl_timeline* tl, int base);

/* --- tracks ------------------------------------------------------------ */

/* Drive `channel` from keys[0..n) on `base`: psytl_set_track() with only
 * keys and n_keys set (no curves, no repeat); n = 0 removes the track. The
 * value changes at the next evaluate, or at once to the initial value when
 * the track is removed. */
PSYTL_API int psytl_set_keys(psytl_timeline* tl, int channel, int base,
                             const psytl_key* keys, int n);

/* Drive `channel` from `tr` on `base` (TRACKS), replacing any track it
 * had; tr NULL removes it. psytl_set_keys() is this with keys only. The
 * struct is copied; the arrays it points at stay the caller's. Returns 0,
 * PSYTL_ERR_ARG (TRACKS lists what is checked) or PSYTL_ERR_BOUND (the
 * channel has events). */
PSYTL_API int psytl_set_track(psytl_timeline* tl, int channel, int base,
                              const psytl_track* tr);

/* Tween `channel` on `base` as `d` says (TWEENS). The tween waits until its
 * start while the channel's track runs on, then takes over from the value
 * the track had there. Its keys live in the handle. Returns 0,
 * PSYTL_ERR_ARG (TWEENS lists what is checked) or PSYTL_ERR_BOUND (the
 * channel has events). */
PSYTL_API int psytl_tween(psytl_timeline* tl, int channel, int base,
                          const psytl_tween_desc* d);

/* --- the frame --------------------------------------------------------- */

/* Evaluate the frame (EVALUATE). Returns the number of events that fired,
 * the first min(that, cap) copied into fired[] in RT target order, or
 * PSYTL_ERR_ARG (period < 0, cap < 0, fired NULL with cap > 0) or
 * PSYTL_ERR_ORDER (onset below the last one). */
PSYTL_API int psytl_evaluate(psytl_timeline* tl, const psytl_frame* f,
                             psytl_event* fired, int cap);

/* The earliest RT time of a pending event on a running base, and true; a
 * late event's time is in the past. False when nothing is pending. */
PSYTL_API bool psytl_next_due(const psytl_timeline* tl, int64_t* rt);

/* The pending events of `base` (or PSYTL_ALL_BASES) whose RT time is in
 * [from_rt, to_rt), on running bases only, without changing anything
 * (PEEK). Returns how many; the first min(that, cap) are copied into out[]
 * in RT time order, then id, with onset = the RT time, residual 0 and
 * frame -1. PSYTL_ERR_ARG for a bad base, cap < 0, out NULL with cap > 0,
 * from_rt > to_rt, or a bound at or past +-2^62. */
PSYTL_API int psytl_peek(const psytl_timeline* tl, int base, int64_t from_rt, int64_t to_rt,
                         psytl_event* out, int cap);

/* The lead in use (QUANTIZATION): 0.5 for a desc lead of 0, 0 for
 * PSYTL_LEAD_NONE, else the desc's. A window ends lead x period, truncated
 * to whole ns, after the onset. 0 when the handle is not open. */
PSYTL_API double psytl_lead(const psytl_timeline* tl);

/* --- state and record -------------------------------------------------- */

/* The channel values, n_channels floats, into the handle. NULL when the
 * handle is not open. */
PSYTL_API const float* psytl_values(const psytl_timeline* tl);
PSYTL_API float        psytl_value(const psytl_timeline* tl, int channel);

/* The events of `base` (PSYTL_ALL_BASES: all, grouped by base), sorted by
 * time then id, with their landing fields: the record. The pointer is into
 * the storage and moves with the next add or delete. */
PSYTL_API const psytl_event* psytl_events(const psytl_timeline* tl, int base, int* n);

/* The event with this id, or NULL. A linear scan. */
PSYTL_API const psytl_event* psytl_find(const psytl_timeline* tl, int id);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PSY_TIMELINE_H_INCLUDED */

/* ======================================================================= *
 *                             IMPLEMENTATION                              *
 * ======================================================================= */
#ifdef PSY_TIMELINE_IMPLEMENTATION
#ifndef PSY_TIMELINE_IMPLEMENTATION_GUARD
#define PSY_TIMELINE_IMPLEMENTATION_GUARD

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define PSYTL__STOPPED 0
#define PSYTL__RUNNING 1
#define PSYTL__PAUSED  2
#define PSYTL__NONE    INT32_MAX
#define PSYTL__LIMIT   (INT64_C(1) << 62)

/* An event the base has passed: fired, or skipped by psytl_skip(). Every
 * rule that asks "is it still to come" asks this, so a skipped event is
 * never fired late and counts for its channel. */
#define PSYTL__DONE    (PSYTL_EV_FIRED | PSYTL_EV_SKIPPED)

/* A tween with keep_velocity: a cubic Hermite segment whose start tangent
 * is curves[0].x1 and whose end tangent is 0. Never accepted from a
 * caller's keys, which stop at PSYTL_EASE_BEZIER. */
#define PSYTL__EASE_HERMITE 255

/* isfinite() is C99 and MSVC's default C dialect does not declare it, so the
 * test is written in comparisons a NaN loses on its own. */
static bool psytl__finite(double x) {
    return x > -HUGE_VAL && x < HUGE_VAL;
}

PSYTL_API const char* psytl_strerror(int code) {
    switch (code) {
    case PSYTL_ERR_ARG:       return "invalid argument";
    case PSYTL_ERR_CLOSED:    return "timeline is not open";
    case PSYTL_ERR_ORDER:     return "call out of order";
    case PSYTL_ERR_FULL:      return "event storage is full";
    case PSYTL_ERR_BOUND:     return "channel is bound elsewhere";
    case PSYTL_ERR_NOT_FOUND: return "no event with that id";
    default:                  return code >= 0 ? "ok" : "unknown error";
    }
}

PSYTL_API const char* psytl_version(void) { return PSYTL_VERSION_STRING; }

PSYTL_API int64_t psytl_ns(double seconds) {
    double ns = seconds * 1e9;
    if (!(ns == ns)) return 0;
    if (ns >= (double)PSYTL__LIMIT) return PSYTL__LIMIT;
    if (ns <= -(double)PSYTL__LIMIT) return -PSYTL__LIMIT;
    return ns >= 0.0 ? (int64_t)(ns + 0.5) : -(int64_t)(-ns + 0.5);
}

static bool psytl__fail(psytl_timeline* tl, const char* fmt, ...) {
    va_list ap;
    int n;
    n = snprintf(tl->error, sizeof(tl->error), "psy_timeline: ");
    if (n < 0) n = 0;
    va_start(ap, fmt);
    vsnprintf(tl->error + n, sizeof(tl->error) - (size_t)n, fmt, ap);
    va_end(ap);
    tl->open = false;
    return false;
}

/* --- tracks ------------------------------------------------------------ */

/* A cubic Bezier coordinate with end points 0 and 1. */
static double psytl__bez(double p1, double p2, double s) {
    double r = 1.0 - s;
    return 3.0 * r * r * s * p1 + 3.0 * r * s * s * p2 + s * s * s;
}

/* The curve's y where its x is u. x(s) is monotone because x1 and x2 are in
 * [0, 1]. Newton from s = u converges in a few steps on ordinary curves;
 * bisection takes over when a step leaves [0, 1] or the slope vanishes. */
static double psytl__bezier(const psytl_curve* c, double u) {
    double s = u, x, d, r, lo = 0.0, hi = 1.0;
    int i;
    for (i = 0; i < 8; i++) {
        x = psytl__bez(c->x1, c->x2, s) - u;
        if (x > -1e-12 && x < 1e-12) return psytl__bez(c->y1, c->y2, s);
        r = 1.0 - s;
        d = 3.0 * r * r * c->x1 + 6.0 * r * s * (c->x2 - c->x1) + 3.0 * s * s * (1.0 - c->x2);
        if (d > -1e-9 && d < 1e-9) break;
        s -= x / d;
        if (!(s >= 0.0 && s <= 1.0)) break;
    }
    for (i = 0; i < 60; i++) {
        s = 0.5 * (lo + hi);
        x = psytl__bez(c->x1, c->x2, s) - u;
        if (x > -1e-12 && x < 1e-12) break;
        if (x < 0.0) lo = s;
        else hi = s;
    }
    return psytl__bez(c->y1, c->y2, s);
}

static double psytl__ease(const psytl_key* a, const psytl_curve* curves,
                          double v0, double v1, double u) {
    switch (a->ease) {
    case PSYTL_EASE_STEP:     return v0;
    case PSYTL_EASE_COSINE:   return v0 + (v1 - v0) * (0.5 - 0.5 * cos(3.14159265358979323846 * u));
    case PSYTL_EASE_QUAD_IN:  return v0 + (v1 - v0) * (u * u);
    case PSYTL_EASE_QUAD_OUT: return v0 + (v1 - v0) * (1.0 - (1.0 - u) * (1.0 - u));
    case PSYTL_EASE_LOG:      return v0 * exp(u * log(v1 / v0));
    case PSYTL_EASE_BEZIER:   return v0 + (v1 - v0) * psytl__bezier(&curves[a->curve], u);
    case PSYTL__EASE_HERMITE: {
        double u2 = u * u, u3 = u2 * u;
        return (2.0 * u3 - 3.0 * u2 + 1.0) * v0 + (u3 - 2.0 * u2 + u) * curves[0].x1
               + (3.0 * u2 - 2.0 * u3) * v1;
    }
    default:                  return v0 + (v1 - v0) * u;
    }
}

/* The last key with time <= t, or -1. */
static int psytl__find_key(const psytl_key* keys, int n, int64_t t) {
    int lo = 0, hi = n;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (keys[mid].time <= t) lo = mid + 1;
        else hi = mid;
    }
    return lo - 1;
}

static double psytl__keys_at(const psytl_key* keys, int n, const psytl_curve* curves,
                             int i, int64_t t) {
    const psytl_key* a;
    const psytl_key* b;
    if (i < 0) return keys[0].value;
    if (i >= n - 1) return keys[n - 1].value;
    a = &keys[i];
    b = &keys[i + 1];
    return psytl__ease(a, curves, a->value, b->value,
                       (double)(t - a->time) / (double)(b->time - a->time));
}

/* Sample k is at t0 + k * 1e9 / rate exactly. A step in whole ns would
 * not do: at 120 samples/s it is a third of a ns short, 24 us by the end of
 * 10 minutes, which measured as a tenfold larger error on a moving target.
 * Splitting d into seconds and ns keeps every product below 2^63 for
 * rate <= 1e9: a * rate <= 4.7e18 and b * rate < 1e18. */
static double psytl__samples_at(const psytl_track* tr, int64_t t) {
    const float* v = tr->samples;
    int n = tr->n_samples, i;
    int64_t d, a, b, k, rem;
    double u, p0, p1, p2, p3;
    if (t <= tr->t0) return v[0];
    d = t - tr->t0;
    a = d / PSYTL_NS_PER_S;
    b = d % PSYTL_NS_PER_S;
    k = a * tr->rate + (b * tr->rate) / PSYTL_NS_PER_S;
    if (k >= n - 1) return v[n - 1];
    i = (int)k;
    if (tr->interp == PSYTL_INTERP_STEP) return v[i];
    rem = (b * tr->rate) % PSYTL_NS_PER_S;
    u = (double)rem / (double)PSYTL_NS_PER_S;
    p1 = v[i];
    p2 = v[i + 1];
    if (tr->interp != PSYTL_INTERP_CUBIC) return p1 + (p2 - p1) * u;
    /* Past an end, a sample extrapolated in a line: repeating the end
     * sample instead flattens the slope there, which measured 3 times the
     * error in the end segments on a sum of sines. */
    p0 = i > 0 ? v[i - 1] : 2.0 * p1 - p2;
    p3 = i + 2 < n ? v[i + 2] : 2.0 * p2 - p1;
    return p1 + 0.5 * u * (p2 - p0 + u * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3
                                          + u * (3.0 * (p1 - p2) + p3 - p0)));
}

/* Fold t into the track's cycle. After the last cycle the track holds its
 * value at the end of a cycle, read without folding. */
static int64_t psytl__cycle(const psytl_track* tr, int64_t start, int64_t t) {
    int64_t q;
    if (tr->period <= 0 || t <= start) return t;
    q = (t - start) / tr->period;
    if (tr->repeats > 0 && q >= tr->repeats) return start + tr->period;
    return start + (t - start) % tr->period;
}

/* `keys` and `curves` come apart from tr because a tween's live in the
 * channel. `seg` caches the key segment: O(1) while time moves forward a
 * segment at a time, a binary search after a jump or a cycle's wrap. */
static double psytl__track_at(const psytl_track* tr, const psytl_key* keys,
                              const psytl_curve* curves, int64_t t, int32_t* seg) {
    if (keys) {
        int n = tr->n_keys, i;
        bool ok;
        t = psytl__cycle(tr, keys[0].time, t);
        if (!seg) return psytl__keys_at(keys, n, curves, psytl__find_key(keys, n, t), t);
        i = *seg;
        ok = (i < 0 ? t < keys[0].time : keys[i].time <= t)
             && (i + 1 >= n || t < keys[i + 1].time);
        if (!ok && i + 2 <= n && keys[i + 1].time <= t
            && (i + 2 >= n || t < keys[i + 2].time)) {
            i++;
            ok = true;
        }
        if (!ok) i = psytl__find_key(keys, n, t);
        *seg = i;
        return psytl__keys_at(keys, n, curves, i, t);
    }
    if (tr->samples && tr->n_samples > 0)
        return psytl__samples_at(tr, psytl__cycle(tr, tr->t0, t));
    return 0.0;
}

PSYTL_API double psytl_sample(const psytl_track* tr, int64_t t) {
    if (!tr) return 0.0;
    return psytl__track_at(tr, tr->keys && tr->n_keys > 0 ? tr->keys : NULL,
                           tr->curves, t, NULL);
}

static float psytl__sample_chan(psytl__chan* c, int64_t t) {
    return (float)psytl__track_at(&c->tr, c->own ? c->ik : c->tr.keys,
                                  c->own ? &c->ic : c->tr.curves, t, &c->seg);
}

/* --- lifecycle --------------------------------------------------------- */

PSYTL_API bool psytl_open(psytl_timeline* tl, const psytl_desc* d) {
    int i;
    if (!tl) return false;
    memset(tl, 0, sizeof(*tl));
    if (!d) return psytl__fail(tl, "null desc");
    if (d->event_capacity < 0) return psytl__fail(tl, "desc.event_capacity is negative");
    if (d->event_capacity > 0 && !d->events)
        return psytl__fail(tl, "desc.events is NULL with event_capacity %d", d->event_capacity);
    if (d->n_channels < 0 || d->n_channels > PSYTL_MAX_CHANNELS)
        return psytl__fail(tl, "desc.n_channels must be in [0, %d]", PSYTL_MAX_CHANNELS);
    if (!(d->lead == PSYTL_LEAD_NONE || (d->lead >= 0.0 && d->lead < 1.0)))
        return psytl__fail(tl, "desc.lead must be in [0, 1) or PSYTL_LEAD_NONE");
    if (d->initial) {
        for (i = 0; i < d->n_channels; i++)
            if (!psytl__finite(d->initial[i]))
                return psytl__fail(tl, "desc.initial[%d] is not finite", i);
    }
    tl->events = d->events;
    tl->capacity = d->event_capacity;
    tl->n_channels = d->n_channels;
    tl->lead = d->lead == 0.0 ? 0.5 : d->lead == PSYTL_LEAD_NONE ? 0.0 : d->lead;
    for (i = 0; i < d->n_channels && i < PSYTL_MAX_CHANNELS; i++) {
        float v = d->initial ? d->initial[i] : 0.0f;
        tl->chans[i].initial = v;
        tl->chans[i].seg = -1;
        tl->values[i] = v;
    }
    for (i = 0; i < PSYTL_MAX_BASES; i++) tl->bases[i].dirty = PSYTL__NONE;
    tl->bases[PSYTL_BASE_RT].state = PSYTL__RUNNING;
    tl->open = true;
    return true;
}

PSYTL_API const char* psytl_error(const psytl_timeline* tl) {
    return tl ? tl->error : "psy_timeline: null handle";
}

PSYTL_API bool psytl_is_open(const psytl_timeline* tl) { return tl && tl->open; }

/* --- time bases -------------------------------------------------------- */

static bool psytl__user_base(const psytl_timeline* tl, int base) {
    return tl && tl->open && base > PSYTL_BASE_RT && base < PSYTL_MAX_BASES;
}

static void psytl__recompute(psytl_timeline* tl, int bi, int only);
static bool psytl__drives(int kind);
static void psytl__apply(psytl_timeline* tl, const psytl_event* e);

/* Make the base's events from `from` (relative) to its window pending
 * again. Late events there were never fired and lose only the late mark. */
static void psytl__unfire_from(psytl_timeline* tl, int bi, int from) {
    psytl__base* b = &tl->bases[bi];
    bool rewound = false;
    int i;
    for (i = from; i < b->hi; i++) {
        psytl_event* e = &tl->events[b->begin + i];
        if (e->flags & PSYTL__DONE) {
            rewound = rewound || psytl__drives(e->kind);
            e->frame = -1;
            e->onset = 0;
            e->residual = 0;
        }
        e->flags = 0;
    }
    if (from < b->hi) b->hi = from;
    if (b->dirty != PSYTL__NONE && b->dirty >= b->hi) b->dirty = PSYTL__NONE;
    if (rewound) psytl__recompute(tl, bi, -1);
}

/* First index in [lo, hi) of the storage whose time is > t. */
static int psytl__upper(const psytl_event* ev, int lo, int hi, int64_t t) {
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (ev[mid].time <= t) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

PSYTL_API int psytl_anchor(psytl_timeline* tl, int base, int64_t rt, int64_t base_time) {
    psytl__base* b;
    if (!tl || !tl->open) return PSYTL_ERR_CLOSED;
    if (!psytl__user_base(tl, base)) return PSYTL_ERR_ARG;
    if (base_time >= PSYTL__LIMIT || base_time <= -PSYTL__LIMIT) return PSYTL_ERR_ARG;
    b = &tl->bases[base];
    /* Moving the base back to base_time puts every event at or after it in
     * the base's future again, the one exactly at base_time included: that
     * is what makes a second anchor at 0 run a trial again. last_hi moves
     * to just before base_time to keep hi = upper(last_hi). */
    if (b->evaluated && base_time <= b->last_now) {
        int from = psytl__upper(tl->events, b->begin, b->begin + b->count,
                                base_time - 1) - b->begin;
        psytl__unfire_from(tl, base, from);
        b->last_hi = base_time - 1;
        b->last_now = base_time - 1;
    }
    b->state = PSYTL__RUNNING;
    b->anchor_rt = rt;
    b->anchor_bt = base_time;
    b->now_bt = base_time;
    return 0;
}

PSYTL_API int psytl_pause(psytl_timeline* tl, int base, int64_t rt) {
    psytl__base* b;
    if (!tl || !tl->open) return PSYTL_ERR_CLOSED;
    if (!psytl__user_base(tl, base)) return PSYTL_ERR_ARG;
    b = &tl->bases[base];
    if (b->state != PSYTL__RUNNING) return PSYTL_ERR_ORDER;
    b->anchor_bt += rt - b->anchor_rt;
    b->anchor_rt = rt;
    b->state = PSYTL__PAUSED;
    b->now_bt = b->anchor_bt;
    return 0;
}

PSYTL_API int psytl_resume(psytl_timeline* tl, int base, int64_t rt) {
    psytl__base* b;
    if (!tl || !tl->open) return PSYTL_ERR_CLOSED;
    if (!psytl__user_base(tl, base)) return PSYTL_ERR_ARG;
    b = &tl->bases[base];
    if (b->state != PSYTL__PAUSED) return PSYTL_ERR_ORDER;
    b->anchor_rt = rt;
    b->state = PSYTL__RUNNING;
    b->now_bt = b->anchor_bt;
    return 0;
}

PSYTL_API int psytl_stop(psytl_timeline* tl, int base) {
    if (!tl || !tl->open) return PSYTL_ERR_CLOSED;
    if (!psytl__user_base(tl, base)) return PSYTL_ERR_ARG;
    tl->bases[base].state = PSYTL__STOPPED;
    return 0;
}

PSYTL_API int psytl_skip(psytl_timeline* tl, int base, int64_t rt, int64_t base_time) {
    psytl__base* b;
    int rc, i, start, end, n = 0;
    /* A skip back must rewind as an anchor does, so it goes through the
     * anchor; the skip proper is only the marking below. */
    rc = psytl_anchor(tl, base, rt, base_time);
    if (rc < 0) return rc;
    b = &tl->bases[base];
    end = psytl__upper(tl->events, b->begin, b->begin + b->count, base_time - 1) - b->begin;
    /* Below min(dirty, hi) every event is done already. */
    start = b->dirty < b->hi ? b->dirty : b->hi;
    for (i = start; i < end; i++) {
        psytl_event* e = &tl->events[b->begin + i];
        if (e->flags & PSYTL__DONE) continue;
        e->flags = PSYTL_EV_SKIPPED;
        e->frame = -1;
        e->onset = rt;
        e->residual = base_time - e->time;
        if (psytl__drives(e->kind)) psytl__apply(tl, e);
        n++;
    }
    /* The base has passed everything before base_time: an add there is
     * late, and an anchor back there must rewind. Both rules read these. */
    if (!b->evaluated || base_time - 1 > b->last_hi) {
        b->last_hi = base_time - 1;
        b->hi = end;
    }
    if (!b->evaluated || base_time - 1 > b->last_now) b->last_now = base_time - 1;
    b->evaluated = 1;
    /* Only [end, hi) can still hold a late event: the window of a frame
     * before the skip may have reached past base_time. */
    if (b->dirty != PSYTL__NONE) {
        int d = b->dirty > end ? b->dirty : end;
        while (d < b->hi && (tl->events[b->begin + d].flags & PSYTL__DONE)) d++;
        b->dirty = d < b->hi ? d : PSYTL__NONE;
    }
    return n;
}

/* Base 0 has anchor (0, 0), which is the identity. */
static int64_t psytl__bt(const psytl__base* b, int64_t rt) {
    return b->state == PSYTL__PAUSED ? b->anchor_bt : b->anchor_bt + (rt - b->anchor_rt);
}

PSYTL_API bool psytl_base_time(const psytl_timeline* tl, int base, int64_t rt, int64_t* out) {
    const psytl__base* b;
    if (!tl || !tl->open || base < 0 || base >= PSYTL_MAX_BASES || !out) return false;
    b = &tl->bases[base];
    if (b->state == PSYTL__STOPPED) return false;
    *out = psytl__bt(b, rt);
    return true;
}

/* --- channels ---------------------------------------------------------- */

static bool psytl__drives(int kind) {
    return kind == PSYTL_ONSET || kind == PSYTL_OFFSET || kind == PSYTL_SET;
}

static float psytl__kind_value(const psytl_event* e) {
    return e->kind == PSYTL_ONSET ? 1.0f : e->kind == PSYTL_OFFSET ? 0.0f : e->value;
}

/* Apply a fired event to its channel unless a later (time, id) already set
 * it: the value is a function of which events have fired, not of the order
 * they fired in. */
static void psytl__apply(psytl_timeline* tl, const psytl_event* e) {
    psytl__chan* c = &tl->chans[e->target];
    if (c->has_last && (e->time < c->last_time
                        || (e->time == c->last_time && e->id < c->last_id)))
        return;
    c->has_last = 1;
    c->last_time = e->time;
    c->last_id = e->id;
    tl->values[e->target] = psytl__kind_value(e);
}

/* Recompute the event-driven channels of base `bi` (or only `only` when it
 * is >= 0) from the base's fired events, in time order. */
static void psytl__recompute(psytl_timeline* tl, int bi, int only) {
    const psytl__base* b = &tl->bases[bi];
    int i;
    for (i = 0; i < tl->n_channels; i++) {
        psytl__chan* c = &tl->chans[i];
        if (only >= 0 && i != only) continue;
        if (c->n_events > 0 && c->base == bi) {
            c->has_last = 0;
            tl->values[i] = c->initial;
        }
    }
    for (i = b->begin; i < b->begin + b->count; i++) {
        const psytl_event* e = &tl->events[i];
        if (!(e->flags & PSYTL__DONE) || !psytl__drives(e->kind)) continue;
        if (only >= 0 && e->target != only) continue;
        psytl__apply(tl, e);
    }
}

/* Called when a channel's last driver goes away. */
static void psytl__free_chan(psytl_timeline* tl, int ch, bool reset_value) {
    psytl__chan* c = &tl->chans[ch];
    c->has_last = 0;
    memset(&c->tr, 0, sizeof(c->tr));
    c->own = 0;
    c->has_track = 0;
    c->has_pend = 0;
    c->seg = -1;
    tl->track_base[ch] = 0;
    if (reset_value) tl->values[ch] = c->initial;
}

/* --- events ------------------------------------------------------------ */

static int psytl__check(const psytl_timeline* tl, const psytl_event* e) {
    if (e->base >= PSYTL_MAX_BASES) return PSYTL_ERR_ARG;
    if (e->kind > PSYTL_SET && e->kind < PSYTL_KIND_USER) return PSYTL_ERR_ARG;
    if (e->time >= PSYTL__LIMIT || e->time <= -PSYTL__LIMIT) return PSYTL_ERR_ARG;
    if (psytl__drives(e->kind)) {
        const psytl__chan* c;
        if (e->target < 0 || e->target >= tl->n_channels) return PSYTL_ERR_ARG;
        if (e->kind == PSYTL_SET && !psytl__finite(e->value)) return PSYTL_ERR_ARG;
        c = &tl->chans[e->target];
        if (tl->track_base[e->target]) return PSYTL_ERR_BOUND;
        if (c->n_events > 0 && c->base != e->base) return PSYTL_ERR_BOUND;
    }
    return 0;
}

/* Insert a checked event whose channel count is already taken. */
static void psytl__insert(psytl_timeline* tl, const psytl_event* src) {
    psytl__base* b = &tl->bases[src->base];
    int end = b->begin + b->count;
    int pos = psytl__upper(tl->events, b->begin, end, src->time);
    psytl_event* e;
    int i;
    if (pos < tl->n_events)
        memmove(&tl->events[pos + 1], &tl->events[pos],
                (size_t)(tl->n_events - pos) * sizeof(psytl_event));
    e = &tl->events[pos];
    *e = *src;
    e->id = tl->next_id++;
    e->flags = 0;
    e->reserved_ = 0;
    e->frame = -1;
    e->onset = 0;
    e->residual = 0;
    tl->n_events++;
    b->count++;
    for (i = src->base + 1; i < PSYTL_MAX_BASES; i++) tl->bases[i].begin++;
    if (b->evaluated && src->time <= b->last_hi) {
        int rel = pos - b->begin;
        b->hi++;
        if (rel < b->dirty) b->dirty = rel;
    }
}

PSYTL_API int psytl_add_n(psytl_timeline* tl, const psytl_event* e, int n) {
    int i, rc = 0, first;
    if (!tl || !tl->open) return PSYTL_ERR_CLOSED;
    if (n < 0 || (n > 0 && !e)) return PSYTL_ERR_ARG;
    if (n > tl->capacity - tl->n_events) return PSYTL_ERR_FULL;
    if (n > INT32_MAX - tl->next_id) return PSYTL_ERR_FULL;
    /* Take the channel counts as we check, so two events of the batch that
     * bind one channel to two bases are caught, and undo them on a refusal. */
    for (i = 0; i < n; i++) {
        rc = psytl__check(tl, &e[i]);
        if (rc < 0) break;
        if (psytl__drives(e[i].kind)) {
            psytl__chan* c = &tl->chans[e[i].target];
            if (c->n_events++ == 0) c->base = e[i].base;
        }
    }
    if (rc < 0) {
        while (i-- > 0) {
            if (psytl__drives(e[i].kind)) tl->chans[e[i].target].n_events--;
        }
        return rc;
    }
    first = tl->next_id;
    for (i = 0; i < n; i++) psytl__insert(tl, &e[i]);
    return first;
}

PSYTL_API int psytl_add(psytl_timeline* tl, const psytl_event* e) {
    if (!e) return tl && tl->open ? PSYTL_ERR_ARG : PSYTL_ERR_CLOSED;
    return psytl_add_n(tl, e, 1);
}

/* Delete the event at storage index k. Recomputes its channel when the
 * value came from it, and frees the channel when it was its last event. */
static void psytl__delete(psytl_timeline* tl, int k, bool recompute) {
    psytl_event e = tl->events[k];
    psytl__base* b = &tl->bases[e.base];
    int rel = k - b->begin, i;
    if (k + 1 < tl->n_events)
        memmove(&tl->events[k], &tl->events[k + 1],
                (size_t)(tl->n_events - k - 1) * sizeof(psytl_event));
    tl->n_events--;
    b->count--;
    for (i = e.base + 1; i < PSYTL_MAX_BASES; i++) tl->bases[i].begin--;
    if (rel < b->hi) b->hi--;
    if (b->dirty != PSYTL__NONE && rel < b->dirty) b->dirty--;
    if (b->dirty >= b->hi) b->dirty = PSYTL__NONE;
    if (psytl__drives(e.kind)) {
        psytl__chan* c = &tl->chans[e.target];
        bool was_shown = (e.flags & PSYTL__DONE) && c->has_last
                         && c->last_id == e.id;
        c->n_events--;
        if (c->n_events == 0) psytl__free_chan(tl, e.target, recompute && was_shown);
        else if (recompute && was_shown) psytl__recompute(tl, e.base, e.target);
    }
}

PSYTL_API int psytl_remove(psytl_timeline* tl, int id) {
    int k;
    if (!tl || !tl->open) return PSYTL_ERR_CLOSED;
    for (k = 0; k < tl->n_events; k++) {
        if (tl->events[k].id == id) {
            psytl__delete(tl, k, true);
            return 0;
        }
    }
    return PSYTL_ERR_NOT_FOUND;
}

static bool psytl__base_arg(int base) {
    return base == PSYTL_ALL_BASES || (base >= 0 && base < PSYTL_MAX_BASES);
}

/* Delete the events of `base` (or all), only the fired ones when
 * `fired_only`, in one pass over the storage: one deletion at a time would
 * move the tail once per event, quadratic for a movie's annotations.
 * Channels left with no event are freed, back to their initial value when
 * `reset`. Returns the number deleted. */
static int psytl__compact(psytl_timeline* tl, int base, bool fired_only, bool reset) {
    int bi, j = 0, removed = 0, i;
    for (bi = 0; bi < PSYTL_MAX_BASES; bi++) {
        psytl__base* b = &tl->bases[bi];
        int new_hi = 0, new_dirty = PSYTL__NONE, kept = 0, rel;
        bool hit = base == PSYTL_ALL_BASES || base == bi;
        for (rel = 0; rel < b->count; rel++) {
            const psytl_event* e = &tl->events[b->begin + rel];
            if (hit && (!fired_only || (e->flags & PSYTL__DONE))) {
                if (psytl__drives(e->kind)) {
                    tl->chans[e->target].n_events--;
                    tl->chans[e->target].mark = 1;
                }
                removed++;
                continue;
            }
            if (rel < b->hi) new_hi++;
            if (rel >= b->dirty && new_dirty == PSYTL__NONE) new_dirty = kept;
            if (j != b->begin + rel) tl->events[j] = *e;
            j++;
            kept++;
        }
        b->begin = j - kept;
        b->count = kept;
        b->hi = new_hi;
        b->dirty = new_dirty < new_hi ? new_dirty : PSYTL__NONE;
    }
    tl->n_events = j;
    for (i = 0; i < tl->n_channels; i++) {
        psytl__chan* c = &tl->chans[i];
        if (c->mark && c->n_events == 0) psytl__free_chan(tl, i, reset);
        c->mark = 0;
    }
    return removed;
}

PSYTL_API int psytl_prune(psytl_timeline* tl, int base) {
    if (!tl || !tl->open) return PSYTL_ERR_CLOSED;
    if (!psytl__base_arg(base)) return PSYTL_ERR_ARG;
    return psytl__compact(tl, base, true, false);
}

PSYTL_API int psytl_clear(psytl_timeline* tl, int base) {
    int removed, i;
    if (!tl || !tl->open) return PSYTL_ERR_CLOSED;
    if (!psytl__base_arg(base)) return PSYTL_ERR_ARG;
    removed = psytl__compact(tl, base, false, true);
    for (i = 0; i < tl->n_channels; i++) {
        psytl__chan* c = &tl->chans[i];
        if (tl->track_base[i] && (base == PSYTL_ALL_BASES || c->base == base))
            psytl__free_chan(tl, i, true);
    }
    return removed;
}

/* --- tracks ------------------------------------------------------------ */

static bool psytl__time_ok(int64_t t) {
    return t < PSYTL__LIMIT && t > -PSYTL__LIMIT;
}

static bool psytl__curve_ok(const psytl_curve* c) {
    return psytl__finite(c->y1) && psytl__finite(c->y2)
           && c->x1 >= 0.0f && c->x1 <= 1.0f && c->x2 >= 0.0f && c->x2 <= 1.0f;
}

/* Everything psytl__track_at() relies on: it does no check of its own. */
static int psytl__check_track(const psytl_track* tr) {
    bool has_keys, has_samples;
    int i;
    if (tr->n_keys < 0 || tr->n_samples < 0 || tr->n_curves < 0) return PSYTL_ERR_ARG;
    if (tr->n_curves > 0 && !tr->curves) return PSYTL_ERR_ARG;
    has_keys = tr->keys && tr->n_keys > 0;
    has_samples = tr->samples && tr->n_samples > 0;
    if (has_keys == has_samples) return PSYTL_ERR_ARG;
    if (tr->period < 0 || tr->period >= PSYTL__LIMIT || tr->repeats < 0) return PSYTL_ERR_ARG;
    if (tr->repeats > 0 && tr->period == 0) return PSYTL_ERR_ARG;
    for (i = 0; i < tr->n_curves; i++)
        if (!psytl__curve_ok(&tr->curves[i])) return PSYTL_ERR_ARG;
    if (has_keys) {
        int n = tr->n_keys;
        for (i = 0; i < n; i++) {
            const psytl_key* k = &tr->keys[i];
            if (!psytl__finite(k->value)) return PSYTL_ERR_ARG;
            if (k->ease > PSYTL_EASE_BEZIER) return PSYTL_ERR_ARG;
            if (!psytl__time_ok(k->time)) return PSYTL_ERR_ARG;
            if (i > 0 && k->time < tr->keys[i - 1].time) return PSYTL_ERR_ARG;
            if (i + 1 < n && k->ease == PSYTL_EASE_LOG
                && !(k->value > 0.0f && tr->keys[i + 1].value > 0.0f))
                return PSYTL_ERR_ARG;
            if (i + 1 < n && k->ease == PSYTL_EASE_BEZIER && k->curve >= tr->n_curves)
                return PSYTL_ERR_ARG;
        }
    } else {
        if (tr->rate < 1 || tr->rate > PSYTL_NS_PER_S || !psytl__time_ok(tr->t0))
            return PSYTL_ERR_ARG;
        if (tr->interp < PSYTL_INTERP_LINEAR || tr->interp > PSYTL_INTERP_CUBIC) return PSYTL_ERR_ARG;
        {
            /* The last sample's time, rounded up, must be below 2^62. The
             * products stay below 2^63: (n-1)/rate < 2^31 s, and the
             * remainder times 1e9 is below 1e18. */
            int64_t m = (int64_t)(tr->n_samples - 1);
            int64_t rem = (m % tr->rate) * PSYTL_NS_PER_S;
            int64_t span = (m / tr->rate) * PSYTL_NS_PER_S
                           + rem / tr->rate + (rem % tr->rate != 0);
            if (span > PSYTL__LIMIT - 1 - tr->t0) return PSYTL_ERR_ARG;
        }
        for (i = 0; i < tr->n_samples; i++)
            if (!psytl__finite(tr->samples[i])) return PSYTL_ERR_ARG;
    }
    return 0;
}

static int psytl__chan_arg(const psytl_timeline* tl, int channel, int base) {
    if (!tl || !tl->open) return PSYTL_ERR_CLOSED;
    if (channel < 0 || channel >= tl->n_channels) return PSYTL_ERR_ARG;
    if (base < 0 || base >= PSYTL_MAX_BASES) return PSYTL_ERR_ARG;
    if (tl->chans[channel].n_events > 0) return PSYTL_ERR_BOUND;
    return 0;
}

/* Called after c->tr, c->own and the tween keys are set. A STEP track
 * changes only at its key times, so it is read where an event would land
 * (QUANTIZATION); see TRACKS for why. */
static void psytl__bind_track(psytl_timeline* tl, int channel, int base) {
    psytl__chan* c = &tl->chans[channel];
    const psytl_key* k = c->own ? c->ik : c->tr.keys;
    int i;
    if (k) {
        c->stepped = 1;
        for (i = 0; i + 1 < c->tr.n_keys; i++)
            if (k[i].ease != PSYTL_EASE_STEP) c->stepped = 0;
    } else {
        c->stepped = c->tr.interp == PSYTL_INTERP_STEP;
    }
    c->seg = -1;
    c->base = (uint8_t)base;
    c->has_last = 0;
    c->has_track = 1;
    c->has_pend = 0;
    tl->track_base[channel] = (uint8_t)(base + 1);
}

PSYTL_API int psytl_set_track(psytl_timeline* tl, int channel, int base,
                              const psytl_track* tr) {
    int rc = psytl__chan_arg(tl, channel, base);
    psytl__chan* c;
    if (rc < 0) return rc;
    c = &tl->chans[channel];
    if (!tr) {
        if (tl->track_base[channel]) psytl__free_chan(tl, channel, true);
        return 0;
    }
    rc = psytl__check_track(tr);
    if (rc < 0) return rc;
    c->tr = *tr;
    c->own = 0;
    psytl__bind_track(tl, channel, base);
    return 0;
}

PSYTL_API int psytl_set_keys(psytl_timeline* tl, int channel, int base,
                             const psytl_key* keys, int n) {
    psytl_track tr;
    if (n < 0 || (n > 0 && !keys)) return tl && tl->open ? PSYTL_ERR_ARG : PSYTL_ERR_CLOSED;
    if (n == 0) return psytl_set_track(tl, channel, base, NULL);
    memset(&tr, 0, sizeof(tr));
    tr.keys = keys;
    tr.n_keys = n;
    return psytl_set_track(tl, channel, base, &tr);
}

/* Resolve `d` against the base's now and check it. On success *out is the
 * desc with start absolute and delay 0. */
static int psytl__tween_check(const psytl_timeline* tl, int base,
                              const psytl_tween_desc* d, psytl_tween_desc* out) {
    const psytl__base* b = &tl->bases[base];
    int64_t start;
    bool once = d->cycles == 0 || d->cycles == 1;
    if (d->start_set) {
        if (!psytl__time_ok(d->start)) return PSYTL_ERR_ARG;
        start = d->start;
    } else {
        start = b->now_bt;
    }
    if (!psytl__time_ok(d->delay)) return PSYTL_ERR_ARG;
    start += d->delay;
    if (!psytl__time_ok(start) || d->duration < 0 || d->duration >= PSYTL__LIMIT)
        return PSYTL_ERR_ARG;
    if (!psytl__time_ok(start + (d->yoyo ? 2 : 1) * d->duration)) return PSYTL_ERR_ARG;
    if (!psytl__finite(d->to) || (d->from_set && !psytl__finite(d->from))) return PSYTL_ERR_ARG;
    if (d->ease < 0 || d->ease > PSYTL_EASE_BEZIER) return PSYTL_ERR_ARG;
    if (d->ease == PSYTL_EASE_BEZIER && !psytl__curve_ok(&d->curve)) return PSYTL_ERR_ARG;
    if (d->ease == PSYTL_EASE_LOG
        && !(d->from_set && d->from > 0.0f && d->to > 0.0f))
        return PSYTL_ERR_ARG;
    if (d->cycles < PSYTL_FOREVER) return PSYTL_ERR_ARG;
    if (!once && d->duration == 0) return PSYTL_ERR_ARG;
    if (d->yoyo && d->duration == 0) return PSYTL_ERR_ARG;
    if (d->keep_velocity
        && (d->ease != PSYTL_EASE_LINEAR || d->from_set || d->yoyo || !once
            || d->duration == 0))
        return PSYTL_ERR_ARG;
    *out = *d;
    out->start = start;
    out->start_set = true;
    out->delay = 0;
    return 0;
}

PSYTL_API int psytl_tween(psytl_timeline* tl, int channel, int base,
                          const psytl_tween_desc* d) {
    int rc = psytl__chan_arg(tl, channel, base);
    psytl_tween_desc r;
    psytl__chan* c;
    if (rc < 0) return rc;
    if (!d) return PSYTL_ERR_ARG;
    rc = psytl__tween_check(tl, base, d, &r);
    if (rc < 0) return rc;
    c = &tl->chans[channel];
    /* A track on another base cannot run on until a start on this one: the
     * two times do not compare. It stops here, at its last value. */
    if (c->has_track && c->base != base) {
        c->has_track = 0;
        c->own = 0;
        memset(&c->tr, 0, sizeof(c->tr));
    }
    c->pend = r;
    c->has_pend = 1;
    c->pend_stepped = r.ease == PSYTL_EASE_STEP;
    c->base = (uint8_t)base;
    c->has_last = 0;
    tl->track_base[channel] = (uint8_t)(base + 1);
    return 0;
}

/* The pending tween takes over at its start. Its start value and slope
 * are the old track's at that base time, not at the frame that happens to
 * promote it, so the takeover does not depend on the frame timing: a
 * tween posted just before a frame starts from where the old track would
 * have been on that frame, with no repeated position. The slope is the
 * difference over the last microsecond before the start, which keeps a
 * jump at the start out of it. */
static void psytl__promote(psytl_timeline* tl, int ch) {
    psytl__chan* c = &tl->chans[ch];
    const psytl_tween_desc* p = &c->pend;
    const psytl_key* keys = c->own ? c->ik : c->tr.keys;
    const psytl_curve* curves = c->own ? &c->ic : c->tr.curves;
    int64_t s = p->start, dur = p->duration;
    double from = tl->values[ch], slope = 0.0;
    bool once = p->cycles == 0 || p->cycles == 1;
    if (c->has_track) {
        int32_t seg = -1;
        from = psytl__track_at(&c->tr, keys, curves, s, &seg);
        if (p->keep_velocity) {
            seg = -1;
            slope = (from - psytl__track_at(&c->tr, keys, curves, s - 1000, &seg)) / 1000.0;
        }
    }
    if (p->from_set) from = p->from;
    memset(&c->tr, 0, sizeof(c->tr));
    memset(c->ik, 0, sizeof(c->ik));
    c->ik[0].time = s;
    c->ik[0].value = (float)from;
    c->ik[0].ease = (uint8_t)p->ease;
    c->ik[1].time = s + dur;
    c->ik[1].value = p->to;
    c->tr.n_keys = 2;
    if (p->yoyo) {
        c->ik[1].ease = (uint8_t)p->ease;
        c->ik[2].time = s + 2 * dur;
        c->ik[2].value = (float)from;
        c->tr.n_keys = 3;
    }
    if (p->ease == PSYTL_EASE_BEZIER) {
        c->ic = p->curve;
        c->tr.n_curves = 1;
    }
    if (p->keep_velocity) {
        /* The Hermite start tangent, in value units over the whole tween. */
        c->ik[0].ease = PSYTL__EASE_HERMITE;
        memset(&c->ic, 0, sizeof(c->ic));
        c->ic.x1 = (float)(slope * (double)dur);
        c->tr.n_curves = 1;
    }
    if (!once) {
        c->tr.period = p->yoyo ? 2 * dur : dur;
        c->tr.repeats = p->cycles == PSYTL_FOREVER ? 0 : p->cycles;
    }
    c->own = 1;
    c->has_track = 1;
    c->has_pend = 0;
    c->stepped = c->pend_stepped;
    c->seg = -1;
}

/* --- the frame --------------------------------------------------------- */

/* RT target of a fired event, the report's sort key. */
static int64_t psytl__target(const psytl_event* e) { return e->onset - e->residual; }

static bool psytl__before(const psytl_event* a, const psytl_event* b) {
    int64_t ta = psytl__target(a), tb = psytl__target(b);
    return ta < tb || (ta == tb && a->id < b->id);
}

/* Insert into the report, which holds the `cap` earliest so far. */
static void psytl__report(psytl_event* out, int cap, int n_out, const psytl_event* e) {
    int j = n_out < cap ? n_out : cap - 1;
    if (cap <= 0) return;
    if (n_out >= cap && !psytl__before(e, &out[cap - 1])) return;
    while (j > 0 && psytl__before(e, &out[j - 1])) {
        out[j] = out[j - 1];
        j--;
    }
    out[j] = *e;
}

PSYTL_API int psytl_evaluate(psytl_timeline* tl, const psytl_frame* f,
                             psytl_event* fired, int cap) {
    int64_t lead_ns;
    int bi, i, n_fired = 0;
    if (!tl || !tl->open) return PSYTL_ERR_CLOSED;
    if (!f || f->period < 0 || cap < 0 || (cap > 0 && !fired)) return PSYTL_ERR_ARG;
    if (tl->has_onset && f->onset < tl->last_onset) return PSYTL_ERR_ORDER;
    tl->has_onset = true;
    tl->last_onset = f->onset;
    lead_ns = (int64_t)(tl->lead * (double)f->period);

    for (bi = 0; bi < PSYTL_MAX_BASES; bi++) {
        psytl__base* b = &tl->bases[bi];
        int64_t now, hi_t;
        int begin, i_hi, start, hi_kept;
        if (b->state == PSYTL__STOPPED) continue;
        now = psytl__bt(b, f->onset);
        hi_t = psytl__bt(b, f->onset + lead_ns);
        begin = b->begin;
        i_hi = psytl__upper(tl->events, begin, begin + b->count, hi_t) - begin;

        /* The window can end behind the base's reach (a pause with a
         * lead, an anchor whose RT time is past this onset). Nothing
         * un-fires then: a frame already showed those events, and only an
         * anchor rewinds. Late events up to the window fire. */
        hi_kept = b->hi < i_hi ? b->hi : i_hi;
        start = b->dirty < hi_kept ? b->dirty : hi_kept;
        for (i = start; i < i_hi; i++) {
            psytl_event* e = &tl->events[begin + i];
            if (e->flags & PSYTL__DONE) continue;
            e->flags = (uint16_t)(PSYTL_EV_FIRED | (i < hi_kept ? PSYTL_EV_LATE : 0u));
            e->frame = f->index;
            e->onset = f->onset;
            e->residual = now - e->time;
            if (psytl__drives(e->kind)) psytl__apply(tl, e);
            psytl__report(fired, cap, n_fired, e);
            n_fired++;
        }
        if (i_hi < b->hi) {
            int d = b->dirty > i_hi ? b->dirty : i_hi;
            while (d < b->hi && (tl->events[begin + d].flags & PSYTL__DONE)) d++;
            b->dirty = d < b->hi ? d : PSYTL__NONE;
        } else {
            /* i_hi == hi with hi_t behind the reach is possible when no
             * event lies between: the reach must not move back, or an
             * event added there later would not count as late. */
            b->hi = i_hi;
            b->dirty = PSYTL__NONE;
            if (!b->evaluated || hi_t > b->last_hi) b->last_hi = hi_t;
        }
        if (!b->evaluated || now > b->last_now) b->last_now = now;
        b->now_bt = now;
        b->evaluated = 1;

        for (i = 0; i < tl->n_channels; i++) {
            psytl__chan* c = &tl->chans[i];
            if (tl->track_base[i] != bi + 1) continue;
            if (c->has_pend && (c->pend_stepped ? hi_t : now) >= c->pend.start)
                psytl__promote(tl, i);
            if (c->has_track)
                tl->values[i] = psytl__sample_chan(c, c->stepped ? hi_t : now);
        }
    }
    return n_fired;
}

PSYTL_API bool psytl_next_due(const psytl_timeline* tl, int64_t* rt) {
    int bi, i;
    bool found = false;
    int64_t best = 0;
    if (!tl || !tl->open || !rt) return false;
    for (bi = 0; bi < PSYTL_MAX_BASES; bi++) {
        const psytl__base* b = &tl->bases[bi];
        int start;
        if (b->state != PSYTL__RUNNING) continue;
        start = b->dirty < b->hi ? b->dirty : b->hi;
        for (i = start; i < b->count; i++) {
            const psytl_event* e = &tl->events[b->begin + i];
            int64_t t;
            if (e->flags & PSYTL__DONE) continue;
            t = b->anchor_rt + (e->time - b->anchor_bt);
            if (!found || t < best) best = t;
            found = true;
            /* Past hi the events are sorted and unfired: the first is it. */
            if (i >= b->hi) break;
        }
    }
    if (found) *rt = best;
    return found;
}

PSYTL_API int psytl_peek(const psytl_timeline* tl, int base, int64_t from_rt, int64_t to_rt,
                         psytl_event* out, int cap) {
    int bi, i, n = 0;
    if (!tl || !tl->open) return PSYTL_ERR_CLOSED;
    if (!psytl__base_arg(base) || cap < 0 || (cap > 0 && !out) || from_rt > to_rt
        || !psytl__time_ok(from_rt) || !psytl__time_ok(to_rt))
        return PSYTL_ERR_ARG;
    for (bi = 0; bi < PSYTL_MAX_BASES; bi++) {
        const psytl__base* b = &tl->bases[bi];
        int lo, hi;
        if (base != PSYTL_ALL_BASES && base != bi) continue;
        if (b->state != PSYTL__RUNNING) continue;
        /* An event's RT time is the first RT ns at which the base reaches
         * it, so it is in [from_rt, to_rt) exactly when the base time at
         * from_rt - 1 is before it and the one at to_rt - 1 is not. Put
         * this way the bounds stay right for any mapping that rounds down,
         * not only for rate 1. */
        lo = psytl__upper(tl->events, b->begin, b->begin + b->count, psytl__bt(b, from_rt - 1));
        hi = psytl__upper(tl->events, lo, b->begin + b->count, psytl__bt(b, to_rt - 1));
        for (i = lo; i < hi; i++) {
            const psytl_event* e = &tl->events[i];
            psytl_event c;
            if (e->flags & PSYTL__DONE) continue;
            c = *e;
            c.onset = b->anchor_rt + (e->time - b->anchor_bt);
            c.residual = 0;
            psytl__report(out, cap, n, &c);
            n++;
        }
    }
    return n;
}

PSYTL_API double psytl_lead(const psytl_timeline* tl) {
    return tl && tl->open ? tl->lead : 0.0;
}

/* --- state and record -------------------------------------------------- */

PSYTL_API const float* psytl_values(const psytl_timeline* tl) {
    return tl && tl->open ? tl->values : NULL;
}

PSYTL_API float psytl_value(const psytl_timeline* tl, int channel) {
    if (!tl || !tl->open || channel < 0 || channel >= tl->n_channels) return 0.0f;
    return tl->values[channel];
}

PSYTL_API const psytl_event* psytl_events(const psytl_timeline* tl, int base, int* n) {
    if (n) *n = 0;
    if (!tl || !tl->open || !psytl__base_arg(base)) return NULL;
    if (base == PSYTL_ALL_BASES) {
        if (n) *n = tl->n_events;
        return tl->events;
    }
    if (n) *n = tl->bases[base].count;
    return tl->events ? tl->events + tl->bases[base].begin : NULL;
}

PSYTL_API const psytl_event* psytl_find(const psytl_timeline* tl, int id) {
    int k;
    if (!tl || !tl->open) return NULL;
    for (k = 0; k < tl->n_events; k++)
        if (tl->events[k].id == id) return &tl->events[k];
    return NULL;
}

#endif /* PSY_TIMELINE_IMPLEMENTATION_GUARD */
#endif /* PSY_TIMELINE_IMPLEMENTATION */

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
