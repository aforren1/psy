# psy MATLAB / Octave (MEX) bindings

One MEX function per header, in the command-dispatch style of widmann's
[ppdev-mex](https://github.com/widmann/ppdev-mex): the first argument is a
command string, and `open` returns an opaque `uint64` handle that you pass
back to every other command.

| Source | Wraps | Example |
|---|---|---|
| `psy_parallel.c` | [psy_parallel.h](../../psy_parallel.h) | [example_parallel.m](example_parallel.m) |

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
Win32 threads and needs no extra library.

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
