# psy.parallel Python binding

A small CPython extension wrapping the
[psy_parallel.h](../../../psy_parallel.h) single-header library, which builds
on the shared [psy_rt.h](../../../psy_rt.h). It uses the plain CPython C API
(no nanobind/pybind/Cython) to stay dependency-free, and is built against the
**Limited API / stable ABI** (`Py_LIMITED_API =
0x03080000`), so a single `psy/parallel.abi3.so` works on CPython 3.8+ without
a rebuild per version.

The distribution is `psy-parallel` and the module is `psy.parallel`. `psy` is a
[PEP 420](https://peps.python.org/pep-0420/) implicit namespace package (no
`__init__.py`), so `psy-parallel` and `psy-serial` install independently or
side by side.

## Build / install

From this directory:

```sh
pip install .                       # build + install an abi3 wheel
# or, in place for development:
python setup.py build_ext --inplace
```

The in-place build drops the extension into `psy/`, an otherwise empty
directory kept in git for exactly that purpose (the `psy` namespace has no
source of its own).

setup.py finds the headers at the repository root in a development tree. A
build outside the tree, such as an sdist or a cibuildwheel run, sees only this
directory, so copy `psy_parallel.h` and `psy_rt.h` next to `setup.py` first:

```sh
cp ../../../psy_parallel.h ../../../psy_rt.h .
```

Both copies are gitignored, and the repository root still wins when it is
there, so a stale copy cannot shadow a newer header.

The implementation of `psy_parallel.h`, and of the `psy_rt.h` it includes, is
compiled directly into the extension. On Linux/macOS the build links
`-pthread` for the async-pulse worker.

## Use

```python
import psy.parallel as pp

for info in pp.list_ports():            # enumerate without opening
    print(info)                         # {'name': '/dev/parport0', 'backend': 1, 'base_addr': 0}

# Defaults: ppdev /dev/parport0 (Linux), inpout @ LPT1 (Windows).
with pp.Port() as port:                 # or pp.Port(device="/dev/parport1")
    port.write_data(0x55)               # latch a trigger byte
    port.pulse(0x55, usec=2000)         # blocking: 0x55 for 2 ms, then 0
    port.pulse_async(0x55, usec=2000)   # returns immediately; worker drops it
    s = port.read_status()
    print(hex(s), port.get_status_bit(pp.STATUS_BUSY))
```

Other backends:

```python
pp.Port(backend=pp.BACKEND_DIRECT, base_addr=pp.LPT1)   # Linux raw I/O (root)
pp.Port(backend=pp.BACKEND_INPOUT, base_addr=0x378)     # Windows inpout
```

Tune the async-pulse worker's real-time reservation (Linux
[SCHED_DEADLINE](https://www.kernel.org/doc/html/latest/scheduler/sched-deadline.html); ns):

```python
port = pp.Port(rt_runtime_ns=500_000, rt_deadline_ns=2_000_000, rt_period_ns=2_000_000)
print(port.sched)   # {'runtime_ns': 500000, 'deadline_ns': 2000000, 'period_ns': 2000000}
```

Omit `rt_period_ns` (or leave it 0) and it defaults to the deadline. Leave all
three unset for the library defaults. Invalid combinations (the rule is
`0 < runtime <= deadline <= period`) raise `pp.Error` at construction.

Timestamp a trigger against the library's own clock, so your numbers and its
deadlines share one base:

```python
t0 = pp.now_us()                # monotonic microseconds
port.write_data(0x55)
t1 = pp.now_us()                # the onset lies after t0; t1 - t0 bounds it
```

`psy.serial.now_us()` reads the same clock: both libraries build on
`psy_rt.h`, so one time base covers the whole rig.

See [example.py](example.py).

## Threading

A `Port` is safe to share between Python threads. Every method holds a
per-`Port` lock for the duration of its library call, so only one thread is
inside the library on a given handle at a time. Two `pulse()` calls on one port
therefore serialize: the second waits for the first to finish its width.

The lock is needed because `psy_parallel.h` guards only its data writes against
its own async worker. Every call that returns a bool clears and rewrites one
shared error buffer in the handle, and `pulse()` and `pulse_async()` drop the
GIL, so the GIL alone would not keep a second thread out. A thread waiting for
the lock releases the GIL, so a 2 ms `pulse()` never stalls unrelated Python
code.

What the lock cannot fix is `BACKEND_DIRECT`: `ioperm`/`iopl` grant I/O
permission to the calling thread only, so a direct-backend `Port` is usable
from the thread that opened it (plus the library's own worker) and no other.
See DIRECT IS PER-THREAD in [psy_parallel.h](../../../psy_parallel.h).

## API

| | |
|---|---|
| `pp.now_us() -> int` | monotonic microseconds from the clock the library times its own deadlines against |
| `pp.list_ports() -> list[dict]` | enumerate ports (`name`, `backend`, `base_addr`) |
| `pp.Port(device=None, backend=BACKEND_DEFAULT, base_addr=0, exclusive=False, rt_runtime_ns=0, rt_deadline_ns=0, rt_period_ns=0)` | open a port |
| `port.write_data(value)` / `port.read_data()` | data register |
| `port.set_data_dir(input)` | data-line direction (bidirectional ports) |
| `port.pulse(value, usec)` | blocking pulse (releases the GIL while waiting) |
| `port.pulse_async(value, usec)` | non-blocking pulse via the RT worker thread |
| `port.read_status()` / `port.get_status_bit(mask)` | status register |
| `port.read_control()` / `port.write_control(value)` / `port.set_control_bit(mask, on)` | control register |
| `port.close()`, `port.is_open`, `port.base_addr`, `port.backend`, `port.sched`, `port.async_policy` | lifecycle / info; `async_policy` is the policy the worker obtained (`ASYNC_*`), log it with timing data |

The `ASYNC_*` values come from `psy_rt.h` and mean the same thing in
`psy.serial`. `ASYNC_TIME_CONSTRAINT` is the macOS rung, which this library
never returns.

Errors raise `pp.Error`. Constants: `BACKEND_*`, `ASYNC_*`, `LPT1/2/3`, `STATUS_*`,
`CONTROL_*`, `DEFAULT_RT_{RUNTIME,DEADLINE,PERIOD}_NS`. `Port` is a context
manager (`with pp.Port() as port:`).

Platform setup (ppdev permissions, the Windows inpout DLL) is documented in
the header: [psy_parallel.h](../../../psy_parallel.h).
