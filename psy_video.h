/* psy_video.h - v0.1.0 - public domain single-header video playback library
 *
 *   A movie as a stimulus. On each display frame the header takes the
 *   frame's PREDICTED ONSET, asks the movie clock for the movie time at
 *   that onset, and shows the video frame due then, by psy_timeline.h's
 *   lead rule. It never counts frames. Every display frame gets a record:
 *   the video frame, when it was due, when the flip showed it, and the
 *   decision (shown, repeated, dropped) with its reason. Frames are decoded
 *   ahead on a psy_rt.h pump and uploaded into a psy_gfx.h IMAGE texture.
 *   Also a frame sequence container (raw or QOI frames, read and write,
 *   value-exact), MPEG-1 through pl_mpeg, and the Video decoder extension
 *   interface of the rig's spec (section 12).
 *
 *   REQUIRES psy_gfx.h (so psy_screen.h and psy_rt.h) and psy_timeline.h
 *   beside it. pl_mpeg.h (commit c871f2b, one MIT file) on the include path
 *   for MPEG-1; PSYVID_NO_PL_MPEG builds without it. psy_audio.h is
 *   optional: include it first and psyvid_follow_audio() appears.
 *
 *   Written in the single-header style of the stb / sokol libraries. C99 is
 *   the floor: it builds as C99, C11 and C++17, and in the C dialect MSVC
 *   compiles by default.
 *
 *   ---------------------------------------------------------------------
 *   CHANGELOG
 *   ---------------------------------------------------------------------
 *   v0.1.0 - first release: the scheduler (due frame by the timeline's
 *          lead rule, cadence, slips, drops and repeats with reasons), the
 *          movie clock read from a psy_timeline base, the decoder
 *          interface, the frame sequence container (raw, QOI), pl_mpeg
 *          with its index, decode-ahead on a pump, upload through
 *          psy_gfx.h, I420 and NV12 converted to RGBA8 on the decode
 *          thread, manual mode, seek, loop, the records.
 *
 *   STATUS: v0.1.0, on one Windows 11 laptop (i7-1360P, Iris Xe, ANGLE),
 *   in a 640 x 360 composed window (tier 2). Measured: psyvid_update() on
 *   the frame thread with a 1920 x 1080 RGBA8 upload 0.95 to 0.98 ms mean,
 *   1.6 to 1.8 ms p99; 1280 x 720 0.52 ms mean, 0.9 to 1.0 ms p99; a
 *   repeat 0.01 ms. On the decode thread at 1080p: pl_mpeg 5.5 ms a frame,
 *   I420 to RGBA8 6.7 to 7.8 ms, XXH64 0.6 to 0.7 ms; QOI decode 0.7 to
 *   3.5 GB/s. No heap call per frame, pl_mpeg included. A core test with a
 *   scripted decoder and display on a virtual clock (cadences and slips
 *   against exact models, stalls, seeks, the timeline agreement under
 *   20 us of onset noise) passes on MSVC 19.44 (C, C++17), MinGW gcc 16.1
 *   (C99, C11, C++17), gcc 11.4 (C99 -O3, ASan and UBSan, ThreadSanitizer)
 *   and emcc 6.0.10 (node, with and without threads); 23 mutations of the
 *   header each make it fail. Not measured: slips on the real panel,
 *   fullscreen, a real audio clock, light. Media Foundation, AVFoundation,
 *   FFmpeg, the shared and zero-copy GPU paths, the planar YUV shader,
 *   the soundtrack source and capture are not in v0.1: open() refuses the
 *   ones a desc can ask for, by name. docs/psy_video.md has the tables.
 *   Outside it and this block, a number in this header is a measurement
 *   only where the text says "measured".
 *
 *   ---------------------------------------------------------------------
 *   USAGE
 *   ---------------------------------------------------------------------
 *   Do this:
 *
 *       #define PSY_VIDEO_IMPLEMENTATION
 *
 *   in *one* C or C++ file before including this header. It brings the
 *   implementations of psy_gfx.h, psy_screen.h, psy_rt.h and
 *   psy_timeline.h with it, once, and pl_mpeg's (BUILDING).
 *
 *   Play a movie, centered at 1:1:
 *
 *       static psyscr_screen scr; static psygfx_gfx gfx; static psyvid_movie mv;
 *       psyscr_open(&scr, &(psyscr_desc){0});
 *       psygfx_open(&gfx, &(psygfx_desc){ .screen = &scr });
 *       if (!psyvid_open(&mv, &gfx, &(psyvid_desc){ .path = "clip.psyseq" }))
 *           die(psyvid_error(&mv));
 *       psygfx_stim film = psyvid_stim(&mv, &(psyvid_stim_desc){0});
 *       psyvid_play_at(&mv, PSYVID_ASAP);
 *       psyscr_frame f;
 *       while (psyscr_begin(&scr, &f) == PSYSCR_OK && psyvid_update(&mv, &f) == PSYVID_OK) {
 *           psygfx_begin(&gfx, &f); psygfx_draw(&gfx, &film); psygfx_end(&gfx);
 *           psyscr_flip(&scr);
 *       }
 *
 *   Psychtoolbox and PsychoPy, for comparison (from their documentation):
 *
 *       movie = Screen('OpenMovie', win, 'clip.mp4');               % PTB
 *       Screen('PlayMovie', movie, 1);
 *       while true
 *           tex = Screen('GetMovieImage', win, movie);
 *           if tex <= 0, break; end
 *           Screen('DrawTexture', win, tex); Screen('Flip', win); Screen('Close', tex);
 *       end
 *
 *       mov = visual.MovieStim(win, 'clip.mp4')                     # PsychoPy
 *       mov.play()
 *       while not mov.isFinished:
 *           mov.draw(); win.flip()
 *
 *   The differences: the stimulus is a psy_gfx.h IMAGE (place, anchor,
 *   size, ori, opacity, groups and timeline bindings work as for any
 *   image) whose texture the movie rewrites, so it does not change from
 *   frame to frame. psyvid_update() picks the frame due at f.onset; it
 *   never waits and never reads a clock. The file must be in the canonical
 *   form (CANONICAL FORM); the others accept any file and convert rate,
 *   size and color without a record. There is no rate argument. In C++17,
 *   zero a desc and set its fields one by one.
 *
 *   ---------------------------------------------------------------------
 *   MODEL
 *   ---------------------------------------------------------------------
 *   TIME
 *     Times are int64_t ns on the psy_rt.h clock. A movie time is ns from
 *     the start of frame 0. Video frame i covers movie time
 *     [t(i), t(i+1)) with t(i) = ceil(i * den * 1e9 / num) for the rate
 *     num/den (psyvid_frame_time). The ceiling makes an integer comparison
 *     with t(i) the same as the exact rational one.
 *
 *   THE DUE FRAME (psyvid_due)
 *     On a display frame with predicted onset T and period P, with m the
 *     movie time at T and L = lead x P truncated to whole ns, the due frame
 *     is the largest i with t(i) <= m + L. This is psy_timeline.h's
 *     QUANTIZATION rule, so an annotation at t(i) on the movie base fires
 *     on the display frame that first shows frame i, with the same lead.
 *     desc.lead: 0 means 0.5 (the nearest display frame; onset noise of
 *     microseconds never moves a frame), PSYVID_LEAD_NONE never early
 *     (Psychtoolbox's "most recent frame"). With desc.timeline the
 *     timeline's own lead is used, and a different desc.lead is refused.
 *
 *   THE MOVIE CLOCK
 *     With desc.timeline and desc.base (1..7), the movie time at T is that
 *     base's time at T (psytl_base_time). psyvid_play_at, pause_at, seek
 *     and loop anchor, pause and rewind the base; the caller evaluates the
 *     timeline after psyvid_update() as usual, and annotations on the base
 *     fire on the display frame that shows them. Without a timeline the
 *     header keeps its own anchor with the same arithmetic. The movie
 *     plays at rate 1 or not at all: slow motion is manual mode
 *     (psyvid_show) or an offline re-encode. A seek moves the base with
 *     psytl_skip(): the annotations it passes over are marked
 *     PSYTL_EV_SKIPPED and never fire, and the one at the target fires on
 *     the frame that shows it; the SEEK record counts the skipped ones.
 *     A base rate other than 1 (a later psy_timeline.h) needs the due frame
 *     from the base time at RT onset + L, not base time at onset plus L:
 *     the lead is RT ns. At rate 1 the two are the same.
 *     psyvid_follow_audio() (with psy_audio.h) or psyvid_follow() re-anchors
 *     the movie clock every frame from another clock: the movie time is the
 *     soundtrack's position at the output, read through the audio fit.
 *     Audio is never resampled; video follows by showing the frame due, so
 *     frames repeat or drop when the two clocks slip.
 *
 *   CADENCE AND SLIPS
 *     At open the header compares the refresh R (desc.refresh_num/den, or
 *     the screen's mode) with the rate r. Within 200 ppm of an integer
 *     multiple k, each frame is due for k display frames; repeats there are
 *     DUE. Otherwise the cadence judders (23.976 fps on 60 Hz: 3, 2, 3, 2
 *     display frames per frame), as in Psychtoolbox's default movie mode:
 *     a showing beyond floor(R/r) is REPEATED with reason CADENCE, and a
 *     frame the refresh cannot show (r > R) is DROPPED with reason
 *     CADENCE. No tier penalty: the cadence is a property of the stimulus,
 *     and psyvid_describe() states it with the frame durations.
 *     desc.strict_cadence refuses a refresh that is not a multiple.
 *     Each decision is also checked against a nominal schedule: the movie
 *     time the clock would give if it ran in step with the vblank count at
 *     the nominal period. A difference is a slip, reason DRIFT: the movie
 *     clock and the display grid run at different rates (13 ppm on the
 *     laptop panel: one slip every 21 minutes at 30 fps), or onset noise
 *     met a frame boundary exactly. The schedule restarts at each slip.
 *
 *   DECISIONS (psyvid_record.decision, .why)
 *     j is the frame on screen, i the due frame, e the nominal due frame.
 *       i = j, i = e      REPEATED j, DUE or CADENCE (above)
 *       i = j, i != e     REPEATED j, DRIFT
 *       i > j, ready      SHOWN i (DUE; DRIFT when i != e; SEEK, LOOP or
 *                         MANUAL after those); frames j+1..i-1 DROPPED
 *       i > j, not ready  the newest ready frame after j that is not later
 *                         than i is SHOWN with DECODE_LATE, else j is
 *                         REPEATED with DECODE_LATE
 *     A dropped run is DECODE_LATE when its frames were due before they
 *     were ready, DISPLAY_LATE when the vblank count jumped (the flip
 *     before missed its vblank), CADENCE when the nominal schedule drops
 *     them too, and DRIFT otherwise. A frame is never shown late as if it
 *     were on time.
 *
 *   SEEK, LOOP, END, MANUAL
 *     psyvid_seek(mt, resume) and psyvid_seek_frame(i, resume) post the
 *     target to the decode thread. The frame on screen stays and FRAME
 *     records stop until the target is ready; the movie resumes at
 *     `resume` (PSYVID_ASAP: once desc.preroll frames are ready;
 *     PSYVID_STAY_PAUSED: shows the target and stays paused) with movie
 *     time t(target). A codec seeks to the keyframe at or before the target
 *     and decodes forward without keeping the frames; closed GOPs and no
 *     B-frames make that exact. desc.loop: after frame N-1 comes frame 0;
 *     the movie clock's base is rewound to 0 at t(N), so annotations fire
 *     again (1 ns of rounding per loop at a rate that is not a whole number
 *     of ns per frame). Without loop, psyvid_update() returns PSYVID_ENDED
 *     from the first display frame whose due frame is N or later; the last
 *     frame stays in the texture. psyvid_show(i) before psyvid_update()
 *     shows frame i on this display frame (MANUAL), and the movie base is
 *     held at t(i): a step to the next frame fires the annotations up to
 *     t(i), a jump skips the ones it passes over. A frame outside the
 *     decoded window is a seek of the decoder, and the record says so.
 *
 *   CANONICAL FORM (checked at open)
 *     One rate num/den, one size and pixel format, closed GOPs of a
 *     constant length, no B-frames, and a stated color: matrix (BT.601,
 *     BT.709, BT.2020 NCL), range, transfer, primaries and chroma siting.
 *     "Unspecified" is refused. 10-bit is refused. A frame sequence holds
 *     its own description; an MPEG-1 file needs its index beside it (path
 *     + ".psyvi"), made by psyvid_index_make() (the pack tool's step): its
 *     description, the media file's size and the hashes of its first and
 *     last 64 KB, and the XXH64 of every decoded frame. Each value the
 *     backend reports must equal the index's. During play every frame's
 *     timestamp must map to the frame expected (else PSYVID_F_TS_MISMATCH)
 *     and every frame decoded on the CPU must hash to the index's value
 *     (else PSYVID_F_HASH_MISMATCH); the frame is still shown and the
 *     describe line says the file is not canonical.
 *
 *   DECODE-AHEAD AND UPLOAD
 *     Each movie has its own psy_rt.h pump at normal priority (desc.pin_cpu
 *     pins it). The pump decodes one frame per idle call into a free slot
 *     until desc.ahead frames are ready, then blocks until the frame
 *     thread frees a slot. Frames older than the due frame are decoded
 *     without being kept (a P-frame needs its reference), or skipped by a
 *     seek to the next keyframe when that is shorter. I420 and NV12 are
 *     converted to RGBA8 on the pump, with the matrix and range of the
 *     canonical form, by integer arithmetic within 1 code of a double
 *     reference; chroma by the stated siting (desc.chroma SITED) or by
 *     replication (NEAREST). The scene value is the code (light CODES):
 *     the shader path for linear light waits for psy_gfx.h's planar
 *     formats. psyvid_update() uploads the due frame with one
 *     psygfx_texture_update() (psy_gfx.h updates textures only between
 *     frames: call it after psyscr_begin() and before psygfx_begin()) and
 *     ends the frame's PSYSCR_PHASE_UPLOAD. Slots: ahead + 2 of w x h x
 *     the upload format's bytes, one allocation at open (or desc.mem).
 *     Nothing allocates per frame on either thread (psyvid_heap_calls()).
 *     desc.inline_decode runs the decode step inside psyvid_update()
 *     instead, so a run is a pure function of its inputs (tests).
 *
 *   RECORDS (desc.ring; source PSYRT_SRC_VIDEO, aux desc.movie_index)
 *     PSYVID_EV_FRAME, one per display frame while playing, pushed when its
 *     flip record arrives (psyscr_frame.done in a later psyvid_update(), or
 *     psyvid_flip_done() from psyscr_on_flip()):
 *       t_ns       the flip's onset; the predicted onset when the flip has
 *                  no record (PSYVID_F_ESTIMATED) or was never shown
 *       u.i64[0]   video frame index
 *       u.i64[1]   due: the RT time of the frame's start on the movie clock
 *       u.i64[2]   the movie time at the predicted onset
 *       u.u32[6]   flags (PSYVID_F_*)
 *       u.u16[14]  decision bits 0..1, why bits 2..5, tier bits 6..8,
 *                  path bits 9..10 (PSYVID_EV_DECISION_OF() and friends)
 *       u.u16[15]  frames ready ahead
 *       u.u32[8]   display frames this video frame has been on screen
 *       u.u32[9]   the display frame index, low 32 bits: the word
 *                  PSYSCR_EV_FLIP carries, so a join needs no table
 *     The residual is t_ns - u.i64[1]; with the audio clock it is the A/V
 *     error of that frame. Other kinds:
 *       PSYVID_EV_DROP    t_ns due time of the first, i64[0] first frame,
 *                         i64[1] count, u16[14] why, u32[9] display index
 *       PSYVID_EV_OPEN    i32[0..5] w, h, format, fps num, den, codec;
 *                         i32[6] backend, i32[7] multiple (0 = judder),
 *                         u32[8] color word (matrix | range << 8 |
 *                         transfer << 16 | primaries << 24), i32[9] ahead
 *       PSYVID_EV_PLAY, _PAUSE  t_ns the display frame's onset, i64[0]
 *                         movie time, i64[1] id, i64[2] frame, i64[3] the
 *                         requested time (0 = ASAP)
 *       PSYVID_EV_SEEK    t_ns the onset that showed the target, i64[0]
 *                         target, i64[1] keyframe used, i64[2] frames
 *                         decoded and discarded, i64[3] the onset of the
 *                         request, u32[8] id, u32[9] annotations
 *                         skipped on the movie base
 *       PSYVID_EV_LOOP    t_ns onset, i64[0] cycle
 *       PSYVID_EV_END     t_ns onset, i64[0] last frame shown
 *       PSYVID_EV_CLOCK   t_ns anchor, i64[0] the other clock's unit at
 *                         movie time 0, i64[1] its rate
 *       PSYVID_EV_DECODE  t_ns start, i64[0] the decode's length in ns,
 *                         i64[1] frame: one decode longer than a frame
 *     Controls (play_at, pause_at, seek) return an id > 0; psyvid_result()
 *     gives its PLAY, PAUSE or SEEK record once it happened.
 *
 *   TIERS
 *     A FRAME record carries its flip's tier (psyscr_tier numbering): the
 *     decision says what was shown, the tier how well the onset is known.
 *     desc.min_tier flags worse frames PSYVID_F_BELOW_TIER; the movie goes
 *     on. A timestamp or hash mismatch is a fault, not a tier.
 *
 *   FRAME SEQUENCE (psyvid_seq_*)
 *     One little-endian file. A 4096-byte header (magic PSYVSEQ1, size,
 *     format, compression, rate, frame count, color, index offset, XXH64 of
 *     the header), then per frame a 64-byte frame header (magic, index,
 *     size, the XXH64 of the decoded texels, and for capture a psy_rt time,
 *     trial, event and camera time) and the data, each padded to 4096
 *     bytes, then the index (offset, size, flags, hash: 24 bytes per
 *     frame). Formats: the eight psy_gfx.h texture formats, stored as
 *     psy_gfx.h takes them (R16F and RGBA16F as float, rounded to half by
 *     the GPU), and I420 and NV12. RAW frames, or QOI for RGBA8 (lossless,
 *     coded by the header's own QOI, bit-equal with the reference qoi.h).
 *     Every frame is a keyframe, so a seek is a read. The writer appends
 *     from one thread, writes the index and fixes the header at close.
 *
 *   DECODER EXTENSION (psyvid_decoder; desc.backend PSYVID_BACKEND_CUSTOM)
 *     The backend owns its codec objects and reports its stream's own
 *     timestamps; the core owns the clock, the index, the checks, the
 *     slots, the schedule and the records. Every call comes from the
 *     decode thread, never two at once. next() decodes the next frame in
 *     presentation order into the caller's planes (dst NULL: decode and
 *     discard), or lends its own planes (PSYVID_OUT_BORROWED), and returns
 *     0, PSYVID_ENDED, PSYVID_PENDING (nothing now; call again later) or a
 *     negative code. seek(key) positions it so the next next() returns the
 *     keyframe `key`. The core maps out.pts to a frame index as
 *     round(pts * num / (den * timescale)), or takes out.index when the
 *     backend knows it.
 *
 *   ---------------------------------------------------------------------
 *   BUILDING
 *   ---------------------------------------------------------------------
 *   Links what psy_gfx.h links. pl_mpeg.h on the include path, or
 *   PSYVID_NO_PL_MPEG. Its implementation is compiled here unless
 *   PSYVID_PL_MPEG_EXTERNAL (then psyvid_index_make() and the reader
 *   source are not available for MPEG-1). Its heap calls go through
 *   PSYVID_MALLOC, PSYVID_REALLOC and PSYVID_FREE (default: the C
 *   library's), which psyvid_heap_calls() counts. PSYRT_NO_THREADS forces
 *   desc.inline_decode, and so does a decode thread that cannot start (wasm
 *   built without -pthread): the movie plays, each decode inside
 *   psyvid_update() on the frame thread, and psyvid_describe() says so.
 *   Define PSYVID_API to change the linkage.
 */
#ifndef PSY_VIDEO_H_INCLUDED
#define PSY_VIDEO_H_INCLUDED

#define PSYVID_VERSION_MAJOR 0
#define PSYVID_VERSION_MINOR 1
#define PSYVID_VERSION_PATCH 0
#define PSYVID_VERSION_STRING "0.1.0"

#include "psy_gfx.h"
#include "psy_timeline.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PSYVID_API
#define PSYVID_API extern
#endif

/* --- results -------------------------------------------------------------- */

#define PSYVID_OK                    0
#define PSYVID_ENDED                 1  /* update: the due frame is past the last */
#define PSYVID_PENDING               2  /* result: not happened yet; decoder: later */
#define PSYVID_ERR_ARG             (-1)
#define PSYVID_ERR_CLOSED          (-2)
#define PSYVID_ERR_FORMAT          (-3)  /* not the canonical form              */
#define PSYVID_ERR_ORDER           (-4)  /* a call out of turn                  */
#define PSYVID_ERR_FULL            (-5)
#define PSYVID_ERR_DECODER         (-6)  /* the backend failed; psyvid_error()  */
#define PSYVID_ERR_REFUSED         (-7)  /* a cadence, a rate, a light          */
#define PSYVID_ERR_LOST            (-8)  /* the GL context is new               */
#define PSYVID_ERR_IO              (-9)
#define PSYVID_ERR_NOT_FOUND      (-10)
#define PSYVID_ERR_NOT_IMPLEMENTED (-11)

#define PSYVID_ASAP           0          /* play_at, seek: the first frame it can */
#define PSYVID_STAY_PAUSED  (-1)         /* seek: show the target, stay paused */
#define PSYVID_LEAD_NONE   (-1.0)        /* desc.lead: never early             */

#define PSYVID_NS_PER_S INT64_C(1000000000)

/* --- formats and color ------------------------------------------------------ */

/* 1 to 8 are psy_gfx.h's texture formats, with its input types. */
#define PSYVID_FMT_R8       1
#define PSYVID_FMT_RG8      2
#define PSYVID_FMT_RGBA8    3
#define PSYVID_FMT_R16F     4    /* float in the file, half on the GPU        */
#define PSYVID_FMT_RGBA16F  5
#define PSYVID_FMT_R32F     6
#define PSYVID_FMT_RGBA32F  7
#define PSYVID_FMT_R16      8    /* uint16_t; psy_gfx.h's R16UI, k / 65535     */
#define PSYVID_FMT_I420    32    /* Y, then Cb, then Cr; 8-bit 4:2:0           */
#define PSYVID_FMT_NV12    33    /* Y, then CbCr interleaved; 8-bit 4:2:0      */
#define PSYVID_FMT_P010    34    /* 10-bit 4:2:0: refused in v0.1             */

/* Color, every field 0 = unspecified (refused for YUV). RGB formats are
 * device values, as psy_gfx.h images are: RGB, FULL, DEVICE, DEVICE, NONE. */
#define PSYVID_MATRIX_RGB      1
#define PSYVID_MATRIX_BT601    2
#define PSYVID_MATRIX_BT709    3
#define PSYVID_MATRIX_BT2020   4   /* non-constant luminance                  */
#define PSYVID_RANGE_LIMITED   1
#define PSYVID_RANGE_FULL      2
#define PSYVID_TRC_DEVICE      1
#define PSYVID_TRC_BT1886      2
#define PSYVID_TRC_SRGB        3
#define PSYVID_TRC_LINEAR      4
#define PSYVID_TRC_GAMMA22     5
#define PSYVID_PRIM_DEVICE     1
#define PSYVID_PRIM_BT709      2
#define PSYVID_PRIM_BT601_525  3
#define PSYVID_PRIM_BT601_625  4
#define PSYVID_PRIM_BT2020     5
#define PSYVID_SITING_NONE     1   /* not subsampled                          */
#define PSYVID_SITING_LEFT     2   /* MPEG-2, H.264, HEVC: co-sited across     */
#define PSYVID_SITING_CENTER   3   /* MPEG-1, JPEG: between both ways          */

#define PSYVID_CODEC_SEQ       1
#define PSYVID_CODEC_MPEG1     2
#define PSYVID_CODEC_H264      3
#define PSYVID_CODEC_HEVC      4
#define PSYVID_CODEC_CUSTOM    5

#define PSYVID_SEQ_RAW 0
#define PSYVID_SEQ_QOI 1

/* --- enums ------------------------------------------------------------------ */

typedef enum psyvid_decision { PSYVID_SHOWN = 1, PSYVID_REPEATED = 2, PSYVID_DROPPED = 3 } psyvid_decision;
typedef enum psyvid_why {
    PSYVID_WHY_DUE = 0,      /* on schedule                                    */
    PSYVID_WHY_CADENCE,      /* the refresh is not a multiple of the rate      */
    PSYVID_WHY_DISPLAY_LATE, /* the flip before missed its vblank              */
    PSYVID_WHY_DECODE_LATE,  /* the frame was not decoded in time              */
    PSYVID_WHY_DRIFT,        /* the movie clock slipped against the grid       */
    PSYVID_WHY_SEEK,
    PSYVID_WHY_LOOP,
    PSYVID_WHY_MANUAL,
    PSYVID_WHY_COUNT
} psyvid_why;

typedef enum psyvid_backend {
    PSYVID_BACKEND_AUTO = 0,   /* from the file's first bytes                 */
    PSYVID_BACKEND_SEQ,        /* frame sequence                              */
    PSYVID_BACKEND_PLMPEG,     /* MPEG-1 in MPEG-PS, pl_mpeg, CPU             */
    PSYVID_BACKEND_MF,         /* Media Foundation: not in v0.1               */
    PSYVID_BACKEND_AVF,        /* AVFoundation: not in v0.1                   */
    PSYVID_BACKEND_FFMPEG,     /* not in v0.1                                 */
    PSYVID_BACKEND_CUSTOM      /* desc.decoder                                */
} psyvid_backend;

typedef enum psyvid_path {
    PSYVID_PATH_AUTO = 0,
    PSYVID_PATH_UPLOAD,        /* glTexSubImage2D through psy_gfx.h           */
    PSYVID_PATH_SHARED,        /* Windows shared textures: not in v0.1        */
    PSYVID_PATH_ZERO_COPY      /* not in v0.1                                 */
} psyvid_path;

