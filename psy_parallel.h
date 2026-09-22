/* psy_parallel.h - v0.1 - public domain single-header parallel-port library
 *
 *   A small, dependency-free C library for reading and writing the PC
 *   parallel port (LPT). Built for low-latency 8-bit trigger output of the
 *   kind used in psychophysics / EEG / electrophysiology rigs, where a byte
 *   written to the data lines is latched onto a recording system.
 *
 *   Inspired by:
 *     - pyparallel    (https://github.com/pyparallel/pyparallel)
 *     - ppdev-mex     (https://github.com/widmann/ppdev-mex)
 *   and written in the single-header style of the stb / sokol libraries.
 *
 *   Targets Windows and Linux.
 *
 *   ---------------------------------------------------------------------
 *   USAGE
 *   ---------------------------------------------------------------------
 *   Do this:
 *
 *       #define PSY_PARALLEL_IMPLEMENTATION
 *
 *   in *one* C or C++ file before including this header to create the
 *   implementation. Every other file just includes the header normally.
 *
 *       #define PSY_PARALLEL_IMPLEMENTATION
 *       #include "psy_parallel.h"
 *
 *       psyp_port pp;
 *       if (!psyp_open(&pp, &(psyp_desc){0})) {       // sensible defaults
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
 *                            search path; ship it next to the executable.
 *
 *     Caveat: InpOut is an unmaintained, legacy-signed kernel driver that
 *     grants ring-0 port I/O. On hardened systems (Secure Boot with Memory
 *     Integrity enabled, or the Windows vulnerable-driver blocklist) it may
 *     be refused no matter how it is installed, and there is no maintained
 *     drop-in replacement.
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
 *   ---------------------------------------------------------------------
 *   PULSES AND TIMING
 *   ---------------------------------------------------------------------
 *   psyp_pulse(p, value, usec) writes value, sleeps until an absolute
 *   deadline taken right after that write, then writes 0. It blocks the
 *   calling thread for the whole width, and that thread is whatever the
 *   caller has: on Linux a normal thread carries 50 us of timer slack plus
 *   wake latency; on Windows the wait is a waitable timer for the bulk and
 *   a QPC spin for the last ~1 ms, preemptible by anything. Its trailing
 *   edge is therefore LESS precise than psyp_pulse_async's, not more.
 *
 *   psyp_pulse_async(p, value, usec) returns at once. The leading edge is
 *   written on the calling thread, so the trigger ONSET is never delayed by
 *   thread scheduling. A dedicated worker thread writes the trailing 0 at
 *   an absolute deadline taken right after the onset write. The worker is
 *   started by psyp_open(), so no trial pays for thread creation and the
 *   policy it obtained is known before the first pulse:
 *
 *     Linux:   SCHED_DEADLINE via the raw sched_setattr syscall (default
 *              1 ms budget in a 10 ms period, tunable per port through
 *              psyp_desc.sched), falling back to SCHED_FIFO priority 80
 *              (above IRQ threads, below the kernel's own 99), then a normal
 *              thread with 1 us timer slack. The trailing edge is timed
 *              against an absolute CLOCK_MONOTONIC deadline with
 *              pthread_cond_timedwait, so wake latency does not accumulate.
 *              A real-time policy needs CAP_SYS_NICE (run elevated, or
 *              setcap cap_sys_nice+ep). SCHED_DEADLINE is also refused when
 *              the process is pinned to a subset of CPUs (taskset, cpusets),
 *              which is common on tuned rigs; you then get FIFO.
 *     Windows: a THREAD_PRIORITY_TIME_CRITICAL worker on a high-resolution
 *              waitable timer (Win10 1803+), plus a QPC spin for the last
 *              ~1.1 ms. If the high-resolution timer is unavailable the
 *              fallback timer ticks every 15.6 ms and the worker spins for
 *              a whole tick instead, which costs CPU but not accuracy.
 *
 *   psyp_port.async_policy says which of these the worker got. Log it.
 *
 *   Every data write is serialized under the worker's lock. A plain
 *   psyp_write_data() or psyp_pulse() issued while an async pulse is
 *   pending CANCELS that pulse's trailing edge: a write means "hold these
 *   lines", and a stale trailing 0 must not undo it. A psyp_pulse_async()
 *   issued while one is pending moves the single trailing edge to the newer
 *   off-time, earlier or later. psyp_close() stops the worker and writes any
 *   pending trailing edge immediately, so the lines are never left asserted.
 *
 *   Timing is best-effort and bounded by OS scheduling. For the tightest
 *   triggers use PSYP_BACKEND_DIRECT on a real-time-tuned kernel with an
 *   isolated CPU. Measure on your rig with a scope or a second recorder,
 *   and record async_policy with the numbers; do not assume.
 *
 *   ---------------------------------------------------------------------
 *   BUILDING
 *   ---------------------------------------------------------------------
 *   Link -pthread on Linux for the async worker; Windows needs no extra
 *   library. On glibc older than 2.17 also link -lrt for clock_gettime.
 *   Define PSYP_NO_THREADS to drop threading and psyp_pulse_async entirely.
 *   Define PSYP_API to override the default `extern` linkage.
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

/* Feature-test macro for nanosleep() in the Linux implementation. Defined
 * here, before the first system header, so it takes effect when this header
 * is the implementation translation unit's first include (the usual case).
 * If you include other system headers before this one in your implementation
 * file, define _POSIX_C_SOURCE>=199309L (or _DEFAULT_SOURCE) yourself. */
