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

## Skip: a seek that does not fire what it passes over

Before v0.3.0, a seek was an anchor. A forward anchor fires every event
that the base passed over on the next frame, late by its residual. The
psy_video.h manual-mode test fired 148 annotations on one display frame
after a seek. On a movie base with one annotation per second, a
30-minute seek fires 1800. Each one is a false record: a trigger sent, a
coroutine resumed, a MARK logged, all at the wrong time.

`psytl_skip(tl, base, rt, bt)` is an anchor, then a mark on every pending
event of the base before `bt`. The mark is `PSYTL_EV_SKIPPED`. A skipped
event never fires. The return value is the count.

Decisions:
- **The call takes `rt`.** The request was `psytl_skip(tl, base, bt)`.
  A running base needs an RT time for its new position. Without `rt`, a
  seek is two calls, a skip and an anchor, that must agree on `bt`.
- **A skip back is an anchor back.** It rewinds as an anchor does, and
  then it skips the late events before `bt` that still wait for a window.
  The other choice was to refuse a skip back. The caller cannot read the
  furthest evaluated base time, so it cannot know whether a seek goes
  back. With this rule a seek calls `psytl_skip()` in both directions.
- **A skipped event counts for its channel.** After a seek, the channels
  show what continuous play shows at `bt`. The other choice keeps the
  values from before the seek: a seek past an onset then shows a
  stimulus that should be visible as hidden.
- **Only events before `bt`.** The event at `bt` fires on its frame, as
  the event at an anchor's `bt` fires after a rewind.
- **The reach and the furthest onset move to just before `bt`.** If the
  reach stays, an event added after the skip at a time before `bt` is not
  late. If the furthest onset stays, a later anchor back does not make
  the skipped events pending again. The model mutations `MUT_SKIP_REACH`
  and `MUT_SKIP_NOW` are these two errors, and the test finds both.
- **A new flag, not FIRED.** A caller that reads FIRED as "landed on a
  frame" stays correct. A skipped event has frame -1, onset = the skip's
  `rt` and residual = `bt` minus its time, so the record shows the seek.
- **Prune deletes skipped events** with the fired ones. Both are behind
  the base.

Tracks need nothing: a track is a function of base time. A tween that
waits for a start before `bt` takes over at the next evaluate, from the
old track's value at its start. The test compares a skip with an anchor
to the same place: the values are the same bits.

A skip costs a binary search and one write per skipped event. This note
did not time it.

## Peek: the look-ahead for sound

