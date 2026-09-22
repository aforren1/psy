/* psy_parallel.h - v0.3 - public domain single-header parallel-port library
 *
 *   A small C library for reading and writing the PC parallel port (LPT).
 *   Built for low-latency 8-bit trigger output of the kind used in
 *   psychophysics / EEG / electrophysiology rigs, where a byte written to
 *   the data lines is latched onto a recording system.
 *
 *   Inspired by:
 *     - pyparallel    (https://github.com/pyparallel/pyparallel)
 *     - ppdev-mex     (https://github.com/widmann/ppdev-mex)
 *   and written in the single-header style of the stb / sokol libraries.
 *
 *   REQUIRES psy_rt.h, its sibling in this repository, beside it. That
 *   header owns the clock, the waits, the scheduling ladder and the
 *   trailing-edge worker; this one owns the port. Copy BOTH files (the
 *   stb_truetype / stb_rect_pack arrangement). Nothing else is needed: the
 *   pair still depends on nothing but the OS.
 *
 *   Targets Windows and Linux.
 *
 *   ---------------------------------------------------------------------
 *   CHANGELOG
 *   ---------------------------------------------------------------------
 *   v0.3 - Depends on psy_rt.h, and its own copies of the monotonic clock,
 *          the sleep-until, the SCHED_DEADLINE/FIFO ladder, the Windows
 *          waitable timers and the worker thread are gone. All three psy
 *          headers now time against one implementation instead of three.
 *          psyp_sched_deadline is a typedef of psyrt_sched_deadline and
 *          psyp_async_policy of psyrt_policy. The PSYP_ASYNC_* and
 *          PSYP_DEFAULT_RT_* names survive as aliases, but the policy
 *          VALUES changed: psyrt_policy carries a macOS rung
 *          (PSYRT_POLICY_TIME_CONSTRAINT = 2) that this header never
 *          returns, so FIFO, TIME_CRITICAL and NORMAL each moved up by one.
 *          Code comparing names is unaffected; code that stored, indexed or
 *          wire-formatted the integers must be rebuilt. PSYP_NO_THREADS now
 *          implies PSYRT_NO_THREADS. psyp_open() validates desc.sched on
 *          Windows too, where it used to be ignored.
 *          On PSYP_BACKEND_DIRECT the worker thread's own ioperm()/iopl()
 *          grant runs from psyrt_worker_desc.on_start (psy_rt.h v0.2), which
 *          executes on the worker thread before psyrt_worker_start()
 *          returns. A refused grant therefore fails psyp_open() as it did in
 *          v0.2, instead of waiting to show up as async_write_failed on the
 *          first trailing edge. (An earlier v0.3 draft, before psy_rt.h had
 *          that hook, did the latter.)
 *   v0.2 - psyp_open() forces the data direction to forward on every
 *          backend, so a port left in reverse mode by a previous client no
 *          longer makes every trigger float. New psyp_desc.dll_path lets a
 *          binding name the inpout DLL explicitly (the loader searches the
 *          EXECUTABLE's directory, which is python.exe or MATLAB for the
 *          bindings). New psyp_port.async_write_failed reports a failed
 *          trailing-edge write from the async worker. The DIRECT backend's
 *          per-thread I/O permission is now granted on the worker thread
 *          itself, and the per-thread rules are documented. Windows creates
 *          psyp_pulse()'s waitable timer in psyp_open(), not on first pulse.
 *   v0.1 - first release.
 *
 *   ---------------------------------------------------------------------
 *   USAGE
 *   ---------------------------------------------------------------------
 *   Copy TWO files into your project, psy_parallel.h and psy_rt.h, and keep
 *   them in the same directory (this header includes "psy_rt.h"). Then do
 *   this:
 *
 *       #define PSY_PARALLEL_IMPLEMENTATION
 *
 *   in *one* C or C++ file before including this header to create the
 *   implementation. Every other file just includes the header normally.
 *   That one file also creates psy_rt.h's implementation, unless the
 *   translation unit already has it; you never need to define
 *   PSY_RT_IMPLEMENTATION for this header's sake, and defining it anyway
 *   (or implementing psy_serial.h in the same file) still yields exactly
 *   one copy.
 *
 *       #define PSY_PARALLEL_IMPLEMENTATION
 *       #include "psy_parallel.h"
 *
 *       psyp_port pp;
 *       psyp_desc desc = {0};                         // sensible defaults
 *       if (!psyp_open(&pp, &desc)) {
 *           fprintf(stderr, "open failed: %s\n", psyp_error(&pp));
 *           return 1;
 *       }
 *       psyp_write_data(&pp, 0x55);                   // raise a trigger
 *       psyp_pulse(&pp, 0x55, 2000);                  // 0x55 for 2 ms, then 0
 *       psyp_close(&pp);
 *
 *   ---------------------------------------------------------------------
 *   BACKENDS
 *   ---------------------------------------------------------------------
 *   Linux:
 *     PSYP_BACKEND_PPDEV   - the kernel ppdev character device
 *                            (/dev/parport0). The portable, recommended
 *                            path. Setup, once:
 *                              sudo modprobe ppdev        # creates /dev/parportN
 *                              sudo usermod -aG lp $USER  # device access; re-login
 *                            (add `ppdev` to /etc/modules-load.d/ to keep
 *                            it across reboots.) "No such file or directory"
 *                            on open almost always means ppdev is not
 *                            loaded. The `lp` printer driver only holds the
 *                            port while printing; unloading it is rarely
 *                            needed. NOTE: claiming the port (PPCLAIM)
 *                            BLOCKS until the port is free, it does not
 *                            fail. If another process holds it, psyp_open()
 *                            hangs; find the holder with
 *                            `lsof /dev/parport0` before you open.
 *     PSYP_BACKEND_DIRECT  - raw x86 port I/O via outb/inb. Lowest latency,
 *                            but requires root (CAP_SYS_RAWIO) and
 *                            x86/x86_64. You must know the I/O base address
 *                            (/proc/ioports). Legacy ISA bases below 0x400
 *                            use ioperm(), scoped to the 3 registers. PCI/
 *                            PCIe cards live above 0x400, where ioperm()
 *                            cannot reach, so the library uses iopl(3)
 *                            there, which grants the process EVERY I/O
 *                            port; a wrong base address then writes to
 *                            whatever device owns it.
 *
 *   DIRECT IS PER-THREAD. ioperm() and iopl() grant I/O permission to the
 *   CALLING THREAD only, not to the process. With PSYP_BACKEND_DIRECT a
 *   handle is therefore usable from two threads and no others: the thread
 *   that called psyp_open(), and the psy_rt.h worker thread psyp_open()
 *   started. The worker grants itself permission on its own thread, from
 *   psyrt_worker_desc.on_start, which runs there before psyrt_worker_start()
 *   returns; so it is safe wherever it was started from, and a REFUSED grant
 *   fails psyp_open() rather than one trigger's trailing edge much later.
 *   outb/inb from any other thread raises
 *   SIGSEGV; there is no error return to check. Call psyp_open(),
 *   psyp_write_data(), psyp_pulse() and the register accessors from one
 *   thread, and use psyp_pulse_async() when you need a trailing edge issued
 *   from elsewhere. This matters most for the Python and MEX bindings, which
 *   are called from whatever thread the host runtime provides: prefer
 *   PSYP_BACKEND_PPDEV there, whose file descriptor is process-wide.
 *
 *   Closing a DIRECT handle revokes I/O permission for the whole thread, not
 *   just for that handle's 3 registers when iopl() was used, and ioperm()
 *   ranges of two handles may overlap. If you open several DIRECT handles on
 *   one thread, closing any of them can leave the others unable to write.
 *   Open one DIRECT handle per thread, or close them all together.
 *
 *   Windows (UNTESTED - compiled in CI but not run on real hardware; see
 *   parallel64 https://github.com/tekktrik/parallel64 for a maintained,
 *   Windows-focused alternative):
 *     PSYP_BACKEND_INPOUT  - loads inpout32.dll / inpoutx64.dll at runtime
 *                            (https://www.highrez.co.uk/downloads/inpout32/)
 *                            and calls its Out32/Inp32 entry points. Modern
 *                            Windows forbids user-mode port I/O, so a kernel
 *                            helper driver such as InpOut is required. The
 *                            DLL self-installs its driver on first use and
 *                            needs administrator rights to do so. Find the
 *                            port's I/O base address in Device Manager (the
 *                            LPT port, then Resources) and pass it as
 *                            desc.base_addr. NOTHING validates that address:
 *                            inpout writes to whatever port you name, and
 *                            0x64 (keyboard controller) or 0xCF8 (PCI
 *                            config) from a typo can hang the machine. The
 *                            DLL is loaded with the CWD excluded from the
 *                            search path, so put it next to the EXECUTABLE
 *                            or in System32, or pass its full path in
 *                            psyp_desc.dll_path. The executable is not
 *                            always yours: under the Python or MEX bindings
 *                            it is python.exe or MATLAB, so a DLL shipped
 *                            beside the binding module is found only via
 *                            dll_path.
 *
 *     Caveat: InpOut is an unmaintained, legacy-signed kernel driver that
 *     grants ring-0 port I/O. On hardened systems (Secure Boot with Memory
 *     Integrity enabled, or the Windows vulnerable-driver blocklist) it may
 *     be refused no matter how it is installed, and there is no maintained
 *     drop-in replacement.
 *
 *     What IS exercised on Windows is the timing underneath: since v0.3 the
 *     waits and the trailing-edge worker are psy_rt.h's, and
 *     examples/rt_jitter.c measures those on Windows 11. The register writes
 *     on top of them are the part that has never reached hardware.
 *
 *   Trigger use needs a real PCIe/PCI parallel-port card or an onboard LPT.
 *   USB-to-parallel *printer* adapters do not expose a register-level I/O
 *   port on any platform.
 *
 *   PSYP_BACKEND_DEFAULT picks ppdev on Linux and inpout on Windows.
 *
 *   ---------------------------------------------------------------------
 *   REGISTER MODEL
 *   ---------------------------------------------------------------------
 *   A standard (SPP) parallel port exposes three byte-wide registers at
 *   consecutive I/O addresses, base+0/+1/+2:
 *
 *     Data    (base+0, R/W) 8 output bits -> pins 2..9. The trigger byte.
 *     Status  (base+1, R)   5 input bits  -> pins 15,13,12,10,11.
 *     Control (base+2, R/W) 4 output bits -> pins 1,14,16,17 + dir bit.
 *
 *   Some pins are inverted in hardware relative to their register bit
 *   (Busy, nStrobe, nAutoFeed, nSelectIn). This library returns/accepts the
 *   raw register bits, matching pyparallel; use the PSYP_STATUS_* and
 *   PSYP_CONTROL_* masks and mind the hardware inversion noted below.
 *
 *   psyp_open() forces the data direction to forward (output) on every
 *   backend: PPDATADIR 0 on ppdev, PSYP_CONTROL_DIR cleared on direct and
 *   inpout. A bidirectional port left in reverse mode by a previous client
 *   tri-states the data pins, so every trigger byte would float instead of
 *   driving the recorder. Call psyp_set_data_dir(p, true) afterwards if you
 *   really want to read the data lines.
 *
 *   ---------------------------------------------------------------------
 *   PULSES AND TIMING
 *   ---------------------------------------------------------------------
 *   Every wait, every clock read and every scheduling decision here belongs
 *   to psy_rt.h. Its WAITS and SCHEDULING sections are the reference for
 *   what each one costs and what it is worth, and examples/rt_jitter.c
 *   measures the waits on YOUR rig rather than asserting a number. Pulse
 *   deadlines are psyrt_now_ns() times, so a pulse timestamp and a
 *   psyrt_now_us() or psys_now_us() timestamp taken in the same process
 *   subtract directly.
 *
 *   psyp_pulse(p, value, usec) writes value, waits until an absolute
 *   deadline taken right after that write, then writes 0. The wait is
 *   psyrt_sleep_until() with PSYRT_DEFAULT_SPIN_NS: the OS gets the bulk of
 *   the width and the last spin window is spun on the clock (about 1.2 ms
 *   on Windows, where even the high-resolution waitable timer can fire that
 *   late, and 200 us on Linux; psy_rt.h WAITS says why the two differ).
 *   It blocks the calling thread for the whole width, and that thread is
 *   whatever the caller has, at whatever priority the caller runs: no
 *   reservation, no elevated priority, preemptible by anything. Its
 *   trailing edge is therefore LESS precise than psyp_pulse_async's, which
 *   spins the same window on a thread that climbed the ladder. Same wait,
 *   different thread.
 *
 *   psyp_pulse_async(p, value, usec) returns at once. The leading edge is
 *   written on the calling thread, so the trigger ONSET is never delayed by
 *   thread scheduling. One psy_rt.h deadline worker per port writes the
 *   trailing 0 at an absolute deadline taken right after the onset write.
 *   The worker is started by psyp_open(), so no trial pays for thread
 *   creation and the rung it obtained is final before the first pulse. It
 *   elevates ITSELF as far as this OS and this process's privileges allow:
 *   SCHED_DEADLINE with the reservation in psyp_desc.sched (default 1 ms of
 *   CPU per 10 ms), else SCHED_FIFO 80 (above the kernel's threaded IRQ
 *   handlers, below migration and the watchdog), else a normal thread with
 *   the 50 us timer slack dropped, on Linux; THREAD_PRIORITY_TIME_CRITICAL
 *   on Windows. Asking is not getting: SCHED_DEADLINE needs CAP_SYS_NICE
 *   and is refused as well when the thread's affinity is not the full root
 *   domain (taskset, cpusets, an isolcpus-pinned rig), and the fall back is
 *   silent.
 *
 *   psyp_port.async_policy is the rung the worker got, a psyrt_policy.
 *   psyrt_policy_name() prints it. Log it: the same jitter histogram means
 *   different things at different rungs.
 *
 *   The worker cannot report a failed trailing-edge write through
 *   psyp_error(), because the message buffer belongs to the calling thread.
 *   It sets psyp_port.async_write_failed instead. The flag is sticky: check
 *   it between trials or at the end of a block, and treat it as "the data
 *   lines may still be asserted, and some trigger offsets are missing". A
 *   PSYP_BACKEND_DIRECT worker that cannot get its own ioperm()/iopl() grant
 *   is NOT one of the cases it covers: that is settled at open time, where
 *   it fails psyp_open() outright.
 *
 *   Every data write on a handle is serialized under one lock this header
 *   owns. It is not psy_rt.h's worker lock: that one guards the job slot and
 *   is released before the trailing edge runs, which is what lets the
 *   callback take this one. A plain psyp_write_data() or psyp_pulse() issued
 *   while an async pulse is pending CANCELS that pulse's trailing edge: a
 *   write means "hold these lines", and a stale trailing 0 must not undo it.
 *   The cancel is not a race the write can lose, not even against a worker
 *   already inside its final spin window, where psyrt_worker_cancel() no
 *   longer bites: both take this header's lock, and the trailing edge writes
 *   only if the pending token it was submitted with is still the current
 *   one. A psyp_pulse_async() issued while one is pending moves the single
 *   trailing edge to the newer off-time, earlier or later. psyp_close()
 *   stops the worker, which runs a pending trailing edge IMMEDIATELY rather
 *   than waiting out the rest of the width, so the lines are never left
 *   asserted.
 *
 *   Timing is best-effort and bounded by OS scheduling. For the tightest
 *   triggers use PSYP_BACKEND_DIRECT on a real-time-tuned kernel with an
 *   isolated CPU. Measure on your rig with a scope or a second recorder,
 *   and record async_policy with the numbers; do not assume.
 *
 *   ---------------------------------------------------------------------
 *   BUILDING
 *   ---------------------------------------------------------------------
 *   psy_rt.h must sit beside this header. It is included by source, so it
 *   adds nothing to the link line; point -I / /I at the directory holding
 *   the pair.
 *
 *   Link -pthread on Linux for the async worker; Windows needs no extra
 *   library. psy_rt.h BUILDING carries the rest of the POSIX link line,
 *   since it is now the header that reads the clock and raises the thread.
 *
 *   PSYP_NO_THREADS drops threading and psyp_pulse_async entirely, and
 *   defines PSYRT_NO_THREADS for you so psy_rt.h's worker goes with it. The
 *   two must agree within a translation unit. If you include psy_rt.h
 *   BEFORE psy_parallel.h, define PSYRT_NO_THREADS yourself alongside
 *   PSYP_NO_THREADS: by then psy_rt.h has already read the macro. Define
 *   both or neither. PSYRT_NO_THREADS alone is a compile error here rather
 *   than a psyp_pulse_async() with no worker to call.
 *
 *   Define PSYP_API to override the default `extern` linkage. PSYRT_API is
 *   separate and belongs to psy_rt.h.
 *
 *       cc -O2 -pthread -I. -o parallel_trigger examples/parallel_trigger.c
 *       cl /O2 /I. examples\parallel_trigger.c
 *
 *   ---------------------------------------------------------------------
 *   BINDINGS
 *   ---------------------------------------------------------------------
 *   Python (CPython Limited API) and MATLAB/Octave (MEX) bindings live in
 *   the psy repository under bindings/python/psy_parallel and bindings/mex.
 *
 *   ---------------------------------------------------------------------
 *   LICENSE: public domain / MIT-0, see end of file.
 */
