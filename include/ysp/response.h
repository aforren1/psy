/* ysp/response.h - v0.1.4 - public domain single-header response collector
 *
 *   Turns timestamped input events from any device into the response of a
 *   trial: which key or button, when, measured from the stimulus onset that
 *   the display or the sound card reported, and how good both times are.
 *   The jsPsych keyboard-response options (choices, ALL_KEYS and NO_KEYS,
 *   minimum_valid_rt, response_ends_trial and persist, allow_held_key,
 *   wait_for_key_release, rt_key_duration) on a device-neutral event, plus
 *   threshold crossings on analog channels and a trace buffer.
 *
 *   REQUIRES ysp/input.h beside it: the input event, its kinds and the
 *   source tiers live there, so producers depend on ysp/input.h alone.
 *   This header includes it; copy both files.
 *
 *   Pure computation: no heap, no OS calls, no threads, no file I/O. Times
 *   are int64 ns on the ysp/rt.h clock, passed in. Needs only the C
 *   standard library. C99 is the floor: it builds as C99, C11 and C++17,
 *   and in the C dialect MSVC compiles by default. Small inline helpers
 *   appear when ysp/screen.h, ysp/audio.h, ysp/timeline.h or SDL3's
 *   SDL_events.h was included before it.
 *
 *   ---------------------------------------------------------------------
 *   CHANGELOG
 *   ---------------------------------------------------------------------
 *   v0.1.4 - YRSP_KINDS_ALL in ALL mode takes every kind but SYNC
 *          (ysp/input.h v0.3.0's YIN_KIND_SYNC: a scanner pulse, a TTL
 *          input, a photodiode edge), so a timing event never ends a trial.
 *          A mask that names the SYNC bit, or a LIST choice of kind SYNC,
 *          still takes it. YRSP_KIND_SYNC; the data row names kind 7
 *          "sync". Needs ysp/input.h v0.3.0.
 *   v0.1.3 - The input event, its constants, the source entry and the SDL
 *          and raw mouse adapters moved to ysp/input.h (yin_ names);
 *          this header includes it and keeps every yrsp_ and YRSP_
 *          name as an alias. API and behavior unchanged.
 *   v0.1.2 - Raw mice: yrsp_from_mouse() turns ysp/screen.h v0.3.5's
 *          raw mouse reports (desc.raw_mice) into mouse inputs per device
 *          (axes YRSP_AXIS_DELTA, _ABSOLUTE, _WHEEL), with
 *          yrsp_raw_mouse_source(); yrsp_cursor integrates one mouse's
 *          movement with a stated gain. SDL's mouse events with which 0
 *          (its absolute mode, message-timed) now get tier 3.
 *   v0.1.1 - Devices (DEVICES): two reports merge only when their devices
 *          are equal or one is 0, so two keyboards (or boxes) pressing one
 *          key within 50 ms are two presses; down state and releases are
 *          per device too, and a repeat of a key that only another
 *          keyboard holds is a press (SDL keeps one key state for all
 *          keyboards). The keyboard's 512-bit down table is gone: every
 *          kind uses the held table, now 64 entries.
 *   v0.1.0 - first version: the input event, the source table, choices
 *          (scancode, keycode, key names, ALL, NONE, analog crossings),
 *          the window, onset refinement, double-report removal, held keys,
 *          releases, the trace, the result and its CSV row, the SDL3
 *          adapter.
 *
 *   STATUS: v0.1.4, 2026-10-08 (v0.1.3's runs, and SYNC in ALL mode). Built and run on Windows 11 with MinGW-w64
 *   gcc 16.1 as C11, C99 and C++17 under -Wall -Wextra -Wpedantic -Wshadow
 *   -Werror and with MSVC 19.44 under /W4 /WX as C11 and C++17.
 *   tests/adapt/response_test.c (20,604 checks; 20,669 with the SDL
 *   adapter, checked against SDL 3.4.0's headers) covers every rule on
 *   synthetic streams, with 20,000 random streams against a direct reading
 *   of the entries. Mutations: 49 of 49 caught
 *   (tests/mutate/response.toml).
 *   examples/response/trial_keyboard.c --sim (simulated display, synthetic
 *   participant) passes its own checks on both compilers; in a window on
 *   the Iris Xe laptop, keys sent with SendInput (scan codes and
 *   virtual-key taps) went through yscr_poll(), yrsp_from_sdl() and
 *   the collector: 6 trials, final onsets from flip records of tier 1
 *   and 2, the virtual-key taps' second reports removed. NOT done: a key
 *   pressed by hand (screen_input --hand is the check); Linux and macOS
 *   (CI runs the test and the example on the simulated display); the
 *   timing of mouse, touch, pen and gamepad events; any response box.
 *
 *   ---------------------------------------------------------------------
 *   USAGE
 *   ---------------------------------------------------------------------
 *   Do this:
 *
 *       #define YSP_RESPONSE_IMPLEMENTATION
 *
 *   in *one* C or C++ file before including this header to create the
 *   implementation. Every other file just includes the header normally.
 *   Include ysp/screen.h (or SDL3) first where you want yrsp_from_sdl()
 *   and yrsp_onset_flip().
 *
 *   The jsPsych html-keyboard-response trial, choices f and j, a response
 *   window of 1.5 s from the stimulus onset, responses before 0.1 s are
 *   anticipations (examples/response/trial_keyboard.c is the whole program):
 *
 *       static const yrsp_choice keys[] = { { .key = "f" }, { .key = "j" } };
 *       yrsp_collector rsp;
 *       yrsp_desc d = { .choices = keys, .n_choices = 2,
 *                         .duration = 1.5, .minimum_valid_rt = 0.1 };
 *       if (!yrsp_init(&rsp, &d)) { fputs(yrsp_error(&rsp), stderr); return 1; }
 *
 *       yrsp_arm(&rsp, f.onset);                      // at the fixation
 *       // every frame:
 *       while (yscr_poll(&scr, &ev, &t))              // feed every event
 *           if (yrsp_from_sdl(&ev, t, NULL, &in)) yrsp_feed(&rsp, &in);
 *       // the stimulus event landed: its planned onset
 *       yrsp_onset o = yrsp_onset_landing(&fired[i]);
 *       yrsp_set_onset(&rsp, &o);
 *       // its flip record completed (f.done[], a later begin()): the onset
 *       o = yrsp_onset_flip(&f.done[k]);
 *       yrsp_set_onset(&rsp, &o);
 *       if (yrsp_update(&rsp, f.onset) == YRSP_ENDED) { ... hide, feedback }
 *       // after the trial:
 *       yrsp_result r;
 *       yrsp_finish(&rsp, &r);
 *       printf("rt %.3f s, key %s, onset tier %d\n", r.rt, r.response_name, r.onset_tier);
 *
 *   ---------------------------------------------------------------------
 *   THE EVENT (yrsp_input)
 *   ---------------------------------------------------------------------
 *   yrsp_input is ysp/input.h's yin_event, and every YRSP_KIND_*,
 *   YRSP_PRESS..., YRSP_IN_*, YRSP_AXIS_* and YRSP_TIER_* name is
 *   its YIN_ name (see ysp/input.h for the fields). yrsp_feed() takes
 *   events in arrival order. Their times need not rise: sources stamp
 *   differently (a response box's device clock, SDL's raw and message
 *   paths), and every rule below reads t, not the order.
 *   ---------------------------------------------------------------------
 *   SOURCES AND TIMING QUALITY (yrsp_source, desc.sources)
 *   ---------------------------------------------------------------------
 *   ysp/input.h's yin_source (its SOURCES section): a tier and measured
 *   bounds per kind or device. The result copies the responding source's
 *   tier, bounds and partial flag next to the onset's tier.
 *   ---------------------------------------------------------------------
 *   CHOICES (desc.choices, desc.mode, desc.kinds)
 *   ---------------------------------------------------------------------
 *   A choice is a key or button by kind, device (0 = any) and control,
 *   or an analog crossing (CROSSINGS). Keys match by SCANCODE by default:
 *   the physical key, which is what a lab means by "the F and J keys" and
 *   which a keyboard layout does not move. match = YRSP_MATCH_KEYCODE
 *   matches the layout's key instead (jsPsych's KeyboardEvent.key).
 *   choice.key is a convenience: a key name, resolved at init to the key's
 *   scancode ON THE US LAYOUT ("f" is the key right of D on any keyboard,
 *   which types "f" on QWERTY and AZERTY alike and "u" on Dvorak), or with
 *   KEYCODE to the keycode that name types. Names, case-insensitive: a to
 *   z, 0 to 9, space (or " "), enter (return), escape (esc), backspace,
 *   tab, - = [ ] \ ; ' ` , . /, f1 to f12, arrowleft arrowright arrowup
 *   arrowdown (left right up down). choice.name is the `response` column
 *   (NULL = choice.key). Every entry logs both the scancode and keycode.
 *   desc.mode:
 *     YRSP_CHOICES_ALL (0)   any press of a kind in desc.kinds; jsPsych
 *                              "ALL_KEYS"
 *     YRSP_CHOICES_LIST      the list; n_choices > 0 implies it
 *     YRSP_CHOICES_NONE      nothing is a response; presses are logged.
 *                              jsPsych "NO_KEYS"
 *   desc.kinds, in ALL mode: !!! 0 MEANS THE KEYBOARD ONLY, NOT EVERY KIND
 *   !!! A click that brings a window to the front must not end a keyboard
 *   trial. YRSP_KINDS_ALL takes every kind but SYNC: a scanner pulse
 *   or a photodiode edge is not a participant's response. To take SYNC
 *   events (wait for the scanner), name the bit, YRSP_KIND_BIT(
 *   YRSP_KIND_SYNC), or use a LIST choice of kind SYNC.
 *   YRSP_KIND_BIT(k) takes one kind.
 *
 *   ---------------------------------------------------------------------
 *   THE WINDOW AND RT
 *   ---------------------------------------------------------------------
 *   yrsp_arm(t) starts a window: presses from then on are logged.
 *   yrsp_set_onset() gives the onset that RT is measured from. For each
 *   press, rt = t - onset, and the press is
 *     EARLY         rt < 0, or no onset yet
 *     ANTICIPATION  0 <= rt < minimum_valid_rt
 *     on time       minimum_valid_rt <= rt < duration (no upper bound when
 *                   duration is 0)
 *     LATE          rt >= duration
 *   An on-time press of a choice is VALID: a response. EARLY, ANTICIPATION
 *   and LATE presses are logged and counted, never dropped, and never end
 *   the window. jsPsych drops a press before minimum_valid_rt; ysp keeps
 *   it, flagged, because anticipations are data.
 *   The window ends (YRSP_ENDED from feed(), set_onset() or update()):
 *     - at the first response, unless desc.persist (jsPsych's
 *       response_ends_trial = false, or persist); with persist, after
 *       desc.max_responses responses (0 = never);
 *     - with desc.wait_for_key_release, at that response's release, or
 *       desc.max_hold (0 = 2 s) after the press;
 *     - at onset + duration + desc.settle, in update(). settle lets a
 *       report stamped before the deadline but delivered after it still be
 *       fed. A press fed after ENDED with t before the end still counts.
 *   Presses with t at or after the press that ended the window are
 *   AFTER_END: logged, never responses (jsPsych cancels the listener).
 *   The result's response is the VALID entry with the earliest t, not the
 *   first to arrive: a box report read late but stamped by the device's
 *   clock wins over a key press that arrived first and happened later.
 *   RT in the result: seconds, a double, NaN when there is none (jsPsych's
 *   null). The ns times are there too (t_response, t_onset).
 *
 *   ---------------------------------------------------------------------
 *   THE ONSET (yrsp_onset, yrsp_set_onset)
 *   ---------------------------------------------------------------------
 *   The onset is the stimulus's best-known time: the flip record's onset
 *   of the frame the stimulus landed on (ysp/screen.h: the OS's vblank
 *   time, plus onset_offset_ns when a photodiode test set it), or a sound's
 *   onset record (ysp/audio.h: a device-clock fit's time). The record
 *   completes a frame or more after the onset, so give the plan first
 *   (final = 0: ytl_event.onset, the landing frame's predicted onset)
 *   and the record when it completes (final = 1). Each set_onset()
 *   classifies every entry again. A plan does not replace a final onset.
 *   When the final onset differs from the plan (a dropped frame shows the
 *   stimulus a frame later), a press can change class: its entry gets
 *   RECLASSIFIED, and when the press that ended the window is no longer a
 *   response, the result gets ENDED_EARLY: the trial ended on a press that
 *   does not count. The caller decides (re-queue the trial). The result
 *   carries the onset's tier, source (FLIP, AUDIO, PLAN, OTHER), residual,
 *   frame and an uncertainty the caller states (from a loopback; 0 = not
 *   stated: the producers give tiers, not bounds). ONSET_PLAN in the
 *   result: no final onset came, so RT is from a prediction.
 *
 *   ---------------------------------------------------------------------
 *   DOUBLE REPORTS (desc.dedup)
 *   ---------------------------------------------------------------------
 *   With SDL_HINT_WINDOWS_RAW_KEYBOARD on (ysp/screen.h turns it on), SDL
 *   3.4 also sends a key from the window message when the message's scan
 *   code is 0 (read in SDL 3.4.0's source). Measured with SendInput
 *   (examples/screen/input.c --reports): a virtual-key tap (wVk, scan code
 *   0, down and up back to back) gave two press-release pairs, the second
 *   report -0.1 to 34.2 ms after the first (518 taps, 1 above 17.1 ms); a
 *   virtual-key 100 ms hold gave one press, the second report marked as a
 *   repeat; scan-code injection (KEYEVENTF_SCANCODE) gave one report,
 *   tapped or held. Keys from a keyboard carry scan codes, so they most
 *   likely report once; screen_input --hand checks keys pressed by hand.
 *   The rule guards against the rest: injected input, keys without a
 *   scan code (media keys), some on-screen keyboards and remote tools.
 *     R1  A PRESS with YRSP_IN_REPEAT is not a new press, unless only
 *         another device holds the control (DEVICES).
 *     R2  A PRESS of a control that is down (no RELEASE since) on the same
 *         device is not a new press. R1 and R2 are HELD (HELD KEYS).
 *     R3  A PRESS of the same kind and control, from the same device or
 *         with either device 0 (DEVICES), within dedup
 *         (0 = 50 ms; < 0 turns R3 off) of the last accepted press of that
 *         control, in either time order, is a second report: logged as
 *         DUPLICATE, never a response, and it does not make the control
 *         down, so its RELEASE is a stray release and is dropped. The
 *         first report to arrive is kept, not the earliest stamp: SDL
 *         queues the raw report first, and the message report's stamp is
 *         tick-quantized.
 *   Why 50 ms: the largest measured gap was 34.2 ms; a person does not
 *   press one key twice within 50 ms (keyboards debounce contact bounce,
 *   and tapping one finger runs at intervals of about 150 ms or more:
 *   stated, not measured here).
 *
 *   ---------------------------------------------------------------------
 *   DEVICES (input.device)
 *   ---------------------------------------------------------------------
 *   Two inputs are of one device when their devices are equal or one of
 *   them is 0. SDL 3.4 on Windows gives a key on the raw path the
 *   keyboard's Raw Input handle (each keyboard its own), a key on the
 *   message path 0, and injected input 0 on both paths (measured with
 *   SendInput: every report had 0). So a raw report and its message
 *   report merge, and two keyboards pressing one key never do. Mice in
 *   SDL's absolute mode all come as device 0 (one pointer);
 *   ysp/screen.h's desc.raw_mice gives each mouse its own id (MICE). This rule
 *   applies to R2, R3, releases and HELD_AT_OPEN. SDL keeps one key state
 *   for all keyboards: while keyboard A holds F, keyboard B's press of F
 *   comes marked as a repeat, and B's release ends SDL's state, so SDL
 *   drops A's later release. The collector takes B's "repeat" for B's
 *   press (R1); A's press then has no release. ysp/screen.h logs the
 *   devices with their names (its INPUT section) to map the ids.
 *
 *   ---------------------------------------------------------------------
 *   MICE (yrsp_from_mouse, yrsp_cursor)
 *   ---------------------------------------------------------------------
 *   With ysp/screen.h's desc.raw_mice (v0.3.5, Windows), each report of
 *   yscr_poll_mouse() becomes inputs of YRSP_KIND_MOUSE with the
 *   mouse's device id: presses and releases (buttons 1 left, 2 middle, 3
 *   right, 4 and 5 the side buttons), a YRSP_AXIS_DELTA sample (x, y in
 *   counts, unaccelerated), a YRSP_AXIS_ABSOLUTE sample for a tablet or
 *   remote desktop (x, y 0..1), a YRSP_AXIS_WHEEL sample (value in
 *   notches, x the horizontal wheel). A mouse SDL does not list has
 *   YRSP_IN_UNLISTED. yrsp_raw_mouse_source() is the source entry
 *   (stamped at read, host part measured with injected input only). A
 *   button of one participant's mouse is a choice with kind MOUSE, the
 *   device's id and the button; a movement onset is a DISTANCE choice on
 *   YRSP_AXIS_DELTA (cumulate it with yrsp_cursor first: DISTANCE
 *   reads positions) or a crossing on the cursor's position. yrsp_cursor
 *   is one participant's cursor: gain pixels per count, clamped to an
 *   area, no acceleration, so the gain goes in the data file.
 *
 *   ---------------------------------------------------------------------
 *   HELD KEYS (desc.allow_held_key)
 *   ---------------------------------------------------------------------
 *   The collector tracks which keys and buttons are down from every event
 *   fed, armed or not, so feed events between windows too. jsPsych's rule
 *   (KeyboardListenerAPI.ts, getKeyboardResponse): with allow_held_key
 *   false, a keydown of a key already down (seen down, no keyup since) is
 *   not a response, so a key held from before the window and its OS
 *   repeats never count; with true, every keydown counts, an OS repeat
 *   too, at the repeat's time. ysp follows it: with allow_held_key, a held
 *   press of a choice is logged with HELD and can be a response; its RT is
 *   the OS repeat's time, which the flag says. Without it, held presses
 *   are counted (n_held), not logged. A choice down at arm() sets
 *   HELD_AT_OPEN in the result. Differences from jsPsych: ysp matches by
 *   scancode, so a Shift change while held is the same key (jsPsych keys
 *   its held set on KeyboardEvent.key, its press times on .code); and a
 *   window blur in jsPsych releases every key, where ysp waits for SDL's
 *   key-up events (SDL sends them when the window loses the keyboard
 *   focus; not measured).
 *
 *   ---------------------------------------------------------------------
 *   RELEASES AND HOLD DURATION
 *   ---------------------------------------------------------------------
 *   A RELEASE closes every logged press of the same kind, control and
 *   device (DEVICES) that has none: the press and, with allow_held_key, its
 *   HELD repeats, which are one press. rt_key_duration = release - press
 *   of the response, NaN when no release came. A release of a control
 *   that is not down is dropped (counted). Releases pair after ENDED and
 *   after finish() too; call finish() again for the duration. jsPsych's
 *   rt_key_duration runs from the key's first keydown; ysp's from the
 *   response's press, which differ only for a held key's response.
 *
 *   ---------------------------------------------------------------------
 *   CROSSINGS (choice.cross)
 *   ---------------------------------------------------------------------
 *   A choice with cross set reads SAMPLE events of its kind, device and
 *   control: a gamepad trigger or stick past a level, pen pressure onset,
 *   mouse movement onset.
 *     RISING    a sample >= level while armed is a crossing at that
 *               sample's t; armed again when the value drops below level -
 *               hysteresis
 *     FALLING   a sample <= level while armed; armed again above level +
 *               hysteresis
 *     DISTANCE  (x, y) at a distance >= level from where the channel was
 *               at arm() (or its first sample after arm()): movement onset
 *   A channel starts armed (at rest). A channel past its level at arm() is
 *   held: no crossing until it re-arms, whatever allow_held_key says, and
 *   HELD_AT_OPEN is set. No interpolation between samples: SDL sends an
 *   axis when it changes, so the signal is a step, and an interpolated time
 *   would claim a precision the samples do not have.
 *
 *   ---------------------------------------------------------------------
 *   TRACE (desc.trace)
 *   ---------------------------------------------------------------------
 *   Every input of desc.trace_kinds (0 = every kind) fed between arm() and
 *   finish() is copied into desc.trace (the caller's array, 48 bytes each):
 *   pointer, pen, touch and stick traces for mouse-tracking and drawing
 *   tasks. A full trace counts what it drops (TRACE_FULL). Mouse motion at
 *   1 kHz for 2 s is 96 KB.
 *
 *   ---------------------------------------------------------------------
 *   THE RESULT (yrsp_result) AND THE DATA ROW
 *   ---------------------------------------------------------------------
 *   jsPsych's names: rt, response, rt_key_duration. Then the response's
 *   kind, scancode (control), keycode, device and stamp tier and bounds;
 *   the onset's time, tier, source, residual, uncertainty and frame;
 *   counts; flags (YRSP_R_*). yrsp_format_header() and
 *   yrsp_format_row() write them as CSV: times in seconds, NaN as an
 *   empty field. The entries (yrsp_entries()) hold every press of the
 *   window, in arrival order.
 *
 *   ---------------------------------------------------------------------
 *   SDL3 AND RAW MICE
 *   ---------------------------------------------------------------------
 *   ysp/input.h's adapters (yin_from_sdl, yin_from_mouse) under this
 *   header's old names (yrsp_from_sdl, yrsp_from_mouse): see there.
 *   ---------------------------------------------------------------------
 *   LIMITS, THREADS AND MEMORY
 *   ---------------------------------------------------------------------
 *   YRSP_MAX_CHOICES 32 choices; YRSP_LOG_CAP 64 entries per window,
 *   the last YRSP_LOG_RESERVE (8) of them only for presses of a choice,
 *   so presses of other keys cannot push the response out (LOG_FULL
 *   counts what did not fit); YRSP_MAX_SOURCES 8; 64 controls tracked
 *   as down (per device); 16 recent presses for R3. The collector is
 *   about 6 KB, caller-allocated; nothing is allocated. One thread: a
 *   reader thread (a serial port, an eye tracker) queues its events, and
 *   the frame thread feeds them.
 *
 *   ---------------------------------------------------------------------
 *   NOT DONE
 *   ---------------------------------------------------------------------
 *   Output to a ysp/rt.h ring (v0.2); stimulus boxes at each flip for
 *   mouse tracking; velocity-based movement onset; chords and modifiers as
 *   part of a choice (entries log the modifiers); a protocol parser for any
 *   response box; text entry.
 *
 *   ---------------------------------------------------------------------
 *   BUILDING
 *   ---------------------------------------------------------------------
 *   Nothing to link but the math library. Define YRSP_API to override the
 *   default `extern` linkage.
 *
 *   ---------------------------------------------------------------------
 *   LICENSE: public domain / MIT-0, see end of file.
 */
