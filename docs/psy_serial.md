# psy_serial.h design

Status: **v0.4, implemented.** Windows uses the Win32 COM API with overlapped
I/O; Linux and macOS share a termios backend. Since v0.4 the header depends on
`psy_rt.h` for the clock, the waits, the scheduling ladder and the deadline
worker; see [Depends on psy_rt.h](#depends-on-psy_rth). Verified on Linux against a
socat pty pair, including a reader thread and a writer thread on one handle
under ThreadSanitizer and under AddressSanitizer plus UndefinedBehaviorSanitizer.
The Windows backend compiles clean under MSVC but has not run on a port: its
open-time validation and enumeration entry point run, but no byte has moved
through it, for lack of a COM device. The macOS paths compile on macOS in CI
but have never run on a device, for lack of a machine. No timing claim in the
header has been measured yet, and the async worker has only ever run at
`PSYS_ASYNC_NORMAL`: the test machine grants no `CAP_SYS_NICE`, so the
`SCHED_DEADLINE` and `SCHED_FIFO` rungs of its ladder are written but
unexercised. The header's top comment is the user-facing documentation
(sokol-style); this page records why the API looks the way it does.

## Goals

- Byte I/O over serial ports for the devices used in psychophysics and
  neuroscience rigs: trigger boxes, response boxes, and DIY microcontroller
  boards.
- Same shape as `psy_parallel.h`: one header plus the shared `psy_rt.h`
  (since v0.4) and no other dependency, a caller-owned handle, a
  zero-initialized `desc` for defaults, and a message string in the handle
  for setup errors.
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

### Depends on psy_rt.h

v0.4 deletes every line of timing code in this header and includes `psy_rt.h`
instead: `psys_now_us` is `psyrt_now_us`, the pulse-width wait is
`psyrt_sleep_until`, the reservation type is `psyrt_sched_deadline`, the policy
enum is `psyrt_policy`, and the async-pulse thread is a `psyrt_worker`. It is a
hard dependency, in the `stb_truetype.h` / `stb_rect_pack.h` and
`sokol_gfx_imgui.h` / `sokol_gfx.h` sense: the user copies two files.

Three arguments, none of them tidiness.

- **Three copies of the same code were already drifting.** `psy_parallel.h`,
  `psy_serial.h` and the experiment code each had a monotonic clock, a spin,
  a sleep and an RT ladder. The header promised that a caller's
  `psys_now_us()` timestamps and the library's deadlines "share one base";
  with two copies of the conversion that was a claim about two pieces of code
  agreeing, and it is now the same function call.
- **The Windows spin bug existed only here.** This copy's blocking
  `psys_pulse` busy-spun the entire pulse width on `QueryPerformanceCounter`,
  where `psy_parallel.h` had long spun only the last ~1.14 ms and slept the
  rest, and this copy's worker spin could not see a replacement pulse or a
  close until v0.3 found it by hand. Neither bug was findable in the other
  copies, because they were not in the other copies. One implementation is one
  place to fix and one place to measure.
- **The real-time rungs are untested everywhere.** No machine in this project
  grants `CAP_SYS_NICE`, so `SCHED_DEADLINE` and `SCHED_FIFO` have never been
  obtained by any of the copies. Three unexercised ladders are three times the
  risk for none of the coverage; `psy_rt.h` at least has `examples/rt_jitter.c`
  pointed at it, and `psys_port.async_policy` now reports the same rung, by the
  same name, as everything else in the collection.

What the user gives up is the single file. `psy_serial.h` alone no longer
compiles, and the STATUS block, USAGE and BUILDING all say so. That is the
whole cost, and it is paid once at copy time.

#### The writer mutex stays here

`psyrt_worker` runs its callback **with its own lock released**. That is not an
oversight in `psy_rt.h`, it is what makes the callback usable: the callback may
re-arm its own worker and may block, and a submit from another thread never
waits behind it. But this library's trailing byte is a write into a byte stream
that can block for up to `write_timeout_ms`, and the library's promise is that
exactly one thread writes the port at a time. Running the callback under the
worker's lock would buy that serialization and deadlock the first time a submit
raced a stalled write; so a straight substitution of `psyrt_worker` for the old
private worker would have quietly dropped the guarantee instead.

The split: `psy_rt.h` owns *when*, this header owns *who writes*. `psys__async`
holds one small writer mutex (a PI `pthread_mutex` where the platform has one,
a `CRITICAL_SECTION` on Windows) plus the pending trailing byte, armed with the
deadline it is due at. Every writer-role write takes the mutex; a user write
disarms the pending byte, calls `psyrt_worker_cancel` and writes;
`psys_pulse_async` writes the onset, arms and submits, all under the mutex; and
the callback takes the mutex and writes only what is still armed and due.

The arm flag plus the deadline is the token, and it is checked at write time
rather than carried from submit time, because a `psyrt_job_fn` receives the
same `ctx` for every job on a handle and cannot carry a per-job counter of its
own. That turns out to be the stronger test anyway. `psyrt_worker_cancel`
covers the ordinary case, where the job has not left `psy_rt.h`'s lock yet. It
cannot reach the window between the worker releasing that lock and the callback
acquiring the writer mutex, and in that window a user write may have disarmed
the byte (the callback must write nothing) or a new pulse may have armed a
different byte at a later deadline (the callback must leave it to its own
deadline). Asking "what is armed, and is it due?" answers both, and a flush
from `psyrt_worker_stop` writes whatever is armed at once, which is what
`psys_close` needs.

Lock order is writer mutex, then `psy_rt.h`'s lock; the callback holds nothing
of `psy_rt.h`'s when it takes the writer mutex, so the cycle does not close.

#### What the rename costs

`psys_sched_deadline` is a typedef of `psyrt_sched_deadline` and
`psys_async_policy` of `psyrt_policy`, with `PSYS_ASYNC_*` and
`PSYS_DEFAULT_RT_*` kept as aliases, so source that used them still compiles.
The enumerators' numeric values did change, because `psyrt_policy` has a macOS
time-constraint rung between `DEADLINE` and `FIFO` that this header never had,
and macOS `async_policy` can now report it. Anything that stored those integers
has to be rebuilt rather than relinked; at v0.x, with the bindings built from
the same tree, that is a recompile and a changelog line, not a migration.

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

### The wait call, per POSIX platform

Linux waits with `poll()`. macOS waits with `select()`, because Apple's
`poll(2)` still carries the note that it does not support devices, and every
descriptor this library waits on is one. pyserial makes the same split for the
same reason, which is the only field evidence available for a platform nobody
here can run.

The alternative considered was keeping `poll()` on both and writing a note into
the header that a Mac should be checked first. It was rejected: a note does not
make a wait wake up. The macOS backend is entirely unverified either way, so
`select()` costs no verification that existed, while `poll()` risks a listener
thread that never wakes or a poll that reports nothing on a live port, which is
a silent timing failure rather than a loud one.

What `select()` gives up is the hangup bit: there is no `POLLHUP` equivalent,
and `exceptfds` means nothing on a tty. A dead port on Darwin surfaces instead
as a readable descriptor whose `read()` returns 0, or as `EIO`/`ENXIO` out of
`read()` or `write()`, both already mapped to `PSYS_ERR_DISCONNECTED`. What it
costs is `FD_SETSIZE`: `select()` cannot name a descriptor at or above it, so
`psys_open()` refuses such a port on Darwin with a message instead of writing
past an `fd_set`. Both waits sit behind one `psys__wait()` helper, so the
reader and the writer have one shape and only the wait call differs.

`read() == 0` is read as a hangup only because the wait reported the
descriptor readable first. Ungated it would be ambiguous: with `VMIN = 0` and
`VTIME = 0`, which is how this library configures the line, Linux `n_tty`
returns 0 for "nothing buffered" as well.

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
on the stack: a canceled read that is not reaped with
`GetOverlappedResult(TRUE)` would complete into a dead frame.

An aborted read that carried no bytes does not end the call. `psys_interrupt`
is checked at the top of every iteration, and a vanished device is asked about
with `ClearCommError`, so an abort that is neither can only be this thread's own
`psys_purge(PSYS_PURGE_RX)`. Returning 0 there would end a
`PSYS_TIMEOUT_INFINITE` read with no data, no interrupt and no disconnect,
which the header promises cannot happen, and would read as a deadline to the
`PSYS_READ_ALL` loop. The read instead resumes, against an absolute deadline
taken on entry so a finite timeout is not restarted by the abort.

A failed `SetCommTimeouts` maps through the same disconnect test as everything
else. It is where a reader on an unplugged port often fails first, and a
`PSYS_ERR_IO` there would have hidden the unplug.

On the write side, only `ERROR_OPERATION_ABORTED` on a live port is reported as
a short count. Every other failure of `GetOverlappedResult` is an error code:
a short count is how `psys_write` signals its timeout, so a driver error
returned that way is indistinguishable from "the device is slow", and the
documented response to a short count is to send the rest again.

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
taken right after the onset write. Since v0.4 that thread is a `psyrt_worker`,
so the ladder, the ready handshake that makes `psys_port.async_policy` final
when `psys_open` returns, the one-pending-job rule, the deadline wait with its
spin tail and the flush on stop are all `psy_rt.h`'s, and `psys_open` validates
`desc.sched` with `psyrt_sched_normalize` before any OS call. What stays here
is the writer mutex and the armed trailing byte; see
[Depends on psy_rt.h](#depends-on-psy_rth). The shape still mirrors
`psyp_pulse_async`: the worker starts in `psys_open`, so the policy is final
before the first trial and no trial pays for thread creation.

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
every writer-role write takes the writer mutex (PI `pthread_mutex` where the
platform has one, critical section on Windows), the worker reports failures
through `wr_oserr` like any writer, and if a stalled device blocks one of them
the other waits for up to
`write_timeout_ms`. `psy_parallel.h` does not pay that because `outb` cannot
block. The alternative, giving the worker its own queue and never blocking a
user write, would reorder bytes on a stream where order is the protocol.

Two behaviors carried over because they are about correctness, not
convenience. A plain `psys_write` / `psys_write_byte` / `psys_pulse` while a
pulse is pending cancels the pending trailing byte, because a write means
"hold this" and a stale `off` would undo it a few milliseconds later (v0.4
keeps that promise with the arm token as well as with `psyrt_worker_cancel`,
because a cancel cannot reach a job the worker has already dispatched). And
`psys_close` stops the worker before it closes anything, which makes the
worker write a pending `off` at once instead of waiting out the width, so a
latching box is never left holding a trigger code. On Windows the worker
carries its own `OVERLAPPED` and event (`pa_ovl`, `pa_event`): a transfer is
reaped by the thread that started it, so it never borrows the user writer's.

That flush is not free: it runs with the worker's lock held and `psys_close`
is blocked in the join behind it, so "at once" is a statement about the
deadline, not about the call returning. It is bounded by a fixed 50 ms rather
than by `write_timeout_ms`, which a trigger sender may well have set to a
second or to `PSYS_TIMEOUT_INFINITE`; a close that hangs forever on a wedged
device is worse than a trailing byte that never lands. A healthy device takes
one byte in microseconds, so the bound only ever fires on a device that is
already lost, and the loopback check of this path measures a few hundred
microseconds. The header states the 50 ms, because a promise of "at once" that
is really "up to `write_timeout_ms`" is the kind of claim this collection must
not make.

The Windows worker's sub-millisecond spin used to re-check the quit flag and a
job generation counter, a bug found and fixed here in v0.3 and nowhere else,
which is one of the reasons there is now one worker for the whole collection.
`psy_rt.h` keeps that behavior (its loop re-evaluates after every spin) and
documents the race that remains: a submit or a cancel issued inside the final
spin window is seen after the spin, not during, so a replacement pulse can be
late by up to the spin window. That window is `PSYRT_DEFAULT_SPIN_NS`, wider on
Windows (about 1.2 ms) than on Linux and macOS (200 us), for the reasons
`psy_rt.h`'s WAITS section gives.

### Timing statement

The header states the USB latency budget up front (about 1 ms host-to-device,
16 ms device-to-host on FTDI defaults) so users do not treat a serial pulse as
a parallel-port pulse. "Take your timestamp after the write" was a guess; the
header now says to bracket the call with `psys_now_us()` and record both
sides, and to measure the offset to the physical edge once with a scope.
`psys_now_us` is public so the caller and the library share one clock base, and
since v0.4 it is literally `psyrt_now_us`, so that base is shared with
`psy_rt.h` and with every other header built on it.

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
`ERROR_OPERATION_ABORTED` is ambiguous, because `psys_purge` aborts a pending
transfer with the same code, so the implementation asks `ClearCommError`
whether the port is still there. On a dead port it is a disconnect. On a live
one it was this thread's own purge, because `PSYS_PURGE_RX` is a reader-role
call and `PSYS_PURGE_TX` a writer-role one: the thread that could have aborted
this transfer is the thread that is in it. A read then resumes its wait rather
than reporting 0, and a write reports what got through, which is the only place
in the write path where a short count is not a timeout. An aborted transfer
still returns any bytes it moved, because `serial.sys` fills in the count even
on a canceled IRP; the interrupted path and the completed-then-aborted path
keep them on the same terms, which they did not in v0.2.

One Windows failure mode has no clean mapping: some VCP drivers never complete
a `ReadFile` that was pending when the device was unplugged. The read does not
fail, it simply never returns, and no timeout applies because the total
timeout is only armed while the driver is alive. `psys_interrupt` is the only
thing that ends such a wait, which is another reason the shutdown recipe does
not depend on the listener noticing a disconnect. POSIX: `POLLHUP`, `POLLERR`
and `POLLNVAL` from `poll`, `read() == 0`, and `EIO`, `ENXIO`, `ENODEV`,
`EPIPE`. Readability is examined before the hangup so bytes that arrived before
it are still delivered; a Linux pty discards them at master close anyway, but a
USB-serial driver need not. macOS has no hangup bit at all, because it waits
with `select()`; see the wait-call section above.

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
cannot do that job on Linux at all, and the header now says so rather than
calling it best effort: the kernel raises DTR and RTS inside `open()` itself,
`O_NONBLOCK` included, so by the time any user code runs the edge is already
on the wire and the option only drops the line again afterwards.

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
success when the timer really is 1 ms. `<tty>` is the basename of the device
after `realpath()`, not of `desc.device`: the header tells experiments to open
the stable `/dev/serial/by-id/usb-..._-if00-port0` symlinks, whose basenames
match nothing in sysfs, so without resolving the name first the attribute never
opened and `low_latency` read false on exactly the FTDI devices whose timer
`ftdi_sio` had just programmed. A fixed `PATH_MAX` buffer, because `psys_open`
allocates nothing the caller did not ask for. An unreadable attribute still
leaves `low_latency` false, which is the honest answer for `cdc_acm` and for
every non-USB port. That file is also the supported way to
set it for good, from udev, which survives a re-plug and needs no privilege in
the experiment:

```
SUBSYSTEM=="usb-serial", DRIVER=="ftdi_sio", ATTR{latency_timer}="1"
```

### Reopening an open handle

`psys_open` starts by zeroing the handle, which is also what clears the message
buffer. On a handle that still owns a port that zeroing drops the descriptor or
`HANDLE` and orphans the worker thread, with nothing left to close either with.
One test of `is_open` before the `memset` turns that into a message and a
`false`. It is not a general safety net, because the contract is "zeroed or
closed" in both directions and an uninitialized handle has no valid flag to
read; it catches the mistake that is actually made, which is reopening a handle
the caller forgot to close. The repository's own example and loopback test had
to start zeroing their stack handles when this landed, which is the argument
for the check rather than against it.

`PSYS_API` is on the definitions as well as the declarations, the sokol
convention. With the keyword on the declarations alone, `-DPSYS_API=static`
does not give a caller a private copy of the library; it gives a compile error,
because the definitions are still external.

### Handle layout

Hot fields first (`is_open`, descriptor or handle, events, per-direction OS
error, write timeout), effective settings next, the two string buffers last,
so the read and write paths do not straddle 384 bytes of text. Platform
fields are public, as in `psy_parallel.h`, for inspection rather than for
writing; the implementation needed no field the specification did not already
declare. The `psyrt_worker` handle is not one of them: it sits in the
heap-allocated `psys__async` block that `psys_open` already allocates, not in
`psys_port`, so the public handle the bindings mirror does not grow half a
kilobyte of opaque OS storage and its size does not depend on whether the build
has threads. Nothing allocates on a read, write or pulse path either way.

The Linux feature-test macro is `_DEFAULT_SOURCE`, not `_POSIX_C_SOURCE`:
glibc hides `CRTSCTS`, `CMSPAR` and every `B*` rate above 38400 behind
`__USE_MISC`, and an explicit `_POSIX_C_SOURCE` switches `__USE_MISC` off for
the whole translation unit. Since `psy_rt.h` v0.2 both headers ask for the same
macro, on their first include, and both stand down when any of
`_DEFAULT_SOURCE`, `_GNU_SOURCE`, `_POSIX_C_SOURCE` or `_XOPEN_SOURCE` is
already set. The include order between them therefore does not matter. The one
rule that remains is the rule for any system header: an implementation file
that includes other system headers first defines `_DEFAULT_SOURCE` itself.

## Tests

- Compile checks: C11 and C++17 on Windows, Linux, and macOS
  (`tests/compile/`, run by CMake and CI, warnings as errors). The `.cpp`
  wrapper should still be extended to call every public function once, so
  `extern "C"` and const-correctness are exercised and not just parsed.
- Double implementation, by hand: a translation unit that defines both
  `PSY_SERIAL_IMPLEMENTATION` and `PSY_RT_IMPLEMENTATION` compiles and links in
  either include order, as C11 and as C++17, because `psy_rt.h`'s
  implementation block guards itself. Since `psy_rt.h` v0.2 the `psy_rt.h`-first
  order needs no `_DEFAULT_SOURCE` of its own: `psy_rt.h` sets it on its first
  include, implementation or not, so `CRTSCTS` and `realpath` are still there
  when `psy_serial.h` is compiled afterwards.
- Loopback: `tests/loopback/psy_serial_loopback.c`, built with
  `-DPSY_BUILD_LOOPBACK=ON` and run by hand with two device names, either a
  virtual pair (`socat -d -d pty,raw,echo=0 pty,raw,echo=0`; com0com on
  Windows) or one port name twice with TX tied to RX. It checks write N then
  `PSYS_READ_ALL` N and the payload, `PSYS_READ_ANY` with timeout 0 on an idle
  port (0 bytes, under 2 ms, so a poll cannot quietly become a wait),
  `PSYS_READ_ANY` with a 100 ms timeout on an idle port (0 bytes after 95 to
  160 ms), that `psys_open` on an already-open handle is refused and leaves
  the handle usable, `psys_available`, purge, the short count at a `PSYS_READ_ALL`
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
  must cancel the trailing byte; thirty rounds of the same cancel raced against
  the worker's own dispatch, alternating a zero width (due the moment it is
  submitted) with 200 us, where either order on the wire is allowed but the
  last byte must always be the write that means "hold this"; two pulses 1 ms
  apart with different `off`
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
  while the far end is fed and drained. Clean, as is the same run under
  `-fsanitize=address,undefined`. This is what validates the
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
- ~~A clock header (`psy_clock.h`) if a third library needs `psys_now_us`.~~
  Done in v0.4, as `psy_rt.h`, and it took the waits and the worker with it.

## Bindings

Done, both of them.

- `bindings/python/psy_serial/`: distribution `psy-serial`, module
  `psy.serial` (a PEP 420 namespace package shared with `psy-parallel`),
  Limited API 0x03080000, one `psy/serial.abi3.so`. A heap-type `Port` built
  with `PyType_FromSpec`, constructed with keywords that mirror `psys_desc`
  (`device` required; `baud`, `data_bits`, `parity`, `stop_bits`, `flow`,
  `write_timeout_ms`, `exclusive`, `low_latency`, `dtr_low_on_open`,
  `rts_low_on_open`, `keep_dtr_on_close`, `rt_runtime_ns`, `rt_deadline_ns`,
  `rt_period_ns`). Methods: `read(n, timeout_ms, all=False) -> bytes`,
  `write(data) -> int`, `write_byte(value) -> int`, `pulse(on, off, usec)`,
  `pulse_async(on, off, usec)`, `available()`, `drain()`,
  `purge(rx=True, tx=False)`, `interrupt()`, `set_dtr(on)`, `set_rts(on)`,
  `get_lines()`, `send_break(ms)`, `close()`, and the context-manager pair.
  Read-only properties for the effective settings, `async_policy` and the
  three `*_oserr` slots. Module level: `list_ports()`, `find_ports(vid=0,
  pid=0, serial_number=None, location=None, description=None, name=None)`,
  and a constant per `PSYS_*` enum, flag and default. Errors are `Error` with
  `Disconnected`, `Interrupted` and `Closed` derived from it, carrying the
  `psys_strerror()` text and the OS error of the failing role. `read`,
  `write`, `drain`, `pulse`, `send_break` and `pulse_async`'s onset release
  the GIL. The one-reader-one-writer rule and the interrupt-join-close
  shutdown recipe are in its README; Python threads can otherwise `close()`
  mid-read.
- `bindings/mex/psy_serial.c`: command dispatch (`list`, `find`, `open`,
  `write`, `writebyte`, `read`, `available`, `drain`, `purge`, `interrupt`,
  `pulse`, `pulseasync`, `setdtr`, `setrts`, `lines`, `break`, `info`,
  `close`), uint64 handles in a table that `mexAtExit` drains, and negative
  codes raised as `error()` with the identifier `psy_serial:<code name>`.
  Picked up by the existing `build.m`; no platform gate, because the header
  supports all three.
