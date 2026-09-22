/* psy_parallel.c - MATLAB/Octave MEX binding for psy_parallel.h (builds to psy_parallel.<mexext>)
 *
 * Command-dispatch interface in the style of widmann's ppdev-mex: the first
 * argument is a command string, the second (when needed) is an opaque uint64
 * port handle returned by 'open'.
 *
 *     h = psy_parallel('open')                 % platform defaults
 *     h = psy_parallel('open', '/dev/parport1')% ppdev device (Linux)
 *     h = psy_parallel('open', 'direct', 888)  % raw x86 I/O @ base (root)
 *     h = psy_parallel('open', 'inpout', 888)  % Windows inpout @ base
 *     % optional trailing struct tunes the async-worker RT reservation (ns):
 *     h = psy_parallel('open', struct('runtime_ns',5e5,'deadline_ns',2e6))
 *
 *     psy_parallel('write',      h, value)     % data register (0..255)
 *     d = psy_parallel('read',   h)            % read data register
 *     psy_parallel('setdir',     h, isInput)   % data direction
 *     psy_parallel('pulse',      h, value, usec)       % blocking
 *     psy_parallel('pulseasync', h, value, usec)       % non-blocking
 *     s = psy_parallel('status', h)            % status register
 *     c = psy_parallel('control',h)            % read control register
 *     psy_parallel('control',    h, value)     % write control register
 *     rt = psy_parallel('sched', h)            % effective RT params struct
 *     t  = psy_parallel('now_us')              % library clock (us), no handle
 *     psy_parallel('close',      h)
 *
 * Build with build.m (MATLAB or Octave). See README.md.
 *
 * NOTE: the handle table is freed (and every open port closed) on 'clear mex'
 * via mexAtExit, so a forgotten close cannot leave an async worker thread
 * pointing at a freed port. A handle is a small counter, not a pointer: a
 * freed port struct can be reallocated at the same address, and a stale
 * pointer handle would then silently drive the port that took its place.
 */
/* clock_gettime/nanosleep/pthread_*: ensure the feature macro is set before any
 * header. Guarded because some mex toolchains (MATLAB) already define it on the
 * command line, while others (Octave mkoctfile) do not. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mex.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define PSY_PARALLEL_IMPLEMENTATION
#include "psy_parallel.h"

/* ---- handle table: tracks every open port for cleanup ------------------ */
/* The handle is `id`, not the port address. malloc happily hands a new port
 * the address a closed one just released, so a pointer handle held in a stale
 * MATLAB variable would pass the lookup and drive someone else's port. Ids
 * are never reused, so a stale handle always raises psy_parallel:handle. */
typedef struct pp_node {
    uint64_t        id;
    psyp_port*      port;
    struct pp_node* next;
} pp_node;

static pp_node* g_ports = NULL;
static uint64_t g_next_id = 1;
static int      g_atexit_registered = 0;

static void pp_cleanup(void) {
    pp_node* n = g_ports;
    while (n) {
        pp_node* next = n->next;
        if (n->port) { psyp_close(n->port); free(n->port); }
        free(n);
        n = next;
    }
    g_ports = NULL;
}

/* Takes ownership of `port` on success. On allocation failure the port is
 * closed here: it would otherwise stay open with no handle to reach it. */
static uint64_t pp_register(psyp_port* port) {
    pp_node* n = (pp_node*)malloc(sizeof(*n));
    if (!n) {
        psyp_close(port);
        free(port);
        mexErrMsgIdAndTxt("psy_parallel:mem", "out of memory");
        return 0;
    }
    n->id = g_next_id++;
    n->port = port;
    n->next = g_ports;
    g_ports = n;
    if (!g_atexit_registered) { mexAtExit(pp_cleanup); g_atexit_registered = 1; }
    return n->id;
}

