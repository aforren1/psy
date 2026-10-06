# psy_audio.h design notes

This note explains why `psy_audio.h` has its current form and records
what was measured. The manual in the header tells you how to use it. The
plan that asked for the header is section 4.4 of
[rig_spec.md](rig_spec.md).

All numbers come from one machine: a Windows 11 25H2 laptop (build 26200),
13th Gen Intel Core i7-1360P, on AC power, Balanced plan. The output is
"Speakers (Realtek(R) Audio)", whose Default Format is 48000 Hz, 2
channels, 24 bits in 32. The shared-mode mix format is float, 48000 Hz,
2 channels. miniaudio is 0.11.25. No loopback cable or second audio
interface was available, so no number here is an acoustic or electrical
latency. The user kept the endpoint muted at volume 0; runs that needed
the loopback tap unmuted it at volume 0 for the run and restored the mute.
Every timing run held the shared measurement lock, released between runs.

## What miniaudio does that the plan did not expect

Read in the miniaudio 0.11.25 source:

- With a nonzero sample rate in shared mode, miniaudio asks WASAPI to
  resample (`AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM`) and then reports the
  rate it asked for as the device's internal rate. A read-back after
  init cannot see the OS resampler. So the header asks for no format, no
  rate and no channel count: the client format is then the device's, and
  miniaudio's converter is a passthrough, which the header checks.
- Exclusive mode opens at the endpoint's `PKEY_AudioEngine_DeviceFormat`,
  the Default Format in the Sound control panel, whatever rate was asked
  for. So exclusive mode does not fix a rate mismatch, and the open
  message says where to change the rate instead.
- miniaudio never uses `IAudioClock`, and its capture path throws away
  each packet's device position and QPC time. The header gets
  `IAudioClock` from miniaudio's `IAudioClient` itself, with its own COM
  declarations (`tests/compile/psy_audio_com.cpp` checks every slot
  against the SDK on MSVC and MinGW), and the loopback test uses raw
  WASAPI.
