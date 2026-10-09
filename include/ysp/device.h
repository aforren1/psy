/* ysp/device.h - v0.1.0 - public domain single-header device layer
 *
 *   A response box, a trigger box or a microcontroller board as a producer
 *   of ysp/input.h events with times on the ysp_rt clock: one instance per
 *   physical device, each with its own reader thread, a decoder from
 *   ysp/box.h, a device clock fit from ysp/rt.h, timer queries for
 *   bracketed pairs, identity (a match key and the device's own answer),
 *   and a lifecycle that survives an unplug: the device is found again by
 *   its key, the fit restarts, and the gap is in the log.
 *   docs/devices_spec.md is the plan; this is its step 2.
 *
 *   REQUIRES ysp/rt.h, ysp/input.h, ysp/box.h and ysp/serial.h beside it;
 *   this header includes them, and its implementation implements ysp/rt.h,
 *   ysp/box.h and ysp/serial.h unless the translation unit already does.
 *   Windows, Linux and macOS. C99 is the floor: it builds as C99, C11 and
 *   C++17.
 *
 *   ---------------------------------------------------------------------
 *   CHANGELOG
 *   ---------------------------------------------------------------------
 *   v0.1.0 - first version: XID, ysp line protocol and photodiode frame
 *          devices on the serial transport (or the caller's), a reader
 *          thread or manual polling, BRACKET fits with timer queries,
 *          match keys, identify, reconnect, records in a ysp/rt.h ring.
 *
 *   STATUS: v0.1.0, 2026-10-08. Built with MSVC 19.44 (/W4 /WX, C11 and
 *   C++17), MinGW-w64 gcc 16.1 and gcc 11.4 on WSL2 (C99, C11, C++17,
 *   -Werror). tests/adapt/device_test.c runs simulated devices through a
 *   fake transport on a virtual clock (manual mode) and on a real thread:
 *   identify, events mapped through the fit, timer queries and their
 *   widths, an unplug with a clock reset (LOST, the gap record, the fit
 *   restarted, RUNNING again), silence, a wrong device, the match keys.
 *   tests/loopback/device_loopback.c runs the serial transport against a
 *   socat pty pair on WSL2. NO REAL DEVICE has been opened: no box or
 *   board was attached. docs/device.md has the numbers.
 *
 *   ---------------------------------------------------------------------
 *   USAGE
 *   ---------------------------------------------------------------------
 *       #define YSP_DEVICE_IMPLEMENTATION
 *       #include "ysp/device.h"
 *
 *       static void to_bridge(void* ctx, const yin_event* e) {
 *           (void)ctx; yscr_push_input(e);              // ysp/screen.h
 *       }
 *       static ydev_device photo;                       // about 44 KB
 *       ydev_desc d = { 0 };
 *       d.role = "photo";
 *       d.family = YBOX_LINE;
 *       d.key = "serial:16C0:0483::";                   // any Teensy
 *       d.device = 1;
 *       d.kind = YIN_KIND_SYNC;
 *       d.sink = to_bridge;
 *       d.ring = &log_ring;                             // optional
 *       if (!ydev_start(&photo, &d)) die(ydev_error(&photo));
 *       ...
 *       ydev_stop(&photo);
 *
 *   ---------------------------------------------------------------------
 *   MATCH KEYS (desc.key)
 *   ---------------------------------------------------------------------
 *   A key names a physical device so that it is found again after a
 *   replug and a reboot, whatever port name the OS gives it:
 *     serial:<vid>:<pid>:<serial>:<location>
 *   vid and pid in hex, the USB serial number and the port's location as
 *   ysp/serial.h lists them (yser_port_info). In a desc.key an empty or
 *   missing field matches anything, case-insensitively:
 *   "serial:16C0:0483::" is any Teensy in serial mode, "serial:0403:6001:
 *   FT4ABC12" one FTDI cable by its serial number. Cedrus ships FTDI's
 *   default vid and pid, so an XID box needs its serial number to be told
 *   from another FTDI cable. "port:COM5" or "port:/dev/ttyACM0" names a
 *   port instead, which a replug can change. ydev_port_key() writes the
 *   key of a listed port; ydev_key_match() is the rule.
 *
 *   ---------------------------------------------------------------------
 *   LIFECYCLE
 *   ---------------------------------------------------------------------
 *     OPENING  looking for the key; every retry_ns (1 s) until it opens
 *     RUNNING  identified, reading
 *     LOST     the transport failed (unplugged) or nothing arrived for
 *              silent_ns (2 s); reopened by key every retry_ns
 *     FAILED   the device at the key did not answer as the family must
 *              (identify): a wrong device. No retry; stop and start again
 *     CLOSED   stopped
 *   IDENTIFY. XID: "_c1" must answer "_xid0" (a box in another mode is
 *   switched to XID mode with "c10", as pyxid2 does, and the log says so),
 *   then "_d2", "_d3", "_d4" give the product, model and firmware. LINE:
 *   "i" must answer an "I" line within 1.5 s (asked three times, for a
 *   board that resets when the port opens). PHOTO: none. desc.no_identify
 *   skips it.
 *   Each open starts a new fit: a device's clock can reset on power-up.
 *   Events of a LOST interval are lost; the GAP record gives the last time
 *   a byte arrived before and the time the device ran again.
 *
 *   ---------------------------------------------------------------------
 *   TIME
 *   ---------------------------------------------------------------------
 *   Every frame that carries the device's time is a pair for ysp/rt.h's
 *   fit in BRACKET mode: a frame the device sent on its own has width 0 (it
 *   is late, never early), and the answer to a timer query sent at t0 and
 *   read at t1 has width t1 - t0. Queries go every probe_ns (100 ms): the
 *   fit then has pairs between a box's rare presses (docs/rt.md: sparse
 *   presses behind a 16 ms latency timer gave millisecond errors for
 *   minutes). Brackets wider than max_width_ns are refused: 1.3 ms for
 *   LINE (native USB), 20 ms for XID, whose answers wait in the FTDI
 *   chip's latency timer (16 ms by default). An XID query writes "_e", waits
 *   1 ms and stamps t0 before the "5" the box answers. Each event's t is
 *   the fit's map of its device time, never later than its read time (a
 *   map past it is clamped and counted); unc_us is the device's tick plus
 *   the widest bracket the map rests on (without brackets, the fit's
 *   spread): a bound from the fit's own evidence, not a measurement; ticks
 *   holds the device time. Before the first pair maps, t is the read
 *   time. The fit's own CLOCK and FIT records go to desc.ring with
 *   clock_id = desc.device.
 *   ydev_source() gives the source entry: tier UNKNOWN, because no loopback
 *   has checked any device on this layer (docs/devices_spec.md, 12).
 *
 *   ---------------------------------------------------------------------
 *   THREADS, MANUAL MODE AND RECORDS
 *   ---------------------------------------------------------------------
 *   ydev_start() starts one reader thread for the instance, elevated with
 *   yrt_thread_elevate() unless desc.no_elevate. It blocks in the
 *   transport's read for at most read_timeout_ms (20 ms) and calls
 *   desc.sink on that thread for each event: a sink must be quick and
 *   thread-safe; yscr_push_input() is. ydev_stop() asks the transport to
 *   interrupt, joins the thread and closes the port.
 *   desc.manual: no thread. The caller calls ydev_poll() (one step: open,
 *   identify, read, decode, sink) from its own loop. With desc.now the
 *   instance reads that clock instead of yrt_now_ns() and does not wait
 *   the 1 ms between XID bytes: for tests and replays.
 *   desc.ring gets records of source YRT_SRC_DEVICE, aux = desc.device:
 *   YDEV_REC_STATE (u32[0] old, u32[1] new state, u32[2] YDEV_WHY_*,
 *   u32[3] the instance's open count), YDEV_REC_GAP (i64[0] the last byte
 *   before the loss, i64[1] running again), YDEV_REC_TEXT (39 characters:
 *   "role ...", "port ...", "ident ..."), YDEV_REC_GARBAGE (u64[0] the
 *   decoder's total, at most one a second).
 *   Nothing allocates after ydev_start(); the instance holds its buffers
 *   and the fit (about 44 KB: make it static or allocate it once).
 *
 *   ---------------------------------------------------------------------
 *   LICENSE: public domain / MIT-0, see end of file.
 */