#ifndef YSP_RESPONSE_H_INCLUDED
#define YSP_RESPONSE_H_INCLUDED

#define YRSP_VERSION_MAJOR 0
#define YRSP_VERSION_MINOR 1
#define YRSP_VERSION_PATCH 4
#define YRSP_VERSION_STRING "0.1.4"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "ysp/input.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef YRSP_API
#define YRSP_API extern
#endif

#define YRSP_MAX_CHOICES  32
#define YRSP_MAX_SOURCES  8
#define YRSP_LOG_CAP      64
#define YRSP_LOG_RESERVE  8
#define YRSP_MAX_HELD     64
#define YRSP_MAX_RECENT   16

/* Status: what feed(), set_onset() and update() return. */
#define YRSP_IDLE      0   /* not armed, or finished                    */
#define YRSP_OPEN      1   /* armed, not ended                          */
#define YRSP_ENDED     2   /* a response or the deadline ended it       */
#define YRSP_ERR_ARG   (-1)
#define YRSP_ERR_STATE (-2) /* the collector was not initialized        */

/* ysp/input.h's names under this header's prefix: ysp/response.h's API
 * as it was before the split (v0.1.2). */
typedef yin_kind   yrsp_kind;
typedef yin_type   yrsp_type;
typedef yin_tier   yrsp_tier;
typedef yin_event  yrsp_input;
typedef yin_source yrsp_source;
#define YRSP_KIND_KEYBOARD     YIN_KIND_KEYBOARD
#define YRSP_KIND_MOUSE        YIN_KIND_MOUSE
#define YRSP_KIND_TOUCH        YIN_KIND_TOUCH
#define YRSP_KIND_PEN          YIN_KIND_PEN
#define YRSP_KIND_GAMEPAD      YIN_KIND_GAMEPAD
#define YRSP_KIND_BOX          YIN_KIND_BOX
#define YRSP_KIND_EYE          YIN_KIND_EYE
#define YRSP_KIND_SYNC         YIN_KIND_SYNC
#define YRSP_KIND_USER         YIN_KIND_USER
#define YRSP_KIND_BIT(k)       YIN_KIND_BIT(k)
#define YRSP_KINDS_ALL         YIN_KINDS_ALL
#define YRSP_NONE              YIN_NONE
#define YRSP_PRESS             YIN_PRESS
#define YRSP_RELEASE           YIN_RELEASE
#define YRSP_SAMPLE            YIN_SAMPLE
#define YRSP_PROXIMITY         YIN_PROXIMITY
#define YRSP_IN_REPEAT         YIN_REPEAT
#define YRSP_IN_SYNTHETIC      YIN_SYNTHETIC
#define YRSP_IN_ERASER         YIN_ERASER
#define YRSP_IN_UNLISTED       YIN_UNLISTED
#define YRSP_AXIS_POSITION     YIN_AXIS_POSITION
#define YRSP_AXIS_PEN          YIN_AXIS_PEN
#define YRSP_AXIS_PEN_PRESSURE YIN_AXIS_PEN_PRESSURE
#define YRSP_AXIS_GAMEPAD      YIN_AXIS_GAMEPAD
#define YRSP_AXIS_DELTA        YIN_AXIS_DELTA
#define YRSP_AXIS_ABSOLUTE     YIN_AXIS_ABSOLUTE
#define YRSP_AXIS_WHEEL        YIN_AXIS_WHEEL
#define YRSP_TIER_UNKNOWN      YIN_TIER_UNKNOWN
#define YRSP_TIER_1            YIN_TIER_1
#define YRSP_TIER_2            YIN_TIER_2
#define YRSP_TIER_3            YIN_TIER_3
#define YRSP_TIER_SIM          YIN_TIER_SIM
#define yrsp_sdl_mouse_stamp   yin_sdl_mouse_stamp
/* defined by ysp/input.h when SDL3, or ysp/screen.h v0.3.5, came first */
#define yrsp_sdl_ctx           yin_sdl_ctx
#define yrsp_from_sdl          yin_from_sdl
#define yrsp_sdl_source        yin_sdl_source
#define yrsp_from_mouse        yin_from_mouse
#define yrsp_raw_mouse_source  yin_raw_mouse_source

