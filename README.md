# psy

Single-header C libraries for psychophysics and neuroscience rigs, in the
style of [stb](https://github.com/nothings/stb) and
[sokol](https://github.com/floooh/sokol). Each header is standalone, has no
dependencies beyond the OS, compiles as C11 or C++17, and carries its own
documentation in its top comment.

## Libraries

| Header | Purpose | Platforms | Status |
|---|---|---|---|
| [psy_parallel.h](psy_parallel.h) | Parallel port (LPT) trigger output and line input, blocking and async pulses. | Windows, Linux | v0.1. Linux tested; Windows backend compiled but not run on hardware. |
| [psy_serial.h](psy_serial.h) | Serial port (RS-232, USB-serial, USB-CDC) byte I/O for trigger and response boxes, blocking and async pulses. | Windows, Linux, macOS | v0.2. Linux tested against a virtual port pair; the Windows backend compiles but has not run on a port; macOS not yet compiled. [Design notes](docs/psy_serial.md). |

## Quick start

1. Copy the header you need into your project.
2. In exactly one `.c` or `.cpp` file, define the implementation macro before
   the include:

   ```c
   #define PSY_PARALLEL_IMPLEMENTATION
   #include "psy_parallel.h"
   ```

3. Include the header normally everywhere else.
4. Read the header's top comment. It is the manual: usage, platform setup,
   timing notes, build flags, and the API reference.

To build the examples and compile checks in this repository:

```sh
cmake -B build
cmake --build build
ctest --test-dir build
```

Or without CMake, from the repository root:

```sh
cc -O2 -pthread -I. -o parallel_trigger examples/parallel_trigger.c   # Linux
cl /O2 /I. examples\parallel_trigger.c                                # Windows (MSVC)
```

CMake consumers can `add_subdirectory(psy)` and link `psy::psy`, which only
adds the include path.

## Layout

```
psy_<name>.h              one library per header, at the root; the header is the documentation
examples/<name>_*.c       runnable demos, one or more per library
tests/compile/            per-header compile checks (C11, C++17, no-threads)
tests/loopback/           opt-in hardware tests (-DPSY_BUILD_LOOPBACK=ON), run by hand
docs/                     design notes and specifications only; nothing a header already says
bindings/python/<name>/   one pip package per library
bindings/mex/             one psy_<name>.c per library, shared build.m
CMakeLists.txt            builds examples and compile checks; registers libraries
```

## Conventions

- **Names.** `psy_<name>.h` uses a short, unique function prefix `psy<x>_`
  and macro prefix `PSY<X>_`, registered in the table above: `psyp_`/`PSYP_`
  for parallel, `psys_`/`PSYS_` for serial. One letter while it stays
  unique; a later `psy_screen.h` picks something like `psyscr_`. The
  implementation macro is `PSY_<NAME>_IMPLEMENTATION`. Private symbols use a
  double underscore (`psyp__now_ns`).
- **Handles.** The caller allocates the handle struct, zeroed or closed.
  `psy<x>_open(p, desc)` fills it. A zero field in `desc` means "default";
  set fields with designated initializers (`psys_desc d = { .device =
  "COM3" };`, C99 or C++20) so everything unset is zero. Some headers have a
  required field (`psys_desc.device`); the header says so.
- **Errors.** A function that cannot run while another thread uses the handle
  returns `bool` and leaves a message in the handle, cleared on entry;
  `psy<x>_error(p)` reads it. Every function a second thread may call returns
  an `int` count or a negative code and never touches the message buffer.
  Where the line falls is per header: `psy_parallel.h` puts all its setup
  calls in the first group, `psy_serial.h` only `psys_open`, because its
  shutdown recipe has a reader, a writer and a main thread on one handle.
- **Threads.** Headers that spawn a worker thread honor `PSY<X>_NO_THREADS`
  to drop it, and link `-pthread` on POSIX otherwise.
- **Platforms.** A header `#error`s on platforms it does not support, so an
  unsupported build fails at compile time, not at run time.
- **Documentation.** sokol-style. The header's top comment holds the manual
  in labeled sections (USAGE, platform setup, TIMING, BUILDING, ...), and
  every declaration carries its reference comment. `docs/` holds only design
  notes for work that is not in a header yet. Code comments say why, not how.
- **Timing.** Anything that claims a latency number measures it. Each header
  says how.

## Add a library

1. Write `psy_<name>.h` with the top-comment manual, the API reference on
   each declaration, and the MIT-0 block, following the conventions above.
2. Add `tests/compile/psy_<name>.c` and `.cpp` (copy an existing pair and
   change the name).
3. Register it in `CMakeLists.txt`: append to `PSY_LIBS`, set
   `PSY_PREFIX_<name>`, `PSY_PLATFORMS_<name>`, and `PSY_THREADS_<name>`.
   Compile checks, examples, and a `tests/loopback/psy_<name>_loopback.c` if
   there is one are found by name from there.
4. Add at least one `examples/<name>_*.c`. It must exit with a documented
   code when no hardware is present; add that expectation to the "Run
   examples" step in `.github/workflows/ci.yml`.
5. Add a row to the table above.
6. Bindings are optional: `bindings/python/<name>/` (add it to the CI wheel
   matrix) and `bindings/mex/psy_<name>.c` (add a platform gate to
   `build.m` if the header does not support every OS).

CI builds every registered header on Windows, Linux, and macOS, so step 3 is
what puts a header under test.

## Bindings

- **Python**: one distribution per library under
  [bindings/python/](bindings/python/), each a dependency-free CPython
  extension on the Limited API (one abi3 wheel per platform for CPython
  3.8+). They share the `psy` namespace (PEP 420, no `__init__.py`), so
  `pip install psy-parallel psy-serial` gives `import psy.parallel` and
  `import psy.serial`, and either installs alone. CI builds the wheels with
  cibuildwheel on Linux, Windows, and (serial only) macOS.
- **MATLAB / Octave**: [bindings/mex/](bindings/mex/), one MEX function per
  library (`psy_parallel`, `psy_serial`) with ppdev-mex-style command
  dispatch.

## License

MIT-0 (public domain equivalent). See [LICENSE](LICENSE) and the end of each
header.
