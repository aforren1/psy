# psy_timeline.h design notes

This note explains why `psy_timeline.h` has its current form. The manual
in the header tells you how to use it. The plan that asked for the header
is section 4.5 of [rig_spec.md](rig_spec.md).

## Time is an integer

All times are `int64_t` nanoseconds. `psyrt_now_ns()` returns this unit,
so a time from `psy_rt.h` goes into the timeline with a cast.

The alternative was `double` seconds. We did not use it, for two reasons:

- Quantization compares an event time with a frame window. With integers,
  the comparison is exact. With `double`, an event at 0.5 s and a frame
  onset at 30/60 s can differ in the last bit, so the event can land one
  frame late.
- Replay must give the same bytes from the same inputs. Integer addition
  and comparison give the same result on every compiler. Only the track
  easings use `double`, and only `COSINE` and `LOG` call libm.

The range limit is 2^62 ns, about 146 years. With this limit, the sum of
two times cannot overflow.

## The state is a set of float channels

The spec says that evaluate "fills a state struct". The header does not
know the caller's stimuli, so the state is an array of floats. The caller
decides what each channel means. Onset and offset set a channel to 1 and
0, so visibility is a channel too. The array is contiguous, so the caller
can upload it as a uniform block.

## One driver and one base per channel

A channel takes events or keys, not both. All its drivers are on one
base. These rules keep one property true: a channel's value is a function
of which events have fired. That property is necessary for a rewind. When
a base moves back, the header resets the channels of that base and applies
the events that are still fired. If two bases could write one channel,
the order of their writes would be the order of the frames, and a rewind
of one base could not recompute the channel.

For the same reason, a late event does not overwrite a later event that
fired first. The value comes from the fired event with the latest
(time, id), not from the most recent write.

## The default lead is half a frame

An event lands on the frame nearest to its time. A desc lead of 0 means
this default, 0.5. `PSYTL_LEAD_NONE` gives the first frame whose onset is
at or after the event, which never shows an event early.

v0.1.0 had "never early" as the default, because it was the first rule
for `wait(dt)` in the spec. A measurement changed it. A predicted onset
carries noise, and a frame-aligned time is then a coin toss between two
frames. With 2 us of noise on a 60 Hz display, "never early" put about
12% of frame-aligned changes a frame late, and the nearest frame put none
on the wrong frame (see "STEP tracks are quantized like events"). The
spec's `wait(dt)` rule now uses the nearest frame too.

## Only an anchor rewinds

The first implementation rewound at evaluate. When a frame window ended
before the previous window, the fired events after the new window became
pending again. Two problems showed:

- The example program found the first. When the caller anchored a trial
  base at 0 again, the event at trial time 0 did not fire again. That
  event was inside the new window, so the rewind did not touch it.
- The independent test found the second. With a lead, a pause can freeze
  a base before an event that a frame already showed early. The next
  evaluate un-fired that event, and after the resume it fired again. A
  trigger went out twice.

Now `psytl_anchor()` is the only call that rewinds. An anchor at base
time `bt`, at or before the furthest base time evaluated on the base,
makes pending every fired event at or after `bt`. The event at `bt` is
included. An evaluate never un-fires an event, because a frame already
showed it.

The cost of this rule: a small backward re-anchor to correct a drift
fires again the events in that small interval. The manual says to correct
forward only.

## Tracks: keys, samples, repeats and tweens

A keyed track suits a few changes: a ramp, a fade, a path through a dozen
points. A long trajectory, such as a 10-minute sum-of-sines target for a
tracking task, needs a different form. As keys it costs 16 bytes per
point, and 8 of those bytes repeat a fixed step. So a track has a second
form: samples at a fixed rate, 4 bytes each, with no time column.

### The sample rate is an integer, not a step in ns

The first version stored the step in whole nanoseconds. At 120 samples/s
the step is 8,333,333.33 ns, so each sample was a third of a nanosecond
early, and after 10 minutes the table was 24 us early. On a moving target
that offset is an error. The example program found it. Sample i is now at
exactly t0 + i * 1e9 / rate, and the lookup splits the time into seconds
and nanoseconds, so every product stays below 2^63 for any rate up to 1e9.

Measured on the 5 Hz sum of sines below, CUBIC at 120 samples/s, worst
error away from the ends, as a fraction of the peak:

| Sample placement | Error |
|---|---|
| Step of whole ns (first version) | 6.44e-5 |
| Integer rate (now) | 7.34e-6 |

A rate that is not an integer, such as 59.94, is a resampling job for
the pack tool. That follows the rule of one canonical form per resource.

### How fine a table must be

Two signals, each the sum of seven sines with amplitude 1/f, 10 minutes
long:
- Set A (0.1 to 2.05 Hz) is typical of visuomotor tracking.
- Set B (0.15 to 4.95 Hz) is a harder case.

