/* psy_rt.h - v0.2 - public domain single-header real-time timing library
 *
 *   The clock, the waits, the scheduling ladder and the one-shot deadline
 *   worker that a psychophysics rig needs, factored out of the transport
 *   headers so experiment code and future transports share one clock base
 *   and one set of timing claims.
 *
 *   Written in the single-header style of the stb / sokol libraries. It is
 *   the base the other psy headers stand on: psy_parallel.h and psy_serial.h
 *   both include it and both run their trailing edges on the worker below.
 *
 *   Targets Windows, Linux and macOS. C++17, C11, or the pre-C11 C dialect
 *   MSVC compiles with by default. Nothing but the OS.
 *
 *   ---------------------------------------------------------------------
 *   CHANGELOG
 *   ---------------------------------------------------------------------
 *   v0.2 - psyrt_worker_desc.on_start, a hook that runs ON THE WORKER THREAD
 *          after elevation and before psyrt_worker_start() publishes
 *          readiness, and fails the start when it returns false. The
 *          parallel port's DIRECT backend needs exactly that: Linux grants
 *          port I/O permission per THREAD, so the grant has to happen on the
 *          worker and a refusal has to fail psyp_open() rather than surface
 *          one trial later.
 *          psyrt_job_info.seq and psyrt_worker_pending(). Every submit
 *          stamps the job with a sequence number and RETURNS it, so a job
 *          can tell whether it is still the one a caller is waiting for.
 *          psyrt_worker_submit() therefore returns a positive seq on success
 *          instead of PSYRT_OK; failures are still negative PSYRT_ERR_*, so
 *          the test is `rc < 0`, not `rc != PSYRT_OK`.
 *          The Linux feature-test macro is now _DEFAULT_SOURCE, and it is
 *          set on the FIRST include of this header rather than only in an
 *          implementation translation unit. Including psy_rt.h before a
 *          transport header no longer hides what that transport needs
 *          (CRTSCTS, realpath) behind strict POSIX.
 *          The compile-time assertions fall back to a negative-array-size
 *          typedef, and _Alignof to a struct offset, before C11. MSVC's
 *          default C dialect, which is what setuptools and mex compile with,
 *          now builds this header without /std:c11.
 *   v0.1 - first release.
 *
 *   STATUS: v0.2. The Windows path is built and measured on Windows 11 by
 *   examples/rt_jitter.c, and also builds in MSVC's default (pre-C11) C mode,
 *   which is what the Python and MEX bindings compile with. The Linux path is
 *   built as C11, as C++17 and with PSYRT_NO_THREADS (warnings as errors) and
 *   run under a WSL2 kernel, including a ThreadSanitizer run of the worker's
 *   submit, replace, cancel, flush, seq and on_start paths. The macOS path
 *   has not been compiled or run at all; CI compiles it.
 *   What NO machine here could exercise is the top of the ladder: neither
 *   test host grants CAP_SYS_NICE, so PSYRT_POLICY_DEADLINE,
 *   PSYRT_POLICY_FIFO and PSYRT_POLICY_TIME_CONSTRAINT have never been
 *   obtained. Only PSYRT_POLICY_TIME_CRITICAL (Windows) and
 *   PSYRT_POLICY_NORMAL have run. Treat the real-time rungs as written, not
 *   as tested, and check what psyrt_describe() reports on your rig.
 *   No number in this header is a measurement. Every latency statement is a
 *   bound or a pointer at rt_jitter.
 *
 *   ---------------------------------------------------------------------
 *   USAGE
 *   ---------------------------------------------------------------------
 *   Do this:
 *
 *       #define PSY_RT_IMPLEMENTATION
 *
 *   in *one* C or C++ file before including this header to create the
 *   implementation. Every other file just includes the header normally.
 *
 *       #define PSY_RT_IMPLEMENTATION
 *       #include "psy_rt.h"
 *
 *       psyrt_policy pol = psyrt_thread_elevate(NULL);   // NULL = defaults
 *       psyrt_report rep;
 *       char line[192];
 *       psyrt_report_get(&rep, pol);
 *       psyrt_describe(&rep, line, sizeof line);
 *       puts(line);                      // log this next to your timings
 *
 *       uint64_t t = psyrt_now_ns();
 *       for (int i = 0; i < 100; i++) {
 *           t += 10000000ull;                       // a 10 ms frame
 *           uint64_t late = psyrt_sleep_until(t, PSYRT_DEFAULT_SPIN_NS);
 *           do_frame(i, late);
 *       }
 *
 *   ---------------------------------------------------------------------
 *   CLOCK
 *   ---------------------------------------------------------------------
 *   psyrt_now_ns() and psyrt_now_us() read a monotonic clock:
 *   CLOCK_MONOTONIC on Linux and macOS, QueryPerformanceCounter on Windows,
 *   converted with integer arithmetic split so that a TSC-rate counter
 *   cannot overflow the multiply. The clock never steps and never goes
 *   backwards; its zero is arbitrary, so only differences mean anything.
 *
 *   This is the SAME clock base as psy_serial.h's psys_now_us() and the same
 *   base psy_parallel.h times its pulses against, on every platform the two
 *   share. A timestamp from psyrt_now_us() and one from psys_now_us() taken
 *   in the same process are directly comparable; subtract them. They are not
 *   comparable across a reboot or with wall-clock time.
 *
 *   psyrt_get_clock_info() reports what the OS says the clock is worth
 *   (clock_getres on POSIX, 1 s / QueryPerformanceFrequency on Windows) and
 *   whether a Windows high-resolution waitable timer could be created. The
 *   resolution is the tick the clock counts in, not the cost of reading it
 *   and not the accuracy of any wait.
 *
 *   ---------------------------------------------------------------------
 *   WAITS
 *   ---------------------------------------------------------------------
 *   psyrt_sleep_until(deadline_ns, spin_ns) is the workhorse. It hands the
 *   bulk of the wait to the OS, wakes spin_ns before the deadline, then
 *   spins on the clock to the deadline itself, and returns how late it
 *   actually woke in nanoseconds. Log that number; it is the only honest
 *   statement this header can make about its own accuracy on your machine.
 *
 *     Linux:   clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME). Absolute, so
 *              a signal restart cannot add the elapsed time back on. Its
 *              accuracy is bounded below by the thread's timer slack (50 us
 *              by default; see psyrt_thread_set_timer_slack) and above by
 *              whatever else the scheduler is doing.
 *     macOS:   no absolute clock_nanosleep exists, so the wait is a relative
 *              nanosleep recomputed from the clock on each iteration. A
 *              short wake does not shorten the total.
 *     Windows: a waitable timer created with
 *              CREATE_WAITABLE_TIMER_HIGH_RESOLUTION (Win10 1803+). If that
 *              is refused, the fallback timer only fires on the system tick,
 *              about 15.6 ms, and the spin window is WIDENED to a full tick
 *              so the deadline is still met, trading CPU for accuracy
 *              exactly as psy_parallel.h does. That widening is the only
 *              time this function ignores your spin_ns.
 *
 *   spin_ns = 0 leaves the entire wait to the OS. On Windows even the
 *   high-resolution timer may fire up to about a millisecond late; on Linux
 *   the floor is the thread's timer slack. A spin window buys accuracy with a
 *   busy core: the thread burns CPU for spin_ns of every wait. Pick it from
 *   measurement, not from this comment, and run examples/rt_jitter.c on the
 *   rig to see what each window is worth there.
 *
 *   psyrt_spin_until() is the pure-spin floor: no syscall, a pause
 *   instruction in the loop so a sibling hyperthread is not starved. It is
 *   as accurate as the clock and as expensive as a full core.
 *
 *   psyrt_sleep_ns() is the relative convenience wrapper. It, and a zeroed
 *   psyrt_worker_desc.spin_ns, take PSYRT_DEFAULT_SPIN_NS as the window. That
 *   default is NOT the same on every platform, because the OS wait it has to
 *   cover is not the same:
 *
 *     Windows: about 1.2 ms, because the high-resolution waitable timer may
 *              fire that late. A narrower window leaves a median lateness the
 *              OS wait itself cannot remove, so the spin is the only thing
 *              that closes it. psy_parallel.h spins the last ~1.14 ms of a
 *              pulse for the same reason.
 *     Linux,
 *     macOS:   200 us, which covers the default 50 us timer slack and the
 *              ordinary scheduling delay behind it. A wider window here would
 *              burn CPU for nothing.
 *
 *   Both are starting points, not measurements, and a rig with a different
 *   power plan, chipset or background load will want a different number.
 *   examples/rt_jitter.c sweeps several windows, marks this platform's
 *   default, and prints the wake latency distribution of each: pick yours
 *   from that output and pass it to psyrt_sleep_until() or to
 *   psyrt_worker_desc.spin_ns.
 *
 *   No wait in this header allocates. On Windows each thread that calls
 *   psyrt_sleep_until() creates one waitable timer on first use and reuses
 *   it; psyrt_thread_cleanup() releases it for a thread that is about to
 *   end and will not wait again.
 *
 *   ---------------------------------------------------------------------
 *   SCHEDULING
 *   ---------------------------------------------------------------------
 *   psyrt_thread_elevate() raises the CALLING thread as far up this ladder
 *   as the OS and this process's privileges allow, and reports the rung that
 *   stuck. Asking is not getting: every rung can be refused, nothing fails,
 *   and the return value is the only way to know what you have.
 *
 *     PSYRT_POLICY_DEADLINE         Linux SCHED_DEADLINE, through the raw
 *                                   sched_setattr syscall (glibc has no
 *                                   wrapper). Needs CAP_SYS_NICE, and the
 *                                   kernel also refuses it when the thread's
 *                                   affinity is not the full root domain,
 *                                   which is why psyrt_thread_pin() and this
 *                                   rung are mutually exclusive.
 *     PSYRT_POLICY_TIME_CONSTRAINT  macOS THREAD_TIME_CONSTRAINT_POLICY via
 *                                   thread_policy_set: the same "runtime in
 *                                   a period by a deadline" reservation,
 *                                   under a different kernel. Its own rung
 *                                   because it is neither SCHED_DEADLINE nor
 *                                   SCHED_FIFO and should not be logged as
 *                                   either.
 *     PSYRT_POLICY_FIFO             SCHED_FIFO at priority 80, or the system
 *                                   maximum when that is lower. 80 is above
 *                                   the kernel's threaded IRQ handlers (50)
 *                                   and below migration and the watchdog
 *                                   (99). Needs CAP_SYS_NICE on Linux.
 *     PSYRT_POLICY_TIME_CRITICAL    Windows THREAD_PRIORITY_TIME_CRITICAL.
 *     PSYRT_POLICY_NORMAL           Nothing was granted. On Linux the timer
 *                                   slack is still dropped to 1 ns (the kernel
 *                                   rounds up to its own floor), which is free
 *                                   and removes 50 us of deliberate lateness
 *                                   from every wait.
 *
 *   The reservation (psyrt_sched_deadline: runtime, deadline, period in ns)
 *   means the same thing here as in psyp_desc.sched and psys_desc.sched:
 *   runtime is the CPU budget guaranteed and capped per period, the relative
 *   deadline orders EDF priority, and runtime/period is the bandwidth the
 *   kernel must admit. All-zero means the library defaults, 1 ms in 10 ms.
 *   psyrt_sched_normalize() applies the same validation the transport
 *   headers apply at open time: a zero period becomes the deadline, and
 *   0 < runtime <= deadline <= period must hold.
 *
 *   The rest of the setup calls each do one thing and say whether it took:
 *
 *     psyrt_thread_set_timer_slack()  Linux PR_SET_TIMERSLACK. A normal
 *                                     thread's timers are allowed to fire
 *                                     50 us late so the kernel can batch
 *                                     wakeups. Set it to 1 ns (rounded up to
 *                                     the kernel's floor) and that deliberate
 *                                     lateness is gone. No-op elsewhere.
 *     psyrt_thread_pin()              Bind the calling thread to one CPU.
 *                                     PINNING DEFEATS SCHED_DEADLINE: the
 *                                     kernel's admission test refuses a
 *                                     deadline task whose affinity is not
 *                                     the root domain, so pin OR take the
 *                                     DEADLINE rung, not both. Pinning plus
 *                                     an isolated core (isolcpus, nohz_full)
 *                                     is the other way to build a quiet
 *                                     thread. macOS has no true pinning, only
 *                                     an affinity hint, so this returns false
 *                                     there.
 *     psyrt_process_lock_memory()     POSIX mlockall(MCL_CURRENT|MCL_FUTURE):
 *                                     no page of this process is paged out,
 *                                     so no wait ends in a major fault. Needs
 *                                     RLIMIT_MEMLOCK headroom or CAP_IPC_LOCK.
 *                                     WINDOWS HAS NO EQUIVALENT. VirtualLock
 *                                     and SetProcessWorkingSetSize pin
 *                                     individual pages into a working set
 *                                     whose size the memory manager still
 *                                     owns; neither is mlockall. This
 *                                     function does nothing and returns false
 *                                     on Windows rather than claim otherwise.
 *     psyrt_timer_resolution_begin()  Windows timeBeginPeriod(1), loaded from
 *                                     winmm at runtime so the header keeps
 *                                     its "no libraries" promise. It lowers
 *                                     the system tick so ordinary waits get
 *                                     finer. Since Windows 10 2004 the effect
 *                                     is per-process, not global, so it no
 *                                     longer slows the whole machine down and
 *                                     no longer helps another process. The
 *                                     high-resolution waitable timer that
 *                                     psyrt_sleep_until() uses makes it
 *                                     largely unnecessary; it still matters
 *                                     for Sleep() and for the fallback timer.
 *                                     Pair with psyrt_timer_resolution_end().
 *                                     No-op elsewhere.
 *
 *   psyrt_report_get() and psyrt_describe() put the whole outcome on one
 *   line, for the log file next to the timing data:
 *
 *     psy_rt: policy=TIME_CRITICAL clock_res=100ns slack=n/a hires_timer=yes
 *             mlock=no timer_res=no
 *
 *   Jitter numbers without that line are unlabeled. Print it.
 *
 *   ---------------------------------------------------------------------
 *   WORKER
 *   ---------------------------------------------------------------------
 *   psyrt_worker is one thread that runs one callback at one absolute
 *   deadline. The caller allocates the handle, psyrt_worker_start() spawns
 *   the thread, and the thread elevates ITSELF up the ladder above and
 *   publishes the rung it got before start() returns, so the policy is known
 *   before the first trial and no trial pays for thread creation.
 *
 *     psyrt_worker_submit(w, deadline_ns, fn, ctx)
 *         One pending job per worker. A submit while a job is pending
 *         REPLACES it, earlier deadline or later, and the worker re-evaluates
 *         at once. There is no queue: a rig that needs two independent
 *         deadlines starts two workers.
 *     psyrt_worker_cancel(w)
 *         Drops the pending job.
 *     psyrt_worker_stop(w)
 *         Runs a pending job IMMEDIATELY (a flush, with info->flushed set)
 *         and then joins. This is what the transport headers do when they
 *         close with a pulse in flight: a trailing edge that never lands is
 *         worse than an early one.
 *
 *   The callback runs ON THE WORKER THREAD, at the worker's policy, WITHOUT
 *   the worker's lock held. That is deliberate and it is what makes the
 *   callback usable: it may call psyrt_worker_submit() on its own worker to
 *   re-arm a periodic job, it may block, and it cannot deadlock against a
 *   submit from another thread. The price is that a callback which outlives
 *   its own deadline overlaps the next one, and that two calls to a
 *   non-reentrant routine from the callback and from the main thread need
 *   the caller's own lock. Nothing in the callback's data is protected by
 *   psy_rt.h.
 *
 *   Nothing allocates after psyrt_worker_start() returns. The handle carries
 *   its own OS objects, the job is three fields, and submit, cancel and the
 *   worker's own wait are all allocation-free.
 *
 *   A submit or cancel issued inside the worker's final spin window is seen
 *   AFTER the spin ends, not during: the worker releases its lock to spin, so
 *   the call does not block, but the job it was racing has already been
 *   chosen. The race is exactly as wide as the worker's spin window, so it is
 *   wider on Windows than on Linux and macOS when the desc leaves that window
 *   at PSYRT_DEFAULT_SPIN_NS. See WAITS.
 *
 *   psyrt_worker_desc.on_start runs ON THE WORKER THREAD, after the thread
 *   elevates itself and before psyrt_worker_start() returns, and a false
 *   return fails the start. It is for state the OS keeps PER THREAD, which
 *   the thread that calls start() cannot set up on the worker's behalf: the
 *   parallel port's DIRECT backend grants itself ioperm()/iopl() there,
 *   because those are per-thread on Linux and a refused grant has to fail
 *   the open, not the first trigger.
 *
 *   Every submit stamps the job with a sequence number, returns it, and
 *   hands the same number to the callback as psyrt_job_info.seq;
 *   psyrt_worker_pending() reports the seq of the job now pending, or 0. A
 *   job that has to know whether it is still the one its caller is waiting
 *   for can compare the two, without a token of its own. The numbers are
 *   positive, increasing and unique per worker; nothing else about them is
 *   promised.
 *
 *   Both transport headers build their trailing edges on this worker, and
 *   both keep a small mutex and a token of their own beside it. That is not
 *   duplication: the worker runs the callback with ITS lock released (see
 *   above), so serializing the register or the byte stream is the
 *   transport's job, and a job already inside its final spin window cannot
 *   be canceled, so "a plain write cancels a pending trailing edge" needs a
 *   token the callback rechecks under the transport's own lock. See the
 *   THREADING section of psy_serial.h and the async block of psy_parallel.h
 *   for each one. Everything ELSE uses this worker as it stands: a stimulus
 *   onset, a frame boundary, a response window, a new transport you write.
 *
 *   ---------------------------------------------------------------------
 *   BUILDING
 *   ---------------------------------------------------------------------
 *   POSIX builds link -pthread: the worker needs it, and so does the
 *   scheduling ladder, which raises the calling thread through
 *   pthread_setschedparam even when the worker is compiled out. On glibc
 *   older than 2.17 also link -lrt for clock_gettime. macOS needs nothing
 *   extra. Windows needs no import library: winmm is loaded at runtime and
 *   only by psyrt_timer_resolution_begin().
 *
 *   Define PSYRT_NO_THREADS to drop the worker. That removes psyrt_worker
 *   and every psyrt_worker_* declaration, so code that uses them must guard
 *   the same way. The clock, the waits and the whole scheduling section stay:
 *   elevating the calling thread is not a threading feature.
 *
 *   Define PSYRT_API to override the default `extern` linkage.
 *
 *       cc -O2 -pthread -I. -o rt_jitter examples/rt_jitter.c
 *       cl /O2 /I. examples\rt_jitter.c
 *
 *   ---------------------------------------------------------------------
 *   LICENSE: public domain / MIT-0, see end of file.
 */
