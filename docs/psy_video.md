# psy_video.h design notes

This note explains why `psy_video.h` has its current form and records what
was measured. The manual in the header tells you how to use it. The plan
that asked for the header is section 4.6 of [rig_spec.md](rig_spec.md).

All numbers come from one machine: a Windows 11 25H2 laptop, i7-1360P, on
AC power, Intel Iris Xe, the 1920 x 1200 panel at 60.0008 Hz
(`\\.\DISPLAY1`), ANGLE 2.1.23876 from Docker Desktop. Window runs were
640 x 360, composed (tier 2), with low-contrast content. Timing runs held
the shared measurement lock. No photodiode was used, so no number here is
a measurement of light.

## Running status

This section is the running record. Update it when an item changes, so
that a restart loses nothing.

| Item | State |
|---|---|
| Scheduler, records, controls (play, pause, seek, loop, end, manual) | Done, tested on a virtual clock |
| Movie clock from a psy_timeline base; own anchor without one | Done, tested (agreement under cadence and noise) |
| Follow another clock (`psyvid_follow`, `psyvid_follow_audio`) | Done, tested with a scripted 200 ppm clock; no run against real audio |
| Decoder interface, frame sequence (read, write, raw, QOI), pl_mpeg | Done, tested; pl_mpeg run on generated clips |
| Decode-ahead on a pump, inline mode | Done, tested (one threaded case) |
| Upload through psy_gfx.h; I420 and NV12 to RGBA8 on the pump | Done, measured |
| Mutations | 23 of 23 caught |
| Build matrix | MSVC, MinGW, gcc (ASan, UBSan, TSan), emcc: pass (see "Builds and mutations") |
| Decode thread that cannot start (wasm without -pthread) | Falls back to inline decode; describe() says so |
| Follow-up (2026-10-05) | psy_timeline.h v0.3.0: `psytl_lead()` replaces the read of `tl->lead`; seeks and manual jumps use `psytl_skip()`, and the SEEK record counts the skipped annotations. psy_screen.h: `psyscr_frame.done` completes every flip record. See "Seeks and annotations" and "Records" |
| Not in this task | Media Foundation, the shared and zero-copy GPU paths (they wait for psy_gfx.h's planar formats and texture import; `psyscr_native()` exists now), the planar shader, capture, FFmpeg, the soundtrack source |

## Decisions that changed the design

The design (2026-10-05, probe and plan) was reviewed. These changes are
the coordinator's and the user's:

1. **Judder is allowed by default**, as in Psychtoolbox's default movie
   mode. The frame due at each onset is shown; repeats and drops follow
   the cadence. A showing beyond floor(R/r) is REPEATED with reason
   CADENCE, and a frame the refresh cannot show is DROPPED with reason
   CADENCE. There is no tier penalty. `desc.strict_cadence` restores the
   refusal.
2. **One lead for video and annotations.** The due rule is
   psy_timeline.h's QUANTIZATION rule with the movie base's lead. With a
   timeline, its lead is used and a different `desc.lead` is refused.
3. **The movie clock is read from the timeline**, not written into it.
   With `desc.timeline` and `desc.base`, `movie_time(onset)` is
   `psytl_base_time()`. play, pause, seek and loop anchor, pause and
   rewind the base. So a later psy_timeline.h with an exact base rate
   gives slow motion with no change here. Rate is 1 in v0.1.
4. The design's other recommendations stand: no second display-locked
   clock, 10-bit refused, the timestamp checked on every frame and the
   hash on CPU paths, pl_mpeg fetched and pinned, QOI in the header,
   manual mode for frame-index control.

## The due rule and frame times

Frame i covers movie time [t(i), t(i+1)) with
t(i) = ceil(i x den x 1e9 / num). The ceiling makes `t(i) <= x` for an
integer x the same as the exact rational comparison. So "the largest i
with t(i) <= m + L" is exact, and an annotation stored at t(i) on the
movie base lands on the same display frame as frame i (the test checks
this under a 3:2 cadence and 20 us of onset noise). The integer
arithmetic is split so that rates with num x den x 1e9 < 2^63 never
overflow; open refuses others.

Under 2 us of onset noise, 30 fps on 60 Hz, lead 0.5 placed every frame;
the never-early rule (`PSYVID_LEAD_NONE`) misplaced 26 to 39 of 700 display
frames (core test).

## Cadence and slips

At open, R/r is compared with integers. Within 200 ppm of k, each frame is
due for k display frames and repeats are DUE. Otherwise the describe line
gives the cadence and the durations, for example "cadence 3:2, frames on
screen 33.3 to 50.0 ms" for 24000/1001 on 60 Hz.

A slip is the movie clock and the display grid running at different
rates. To find slips, each decision is compared with a nominal schedule:
the due frame if the clock ran in step with the vblank count at the
nominal period (T / k at a multiple, the mode's period otherwise) since
the last anchor or slip. A difference is a slip (DRIFT), and the schedule
restarts from the clock's phase at that frame.

The first version tracked the offset between the due and the nominal
frame and called a change in it a slip, without restarting. The test
showed it wrong: after the first slip the phase of the frames against the
nominal grid is a fraction of a frame, so the offset alternates 0, 1,
0, 1 and every frame looked like a slip. Restarting at each slip counts
one record per slip, which the test checks against an exact model of the
clock and the grid (487.3 ppm: 4 slips in 9000 display frames, the first
at frame 1027 or 1029 against 1026 predicted, both signs).

An onset within a few ns of a frame boundary is a tie, where rounding or
noise decides; DRIFT is then the right reason. A rational rate on an
exact grid makes ties periodic (24000/1001 on exactly 60 Hz: every 1001
display frames; 500 ppm at 30 fps on 60 Hz: at every slip). The tests
avoid such grids by 10 ppb; a real display is never that exact.

## Decisions

j is the frame on screen, i the due frame, e the nominal due frame.

| Case | Decision | Why |
|---|---|---|
| i = j = e | REPEATED j | DUE at a multiple; CADENCE beyond floor(R/r) |
| i = j, i != e | REPEATED j | DRIFT |
| i > j, i ready | SHOWN i | DUE; DRIFT when the slip has no drop; SEEK, LOOP, MANUAL after those |
| i > j, i not ready, a frame in (j, i) ready | SHOWN that frame | DECODE_LATE |
| i > j, nothing ready | REPEATED j | DECODE_LATE |
| frames j+1 .. shown-1 | DROPPED | DECODE_LATE for those due before they were ready; DISPLAY_LATE when the vblank count jumped; DRIFT for a slip; CADENCE when the nominal schedule drops them too |

The decoder learns the due frame before it decodes, so a random-access
decoder skips a stalled frame as soon as it is older than due (one
DECODE_LATE repeat whatever the stall), and a GOP decoder waits for its
keyframe, then seeks to the next keyframe when that is shorter (core test:
stalls of 1, 3 and 20 frames at GOP 1 and 10).

A decoder failure ends the movie only after the frames decoded before it
are shown: they are good frames.

## Pause, resume and seek

A pause freezes the movie time at the onset of the display frame nearest
the requested time. The frozen frame stays on screen. A resume starts one
display period after the frozen time. The first version resumed at the
frozen time, which showed that movie time on two display frames, with the
pause between them (core test, and a mutation).

A seek posts the target to the decode thread with a new epoch. Slots of an
older epoch are freed at the head of the ready queue on every update; the
first version did not, so after a seek every slot held a stale frame and
the decoder had none free (core test).

## Records

One FRAME record per display frame while playing, pushed when its flip
record arrives. `psyvid_update()` completes the records of every flip in
`psyscr_frame.done` (psy_screen.h lists each record completed since the
last begin). A frame filled by hand with only `last` still works: a
pending record older than `last` is then completed as ESTIMATED.
`psyvid_flip_done()` from `psyscr_on_flip()` gives every flip too. The
core test holds one flip record back a frame and delivers it in `done`
ahead of the newer one: the frame keeps its own onset and is not
ESTIMATED (a mutation that reads only `last` fails it). The record's
word 9 is the display frame index, the word PSYSCR_EV_FLIP carries.

## Seeks and annotations

A seek anchors the movie base with `psytl_skip()`: the annotations it
passes over are marked SKIPPED and never fire, and the one at the target
fires on the frame that shows it. The SEEK record's word 9 counts the
skipped annotations, and `info.skipped` sums them. Before psy_timeline.h
v0.3.0 a forward seek fired every annotation it passed over, late, on one
display frame. In manual mode a jump skips the same way; a step to the
next frame fires the annotations up to that frame's time, as playing
would. Core test: a seek from frame 25 to 133 skips 108
annotations (the record equals the count of SKIPPED events) and no
annotation fires more than a period from its time; in manual mode the
jump from frame 2 to 150 skips the 147 between them and fires only 150's.
A loop wrap still uses an anchor: it rewinds, so the annotations fire
again in the next cycle.

The timeline's lead comes from `psytl_lead()`.

**A base rate other than 1 needs a change here.** The due frame uses the
movie time at the onset plus the lead, L = lead x period. L is RT ns and
the movie time is base ns; at rate 1 they are the same unit. At rate r
the due frame needs the base time at RT time onset + L (or the window
psy_timeline.h computes, a `psytl_window()`), not base time at onset plus
L. psy_video.h reads the base through one function, `movie_time()`, and
adds L in `psyvid_update()`; both lines change together.

## Frame sequence and the index

The frame sequence is as in the design (section 3.3), with XXH64 in place
of CRC-32 for the header, the frames and the index. One hash function
serves the container, the .psyvi index and the run-time check. XXH64
runs at 4.3 to 5.3 GB/s here (table below), so a 1080p I420 frame costs
0.6 to 0.7 ms on the decode thread. The writer reserves the index at
create when `max_frames` is set, so writing allocates nothing per frame.

The .psyvi index: a 128-byte header (magic `PSYVIDX1`, version 1, codec,
size, format, rate, frame count, GOP, the five color fields, the media
file's size and the XXH64 of its first and last 64 KB, the XXH64 of the
header), then one XXH64 per decoded frame, then the command that made
the file, as text. `psyvid_index_make()` decodes the file with pl_mpeg,
refuses B-frames, a GOP that changes length, a first frame that is not a
keyframe, and a frame whose time is not its index, and writes the index.

Checked on generated clips: a clip with `-bf 2` is refused ("B-frames
(frame 2)"); one byte changed in the middle of an indexed clip passes
the size and end hashes and is flagged by the frame hashes ("NOT
CANONICAL: hash mismatches"); one changed index hash is flagged.

## pl_mpeg

- **No delay.** By default pl_mpeg returns each frame one call late, to
  reorder B-frames, and its `picture_type` is then the next frame's. The
  canonical form has no B-frames, so the backend sets
  `plm_video_set_no_delay()`: decode order is display order, a frame
  comes out as it is decoded, and the index maker reads the type of the
  frame it got. Found when the index maker refused every clip ("the first
  frame is not a keyframe").
- **Files over 2 GB.** pl_mpeg's file buffer uses `ftell`, 32 bits on
  Windows. The backend gives pl_mpeg its own load, seek and tell callbacks
  over the header's 64-bit byte source, which also carries the reader.
  The load discards read bytes first, as pl_mpeg's file buffer does, so
  the buffer does not grow.
- **Heap.** `PLM_MALLOC`, `PLM_REALLOC` and `PLM_FREE` go through the
  header's counter. 0 heap calls per frame after frame 120 in every window
  run, pl_mpeg included.
- **Warnings.** MSVC /W4 reports C4244, C4267 and C4305 in pl_mpeg; they
  are disabled around its include only. gcc 11 and clang are clean.
- **Time.** pl_mpeg's frame time is a count over a double rate. The
  backend maps it to 90 kHz units and the core rounds to a frame index,
  which holds for hours at any MPEG-1 rate.

## Upload and conversion

The decode thread converts I420 and NV12 to RGBA8 with the canonical
matrix and range; the frame thread uploads RGBA8 with one
`psygfx_texture_update()`. The planar shader path, with linear light,
waits for psy_gfx.h. Chroma is brought to 4:4:4 by weights in sixteenths
for the stated siting (left: co-sited across, between rows down;
center: between both), or replicated (`PSYVID_CHROMA_NEAREST`). The
conversion is within 1 code of the same weights and matrix in double,
over every matrix, range, siting and both formats (core test).

The conversion runs in two plain loops per row, a chroma pass and a
matrix pass, so that a compiler can vectorize them. An A/B in one process
at 1080p: MSVC 19.44 /O2 6.80 ms (one pass) against 7.03 ms (two passes),
the same bytes; gcc 11 -O3 in WSL2 11.2 ms against 6.7 ms. Kept the two
passes.

## Measurements

Under the lock, 2026-10-05, MSVC 19.44 Release.

Decode thread, per frame (`examples/video_bench.c`, 200 repetitions or
600 frames):

| Work | 1280 x 720 mean / p99 | 1920 x 1080 mean / p99 |
|---|---|---|
| pl_mpeg decode (generated clip, q 3, GOP 30) | 2.22 / 4.88 ms | 5.54 / 12.98 ms |
| XXH64 of the I420 planes | 0.27 / 0.31 ms | 0.59 to 0.72 / 0.88 to 1.61 ms |
| I420 to RGBA8, sited chroma | 3.08 to 3.12 / 3.96 to 7.08 ms | 6.66 to 7.78 / 7.71 to 20.0 ms |
| I420 to RGBA8, nearest chroma | 2.33 / 2.93 ms | 5.41 / 9.97 ms |
| QOI decode, smooth frame (26.7:1) | 1.01 / 1.26 ms (3.7 GB/s) | 2.37 / 4.18 ms (3.5 GB/s) |
| QOI decode, noise (1.0:1) | 5.00 / 5.74 ms (0.74 GB/s) | 11.47 / 16.45 ms (0.72 GB/s) |

So a 1080p MPEG-1 frame costs about 14 ms of one core on the decode
thread (decode, hash, conversion): 2.4 times real time at 30 fps, and
not enough for 1080p at 60 fps. The conversion costs more than the
decode; the planar shader removes it.

Frame thread, `psyvid_update()` including the upload, in a 640 x 360
window, display frames after the 120th (`examples/video_play.c`, 20 s
each). Bar: the 16.7 ms frame. psy_gfx.md measured a full 1920 x 1200
RGBA8 upload at 1.1 to 2.6 ms mean and 4.4 to 5.8 ms p99.

| Movie | Frames with an upload: mean / p50 / p99 / max | Repeats: mean / p99 |
|---|---|---|
| 1280 x 720 MPEG-1, 30 fps | 0.52 / 0.46 / 1.00 / 2.08 ms | 0.008 / 0.017 ms |
| 1280 x 720 RGBA8 frame sequence, raw | 0.52 / 0.46 / 0.89 / 2.48 ms | 0.010 / 0.025 ms |
| 1920 x 1080 MPEG-1, 30 fps | 0.95 / 0.82 / 1.62 / 2.64 ms | 0.007 / 0.015 ms |
| 1920 x 1080 RGBA8 frame sequence, raw | 0.98 / 0.85 / 1.78 / 2.85 ms | 0.009 / 0.024 ms |

Every run: 0 heap calls after frame 120, no DECODE_LATE, every repeat
DUE (30 fps on 60.0008 Hz is a multiple at +13 ppm). One run had one
DISPLAY_LATE drop, and a 23.976 run one at a 117 ms stall of the
compositor (5 vblanks), both with the flip's LATE flag.

24000/1001 MPEG-1 on the window, three runs of 9 s: CADENCE repeats on
the 3-showing frames (105 to 109 a run), one DRIFT at the first frames of
a run (the screen's predicted onset moved by a period while the vblank
count moved by one) and one later in another.

What the composed window does to the counts. In the first runs,
psy_screen.h sometimes planned two consecutive frames for the same vblank
in a composed window (onsets 136 ns apart, after flips flagged EARLY) and
sometimes skipped one, so a count of display frames per video frame read
1 and 4 where the cadence is 2 and 3. psy_screen.h fixed the double
planning; three new 9 s runs at 24000/1001 (2026-10-05, 21:43, under the
lock) had no vblank planned twice (0 of 1600 display frames; 2 in 2 runs
before). One run showed only 2 and 3 vblanks per frame. The other two had
one frame on 4 vblanks and the next on 1, each at a vblank gap of 2 whose
flip carried LATE_TARGET (the frame loop was late), at the start and
about 1.7 s in; psy_video.h called those DISPLAY_LATE or DRIFT. Every
record in those runs came through `psyscr_frame.done` (n_done 1, and 2 or
3 after a gap). psy_video.h's decisions follow the vblank count and the
onset either way.

Not measured: slips on the real panel (13 ppm: one each 20.9 min at
30 fps), fullscreen, the audio clock against a real device, a decode
thread under load, any GPU but the Iris Xe.

## Tests

`tests/adapt/psy_video_test.c`, no GPU, no display, no pl_mpeg: a scripted
decoder, scripted display frames on a virtual clock, and psy_gfx.h on the
simulated display with a recording backend, so each upload is checked
byte for byte. It covers the pure functions against their definitions,
XXH64 against python-xxhash's vectors, QOI round trips and truncations
(and the encoder's bytes against the reference `qoi.h` when it is on the
include path: equal), the conversion against double, 30 on 60 under noise
and the never-early control, five cadences against an exact model, slips
at 487.3 ppm both ways against a model, display drops, decode stalls with
the catch-up seek, seeks across GOPs (target exact, keyframe and discards
in the record), stay-paused seek, loop with the base rewound, end,
manual mode with the base held, pause and resume, the timeline agreement
at 23.976 on 60 (20 us noise), 30 on 60 and 60 on 30, a timeline lead
that differs, the follow clock at 200 ppm, the FRAME record field by field,
each refusal at open with its message, decoder faults, the frame sequence
in all ten formats from a file and from memory with padded strides,
damage, the reader, replay (byte-equal records over 3000 frames with a
stall, a display drop, a seek and a pause), 0 heap calls per frame, and
one case with the decode thread running for real.

## Builds and mutations

Warnings as errors everywhere.

| Toolchain | What ran |
|---|---|
| MSVC 19.44, CMake, Release | compile check C and C++17 (with and without pl_mpeg), core test with the qoi.h comparison, examples; full ctest without pl_mpeg 39 of 40, the one failure `test_psy_gfx`, which did not compile at that moment (psy_gfx_test.c line 2061, another agent's edit in progress) |
| MSVC 19.44, cl alone | compile check as C and as C++17, each with and without pl_mpeg, /W4 /WX |
| MinGW-w64 gcc 16.1 (winlibs) | compile check C99, C11 and C++17 with pl_mpeg, C11 without; core test with the qoi.h comparison |
| gcc 11.4, WSL2 | compile check C99 -O3, C11 and C++17 under ASan and UBSan; core test under ASan and UBSan, and under ThreadSanitizer (no report; the compile check with its decode thread too). In the follow-up, psy_gfx.h (mid-edit, v0.3) had unused-function warnings for a while; the mutation runs used -Wno-error=unused-function |
| emcc 6.0.10, node | compile check with threads, without (decodes inline), and with PSYRT_NO_THREADS; core test with threads and with PSYRT_NO_THREADS; pl_mpeg compiled as C11 and C++17 |

Not built here: clang on Linux and macOS, MinGW against SDL3 (CI does).

The core test caught each of 23 deliberate mutations of the header: the
at-or-after rule; counting vblanks instead of reading the clock;
DISPLAY_LATE and DECODE_LATE swapped; no catch-up to the due frame; a
resume that re-shows the frozen time; the follow clock ignored; record
completion one frame off; a missing DROP record; the hash check skipped;
the timestamp check skipped; the CADENCE repeat a showing late; frame
times floored; stale slots not purged; the due published after the inline
decode; QOI's LUMA range off by one (caught only by the qoi.h comparison:
the round trip still works); left siting weighted as center; an XXH64
constant; no resync after a slip; the loop wrap without the base rewind;
the canonical check skipping the siting (the loop wrap mutation is caught
by the run's 600 s timeout: the test hangs); `psyscr_frame.done` ignored
in favor of `last`; a seek that anchors instead of skipping; a manual step
that skips instead of firing (caught after the test gained an annotation
between frames 5 and 6). A first try at "a seek lands on
the keyframe" was not caught because the catch-up to the due frame masks
it; it is not a fault the code can show. The test also found that the
first conversion scratch in the test was too small for the two-pass
conversion: `psyvid_yuv_rows_bytes()` now sizes it, and the test checks.

## Needed from the other headers

| Header | Request | Why |
|---|---|---|
| psy_screen.h | `psyscr_native()`: the D3D11 device and context, the EGL display, the adapter LUID | Delivered (psy_screen.h, 2026-10-05). Not used yet: the shared path waits for psy_gfx.h |
| psy_screen.h | A desc flag for a device with VIDEO_SUPPORT and multithread protection | Zero-copy only |
| psy_screen.h | Each completed flip record in `psyscr_frame` | Delivered: `done`, `n_done`, `done_lost`; used |
| psy_gfx.h | Planar `PSYGFX_NV12` and `PSYGFX_I420` with matrix, range, light and siting, converted in the IMAGE shader | Removes the 6.7 to 7.8 ms conversion at 1080p from the decode thread, and gives linear light |
| psy_gfx.h | `psygfx_texture_import()`, `psygfx_texture_rebind()`, per-plane update | The shared path |
| psy_gfx.h | `psygfx_screen(g)` and `psygfx_calibrated(g)` | psy_video.h reads `g->screen` and `g->cal_crc`, private fields |
| psy_timeline.h | `psytl_skip(tl, base, rt, bt)` | Delivered (v0.3.0); used for seeks and manual jumps |
| psy_timeline.h | `psytl_lead(tl)` | Delivered (v0.3.0); used |
| psy_timeline.h | An exact base rate, `psytl_rate(tl, base, num, den)`: base time at RT t is bt + (t - rt) x num / den in integers, the anchor rules unchanged, and the window end of a frame (`psytl_window()`) | Slow motion: `movie_time()` reads the base, and the due frame must take the base time at RT onset + L (see "Seeks and annotations"). `psyvid__base_rate()` returns 1/1 today; a soundtrack is refused at any other rate (no resampling) |

## Future work

- `examples/movie_play.c` was asked for; the example is
  `examples/video_play.c`, because CMake builds `examples/<lib>_*.c` for
  each header.
- docs/rig_spec.md is not edited; the design's sections 0 and 10.6 and
  this note carry the findings.
- The soundtrack source (a PCM resource streamed into a `psyau_source`)
  and its loop check. `psyvid_follow_audio()` exists; the source does not.
- Media Foundation, the shared GPU path, zero-copy after V9, AVFoundation,
  FFmpeg, capture: the design's sections 3.2, 4 and 8.
- A faster conversion, or none: the planar shader.