/* One participant's cursor from one mouse's raw movement: each DELTA
 * sample of `device` (0 = any) moves it by gain pixels per count, clamped
 * to [0, w] x [0, h]. No acceleration: the gain is the design's, stated in
 * the data. Absolute devices (ABSOLUTE samples) are not integrated. */
typedef struct yrsp_cursor {
    uint32_t device;
    float    gain;             /* pixels per count; 0 = 1                  */
    float    w, h;             /* the area, pixels                         */
    float    x, y;             /* the position, start it where it belongs  */
} yrsp_cursor;

/* Returns 1 when the sample moved this cursor. */
static inline int yrsp_cursor_feed(yrsp_cursor* c, const yrsp_input* in) {
    float g = c->gain > 0 ? c->gain : 1.0f;
    if (in->kind != YRSP_KIND_MOUSE || in->type != YRSP_SAMPLE || in->control != YRSP_AXIS_DELTA) return 0;
    if (c->device && in->device != c->device) return 0;
    c->x += g * in->x;
    c->y += g * in->y;
    if (c->x < 0) c->x = 0;
    if (c->x > c->w) c->x = c->w;
    if (c->y < 0) c->y = 0;
    if (c->y > c->h) c->y = c->h;
    return 1;
}

typedef enum yrsp_match {
    YRSP_MATCH_SCANCODE = 0, /* the physical key                         */
    YRSP_MATCH_KEYCODE  = 1  /* the layout's key                         */
} yrsp_match;

typedef enum yrsp_cross {
    YRSP_CROSS_NONE     = 0, /* a PRESS                                  */
    YRSP_CROSS_RISING   = 1,
    YRSP_CROSS_FALLING  = 2,
    YRSP_CROSS_DISTANCE = 3
} yrsp_cross;

/* One accepted response. A zeroed choice is keyboard scancode 0, which
 * matches no key: set key, control or code. */
typedef struct yrsp_choice {
    const char* key;           /* a key name (CHOICES), resolved at init   */
    const char* name;          /* the response column; NULL = key          */
    uint8_t     kind;          /* yrsp_kind; 0 = keyboard                */
    uint8_t     match;         /* yrsp_match                             */
    uint8_t     cross;         /* yrsp_cross                             */
    uint8_t     reserved_;
    uint32_t    device;        /* 0 = any                                  */
    uint32_t    control;       /* scancode, button, axis                   */
    uint32_t    code;          /* keycode, with match = KEYCODE            */
    float       level;         /* crossings: the threshold                 */
    float       hysteresis;    /* crossings: re-arm this far back; >= 0    */
} yrsp_choice;

typedef enum yrsp_choices {
    YRSP_CHOICES_ALL  = 0,
    YRSP_CHOICES_LIST = 1,
    YRSP_CHOICES_NONE = 2
} yrsp_choices;

/* The rules, constant over a block. Zero is the jsPsych default trial: any
 * key, no minimum, no deadline, the first press ends it, held keys do not
 * count. Durations in seconds. */