typedef enum psyvid_light {
    PSYVID_LIGHT_AUTO = 0,     /* CODES; refused with a calibration for YUV   */
    PSYVID_LIGHT_CODES,        /* R'G'B' codes as device values               */
    PSYVID_LIGHT_EOTF          /* linear light: needs the planar shader       */
} psyvid_light;

typedef enum psyvid_chroma { PSYVID_CHROMA_SITED = 0, PSYVID_CHROMA_NEAREST = 1 } psyvid_chroma;

/* --- the decoder extension ------------------------------------------------------ */

#define PSYVID_DECODER_VERSION 1

#define PSYVID_DEC_CPU           0x1u   /* decodes into CPU planes            */
#define PSYVID_DEC_RANDOM_ACCESS 0x2u   /* any frame is a keyframe            */
#define PSYVID_DEC_GPU_D3D11     0x4u   /* reserved                           */

#define PSYVID_OUT_KEYFRAME 0x1u
#define PSYVID_OUT_BORROWED 0x2u        /* out.planes are the backend's, valid
                                         * until its next call               */
#define PSYVID_OUT_HAS_HASH 0x4u        /* out.hash is the container's XXH64  */

/* A byte source read by range (a streamed pack entry). */
typedef struct psyvid_reader {
    int64_t (*read)(void* ctx, int64_t offset, void* buf, int64_t n);   /* bytes, or < 0 */
    int64_t size;
} psyvid_reader;

/* What the backend can report; -1 or 0 where it cannot (the index fills it). */
typedef struct psyvid_stream {
    int32_t  w, h;                /* 0 = unknown                              */
    int32_t  format;              /* PSYVID_FMT_*; 0 = unknown                */
    int32_t  fps_num, fps_den;    /* 0 = unknown                              */
    int64_t  frames;              /* -1 = unknown                             */
    int32_t  gop;                 /* 0 = unknown; 1 = every frame a keyframe  */
    int32_t  codec;               /* PSYVID_CODEC_*                           */
    int16_t  matrix, range, transfer, primaries, siting;   /* -1 or 0 = unknown */
    int16_t  reserved_;
    int64_t  timescale;           /* pts units per second; 0 = pts unused     */
    uint32_t caps;                /* PSYVID_DEC_*                             */
    uint32_t reserved2_;
} psyvid_stream;

/* Planes of a frame. The core's: tight rows; the backend's: its strides. */
typedef struct psyvid_planes {
    uint8_t* data[3];
    int32_t  stride[3];           /* bytes per row                            */
    int32_t  w[3], h[3];          /* texels per plane                         */
} psyvid_planes;

typedef struct psyvid_out {
    int64_t       pts;            /* in stream.timescale units, as the stream says */
    int64_t       index;          /* the frame's index when known; else -1    */
    uint64_t      hash;           /* with PSYVID_OUT_HAS_HASH                 */
    uint32_t      flags;          /* PSYVID_OUT_*                             */
    uint32_t      reserved_;
    psyvid_planes planes;         /* with PSYVID_OUT_BORROWED                 */
} psyvid_out;

typedef struct psyvid_decoder_open {
    const char*          path;
    const void*          data;
    size_t               size;
    const psyvid_reader* reader;
    void*                reader_ctx;
    const void*          index;
    size_t               index_size;
} psyvid_decoder_open;

typedef struct psyvid_decoder {
    uint32_t    version;          /* PSYVID_DECODER_VERSION                   */
    const char* name;
    int  (*open)(void* ctx, const psyvid_decoder_open* in, psyvid_stream* out,
                 char* err, size_t err_cap);
    int  (*next)(void* ctx, psyvid_planes* dst, psyvid_out* out);
    int  (*seek)(void* ctx, int64_t key, int64_t key_pts);
    void (*close)(void* ctx);
    int  (*describe)(void* ctx, char* buf, size_t cap);   /* may be NULL       */
} psyvid_decoder;

/* --- description, info, record ------------------------------------------------ */

typedef struct psyvid_desc {
    const char*          path;          /* a file; or                          */
    const void*          data;          /* the file in memory; or              */
    size_t               size;
    const psyvid_reader* reader;        /* a byte source                       */
    void*                reader_ctx;
    const void*          index;         /* the .psyvi bytes; NULL = path + ".psyvi" */
    size_t               index_size;
    psyvid_backend       backend;       /* 0 = from the file                   */
    psyvid_path          gpu_path;      /* 0 = UPLOAD                          */
    int32_t              ahead;         /* frames decoded ahead; 0 = 6         */
    int32_t              preroll;       /* frames ready before ASAP; 0 = ahead */
    bool                 loop;
    bool                 strict_cadence;/* refuse a refresh that is not a multiple */
    bool                 inline_decode; /* decode inside update(): tests       */
    bool                 reserved_;
    int32_t              inline_budget; /* decodes per update inline; 0 = no limit */
    double               lead;          /* 0 = 0.5; PSYVID_LEAD_NONE; (0, 1)   */
    psyvid_light         light;
    psyvid_chroma        chroma;
    psytl_timeline*      timeline;      /* the movie base lives here; or NULL  */
    int32_t              base;          /* 1 .. PSYTL_MAX_BASES-1 with a timeline */
    int32_t              refresh_num;   /* 0 = the screen's mode               */
    int32_t              refresh_den;
    psyrt_ring*          ring;          /* records; NULL = none                */
    uint32_t             movie_index;   /* aux of every record                 */
    int32_t              min_tier;      /* 0 = off                             */
    void*                mem;           /* slot memory; NULL = allocated       */
    size_t               mem_bytes;
    const psyvid_decoder* decoder;      /* PSYVID_BACKEND_CUSTOM               */
    void*                decoder_ctx;
    int32_t              pin_cpu;       /* decode thread; 0 = no pin           */
} psyvid_desc;

typedef struct psyvid_info {
    int32_t  w, h;
    int32_t  fps_num, fps_den;
    int64_t  frames;
    int64_t  duration;               /* ns: t(frames)                         */
    int32_t  gop;
    int32_t  codec;
    int32_t  format;                 /* the stream's                          */
    int32_t  upload_format;          /* the texture's (psygfx_format)         */
    uint8_t  matrix, range, transfer, primaries, siting;
    uint8_t  reserved_[3];
    int32_t  refresh_num, refresh_den;   /* 0 = unknown                       */
    double   per_frame;              /* display frames per video frame        */
    int32_t  multiple;               /* k, or 0 for a cadence that judders    */
    double   err_ppm;                /* the refresh against k x rate          */
    double   lead;                   /* resolved: 0.5, 0 for NONE, ...        */
    int32_t  ahead, slots;
    size_t   slot_bytes;
    psyvid_backend backend;
    psyvid_path    path;
    psyvid_light   light;
    int32_t  worst_tier;
    uint64_t shown, repeated, dropped;
    uint64_t repeats_by[PSYVID_WHY_COUNT];
    uint64_t drops_by[PSYVID_WHY_COUNT];
    uint64_t ts_mismatch, hash_mismatch;
    uint64_t skipped;                /* annotations seeks passed over (psytl_skip) */
} psyvid_info;

/* The record of one display frame while playing. */
typedef struct psyvid_record {
    int64_t  display;        /* psyscr_frame.index                             */
    int64_t  frame;          /* video frame index                              */
    int64_t  due;            /* RT time the frame was due on the movie clock   */
    int64_t  movie_t;        /* movie time at the predicted onset              */
    int64_t  predicted;      /* the predicted onset                            */
    int64_t  onset;          /* the flip's onset; 0 while pending              */
    uint8_t  decision;       /* psyvid_decision                                */
    uint8_t  why;            /* psyvid_why                                     */
    uint8_t  tier;           /* psyscr_tier numbering; 0 while pending         */
    uint8_t  path;
    uint16_t flags;          /* PSYVID_F_*                                     */
    uint16_t ahead;          /* frames ready after this one                    */
    uint32_t shows;          /* display frames this frame has been on screen   */
    uint32_t reserved_;
} psyvid_record;

#define PSYVID_F_PENDING        0x001u  /* the flip has no record yet          */
#define PSYVID_F_LATE           0x002u  /* the flip showed after its vblank    */
#define PSYVID_F_ESTIMATED      0x004u  /* t_ns is not an OS time              */
#define PSYVID_F_BELOW_TIER     0x008u
#define PSYVID_F_AUDIO_CLOCK    0x010u  /* the movie clock followed another clock */
#define PSYVID_F_TS_MISMATCH    0x020u  /* the decoder's time is not the index's */
#define PSYVID_F_HASH_MISMATCH  0x040u  /* the frame's hash is not the index's */
#define PSYVID_F_NOT_SHOWN      0x080u  /* the flip was skipped or canceled    */

/* Kinds under PSYRT_SRC_VIDEO. */
#define PSYVID_EV_FRAME   1u
#define PSYVID_EV_DROP    2u
#define PSYVID_EV_OPEN    3u
#define PSYVID_EV_PLAY    4u
#define PSYVID_EV_PAUSE   5u
#define PSYVID_EV_SEEK    6u
#define PSYVID_EV_LOOP    7u
#define PSYVID_EV_END     8u
#define PSYVID_EV_CLOCK   9u
#define PSYVID_EV_DECODE 10u

#define PSYVID_EV_DECISION_OF(w) ((unsigned)(w) & 0x3u)
#define PSYVID_EV_WHY_OF(w)      (((unsigned)(w) >> 2) & 0xFu)
#define PSYVID_EV_TIER_OF(w)     (((unsigned)(w) >> 6) & 0x7u)
#define PSYVID_EV_PATH_OF(w)     (((unsigned)(w) >> 9) & 0x3u)

typedef struct psyvid_stim_desc {
    psygfx_align place, anchor;      /* 0 = CENTER                            */
    float x, y, w, h, ori;           /* w, h 0 = the frame's texels           */
    float opacity;                   /* 0 = 1                                 */
    bool  linear;                    /* filter when scaled; default nearest   */
    const psygfx_group* group;
} psyvid_stim_desc;

/* --- frame sequence writer ---------------------------------------------------- */

typedef struct psyvid_seq_desc {
    const char* path;
    int32_t  w, h;
    int32_t  format;                 /* PSYVID_FMT_*                          */
    int32_t  compression;            /* PSYVID_SEQ_RAW, or _QOI for RGBA8     */
    int32_t  fps_num, fps_den;       /* den 0 = 1                             */
    uint8_t  matrix, range, transfer, primaries, siting;  /* YUV: required    */
    uint8_t  reserved_[3];
    int64_t  max_frames;             /* > 0: the index is reserved at create
                                      * and no frame allocates; 0 = grow     */
} psyvid_seq_desc;

typedef struct psyvid_seq_meta {     /* per frame, for capture; NULL = zeros  */
    int64_t t_ns, trial, event, camera_ns;
} psyvid_seq_meta;

/* Writer handle. Caller-allocated and zeroed; private fields. */
typedef struct psyvid_seq {
    void*    fh;
    psyvid_seq_desc d;
    int64_t  n, cap, offset;
    uint8_t* index;
    uint8_t* scratch;
    size_t   scratch_bytes;
    int      open;
    char     error[256];
} psyvid_seq;

/* --- the movie (private fields) ----------------------------------------------- */

#ifndef PSYVID_MAX_SLOTS
#define PSYVID_MAX_SLOTS 34
#endif
#define PSYVID__QCAP     64          /* power of two above PSYVID_MAX_SLOTS   */
#define PSYVID__MAX_PEND 16
#define PSYVID__RESULTS  8

typedef struct psyvid__slot {
    int64_t  g;              /* global frame: cycle * frames + index           */
    int64_t  key;            /* keyframe used to reach it after a seek, or -1  */
    int64_t  discarded;      /* frames decoded and discarded before it         */
    uint32_t epoch;
    uint32_t flags;          /* PSYVID_F_TS_MISMATCH, _HASH_MISMATCH          */
    uint8_t* data;
} psyvid__slot;

typedef struct psyvid__ctl {
    int64_t  id, t, target;
    int32_t  op, active;
} psyvid__ctl;

typedef struct psyvid__result {
    int64_t     id;
    psyrt_event ev;
} psyvid__result;

typedef struct psyvid_movie {
    int                 open;
    int                 state;
    psygfx_gfx*         gfx;
    psyscr_screen*      screen;
    psygfx_tex          tex;
    psyvid_info         info;
    psyvid_desc         d;
    const psyvid_decoder* dec;
    void*               dec_ctx;
    void*               be_mem;          /* a built-in backend's state        */
    uint64_t*           hashes;          /* the index's, or NULL              */
    unsigned char*      mem_raw;         /* slots, as allocated               */
    unsigned char*      yuv;             /* the decode thread's YUV planes    */
    int16_t*            yuv_rows;        /* the conversion's row scratch      */
    int32_t             yuv_cm[8];       /* fixed-point conversion            */
    int64_t             timescale;
    uint32_t            dec_caps;
    int32_t             n_slots;
    psyvid__slot        slots[PSYVID_MAX_SLOTS];
    /* queues: ready (decode -> frame) and free (frame -> decode) */
    uint32_t            ready_q[PSYVID__QCAP];
    uint32_t            free_q[PSYVID__QCAP];
    uint32_t            ready_head, ready_tail, free_head, free_tail;
    uint32_t            idle, dec_err;
    int64_t             want_g;
    /* decode thread */
    uint32_t            dt_epoch;
    int64_t             dt_next, dt_pos, dt_key, dt_discarded;
    int                 dt_eos;
    char                dt_error[256];
    /* frame thread */
    uint32_t            ft_epoch;
    int64_t             last_display, last_vblank;
    int                 has_display;
    int64_t             anchor_rt, anchor_mt;
    int                 running;
    int64_t             cycle;
    int64_t             shown_g;             /* -1 = nothing yet               */
    uint32_t            shows;
    int64_t             late_from;
    int64_t             nom_m, nom_vb, nom_d;
    int                 nom_set;
    int                 just;                /* SEEK, LOOP, MANUAL for the next SHOWN */
    int64_t             seek_g, seek_id, seek_req_onset;
    int                 seek_resume_kind;    /* 0 ASAP, 1 at t, 2 stay paused  */
    int64_t             seek_resume_t;
    int64_t             manual_g;
    int                 manual_req;
    int                 seek_posted;
    psyvid__ctl         ctl;                 /* a pending play or pause        */
    int64_t             next_id;
    psyvid__result      results[PSYVID__RESULTS];
    int                 n_results;
    psyvid_record       pend[PSYVID__MAX_PEND];
    int                 n_pend;
    psyvid_record       last;
    int                 has_last;
    int64_t             lead_ns_last, period_last;
    int64_t             follow_f0;           /* the other clock's unit at mt 0 */
    int64_t             follow_last_m;
    uint32_t            follow_rate;
    uint32_t            anchor_epoch, follow_epoch;
    int                 following;
    uint64_t            upload_ns_last, upload_ns_max;
    int                 inline_mode;
    char                inline_why[128];     /* why the decode thread did not start */
    char                error[512];
#if !defined(PSYRT_NO_THREADS)
    psyrt_pump          pump;
#endif
} psyvid_movie;

/* --- API ------------------------------------------------------------------- */

PSYVID_API const char* psyvid_version(void);
PSYVID_API const char* psyvid_strerror(int code);

/* The rule and the times, as pure functions. num/den is the rate; m a
 * movie time; lead_ns = psyvid_lead_ns(lead, period). */
PSYVID_API int64_t psyvid_frame_time(int32_t num, int32_t den, int64_t i);
PSYVID_API int64_t psyvid_due(int32_t num, int32_t den, int64_t m, int64_t lead_ns);
PSYVID_API int64_t psyvid_lead_ns(double lead, int64_t period);

/* Reads the description (a frame sequence's header, or the index of a
 * codec file) without opening a movie. */
PSYVID_API bool        psyvid_probe(const psyvid_desc* d, psyvid_info* out, char* err, size_t cap);

/* Opens the movie on a gfx (NULL: decode and schedule, no texture). The
 * handle must be zeroed or closed. False with psyvid_error() set. */
PSYVID_API bool        psyvid_open(psyvid_movie* mv, psygfx_gfx* g, const psyvid_desc* d);
PSYVID_API void        psyvid_close(psyvid_movie* mv);
PSYVID_API const char* psyvid_error(const psyvid_movie* mv);
PSYVID_API bool        psyvid_is_open(const psyvid_movie* mv);
PSYVID_API void        psyvid_get_info(const psyvid_movie* mv, psyvid_info* out);
PSYVID_API int         psyvid_describe(const psyvid_movie* mv, char* buf, size_t cap);
PSYVID_API psygfx_stim psyvid_stim(const psyvid_movie* mv, const psyvid_stim_desc* d);
PSYVID_API psygfx_tex  psyvid_texture(const psyvid_movie* mv);

/* Controls. Each returns an id > 0, or a negative code. */
PSYVID_API int64_t psyvid_play_at(psyvid_movie* mv, int64_t t);
PSYVID_API int64_t psyvid_pause_at(psyvid_movie* mv, int64_t t);
PSYVID_API int64_t psyvid_seek(psyvid_movie* mv, int64_t movie_t, int64_t resume_at);
PSYVID_API int64_t psyvid_seek_frame(psyvid_movie* mv, int64_t frame, int64_t resume_at);
PSYVID_API int     psyvid_show(psyvid_movie* mv, int64_t frame);
PSYVID_API int     psyvid_result(const psyvid_movie* mv, int64_t id, psyrt_event* out);

/* The frame: after psyscr_begin(), before psygfx_begin(). */
PSYVID_API int     psyvid_update(psyvid_movie* mv, const psyscr_frame* f);
PSYVID_API void    psyvid_flip_done(psyvid_movie* mv, const psyscr_record* r);
PSYVID_API int     psyvid_last(const psyvid_movie* mv, psyvid_record* out);
PSYVID_API int64_t psyvid_movie_time(const psyvid_movie* mv, int64_t t);

/* Following another clock: before psyvid_update(), every frame. unit_at(t)
 * is the clock's unit (a stream frame) at RT time t; rate its units per
 * second. Re-anchors the movie clock at f->onset. */
typedef int64_t (*psyvid_unit_at_fn)(void* ctx, int64_t t);
PSYVID_API int psyvid_follow(psyvid_movie* mv, const psyscr_frame* f,
                             psyvid_unit_at_fn unit_at, void* ctx, uint32_t rate);

/* Frame sequences. */
PSYVID_API bool psyvid_seq_create(psyvid_seq* w, const psyvid_seq_desc* d);
PSYVID_API int  psyvid_seq_write(psyvid_seq* w, const void* const planes[3],
                                 const int32_t strides[3], const psyvid_seq_meta* meta);
PSYVID_API int  psyvid_seq_close(psyvid_seq* w);
PSYVID_API const char* psyvid_seq_error(const psyvid_seq* w);

/* The index of an MPEG-1 file (pl_mpeg): decodes every frame, checks the
 * canonical form, writes index_path (NULL = media_path + ".psyvi").
 * color: matrix, range, transfer, primaries, siting; 0 = BT.601, LIMITED,
 * BT1886, BT709, CENTER. Returns frames, or a negative code with err. */
typedef struct psyvid_index_desc {
    uint8_t matrix, range, transfer, primaries, siting;
    uint8_t reserved_[3];
    const char* note;                /* the command that made the file        */
} psyvid_index_desc;
PSYVID_API int64_t psyvid_index_make(const char* media_path, const char* index_path,
                                     const psyvid_index_desc* d, char* err, size_t cap);

/* Small pieces, public for tools and tests. */
PSYVID_API uint64_t psyvid_xxh64(const void* data, size_t n, uint64_t seed);
PSYVID_API int64_t  psyvid_qoi_encode(const uint8_t* rgba, int32_t w, int32_t h, int32_t channels,
                                      uint8_t* out, size_t cap);
PSYVID_API int      psyvid_qoi_decode(const uint8_t* in, size_t n, uint8_t* rgba, int32_t w, int32_t h);
PSYVID_API size_t   psyvid_qoi_max_bytes(int32_t w, int32_t h, int32_t channels);
/* rgba and stride must be 4-byte aligned, and rows 2-byte aligned (at least
 * psyvid_yuv_rows_bytes(w) bytes): the conversion stores whole pixels and
 * returns PSYVID_ERR_ARG otherwise. A uint8_t array has no such alignment;
 * use a uint32_t array or malloc. */
PSYVID_API int      psyvid_yuv_to_rgba(const psyvid_planes* p, int32_t format, int32_t w, int32_t h,
                                       int32_t matrix, int32_t range, int32_t siting, int32_t chroma,
                                       uint8_t* rgba, int32_t stride, int16_t* rows);
PSYVID_API size_t   psyvid_yuv_rows_bytes(int32_t w);
PSYVID_API uint64_t psyvid_heap_calls(void);

PSYVID_API const psyscr_param* psyvid_params(int* n);

#ifdef __cplusplus
}
#endif

#endif /* PSY_VIDEO_H_INCLUDED */

/* --- with psy_audio.h included first ----------------------------------------- */
#if defined(PSY_AUDIO_H_INCLUDED) && !defined(PSY_VIDEO_AUDIO_INCLUDED)
#define PSY_VIDEO_AUDIO_INCLUDED
#ifdef __cplusplus
extern "C" {
#endif
/* psyvid_follow() on the audio fit: movie time = the soundtrack's stream
 * frame at the output minus its frame at movie time 0. */
PSYVID_API int psyvid_follow_audio(psyvid_movie* mv, psyau_audio* au, const psyscr_frame* f);
#ifdef __cplusplus
}
#endif
#endif

/* ======================================================================= *
 *                             IMPLEMENTATION                              *
 * ======================================================================= */
#ifdef PSY_VIDEO_IMPLEMENTATION
#ifndef PSY_VIDEO_IMPLEMENTATION_GUARD
#define PSY_VIDEO_IMPLEMENTATION_GUARD

#ifndef PSY_GFX_IMPLEMENTATION_GUARD
    #define PSY_GFX_IMPLEMENTATION
    #include "psy_gfx.h"
#endif
#ifndef PSY_TIMELINE_IMPLEMENTATION_GUARD
    #define PSY_TIMELINE_IMPLEMENTATION
    #include "psy_timeline.h"
#endif

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>

#ifndef PSYVID_MALLOC
#define PSYVID_MALLOC(n)     malloc(n)
#define PSYVID_REALLOC(p, n) realloc((p), (n))
#define PSYVID_FREE(p)       free(p)
#endif
#ifndef PSYVID__NOW
#define PSYVID__NOW() ((int64_t)psyrt_now_ns())
#endif

/* --- atomics ---------------------------------------------------------------
 * The queues are single-producer single-consumer: an acquire load of the
 * other side's index and a release store of one's own. The idle flag is a
 * Dekker pair, so both sides use a full-barrier exchange. */
#if defined(_MSC_VER)
    #include <intrin.h>
static uint32_t psyvid__ld32(const uint32_t* p) { uint32_t v = *(const volatile uint32_t*)p; _ReadWriteBarrier(); return v; }
static void psyvid__st32(uint32_t* p, uint32_t v) { _ReadWriteBarrier(); *(volatile uint32_t*)p = v; }
static int64_t psyvid__ld64(const int64_t* p) { return _InterlockedCompareExchange64((volatile __int64*)(uintptr_t)p, 0, 0); }
static void psyvid__st64(int64_t* p, int64_t v) { (void)_InterlockedExchange64((volatile __int64*)p, v); }
static uint32_t psyvid__xchg32(uint32_t* p, uint32_t v) { return (uint32_t)_InterlockedExchange((volatile long*)p, (long)v); }
static uint64_t psyvid__inc64(uint64_t* p) { return (uint64_t)_InterlockedIncrement64((volatile __int64*)p); }
#elif defined(__GNUC__) || defined(__clang__)
static uint32_t psyvid__ld32(const uint32_t* p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
static void psyvid__st32(uint32_t* p, uint32_t v) { __atomic_store_n(p, v, __ATOMIC_RELEASE); }
static int64_t psyvid__ld64(const int64_t* p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
static void psyvid__st64(int64_t* p, int64_t v) { __atomic_store_n(p, v, __ATOMIC_RELEASE); }
static uint32_t psyvid__xchg32(uint32_t* p, uint32_t v) { return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST); }
static uint64_t psyvid__inc64(uint64_t* p) { return __atomic_add_fetch(p, 1, __ATOMIC_RELAXED); }
#else
#error "psy_video: needs atomic loads and stores (GCC, Clang or MSVC builtins)"
#endif

/* --- heap ----------------------------------------------------------------- */

static uint64_t psyvid__heap_calls_n;

static void* psyvid__malloc(size_t n) { psyvid__inc64(&psyvid__heap_calls_n); return PSYVID_MALLOC(n); }
static void* psyvid__realloc(void* p, size_t n) { psyvid__inc64(&psyvid__heap_calls_n); return PSYVID_REALLOC(p, n); }
static void  psyvid__free(void* p) { if (p) PSYVID_FREE(p); }

PSYVID_API uint64_t psyvid_heap_calls(void) { return (uint64_t)psyvid__ld64((const int64_t*)(const void*)&psyvid__heap_calls_n); }

/* --- pl_mpeg ---------------------------------------------------------------- */

#ifndef PSYVID_NO_PL_MPEG
    #define PLM_MALLOC(sz)     psyvid__malloc(sz)
    #define PLM_FREE(p)        psyvid__free(p)
    #define PLM_REALLOC(p, sz) psyvid__realloc((p), (sz))
    #if defined(_MSC_VER)
        #pragma warning(push)
        #pragma warning(disable: 4244 4267 4305 4100 4189)
    #elif defined(__clang__)
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wshorten-64-to-32"
        #pragma clang diagnostic ignored "-Wunknown-warning-option"
    #endif
    #if !defined(PSYVID_PL_MPEG_EXTERNAL)
        #define PL_MPEG_IMPLEMENTATION
    #endif
    #include "pl_mpeg.h"
    #if defined(_MSC_VER)
        #pragma warning(pop)
    #elif defined(__clang__)
        #pragma clang diagnostic pop
    #endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define PSYVID__NS PSYVID_NS_PER_S

enum { PSYVID__STOPPED = 0, PSYVID__PLAYING, PSYVID__PAUSED, PSYVID__SEEKING, PSYVID__MANUAL,
       PSYVID__ENDED, PSYVID__FAILED };
enum { PSYVID__OP_PLAY = 1, PSYVID__OP_PAUSE = 2 };
enum { PSYVID__MSG_WAKE = 1, PSYVID__MSG_SEEK = 2 };

typedef struct psyvid__msg { uint32_t op, epoch; int64_t g; } psyvid__msg;

static void psyvid__fmt(char* buf, size_t cap, const char* fmt, ...) {
    va_list ap;
    if (!buf || cap == 0) return;
    va_start(ap, fmt);
    vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
}

PSYVID_API const char* psyvid_version(void) { return PSYVID_VERSION_STRING; }

PSYVID_API const char* psyvid_strerror(int code) {
    switch (code) {
    case PSYVID_OK: return "ok";
    case PSYVID_ENDED: return "ended";
    case PSYVID_PENDING: return "pending";
    case PSYVID_ERR_ARG: return "bad argument";
    case PSYVID_ERR_CLOSED: return "not open";
    case PSYVID_ERR_FORMAT: return "not the canonical form";
    case PSYVID_ERR_ORDER: return "call out of turn";
    case PSYVID_ERR_FULL: return "full";
    case PSYVID_ERR_DECODER: return "decoder failed";
    case PSYVID_ERR_REFUSED: return "refused";
    case PSYVID_ERR_LOST: return "GL context lost";
    case PSYVID_ERR_IO: return "I/O error";
    case PSYVID_ERR_NOT_FOUND: return "not found";
    case PSYVID_ERR_NOT_IMPLEMENTED: return "not implemented in v0.1";
    default: return code >= 0 ? "ok" : "unknown error";
    }
}

/* --- time ----------------------------------------------------------------- */

static int64_t psyvid__floordiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}