#ifndef PSY_PARALLEL_H_INCLUDED
#define PSY_PARALLEL_H_INCLUDED

/* Feature-test macro for the Linux implementation. This header's own needs
 * are modest, but the psy_rt.h implementation it pulls in below wants
 * clock_nanosleep() and mlockall(), and psy_rt.h's own feature-macro block
 * lives inside its include guard: by the time this file asks for that
 * implementation, the guard may already have been passed by an earlier
 * include, and the block can no longer fire. So the macro is set HERE too,
 * before the first system header. Both headers ask for the SAME macro on
 * Linux and both stand down once it is set, so whichever of them comes first
 * settles it and the include order does not matter.
 *
 * If you include other system headers before this one in your implementation
 * file, define _DEFAULT_SOURCE yourself. Prefer it to _POSIX_C_SOURCE: glibc
 * treats an explicit _POSIX_C_SOURCE (or _XOPEN_SOURCE, or -std=c11 without
 * -std=gnu11) as "strict POSIX", which switches _DEFAULT_SOURCE OFF for the
 * WHOLE translation unit, so strdup(), usleep(), M_PI and friends vanish from
 * the rest of your implementation file as well. If you must set
 * _POSIX_C_SOURCE, set _DEFAULT_SOURCE alongside it; glibc then keeps both. */