#ifndef PSY_RT_H_INCLUDED
#define PSY_RT_H_INCLUDED

/* Feature-test macro for clock_nanosleep() and mlockall() in the Linux
 * implementation. Defined here, before the first system header, so it takes
 * effect when this header is the translation unit's first include (the usual
 * case). If you include other system headers before this one, define
 * _DEFAULT_SOURCE yourself.
 *
 * _DEFAULT_SOURCE rather than the _POSIX_C_SOURCE 200809L this header's own
 * code needs, and set on the FIRST include rather than only in an
 * implementation translation unit. Both for the same reason: a translation
 * unit may include this header without implementing it and then implement
 * psy_serial.h, and an explicit _POSIX_C_SOURCE makes glibc drop __USE_MISC
 * for the WHOLE unit, taking CRTSCTS, realpath, strdup and usleep with it.
 * _DEFAULT_SOURCE is a superset of what this header needs and is the same
 * macro both transport headers ask for, so every include order agrees.
 *
 * Linux only, deliberately. Setting a feature macro on macOS drops
 * __DARWIN_C_LEVEL out of __DARWIN_C_FULL, which hides exactly what the
 * Darwin path needs: pthread_mach_thread_np and
 * pthread_cond_timedwait_relative_np. */
#if defined(__linux__) && !defined(_DEFAULT_SOURCE) && !defined(_GNU_SOURCE) && \
    !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
    #define _DEFAULT_SOURCE 1
#endif

/* CONDITION_VARIABLE and CreateWaitableTimerEx are Vista; raise the target
 * only when the user set it lower, and only in the implementation. */
#if defined(PSY_RT_IMPLEMENTATION) && defined(_WIN32)
    #if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
        #undef _WIN32_WINNT
        #define _WIN32_WINNT 0x0600
    #endif
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PSYRT_API
#define PSYRT_API extern
#endif

/* --- clock -------------------------------------------------------------- */

/* Monotonic nanoseconds. CLOCK_MONOTONIC on Linux and macOS,
 * QueryPerformanceCounter on Windows. The zero point is arbitrary; only
 * differences are meaningful. This is the clock every deadline in this header
 * is expressed in, and the same base as psy_serial.h's psys_now_us(). */
PSYRT_API uint64_t psyrt_now_ns(void);

/* Monotonic microseconds, the same clock truncated. Use it where a uint64 of
 * nanoseconds is more precision than the record needs. */
PSYRT_API uint64_t psyrt_now_us(void);

/* What the OS says the timing facilities are worth. Filled by
 * psyrt_get_clock_info(); every field is a property of the machine, not of
 * any one thread. */
typedef struct psyrt_clock_info {
    uint64_t resolution_ns; /* the tick the clock counts in: clock_getres() on
                             * POSIX, 1 s / QueryPerformanceFrequency() on
                             * Windows. Not the cost of reading the clock and
                             * not the accuracy of a wait. */
    bool hires_timer;       /* Windows: a CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
                             * timer could be created (Win10 1803+). False
                             * means every OS wait is quantized to the ~15.6 ms
                             * system tick and psyrt_sleep_until() spins the
                             * last tick instead. Always true on POSIX, where
                             * the sleep syscalls already work at the clock's
                             * own resolution. */
} psyrt_clock_info;

/* Fill `out` with the clock and timer properties above. Cheap, but it creates
 * and destroys a waitable timer on Windows, so do not call it in a hot loop. */
PSYRT_API void psyrt_get_clock_info(psyrt_clock_info* out);

/* --- waits -------------------------------------------------------------- */