The table rate varies. The test reads the table at 2 million random times
and compares it with the analytic signal. The error is the worst absolute
error as a fraction of the peak (the sum of the amplitudes), away from the
first and last segment.

| Table rate | Size, 10 min | Set A linear | Set A cubic | Set B linear | Set B cubic |
|---|---|---|---|---|---|
| 60/s | 0.14 MB | 4.4e-4 | 8.1e-6 | 1.3e-3 | 6.1e-5 |
| 120/s | 0.29 MB | 1.1e-4 | 1.0e-6 | 3.2e-4 | 7.4e-6 |
| 250/s | 0.60 MB | 2.5e-5 | 1.3e-7 | 7.4e-5 | 8.2e-7 |
| 500/s | 1.2 MB | 6.4e-6 | 5.9e-8 | 1.8e-5 | 1.3e-7 |
| 1000/s | 2.4 MB | 1.6e-6 | 5.5e-8 | 4.6e-6 | 5.0e-8 |

Float rounding of the samples alone is 4e-8 to 5e-8 of the peak. For a
target that moves 1000 pixels from end to end, 1e-5 is 0.01 pixel.
Recommendation: use CUBIC at 120 samples/s or more. LINEAR needs about 8
times the rate for the same error, and its velocity jumps at every sample,
which matters if the analysis uses velocity.

### The ends of a cubic table

Catmull-Rom needs one sample beyond each end. The first version repeated
the end sample. That makes the slope zero at the end, and the error in the
first and last segment was 5.55e-5 (set B, 120/s). A sample extrapolated
in a line (v_-1 = 2 v_0 - v_1) gives 1.73e-5. The interior stays at
7.3e-6. A table that starts before the experiment needs it removes the
remaining error at the ends.

### Why samples and not sines at run time

The runtime could compute the sum of sines itself. It does not, for two
reasons:
- `sin()` differs in the last bit between C libraries, so the trajectory
  would differ between platforms.
- Interpolating a table is +, -, * and /, so a table that the pack tool
  computes once gives the same bits everywhere.

The cost is memory: 288 KB for 10 minutes at 120 samples/s.

### Repeats

A flicker, a counterphase reversal or an SSVEP drive is periodic. A
repeat folds the time into one cycle, so a 10-minute 7.5 Hz square wave
takes two keys, not 9000. The period is in whole nanoseconds. At 7.5 Hz
the period is 133,333,333 ns, a third of a nanosecond short per cycle,
which adds up to 1.5 us after 10 minutes.

### STEP tracks are quantized like events

A frame-locked flicker has its edges exactly on frame onsets: a 7.5 Hz
square wave on a 60 Hz display changes every 4 frames. Frame onsets are
rounded to whole nanoseconds, and a predicted onset carries estimation
noise. So an edge read exactly at the onset falls on either side of it.
Each test below runs 10 minutes and counts the frames whose state
disagrees with the frame count (the state changes every 4 frames at
60 Hz).

Edges at exact rational times, onsets rounded to ns, no noise:

| Read at | Wrong frames, 60 to 600 Hz |
|---|---|
| The onset | 3000 at every rate (of 36,000 to 360,000) |
| Onset + half a period | 0 |

The header's truncated period read at the onset also gave 0. That result
is luck: every truncated edge is a fraction of a nanosecond early, so it
never falls after a rounded onset.

The header at 60 Hz, with uniform noise added to the predicted onsets.
Channel 0 is a STEP track with a repeat. Channel 1 is 9000 SET events at
the edges:

| lead | Onset noise | STEP track wrong | Events wrong |
|---|---|---|---|
| None (never early) | 0 | 0 | 0 |
| None (never early) | +-2 us | 2791 | 4399 |
| None (never early) | +-20 us | 4350 | 4518 |
| 0.5 (the default) | 0 | 0 | 0 |
| 0.5 (the default) | +-2 us | 0 | 0 |
| 0.5 (the default) | +-20 us | 0 | 0 |

So a STEP track is read at the end of the lead window, like an event. A
ramp is still read at the onset, because it must show its value at the
moment the frame appears. Never early, a frame-aligned change is a coin
toss under any onset noise, for events and STEP tracks alike.

### Tweens

A script needs a fade that starts where the value is now, and it cannot
keep an array alive for it. `psytl_tween()` stores its keys (three with
yoyo), its Bezier curve and a waiting tween's desc in the channel record.
With this storage, a channel record is 232 bytes and the handle is 60 KB
(it was 12 KB in v0.1.0). An evaluate reads a byte per channel to find
the tracked channels, so the size does not add to the frame cost. A rig
that needs fewer channels sets `PSYTL_MAX_CHANNELS`.