typedef struct yrsp_desc {
    const yrsp_choice* choices;   /* copied at init                      */
    int       n_choices;            /* > 0 implies LIST                    */
    int       mode;                 /* yrsp_choices                      */
    uint16_t  kinds;                /* ALL mode: 0 = THE KEYBOARD ONLY;
                                     * YRSP_KINDS_ALL = every kind but
                                     * SYNC                                */
    double    minimum_valid_rt;     /* s; earlier is ANTICIPATION, logged  */
    double    duration;             /* s from the onset; 0 = no deadline   */
    bool      persist;              /* the first response does not end it  */
    int       max_responses;        /* with persist: end after this many;
                                     * 0 = never                           */
    bool      allow_held_key;       /* a held key's press can count        */
    bool      wait_for_key_release; /* end at the response's release       */
    double    max_hold;             /* s; 0 = 2                            */
    double    dedup;                /* s; R3's window; 0 = 0.050, < 0 off  */
    double    settle;               /* s after the deadline; 0 = none      */
    const yrsp_source* sources;   /* copied at init                      */
    int       n_sources;
    yrsp_input* trace;            /* the caller's array; NULL = none     */
    int       trace_cap;
    uint16_t  trace_kinds;          /* 0 = every kind                      */
} yrsp_desc;

typedef enum yrsp_onset_src {
    YRSP_ONSET_OTHER = 0,
    YRSP_ONSET_FLIP  = 1,         /* yscr_record.onset                 */
    YRSP_ONSET_AUDIO = 2,         /* yau_onset.onset                   */
    YRSP_ONSET_PLAN  = 3          /* a prediction                        */
} yrsp_onset_src;

typedef struct yrsp_onset {
    int64_t t;                      /* ysp_rt ns                           */
    int64_t residual;               /* ns, the landing's or the record's   */
    int64_t uncertainty;            /* ns, half-width you state; 0 = none  */
    int64_t frame;                  /* flip index, or -1                   */
    uint8_t tier;                   /* the producer's tier                 */
    uint8_t src;                    /* yrsp_onset_src                    */
    uint8_t final;                  /* 1: a completed record; 0: a plan    */
    uint8_t reserved_[5];
} yrsp_onset;

/* Entry flags */
#define YRSP_E_VALID        0x0001u /* a response                        */
#define YRSP_E_EARLY        0x0002u /* before the onset, or no onset     */
#define YRSP_E_ANTICIPATION 0x0004u /* 0 <= rt < minimum_valid_rt         */
#define YRSP_E_LATE         0x0008u /* rt >= duration                    */
#define YRSP_E_AFTER_END    0x0010u /* at or after the press that ended it */
#define YRSP_E_NOT_CHOICE   0x0020u /* a press that is no choice         */
#define YRSP_E_DUPLICATE    0x0040u /* a second report (R3)              */
#define YRSP_E_HELD         0x0080u /* a held press, allow_held_key      */
#define YRSP_E_CROSSING     0x0100u /* a threshold crossing              */
#define YRSP_E_RECLASSIFIED 0x0200u /* its class changed after arrival   */
#define YRSP_E_RELEASED     0x0400u /* t_release is set                  */
#define YRSP_E_ENDED        0x0800u /* it ended the window               */
#define YRSP_E_CLASS_MASK   0x000Fu

/* One press or crossing of the window. 48 bytes. */
typedef struct yrsp_entry {
    int64_t  t;                     /* ysp_rt ns                           */
    int64_t  t_release;             /* 0 = none                            */
    uint32_t device;
    uint32_t control;               /* scancode, button, axis              */
    uint32_t code;                  /* keycode                             */
    int16_t  choice;                /* index into choices; -1 = none       */
    uint16_t flags;                 /* YRSP_E_*                          */
    uint8_t  kind;
    uint8_t  stamp;                 /* its tier                            */
    uint16_t mods;
    float    value;
    uint8_t  arrival_class;         /* class bits when it arrived          */
    uint8_t  reserved_[3];
} yrsp_entry;

/* Result flags */
#define YRSP_R_RESPONDED    0x0001u
#define YRSP_R_TIMEOUT      0x0002u /* a deadline, no response           */
#define YRSP_R_ENDED_EARLY  0x0004u /* ended on a press that no longer counts */
#define YRSP_R_ONSET_PLAN   0x0008u /* RT from a plan, no final onset    */
#define YRSP_R_NO_ONSET     0x0010u /* no onset: no RT                   */
#define YRSP_R_LOG_FULL     0x0020u
#define YRSP_R_TRACE_FULL   0x0040u
#define YRSP_R_RECLASSIFIED 0x0080u
#define YRSP_R_HELD_AT_OPEN 0x0100u /* a choice was down at arm()        */
#define YRSP_R_HOLD_TIMEOUT 0x0200u /* wait_for_key_release gave up      */

typedef struct yrsp_result {
    double      rt;                 /* s; NaN = none                       */
    double      rt_key_duration;    /* s; NaN = no release                 */
    int32_t     response;           /* choice index; -1 = none or ALL      */
    int32_t     entry;              /* the response's entry; -1 = none     */
    const char* response_name;      /* the choice's name, or NULL          */
    int64_t     t_response;         /* ysp_rt ns; 0 = none                 */
    uint32_t    control, code, device;
    uint8_t     kind;
    uint8_t     stamp_tier;
    uint8_t     stamp_partial;
    uint8_t     onset_tier;
    int32_t     stamp_lo_us, stamp_hi_us;
    uint8_t     onset_src;
    uint8_t     reserved_[3];
    uint32_t    flags;              /* YRSP_R_*                          */
    int64_t     t_onset;            /* 0 = none                            */
    int64_t     onset_residual;
    int64_t     onset_uncertainty;
    int64_t     onset_frame;
    int32_t     n_responses, n_early, n_anticipations, n_late, n_after_end,
                n_duplicates, n_not_choice, n_held, n_entries, n_lost, n_trace;
} yrsp_result;

/* Private state, sized for no allocation. */
typedef struct yrsp__chan { float v, x, y, rx, ry; uint8_t has, armed, has_ref, reserved_; } yrsp__chan;
typedef struct yrsp__held { uint32_t device, control; uint8_t kind, used, reserved_[2]; } yrsp__held;
typedef struct yrsp__recent { int64_t t; uint32_t device, control; uint8_t kind, used, reserved_[2]; } yrsp__recent;

/* The collector. Caller-allocated; every field is private. */
typedef struct yrsp_collector {
    yrsp_desc    d;
    yrsp_choice  ch[YRSP_MAX_CHOICES];
    yrsp_source  src[YRSP_MAX_SOURCES];
    int64_t        min_rt_ns, duration_ns, max_hold_ns, dedup_ns, settle_ns;
    yrsp_onset   onset;
    int64_t        t_arm, t_end;
    int32_t        ok, armed, finished, have_onset;
    int32_t        status;
    int32_t        ended_by;        /* 0, 1 a response, 2 the deadline, 3 a hold */
    int32_t        end_entry;       /* -1                                   */
    int32_t        closing;         /* waiting for end_entry's release      */
    yrsp__held   held[YRSP_MAX_HELD];
    yrsp__recent recent[YRSP_MAX_RECENT];
    int32_t        recent_next;
    yrsp__chan   chan[YRSP_MAX_CHOICES];
    yrsp_entry   log[YRSP_LOG_CAP];
    int32_t        n_log, n_trace, n_lost, n_lost_trace, n_held, n_stray;
    uint32_t       held_at_open;
    char           error[160];
} yrsp_collector;

YRSP_API const char* yrsp_version(void);
YRSP_API const char* yrsp_strerror(int code);

/* Validates and copies the rules (choices and sources too); clears the
 * held state. false with yrsp_error() set. */
YRSP_API bool        yrsp_init(yrsp_collector* c, const yrsp_desc* d);
YRSP_API const char* yrsp_error(const yrsp_collector* c);

/* Starts a window at t: clears the entries, the onset and the trace
 * count; keeps which keys are down. */
YRSP_API void yrsp_arm(yrsp_collector* c, int64_t t);

/* Sets or refines the onset and classifies every entry again. A plan
 * (final 0) does not replace a final onset. Returns the status. */
YRSP_API int  yrsp_set_onset(yrsp_collector* c, const yrsp_onset* o);

/* One input, armed or not. Returns the status, or YRSP_ERR_ARG for a
 * type or kind out of range. */
YRSP_API int  yrsp_feed(yrsp_collector* c, const yrsp_input* in);

/* The clock without input: ENDED at onset + duration + settle, or at the
 * press + max_hold while waiting for a release. Call it once a frame with
 * f.onset or yrt_now_ns(). */
YRSP_API int  yrsp_update(yrsp_collector* c, int64_t now);

/* Classifies against the onset as it is now and fills *out. Ends the
 * window: no later press ends anything, and the trace stops. Presses fed
 * until the next arm() are still logged and classified (a report stamped
 * before the end and delivered after it still counts; a press after a
 * finish() that came before any end is AFTER_END), and releases pair, so
 * call it again for the final result (a release, the final onset, a late
 * report). Returns YRSP_ENDED, or an error. */
YRSP_API int  yrsp_finish(yrsp_collector* c, yrsp_result* out);

/* The window's entries in arrival order; valid until the next arm(). */
YRSP_API const yrsp_entry* yrsp_entries(const yrsp_collector* c, int* n);

/* Key names on the US layout (CHOICES): the scancode of a name, -1 when
 * unknown; the name of a scancode, NULL when it has none. */
YRSP_API int         yrsp_scancode(const char* name);
YRSP_API const char* yrsp_key_name(uint32_t scancode);

/* The CSV header and a result's row: rt, response, rt_key_duration,
 * rsp_kind, rsp_scancode, rsp_keycode, rsp_device, rsp_tier, rsp_stamp_lo,
 * rsp_stamp_hi, rsp_partial, onset_tier, onset_src, onset_residual,
 * onset_uncertainty, onset_frame, n_responses, n_anticipations, n_early,
 * n_late, n_duplicates, rsp_flags. Seconds; NaN as an empty field. No
 * newline. Return snprintf's count. */