#if defined(PSY_PARALLEL_IMPLEMENTATION) && defined(__linux__) && \
    !defined(_POSIX_C_SOURCE) && !defined(_GNU_SOURCE) && !defined(_DEFAULT_SOURCE)
    #define _DEFAULT_SOURCE 1
#endif

/* psy_rt.h reads PSYRT_NO_THREADS when IT is first included, which may be
 * from another header entirely, so the coupling has to be declared before the
 * include below and cannot be repaired afterwards. */
#if defined(PSYP_NO_THREADS) && !defined(PSYRT_NO_THREADS)
    #define PSYRT_NO_THREADS
#endif
#if defined(PSYRT_NO_THREADS) && !defined(PSYP_NO_THREADS)
    #error "psy_parallel: PSYRT_NO_THREADS without PSYP_NO_THREADS. \
psyp_pulse_async needs psy_rt.h's worker. Define both or neither (defining \
PSYP_NO_THREADS first defines PSYRT_NO_THREADS for you, but only if this \
header is reached before psy_rt.h)."
#endif

/* A hard dependency, not an option: psyrt_policy and psyrt_sched_deadline are
 * part of this header's public types, and psy_rt.h owns every wait below. Keep
 * the two files together. */
#include "psy_rt.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PSYP_API
#define PSYP_API extern
#endif

/* Default I/O base addresses of the legacy ISA LPT ports. Add-in PCI/PCIe
 * cards usually live elsewhere; check your system (Linux: /proc/ioports;
 * Windows: Device Manager -> port -> Resources). */
#define PSYP_LPT1 0x378
#define PSYP_LPT2 0x278
#define PSYP_LPT3 0x3BC

/* Status register (base+1) bit masks. Read-only.
 * NOTE: Busy (bit 7) is inverted in hardware: the bit reads 0 while the
 * physical Busy line is asserted high. */
#define PSYP_STATUS_BUSY    0x80  /* pin 11, hardware-inverted */
#define PSYP_STATUS_ACK     0x40  /* pin 10, nAck             */
#define PSYP_STATUS_PAPER   0x20  /* pin 12, paper-out        */
#define PSYP_STATUS_SELECT  0x10  /* pin 13, select-in        */
#define PSYP_STATUS_ERROR   0x08  /* pin 15, nError/nFault    */

/* Control register (base+2) bit masks. Read/write.
 * NOTE: Strobe, AutoFeed and SelectIn are inverted in hardware relative to
 * their register bit; Init is not. PSYP_CONTROL_DIR=1 switches an enhanced
 * (PS/2/bidirectional) port's data lines to input. */
#define PSYP_CONTROL_STROBE   0x01  /* pin 1,  nStrobe,   hardware-inverted */
#define PSYP_CONTROL_AUTOFEED 0x02  /* pin 14, nAutoFeed, hardware-inverted */
#define PSYP_CONTROL_INIT     0x04  /* pin 16, nInit                        */
#define PSYP_CONTROL_SELECT   0x08  /* pin 17, nSelectIn, hardware-inverted */
#define PSYP_CONTROL_DIR      0x20  /* data direction: 1 = input/reverse    */

typedef enum psyp_backend {
    PSYP_BACKEND_DEFAULT = 0, /* ppdev on Linux, inpout on Windows */
    PSYP_BACKEND_PPDEV,       /* Linux kernel ppdev character device */
    PSYP_BACKEND_DIRECT,      /* Linux raw x86 port I/O (ioperm)     */
    PSYP_BACKEND_INPOUT       /* Windows inpout32/inpoutx64 DLL      */
} psyp_backend;

/* Real-time scheduling reservation for the async-pulse worker thread on Linux
 * (SCHED_DEADLINE). This is psyrt_sched_deadline under an old name: the type
 * is shared with psy_rt.h and psy_serial.h, so one reservation struct can be
 * handed to all three. Times are in nanoseconds. They do NOT set the pulse
 * width (that is clock-driven); they govern how quickly and predictably the
 * worker gets the CPU when it wakes to write the trailing edge, which is edge
 * jitter.
 *
 *   runtime_ns  - CPU budget guaranteed (and capped) per period.
 *   deadline_ns - relative deadline; smaller = higher EDF priority = lower
 *                 wake latency, but harder to admit alongside other RT tasks.
 *   period_ns   - replenishment interval; reserved bandwidth = runtime/period.
 *
 * Leave all three zero for the library defaults below. If you set any field,
 * require 0 < runtime_ns <= deadline_ns <= period_ns (a zero period_ns is
 * taken as "same as deadline_ns"); psyp_open() rejects any other combination
 * with an error. A valid reservation that the kernel cannot admit (or any
 * setup on Windows) transparently falls back to SCHED_FIFO / normal. */
typedef psyrt_sched_deadline psyp_sched_deadline;

#define PSYP_DEFAULT_RT_RUNTIME_NS  PSYRT_DEFAULT_RT_RUNTIME_NS  /*  1 ms */
#define PSYP_DEFAULT_RT_DEADLINE_NS PSYRT_DEFAULT_RT_DEADLINE_NS /* 10 ms */
#define PSYP_DEFAULT_RT_PERIOD_NS   PSYRT_DEFAULT_RT_PERIOD_NS   /* 10 ms */

/* Scheduling policy the async-pulse worker actually obtained: psyrt_policy,
 * the one ladder every psy header reports against. Requesting SCHED_DEADLINE
 * is not getting it: the kernel refuses it without CAP_SYS_NICE, and also when
 * the thread's CPU affinity is not the full root domain (taskset, cpusets, an
 * isolcpus-pinned experiment process), and the library then falls back. Read
 * psyp_port.async_policy after psyp_open(), print it with psyrt_policy_name(),
 * and log it next to your timing data; jitter numbers without it are
 * unlabeled.
 *
 * The PSYP_ASYNC_* spellings below are kept so v0.2 code still compiles. They
 * name the same rungs, but their VALUES changed in v0.3: psyrt_policy has a
 * macOS rung at 2 that this header, being Windows and Linux only, never
 * returns. Compare the names, not the integers, and rebuild anything that
 * stored the old ones. */
typedef psyrt_policy psyp_async_policy;

#define PSYP_ASYNC_NONE          PSYRT_POLICY_NONE          /* no worker      */
#define PSYP_ASYNC_DEADLINE      PSYRT_POLICY_DEADLINE      /* Linux          */
#define PSYP_ASYNC_FIFO          PSYRT_POLICY_FIFO          /* Linux          */
#define PSYP_ASYNC_TIME_CRITICAL PSYRT_POLICY_TIME_CRITICAL /* Windows        */
#define PSYP_ASYNC_NORMAL        PSYRT_POLICY_NORMAL        /* nothing granted*/

/* Open description. Zero-initialize it and only set what you need; missing
 * fields fall back to sensible defaults inside psyp_open(). */