/* Spin window psyrt_sleep_ns() and a zeroed psyrt_worker_desc use: that much
 * of the tail of a wait is spun on the clock rather than left to the OS. The
 * value differs by platform because the OS wait it has to cover differs; WAITS
 * says why. Measure it on the rig with examples/rt_jitter.c before you trust
 * it, and pass your own window rather than editing this one. */
#if defined(_WIN32)
    #define PSYRT_DEFAULT_SPIN_NS 1200000u
#else
    #define PSYRT_DEFAULT_SPIN_NS 200000u
#endif

/* Wait until the absolute monotonic time `deadline_ns`, then return how late
 * the wake actually was, in nanoseconds (0 if it landed on or before the
 * deadline). A deadline already in the past returns immediately with the
 * lateness so far; it never waits a whole clock period.
 *
 * The OS is given everything up to `spin_ns` before the deadline and the
 * remainder is spun on the clock with a pause instruction. spin_ns = 0 leaves
 * the whole wait to the OS. On Windows WITHOUT a high-resolution waitable
 * timer the window is widened to one system tick (~15.6 ms) regardless of
 * spin_ns, because the fallback timer cannot fire any finer and overshooting
 * the deadline is the worse failure.
 *
 * Callable from any thread, including several at once. Allocates nothing; on
 * Windows it creates one waitable timer per calling thread on first use (see
 * psyrt_thread_cleanup). */
PSYRT_API uint64_t psyrt_sleep_until(uint64_t deadline_ns, uint32_t spin_ns);

/* Wait `ns` nanoseconds from now with PSYRT_DEFAULT_SPIN_NS as the spin
 * window, and return how late the wake was. The deadline is taken on entry,
 * so the call's own overhead comes out of the wait, not on top of it. For a
 * sequence of frames use psyrt_sleep_until() against a running absolute
 * deadline instead, so wake latency cannot accumulate. */
PSYRT_API uint64_t psyrt_sleep_ns(uint64_t ns);

/* Spin on the clock until `deadline_ns` and return how late the loop exited.
 * No syscall, no sleep, a pause instruction per iteration so a sibling
 * hyperthread keeps its share of the core. As accurate as the clock, and it
 * costs a whole CPU for the duration: this is the floor the other waits are
 * measured against, not a general-purpose sleep. */
PSYRT_API uint64_t psyrt_spin_until(uint64_t deadline_ns);

/* Release the per-thread resources psy_rt.h created for the CALLING thread
 * (today: the Windows waitable timer psyrt_sleep_until() caches). Optional.
 * A thread that exits without calling it leaks one timer handle until the
 * process ends, which is why a long-lived experiment thread need not bother
 * and a worker pool should. No-op on POSIX. */
PSYRT_API void psyrt_thread_cleanup(void);

/* --- scheduling --------------------------------------------------------- */

/* A real-time reservation, in nanoseconds, with the same meaning as
 * psyp_desc.sched and psys_desc.sched: Linux SCHED_DEADLINE, macOS
 * THREAD_TIME_CONSTRAINT_POLICY. These do not set any deadline the caller
 * waits on; they govern how quickly and how predictably the thread gets the
 * CPU once it wakes.
 *
 *   runtime_ns  - CPU budget guaranteed (and capped) per period.
 *   deadline_ns - relative deadline; smaller = higher EDF priority = lower
 *                 wake latency, but harder to admit alongside other RT tasks.
 *   period_ns   - replenishment interval; reserved bandwidth = runtime/period.
 *
 * All three zero means the PSYRT_DEFAULT_RT_* values below. Otherwise
 * 0 < runtime_ns <= deadline_ns <= period_ns must hold, with a zero period
 * read as "same as deadline". See psyrt_sched_normalize(). */
typedef struct psyrt_sched_deadline {
    uint64_t runtime_ns;
    uint64_t deadline_ns;
    uint64_t period_ns;
} psyrt_sched_deadline;

#define PSYRT_DEFAULT_RT_RUNTIME_NS   1000000ull  /*  1 ms */
#define PSYRT_DEFAULT_RT_DEADLINE_NS 10000000ull  /* 10 ms */
#define PSYRT_DEFAULT_RT_PERIOD_NS   10000000ull  /* 10 ms */

/* Apply the defaults and the validation described above to `rt`, in place.
 * All-zero becomes the PSYRT_DEFAULT_RT_* reservation; a zero period_ns
 * becomes deadline_ns. Returns false, leaving `rt` alone, when the result
 * would violate 0 < runtime <= deadline <= period, so a half-filled struct
 * fails loudly instead of silently degrading to the next rung down.
 * A NULL `rt` returns false. */
PSYRT_API bool psyrt_sched_normalize(psyrt_sched_deadline* rt);

/* A rung of the scheduling ladder. See SCHEDULING in the manual for what each
 * one costs to get and what it buys. Log the value you obtained with your
 * timing data; the same jitter histogram means different things at different
 * rungs. */
typedef enum psyrt_policy {
    PSYRT_POLICY_NONE = 0,       /* nothing was attempted, or a worker is not
                                  * running (PSYRT_NO_THREADS, start failed) */
    PSYRT_POLICY_DEADLINE,       /* Linux SCHED_DEADLINE                     */
    PSYRT_POLICY_TIME_CONSTRAINT,/* macOS THREAD_TIME_CONSTRAINT_POLICY      */
    PSYRT_POLICY_FIFO,           /* SCHED_FIFO, priority 80 or system max    */
    PSYRT_POLICY_TIME_CRITICAL,  /* Windows THREAD_PRIORITY_TIME_CRITICAL    */
    PSYRT_POLICY_NORMAL          /* no real-time policy was granted          */
} psyrt_policy;

/* Short static name of a policy ("DEADLINE", "FIFO", ...), for log lines. */
PSYRT_API const char* psyrt_policy_name(psyrt_policy p);

/* Raise the CALLING thread as far up the ladder as this OS and this process's
 * privileges allow and return the rung that stuck. `rt` is the reservation
 * for the DEADLINE and TIME_CONSTRAINT rungs; NULL means the defaults. An
 * `rt` that psyrt_sched_normalize() rejects skips those rungs and starts at
 * FIFO, because a malformed reservation the kernel would refuse anyway is not
 * worth a syscall.
 *
 * Never fails: the bottom rung is PSYRT_POLICY_NORMAL, where the only thing
 * done is dropping the Linux timer slack to 1 ns, which needs no privilege.
 * Nothing here is undone for you; a thread stays elevated until it exits.
 *
 * On Linux, call this BEFORE psyrt_thread_pin(): the kernel refuses
 * SCHED_DEADLINE to a thread whose affinity is not the root domain, and it
 * refuses to narrow the affinity of a thread that already has it. */
PSYRT_API psyrt_policy psyrt_thread_elevate(const psyrt_sched_deadline* rt);

/* Ask the kernel to stop deliberately firing this thread's timers late.
 * Linux only (PR_SET_TIMERSLACK): a normal thread's default is 50000 ns, and
 * every clock_nanosleep in this header pays it. `ns` is clamped to at least 1;
 * the kernel keeps its own floor. Returns true if it was applied, false on
 * every other platform and when the call was refused. Real-time policies
 * ignore timer slack entirely, so this only matters at PSYRT_POLICY_NORMAL,
 * where psyrt_thread_elevate() already does it for you. */
PSYRT_API bool psyrt_thread_set_timer_slack(uint64_t ns);

/* Bind the calling thread to logical CPU `cpu`. Returns true if the OS
 * applied it.
 *
 * PINNING DEFEATS SCHED_DEADLINE. The kernel's admission control requires a
 * deadline task to be schedulable over the whole root domain, so a pinned
 * thread cannot hold PSYRT_POLICY_DEADLINE and an already-deadline thread
 * cannot be pinned. Choose: a deadline reservation on a normal machine, or a
 * FIFO thread pinned to a core the kernel was told to leave alone (isolcpus,
 * nohz_full, irqaffinity). Do not expect both.
 *
 * macOS has no thread pinning, only THREAD_AFFINITY_POLICY, which is a
 * cache-locality hint the scheduler may ignore; this returns false there
 * rather than pretend. */
PSYRT_API bool psyrt_thread_pin(int cpu);

/* Lock this PROCESS's pages into RAM so no wait can end in a page fault:
 * mlockall(MCL_CURRENT|MCL_FUTURE) on POSIX, which needs RLIMIT_MEMLOCK
 * headroom or CAP_IPC_LOCK. Returns true if it was applied.
 *
 * Returns false and does nothing on Windows, which has no equivalent:
 * VirtualLock and SetProcessWorkingSetSize operate on pages and on a working
 * set the memory manager still owns, and calling them would buy a claim this
 * header cannot honor. Process-wide, so call it once from the main thread. */
PSYRT_API bool psyrt_process_lock_memory(void);

/* Windows: timeBeginPeriod(1), raising this process's timer resolution to
 * 1 ms. Returns true if it was applied. winmm is loaded at run time, so the
 * header still links against nothing.
 *
 * Since Windows 10 2004 this is per-process, not machine-wide, so it neither
 * slows other processes down nor speeds them up, and since the
 * high-resolution waitable timer arrived in Win10 1803 psyrt_sleep_until()
 * rarely needs it. It still shortens Sleep() and the fallback waitable timer,
 * which is what an older machine or a third-party library in the same process
 * will be using. Pair every call with psyrt_timer_resolution_end(): the OS
 * reference-counts them. Process-wide; call from one thread. No-op and false
 * elsewhere. */
PSYRT_API bool psyrt_timer_resolution_begin(void);

/* Undo one psyrt_timer_resolution_begin(). No-op elsewhere, and no-op if the
 * resolution was never raised. */
PSYRT_API void psyrt_timer_resolution_end(void);

/* --- report ------------------------------------------------------------- */

/* Everything psy_rt.h obtained or measured about the current thread and
 * process, in one struct, so it can go in the log next to the timing data.
 * Filled by psyrt_report_get(). */
typedef struct psyrt_report {
    psyrt_policy policy;            /* the rung the caller obtained; the OS
                                     * cannot be asked about every rung, so
                                     * the caller supplies this one          */
    uint64_t clock_res_ns;          /* psyrt_clock_info.resolution_ns        */
    uint64_t timer_slack_ns;        /* Linux PR_GET_TIMERSLACK               */
    bool     timer_slack_known;     /* false where the OS has no such notion */
    bool     hires_timer;           /* psyrt_clock_info.hires_timer          */
    bool     memory_locked;         /* psyrt_process_lock_memory() succeeded */
    bool     timer_resolution_raised; /* psyrt_timer_resolution_begin() did  */
} psyrt_report;

/* Fill `out` with what can be queried right now, plus the `policy` the caller
 * obtained from psyrt_thread_elevate() or psyrt_worker_policy(). The two
 * process-wide flags reflect calls made through this header in this process;
 * if something else locked memory or raised the timer resolution, they stay
 * false. Reads the calling thread's timer slack, so call it from the thread
 * the report is about. */
PSYRT_API void psyrt_report_get(psyrt_report* out, psyrt_policy policy);

/* Write a one-line, log-ready summary of `r` into `buf` (NUL-terminated,
 * truncated to fit) and return the number of characters written, not counting
 * the NUL. 192 bytes is always enough. The line looks like:
 *
 *   psy_rt: policy=DEADLINE clock_res=1ns slack=50000ns hires_timer=yes
 *           mlock=yes timer_res=no
 *
 * (on one line). Print it once per session, beside the jitter numbers. */