#ifndef YSP_DEVICE_H_INCLUDED
#define YSP_DEVICE_H_INCLUDED

#define YDEV_VERSION_MAJOR 0
#define YDEV_VERSION_MINOR 1
#define YDEV_VERSION_PATCH 0
#define YDEV_VERSION_STRING "0.1.0"

#include "ysp/rt.h"
#include "ysp/input.h"
#include "ysp/box.h"
#include "ysp/serial.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef YDEV_API
#define YDEV_API extern
#endif

typedef enum ydev_lifecycle {
    YDEV_CLOSED  = 0,
    YDEV_OPENING = 1,
    YDEV_RUNNING = 2,
    YDEV_LOST    = 3,
    YDEV_FAILED  = 4
} ydev_lifecycle;

/* Why a state changed (YDEV_REC_STATE's u32[2]). */
#define YDEV_WHY_START        1u
#define YDEV_WHY_IDENTIFIED   2u
#define YDEV_WHY_DISCONNECTED 3u
#define YDEV_WHY_SILENT       4u
#define YDEV_WHY_WRONG_DEVICE 5u
#define YDEV_WHY_STOP         6u

/* Record kinds under YRT_SRC_DEVICE. */
#define YDEV_REC_STATE   1u
#define YDEV_REC_GAP     2u
#define YDEV_REC_TEXT    3u
#define YDEV_REC_GARBAGE 4u

/* How bytes move. Every function is called on the reader thread except
 * interrupt (from ydev_stop()). */
typedef struct ydev_transport {
    /* Find and open the device at key; NULL when it is not there (the
     * instance tries again later). found: a name for the log. */
    void* (*open)(void* user, const char* key, char* found, size_t found_cap);
    /* > 0 bytes, 0 after timeout_ms with none, < 0 the device is gone. */
    int   (*read)(void* conn, uint8_t* buf, int cap, int timeout_ms);
    /* n, or < 0 the device is gone. */
    int   (*write)(void* conn, const uint8_t* buf, int n);
    void  (*interrupt)(void* conn);       /* end a blocked read; may be NULL */
    void  (*close)(void* conn);
    void* user;
} ydev_transport;

typedef void (*ydev_sink)(void* ctx, const yin_event* e);

