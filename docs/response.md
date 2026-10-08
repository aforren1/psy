# ysp/response.h

Status: v0.1.4, 2026-10-08. The input event lives in `ysp/input.h` from v0.1.3
(`docs/input.md`); this header keeps its names as aliases. v0.1.4:
`YRSP_KINDS_ALL` takes every kind but SYNC. The header's manual (its comment block) is
the reference for each rule. This page has a tutorial built on
`examples/trial_keyboard.c`, how-to guides for other devices, the data
columns, and the decisions with their evidence.

## Tutorial: a keyboard trial with RT from the flip onset

This tutorial runs `examples/trial_keyboard.c`, the ysp version of
jsPsych's `html-keyboard-response`, and explains each part of it.

### 1. Run the example on the simulated display

Build the examples with SDL3 (see the README), then run:

```sh
trial_keyboard --sim
```

The simulated display needs no window. A synthetic participant presses
keys. The program writes `trial_keyboard.csv` and checks its own results:

```text
trial  0 circle rt 0.450000000 response f correct
trial  1 square rt 0.520000000 response j correct
trial  2 circle rt 0.400000000 response j wrong
trial  3 square rt - response - (too slow)
...
--sim: every trial came out as scripted
```

The trial order comes from the seed, so the stimuli in your output can
differ. Trial 1 has an anticipation at 0.05 s before its response. Trial 2
has a key that SDL reported two times, 9 ms apart. Each trial counts one
response.

### 2. Run it with a keyboard

```sh
trial_keyboard --reps 2
```

A window opens. Press F for a circle and J for a square. Press Shift+Esc
to stop. The rows that are complete stay in the file.

### 3. Read the code

The example has five parts.

1. **The conditions.** A CSV string goes through `ysp/table.h` into
   `ysp/trials.h` (`desc.table`). `ytr_frames(ytr_uniform("iti", 0.8,
   1.2), num, den)` draws the inter-trial interval in whole frames of the
   display's rate. The draw goes into the data as the `iti` column.
2. **The response rules.** One call:

   ```c
   static const yrsp_choice keys[] = { { .key = "f" }, { .key = "j" } };
   yrsp_desc d = { .choices = keys, .n_choices = 2,
                     .duration = 1.5, .minimum_valid_rt = 0.1 };
   yrsp_init(&rsp, &d);
   ```

   `"f"` is the physical key at the US-layout F position. On an AZERTY
   keyboard it is also F; on Dvorak it types "u". Durations are in
   seconds.
3. **The trial.** At the fixation, `yrsp_arm(&rsp, f.onset)` opens the
   window. Presses before the stimulus are logged as EARLY. A
   `ytl_seq` turns the fixation off and the stimulus on 0.5 s later.
4. **The onset.** When the stimulus event lands on a frame,
   `yrsp_onset_landing()` gives its planned onset. When that frame's
   flip record completes (in `f.done[]` of a later `begin()`),
   `yrsp_onset_flip()` gives the final onset with its tier. RT is
   measured from the final onset.
5. **Every frame.** Each SDL event goes through `yrsp_from_sdl()` into
   `yrsp_feed()`. `yrsp_update(&rsp, f.onset)` returns
   `YRSP_ENDED` at the first response or at the deadline. The example
   then hides the stimulus on that frame and shows the feedback. At the
   end of the inter-trial interval, `yrsp_finish()` fills the result,
   and the example writes the row.

### 4. Read the data

