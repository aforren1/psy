# psy.serial Python binding

A small CPython extension wrapping the
[psy_serial.h](../../../psy_serial.h) single-header library. It uses the plain
CPython C API (no nanobind/pybind/Cython) to stay dependency-free, and is
built against the **Limited API / stable ABI** (`Py_LIMITED_API =
0x03080000`), so a single `psy/serial.abi3.so` works on CPython 3.8+ without a
rebuild per version.

The distribution is `psy-serial` and the module is `psy.serial`. `psy` is a
[PEP 420](https://peps.python.org/pep-0420/) implicit namespace package (no
`__init__.py`), so `psy-serial` and `psy-parallel` install independently or
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

The library implementation is compiled directly into the extension. On
Linux/macOS the build links `-pthread` for the async-pulse worker; on Windows
it links `setupapi` and `advapi32` for port enumeration.

## Use

```python
import psy.serial as ps

for info in ps.list_ports():            # enumerate without opening
    print(info)   # {'name': '/dev/ttyUSB0', 'description': ..., 'vid': 1027, ...}

# Find a box by USB id instead of hard-coding the port name.
match = ps.find_ports(vid=0x0403, pid=0x6001)     # FTDI FT232R
device = match[0]["name"] if match else "/dev/ttyUSB0"

with ps.Port(device, baud=115200) as port:
    port.write_byte(0x55)                   # raise a trigger
    port.pulse(0x55, 0x00, 2000)            # 0x55, hold 2 ms, then 0x00
    port.pulse_async(0x55, 0x00, 2000)      # same, without blocking
    frame = port.read(6, 500, all=True)     # a 6-byte frame within 500 ms
    pending = port.read(64, 0)              # poll: whatever is buffered now
```

`read` returns `b""` on timeout. A disconnect, an `interrupt()` or a closed
port raise instead, so a listener with a long timeout cannot spin on a dead
port.

See [example.py](example.py).

## Threading

The header's rule holds for Python threads: **one reader thread and one writer
thread** may use a `Port` at the same time, and nothing else.

| Role | Calls |
|---|---|
| reader | `read`, `available`, `purge(rx=True)` |
| writer | `write`, `write_byte`, `drain`, `pulse`, `pulse_async`, `send_break`, `purge(tx=True)` |
| any (one at a time) | `interrupt`, `get_lines`, `set_dtr`, `set_rts` |
| neither | the constructor, `close` (only after both roles returned) |

`read`, `write`, `drain`, `pulse`, `send_break` and `pulse_async`'s onset write
release the GIL, so a listener thread does not stall the interpreter.

Shut a listener down with interrupt, join, close. Do **not** call `close()`
from another thread while a `read()` is in flight: closing a descriptor a
second thread is still using is undefined on POSIX.

```python
stop_reading = False

def listener(port):
    while not stop_reading:
        try:
            data = port.read(64, ps.TIMEOUT_INFINITE)
        except ps.Interrupted:
            return                      # shutdown, not an error
        except ps.Disconnected:
            return                      # the box was unplugged
        handle(data)

t = threading.Thread(target=listener, args=(port,))
t.start()
...
stop_reading = True
port.interrupt()        # wakes the blocked read
t.join()                # the reader has returned
port.close()            # now it is safe
```

## API

| | |
|---|---|
| `ps.list_ports() -> list[dict]` | enumerate ports (`name`, `description`, `serial_number`, `location`, `vid`, `pid`) |
| `ps.find_ports(vid=0, pid=0, serial_number=None, location=None, description=None, name=None) -> list[dict]` | the matching subset; zero/`None` matches anything |
| `ps.Port(device, baud=0, data_bits=0, parity=PARITY_NONE, stop_bits=STOP_BITS_1, flow=FLOW_NONE, write_timeout_ms=0, exclusive=False, low_latency=False, dtr_low_on_open=False, rts_low_on_open=False, keep_dtr_on_close=False, rt_runtime_ns=0, rt_deadline_ns=0, rt_period_ns=0)` | open a port; `device` is required, a zero field means the library default |
| `port.read(n, timeout_ms, all=False) -> bytes` | read; `all=True` waits for the full `n` |
| `port.write(data) -> int` | write a bytes-like object, or any iterable of ints 0..255; returns the count accepted |
| `port.write_byte(value) -> int` | write one byte; returns 1 |
| `port.pulse(on, off, usec) -> int` | blocking pulse; returns 2 |
| `port.pulse_async(on, off, usec) -> int` | non-blocking pulse via the RT worker thread; returns 1 |
| `port.available() -> int` | bytes buffered by the driver |
| `port.drain()` | wait until the transmit buffer is empty |
| `port.purge(rx=True, tx=False)` | discard buffered bytes |
| `port.interrupt()` | wake a `read()` blocked on another thread |
| `port.set_dtr(on)` / `port.set_rts(on)` | drive the output lines |
| `port.get_lines() -> int` | input lines as a `LINE_*` mask |
| `port.send_break(ms)` | hold TX in the break state |
| `port.close()`, `port.is_open` | lifecycle; `Port` is a context manager |

Read-only properties: `is_open`, `device`, `baud`, `data_bits`, `parity`,
`stop_bits`, `flow`, `write_timeout_ms`, `low_latency`, `keep_dtr_on_close`,
`async_policy`, `rd_oserr`, `wr_oserr`, `misc_oserr`. The effective settings
are what the driver accepted, not what you asked for. Log `async_policy`
(`ASYNC_*`) next to timing data: `SCHED_DEADLINE` is refused without
`CAP_SYS_NICE` or under CPU pinning, and the library falls back silently.

Tune the async-pulse worker's real-time reservation (Linux
[SCHED_DEADLINE](https://www.kernel.org/doc/html/latest/scheduler/sched-deadline.html); ns):

```python
port = ps.Port(device, rt_runtime_ns=1_000_000,
               rt_deadline_ns=10_000_000, rt_period_ns=10_000_000)
```

The rule is `0 < runtime <= deadline <= period`; a zero period means "same as
deadline". Invalid combinations raise `ps.Error` at construction.

## Errors

| Exception | Raised when |
|---|---|
| `ps.Error` | base class; also a failed open, a bad argument or an OS error |
| `ps.Disconnected` | the device went away (USB unplug); close the port |
| `ps.Interrupted` | `interrupt()` or `close()` woke a blocked `read()` |
| `ps.Closed` | the port is not open |

The message is the library's `psys_strerror()` text plus the OS error number
from the slot that belongs to the failing role (`rd_oserr`, `wr_oserr` or
`misc_oserr`). Constants mirror the header: `PARITY_*`, `STOP_BITS_*`,
`FLOW_*`, `LINE_*`, `PURGE_*`, `READ_*`, `ERR_*`, `ASYNC_*`,
`TIMEOUT_INFINITE`, `DEFAULT_BAUD`, `DEFAULT_WRITE_TIMEOUT_MS`,
`DEFAULT_RT_{RUNTIME,DEADLINE,PERIOD}_NS`.

Device naming, USB latency, DTR-driven board resets and what a serial trigger
is worth in timing terms are documented in the header:
[psy_serial.h](../../../psy_serial.h).
