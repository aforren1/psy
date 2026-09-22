# psy_serial.h design

Status: **v0.2, implemented.** Windows uses the Win32 COM API with overlapped
I/O; Linux and macOS share a termios backend. Verified on Linux against a
socat pty pair, including a reader thread and a writer thread on one handle
under ThreadSanitizer. The Windows backend compiles clean under MSVC but has
not run on a port: its open-time validation and enumeration entry point run,
but no byte has moved through it, for lack of a COM device. The macOS paths
have not been compiled or run at all, for lack of a machine. No timing claim
in the header has been measured yet, and the async worker has only ever run at
`PSYS_ASYNC_NORMAL`: the test machine grants no `CAP_SYS_NICE`, so the
`SCHED_DEADLINE` and `SCHED_FIFO` rungs of its ladder are written but
unexercised. The header's top comment is the user-facing documentation
(sokol-style); this page records why the API looks the way it does.

## Goals

- Byte I/O over serial ports for the devices used in psychophysics and
  neuroscience rigs: trigger boxes, response boxes, and DIY microcontroller
  boards.
- Same shape as `psy_parallel.h`: one header, zero dependencies, a
  caller-owned handle, a zero-initialized `desc` for defaults, a message
  string in the handle for setup errors.
- Honest timing. The header documents what USB-serial can and cannot do, and
  the API gives the caller the clock to bracket a write.
- Windows, Linux, and macOS. Labs use all three.

## Non-goals

- Protocol layers (XID, SRBox, STK). They belong in device-specific headers or
  in the experiment code, built on `psys_read` with `PSYS_READ_ALL` and
  `psys_write`.
- Async reads, callbacks, or an event loop. A listener thread that blocks in
  `psys_read` is simpler and matches how experiment code is written;
  `psys_interrupt` exists so it can be stopped.
- Custom baud rates through `termios2`/`BOTHER` on Linux. The requested rate
  goes to the driver as given and `psys_open` fails if the driver rejects it;
  on Linux that means rates without a termios `B*` constant. macOS gets
  `IOSSIOSPEED` because the TriggerBox runs at 2 Mbaud and Darwin's termios
  stops at 230400. Windows accepts any `DCB.BaudRate` the driver can do.
- Runtime reconfiguration (`psys_reconfigure`). The devices that switch baud
  after a handshake are GPS receivers (u-blox `UBX-CFG-PRT`), cellular modems
  (`AT+IPR`), HC-05 Bluetooth modules in AT mode, and some bootloaders. None
  is in the target list, and close plus reopen with a new `desc` covers the
  rare case.
- Waiting on modem lines (`psys_wait_lines`). No target device signals on
  CTS/DSR; `psys_get_lines` polls and that is enough.

## Target devices

| Device | Transport | Needs from the API |
|---|---|---|
| Brain Products TriggerBox / TriggerBox Plus | FTDI, 2 Mbaud | `psys_write_byte`, `psys_pulse`; Plus times pulses itself; `IOSSIOSPEED` on macOS |
| BioSemi USB trigger interface | FTDI | `psys_write_byte`, `psys_pulse` |
| Cedrus c-pod, StimTracker, RB-x40 (XID) | FTDI, 115200 8N1 | `psys_write` commands, `PSYS_READ_ALL` for 6-byte key reports, `psys_find_ports` by vid/pid/serial, `low_latency` |
| Black Box ToolKit TTL modules | FTDI | `psys_write_byte` |
| PST Serial Response Box | RS-232 / USB-serial, 19200 | `psys_read` polling, `psys_set_dtr` (some models power the lamps from DTR) |
| Current Designs fORP (serial mode) | USB-CDC | `psys_read` polling |
| Arduino / Teensy / RP2040 DIY boxes | USB-CDC or CH340 | `psys_read`, `psys_write`, `keep_dtr_on_close`, `psys_purge` |
| Legacy RS-232 hardware | UART | parity/stop-bit options, modem lines, `psys_send_break` |

## Decisions

### Return codes everywhere, a message only from psys_open

`psys_open` returns `bool` and writes `psys_error()`, which its own `memset`
clears on entry, as `psyp_` does. Everything else returns an `int`: a count or
a `PSYS_LINE_*` mask, 0 for done, or a negative `PSYS_ERR_*` code, and never
touches the string. `psys_strerror(code)` names a code.