| Column | Meaning |
|---|---|
| `trial_index`, `stimulus`, `key_answer` | The trial and its condition |
| `response` | The choice's name; empty when there is no response |
| `rt` | Seconds from the stimulus's flip onset; empty when there is no response |
| `correct` | 1 when `response` equals `key_answer` |
| `rt_key_duration` | Seconds from press to release of the response key |
| `onset_tier` | The flip record's tier: 1 is an OS-observed vblank, 3 is a plan, 4 is the simulated display |
| `onset_src` | 1 a flip record, 2 a sound, 3 a plan only |
| `landing_residual` | Where the stimulus landed, minus where it was planned, in seconds |
| `onset_frame` | The frame the stimulus landed on |
| `rsp_tier` | The response stamp's tier (0 not measured end to end) |
| `rsp_flags` | `YRSP_R_*` |
| `n_anticipations`, `n_duplicates` | Counts in the window |
| `iti` | The drawn inter-trial interval, seconds |

A row with `onset_tier` 3, or `onset_src` 3, has an RT that is good for
task development only. The data row says so.

## How-to guides

### Use a response box on a serial port

`ysp/serial.h` reads bytes; it does not parse a box's protocol. Parse
each report into a `yrsp_input`:

```c
yrsp_input in = { .t = t_read, .kind = YRSP_KIND_BOX, .device = 0,
                    .control = button, .type = down ? YRSP_PRESS : YRSP_RELEASE };
yrsp_feed(&rsp, &in);
```

- `t_read` is `yrt_now_ns()` when `yser_read()` returned. This time
  includes the USB-serial latency timer (16 ms on FTDI parts unless
  `desc.low_latency` is accepted). It is not measured: give the source
  tier UNKNOWN.
- A box with its own clock (Cedrus XID reports a timer) gives a better
  time. Map the box's time to ysp_rt with a fit of time pairs, and give
  tier 1 only after a loopback test checks the map.
- Read the port on another thread if you must, but queue the events: only
  the frame thread calls `yrsp_feed()`.

### Keep key stamps honest when a GUI library runs