/* Zero-initialize, then set role, family, key, device and sink. */
typedef struct ydev_desc {
    const char*    role;            /* the experiment's name for it; logged     */
    int            family;          /* YBOX_XID, YBOX_LINE, YBOX_PHOTO          */
    const char*    key;             /* the match key (MATCH KEYS)               */
    uint32_t       device;          /* yin_event.device of its events, > 0      */
    int            kind;            /* yin_event.kind; 0 = the family's: BOX for
                                     * XID and LINE, SYNC for PHOTO            */
    ydev_sink      sink;            /* called for each event                   */
    void*          sink_ctx;
    yrt_ring*      ring;            /* records; NULL = none                    */
    ydev_transport transport;       /* all NULL = ysp/serial.h                 */
    uint32_t       baud;            /* serial: 0 = 115200                      */
    int64_t        probe_ns;        /* timer queries: 0 = 100 ms; < 0 = none   */
    int64_t        retry_ns;        /* reopen: 0 = 1 s                         */
    int64_t        silent_ns;       /* LOST after this long with no byte:
                                     * 0 = 2 s; < 0 = never                    */
    int64_t        max_width_ns;    /* 0 = 1.3 ms (LINE), 20 ms (XID)          */
    int            read_timeout_ms; /* 0 = 20                                  */
    bool           manual;          /* no thread: call ydev_poll()             */
    bool           no_identify;
    bool           no_elevate;
    int64_t      (*now)(void* ctx); /* the clock; NULL = yrt_now_ns()          */
    void*          now_ctx;
} ydev_desc;

typedef struct ydev_stats {
    int           state;            /* ydev_lifecycle                          */
    uint32_t      opens;            /* successful opens (the first and every
                                     * reconnect)                              */
    uint64_t      reads, bytes, events, pairs, probes, answers, refused;
    uint64_t      garbage;          /* bytes that fit no frame                 */
    uint64_t      clamped;          /* events whose map passed their read time */
    int64_t       last_rx_ns;       /* the last byte                           */
    yrt_fit_info  fit;
    char          found[64];        /* the port that matched                   */
    char          ident[96];        /* what the device said it is              */
} ydev_stats;

#define YDEV_EVENTS 64

typedef struct ydev_device {
    ydev_desc      d;
    ydev_transport tr;
    void*          conn;
    yser_port      port;            /* the serial transport's connection       */
    ybox_decoder   dec;
    yrt_fit        fit;
    yin_event      ev[YDEV_EVENTS];
    ybox_pair      pair[YDEV_EVENTS];
    uint8_t        rx[512];
    ydev_stats     st;              /* the reader's; copied under the lock     */
    ydev_stats     snap;
    int64_t        next_probe, probe_t0, next_try, lost_at, last_garbage_rec;
    uint32_t       probe_seq, probe_pending;
    int            state;
    int            stop;            /* written and read under the lock         */
    int            started, have_thread;
    union { unsigned char b[64]; void* p; int64_t i; double f; } thread_mem, lock_mem;
    char           err[160];
} ydev_device;

YDEV_API const char* ydev_version(void);

/* Validate desc and start (a thread, or manual mode). false with a message
 * in ydev_error() for a bad desc or a thread that did not start. The device
 * need not be there: the instance looks for it (OPENING). */
YDEV_API bool ydev_start(ydev_device* dev, const ydev_desc* desc);

/* Stop the thread, close the port, write CLOSED. Safe on a zeroed or
 * stopped instance. */
YDEV_API void ydev_stop(ydev_device* dev);

/* Manual mode: one step, reading for at most timeout_ms. Returns the state. */
YDEV_API int  ydev_poll(ydev_device* dev, int timeout_ms);

YDEV_API int  ydev_state(ydev_device* dev);
YDEV_API void ydev_get_stats(ydev_device* dev, ydev_stats* out);
YDEV_API const char* ydev_error(const ydev_device* dev);
YDEV_API const char* ydev_state_name(int state);

/* The source entry (ysp/input.h SOURCES) of the instance's events. */
YDEV_API yin_source ydev_source(const ydev_device* dev);

/* The key of a listed port: "serial:<VID>:<PID>:<serial>:<location>". */
YDEV_API int  ydev_port_key(const yser_port_info* p, char* out, size_t cap);

/* Whether key matches pattern (MATCH KEYS). */
YDEV_API bool ydev_key_match(const char* pattern, const char* key);

/* The serial transport (the default), for a caller who wraps it. user is
 * the instance. */
YDEV_API ydev_transport ydev_serial_transport(ydev_device* dev);

#ifdef __cplusplus
}
#endif

#endif /* YSP_DEVICE_H_INCLUDED */

/* ======================================================================= *
 *                            IMPLEMENTATION                               *
 * ======================================================================= */
#ifdef YSP_DEVICE_IMPLEMENTATION
#ifndef YSP_DEVICE_IMPLEMENTATION_GUARD
#define YSP_DEVICE_IMPLEMENTATION_GUARD

#ifndef YSP_SERIAL_IMPLEMENTATION_GUARD
    #define YSP_SERIAL_IMPLEMENTATION
    #include "ysp/serial.h"          /* implements ysp/rt.h too, unless done */
#endif
#ifndef YSP_BOX_IMPLEMENTATION_GUARD
    #define YSP_BOX_IMPLEMENTATION
    #include "ysp/box.h"
#endif

#include <string.h>
#include <stdio.h>
#include <ctype.h>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <pthread.h>
#endif

/* --- the lock and the thread ---------------------------------------------------- */

#if defined(_WIN32)
typedef CRITICAL_SECTION ydev__mutex;
static void ydev__lock_init(ydev__mutex* m)  { InitializeCriticalSection(m); }
static void ydev__lock_free(ydev__mutex* m)  { DeleteCriticalSection(m); }
static void ydev__lock(ydev__mutex* m)       { EnterCriticalSection(m); }
static void ydev__unlock(ydev__mutex* m)     { LeaveCriticalSection(m); }
#else
typedef pthread_mutex_t ydev__mutex;
static void ydev__lock_init(ydev__mutex* m)  { pthread_mutex_init(m, NULL); }
static void ydev__lock_free(ydev__mutex* m)  { pthread_mutex_destroy(m); }
static void ydev__lock(ydev__mutex* m)       { pthread_mutex_lock(m); }
static void ydev__unlock(ydev__mutex* m)     { pthread_mutex_unlock(m); }
#endif
/* The lock lives in the instance (no allocation): 40 bytes on Windows and
 * Linux, 64 on macOS. */