PSYRT_API int psyrt_describe(const psyrt_report* r, char* buf, size_t cap);

/* --- worker ------------------------------------------------------------- */

#ifndef PSYRT_NO_THREADS

/* Return codes for the functions a second thread may call. A count, a job
 * seq, or 0 when >= 0; one of these when negative. psyrt_strerror() names
 * them. PSYRT_OK is what "no error, nothing to report" means; do not compare
 * against it, compare against 0. */
#define PSYRT_OK           0
#define PSYRT_ERR_ARG    (-1)  /* null handle, null callback                */
#define PSYRT_ERR_STOPPED (-2) /* the worker is not running                 */

/* Static description of a PSYRT_ERR_* code ("ok" for values >= 0). */
PSYRT_API const char* psyrt_strerror(int code);

/* What the worker thread knows about the job it is running. Valid only for
 * the duration of the callback. */
typedef struct psyrt_job_info {
    uint64_t deadline_ns; /* the deadline this job was submitted for        */
    uint64_t at_ns;       /* the clock when the worker entered the callback */
    int64_t  late_ns;     /* at_ns - deadline_ns; negative only on a flush  */
    uint32_t seq;         /* what psyrt_worker_submit() returned for this
                           * job. Positive, increasing and unique within one
                           * worker; a job that must know whether it is the
                           * one a caller is still waiting for compares this
                           * with the number that caller kept.             */
    bool     flushed;     /* run early by psyrt_worker_stop(), not by its
                           * deadline. A trailing edge, a cleanup write or a
                           * "the trial ended" hook still has to happen; a
                           * job that is only meaningful on time should check
                           * this and return. */
} psyrt_job_info;

/* The job. Runs on the worker thread, at the worker's policy, with the
 * worker's lock NOT held: it may submit to its own worker to re-arm, and it
 * may block, at the cost of overlapping the next deadline. Keep it short and
 * allocation-free; everything this header promises about jitter ends where
 * this function begins. */
typedef void (*psyrt_job_fn)(void* ctx, const psyrt_job_info* info);

/* Optional start hook. Runs ONCE, on the worker thread, after the thread has
 * elevated itself and before psyrt_worker_start() returns. Return true to let
 * the worker run. Return false and the start FAILS: the thread exits, no job
 * can be submitted, and `err` (or a fixed message when the hook wrote none)
 * is what psyrt_worker_error() reports.
 *
 * This exists for state the OS keeps per THREAD, which the thread calling
 * psyrt_worker_start() cannot set up on the worker's behalf. Write at most
 * `err_cap` bytes into `err`, NUL-terminated; snprintf() is the expected
 * tool. Keep it short: psyrt_worker_start() is blocked until it returns. */
typedef bool (*psyrt_start_fn)(void* ctx, char* err, size_t err_cap);

/* Worker description. Zero-initialize it and set only what you need. */
typedef struct psyrt_worker_desc {
    psyrt_sched_deadline sched; /* reservation for the DEADLINE and
                                 * TIME_CONSTRAINT rungs; all-zero = defaults */
    psyrt_start_fn on_start;    /* per-thread setup, on the worker thread;
                                 * NULL = none. False fails the start.        */
    void*    start_ctx;         /* passed to on_start. Separate from the ctx a
                                 * job carries, which belongs to one submit.  */
    uint32_t spin_ns;           /* spin window for the worker's own wait;
                                 * 0 = PSYRT_DEFAULT_SPIN_NS. See WAITS.      */
    bool     no_elevate;        /* skip the ladder entirely and run the worker
                                 * as a normal thread. For a shared machine, a
                                 * profiling run, or a bug hunt where an RT
                                 * thread that spins would lock the box up.    */
} psyrt_worker_desc;

/* Worker handle. The caller allocates it (stack, or a struct the experiment
 * owns) and treats every field as opaque. The layout is ordered by what a
 * submit touches: the job fields and the lock share the first cache lines,
 * because a submit takes the lock and then writes the job, and the 256-byte
 * error buffer is cold and sits last so it cannot push the lock away from the
 * job. Must be zeroed or stopped before psyrt_worker_start(); a fresh handle
 * has no valid running flag, so start() cannot detect and stop a worker
 * already running in it. */
typedef struct psyrt_worker {
    /* --- hot: read or written on every submit and every wake --- */
    uint64_t     deadline_ns;
    psyrt_job_fn fn;
    void*        ctx;
    uint32_t     spin_ns;
    uint32_t     gen;      /* job generation, so the worker can tell after a
                            * spin whether the job it chose is still the one.
                            * It is also the job's public seq: submit returns
                            * it and the callback sees it.                   */
    int          has_job;
    int          quit;
    int          ready;
    psyrt_policy policy;   /* the rung the worker thread got; final once
                            * psyrt_worker_start() returns true             */
    int          running;  /* read and written atomically: it is the one
                            * field a submit consults before it dares touch
                            * the lock, so it cannot be guarded by the lock */
    /* OS objects (mutex/critical section, condition variable, thread, timer),
     * stored inline so the worker owns no heap, and kept next to the hot
     * fields because the lock is the first thing a submit touches. The
     * implementation asserts at compile time that they fit and are aligned. */
    union { void* p[32]; uint64_t u[32]; double d[32]; } os;
    /* --- cold: setup, teardown, diagnostics --- */
    psyrt_sched_deadline sched;
    psyrt_start_fn on_start;
    void*        start_ctx;
    int          start_failed; /* the hook refused; written before the ready
                                * handshake, read by start() after it        */
    bool         no_elevate;
    char         error[256];
} psyrt_worker;

/* Start `w`'s thread per `desc` (NULL = all defaults). Returns true once the
 * thread is up, has run desc.on_start and has published w->policy, so both
 * are final before the first trial. On failure returns false and leaves a
 * message in psyrt_worker_error(); a refused on_start is one such failure,
 * and the thread is joined before this returns. This is the only worker call
 * that writes that message, and the only one that must not run while another
 * thread is using the handle. */
PSYRT_API bool psyrt_worker_start(psyrt_worker* w, const psyrt_worker_desc* desc);

/* Stop the worker and join its thread. A pending job is run IMMEDIATELY
 * first, with psyrt_job_info.flushed set, so work that must happen is never
 * dropped just because the session ended. Safe on a zeroed or already-stopped
 * handle. Must not race another call on the same handle. */
PSYRT_API void psyrt_worker_stop(psyrt_worker* w);

/* Install `fn(ctx, ...)` to run at the absolute monotonic time
 * `deadline_ns`, replacing any pending job (earlier or later; the worker
 * re-evaluates at once). A deadline already past runs as soon as the worker
 * gets the CPU.
 *
 * Returns this job's sequence number, always POSITIVE, which the callback
 * also sees as psyrt_job_info.seq; on failure a negative PSYRT_ERR_ARG or
 * PSYRT_ERR_STOPPED. Test `rc < 0`, not `rc != PSYRT_OK`. Callable from any
 * thread, including from a job on this same worker. Allocates nothing. */
PSYRT_API int psyrt_worker_submit(psyrt_worker* w, uint64_t deadline_ns,
                                  psyrt_job_fn fn, void* ctx);

/* Drop the pending job, if any. Returns 1 if a job was canceled, 0 if there
 * was none, or a negative PSYRT_ERR_* code. A job already inside its final
 * spin window, or already running, cannot be canceled; see WORKER. */
PSYRT_API int psyrt_worker_cancel(psyrt_worker* w);

/* The seq of the job waiting for its deadline, or 0 when none is pending or
 * the worker is not running. A job that has already been dispatched is no
 * longer pending, so this cannot be used to decide whether a callback will
 * still run; it answers "is something armed", not "is something done". Takes
 * the worker's lock, so it is not for a hot loop. */
PSYRT_API uint32_t psyrt_worker_pending(const psyrt_worker* w);

/* The rung the worker thread obtained. PSYRT_POLICY_NONE before a successful
 * start and after a stop. */
PSYRT_API psyrt_policy psyrt_worker_policy(const psyrt_worker* w);

/* Last psyrt_worker_start() message for this handle ("" if none). */
PSYRT_API const char* psyrt_worker_error(const psyrt_worker* w);

/* True while the worker thread is running. */
PSYRT_API bool psyrt_worker_is_running(const psyrt_worker* w);

#endif /* PSYRT_NO_THREADS */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PSY_RT_H_INCLUDED */

/* ======================================================================= *
 *                            IMPLEMENTATION                               *
 * ======================================================================= */
#ifdef PSY_RT_IMPLEMENTATION
#ifndef PSY_RT_IMPLEMENTATION_GUARD
#define PSY_RT_IMPLEMENTATION_GUARD

#include <string.h>
#include <stdio.h>

/* Pick the concrete platform once. */
#if defined(_WIN32)
    #define PSYRT__WINDOWS 1
#elif defined(__linux__)
    #define PSYRT__POSIX 1
    #define PSYRT__LINUX 1
#elif defined(__APPLE__)
    #define PSYRT__POSIX 1
    #define PSYRT__DARWIN 1
#else
    #error "psy_rt: unsupported platform (need Windows, Linux or macOS)"
#endif

/* The third form covers MSVC's legacy C dialect, which is what setuptools and
 * MATLAB's mex compile with unless they are told otherwise. It has neither
 * _Static_assert nor _Alignof: a typedef whose array bound goes negative fails
 * the same build with a worse message, and a char in front of the type puts
 * the type's alignment in the second member's offset. */
#if defined(__cplusplus)
    #define PSYRT__STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    #define PSYRT__STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#else
    #define PSYRT__CAT2(a, b) a##b
    #define PSYRT__CAT(a, b)  PSYRT__CAT2(a, b)
    #define PSYRT__STATIC_ASSERT(cond, msg) \
        typedef char PSYRT__CAT(psyrt__assert_, __LINE__)[(cond) ? 1 : -1]
#endif

/* PSYRT__ALIGN_PROBE(T) declares what the pre-C11 PSYRT__ALIGNOF(T) measures,
 * and is a bare forward declaration where the language has an alignof of its
 * own. The helper struct is NAMED after its type rather than written inline,
 * because an unnamed type defined inside offsetof() is MSVC's C4116 and is
 * not usable twice. */
#if defined(__cplusplus)
    #define PSYRT__ALIGN_PROBE(T) struct psyrt__align_probe_##T
    #define PSYRT__ALIGNOF(T)     alignof(T)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    #define PSYRT__ALIGN_PROBE(T) struct psyrt__align_probe_##T
    #define PSYRT__ALIGNOF(T)     _Alignof(T)
#else
    #define PSYRT__ALIGN_PROBE(T) struct psyrt__align_probe_##T { char psyrt__c; T psyrt__v; }
    #define PSYRT__ALIGNOF(T)     offsetof(struct psyrt__align_probe_##T, psyrt__v)
#endif

/* The pause / yield hint: tells the core this is a spin-wait, so it stops
 * speculating down the loop and stops starving a sibling hyperthread. Not a
 * sleep and not a barrier; on a core that has no such hint it compiles away. */
#if defined(_MSC_VER)
    #include <intrin.h>
    #if defined(_M_ARM64) || defined(_M_ARM)
        #define PSYRT__PAUSE() __yield()
    #else
        #define PSYRT__PAUSE() _mm_pause()
    #endif