YRSP_API int yrsp_format_header(char* buf, size_t cap);
YRSP_API int yrsp_format_row(const yrsp_result* r, char* buf, size_t cap);

/* --- helpers for the other headers, inline, when they came first ----- */

#if defined(YSP_SCREEN_H_INCLUDED)
/* The onset of a flip record: final once shown. A frame never shown
 * (SKIPPED, CANCELED: onset 0) gives its planned vblank as a plan. */
static inline yrsp_onset yrsp_onset_flip(const yscr_record* r) {
    yrsp_onset o;
    o.t = r->onset ? r->onset : r->planned;
    o.residual = r->residual;
    o.uncertainty = 0;
    o.frame = r->index;
    o.tier = r->tier;
    o.src = (uint8_t)(r->onset ? YRSP_ONSET_FLIP : YRSP_ONSET_PLAN);
    o.final = (uint8_t)(r->onset != 0 && !(r->flags & YSCR_FLIP_PENDING));
    o.reserved_[0] = o.reserved_[1] = o.reserved_[2] = o.reserved_[3] = o.reserved_[4] = 0;
    return o;
}
#endif


#if defined(YSP_AUDIO_H_INCLUDED)
/* The onset of a sound: final once it is not pending. */
static inline yrsp_onset yrsp_onset_audio(const yau_onset* r) {
    yrsp_onset o;
    o.t = r->onset;
    o.residual = r->residual;
    o.uncertainty = 0;
    o.frame = -1;
    o.tier = r->tier;
    o.src = (uint8_t)YRSP_ONSET_AUDIO;
    o.final = (uint8_t)(r->onset != 0 && !(r->flags & YAU_ONSET_PENDING));
    o.reserved_[0] = o.reserved_[1] = o.reserved_[2] = o.reserved_[3] = o.reserved_[4] = 0;
    return o;
}
#endif

#if defined(YSP_TIMELINE_H_INCLUDED)
/* The plan from a fired timeline event: the landing frame's predicted
 * onset and the landing residual. */
static inline yrsp_onset yrsp_onset_landing(const ytl_event* e) {
    yrsp_onset o;
    o.t = e->onset;
    o.residual = e->residual;
    o.uncertainty = 0;
    o.frame = e->frame;
    o.tier = 0;
    o.src = (uint8_t)YRSP_ONSET_PLAN;
    o.final = 0;
    o.reserved_[0] = o.reserved_[1] = o.reserved_[2] = o.reserved_[3] = o.reserved_[4] = 0;
    return o;
}
#endif


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* YSP_RESPONSE_H_INCLUDED */

/* =======================================================================
 *
 * IMPLEMENTATION
 *
 * ======================================================================= */
#ifdef YSP_RESPONSE_IMPLEMENTATION
#ifndef YSP_RESPONSE_IMPLEMENTATION_GUARD
#define YSP_RESPONSE_IMPLEMENTATION_GUARD

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef NAN
    #define YRSP__NAN ((double)NAN)
#else
    #define YRSP__NAN (HUGE_VAL - HUGE_VAL)
#endif

#define YRSP__NS_PER_S 1000000000.0

YRSP_API const char* yrsp_version(void) { return YRSP_VERSION_STRING; }

YRSP_API const char* yrsp_strerror(int code) {
    switch (code) {
    case YRSP_IDLE:      return "idle";
    case YRSP_OPEN:      return "open";
    case YRSP_ENDED:     return "ended";
    case YRSP_ERR_ARG:   return "bad argument";
    case YRSP_ERR_STATE: return "the collector is not initialized";
    default:               return "unknown";
    }
}

YRSP_API const char* yrsp_error(const yrsp_collector* c) {
    return c ? c->error : "ysp_response: NULL collector";
}

/* isfinite() is C99 and MSVC's default C dialect does not declare it. */
static bool yrsp__finite(double x) { return x > -HUGE_VAL && x < HUGE_VAL; }

static bool yrsp__fail(yrsp_collector* c, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->error, sizeof c->error, fmt, ap);
    va_end(ap);
    c->ok = 0;
    return false;
}

/* --- key names --------------------------------------------------------- */

typedef struct yrsp__name { const char* name; uint16_t sc; uint16_t ch; } yrsp__name;

/* US layout. ch: the character the key types unshifted (its SDL keycode);
 * 0 for keys whose SDL keycode is the scancode with bit 30 set. */
static const yrsp__name yrsp__names[] = {
    { "enter", 40, 13 },  { "return", 40, 13 }, { "escape", 41, 27 }, { "esc", 41, 27 },
    { "backspace", 42, 8 }, { "tab", 43, 9 },   { "space", 44, 32 },  { " ", 44, 32 },
    { "-", 45, '-' },  { "=", 46, '=' },  { "[", 47, '[' },  { "]", 48, ']' },
    { "\\", 49, '\\' }, { ";", 51, ';' }, { "'", 52, '\'' }, { "`", 53, '`' },
    { ",", 54, ',' },  { ".", 55, '.' },  { "/", 56, '/' },
    { "f1", 58, 0 },  { "f2", 59, 0 },  { "f3", 60, 0 },  { "f4", 61, 0 },
    { "f5", 62, 0 },  { "f6", 63, 0 },  { "f7", 64, 0 },  { "f8", 65, 0 },
    { "f9", 66, 0 },  { "f10", 67, 0 }, { "f11", 68, 0 }, { "f12", 69, 0 },
    { "arrowright", 79, 0 }, { "right", 79, 0 }, { "arrowleft", 80, 0 }, { "left", 80, 0 },
    { "arrowdown", 81, 0 },  { "down", 81, 0 },  { "arrowup", 82, 0 },   { "up", 82, 0 },
};

static int yrsp__lower(int ch) { return ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch; }

static bool yrsp__name_eq(const char* a, const char* b) {
    while (*a && *b && yrsp__lower((unsigned char)*a) == yrsp__lower((unsigned char)*b)) { a++; b++; }
    return *a == 0 && *b == 0;
}

/* The scancode and keycode of a name; false when unknown. */
static bool yrsp__lookup(const char* name, uint32_t* sc, uint32_t* kc) {
    size_t i;
    if (!name || !name[0]) return false;
    if (name[1] == 0) {
        int ch = yrsp__lower((unsigned char)name[0]);
        if (ch >= 'a' && ch <= 'z') { *sc = (uint32_t)(4 + ch - 'a'); *kc = (uint32_t)ch; return true; }
        if (ch >= '1' && ch <= '9') { *sc = (uint32_t)(30 + ch - '1'); *kc = (uint32_t)ch; return true; }
        if (ch == '0') { *sc = 39; *kc = '0'; return true; }
    }
    for (i = 0; i < sizeof yrsp__names / sizeof yrsp__names[0]; i++)
        if (yrsp__name_eq(name, yrsp__names[i].name)) {
            *sc = yrsp__names[i].sc;
            *kc = yrsp__names[i].ch ? yrsp__names[i].ch : (yrsp__names[i].sc | 0x40000000u);
            return true;
        }
    return false;
}

YRSP_API int yrsp_scancode(const char* name) {
    uint32_t sc, kc;
    return yrsp__lookup(name, &sc, &kc) ? (int)sc : -1;
}

YRSP_API const char* yrsp_key_name(uint32_t sc) {
    static const char letters[26][2] = { "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
                                         "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z" };
    static const char digits[10][2] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" };
    size_t i;
    if (sc >= 4 && sc <= 29) return letters[sc - 4];
    if (sc >= 30 && sc <= 39) return digits[sc - 30];
    for (i = 0; i < sizeof yrsp__names / sizeof yrsp__names[0]; i++)
        if (yrsp__names[i].sc == sc && yrsp__names[i].name[0] != ' ' &&
            strcmp(yrsp__names[i].name, "return") && strcmp(yrsp__names[i].name, "esc") &&
            strncmp(yrsp__names[i].name, "arrow", 5))
            return yrsp__names[i].name;
    return NULL;
}

/* --- init ---------------------------------------------------------------- */

static bool yrsp__dur(yrsp_collector* c, const char* what, double s, int64_t* out) {
    if (!(s >= 0.0) || s > 1e6) return yrsp__fail(c, "ysp_response: %s %g is not a duration in s (0..1e6)", what, s);
    *out = (int64_t)llround(s * YRSP__NS_PER_S);
    return true;
}