#if defined(PSY_PARALLEL_IMPLEMENTATION) && defined(__linux__) && \
    !defined(_POSIX_C_SOURCE) && !defined(_GNU_SOURCE) && !defined(_DEFAULT_SOURCE)
    #define _POSIX_C_SOURCE 200809L
#endif

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
 * (SCHED_DEADLINE). Times are in nanoseconds and must satisfy
 * runtime_ns <= deadline_ns <= period_ns. These do NOT set the pulse width
 * (that is clock-driven); they govern how quickly and predictably the worker
 * gets the CPU when it wakes to write the trailing edge -- i.e. edge jitter.
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
typedef struct psyp_sched_deadline {
    uint64_t runtime_ns;
    uint64_t deadline_ns;
    uint64_t period_ns;
} psyp_sched_deadline;

#define PSYP_DEFAULT_RT_RUNTIME_NS   1000000ull  /*  1 ms */
#define PSYP_DEFAULT_RT_DEADLINE_NS 10000000ull  /* 10 ms */
#define PSYP_DEFAULT_RT_PERIOD_NS   10000000ull  /* 10 ms */

/* Scheduling policy the async-pulse worker actually obtained. Requesting
 * SCHED_DEADLINE is not getting it: the kernel refuses it without
 * CAP_SYS_NICE, and also when the thread's CPU affinity is not the full root
 * domain (taskset, cpusets, an isolcpus-pinned experiment process), and the
 * library then falls back. Read psyp_port.async_policy after psyp_open() and
 * log it next to your timing data; jitter numbers without it are unlabeled. */
typedef enum psyp_async_policy {
    PSYP_ASYNC_NONE = 0,       /* no worker (PSYP_NO_THREADS, or start failed) */
    PSYP_ASYNC_DEADLINE,       /* Linux SCHED_DEADLINE with psyp_desc.sched      */
    PSYP_ASYNC_FIFO,           /* Linux SCHED_FIFO, priority 80                   */
    PSYP_ASYNC_TIME_CRITICAL,  /* Windows THREAD_PRIORITY_TIME_CRITICAL           */
    PSYP_ASYNC_NORMAL          /* no real-time policy granted                      */
} psyp_async_policy;

/* Open description. Zero-initialize it and only set what you need; missing
 * fields fall back to sensible defaults inside psyp_open(). */