#elif defined(__i386__) || defined(__x86_64__)
    #define PSYRT__PAUSE() __builtin_ia32_pause()
#elif defined(__aarch64__) || defined(__arm__)
    #define PSYRT__PAUSE() __asm__ __volatile__("yield" ::: "memory")
#else
    #define PSYRT__PAUSE() ((void)0)
#endif

/* The Windows system tick with no high-resolution timer. Rounded up from the
 * usual 15.625 ms so a wait that must spin the last tick spins a whole one. */
#define PSYRT__TICK_NS 16000000ull

/* Process-wide outcomes, for psyrt_report_get(). Both are set by functions
 * the manual says to call once from one thread, so a plain static is honest
 * here and a lock would only hide misuse. */
static bool psyrt__mem_locked = false;
static bool psyrt__timer_res_raised = false;

/* ======================================================================= *
 *  WINDOWS
 * ======================================================================= */
#if defined(PSYRT__WINDOWS)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#if defined(__cplusplus)
    #define PSYRT__THREAD_LOCAL thread_local
#elif defined(_MSC_VER)
    #define PSYRT__THREAD_LOCAL __declspec(thread)
#else
    #define PSYRT__THREAD_LOCAL _Thread_local
#endif

uint64_t psyrt_now_ns(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    uint64_t t = (uint64_t)c.QuadPart, fr = (uint64_t)f.QuadPart;
    /* Split the multiply: a TSC-rate counter (GHz) times 1e9 overflows 64
     * bits within hours of uptime if done in one step. */
    return (t / fr) * 1000000000ull + (t % fr) * 1000000000ull / fr;
}

/* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, spelled out because the SDK only
 * defines it when the target version is Win10 1803 or newer. */
#define PSYRT__TIMER_HIRES 0x00000002

/* Create the best waitable timer this machine offers. *hires tells which one
 * came back, which decides whether a caller must spin a whole system tick. */
static HANDLE psyrt__timer_create(bool* hires) {
    HANDLE h = CreateWaitableTimerExW(NULL, NULL, PSYRT__TIMER_HIRES,
                                      TIMER_ALL_ACCESS);
    *hires = (h != NULL);
    if (!h) h = CreateWaitableTimerW(NULL, FALSE, NULL);
    return h;
}

/* One waitable timer per thread that waits. Creating a kernel object inside
 * every psyrt_sleep_until() would put a syscall in the path the function
 * exists to keep short. */
typedef struct psyrt__tls {
    HANDLE timer;
    bool   hires;
    bool   tried;
} psyrt__tls;

static PSYRT__THREAD_LOCAL psyrt__tls psyrt__tls_state;

static psyrt__tls* psyrt__tls_get(void) {
    psyrt__tls* t = &psyrt__tls_state;
    if (!t->tried) {
        t->tried = true;
        t->timer = psyrt__timer_create(&t->hires);
    }
    return t;
}

void psyrt_thread_cleanup(void) {
    psyrt__tls* t = &psyrt__tls_state;
    if (t->timer) CloseHandle(t->timer);
    t->timer = NULL;
    t->hires = false;
    t->tried = false;
}

/* Hand the wait to the OS until an absolute monotonic time. Returns as soon
 * as the timer fires; the caller re-reads the clock and decides what is left. */
static void psyrt__coarse_wait_until(HANDLE timer, uint64_t until_ns) {
    uint64_t now = psyrt_now_ns();
    if (until_ns <= now) return;
    uint64_t delta = until_ns - now;
    if (timer) {
        LARGE_INTEGER due; /* negative = relative, 100 ns units */
        due.QuadPart = -(LONGLONG)(delta / 100ull);
        if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) {
            WaitForSingleObject(timer, INFINITE);
            return;
        }
    }
    /* No timer at all: Sleep is coarse, but it is still better than spinning
     * out an arbitrarily long wait. */
    Sleep((DWORD)(delta / 1000000ull));
}

void psyrt_get_clock_info(psyrt_clock_info* out) {
    if (!out) return;
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    uint64_t fr = (uint64_t)f.QuadPart;
    out->resolution_ns = fr ? (1000000000ull + fr - 1) / fr : 0;
    bool hires = false;
    HANDLE h = psyrt__timer_create(&hires);
    if (h) CloseHandle(h);
    out->hires_timer = hires;
}

uint64_t psyrt_sleep_until(uint64_t deadline_ns, uint32_t spin_ns) {
    psyrt__tls* t = psyrt__tls_get();
    uint64_t spin = spin_ns;
    /* The fallback timer only fires on the system tick, so the coarse wait
     * has to stop a whole tick early and spin the rest. Overshooting the
     * deadline by 15 ms is worse than burning 15 ms of one core. */
    if (!t->hires && spin < PSYRT__TICK_NS) spin = PSYRT__TICK_NS;
    uint64_t now = psyrt_now_ns();
    if (now >= deadline_ns) return now - deadline_ns;
    uint64_t wake = (deadline_ns > spin) ? deadline_ns - spin : 0;
    if (now < wake) psyrt__coarse_wait_until(t->timer, wake);
    return psyrt_spin_until(deadline_ns);
}

bool psyrt_thread_set_timer_slack(uint64_t ns) {
    (void)ns;
    return false; /* Windows has no per-thread timer slack to set. */
}

bool psyrt_thread_pin(int cpu) {
    if (cpu < 0 || cpu >= (int)(8 * sizeof(DWORD_PTR))) return false;
    DWORD_PTR mask = (DWORD_PTR)1 << cpu;
    return SetThreadAffinityMask(GetCurrentThread(), mask) != 0;
}

bool psyrt_process_lock_memory(void) {
    /* Deliberately empty. See the declaration: nothing Windows offers is
     * mlockall, and a partial measure logged as a success is worse than a
     * documented false. */
    return false;
}

/* winmm is loaded at run time so this header still links against nothing.
 * timeBeginPeriod is called once per process here, so the load cost is not
 * on any timing path. */
typedef UINT (WINAPI *psyrt__timeperiod_fn)(UINT); /* MMRESULT, which
 * lives in mmsystem.h; this header does not include it. */
static HMODULE psyrt__winmm = NULL;
static psyrt__timeperiod_fn psyrt__time_end = NULL;

bool psyrt_timer_resolution_begin(void) {
    if (psyrt__timer_res_raised) return true;
    if (!psyrt__winmm) {
        /* System32 only: this is a system DLL and must not be picked up from
         * a data directory next to the experiment. */
        psyrt__winmm = LoadLibraryExA("winmm.dll", NULL,
                                      LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!psyrt__winmm) return false;
    }
    psyrt__timeperiod_fn begin =
        (psyrt__timeperiod_fn)(void*)GetProcAddress(psyrt__winmm, "timeBeginPeriod");
    psyrt__time_end =
        (psyrt__timeperiod_fn)(void*)GetProcAddress(psyrt__winmm, "timeEndPeriod");
    if (!begin || !psyrt__time_end) return false;
    if (begin(1) != 0 /*TIMERR_NOERROR*/) return false;
    psyrt__timer_res_raised = true;
    return true;
}

void psyrt_timer_resolution_end(void) {
    if (!psyrt__timer_res_raised || !psyrt__time_end) return;
    psyrt__time_end(1);
    psyrt__timer_res_raised = false;
}

psyrt_policy psyrt_thread_elevate(const psyrt_sched_deadline* rt) {
    /* Windows has no reservation to hand a thread, so the parameters are
     * accepted and ignored rather than refused: the same call site works on
     * every platform and only the returned rung differs. */
    (void)rt;
    if (SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL))
        return PSYRT_POLICY_TIME_CRITICAL;
    return PSYRT_POLICY_NORMAL;
}

#endif /* PSYRT__WINDOWS */

/* ======================================================================= *
 *  POSIX (Linux and macOS)
 * ======================================================================= */
#if defined(PSYRT__POSIX)

#include <time.h>
#include <errno.h>
#include <sched.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

#if defined(PSYRT__LINUX)
    #include <sys/syscall.h>
    #include <sys/prctl.h>
    #ifndef SCHED_DEADLINE
    #define SCHED_DEADLINE 6
    #endif
    #ifndef PR_SET_TIMERSLACK
    #define PR_SET_TIMERSLACK 29
    #endif
    #ifndef PR_GET_TIMERSLACK
    #define PR_GET_TIMERSLACK 30
    #endif
/* syscall()'s prototype lives behind _DEFAULT_SOURCE in <unistd.h>; declare
 * it directly so this header does not force a feature-test macro onto the
 * user's translation unit. */
extern long syscall(long, ...);
#endif

#if defined(PSYRT__DARWIN)
    #include <mach/mach.h>
    #include <mach/mach_time.h>
    #include <mach/thread_policy.h>
    #include <mach/thread_act.h>
#endif

uint64_t psyrt_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void psyrt__ns_to_ts(uint64_t ns, struct timespec* ts) {
    ts->tv_sec  = (time_t)(ns / 1000000000ull);
    ts->tv_nsec = (long)(ns % 1000000000ull);
}

void psyrt_thread_cleanup(void) {
    /* POSIX waits need no per-thread object; the entry point exists so
     * portable shutdown code does not have to be #ifdef'd. */
}

static void psyrt__coarse_wait_until(uint64_t until_ns) {
#if defined(PSYRT__DARWIN)
    /* Darwin has no absolute clock_nanosleep, so the remainder is recomputed
     * from the clock each time round: an early or interrupted wake cannot
     * shorten the total, and a late one is not paid twice. */
    for (;;) {
        uint64_t now = psyrt_now_ns();
        if (now >= until_ns) return;
        struct timespec rel;
        psyrt__ns_to_ts(until_ns - now, &rel);
        if (nanosleep(&rel, NULL) == 0) return;
        if (errno != EINTR) return;
    }
#else
    /* Absolute, so a signal restart does not add the elapsed time back on.
     * clock_nanosleep returns the error rather than setting errno. */
    struct timespec ts;
    psyrt__ns_to_ts(until_ns, &ts);
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) { }
#endif
}

void psyrt_get_clock_info(psyrt_clock_info* out) {
    if (!out) return;
    struct timespec res;
    if (clock_getres(CLOCK_MONOTONIC, &res) == 0)
        out->resolution_ns = (uint64_t)res.tv_sec * 1000000000ull + (uint64_t)res.tv_nsec;
    else
        out->resolution_ns = 0;
    /* The POSIX sleep syscalls already work at the clock's own resolution;
     * there is no second-class timer to fall back to. */
    out->hires_timer = true;
}

uint64_t psyrt_sleep_until(uint64_t deadline_ns, uint32_t spin_ns) {
    uint64_t now = psyrt_now_ns();
    if (now >= deadline_ns) return now - deadline_ns;
    uint64_t wake = (deadline_ns > spin_ns) ? deadline_ns - spin_ns : 0;
    if (now < wake) psyrt__coarse_wait_until(wake);
    return psyrt_spin_until(deadline_ns);
}

bool psyrt_thread_set_timer_slack(uint64_t ns) {
#if defined(PSYRT__LINUX)
    unsigned long v = (ns < 1) ? 1UL : (unsigned long)ns;
    return prctl(PR_SET_TIMERSLACK, v, 0UL, 0UL, 0UL) == 0;
#else
    (void)ns;
    return false; /* Darwin has no timer slack to set. */
#endif
}