YRSP_API bool yrsp_init(yrsp_collector* c, const yrsp_desc* d) {
    int i;
    if (!c) return false;
    memset(c, 0, sizeof *c);
    if (!d) return yrsp__fail(c, "ysp_response: NULL desc");
    c->d = *d;
    if (d->n_choices < 0 || d->n_choices > YRSP_MAX_CHOICES)
        return yrsp__fail(c, "ysp_response: n_choices %d is outside 0..%d", d->n_choices, YRSP_MAX_CHOICES);
    if (d->n_choices > 0 && !d->choices) return yrsp__fail(c, "ysp_response: n_choices %d with NULL choices", d->n_choices);
    if (d->mode < YRSP_CHOICES_ALL || d->mode > YRSP_CHOICES_NONE)
        return yrsp__fail(c, "ysp_response: mode %d is not a yrsp_choices", d->mode);
    if (d->mode == YRSP_CHOICES_NONE && d->n_choices > 0)
        return yrsp__fail(c, "ysp_response: mode NONE takes no choices");
    if (d->n_choices > 0) c->d.mode = YRSP_CHOICES_LIST;
    if (c->d.mode == YRSP_CHOICES_LIST && d->n_choices == 0)
        return yrsp__fail(c, "ysp_response: mode LIST with no choices");
    if (c->d.kinds == 0) c->d.kinds = YRSP_KIND_BIT(YRSP_KIND_KEYBOARD);
    /* "every kind" means every response: a scanner pulse arriving in an
     * ALL_KEYS trial must not end it. Only an explicit mask takes SYNC. */
    if (c->d.kinds == YRSP_KINDS_ALL) c->d.kinds = (uint16_t)(c->d.kinds & ~YRSP_KIND_BIT(YRSP_KIND_SYNC));
    for (i = 0; i < d->n_choices; i++) {
        yrsp_choice* h = &c->ch[i];
        *h = d->choices[i];
        if (h->kind > 15) return yrsp__fail(c, "ysp_response: choice %d: kind %d is outside 0..15", i, h->kind);
        if (h->match > YRSP_MATCH_KEYCODE) return yrsp__fail(c, "ysp_response: choice %d: match %d", i, h->match);
        if (h->cross > YRSP_CROSS_DISTANCE) return yrsp__fail(c, "ysp_response: choice %d: cross %d", i, h->cross);
        if (h->key) {
            uint32_t sc, kc;
            if (h->kind != YRSP_KIND_KEYBOARD || h->cross)
                return yrsp__fail(c, "ysp_response: choice %d: a key name on a choice that is no key", i);
            if (!yrsp__lookup(h->key, &sc, &kc))
                return yrsp__fail(c, "ysp_response: choice %d: '%s' is not a key name (CHOICES)", i, h->key);
            if (h->match == YRSP_MATCH_SCANCODE) {
                if (h->control && h->control != sc)
                    return yrsp__fail(c, "ysp_response: choice %d: key '%s' is scancode %u, control says %u", i,
                                        h->key, (unsigned)sc, (unsigned)h->control);
                h->control = sc;
            } else {
                if (h->code && h->code != kc)
                    return yrsp__fail(c, "ysp_response: choice %d: key '%s' is keycode %u, code says %u", i,
                                        h->key, (unsigned)kc, (unsigned)h->code);
                h->code = kc;
            }
            if (!h->name) h->name = h->key;
        }
        if (h->cross) {
            if (h->kind == YRSP_KIND_KEYBOARD)
                return yrsp__fail(c, "ysp_response: choice %d: a keyboard has no analog channel to cross", i);
            if (!yrsp__finite(h->level) || !yrsp__finite(h->hysteresis) || h->hysteresis < 0)
                return yrsp__fail(c, "ysp_response: choice %d: level %g and hysteresis %g must be finite, "
                                    "hysteresis >= 0", i, (double)h->level, (double)h->hysteresis);
            if (h->cross == YRSP_CROSS_DISTANCE && !(h->level > 0))
                return yrsp__fail(c, "ysp_response: choice %d: a DISTANCE level must be > 0", i);
        } else if (h->kind == YRSP_KIND_KEYBOARD && h->match == YRSP_MATCH_SCANCODE && h->control == 0) {
            return yrsp__fail(c, "ysp_response: choice %d: no key (set key or control)", i);
        } else if (h->match == YRSP_MATCH_KEYCODE && h->code == 0) {
            return yrsp__fail(c, "ysp_response: choice %d: KEYCODE with no code", i);
        }
    }
    c->d.choices = c->ch;
    if (d->n_sources < 0 || d->n_sources > YRSP_MAX_SOURCES)
        return yrsp__fail(c, "ysp_response: n_sources %d is outside 0..%d", d->n_sources, YRSP_MAX_SOURCES);
    if (d->n_sources > 0 && !d->sources) return yrsp__fail(c, "ysp_response: n_sources %d with NULL sources", d->n_sources);
    for (i = 0; i < d->n_sources; i++) {
        c->src[i] = d->sources[i];
        if (c->src[i].kind > 15 || c->src[i].tier > YRSP_TIER_SIM || c->src[i].lo_us > c->src[i].hi_us)
            return yrsp__fail(c, "ysp_response: source %d: kind, tier or bounds out of range", i);
    }
    c->d.sources = c->src;
    if (d->trace_cap < 0 || (d->trace_cap > 0 && !d->trace))
        return yrsp__fail(c, "ysp_response: trace_cap %d with %s trace", d->trace_cap, d->trace ? "a" : "a NULL");
    if (d->max_responses < 0) return yrsp__fail(c, "ysp_response: max_responses %d < 0", d->max_responses);
    if (!yrsp__dur(c, "minimum_valid_rt", d->minimum_valid_rt, &c->min_rt_ns) ||
        !yrsp__dur(c, "duration", d->duration, &c->duration_ns) ||
        !yrsp__dur(c, "max_hold", d->max_hold, &c->max_hold_ns) ||
        !yrsp__dur(c, "settle", d->settle, &c->settle_ns))
        return false;
    if (c->duration_ns > 0 && c->min_rt_ns >= c->duration_ns)
        return yrsp__fail(c, "ysp_response: minimum_valid_rt %g s leaves no time in a duration of %g s",
                            d->minimum_valid_rt, d->duration);
    if (c->max_hold_ns == 0) c->max_hold_ns = 2000000000;
    if (d->dedup != d->dedup) return yrsp__fail(c, "ysp_response: dedup is NaN");
    if (d->dedup < 0) c->dedup_ns = 0;
    else if (!yrsp__dur(c, "dedup", d->dedup == 0 ? 0.050 : d->dedup, &c->dedup_ns)) return false;
    c->end_entry = -1;
    c->status = YRSP_IDLE;
    c->ok = 1;
    c->error[0] = 0;
    return true;
}

/* --- state ------------------------------------------------------------- */

/* Two reports can be of one press when their devices are equal or one is
 * 0: SDL's message path and injected input carry no device (DEVICES). Two
 * different devices are two presses. */
static bool yrsp__same_dev(uint32_t a, uint32_t b) { return a == b || a == 0 || b == 0; }

static bool yrsp__is_down(const yrsp_collector* c, const yrsp_input* in) {
    int i;
    for (i = 0; i < YRSP_MAX_HELD; i++) {
        const yrsp__held* h = &c->held[i];
        if (h->used && h->kind == in->kind && h->control == in->control && yrsp__same_dev(h->device, in->device))
            return true;
    }
    return false;
}

/* Down on another device, both known: SDL keeps one key state for all
 * keyboards, so a second keyboard's press of a held key comes as a repeat. */
static bool yrsp__down_elsewhere(const yrsp_collector* c, const yrsp_input* in) {
    int i;
    if (!in->device) return false;
    for (i = 0; i < YRSP_MAX_HELD; i++) {
        const yrsp__held* h = &c->held[i];
        if (h->used && h->kind == in->kind && h->control == in->control && h->device && h->device != in->device)
            return true;
    }
    return false;
}

static void yrsp__set_down(yrsp_collector* c, const yrsp_input* in, bool down) {
    int i, free_i = -1;
    for (i = 0; i < YRSP_MAX_HELD; i++) {
        yrsp__held* h = &c->held[i];
        if (!h->used) { if (free_i < 0) free_i = i; continue; }
        if (h->kind != in->kind || h->control != in->control) continue;
        if (!down && yrsp__same_dev(h->device, in->device)) h->used = 0;
        else if (down && h->device == in->device) return;
    }
    if (down && free_i >= 0) {
        c->held[free_i].used = 1;
        c->held[free_i].kind = in->kind;
        c->held[free_i].device = in->device;
        c->held[free_i].control = in->control;
    }
}

static const yrsp_source* yrsp__source(const yrsp_collector* c, int kind, uint32_t device) {
    int i;
    const yrsp_source* any = NULL;
    for (i = 0; i < c->d.n_sources; i++) {
        if (c->src[i].kind != kind) continue;
        if (c->src[i].device == device && device != 0) return &c->src[i];
        if (c->src[i].device == 0 && !any) any = &c->src[i];
    }
    return any;
}

/* The class bits of an entry against the onset as it is now. */
static uint16_t yrsp__time_class(const yrsp_collector* c, int64_t t) {
    int64_t rt;
    if (!c->have_onset) return YRSP_E_EARLY;
    rt = t - c->onset.t;
    if (rt < 0) return YRSP_E_EARLY;
    if (rt < c->min_rt_ns) return YRSP_E_ANTICIPATION;
    if (c->duration_ns > 0 && rt >= c->duration_ns) return YRSP_E_LATE;
    return 0;
}

static uint16_t yrsp__class(const yrsp_collector* c, const yrsp_entry* e) {
    uint16_t k = yrsp__time_class(c, e->t);
    if (k) return k;
    if (e->flags & (YRSP_E_NOT_CHOICE | YRSP_E_DUPLICATE | YRSP_E_AFTER_END)) return 0;
    if ((e->flags & YRSP_E_HELD) && !c->d.allow_held_key) return 0;
    return YRSP_E_VALID;
}

static void yrsp__reclassify(yrsp_collector* c) {
    int i;
    for (i = 0; i < c->n_log; i++) {
        yrsp_entry* e = &c->log[i];
        e->flags = (uint16_t)((e->flags & ~YRSP_E_CLASS_MASK) | yrsp__class(c, e));
    }
}

/* Ends the window once enough responses are in (THE WINDOW). */
static void yrsp__check_end(yrsp_collector* c) {
    int need, i, k, best = -1;
    int64_t prev_t = INT64_MIN;
    int prev_i = -1;
    if (!c->armed || c->finished || c->status != YRSP_OPEN || c->closing) return;
    need = c->d.persist ? c->d.max_responses : 1;
    if (need <= 0) return;
    /* the need-th earliest response, ties by arrival */
    for (k = 0; k < need; k++) {
        best = -1;
        for (i = 0; i < c->n_log; i++) {
            const yrsp_entry* e = &c->log[i];
            if (!(e->flags & YRSP_E_VALID)) continue;
            if (e->t < prev_t || (e->t == prev_t && i <= prev_i)) continue;
            if (best < 0 || e->t < c->log[best].t) best = i;
        }
        if (best < 0) return;
        prev_t = c->log[best].t;
        prev_i = best;
    }
    c->end_entry = best;
    c->t_end = c->log[best].t;
    c->log[best].flags |= YRSP_E_ENDED;
    for (i = 0; i < c->n_log; i++)
        if ((c->log[i].t > c->t_end || (c->log[i].t == c->t_end && i > best)) &&
            !(c->log[i].flags & YRSP_E_AFTER_END)) {
            c->log[i].flags |= YRSP_E_AFTER_END;
            c->log[i].flags = (uint16_t)((c->log[i].flags & ~YRSP_E_CLASS_MASK) | yrsp__class(c, &c->log[i]));
        }
    if (c->d.wait_for_key_release && !(c->log[best].flags & YRSP_E_CROSSING) &&
        !(c->log[best].flags & YRSP_E_RELEASED)) {
        c->closing = 1;
        return;
    }
    c->status = YRSP_ENDED;
    c->ended_by = 1;
}