typedef char ydev__lock_fits[sizeof(ydev__mutex) <= 64 ? 1 : -1];

static ydev__mutex* ydev__mx(ydev_device* dev) { return (ydev__mutex*)(void*)dev->lock_mem.b; }

static bool ydev__stopping(ydev_device* dev) {
    int s;
    ydev__lock(ydev__mx(dev));
    s = dev->stop;
    ydev__unlock(ydev__mx(dev));
    return s != 0;
}

YDEV_API const char* ydev_version(void) { return YDEV_VERSION_STRING; }

YDEV_API const char* ydev_state_name(int state) {
    switch (state) {
    case YDEV_CLOSED:  return "closed";
    case YDEV_OPENING: return "opening";
    case YDEV_RUNNING: return "running";
    case YDEV_LOST:    return "lost";
    case YDEV_FAILED:  return "failed";
    default:           return "?";
    }
}

static int64_t ydev__now(const ydev_device* dev) {
    return dev->d.now ? dev->d.now(dev->d.now_ctx) : (int64_t)yrt_now_ns();
}

/* --- keys ---------------------------------------------------------------------- */

YDEV_API int ydev_port_key(const yser_port_info* p, char* out, size_t cap) {
    char tmp[256];          /* holds the longest key: 7 + 4 + 4 + 63 + 31 + 3 */
    int n;
    if (!p || !out || cap == 0) return -1;
    n = snprintf(tmp, sizeof tmp, "serial:%04X:%04X:%s:%s", (unsigned)p->vid, (unsigned)p->pid,
                 p->serial_number, p->location);
    if (n < 0 || (size_t)n >= cap) { out[0] = '\0'; return -1; }
    memcpy(out, tmp, (size_t)n + 1);
    return n;
}

/* One ':'-separated field of s starting at *i, into f (cut to cap). */
static void ydev__field(const char* s, size_t* i, char* f, size_t cap) {
    size_t n = 0;
    while (s[*i] && s[*i] != ':') {
        if (n + 1 < cap) f[n++] = s[*i];
        (*i)++;
    }
    if (s[*i] == ':') (*i)++;
    f[n] = '\0';
}