typedef struct psyp_desc {
    psyp_backend backend;   /* default: PSYP_BACKEND_DEFAULT          */
    const char*  device;    /* ppdev path, default "/dev/parport0"    */
    uint16_t     base_addr; /* direct/inpout base, default PSYP_LPT1   */
    bool         exclusive; /* ppdev: claim the port exclusively (PPEXCL) */
    psyp_sched_deadline sched; /* async-worker RT params; all-zero = defaults */
    /* Windows/inpout: full path of the inpout DLL to load, e.g.
     * "C:\\rig\\inpoutx64.dll". NULL (the default) searches the executable's
     * directory, System32 and AddDllDirectory() entries, never the current
     * directory. A binding must usually set this, because the executable is
     * python.exe or MATLAB, not the module the DLL ships beside. Ignored on
     * Linux. */
    const char*  dll_path;
} psyp_desc;

/* Port handle. Allocate it (stack/heap), pass to psyp_open(), and treat the
 * fields as opaque. */
typedef struct psyp_port {
    psyp_backend backend;
    uint16_t     base_addr;
    bool         is_open;
    char         error[256];
#if defined(_WIN32)
    void* dll;     /* HMODULE of the inpout DLL */
    void (*out_fn)(void); /* Out32 entry point, generic function pointer: ISO C
                           * has no object-to-function pointer conversion   */
    void (*inp_fn)(void); /* Inp32 entry point                              */
#else
    int  fd;       /* ppdev file descriptor (-1 if direct/closed) */
    bool claimed;  /* ppdev port claimed                          */
    bool direct;   /* using ioperm()+outb/inb                     */
#endif
    void* async;   /* async-pulse block (psyrt_worker + data lock), started by
                    * psyp_open(), or NULL                                    */
    psyp_sched_deadline sched; /* effective async-worker RT params (Linux)    */
    psyp_async_policy async_policy; /* rung the worker obtained; final once
                                     * psyp_open() returns                     */
    /* Non-zero once an async trailing-edge write has failed on this handle.
     * The worker must not touch p->error (that buffer belongs to the calling
     * thread), so this flag is how a dropped trigger offset becomes visible.
     * Sticky: psyp_open() clears it and nothing else does, so poll it between
     * trials or at the end of a block. Read it, do not write it. */
    int async_write_failed;
} psyp_port;

/* One detected parallel port, as returned by psyp_list_ports(). */
typedef struct psyp_port_info {
    char         name[64];  /* openable identifier: ppdev path (Linux) or
                             * "LPTn" (Windows). Pass to psyp_desc.device on
                             * Linux; on Windows it is informational. */
    psyp_backend backend;   /* backend this entry was discovered for */
    uint16_t     base_addr; /* I/O base if known, else 0 */
} psyp_port_info;

/* --- discovery ---------------------------------------------------------- */

/* Enumerate parallel ports the system exposes, without opening any. Writes up
 * to `max` entries into `out` (pass out=NULL/max=0 to just count) and returns
 * the total number found, which may exceed `max`.
 *
 *   Linux:   scans /dev/parport0../dev/parport15 (the ppdev nodes); entries
 *            carry the device path and PSYP_BACKEND_PPDEV. For raw DIRECT
 *            access, find base addresses in /proc/ioports instead.
 *   Windows: lists LPT* DOS devices via QueryDosDevice; entries carry the name
 *            and PSYP_BACKEND_INPOUT with base_addr 0 (supply the real base
 *            address yourself via psyp_desc.base_addr).
 *
 * Typical use:
 *   int n = psyp_list_ports(NULL, 0);
 *   psyp_port_info* v = malloc(n * sizeof *v);
 *   psyp_list_ports(v, n);
 */
PSYP_API int psyp_list_ports(psyp_port_info* out, int max);

/* --- lifecycle ---------------------------------------------------------- */

/* Open a parallel port per `desc` (NULL = all defaults). Returns true on
 * success; on failure returns false and leaves a message in psyp_error().
 * `p` must be zeroed or closed: a fresh handle has no valid is_open flag, so
 * psyp_open() cannot detect and close a still-open port for you. Opening an
 * open handle leaks its descriptor and orphans its async worker.
 *
 * Forces the data direction to forward (output) so a port left in reverse
 * mode by a previous client does not tri-state every trigger byte; see
 * REGISTER MODEL.
 *
 * THREAD AFFINITY (PSYP_BACKEND_DIRECT only): ioperm()/iopl() grant I/O
 * permission to the CALLING THREAD, not to the process. The handle is usable
 * only from this thread and from the async worker started here, which grants
 * itself permission on its own thread while this call waits for it; if that
 * is refused, THIS CALL FAILS and the port is closed again. A write from any
 * other thread raises SIGSEGV rather than returning an error. Bindings called
 * from arbitrary host threads (Python, MEX) should use PSYP_BACKEND_PPDEV,
 * whose descriptor is process-wide. See BACKENDS. */
PSYP_API bool psyp_open(psyp_port* p, const psyp_desc* desc);

/* Release and close the port. Safe to call on a zeroed or already-closed
 * handle.
 *
 * PSYP_BACKEND_DIRECT: this drops the calling thread's I/O permission, which
 * is per-thread and not per-handle, so closing one DIRECT handle can leave
 * another open one on the same thread unable to write. See BACKENDS. */
PSYP_API void psyp_close(psyp_port* p);

/* Last error message for this handle ("" if none). */
PSYP_API const char* psyp_error(const psyp_port* p);

/* True if the handle currently owns an open port. */
PSYP_API bool psyp_is_open(const psyp_port* p);

/* --- data register (base+0) -------------------------------------------- */

/* Write the 8 data bits. This is the trigger-output workhorse.
 *
 * PSYP_BACKEND_DIRECT: call it from the thread that called psyp_open(). I/O
 * permission on Linux is per-thread, so outb() from another thread raises
 * SIGSEGV instead of failing; this function cannot detect that for you. The
 * ppdev and inpout backends have no such restriction. See BACKENDS. */
PSYP_API bool psyp_write_data(psyp_port* p, uint8_t value);

/* Read the data register back. On a forward (output) port this returns the
 * last value latched; on a bidirectional port in input mode it reads the
 * external lines. */
PSYP_API uint8_t psyp_read_data(psyp_port* p);

/* Set data-line direction on a bidirectional port: input=true switches the
 * data pins to read mode, input=false (default) drives them as outputs. */
PSYP_API bool psyp_set_data_dir(psyp_port* p, bool input);

/* Write `value` to the data lines, hold for `usec` microseconds, then write
 * 0. The classic fixed-width trigger pulse. It blocks the caller for the
 * width, waiting with psyrt_sleep_until() and PSYRT_DEFAULT_SPIN_NS on the
 * CALLER's thread at the caller's priority, so its trailing edge is less
 * precise than psyp_pulse_async's. Timing is best-effort and bounded by OS
 * scheduling; expect microsecond-to-millisecond jitter on a non-RT kernel,
 * and measure with examples/rt_jitter.c. */
PSYP_API bool psyp_pulse(psyp_port* p, uint8_t value, uint32_t usec);

/* Non-blocking pulse. Writes `value` to the data lines immediately on the
 * calling thread (so the trigger ONSET is not delayed by thread wake-up),
 * then returns at once; a dedicated real-time worker thread writes the
 * trailing 0 after `usec` microseconds.
 *
 * The worker is a psy_rt.h deadline worker, started by psyp_open() (if that
 * failed, the first call here retries and reports; on PSYP_BACKEND_DIRECT
 * there is nothing to retry, because a worker that failed to start would
 * have failed psyp_open() too). It elevates itself for
 * low timing jitter: SCHED_DEADLINE (falling back to SCHED_FIFO, then normal)
 * on Linux, THREAD_PRIORITY_TIME_CRITICAL on Windows, and waits with
 * psyrt_sleep_until(). Acquiring a real-time policy needs privileges
 * (CAP_SYS_NICE / running elevated); psyp_pulse_async still works without
 * them, just with more jitter. p->async_policy reports the rung obtained.
 *
 * If a new async pulse is issued while one is still pending, the most
 * recently requested off-time governs the single trailing edge (the data
 * register is shared, so overlapping triggers cannot carry distinct values
 * simultaneously); the worker re-evaluates immediately, whether the new
 * deadline falls earlier or later than the old one. A psyp_write_data() or
 * psyp_pulse() while a pulse is pending cancels its trailing edge.
 *
 * A trailing edge that fails to reach the hardware cannot be reported here:
 * it happens after this call returned, on a thread that must not touch the
 * error buffer. It sets psyp_port.async_write_failed instead. On
 * PSYP_BACKEND_DIRECT that includes the worker thread being refused its own
 * I/O permission, which is per-thread and can only be asked for there.
 *
 * Returns false if the worker could not be started (e.g. built with
 * PSYP_NO_THREADS) or the port is not open; see psyp_error(). Compile with
 * -DPSYP_NO_THREADS to drop threading support and this function entirely. */