The tween takes a desc (`psytl_tween_desc`), not eight arguments. The
reasons: principle 8 of the spec (every parameter a designer can set is
a plain value in a desc), two `int64_t` times that could be swapped
silently, an ease and a curve that had to agree, and fields that the
script draft needs (`from`, `cycles`, `yoyo`, `keep_velocity`). With C99
designated initializers, a call is about as short as GSAP's `to()`.

### The takeover

The first `psytl_tween()` replaced the channel's track at the call and
started from the value of the last evaluated frame. A script posts its
tween just before the next frame is evaluated, so the tween started one
frame late from a value one frame old. A moving object repeated its
position for one frame.

Now the tween waits until its start while the old track runs on, as
GSAP lets an overwritten tween run until the new one starts. At the
start, it takes the old track's value at that base time. With
`keep_velocity`, it also takes the old track's slope, and a cubic
Hermite segment brings the value to rest at the end value.

A dot moves at 10 units/s on a 500 Hz display. A script retargets it in
mid-flight to a speed of 15 units/s:

| Tween | Repeated positions | Largest step in frame-to-frame velocity |
|---|---|---|
| First version | 1 | 15.0 /s (10, then 0, then 15) |
| Takeover | 0 | 5.0 /s (10, then 15: what a linear retarget asks for) |
| Takeover with `keep_velocity` | 0 | 0.10 /s |

A step in target velocity is a confound for smooth pursuit, which
responds to velocity steps. `keep_velocity` removes it.

### Bezier easing

`PSYTL_EASE_BEZIER` is CSS `cubic-bezier()`, the curve designers already
edit with handles. A key holds an index into the track's curve table, so
a key stays 16 bytes. The solver uses Newton steps from s = u and
bisection when a step leaves [0, 1]. It is plain arithmetic, so replay
stays exact.

### Measured cost

The `tracks` workload of `timeline_bench` runs four kinds of track for 10
minutes: a sampled CUBIC path at 120/s, the same length as 600,001 keys, a
repeating flicker, and four Bezier channels. With gcc under WSL2 it costs
310 ns mean and 521 ns p99 at 60 Hz, and 241 ns mean and 311 ns p99 at
500 Hz. The p99 is 0.016% of a 2 ms frame.

## No playback rate

A base advances at the rate of the RT clock, or it is paused. A movie at
another speed is a decoder problem. With a rate, a base-time conversion
needs a multiplication, and integer exactness is lost.

## Storage

Events go in a sorted array that the caller supplies, and the caller sets
its size. At movie scale, the array holds thousands of annotations, so a
compile-time maximum would make every handle large. Each base has a range
in the array, a fired prefix and a lowest late index. With these, an
evaluate is a binary search per base plus the work for the events that
fire.

Channels and bases have compile-time maximums (256 and 8). There are few
of them, and the handle stays at about 12 KB.

Keys are not copied. A pack table or a static array is used where it is.
An adapter would only copy the same data.

## Measured costs

`examples/timeline_bench.c` gives these values. The machine is the
development machine. The compilers are gcc 11.4 at -O2 under WSL2 and
MSVC 19.44 at /O2 on Windows 11. The time for one frame includes one
clock read. The display is 60 Hz. Each cell is the range over three
runs, because the values changed by up to 1.6 times from one run to the
next.

| Workload | gcc mean | gcc p99 | MSVC mean | MSVC p99 |
|---|---|---|---|---|
| Trial: 5 events, 1 keyed channel, a rewind each trial | 51-82 ns | 86-144 ns | 53-70 ns | 100 ns |
| Movie: 10,000 annotations, 32 keyed channels of 256 keys, 8 SET channels | 297-429 ns | 0.6-1.2 us | 321-527 ns | 0.7-1.6 us |
| Script: 1 add per frame on the RT base, prune every 60 frames | 60-91 ns | 84-134 ns | 57-92 ns | 100-200 ns |

On Windows the clock ticks every 100 ns, so the MSVC quantiles are
multiples of 100 ns.

The worst p99 is 1.6 us, about 0.01% of a 16.7 ms frame. The worst single
frame was 648 us (gcc, movie), about 4% of a frame. "Slow single frames"
below shows that such frames come from the machine, not the header.

### Refresh rate

Displays at 360 Hz and 500 Hz are on sale, so the header must not assume
60 Hz. Nothing in it does: the period is an argument, and times are whole
nanoseconds. The cost of one evaluate does not depend on the rate. The
budget does: 2 ms at 500 Hz, 1 ms at 1000 Hz. Run
`timeline_bench <refresh_hz>` to measure a rate. The movie workload
covers two hours at each rate, so 3.6 million frames at 500 Hz. One run
at each rate:

| Rate | Budget | Movie mean, gcc / MSVC | Movie p99, gcc / MSVC | p99 share of the budget |
|---|---|---|---|---|
| 60 Hz | 16.7 ms | 343 / 269 ns | 651 / 600 ns | 0.004% |
| 500 Hz | 2 ms | 308 / 258 ns | 606 / 500 ns | 0.03% |
| 1000 Hz | 1 ms | 277 / 260 ns | 575 / 600 ns | 0.06% |

The test checks quantization on grids of 60, 144, 240, 360, 500 and 1000
Hz.

### Slow single frames

At high rates a slow single frame matters more. The slowest movie frame
was 0.85 ms at 500 Hz (gcc) and 0.44 ms at 1000 Hz (MSVC). To find out
whether the header causes these frames, a scratch program ran two
identical timelines in lockstep on the same inputs and timed each one on
every frame. Work done by the header is the same in both timelines, so a
frame that the header makes slow is slow in both. A frame made slow by
the machine or the OS is slow in one only.

| 500 Hz, 3.6 million frames | Slow in A | Slow in B | Slow in both |
|---|---|---|---|
| gcc, WSL2, above 20 us | 319 | 337 | 1 |
| gcc, WSL2, above 5 us | 1006 | 1023 | 4 |
| MSVC, Windows 11, above 20 us | 200 | 191 | 2 |
| MSVC, Windows 11, above 5 us | 445 | 434 | 8 |

The slow frames are not the header's. They are more frequent in the movie
workload than in the trial workload. The movie uses about 800 KB of
memory, against a few hundred bytes for the trial. A probable cause is
that an interruption evicts that memory from the cache. This note did not
measure that cause. The benchmark's "floor" line gives the machine's own
slow frames with no header code between the clock reads.

### Loads

| Load of 10,000 events | gcc | MSVC |
|---|---|---|
| Sorted, one `psytl_add_n` | 0.17-0.23 ms | 0.29-0.41 ms |
| Sorted, one `psytl_add` each | 0.14-0.22 ms | 0.32-0.46 ms |
| Shuffled, one `psytl_add` each | 23-37 ms | 23-34 ms |

A shuffled load is quadratic, because each add moves the tail of the
array. Sort the table before you load it. The pack tool sorts it.

## Verification

`tests/adapt/psy_timeline_test.c` was written from the manual only, by a
writer who did not read the implementation. It has its own reference
model. It checks quantization on a 60 Hz grid at three leads, against a
ceiling-division model, with event times on the window edges. It also
checks every easing against its own formulas, binding, late events,
rewinds, remove, prune and clear, and replay. Replay runs 600 calls into
two handles and compares the storages with `memcmp`. A random model check
runs 10 seeds of 800 steps and compares the whole storage and every
channel value after each step.

The writer broke the model on purpose in several ways to show that the
check finds errors. These mutations were caught: the lead window off by
one, `>` for `>=` in the rewind, `<` for `<=` in the rewind threshold, the
LATE flag dropped, and an evaluate-time un-fire put back.

The test found four header bugs before the first release:
- The duplicate fire after a pause with a lead (see "Only an anchor
  rewinds").
- A window behind the reach moved the reach back, so a later add was not
  marked late.
- On the first evaluate of a base, a negative window did not become the
  reach.
- A remove of an event that never fired reset its channel to the initial
  value.

It also found one disagreement between two sentences of the manual
(`psytl_set_keys` on another base). The header's behavior was kept and
the manual was changed.

For v0.2.0 a second writer extended the test the same way, from the
manual only. It covers sampled tracks, repeats, Bezier, tweens and the
STEP read rule, and it adds track operations to the random model check.
Six new model mutations are each caught: repeated instead of phantom end
samples, a whole-ns sample step, the hold one cycle late, the hold folded,
the Bezier parameter used as x, and STEP tracks read at the onset. The
test found one disagreement: the limit on the last sample's time was a
whole-second bound in the code but "within a second of 2^62" in the
manual. The check is now exact (a last sample at or past 2^62, rounded
up to a whole ns), and the manual says so. Two other bugs of v0.2.0 were
found before the test ran, both by the example program: the
whole-ns sample step and the repeated end samples.

A third writer ported the test to `psytl_tween_desc` and covered the
takeover. Three more model mutations are each caught: `from` read at the
call, the slope ignored with `keep_velocity`, and the yoyo return leg
missing. The writer found that the header's "now" for a tween did not
match the manual. The header used the furthest evaluated base time and,
after a rewinding anchor, the time just before the anchor. Both came from
bookkeeping that serves the rewind, not the tween. Now each base keeps
its own "now": the base time of the last evaluated onset, or the time a
later anchor, pause or resume set. The random model uses that rule with
no cases excluded, and fails 10 checks against the first version's rule.

## Not done

- No snapshot format. The storage is the record. A resume after a crash
  adds the events again and anchors again.
- One timeline is for one display. A second display that flips on its own
  grid needs its own timeline or its own evaluate.
- No run on macOS or on a big-endian machine.