/* The open check keeps num * den * 1e9 below 2^63, so r * den * 1e9 fits. */
PSYVID_API int64_t psyvid_frame_time(int32_t num, int32_t den, int64_t i) {
    int64_t q, r;
    if (num <= 0 || den <= 0) return 0;
    if (i < 0) return -psyvid_frame_time(num, den, -i);
    q = i / num;
    r = i % num;
    return q * (int64_t)den * PSYVID__NS + ((r * (int64_t)den * PSYVID__NS) + num - 1) / num;
}

PSYVID_API int64_t psyvid_due(int32_t num, int32_t den, int64_t m, int64_t lead_ns) {
    int64_t x = m + lead_ns, d, a, b;
    if (num <= 0 || den <= 0) return 0;
    d = (int64_t)den * PSYVID__NS;
    a = psyvid__floordiv(x, d);
    b = x - a * d;
    return a * num + (b * num) / d;
}

PSYVID_API int64_t psyvid_lead_ns(double lead, int64_t period) {
    double l = lead == 0.0 ? 0.5 : lead < 0.0 ? 0.0 : lead;
    if (period <= 0) return 0;
    return (int64_t)(l * (double)period);
}

/* --- XXH64 ------------------------------------------------------------------
 * Its own copy rather than a dependency: the frame hash is the index's
 * check, and the format's reference vectors are in the test. */
#define PSYVID__P1 UINT64_C(0x9E3779B185EBCA87)
#define PSYVID__P2 UINT64_C(0xC2B2AE3D27D4EB4F)
#define PSYVID__P3 UINT64_C(0x165667B19E3779F9)
#define PSYVID__P4 UINT64_C(0x85EBCA77C2B2AE63)
#define PSYVID__P5 UINT64_C(0x27D4EB2F165667C5)

typedef struct psyvid__xxh {
    uint64_t v[4];
    uint64_t total;
    unsigned char mem[32];
    uint32_t memsize;
    uint64_t seed;
} psyvid__xxh;

static uint64_t psyvid__rotl(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }
static uint64_t psyvid__rd64(const unsigned char* p) {
    return (uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24) |
           ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) | ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
}
static uint32_t psyvid__rd32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t psyvid__xround(uint64_t acc, uint64_t in) {
    acc += in * PSYVID__P2;
    acc = psyvid__rotl(acc, 31);
    return acc * PSYVID__P1;
}
static uint64_t psyvid__xmerge(uint64_t acc, uint64_t v) {
    acc ^= psyvid__xround(0, v);
    return acc * PSYVID__P1 + PSYVID__P4;
}
static void psyvid__xxh_init(psyvid__xxh* s, uint64_t seed) {
    memset(s, 0, sizeof *s);
    s->seed = seed;
    s->v[0] = seed + PSYVID__P1 + PSYVID__P2;
    s->v[1] = seed + PSYVID__P2;
    s->v[2] = seed;
    s->v[3] = seed - PSYVID__P1;
}
static void psyvid__xxh_update(psyvid__xxh* s, const void* data, size_t n) {
    const unsigned char* p = (const unsigned char*)data;
    const unsigned char* end = p + n;
    s->total += n;
    if (s->memsize + n < 32) {
        if (n) memcpy(s->mem + s->memsize, p, n);
        s->memsize += (uint32_t)n;
        return;
    }
    if (s->memsize) {
        memcpy(s->mem + s->memsize, p, 32 - s->memsize);
        s->v[0] = psyvid__xround(s->v[0], psyvid__rd64(s->mem));
        s->v[1] = psyvid__xround(s->v[1], psyvid__rd64(s->mem + 8));
        s->v[2] = psyvid__xround(s->v[2], psyvid__rd64(s->mem + 16));
        s->v[3] = psyvid__xround(s->v[3], psyvid__rd64(s->mem + 24));
        p += 32 - s->memsize;
        s->memsize = 0;
    }
    {
        uint64_t v0 = s->v[0], v1 = s->v[1], v2 = s->v[2], v3 = s->v[3];
        while (p + 32 <= end) {
            v0 = psyvid__xround(v0, psyvid__rd64(p));
            v1 = psyvid__xround(v1, psyvid__rd64(p + 8));
            v2 = psyvid__xround(v2, psyvid__rd64(p + 16));
            v3 = psyvid__xround(v3, psyvid__rd64(p + 24));
            p += 32;
        }
        s->v[0] = v0; s->v[1] = v1; s->v[2] = v2; s->v[3] = v3;
    }
    if (p < end) {
        memcpy(s->mem, p, (size_t)(end - p));
        s->memsize = (uint32_t)(end - p);
    }
}
static uint64_t psyvid__xxh_digest(const psyvid__xxh* s) {
    uint64_t h;
    const unsigned char* p = s->mem;
    const unsigned char* end = s->mem + s->memsize;
    if (s->total >= 32) {
        h = psyvid__rotl(s->v[0], 1) + psyvid__rotl(s->v[1], 7) + psyvid__rotl(s->v[2], 12) + psyvid__rotl(s->v[3], 18);
        h = psyvid__xmerge(h, s->v[0]);
        h = psyvid__xmerge(h, s->v[1]);
        h = psyvid__xmerge(h, s->v[2]);
        h = psyvid__xmerge(h, s->v[3]);
    } else {
        h = s->seed + PSYVID__P5;
    }
    h += s->total;
    while (p + 8 <= end) {
        h ^= psyvid__xround(0, psyvid__rd64(p));
        h = psyvid__rotl(h, 27) * PSYVID__P1 + PSYVID__P4;
        p += 8;
    }
    if (p + 4 <= end) {
        h ^= (uint64_t)psyvid__rd32(p) * PSYVID__P1;
        h = psyvid__rotl(h, 23) * PSYVID__P2 + PSYVID__P3;
        p += 4;
    }
    while (p < end) {
        h ^= (uint64_t)(*p) * PSYVID__P5;
        h = psyvid__rotl(h, 11) * PSYVID__P1;
        p++;
    }
    h ^= h >> 33; h *= PSYVID__P2;
    h ^= h >> 29; h *= PSYVID__P3;
    h ^= h >> 32;
    return h;
}

PSYVID_API uint64_t psyvid_xxh64(const void* data, size_t n, uint64_t seed) {
    psyvid__xxh s;
    psyvid__xxh_init(&s, seed);
    psyvid__xxh_update(&s, data, n);
    return psyvid__xxh_digest(&s);
}

/* --- formats ----------------------------------------------------------------- */

static int psyvid__is_yuv(int32_t f) { return f == PSYVID_FMT_I420 || f == PSYVID_FMT_NV12; }
static int psyvid__is_rgb(int32_t f) { return f >= PSYVID_FMT_R8 && f <= PSYVID_FMT_R16; }

/* Bytes per texel as psy_gfx.h takes the format. */
static int32_t psyvid__bpt(int32_t f) {
    switch (f) {
    case PSYVID_FMT_R8: return 1;
    case PSYVID_FMT_RG8: return 2;
    case PSYVID_FMT_RGBA8: return 4;
    case PSYVID_FMT_R16F: return 4;
    case PSYVID_FMT_RGBA16F: return 16;
    case PSYVID_FMT_R32F: return 4;
    case PSYVID_FMT_RGBA32F: return 16;
    case PSYVID_FMT_R16: return 2;
    default: return 0;
    }
}

static const char* psyvid__fmt_name(int32_t f) {
    switch (f) {
    case PSYVID_FMT_R8: return "R8";
    case PSYVID_FMT_RG8: return "RG8";
    case PSYVID_FMT_RGBA8: return "RGBA8";
    case PSYVID_FMT_R16F: return "R16F";
    case PSYVID_FMT_RGBA16F: return "RGBA16F";
    case PSYVID_FMT_R32F: return "R32F";
    case PSYVID_FMT_RGBA32F: return "RGBA32F";
    case PSYVID_FMT_R16: return "R16";
    case PSYVID_FMT_I420: return "I420";
    case PSYVID_FMT_NV12: return "NV12";
    case PSYVID_FMT_P010: return "P010";
    default: return "?";
    }
}

/* The planes of a frame of format f at w x h, tight and contiguous. */
static size_t psyvid__planes_layout(int32_t f, int32_t w, int32_t h, uint8_t* base, psyvid_planes* p) {
    size_t total = 0;
    int32_t cw = (w + 1) / 2, ch = (h + 1) / 2;
    psyvid_planes q;
    memset(&q, 0, sizeof q);
    if (psyvid__is_rgb(f)) {
        q.w[0] = w; q.h[0] = h; q.stride[0] = w * psyvid__bpt(f);
        total = (size_t)q.stride[0] * (size_t)h;
    } else if (f == PSYVID_FMT_I420) {
        q.w[0] = w; q.h[0] = h; q.stride[0] = w;
        q.w[1] = q.w[2] = cw; q.h[1] = q.h[2] = ch; q.stride[1] = q.stride[2] = cw;
        total = (size_t)w * (size_t)h + 2 * (size_t)cw * (size_t)ch;
    } else if (f == PSYVID_FMT_NV12) {
        q.w[0] = w; q.h[0] = h; q.stride[0] = w;
        q.w[1] = cw; q.h[1] = ch; q.stride[1] = cw * 2;
        total = (size_t)w * (size_t)h + (size_t)cw * 2 * (size_t)ch;
    }
    if (base) {
        q.data[0] = base;
        if (f == PSYVID_FMT_I420) {
            q.data[1] = base + (size_t)w * (size_t)h;
            q.data[2] = q.data[1] + (size_t)cw * (size_t)ch;
        } else if (f == PSYVID_FMT_NV12) {
            q.data[1] = base + (size_t)w * (size_t)h;
        }
    }
    if (p) *p = q;
    return total;
}

static int psyvid__n_planes(int32_t f) { return f == PSYVID_FMT_I420 ? 3 : f == PSYVID_FMT_NV12 ? 2 : 1; }

/* Bytes of visible texels in plane k (the row length hashed and stored). */
static size_t psyvid__row_bytes(int32_t f, const psyvid_planes* p, int k) {
    if (psyvid__is_rgb(f)) return (size_t)p->w[0] * (size_t)psyvid__bpt(f);
    if (f == PSYVID_FMT_NV12 && k == 1) return (size_t)p->w[1] * 2;
    return (size_t)p->w[k];
}

static uint64_t psyvid__hash_planes(int32_t f, const psyvid_planes* p) {
    psyvid__xxh s;
    int k, y;
    psyvid__xxh_init(&s, 0);
    for (k = 0; k < psyvid__n_planes(f); k++) {
        size_t rb = psyvid__row_bytes(f, p, k);
        for (y = 0; y < p->h[k]; y++)
            psyvid__xxh_update(&s, p->data[k] + (size_t)y * (size_t)p->stride[k], rb);
    }
    return psyvid__xxh_digest(&s);
}

/* --- QOI --------------------------------------------------------------------
 * The header's own codec into caller-owned memory: the reference decoder
 * allocates its output per call, which the decode thread must not. The
 * encoder makes the reference encoder's bytes (the test compares). */
#define PSYVID__QOI_INDEX   0x00
#define PSYVID__QOI_DIFF    0x40
#define PSYVID__QOI_LUMA    0x80
#define PSYVID__QOI_RUN     0xc0
#define PSYVID__QOI_RGB     0xfe
#define PSYVID__QOI_RGBA    0xff
#define PSYVID__QOI_MASK    0xc0
#define PSYVID__QOI_HASH(c) (((unsigned)(c)[0] * 3u + (unsigned)(c)[1] * 5u + (unsigned)(c)[2] * 7u + (unsigned)(c)[3] * 11u) % 64u)

PSYVID_API size_t psyvid_qoi_max_bytes(int32_t w, int32_t h, int32_t channels) {
    if (w <= 0 || h <= 0 || (channels != 3 && channels != 4)) return 0;
    return (size_t)w * (size_t)h * (size_t)(channels + 1) + 14 + 8;
}

static void psyvid__wbe32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

PSYVID_API int64_t psyvid_qoi_encode(const uint8_t* px, int32_t w, int32_t h, int32_t channels,
                                     uint8_t* out, size_t cap) {
    uint8_t index[64][4];
    uint8_t prev[4] = { 0, 0, 0, 255 }, cur[4];
    size_t p = 0, i, n, last;
    int run = 0;
    static const uint8_t pad[8] = { 0, 0, 0, 0, 0, 0, 0, 1 };
    if (!px || !out || cap < psyvid_qoi_max_bytes(w, h, channels)) return PSYVID_ERR_ARG;
    memset(index, 0, sizeof index);
    memcpy(out, "qoif", 4);
    psyvid__wbe32(out + 4, (uint32_t)w);
    psyvid__wbe32(out + 8, (uint32_t)h);
    out[12] = (uint8_t)channels;
    out[13] = 1;   /* all channels linear: device values */
    p = 14;
    n = (size_t)w * (size_t)h;
    last = n - 1;
    for (i = 0; i < n; i++) {
        const uint8_t* s = px + i * (size_t)channels;
        cur[0] = s[0]; cur[1] = s[1]; cur[2] = s[2];
        cur[3] = channels == 4 ? s[3] : prev[3];
        if (memcmp(cur, prev, 4) == 0) {
            run++;
            if (run == 62 || i == last) {
                out[p++] = (uint8_t)(PSYVID__QOI_RUN | (run - 1));
                run = 0;
            }
        } else {
            unsigned hpos;
            if (run > 0) {
                out[p++] = (uint8_t)(PSYVID__QOI_RUN | (run - 1));
                run = 0;
            }
            hpos = PSYVID__QOI_HASH(cur);
            if (memcmp(index[hpos], cur, 4) == 0) {
                out[p++] = (uint8_t)(PSYVID__QOI_INDEX | hpos);
            } else {
                memcpy(index[hpos], cur, 4);
                if (cur[3] == prev[3]) {
                    int vr = (int)(signed char)(uint8_t)(cur[0] - prev[0]);
                    int vg = (int)(signed char)(uint8_t)(cur[1] - prev[1]);
                    int vb = (int)(signed char)(uint8_t)(cur[2] - prev[2]);
                    int vg_r = vr - vg, vg_b = vb - vg;
                    if (vr > -3 && vr < 2 && vg > -3 && vg < 2 && vb > -3 && vb < 2) {
                        out[p++] = (uint8_t)(PSYVID__QOI_DIFF | (vr + 2) << 4 | (vg + 2) << 2 | (vb + 2));
                    } else if (vg_r > -9 && vg_r < 8 && vg > -33 && vg < 32 && vg_b > -9 && vg_b < 8) {
                        out[p++] = (uint8_t)(PSYVID__QOI_LUMA | (vg + 32));
                        out[p++] = (uint8_t)((vg_r + 8) << 4 | (vg_b + 8));
                    } else {
                        out[p++] = PSYVID__QOI_RGB;
                        out[p++] = cur[0]; out[p++] = cur[1]; out[p++] = cur[2];
                    }
                } else {
                    out[p++] = PSYVID__QOI_RGBA;
                    out[p++] = cur[0]; out[p++] = cur[1]; out[p++] = cur[2]; out[p++] = cur[3];
                }
            }
        }
        memcpy(prev, cur, 4);
    }
    memcpy(out + p, pad, 8);
    p += 8;
    return (int64_t)p;
}

/* Decodes a 4-channel image of exactly w x h into rgba (tight). Every read
 * is bounds-checked: a damaged file fails, it does not overrun. */
PSYVID_API int psyvid_qoi_decode(const uint8_t* in, size_t n, uint8_t* rgba, int32_t w, int32_t h) {
    uint8_t index[64][4];
    uint8_t px[4] = { 0, 0, 0, 255 };
    size_t p = 14, chunks, i, total;
    int run = 0;
    if (!in || !rgba || n < 14 + 8 || memcmp(in, "qoif", 4) != 0) return PSYVID_ERR_FORMAT;
    if (((uint32_t)in[4] << 24 | (uint32_t)in[5] << 16 | (uint32_t)in[6] << 8 | in[7]) != (uint32_t)w ||
        ((uint32_t)in[8] << 24 | (uint32_t)in[9] << 16 | (uint32_t)in[10] << 8 | in[11]) != (uint32_t)h)
        return PSYVID_ERR_FORMAT;
    if (in[12] != 3 && in[12] != 4) return PSYVID_ERR_FORMAT;
    memset(index, 0, sizeof index);
    chunks = n - 8;
    total = (size_t)w * (size_t)h;
    for (i = 0; i < total; i++) {
        if (run > 0) {
            run--;
        } else if (p < chunks) {
            int b1 = in[p++];
            if (b1 == PSYVID__QOI_RGB) {
                if (p + 3 > chunks) return PSYVID_ERR_FORMAT;
                px[0] = in[p]; px[1] = in[p + 1]; px[2] = in[p + 2]; p += 3;
            } else if (b1 == PSYVID__QOI_RGBA) {
                if (p + 4 > chunks) return PSYVID_ERR_FORMAT;
                px[0] = in[p]; px[1] = in[p + 1]; px[2] = in[p + 2]; px[3] = in[p + 3]; p += 4;
            } else if ((b1 & PSYVID__QOI_MASK) == PSYVID__QOI_INDEX) {
                memcpy(px, index[b1], 4);
            } else if ((b1 & PSYVID__QOI_MASK) == PSYVID__QOI_DIFF) {
                px[0] = (uint8_t)(px[0] + ((b1 >> 4) & 0x03) - 2);
                px[1] = (uint8_t)(px[1] + ((b1 >> 2) & 0x03) - 2);
                px[2] = (uint8_t)(px[2] + (b1 & 0x03) - 2);
            } else if ((b1 & PSYVID__QOI_MASK) == PSYVID__QOI_LUMA) {
                int b2, vg;
                if (p + 1 > chunks) return PSYVID_ERR_FORMAT;
                b2 = in[p++];
                vg = (b1 & 0x3f) - 32;
                px[0] = (uint8_t)(px[0] + vg - 8 + ((b2 >> 4) & 0x0f));
                px[1] = (uint8_t)(px[1] + vg);
                px[2] = (uint8_t)(px[2] + vg - 8 + (b2 & 0x0f));
            } else {
                run = b1 & 0x3f;
            }
            memcpy(index[PSYVID__QOI_HASH(px)], px, 4);
        } else {
            return PSYVID_ERR_FORMAT;   /* data ran out before the pixels did */
        }
        memcpy(rgba + i * 4, px, 4);
    }
    return PSYVID_OK;
}

/* --- YUV to RGBA8 -------------------------------------------------------------
 * 4:2:0 chroma is brought to 4:4:4 with weights in sixteenths (siting), then
 * the matrix in 16.16 fixed point. Within 1 code of the same weights and
 * matrix in double (the test). */
/* Two int16 chroma rows (one edge texel each side), then per-pixel U and V
 * in int32 for the matrix pass. */
PSYVID_API size_t psyvid_yuv_rows_bytes(int32_t w) {
    return (size_t)((w + 1) / 2 + 2) * 2 * sizeof(int16_t) + (size_t)(w + 2) * 2 * sizeof(int32_t) + 16;
}

static void psyvid__yuv_coef(int32_t matrix, int32_t range, int32_t* cm) {
    double kr = 0.299, kb = 0.114, kg, sy, sc, oy;
    if (matrix == PSYVID_MATRIX_BT709) { kr = 0.2126; kb = 0.0722; }
    else if (matrix == PSYVID_MATRIX_BT2020) { kr = 0.2627; kb = 0.0593; }
    kg = 1.0 - kr - kb;
    if (range == PSYVID_RANGE_FULL) { sy = 1.0; sc = 255.0 / 255.0; oy = 0; }
    else { sy = 255.0 / 219.0; sc = 255.0 / 224.0; oy = 16; }
    /* R = sy (Y - oy) + rv (Cr - 128); G = sy (Y - oy) - gu (Cb - 128) - gv (Cr - 128);
     * B = sy (Y - oy) + bu (Cb - 128). Chroma enters in sixteenths. */
    cm[0] = (int32_t)floor(sy * 65536.0 + 0.5);
    cm[1] = (int32_t)floor(sc * 2.0 * (1.0 - kr) * 65536.0 / 16.0 + 0.5);
    cm[2] = (int32_t)floor(sc * 2.0 * kb * (1.0 - kb) / kg * 65536.0 / 16.0 + 0.5);
    cm[3] = (int32_t)floor(sc * 2.0 * kr * (1.0 - kr) / kg * 65536.0 / 16.0 + 0.5);
    cm[4] = (int32_t)floor(sc * 2.0 * (1.0 - kb) * 65536.0 / 16.0 + 0.5);
    cm[5] = (int32_t)oy;
}

/* Two passes per row, each a plain loop a compiler can vectorize: chroma
 * to one U and V per pixel (in sixteenths, minus the offset), then the
 * matrix into packed RGBA. */
PSYVID_API int psyvid_yuv_to_rgba(const psyvid_planes* p, int32_t format, int32_t w, int32_t h,
                                  int32_t matrix, int32_t range, int32_t siting, int32_t chroma,
                                  uint8_t* rgba, int32_t stride, int16_t* rows) {
    int32_t cm[8];
    int32_t cw = (w + 1) / 2, ch = (h + 1) / 2, y, x;
    int16_t *vu, *vv;
    int32_t *U, *V;
    int nv12 = format == PSYVID_FMT_NV12;
    int left = siting == PSYVID_SITING_LEFT;
    int step = nv12 ? 2 : 1;
    if (!p || !rgba || !rows || w <= 0 || h <= 0 || !psyvid__is_yuv(format)) return PSYVID_ERR_ARG;
    if (((uintptr_t)rgba & 3u) || (stride & 3) || ((uintptr_t)rows & 1u)) return PSYVID_ERR_ARG;
    psyvid__yuv_coef(matrix, range, cm);
    vu = rows + 1;
    vv = rows + (cw + 2) + 1;
    U = (int32_t*)(void*)(((uintptr_t)(rows + 2 * (cw + 2)) + 15) & ~(uintptr_t)15);
    V = U + (w + 2);
    for (y = 0; y < h; y++) {
        const uint8_t* Y = p->data[0] + (size_t)y * (size_t)p->stride[0];
        uint32_t* o = (uint32_t*)(void*)(rgba + (size_t)y * (size_t)stride);
        const int32_t c0 = cm[0], c1 = cm[1], c2 = cm[2], c3 = cm[3], c4 = cm[4], oy = cm[5];
        int32_t cy = y >> 1;
        if (chroma == PSYVID_CHROMA_NEAREST) {
            const uint8_t* Ua = p->data[1] + (size_t)cy * (size_t)p->stride[1];
            const uint8_t* Va = nv12 ? Ua + 1 : p->data[2] + (size_t)cy * (size_t)p->stride[2];
            for (x = 0; x < cw; x++) {
                int32_t u = 16 * ((int32_t)Ua[x * step] - 128), v = 16 * ((int32_t)Va[x * step] - 128);
                U[2 * x] = u; U[2 * x + 1] = u;
                V[2 * x] = v; V[2 * x + 1] = v;
            }
        } else {
            /* Vertical: chroma rows sit between luma rows (both sitings), so
             * an even luma row takes 3/4 of its chroma row and 1/4 of the one
             * above, an odd row 1/4 of the one below. */
            int32_t cb = (y & 1) ? cy + 1 : cy - 1;
            const uint8_t *Ua, *Ub, *Va, *Vb;
            if (cb < 0) cb = 0;
            if (cb >= ch) cb = ch - 1;
            Ua = p->data[1] + (size_t)cy * (size_t)p->stride[1];
            Ub = p->data[1] + (size_t)cb * (size_t)p->stride[1];
            Va = nv12 ? Ua + 1 : p->data[2] + (size_t)cy * (size_t)p->stride[2];
            Vb = nv12 ? Ub + 1 : p->data[2] + (size_t)cb * (size_t)p->stride[2];
            for (x = 0; x < cw; x++) {
                vu[x] = (int16_t)(3 * (int32_t)Ua[x * step] + (int32_t)Ub[x * step]);
                vv[x] = (int16_t)(3 * (int32_t)Va[x * step] + (int32_t)Vb[x * step]);
            }
            vu[-1] = vu[0]; vv[-1] = vv[0];
            vu[cw] = vu[cw - 1]; vv[cw] = vv[cw - 1];
            if (left) {
                /* co-sited with even columns: even x takes it whole, odd x
                 * the mean of the two around it */
                for (x = 0; x < cw; x++) {
                    U[2 * x] = 4 * vu[x] - 2048;     U[2 * x + 1] = 2 * (vu[x] + vu[x + 1]) - 2048;
                    V[2 * x] = 4 * vv[x] - 2048;     V[2 * x + 1] = 2 * (vv[x] + vv[x + 1]) - 2048;
                }
            } else {
                for (x = 0; x < cw; x++) {
                    U[2 * x] = 3 * vu[x] + vu[x - 1] - 2048; U[2 * x + 1] = 3 * vu[x] + vu[x + 1] - 2048;
                    V[2 * x] = 3 * vv[x] + vv[x - 1] - 2048; V[2 * x + 1] = 3 * vv[x] + vv[x + 1] - 2048;
                }
            }
        }
        for (x = 0; x < w; x++) {
            int32_t yy = c0 * ((int32_t)Y[x] - oy) + 32768;
            int32_t r = (yy + c1 * V[x]) >> 16;
            int32_t g = (yy - c2 * U[x] - c3 * V[x]) >> 16;
            int32_t b = (yy + c4 * U[x]) >> 16;
            r = r < 0 ? 0 : r > 255 ? 255 : r;
            g = g < 0 ? 0 : g > 255 ? 255 : g;
            b = b < 0 ? 0 : b > 255 ? 255 : b;
            /* little-endian: R in the first byte */
            o[x] = (uint32_t)r | (uint32_t)g << 8 | (uint32_t)b << 16 | UINT32_C(0xFF000000);
        }
    }
    return PSYVID_OK;
}