v0.1 split this by "setup" and "data path" and let `interrupt`, `purge`,
`send_break`, `set_dtr` and `set_rts` write the message. That was wrong, and
the header's own shutdown recipe proved it: it calls `psys_interrupt` from the
main thread while the writer thread may be inside `psys_purge(PSYS_PURGE_TX)`
or `psys_send_break`, and an RX purge on the reader races a break on the
writer. Two threads formatting into one 256-byte buffer is a data race under
C11 whether or not anyone reads the result, so the buffer now belongs to the
one function that runs before any other thread exists.

The OS error number goes to the slot of the calling role: `rd_oserr` for the
reader, `wr_oserr` for the writer, `misc_oserr` for the any-role calls
(`interrupt`, `get_lines`, `set_dtr`, `set_rts`), which are documented as
one-at-a-time. `psys_get_lines` used to write `rd_oserr`, which a writer
thread calling it would have scribbled over a failing reader's error number.
`psys_purge` writes the slot of the direction it was asked to purge.

`PSYS_ERR_DISCONNECTED` is distinct from timeout (0) because a USB unplug
otherwise looks like silence: macOS `read()` returns 0 (EOF), Linux `poll()`
returns `POLLHUP` immediately, and a listener with a long timeout would spin
at 100% CPU on a dead port.

### Interrupt

