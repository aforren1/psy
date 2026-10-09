# ysp/box.h

Status: v0.1.0, 2026-10-08. The header's manual (its comment block) is the
reference for each frame and query. This page records why the header has
its form, what its bytes were checked against, and what is not checked.
The plan is `docs/devices_spec.md` (sections 4.3 and 10); this header is
part of its step 2.

## Why a pure header

A decoder takes bytes and the time they were read, and gives events and
clock pairs. It does no I/O, starts no thread, reads no clock and
allocates nothing (devices_spec principle 3). So:

- a test feeds it generated or captured bytes, split anywhere;
- an analysis can decode a raw capture of a session with this header
  alone;
- `ysp/device.h` owns the threads, the transport and the clock.

## Families

| Family | Frames | Clock | Queries | Checked against |
|---|---|---|---|---|
| `YBOX_XID` | `'k'`, info, uint32 LE ms (6 bytes); `"_e5"` + uint32 LE (7 bytes) | the box's ms timer | `"_e5"` (timer), `"_c1"` (identity) | pyxid2 (cedrus-opensource/pyxid at `d5b0a73`, 2025-01-14): `unpack('<cBI')` for key frames with port `& 0x0F`, key `(info & 0xE0) >> 5` (0 means 8), pressed `0x10`, frames with `info & 0x0C` refused; `unpack('<cccI')` for `"_e5"`; `"_c1"` answered `"_xid0"` in XID mode; `"_d2"`, `"_d3"`, `"_d4"` one byte each. Cedrus's XID command page agrees on the frame |
| `YBOX_LINE` | text lines (below) | the board's microsecond counter | `"q <seq>"`, `"i"` | this repository's own protocol and firmware |
| `YBOX_PHOTO` | `0xA5`, level, uint32 LE us; `0x5A`, 0, uint32 LE us | the board's microsecond counter | none | `tests/loopback/screen_loopback.c` |

## The ysp line protocol

The plan's first draft had comma-separated lines (`t,<us>,<button>,<0|1>`).
The protocol as built has one letter per line type and spaces:

```
S <t>                  sync, every 100 ms
E <t> <ch> <0|1>       edge
A <t> <ch> <value>     sample
Q <seq> <t>            answer to "q <seq>"
I <text>               answer to "i"
# <text>               comment
```

Why:

- A letter per type lets one parser take syncs, edges, samples, answers
  and identity, and refuse the rest by name.
- Spaces are what `Serial.print` writes between numbers without format
  strings, so the firmware stays small and board-neutral.
- The sync gives the host pairs and a heartbeat when no edge comes; the
  query gives a bracketed pair (its width is the round trip), which a
  sparse device needs (docs/rt.md: sparse presses behind a 16 ms latency
  timer gave millisecond errors for minutes).
- A 64-bit decimal time is allowed, so a board with a 64-bit counter
  (the RP2040's `time_us_64()`) can send it; the reference firmware sends
  `micros()`, 32 bits, which the host unwraps.

## Decisions

| Question | Decision | Why |
|---|---|---|
| Output | The caller's arrays of events and pairs; the decoder stops when one is full and returns the bytes it used | No allocation, and a bounded output per call. A byte finishes at most one frame (a broken frame shows by its third byte, and the two bytes fed again cannot hold one), so arrays of one entry suffice; mutant `box-11` writes one past them and the test catches it |
| A broken frame | Drop its first byte and feed the rest again | A frame that starts inside a broken one is still found. XID frames with port bits 2 or 3 set are broken, as pyxid2 says |
| Garbage | Counted per call and in total, never an event | A misaligned stream must not give a press. The device layer logs the total at most once a second |
| Event time | The read time given; `ticks` the device time with `YIN_DEVTICKS` | The decoder reads no clock; the device layer maps `ticks` through the fit |
| XID writes | `ybox_slow_write()`: one byte at a time, 1 ms apart | pyxid2 writes every command this way. Its reason is not stated; it is kept until a box shows it is not needed |
| A text line | At most 94 characters; printable ASCII only; an overlong line or a control byte drops the whole line | The rest of an overlong line must not read as a line of its own |

## Verification

On 2026-10-08, MSVC 19.44 (`/W4 /WX`, C11, C++17), MinGW-w64 gcc 16.1 and
gcc 11.4 on WSL2 (C99, C11, C++17, `-Werror`), ASan and UBSan on WSL2:

| Check | Result |
|---|---|
| `tests/adapt/box_test.c` | 135 checks pass. Each family: 1000 generated frames decoded whole, byte by byte and in random chunks, with output arrays of 64 and of 1, every field compared; 3 garbage bytes before each frame counted exactly; broken frames with a real frame inside found again; the line rules (seq 0, channel 0 and 256, level 2, missing and extra fields, 64-bit overflow, signs and exponents in samples, identity, comments, overlong lines, control bytes); 200,000 random bytes per family |
| `tests/compile/box.c`, `.cpp` | Build and run on both compilers |
| Mutants (`tests/mutate/box.toml`) | 13 of 13 caught. The first run caught 11: `box-11` (the reserve of 2 entries) was equivalent, which showed the reserve's reason was wrong (now 1 entry, and the mutant writes past the array), and `box-13` (an overlong line's tail read as a line) needed a check |
| Firmware (`firmware/ysp_line/ysp_line.ino`) | Compiles with arduino-cli 1.5.1 for `teensy:avr:teensy41` and `teensy40` (core 1.62.0) and `rp2040:rp2040:rpipico` (core 6.3.0, earlephilhower), in digital and analog mode. Installed as a user, in `C:\tmp\psy-work\devices\arduino\`, not in the repository |

Not checked:

- Any byte from a Cedrus device. The XID decoder follows pyxid2's struct
  formats; a capture from a real box pins it before a timing claim.
- The firmware on a board: not flashed, not run.
- StimTracker 2's 9-byte frame (`'<ccBcIB'` in pyxid2), and XID1 devices,
  which have no timer query.