/* --- byte source ---------------------------------------------------------------- */

typedef struct psyvid__src {
    FILE*                fh;
    const uint8_t*       mem;
    const psyvid_reader* rd;
    void*                rd_ctx;
    int64_t              size;
} psyvid__src;

static int psyvid__fseek(FILE* f, int64_t off) {
#if defined(_WIN32)
    return _fseeki64(f, off, SEEK_SET);
#else
    return fseeko(f, (off_t)off, SEEK_SET);
#endif
}

static FILE* psyvid__fopen(const char* path, const char* mode) {
#if defined(_MSC_VER)
    FILE* f = NULL;
    if (fopen_s(&f, path, mode) != 0) return NULL;
    return f;
#else
    return fopen(path, mode);
#endif
}

static int psyvid__src_open(psyvid__src* s, const char* path, const void* data, size_t size,
                            const psyvid_reader* rd, void* rd_ctx) {
    memset(s, 0, sizeof *s);
    if (path) {
        s->fh = psyvid__fopen(path, "rb");
        if (!s->fh) return PSYVID_ERR_NOT_FOUND;
#if defined(_WIN32)
        if (_fseeki64(s->fh, 0, SEEK_END) != 0) { fclose(s->fh); s->fh = NULL; return PSYVID_ERR_IO; }
        s->size = _ftelli64(s->fh);
#else
        if (fseeko(s->fh, 0, SEEK_END) != 0) { fclose(s->fh); s->fh = NULL; return PSYVID_ERR_IO; }
        s->size = (int64_t)ftello(s->fh);
#endif
        if (s->size < 0) { fclose(s->fh); s->fh = NULL; return PSYVID_ERR_IO; }
        return PSYVID_OK;
    }
    if (data) { s->mem = (const uint8_t*)data; s->size = (int64_t)size; return PSYVID_OK; }
    if (rd && rd->read) { s->rd = rd; s->rd_ctx = rd_ctx; s->size = rd->size; return PSYVID_OK; }
    return PSYVID_ERR_ARG;
}

static void psyvid__src_close(psyvid__src* s) {
    if (s->fh) fclose(s->fh);
    memset(s, 0, sizeof *s);
}

/* Reads exactly n bytes at off, or fails. */
static int psyvid__src_read(psyvid__src* s, int64_t off, void* buf, int64_t n) {
    if (off < 0 || n < 0 || off + n > s->size) return PSYVID_ERR_IO;
    if (n == 0) return PSYVID_OK;
    if (s->mem) { memcpy(buf, s->mem + off, (size_t)n); return PSYVID_OK; }
    if (s->fh) {
        if (psyvid__fseek(s->fh, off) != 0) return PSYVID_ERR_IO;
        return fread(buf, 1, (size_t)n, s->fh) == (size_t)n ? PSYVID_OK : PSYVID_ERR_IO;
    }
    if (s->rd) {
        int64_t got = 0;
        while (got < n) {
            int64_t r = s->rd->read(s->rd_ctx, off + got, (uint8_t*)buf + got, n - got);
            if (r <= 0) return PSYVID_ERR_IO;
            got += r;
        }
        return PSYVID_OK;
    }
    return PSYVID_ERR_IO;
}

/* XXH64 of the first and last 64 KB: a cheap identity of the media file;
 * the full SHA-256 is the pack loader's, once. */
static int psyvid__src_ends(psyvid__src* s, uint64_t* head, uint64_t* tail) {
    unsigned char* buf;
    int64_t n = s->size < 65536 ? s->size : 65536;
    int rc;
    buf = (unsigned char*)psyvid__malloc((size_t)(n > 0 ? n : 1));
    if (!buf) return PSYVID_ERR_FULL;
    rc = psyvid__src_read(s, 0, buf, n);
    if (rc == PSYVID_OK) *head = psyvid_xxh64(buf, (size_t)n, 0);
    if (rc == PSYVID_OK) rc = psyvid__src_read(s, s->size - n, buf, n);
    if (rc == PSYVID_OK) *tail = psyvid_xxh64(buf, (size_t)n, 0);
    psyvid__free(buf);
    return rc;
}

/* --- little-endian fields ------------------------------------------------------- */

static void psyvid__w32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void psyvid__w64(uint8_t* p, uint64_t v) { psyvid__w32(p, (uint32_t)v); psyvid__w32(p + 4, (uint32_t)(v >> 32)); }
static uint32_t psyvid__r32(const uint8_t* p) { return psyvid__rd32(p); }
static uint64_t psyvid__r64(const uint8_t* p) { return psyvid__rd64(p); }

/* --- the canonical description ------------------------------------------------ */

typedef struct psyvid__canon {
    int32_t w, h, format, fps_num, fps_den, gop, codec;
    int64_t frames;
    uint8_t matrix, range, transfer, primaries, siting;
} psyvid__canon;

static int psyvid__rate_ok(int32_t num, int32_t den) {
    if (num <= 0 || den <= 0) return 0;
    if ((double)num * (double)den * 1e9 >= 9.2e18) return 0;
    if ((double)num / (double)den > 1000.0 || (double)num / (double)den < 0.001) return 0;
    return 1;
}

static void psyvid__rgb_color(psyvid__canon* c) {
    if (!c->matrix) c->matrix = PSYVID_MATRIX_RGB;
    if (!c->range) c->range = PSYVID_RANGE_FULL;
    if (!c->transfer) c->transfer = PSYVID_TRC_DEVICE;
    if (!c->primaries) c->primaries = PSYVID_PRIM_DEVICE;
    if (!c->siting) c->siting = PSYVID_SITING_NONE;
}

/* The fields a canonical description must have; "" when it has them. */
static int psyvid__canon_check(const psyvid__canon* c, char* err, size_t cap) {
    if (c->w <= 0 || c->h <= 0 || c->w > 16384 || c->h > 16384) { psyvid__fmt(err, cap, "size %dx%d", (int)c->w, (int)c->h); return PSYVID_ERR_FORMAT; }
    if (c->format == PSYVID_FMT_P010) { psyvid__fmt(err, cap, "10-bit (P010) is refused in v0.1: the panel link is 8 bits; re-encode as 8-bit 4:2:0"); return PSYVID_ERR_REFUSED; }
    if (!psyvid__is_rgb(c->format) && !psyvid__is_yuv(c->format)) { psyvid__fmt(err, cap, "pixel format %d is not one psy_video.h knows", (int)c->format); return PSYVID_ERR_FORMAT; }
    if (!psyvid__rate_ok(c->fps_num, c->fps_den)) { psyvid__fmt(err, cap, "rate %d/%d is not a canonical rate", (int)c->fps_num, (int)c->fps_den); return PSYVID_ERR_FORMAT; }
    if (c->frames <= 0) { psyvid__fmt(err, cap, "no frames"); return PSYVID_ERR_FORMAT; }
    if (c->gop <= 0) { psyvid__fmt(err, cap, "no constant GOP length"); return PSYVID_ERR_FORMAT; }
    if (psyvid__is_yuv(c->format)) {
        if (c->matrix < PSYVID_MATRIX_BT601 || c->matrix > PSYVID_MATRIX_BT2020) { psyvid__fmt(err, cap, "the matrix is unspecified"); return PSYVID_ERR_FORMAT; }
        if (c->range < PSYVID_RANGE_LIMITED || c->range > PSYVID_RANGE_FULL) { psyvid__fmt(err, cap, "the range is unspecified"); return PSYVID_ERR_FORMAT; }
        if (c->transfer < PSYVID_TRC_BT1886 || c->transfer > PSYVID_TRC_GAMMA22) { psyvid__fmt(err, cap, "the transfer is unspecified"); return PSYVID_ERR_FORMAT; }
        if (c->primaries < PSYVID_PRIM_BT709 || c->primaries > PSYVID_PRIM_BT2020) { psyvid__fmt(err, cap, "the primaries are unspecified"); return PSYVID_ERR_FORMAT; }
        if (c->siting != PSYVID_SITING_LEFT && c->siting != PSYVID_SITING_CENTER) { psyvid__fmt(err, cap, "the chroma siting is unspecified"); return PSYVID_ERR_FORMAT; }
    }
    return PSYVID_OK;
}

/* --- the .psyvi index ------------------------------------------------------------
 * 128-byte header, then frames x u64 XXH64 of the decoded planes, then the
 * note (the command that made the media file). */
#define PSYVID__IDX_HDR 128

typedef struct psyvid__index {
    psyvid__canon c;
    int64_t  media_size;
    uint64_t head, tail;
    uint64_t* hashes;
} psyvid__index;

static int psyvid__index_parse(const uint8_t* b, size_t n, psyvid__index* out, int want_hashes, char* err, size_t cap) {
    uint32_t hdr;
    memset(out, 0, sizeof *out);
    if (n < PSYVID__IDX_HDR || memcmp(b, "PSYVIDX1", 8) != 0) { psyvid__fmt(err, cap, "not a psy_video index"); return PSYVID_ERR_FORMAT; }
    if (psyvid__r32(b + 8) != 1) { psyvid__fmt(err, cap, "index version %u is not one this header knows", (unsigned)psyvid__r32(b + 8)); return PSYVID_ERR_FORMAT; }
    hdr = psyvid__r32(b + 12);
    if (hdr != PSYVID__IDX_HDR) { psyvid__fmt(err, cap, "index header size %u", (unsigned)hdr); return PSYVID_ERR_FORMAT; }
    if (psyvid__r64(b + 120) != psyvid_xxh64(b, 120, 0)) { psyvid__fmt(err, cap, "the index header is damaged (hash)"); return PSYVID_ERR_FORMAT; }
    out->c.codec = (int32_t)psyvid__r32(b + 16);
    out->c.w = (int32_t)psyvid__r32(b + 20);
    out->c.h = (int32_t)psyvid__r32(b + 24);
    out->c.format = (int32_t)psyvid__r32(b + 28);
    out->c.fps_num = (int32_t)psyvid__r32(b + 32);
    out->c.fps_den = (int32_t)psyvid__r32(b + 36);
    out->c.frames = (int64_t)psyvid__r64(b + 40);
    out->c.gop = (int32_t)psyvid__r32(b + 48);
    out->c.matrix = b[52]; out->c.range = b[53]; out->c.transfer = b[54]; out->c.primaries = b[55]; out->c.siting = b[56];
    out->media_size = (int64_t)psyvid__r64(b + 64);
    out->head = psyvid__r64(b + 72);
    out->tail = psyvid__r64(b + 80);
    if (out->c.frames <= 0 || out->c.frames > ((int64_t)1 << 40) ||
        (size_t)PSYVID__IDX_HDR + (size_t)out->c.frames * 8 > n) { psyvid__fmt(err, cap, "the index is truncated"); return PSYVID_ERR_FORMAT; }
    if (want_hashes) {
        int64_t i;
        out->hashes = (uint64_t*)psyvid__malloc((size_t)out->c.frames * 8);
        if (!out->hashes) { psyvid__fmt(err, cap, "out of memory for the index"); return PSYVID_ERR_FULL; }
        for (i = 0; i < out->c.frames; i++) out->hashes[i] = psyvid__r64(b + PSYVID__IDX_HDR + (size_t)i * 8);
    }
    return PSYVID_OK;
}

/* Reads path + ".psyvi" or the desc's bytes. */
static int psyvid__index_load(const psyvid_desc* d, psyvid__index* out, int want_hashes, char* err, size_t cap) {
    uint8_t* buf = NULL;
    size_t n = 0;
    int rc;
    if (d->index) {
        return psyvid__index_parse((const uint8_t*)d->index, d->index_size, out, want_hashes, err, cap);
    }
    if (!d->path) { psyvid__fmt(err, cap, "no index: give desc.index, or a path with its .psyvi beside it"); return PSYVID_ERR_NOT_FOUND; }
    {
        char ip[1024];
        psyvid__src s;
        psyvid__fmt(ip, sizeof ip, "%s.psyvi", d->path);
        if (psyvid__src_open(&s, ip, NULL, 0, NULL, NULL) != PSYVID_OK) {
            psyvid__fmt(err, cap, "%s has no index (%s); make it with psyvid_index_make(), the pack tool's step", d->path, ip);
            return PSYVID_ERR_NOT_FOUND;
        }
        n = (size_t)s.size;
        buf = (uint8_t*)psyvid__malloc(n ? n : 1);
        if (!buf) { psyvid__src_close(&s); return PSYVID_ERR_FULL; }
        rc = psyvid__src_read(&s, 0, buf, (int64_t)n);
        psyvid__src_close(&s);
        if (rc != PSYVID_OK) { psyvid__free(buf); psyvid__fmt(err, cap, "cannot read %s", ip); return rc; }
    }
    rc = psyvid__index_parse(buf, n, out, want_hashes, err, cap);
    psyvid__free(buf);
    return rc;
}

static void psyvid__index_header(uint8_t* b, const psyvid__canon* c, int64_t media_size, uint64_t head, uint64_t tail) {
    memset(b, 0, PSYVID__IDX_HDR);
    memcpy(b, "PSYVIDX1", 8);
    psyvid__w32(b + 8, 1);
    psyvid__w32(b + 12, PSYVID__IDX_HDR);
    psyvid__w32(b + 16, (uint32_t)c->codec);
    psyvid__w32(b + 20, (uint32_t)c->w);
    psyvid__w32(b + 24, (uint32_t)c->h);
    psyvid__w32(b + 28, (uint32_t)c->format);
    psyvid__w32(b + 32, (uint32_t)c->fps_num);
    psyvid__w32(b + 36, (uint32_t)c->fps_den);
    psyvid__w64(b + 40, (uint64_t)c->frames);
    psyvid__w32(b + 48, (uint32_t)c->gop);
    b[52] = c->matrix; b[53] = c->range; b[54] = c->transfer; b[55] = c->primaries; b[56] = c->siting;
    psyvid__w64(b + 64, (uint64_t)media_size);
    psyvid__w64(b + 72, head);
    psyvid__w64(b + 80, tail);
    psyvid__w64(b + 120, psyvid_xxh64(b, 120, 0));
}

/* --- frame sequence: format -------------------------------------------------- */

#define PSYVID__SEQ_HDR   4096
#define PSYVID__SEQ_ALIGN 4096
#define PSYVID__SEQ_FHDR  64
#define PSYVID__SEQ_ENTRY 24

static int64_t psyvid__align(int64_t v) { return (v + PSYVID__SEQ_ALIGN - 1) / PSYVID__SEQ_ALIGN * PSYVID__SEQ_ALIGN; }

static void psyvid__seq_header(uint8_t* b, const psyvid__canon* c, int32_t compression, int64_t index_offset) {
    memset(b, 0, 128);
    memcpy(b, "PSYVSEQ1", 8);
    psyvid__w32(b + 8, 1);
    psyvid__w32(b + 12, PSYVID__SEQ_HDR);
    psyvid__w32(b + 16, (uint32_t)c->w);
    psyvid__w32(b + 20, (uint32_t)c->h);
    psyvid__w32(b + 24, (uint32_t)c->format);
    psyvid__w32(b + 28, (uint32_t)compression);
    psyvid__w32(b + 32, (uint32_t)c->fps_num);
    psyvid__w32(b + 36, (uint32_t)c->fps_den);
    psyvid__w64(b + 40, (uint64_t)c->frames);
    b[48] = c->matrix; b[49] = c->range; b[50] = c->transfer; b[51] = c->primaries; b[52] = c->siting;
    psyvid__w64(b + 64, (uint64_t)index_offset);
    psyvid__w64(b + 72, psyvid_xxh64(b, 72, 0));
}

static int psyvid__seq_parse(const uint8_t* b, psyvid__canon* c, int32_t* compression, int64_t* index_offset,
                             char* err, size_t cap) {
    memset(c, 0, sizeof *c);
    if (memcmp(b, "PSYVSEQ1", 8) != 0) { psyvid__fmt(err, cap, "not a frame sequence"); return PSYVID_ERR_FORMAT; }
    if (psyvid__r32(b + 8) != 1 || psyvid__r32(b + 12) != PSYVID__SEQ_HDR) { psyvid__fmt(err, cap, "frame sequence version %u is not one this header knows", (unsigned)psyvid__r32(b + 8)); return PSYVID_ERR_FORMAT; }
    if (psyvid__r64(b + 72) != psyvid_xxh64(b, 72, 0)) { psyvid__fmt(err, cap, "the header is damaged or the writer never closed the file (hash)"); return PSYVID_ERR_FORMAT; }
    c->w = (int32_t)psyvid__r32(b + 16);
    c->h = (int32_t)psyvid__r32(b + 20);
    c->format = (int32_t)psyvid__r32(b + 24);
    *compression = (int32_t)psyvid__r32(b + 28);
    c->fps_num = (int32_t)psyvid__r32(b + 32);
    c->fps_den = (int32_t)psyvid__r32(b + 36);
    c->frames = (int64_t)psyvid__r64(b + 40);
    c->matrix = b[48]; c->range = b[49]; c->transfer = b[50]; c->primaries = b[51]; c->siting = b[52];
    c->gop = 1;
    c->codec = PSYVID_CODEC_SEQ;
    *index_offset = (int64_t)psyvid__r64(b + 64);
    if (*compression != PSYVID_SEQ_RAW && *compression != PSYVID_SEQ_QOI) { psyvid__fmt(err, cap, "compression %d", (int)*compression); return PSYVID_ERR_FORMAT; }
    if (*compression == PSYVID_SEQ_QOI && c->format != PSYVID_FMT_RGBA8) { psyvid__fmt(err, cap, "QOI frames must be RGBA8"); return PSYVID_ERR_FORMAT; }
    return PSYVID_OK;
}

/* --- frame sequence: writer ------------------------------------------------------ */

static const uint8_t psyvid__zeros[PSYVID__SEQ_ALIGN] = { 0 };

PSYVID_API const char* psyvid_seq_error(const psyvid_seq* w) { return w ? w->error : ""; }

PSYVID_API bool psyvid_seq_create(psyvid_seq* w, const psyvid_seq_desc* d) {
    psyvid__canon c;
    psyvid_planes pl;
    char e[200];
    if (!w) return false;
    memset(w, 0, sizeof *w);
    if (!d || !d->path) { psyvid__fmt(w->error, sizeof w->error, "psy_video: seq: desc.path is required"); return false; }
    w->d = *d;
    if (w->d.fps_den == 0) w->d.fps_den = 1;
    memset(&c, 0, sizeof c);
    c.w = d->w; c.h = d->h; c.format = d->format; c.fps_num = w->d.fps_num; c.fps_den = w->d.fps_den;
    c.frames = 1; c.gop = 1; c.codec = PSYVID_CODEC_SEQ;
    c.matrix = d->matrix; c.range = d->range; c.transfer = d->transfer; c.primaries = d->primaries; c.siting = d->siting;
    if (psyvid__is_rgb(c.format)) psyvid__rgb_color(&c);
    if (psyvid__canon_check(&c, e, sizeof e) < 0) { psyvid__fmt(w->error, sizeof w->error, "psy_video: seq: %s", e); return false; }
    if (d->compression == PSYVID_SEQ_QOI && d->format != PSYVID_FMT_RGBA8) { psyvid__fmt(w->error, sizeof w->error, "psy_video: seq: QOI needs RGBA8"); return false; }
    if (d->compression != PSYVID_SEQ_RAW && d->compression != PSYVID_SEQ_QOI) { psyvid__fmt(w->error, sizeof w->error, "psy_video: seq: unknown compression %d", (int)d->compression); return false; }
    w->d.matrix = c.matrix; w->d.range = c.range; w->d.transfer = c.transfer; w->d.primaries = c.primaries; w->d.siting = c.siting;
    w->scratch_bytes = psyvid__planes_layout(d->format, d->w, d->h, NULL, &pl);
    if (d->compression == PSYVID_SEQ_QOI) w->scratch_bytes = psyvid_qoi_max_bytes(d->w, d->h, 4) + (size_t)d->w * (size_t)d->h * 4;
    w->scratch = (uint8_t*)psyvid__malloc(w->scratch_bytes);
    w->cap = d->max_frames > 0 ? d->max_frames : 1024;
    w->index = (uint8_t*)psyvid__malloc((size_t)w->cap * PSYVID__SEQ_ENTRY);
    if (!w->scratch || !w->index) { psyvid__free(w->scratch); psyvid__free(w->index); w->scratch = w->index = NULL; psyvid__fmt(w->error, sizeof w->error, "psy_video: seq: out of memory"); return false; }
    w->fh = psyvid__fopen(d->path, "wb");
    if (!w->fh) { psyvid__free(w->scratch); psyvid__free(w->index); w->scratch = w->index = NULL; psyvid__fmt(w->error, sizeof w->error, "psy_video: seq: cannot create %s", d->path); return false; }
    /* the header is written at close; until then the file has none, so a
     * reader refuses a file whose writer died (the frame headers rebuild it) */
    if (fwrite(psyvid__zeros, 1, PSYVID__SEQ_HDR, (FILE*)w->fh) != PSYVID__SEQ_HDR) { fclose((FILE*)w->fh); w->fh = NULL; psyvid__fmt(w->error, sizeof w->error, "psy_video: seq: write failed"); return false; }
    w->offset = PSYVID__SEQ_HDR;
    w->open = 1;
    return true;
}

PSYVID_API int psyvid_seq_write(psyvid_seq* w, const void* const planes[3], const int32_t strides[3],
                                const psyvid_seq_meta* meta) {
    psyvid_planes in;
    uint8_t fh[PSYVID__SEQ_FHDR];
    FILE* f;
    uint64_t hash;
    size_t data_bytes = 0;
    int k, y, np;
    if (!w || !w->open) return PSYVID_ERR_CLOSED;
    if (!planes || !strides) return PSYVID_ERR_ARG;
    f = (FILE*)w->fh;
    psyvid__planes_layout(w->d.format, w->d.w, w->d.h, NULL, &in);
    np = psyvid__n_planes(w->d.format);
    for (k = 0; k < np; k++) {
        if (!planes[k] || strides[k] < (int32_t)psyvid__row_bytes(w->d.format, &in, k)) return PSYVID_ERR_ARG;
        in.data[k] = (uint8_t*)(uintptr_t)planes[k];
        in.stride[k] = strides[k];
    }
    if (w->n >= w->cap) {
        uint8_t* ni;
        if (w->d.max_frames > 0) return PSYVID_ERR_FULL;
        ni = (uint8_t*)psyvid__realloc(w->index, (size_t)w->cap * 2 * PSYVID__SEQ_ENTRY);
        if (!ni) return PSYVID_ERR_FULL;
        w->index = ni;
        w->cap *= 2;
    }
    hash = psyvid__hash_planes(w->d.format, &in);
    if (w->d.compression == PSYVID_SEQ_QOI) {
        uint8_t* tight = w->scratch + psyvid_qoi_max_bytes(w->d.w, w->d.h, 4);
        int64_t qn;
        for (y = 0; y < w->d.h; y++)
            memcpy(tight + (size_t)y * (size_t)w->d.w * 4, in.data[0] + (size_t)y * (size_t)in.stride[0], (size_t)w->d.w * 4);
        qn = psyvid_qoi_encode(tight, w->d.w, w->d.h, 4, w->scratch, psyvid_qoi_max_bytes(w->d.w, w->d.h, 4));
        if (qn < 0) return (int)qn;
        data_bytes = (size_t)qn;
    } else {
        for (k = 0; k < np; k++) data_bytes += psyvid__row_bytes(w->d.format, &in, k) * (size_t)in.h[k];
    }
    memset(fh, 0, sizeof fh);
    memcpy(fh, "PSYF", 4);
    psyvid__w32(fh + 4, PSYVID__SEQ_FHDR);
    psyvid__w64(fh + 8, (uint64_t)w->n);
    psyvid__w32(fh + 16, (uint32_t)data_bytes);
    psyvid__w64(fh + 24, hash);
    if (meta) {
        psyvid__w64(fh + 32, (uint64_t)meta->t_ns);
        psyvid__w64(fh + 40, (uint64_t)meta->trial);
        psyvid__w64(fh + 48, (uint64_t)meta->event);
        psyvid__w64(fh + 56, (uint64_t)meta->camera_ns);
    }
    if (psyvid__fseek(f, w->offset) != 0 || fwrite(fh, 1, sizeof fh, f) != sizeof fh) return PSYVID_ERR_IO;
    if (w->d.compression == PSYVID_SEQ_QOI) {
        if (fwrite(w->scratch, 1, data_bytes, f) != data_bytes) return PSYVID_ERR_IO;
    } else {
        for (k = 0; k < np; k++) {
            size_t rb = psyvid__row_bytes(w->d.format, &in, k);
            if ((size_t)in.stride[k] == rb) {
                if (fwrite(in.data[k], 1, rb * (size_t)in.h[k], f) != rb * (size_t)in.h[k]) return PSYVID_ERR_IO;
            } else {
                for (y = 0; y < in.h[k]; y++)
                    if (fwrite(in.data[k] + (size_t)y * (size_t)in.stride[k], 1, rb, f) != rb) return PSYVID_ERR_IO;
            }
        }
    }
    {
        int64_t end = w->offset + PSYVID__SEQ_FHDR + (int64_t)data_bytes;
        int64_t padded = psyvid__align(end);
        if (padded > end && fwrite(psyvid__zeros, 1, (size_t)(padded - end), f) != (size_t)(padded - end)) return PSYVID_ERR_IO;
        psyvid__w64(w->index + (size_t)w->n * PSYVID__SEQ_ENTRY, (uint64_t)w->offset);
        psyvid__w32(w->index + (size_t)w->n * PSYVID__SEQ_ENTRY + 8, (uint32_t)data_bytes);
        psyvid__w32(w->index + (size_t)w->n * PSYVID__SEQ_ENTRY + 12, 0);
        psyvid__w64(w->index + (size_t)w->n * PSYVID__SEQ_ENTRY + 16, hash);
        w->offset = padded;
    }
    w->n++;
    return PSYVID_OK;
}