typedef struct psyp_desc {
    psyp_backend backend;   /* default: PSYP_BACKEND_DEFAULT          */
    const char*  device;    /* ppdev path, default "/dev/parport0"    */
    uint16_t     base_addr; /* direct/inpout base, default PSYP_LPT1   */
    bool         exclusive; /* ppdev: claim the port exclusively (PPEXCL) */
    psyp_sched_deadline sched; /* async-worker RT params; all-zero = defaults */
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
    void* out_fn;  /* Out32 entry point         */
    void* inp_fn;  /* Inp32 entry point         */
    void* timer;   /* waitable timer for psyp_pulse, created on first use */
    bool  timer_hires; /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION succeeded */
#else
    int  fd;       /* ppdev file descriptor (-1 if direct/closed) */
    bool claimed;  /* ppdev port claimed                          */
    bool direct;   /* using ioperm()+outb/inb                     */
#endif
    void* async;   /* async-pulse worker, started by psyp_open(), or NULL */
    psyp_sched_deadline sched; /* effective async-worker RT params (Linux)    */
    psyp_async_policy async_policy; /* policy the worker obtained; valid once
                                     * psyp_open() returns                     */
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
 * open handle leaks its descriptor and orphans its async worker. */
PSYP_API bool psyp_open(psyp_port* p, const psyp_desc* desc);

/* Release and close the port. Safe to call on a zeroed or already-closed
 * handle. */
PSYP_API void psyp_close(psyp_port* p);

/* Last error message for this handle ("" if none). */
PSYP_API const char* psyp_error(const psyp_port* p);

/* True if the handle currently owns an open port. */
PSYP_API bool psyp_is_open(const psyp_port* p);

/* --- data register (base+0) -------------------------------------------- */

/* Write the 8 data bits. This is the trigger-output workhorse. */
PSYP_API bool psyp_write_data(psyp_port* p, uint8_t value);

/* Read the data register back. On a forward (output) port this returns the
 * last value latched; on a bidirectional port in input mode it reads the
 * external lines. */
PSYP_API uint8_t psyp_read_data(psyp_port* p);

/* Set data-line direction on a bidirectional port: input=true switches the
 * data pins to read mode, input=false (default) drives them as outputs. */
PSYP_API bool psyp_set_data_dir(psyp_port* p, bool input);

/* Write `value` to the data lines, hold for `usec` microseconds, then write
 * 0. The classic fixed-width trigger pulse. Timing is best-effort and bounded
 * by OS scheduling; expect microsecond-to-millisecond jitter on a non-RT
 * kernel. */
PSYP_API bool psyp_pulse(psyp_port* p, uint8_t value, uint32_t usec);

/* Non-blocking pulse. Writes `value` to the data lines immediately on the
 * calling thread (so the trigger ONSET is not delayed by thread wake-up),
 * then returns at once; a dedicated real-time worker thread writes the
 * trailing 0 after `usec` microseconds.
 *
 * The worker is started by psyp_open() (if that failed, the first call here
 * retries and reports). It runs at elevated scheduling priority for low
 * timing jitter: SCHED_DEADLINE (falling back to SCHED_FIFO, then normal) on
 * Linux, THREAD_PRIORITY_TIME_CRITICAL with a high-resolution waitable timer
 * on Windows. Acquiring a real-time policy needs privileges (CAP_SYS_NICE /
 * running elevated); psyp_pulse_async still works without them, just with
 * more jitter. p->async_policy reports what was obtained.
 *
 * If a new async pulse is issued while one is still pending, the most
 * recently requested off-time governs the single trailing edge (the data
 * register is shared, so overlapping triggers cannot carry distinct values
 * simultaneously); the worker re-evaluates immediately, whether the new
 * deadline falls earlier or later than the old one. A psyp_write_data() or
 * psyp_pulse() while a pulse is pending cancels its trailing edge.
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

/* Per-platform data write that sets p->error but takes no worker lock. The
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
    if (!psyp__async_start(p)) psyp__clear_error(p);
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
#include <time.h>
#include <sys/ioctl.h>
#include <linux/ppdev.h>
#include <linux/parport.h>
#if defined(PSYP__HAVE_IOPORT)
    #include <sys/io.h>
#endif

static uint64_t psyp__now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* Sleep until an absolute CLOCK_MONOTONIC time. Absolute, so a signal
 * restart does not add the elapsed time back onto the width. */
static void psyp__sleep_until(psyp_port* p, uint64_t ns) {
    (void)p;
    struct timespec ts;
    ts.tv_sec  = (time_t)(ns / 1000000000ull);
    ts.tv_nsec = (long)(ns % 1000000000ull);
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) { }
}

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

    /* Resolve the async-worker RT reservation. All-zero means use defaults.
     * Otherwise normalize a zero period to the deadline (as the kernel does)
     * and require 0 < runtime <= deadline <= period, so a partially filled
     * struct fails loudly here instead of silently degrading to SCHED_FIFO
     * (the kernel rejects e.g. deadline_ns == 0 with EINVAL). A *valid*
     * reservation the kernel merely can't admit still falls back to FIFO. */
    p->sched = d.sched;
    if (p->sched.runtime_ns == 0 && p->sched.deadline_ns == 0 &&
        p->sched.period_ns == 0) {
        p->sched.runtime_ns  = PSYP_DEFAULT_RT_RUNTIME_NS;
        p->sched.deadline_ns = PSYP_DEFAULT_RT_DEADLINE_NS;
        p->sched.period_ns   = PSYP_DEFAULT_RT_PERIOD_NS;
    } else {
        if (p->sched.period_ns == 0) p->sched.period_ns = p->sched.deadline_ns;
        if (p->sched.runtime_ns == 0 || p->sched.deadline_ns == 0 ||
            p->sched.runtime_ns > p->sched.deadline_ns ||
            p->sched.deadline_ns > p->sched.period_ns) {
            psyp__set_error(p, "invalid desc.sched: require 0 < runtime_ns <= "
                               "deadline_ns <= period_ns (period_ns 0 means "
                               "same as deadline_ns)");
            return false;
        }
    }

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
        return psyp__open_done(p);
    }

    if (p->backend == PSYP_BACKEND_DIRECT) {
#if defined(PSYP__HAVE_IOPORT)
        /* Needs root/CAP_SYS_RAWIO either way. ioperm() covers only ports
         * below 0x400, which is where legacy ISA LPTs live; PCI/PCIe cards
         * sit above that and need iopl(3), which opens every port. */
        if ((unsigned)p->base_addr + 3u <= 0x400u) {
            if (ioperm(p->base_addr, 3, 1) != 0) {
                psyp__set_error(p, "ioperm(0x%X): %s (need root/CAP_SYS_RAWIO)",
                                p->base_addr, strerror(errno));
                return false;
            }
        } else {
            if (iopl(3) != 0) {
                psyp__set_error(p, "iopl(3) for base 0x%X: %s (need root/CAP_SYS_RAWIO)",
                                p->base_addr, strerror(errno));
                return false;
            }
        }
        p->direct = true;
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

/* Monotonic nanoseconds from QPC. Integer arithmetic split so a TSC-rate
 * counter (GHz) cannot overflow the multiply. */
static uint64_t psyp__now_ns(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    uint64_t t = (uint64_t)c.QuadPart, fr = (uint64_t)f.QuadPart;
    return (t / fr) * 1000000000ull + (t % fr) * 1000000000ull / fr;
}

/* Timer slack in ns: how late the waitable timer may fire. The high-res
 * timer is good to ~0.5 ms; the fallback ticks at the 15.6 ms system clock. */
#define PSYP__TIMER_SLACK_HIRES_NS  1140000ull
#define PSYP__TIMER_SLACK_TICK_NS  16000000ull

/* Wait until an absolute QPC time: waitable timer for the bulk, QPC spin for
 * the tail, so a 2 ms pulse spins ~1 ms instead of 2 and a 500 ms pulse does
 * not burn a core. Same hybrid as the async worker, on the caller's thread. */
static void psyp__sleep_until(psyp_port* p, uint64_t ns) {
    if (!p->timer) {
        /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION needs Win10 1803+; fall back. */
        p->timer = (void*)CreateWaitableTimerExW(NULL, NULL,
                       0x00000002 /*HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
        p->timer_hires = (p->timer != NULL);
        if (!p->timer) p->timer = (void*)CreateWaitableTimerW(NULL, FALSE, NULL);
    }
    uint64_t slack = p->timer_hires ? PSYP__TIMER_SLACK_HIRES_NS : PSYP__TIMER_SLACK_TICK_NS;
    uint64_t now = psyp__now_ns();
    if (p->timer && ns > now + slack) {
        LARGE_INTEGER due; /* negative = relative, 100 ns units */
        due.QuadPart = -(LONGLONG)((ns - now - slack) / 100ull);
        if (SetWaitableTimer((HANDLE)p->timer, &due, 0, NULL, NULL, FALSE))
            WaitForSingleObject((HANDLE)p->timer, INFINITE);
    }
    while (psyp__now_ns() < ns) YieldProcessor(); /* pause: spares the sibling hyperthread */
}

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

    /* Prefer the 64-bit DLL when running as a 64-bit process. The caller can
     * place either DLL alongside the executable or in the search path. */
    /* LOAD_LIBRARY_SEARCH_DEFAULT_DIRS: application directory, System32 and
     * AddDllDirectory() entries, but not the current directory, so a DLL
     * dropped into a data folder cannot be picked up as a ring-0 port driver. */
    const DWORD search = LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;
    HMODULE dll = NULL;
#if defined(_WIN64)
    dll = LoadLibraryExA("inpoutx64.dll", NULL, search);
    if (!dll) dll = LoadLibraryExA("inpout32.dll", NULL, search);
#else
    dll = LoadLibraryExA("inpout32.dll", NULL, search);
#endif
    if (!dll) {
        psyp__set_error(p, "LoadLibrary inpout DLL failed (err %lu); install "
                           "InpOut from highrez.co.uk and ship the DLL",
                        (unsigned long)GetLastError());
        return false;
    }

    psyp__out32_t out_fn = (psyp__out32_t)(void*)GetProcAddress(dll, "Out32");
    psyp__inp32_t inp_fn = (psyp__inp32_t)(void*)GetProcAddress(dll, "Inp32");
    if (!out_fn || !inp_fn) {
        psyp__set_error(p, "inpout DLL missing Out32/Inp32 entry points");
        FreeLibrary(dll);
        return false;
    }

    p->dll = (void*)dll;
    p->out_fn = (void*)out_fn;
    p->inp_fn = (void*)inp_fn;
    return psyp__open_done(p);
}

void psyp_close(psyp_port* p) {
    if (!p || !p->is_open) return;
#ifndef PSYP_NO_THREADS
    psyp__async_stop(p);
#endif
    if (p->dll) FreeLibrary((HMODULE)p->dll);
    if (p->timer) CloseHandle((HANDLE)p->timer);
    p->dll = NULL;
    p->out_fn = NULL;
    p->inp_fn = NULL;
    p->timer = NULL;
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
 *  ASYNC PULSE WORKER
 *
 *  A single long-lived worker thread per port handles the trailing edge of
 *  non-blocking pulses. The leading edge is written by the caller (in
 *  psyp_pulse_async) for minimal onset latency; the worker only waits until
 *  an absolute target time and then writes 0.
 *
 *  The worker keeps just one pending off-deadline. A pulse issued while one
 *  is pending updates the deadline, so the trailing 0 lands once, after the
 *  most recent request.
 * ======================================================================= */
#ifndef PSYP_NO_THREADS

#if defined(PSYP__LINUX)
/* ---- Linux: pthreads + SCHED_DEADLINE / SCHED_FIFO -------------------- */
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <sys/prctl.h>

#ifndef SCHED_DEADLINE
#define SCHED_DEADLINE 6
#endif

/* syscall()'s prototype lives behind _DEFAULT_SOURCE in <unistd.h>; declare
 * it directly so we don't force a feature-test macro onto the user's TU. */
extern long syscall(long, ...);

/* glibc has no wrapper or type for sched_setattr; declare both ourselves. */
struct psyp__sched_attr {
    uint32_t size;
    uint32_t sched_policy;
    uint64_t sched_flags;
    int32_t  sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime;
    uint64_t sched_deadline;
    uint64_t sched_period;
};

static int psyp__setattr(struct psyp__sched_attr* a) {
#if defined(SYS_sched_setattr)
    return (int)syscall(SYS_sched_setattr, 0, a, 0u);
#elif defined(__x86_64__)
    return (int)syscall(314 /* __NR_sched_setattr */, 0, a, 0u);
#else
    (void)a; errno = ENOSYS; return -1;
#endif
}

/* Raise the calling (worker) thread to a real-time policy and report which
 * one stuck. SCHED_DEADLINE first, using the reservation from psyp_desc.sched
 * (defaults: 1 ms budget in a 10 ms period/deadline) -- enough for short
 * trigger pulses while staying admissible by the kernel's EDF acceptance
 * test. Falls back to SCHED_FIFO, then to a normal thread. */
static psyp_async_policy psyp__worker_realtime(const psyp_sched_deadline* rt) {
    struct psyp__sched_attr a;
    memset(&a, 0, sizeof(a));
    a.size           = (uint32_t)sizeof(a);
    a.sched_policy   = SCHED_DEADLINE;
    a.sched_runtime  = rt->runtime_ns;
    a.sched_deadline = rt->deadline_ns;
    a.sched_period   = rt->period_ns;
    if (psyp__setattr(&a) == 0) return PSYP_ASYNC_DEADLINE;

    /* FIFO 80: above the kernel's IRQ threads (50), so a trailing edge is not
     * queued behind a busy NIC, but below migration/watchdog at 99. */
    struct sched_param sp;
    memset(&sp, 0, sizeof(sp));
    sp.sched_priority = 80;
    int max = sched_get_priority_max(SCHED_FIFO);
    if (max > 0 && sp.sched_priority > max) sp.sched_priority = max;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) == 0)
        return PSYP_ASYNC_FIFO;

    /* No privilege for either policy: normal thread, but without the default
     * 50 us timer slack that would otherwise be added to every wake. */
    (void)prctl(PR_SET_TIMERSLACK, 1UL, 0UL, 0UL, 0UL);
    return PSYP_ASYNC_NORMAL;
}