- miniaudio starts the stream on an empty buffer. Measured: the position
  stays 0 for the first three callbacks, as Microsoft documents ("its
  device position might remain 0 for a few milliseconds until the audio
  data has had time to propagate"). Reports that have not moved are not
  used.

## What a WASAPI position is on this machine

Microsoft documents `IAudioClock::GetPosition`'s position as "the stream
position of the sample that is currently playing through the speakers",
with a QPC time "at the time that the audio endpoint device read the
device position". Measured over 10 minutes (60060 callbacks):

| Quantity | Result |
|---|---|
| stamp minus callback entry | 1.2 us p50, 3.3 us p99: the stamp is taken during the call |
| stream frames written minus position | 1857 to 1913, 1895 p50 (39.5 ms) |
| step of the position between callbacks | 440 to 478 frames, not 480 |
| residual of the reports from a straight line | sd 87 us; correlated (r = 0.54) with how late the callback ran |
| `GetPosition()` cost | 2.0 us p50, 8.1 us p99, 203 us max |

So a position is the engine's last pass, stamped when the call is made:
a report is late by up to a few hundred microseconds and never early.
The first design took an onset's time from the two reports around its
frame. One report can be hundreds of microseconds late, so the header
takes every onset's time from the fit over many reports instead. A
report then only confirms, in order, that the device has played past the
frame with no underrun since. No API reports when a given frame reached
the output, so the time is a model value, confirmed, never observed.

## Tiers

No v0.1 path reaches tier 1, whose definition (rig_spec.md section 8,
psy_screen.h) is an OS observation of the onset on a path the header can
verify. WASAPI shared onsets are tier 2, with the conditions stated: the
time is the device-clock fit's, the frame was confirmed played without an
underrun by later position reports, and the digital loopback test found
every burst at its planned frame and the time 67 us p99 from its median
at idle and 20 us under load in one run, against a one-frame bar of 20.8
us. Exclusive mode is tier 3: the loopback capture cannot see it, and it
did not run usably here. Every unconfirmed onset (no report past its
frame, an underrun, callback times only) is tier 3 and carries
PSYAU_ONSET_UNCONFIRMED.

`IAudioClient::GetStreamLatency` returned 0 for this stream (the slot is
checked against the SDK). The header reports it as the claim it is.

## The fit

Two clocks with their own crystals drift apart. Measured here: the device
ran +3.9 ppm against QPC over 10 minutes. At 48 kHz one frame is 20.8 us,
so even this drift moves a nominal-rate plan by one frame in 5 s, and a
50 ppm device would do it in 0.4 s. The header fits the device's frame
period on the psy_rt clock and plans each onset in device frames. It
never resamples and never inserts a frame (principle 5); a long buffer's
end follows the device clock and the END record says where it ended.

M5 replayed the 10-minute run through each candidate estimator, the way
the header runs it (one point per bucket, a refit every second over a
sliding window), and scored each prediction of the frame one second ahead
against the 10-minute line. Constant offsets do not matter here (the
loopback median absorbs them), so the score is the deviation from the
median.

| Estimator | 0.5 to 15 s: p50 / p99 us | 15 s to 10 min: p50 / p99 us |
|---|---|---|
| reports, least squares on the bucket minima, 50 ms, 1024 points | 6.2 / 22.1 | 23.7 / 108.4 |
| reports, the same, 50 ms, 4096 points | 6.2 / 22.1 | 12.2 / 98.8 |
| reports, the same, 200 ms, 1024 points (kept) | 6.3 / 21.3 | 10.5 / 109.8 |
| reports, the same, 400 ms, 512 points | 18.6 / 51.1 | 14.1 / 123.2 |
| reports, least squares on the bucket means, 50 ms | | 35.0 / 116.6 |
| reports, lower envelope of the bucket minima, 50 ms | 28.5 / 73.7 (slope from 5 s) | 40.5 / 357.5 |
| callbacks, lower envelope of the bucket minima, 50 ms (kept) | 55.5 / 101.0 | 33.9 / 206.7 |
| callbacks, least squares on the bucket means, 50 ms | | 61.7 / 359.1 |

The slope is fitted once the points span 5 s; before that the slope is
nominal and the offset comes from the last second. With least squares, a
slope from 2 s of reports gave 80.8 us at p99 in the first 15 s, from 5 s
22.1 us, from 10 s 20.3 us.

So reports are fitted by least squares on the least late report of each
200 ms, over 1024 points (205 s), and callback times (the only source on
the other backends) by the lower envelope of the least late callback of
each 50 ms. The design's guess, the envelope for reports, lost: the
reports are not purely late, and the envelope follows their outliers. On
the scripted device of the core test, where callbacks are late by an
exponential with a 1 ms mean, the envelope recovers the offset within
0.3 us and least squares on the same points misses it by 182 to 188 us.

One refit over 1024 points, timed (MSVC /O2, 200 runs, quiet machine):
least squares 5.5 us p50, 10.2 us max; the envelope 12.0 us p50, 22.9
us max. At 4096 points (timed under load) least squares took 58 us p50,
over the 50 us bar, for no better prediction. The fit runs in the
callback once a second.

### Reports early under load

In one 3-minute run with 8 spinning threads and a 60 Hz frame loop that
spins 8 ms per frame, the position jumped ahead of the fit by up to 45
ms for tens of seconds, with callbacks up to 24.5 ms late but the queue
never empty (written minus position fell to 480 frames at worst). Fitted
as they were, those reports made the fit claim -100 ppm. A report can be
late, never early, so the header now keeps any report more than half a
period early out of the fit, flags the sounds in flight XRUN (their
onsets unconfirmed, tier 3), and starts the fit over if the jump lasts 2 s.
The two loopback runs under the same load afterwards saw no such jump, so
whether the sound moves with the position when it jumps is not known.

## Scheduling accuracy (M4)

`tests/loopback/psy_audio_loopback.c --digital` plays bursts of 64 frames
(a fixed +-1 sequence at -40 dBFS, at endpoint volume 0) and captures the
same endpoint with WASAPI loopback, raw. It finds each burst by
cross-correlation and compares where it is in the capture with where
psy_audio.h planned it, and when.

The tap is not the samples as written. Muted, the capture was all zeros;
unmuted at volume 0.02 the bursts were scaled by 0.0029, at volume 0 by
0.00054, quantized to 2^-27 and smeared by a filter (the first nonzero
samples came before the burst). So the tap is after the endpoint volume
and an effect on this machine, and bit-exactness cannot be checked
there; placement and time can.

| Run | Bursts | At the planned frame distance from the one before | Capture time minus onset, deviation from its median: p50 / p99 / max us |
|---|---|---|---|
| idle, first fit design (envelope, 2 s slope) | 100 | 100 of 100 | 30 / 512 / 513 (a sawtooth: each refit moved the plan) |
| idle, least squares, 50 ms buckets | 700 | 700 of 700 | 17.8 / 41.0 / 90.9 |
| idle | 700 | 681 of 684, 2 across capture gaps (3 off by -480, -676 and -16 frames, next to the capture's own discontinuities) | 12.9 / 55.9 / 304 |
| idle, final header | 700 | 700 of 700 | 21.8 / 67.3 / 219.6 (63.5 / 67.8 after the first 6 s) |
| load, before the set-aside rule | 500 | 499 of 500 | 6.3 / 299.5 / 10005 |
| load, final header | 500 | 500 of 500 | 6.2 / 19.8 / 139.9 |

Placement held wherever the capture itself had no gap: no frame was
inserted, dropped or resampled between the mixer and the engine's
output. The time bar (p99 within a frame, 20.8 us) was missed at idle (56
to 67 us) and met in one run under load (19.8 us). The deviation wanders
slowly, by tens of microseconds over tens of seconds, and both sides of
the comparison are fits of noisy stamps of the same engine clock, so this
test cannot say which side wanders.

The capture's own packet stamps move by 228 us at p99, so the capture
time is the capture's frame count through a least-squares line of its
packets, not each packet's stamp. The median offset (33.0 ms) is the
distance from the position point to the tap; the tap comes later than
the "currently playing" position. A tap before the speakers should come
earlier, so either the position is not the sample at the speakers or the
loopback packet's stamp is not when the engine mixed it; this test cannot
tell which. It is not a latency.

Lead: with the default 10 ms period, the shortest lead with no LATE
record was 43.5 to 54.5 ms (three runs), against 50 to 55 ms from
`psyau_lead_ns()` in the same runs. That is what `psytl_peek()` must look ahead for audio on this
machine; `psytl_peek()` does not exist yet in psy_timeline.h v0.2.0
(rig_spec.md 4.5.1).

## Callbacks (M2, M3) and cost (M6, M8)

Shared mode ran at a 10 ms period whatever was asked: 240 and 96 frames
gave the same 480-frame callbacks (low-latency shared mode was not
offered for this endpoint, or miniaudio did not take it). So the default
period is 10 ms and the other periods are not offered.

| Run | Callbacks | Interval p1 / p50 / p99 / p99.9 / max ms | Underruns |
|---|---|---|---|
| idle, 10 min | 60060 | 9.71 / 10.00 / 10.29 / 10.93 / 14.35 | 0 |
| asked 240 frames, 3 min | 18061 | 9.55 / 10.00 / 10.42 / 10.79 / 11.44 | 0 |
| asked 96 frames, 3 min | 18065 | 9.51 / 10.00 / 10.49 / 10.94 / 13.72 | 0 |
| load, 3 min | 18064 | 8.94 / 10.00 / 10.77 / 24.52 / 46.51 | 0 (the jump above) |
| load, asked 240, 3 min | 18061 | 9.48 / 10.00 / 10.51 / 10.88 / 14.83 | 0 |
| M3 setup: miniaudio's default priority, load, 3 min | 18088 | 9.71 / 10.00 / 10.28 / 11.12 / 19.32 | 0 |
| M3 setup: MMCSS "Pro Audio", load, 3 min | 18088 | 9.75 / 10.00 / 10.26 / 11.01 / 13.27 | 0 |
| M3 setup: `psyrt_thread_elevate()`, load, 3 min | 18066 | 9.60 / 10.00 / 10.39 / 10.87 / 12.15 | 0 |
| M3 setup: MMCSS and `psyrt_thread_elevate()`, load, 3 min | 18088 | 9.70 / 10.00 / 10.30 / 10.87 / 16.46 | 0 |
| final header (MMCSS), load, 3 min | 18066 | 9.66 / 10.00 / 10.35 / 10.97 / 18.03 | 0 |

The rule set before M3 was the fewest underruns, then the smallest p99
interval error. No setup had an underrun, and MMCSS had the smallest p99
error (0.26 ms, against 0.28, 0.39 and 0.30), so it is the one kept and
the others are deleted. The differences are small and come from one run
each; the earlier load run with `psyrt_thread_elevate()` (the first row
under "load" above) had a p99.9 of 24.5 ms. A 10-minute idle run with
MMCSS was not repeated.

"Load" is 8 threads spinning at normal priority and a 60 Hz frame loop at
TIME_CRITICAL that spins 8 ms of each frame.

Exclusive mode, 3 runs of 30 s: the device opened at s32 (24 valid bits),
48000 Hz, a 1440-frame buffer, and `GetStreamLatency` said 30 ms. The
callbacks came in bursts (interval p50 0.12 to 0.15 ms, p99 72 to 80 ms,
max 98 to 101 ms), there were 2 underruns per run, `GetPosition` took up
to 45 ms, and the fit could not settle (spread 9 ms). Exclusive mode
through miniaudio 0.11.25 does not work on this machine; the header
still opens it, and its records say so (XRUN, a fit spread in the describe
line). Not fixed in v0.1.

Cost per callback (render, without `GetPosition`), MSVC /O2:

| Voices (stereo buffers) | Mean us | p50 us | p99 us | max us |
|---|---|---|---|---|
| 0 (10 min) | 3.3 | 2.5 | 18.6 | 1941 |
| 8, first mixer (1 min) | 23.5 | 22.5 | 57.7 | 709 |
| 32, first mixer (1 min) | 55.6 | 43.1 | 158.4 | 2367 |
| 8, final mixer (1 min) | 11.5 | 10.2 | 39.9 | 376 |
| 32, final mixer (1 min) | 18.7 | 17.0 | 56.7 | 269 |

The bar was 20 us mean and 100 us p99 at 32 voices. The first mixer
missed it (a channel-mask test per sample); the final one, with the
channel list built once per run and straight loops for full-mask mono to
stereo and for full-mask buffers, meets it (18.7 and 56.7 us). The max
values are preemptions. `GetPosition()` adds 2 to 4 us p50 and 6 to 20
us p99 (bar 20 us p99: met).

Frame thread (`psyau_play_at` + `psyau_update`, one sound per frame at 60
Hz, under the load): 1.86 us mean, 3.7 us p99, 410 us max. Bar 5 us mean
and 20 us p99: met.

## Allocations (M7)

MSVC debug build, `_CrtSetAllocHook`, with the caller's arena:

| Window | C runtime heap calls | Made by |
|---|---|---|
| `psyau_open()` | 18 | 10 through the header's allocation callbacks by miniaudio (context, device enumeration for `desc.device`, the data converter, its two threads); 8 by the C runtime making per-thread data when an OS component raised and caught a C++ exception on the new threads. None by the header's own code |
| 20 s of playing, one sound per 60 Hz frame (1200 plays) | 0 | |
| control: one malloc and free per callback, 10 s | 2000 | the control |

The stacks came from `RtlCaptureStackBackTrace` in the hook, named from
the PDB. miniaudio made 0 allocations after open in every run. Without
the caller's arena, `psyau_open()` adds one `malloc` for the arena, and
`psyau_close()` frees it.

## Underruns (M0)

A 60 ms sleep injected in one callback, against 39.5 ms of queue:

| | Result |
|---|---|
| position during the stall | stood at the frames written (253440), for three callbacks, then moved on |
| written minus position after | 1880 to 1895, as before |
| time of a given position, before and after | 40.3 ms later after the stall |

So on WASAPI the position halts while the device has no data and counts
only frames played, and every later frame plays later by the time it
stood. The header sees the position reach the frames written, records an
XRUN, flags the sounds in flight, and starts the fit over. The first
design shifted the map by the silence instead, which would be right for a
device whose position counts the silence; the scripted device of the core
test checks both. Measured with the final header: one XRUN record for the
stall.

## Core test

`tests/adapt/psy_audio_test.c` runs the header without miniaudio against
a scripted device on a virtual clock: a device clock at a set drift
(+-100 ppm), callbacks late by a seeded exponential, position reports
stamped late, injected underruns and position jumps, a tape of every
sample written. It checks the plan (200 of 200 start frames on the
truth's frame, or within 2 us of a tie), the fit (drift within 0.03 ppm,
offset within 1.1 us), LATE, cancel, stop with its fade, gain, loops,
routing, clipping, the replan after the slope is known, underruns of both
kinds (a position that counts the silence, one that halts), position
jumps, the ring records field by field, the queues and their limits, the
strict open's refusals, bit-exact 16- and 24-bit output, synthesis, the
arena, and the device on a real thread against the frame thread. Fourteen
deliberate mutations of the header each make it fail. With the two
queues' atomics made plain loads and stores, ThreadSanitizer reports 70
races; as written, none (3 runs).

Builds, warnings as errors: MSVC 19.44 (C, C++17; compile check, COM
check, core test, examples, loopback test), MinGW-w64 gcc 16.1 (the
same), gcc 11.4 in WSL2 (C99 -O3, C11 under ASan and UBSan, C11 under
ThreadSanitizer, C++17 compile check; the null device runs there), emcc
6.0.10 (core test run under node; the header with miniaudio's Web Audio
backend compiled as gnu11 and gnu++17, because miniaudio's `EM_ASM`
needs a GNU dialect). Not built here: clang on Linux and macOS (CI does).

## Streams (v0.2.0)

Movie sound must take the same path as every other sound. In
Psychtoolbox it does not: a movie's audio goes through GStreamer's own
sink, outside PsychPortAudio's schedule and records. A first soundtrack
design for `psy_video.h` used `desc.source`. That slot is fixed at
`psyau_open()` and there is only one. A soundtrack there would be
scheduled differently from every other sound, and psy_audio confirms only
its own voices. So v0.2.0 adds a voice whose samples come from a ring.
`desc.source` stays the Audio source extension slot.

### Design

- A stream is a voice. It takes a voice slot and an id. It uses the same
  plan, the same confirmation by position reports, the same ONSET and END
  records and the same stop, gain and cancel commands as a buffer. Only
  the source of the samples differs. The gain, fade and channel-routing
  loops are one kernel for buffers and streams. The kernel was moved out
  of the buffer path with no change to its arithmetic. A regression tape
  of buffer voices through every path of the kernel (mono to stereo,
  masks, a stereo buffer, a gain ramp, a stop fade, loops, clipping, on
  f32 and s16 devices) is byte-identical between v0.1.0 and v0.2.0, on
  MSVC and on MinGW gcc. `psy_audio_test --tape FILE` writes the tape.
- The lock rule. At the start the callback fixes origin, the stream frame
  of sample 0. Sample s plays on frame origin + s for the whole play. A
  ring underrun leaves the frame silent and moves the stream's position
  on by exactly that frame. A sample that arrives after its frame is
  discarded. A late start skips the samples it missed. Neither moves
  origin, so a movie clock that follows the stream stays on its sound
  frame for frame. The read index never passes the write index. The
  device thread keeps the play position separately, so the producer never
  writes a slot that the device thread reads.
- GAP records are not confirmed. A gap's frames are exact by
  construction, because the mixer wrote the silence on them. Its time is
  the fit's, as an END record's time is, and END records are not
  confirmed either. Whether the device timing held during a gap is in the
  XRUN records, in stream frames. A GAP record has the XRUN flag when a
  device underrun came while it lasted.
- GAP and STREAM records go only to the event ring. The callback-to-frame
  queue carries ONSET and END, and the loss of one of those would leak a
  voice count. Every gap is counted also without an event ring: in
  `psyau_stream_get_info()` (gaps, gap_frames, discarded), in
  `psyau_onset.gap_frames` and in `psyau_caps.gaps`. A test with 300 gaps
  in one play dropped no message.
- Loops are the producer's job. The ring is read once. A loop held in
  memory is a buffer, and buffers already loop. The sample count must go
  on across cycles, or the lock rule breaks at each wrap. `psyau_wav`
  loops by reading sample s from file frame s mod A.
- A play at 0 waits for the preroll (a quarter of the ring by default;
  `PSYAU_PREROLL_NONE` for no wait), so an "as soon as it can" stream
  does not begin with a gap. A timed play checks nothing at the call. The
  producer can catch up before a start that is seconds away, and a start
  with an empty ring is a recorded gap. `ready` and `frames_to_start` in
  `psyau_stream_get_info()` let the caller check before the start.
- A reset and a play cannot race. Each takes the stream's state by
  compare-and-swap, and only the frame thread and the producer use it.
  The device thread only loads and stores, and it stores the read index
  before it stores ENDED.
- Memory. A stream is 256 bytes, one cache line for each writer. A stream
  voice keeps its stream pointer in the place of the buffer pointer, so
  the voice array stays 200 bytes a voice. A `psyau_wav` is about 17 KB,
  of which 16 KB is its read buffer.
- The ring holds float samples. M-S2 measured an int16 ring, widened in
  the callback and fused with the mix: no benefit, so it was deleted.

### WAV files

The parser is in `psy_audio.h` for three reasons. The psy_video
soundtrack and every long sound need the same parser. `psy_video.h`
already includes `psy_audio.h` for the soundtrack. Loading audio is this
header's job (rig_spec 5.2: audio is widened to float exactly at load).
The parser accepts RIFF, RF64 and BW64 files with 16-bit, 24-bit,
24-in-32 or 32-bit float samples, and refuses everything else by name.

A path is read with stdio and `setvbuf(f, NULL, _IONBF, 0)`. So the C
runtime allocates no stdio buffer on the producer thread, and each read
(at most 16 KB) goes to the OS (`ReadFile` on Windows, `read()` on POSIX),
which still caches the file. The header does not use
`FILE_FLAG_NO_BUFFERING` or `O_DIRECT`, which bypass the OS cache and
need sector-aligned offsets, sizes and buffers. `psyau_wav_load()` reads a
whole file into the arena and widens it in place.

### Tests

`tests/adapt/psy_audio_test.c` uses the scripted device and the virtual
clock, as before. The stream cases use identity samples. Every value is a
multiple of 2^-13, so sums are exact, and the checks compare every output
frame to the bit.

| Case | What it checks |
|---|---|
| Placement | A 60 s stream through a 1000-frame ring (2880 wraps), written in chunks of 1, 13, 479, 997 and 4097 frames: 2879016 frames checked, 0 wrong. 50 timed starts at every phase between frames, at +37, +100 and -100 ppm: each start on the truth's frame (or within 2 us of a tie), `sample` = first, tier 2, confirmed |
| Ring | Regions up to the wrap, a commit past the region refused, the space after a block, end, reset, the states. Refusals: loops, offset or a buffer with a stream; 3 channels; a ring under 2 periods; a negative first; a preroll over the ring; another handle's stream; an arena that is too small |
| Gaps | Three producer stalls, the gaps starting inside a block: silence exactly on the frames whose samples were missing, every later sample on its planned frame, one GAP record per run; records, counters and output agree. A gap at the start keeps the ONSET on its planned frame, flagged GAP |
| Late start | origin from the target, the skipped samples in the STREAM record and in the discard count. A late start with too little data is LATE and GAP |
| At 0 | Waits for the preroll, starts on the next block when the preroll is there. `PSYAU_PREROLL_NONE` starts at once. An end inside the preroll starts the play. A cancel while it waits gives CANCELED |
| Mix | 2 streams (mono to the right channel only, stereo) and a buffer voice in one run: the output is the model sum, bit for bit. A buffer and a stream with the same samples, gain ramp and stop fade on the two channels give identical output. CLIPPED reaches the stream |
| Device | Both kinds of device underrun while a stream plays (XRUN, UNCONFIRMED, the identity kept in stream frames), a device with callback times only (tier 3), close in the middle of a stream (ENDED) |
| Gap storm | 300 gaps in one play: 300 counted, 0 messages dropped, ONSET and END complete |
| Threads | A producer thread (random chunks, random stalls of up to 40 ms) races the device thread while the frame thread plays, stops and replays the stream 40 times and the producer resets it between plays. Every output frame is checked against the record of its play, and the silent frames equal the records' gap counts |
| WAV | Seven forms (s16, s24 mono, 24 in 32, float, EXTENSIBLE float, RF64 with ds64, BW64 with an odd chunk) parsed, streamed through a 1000-frame ring, played and checked to the bit, signs included. `psyau_wav_load` of each gives the same values. Loops over a ring smaller than the file, a seek after the end, a seek message during a play that waits for the end. Path, reader and memory give identical samples. A NaN ends the stream before it. Ten refusals with their reasons, and the speaker mask against the project map |
| WAV on a pump | `psyau_wav_step` and `psyau_wav_on_msg` on a real `psyrt_pump`, the device on a real thread, 12 plays with seeks sent as messages: every play starts on its seek sample, every frame checked |
| Regression tape | Buffer voices through every kernel path on f32 and s16 devices: byte-identical to v0.1.0 (MSVC and MinGW) |

Builds: MSVC 19.44 (C /W4 /WX; the compile check as C and C++17),
MinGW-w64 gcc 16.1 (C11 test; C11, C++17 and C99 compile checks; the
loopback test and audio_clockstats), gcc 11.4 in WSL2 (C99 -O3, C11 under
ASan and UBSan, C11 under ThreadSanitizer: 0 reports in 3 runs), emcc
6.0.10 (the core test under node with threads; the header with
miniaudio's Web Audio backend compiled as gnu11 and gnu++17). The compile
checks play a stream on miniaudio's null device.

Mutations of the header, each in a scratch copy. All 18 below made the
core test fail. The last two are races that only ThreadSanitizer can see:

| Mutation | Failed checks |
|---|---|
| An underrun moves origin instead of discarding | 11 |
| A late start plays the first sample on its first frame (no skip) | 3 |
| The late skip off by one | 1 |
| The ring slot off by one at the wrap | 68 |
| Producer space one frame too large | 36 |
| A gap counts the whole block, not its missing frames | 5 (it survived until the gap test made gaps start inside a block) |
| A play at 0 starts without the preroll | 7 |
| Stream onsets skipped by the confirmation | 354 |
| `end` ignored | 459 |
| Late samples played instead of discarded | 12 |
| The stream path without the kernel's gain and fade | 7 |
| 24-bit samples without sign extension | 6 |
| ds64 sizes ignored | 4 |
| Reset allowed in any state | 15 |
| WAV loops without the modulo | 4 |
| GAP records not pushed | 2 |
| origin without the first sample's index | 300 |
| The ONSET record's `sample` off by one | 76 |
| `w` committed with a plain store (TSan) | 4, 4, 4 reports in 3 runs |
| The read index stored after ENDED (TSan) | 2, 2, 2 reports in 3 runs |

### Measurements

Conditions for every row: the laptop above, on AC power (battery status
2), the measurement lock held for each row and released between rows,
`C:\tmp\psy-quiet` checked before each row, MSVC 19.44 /O2, WASAPI shared
mode on the default endpoint with no format request. The endpoint stayed
muted at volume 0 and no endpoint setting was touched. All streams and
voices played silence or a -60 dBFS tone, and the mix cost does not
depend on the values. Rows were interleaved, one round after the other.
The tool is `examples/audio_clockstats.c` (`--streams`, `--wav`, `--ring`).
The baseline is v0.1.0 from `git show HEAD:psy_audio.h`, built from the
same tool source.

M-S1 and M-S2, render cost per callback, 60 s a row, 3 rounds. A stream
here is stereo, with a 1 s ring that a producer thread fills with
silence in 4096-frame steps.

| Row | Mean us, rounds 1 / 2 / 3 (average) | p99 us | max us | Gaps | Underruns |
|---|---|---|---|---|---|
| a. v0.1.0, 32 voices | 12.16 / 13.76 / 8.22 (11.38) | 29.4 / 27.2 / 27.2 | 123 / 70 / 65 | | 0 |
| b. v0.2.0, 32 voices | 9.92 / 12.87 / 8.26 (10.35) | 26.9 / 30.7 / 23.2 | 128 / 67 / 118 | | 0 |
| c. v0.2.0, 32 voices + 1 stream | 11.73 / 11.03 / 8.67 (10.48) | 30.3 / 30.5 / 25.5 | 68 / 112 / 113 | 0 | 0 |
| d. v0.2.0, 32 voices + 4 streams | 13.06 / 12.59 / 10.56 (12.07) | 33.7 / 34.7 / 30.5 | 75 / 91 / 115 | 0 | 0 |
| e. v0.2.0, 0 voices + 1 stream | 4.95 / 5.56 / 4.01 (4.84) | 13.8 / 13.4 / 11.2 | 87 / 64 / 34 | 0 | 0 |
| f. int16 ring, 32 voices + 4 streams | 13.14 / 11.18 / 12.09 (12.14) | 32.3 / 33.0 / 27.8 | 89 / 102 / 293 | 0 | 0 |

Bars and results:

- (b) against (a), no regression for buffer voices: met. The mean was
  1.0 us lower, inside the spread of the rounds (8.2 to 13.8 us for the
  same row). The 32-voice means are lower than v0.1.0's 18.7 us of
  2026-10-05 in both headers; the comparison is the interleaved one.
- (c) at most (b) + 2 us: met (+0.13 us).
- (d) at most (b) + 6 us and 25 us, p99 at most 100 us: met (+1.7 us,
  12.07 us, p99 34.7 us).
- M-S2: keep the int16 ring only if it is at least 1 us faster at 4
  streams. It was 0.07 us slower (12.14 against 12.07 us), so it was
  deleted and the ring holds float.

M-S3, the producer, no device. `acquire` + `commit`: 10000 calls each.
`psyau_wav_feed` of 4096 frames: one 3-minute stereo file per format
(34.6, 51.8 and 69.1 MB), every 4096-frame block of it, from the path
twice and from memory once.

| Operation | Mean us | p50 us | p99 us | max us |
|---|---|---|---|---|
| acquire + commit, 480 frames | 0.04 | 0.00 | 0.10 | 0.10 |
| acquire + commit, 4096 frames | 0.05 | 0.00 | 0.10 | 0.20 |
| feed s16, path, pass 1 | 15.2 | 11.8 | 63.8 | 199.1 |
| feed s16, path, pass 2 | 15.4 | 14.1 | 58.8 | 109.7 |
| feed s16, memory | 5.1 | 4.5 | 8.7 | 35.0 |
| feed s24, path, pass 1 | 24.4 | 18.9 | 76.7 | 119.0 |
| feed s24, path, pass 2 | 22.1 | 18.9 | 71.0 | 129.7 |
| feed s24, memory | 13.9 | 15.1 | 19.6 | 47.4 |
| feed f32, path, pass 1 | 22.3 | 19.4 | 66.7 | 119.3 |
| feed f32, path, pass 2 | 21.8 | 19.7 | 65.9 | 150.5 |
| feed f32, memory | 11.9 | 8.4 | 19.1 | 48.9 |

Bars: acquire + commit at most 1 us p99, and feed at most 0.5 ms p99 per
4096 frames: both met. The page cache: each file was written just before
the run, so pass 1 was already served from the OS cache. Pass 1 and pass
2 do not differ, so this run says nothing about a cold read from the
disk. That was not measured, because Windows gives no user-level way to
drop a file from the cache. A cold read is a producer cost: the ring
(500 ms of margin at the default size, M-S4) absorbs it, and the device
thread never waits for it.

M-S4, the default ring size. One `psyau_wav` stream (the 3-minute s16
file, looped) on a `psyrt_pump`, woken by a 60 Hz frame loop when the
ring is under half. Load: 8 threads spinning at normal priority, and a
frame loop at TIME_CRITICAL that spins 8 ms of each frame. 3 minutes a
row, 2 rounds.

| Ring | Gaps | Lowest fill seen by the frame loop | Render mean us | Frame thread mean / p99 us |
|---|---|---|---|---|
| 0.25 s | 0, 0 | 110 ms, 110 ms | 8.46, 8.35 | 3.49 / 15.8, 3.11 / 14.7 |
| 0.5 s | 0, 0 | 230 ms, 230 ms | 8.11, 8.35 | 2.77 / 13.9, 2.64 / 12.8 |
| 1 s | 0, 0 | 480 ms, 480 ms | 8.34, 9.40 | 2.36 / 11.3, 2.51 / 11.2 |

The bar was 0 gaps and a lowest fill of at least half the ring. No row
met the second half, and the bar was wrong. The frame loop wakes the pump
when the ring falls under half, so the lowest fill is half the ring minus
one wake interval and a block (about 20 ms) at every size. It measures
the wake rule, not the producer. What the runs show is that a step of the
pump refills the ring before the next frame: no gap at any size. The
default stays 1 s. The margin against a producer stall is the half ring
that is left when the pump is woken: 480 ms at 1 s, 110 ms at 0.25 s. A
psy_video pump that also decodes a 1080p frame (about 15 ms a step) and
reads a cold file shares that margin. The cost is memory: 384 KB at 48
kHz stereo. A smaller ring is one desc field for a program that wants
less memory.

M-S5, heap calls, MSVC debug build with `_CrtSetAllocHook`, 20 s, 4
voices and 4 streams, a frame loop playing a sound every frame:

| Run | C runtime heap calls in the run | miniaudio |
|---|---|---|
| 4 WAV streams on a pump | 0 | 0 |
| 4 streams from a producer thread | 0 | 0 |
| control (one malloc and free per callback, 10 s) | 2000 | 0 |

The first run of the producer-thread row counted 2 calls. In that run
the tool started the producer thread just before the count began. With
the thread started 100 ms before the count, the row reads 0, so the 2
calls are put down to the thread's start (they were not traced). The C
runtime calls of `fopen` in `psyau_wav_open` come at open, on the frame
thread, before the count. None follow, because the stdio buffer is off.

M-S6, the frame thread under the same load as M-S4: `psyau_play_at` and
`psyau_update` per 60 Hz frame, plus, with streams,
`psyau_stream_get_info` and `psyau_wav_wants` for each stream and a pump
wake when one is under half. 60 s a row, 2 rounds, 8 voices.

| Row | Mean us | p99 us | max us | Render mean us |
|---|---|---|---|---|
| 4 WAV streams | 2.93, 2.70 | 13.0, 11.5 | 20, 18 | 13.77, 13.77 |
| no streams | 1.74, 1.97 | 2.6, 2.7 | 13, 8 | 9.30, 9.49 |

Bar: mean at most 5 us and p99 at most 20 us: met. The p99 with streams
is about 10 us higher. That difference was not broken down. The one
system call that streams add to the path is the pump wake
(`psyrt_pump_submit` takes the pump's mutex and signals its condition
variable), which comes about once every 10 frames.

Not run: the line-in check of a stream (`tests/loopback/psy_audio_loopback.c
--line --stream`, built, not run: the user runs the hardware checks).
WASAPI's loopback capture with a stream was not run either, because it
needs the endpoint unmuted.

## Not measured

- Latency to sound. No loopback cable; `--line` is built, not run. It
  reports the measured latency minus the OS's claim when it does.
  `--line --stream` (stream placement and latency) is built, not run.
- CoreAudio, ALSA, PulseAudio, Web Audio: compiled, never run. PulseAudio
  under WSLg plays to the Windows speakers and was not run.
- Exclusive mode beyond the 3 runs above.
- Whether the sound follows a WASAPI position jump under load: the jump
  happened once, in a run without the loopback capture.
- Any other machine, driver, rate or channel count.