PSYVID_API int psyvid_seq_close(psyvid_seq* w) {
    uint8_t hdr[128];
    psyvid__canon c;
    FILE* f;
    int rc = PSYVID_OK;
    if (!w || !w->open) return PSYVID_ERR_CLOSED;
    f = (FILE*)w->fh;
    memset(&c, 0, sizeof c);
    c.w = w->d.w; c.h = w->d.h; c.format = w->d.format; c.fps_num = w->d.fps_num; c.fps_den = w->d.fps_den;
    c.frames = w->n;
    c.matrix = w->d.matrix; c.range = w->d.range; c.transfer = w->d.transfer; c.primaries = w->d.primaries; c.siting = w->d.siting;
    if (psyvid__fseek(f, w->offset) != 0 ||
        fwrite(w->index, 1, (size_t)w->n * PSYVID__SEQ_ENTRY, f) != (size_t)w->n * PSYVID__SEQ_ENTRY) rc = PSYVID_ERR_IO;
    psyvid__seq_header(hdr, &c, w->d.compression, w->offset);
    if (rc == PSYVID_OK && (psyvid__fseek(f, 0) != 0 || fwrite(hdr, 1, sizeof hdr, f) != sizeof hdr)) rc = PSYVID_ERR_IO;
    if (fclose(f) != 0) rc = PSYVID_ERR_IO;
    psyvid__free(w->index);
    psyvid__free(w->scratch);
    w->fh = NULL; w->index = NULL; w->scratch = NULL;
    w->open = 0;
    if (rc == PSYVID_OK && w->n == 0) { psyvid__fmt(w->error, sizeof w->error, "psy_video: seq: no frames written"); rc = PSYVID_ERR_ARG; }
    return rc;
}

/* --- frame sequence: reader backend ------------------------------------------------ */

typedef struct psyvid__seqr {
    psyvid__src src;
    psyvid__canon c;
    int32_t compression;
    uint8_t* index;           /* frames x 24 bytes                              */
    uint8_t* comp;            /* one compressed frame                          */
    size_t   comp_bytes;
    int64_t  pos;
    size_t   frame_bytes;
} psyvid__seqr;

static int psyvid__seqr_open(void* ctx, const psyvid_decoder_open* in, psyvid_stream* out, char* err, size_t cap) {
    psyvid__seqr* s = (psyvid__seqr*)ctx;
    uint8_t hdr[128];
    int64_t index_offset, i, prev = 0;
    int rc;
    rc = psyvid__src_open(&s->src, in->path, in->data, in->size, in->reader, in->reader_ctx);
    if (rc < 0) { psyvid__fmt(err, cap, "cannot open %s", in->path ? in->path : "the source"); return rc; }
    if (s->src.size < PSYVID__SEQ_HDR || psyvid__src_read(&s->src, 0, hdr, sizeof hdr) != PSYVID_OK) { psyvid__fmt(err, cap, "too short for a frame sequence"); return PSYVID_ERR_FORMAT; }
    rc = psyvid__seq_parse(hdr, &s->c, &s->compression, &index_offset, err, cap);
    if (rc < 0) return rc;
    if (s->c.frames <= 0 || s->c.frames > ((int64_t)1 << 32) || index_offset < PSYVID__SEQ_HDR ||
        index_offset + s->c.frames * PSYVID__SEQ_ENTRY > s->src.size) { psyvid__fmt(err, cap, "the index is outside the file"); return PSYVID_ERR_FORMAT; }
    s->frame_bytes = psyvid__planes_layout(s->c.format, s->c.w, s->c.h, NULL, NULL);
    s->index = (uint8_t*)psyvid__malloc((size_t)s->c.frames * PSYVID__SEQ_ENTRY);
    if (!s->index) { psyvid__fmt(err, cap, "out of memory for the index"); return PSYVID_ERR_FULL; }
    if (psyvid__src_read(&s->src, index_offset, s->index, s->c.frames * PSYVID__SEQ_ENTRY) != PSYVID_OK) { psyvid__fmt(err, cap, "cannot read the index"); return PSYVID_ERR_IO; }
    s->comp_bytes = 0;
    for (i = 0; i < s->c.frames; i++) {
        const uint8_t* e = s->index + (size_t)i * PSYVID__SEQ_ENTRY;
        int64_t off = (int64_t)psyvid__r64(e);
        uint32_t size = psyvid__r32(e + 8);
        if (off < PSYVID__SEQ_HDR || off % PSYVID__SEQ_ALIGN != 0 || off <= prev - 1 || off + PSYVID__SEQ_FHDR + (int64_t)size > index_offset) {
            psyvid__fmt(err, cap, "index entry %lld is outside the frames", (long long)i);
            return PSYVID_ERR_FORMAT;
        }
        if (s->compression == PSYVID_SEQ_RAW && size != s->frame_bytes) { psyvid__fmt(err, cap, "frame %lld has %u bytes, the format needs %zu", (long long)i, (unsigned)size, s->frame_bytes); return PSYVID_ERR_FORMAT; }
        if ((size_t)size > s->comp_bytes) s->comp_bytes = size;
        prev = off + 1;
    }
    if (s->compression == PSYVID_SEQ_QOI) {
        s->comp = (uint8_t*)psyvid__malloc(s->comp_bytes + PSYVID__SEQ_FHDR);
        if (!s->comp) { psyvid__fmt(err, cap, "out of memory"); return PSYVID_ERR_FULL; }
    }
    out->w = s->c.w; out->h = s->c.h; out->format = s->c.format;
    out->fps_num = s->c.fps_num; out->fps_den = s->c.fps_den;
    out->frames = s->c.frames; out->gop = 1; out->codec = PSYVID_CODEC_SEQ;
    out->matrix = s->c.matrix; out->range = s->c.range; out->transfer = s->c.transfer;
    out->primaries = s->c.primaries; out->siting = s->c.siting;
    out->timescale = 0;
    out->caps = PSYVID_DEC_CPU | PSYVID_DEC_RANDOM_ACCESS;
    s->pos = 0;
    return PSYVID_OK;
}

static int psyvid__seqr_next(void* ctx, psyvid_planes* dst, psyvid_out* out) {
    psyvid__seqr* s = (psyvid__seqr*)ctx;
    const uint8_t* e;
    uint8_t fh[PSYVID__SEQ_FHDR];
    int64_t off;
    uint32_t size;
    if (s->pos >= s->c.frames) return PSYVID_ENDED;
    e = s->index + (size_t)s->pos * PSYVID__SEQ_ENTRY;
    off = (int64_t)psyvid__r64(e);
    size = psyvid__r32(e + 8);
    out->index = s->pos;
    out->pts = s->pos;
    out->hash = psyvid__r64(e + 16);
    out->flags = PSYVID_OUT_KEYFRAME | PSYVID_OUT_HAS_HASH;
    s->pos++;
    if (!dst) return PSYVID_OK;   /* every frame is a keyframe: nothing to decode */
    if (psyvid__src_read(&s->src, off, fh, sizeof fh) != PSYVID_OK) return PSYVID_ERR_IO;
    if (memcmp(fh, "PSYF", 4) != 0 || (int64_t)psyvid__r64(fh + 8) != out->index || psyvid__r32(fh + 16) != size)
        return PSYVID_ERR_FORMAT;
    if (s->compression == PSYVID_SEQ_QOI) {
        if (psyvid__src_read(&s->src, off + PSYVID__SEQ_FHDR, s->comp, size) != PSYVID_OK) return PSYVID_ERR_IO;
        if (dst->stride[0] != s->c.w * 4) return PSYVID_ERR_ARG;
        return psyvid_qoi_decode(s->comp, size, dst->data[0], s->c.w, s->c.h);
    }
    /* raw: the core's planes are tight and contiguous, as the file's are */
    return psyvid__src_read(&s->src, off + PSYVID__SEQ_FHDR, dst->data[0], size);
}

static int psyvid__seqr_seek(void* ctx, int64_t key, int64_t key_pts) {
    psyvid__seqr* s = (psyvid__seqr*)ctx;
    (void)key_pts;
    if (key < 0 || key >= s->c.frames) return PSYVID_ERR_ARG;
    s->pos = key;
    return PSYVID_OK;
}

static void psyvid__seqr_close(void* ctx) {
    psyvid__seqr* s = (psyvid__seqr*)ctx;
    psyvid__src_close(&s->src);
    psyvid__free(s->index);
    psyvid__free(s->comp);
    s->index = NULL; s->comp = NULL;
}

static int psyvid__seqr_describe(void* ctx, char* buf, size_t cap) {
    psyvid__seqr* s = (psyvid__seqr*)ctx;
    return snprintf(buf, cap, "frame sequence %s", s->compression == PSYVID_SEQ_QOI ? "QOI" : "RAW");
}

static const psyvid_decoder psyvid__seq_decoder = {
    PSYVID_DECODER_VERSION, "seq", psyvid__seqr_open, psyvid__seqr_next, psyvid__seqr_seek,
    psyvid__seqr_close, psyvid__seqr_describe
};

/* --- pl_mpeg backend ---------------------------------------------------------- */

#ifndef PSYVID_NO_PL_MPEG

typedef struct psyvid__plm {
    plm_t*        plm;
    psyvid__src   src;
    int64_t       read_pos;
    plm_frame_t*  pending;          /* the keyframe a seek decoded            */
    int32_t       num, den, w, h;
} psyvid__plm;

static void psyvid__plm_rate(double fps, int32_t* num, int32_t* den) {
    static const struct { double f; int32_t n, d; } t[] = {
        { 23.976, 24000, 1001 }, { 24.0, 24, 1 }, { 25.0, 25, 1 }, { 29.97, 30000, 1001 },
        { 30.0, 30, 1 }, { 50.0, 50, 1 }, { 59.94, 60000, 1001 }, { 60.0, 60, 1 } };
    size_t i;
    *num = 0; *den = 0;
    for (i = 0; i < sizeof t / sizeof t[0]; i++)
        if (fabs(fps - t[i].f) < 0.002) { *num = t[i].n; *den = t[i].d; }
}

#if !defined(PSYVID_PL_MPEG_EXTERNAL)
/* Our own source behind pl_mpeg's buffer: 64-bit offsets (pl_mpeg's file
 * buffer uses ftell, 32 bits on Windows) and the reader. The read bytes are
 * discarded before each load, as pl_mpeg's own file buffer does, so the
 * buffer does not grow. */
static void psyvid__plm_bload(plm_buffer_t* b, void* user) {
    psyvid__plm* p = (psyvid__plm*)user;
    uint8_t chunk[16384];
    int64_t n;
    plm_buffer_discard_read_bytes(b);
    n = p->src.size - p->read_pos;
    if (n > (int64_t)sizeof chunk) n = (int64_t)sizeof chunk;
    if (n > (int64_t)(b->capacity - b->length)) n = (int64_t)(b->capacity - b->length);
    if (n <= 0 || psyvid__src_read(&p->src, p->read_pos, chunk, n) != PSYVID_OK) { b->has_ended = TRUE; return; }
    plm_buffer_write(b, chunk, (size_t)n);
    p->read_pos += n;
}
static void psyvid__plm_bseek(plm_buffer_t* b, size_t off, void* user) {
    psyvid__plm* p = (psyvid__plm*)user;
    (void)b;
    p->read_pos = (int64_t)off;
}
static size_t psyvid__plm_btell(plm_buffer_t* b, void* user) {
    psyvid__plm* p = (psyvid__plm*)user;
    (void)b;
    return (size_t)p->read_pos;
}
#endif

static int psyvid__plm_open(void* ctx, const psyvid_decoder_open* in, psyvid_stream* out, char* err, size_t cap) {
    psyvid__plm* p = (psyvid__plm*)ctx;
    int rc;
    memset(p, 0, sizeof *p);
    if (in->data) {
        p->plm = plm_create_with_memory((uint8_t*)(uintptr_t)in->data, in->size, 0);
    } else {
#if !defined(PSYVID_PL_MPEG_EXTERNAL)
        plm_buffer_t* b;
        rc = psyvid__src_open(&p->src, in->path, NULL, 0, in->reader, in->reader_ctx);
        if (rc < 0) { psyvid__fmt(err, cap, "cannot open %s", in->path ? in->path : "the reader"); return rc; }
        b = plm_buffer_create_with_callbacks(psyvid__plm_bload, psyvid__plm_bseek, psyvid__plm_btell, (size_t)p->src.size, p);
        if (b) p->plm = plm_create_with_buffer(b, TRUE);
#else
        if (!in->path) { psyvid__fmt(err, cap, "pl_mpeg is external: a path or memory only"); return PSYVID_ERR_NOT_IMPLEMENTED; }
        p->plm = plm_create_with_filename(in->path);
#endif
    }
    if (!p->plm) { psyvid__fmt(err, cap, "pl_mpeg could not open the stream"); return PSYVID_ERR_DECODER; }
    plm_set_audio_enabled(p->plm, FALSE);
    if (!plm_has_headers(p->plm) || plm_get_num_video_streams(p->plm) < 1) {
        psyvid__fmt(err, cap, "pl_mpeg found no MPEG-1 video stream (MPEG-PS with a video stream is required)");
        return PSYVID_ERR_FORMAT;
    }
#if !defined(PSYVID_PL_MPEG_EXTERNAL)
    /* The canonical form has no B-frames, so decode order is display order:
     * each frame comes out as it is decoded, not one call later, and its
     * picture type is the one just decoded (the index maker reads it). */
    if (p->plm->video_decoder) plm_video_set_no_delay(p->plm->video_decoder, TRUE);
#endif
    p->w = plm_get_width(p->plm);
    p->h = plm_get_height(p->plm);
    psyvid__plm_rate(plm_get_framerate(p->plm), &p->num, &p->den);
    out->w = p->w; out->h = p->h; out->format = PSYVID_FMT_I420;
    out->fps_num = p->num; out->fps_den = p->den;
    out->frames = -1; out->gop = 0; out->codec = PSYVID_CODEC_MPEG1;
    out->matrix = out->range = out->transfer = out->primaries = out->siting = -1;
    out->timescale = 90000;
    out->caps = PSYVID_DEC_CPU;
    return PSYVID_OK;
}

static int psyvid__plm_next(void* ctx, psyvid_planes* dst, psyvid_out* out) {
    psyvid__plm* p = (psyvid__plm*)ctx;
    plm_frame_t* f = p->pending;
    p->pending = NULL;
    if (!f) f = plm_decode_video(p->plm);
    if (!f) return plm_has_ended(p->plm) ? PSYVID_ENDED : PSYVID_ERR_DECODER;
    out->pts = (int64_t)floor(f->time * 90000.0 + 0.5);
    out->index = -1;
    out->flags = PSYVID_OUT_BORROWED;
    out->planes.data[0] = f->y.data;  out->planes.stride[0] = (int32_t)f->y.width;
    out->planes.data[1] = f->cb.data; out->planes.stride[1] = (int32_t)f->cb.width;
    out->planes.data[2] = f->cr.data; out->planes.stride[2] = (int32_t)f->cr.width;
    out->planes.w[0] = p->w; out->planes.h[0] = p->h;
    out->planes.w[1] = out->planes.w[2] = (p->w + 1) / 2;
    out->planes.h[1] = out->planes.h[2] = (p->h + 1) / 2;
    (void)dst;
    return PSYVID_OK;
}

static int psyvid__plm_seek(void* ctx, int64_t key, int64_t key_pts) {
    psyvid__plm* p = (psyvid__plm*)ctx;
    double t;
    (void)key_pts;
    if (p->num <= 0) return PSYVID_ERR_ARG;
    /* half a frame past the keyframe: pl_mpeg takes the last intra frame
     * before the time, and the PES time is in 90 kHz units */
    t = ((double)key + 0.5) * (double)p->den / (double)p->num;
    p->pending = plm_seek_frame(p->plm, t, FALSE);
    return p->pending ? PSYVID_OK : PSYVID_ERR_DECODER;
}

static void psyvid__plm_close(void* ctx) {
    psyvid__plm* p = (psyvid__plm*)ctx;
    if (p->plm) plm_destroy(p->plm);
    psyvid__src_close(&p->src);
    p->plm = NULL;
}

static int psyvid__plm_describe(void* ctx, char* buf, size_t cap) {
    (void)ctx;
    return snprintf(buf, cap, "pl_mpeg c871f2b (MPEG-1, CPU)");
}

static const psyvid_decoder psyvid__plm_decoder = {
    PSYVID_DECODER_VERSION, "pl_mpeg", psyvid__plm_open, psyvid__plm_next, psyvid__plm_seek,
    psyvid__plm_close, psyvid__plm_describe
};

#endif /* PSYVID_NO_PL_MPEG */

PSYVID_API int64_t psyvid_index_make(const char* media_path, const char* index_path,
                                     const psyvid_index_desc* d, char* err, size_t cap) {
#if defined(PSYVID_NO_PL_MPEG) || defined(PSYVID_PL_MPEG_EXTERNAL)
    (void)media_path; (void)index_path; (void)d;
    (void)&psyvid__index_header;   /* the writer is unused in this build */
    psyvid__fmt(err, cap, "psyvid_index_make needs pl_mpeg compiled into this file");
    return PSYVID_ERR_NOT_IMPLEMENTED;
#else
    psyvid__plm p;
    psyvid_stream st;
    psyvid_decoder_open in;
    psyvid__canon c;
    psyvid__src s;
    uint64_t* hashes = NULL;
    int64_t n = 0, cap_h = 4096, last_key = -1, gop = 0;
    uint64_t head = 0, tail = 0;
    char e[256];
    int rc;
    FILE* f;
    uint8_t hdr[PSYVID__IDX_HDR];
    char ip[1024];
    if (!media_path) { psyvid__fmt(err, cap, "no media path"); return PSYVID_ERR_ARG; }
    memset(&in, 0, sizeof in);
    memset(&st, 0, sizeof st);
    in.path = media_path;
    rc = psyvid__plm_open(&p, &in, &st, e, sizeof e);
    if (rc < 0) { psyvid__fmt(err, cap, "%s: %s", media_path, e); psyvid__plm_close(&p); return rc; }
    if (st.fps_num <= 0) { psyvid__fmt(err, cap, "%s: rate %.3f is not one MPEG-1 has", media_path, plm_get_framerate(p.plm)); psyvid__plm_close(&p); return PSYVID_ERR_FORMAT; }
    hashes = (uint64_t*)psyvid__malloc((size_t)cap_h * 8);
    if (!hashes) { psyvid__plm_close(&p); return PSYVID_ERR_FULL; }
    for (;;) {
        psyvid_out o;
        int64_t idx;
        int type;
        memset(&o, 0, sizeof o);
        rc = psyvid__plm_next(&p, NULL, &o);
        if (rc == PSYVID_ENDED) break;
        if (rc < 0) { psyvid__fmt(err, cap, "%s: decode failed at frame %lld", media_path, (long long)n); goto fail; }
        type = p.plm->video_decoder->picture_type;
        if (type == 3) { psyvid__fmt(err, cap, "%s: B-frames (frame %lld); re-encode with -bf 0", media_path, (long long)n); rc = PSYVID_ERR_FORMAT; goto fail; }
        if (type == 1) {
            if (last_key >= 0) {
                if (gop == 0) gop = n - last_key;
                else if (n - last_key != gop) { psyvid__fmt(err, cap, "%s: GOP length changes at frame %lld (%lld, was %lld); re-encode with a fixed -g and -sc_threshold 0", media_path, (long long)n, (long long)(n - last_key), (long long)gop); rc = PSYVID_ERR_FORMAT; goto fail; }
            } else if (n != 0) { psyvid__fmt(err, cap, "%s: the first frame is not a keyframe", media_path); rc = PSYVID_ERR_FORMAT; goto fail; }
            last_key = n;
        } else if (n == 0) { psyvid__fmt(err, cap, "%s: the first frame is not a keyframe", media_path); rc = PSYVID_ERR_FORMAT; goto fail; }
        idx = (o.pts * st.fps_num + (int64_t)st.fps_den * 45000) / ((int64_t)st.fps_den * 90000);
        if (idx != n) { psyvid__fmt(err, cap, "%s: frame %lld has the time of frame %lld: not a constant rate", media_path, (long long)n, (long long)idx); rc = PSYVID_ERR_FORMAT; goto fail; }
        if (n >= cap_h) {
            uint64_t* nh = (uint64_t*)psyvid__realloc(hashes, (size_t)cap_h * 2 * 8);
            if (!nh) { rc = PSYVID_ERR_FULL; goto fail; }
            hashes = nh; cap_h *= 2;
        }
        hashes[n] = psyvid__hash_planes(PSYVID_FMT_I420, &o.planes);
        n++;
    }
    if (n == 0) { psyvid__fmt(err, cap, "%s: no frames", media_path); rc = PSYVID_ERR_FORMAT; goto fail; }
    if (gop == 0) gop = last_key == 0 && n > 0 ? n : 1;
    memset(&c, 0, sizeof c);
    c.codec = PSYVID_CODEC_MPEG1; c.w = st.w; c.h = st.h; c.format = PSYVID_FMT_I420;
    c.fps_num = st.fps_num; c.fps_den = st.fps_den; c.frames = n; c.gop = (int32_t)gop;
    c.matrix = d && d->matrix ? d->matrix : PSYVID_MATRIX_BT601;
    c.range = d && d->range ? d->range : PSYVID_RANGE_LIMITED;
    c.transfer = d && d->transfer ? d->transfer : PSYVID_TRC_BT1886;
    c.primaries = d && d->primaries ? d->primaries : PSYVID_PRIM_BT709;
    c.siting = d && d->siting ? d->siting : PSYVID_SITING_CENTER;
    rc = psyvid__canon_check(&c, e, sizeof e);
    if (rc < 0) { psyvid__fmt(err, cap, "%s: %s", media_path, e); goto fail; }
    psyvid__plm_close(&p);
    rc = psyvid__src_open(&s, media_path, NULL, 0, NULL, NULL);
    if (rc == PSYVID_OK) { rc = psyvid__src_ends(&s, &head, &tail); c.frames = n; }
    {
        int64_t size = s.size;
        psyvid__src_close(&s);
        if (rc < 0) { psyvid__fmt(err, cap, "%s: cannot read it again", media_path); psyvid__free(hashes); return rc; }
        psyvid__index_header(hdr, &c, size, head, tail);
    }
    if (!index_path) { psyvid__fmt(ip, sizeof ip, "%s.psyvi", media_path); index_path = ip; }
    f = psyvid__fopen(index_path, "wb");
    if (!f) { psyvid__fmt(err, cap, "cannot create %s", index_path); psyvid__free(hashes); return PSYVID_ERR_IO; }
    rc = fwrite(hdr, 1, sizeof hdr, f) == sizeof hdr ? PSYVID_OK : PSYVID_ERR_IO;
    {
        int64_t i;
        uint8_t b8[8];
        for (i = 0; i < n && rc == PSYVID_OK; i++) {
            psyvid__w64(b8, hashes[i]);
            if (fwrite(b8, 1, 8, f) != 8) rc = PSYVID_ERR_IO;
        }
    }
    if (rc == PSYVID_OK && d && d->note) {
        size_t ln = strlen(d->note);
        if (fwrite(d->note, 1, ln, f) != ln) rc = PSYVID_ERR_IO;
    }
    if (fclose(f) != 0) rc = PSYVID_ERR_IO;
    psyvid__free(hashes);
    if (rc < 0) { psyvid__fmt(err, cap, "cannot write %s", index_path); return rc; }
    return n;
fail:
    psyvid__plm_close(&p);
    psyvid__free(hashes);
    return rc < 0 ? rc : PSYVID_ERR_FORMAT;
#endif
}

/* --- the movie: helpers ----------------------------------------------------------- */

static void psyvid__record_push(psyvid_movie* mv, const psyvid_record* r);

static void psyvid__err(psyvid_movie* mv, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(mv->error, sizeof mv->error, fmt, ap);
    va_end(ap);
}

static void psyvid__push(psyvid_movie* mv, uint16_t kind, int64_t t, const psyrt_payload* u) {
    psyrt_event ev;
    if (!mv->d.ring) return;
    memset(&ev, 0, sizeof ev);
    ev.t_ns = (uint64_t)(t > 0 ? t : 1);
    ev.source = (uint16_t)PSYRT_SRC_VIDEO;
    ev.kind = kind;
    ev.aux = mv->d.movie_index;
    ev.u = *u;
    psyrt_ring_push(mv->d.ring, &ev);
}

static void psyvid__result_put(psyvid_movie* mv, int64_t id, uint16_t kind, int64_t t, const psyrt_payload* u) {
    psyvid__result* r = &mv->results[mv->n_results % PSYVID__RESULTS];
    memset(r, 0, sizeof *r);
    r->id = id;
    r->ev.t_ns = (uint64_t)(t > 0 ? t : 1);
    r->ev.source = (uint16_t)PSYRT_SRC_VIDEO;
    r->ev.kind = kind;
    r->ev.aux = mv->d.movie_index;
    r->ev.u = *u;
    mv->n_results++;
    psyvid__push(mv, kind, t, u);
}