typedef struct psyp__async {
    pthread_t       thread;
    pthread_mutex_t mtx;
    pthread_cond_t  cv;
    int             ready;   /* worker published its policy; start may return */
    int             quit;
    int             has_job;
    struct timespec off_at;  /* absolute CLOCK_MONOTONIC target */
    psyp_port*      port;
} psyp__async;

static int psyp__ts_before(const struct timespec* a, const struct timespec* b) {
    if (a->tv_sec != b->tv_sec) return a->tv_sec < b->tv_sec;
    return a->tv_nsec < b->tv_nsec;
}

static void* psyp__worker_main(void* arg) {
    psyp__async* w = (psyp__async*)arg;
    psyp_async_policy pol = psyp__worker_realtime(&w->port->sched);

    /* The mutex is held across the whole loop except while blocked in
     * pthread_cond_*wait (which atomically releases it). Holding it around
     * every data write serializes the trailing edge with the caller's
     * leading edge in psyp__async_submit, so neither can stomp the other. */
    pthread_mutex_lock(&w->mtx);
    /* Publish the obtained policy and release psyp__async_start(), which
     * waits for it so p->async_policy is final when psyp_open() returns. */
    w->port->async_policy = pol;
    w->ready = 1;
    pthread_cond_broadcast(&w->cv);
    while (!w->quit) {
        if (!w->has_job) {
            pthread_cond_wait(&w->cv, &w->mtx);
            continue;
        }
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (!psyp__ts_before(&now, &w->off_at)) {
            /* Deadline reached: drop the trailing edge. */
            psyp__write_data_raw(w->port, 0);
            w->has_job = 0;
            continue;
        }
        /* Wait until the deadline OR until a new submit / quit wakes us. The
         * cond uses CLOCK_MONOTONIC (set in psyp__async_start) to match
         * off_at. Either way we re-evaluate from the top, so a re-trigger
         * that moves the deadline earlier or later takes effect immediately. */
        pthread_cond_timedwait(&w->cv, &w->mtx, &w->off_at);
    }
    /* On quit, flush any pending trailing edge right away so the data lines
     * are never left asserted -- without waiting out the remaining width. */
    if (w->has_job) {
        psyp__write_data_raw(w->port, 0);
        w->has_job = 0;
    }
    pthread_mutex_unlock(&w->mtx);
    return NULL;
}