static void pp_unregister(psyp_port* port) {
    pp_node** pp = &g_ports;
    while (*pp) {
        if ((*pp)->port == port) { pp_node* dead = *pp; *pp = dead->next; free(dead); return; }
        pp = &(*pp)->next;
    }
}

/* Resolve a handle arg to a registered port pointer; error if unknown. */
static psyp_port* pp_handle(const mxArray* a) {
    uint64_t id;
    if (mxIsUint64(a))
        id = *(const uint64_t*)mxGetData(a);
    else if (mxIsDouble(a) && mxGetNumberOfElements(a) == 1)
        id = (uint64_t)mxGetScalar(a);
    else {
        mexErrMsgIdAndTxt("psy_parallel:handle", "handle must be a scalar uint64");
        return NULL;
    }
    for (pp_node* n = g_ports; n; n = n->next)
        if (n->id == id) return n->port;
    mexErrMsgIdAndTxt("psy_parallel:handle", "invalid or closed port handle");
    return NULL;
}

static uint8_t pp_byte(const mxArray* a, const char* what) {
    if (!mxIsNumeric(a) || mxGetNumberOfElements(a) != 1)
        mexErrMsgIdAndTxt("psy_parallel:arg", "%s must be a numeric scalar", what);
    double d = mxGetScalar(a);
    /* Reject fractions explicitly; a silent (uint8_t) truncation of 42.7 would
     * send trigger 42 with no warning. */
    if (d < 0 || d > 255 || d != (double)(uint8_t)d)
        mexErrMsgIdAndTxt("psy_parallel:arg", "%s must be an integer in 0..255", what);
    return (uint8_t)d;
}

static uint32_t pp_u32(const mxArray* a, const char* what) {
    if (!mxIsNumeric(a) || mxGetNumberOfElements(a) != 1)
        mexErrMsgIdAndTxt("psy_parallel:arg", "%s must be a numeric scalar", what);
    double d = mxGetScalar(a);
    if (d < 0) mexErrMsgIdAndTxt("psy_parallel:arg", "%s must be >= 0", what);
    return (uint32_t)d;
}

static mxArray* pp_scalar_u8(uint8_t v) {
    mxArray* a = mxCreateNumericMatrix(1, 1, mxUINT8_CLASS, mxREAL);
    *(uint8_t*)mxGetData(a) = v;
    return a;
}

/* The policy the worker obtained, as a string, so a log is readable without a
 * lookup table. psy_rt.h owns the names; it spells them in capitals, and this
 * binding has always published lower case, so fold the case here rather than
 * keep a second table that psy_rt.h could outgrow. */
static mxArray* pp_policy_string(psyrt_policy pol) {
    const char* name = psyrt_policy_name(pol);
    char buf[32];
    size_t i = 0;
    for (; name[i] != 0 && i + 1 < sizeof(buf); i++)
        buf[i] = (name[i] >= 'A' && name[i] <= 'Z')
               ? (char)(name[i] - 'A' + 'a') : name[i];
    buf[i] = 0;
    return mxCreateString(buf);
}

static void check(psyp_port* port, int ok) {
    if (!ok) mexErrMsgIdAndTxt("psy_parallel:io", "%s", psyp_error(port));
}

/* Read an async-worker RT reservation from a struct with fields runtime_ns /
 * deadline_ns / period_ns (any subset; missing fields stay 0, which psyp_open
 * resolves -- a 0 period means "same as deadline"). */
static void pp_read_sched(const mxArray* s, psyp_desc* d) {
    const mxArray* f;
    if ((f = mxGetField(s, 0, "runtime_ns")))  d->sched.runtime_ns  = (uint64_t)mxGetScalar(f);
    if ((f = mxGetField(s, 0, "deadline_ns"))) d->sched.deadline_ns = (uint64_t)mxGetScalar(f);
    if ((f = mxGetField(s, 0, "period_ns")))   d->sched.period_ns   = (uint64_t)mxGetScalar(f);
}