static int64_t psyvid__ft(const psyvid_movie* mv, int64_t i) {
    return psyvid_frame_time(mv->info.fps_num, mv->info.fps_den, i);
}

static int64_t psyvid__dueg(const psyvid_movie* mv, int64_t m, int64_t lead_ns) {
    return psyvid_due(mv->info.fps_num, mv->info.fps_den, m, lead_ns);
}

/* The movie time at RT time t, in the current cycle. With a timeline the
 * movie base is the clock (a later base rate needs no change here). */
static int64_t psyvid__movie_time(const psyvid_movie* mv, int64_t t) {
    int64_t m;
    if (mv->d.timeline && psytl_base_time(mv->d.timeline, mv->d.base, t, &m)) return m;
    return mv->running ? mv->anchor_mt + (t - mv->anchor_rt) : mv->anchor_mt;
}

/* The movie base's rate, num/den. psy_timeline.h has rate 1 only today;
 * the soundtrack check below is written against this. */
static void psyvid__base_rate(const psyvid_movie* mv, int64_t* num, int64_t* den) {
    (void)mv;
    *num = 1; *den = 1;
}

static void psyvid__anchor(psyvid_movie* mv, int64_t rt, int64_t mt) {
    mv->anchor_rt = rt;
    mv->anchor_mt = mt;
    mv->running = 1;
    mv->anchor_epoch++;
    if (mv->d.timeline) psytl_anchor(mv->d.timeline, mv->d.base, rt, mt);
}

/* A seek's anchor: the annotations it passes over are skipped, not fired
 * late on one frame (psytl_skip). Returns how many were skipped. */
static int psyvid__seek_anchor(psyvid_movie* mv, int64_t rt, int64_t mt) {
    int n = 0;
    mv->anchor_rt = rt;
    mv->anchor_mt = mt;
    mv->running = 1;
    mv->anchor_epoch++;
    if (mv->d.timeline) {
        n = psytl_skip(mv->d.timeline, mv->d.base, rt, mt);
        if (n < 0) n = 0;
        mv->info.skipped += (uint64_t)n;
    }
    return n;
}

static void psyvid__pause_clock(psyvid_movie* mv, int64_t rt) {
    if (mv->running) mv->anchor_mt = mv->anchor_mt + (rt - mv->anchor_rt);
    mv->anchor_rt = rt;
    mv->running = 0;
    if (mv->d.timeline) {
        int64_t m;
        if (psytl_base_time(mv->d.timeline, mv->d.base, rt, &m)) mv->anchor_mt = m;
        psytl_pause(mv->d.timeline, mv->d.base, rt);
    }
}

/* Anchors and pauses at mt: manual mode and a seek that stays paused. A
 * jump skips the annotations it passes over; a step to the next frame
 * fires them, on the frame that shows it. Returns how many were skipped. */
static int psyvid__hold_at(psyvid_movie* mv, int64_t rt, int64_t mt, int jump) {
    int n = 0;
    mv->anchor_rt = rt;
    mv->anchor_mt = mt;
    mv->running = 0;
    mv->anchor_epoch++;
    if (mv->d.timeline) {
        if (jump) {
            n = psytl_skip(mv->d.timeline, mv->d.base, rt, mt);
            if (n < 0) n = 0;
            mv->info.skipped += (uint64_t)n;
        } else {
            psytl_anchor(mv->d.timeline, mv->d.base, rt, mt);
        }
        psytl_pause(mv->d.timeline, mv->d.base, rt);
    }
    return n;
}

/* --- queues ------------------------------------------------------------------------ */

static void psyvid__free_push(psyvid_movie* mv, uint32_t s) {
    uint32_t t = mv->free_tail;
    mv->free_q[t & (PSYVID__QCAP - 1)] = s;
    psyvid__st32(&mv->free_tail, t + 1);
}
static int psyvid__free_pop(psyvid_movie* mv) {
    uint32_t h = mv->free_head;
    int s;
    if (h == psyvid__ld32(&mv->free_tail)) return -1;
    s = (int)mv->free_q[h & (PSYVID__QCAP - 1)];
    psyvid__st32(&mv->free_head, h + 1);
    return s;
}
static void psyvid__ready_push(psyvid_movie* mv, uint32_t s) {
    uint32_t t = mv->ready_tail;
    mv->ready_q[t & (PSYVID__QCAP - 1)] = s;
    psyvid__st32(&mv->ready_tail, t + 1);
}

/* --- decode thread ---------------------------------------------------------------- */

static void psyvid__dt_fail(psyvid_movie* mv, int rc, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(mv->dt_error, sizeof mv->dt_error, fmt, ap);
    va_end(ap);
    psyvid__st32(&mv->dec_err, (uint32_t)(-rc));
}

static void psyvid__on_msg(void* ctx, const void* msg, uint32_t seq) {
    psyvid_movie* mv = (psyvid_movie*)ctx;
    psyvid__msg m;
    (void)seq;
    memcpy(&m, msg, sizeof m);   /* the pump's slot has no alignment promise */
    if (m.op == PSYVID__MSG_SEEK) {
        mv->dt_epoch = m.epoch;
        mv->dt_next = m.g;
        mv->dt_eos = 0;
        mv->dt_key = -1;
        mv->dt_discarded = 0;
    }
}

/* One step: discard one frame on the way to the target, or decode one into a
 * free slot. True: there is more to do now. */
static bool psyvid__dstep(psyvid_movie* mv) {
    const int64_t N = mv->info.frames;
    int64_t want, g, idx;
    int s, rc;
    psyvid_out out;
    psyvid__slot* sl;
    if (psyvid__ld32(&mv->dec_err) || mv->dt_eos) return false;
    want = psyvid__ld64(&mv->want_g);
    g = mv->dt_next > want ? mv->dt_next : want;
    if (!mv->d.loop && g >= N) { mv->dt_eos = 1; return false; }
    idx = g % N;
    if (mv->dt_pos != idx) {
        int64_t key = (mv->dec_caps & PSYVID_DEC_RANDOM_ACCESS) ? idx : idx - idx % mv->info.gop;
        if (mv->dt_pos < 0 || mv->dt_pos > idx || mv->dt_pos < key) {
            PSYRT_ZONE(zs, "psyvid.seek");
            rc = mv->dec->seek(mv->dec_ctx, key, mv->timescale > 0 ? psyvid__ft(mv, key) / 1000 * mv->timescale / 1000000 : key);
            PSYRT_ZONE_END(zs);
            if (rc < 0) { psyvid__dt_fail(mv, PSYVID_ERR_DECODER, "psy_video: %s: seek to keyframe %lld failed (%d)", mv->dec->name, (long long)key, rc); return false; }
            mv->dt_pos = key;
            mv->dt_key = key;
        }
        if (mv->dt_pos < idx) {
            memset(&out, 0, sizeof out);
            out.index = -1;
            rc = mv->dec->next(mv->dec_ctx, NULL, &out);
            if (rc == PSYVID_PENDING) return false;
            if (rc != PSYVID_OK) { psyvid__dt_fail(mv, PSYVID_ERR_DECODER, "psy_video: %s: the stream ended or failed at frame %lld while seeking (%d)", mv->dec->name, (long long)mv->dt_pos, rc); return false; }
            mv->dt_pos++;
            mv->dt_discarded++;
            mv->dt_next = g;
            return true;
        }
    }
    s = psyvid__free_pop(mv);
    if (s < 0) {
        if (mv->inline_mode) return false;
        psyvid__xchg32(&mv->idle, 1);
        s = psyvid__free_pop(mv);
        if (s < 0) return false;
        psyvid__xchg32(&mv->idle, 0);
    }
    sl = &mv->slots[s];
    memset(&out, 0, sizeof out);
    out.index = -1;
    {
        psyvid_planes dst;
        int64_t t0 = PSYVID__NOW(), dt;
        uint8_t* base = psyvid__is_yuv(mv->info.format) ? mv->yuv : sl->data;
        PSYRT_ZONE(zd, "psyvid.decode");
        psyvid__planes_layout(mv->info.format, mv->info.w, mv->info.h, base, &dst);
        rc = mv->dec->next(mv->dec_ctx, &dst, &out);
        dt = PSYVID__NOW() - t0;
        PSYRT_ZONE_END(zd);
        if (rc == PSYVID_PENDING) { psyvid__free_push(mv, (uint32_t)s); return false; }
        if (rc == PSYVID_ENDED) {
            psyvid__free_push(mv, (uint32_t)s);
            psyvid__dt_fail(mv, PSYVID_ERR_DECODER, "psy_video: %s: the stream ended at frame %lld; the index says %lld frames", mv->dec->name, (long long)idx, (long long)N);
            return false;
        }
        if (rc < 0) {
            psyvid__free_push(mv, (uint32_t)s);
            psyvid__dt_fail(mv, PSYVID_ERR_DECODER, "psy_video: %s: decode of frame %lld failed (%d)", mv->dec->name, (long long)idx, rc);
            return false;
        }
        if (dt > psyvid__ft(mv, 1) && mv->d.ring) {
            psyrt_payload u;
            memset(&u, 0, sizeof u);
            u.i64[0] = dt; u.i64[1] = idx;
            psyvid__push(mv, (uint16_t)PSYVID_EV_DECODE, t0, &u);
        }
        sl->flags = 0;
        {
            int64_t got = out.index;
            if (got < 0 && mv->timescale > 0) {
                /* round(pts * num / (den * timescale)), in two steps so a long
                 * stream's pts cannot overflow the product */
                int64_t d = (int64_t)mv->info.fps_den * mv->timescale;
                int64_t a = psyvid__floordiv(out.pts, d), b = out.pts - a * d;
                got = a * mv->info.fps_num + (b * mv->info.fps_num + d / 2) / d;
            }
            if (got != idx) sl->flags |= PSYVID_F_TS_MISMATCH;
        }
        {
            const psyvid_planes* src = (out.flags & PSYVID_OUT_BORROWED) ? &out.planes : &dst;
            uint64_t want_h = 0;
            int have = 0;
            if (out.flags & PSYVID_OUT_HAS_HASH) { want_h = out.hash; have = 1; }
            else if (mv->hashes) { want_h = mv->hashes[idx]; have = 1; }
            if (have && psyvid__hash_planes(mv->info.format, src) != want_h) sl->flags |= PSYVID_F_HASH_MISMATCH;
            if (psyvid__is_yuv(mv->info.format)) {
                psyvid_yuv_to_rgba(src, mv->info.format, mv->info.w, mv->info.h, mv->info.matrix, mv->info.range,
                                   mv->info.siting, mv->d.chroma, sl->data, mv->info.w * 4, mv->yuv_rows);
            } else if (out.flags & PSYVID_OUT_BORROWED) {
                int y;
                size_t rb = (size_t)mv->info.w * (size_t)psyvid__bpt(mv->info.format);
                for (y = 0; y < mv->info.h; y++)
                    memcpy(sl->data + (size_t)y * rb, out.planes.data[0] + (size_t)y * (size_t)out.planes.stride[0], rb);
            }
        }
    }
    mv->dt_pos = idx + 1;
    sl->g = g;
    sl->epoch = mv->dt_epoch;
    sl->key = mv->dt_key;
    sl->discarded = mv->dt_discarded;
    mv->dt_key = -1;
    mv->dt_discarded = 0;
    mv->dt_next = g + 1;
    psyvid__ready_push(mv, (uint32_t)s);
    return true;
}

#if !defined(PSYRT_NO_THREADS)
static bool psyvid__on_idle(void* ctx) {
    psyvid_movie* mv = (psyvid_movie*)ctx;
    return psyvid__dstep(mv);
}

static bool psyvid__on_start(void* ctx, char* err, size_t cap) {
    (void)ctx; (void)err; (void)cap;
    PSYRT_THREAD_NAME("psy_video decode");
    return true;
}
#endif

/* Hands a message to the decode thread, or runs it here inline. */
static int psyvid__post(psyvid_movie* mv, uint32_t op, int64_t g) {
    psyvid__msg m;
    m.op = op; m.epoch = mv->ft_epoch; m.g = g;
    if (mv->inline_mode) { psyvid__on_msg(mv, &m, 0); return PSYVID_OK; }
#if !defined(PSYRT_NO_THREADS)
    return psyrt_pump_submit(&mv->pump, &m) < 0 ? PSYVID_ERR_FULL : PSYVID_OK;
#else
    return PSYVID_ERR_ARG;
#endif
}

static void psyvid__wake(psyvid_movie* mv) {
    if (mv->inline_mode) return;
    if (psyvid__xchg32(&mv->idle, 0) == 1) psyvid__post(mv, PSYVID__MSG_WAKE, 0);
}

/* --- open ---------------------------------------------------------------------- */

static int psyvid__display_rate(psyvid_movie* mv, int32_t* num, int32_t* den) {
    *num = mv->d.refresh_num; *den = mv->d.refresh_den > 0 ? mv->d.refresh_den : 1;
    if (*num > 0) return 1;
    if (mv->screen && psyscr_is_open(mv->screen)) {
        psyscr_caps c;
        psyscr_get_caps(mv->screen, &c);
        if (c.mode.refresh_num > 0 && c.mode.refresh_den > 0) { *num = c.mode.refresh_num; *den = c.mode.refresh_den; return 1; }
        if (c.period_ns > 0) { *num = 1000000000; *den = (int32_t)c.period_ns; return 1; }
    }
    *num = 0; *den = 0;
    return 0;
}

static int psyvid__backend_for(const psyvid_desc* d) {
    uint8_t b[8];
    psyvid__src s;
    if (d->backend != PSYVID_BACKEND_AUTO) return d->backend;
    if (d->decoder) return PSYVID_BACKEND_CUSTOM;
    if (psyvid__src_open(&s, d->path, d->data, d->size, d->reader, d->reader_ctx) != PSYVID_OK) return -1;
    memset(b, 0, sizeof b);
    if (s.size >= 8) psyvid__src_read(&s, 0, b, 8);
    psyvid__src_close(&s);
    if (memcmp(b, "PSYVSEQ1", 8) == 0) return PSYVID_BACKEND_SEQ;
    if (b[0] == 0 && b[1] == 0 && b[2] == 1 && (b[3] == 0xBA || b[3] == 0xB3)) return PSYVID_BACKEND_PLMPEG;
    return PSYVID_BACKEND_MF;
}

PSYVID_API bool psyvid_probe(const psyvid_desc* d, psyvid_info* out, char* err, size_t cap) {
    psyvid__canon c;
    int b;
    if (!d || !out) { psyvid__fmt(err, cap, "no desc"); return false; }
    memset(out, 0, sizeof *out);
    b = psyvid__backend_for(d);
    if (b == PSYVID_BACKEND_SEQ) {
        psyvid__src s;
        uint8_t hdr[128];
        int32_t comp;
        int64_t io;
        if (psyvid__src_open(&s, d->path, d->data, d->size, d->reader, d->reader_ctx) != PSYVID_OK ||
            psyvid__src_read(&s, 0, hdr, sizeof hdr) != PSYVID_OK) { psyvid__src_close(&s); psyvid__fmt(err, cap, "cannot read the file"); return false; }
        psyvid__src_close(&s);
        if (psyvid__seq_parse(hdr, &c, &comp, &io, err, cap) < 0) return false;
    } else if (b == PSYVID_BACKEND_PLMPEG) {
        psyvid__index ix;
        if (psyvid__index_load(d, &ix, 0, err, cap) < 0) return false;
        c = ix.c;
    } else {
        psyvid__fmt(err, cap, "psyvid_probe reads frame sequences and indexed MPEG-1 files only");
        return false;
    }
    out->w = c.w; out->h = c.h; out->fps_num = c.fps_num; out->fps_den = c.fps_den;
    out->frames = c.frames; out->gop = c.gop; out->codec = c.codec; out->format = c.format;
    out->matrix = c.matrix; out->range = c.range; out->transfer = c.transfer; out->primaries = c.primaries; out->siting = c.siting;
    out->duration = psyvid_frame_time(c.fps_num, c.fps_den, c.frames);
    out->backend = (psyvid_backend)b;
    return true;
}

static int psyvid__fill(int32_t* have, int32_t from_index, const char* what, char* err, size_t cap) {
    if (*have <= 0) { *have = from_index; return PSYVID_OK; }
    if (from_index > 0 && *have != from_index) {
        psyvid__fmt(err, cap, "the stream says %s %d, the index says %d; the index is stale: make it again", what, (int)*have, (int)from_index);
        return PSYVID_ERR_FORMAT;
    }
    return PSYVID_OK;
}

static void psyvid__close_partial(psyvid_movie* mv) {
#if !defined(PSYRT_NO_THREADS)
    if (!mv->inline_mode) psyrt_pump_stop(&mv->pump);
#endif
    if (mv->dec && mv->dec_ctx && mv->dec->close) mv->dec->close(mv->dec_ctx);
    if (mv->gfx && mv->tex.id) psygfx_texture_free(mv->gfx, mv->tex);
    psyvid__free(mv->be_mem);
    psyvid__free(mv->hashes);
    psyvid__free(mv->mem_raw);
    psyvid__free(mv->yuv);
    psyvid__free(mv->yuv_rows);
    mv->be_mem = NULL; mv->hashes = NULL; mv->mem_raw = NULL; mv->yuv = NULL; mv->yuv_rows = NULL;
    mv->dec = NULL; mv->dec_ctx = NULL;
    mv->tex.id = 0;
}

PSYVID_API bool psyvid_open(psyvid_movie* mv, psygfx_gfx* g, const psyvid_desc* desc) {
    psyvid_decoder_open in;
    psyvid_stream st;
    psyvid__canon c;
    psyvid__index ix;
    char e[384];
    int backend, rc, i;
    int32_t rn, rd;
    if (!mv) return false;
    if (mv->open) { psyvid__err(mv, "psy_video: already open"); return false; }
    memset(mv, 0, sizeof *mv);
    if (!desc) { psyvid__err(mv, "psy_video: no desc"); return false; }
    mv->d = *desc;
    mv->gfx = g;
    mv->screen = g ? g->screen : NULL;
    if (!(desc->lead == PSYVID_LEAD_NONE || (desc->lead >= 0.0 && desc->lead < 1.0))) { psyvid__err(mv, "psy_video: desc.lead must be in [0, 1) or PSYVID_LEAD_NONE"); return false; }
    mv->info.lead = desc->lead == 0.0 ? 0.5 : desc->lead == PSYVID_LEAD_NONE ? 0.0 : desc->lead;
    if (desc->timeline) {
        /* one rule for video and annotations: the timeline's own lead */
        double tl_lead = psytl_lead(desc->timeline);
        if (!psytl_is_open(desc->timeline)) { psyvid__err(mv, "psy_video: desc.timeline is not open"); return false; }
        if (desc->base < 1 || desc->base >= PSYTL_MAX_BASES) { psyvid__err(mv, "psy_video: desc.base must be 1..%d with a timeline", PSYTL_MAX_BASES - 1); return false; }
        if (desc->lead != 0.0 && mv->info.lead != tl_lead) { psyvid__err(mv, "psy_video: desc.lead (%g) differs from the timeline's (%g): video and annotations must share one rule", mv->info.lead, tl_lead); return false; }
        mv->info.lead = tl_lead;
    }
    if (desc->ahead < 0 || desc->ahead > PSYVID_MAX_SLOTS - 2) { psyvid__err(mv, "psy_video: desc.ahead must be 0..%d", PSYVID_MAX_SLOTS - 2); return false; }
    if (desc->gpu_path == PSYVID_PATH_SHARED || desc->gpu_path == PSYVID_PATH_ZERO_COPY) {
        psyvid__err(mv, "psy_video: the shared and zero-copy GPU paths are not in v0.1: they need psyscr_native() from psy_screen.h and texture import from psy_gfx.h (docs/psy_video.md)");
        return false;
    }
    if (desc->light == PSYVID_LIGHT_EOTF) {
        psyvid__err(mv, "psy_video: light EOTF needs psy_gfx.h's planar YUV formats, not in v0.1; use CODES");
        return false;
    }
    backend = psyvid__backend_for(desc);
    if (backend < 0) { psyvid__err(mv, "psy_video: cannot open %s", desc->path ? desc->path : "the source"); return false; }
    if (backend == PSYVID_BACKEND_MF || backend == PSYVID_BACKEND_AVF || backend == PSYVID_BACKEND_FFMPEG) {
        psyvid__err(mv, "psy_video: %s is not a frame sequence or MPEG-1; H.264 and HEVC need the Media Foundation, AVFoundation or FFmpeg backend, not in v0.1", desc->path ? desc->path : "the source");
        return false;
    }
    if (backend == PSYVID_BACKEND_CUSTOM) {
        if (!desc->decoder || desc->decoder->version != PSYVID_DECODER_VERSION || !desc->decoder->open ||
            !desc->decoder->next || !desc->decoder->seek) { psyvid__err(mv, "psy_video: desc.decoder is missing or of another version"); return false; }
        mv->dec = desc->decoder;
        mv->dec_ctx = desc->decoder_ctx;
    } else if (backend == PSYVID_BACKEND_SEQ) {
        mv->be_mem = psyvid__malloc(sizeof(psyvid__seqr));
        if (!mv->be_mem) { psyvid__err(mv, "psy_video: out of memory"); return false; }
        memset(mv->be_mem, 0, sizeof(psyvid__seqr));
        mv->dec = &psyvid__seq_decoder;
        mv->dec_ctx = mv->be_mem;
    } else if (backend == PSYVID_BACKEND_PLMPEG) {
#ifdef PSYVID_NO_PL_MPEG
        psyvid__err(mv, "psy_video: MPEG-1 needs pl_mpeg; this build has PSYVID_NO_PL_MPEG");
        return false;
#else
        mv->be_mem = psyvid__malloc(sizeof(psyvid__plm));
        if (!mv->be_mem) { psyvid__err(mv, "psy_video: out of memory"); return false; }
        memset(mv->be_mem, 0, sizeof(psyvid__plm));
        mv->dec = &psyvid__plm_decoder;
        mv->dec_ctx = mv->be_mem;
#endif
    } else {
        psyvid__err(mv, "psy_video: unknown backend %d", backend);
        return false;
    }
    mv->info.backend = (psyvid_backend)backend;
    memset(&in, 0, sizeof in);
    in.path = desc->path; in.data = desc->data; in.size = desc->size;
    in.reader = desc->reader; in.reader_ctx = desc->reader_ctx;
    in.index = desc->index; in.index_size = desc->index_size;
    memset(&st, 0, sizeof st);
    st.frames = -1;
    e[0] = 0;
    rc = mv->dec->open(mv->dec_ctx, &in, &st, e, sizeof e);
    if (rc < 0) {
        psyvid__err(mv, "psy_video: %s: %s", mv->dec->name, e[0] ? e : psyvid_strerror(rc));
        if (mv->dec->close) mv->dec->close(mv->dec_ctx);
        mv->dec = NULL;
        psyvid__close_partial(mv);
        return false;
    }
    /* the description: the stream's values, checked against the index's */
    memset(&c, 0, sizeof c);
    c.w = st.w; c.h = st.h; c.format = st.format; c.fps_num = st.fps_num; c.fps_den = st.fps_den;
    c.frames = st.frames; c.gop = st.gop; c.codec = st.codec;
    c.matrix = (uint8_t)(st.matrix > 0 ? st.matrix : 0); c.range = (uint8_t)(st.range > 0 ? st.range : 0);
    c.transfer = (uint8_t)(st.transfer > 0 ? st.transfer : 0); c.primaries = (uint8_t)(st.primaries > 0 ? st.primaries : 0);
    c.siting = (uint8_t)(st.siting > 0 ? st.siting : 0);
    memset(&ix, 0, sizeof ix);
    if (backend == PSYVID_BACKEND_PLMPEG || desc->index || (backend == PSYVID_BACKEND_CUSTOM && desc->path)) {
        int32_t v;
        rc = psyvid__index_load(desc, &ix, 1, e, sizeof e);
        if (rc < 0) { psyvid__err(mv, "psy_video: %s", e); psyvid__close_partial(mv); return false; }
        mv->hashes = ix.hashes;
        if (backend == PSYVID_BACKEND_PLMPEG || desc->path) {
            psyvid__src s;
            uint64_t head = 0, tail = 0;
            if (psyvid__src_open(&s, desc->path, desc->data, desc->size, desc->reader, desc->reader_ctx) == PSYVID_OK) {
                rc = s.size == ix.media_size ? psyvid__src_ends(&s, &head, &tail) : PSYVID_ERR_FORMAT;
                psyvid__src_close(&s);
                if (rc < 0 || head != ix.head || tail != ix.tail) {
                    psyvid__err(mv, "psy_video: the index does not belong to this file (size or hash of its ends differ); make it again");
                    psyvid__close_partial(mv);
                    return false;
                }
            }
        }
        rc = PSYVID_OK;
        if (rc == PSYVID_OK) rc = psyvid__fill(&c.w, ix.c.w, "width", e, sizeof e);
        if (rc == PSYVID_OK) rc = psyvid__fill(&c.h, ix.c.h, "height", e, sizeof e);
        if (rc == PSYVID_OK) rc = psyvid__fill(&c.format, ix.c.format, "format", e, sizeof e);
        if (rc == PSYVID_OK) rc = psyvid__fill(&c.fps_num, ix.c.fps_num, "rate numerator", e, sizeof e);
        if (rc == PSYVID_OK) rc = psyvid__fill(&c.fps_den, ix.c.fps_den, "rate denominator", e, sizeof e);
        if (rc == PSYVID_OK) rc = psyvid__fill(&c.gop, ix.c.gop, "GOP", e, sizeof e);
        if (rc == PSYVID_OK) rc = psyvid__fill(&c.codec, ix.c.codec, "codec", e, sizeof e);
        v = c.matrix; if (rc == PSYVID_OK) rc = psyvid__fill(&v, ix.c.matrix, "matrix", e, sizeof e); c.matrix = (uint8_t)v;
        v = c.range; if (rc == PSYVID_OK) rc = psyvid__fill(&v, ix.c.range, "range", e, sizeof e); c.range = (uint8_t)v;
        v = c.transfer; if (rc == PSYVID_OK) rc = psyvid__fill(&v, ix.c.transfer, "transfer", e, sizeof e); c.transfer = (uint8_t)v;
        v = c.primaries; if (rc == PSYVID_OK) rc = psyvid__fill(&v, ix.c.primaries, "primaries", e, sizeof e); c.primaries = (uint8_t)v;
        v = c.siting; if (rc == PSYVID_OK) rc = psyvid__fill(&v, ix.c.siting, "siting", e, sizeof e); c.siting = (uint8_t)v;
        if (rc == PSYVID_OK && c.frames > 0 && c.frames != ix.c.frames) { psyvid__fmt(e, sizeof e, "the stream says %lld frames, the index says %lld", (long long)c.frames, (long long)ix.c.frames); rc = PSYVID_ERR_FORMAT; }
        if (c.frames <= 0) c.frames = ix.c.frames;
        if (rc < 0) { psyvid__err(mv, "psy_video: %s: %s", desc->path ? desc->path : "the source", e); psyvid__close_partial(mv); return false; }
    }
    if (psyvid__is_rgb(c.format)) psyvid__rgb_color(&c);
    rc = psyvid__canon_check(&c, e, sizeof e);
    if (rc < 0) { psyvid__err(mv, "psy_video: %s: not the canonical form: %s", desc->path ? desc->path : "the source", e); psyvid__close_partial(mv); return false; }
    mv->info.w = c.w; mv->info.h = c.h; mv->info.format = c.format;
    mv->info.fps_num = c.fps_num; mv->info.fps_den = c.fps_den;
    mv->info.frames = c.frames; mv->info.gop = c.gop; mv->info.codec = c.codec;
    mv->info.matrix = c.matrix; mv->info.range = c.range; mv->info.transfer = c.transfer;
    mv->info.primaries = c.primaries; mv->info.siting = c.siting;
    mv->info.duration = psyvid__ft(mv, c.frames);
    mv->info.upload_format = psyvid__is_yuv(c.format) ? PSYVID_FMT_RGBA8 : c.format;
    mv->info.path = PSYVID_PATH_UPLOAD;
    mv->info.light = PSYVID_LIGHT_CODES;
    mv->timescale = st.timescale;
    mv->dec_caps = st.caps;
    if (psyvid__is_yuv(c.format) && g && g->cal_crc != 0 && desc->light == PSYVID_LIGHT_AUTO) {
        psyvid__err(mv, "psy_video: the gfx has a calibration, so light AUTO means EOTF (linear light), which needs psy_gfx.h's planar formats, not in v0.1; set light CODES to show the codes as device values");
        psyvid__close_partial(mv);
        return false;
    }
    /* the display against the rate */
    if (psyvid__display_rate(mv, &rn, &rd)) {
        double R = (double)rn / (double)rd, r = (double)c.fps_num / (double)c.fps_den;
        double ratio = R / r;
        int32_t k = (int32_t)floor(ratio + 0.5);
        mv->info.refresh_num = rn; mv->info.refresh_den = rd;
        mv->info.per_frame = ratio;
        if (k >= 1 && fabs(ratio / k - 1.0) <= 200e-6) { mv->info.multiple = k; mv->info.err_ppm = (ratio / k - 1.0) * 1e6; }
        else if (desc->strict_cadence) {
            psyvid__err(mv, "psy_video: %s is %d/%d fps; the display runs %.4f Hz, which is not a multiple (%.4f display frames per frame). "
                        "Re-encode at a rate it divides, choose a mode with psyscr_mode_multiple(), or leave desc.strict_cadence off to accept the cadence",
                        desc->path ? desc->path : "the movie", (int)c.fps_num, (int)c.fps_den, R, ratio);
            psyvid__close_partial(mv);
            return false;
        }
    }
    /* slots and scratch */
    mv->info.ahead = desc->ahead > 0 ? desc->ahead : 6;
    mv->n_slots = mv->info.ahead + 2;
    mv->info.slots = mv->n_slots;
    mv->info.slot_bytes = (size_t)c.w * (size_t)c.h * (size_t)psyvid__bpt(mv->info.upload_format);
    {
        size_t stride = (mv->info.slot_bytes + 4095) / 4096 * 4096;
        size_t need = stride * (size_t)mv->n_slots + 4096;
        unsigned char* base;
        if (desc->mem) {
            if (desc->mem_bytes < need) { psyvid__err(mv, "psy_video: desc.mem holds %zu bytes; %zu needed", desc->mem_bytes, need); psyvid__close_partial(mv); return false; }
            base = (unsigned char*)desc->mem;
        } else {
            mv->mem_raw = (unsigned char*)psyvid__malloc(need);
            if (!mv->mem_raw) { psyvid__err(mv, "psy_video: out of memory for %zu bytes of slots", need); psyvid__close_partial(mv); return false; }
            base = mv->mem_raw;
        }
        base = (unsigned char*)(((uintptr_t)base + 4095) & ~(uintptr_t)4095);
        for (i = 0; i < mv->n_slots; i++) {
            mv->slots[i].data = base + stride * (size_t)i;
            mv->slots[i].g = -1;
        }
    }
    if (psyvid__is_yuv(c.format)) {
        size_t yb = psyvid__planes_layout(c.format, c.w, c.h, NULL, NULL);
        mv->yuv = (unsigned char*)psyvid__malloc(yb);
        mv->yuv_rows = (int16_t*)psyvid__malloc(psyvid_yuv_rows_bytes(c.w));
        if (!mv->yuv || !mv->yuv_rows) { psyvid__err(mv, "psy_video: out of memory"); psyvid__close_partial(mv); return false; }
    }
    if (g) {
        psygfx_texture_desc td;
        memset(&td, 0, sizeof td);
        td.w = c.w; td.h = c.h; td.format = (psygfx_format)mv->info.upload_format;
        mv->tex = psygfx_texture(g, &td);
        if (mv->tex.id == 0) { psyvid__err(mv, "psy_video: psy_gfx.h refused the texture: %s", psygfx_error(g)); psyvid__close_partial(mv); return false; }
    }
    for (i = 0; i < mv->n_slots; i++) psyvid__free_push(mv, (uint32_t)i);
    mv->dt_pos = -1;
    mv->dt_key = -1;
    mv->shown_g = -1;
    mv->late_from = INT64_MAX;
    mv->next_id = 1;
    mv->state = PSYVID__STOPPED;
    mv->inline_mode = desc->inline_decode ? 1 : 0;
#if defined(PSYRT_NO_THREADS)
    mv->inline_mode = 1;
#else
    if (!mv->inline_mode) {
        psyrt_pump_desc pd;
        memset(&pd, 0, sizeof pd);
        pd.msg_size = sizeof(psyvid__msg);
        pd.capacity = 32;
        pd.on_msg = psyvid__on_msg;
        pd.on_idle = psyvid__on_idle;
        pd.ctx = mv;
        pd.on_start = psyvid__on_start;
        pd.pin_cpu = desc->pin_cpu;
        if (!psyrt_pump_start(&mv->pump, &pd)) {
            /* A target without threads (wasm without -pthread) still plays,
             * decoding inside psyvid_update(); describe() says so. */
            psyvid__fmt(mv->inline_why, sizeof mv->inline_why, "%s", psyrt_pump_error(&mv->pump));
            mv->inline_mode = 1;
        }
    }
#endif
    mv->open = 1;
    {
        psyrt_payload u;
        memset(&u, 0, sizeof u);
        u.i32[0] = c.w; u.i32[1] = c.h; u.i32[2] = c.format; u.i32[3] = c.fps_num; u.i32[4] = c.fps_den;
        u.i32[5] = c.codec; u.i32[6] = backend; u.i32[7] = mv->info.multiple;
        u.u32[8] = (uint32_t)c.matrix | (uint32_t)c.range << 8 | (uint32_t)c.transfer << 16 | (uint32_t)c.primaries << 24;
        u.i32[9] = mv->info.ahead;
        psyvid__push(mv, (uint16_t)PSYVID_EV_OPEN, PSYVID__NOW(), &u);
    }
    return true;
}