static bool psyp__async_start(psyp_port* p) {
    if (p->async) return true;
    psyp__async* w = (psyp__async*)calloc(1, sizeof(*w));
    if (!w) { psyp__set_error(p, "async: out of memory"); return false; }
    w->port = p;
    /* Priority inheritance: the submitter (normal priority) holds the mutex
     * across the onset ioctl; without PI the RT worker would wait behind any
     * thread that preempts the submitter. */
    pthread_mutexattr_t ma;
    pthread_mutexattr_init(&ma);
    pthread_mutexattr_setprotocol(&ma, PTHREAD_PRIO_INHERIT);
    int mtx_rc = pthread_mutex_init(&w->mtx, &ma);
    pthread_mutexattr_destroy(&ma);
    if (mtx_rc != 0) { free(w); psyp__set_error(p, "async: mutex init"); return false; }
    /* The condvar must time out against CLOCK_MONOTONIC to match off_at. */
    pthread_condattr_t ca;
    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    int cv_rc = pthread_cond_init(&w->cv, &ca);
    pthread_condattr_destroy(&ca);
    if (cv_rc != 0) { pthread_mutex_destroy(&w->mtx); free(w); psyp__set_error(p, "async: cond init"); return false; }
    /* pthread_create returns the error; it does not set errno. */
    int rc = pthread_create(&w->thread, NULL, psyp__worker_main, w);
    if (rc != 0) {
        pthread_cond_destroy(&w->cv); pthread_mutex_destroy(&w->mtx); free(w);
        psyp__set_error(p, "async: thread create: %s", strerror(rc));
        return false;
    }
    /* Wait for the worker to publish its policy (see psyp__worker_main). */
    pthread_mutex_lock(&w->mtx);
    while (!w->ready) pthread_cond_wait(&w->cv, &w->mtx);
    pthread_mutex_unlock(&w->mtx);
    p->async = w;
    return true;
}