/* ---- 'open' ------------------------------------------------------------ */
static void cmd_open(int nlhs, mxArray* plhs[], int nrhs, const mxArray* prhs[]) {
    psyp_desc desc;
    memset(&desc, 0, sizeof(desc));
    char* devbuf = NULL;

    /* Optional trailing async-worker RT reservation as a struct with fields
     * runtime_ns/deadline_ns/period_ns (matches the 'sched' query and the
     * 'list' return shape). A struct is unambiguous against the device string
     * and the scalar base address. psyp_open validates the values. */
    if (nrhs >= 2 && mxIsStruct(prhs[nrhs - 1])) {
        pp_read_sched(prhs[nrhs - 1], &desc);
        nrhs--; /* consume it before parsing device/mode/base */
    }

    if (nrhs >= 2) {
        if (!mxIsChar(prhs[1]))
            mexErrMsgIdAndTxt("psy_parallel:arg",
                "open: expected a device path, 'direct'/'inpout', or an RT-params struct");
        char* s = mxArrayToString(prhs[1]);
        if (strcmp(s, "direct") == 0) {
            desc.backend = PSYP_BACKEND_DIRECT;
            if (nrhs >= 3) desc.base_addr = (uint16_t)pp_u32(prhs[2], "base address");
        } else if (strcmp(s, "inpout") == 0) {
            desc.backend = PSYP_BACKEND_INPOUT;
            if (nrhs >= 3) desc.base_addr = (uint16_t)pp_u32(prhs[2], "base address");
        } else {
            /* treat the string as a ppdev device path */
            devbuf = s; s = NULL;
            desc.device = devbuf;
        }
        if (s) mxFree(s);
    }

    psyp_port* port = (psyp_port*)calloc(1, sizeof(*port));
    if (!port) mexErrMsgIdAndTxt("psy_parallel:mem", "out of memory");
    int ok = psyp_open(port, &desc);
    if (devbuf) mxFree(devbuf);
    if (!ok) {
        char msg[300];
        snprintf(msg, sizeof(msg), "%s", psyp_error(port));
        free(port);
        mexErrMsgIdAndTxt("psy_parallel:open", "%s", msg);
    }
    uint64_t id = pp_register(port);

    mxArray* h = mxCreateNumericMatrix(1, 1, mxUINT64_CLASS, mxREAL);
    *(uint64_t*)mxGetData(h) = id;
    plhs[0] = h;
    (void)nlhs;
}