static int yrsp__log(yrsp_collector* c, const yrsp_input* in, int choice, uint16_t flags, float value) {
    yrsp_entry* e;
    const yrsp_source* s;
    bool match = !(flags & (YRSP_E_NOT_CHOICE | YRSP_E_DUPLICATE));
    int cap = match ? YRSP_LOG_CAP : YRSP_LOG_CAP - YRSP_LOG_RESERVE;
    if (c->n_log >= cap) { c->n_lost++; return -1; }
    e = &c->log[c->n_log];
    memset(e, 0, sizeof *e);
    e->t = in->t;
    e->device = in->device;
    e->control = in->control;
    e->code = in->code;
    e->choice = (int16_t)choice;
    e->kind = in->kind;
    e->mods = in->mods;
    e->value = value;
    s = yrsp__source(c, in->kind, in->device);
    e->stamp = in->stamp ? in->stamp : (uint8_t)(s ? s->tier : 0);
    /* a press at or after the one that ended the window is no response,
     * nor any press after a finish() that came before an end */
    if (((c->status == YRSP_ENDED && (c->ended_by == 1 || c->ended_by == 3)) || c->closing) &&
        in->t >= c->t_end)
        flags |= YRSP_E_AFTER_END;
    if (c->finished && c->ended_by == 0) flags |= YRSP_E_AFTER_END;
    e->flags = flags;
    e->flags |= yrsp__class(c, e);
    e->arrival_class = (uint8_t)(e->flags & YRSP_E_CLASS_MASK);
    return c->n_log++;
}

static int yrsp__match(const yrsp_collector* c, const yrsp_input* in) {
    int i;
    if (c->d.mode == YRSP_CHOICES_NONE) return -2;
    if (c->d.mode == YRSP_CHOICES_ALL) return (c->d.kinds >> in->kind) & 1u ? -1 : -2;
    for (i = 0; i < c->d.n_choices; i++) {
        const yrsp_choice* h = &c->ch[i];
        if (h->cross || h->kind != in->kind) continue;
        if (h->device && h->device != in->device) continue;
        if (h->match == YRSP_MATCH_KEYCODE ? h->code == in->code : h->control == in->control) return i;
    }
    return -2;
}

/* R3: a press of this control within dedup of the last accepted one. */
static bool yrsp__duplicate(yrsp_collector* c, const yrsp_input* in) {
    int i;
    if (c->dedup_ns <= 0) return false;
    for (i = 0; i < YRSP_MAX_RECENT; i++) {
        const yrsp__recent* r = &c->recent[i];
        int64_t dt;
        if (!r->used || r->kind != in->kind || r->control != in->control) continue;
        if (!yrsp__same_dev(r->device, in->device)) continue;
        dt = in->t - r->t;
        if (dt < c->dedup_ns && dt > -c->dedup_ns) return true;
    }
    return false;
}

static void yrsp__remember(yrsp_collector* c, const yrsp_input* in) {
    int i;
    yrsp__recent* r = NULL;
    for (i = 0; i < YRSP_MAX_RECENT; i++)
        if (c->recent[i].used && c->recent[i].kind == in->kind && c->recent[i].control == in->control &&
            c->recent[i].device == in->device) {
            r = &c->recent[i];
            break;
        }
    if (!r) {
        r = &c->recent[c->recent_next];
        c->recent_next = (c->recent_next + 1) % YRSP_MAX_RECENT;
    }
    r->used = 1;
    r->kind = in->kind;
    r->control = in->control;
    r->device = in->device;
    r->t = in->t;
}

/* Presses are logged from arm() to the next arm(), after finish() too, so
 * a report delivered late still counts and a second report is still seen. */
static bool yrsp__window(const yrsp_collector* c) { return c->armed != 0; }

static void yrsp__press(yrsp_collector* c, const yrsp_input* in) {
    int choice;
    bool held, repeat = (in->flags & YRSP_IN_REPEAT) != 0;
    /* a "repeat" of a key down only on another keyboard is this keyboard's
     * first press (one key state for all keyboards in SDL) */
    if (repeat && !yrsp__is_down(c, in) && yrsp__down_elsewhere(c, in)) repeat = false;
    if (!repeat && yrsp__duplicate(c, in)) {
        if (yrsp__window(c)) {
            choice = yrsp__match(c, in);
            yrsp__log(c, in, choice >= 0 ? choice : -1, YRSP_E_DUPLICATE, in->value);
        }
        return;
    }
    held = repeat || yrsp__is_down(c, in);
    if (!held) {
        yrsp__set_down(c, in, true);
        yrsp__remember(c, in);
    }
    if (!yrsp__window(c)) return;
    choice = yrsp__match(c, in);
    if (held) {
        if (choice >= -1 && c->d.allow_held_key) {
            yrsp__log(c, in, choice, YRSP_E_HELD, in->value);
            yrsp__check_end(c);
        } else {
            c->n_held++;
        }
        return;
    }
    if (choice < -1) yrsp__log(c, in, -1, YRSP_E_NOT_CHOICE, in->value);
    else yrsp__log(c, in, choice, 0, in->value);
    yrsp__check_end(c);
}

static void yrsp__release(yrsp_collector* c, const yrsp_input* in) {
    int i;
    if (!yrsp__is_down(c, in)) { c->n_stray++; return; }
    yrsp__set_down(c, in, false);
    if (!c->armed) return;
    /* every open entry of this control is one press: the press and, with
     * allow_held_key, its HELD repeats */
    for (i = 0; i < c->n_log; i++) {
        yrsp_entry* e = &c->log[i];
        if (e->kind != in->kind || e->control != in->control) continue;
        if (!yrsp__same_dev(e->device, in->device)) continue;
        if (e->flags & (YRSP_E_CROSSING | YRSP_E_DUPLICATE | YRSP_E_RELEASED)) continue;
        e->t_release = in->t;
        e->flags |= YRSP_E_RELEASED;
        if (c->closing && i == c->end_entry) {
            c->closing = 0;
            c->status = YRSP_ENDED;
            c->ended_by = 1;
        }
    }
}

static bool yrsp__past(const yrsp_choice* h, float v) {
    return h->cross == YRSP_CROSS_FALLING ? v <= h->level : v >= h->level;
}
static bool yrsp__rearmed(const yrsp_choice* h, float v) {
    return h->cross == YRSP_CROSS_FALLING ? v > h->level + h->hysteresis : v < h->level - h->hysteresis;
}

static void yrsp__sample(yrsp_collector* c, const yrsp_input* in) {
    int i;
    if (c->d.mode != YRSP_CHOICES_LIST) return;
    for (i = 0; i < c->d.n_choices; i++) {
        const yrsp_choice* h = &c->ch[i];
        yrsp__chan* ch = &c->chan[i];
        float v;
        if (!h->cross || h->kind != in->kind || h->control != in->control) continue;
        if (h->device && h->device != in->device) continue;
        if (h->cross == YRSP_CROSS_DISTANCE) {
            ch->x = in->x;
            ch->y = in->y;
            ch->has = 1;
            if (!yrsp__window(c)) continue;
            if (!ch->has_ref) { ch->rx = in->x; ch->ry = in->y; ch->has_ref = 1; continue; }
            v = (float)sqrt((double)(in->x - ch->rx) * (in->x - ch->rx) + (double)(in->y - ch->ry) * (in->y - ch->ry));
        } else {
            v = in->value;
            ch->v = v;
            ch->has = 1;
        }
        if (ch->armed && yrsp__past(h, v)) {
            ch->armed = 0;
            if (yrsp__window(c)) {
                yrsp__log(c, in, i, YRSP_E_CROSSING, v);
                yrsp__check_end(c);
            }
        } else if (!ch->armed && yrsp__rearmed(h, v)) {
            ch->armed = 1;
        }
    }
}

/* --- API ------------------------------------------------------------------ */

YRSP_API void yrsp_arm(yrsp_collector* c, int64_t t) {
    int i;
    if (!c || !c->ok) return;
    c->armed = 1;
    c->finished = 0;
    c->have_onset = 0;
    memset(&c->onset, 0, sizeof c->onset);
    c->t_arm = t;
    c->t_end = 0;
    c->status = YRSP_OPEN;
    c->ended_by = 0;
    c->end_entry = -1;
    c->closing = 0;
    c->n_log = c->n_trace = c->n_lost = c->n_lost_trace = c->n_held = c->n_stray = 0;
    c->held_at_open = 0;
    for (i = 0; i < c->d.n_choices; i++) {
        const yrsp_choice* h = &c->ch[i];
        yrsp__chan* ch = &c->chan[i];
        if (h->cross == YRSP_CROSS_DISTANCE) {
            ch->has_ref = ch->has;
            ch->rx = ch->x;
            ch->ry = ch->y;
            ch->armed = 1;
        } else if (h->cross) {
            if (!ch->has) ch->armed = 1;
            else if (yrsp__past(h, ch->v)) ch->armed = 0;
            if (!ch->armed) c->held_at_open = 1;
        } else if (h->match == YRSP_MATCH_SCANCODE) {
            int k;   /* a choice's device 0 is any device */
            for (k = 0; k < YRSP_MAX_HELD; k++)
                if (c->held[k].used && c->held[k].kind == h->kind && c->held[k].control == h->control &&
                    yrsp__same_dev(c->held[k].device, h->device))
                    c->held_at_open = 1;
        }
    }
}

YRSP_API int yrsp_set_onset(yrsp_collector* c, const yrsp_onset* o) {
    if (!c || !c->ok) return YRSP_ERR_STATE;
    if (!o) return YRSP_ERR_ARG;
    if (!c->armed) return c->status;
    if (c->have_onset && c->onset.final && !o->final) return c->status;
    c->onset = *o;
    c->have_onset = 1;
    yrsp__reclassify(c);
    yrsp__check_end(c);
    return c->status;
}

