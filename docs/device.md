# ysp/device.h

Status: v0.1.0, 2026-10-08. The header's manual is the reference. This
page records the decisions the plan (`docs/devices_spec.md`, step 2 of
section 14.2) left open, the results, and what needs the user's hardware.

## How-to: run the photodiode board

1. Wire the photodiode module's digital output to pin 2 of a Teensy 4.x
   or a Raspberry Pi Pico (light gives HIGH; define `YSP_INVERT 1` if it
   gives LOW). For a bare photodiode on an analog pin, define `YSP_ANALOG 1`
   and set `YSP_PIN`, `YSP_HI` and `YSP_LO`.
2. Compile and upload `firmware/ysp_line/` with the Arduino IDE or
   arduino-cli:
   ```
   arduino-cli compile --fqbn teensy:avr:teensy41 firmware/ysp_line
   arduino-cli upload  --fqbn teensy:avr:teensy41 -p <port> firmware/ysp_line
   ```
   (`rp2040:rp2040:rpipico` for a Pico.)
3. Find its key: `photodiode_check --list`.
4. Check the board alone: `photodiode_check --key serial:16C0:0483:: --monitor 10`
   prints each edge, the state changes and the fit.
5. Tape it over the top-left corner of the display and run
   `photodiode_check --key <key>` (WARNING: flicker up to 30 Hz). It prints
   edge minus flip onset; that mean is `desc.onset_offset_ns` of
   `ysp/screen.h`.

## Decisions

| Question | Decision | Why |
|---|---|---|
| The transport | `ysp/serial.h` is required and is the default; `desc.transport` replaces it | Every family of step 2 is serial. A header that must also work without serial would need a second build variant for no device yet. A test, a replay or a later HID or network transport passes its own functions |
| Threads | One reader thread per instance (devices_spec 4.8); `desc.manual` and `ydev_poll()` for no thread | Blocking reads are the simplest correct model, and a hung driver hangs only its own instance. Manual mode is also how the test runs the layer on a virtual clock (`desc.now`) |
| The fit | BRACKET mode for every family: frames the device sends on its own are pairs of width 0, answers to timer queries have the round trip as width | BRACKET is LATE plus a width bound, so both kinds of pair go into one fit |
| Timer queries | Every 100 ms; LINE `q <seq>`, XID `_e5`; none for the photodiode frame | The fit needs pairs between a box's rare presses (docs/rt.md) |
| Bracket bound | 1.3 ms for LINE (native USB); 20 ms for XID | An XID answer waits in the FTDI chip's latency timer, 16 ms by default; a 1.3 ms bound would refuse every answer. The lower envelope of the bracket ends still finds the least late ones |
| XID's 1 ms between bytes | Kept, as pyxid2 does; the bracket starts before the last byte | The box answers after the last byte, so the gap is not in the bracket |
| Identify | XID `_c1` must answer `_xid0` (another mode is switched with `c10`, logged), then `_d2`, `_d3`, `_d4`; LINE `i` must answer an `I` line; asked three times, 500 ms each | A wrong device at the key fails before the session (FAILED, not retried). Three tries cover a board that resets when its port opens |
| Silence | LOST after 2 s with no byte | Some USB-serial drivers never fail a read after an unplug (docs/serial.md). Syncs and query answers keep a live device talking |
| Reconnect | Every 1 s by key; each open starts a new fit; a GAP record gives the last byte before and the time it ran again | A device's clock can reset on power-up |
| `unc_us` | The device's tick plus the widest bracket the map rests on; without brackets, the fit's spread | The spread of XID answers (15.5 ms behind the latency timer) is not the map's error (about 1 ms): the first version used it and gave 16 ms per event. Mutant `dev-12` keeps the fix |
| A map past the read time | Clamped to the read time and counted (`clamped`) | An event cannot happen after it arrived; such a map is fit error or a board bug |
| Kind | 0 = the family's: BOX for XID and LINE, SYNC for the photodiode frame | `desc.kind` 0 would otherwise be KEYBOARD, which no box is |
| The bridge | The caller's sink; the example uses `yscr_push_input()` | `ysp/screen.h` stays out of this header; a logger or a test is another sink |
| Source tier | UNKNOWN | No loopback has checked any device on this layer (devices_spec 12) |

## Results

On 2026-10-08, MSVC 19.44 and MinGW-w64 gcc 16.1 on Windows 11, gcc 11.4
on WSL2. "Event t minus edge" is the mapped time minus the true host time
of the simulated edge.

| Check | Result |
|---|---|
| `tests/adapt/device_test.c`, 373 checks | Pass on all three compilers, also under ThreadSanitizer, ASan and UBSan on WSL2 |
| LINE board, native USB (125 us microframe, 50 us tail), 30 ppm, counter wrapping | 10 to 30 s: +6.0 to +15.1 us; unplugged at 30 s for 3 s with a clock reset; 43 to 60 s: +5.4 to +13.6 us. States OPENING, RUNNING, LOST, RUNNING, CLOSED; one GAP of 3.03 s; 522 queries, 522 answers; fit +29.67 ppm, widest bracket 211 us |
| XID box, a press every 2 to 4 s behind a 16 ms latency timer, -50 ppm | from 10 s: -972 to +852 us; from 120 s: -972 to +481 us (an event lies anywhere in its 1 ms tick, hence the -1 ms); 2768 queries, all answered, none refused; `unc_us` 1428 |
| Photodiode frame, 1 ms USB frame, no queries | +6.0 to +100.8 us |
| Silence 3 s | LOST after 2 s, RUNNING again |
| A device that does not identify | FAILED after 1.5 s, not opened again |
| An edge stamped 60 ms ahead | Clamped to its read time, counted once |
| The reader thread on the real clock, 1.2 s | 20 events, 12 queries, CLOSED after stop |
| `tests/loopback/device_loopback.c` on a socat pty pair (WSL2) | The serial transport: identified, 120 of 120 edges, the 100 after the first second within 229 us of their stamps, 55 queries answered; socat killed: LOST in 10 ms |
| `photodiode_check --list` (`examples/device/photodiode_check.c`) on the development laptop | "no serial ports" (none attached) |
| Mutants (`tests/mutate/device.toml`) | 11 of 12 caught; `dev-11` (an answer matched to any query) is equivalent here, because the simulated boards answer each query before the next. The first run missed `dev-10` (no clamp; `test_clamp` added) and `dev-12` (the uncertainty bound was too loose) |

## What needs the user's hardware

| Hardware | Checks | How |
|---|---|---|
| Teensy 4.x or Pico with the photodiode module | The firmware runs; identify; query widths on native USB; the fit on real USB delivery; edge minus flip onset; the bridge under a window | Steps 1 to 5 above |
| Unplugging that board during `--monitor` | LOST, reconnect by key, the gap | Pull the cable, put it back |
| The USB-serial adapter | Nothing yet: outputs are step 3 | |
| A Cedrus box (XID) | The decoder on real bytes, `_c1` and `_d` answers, the 1 ms byte gap, brackets behind the FTDI latency timer | `photodiode_check --family xid --key serial:0403:6001:<serial>: --monitor 30`, pressing buttons |
| The LabStreamer | An independent reference for the same flips | After LSL exists (step 4) |

## Not done

- Outputs (step 3), the `ysp/screen.h` match key for keyboard-mode boxes,
  other transports.
- A device on this layer under a real load; any timing claim.