static void psyp__async_stop(psyp_port* p) {
    psyp__async* w = (psyp__async*)p->async;
    if (!w) return;
    /* Clear p->async first: psyp_write_data() routes through the worker lock
     * whenever it sees a worker, and a call racing this teardown (a contract
     * violation, but a cheap one to survive) must not reach for a mutex that
     * is about to be destroyed. */
    p->async = NULL;
    pthread_mutex_lock(&w->mtx);
    w->quit = 1;
    pthread_cond_signal(&w->cv);
    pthread_mutex_unlock(&w->mtx);
    pthread_join(w->thread, NULL);
    pthread_cond_destroy(&w->cv);
    pthread_mutex_destroy(&w->mtx);
    free(w);
    p->async_policy = PSYP_ASYNC_NONE;
}

static bool psyp__write_data_cancel(psyp_port* p, uint8_t value) {
    psyp__async* w = (psyp__async*)p->async;
    pthread_mutex_lock(&w->mtx);
    /* A plain write means "hold these lines"; a pending trailing edge would
     * silently undo it, so drop the job. The worker finds no job when its
     * timed wait expires and goes back to sleep. */
    w->has_job = 0;
    bool ok = psyp__write_data_nolock(p, value);
    pthread_mutex_unlock(&w->mtx);
    return ok;
}