PSYP_API bool psyp_pulse_async(psyp_port* p, uint8_t value, uint32_t usec);

/* --- status register (base+1, read-only) ------------------------------- */

/* Read the raw status byte. Combine with PSYP_STATUS_* masks. */
PSYP_API uint8_t psyp_read_status(psyp_port* p);

/* Convenience: test a single status line (raw register bit). */
PSYP_API bool psyp_get_status_bit(psyp_port* p, uint8_t mask);

/* --- control register (base+2) ----------------------------------------- */

/* Read / write the raw control byte. Combine with PSYP_CONTROL_* masks. */
PSYP_API uint8_t psyp_read_control(psyp_port* p);
PSYP_API bool    psyp_write_control(psyp_port* p, uint8_t value);

/* Set or clear individual control bits without disturbing the others. */
PSYP_API bool psyp_set_control_bit(psyp_port* p, uint8_t mask, bool on);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PSY_PARALLEL_H_INCLUDED */

/* ======================================================================= *
 *                            IMPLEMENTATION                               *
 * ======================================================================= */
#ifdef PSY_PARALLEL_IMPLEMENTATION
#ifndef PSY_PARALLEL_IMPLEMENTATION_GUARD
#define PSY_PARALLEL_IMPLEMENTATION_GUARD

/* psy_rt.h's implementation block sits outside its include guard and carries
 * a guard of its own, so asking for it here is safe however this translation
 * unit arrived: a file that defines PSY_RT_IMPLEMENTATION itself, or that also
 * implements psy_serial.h, still ends up with exactly one copy. */
#ifndef PSY_RT_IMPLEMENTATION_GUARD
    #define PSY_RT_IMPLEMENTATION
    #include "psy_rt.h"
#endif

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

/* Pick the concrete platform once. */
#if defined(_WIN32)
    #define PSYP__WINDOWS 1
#elif defined(__linux__)
    #define PSYP__LINUX 1
    /* Raw port I/O (outb/inb) only exists on x86. */
    #if defined(__i386__) || defined(__x86_64__)
        #define PSYP__HAVE_IOPORT 1
    #endif
#else
    #error "psy_parallel: unsupported platform (need Windows or Linux)"
#endif

/* --- shared helpers ----------------------------------------------------- */

static void psyp__set_error(psyp_port* p, const char* fmt, ...) {
    if (!p) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(p->error, sizeof(p->error), fmt, ap);
    va_end(ap);
}

static void psyp__clear_error(psyp_port* p) {
    if (p) p->error[0] = '\0';
}

const char* psyp_error(const psyp_port* p) {
    return p ? p->error : "null port handle";
}

bool psyp_is_open(const psyp_port* p) {
    return p && p->is_open;
}

/* Resolve desc.sched into p->sched. All-zero means the library defaults, a
 * zero period means "same as the deadline", and anything else must satisfy
 * 0 < runtime <= deadline <= period; psyrt_sched_normalize() is the one place
 * that rule lives. Checking at open time is the point: a half-filled struct
 * the kernel would reject with EINVAL fails loudly here instead of silently
 * degrading to the next rung down. */
static bool psyp__set_sched(psyp_port* p, const psyp_desc* d) {
    p->sched = d->sched;
    if (!psyrt_sched_normalize(&p->sched)) {
        psyp__set_error(p, "invalid desc.sched: require 0 < runtime_ns <= "
                           "deadline_ns <= period_ns (period_ns 0 means "
                           "same as deadline_ns)");
        return false;
    }
    return true;
}

/* Per-platform data write that sets p->error but takes no data lock. The
 * public psyp_write_data() wraps it with the lock and pending-pulse cancel. */
static bool psyp__write_data_nolock(psyp_port* p, uint8_t value);

#ifndef PSYP_NO_THREADS
/* defined in the async-pulse section below */
static void psyp__async_stop(psyp_port* p);
static bool psyp__async_start(psyp_port* p);
static bool psyp__write_data_cancel(psyp_port* p, uint8_t value);
#endif

/* Shared tail of a successful psyp_open(): start the async worker now, so
 * its scheduling policy is known before the first trial and no pulse pays
 * for thread creation. A start failure is not fatal here; psyp_pulse_async()
 * retries and reports it. */
static bool psyp__open_done(psyp_port* p) {
    p->is_open = true;
#ifndef PSYP_NO_THREADS
    if (!psyp__async_start(p)) {
#if defined(PSYP__HAVE_IOPORT)
        /* On PSYP_BACKEND_DIRECT the worker's own per-thread ioperm()/iopl()
         * grant is part of what just failed, and no later call can repair it:
         * a port whose async trailing edges cannot write is not an open port.
         * psyp_close() leaves p->error alone, so the reason survives. */
        if (p->direct) { psyp_close(p); return false; }
#endif
        /* Every other backend writes through a process-wide descriptor or
         * DLL, so the blocking calls work and psyp_pulse_async() retries the
         * start; a failure here is not the caller's problem yet. */
        psyp__clear_error(p);
    }
#endif
    return true;
}

/* ======================================================================= *
 *  LINUX
 * ======================================================================= */
#if defined(PSYP__LINUX)

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/ppdev.h>
#include <linux/parport.h>
#if defined(PSYP__HAVE_IOPORT)
    #include <sys/io.h>
#endif

#if defined(PSYP__HAVE_IOPORT)
/* Grant the CALLING thread permission for the port's 3 registers. Needs
 * root/CAP_SYS_RAWIO either way. ioperm() covers only ports below 0x400,
 * which is where legacy ISA LPTs live; PCI/PCIe cards sit above that and need
 * iopl(3), which opens every port.
 *
 * Both are per-thread, which is why this is a helper: the async worker calls
 * it for itself before its first write, so a worker started from a thread
 * other than the one that opened the port still has permission. Re-granting
 * on a thread that already has it is a no-op. */
static int psyp__io_grant(uint16_t base) {
    if ((unsigned)base + 3u <= 0x400u) return ioperm(base, 3, 1);
    return iopl(3);
}
#endif

bool psyp_open(psyp_port* p, const psyp_desc* desc) {
    if (!p) return false;
    psyp_desc d;
    memset(&d, 0, sizeof(d));
    if (desc) d = *desc;
    memset(p, 0, sizeof(*p));
    p->fd = -1;

    p->backend = d.backend;
    if (p->backend == PSYP_BACKEND_DEFAULT) p->backend = PSYP_BACKEND_PPDEV;
    if (p->backend == PSYP_BACKEND_INPOUT) {
        psyp__set_error(p, "inpout backend is Windows-only");
        return false;
    }
    p->base_addr = d.base_addr ? d.base_addr : PSYP_LPT1;

    /* A *valid* reservation the kernel merely cannot admit still falls back
     * to FIFO; only a malformed one fails the open. */
    if (!psyp__set_sched(p, &d)) return false;

    if (p->backend == PSYP_BACKEND_PPDEV) {
        const char* path = d.device ? d.device : "/dev/parport0";
        /* O_CLOEXEC: a claimed port must not leak into subprocesses
         * (Python subprocess, MATLAB system()), which would keep it claimed
         * after this handle closes. */
        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            psyp__set_error(p, "open(%s): %s", path, strerror(errno));
            return false;
        }
        /* Claim the port from the kernel before any read/write. PPEXCL must
         * precede PPCLAIM; the caller asked for exclusivity, so its failure
         * is the caller's failure. */
        if (d.exclusive && ioctl(fd, PPEXCL) != 0) {
            psyp__set_error(p, "PPEXCL(%s): %s", path, strerror(errno));
            close(fd);
            return false;
        }
        /* PPCLAIM blocks while another client holds the port; it only fails
         * when the device has no parport behind it. */
        if (ioctl(fd, PPCLAIM) != 0) {
            psyp__set_error(p, "PPCLAIM(%s): %s", path, strerror(errno));
            close(fd);
            return false;
        }
        p->fd = fd;
        p->claimed = true;
        p->direct = false;
        /* A previous client may have left a bidirectional port in reverse
         * mode, where the data pins are tri-stated and every trigger byte
         * would float instead of driving the recorder. */
        int dir = 0; /* PPDATADIR: 0 = forward/out */
        if (ioctl(fd, PPDATADIR, &dir) != 0) {
            psyp__set_error(p, "PPDATADIR(%s): %s", path, strerror(errno));
            ioctl(fd, PPRELEASE);
            close(fd);
            p->fd = -1;
            p->claimed = false;
            return false;
        }
        return psyp__open_done(p);
    }

    if (p->backend == PSYP_BACKEND_DIRECT) {
#if defined(PSYP__HAVE_IOPORT)
        if (psyp__io_grant(p->base_addr) != 0) {
            psyp__set_error(p, "I/O permission for base 0x%X: %s "
                               "(need root/CAP_SYS_RAWIO)",
                            p->base_addr, strerror(errno));
            return false;
        }
        p->direct = true;
        /* Force forward mode; see the ppdev branch. */
        {
            uint8_t c = inb((uint16_t)(p->base_addr + 2));
            outb((uint8_t)(c & (uint8_t)~PSYP_CONTROL_DIR),
                 (uint16_t)(p->base_addr + 2));
        }
        return psyp__open_done(p);
#else
        psyp__set_error(p, "direct port I/O requires x86/x86_64");
        return false;
#endif
    }

    psyp__set_error(p, "unknown backend %d", (int)p->backend);
    return false;
}