`psys_interrupt(p)` wakes a `psys_read` blocked on another thread, which
returns `PSYS_ERR_INTERRUPTED`. Every experiment shuts down, so every
listener thread needs this. A self-pipe on POSIX (added to the reader's
`poll` set) and a manual-reset event on Windows (waited on next to the
overlapped read's event). `psys_close` also interrupts, but the caller must
join the listener before closing: closing a descriptor another thread is
using is undefined on POSIX.

The wake is **latched**, not dropped. The v0.1 header said an interrupt with
nobody waiting is a no-op; implementing it that way means clearing the event
or draining the pipe at the top of every read, and then the documented
shutdown sequence deadlocks whenever the interrupt lands while the listener
is between two reads of a `PSYS_READ_ALL` loop, or just before its next
`psys_read(PSYS_TIMEOUT_INFINITE)`. So the wake stays pending until one read
consumes it, and several interrupts collapse into one. The cost is that a
handle interrupted while idle returns `PSYS_ERR_INTERRUPTED` from its next
read, and shutdown is the only documented use of the call.

### Read model

`psys_read(p, buf, cap, timeout_ms, flags)` with `PSYS_READ_ANY` returns
when at least one byte is available, with everything buffered at that moment.
`timeout_ms = 0` polls, `PSYS_TIMEOUT_INFINITE` blocks. This is the pyserial
`timeout` model reduced to one call with a per-call timeout instead of
port-level state, so a frame loop can poll with 0 and a listener thread can
block, on the same handle, without reconfiguring the port.

A negative return discards the count. `PSYS_READ_ALL` interrupted or
disconnected part way through a frame returns the code, and the bytes already
in the caller's buffer are neither counted nor reported: a half frame is not
usable, and `psys_interrupt` means shutdown, not "give me what you have". A
caller who wants partial input loops on `PSYS_READ_ANY` instead.

On Windows the `PSYS_READ_ALL` loop pays one `SetCommTimeouts` per iteration,
because the remaining time shrinks each time and `rd_timeout_applied_ms` only
elides repeated identical timeouts. That cache is there for `PSYS_READ_ANY`,
which is the hot path: a frame loop polling with timeout 0, or a listener
blocking on one fixed timeout, sets the timeouts once and never again.

`PSYS_READ_ALL` keeps reading until `cap` bytes or the total timeout, for
framed protocols. It is a flag rather than a separate `psys_read_exact`
because both modes share every argument and the caller's error handling; the
loop is platform-independent and sits over the per-platform `psys__read_some`
primitive. The first read always happens, so `PSYS_READ_ALL` with timeout 0
still collects what is buffered.

### Windows read implementation

pyserial's `COMMTIMEOUTS` model, not `WaitCommEvent`. `EV_RXCHAR` is latched
only after `SetCommMask`, so a byte landing between "is the buffer empty?"
and the event arm is missed until the next byte, which for a one-byte
response is a full timeout late. FTDI and CH34x VCP drivers are also known to
misbehave with `WaitCommEvent` plus `CancelIo`. `{ReadIntervalTimeout =
MAXDWORD, ReadTotalTimeoutMultiplier = MAXDWORD, ReadTotalTimeoutConstant =
timeout}` gives "buffered bytes now, else the first byte within `timeout`"
in one `ReadFile`. The constant may not be `MAXDWORD`, so infinite waits loop
on `0x7FFFFFFF`. `SetCommTimeouts` is port-global, so the reader rewrites the
whole struct with the writer's constant preserved, and only when the timeout
changed (`rd_timeout_applied_ms`).

The `OVERLAPPED` structures live in the handle (`rd_ovl`, `wr_ovl`), never
on the stack: a cancelled read that is not reaped with
`GetOverlappedResult(TRUE)` would complete into a dead frame.

### Write model

`psys_write` blocks until the driver accepted every byte or `write_timeout_ms`
elapsed, and returns the count accepted; a short count is the timeout signal,
not an error code. The timeout is port-level (in `desc`) because a trigger
write should never wait on a policy decision at the call site; the 1 s
default is long enough that only a wedged device hits it. Zero in `desc`
means default, as everywhere in this collection, so the smallest configurable
write timeout is 1 ms; a fail-fast write is not a use case for any target
device. `psys_drain` is separate because draining on every write would add a
round trip to the bridge chip per trigger.

### Pulse

`psys_pulse(p, on, off, usec)` takes both bytes because `0x00` is the idle
byte for latching boxes (TriggerBox, BioSemi, DIY) and a protocol byte for
XID. It is blocking and spins on Windows; the header says to prefer
device-timed pulses.

### Async pulse

`psys_pulse_async(p, on, off, usec)` writes `on` on the calling thread and
returns; a worker thread the library owns writes `off` at an absolute deadline
taken right after the onset write. It mirrors `psyp_pulse_async`: worker
started in `psys_open` so the policy is final before the first trial and no
trial pays thread creation, a ready handshake so `psys_port.async_policy` is
valid when the open returns, one pending job per handle, the RT ladder
(`SCHED_DEADLINE` with `desc.sched`, then `SCHED_FIFO` 80, then a normal
thread with the timer slack removed; `THREAD_PRIORITY_TIME_CRITICAL` with a
high-resolution waitable timer and a QPC spin tail on Windows), and the same
`psys_sched_deadline` contract validated in `psys_open` before any OS call.

This was left out of v0.1, on two arguments that were both wrong. "A worker
buys nothing measurable over USB" confused a floor with a budget: frame
quantization (1 ms full-speed, 125 us high-speed) is the least jitter the
transport can have, while OS scheduling and driver batching add a tail on top
of it, and on Windows that tail is reported to be much larger than a frame.
An elevated-priority thread removes the part that is ours to remove. The
second argument, that the lock would serialize the trigger thread behind a
user write, is a real cost but not a reason to have no call: the alternative
was `psys_pulse`, which does not serialize the trigger thread behind a write,
it stops it dead for the whole width. The point of the call was never
trailing-edge precision alone; it is that the caller gets its thread back.

What is genuinely different from the parallel port is that the trailing edge
is a stream write, not an `outb`, so it can block. That makes the worker a
second writer rather than an invisible helper, and it is documented as one:
every writer-role write takes the worker's lock (PI mutex on POSIX, critical
section on Windows), the worker reports failures through `wr_oserr` like any
writer, and if a stalled device blocks one of them the other waits for up to
`write_timeout_ms`. `psy_parallel.h` does not pay that because `outb` cannot
block. The alternative, giving the worker its own queue and never blocking a
user write, would reorder bytes on a stream where order is the protocol.

Two behaviors carried over because they are about correctness, not
convenience. A plain `psys_write` / `psys_write_byte` / `psys_pulse` while a
pulse is pending cancels the pending trailing byte, because a write means
"hold this" and a stale `off` would undo it a few milliseconds later. And
`psys_close` stops the worker before it closes anything, which makes the
worker write a pending `off` at once instead of waiting out the width, so a
latching box is never left holding a trigger code. On Windows the worker
carries its own `OVERLAPPED` and event (`pa_ovl`, `pa_event`): a transfer is
reaped by the thread that started it, so it never borrows the user writer's.

### Timing statement

The header states the USB latency budget up front (about 1 ms host-to-device,
16 ms device-to-host on FTDI defaults) so users do not treat a serial pulse as
a parallel-port pulse. "Take your timestamp after the write" was a guess; the
header now says to bracket the call with `psys_now_us()` and record both
sides, and to measure the offset to the physical edge once with a scope.
`psys_now_us` is public so the caller and the library share one clock base.

### Threading