PSYVID_API void psyvid_close(psyvid_movie* mv) {
    int i;
    if (!mv || !mv->open) return;
    /* records whose flips never reported go out as they are, PENDING */
    for (i = 0; i < mv->n_pend; i++) psyvid__record_push(mv, &mv->pend[i]);
    mv->n_pend = 0;
    psyvid__close_partial(mv);
    mv->open = 0;
}

PSYVID_API const char* psyvid_error(const psyvid_movie* mv) { return mv ? mv->error : ""; }
PSYVID_API bool psyvid_is_open(const psyvid_movie* mv) { return mv && mv->open; }
PSYVID_API psygfx_tex psyvid_texture(const psyvid_movie* mv) { psygfx_tex t; t.id = mv && mv->open ? mv->tex.id : 0; return t; }

PSYVID_API void psyvid_get_info(const psyvid_movie* mv, psyvid_info* out) {
    if (!out) return;
    if (!mv || !mv->open) { memset(out, 0, sizeof *out); return; }
    *out = mv->info;
}

PSYVID_API psygfx_stim psyvid_stim(const psyvid_movie* mv, const psyvid_stim_desc* d) {
    psygfx_image_desc id;
    memset(&id, 0, sizeof id);
    if (d) {
        id.place = d->place; id.anchor = d->anchor; id.x = d->x; id.y = d->y; id.w = d->w; id.h = d->h;
        id.ori = d->ori; id.opacity = d->opacity; id.linear = d->linear; id.group = d->group;
    }
    id.tex = psyvid_texture(mv);
    return psygfx_image(mv ? mv->gfx : NULL, &id);
}

/* --- controls ------------------------------------------------------------------- */

PSYVID_API int64_t psyvid_play_at(psyvid_movie* mv, int64_t t) {
    if (!mv || !mv->open) return PSYVID_ERR_CLOSED;
    if (mv->state == PSYVID__FAILED) return PSYVID_ERR_DECODER;
    if (mv->state == PSYVID__ENDED || mv->state == PSYVID__PLAYING) return PSYVID_ERR_ORDER;
    if (mv->state == PSYVID__SEEKING) {
        mv->seek_resume_kind = t == PSYVID_ASAP ? 0 : 1;
        mv->seek_resume_t = t;
        return mv->seek_id;
    }
    mv->ctl.op = PSYVID__OP_PLAY;
    mv->ctl.t = t;
    mv->ctl.id = mv->next_id++;
    mv->ctl.active = 1;
    return mv->ctl.id;
}

PSYVID_API int64_t psyvid_pause_at(psyvid_movie* mv, int64_t t) {
    if (!mv || !mv->open) return PSYVID_ERR_CLOSED;
    if (mv->state != PSYVID__PLAYING) return PSYVID_ERR_ORDER;
    mv->ctl.op = PSYVID__OP_PAUSE;
    mv->ctl.t = t;
    mv->ctl.id = mv->next_id++;
    mv->ctl.active = 1;
    return mv->ctl.id;
}

PSYVID_API int64_t psyvid_seek_frame(psyvid_movie* mv, int64_t frame, int64_t resume_at) {
    if (!mv || !mv->open) return PSYVID_ERR_CLOSED;
    if (mv->state == PSYVID__FAILED) return PSYVID_ERR_DECODER;
    if (frame < 0 || frame >= mv->info.frames) return PSYVID_ERR_ARG;
    mv->ctl.active = 0;
    mv->state = PSYVID__SEEKING;
    mv->seek_g = frame;
    mv->seek_id = mv->next_id++;
    mv->seek_req_onset = 0;
    mv->seek_resume_kind = resume_at == PSYVID_ASAP ? 0 : resume_at == PSYVID_STAY_PAUSED ? 2 : 1;
    mv->seek_resume_t = resume_at;
    mv->ft_epoch++;
    mv->seek_posted = psyvid__post(mv, PSYVID__MSG_SEEK, frame) == PSYVID_OK;
    psyvid__st64(&mv->want_g, frame);
    /* the clock stops at the next onset (update), which is the first time
     * this thread knows */
    return mv->seek_id;
}

PSYVID_API int64_t psyvid_seek(psyvid_movie* mv, int64_t movie_t, int64_t resume_at) {
    if (!mv || !mv->open) return PSYVID_ERR_CLOSED;
    if (movie_t < 0 || movie_t >= mv->info.duration) return PSYVID_ERR_ARG;
    return psyvid_seek_frame(mv, psyvid__dueg(mv, movie_t, 0), resume_at);
}

PSYVID_API int psyvid_show(psyvid_movie* mv, int64_t frame) {
    if (!mv || !mv->open) return PSYVID_ERR_CLOSED;
    if (mv->state == PSYVID__FAILED) return PSYVID_ERR_DECODER;
    if (frame < 0 || frame >= mv->info.frames) return PSYVID_ERR_ARG;
    mv->ctl.active = 0;
    if (mv->state != PSYVID__MANUAL) {
        mv->state = PSYVID__MANUAL;
        if (mv->shown_g >= 0) mv->shown_g %= mv->info.frames;
        mv->cycle = 0;
    }
    mv->manual_g = frame;
    mv->manual_req = 1;
    return PSYVID_OK;
}

PSYVID_API int psyvid_result(const psyvid_movie* mv, int64_t id, psyrt_event* out) {
    int i, n;
    if (!mv || !mv->open) return PSYVID_ERR_CLOSED;
    n = mv->n_results < PSYVID__RESULTS ? mv->n_results : PSYVID__RESULTS;
    for (i = 0; i < n; i++) {
        const psyvid__result* r = &mv->results[i];
        if (r->id == id) { if (out) *out = r->ev; return PSYVID_OK; }
    }
    return id > 0 && id < mv->next_id ? PSYVID_PENDING : PSYVID_ERR_NOT_FOUND;
}

PSYVID_API int64_t psyvid_movie_time(const psyvid_movie* mv, int64_t t) {
    if (!mv || !mv->open) return 0;
    return psyvid__movie_time(mv, t) + mv->cycle * mv->info.duration;
}

/* --- records --------------------------------------------------------------------- */

static void psyvid__record_push(psyvid_movie* mv, const psyvid_record* r) {
    psyrt_payload u;
    memset(&u, 0, sizeof u);
    u.i64[0] = r->frame; u.i64[1] = r->due; u.i64[2] = r->movie_t;
    u.u32[6] = r->flags;
    u.u16[14] = (uint16_t)(r->decision | r->why << 2 | (r->tier & 7) << 6 | (r->path & 3) << 9);
    u.u16[15] = r->ahead;
    u.u32[8] = r->shows;
    u.u32[9] = (uint32_t)r->display;
    psyvid__push(mv, (uint16_t)PSYVID_EV_FRAME, r->onset ? r->onset : r->predicted, &u);
}

static void psyvid__complete(psyvid_movie* mv, psyvid_record* r, const psyscr_record* fr) {
    r->flags = (uint16_t)(r->flags & ~PSYVID_F_PENDING);
    if (fr) {
        r->tier = fr->tier;
        if (fr->onset) r->onset = fr->onset;
        else r->flags |= PSYVID_F_NOT_SHOWN;
        if (fr->dropped > 0) r->flags |= PSYVID_F_LATE;
        if (fr->flags & (PSYSCR_FLIP_ESTIMATED | PSYSCR_FLIP_ONSET_PLANNED)) r->flags |= PSYVID_F_ESTIMATED;
    } else {
        r->flags |= PSYVID_F_ESTIMATED;
    }
    if (r->tier > mv->info.worst_tier) mv->info.worst_tier = r->tier;
    if (mv->d.min_tier > 0 && r->tier > mv->d.min_tier) r->flags |= PSYVID_F_BELOW_TIER;
    if (mv->has_last && mv->last.display == r->display) mv->last = *r;
    psyvid__record_push(mv, r);
}

PSYVID_API void psyvid_flip_done(psyvid_movie* mv, const psyscr_record* fr) {
    int i, j;
    if (!mv || !mv->open || !fr) return;
    for (i = 0; i < mv->n_pend; ) {
        psyvid_record* r = &mv->pend[i];
        if (r->display <= fr->index) {
            psyvid__complete(mv, r, r->display == fr->index ? fr : NULL);
            for (j = i + 1; j < mv->n_pend; j++) mv->pend[j - 1] = mv->pend[j];
            mv->n_pend--;
        } else {
            i++;
        }
    }
}

PSYVID_API int psyvid_last(const psyvid_movie* mv, psyvid_record* out) {
    if (!mv || !mv->open) return PSYVID_ERR_CLOSED;
    if (!mv->has_last) return PSYVID_ERR_NOT_FOUND;
    if (out) *out = mv->last;
    return PSYVID_OK;
}

/* The movie time of global frame g, counting whole loops. */
static int64_t psyvid__gt(const psyvid_movie* mv, int64_t g) {
    return (g / mv->info.frames) * mv->info.duration + psyvid__ft(mv, g % mv->info.frames);
}

/* mg: the global movie time at the onset. */
static void psyvid__drop(psyvid_movie* mv, int64_t first, int64_t count, int why, int64_t mg, int64_t onset, int64_t display) {
    psyrt_payload u;
    if (count <= 0) return;
    mv->info.dropped += (uint64_t)count;
    mv->info.drops_by[why] += (uint64_t)count;
    memset(&u, 0, sizeof u);
    u.i64[0] = first % mv->info.frames;
    u.i64[1] = count;
    u.u16[14] = (uint16_t)why;
    u.u32[9] = (uint32_t)display;
    psyvid__push(mv, (uint16_t)PSYVID_EV_DROP, onset - (mg - psyvid__gt(mv, first)), &u);
}

/* A run j+1 .. rr-1 never shown: DECODE_LATE for the frames that were due
 * before they were ready, then by the vblank gap, the nominal schedule, or
 * a slip. */
static void psyvid__drop_run(psyvid_movie* mv, int64_t first, int64_t end, int64_t gap, int nominal,
                             int64_t mg, int64_t onset, int64_t display) {
    int64_t n = end - first, late = 0;
    if (n <= 0) return;
    if (mv->late_from < end) {
        late = end - (mv->late_from > first ? mv->late_from : first);
        if (late > n) late = n;
    }
    if (n - late > 0) {
        int w = gap > 1 ? PSYVID_WHY_DISPLAY_LATE
              : (nominal && mv->info.per_frame > 0 && mv->info.per_frame < 1.0) ? PSYVID_WHY_CADENCE
              : PSYVID_WHY_DRIFT;
        psyvid__drop(mv, first, n - late, w, mg, onset, display);
    }
    if (late > 0) psyvid__drop(mv, end - late, late, PSYVID_WHY_DECODE_LATE, mg, onset, display);
}

/* --- the frame ---------------------------------------------------------------------- */

/* Takes the newest ready slot of this epoch whose frame is not later than
 * `upto` and after `after`; frees the stale and the passed-over ones.
 * Returns the slot or -1. */
static int psyvid__take(psyvid_movie* mv, int64_t upto, int64_t after, int exact) {
    int best = -1;
    for (;;) {
        uint32_t h = mv->ready_head;
        int s;
        psyvid__slot* sl;
        if (h == psyvid__ld32(&mv->ready_tail)) break;
        s = (int)mv->ready_q[h & (PSYVID__QCAP - 1)];
        sl = &mv->slots[s];
        if (sl->epoch != mv->ft_epoch || sl->g <= after) {
            mv->ready_head = h + 1;
            psyvid__free_push(mv, (uint32_t)s);
            continue;
        }
        if (sl->g > upto) break;
        if (exact && sl->g != upto) {   /* manual: only the frame asked for */
            mv->ready_head = h + 1;
            psyvid__free_push(mv, (uint32_t)s);
            continue;
        }
        mv->ready_head = h + 1;
        if (best >= 0) psyvid__free_push(mv, (uint32_t)best);
        best = s;
    }
    return best;
}

static uint32_t psyvid__ready_count(const psyvid_movie* mv) {
    return psyvid__ld32(&mv->ready_tail) - mv->ready_head;
}

/* Frames ready from `from` on, consecutive, in this epoch. */
static int64_t psyvid__ready_from(const psyvid_movie* mv, int64_t from) {
    uint32_t h = mv->ready_head, t = psyvid__ld32(&mv->ready_tail);
    int64_t n = 0, expect = from;
    for (; h != t; h++) {
        const psyvid__slot* sl = &mv->slots[mv->ready_q[h & (PSYVID__QCAP - 1)]];
        if (sl->epoch != mv->ft_epoch || sl->g < expect) continue;
        if (sl->g != expect) break;
        n++;
        expect++;
    }
    return n;
}

/* Frees the slots of an older epoch at the head of the ready queue: after
 * a seek they come before every frame of the new one. */
static void psyvid__purge(psyvid_movie* mv) {
    for (;;) {
        uint32_t h = mv->ready_head;
        int s;
        if (h == psyvid__ld32(&mv->ready_tail)) return;
        s = (int)mv->ready_q[h & (PSYVID__QCAP - 1)];
        if (mv->slots[s].epoch == mv->ft_epoch) return;
        mv->ready_head = h + 1;
        psyvid__free_push(mv, (uint32_t)s);
    }
}

static int psyvid__upload(psyvid_movie* mv, int s) {
    int rc = PSYVID_OK;
    if (mv->gfx) {
        int64_t t0 = PSYVID__NOW();
        int g;
        PSYRT_ZONE(zu, "psyvid.upload");
        g = psygfx_texture_update(mv->gfx, mv->tex, 0, 0, mv->info.w, mv->info.h, mv->slots[s].data,
                                  (size_t)mv->info.w * (size_t)psyvid__bpt(mv->info.upload_format));
        PSYRT_ZONE_END(zu);
        mv->upload_ns_last = (uint64_t)(PSYVID__NOW() - t0);
        if (mv->upload_ns_last > mv->upload_ns_max) mv->upload_ns_max = mv->upload_ns_last;
        if (g == PSYGFX_ERR_ORDER) rc = PSYVID_ERR_ORDER;
        else if (g == PSYGFX_ERR_LOST) rc = PSYVID_ERR_LOST;
        else if (g < 0) rc = PSYVID_ERR_ARG;
    }
    return rc;
}

static void psyvid__pend_add(psyvid_movie* mv, const psyvid_record* r) {
    if (mv->n_pend == PSYVID__MAX_PEND) {
        /* a caller that never flips: the oldest goes out as it is */
        psyvid__complete(mv, &mv->pend[0], NULL);
        memmove(&mv->pend[0], &mv->pend[1], sizeof(psyvid_record) * (PSYVID__MAX_PEND - 1));
        mv->n_pend--;
    }
    mv->pend[mv->n_pend++] = *r;
    mv->last = *r;
    mv->has_last = 1;
}

/* The global frame due at global movie time mg. */
static int64_t psyvid__due_global(const psyvid_movie* mv, int64_t mg, int64_t lead_ns) {
    int64_t y = mg + lead_ns, D = mv->info.duration, q = psyvid__floordiv(y, D);
    return q * mv->info.frames + psyvid__dueg(mv, y - q * D, 0);
}

/* The nominal due frame: where the frame would be if the movie clock ran in
 * step with the vblank count at the nominal period (T / k at a multiple k,
 * else the mode's period) since the anchor or the last slip. A difference
 * is a slip; the schedule then starts again from the clock's phase, which
 * is where the frames now land. */
static int64_t psyvid__nominal(const psyvid_movie* mv, const psyscr_frame* f, int64_t lead_ns) {
    int64_t dv = f->vblank - mv->nom_vb, m;
    if (mv->info.multiple > 0) {
        int64_t q = (int64_t)mv->info.fps_num * mv->info.multiple;
        m = mv->nom_m + psyvid__floordiv(dv * mv->info.fps_den * PSYVID__NS, q);
    } else if (mv->info.refresh_num > 0) {
        m = mv->nom_m + psyvid__floordiv(dv * mv->info.refresh_den * PSYVID__NS, mv->info.refresh_num);
    } else {
        m = mv->nom_m + dv * f->period;
    }
    return psyvid__due_global(mv, m, lead_ns);
}

/* The schedule starts again at an anchor: offset 0 there. */
static void psyvid__nom_sync(psyvid_movie* mv, const psyscr_frame* f, int64_t m_global) {
    mv->nom_m = m_global;
    mv->nom_vb = f->vblank;
    mv->nom_d = 0;
    mv->nom_set = 1;
}

