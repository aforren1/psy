# psy MATLAB / Octave (MEX) bindings

One MEX function per header, in the command-dispatch style of widmann's
[ppdev-mex](https://github.com/widmann/ppdev-mex): the first argument is a
command string, and `open` returns an opaque `uint64` handle that you pass
back to every other command.

| Source | Wraps | Example |
|---|---|---|
| `psy_parallel.c` | [psy_parallel.h](../../psy_parallel.h) | [example_parallel.m](example_parallel.m) |
| `psy_serial.c` | [psy_serial.h](../../psy_serial.h) | [example_serial.m](example_serial.m) |

## Build

In MATLAB or Octave, from this directory:

```matlab
run build.m            % builds every psy_*.c
build('parallel')      % builds one
```

- **Octave** needs the development package that provides `mex`/`mkoctfile`
  (`sudo apt install octave-dev`, or `liboctave-dev` on older releases).
- **MATLAB** needs a configured compiler (`mex -setup`).

The build adds `-I` for the repository root (where the headers live) and
links `-lpthread` on Linux/macOS for the async-pulse workers. Windows uses
Win32 threads and needs no extra library; `psy_serial` also needs `setupapi`
and `advapi32` for port enumeration, which MSVC picks up from a `#pragma
comment` in the header. A MinGW-based Octave on Windows has no such pragma and
needs `mex -lsetupapi psy_serial.c` by hand.

## psy_parallel

```matlab
ports = psy_parallel('list');                 % struct array: name/backend/base_addr

h = psy_parallel('open');                      % platform defaults
% h = psy_parallel('open', '/dev/parport1');   % ppdev device (Linux)
% h = psy_parallel('open', 'direct', 888);     % raw x86 I/O @ base (root)
% h = psy_parallel('open', 'inpout', 888);     % Windows inpout @ base

psy_parallel('write',      h, 85);             % data register (0..255)
d = psy_parallel('read',   h);                 % read data register (uint8)
psy_parallel('setdir',     h, true);           % data direction: input
psy_parallel('pulse',      h, 85, 2000);       % blocking 2 ms pulse
psy_parallel('pulseasync', h, 85, 2000);       % non-blocking pulse
s = psy_parallel('status', h);                 % status register
c = psy_parallel('control', h);                % read control register
psy_parallel('control',    h, 4);              % write control register
psy_parallel('close',      h);
```

Tune the async-pulse worker's real-time reservation (Linux
[SCHED_DEADLINE](https://www.kernel.org/doc/html/latest/scheduler/sched-deadline.html))
by passing a trailing **struct** (fields in nanoseconds) to `open`:

```matlab
rt = struct('runtime_ns', 5e5, 'deadline_ns', 2e6, 'period_ns', 2e6);
h  = psy_parallel('open', rt);                  % default device + RT params
h  = psy_parallel('open', '/dev/parport0', rt); % with an explicit device
cur = psy_parallel('sched', h);                 % struct of effective params
```

Any subset of fields is accepted. Missing fields are 0, and a 0 `period_ns`
means "same as `deadline_ns`". Leave the struct off for the library defaults.
Invalid combinations (the rule is `0 < runtime <= deadline <= period`) raise
an error at `open`.

### Commands

| Command | Returns | Notes |
|---|---|---|
| `'list'` | struct array | `name`, `backend`, `base_addr`; opens nothing |
| `'open' [, dev \| 'direct' \| 'inpout' [, base]] [, rtStruct]` | `uint64` handle | defaults per platform; optional trailing RT struct (ns) |
| `'write', h, value` | none | data register, 0..255 |
| `'read', h` | `uint8` | data register |
| `'setdir', h, isInput` | none | bidirectional-port direction |
| `'pulse', h, value, usec` | none | blocking |
| `'pulseasync', h, value, usec` | none | non-blocking (RT worker) |
| `'status', h` | `uint8` | status register (read-only) |
| `'control', h [, value]` | `uint8` / none | read or write control register |
| `'sched', h` | struct | effective RT params `runtime_ns`/`deadline_ns`/`period_ns` and `policy` (`deadline`, `fifo`, `time_critical`, `normal`, `none`), the policy the worker actually obtained |
| `'close', h` | none | release the port |

Errors are raised through `error()` with id `psy_parallel:*`. Every open port
is closed automatically on `clear psy_parallel` and at interpreter exit
(a `mexAtExit` hook), so a forgotten `close` cannot leave the async worker
thread running against a freed port. Calling `close` yourself is still good
form.

Platform setup (ppdev permissions, the Windows inpout DLL) is documented in
the header: [psy_parallel.h](../../psy_parallel.h).

## psy_serial

```matlab
ports = psy_serial('list');                    % struct array, one per port
% name / description / serial_number / location / vid / pid
match = psy_serial('find', struct('vid', 1027, 'pid', 24577));   % FTDI FT232R

h = psy_serial('open', '/dev/ttyUSB0');        % 115200 8N1 by default
% h = psy_serial('open', 'COM3', struct('baud', 9600, 'parity', 'even'));

n    = psy_serial('write',     h, uint8([1 2 3]));  % or [1 2 3]
n    = psy_serial('writebyte', h, 85);              % trigger workhorse
data = psy_serial('read',      h, 6, 500, 'all');   % 6-byte frame in 500 ms
data = psy_serial('read',      h, 64, 0);           % poll: [] if nothing
psy_serial('pulse',      h, 85, 0, 2000);      % blocking: 85, 2 ms, then 0
psy_serial('pulseasync', h, 85, 0, 2000);      % non-blocking (RT worker)
info = psy_serial('info', h);                  % effective settings
psy_serial('close', h);
```

`read` returns a `uint8` row vector, empty on timeout. A disconnect, an
`interrupt` or a closed port raise instead, so a listener with a long timeout
cannot spin on a dead port.

`open` takes an optional struct of settings. Any subset is accepted; a missing
field is the library default (115200 baud, 8 data bits, no parity, 1 stop bit,
no flow control, 1000 ms write timeout).

| Field | Values |
|---|---|
| `baud` | any rate the driver accepts, e.g. `9600`, `115200` |
| `data_bits` | `5`..`8` |
| `parity` | `'none'`, `'odd'`, `'even'`, `'mark'`, `'space'` (the last two are rejected on macOS) |
| `stop_bits` | `1` or `2` |
| `flow` | `'none'`, `'rtscts'`, `'xonxoff'` |
| `write_timeout_ms` | milliseconds; the minimum is 1 |
| `exclusive`, `low_latency` | logical |
| `dtr_low_on_open`, `rts_low_on_open`, `keep_dtr_on_close` | logical; see DTR AND DEVICE RESET in the header |
| `rt_runtime_ns`, `rt_deadline_ns`, `rt_period_ns` | async-worker RT reservation (Linux SCHED_DEADLINE), ns |

The RT rule is `0 < runtime <= deadline <= period`; a zero period means "same
as deadline". Invalid combinations raise an error at `open`. `info` reports the
policy the worker actually obtained, which is not always the one requested.

### Commands

| Command | Returns | Notes |
|---|---|---|
| `'list'` | struct array | `name`, `description`, `serial_number`, `location`, `vid`, `pid`; opens nothing |
| `'find' [, filterStruct]` | struct array | filter fields `vid`, `pid`, `serial_number`, `location`, `description`, `name`; absent or 0/`''` matches anything |
| `'open', device [, settingsStruct]` | `uint64` handle | `device` is required |
| `'write', h, data` | count (optional) | `uint8` array, or a `double` array of integers 0..255 |
| `'writebyte', h, value` | count (optional) | one byte, 0..255 |
| `'read', h, n, timeout_ms [, 'all']` | `uint8` row vector | empty on timeout; `'all'` waits for the full `n` |
| `'available', h` | count | bytes buffered by the driver |
| `'drain', h` | none | wait until the transmit buffer is empty |
| `'purge', h [, 'rx' \| 'tx' \| 'both']` | none | discard buffered bytes; default `'rx'` |
| `'interrupt', h` | none | wake a blocked `read`; the wake is latched until one read consumes it |
| `'pulse', h, on, off, usec` | count (optional) | blocking; returns 2 |
| `'pulseasync', h, on, off, usec` | count (optional) | non-blocking; returns 1 when the onset was accepted |
| `'setdtr', h, on` / `'setrts', h, on` | none | drive the output lines |
| `'lines', h` | struct | `mask` plus `cts`, `dsr`, `ri`, `dcd` logicals |
| `'break', h, ms` | none | hold TX in the break state |
| `'info', h` | struct | effective settings, `async_policy` as a string, and `rd_oserr` / `wr_oserr` / `misc_oserr` |
| `'close', h` | none | release the port |

`write`, `writebyte`, `pulse` and `pulseasync` return their count only when
you ask for it (`n = psy_serial('write', ...)`), so a bare call prints
nothing. The count matters: a write that hits `write_timeout_ms` returns fewer
bytes than you passed and is not an error.

Errors are raised through `error()` with an identifier that names the library
code, so a script can branch on it:

| Identifier | Meaning |
|---|---|
| `psy_serial:io` | OS error; the number is in `info.rd_oserr` / `wr_oserr` / `misc_oserr` |
| `psy_serial:disconnected` | the device went away (USB unplug); close the handle |
| `psy_serial:closed` | the port is not open |
| `psy_serial:interrupted` | `interrupt` woke a blocked `read` |
| `psy_serial:arg` | bad argument |
| `psy_serial:open`, `psy_serial:handle`, `psy_serial:usage` | open failure, bad handle, bad call |

Every open port is closed automatically on `clear psy_serial` and at
interpreter exit (a `mexAtExit` hook), so a forgotten `close` cannot leave the
async worker thread running against a freed port. Calling `close` yourself is
still good form.

MATLAB and Octave run this binding on one thread, so the header's
one-reader-one-writer rule collapses to "one call at a time". `interrupt`
exists for symmetry with the C API and for a timer callback that has to end a
long `read`.

Device naming, USB latency, DTR-driven board resets and what a serial trigger
is worth in timing terms are documented in the header:
[psy_serial.h](../../psy_serial.h).