static bool ydev__ieq(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

YDEV_API bool ydev_key_match(const char* pattern, const char* key) {
    size_t i = 0, j = 0;
    int k;
    if (!pattern || !key) return false;
    if (strncmp(pattern, "port:", 5) == 0) return strncmp(key, "port:", 5) == 0 && strcmp(pattern + 5, key + 5) == 0;
    if (strncmp(pattern, "serial:", 7) != 0 || strncmp(key, "serial:", 7) != 0) return false;
    i = 7; j = 7;
    for (k = 0; k < 4; k++) {
        char fp[96], fk[96];
        ydev__field(pattern, &i, fp, sizeof fp);
        ydev__field(key, &j, fk, sizeof fk);
        if (fp[0] && !ydev__ieq(fp, fk)) return false;
    }
    return true;
}

/* --- the serial transport ------------------------------------------------------- */

static void* ydev__ser_open(void* user, const char* key, char* found, size_t found_cap) {
    ydev_device* dev = (ydev_device*)user;
    yser_port_info info[32];
    yser_desc sd;
    char k[256];
    const char* name = NULL;
    int n, i;
    if (strncmp(key, "port:", 5) == 0) {
        name = key + 5;
    } else {
        n = yser_list_ports(info, 32);
        for (i = 0; i < n && !name; i++)
            if (ydev_port_key(&info[i], k, sizeof k) > 0 && ydev_key_match(key, k)) name = info[i].name;
    }
    if (!name || !name[0]) return NULL;
    memset(&sd, 0, sizeof sd);
    sd.device = name;
    sd.baud = dev->d.baud ? dev->d.baud : 115200u;
    sd.low_latency = true;
    sd.write_timeout_ms = 100;
    if (!yser_open(&dev->port, &sd)) return NULL;
    snprintf(found, found_cap, "%s", name);
    return &dev->port;
}

static int ydev__ser_read(void* conn, uint8_t* buf, int cap, int timeout_ms) {
    int n = yser_read((yser_port*)conn, buf, cap, (uint32_t)timeout_ms, YSER_READ_ANY);
    if (n == YSER_ERR_INTERRUPTED) return 0;
    return n;
}

static int ydev__ser_write(void* conn, const uint8_t* buf, int n) {
    return yser_write((yser_port*)conn, buf, n);
}

static void ydev__ser_interrupt(void* conn) { (void)yser_interrupt((yser_port*)conn); }
static void ydev__ser_close(void* conn) { yser_close((yser_port*)conn); }

YDEV_API ydev_transport ydev_serial_transport(ydev_device* dev) {
    ydev_transport t;
    t.open = ydev__ser_open;
    t.read = ydev__ser_read;
    t.write = ydev__ser_write;
    t.interrupt = ydev__ser_interrupt;
    t.close = ydev__ser_close;
    t.user = dev;
    return t;
}

/* --- records -------------------------------------------------------------------- */

static void ydev__rec(ydev_device* dev, uint16_t kind, const yrt_payload* u) {
    yrt_event ev;
    if (!dev->d.ring) return;
    memset(&ev, 0, sizeof ev);
    ev.t_ns = (uint64_t)ydev__now(dev);
    ev.source = (uint16_t)YRT_SRC_DEVICE;
    ev.kind = kind;
    ev.aux = dev->d.device;
    ev.u = *u;
    (void)yrt_ring_push(dev->d.ring, &ev);
}

static void ydev__text(ydev_device* dev, const char* what, const char* text) {
    yrt_payload u;
    memset(&u, 0, sizeof u);
    snprintf(u.text, sizeof u.text, "%s %s", what, text ? text : "");
    ydev__rec(dev, (uint16_t)YDEV_REC_TEXT, &u);
}

static void ydev__set_state(ydev_device* dev, int s, uint32_t why) {
    yrt_payload u;
    int old = dev->state;
    if (old == s) return;
    memset(&u, 0, sizeof u);
    u.u32[0] = (uint32_t)old;
    u.u32[1] = (uint32_t)s;
    u.u32[2] = why;
    u.u32[3] = dev->st.opens;
    ydev__rec(dev, (uint16_t)YDEV_REC_STATE, &u);
    dev->state = s;
    dev->st.state = s;
}

static void ydev__publish(ydev_device* dev) {
    yrt_fit_get(&dev->fit, &dev->st.fit);
    ydev__lock(ydev__mx(dev));
    dev->snap = dev->st;
    ydev__unlock(ydev__mx(dev));
}

/* --- writing ------------------------------------------------------------------------ */

/* XID commands go one byte at a time, 1 ms apart, as pyxid2 writes them.
 * *t_last gets the time just before the last byte: the start of a bracket. */
static int ydev__write(ydev_device* dev, const uint8_t* b, int n, int64_t* t_last) {
    int i;
    if (!ybox_slow_write(dev->d.family)) {
        if (t_last) *t_last = ydev__now(dev);
        return dev->tr.write(dev->conn, b, n) == n ? n : -1;
    }
    for (i = 0; i < n; i++) {
        if (i > 0 && !dev->d.now) (void)yrt_sleep_ns(YBOX_SLOW_WRITE_NS);
        if (i == n - 1 && t_last) *t_last = ydev__now(dev);
        if (dev->tr.write(dev->conn, b + i, 1) != 1) return -1;
    }
    return n;
}

/* --- identify ------------------------------------------------------------------- */

/* Reads until bytes arrive or ms pass (in reads of 10 ms, so a transport on
 * a virtual clock advances it), appending to acc; false when the device is
 * gone. */
static bool ydev__gather(ydev_device* dev, char* acc, size_t cap, size_t* len, int ms) {
    int64_t end = ydev__now(dev) + (int64_t)ms * 1000000;
    do {
        uint8_t b[128];
        int n = dev->tr.read(dev->conn, b, (int)sizeof b, 10);
        if (n < 0) return false;
        if (n > 0) {
            dev->st.last_rx_ns = ydev__now(dev);
            if (*len + (size_t)n >= cap) n = (int)(cap - 1 - *len);
            memcpy(acc + *len, b, (size_t)n);
            *len += (size_t)n;
            acc[*len] = '\0';
            return true;
        }
    } while (ydev__now(dev) < end && !ydev__stopping(dev));
    return true;
}

static const char* ydev__find(const char* acc, size_t len, const char* what) {
    size_t i, m = strlen(what);
    for (i = 0; i + m <= len; i++) if (memcmp(acc + i, what, m) == 0) return acc + i;
    return NULL;
}

/* XID: "_c1" answered "_xid<mode>"; the mode set to 0 if it is not; the
 * product, model and firmware asked. 1 done, -2 gone. */
static int ydev__xid_details(ydev_device* dev, char mode) {
    static const char* const cmd[3] = { "_d2", "_d3", "_d4" };
    char acc[64], id[3];
    size_t len;
    int k;
    if (mode != '0') {
        static const uint8_t c10[3] = { 'c', '1', '0' };
        if (ydev__write(dev, c10, 3, NULL) < 0) return -2;
        ydev__text(dev, "xid mode", "switched to 0");
    }
    for (k = 0; k < 3; k++) {
        len = 0;
        acc[0] = '\0';
        if (ydev__write(dev, (const uint8_t*)cmd[k], 3, NULL) < 0) return -2;
        if (!ydev__gather(dev, acc, sizeof acc, &len, 500)) return -2;
        id[k] = len ? acc[0] : '?';
    }
    snprintf(dev->st.ident, sizeof dev->st.ident, "xid product %c model %c firmware %c%s", id[0], id[1], id[2],
             mode != '0' ? " (was mode 1 or 2)" : "");
    return 1;
}

/* 1 identified, -1 a wrong device, -2 gone. Asks three times, 500 ms each:
 * a board that resets when its port opens needs a moment. In manual mode
 * the poll that opens the device blocks here, at most 1.5 s. */
static int ydev__identify(ydev_device* dev) {
    char acc[512];
    uint8_t q[8];
    int tries, nq;
    if (dev->d.no_identify || dev->d.family == YBOX_PHOTO) return 1;
    nq = ybox_identify(dev->d.family, q, (int)sizeof q);
    for (tries = 0; tries < 3 && !ydev__stopping(dev); tries++) {
        size_t len = 0;
        int64_t end;
        acc[0] = '\0';
        if (ydev__write(dev, q, nq, NULL) < 0) return -2;
        end = ydev__now(dev) + 500000000;
        while (ydev__now(dev) < end && !ydev__stopping(dev)) {
            if (!ydev__gather(dev, acc, sizeof acc, &len, (int)((end - ydev__now(dev)) / 1000000) + 1)) return -2;
            if (dev->d.family == YBOX_XID) {
                const char* x = ydev__find(acc, len, "_xid");
                if (x && (size_t)(x - acc) + 5 <= len) return ydev__xid_details(dev, x[4]);
            } else {
                /* LINE: the decoder finds the "I" line among syncs and edges */
                ybox_decoder tmp;
                ybox_out o;
                yin_event e[8];
                ybox_pair p[8];
                size_t used = 0;
                ybox_init(&tmp, YBOX_LINE, YIN_KIND_BOX, 0);
                memset(&o, 0, sizeof o);
                o.ev = e; o.ev_cap = 8; o.pair = p; o.pair_cap = 8;
                while (used < len) {
                    size_t u;
                    ybox_out_clear(&o);
                    u = ybox_decode(&tmp, (const uint8_t*)acc + used, len - used, 0, &o);
                    if (o.has_text) {
                        snprintf(dev->st.ident, sizeof dev->st.ident, "%s", o.text);
                        return 1;
                    }
                    if (u == 0) break;
                    used += u;
                }
            }
            if (len + 1 >= sizeof acc) break;
        }
    }
    return -1;
}

/* --- the step ----------------------------------------------------------------------- */

static void ydev__close_conn(ydev_device* dev) {
    void* c;
    ydev__lock(ydev__mx(dev));
    c = dev->conn;
    dev->conn = NULL;
    ydev__unlock(ydev__mx(dev));
    if (c) dev->tr.close(c);
}

static void ydev__lose(ydev_device* dev, uint32_t why) {
    ydev__close_conn(dev);
    dev->lost_at = dev->st.last_rx_ns ? dev->st.last_rx_ns : ydev__now(dev);
    ydev__set_state(dev, YDEV_LOST, why);
    dev->next_try = ydev__now(dev) + dev->d.retry_ns;
}

static void ydev__fit_start(ydev_device* dev) {
    yrt_fit_desc fd;
    memset(&fd, 0, sizeof fd);
    fd.mode = YRT_FIT_BRACKET;
    fd.ns_per_tick = ybox_ns_per_tick(dev->d.family);
    fd.tick_bits = 32;
    fd.max_width_ns = dev->d.max_width_ns;
    fd.clock_id = dev->d.device;
    fd.ring = dev->d.ring;
    (void)yrt_fit_init(&dev->fit, &fd);
    ybox_init(&dev->dec, dev->d.family, (uint8_t)dev->d.kind, dev->d.device);
    dev->probe_pending = 0;
}

/* OPENING or LOST: try the key once. */
static void ydev__try_open(ydev_device* dev) {
    char found[64];
    void* c;
    int r;
    int64_t now = ydev__now(dev);
    if (now < dev->next_try) return;
    dev->next_try = now + dev->d.retry_ns;
    found[0] = '\0';
    c = dev->tr.open(dev->tr.user, dev->d.key, found, sizeof found);
    if (!c) return;
    ydev__lock(ydev__mx(dev));
    dev->conn = c;
    ydev__unlock(ydev__mx(dev));
    snprintf(dev->st.found, sizeof dev->st.found, "%s", found);
    ydev__text(dev, "port", found);
    dev->st.last_rx_ns = 0;
    r = ydev__identify(dev);
    if (r == -2) { ydev__close_conn(dev); return; }
    if (r == -1) {
        ydev__close_conn(dev);
        snprintf(dev->err, sizeof dev->err, "ysp_device: %s at %s did not answer as a %s device", dev->d.role,
                 found, ybox_family_name(dev->d.family));
        ydev__set_state(dev, YDEV_FAILED, YDEV_WHY_WRONG_DEVICE);
        return;
    }
    if (dev->st.ident[0]) ydev__text(dev, "ident", dev->st.ident);
    dev->st.opens++;
    if (dev->state == YDEV_LOST) {
        yrt_payload u;
        memset(&u, 0, sizeof u);
        u.i64[0] = dev->lost_at;
        u.i64[1] = ydev__now(dev);
        ydev__rec(dev, (uint16_t)YDEV_REC_GAP, &u);
    }
    ydev__fit_start(dev);
    dev->st.last_rx_ns = ydev__now(dev);
    dev->next_probe = ydev__now(dev);
    ydev__set_state(dev, YDEV_RUNNING, YDEV_WHY_IDENTIFIED);
}

static void ydev__probe(ydev_device* dev) {
    uint8_t q[24];
    int n;
    int64_t now = ydev__now(dev), t0 = 0;
    if (dev->d.probe_ns < 0 || now < dev->next_probe) return;
    dev->next_probe = now + dev->d.probe_ns;
    if (++dev->probe_seq == 0) dev->probe_seq = 1;
    n = ybox_probe(dev->d.family, dev->probe_seq, q, (int)sizeof q);
    if (n <= 0) return;
    if (ydev__write(dev, q, n, &t0) < 0) { ydev__lose(dev, YDEV_WHY_DISCONNECTED); return; }
    dev->probe_pending = dev->d.family == YBOX_XID ? 1u : dev->probe_seq;
    dev->probe_t0 = t0;
    dev->st.probes++;
}

/* The event's uncertainty from the fit's own evidence: the device's tick
 * (an event is somewhere in its tick) plus the widest bracket the map
 * rests on; without brackets, the fit's spread (how late its points sit
 * above the map), which bounds the map more loosely. Not a measurement:
 * only a loopback gives that. */
static uint16_t ydev__unc(const ydev_device* dev, const yrt_fit_info* fi) {
    int64_t ns = (int64_t)ybox_ns_per_tick(dev->d.family) + (fi->width_ns > 0 ? fi->width_ns : fi->spread_ns);
    int64_t us = (ns + 999) / 1000;
    return (uint16_t)(us >= (int64_t)YIN_UNC_MAX ? YIN_UNC_MAX : (us < 1 ? 1 : us));
}

static void ydev__read(ydev_device* dev, int timeout_ms) {
    int n, i;
    size_t used = 0;
    int64_t t;
    ybox_out o;
    n = dev->tr.read(dev->conn, dev->rx, (int)sizeof dev->rx, timeout_ms);
    t = ydev__now(dev);
    if (n < 0) { ydev__lose(dev, YDEV_WHY_DISCONNECTED); return; }
    if (n == 0) {
        if (dev->d.silent_ns > 0 && t - dev->st.last_rx_ns > dev->d.silent_ns) ydev__lose(dev, YDEV_WHY_SILENT);
        return;
    }
    dev->st.reads++;
    dev->st.bytes += (uint64_t)n;
    dev->st.last_rx_ns = t;
    memset(&o, 0, sizeof o);
    o.ev = dev->ev; o.ev_cap = YDEV_EVENTS; o.pair = dev->pair; o.pair_cap = YDEV_EVENTS;
    while (used < (size_t)n) {
        yrt_fit_info fi;
        bool mapped;
        ybox_out_clear(&o);
        used += ybox_decode(&dev->dec, dev->rx + used, (size_t)n - used, t, &o);
        dev->st.garbage += o.garbage;
        /* the pairs first, so the events of this read map through them */
        for (i = 0; i < o.n_pair; i++) {
            int64_t w = 0;
            int rc;
            if (o.pair[i].probe && o.pair[i].probe == dev->probe_pending) {
                w = t - dev->probe_t0;
                dev->probe_pending = 0;
                dev->st.answers++;
            }
            rc = yrt_fit_add(&dev->fit, o.pair[i].ticks, t, w);
            if (rc >= 0 && (rc & YRT_FIT_REJECTED)) dev->st.refused++;
            dev->st.pairs++;
        }
        yrt_fit_get(&dev->fit, &fi);
        mapped = fi.ready;
        for (i = 0; i < o.n_ev; i++) {
            yin_event* e = &o.ev[i];
            if (mapped) {
                int64_t m = yrt_fit_map(&dev->fit, e->ticks);
                if (m > t) { m = t; dev->st.clamped++; }
                e->t = m;
                e->unc_us = ydev__unc(dev, &fi);
            }
            dev->st.events++;
            if (dev->d.sink) dev->d.sink(dev->d.sink_ctx, e);
        }
    }
    if (dev->d.ring && dev->st.garbage && t - dev->last_garbage_rec >= 1000000000) {
        yrt_payload u;
        memset(&u, 0, sizeof u);
        u.u64[0] = dev->st.garbage;
        ydev__rec(dev, (uint16_t)YDEV_REC_GARBAGE, &u);
        dev->last_garbage_rec = t;
    }
}

static void ydev__step(ydev_device* dev, int timeout_ms) {
    if (dev->state == YDEV_OPENING || dev->state == YDEV_LOST) ydev__try_open(dev);
    if (dev->state == YDEV_RUNNING && dev->conn) {
        ydev__probe(dev);
        if (dev->state == YDEV_RUNNING) ydev__read(dev, timeout_ms);
    }
    ydev__publish(dev);
}

/* --- the thread --------------------------------------------------------------------- */

static void ydev__loop(ydev_device* dev) {
    if (!dev->d.no_elevate) (void)yrt_thread_elevate(NULL);
    while (!ydev__stopping(dev)) {
        if (dev->state == YDEV_RUNNING) {
            ydev__step(dev, dev->d.read_timeout_ms);
        } else {
            ydev__step(dev, 0);
            /* not there yet, or FAILED: the retry is timed by next_try */
            if (dev->state != YDEV_RUNNING) (void)yrt_sleep_ns(20000000);
        }
    }
}

#if defined(_WIN32)
static DWORD WINAPI ydev__thread(LPVOID arg) { ydev__loop((ydev_device*)arg); return 0; }
#else
static void* ydev__thread(void* arg) { ydev__loop((ydev_device*)arg); return NULL; }
#endif

YDEV_API bool ydev_start(ydev_device* dev, const ydev_desc* desc) {
    if (!dev) return false;
    memset(dev, 0, sizeof *dev);
#define YDEV__FAIL(msg) do { snprintf(dev->err, sizeof dev->err, "ysp_device: %s", msg); return false; } while (0)
    if (!desc) YDEV__FAIL("NULL desc");
    if (!desc->role || !desc->role[0]) YDEV__FAIL("desc.role is required");
    if (desc->family < YBOX_XID || desc->family > YBOX_PHOTO) YDEV__FAIL("desc.family is not a YBOX_ family");
    if (!desc->key || !desc->key[0]) YDEV__FAIL("desc.key is required (MATCH KEYS)");
    if (strncmp(desc->key, "serial:", 7) != 0 && strncmp(desc->key, "port:", 5) != 0 && !desc->transport.open)
        YDEV__FAIL("desc.key must start with serial: or port:");
    if (desc->device == 0) YDEV__FAIL("desc.device must be > 0 (0 means unknown in ysp/input.h)");
    if (desc->kind < 0 || desc->kind > 15) YDEV__FAIL("desc.kind is not a yin_kind");
    if (desc->transport.open && (!desc->transport.read || !desc->transport.write || !desc->transport.close))
        YDEV__FAIL("desc.transport needs open, read, write and close");
    dev->d = *desc;
    if (dev->d.kind == 0) dev->d.kind = dev->d.family == YBOX_PHOTO ? YIN_KIND_SYNC : YIN_KIND_BOX;
    if (dev->d.probe_ns == 0) dev->d.probe_ns = 100000000;
    if (dev->d.retry_ns <= 0) dev->d.retry_ns = 1000000000;
    if (dev->d.silent_ns == 0) dev->d.silent_ns = 2000000000;
    if (dev->d.max_width_ns <= 0) dev->d.max_width_ns = dev->d.family == YBOX_XID ? 20000000 : 1300000;
    if (dev->d.read_timeout_ms <= 0) dev->d.read_timeout_ms = 20;
    if (dev->d.family == YBOX_PHOTO) dev->d.probe_ns = -1;   /* no queries */
    dev->tr = desc->transport.open ? desc->transport : ydev_serial_transport(dev);
    ydev__lock_init(ydev__mx(dev));
    dev->state = YDEV_CLOSED;
    ydev__text(dev, "role", dev->d.role);
    ydev__set_state(dev, YDEV_OPENING, YDEV_WHY_START);
    dev->next_try = ydev__now(dev);
    dev->started = 1;
    ydev__publish(dev);
    if (dev->d.manual) return true;
#if defined(_WIN32)
    {
        HANDLE h = CreateThread(NULL, 0, ydev__thread, dev, 0, NULL);
        if (h) { memcpy(dev->thread_mem.b, &h, sizeof h); dev->have_thread = 1; }
    }
#else
    {
        pthread_t th;
        typedef char ydev__thread_fits[sizeof(pthread_t) <= sizeof dev->thread_mem ? 1 : -1];
        (void)sizeof(ydev__thread_fits);
        if (pthread_create(&th, NULL, ydev__thread, dev) == 0) {
            memcpy(dev->thread_mem.b, &th, sizeof th);
            dev->have_thread = 1;
        }
    }
#endif
    if (!dev->have_thread) {
        ydev__lock_free(ydev__mx(dev));
        dev->started = 0;
        YDEV__FAIL("the reader thread did not start");
    }
#undef YDEV__FAIL
    return true;
}

YDEV_API void ydev_stop(ydev_device* dev) {
    if (!dev || !dev->started) return;
    ydev__lock(ydev__mx(dev));
    dev->stop = 1;
    if (dev->conn && dev->tr.interrupt) dev->tr.interrupt(dev->conn);
    ydev__unlock(ydev__mx(dev));
    if (dev->have_thread) {
#if defined(_WIN32)
        HANDLE h;
        memcpy(&h, dev->thread_mem.b, sizeof h);
        WaitForSingleObject(h, INFINITE);
        CloseHandle(h);
#else
        pthread_t th;
        memcpy(&th, dev->thread_mem.b, sizeof th);
        pthread_join(th, NULL);
#endif
        dev->have_thread = 0;
    }
    ydev__close_conn(dev);
    ydev__set_state(dev, YDEV_CLOSED, YDEV_WHY_STOP);
    ydev__publish(dev);
    ydev__lock_free(ydev__mx(dev));
    dev->started = 0;
}

YDEV_API int ydev_poll(ydev_device* dev, int timeout_ms) {
    if (!dev || !dev->started || !dev->d.manual) return dev ? dev->state : YDEV_CLOSED;
    ydev__step(dev, timeout_ms < 0 ? 0 : timeout_ms);
    return dev->state;
}

YDEV_API int ydev_state(ydev_device* dev) {
    int s;
    if (!dev || !dev->started) return YDEV_CLOSED;
    ydev__lock(ydev__mx(dev));
    s = dev->snap.state;
    ydev__unlock(ydev__mx(dev));
    return s;
}

YDEV_API void ydev_get_stats(ydev_device* dev, ydev_stats* out) {
    if (!out) return;
    if (!dev || !dev->started) {
        if (dev) *out = dev->snap; else memset(out, 0, sizeof *out);
        return;
    }
    ydev__lock(ydev__mx(dev));
    *out = dev->snap;
    ydev__unlock(ydev__mx(dev));
}

YDEV_API const char* ydev_error(const ydev_device* dev) { return dev ? dev->err : "ysp_device: NULL device"; }

YDEV_API yin_source ydev_source(const ydev_device* dev) {
    yin_source s;
    memset(&s, 0, sizeof s);
    if (!dev) return s;
    s.kind = (uint8_t)dev->d.kind;
    s.device = dev->d.device;
    s.tier = (uint8_t)YIN_TIER_UNKNOWN;
    s.note = dev->d.family == YBOX_XID ? "ysp_device xid: box clock fitted (BRACKET), no loopback"
           : dev->d.family == YBOX_LINE ? "ysp_device line: board clock fitted (BRACKET), no loopback"
                                        : "ysp_device photo: board clock fitted (LATE), no loopback";
    return s;
}

#endif /* YSP_DEVICE_IMPLEMENTATION_GUARD */
#endif /* YSP_DEVICE_IMPLEMENTATION */

/* ------------------------------------------------------------------------
 * This software is available under the MIT-0 (MIT No Attribution) license.
 *
 * Copyright (c) 2026 ysp contributors
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