Another library can turn SDL text input on for the stimulus window (Dear
ImGui's SDL3 backend does, on the window with the focus). Keys then come
on the message path, about 11 ms late (`docs/imgui_probe.md`). Read the
path each frame and give it to the adapter, as `examples/trial_keyboard.c`
does:

```c
yscr_get_caps(&scr, &caps);              /* about 0.2 us */
sdl.raw_keyboard = caps.raw_keyboard;
```

Keys of those frames then get tier 3 in their entries and in the result
(`rsp_tier`), and `ysp/screen.h` v0.3.3 flags the flips
(`YSCR_FLIP_TEXT_INPUT`) and writes a ring record marked external.

### Run two keyboards or two participants

Each keyboard on SDL's raw path has its own device id, so a response can
come from one keyboard only.

1. Find the ids. Run `screen_input --devices`, press a key on each
   keyboard, and read which id each key came from:

   ```text
     key F          from keyboard 65601
     key J          from keyboard 65659
   devices in the ring:
     keyboard present id 65659        Computer Corp Dell Univ
     keyboard present id 65601        Standard PS/2 Keyboard
   ```

   The ids are Windows handles. They change when a keyboard is plugged
   in again and from one session to the next, so do not store them in a
   design. Open the screen with `desc.ring`: `ysp/screen.h` logs every
   keyboard, mouse and touch device with its id and name at open
   (`YSCR_EV_DEVICE`), and on hot-plug, so the data log maps ids to
   devices.
2. Give each choice its keyboard:

   ```c
   yrsp_choice keys[] = {
       { .key = "f", .name = "p1_left",  .device = id_p1 },
       { .key = "j", .name = "p1_right", .device = id_p1 },
       { .key = "f", .name = "p2_left",  .device = id_p2 },
       { .key = "j", .name = "p2_right", .device = id_p2 },
   };
   ```

   A choice with `device` 0 takes any keyboard. Choose the ids at run
   time, for example by asking each participant to press a key at the
   start and reading `in.device`.
3. Use `persist` with `max_responses` 2 for one response from each, or one
   collector per participant.

Limits:
- Keys on the message path have device 0 and match every keyboard: keep
  SDL text input off during the trials (`caps.raw_keyboard` says so).
  Injected keys (SendInput, remote desktop) also have device 0.
- When both participants hold the same key at once, SDL's shared key
  state drops the first one's release (see "The double report").
- Mice: in SDL's absolute mode every mouse is device 0, one pointer, with
  message times (tier 3). For a mouse per participant, see the next
  section.
- The abort key (Shift+Esc) works from any keyboard, on purpose.

### Give each participant a mouse

`ysp/screen.h` v0.3.5 reads every mouse's Raw Input on its own thread when
asked (`desc.raw_mice`, Windows): each report carries the mouse's device
id and is stamped when read, like the raw keyboard. The system cursor,
the operator's console and ImGui keep working.

1. Open the screen with `desc.raw_mice = true` and `desc.ring` (the ring
   gets the device list and the reader's state).
2. Find the ids: `screen_input --mice` prints each click with its mouse and
   ends with one line per mouse, with its name. This separation is
   verified with injected input only: run `screen_input --mice` with two
   physical mice before you rely on it.
3. Each frame, one loop for every input (`ysp/screen.h` v0.4.0's input
   bridge: the raw mouse events arrive as SDL events, decoded by
   `yscr_event_input()`, with the keyboard's in the same stream):

   ```c
   SDL_Event ev;
   yrsp_input in;
   while (yscr_poll(&scr, &ev, NULL)) {
       if (!yscr_event_input(&scr, &ev, &in)) continue;
       yrsp_feed(&rsp, &in);
       yrsp_cursor_feed(&cursor_p1, &in);   /* if a cursor is drawn */
   }
   ```

   `yscr_poll_mouse()` (v0.3.5) still works in v0.4.0 but is
   deprecated.

4. Make each choice a button of one mouse: `{ .name = "p1", .kind =
   YRSP_KIND_MOUSE, .device = id_p1, .control = 1 }` (1 left, 2 middle,
   3 right). Add `yrsp_raw_mouse_source()` to `desc.sources`.
5. A drawn cursor per participant: `yrsp_cursor` with `device`, `gain`
   (pixels per count) and the area. The movement is unaccelerated, so it
   does not follow the system cursor's ballistics. State the gain in the
   data.

Reports from devices SDL does not list carry `YRSP_IN_UNLISTED`.
Device 0 is every pointer Windows synthesizes itself: a precision
touchpad's cursor, injected input, remote desktop. If
other code turns on SDL's relative mouse mode, the raw reports stop until
it ends; the ring says so (`YSCR_EV_RAW_MICE`).

### Use a gamepad trigger as the response

1. Open the screen with `desc.gamepads = true` (ysp/screen.h v0.3.3), and
   read events with `yscr_poll()`.
2. Make the choice a crossing:

   ```c
   static const yrsp_choice trig[] = {
       { .name = "trigger", .kind = YRSP_KIND_GAMEPAD, .cross = YRSP_CROSS_RISING,
         .control = YRSP_AXIS_GAMEPAD + SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
         .level = 0.5f, .hysteresis = 0.2f } };
   ```

The gamepad's timing is not measured. XInput pads are polled.

### Measure RT from a sound

Give the sound's onset record to the collector:

```c
yau_onset rec;            /* from ysp/audio.h */
yrsp_onset o = yrsp_onset_audio(&rec);
yrsp_set_onset(&rsp, &o);
```

The onset is the device-clock fit's time, tier 2 at best (ysp/audio.h).

### Detect pen pressure onset or mouse movement onset

- Pen: a RISING crossing on `YRSP_KIND_PEN`, control
  `YRSP_AXIS_PEN_PRESSURE`, level about 0.05.
- Mouse: a DISTANCE crossing on `YRSP_KIND_MOUSE`, control
  `YRSP_AXIS_POSITION`, level in window coordinates. The distance is
  measured from the position at `yrsp_arm()`.

Add a trace (`desc.trace`, `desc.trace_cap`) to keep every sample of
the window for a mouse-tracking or drawing task.

### Check that your keyboard reports each key once

Run this command, click its window, and press keys for 30 s:

```sh
screen_input --hand
```

It prints one line for each press and, at the end, a summary:

```text
N presses, 0 second reports within 50 ms, M OS repeats; reports with a device id N, without 0
RESULT: one report per press
```

`RESULT: some keys reported twice` means that SDL sends those keys from
both paths on this machine. `ysp/response.h` removes the second reports,
but tell the maintainers: it is not what was expected.

`which` is the keyboard's device id on SDL's raw path, and 0 on the
message path. A second report from the message path shows `which=0`.

## Reference

The header's comment block lists every field and flag. In short:

| Call | Does |
|---|---|
| `yrsp_init(&c, &desc)` | Validates and copies the rules. No allocation after it. |
| `yrsp_arm(&c, t)` | Opens a window; keeps which keys are down. |
| `yrsp_set_onset(&c, &o)` | Sets or refines the onset; classifies again. |
| `yrsp_feed(&c, &in)` | One input event, armed or not. |
| `yrsp_update(&c, now)` | The deadline, and `max_hold` while waiting for a release. |
| `yrsp_finish(&c, &r)` | The result. Call it again after a late release or onset. |
| `yrsp_entries(&c, &n)` | Every press of the window. |
| `yrsp_format_header`, `yrsp_format_row` | A CSV row of the result. |
| `yrsp_from_sdl`, `yrsp_sdl_source` | SDL3 events and their source entries (inline, with SDL3). |
| `yrsp_onset_flip`, `_audio`, `_landing` | Onsets from ysp/screen.h, ysp/audio.h, ysp/timeline.h records (inline). |

jsPsych option to ysp field:

| jsPsych | ysp/response.h | Difference |
|---|---|---|
| `choices` | `desc.choices` (`key`, `control`, `code`) | Physical key by default; keycode match on request |
| `"ALL_KEYS"` | `mode = YRSP_CHOICES_ALL` (the zero default) | `kinds` 0 is the keyboard only |
| `"NO_KEYS"` | `mode = YRSP_CHOICES_NONE` | Presses are logged |
| `trial_duration` | `desc.duration`, from the onset | Seconds; from the stimulus, not the trial start |
| `response_ends_trial: false`, `persist` | `desc.persist`, `desc.max_responses` | |
| `minimum_valid_rt` | `desc.minimum_valid_rt` | Earlier presses are logged as anticipations, not dropped |
| `allow_held_key` | `desc.allow_held_key` | Same rule; held responses are flagged |
| `wait_for_key_release` | `desc.wait_for_key_release`, `desc.max_hold` | Gives up after `max_hold` |
| `rt` | `result.rt` | From the flip onset, with its tier |
| `rt_key_duration` | `result.rt_key_duration` | From the response's press |

## Decisions

### RT starts at the flip onset

jsPsych measures `rt` from the call that registers the key listener
(`performance.now()`, `KeyboardListenerAPI.ts`). That moment is not the
stimulus's screen time. ysp measures from the flip record's onset of the
frame that showed the stimulus, and the row carries that onset's tier and
the landing residual. When only a plan is known, the result says so
(`ONSET_PLAN`).

The record arrives a frame or more after the onset. So the collector
keeps absolute times and classifies again at each `set_onset()`. A press
that ended the trial live but is an anticipation against the final onset
gets `ENDED_EARLY` in the result. The caller decides what to do (for
example, re-queue the trial).

### Physical keys by default

The coordinator chose scancode matching on 2026-10-07: "the F and J keys"
means positions, and positions do not move with the layout. Key names
resolve to US-layout scancodes at `yrsp_init()`. Keycode matching is an
option. Every entry logs both.

### `kinds` 0 is the keyboard only

In ALL mode, `kinds` 0 takes keyboard presses only. A click that brings
the window to the front must not end a keyboard trial. This breaks "zero
means all" on purpose. `YRSP_KINDS_ALL` takes every kind but SYNC
(v0.1.4, a default the user adopted on 2026-10-08, `docs/devices_spec.md`
section 16): a scanner pulse or a photodiode edge in an ALL_KEYS trial is
logged and does not end it. A mask that names the SYNC bit
(`YRSP_KIND_BIT(YRSP_KIND_SYNC)`), or a LIST choice of kind SYNC, takes
SYNC events: that is how a trial waits for the scanner.

### Held keys follow jsPsych

Read in jsPsych's `KeyboardListenerAPI.ts` (`main`, 2026-10-07): the
listener keeps a set of held keys, adds a key at its keydown and removes
it at its keyup. With `allow_held_key` false, a keydown of a key in the
set is not a response. So the OS repeats of a key held from before the
window never count. With true, every keydown counts, an OS repeat too.
ysp does the same and flags a held response `HELD`. Differences:

- jsPsych drops a press before `minimum_valid_rt`; ysp logs it as an
  anticipation.
- jsPsych keys the held set by `KeyboardEvent.key`; ysp by scancode, so a
  Shift change during a hold does not make a new key.
- jsPsych releases every key when the window loses focus. ysp waits for
  SDL's key-up events.
- jsPsych's `rt_key_duration` runs from the key's first keydown; ysp's
  from the response's press. They differ only for a held response.
- With `persist` and `wait_for_key_release`, jsPsych reports only the
  newest press's release. ysp ends at the release of the press that
  reached `max_responses`.

### The double report

SDL 3.4.0's Windows code sends a key from the window message only when
the raw keyboard is off, text input is on, or the message has no scan
code (`virtual_key`). With the raw path on, a key whose message has no
scan code arrives two times: from the raw-input thread and from the
message loop.

Measured with `screen_input --reports` (F24 injected while the window had
the focus; 20, 50, 50, 199 and 199 presses per row in five runs, 518
taps in all):

| Injection | Hold | Reports per press | Second report after the first |
|---|---|---|---|
| Virtual key (`wVk`, scan code 0) | tap | 2 press-release pairs | -0.1 to 34.2 ms; 517 of 518 within 17.1 ms |
| Virtual key | 100 ms | 1 press, then 1 repeat | |
| Scan code (`KEYEVENTF_SCANCODE`) | tap | 1 | |
| Scan code | 100 ms | 1 | |

Before this probe, `docs/screen.md` said that SDL 3.4 reports each
key two times. The probes that found it injected by virtual key only.
A physical key carries a scan code, so it most likely reports once.
`screen_input --hand` lets a user check keys pressed by hand; this was
not run here.

The rule (R3 in the manual): a press of the same kind and control within
`dedup` (50 ms) of the last accepted press, in either time order, is a
second report. The first report to arrive is kept. 50 ms covers the
largest measured gap (34.2 ms). Pressing one key two times within 50 ms
is not a response a person makes.

v0.1.1 adds the device to the rule. On the raw path, SDL gives each key
the Raw Input handle of its keyboard (`which`); the message path gives 0.
SendInput gave 0 on both paths: every report in the table above had
`which` 0, because injected input has no device handle. So two reports
merge only when their devices are equal or one of them is 0. Two
different keyboards pressing one key within 50 ms are two presses (two
participants, or two hands on two boxes). The same rule decides whether a
key is down (R2) and which press a release closes.

SDL 3.4 keeps one key state for all keyboards. While keyboard A holds F,
keyboard B's press of F arrives marked as a repeat, and B's release ends
SDL's state, so SDL drops A's release later. The collector takes B's
"repeat" for B's first press, because only A holds the key; A's press
then has no release (`rt_key_duration` empty).

`ysp/screen.h` merged abort presses within 30 ms. One of the 518
measured gaps (34.2 ms) is longer, so two Shift+Esc presses through a
remote-desktop or assistive tool (they inject by virtual key) could count
as four and trigger the panic watchdog. v0.3.3 merges within 50 ms too.

### No interpolation at a crossing

SDL sends an axis value when it changes, so the signal is a step. The
crossing time is the time of the first sample at or past the level.

### No combined uncertainty

The result carries the onset's tier and the response stamp's tier and
bounds side by side. The two rest on different evidence (an OS vblank
time, a measured host-side stamp), and a sum would hide which part is
unknown.

## Verification

All on 2026-10-07, Windows 11, the Iris Xe laptop of `ysp/screen.h`'s
STATUS, with MinGW-w64 gcc 16.1 (C11, C99, C++17; `-Wall -Wextra
-Wpedantic -Wshadow -Werror`) and MSVC 19.44 (C11, C++17; `/W4 /WX`).

| Check | Result |
|---|---|
| `tests/adapt/response_test.c` | v0.1.4 (2026-10-08): 20,617 checks pass; a SYNC press in ALL mode with `YRSP_KINDS_ALL` is not a response, with the SYNC bit named it is, and a hand-made mask of every bit but one keeps SYNC. v0.1.2: 20,604 checks pass; 20,669 with `YRSP_TEST_SDL`. Raw mice: a report into presses, releases, movement and wheel in order, the cap, absolute devices, a choice on one mouse, the cursor's gain, clamp and device. v0.1.1: 20,545 checks pass; 20,608 with `YRSP_TEST_SDL` (the SDL adapter on `SDL_Event` structs filled by hand, SDL 3.4.0 headers, nothing linked). Devices: two keyboards 5 ms apart (two responses, also when SDL marks the second a repeat), a raw report and its device-0 report (one), releases per device, HELD_AT_OPEN per device |
| Random streams | 20,000 streams of 40 presses, releases and repeats with random onsets, rules and deadlines: one class per entry, every VALID entry eligible, the result's response the earliest VALID entry |
| Mutations (`tests/mutate/response.toml`) | v0.1.4: 50 of 50 caught (`sync-01`, SYNC in ALL mode). v0.1.2: 49 of 49 caught (`ms-01` to `ms-04`; `ms-04` survived until the SDL mouse tier moved into a helper the SDL-free test reaches). v0.1.1: 45 of 45 caught (`dv-01` devices ignored, `dv-02` device 0 not a wildcard, `dv-03` another keyboard's "repeat" held). v0.1.0: 42 of 42 caught. The first run caught 37; the 5 survivors (rounding of seconds to ns, TIMEOUT without a deadline, a repeat whose press was never seen, release pairing, NaN in a row) each got a test. The release one found a fault: a release closed only the newest entry, so with `allow_held_key` a response followed by its HELD repeats had no duration. A release now closes the press and its repeats. |
| `trial_keyboard --sim` | Both compilers: 6 trials as scripted, onset tier 4 (SIM) from the flip records |
| `trial_keyboard` in a window | No keys: 2 timeouts, final onsets of tier 2 (composed) and 1 (independent flip), landing residuals 5 to 49 us. Keys from SendInput (scan codes and virtual-key taps; sent only while the program's window was in front): 6 trials through `yscr_poll()` and `yrsp_from_sdl()`, the virtual-key taps' second reports counted as duplicates. A virtual-key tap's press and release came with the same stamp (one raw-input poll), so its `rt_key_duration` was 0. |
| `screen_input --reports` | The table in "The double report" |

Not run: a key pressed by hand (`screen_input --hand` is the check); Linux
and macOS (CI runs the test and `trial_keyboard --sim` there); mouse,
touch, pen and gamepad events from a device (the adapter is checked on
filled structs only); any response box.

## Not done

- Output to a `ysp/rt.h` ring (v0.2, `YRT_SRC_INPUT`).
- Stimulus boxes at each flip for mouse tracking; velocity-based movement
  onset; chords.
- A protocol parser for any response box.
- Timing of mouse, touch, pen and gamepad events. Keyboard timing with a
  physical key.