static bool psyp__async_submit(psyp_port* p, uint8_t value, uint32_t usec) {
    psyp__async* w = (psyp__async*)p->async;
    pthread_mutex_lock(&w->mtx);
    /* Leading edge under the lock so it can't be stomped by the worker's
     * trailing-edge write of an in-flight pulse. */
    bool ok = psyp__write_data_nolock(p, value);
    /* Width is measured from the onset write, not from entry, so the ioctl
     * round trip is not subtracted from it. */
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t ns = now.tv_nsec + (uint64_t)(usec % 1000000u) * 1000ull;
    w->off_at.tv_sec  = now.tv_sec + (time_t)(usec / 1000000u) + (time_t)(ns / 1000000000ull);
    w->off_at.tv_nsec = (long)(ns % 1000000000ull);
    w->has_job = 1;
    pthread_cond_signal(&w->cv);
    pthread_mutex_unlock(&w->mtx);
    return ok;
}

#elif defined(PSYP__WINDOWS)
/* ---- Windows: Win32 thread + waitable timer + TIME_CRITICAL ----------- */

typedef struct psyp__async {
    HANDLE             thread;
    HANDLE             timer;     /* waitable timer, high-resolution if available */
    HANDLE             wakeup;    /* auto-reset event: a job is pending */
    HANDLE             ready;     /* worker published its policy        */
    CRITICAL_SECTION   cs;
    volatile LONG      quit;
    volatile LONG      has_job;
    LARGE_INTEGER      off_at;    /* absolute QPC target               */
    LARGE_INTEGER      qpc_freq;
    LONGLONG           slack;     /* QPC ticks the timer may fire late; spun instead */
    psyp_port*         port;
} psyp__async;

static DWORD WINAPI psyp__worker_main(LPVOID arg) {
    psyp__async* w = (psyp__async*)arg;
    w->port->async_policy =
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL)
            ? PSYP_ASYNC_TIME_CRITICAL : PSYP_ASYNC_NORMAL;
    SetEvent(w->ready); /* releases psyp__async_start() */

    /* State (quit/has_job/off_at) and every data write are guarded by the
     * critical section, so the trailing edge can't stomp the caller's leading
     * edge in psyp__async_submit. The section is released only while blocked
     * in a wait. */
    EnterCriticalSection(&w->cs);
    while (!w->quit) {
        if (!w->has_job) {
            LeaveCriticalSection(&w->cs);
            WaitForSingleObject(w->wakeup, INFINITE);
            EnterCriticalSection(&w->cs);
            continue;
        }
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        LONGLONG rem = w->off_at.QuadPart - now.QuadPart;
        if (rem <= 0) {
            psyp__write_data_raw(w->port, 0);
            w->has_job = 0;
            continue;
        }
        LONGLONG freq = w->qpc_freq.QuadPart;
        LONGLONG slack = w->slack;
        LONGLONG target = w->off_at.QuadPart;
        LeaveCriticalSection(&w->cs);
        if (rem > slack) {
            /* Coarse wait on the high-res timer, but also wake on the event so
             * a new submit / quit re-evaluates the deadline immediately. Split
             * the tick-to-100ns conversion so a TSC-rate QPC (GHz) cannot
             * overflow int64 for long waits. */
            LONGLONG t = rem - slack;
            LARGE_INTEGER due; /* negative = relative, 100ns units */
            due.QuadPart = -((t / freq) * 10000000LL + (t % freq) * 10000000LL / freq);
            HANDLE handles[2] = { w->timer, w->wakeup };
            if (SetWaitableTimer(w->timer, &due, 0, NULL, NULL, FALSE))
                WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        } else {
            /* Sub-millisecond remainder: busy-wait on QPC. */
            do { YieldProcessor(); QueryPerformanceCounter(&now); }
            while (now.QuadPart < target);
        }
        EnterCriticalSection(&w->cs);
    }
    /* On quit, flush any pending trailing edge right away (don't wait out the
     * remaining width) so the data lines are never left asserted. */
    if (w->has_job) {
        psyp__write_data_raw(w->port, 0);
        w->has_job = 0;
    }
    LeaveCriticalSection(&w->cs);
    return 0;
}