void psyp_close(psyp_port* p) {
    if (!p || !p->is_open) return;
#ifndef PSYP_NO_THREADS
    psyp__async_stop(p);
#endif
    if (p->direct) {
#if defined(PSYP__HAVE_IOPORT)
        /* Per-thread, not per-handle: this drops permission for every DIRECT
         * handle on this thread, and iopl(0) drops all ports, not just ours.
         * Documented in BACKENDS rather than reference-counted, because the
         * ioperm() ranges of two handles can overlap anyway. */
        if ((unsigned)p->base_addr + 3u <= 0x400u) ioperm(p->base_addr, 3, 0);
        else iopl(0);
#endif
    } else if (p->fd >= 0) {
        if (p->claimed) ioctl(p->fd, PPRELEASE);
        close(p->fd);
    }
    p->fd = -1;
    p->claimed = false;
    p->is_open = false;
}

static bool psyp__write_data_nolock(psyp_port* p, uint8_t value) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return false; }
    psyp__clear_error(p);
    if (p->direct) {
#if defined(PSYP__HAVE_IOPORT)
        outb(value, p->base_addr);
        return true;
#endif
    }
    if (ioctl(p->fd, PPWDATA, &value) != 0) {
        psyp__set_error(p, "PPWDATA: %s", strerror(errno));
        return false;
    }
    return true;
}

#ifndef PSYP_NO_THREADS
/* Hardware data write that does NOT touch the shared p->error buffer, so the
 * async worker thread can drop the trailing edge without racing main-thread
 * API calls (which also read/write p->error). */
static bool psyp__write_data_raw(psyp_port* p, uint8_t value) {
    if (p->direct) {
#if defined(PSYP__HAVE_IOPORT)
        outb(value, p->base_addr);
        return true;
#endif
    }
    return ioctl(p->fd, PPWDATA, &value) == 0;
}
#endif

uint8_t psyp_read_data(psyp_port* p) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return 0; }
    psyp__clear_error(p);
    if (p->direct) {
#if defined(PSYP__HAVE_IOPORT)
        return inb(p->base_addr);
#endif
    }
    unsigned char v = 0;
    if (ioctl(p->fd, PPRDATA, &v) != 0)
        psyp__set_error(p, "PPRDATA: %s", strerror(errno));
    return (uint8_t)v;
}

bool psyp_set_data_dir(psyp_port* p, bool input) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return false; }
    psyp__clear_error(p);
    if (p->direct) {
#if defined(PSYP__HAVE_IOPORT)
        uint8_t c = inb((uint16_t)(p->base_addr + 2));
        c = input ? (c | PSYP_CONTROL_DIR) : (c & (uint8_t)~PSYP_CONTROL_DIR);
        outb(c, (uint16_t)(p->base_addr + 2));
        return true;
#endif
    }
    int dir = input ? 1 : 0; /* PPDATADIR: 0 = forward/out, 1 = reverse/in */
    if (ioctl(p->fd, PPDATADIR, &dir) != 0) {
        psyp__set_error(p, "PPDATADIR: %s", strerror(errno));
        return false;
    }
    return true;
}

uint8_t psyp_read_status(psyp_port* p) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return 0; }
    psyp__clear_error(p);
    if (p->direct) {
#if defined(PSYP__HAVE_IOPORT)
        return inb((uint16_t)(p->base_addr + 1));
#endif
    }
    unsigned char v = 0;
    if (ioctl(p->fd, PPRSTATUS, &v) != 0)
        psyp__set_error(p, "PPRSTATUS: %s", strerror(errno));
    return (uint8_t)v;
}

uint8_t psyp_read_control(psyp_port* p) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return 0; }
    psyp__clear_error(p);
    if (p->direct) {
#if defined(PSYP__HAVE_IOPORT)
        return inb((uint16_t)(p->base_addr + 2));
#endif
    }
    unsigned char v = 0;
    if (ioctl(p->fd, PPRCONTROL, &v) != 0)
        psyp__set_error(p, "PPRCONTROL: %s", strerror(errno));
    return (uint8_t)v;
}

bool psyp_write_control(psyp_port* p, uint8_t value) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return false; }
    psyp__clear_error(p);
    if (p->direct) {
#if defined(PSYP__HAVE_IOPORT)
        outb(value, (uint16_t)(p->base_addr + 2));
        return true;
#endif
    }
    if (ioctl(p->fd, PPWCONTROL, &value) != 0) {
        psyp__set_error(p, "PPWCONTROL: %s", strerror(errno));
        return false;
    }
    return true;
}

int psyp_list_ports(psyp_port_info* out, int max) {
    int count = 0;
    for (int i = 0; i < 16; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/parport%d", i);
        /* R_OK|W_OK: list what this user can open, not what merely exists. */
        if (access(path, R_OK | W_OK) != 0) continue;
        if (out && count < max) {
            psyp_port_info* e = &out[count];
            memset(e, 0, sizeof(*e));
            snprintf(e->name, sizeof(e->name), "%s", path);
            e->backend = PSYP_BACKEND_PPDEV;
            e->base_addr = 0;
        }
        count++;
    }
    return count;
}

#endif /* PSYP__LINUX */

/* ======================================================================= *
 *  WINDOWS  (inpout32 / inpoutx64)
 * ======================================================================= */
#if defined(PSYP__WINDOWS)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef void  (__stdcall *psyp__out32_t)(short, short);
typedef short (__stdcall *psyp__inp32_t)(short);

bool psyp_open(psyp_port* p, const psyp_desc* desc) {
    if (!p) return false;
    psyp_desc d;
    memset(&d, 0, sizeof(d));
    if (desc) d = *desc;
    memset(p, 0, sizeof(*p));

    p->backend = d.backend;
    if (p->backend == PSYP_BACKEND_DEFAULT) p->backend = PSYP_BACKEND_INPOUT;
    if (p->backend != PSYP_BACKEND_INPOUT) {
        psyp__set_error(p, "only the inpout backend is available on Windows");
        return false;
    }
    p->base_addr = d.base_addr ? d.base_addr : PSYP_LPT1;
    /* Windows grants no reservation, but a malformed one is still a caller
     * error and fails here, so the same desc is accepted or rejected on both
     * platforms rather than only on Linux. */
    if (!psyp__set_sched(p, &d)) return false;

    /* LOAD_LIBRARY_SEARCH_DEFAULT_DIRS: the EXECUTABLE's directory, System32
     * and AddDllDirectory() entries, but never the current directory, so a
     * DLL dropped into a data folder cannot be picked up as a ring-0 port
     * driver. The executable is python.exe or MATLAB under the bindings, so
     * desc.dll_path exists for callers that ship the DLL elsewhere; its
     * dependencies then resolve from its own folder too. */
    const DWORD search = d.dll_path
        ? (LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR)
        : LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;
    const char* name = d.dll_path;
    HMODULE dll = NULL;
    DWORD err = 0;
    if (name) {
        dll = LoadLibraryExA(name, NULL, search);
        if (!dll) err = GetLastError();
    } else {
#if defined(_WIN64)
        /* Report the 64-bit DLL's error, not the 32-bit fallback's: in a
         * 64-bit process inpout32.dll almost always fails with
         * ERROR_BAD_EXE_FORMAT, which says nothing about why the right DLL
         * was missing. The fallback is kept because some InpOut installs ship
         * a 64-bit binary under the old name. */
        name = "inpoutx64.dll";
        dll = LoadLibraryExA(name, NULL, search);
        if (!dll) {
            err = GetLastError();
            dll = LoadLibraryExA("inpout32.dll", NULL, search);
        }
#else
        name = "inpout32.dll";
        dll = LoadLibraryExA(name, NULL, search);
        if (!dll) err = GetLastError();
#endif
    }
    if (!dll) {
        psyp__set_error(p, "LoadLibrary %s failed (err %lu); install InpOut "
                           "from highrez.co.uk and put the DLL next to the "
                           "executable, in System32, or name it in "
                           "desc.dll_path",
                        name, (unsigned long)err);
        return false;
    }

    /* FARPROC to the real signature by way of void (*)(void), the one
     * function type GCC exempts from -Wcast-function-type; a void* detour is
     * a pedantic error on MinGW. */
    psyp__out32_t out_fn = (psyp__out32_t)(void (*)(void))GetProcAddress(dll, "Out32");
    psyp__inp32_t inp_fn = (psyp__inp32_t)(void (*)(void))GetProcAddress(dll, "Inp32");
    if (!out_fn || !inp_fn) {
        psyp__set_error(p, "inpout DLL missing Out32/Inp32 entry points");
        FreeLibrary(dll);
        return false;
    }

    p->dll = (void*)dll;
    p->out_fn = (void (*)(void))out_fn;
    p->inp_fn = (void (*)(void))inp_fn;
    /* A previous client may have left a bidirectional port in reverse mode,
     * where the data pins are tri-stated and every trigger byte would float
     * instead of driving the recorder. */
    {
        short ctrl = (short)(uint16_t)(p->base_addr + 2);
        uint8_t c = (uint8_t)(inp_fn(ctrl) & 0xFF);
        out_fn(ctrl, (short)(uint8_t)(c & (uint8_t)~PSYP_CONTROL_DIR));
    }
    return psyp__open_done(p);
}