bool psyrt_thread_pin(int cpu) {
#if defined(PSYRT__LINUX)
    /* The raw syscall, with the mask built by hand, so this compiles without
     * _GNU_SOURCE and the CPU_SET macros it hides. Thread id 0 is "me": on
     * Linux affinity is a per-thread property. */
    if (cpu < 0 || cpu >= 1024) return false;
    unsigned long mask[1024 / (8 * sizeof(unsigned long))];
    memset(mask, 0, sizeof(mask));
    size_t bits = 8 * sizeof(unsigned long);
    mask[(size_t)cpu / bits] |= 1UL << ((size_t)cpu % bits);
    return syscall(SYS_sched_setaffinity, 0, (unsigned)sizeof(mask), mask) == 0;
#else
    (void)cpu;
    return false; /* Darwin: affinity sets are a hint, not a binding. */
#endif
}

bool psyrt_process_lock_memory(void) {
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) return false;
    psyrt__mem_locked = true;
    return true;
}

bool psyrt_timer_resolution_begin(void) {
    return false; /* POSIX has no global timer tick to raise. */
}

void psyrt_timer_resolution_end(void) {
}

#if defined(PSYRT__LINUX)
/* glibc has no wrapper or type for sched_setattr; declare both here. The
 * kernel identifies the ABI version by the struct's own size field, so this
 * must stay exactly the 48-byte VER0 layout. */
struct psyrt__sched_attr {
    uint32_t size;
    uint32_t sched_policy;
    uint64_t sched_flags;
    int32_t  sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime;
    uint64_t sched_deadline;
    uint64_t sched_period;
};
PSYRT__STATIC_ASSERT(sizeof(struct psyrt__sched_attr) == 48,
                     "sched_attr must match the kernel's VER0 layout");

static int psyrt__setattr(struct psyrt__sched_attr* a) {
#if defined(SYS_sched_setattr)
    return (int)syscall(SYS_sched_setattr, 0, a, 0u);
#elif defined(__x86_64__)
    return (int)syscall(314 /* __NR_sched_setattr */, 0, a, 0u);
#elif defined(__aarch64__)
    return (int)syscall(274 /* __NR_sched_setattr */, 0, a, 0u);
#else
    (void)a; errno = ENOSYS; return -1;
#endif
}
#endif /* PSYRT__LINUX */

#if defined(PSYRT__DARWIN)
/* Ask the Mach scheduler for the same shape of reservation SCHED_DEADLINE
 * describes: `computation` of CPU inside `constraint` of every `period`.
 * Mach counts in absolute time units, not nanoseconds, and the ratio is not
 * 1 on every Apple machine. */
static bool psyrt__darwin_time_constraint(const psyrt_sched_deadline* rt) {
    mach_timebase_info_data_t tb;
    if (mach_timebase_info(&tb) != KERN_SUCCESS || tb.numer == 0) return false;
    /* ns * denom / numer, split so a long period cannot overflow. */
    #define PSYRT__NS2ABS(ns) \
        (uint32_t)(((ns) / 1000000ull) * ((uint64_t)tb.denom * 1000000ull) / tb.numer \
                 + ((ns) % 1000000ull) * (uint64_t)tb.denom / tb.numer)
    thread_time_constraint_policy_data_t pol;
    pol.period      = PSYRT__NS2ABS(rt->period_ns);
    pol.computation = PSYRT__NS2ABS(rt->runtime_ns);
    pol.constraint  = PSYRT__NS2ABS(rt->deadline_ns);
    /* Non-preemptible: the point of the rung is that nothing gets between the
     * wake and the deadline. The kernel caps how much it will honor. */
    pol.preemptible = FALSE;
    #undef PSYRT__NS2ABS
    /* pthread_mach_thread_np borrows the port; mach_thread_self would hand
     * back a reference this function would then have to deallocate. */
    return thread_policy_set(pthread_mach_thread_np(pthread_self()),
                             THREAD_TIME_CONSTRAINT_POLICY,
                             (thread_policy_t)&pol,
                             THREAD_TIME_CONSTRAINT_POLICY_COUNT) == KERN_SUCCESS;
}
#endif /* PSYRT__DARWIN */

psyrt_policy psyrt_thread_elevate(const psyrt_sched_deadline* rt) {
    psyrt_sched_deadline r;
    memset(&r, 0, sizeof(r));
    if (rt) r = *rt;
    bool have_rt = psyrt_sched_normalize(&r);

#if defined(PSYRT__LINUX)
    if (have_rt) {
        struct psyrt__sched_attr a;
        memset(&a, 0, sizeof(a));
        a.size           = (uint32_t)sizeof(a);
        a.sched_policy   = SCHED_DEADLINE;
        a.sched_runtime  = r.runtime_ns;
        a.sched_deadline = r.deadline_ns;
        a.sched_period   = r.period_ns;
        if (psyrt__setattr(&a) == 0) return PSYRT_POLICY_DEADLINE;
    }
#elif defined(PSYRT__DARWIN)
    if (have_rt && psyrt__darwin_time_constraint(&r))
        return PSYRT_POLICY_TIME_CONSTRAINT;
#else
    (void)have_rt;
#endif

    /* FIFO 80: above the kernel's threaded IRQ handlers (50), so a wake is
     * not queued behind a busy NIC, but below migration and the watchdog
     * (99), which must still be able to preempt a runaway spin. */
    struct sched_param sp;
    memset(&sp, 0, sizeof(sp));
    sp.sched_priority = 80;
    int top = sched_get_priority_max(SCHED_FIFO);
    if (top > 0 && sp.sched_priority > top) sp.sched_priority = top;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) == 0)
        return PSYRT_POLICY_FIFO;

    /* No privilege for any of that. Dropping the timer slack costs nothing
     * and removes 50 us of deliberate lateness from every wait this thread
     * takes, which is the single biggest win available to an unprivileged
     * thread on Linux. */
    (void)psyrt_thread_set_timer_slack(1);
    return PSYRT_POLICY_NORMAL;
}

#endif /* PSYRT__POSIX */

/* ======================================================================= *
 *  PLATFORM-INDEPENDENT
 * ======================================================================= */

uint64_t psyrt_now_us(void) {
    return psyrt_now_ns() / 1000ull;
}

uint64_t psyrt_spin_until(uint64_t deadline_ns) {
    uint64_t now = psyrt_now_ns();
    while (now < deadline_ns) {
        PSYRT__PAUSE();
        now = psyrt_now_ns();
    }
    return now - deadline_ns;
}

uint64_t psyrt_sleep_ns(uint64_t ns) {
    /* The deadline is taken here, so the call's own overhead comes out of the
     * wait instead of being added to it. */
    return psyrt_sleep_until(psyrt_now_ns() + ns, PSYRT_DEFAULT_SPIN_NS);
}

bool psyrt_sched_normalize(psyrt_sched_deadline* rt) {
    if (!rt) return false;
    if (rt->runtime_ns == 0 && rt->deadline_ns == 0 && rt->period_ns == 0) {
        rt->runtime_ns  = PSYRT_DEFAULT_RT_RUNTIME_NS;
        rt->deadline_ns = PSYRT_DEFAULT_RT_DEADLINE_NS;
        rt->period_ns   = PSYRT_DEFAULT_RT_PERIOD_NS;
        return true;
    }
    uint64_t period = rt->period_ns ? rt->period_ns : rt->deadline_ns;
    if (rt->runtime_ns == 0 || rt->deadline_ns == 0 ||
        rt->runtime_ns > rt->deadline_ns || rt->deadline_ns > period)
        return false;
    rt->period_ns = period;
    return true;
}

const char* psyrt_policy_name(psyrt_policy p) {
    switch (p) {
        case PSYRT_POLICY_NONE:            return "NONE";
        case PSYRT_POLICY_DEADLINE:        return "DEADLINE";
        case PSYRT_POLICY_TIME_CONSTRAINT: return "TIME_CONSTRAINT";
        case PSYRT_POLICY_FIFO:            return "FIFO";
        case PSYRT_POLICY_TIME_CRITICAL:   return "TIME_CRITICAL";
        case PSYRT_POLICY_NORMAL:          return "NORMAL";
        default:                           return "?";
    }
}

void psyrt_report_get(psyrt_report* out, psyrt_policy policy) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->policy = policy;
    psyrt_clock_info ci;
    memset(&ci, 0, sizeof(ci));
    psyrt_get_clock_info(&ci);
    out->clock_res_ns = ci.resolution_ns;
    out->hires_timer  = ci.hires_timer;
    out->memory_locked = psyrt__mem_locked;
    out->timer_resolution_raised = psyrt__timer_res_raised;
#if defined(PSYRT__LINUX)
    /* PR_GET_TIMERSLACK returns the value itself, so a negative result is the
     * error and 0 is a legitimate (if unusual) answer. */
    int slack = prctl(PR_GET_TIMERSLACK, 0UL, 0UL, 0UL, 0UL);
    if (slack >= 0) {
        out->timer_slack_ns    = (uint64_t)slack;
        out->timer_slack_known = true;
    }
#endif
}

int psyrt_describe(const psyrt_report* r, char* buf, size_t cap) {
    if (!buf || cap == 0) return 0;
    if (!r) { buf[0] = '\0'; return 0; }
    char slack[32];
    if (r->timer_slack_known)
        snprintf(slack, sizeof(slack), "%lluns",
                 (unsigned long long)r->timer_slack_ns);
    else
        snprintf(slack, sizeof(slack), "n/a");
    int n = snprintf(buf, cap,
                     "psy_rt: policy=%s clock_res=%lluns slack=%s "
                     "hires_timer=%s mlock=%s timer_res=%s",
                     psyrt_policy_name(r->policy),
                     (unsigned long long)r->clock_res_ns,
                     slack,
                     r->hires_timer ? "yes" : "no",
                     r->memory_locked ? "yes" : "no",
                     r->timer_resolution_raised ? "yes" : "no");
    /* snprintf reports what it WOULD have written; the caller wants what is
     * in the buffer. */
    if (n < 0) { buf[0] = '\0'; return 0; }
    return (n >= (int)cap) ? (int)cap - 1 : n;
}

/* ======================================================================= *
 *  DEADLINE WORKER
 *
 *  One thread, one pending job, one absolute deadline. The thread elevates
 *  itself and publishes the rung before psyrt_worker_start() returns, so the
 *  policy is known before the first trial and no trial pays for thread
 *  creation. Everything the worker needs lives in the caller's handle, so
 *  nothing allocates once it is running.
 * ======================================================================= */
#ifndef PSYRT_NO_THREADS

/* `running` is the one field that crosses threads outside the worker lock:
 * a job may call psyrt_worker_submit() at the instant psyrt_worker_stop()
 * clears the flag, and a flag whose whole job is to say whether the lock is
 * still there cannot itself be guarded by that lock. One integer, read and
 * written atomically, is the whole synchronization. */