void mexFunction(int nlhs, mxArray* plhs[], int nrhs, const mxArray* prhs[]) {
    if (nrhs < 1 || !mxIsChar(prhs[0]))
        mexErrMsgIdAndTxt("psy_parallel:usage", "first argument must be a command string");

    char* cmd = mxArrayToString(prhs[0]);

    if (strcmp(cmd, "open") == 0) {
        cmd_open(nlhs, plhs, nrhs, prhs);
        mxFree(cmd);
        return;
    }

    if (strcmp(cmd, "list") == 0) {
        int n = psyp_list_ports(NULL, 0);
        if (n < 0) n = 0;
        psyp_port_info* v = NULL;
        if (n > 0) {
            v = (psyp_port_info*)mxMalloc((size_t)n * sizeof(*v));
            int got = psyp_list_ports(v, n);
            if (got < n) n = got;
        }
        const char* fields[] = { "name", "backend", "base_addr" };
        mxArray* s = mxCreateStructMatrix(n ? 1 : 0, n ? (size_t)n : 0, 3, fields);
        for (int i = 0; i < n; i++) {
            mxSetField(s, i, "name", mxCreateString(v[i].name));
            mxSetField(s, i, "backend", mxCreateDoubleScalar((double)v[i].backend));
            mxSetField(s, i, "base_addr", mxCreateDoubleScalar((double)v[i].base_addr));
        }
        if (v) mxFree(v);
        plhs[0] = s;
        mxFree(cmd);
        return;
    }

    if (strcmp(cmd, "now_us") == 0) {
        /* The clock the library times its own deadlines against, from
         * psy_rt.h. Bracket a write with it so your timestamps and the
         * library's share one base. psy_serial('now_us') reads the same
         * clock. A double holds whole microseconds exactly up to 2^53, which
         * is 285 years of uptime. */
        plhs[0] = mxCreateDoubleScalar((double)psyrt_now_us());
        mxFree(cmd);
        return;
    }

    /* all other commands take a handle as the 2nd argument */
    if (nrhs < 2) mexErrMsgIdAndTxt("psy_parallel:usage", "'%s' needs a port handle", cmd);
    psyp_port* port = pp_handle(prhs[1]);

    if (strcmp(cmd, "write") == 0) {
        if (nrhs < 3) mexErrMsgIdAndTxt("psy_parallel:usage", "write needs a value");
        check(port, psyp_write_data(port, pp_byte(prhs[2], "value")));

    } else if (strcmp(cmd, "read") == 0) {
        uint8_t v = psyp_read_data(port);
        check(port, psyp_error(port)[0] == '\0');
        plhs[0] = pp_scalar_u8(v);

    } else if (strcmp(cmd, "status") == 0) {
        uint8_t v = psyp_read_status(port);
        check(port, psyp_error(port)[0] == '\0');
        plhs[0] = pp_scalar_u8(v);

    } else if (strcmp(cmd, "sched") == 0) {
        /* effective async-worker RT params (ns) plus the policy the worker
         * actually obtained */
        const char* fields[] = { "runtime_ns", "deadline_ns", "period_ns", "policy" };
        mxArray* s = mxCreateStructMatrix(1, 1, 4, fields);
        mxSetField(s, 0, "runtime_ns",  mxCreateDoubleScalar((double)port->sched.runtime_ns));
        mxSetField(s, 0, "deadline_ns", mxCreateDoubleScalar((double)port->sched.deadline_ns));
        mxSetField(s, 0, "period_ns",   mxCreateDoubleScalar((double)port->sched.period_ns));
        mxSetField(s, 0, "policy", pp_policy_string(port->async_policy));
        plhs[0] = s;

    } else if (strcmp(cmd, "control") == 0) {
        if (nrhs >= 3) {                       /* write control */
            check(port, psyp_write_control(port, pp_byte(prhs[2], "value")));
        } else {                               /* read control */
            uint8_t v = psyp_read_control(port);
            check(port, psyp_error(port)[0] == '\0');
            plhs[0] = pp_scalar_u8(v);
        }

    } else if (strcmp(cmd, "setdir") == 0) {
        if (nrhs < 3) mexErrMsgIdAndTxt("psy_parallel:usage", "setdir needs isInput");
        /* mxGetScalar yields 0/1 for logical and numeric alike. */
        check(port, psyp_set_data_dir(port, mxGetScalar(prhs[2]) != 0));

    } else if (strcmp(cmd, "pulse") == 0) {
        if (nrhs < 4) mexErrMsgIdAndTxt("psy_parallel:usage", "pulse needs value, usec");
        check(port, psyp_pulse(port, pp_byte(prhs[2], "value"), pp_u32(prhs[3], "usec")));

    } else if (strcmp(cmd, "pulseasync") == 0) {
        if (nrhs < 4) mexErrMsgIdAndTxt("psy_parallel:usage", "pulseasync needs value, usec");
        check(port, psyp_pulse_async(port, pp_byte(prhs[2], "value"), pp_u32(prhs[3], "usec")));

    } else if (strcmp(cmd, "close") == 0) {
        psyp_close(port);
        pp_unregister(port);
        free(port);

    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "unknown command '%s'", cmd);
        mxFree(cmd);
        mexErrMsgIdAndTxt("psy_parallel:usage", "%s", msg);
    }
    mxFree(cmd);
}