void psyp_close(psyp_port* p) {
    if (!p || !p->is_open) return;
#ifndef PSYP_NO_THREADS
    psyp__async_stop(p);
#endif
    if (p->dll) FreeLibrary((HMODULE)p->dll);
    p->dll = NULL;
    p->out_fn = NULL;
    p->inp_fn = NULL;
    p->is_open = false;
}

static void psyp__out(psyp_port* p, uint16_t addr, uint8_t val) {
    ((psyp__out32_t)p->out_fn)((short)addr, (short)val);
}
static uint8_t psyp__in(psyp_port* p, uint16_t addr) {
    return (uint8_t)(((psyp__inp32_t)p->inp_fn)((short)addr) & 0xFF);
}

static bool psyp__write_data_nolock(psyp_port* p, uint8_t value) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return false; }
    psyp__clear_error(p);
    psyp__out(p, p->base_addr, value);
    return true;
}

#ifndef PSYP_NO_THREADS
/* Hardware data write that does NOT touch the shared p->error buffer (see the
 * Linux note); used by the async worker for the trailing edge. */
static bool psyp__write_data_raw(psyp_port* p, uint8_t value) {
    psyp__out(p, p->base_addr, value);
    return true;
}
#endif

uint8_t psyp_read_data(psyp_port* p) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return 0; }
    psyp__clear_error(p);
    return psyp__in(p, p->base_addr);
}

bool psyp_set_data_dir(psyp_port* p, bool input) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return false; }
    psyp__clear_error(p);
    uint8_t c = psyp__in(p, (uint16_t)(p->base_addr + 2));
    c = input ? (c | PSYP_CONTROL_DIR) : (c & (uint8_t)~PSYP_CONTROL_DIR);
    psyp__out(p, (uint16_t)(p->base_addr + 2), c);
    return true;
}

uint8_t psyp_read_status(psyp_port* p) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return 0; }
    psyp__clear_error(p);
    return psyp__in(p, (uint16_t)(p->base_addr + 1));
}

uint8_t psyp_read_control(psyp_port* p) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return 0; }
    psyp__clear_error(p);
    return psyp__in(p, (uint16_t)(p->base_addr + 2));
}

bool psyp_write_control(psyp_port* p, uint8_t value) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return false; }
    psyp__clear_error(p);
    psyp__out(p, (uint16_t)(p->base_addr + 2), value);
    return true;
}

int psyp_list_ports(psyp_port_info* out, int max) {
    int count = 0;
    /* Query LPT1..LPT9 by name. QueryDosDevice(NULL, ...) returns the whole
     * device namespace (tens of KB on a current system) and fails outright
     * with ERROR_INSUFFICIENT_BUFFER on any fixed buffer, which used to make
     * this function report zero ports on every machine. */
    for (int i = 1; i <= 9; i++) {
        char name[8], target[256];
        snprintf(name, sizeof(name), "LPT%d", i);
        if (QueryDosDeviceA(name, target, (DWORD)sizeof(target)) == 0) continue;
        if (out && count < max) {
            psyp_port_info* e = &out[count];
            memset(e, 0, sizeof(*e));
            snprintf(e->name, sizeof(e->name), "%s", name);
            e->backend = PSYP_BACKEND_INPOUT;
            e->base_addr = 0; /* unknown; supply via psyp_desc.base_addr */
        }
        count++;
    }
    return count;
}

#endif /* PSYP__WINDOWS */

/* ======================================================================= *
 *  ASYNC PULSE
 *
 *  The trailing edge of a non-blocking pulse is one job on one psy_rt.h
 *  deadline worker per port. The leading edge is written by the caller, in
 *  psyp_pulse_async, so the onset never waits for a thread to wake; the
 *  worker only waits out an absolute deadline and then writes 0. There is
 *  one pending off-deadline per port, so a pulse issued while one is in
 *  flight moves that single trailing edge rather than queueing a second.
 *
 *  psy_rt.h runs the job with ITS lock released, and cannot cancel a job
 *  that has already entered its final spin window. So this header keeps a
 *  lock of its own over the data register, plus a token: every data write
 *  bumps the token, and the trailing edge writes only while the token it was
 *  submitted with is still the current one. That is what keeps "a plain
 *  write cancels a pending trailing edge" a guarantee instead of a race, and
 *  it is what serializes the two writers on one register.
 *
 *  The lock order is one-way and therefore deadlock-free: a caller takes
 *  this lock and then calls into psy_rt.h (submit or cancel, which take
 *  psy_rt.h's own), while the job takes this one with psy_rt.h's already
 *  released.
 * ======================================================================= */
#ifndef PSYP_NO_THREADS

#if defined(PSYP__LINUX)
#include <pthread.h>
typedef pthread_mutex_t psyp__lock;
#else
typedef CRITICAL_SECTION psyp__lock;
#endif

typedef struct psyp__async {
    psyrt_worker w;
    psyp__lock   lock;
    psyp_port*   port;
    /* Bumped by every data write and by every submit. job_token is the value
     * the pending trailing edge carries; unequal means a later write has
     * claimed the lines and the trailing 0 must not undo it. */
    uint32_t     token;
    uint32_t     job_token;
} psyp__async;

#if defined(PSYP__LINUX)

static bool psyp__lock_init(psyp__lock* m) {
    /* Priority inheritance: the submitter (normal priority) holds this lock
     * across the onset ioctl, and without PI the real-time worker would wait
     * behind whatever preempted that submitter. */
    pthread_mutexattr_t ma;
    pthread_mutexattr_init(&ma);
    pthread_mutexattr_setprotocol(&ma, PTHREAD_PRIO_INHERIT);
    int rc = pthread_mutex_init(m, &ma);
    pthread_mutexattr_destroy(&ma);
    return rc == 0;
}
static void psyp__lock_destroy(psyp__lock* m) { pthread_mutex_destroy(m); }
static void psyp__lock_acquire(psyp__lock* m) { pthread_mutex_lock(m); }
static void psyp__lock_release(psyp__lock* m) { pthread_mutex_unlock(m); }

#else

static bool psyp__lock_init(psyp__lock* m)    { InitializeCriticalSection(m); return true; }
static void psyp__lock_destroy(psyp__lock* m) { DeleteCriticalSection(m); }
static void psyp__lock_acquire(psyp__lock* m) { EnterCriticalSection(m); }
static void psyp__lock_release(psyp__lock* m) { LeaveCriticalSection(m); }

#endif

/* Give the worker thread port I/O permission. ioperm() and iopl() are
 * per-THREAD on Linux, so the thread that will run the trailing edges has to
 * ask for itself, and a missing grant is a SIGSEGV on the outb rather than an
 * error code. psyrt_worker_desc.on_start runs on that thread before
 * psyrt_worker_start() returns, which is what lets a refusal fail psyp_open()
 * instead of one trigger's offset. Only PSYP_BACKEND_DIRECT needs it: ppdev
 * has a process-wide descriptor and inpout a process-wide DLL. */