YRSP_API int yrsp_feed(yrsp_collector* c, const yrsp_input* in) {
    if (!c || !c->ok) return YRSP_ERR_STATE;
    if (!in || in->type < YRSP_PRESS || in->type > YRSP_PROXIMITY || in->kind > 15) return YRSP_ERR_ARG;
    if (c->armed && !c->finished && c->d.trace && (c->d.trace_kinds == 0 || ((c->d.trace_kinds >> in->kind) & 1u))) {
        if (c->n_trace < c->d.trace_cap) c->d.trace[c->n_trace++] = *in;
        else c->n_lost_trace++;
    }
    switch (in->type) {
    case YRSP_PRESS:   yrsp__press(c, in); break;
    case YRSP_RELEASE: yrsp__release(c, in); break;
    case YRSP_SAMPLE:  yrsp__sample(c, in); break;
    default: break;
    }
    return c->status;
}

YRSP_API int yrsp_update(yrsp_collector* c, int64_t now) {
    if (!c || !c->ok) return YRSP_ERR_STATE;
    if (c->status != YRSP_OPEN || c->finished) return c->status;
    if (c->closing) {
        if (now >= c->t_end + c->max_hold_ns) {
            c->closing = 0;
            c->status = YRSP_ENDED;
            c->ended_by = 3;
        }
        return c->status;
    }
    if (c->have_onset && c->duration_ns > 0 && now >= c->onset.t + c->duration_ns + c->settle_ns) {
        c->status = YRSP_ENDED;
        c->ended_by = 2;
        c->t_end = c->onset.t + c->duration_ns;
    }
    return c->status;
}

YRSP_API int yrsp_finish(yrsp_collector* c, yrsp_result* r) {
    int i, best = -1;
    if (!c || !c->ok) return YRSP_ERR_STATE;
    if (!r) return YRSP_ERR_ARG;
    memset(r, 0, sizeof *r);
    r->rt = r->rt_key_duration = YRSP__NAN;
    r->response = -1;
    r->entry = -1;
    r->onset_frame = -1;
    yrsp__reclassify(c);
    c->finished = 1;
    if (c->status == YRSP_OPEN) c->status = YRSP_ENDED;
    c->closing = 0;
    for (i = 0; i < c->n_log; i++) {
        const yrsp_entry* e = &c->log[i];
        uint16_t k = (uint16_t)(e->flags & YRSP_E_CLASS_MASK);
        if (k != e->arrival_class) {
            c->log[i].flags |= YRSP_E_RECLASSIFIED;
            r->flags |= YRSP_R_RECLASSIFIED;
        }
        if (e->flags & YRSP_E_VALID) {
            r->n_responses++;
            if (best < 0 || e->t < c->log[best].t) best = i;
        }
        if (e->flags & YRSP_E_EARLY) r->n_early++;
        if (e->flags & YRSP_E_ANTICIPATION) r->n_anticipations++;
        if (e->flags & YRSP_E_LATE) r->n_late++;
        if (e->flags & YRSP_E_AFTER_END) r->n_after_end++;
        if (e->flags & YRSP_E_DUPLICATE) r->n_duplicates++;
        if (e->flags & YRSP_E_NOT_CHOICE) r->n_not_choice++;
    }
    r->n_held = c->n_held;
    r->n_entries = c->n_log;
    r->n_lost = c->n_lost;
    r->n_trace = c->n_trace;
    if (c->n_lost) r->flags |= YRSP_R_LOG_FULL;
    if (c->n_lost_trace) r->flags |= YRSP_R_TRACE_FULL;
    if (c->held_at_open) r->flags |= YRSP_R_HELD_AT_OPEN;
    if (c->ended_by == 3) r->flags |= YRSP_R_HOLD_TIMEOUT;
    if (c->have_onset) {
        r->t_onset = c->onset.t;
        r->onset_residual = c->onset.residual;
        r->onset_uncertainty = c->onset.uncertainty;
        r->onset_frame = c->onset.frame;
        r->onset_tier = c->onset.tier;
        r->onset_src = c->onset.src;
        if (!c->onset.final) r->flags |= YRSP_R_ONSET_PLAN;
    } else {
        r->flags |= YRSP_R_NO_ONSET;
    }
    if (c->ended_by == 1 && c->end_entry >= 0 && !(c->log[c->end_entry].flags & YRSP_E_VALID))
        r->flags |= YRSP_R_ENDED_EARLY;
    if (best >= 0) {
        const yrsp_entry* e = &c->log[best];
        const yrsp_source* s = yrsp__source(c, e->kind, e->device);
        r->flags |= YRSP_R_RESPONDED;
        r->entry = best;
        r->response = e->choice;
        r->response_name = e->choice >= 0 ? c->ch[e->choice].name : NULL;
        r->t_response = e->t;
        r->rt = (double)(e->t - c->onset.t) / YRSP__NS_PER_S;
        if (e->flags & YRSP_E_RELEASED) r->rt_key_duration = (double)(e->t_release - e->t) / YRSP__NS_PER_S;
        r->control = e->control;
        r->code = e->code;
        r->device = e->device;
        r->kind = e->kind;
        r->stamp_tier = e->stamp;
        if (s && s->tier == e->stamp) {
            r->stamp_partial = s->partial;
            r->stamp_lo_us = s->lo_us;
            r->stamp_hi_us = s->hi_us;
        }
    } else if (c->duration_ns > 0) {
        r->flags |= YRSP_R_TIMEOUT;
    }
    return YRSP_ENDED;
}

YRSP_API const yrsp_entry* yrsp_entries(const yrsp_collector* c, int* n) {
    if (n) *n = c ? c->n_log : 0;
    return c ? c->log : NULL;
}

/* --- CSV --------------------------------------------------------------- */

YRSP_API int yrsp_format_header(char* buf, size_t cap) {
    return snprintf(buf, cap, "rt,response,rt_key_duration,rsp_kind,rsp_scancode,rsp_keycode,rsp_device,"
                              "rsp_tier,rsp_stamp_lo,rsp_stamp_hi,rsp_partial,onset_tier,onset_src,"
                              "onset_residual,onset_uncertainty,onset_frame,n_responses,n_anticipations,"
                              "n_early,n_late,n_duplicates,rsp_flags");
}

/* Seconds with 9 decimals, or nothing for NaN. */
static void yrsp__sec(char* out, size_t cap, double s) {
    if (s != s) out[0] = 0;
    else snprintf(out, cap, "%.9f", s);
}

/* A CSV field: quoted when it holds a comma, a quote or a line break. */
static void yrsp__field(char* out, size_t cap, const char* s) {
    size_t n = 0;
    const char* p;
    bool q = strpbrk(s, ",\"\r\n") != NULL;
    if (cap == 0) return;
    if (q && n + 1 < cap) out[n++] = '"';
    for (p = s; *p && n + 2 < cap; p++) {
        if (*p == '"') out[n++] = '"';
        out[n++] = *p;
    }
    if (q && n + 1 < cap) out[n++] = '"';
    out[n] = 0;
}

YRSP_API int yrsp_format_row(const yrsp_result* r, char* buf, size_t cap) {
    static const char* const kinds[16] = { "keyboard", "mouse", "touch", "pen", "gamepad", "box", "eye", "sync",
                                           "user0", "user1", "user2", "user3", "user4", "user5", "user6", "user7" };
    char rt[40], dur[40], res[40], unc[40], lo[40], hi[40], resp[80], tmp[48];
    const char* name = NULL;
    if (!r) return -1;
    yrsp__sec(rt, sizeof rt, r->rt);
    yrsp__sec(dur, sizeof dur, r->rt_key_duration);
    resp[0] = 0;
    if (r->flags & YRSP_R_RESPONDED) {
        name = r->response_name;
        if (!name && r->kind == YRSP_KIND_KEYBOARD) name = yrsp_key_name(r->control);
        if (!name) {
            snprintf(tmp, sizeof tmp, "%s:%u", kinds[r->kind & 15], (unsigned)r->control);
            name = tmp;
        }
        yrsp__field(resp, sizeof resp, name);
    }
    if (r->flags & YRSP_R_NO_ONSET) res[0] = unc[0] = 0;
    else {
        yrsp__sec(res, sizeof res, (double)r->onset_residual / YRSP__NS_PER_S);
        yrsp__sec(unc, sizeof unc, (double)r->onset_uncertainty / YRSP__NS_PER_S);
    }
    if (r->stamp_lo_us == 0 && r->stamp_hi_us == 0) lo[0] = hi[0] = 0;
    else {
        yrsp__sec(lo, sizeof lo, (double)r->stamp_lo_us / 1e6);
        yrsp__sec(hi, sizeof hi, (double)r->stamp_hi_us / 1e6);
    }
    if (!(r->flags & YRSP_R_RESPONDED))
        return snprintf(buf, cap, ",,,,,,,,,,,%u,%u,%s,%s,%lld,%d,%d,%d,%d,%d,%u", (unsigned)r->onset_tier,
                        (unsigned)r->onset_src, res, unc, (long long)r->onset_frame, r->n_responses,
                        r->n_anticipations, r->n_early, r->n_late, r->n_duplicates, (unsigned)r->flags);
    return snprintf(buf, cap, "%s,%s,%s,%s,%u,%u,%u,%u,%s,%s,%u,%u,%u,%s,%s,%lld,%d,%d,%d,%d,%d,%u", rt, resp, dur,
                    kinds[r->kind & 15], (unsigned)r->control, (unsigned)r->code, (unsigned)r->device,
                    (unsigned)r->stamp_tier, lo, hi, (unsigned)r->stamp_partial, (unsigned)r->onset_tier,
                    (unsigned)r->onset_src, res, unc, (long long)r->onset_frame, r->n_responses,
                    r->n_anticipations, r->n_early, r->n_late, r->n_duplicates, (unsigned)r->flags);
}

#endif /* YSP_RESPONSE_IMPLEMENTATION_GUARD */
#endif /* YSP_RESPONSE_IMPLEMENTATION */

/* ------------------------------------------------------------------------
 * This software is available under the MIT-0 (MIT No Attribution) license.
 *
 * Copyright (c) 2026 ysp contributors
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
