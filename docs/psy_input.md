# psy_input.h

Status: v0.3.0, 2026-10-08. The header's manual (its comment block) is the
reference for each field and constant. This page records why the header
exists and how it was checked.

## Why a header of its own

`psy_response.h` v0.1.0 to v0.1.2 defined the input event. A producer, such
as a serial box reader, `psy_screen.h`'s raw mice or a later eye-tracker
reader, then had to include the response collector to make events. A data
logger or the player's `input.key` had to include it to read them. The
coordinator split the event out on 2026-10-08, as the user approved:

| Part | Header |
|---|---|
| The 48-byte event, its kinds, types, flags, controls and axes | `psy_input.h` |
| The source entry and the tier scale | `psy_input.h` |
| The SDL3 adapter (`psyin_from_sdl`, inline, when SDL3 came first) | `psy_input.h` |
| The raw mouse adapter (`psyin_from_mouse`, inline, when `psy_screen.h` v0.3.5 came first) | `psy_input.h` |
| The collector, the window, the result, key names, the cursor | `psy_response.h` |

`psy_response.h` requires `psy_input.h` beside it and includes it, in the
same way that `psy_gfx.h` requires `psy_color.h`. Every `psyrsp_` and
`PSYRSP_` name of the event stays as an alias (`psyrsp_input` is
`psyin_event`), so code written for `psy_response.h` v0.1.2 builds
unchanged.

## v0.2.0

- The raw mouse report (`psyin_mouse_report`, `PSYIN_MOUSE_*`) moved here
  from `psy_screen.h`, so `psyin_from_mouse()` needs no other header.
  `psy_screen.h`'s `psyscr_mouse_event` and `PSYSCR_MOUSE_*` are aliases.
- The SDL3 adapter sits outside the include guard, behind its own guard:
  a file that includes SDL3 after `psy_input.h` includes `psy_input.h`
  again and gets the adapter. `psy_screen.h`'s implementation does this
  for `psyscr_event_input()`.
- `psy_screen.h` v0.4.0 requires `psy_input.h`: its input bridge stores
  and delivers `psyin_event`s.

## v0.3.0

For specialized devices (`docs/devices_spec.md`, section 7; the user's
defaults of 2026-10-08, section 16):

| Change | Where | Why |
|---|---|---|
| `PSYIN_KIND_SYNC` (7) | the kind | A scanner pulse, a TTL input, a photodiode or sound-key edge is a timing event, not a participant's response. Kind 7 was free. `psy_response.h` v0.1.4 keeps it out of ALL mode |
| `uint16_t unc_us` | offset 26, the 2 bytes that were `reserved_` | This event's own uncertainty: a device clock fit's spread at the time, a bracket's width, a known quantizer. ioHub (`confidence_interval`) and Presentation (`unc_dms`) carry one per event; a source entry has one bound for a whole session. 0 = the source entry's; `PSYIN_UNC_MAX` (65535) = 65.5 ms or more |
| `uint32_t ticks` | offset 44, the 4 bytes of tail padding | The device clock's raw count, low 32 bits, valid with `PSYIN_DEVTICKS` (0x10). The analysis can map the event again with an offline fit of the clock pairs in the log (`psy_rt.h` v0.6.0). 32 bits suffice because the pairs carry the whole count |
| `PSYIN_EYE_GAZE` (0, as `PSYIN_AXIS_POSITION`), `_FIXATION` (1), `_SACCADE` (2), `_BLINK` (3); `PSYIN_EYE_LEFT` (0), `_RIGHT` (1), `_BOTH` (2) in `code` | constants | The existing types carry eye events: a gaze SAMPLE, a blink as a press and release of the BLINK control, PROXIMITY for track lost and found |

The size stays 48 bytes and every older offset and value stays. A producer
built against v0.2.0 writes zeros in both places (the adapters `memset`
the event), which reads as "the source entry's bounds" and "no device
ticks".

## Decisions

| Question | Decision | Why |
|---|---|---|
| Prefix | `psyin_`, `PSYIN_` | One prefix per header. The old names stay in `psy_response.h` as aliases. |
| Implementation section | None | Types and inline functions only: nothing to link. |
| Flag names | `PSYIN_REPEAT`, `PSYIN_SYNTHETIC`, `PSYIN_ERASER`, `PSYIN_UNLISTED` | `PSYRSP_IN_REPEAT` read as "input repeat" in the collector; in the input header the `IN_` is redundant. |
| Values | Unchanged | Kinds, types, axes and tiers are in data files: their numbers are pinned by the test. |
| Key names (`psyrsp_scancode`, `psyrsp_key_name`) | Stay in `psy_response.h` | They resolve choices; a producer does not need them. |
| The cursor (`psyrsp_cursor`) | Stays in `psy_response.h` | A consumer's integration of events, with the design's gain. |

## Verification

v0.3.0, on 2026-10-08, with the same compilers and flags as below: the
test pins the new offsets and sizes (`unc_us` at 26, 2 bytes; `ticks` at
44, 4 bytes), the new constants, and that the raw mouse adapter writes
both new fields as 0 over a buffer filled with 0xA5. 77 checks pass
(gcc, MinGW, MSVC; with SDL3 through `test_psy_input_sdl`).
`psy_response.h`'s test and its 50 mutants pass on v0.3.0.

v0.2.0:

On 2026-10-08, with MinGW-w64 gcc 16.1 (C11, C99, C++17; `-Wall -Wextra
-Wpedantic -Wshadow -Werror`) and MSVC 19.44 (C11, C++17; `/W4 /WX`):

| Check | Result |
|---|---|
| `tests/adapt/psy_input_test.c` | 58 checks pass; 67 with `PSYIN_TEST_SDL` (v0.1.0 and v0.2.0) (SDL 3.4.0 headers, nothing linked): the event's size and every field offset, every constant's value, the raw mouse adapter, the SDL adapter |
| `tests/compile/psy_input.c`, `.cpp` | Build and run on both compilers |
| `psy_response.h` through the aliases | Its test unchanged: 20,604 checks, 20,669 with the SDL adapter; 49 of 49 mutations caught (`ms-01` and `ms-04` now edit `psy_input.h`) |
| `examples/trial_keyboard.c --sim` | As scripted |