An evaluate fires an event on the frame that shows it, at most
lead x period early. psy_audio.h needs an onset 43.5 to 54.5 ms before
its time ([psy_audio.md](psy_audio.md)), and it places a sound at the
event's own time, not at a frame. `psytl_peek(tl, base, from_rt, to_rt,
out, cap)` lists the pending events whose RT time is in `[from_rt,
to_rt)` and changes nothing.

Decisions:
- **A half-open window with a start.** The draft in rig_spec.md 4.5.1
  had only an end (`until_rt`). With a start, the audio path peeks
  `[last to_rt, now + lead)` on each frame. The windows follow each other
  with no overlap and no gap, so "already scheduled" is a range.
- **The RT time goes in `onset`, with residual 0.** In a report,
  onset - residual is the event's RT time. In a peek copy it is the same.
  The event is 64 bytes and has no free field for a second time.
- **Pending events only.** A fired event has its frame's record. An
  event added less than the audio lead before its time can be in no peek
  before a frame fires it. It is in that evaluate's report, and the
  caller plays it late from there.
- **Running bases only.** A paused base has no RT time for its events.
  The mapping gives an empty window for a paused base on its own, so the
  state check is there for a stopped base, whose anchor is stale.
- **The bounds are written as `bt(from_rt - 1) < time <= bt(to_rt - 1)`.**
  At rate 1 this is the same as converting the bounds directly. It also
  stays correct for any mapping that rounds down, which is what a rate
  needs (see the proposal below).

The test runs the audio path's use for 22 s at 60 Hz with a look-ahead of
55 ms plus a frame. Every one of 200 events was in exactly one peek, at
least 55 ms before its RT time and before the frame that fired it, and
fired once.

`psytl_lead()` returns 0 for `PSYTL_LEAD_NONE`, not -1. The value is the
one that multiplies the period, and psy_video.h's own lead field uses the
same convention, so psy_video.h can replace its read of `tl->lead` with
`psytl_lead(tl)` and change nothing else.

## No playback rate (yet)

A base advances at the rate of the RT clock, or it is paused. The next
section proposes an exact rational rate. It is not implemented.

## Proposal: an exact rational base rate

Status: proposal, 2026-10-05. Not implemented. The recommendation is at
the end: later than v0.3.0.

The idea: slow motion and time scaling live in the timeline. psy_video.h
reads movie time from the movie base, so video gets rates from the base.
A soundtrack cannot follow a rate other than 1, because psy_audio.h does
not resample, so psy_video.h refuses that combination
(`psyvid__base_rate()` already checks for it).

### Calls

```c
/* From RT time rt on, `base` advances num/den base ns per RT ns. */
int  psytl_rate(psytl_timeline* tl, int base, int64_t rt, int32_t num, int32_t den);
bool psytl_get_rate(const psytl_timeline* tl, int base, int32_t* num, int32_t* den);
```

The request was `psytl_rate(tl, base, num, den)`. The call needs `rt`,
the change point. Without it, the new rate applies from the last anchor,
and the base time jumps at the call.

Rules:
- `num` and `den` are in [1, 2^31 - 1]. The call reduces them by their
  greatest common divisor, so equal rates store equal bytes.
- Rate 0 is refused (`PSYTL_ERR_ARG`). `psytl_pause()` is the one way to
  freeze a base. A running base at rate 0 has no inverse mapping, so
  `psytl_next_due()` and `psytl_peek()` divide by zero, and two frozen
  states double the cases that every rule and the model must cover.
- A negative rate is refused. See "Reverse" below.
- Base 0 is refused, as for anchor.
- `psytl_open()` sets every base to 1/1. Anchor, skip, pause, resume and
  clear keep the rate. A paused or stopped base stores the rate and uses
  it from the next resume or anchor.

### Arithmetic

Base time at RT time t, with the anchor (rt0, bt0):

    bt(t) = bt0 + floor((t - rt0) * num / den)

The RT time of base time T, the first RT ns at which the base reaches T:

    rt(T) = rt0 + ceil((T - bt0) * den / num)

These two are exact inverses for every rule that asks "has the base
reached T": bt(t) >= T exactly when t >= rt(T). At 1/1 both are today's
formulas.

The product must not overflow. Split d = t - rt0 as d = q * den + r with
0 <= r < den (floor division: C's `/` truncates, so subtract 1 from q
when r < 0). Then

    bt(t) = bt0 + q * num + floor(r * num / den)

r * num < 2^31 * 2^31 = 2^62, so the second product cannot overflow.
q * num is at most d * num / den + num in magnitude: the base time span
itself, which the contract keeps below 2^62. The proposed call saturates it and
does not wrap when a caller goes past the contract. The inverse splits
by num the same way. This is the split that the sampled-track lookup
already uses: there, d is split into seconds and ns, which is den = 1e9
and num = the sample rate.

No drift: each evaluate computes from the anchor, not from the last
frame, so a rate held for 10 hours has the same error as one held for
1 s: the floor, under 1 ns, never added up. A rate change re-anchors at
(rt, bt(rt)), which takes the floor once. So n rate changes lose at most
n ns against the exact rational time. A change is a user action, and
1000 changes lose at most 1 us. Keeping the remainder as a carry would
make even that exact, at the cost of a third anchor field. Replay is
exact in both forms: the arithmetic is integers only.

### Rate change mid-run

`psytl_rate(tl, b, rt, num, den)` on a running base computes bt_c =
bt(rt) with the old rate and anchors at (rt, bt_c) with the new one. The
mapping is continuous at rt, exactly, in integers.

It must not call `psytl_anchor()`. An anchor at a base time at or before
the furthest evaluated base time rewinds, and when rt is the last onset,
bt_c is that time: the event exactly there would fire twice. So
`psytl_rate()` is in the pause and resume family. It changes the mapping
and never rewinds. Only an anchor (and a skip, which is an anchor)
rewinds. When rt is before the last evaluated onset, the next window can
end behind the reach: nothing un-fires, as after an anchor whose rt is
after the onset.

### Quantization and the lead

The window ends at RT time onset + lead x period, and in base time at
bt(onset + lead x period). The code does this today. With a rate, an
event lands on the first frame whose RT window end is at or after
rt(T): the frame nearest to when the base reaches the event, in RT, at
any rate. The lead is RT time and is not scaled.

psy_video.h does not do this. It computes the due frame as the last
frame time at or before m(onset) + L, a movie time plus an RT duration.
At rate 1/2 and 60 Hz, L is 8.33 ms of RT, which is 4.17 ms of movie
time. Video frames would then use a window twice as wide as the
annotations' window, and video frames and annotations would land on
different display frames. So "no change in psy_video.h" is not true. The
change is small: the movie time at onset + L with a lead of 0, or the
window end read from the timeline through `psytl_window()`, which rig_spec
6.1 already asks for.

### Lateness and the record

The reach is a base time and does not change. The residual stays "base
time at the onset minus the event's time", in base ns. It is exact, and
it is defined on a paused base. At a rate r, the lateness in RT ns is
about residual / r; the manual says so.

The report's order, onset - residual, is the RT target only at rate 1.
With a rate, the key is rt(T) on a running base, with the mapping in
force at the evaluate, and onset - residual on a paused base (as now).
The comparator reads the event's base, so it needs no storage.
`psytl_next_due()` and the onset of a peek copy use rt(T). Peek's bounds
are already in the form that a rate needs.

### Tracks, repeats and STEP tracks

A track is a function of base time, so it plays at the rate. A sampled
track stays exact: the sample index is integer arithmetic on base time.
At rate 1/2, a 120/s table is read at 60 samples per RT second, and
CUBIC keeps it smooth. The table rate stays an integer.

A repeat runs at f x r in RT. A flicker with its edges on frame onsets is
no longer on frame onsets at rate 1001/1000. A flicker, an SSVEP drive
or anything else defined in display frames goes on a base at rate 1. The
rule of one base per channel makes this a choice per channel.

A STEP track is read at bt(onset + lead x period), as an event is.

### Tweens: base time

A tween runs in base time. Its keys are base times, its duration is base
ns and its "now" is a base time. At rate 1/2, a 200 ms tween on the movie
base takes 400 ms of RT. This is GSAP's `timeScale()` on a parent
timeline. It is also the only rule that keeps "a tween is a track" true:
a tween in RT on a scaled base needs a second clock per channel. A fade
that must take 200 ms of RT, whatever the movie does, goes on the trial
base. `keep_velocity` takes the slope per base ns. When the rate changes
during a tween, its RT velocity steps by the ratio of the two rates.
That step is a consequence of the rate change, not of the tween.

### Pause and resume

No change. A pause computes the frozen time with the rate. A resume
anchors at (rt, frozen time) with the same rate.

### Reverse

A negative rate is refused. Base time that decreases conflicts with "an
evaluate never un-fires". Every window ends behind the reach, so nothing
fires and nothing un-fires. The event channels then keep the state of the
furthest point reached: an onset at 5 s, passed backward from 10 s to
3 s, leaves the stimulus visible. Tracks reverse correctly; events do
not. A correct reverse needs a rewind at each frame, which is the rule
that v0.1.0 dropped (see "Only an anchor rewinds"). A reverse scrub is a
series of `psytl_skip()` calls. Each one is a rewind and says so in the
record. psy_video.h cannot decode backward in any case.

### Script API

Rule 2 of rig_spec.md 6.1 says a script never anchors a base. A rate on
the trial base scales every wait, tween and duration of the trial. In
psychophysics that is a confound, so the script gets no rate on the trial
base. It gets a rate on a movie only:

```lua
local m = movie("clip")
m:play{rate = 0.5}        -- from now()
m.rate = 1                -- a change at now()
await(m:at(12.5))         -- a MARK at movie time 12.5 s; resolves at any rate
```

| Script | Timeline |
|---|---|
| `m:play{rate = r}`, `m.rate = r` | `psytl_rate(tl, MOVIE, rt, num, den)`, with rt the RT time of `now()` on the trial base |
| `m:at(t)` | `psytl_add` MARK on the movie base at `t` |

A number becomes num/den with den <= 1000: exact for up to three
decimals (0.5 is 1/2, 0.999 is 999/1000), and the log records the
fraction. A movie with a soundtrack at a rate other than 1 is an error at
the call. The "Left out" entry "Time scale, reverse, seek inside a
trial" stays for the trial base.

### C additions this needs

- `psytl_rate()` and `psytl_get_rate()`. psy_video.h's
  `psyvid__base_rate()` reads the second.
- `psytl_window(tl, base, &frame, &bt_end)`: the window end in base
  time. psy_video.h and the script scheduler use it, so neither copies
  the rule.
- `psytl_rt_time(tl, base, bt, &rt)`: the inverse, rt(T). The player
  needs it to lower `now()` to an RT change point.

### Recommendation: later, not v0.3.0

- No experiment asks for it yet. With a soundtrack, a rate is refused.
  The only consumer is a silent movie in slow motion.
- It changes every conversion between base time and RT time (evaluate,
  pause, next_due, peek, skip, the report order) and the random model.
  Skip and peek are small and are needed now.
- psy_video.h needs a change too (the window end above), so a rate does
  not come without a change there. Do the rate together with
  `psytl_window()`.
- v0.3.0 keeps the change local. Peek's bounds already have the form
  that a rate needs. Each function computes the RT time of an event in
  one place.

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

For v0.3.0 the writer of the implementation extended the test. This is
weaker than the earlier rounds: the same person wrote the code, the
manual and the model, so a misreading can be in all three. The checks:
- Directed: the 30-minute seek against an anchor to the same place (1978
  skipped against 1980 fired late on one frame, the same channel
  values), a skip back against an anchor back (the same storage bytes),
  a late event that waits behind the reach and is skipped by a skip back,
  the reach and the rewind after a skip, prune and remove of skipped
  events, a never-anchored and a paused base, tracks and a waiting tween
  after a skip against an anchor (the same bits), peek's window edges,
  order across bases, cap and total, paused, stopped, fired, skipped and
  late events, refusals, and purity by `memcmp` of the whole handle and
  the storage around every peek.
- The audio look-ahead for 22 s at 60 Hz (see "Peek").
- Ten more seeds of the random model, with skip and peek among the ops.
  The model compares the whole storage and every value after each step,
  and the handle and the storage are unchanged by each peek. The first
  ten seeds run as before, op for op, and give the same counts.
- Replay: skip and peek are among the calls hashed in both handles.

Model mutations, each caught: a skip that only anchors, skipped SETs
not counted for the channel, the reach or the furthest onset not moved,
the event at `bt` skipped, a rewind that leaves skipped events skipped,
peek's window closed at `to_rt`, fired events in a peek, a peek copy's
onset as its base time, and a skip back that leaves waiting late events
pending.

Mutations of the header, made one at a time on a copy: 23. 20 are
caught. Three cannot change any result, and the reasons are:
- `FIRED` for `DONE` in the evaluate's advance of the lowest late index.
  That index is a lower bound for a scan that skips done events.
- `FIRED` for `DONE` in `psytl_next_due()`. Its scan starts at the lowest
  late event, which is pending, and every skipped event after it in the
  storage has a time that is not earlier. So the minimum cannot change.
- A paused base admitted to a peek. The paused mapping gives the frozen
  time for every RT time, so the window is empty.

The v0.2.0 test, unchanged, passes against v0.3.0 with the same model
counts. `timeline_trial` and `timeline_tracking` print the same bytes as
with v0.2.0, except the version, on gcc 11.4, MSVC 19.44 and MinGW gcc
16.1. One run of `timeline_bench` at 60 Hz (gcc 11.4, WSL2): trial 51 ns,
movie 322 ns mean and 599 ns p99, script 60 ns, tracks 246 ns. These are
inside the v0.2.0 ranges above.

## Not done

- No snapshot format. The storage is the record. A resume after a crash
  adds the events again and anchors again.
- One timeline is for one display. A second display that flips on its own
  grid needs its own timeline or its own evaluate.
- No run on macOS or on a big-endian machine.
- psy_video.h still anchors on a seek and reads `tl->lead`. Changing it
  to `psytl_skip()` and `psytl_lead()` is that header's change.
- rig_spec.md 4.5.1 still shows the draft `psytl_peek(tl, base,
  until_rt, out, cap)`. The header has a `from_rt` bound (see "Peek").
- The cost of a skip is not timed. It is one write per skipped event.
- No base rate (see the proposal).