One reader thread plus one writer thread per handle, with each function's
role stated in the header (purge RX is reader-side because on Windows
`PurgeComm` aborts an in-flight overlapped read). On POSIX the promise is
free. On Windows it requires `FILE_FLAG_OVERLAPPED` with one `OVERLAPPED`
per direction; synchronous handles serialize all I/O and a blocking read
would stall a trigger write.

Nothing in the library writes shared mutable state except the three
`*_oserr` slots, one per role, and each field of the handle has exactly one
writer once `psys_open` has returned. That is the whole reason the reader and
the writer need no lock between them, and it is why the error message moved
out of every function but `psys_open`.

The async-pulse worker is the one exception, and it is a writer, not a third
role: it shares the writer's `wr_oserr` and every writer-role write is
serialized under its lock. See Async pulse. A reader is never blocked by it,
which is what matters for a listener thread.

### Disconnect mapping

A USB unplug must never look like silence. Windows: `ERROR_ACCESS_DENIED`,
`ERROR_BAD_COMMAND`, `ERROR_DEVICE_NOT_CONNECTED`, `ERROR_GEN_FAILURE`,
`ERROR_NOT_READY`, `ERROR_INVALID_HANDLE` and `ERROR_DEVICE_REMOVED` from a
started or completed overlapped I/O all become `PSYS_ERR_DISCONNECTED`.
`ERROR_OPERATION_ABORTED` is ambiguous, because `psys_purge(PSYS_PURGE_RX)`
aborts a pending read with the same code, so the implementation asks
`ClearCommError` whether the port is still there and reports 0 bytes (a
cancelled read) if it is. Reporting 0 rather than an error is only safe
because `PSYS_PURGE_RX` is a reader-role call: the thread that could have
aborted this read is the thread that is in it. An aborted read still returns
any bytes it transferred, because `serial.sys` fills in the count even on a
cancelled IRP.

One Windows failure mode has no clean mapping: some VCP drivers never complete
a `ReadFile` that was pending when the device was unplugged. The read does not
fail, it simply never returns, and no timeout applies because the total
timeout is only armed while the driver is alive. `psys_interrupt` is the only
thing that ends such a wait, which is another reason the shutdown recipe does
not depend on the listener noticing a disconnect. POSIX: `POLLHUP`, `POLLERR` and `POLLNVAL` from
`poll`, `read() == 0`, and `EIO`, `ENXIO`, `ENODEV`, `EPIPE`. `POLLIN` is
examined before `POLLHUP` so bytes that arrived before the hangup are still
delivered; a Linux pty discards them at master close anyway, but a
USB-serial driver need not.

### Enumeration, per platform

Windows: SetupAPI over `GUID_DEVINTERFACE_COMPORT` (not the Ports device
class, which also holds LPT), the openable name from `PortName` in the
device's registry key, vid/pid parsed out of `SPDRP_HARDWAREID`, and the
serial number out of the device instance id. Instance ids come in three
shapes and only one of them holds a serial number: a tail containing an
ampersand is path-derived (`5&2c7b7f6&0&2`) and is not a serial, and an
`FTDIBUS` id carries the serial plus a trailing port letter, which becomes
`location` because FT2232 and FT4232 channels differ only in that letter.
Otherwise `location` is `SPDRP_LOCATION_INFORMATION`
(`Port_#0003.Hub_#0001`).