/* Shows (or repeats) on this display frame; i is the due global frame. */
static int psyvid__decide(psyvid_movie* mv, const psyscr_frame* f, int64_t i, int64_t m, int64_t lead_ns) {
    const int64_t N = mv->info.frames;
    int64_t j = mv->shown_g, mg = m + mv->cycle * mv->info.duration;
    int64_t e = mv->nom_set && f->period > 0 ? psyvid__nominal(mv, f, lead_ns) : i;
    int64_t d = i - e, slip = d - mv->nom_d;
    int64_t gap = mv->has_display ? f->vblank - mv->last_vblank : 1;
    int s, rc = PSYVID_OK;
    psyvid_record r;
    memset(&r, 0, sizeof r);
    r.display = f->index;
    r.movie_t = m;
    r.predicted = f->onset;
    r.flags = PSYVID_F_PENDING | (mv->following ? PSYVID_F_AUDIO_CLOCK : 0);
    r.path = (uint8_t)mv->info.path;
    psyvid__st64(&mv->want_g, i);
    s = psyvid__take(mv, i, j, 0);
    if (s >= 0) {
        int64_t rr = mv->slots[s].g;
        int why;
        if (rr < i) { why = PSYVID_WHY_DECODE_LATE; if (mv->late_from > rr + 1) mv->late_from = rr + 1; }
        else if (mv->just) why = mv->just;
        else why = PSYVID_WHY_DUE;
        if (j >= 0 && !mv->just) {
            /* the run j+1 .. rr-1 that was never shown */
            int64_t first = j + 1, n = rr - first, late = 0;
            if (n > 0 && mv->late_from < rr) {
                late = rr - (mv->late_from > first ? mv->late_from : first);
                if (late > n) late = n;
            }
            if (n - late > 0) {
                int64_t rest = n - late;
                if (gap > 1) {
                    psyvid__drop(mv, first, rest, PSYVID_WHY_DISPLAY_LATE, mg, f->onset, f->index);
                } else {
                    int64_t dr = slip > 0 ? (slip < rest ? slip : rest) : 0;
                    psyvid__drop(mv, first, rest - dr, PSYVID_WHY_CADENCE, mg, f->onset, f->index);
                    psyvid__drop(mv, first + rest - dr, dr, PSYVID_WHY_DRIFT, mg, f->onset, f->index);
                    slip -= dr;
                }
            }
            if (late > 0) psyvid__drop(mv, rr - late, late, PSYVID_WHY_DECODE_LATE, mg, f->onset, f->index);
        }
        /* a slip with nothing dropped: this frame came a display frame early */
        if (why == PSYVID_WHY_DUE && slip > 0 && gap <= 1) why = PSYVID_WHY_DRIFT;
        if (j >= 0 && rr / N > j / N && why == PSYVID_WHY_DUE) why = PSYVID_WHY_LOOP;
        if (rr >= i && why != PSYVID_WHY_DECODE_LATE) mv->late_from = INT64_MAX;
        rc = psyvid__upload(mv, s);
        if (mv->slots[s].flags & PSYVID_F_TS_MISMATCH) mv->info.ts_mismatch++;
        if (mv->slots[s].flags & PSYVID_F_HASH_MISMATCH) mv->info.hash_mismatch++;
        r.flags |= (uint16_t)mv->slots[s].flags;
        psyvid__free_push(mv, (uint32_t)s);
        mv->shown_g = rr;
        mv->shows = 1;
        r.frame = rr % N;
        r.decision = PSYVID_SHOWN;
        r.why = (uint8_t)why;
        mv->info.shown++;
        if (slip != 0 || d != 0) psyvid__nom_sync(mv, f, mg);
        if (j >= 0 && rr / N > j / N) {
            psyrt_payload u;
            memset(&u, 0, sizeof u);
            u.i64[0] = rr / N;
            psyvid__push(mv, (uint16_t)PSYVID_EV_LOOP, f->onset, &u);
        }
    } else if (j < 0) {
        /* nothing on screen yet and nothing ready: no record */
        if (mv->late_from > i) mv->late_from = i;
        return PSYVID_OK;
    } else {
        int why;
        if (i > j) { why = PSYVID_WHY_DECODE_LATE; if (mv->late_from > j + 1) mv->late_from = j + 1; }
        else if (i < j || slip < 0) why = PSYVID_WHY_DRIFT;
        else {
            int32_t q = (int32_t)floor(mv->info.per_frame);
            why = (mv->info.multiple > 0 || (int64_t)mv->shows < q) ? PSYVID_WHY_DUE : PSYVID_WHY_CADENCE;
        }
        mv->shows++;
        r.frame = j % N;
        r.decision = PSYVID_REPEATED;
        r.why = (uint8_t)why;
        mv->info.repeated++;
        mv->info.repeats_by[why]++;
        if (d != 0) psyvid__nom_sync(mv, f, mg);
    }
    mv->just = 0;
    r.shows = mv->shows;
    r.ahead = (uint16_t)psyvid__ready_count(mv);
    r.due = f->onset - (mg - psyvid__gt(mv, mv->shown_g));
    psyvid__pend_add(mv, &r);
    return rc;
}

static void psyvid__inline_decode(psyvid_movie* mv) {
    int n = 0;
    if (!mv->inline_mode) return;
    while (psyvid__dstep(mv)) {
        if (mv->d.inline_budget > 0 && ++n >= mv->d.inline_budget) break;
    }
}

/* A decoder failure ends the movie once the frames it made before are
 * used up: they are good frames. */
static int psyvid__fail_check(psyvid_movie* mv) {
    if (mv->state == PSYVID__FAILED) return PSYVID_ERR_DECODER;
    if (psyvid__ld32(&mv->dec_err) && psyvid__ready_count(mv) == 0) {
        if (mv->state != PSYVID__FAILED) {
            psyvid__fmt(mv->error, sizeof mv->error, "%s", mv->dt_error);
            mv->state = PSYVID__FAILED;
        }
        return PSYVID_ERR_DECODER;
    }
    return PSYVID_OK;
}

PSYVID_API int psyvid_update(psyvid_movie* mv, const psyscr_frame* f) {
    int64_t lead_ns, m, i;
    int rc = PSYVID_OK;
    PSYRT_ZONE(z, "psyvid.update");
    if (!mv || !mv->open) { PSYRT_ZONE_END(z); return PSYVID_ERR_CLOSED; }
    if (!f) { PSYRT_ZONE_END(z); return PSYVID_ERR_ARG; }
    if (mv->has_display && f->index <= mv->last_display) { PSYRT_ZONE_END(z); return PSYVID_ERR_ORDER; }
    /* every flip completed since the last begin; a frame filled by hand
     * may carry only the newest */
    if (f->done && f->n_done > 0) {
        int k;
        for (k = 0; k < f->n_done; k++) psyvid_flip_done(mv, &f->done[k]);
    } else if (f->last) {
        psyvid_flip_done(mv, f->last);
    }
    lead_ns = psyvid_lead_ns(mv->info.lead == 0.0 ? -1.0 : mv->info.lead, f->period);
    mv->lead_ns_last = lead_ns;
    mv->period_last = f->period;
    psyvid__purge(mv);
    /* the decoder learns this frame's due before it decodes (inline: now) */
    if (mv->state == PSYVID__PLAYING)
        psyvid__st64(&mv->want_g, mv->cycle * mv->info.frames + psyvid__dueg(mv, psyvid__movie_time(mv, f->onset), lead_ns));
    else if (mv->state == PSYVID__MANUAL)
        psyvid__st64(&mv->want_g, mv->manual_g);
    psyvid__inline_decode(mv);
    if (psyvid__fail_check(mv) < 0) { mv->has_display = 1; mv->last_display = f->index; mv->last_vblank = f->vblank; PSYRT_ZONE_END(z); return PSYVID_ERR_DECODER; }

    /* a seek: land, then resume */
    if (mv->state == PSYVID__SEEKING) {
        if (!mv->seek_posted) mv->seek_posted = psyvid__post(mv, PSYVID__MSG_SEEK, mv->seek_g) == PSYVID_OK;
        if (!mv->seek_req_onset) mv->seek_req_onset = f->onset;
        if (mv->running) psyvid__pause_clock(mv, f->onset);
        {
            int64_t need = mv->d.preroll > 0 ? mv->d.preroll : mv->info.ahead;
            int64_t avail = psyvid__ready_from(mv, mv->seek_g);
            int ready_target = avail > 0;
            int go = 0;
            if (need > mv->info.frames - mv->seek_g && !mv->d.loop) need = mv->info.frames - mv->seek_g;
            if (mv->seek_resume_kind == 2) go = ready_target;
            else if (mv->seek_resume_kind == 0) go = avail >= need;
            else go = ready_target && mv->seek_resume_t <= f->onset + lead_ns;
            if (go) {
                const psyvid__slot* sl = NULL;
                uint32_t h, t = psyvid__ld32(&mv->ready_tail);
                psyrt_payload u;
                for (h = mv->ready_head; h != t; h++) {
                    const psyvid__slot* c = &mv->slots[mv->ready_q[h & (PSYVID__QCAP - 1)]];
                    if (c->epoch == mv->ft_epoch && c->g == mv->seek_g) { sl = c; break; }
                }
                memset(&u, 0, sizeof u);
                u.i64[0] = mv->seek_g;
                u.i64[1] = sl ? sl->key : -1;
                u.i64[2] = sl ? sl->discarded : 0;
                u.i64[3] = mv->seek_req_onset;
                u.u32[8] = (uint32_t)mv->seek_id;
                mv->shown_g = -1;
                mv->cycle = 0;
                mv->late_from = INT64_MAX;
                mv->just = PSYVID_WHY_SEEK;
                if (mv->seek_resume_kind == 2) {
                    u.u32[9] = (uint32_t)psyvid__hold_at(mv, f->onset, psyvid__ft(mv, mv->seek_g), 1);
                    mv->state = PSYVID__PAUSED;
                    {
                        int s = psyvid__take(mv, mv->seek_g, -1, 1);
                        if (s >= 0) {
                            rc = psyvid__upload(mv, s);
                            psyvid__free_push(mv, (uint32_t)s);
                            mv->shown_g = mv->seek_g;
                            mv->shows = 1;
                        }
                    }
                    mv->just = 0;
                } else {
                    u.u32[9] = (uint32_t)psyvid__seek_anchor(mv, f->onset, psyvid__ft(mv, mv->seek_g));
                    psyvid__nom_sync(mv, f, psyvid__ft(mv, mv->seek_g));
                    mv->state = PSYVID__PLAYING;
                }
                psyvid__result_put(mv, mv->seek_id, (uint16_t)PSYVID_EV_SEEK, f->onset, &u);
            }
        }
    }

    /* manual mode */
    if (mv->state == PSYVID__MANUAL) {
        int64_t g = mv->manual_g;
        psyvid__st64(&mv->want_g, g);
        if (mv->running) psyvid__pause_clock(mv, f->onset);
        if (g == mv->shown_g) {
            psyvid_record r;
            memset(&r, 0, sizeof r);
            mv->shows++;
            r.display = f->index; r.frame = g; r.predicted = f->onset; r.movie_t = psyvid__ft(mv, g);
            r.due = f->onset; r.decision = PSYVID_REPEATED; r.why = PSYVID_WHY_MANUAL; r.flags = PSYVID_F_PENDING;
            r.shows = mv->shows; r.ahead = (uint16_t)psyvid__ready_count(mv); r.path = (uint8_t)mv->info.path;
            mv->info.repeated++; mv->info.repeats_by[PSYVID_WHY_MANUAL]++;
            psyvid__pend_add(mv, &r);
        } else {
            int s = psyvid__take(mv, g, -1, 1);
            psyvid_record r;
            memset(&r, 0, sizeof r);
            r.display = f->index; r.predicted = f->onset; r.flags = PSYVID_F_PENDING; r.path = (uint8_t)mv->info.path;
            if (s >= 0) {
                rc = psyvid__upload(mv, s);
                r.flags |= (uint16_t)mv->slots[s].flags;
                psyvid__free_push(mv, (uint32_t)s);
                psyvid__hold_at(mv, f->onset, psyvid__ft(mv, g), g != mv->shown_g + 1);
                mv->shown_g = g;
                mv->shows = 1;
                mv->manual_req = 0;
                r.frame = g; r.movie_t = psyvid__ft(mv, g); r.due = f->onset;
                r.decision = PSYVID_SHOWN; r.why = PSYVID_WHY_MANUAL;
                mv->info.shown++;
            } else {
                /* not decoded: the decoder seeks there; the frame on screen stays */
                if (mv->manual_req) {
                    mv->ft_epoch++;
                    psyvid__purge(mv);
                    psyvid__post(mv, PSYVID__MSG_SEEK, g);
                    mv->manual_req = 0;
                    psyvid__inline_decode(mv);
                    s = psyvid__take(mv, g, -1, 1);
                    if (s >= 0) {
                        rc = psyvid__upload(mv, s);
                        r.flags |= (uint16_t)mv->slots[s].flags;
                        psyvid__free_push(mv, (uint32_t)s);
                        psyvid__hold_at(mv, f->onset, psyvid__ft(mv, g), g != mv->shown_g + 1);
                        mv->shown_g = g; mv->shows = 1;
                        r.frame = g; r.movie_t = psyvid__ft(mv, g); r.due = f->onset;
                        r.decision = PSYVID_SHOWN; r.why = PSYVID_WHY_MANUAL;
                        mv->info.shown++;
                    }
                }
                if (r.decision == 0) {
                    if (mv->shown_g >= 0) {
                        mv->shows++;
                        r.frame = mv->shown_g; r.movie_t = psyvid__ft(mv, mv->shown_g); r.due = f->onset;
                        r.decision = PSYVID_REPEATED; r.why = PSYVID_WHY_SEEK;
                        mv->info.repeated++; mv->info.repeats_by[PSYVID_WHY_SEEK]++;
                    }
                }
            }
            if (r.decision) {
                r.shows = mv->shows;
                r.ahead = (uint16_t)psyvid__ready_count(mv);
                psyvid__pend_add(mv, &r);
            }
        }
    }

    /* a pending play or pause lands on the frame whose window reaches its time */
    if (mv->ctl.active) {
        int land = mv->ctl.t == PSYVID_ASAP ? 1 : mv->ctl.t <= f->onset + lead_ns;
        /* A resume starts one display period after the frozen time: the
         * frozen frame was on screen for the pause. */
        int64_t mt0 = mv->state == PSYVID__STOPPED ? 0 : mv->anchor_mt + f->period;
        if (mv->ctl.op == PSYVID__OP_PLAY && mv->ctl.t == PSYVID_ASAP) {
            int64_t need = mv->d.preroll > 0 ? mv->d.preroll : mv->info.ahead;
            int64_t from = psyvid__dueg(mv, mt0, lead_ns);
            if (mv->state != PSYVID__STOPPED && from <= mv->shown_g) from = mv->shown_g + 1;
            if (need > mv->info.frames - from && !mv->d.loop) need = mv->info.frames - from;
            if (from < mv->info.frames) {
                psyvid__st64(&mv->want_g, from);
                land = psyvid__ready_from(mv, from) >= need;
            }
        }
        if (land) {
            psyrt_payload u;
            int64_t mt;
            memset(&u, 0, sizeof u);
            if (mv->ctl.op == PSYVID__OP_PLAY) {
                mt = mt0;
                psyvid__anchor(mv, f->onset, mt);
                psyvid__nom_sync(mv, f, mt + mv->cycle * mv->info.duration);
                mv->state = PSYVID__PLAYING;
                u.i64[0] = mt; u.i64[1] = mv->ctl.id; u.i64[2] = psyvid__dueg(mv, mt, lead_ns); u.i64[3] = mv->ctl.t;
                psyvid__result_put(mv, mv->ctl.id, (uint16_t)PSYVID_EV_PLAY, f->onset, &u);
            }
            mv->ctl.active = mv->ctl.op == PSYVID__OP_PAUSE ? 2 : 0;
        }
    }

    if (mv->state == PSYVID__PLAYING) {
        m = psyvid__movie_time(mv, f->onset);
        i = psyvid__dueg(mv, m, lead_ns);
        while (i >= 0 && mv->d.loop && i >= mv->info.frames) {
            /* the loop: the base goes back to 0 at t(N); annotations fire again */
            int64_t rt_wrap = f->onset - (m - mv->info.duration);
            uint32_t keep = mv->anchor_epoch;
            mv->cycle++;
            psyvid__anchor(mv, rt_wrap, 0);
            if (mv->following) mv->anchor_epoch = keep;   /* the other clock runs on */
            m = psyvid__movie_time(mv, f->onset);
            i = psyvid__dueg(mv, m, lead_ns);
        }
        if (i >= 0) {
            int64_t gi = i + mv->cycle * mv->info.frames;
            if (!mv->d.loop && i >= mv->info.frames) {
                psyrt_payload u;
                memset(&u, 0, sizeof u);
                u.i64[0] = mv->shown_g;
                mv->state = PSYVID__ENDED;
                psyvid__push(mv, (uint16_t)PSYVID_EV_END, f->onset, &u);
                if (mv->shown_g >= 0)
                    psyvid__drop_run(mv, mv->shown_g + 1, mv->info.frames, f->vblank - mv->last_vblank, 0, m, f->onset, f->index);
            } else {
                rc = psyvid__decide(mv, f, gi, m, lead_ns);
            }
        }
        if (mv->ctl.active == 2) {
            psyrt_payload u;
            memset(&u, 0, sizeof u);
            psyvid__pause_clock(mv, f->onset);
            mv->state = PSYVID__PAUSED;
            u.i64[0] = mv->anchor_mt; u.i64[1] = mv->ctl.id; u.i64[2] = mv->shown_g % mv->info.frames; u.i64[3] = mv->ctl.t;
            psyvid__result_put(mv, mv->ctl.id, (uint16_t)PSYVID_EV_PAUSE, f->onset, &u);
            mv->ctl.active = 0;
        }
    } else if (mv->state == PSYVID__STOPPED && !mv->ctl.active) {
        psyvid__st64(&mv->want_g, 0);
    }

    psyvid__wake(mv);
    mv->has_display = 1;
    mv->last_display = f->index;
    mv->last_vblank = f->vblank;
    if (mv->screen) psyscr_mark(mv->screen, PSYSCR_PHASE_UPLOAD);
    PSYRT_PLOT("psyvid.ready", psyvid__ready_count(mv));
    PSYRT_ZONE_END(z);
    if (rc < 0) return rc;
    return mv->state == PSYVID__ENDED ? PSYVID_ENDED : PSYVID_OK;
}

/* --- following another clock ------------------------------------------------------- */

PSYVID_API int psyvid_follow(psyvid_movie* mv, const psyscr_frame* f, psyvid_unit_at_fn unit_at, void* ctx, uint32_t rate) {
    int64_t num, den, u, m;
    if (!mv || !mv->open) return PSYVID_ERR_CLOSED;
    if (!f || !unit_at || rate == 0) return PSYVID_ERR_ARG;
    psyvid__base_rate(mv, &num, &den);
    if (num != den) return PSYVID_ERR_REFUSED;   /* a soundtrack is never resampled */
    if (mv->state != PSYVID__PLAYING || !mv->running) return PSYVID_OK;
    if (mv->follow_epoch != mv->anchor_epoch || !mv->following || mv->follow_rate != rate) {
        /* the other clock's unit at movie time 0 of this anchor */
        int64_t a = unit_at(ctx, mv->anchor_rt);
        int64_t mg = mv->anchor_mt + mv->cycle * mv->info.duration;
        psyrt_payload pu;
        mv->follow_f0 = a - (int64_t)floor((double)mg * (double)rate / 1e9 + 0.5);
        mv->follow_rate = rate;
        mv->follow_epoch = mv->anchor_epoch;
        mv->follow_last_m = mg;
        mv->following = 1;
        memset(&pu, 0, sizeof pu);
        pu.i64[0] = mv->follow_f0; pu.i64[1] = rate;
        psyvid__push(mv, (uint16_t)PSYVID_EV_CLOCK, mv->anchor_rt, &pu);
        return PSYVID_OK;
    }
    u = unit_at(ctx, f->onset);
    m = (int64_t)floor((double)(u - mv->follow_f0) * 1e9 / (double)rate + 0.5);
    /* A clock that stalls or steps back would rewind the base and fire its
     * annotations again; the base runs on instead until the clock moves. */
    if (m <= mv->follow_last_m) return PSYVID_OK;
    mv->follow_last_m = m;
    m -= mv->cycle * mv->info.duration;
    mv->anchor_rt = f->onset;
    mv->anchor_mt = m;
    if (mv->d.timeline) psytl_anchor(mv->d.timeline, mv->d.base, f->onset, m);
    return PSYVID_OK;
}

/* --- describe and params ---------------------------------------------------------- */

static const char* psyvid__why_name(int w) {
    static const char* const n[] = { "DUE", "CADENCE", "DISPLAY_LATE", "DECODE_LATE", "DRIFT", "SEEK", "LOOP", "MANUAL" };
    return w >= 0 && w < PSYVID_WHY_COUNT ? n[w] : "?";
}

PSYVID_API int psyvid_describe(const psyvid_movie* mv, char* buf, size_t cap) {
    char cad[200], counts[300], be[96];
    int w, n;
    size_t used = 0;
    if (!mv || !mv->open) return snprintf(buf, cap, "psy_video %s: not open", PSYVID_VERSION_STRING);
    be[0] = 0;
    if (mv->dec && mv->dec->describe) mv->dec->describe(mv->dec_ctx, be, sizeof be);
    else if (mv->dec) psyvid__fmt(be, sizeof be, "%s", mv->dec->name);
    if (mv->info.refresh_num <= 0) {
        psyvid__fmt(cad, sizeof cad, "display rate unknown: no cadence check");
    } else if (mv->info.multiple > 0) {
        psyvid__fmt(cad, sizeof cad, "display %.4f Hz: %d display frames per frame (multiple, %+.1f ppm)",
                    (double)mv->info.refresh_num / mv->info.refresh_den, (int)mv->info.multiple, mv->info.err_ppm);
    } else {
        /* the cadence's counts over one cycle of up to 24 frames */
        double P = 1e9 * mv->info.refresh_den / mv->info.refresh_num;
        int64_t L = psyvid_lead_ns(mv->info.lead == 0.0 ? -1.0 : mv->info.lead, (int64_t)P);
        int counts_n[24], k, f0 = 0, nf = 0, lo = 1 << 30, hi = 0;
        char pat[96];
        size_t pu = 0;
        memset(counts_n, 0, sizeof counts_n);
        for (k = 0; k < 2000 && nf < 24; k++) {
            int64_t due = psyvid__dueg(mv, (int64_t)floor(k * P), L);
            if (due >= 24) break;
            if (due >= 0) { counts_n[due]++; if (due + 1 > nf) nf = (int)due + 1; }
        }
        pat[0] = 0;
        for (k = 0; k < nf - 1 && k < 10; k++) {
            pu += (size_t)snprintf(pat + pu, sizeof pat - pu, "%s%d", k ? ":" : "", counts_n[k]);
            if (pu >= sizeof pat) break;
        }
        for (k = 0; k < nf - 1; k++) { if (counts_n[k] < lo) lo = counts_n[k]; if (counts_n[k] > hi) hi = counts_n[k]; }
        (void)f0;
        if (lo == 0)
            psyvid__fmt(cad, sizeof cad, "display %.4f Hz: %.4f display frames per frame, not a multiple: cadence %s, frames on screen 0 (dropped, CADENCE) to %.1f ms",
                        (double)mv->info.refresh_num / mv->info.refresh_den, mv->info.per_frame, pat, hi * P / 1e6);
        else
            psyvid__fmt(cad, sizeof cad, "display %.4f Hz: %.4f display frames per frame, not a multiple: cadence %s, frames on screen %.1f to %.1f ms (CADENCE)",
                        (double)mv->info.refresh_num / mv->info.refresh_den, mv->info.per_frame, pat, lo * P / 1e6, hi * P / 1e6);
    }
    counts[0] = 0;
    used = (size_t)snprintf(counts, sizeof counts, "shown %llu, repeated %llu, dropped %llu",
                            (unsigned long long)mv->info.shown, (unsigned long long)mv->info.repeated, (unsigned long long)mv->info.dropped);
    for (w = 0; w < PSYVID_WHY_COUNT && used < sizeof counts; w++) {
        if (mv->info.repeats_by[w] || mv->info.drops_by[w])
            used += (size_t)snprintf(counts + used, sizeof counts - used, "; %s %llu/%llu", psyvid__why_name(w),
                                     (unsigned long long)mv->info.repeats_by[w], (unsigned long long)mv->info.drops_by[w]);
    }
    n = snprintf(buf, cap, "psy_video %s: %s, %dx%d %s, %d/%d fps, %lld frames, GOP %d; %s; lead %.2f; ahead %d (%d slots, %.1f MB%s%s); path UPLOAD to %s; light CODES; %s%s%s; worst tier %d",
                 PSYVID_VERSION_STRING, be, (int)mv->info.w, (int)mv->info.h, psyvid__fmt_name(mv->info.format),
                 (int)mv->info.fps_num, (int)mv->info.fps_den, (long long)mv->info.frames, (int)mv->info.gop, cad,
                 mv->info.lead, (int)mv->info.ahead, (int)mv->info.slots,
                 (double)mv->info.slot_bytes * mv->info.slots / 1048576.0,
                 mv->inline_mode ? ", decoded inline" : "", mv->inline_why[0] ? ": no decode thread" : "",
                 psyvid__fmt_name(mv->info.upload_format), counts,
                 mv->info.ts_mismatch ? "; NOT CANONICAL: timestamp mismatches" : "",
                 mv->info.hash_mismatch ? "; NOT CANONICAL: hash mismatches" : "", (int)mv->info.worst_tier);
    return n;
}

PSYVID_API const psyscr_param* psyvid_params(int* n) {
    static const psyscr_param p[] = {
        { "ahead", "i32", 0, PSYVID_MAX_SLOTS - 2, 6, "frame", "frames decoded ahead" },
        { "preroll", "i32", 0, PSYVID_MAX_SLOTS - 2, 6, "frame", "frames ready before an ASAP start; 0 = ahead" },
        { "loop", "bool", 0, 1, 0, "", "frame 0 follows the last; annotations fire again" },
        { "strict_cadence", "bool", 0, 1, 0, "", "refuse a refresh that is not a multiple of the rate" },
        { "lead", "f64", -1, 0.999, 0.5, "frame", "the due rule's lead; -1 = never early" },
        { "light", "enum", 0, 2, 0, "", "AUTO, CODES, EOTF (EOTF not in v0.1)" },
        { "chroma", "enum", 0, 1, 0, "", "SITED, NEAREST" },
        { "base", "i32", 0, 7, 0, "", "the movie base on the timeline" },
        { "min_tier", "i32", 0, 4, 0, "", "flag frames worse than this tier" },
        { "movie_index", "u32", 0, 4294967295.0, 0, "", "aux of every record" },
        { "pin_cpu", "i32", 0, 1024, 0, "", "decode thread's CPU; 0 = no pin" },
    };
    if (n) *n = (int)(sizeof p / sizeof p[0]);
    return p;
}

#ifdef __cplusplus
}
#endif

#endif /* PSY_VIDEO_IMPLEMENTATION_GUARD */
#endif /* PSY_VIDEO_IMPLEMENTATION */

/* The audio part of the implementation: compiled when psy_audio.h came
 * first. */
#if defined(PSY_VIDEO_IMPLEMENTATION) && defined(PSY_AUDIO_H_INCLUDED) && !defined(PSY_VIDEO_AUDIO_IMPL)
#define PSY_VIDEO_AUDIO_IMPL
#ifdef __cplusplus
extern "C" {
#endif
static int64_t psyvid__au_unit_at(void* ctx, int64_t t) { return psyau_frame_at((const psyau_audio*)ctx, t); }
PSYVID_API int psyvid_follow_audio(psyvid_movie* mv, psyau_audio* au, const psyscr_frame* f) {
    psyau_caps c;
    if (!au) return PSYVID_ERR_ARG;
    psyau_get_caps(au, &c);
    return psyvid_follow(mv, f, psyvid__au_unit_at, au, c.rate);
}
#ifdef __cplusplus
}
#endif
#endif