static bool psyp__async_on_start(void* ctx, char* err, size_t cap) {
#if defined(PSYP__HAVE_IOPORT)
    psyp_port* p = (psyp_port*)ctx;
    if (!p->direct) return true;
    if (psyp__io_grant(p->base_addr) != 0) {
        snprintf(err, cap, "worker thread I/O permission for base 0x%X: %s "
                           "(need root/CAP_SYS_RAWIO)",
                 (unsigned)p->base_addr, strerror(errno));
        return false;
    }
    return true;
#else
    (void)ctx; (void)err; (void)cap;
    return true;
#endif
}

/* The trailing edge. Runs on the psy_rt.h worker thread, at the worker's
 * policy, with psy_rt.h's lock released. p->error belongs to the calling
 * thread, so a failure is reported through the sticky flag instead. */
static void psyp__async_job(void* ctx, const psyrt_job_info* info) {
    psyp__async* a = (psyp__async*)ctx;
    /* info->flushed is deliberately ignored: a psyp_close() with a pulse in
     * flight still has to drop the lines, just earlier than asked. The job's
     * info->seq is ignored too: job_token is the identity this header acts
     * on, because a plain write must be able to retire a trailing edge
     * without going anywhere near psy_rt.h. */
    (void)info;
    psyp__lock_acquire(&a->lock);
    if (a->job_token == a->token) {
        if (!psyp__write_data_raw(a->port, 0))
            a->port->async_write_failed = 1;
    }
    psyp__lock_release(&a->lock);
}

static bool psyp__async_start(psyp_port* p) {
    if (p->async) return true;
    /* One allocation per handle, made by psyp_open(), never on a write or
     * pulse path. The psyrt_worker is caller-allocated and lives in this
     * block rather than in psyp_port so the public struct neither grows by a
     * worker handle nor changes shape under PSYRT_NO_THREADS. */
    psyp__async* a = (psyp__async*)calloc(1, sizeof(*a));
    if (!a) { psyp__set_error(p, "async: out of memory"); return false; }
    a->port = p;
    if (!psyp__lock_init(&a->lock)) {
        free(a);
        psyp__set_error(p, "async: mutex init");
        return false;
    }
    psyrt_worker_desc wd;
    memset(&wd, 0, sizeof(wd));
    wd.sched = p->sched; /* already normalized by psyp_open() */
    wd.on_start = psyp__async_on_start;
    wd.start_ctx = p;
    if (!psyrt_worker_start(&a->w, &wd)) {
        psyp__set_error(p, "async: %s", psyrt_worker_error(&a->w));
        psyp__lock_destroy(&a->lock);
        free(a);
        return false;
    }
    /* psyrt_worker_start() does not return until the thread has published
     * its rung, so async_policy is final when psyp_open() returns. */
    p->async_policy = psyrt_worker_policy(&a->w);
    p->async = a;
    return true;
}

static void psyp__async_stop(psyp_port* p) {
    psyp__async* a = (psyp__async*)p->async;
    if (!a) return;
    /* Clear p->async first: psyp_write_data() routes through the lock
     * whenever it sees a block, and a call racing this teardown (a contract
     * violation, but a cheap one to survive) must not reach for a lock that
     * is about to be destroyed. */
    p->async = NULL;
    /* Runs a pending trailing edge IMMEDIATELY through psyp__async_job
     * instead of waiting out the rest of the width, so the lines are never
     * left asserted. Called without the lock held; the job takes it. */
    psyrt_worker_stop(&a->w);
    psyp__lock_destroy(&a->lock);
    free(a);
    p->async_policy = PSYP_ASYNC_NONE;
}

static bool psyp__write_data_cancel(psyp_port* p, uint8_t value) {
    psyp__async* a = (psyp__async*)p->async;
    psyp__lock_acquire(&a->lock);
    /* A plain write means "hold these lines"; a pending trailing edge would
     * silently undo it. The token bump is the authoritative cancel, because
     * psyrt_worker_cancel() cannot reach a job already inside its final spin
     * window; the cancel call itself only saves the worker a pointless wake. */
    a->token++;
    (void)psyrt_worker_cancel(&a->w);
    bool ok = psyp__write_data_nolock(p, value);
    psyp__lock_release(&a->lock);
    return ok;
}

static bool psyp__async_submit(psyp_port* p, uint8_t value, uint32_t usec) {
    psyp__async* a = (psyp__async*)p->async;
    psyp__lock_acquire(&a->lock);
    /* Leading edge under the lock so it cannot be stomped by the trailing
     * edge of an in-flight pulse. */
    bool ok = psyp__write_data_nolock(p, value);
    /* The width is measured from the onset write, not from entry, so the
     * ioctl round trip is not subtracted from it. */
    uint64_t off_at = psyrt_now_ns() + (uint64_t)usec * 1000ull;
    a->token++;
    a->job_token = a->token;
    /* Replaces any pending job, earlier deadline or later, and the worker
     * re-evaluates at once. */
    /* A positive return is this job's seq; only a negative one is an error. */
    int rc = psyrt_worker_submit(&a->w, off_at, psyp__async_job, a);
    if (rc < 0) {
        /* Cannot happen while the worker is running, and if it somehow does,
         * the lines must not be left asserted with nothing to clear them. */
        psyp__write_data_raw(p, 0);
        psyp__lock_release(&a->lock);
        psyp__set_error(p, "async: submit: %s", psyrt_strerror(rc));
        return false;
    }
    psyp__lock_release(&a->lock);
    return ok;
}

bool psyp_pulse_async(psyp_port* p, uint8_t value, uint32_t usec) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return false; }
    if (!p->async) {
        /* psyp_open() normally started the worker; this path only runs if
         * that failed. Write the onset NOW, before paying the worker-thread
         * creation cost, so this trigger's onset is not delayed by thread
         * spin-up (its width will include it). No worker exists yet, so
         * nothing can race this write or stomp it with a trailing edge. */
        if (!psyp_write_data(p, value)) return false;
        if (!psyp__async_start(p)) {
            /* Don't leave the data lines asserted with no worker to clear
             * them; use the raw write so psyp__async_start's error survives. */
            psyp__write_data_raw(p, 0);
            return false;
        }
    }
    /* Steady state: the leading edge is (re)written inside submit on the
     * caller's thread under the data lock, so onset is not delayed by the
     * worker waking up and the write cannot race the worker's trailing edge.
     * On the first-use path above this re-asserts the same value (no edge). */
    return psyp__async_submit(p, value, usec);
}

#else /* PSYP_NO_THREADS */

bool psyp_pulse_async(psyp_port* p, uint8_t value, uint32_t usec) {
    (void)value; (void)usec;
    psyp__set_error(p, "psyp_pulse_async unavailable: built with PSYP_NO_THREADS");
    return false;
}

#endif /* PSYP_NO_THREADS */

/* ======================================================================= *
 *  PLATFORM-INDEPENDENT helpers (built on the primitives above)
 * ======================================================================= */

bool psyp_get_status_bit(psyp_port* p, uint8_t mask) {
    return (psyp_read_status(p) & mask) != 0;
}

bool psyp_set_control_bit(psyp_port* p, uint8_t mask, bool on) {
    uint8_t c = psyp_read_control(p);
    /* A failed read returns 0 with an error set; writing that back would
     * clear every control line. */
    if (!psyp_is_open(p) || p->error[0]) return false;
    c = on ? (uint8_t)(c | mask) : (uint8_t)(c & (uint8_t)~mask);
    return psyp_write_control(p, c);
}

bool psyp_write_data(psyp_port* p, uint8_t value) {
#ifndef PSYP_NO_THREADS
    if (p && p->async) return psyp__write_data_cancel(p, value);
#endif
    return psyp__write_data_nolock(p, value);
}

bool psyp_pulse(psyp_port* p, uint8_t value, uint32_t usec) {
    if (!psyp_write_data(p, value)) return false;
    /* The deadline is absolute and taken just after the onset write, so the
     * write's own round trip is not added to the width and a signal restart
     * does not stretch it. psy_rt.h hands the bulk of the wait to the OS and
     * spins the tail; this one runs on the CALLER's thread at the caller's
     * priority, which is why PULSES AND TIMING calls this edge the less
     * precise of the two. */
    if (usec)
        (void)psyrt_sleep_until(psyrt_now_ns() + (uint64_t)usec * 1000ull,
                                PSYRT_DEFAULT_SPIN_NS);
    return psyp_write_data(p, 0);
}

#endif /* PSY_PARALLEL_IMPLEMENTATION_GUARD */
#endif /* PSY_PARALLEL_IMPLEMENTATION */

/* ------------------------------------------------------------------------
 * This software is available under the MIT-0 (MIT No Attribution) license.
 *
 * Copyright (c) 2026 psy contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY.
 * ------------------------------------------------------------------------ */