That leaves one gap on Windows: two FTDI cables with blank EEPROMs both report
serial `""` and location `"A"`, because the port letter wins the field, and
nothing in the SetupAPI properties of the COM device says which USB port they
are in. Telling them apart needs the cfgmgr32 parent walk
(`CM_Get_Parent` up to the hub, then that device's location), which is more
machinery than this is worth until a lab hits it. Until then: label the
cables, write serial numbers into the EEPROMs with FT_Prog, or unplug one and
see which port name disappears. Linux has no such gap, because the sysfs walk
reaches the real USB device either way.

Both platforms enumerate into a scratch list, sort it, and only then copy out
the first `max` entries. Filling the caller's array as ports are met and
sorting afterwards would hand a caller with a four-entry array whichever four
ports the directory or SetupAPI happened to yield first, which is neither the
first four in any order the user can predict nor stable between calls. The
sort is a natural compare on digit runs, so `COM3` precedes `COM10` and
`ttyUSB2` precedes `ttyUSB10`.

Linux: `/sys/class/tty/*` entries that have a `device` link, walking toward
the root for `idVendor`, `idProduct`, `serial`, `manufacturer` and `product`,
and taking `location` from the USB interface directory (`1-1.4:1.0`).
Entries whose `type` attribute reads 0 are dropped: the 8250 driver registers
every port the chipset could have, and a stock machine otherwise lists eight
`/dev/ttyS*` that cannot be opened. That is a narrower filter than
pyserial's, which drops everything on the `platform` bus and so also hides a
real ISA UART, and it survives the newer kernels that moved these devices to
the `serial-base` bus.

macOS: `/dev/cu.*` names only, with no USB fields. IOKit
(`kIOSerialBSDServiceValue`, walking up to `IOUSBHostDevice`) is the way to
get them, but it would put `-framework IOKit -framework CoreFoundation` on
every translation unit that defines the implementation, which is a real
dependency in exchange for a setup-time convenience, in a collection whose
rule is nothing beyond the OS. The node names carry the FTDI serial number
anyway, so `psys_port_filter.name` covers the common case. Revisit if a lab
needs vid/pid matching on a Mac.

### Discovery by filter

`psys_find_ports(filter, out, max)` takes a `psys_port_filter` whose zero or
NULL fields match anything. It keeps the out/max/return contract of
`psys_list_ports` instead of returning one match, because two identical boxes
on one rig is a real configuration and the caller should see both.
`psys_port_info.location` exists because FTDI multi-port bridges share one
serial number across ports, serial-less devices get path-derived instance ids
on Windows, and Cedrus ships the FTDI default vid/pid, so vid/pid alone
matches every FTDI cable on the rig. Serial comparison is case-insensitive
because Windows uppercases instance ids.

### Defaults and DTR

115200 8N1, no flow control, 1 s write timeout, DTR and RTS asserted on open.
These are the pyserial defaults, which every device vendor's example code
assumes. `desc.device` is the only required field because there is no sane
default port name on any platform.

`keep_dtr_on_close` (clear `HUPCL`) is the real fix for Arduino-class boxes
that reset on the DTR edge: the next open then sees no edge. `dtr_low_on_open`
is best effort because Linux drivers assert DTR inside `open()` before user
code runs.

`keep_dtr_on_close` ended up POSIX-only. The v0.1 plan hoped the Windows
driver would hold DTR while the handle count was nonzero, but the serial
driver drops it inside `CloseHandle` whatever `DCB.fDtrControl` says, and
there is no supported way to stop it. A Windows user who must not reset the
box between sessions has to keep the handle open. The header says so.

### Low latency

`desc.low_latency` is a request, not a requirement, and `psys_port.low_latency`
reports the outcome. Only Linux can honor it at runtime (`ASYNC_LOW_LATENCY`
through `TIOCSSERIAL`, which `ftdi_sio` maps to a 1 ms latency timer). The
header tells Windows and macOS users where the setting actually lives.

The outcome is read back rather than inferred from the ioctl. `cdc_acm`
implements `TIOCSSERIAL`, accepts the flag and does nothing with it, so a
ttyACM device would otherwise report `low_latency = true` while still holding
bytes: a false claim about timing, which is the one thing this collection must
not do. `psys_open` therefore reads
`/sys/bus/usb-serial/devices/<tty>/latency_timer` afterwards and only reports
success when the timer really is 1 ms. That file is also the supported way to
set it for good, from udev, which survives a re-plug and needs no privilege in
the experiment:

```
SUBSYSTEM=="usb-serial", DRIVER=="ftdi_sio", ATTR{latency_timer}="1"
```

### Handle layout

Hot fields first (`is_open`, descriptor or handle, events, per-direction OS
error, write timeout), effective settings next, the two string buffers last,
so the read and write paths do not straddle 384 bytes of text. Platform
fields are public, as in `psy_parallel.h`, for inspection rather than for
writing; the implementation needed no field the specification did not already
declare. The Linux feature-test macro is
`_DEFAULT_SOURCE`, not `_POSIX_C_SOURCE`: glibc hides `CRTSCTS`, `CMSPAR`
and every `B*` rate above 38400 behind `__USE_MISC`.

## Tests

- Compile checks: C11 and C++17 on Windows, Linux, and macOS
  (`tests/compile/`, run by CMake and CI, warnings as errors). The `.cpp`
  wrapper should still be extended to call every public function once, so
  `extern "C"` and const-correctness are exercised and not just parsed.
- Loopback: `tests/loopback/psy_serial_loopback.c`, built with
  `-DPSY_BUILD_LOOPBACK=ON` and run by hand with two device names, either a
  virtual pair (`socat -d -d pty,raw,echo=0 pty,raw,echo=0`; com0com on
  Windows) or one port name twice with TX tied to RX. It checks write N then
  `PSYS_READ_ALL` N and the payload, `PSYS_READ_ANY` with timeout 0 on an idle
  port (0 bytes, under 2 ms, so a poll cannot quietly become a wait),
  `PSYS_READ_ANY` with a 100 ms timeout on an idle port (0 bytes after 95 to
  160 ms), `psys_available`, purge, the short count at a `PSYS_READ_ALL`
  deadline (200 ms nominal, 195 to 260 ms accepted), the write timeout with a
  peer that has stopped draining (a short count after 95 to 160 ms, skipped
  with a note if the transport never blocks), `psys_interrupt` on a reader
  blocked in `PSYS_TIMEOUT_INFINITE` (under 10 ms), `psys_find_ports` with a
  zero filter, a reader thread plus a writer thread on one handle, and that no
  latched interrupt is left over afterwards. Not registered with ctest: no CI
  runner has a port pair.
- Async pulse, on the same pty pair: `psys_pulse_async(0xA1, 0xA2, 2000)`
  then two `PSYS_READ_ANY` reads, timestamping each byte as the reader sees it,
  which is the spacing a device would see (the worker's deadline plus one
  transport hop at each end); a plain write while a pulse is pending, which
  must cancel the trailing byte; two pulses 1 ms apart with different `off`
  bytes, which must yield two onsets and only the newer `off`; and
  `psys_close` on a third handle with a 1 s pulse pending, which must put the
  trailing byte on the wire within 5 ms of the close instead of after the
  width. The concurrency phase also issues an async pulse every eighth write,
  so the worker is writing into the stream while the writer thread writes and
  the reader reads: three threads on one handle.
- Two ordering details the test learned the hard way. A phase that fills the
  transmit path leaves tens of kilobytes inside socat, and a `psys_purge` does
  not stop them arriving, so the test drains both ends until the transport is
  quiet for 200 ms before any check that expects silence. And the interrupt
  that stops the hammer threads can land after the reader has already left on
  its stop flag, leaving the wake latched by design, so the test consumes it
  with one read before the disconnect check.
- Pure logic that no loopback can reach (the natural-order name compare, the
  location prefix match, the device-name length limit, and the `PSYS_ERR_CLOSED`
  return of every function on a closed handle) is covered by a throwaway probe
  during development rather than by a committed test. Worth promoting to
  `tests/` if that logic grows.
- What a socat pty pair does not test: closing one slave does not hang up the
  other, because socat still holds both masters, so the disconnect check is
  informational there. Closing the master does hang up the slave, and
  `psys_read`, `psys_available` and `psys_write` then all return
  `PSYS_ERR_DISCONNECTED`, which is how the POSIX disconnect path was
  verified.
- Concurrency: the same loopback binary built with `-fsanitize=thread` and
  run on a socat pair, one reader thread and one writer thread on one handle
  while the far end is fed and drained. Clean. This is what validates the
  error-code split. It does not prove the split is complete, because a
  function nobody calls concurrently in the test cannot race in the test; the
  argument for `misc_oserr` and for the message buffer belonging to
  `psys_open` is the role table, not the sanitizer.
- Timing: not measured yet. A loopback round trip gives write-accept to
  first-byte latency. For trigger-onset latency use a scope or a second box
  sampling the trigger line, and record the distribution (median, 99th
  percentile), with and without `low_latency`. Add Tracy zones around the
  driver calls if the numbers need explaining.

## Deferred

- Custom baud rates on Linux (`termios2`/`BOTHER`) once a target device
  needs one.
- `psys_wait_lines` if a device that signals on CTS/DSR shows up.
- A clock header (`psy_clock.h`) if a third library needs `psys_now_us`.

## Bindings plan

- `bindings/python/psy_serial/`: same layout as `psy_parallel`, Limited API,
  a `Port` type with `read(n, timeout_ms, all=False) -> bytes` (raising
  `Disconnected`/`Interrupted` subclasses of `Error`), `write(bytes)`,
  `interrupt()`, `find_ports(vid=0, pid=0, serial_number=None, ...)`, and
  `pulse` releasing the GIL. The one-reader-one-writer rule goes in its
  README; Python threads can otherwise `close()` mid-read.
- `bindings/mex/psy_serial.c`: command dispatch (`open`, `write`, `read`,
  `pulse`, ...), picked up by the existing `build.m`.