static bool psyp__async_start(psyp_port* p) {
    if (p->async) return true;
    psyp__async* w = (psyp__async*)calloc(1, sizeof(*w));
    if (!w) { psyp__set_error(p, "async: out of memory"); return false; }
    w->port = p;
    QueryPerformanceFrequency(&w->qpc_freq);
    InitializeCriticalSection(&w->cs);
    w->wakeup = CreateEventA(NULL, FALSE, FALSE, NULL);
    w->ready  = CreateEventA(NULL, TRUE, FALSE, NULL);
    /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION needs Win10 1803+; fall back. */
    w->timer = CreateWaitableTimerExW(NULL, NULL,
                   0x00000002 /*HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
    bool hires = (w->timer != NULL);
    if (!w->timer) w->timer = CreateWaitableTimerW(NULL, FALSE, NULL);
    /* The timer is asked to fire `slack` early and the rest is spun on QPC:
     * ~1.14 ms for the high-resolution timer, a full 15.6 ms tick for the
     * fallback (accuracy kept, CPU spent). */
    LONGLONG freq = w->qpc_freq.QuadPart;
    w->slack = hires ? (freq / 1000) + (freq / 7000) : (freq * 16) / 1000;
    if (!w->wakeup || !w->ready || !w->timer) {
        if (w->wakeup) CloseHandle(w->wakeup);
        if (w->ready)  CloseHandle(w->ready);
        if (w->timer)  CloseHandle(w->timer);
        DeleteCriticalSection(&w->cs); free(w);
        psyp__set_error(p, "async: timer/event create failed");
        return false;
    }
    w->thread = CreateThread(NULL, 0, psyp__worker_main, w, 0, NULL);
    if (!w->thread) {
        CloseHandle(w->wakeup); CloseHandle(w->ready); CloseHandle(w->timer);
        DeleteCriticalSection(&w->cs); free(w);
        psyp__set_error(p, "async: thread create failed");
        return false;
    }
    /* Wait for the worker to publish its policy (see psyp__worker_main). */
    WaitForSingleObject(w->ready, INFINITE);
    p->async = w;
    return true;
}

static void psyp__async_stop(psyp_port* p) {
    psyp__async* w = (psyp__async*)p->async;
    if (!w) return;
    /* Clear p->async first; see the Linux twin. */
    p->async = NULL;
    EnterCriticalSection(&w->cs);
    w->quit = 1;
    LeaveCriticalSection(&w->cs);
    SetEvent(w->wakeup); /* wake an idle/timer wait so it observes quit */
    WaitForSingleObject(w->thread, INFINITE);
    CloseHandle(w->thread);
    CloseHandle(w->timer);
    CloseHandle(w->wakeup);
    CloseHandle(w->ready);
    DeleteCriticalSection(&w->cs);
    free(w);
    p->async_policy = PSYP_ASYNC_NONE;
}

static bool psyp__write_data_cancel(psyp_port* p, uint8_t value) {
    psyp__async* w = (psyp__async*)p->async;
    EnterCriticalSection(&w->cs);
    /* See the Linux twin: a plain write cancels a pending trailing edge. */
    w->has_job = 0;
    bool ok = psyp__write_data_nolock(p, value);
    LeaveCriticalSection(&w->cs);
    return ok;
}

static bool psyp__async_submit(psyp_port* p, uint8_t value, uint32_t usec) {
    psyp__async* w = (psyp__async*)p->async;
    LONGLONG freq = w->qpc_freq.QuadPart;
    /* Split so usec * freq cannot overflow int64 on a TSC-rate QPC. */
    LONGLONG ticks = (LONGLONG)(usec / 1000000u) * freq
                   + (LONGLONG)(usec % 1000000u) * freq / 1000000LL;
    EnterCriticalSection(&w->cs);
    /* Leading edge under the lock so it can't be stomped by the worker's
     * trailing-edge write of an in-flight pulse. */
    bool ok = psyp__write_data_nolock(p, value);
    /* Width is measured from the onset write, not from entry. */
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    w->off_at.QuadPart = now.QuadPart + ticks;
    w->has_job = 1;
    LeaveCriticalSection(&w->cs);
    SetEvent(w->wakeup);
    return ok;
}

#endif /* platform */

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
     * caller's thread under the worker lock, so onset is not delayed by the
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
    /* Absolute deadline taken just after the onset write, so the write's own
     * round trip is not added to the width and a signal restart does not
     * stretch it. */
    if (usec) psyp__sleep_until(p, psyp__now_ns() + (uint64_t)usec * 1000ull);
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