#if defined(_MSC_VER)
static int psyrt__flag_load(const int* p) { return (int)*(const volatile LONG*)p; }
static void psyrt__flag_store(int* p, int v) {
    (void)InterlockedExchange((volatile LONG*)p, (LONG)v);
}
#elif defined(__GNUC__) || defined(__clang__)
static int psyrt__flag_load(const int* p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
static void psyrt__flag_store(int* p, int v) { __atomic_store_n(p, v, __ATOMIC_RELEASE); }
#else
/* No atomics available: volatile keeps the compiler from caching the flag,
 * which is all any real 32-bit aligned load or store needs here. */
static int psyrt__flag_load(const int* p) { return *(const volatile int*)p; }
static void psyrt__flag_store(int* p, int v) { *(volatile int*)p = v; }
#endif

/* The generation is also the job's public seq, which psyrt_worker_submit()
 * returns as an int, so it is kept inside 1..INT32_MAX: 0 means "no job" to
 * psyrt_worker_pending() and a negative return means an error. Wrapping keeps
 * the counter unique for long enough that no session can see a repeat, and
 * the generation check only needs "different", not "greater". */
static uint32_t psyrt__next_gen(uint32_t g) {
    return (g >= 0x7fffffffu) ? 1u : g + 1u;
}

const char* psyrt_strerror(int code) {
    switch (code) {
        case PSYRT_ERR_ARG:     return "bad argument";
        case PSYRT_ERR_STOPPED: return "worker not running";
        default:                return code >= 0 ? "ok" : "unknown error";
    }
}

psyrt_policy psyrt_worker_policy(const psyrt_worker* w) {
    return w ? w->policy : PSYRT_POLICY_NONE;
}

const char* psyrt_worker_error(const psyrt_worker* w) {
    return w ? w->error : "null worker handle";
}

bool psyrt_worker_is_running(const psyrt_worker* w) {
    return w && psyrt__flag_load(&w->running) != 0;
}

#if defined(PSYRT__POSIX)
/* ---- POSIX: pthreads, PI mutex, CLOCK_MONOTONIC condvar --------------- */

typedef struct psyrt__wstate {
    pthread_t       thread;
    pthread_mutex_t mtx;
    pthread_cond_t  cv;
} psyrt__wstate;

#else
/* ---- Windows: critical section, condition variable, waitable timer ---- */

typedef struct psyrt__wstate {
    CRITICAL_SECTION   cs;
    CONDITION_VARIABLE cv;
    HANDLE thread;
    HANDLE timer;   /* the coarse wait                                    */
    HANDLE wakeup;  /* auto-reset: a submit happened during a timed wait  */
    bool   hires;
} psyrt__wstate;

#endif

PSYRT__ALIGN_PROBE(psyrt__wstate);
PSYRT__STATIC_ASSERT(sizeof(psyrt__wstate) <= sizeof(((psyrt_worker*)0)->os),
                     "psyrt_worker.os is too small for this platform's OS objects");
PSYRT__STATIC_ASSERT(PSYRT__ALIGNOF(psyrt__wstate) <= 8,
                     "psyrt_worker.os is not aligned enough for this platform");

/* The OS objects live inside the caller's handle; this is the one place that
 * knows it. */
static psyrt__wstate* psyrt__w(psyrt_worker* w) {
    return (psyrt__wstate*)(void*)&w->os;
}

static void psyrt__worker_lock(psyrt_worker* w);
static void psyrt__worker_unlock(psyrt_worker* w);

/* Run desc.on_start on the worker thread, which is the whole point of it:
 * ioperm(), iopl() and the rest of what an OS keeps per thread can only be
 * asked for from the thread that will use them. Called before the ready
 * handshake, so the message it leaves is published to psyrt_worker_start()
 * by the same lock that publishes w->ready. */
static bool psyrt__worker_on_start(psyrt_worker* w) {
    char msg[sizeof(w->error)];
    if (!w->on_start) return true;
    msg[0] = '\0';
    if (w->on_start(w->start_ctx, msg, sizeof(msg))) return true;
    /* A hook that refuses without saying why still must not fail silently. */
    if (msg[0] == '\0')
        snprintf(w->error, sizeof(w->error), "worker: desc.on_start refused");
    else
        snprintf(w->error, sizeof(w->error), "%s", msg);
    return false;
}

/* Take the pending job and run it with the lock RELEASED. Called with the
 * lock held and returns with it held. Dropping the lock is what lets the
 * callback submit to this same worker and block without deadlocking; see
 * WORKER for what the caller owes in exchange. */
static void psyrt__worker_run(psyrt_worker* w, bool flushed) {
    psyrt_job_fn fn = w->fn;
    void* ctx = w->ctx;
    psyrt_job_info info;
    info.deadline_ns = w->deadline_ns;
    info.flushed = flushed;
    /* The generation the submit stamped this job with, read before the bump
     * that retires it. */
    info.seq = w->gen;
    w->has_job = 0;
    w->gen = psyrt__next_gen(w->gen);
    psyrt__worker_unlock(w);
    info.at_ns = psyrt_now_ns();
    info.late_ns = (int64_t)(info.at_ns - info.deadline_ns);
    if (fn) fn(ctx, &info);
    psyrt__worker_lock(w);
}

#if defined(PSYRT__POSIX)

static void psyrt__worker_lock(psyrt_worker* w)   { pthread_mutex_lock(&psyrt__w(w)->mtx); }
static void psyrt__worker_unlock(psyrt_worker* w) { pthread_mutex_unlock(&psyrt__w(w)->mtx); }

/* Wait for a submit, a cancel or a quit. Lock held on entry and on return. */
static void psyrt__worker_wait_idle(psyrt_worker* w) {
    psyrt__wstate* s = psyrt__w(w);
    pthread_cond_wait(&s->cv, &s->mtx);
}

/* Wait until `until_ns` or until something changes, whichever comes first.
 * Lock held on entry and on return; the caller re-evaluates from the top, so
 * a spurious wake costs one clock read. */
static void psyrt__worker_wait_until(psyrt_worker* w, uint64_t until_ns) {
    psyrt__wstate* s = psyrt__w(w);
#if defined(PSYRT__DARWIN)
    /* Darwin has no pthread_condattr_setclock, so an absolute wait would run
     * against CLOCK_REALTIME and step with the wall clock. Wait relative to
     * the monotonic clock instead. */
    uint64_t now = psyrt_now_ns();
    if (until_ns <= now) return;
    struct timespec rel;
    psyrt__ns_to_ts(until_ns - now, &rel);
    pthread_cond_timedwait_relative_np(&s->cv, &s->mtx, &rel);
#else
    struct timespec ts;
    psyrt__ns_to_ts(until_ns, &ts);
    pthread_cond_timedwait(&s->cv, &s->mtx, &ts);
#endif
}

static void psyrt__worker_wake(psyrt_worker* w) {
    pthread_cond_signal(&psyrt__w(w)->cv);
}

static void psyrt__worker_loop(psyrt_worker* w);

static void* psyrt__worker_main(void* arg) {
    psyrt_worker* w = (psyrt_worker*)arg;
    psyrt_policy pol = w->no_elevate ? PSYRT_POLICY_NORMAL
                                     : psyrt_thread_elevate(&w->sched);
    /* After the elevation, so a hook can see the rung it is running at, and
     * before the handshake, so a refusal fails the start instead of a job. */
    bool ok = psyrt__worker_on_start(w);
    psyrt__worker_lock(w);
    /* Publish the rung and release psyrt_worker_start(), which waits for it
     * so w->policy is final when start returns. */
    w->policy = ok ? pol : PSYRT_POLICY_NONE;
    w->start_failed = ok ? 0 : 1;
    w->ready = 1;
    pthread_cond_broadcast(&psyrt__w(w)->cv);
    if (ok) psyrt__worker_loop(w);
    psyrt__worker_unlock(w);
    return NULL;
}

bool psyrt_worker_start(psyrt_worker* w, const psyrt_worker_desc* desc) {
    if (!w) return false;
    psyrt_worker_desc d;
    memset(&d, 0, sizeof(d));
    if (desc) d = *desc;
    if (psyrt__flag_load(&w->running)) return true;

    memset(w, 0, sizeof(*w));
    w->sched = d.sched;
    if (!psyrt_sched_normalize(&w->sched)) {
        snprintf(w->error, sizeof(w->error),
                 "invalid desc.sched: require 0 < runtime_ns <= deadline_ns <= "
                 "period_ns (period_ns 0 means same as deadline_ns)");
        return false;
    }
    w->spin_ns    = d.spin_ns ? d.spin_ns : PSYRT_DEFAULT_SPIN_NS;
    w->no_elevate = d.no_elevate;
    w->on_start   = d.on_start;
    w->start_ctx  = d.start_ctx;
    w->policy     = PSYRT_POLICY_NONE;

    psyrt__wstate* s = psyrt__w(w);
    /* Priority inheritance: a normal-priority submitter holds the mutex for a
     * few instructions, and without PI the real-time worker would wait behind
     * whatever preempted that submitter. */
    pthread_mutexattr_t ma;
    pthread_mutexattr_init(&ma);
    pthread_mutexattr_setprotocol(&ma, PTHREAD_PRIO_INHERIT);
    int mtx_rc = pthread_mutex_init(&s->mtx, &ma);
    pthread_mutexattr_destroy(&ma);
    if (mtx_rc != 0) {
        snprintf(w->error, sizeof(w->error), "worker: mutex init: %s", strerror(mtx_rc));
        return false;
    }
    pthread_condattr_t ca;
    pthread_condattr_init(&ca);
#if !defined(PSYRT__DARWIN)
    /* Absolute timed waits run against the same clock the deadlines are in. */
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
#endif
    int cv_rc = pthread_cond_init(&s->cv, &ca);
    pthread_condattr_destroy(&ca);
    if (cv_rc != 0) {
        pthread_mutex_destroy(&s->mtx);
        snprintf(w->error, sizeof(w->error), "worker: cond init: %s", strerror(cv_rc));
        return false;
    }
    /* pthread_create returns the error; it does not set errno. */
    int rc = pthread_create(&s->thread, NULL, psyrt__worker_main, w);
    if (rc != 0) {
        pthread_cond_destroy(&s->cv);
        pthread_mutex_destroy(&s->mtx);
        snprintf(w->error, sizeof(w->error), "worker: thread create: %s", strerror(rc));
        return false;
    }
    pthread_mutex_lock(&s->mtx);
    while (!w->ready) pthread_cond_wait(&s->cv, &s->mtx);
    int refused = w->start_failed;
    pthread_mutex_unlock(&s->mtx);
    if (refused) {
        /* The thread is on its way out and never set `running`, so nothing
         * can be in the lock; join it before the objects it used go away.
         * w->error already holds the hook's message. */
        pthread_join(s->thread, NULL);
        pthread_cond_destroy(&s->cv);
        pthread_mutex_destroy(&s->mtx);
        return false;
    }
    psyrt__flag_store(&w->running, 1);
    return true;
}

void psyrt_worker_stop(psyrt_worker* w) {
    if (!w || !psyrt__flag_load(&w->running)) return;
    psyrt__wstate* s = psyrt__w(w);
    /* Clear `running` first: a submit racing this teardown (a contract
     * violation, but a cheap one to survive) must be refused rather than
     * reach for a mutex that is about to be destroyed. */
    psyrt__flag_store(&w->running, 0);
    pthread_mutex_lock(&s->mtx);
    w->quit = 1;
    pthread_cond_signal(&s->cv);
    pthread_mutex_unlock(&s->mtx);
    pthread_join(s->thread, NULL);
    pthread_cond_destroy(&s->cv);
    pthread_mutex_destroy(&s->mtx);
    w->policy = PSYRT_POLICY_NONE;
}

int psyrt_worker_submit(psyrt_worker* w, uint64_t deadline_ns,
                        psyrt_job_fn fn, void* ctx) {
    if (!w || !fn) return PSYRT_ERR_ARG;
    if (!psyrt__flag_load(&w->running)) return PSYRT_ERR_STOPPED;
    psyrt__wstate* s = psyrt__w(w);
    pthread_mutex_lock(&s->mtx);
    w->deadline_ns = deadline_ns;
    w->fn  = fn;
    w->ctx = ctx;
    w->has_job = 1;
    w->gen = psyrt__next_gen(w->gen);
    uint32_t seq = w->gen;
    psyrt__worker_wake(w);
    pthread_mutex_unlock(&s->mtx);
    return (int)seq;
}

int psyrt_worker_cancel(psyrt_worker* w) {
    if (!w) return PSYRT_ERR_ARG;
    if (!psyrt__flag_load(&w->running)) return PSYRT_ERR_STOPPED;
    psyrt__wstate* s = psyrt__w(w);
    pthread_mutex_lock(&s->mtx);
    int had = w->has_job;
    w->has_job = 0;
    w->gen = psyrt__next_gen(w->gen);
    psyrt__worker_wake(w);
    pthread_mutex_unlock(&s->mtx);
    return had ? 1 : 0;
}

#else /* PSYRT__WINDOWS */

static void psyrt__worker_lock(psyrt_worker* w)   { EnterCriticalSection(&psyrt__w(w)->cs); }
static void psyrt__worker_unlock(psyrt_worker* w) { LeaveCriticalSection(&psyrt__w(w)->cs); }

static void psyrt__worker_wait_idle(psyrt_worker* w) {
    psyrt__wstate* s = psyrt__w(w);
    SleepConditionVariableCS(&s->cv, &s->cs, INFINITE);
}

/* Wait until `until_ns` or until a submit wakes us. The condition variable
 * cannot be waited on together with a waitable timer, so the timed wait uses
 * the timer plus an auto-reset event that every submit sets. A submit that
 * arrives while the worker is between the unlock and the wait leaves the
 * event signaled, so a wakeup is never lost; the cost is one spurious loop. */
static void psyrt__worker_wait_until(psyrt_worker* w, uint64_t until_ns) {
    psyrt__wstate* s = psyrt__w(w);
    psyrt__worker_unlock(w);
    uint64_t now = psyrt_now_ns();
    if (until_ns > now) {
        uint64_t delta = until_ns - now;
        LARGE_INTEGER due; /* negative = relative, 100 ns units */
        due.QuadPart = -(LONGLONG)(delta / 100ull);
        if (s->timer && SetWaitableTimer(s->timer, &due, 0, NULL, NULL, FALSE)) {
            HANDLE h[2];
            h[0] = s->timer;
            h[1] = s->wakeup;
            (void)WaitForMultipleObjects(2, h, FALSE, INFINITE);
        } else {
            (void)WaitForSingleObject(s->wakeup, (DWORD)(delta / 1000000ull));
        }
    }
    psyrt__worker_lock(w);
}

static void psyrt__worker_wake(psyrt_worker* w) {
    psyrt__wstate* s = psyrt__w(w);
    WakeConditionVariable(&s->cv); /* an idle worker */
    SetEvent(s->wakeup);           /* a worker in a timed wait */
}

static void psyrt__worker_loop(psyrt_worker* w);

static DWORD WINAPI psyrt__worker_main(LPVOID arg) {
    psyrt_worker* w = (psyrt_worker*)arg;
    psyrt_policy pol = w->no_elevate ? PSYRT_POLICY_NORMAL
                                     : psyrt_thread_elevate(&w->sched);
    /* See the POSIX twin: after the elevation, before the handshake. */
    bool ok = psyrt__worker_on_start(w);
    psyrt__worker_lock(w);
    /* Publish the rung and release psyrt_worker_start(). */
    w->policy = ok ? pol : PSYRT_POLICY_NONE;
    w->start_failed = ok ? 0 : 1;
    w->ready = 1;
    WakeAllConditionVariable(&psyrt__w(w)->cv);
    if (ok) psyrt__worker_loop(w);
    psyrt__worker_unlock(w);
    return 0;
}

bool psyrt_worker_start(psyrt_worker* w, const psyrt_worker_desc* desc) {
    if (!w) return false;
    psyrt_worker_desc d;
    memset(&d, 0, sizeof(d));
    if (desc) d = *desc;
    if (psyrt__flag_load(&w->running)) return true;

    memset(w, 0, sizeof(*w));
    w->sched = d.sched;
    if (!psyrt_sched_normalize(&w->sched)) {
        snprintf(w->error, sizeof(w->error),
                 "invalid desc.sched: require 0 < runtime_ns <= deadline_ns <= "
                 "period_ns (period_ns 0 means same as deadline_ns)");
        return false;
    }
    w->spin_ns    = d.spin_ns ? d.spin_ns : PSYRT_DEFAULT_SPIN_NS;
    w->no_elevate = d.no_elevate;
    w->on_start   = d.on_start;
    w->start_ctx  = d.start_ctx;
    w->policy     = PSYRT_POLICY_NONE;

    psyrt__wstate* s = psyrt__w(w);
    InitializeCriticalSection(&s->cs);
    InitializeConditionVariable(&s->cv);
    s->wakeup = CreateEventA(NULL, FALSE, FALSE, NULL);
    s->timer  = psyrt__timer_create(&s->hires);
    if (!s->wakeup || !s->timer) {
        snprintf(w->error, sizeof(w->error),
                 "worker: timer/event create failed (err %lu)",
                 (unsigned long)GetLastError());
        if (s->wakeup) CloseHandle(s->wakeup);
        if (s->timer)  CloseHandle(s->timer);
        DeleteCriticalSection(&s->cs);
        return false;
    }
    s->thread = CreateThread(NULL, 0, psyrt__worker_main, w, 0, NULL);
    if (!s->thread) {
        snprintf(w->error, sizeof(w->error), "worker: thread create failed (err %lu)",
                 (unsigned long)GetLastError());
        CloseHandle(s->wakeup);
        CloseHandle(s->timer);
        DeleteCriticalSection(&s->cs);
        return false;
    }
    EnterCriticalSection(&s->cs);
    while (!w->ready) SleepConditionVariableCS(&s->cv, &s->cs, INFINITE);
    int refused = w->start_failed;
    LeaveCriticalSection(&s->cs);
    if (refused) {
        /* See the POSIX twin: join before the objects go away. w->error
         * already holds the hook's message. */
        WaitForSingleObject(s->thread, INFINITE);
        CloseHandle(s->thread);
        CloseHandle(s->timer);
        CloseHandle(s->wakeup);
        DeleteCriticalSection(&s->cs);
        return false;
    }
    psyrt__flag_store(&w->running, 1);
    return true;
}

void psyrt_worker_stop(psyrt_worker* w) {
    if (!w || !psyrt__flag_load(&w->running)) return;
    psyrt__wstate* s = psyrt__w(w);
    /* Clear `running` first; see the POSIX twin. */
    psyrt__flag_store(&w->running, 0);
    EnterCriticalSection(&s->cs);
    w->quit = 1;
    LeaveCriticalSection(&s->cs);
    WakeAllConditionVariable(&s->cv);
    SetEvent(s->wakeup);
    WaitForSingleObject(s->thread, INFINITE);
    CloseHandle(s->thread);
    CloseHandle(s->timer);
    CloseHandle(s->wakeup);
    DeleteCriticalSection(&s->cs);
    w->policy = PSYRT_POLICY_NONE;
}

int psyrt_worker_submit(psyrt_worker* w, uint64_t deadline_ns,
                        psyrt_job_fn fn, void* ctx) {
    if (!w || !fn) return PSYRT_ERR_ARG;
    if (!psyrt__flag_load(&w->running)) return PSYRT_ERR_STOPPED;
    psyrt__wstate* s = psyrt__w(w);
    EnterCriticalSection(&s->cs);
    w->deadline_ns = deadline_ns;
    w->fn  = fn;
    w->ctx = ctx;
    w->has_job = 1;
    w->gen = psyrt__next_gen(w->gen);
    uint32_t seq = w->gen;
    LeaveCriticalSection(&s->cs);
    psyrt__worker_wake(w);
    return (int)seq;
}

int psyrt_worker_cancel(psyrt_worker* w) {
    if (!w) return PSYRT_ERR_ARG;
    if (!psyrt__flag_load(&w->running)) return PSYRT_ERR_STOPPED;
    psyrt__wstate* s = psyrt__w(w);
    EnterCriticalSection(&s->cs);
    int had = w->has_job;
    w->has_job = 0;
    w->gen = psyrt__next_gen(w->gen);
    LeaveCriticalSection(&s->cs);
    psyrt__worker_wake(w);
    return had ? 1 : 0;
}

#endif /* platform */

uint32_t psyrt_worker_pending(const psyrt_worker* w) {
    if (!w || !psyrt__flag_load(&w->running)) return 0;
    /* The lock is what makes the pair (has_job, gen) consistent, and taking
     * it is a mutation the const in the signature cannot express: the handle
     * the CALLER sees is unchanged. */
    psyrt_worker* m = (psyrt_worker*)(uintptr_t)w;
    psyrt__worker_lock(m);
    uint32_t seq = m->has_job ? m->gen : 0u;
    psyrt__worker_unlock(m);
    return seq;
}

/* The loop itself is platform-independent; only the four wait/wake helpers
 * above differ. Entered and left with the lock held. */
static void psyrt__worker_loop(psyrt_worker* w) {
    while (!w->quit) {
        if (!w->has_job) {
            psyrt__worker_wait_idle(w);
            continue;
        }
        uint64_t deadline = w->deadline_ns;
        uint32_t gen = w->gen;
        uint64_t spin = w->spin_ns;
#if defined(PSYRT__WINDOWS)
        /* Without a high-resolution timer the coarse wait cannot land closer
         * than a system tick, so the last tick is spun. See psyrt_sleep_until. */
        if (!psyrt__w(w)->hires && spin < PSYRT__TICK_NS) spin = PSYRT__TICK_NS;
#endif
        uint64_t now = psyrt_now_ns();
        uint64_t wake = (deadline > spin) ? deadline - spin : 0;
        if (now < wake) {
            /* Wakes on the deadline OR on a submit, then re-evaluates from
             * the top, so a replacement job takes effect at once whether its
             * deadline is earlier or later than the one it replaced. */
            psyrt__worker_wait_until(w, wake);
            continue;
        }
        if (now < deadline) {
            /* The lock is released for the spin so a submit from another
             * thread does not block behind it; the generation check is what
             * makes that safe. */
            psyrt__worker_unlock(w);
            psyrt_spin_until(deadline);
            psyrt__worker_lock(w);
            if (w->quit || !w->has_job || w->gen != gen) continue;
        }
        psyrt__worker_run(w, false);
    }
    /* Run a pending job immediately rather than waiting out its deadline:
     * psyrt_worker_stop() is tearing the session down, and work that has to
     * happen (a trailing edge, a cleanup write) is better early than never. */
    if (w->has_job) psyrt__worker_run(w, true);
}

#endif /* PSYRT_NO_THREADS */

#endif /* PSY_RT_IMPLEMENTATION_GUARD */
#endif /* PSY_RT_IMPLEMENTATION */

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
