/* psy_serial.c - MATLAB/Octave MEX binding for psy_serial.h (builds to psy_serial.<mexext>)
 *
 * Command-dispatch interface in the style of widmann's ppdev-mex: the first
 * argument is a command string, the second (when needed) is an opaque uint64
 * port handle returned by 'open'.
 *
 *     ports = psy_serial('list')                    % struct array
 *     ports = psy_serial('find', struct('vid', 1027))
 *
 *     h = psy_serial('open', '/dev/ttyUSB0')        % defaults (115200 8N1)
 *     h = psy_serial('open', 'COM3', struct('baud', 9600, 'parity', 'even'))
 *
 *     n    = psy_serial('write',     h, uint8([1 2 3]))
 *     n    = psy_serial('writebyte', h, 85)
 *     data = psy_serial('read',      h, 6, 500, 'all')  % 6-byte frame in 500 ms
 *     data = psy_serial('read',      h, 64, 0)          % poll, [] if nothing
 *     psy_serial('pulse',      h, 85, 0, 2000)          % blocking 2 ms pulse
 *     psy_serial('pulseasync', h, 85, 0, 2000)          % non-blocking
 *     psy_serial('interrupt',  h)                       % wake a blocked read
 *     psy_serial('close',      h)
 *
 * Build with build.m (MATLAB or Octave). See README.md.
 *
 * NOTE: the handle table is freed (and every open port closed) on 'clear mex'
 * via mexAtExit, so a forgotten close cannot leave an async worker thread
 * pointing at a freed port.
 *
 * NOTE: MATLAB and Octave are single-threaded here, so the header's
 * one-reader-one-writer rule collapses to "one call at a time"; 'interrupt'
 * is only reachable from a timer callback or a second MATLAB process that
 * shares no handle, which is why it exists mainly for symmetry with the C API.
 */
/* clock_gettime/nanosleep/pthread_* and the termios extensions (CRTSCTS,
 * CMSPAR, the B* rates above 38400) live behind this feature macro, and
 * psy_serial.h can only set it for itself when it is the first include, which
 * it is not here. Guarded because some mex toolchains (MATLAB) already define
 * it on the command line, while others (Octave mkoctfile) do not. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mex.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define PSY_SERIAL_IMPLEMENTATION
#include "psy_serial.h"

/* ---- handle table: tracks every open port for cleanup ------------------ */
typedef struct ps_node {
    psys_port*      port;
    struct ps_node* next;
} ps_node;

static ps_node* g_ports = NULL;
static int      g_atexit_registered = 0;

static void ps_cleanup(void) {
    ps_node* n = g_ports;
    while (n) {
        ps_node* next = n->next;
        if (n->port) { psys_close(n->port); free(n->port); }
        free(n);
        n = next;
    }
    g_ports = NULL;
}

static void ps_register(psys_port* port) {
    ps_node* n = (ps_node*)malloc(sizeof(*n));
    n->port = port;
    n->next = g_ports;
    g_ports = n;
    if (!g_atexit_registered) { mexAtExit(ps_cleanup); g_atexit_registered = 1; }
}

static void ps_unregister(psys_port* port) {
    ps_node** pp = &g_ports;
    while (*pp) {
        if ((*pp)->port == port) { ps_node* dead = *pp; *pp = dead->next; free(dead); return; }
        pp = &(*pp)->next;
    }
}

/* Resolve a handle arg to a registered port pointer; error if unknown. */
static psys_port* ps_handle(const mxArray* a) {
    uint64_t bits;
    if (mxIsUint64(a))
        bits = *(const uint64_t*)mxGetData(a);
    else if (mxIsDouble(a) && mxGetNumberOfElements(a) == 1)
        bits = (uint64_t)mxGetScalar(a);
    else {
        mexErrMsgIdAndTxt("psy_serial:handle", "handle must be a scalar uint64");
        return NULL;
    }
    psys_port* port = (psys_port*)(uintptr_t)bits;
    for (ps_node* n = g_ports; n; n = n->next)
        if (n->port == port) return port;
    mexErrMsgIdAndTxt("psy_serial:handle", "invalid or closed port handle");
    return NULL;
}

/* ---- argument conversion ------------------------------------------------ */

static uint8_t ps_byte(const mxArray* a, const char* what) {
    if (!mxIsNumeric(a) || mxGetNumberOfElements(a) != 1)
        mexErrMsgIdAndTxt("psy_serial:arg", "%s must be a numeric scalar", what);
    double d = mxGetScalar(a);
    /* Reject fractions explicitly; a silent (uint8_t) truncation of 42.7 would
     * send trigger 42 with no warning. */
    if (d < 0 || d > 255 || d != (double)(uint8_t)d)
        mexErrMsgIdAndTxt("psy_serial:arg", "%s must be an integer in 0..255", what);
    return (uint8_t)d;
}

static uint32_t ps_u32(const mxArray* a, const char* what) {
    if (!mxIsNumeric(a) || mxGetNumberOfElements(a) != 1)
        mexErrMsgIdAndTxt("psy_serial:arg", "%s must be a numeric scalar", what);
    double d = mxGetScalar(a);
    if (d < 0) mexErrMsgIdAndTxt("psy_serial:arg", "%s must be >= 0", what);
    return (uint32_t)d;
}

/* mxGetScalar yields 0/1 for logical and numeric alike. */
static bool ps_bool(const mxArray* a, const char* what) {
    if ((!mxIsNumeric(a) && !mxIsLogical(a)) || mxGetNumberOfElements(a) != 1)
        mexErrMsgIdAndTxt("psy_serial:arg", "%s must be a logical or numeric scalar", what);
    return mxGetScalar(a) != 0;
}

/* Map a field of `s` that holds one of `names` to its index, or leave *out
 * untouched when the field is absent. Strings beat enum numbers here: a
 * struct written by hand reads better as 'even' than as 2, and 'info' returns
 * the same spellings. */
static void ps_field_enum(const mxArray* s, const char* field,
                          const char* const* names, int count, int* out) {
    const mxArray* f = mxGetField(s, 0, field);
    if (!f) return;
    if (!mxIsChar(f))
        mexErrMsgIdAndTxt("psy_serial:arg", "%s must be a string", field);
    char* v = mxArrayToString(f);
    for (int i = 0; i < count; i++) {
        if (strcmp(v, names[i]) == 0) { *out = i; mxFree(v); return; }
    }
    char msg[160];
    snprintf(msg, sizeof(msg), "unknown %s '%s'", field, v);
    mxFree(v);
    mexErrMsgIdAndTxt("psy_serial:arg", "%s", msg);
}

static const char* const ps_parity_names[] = { "none", "odd", "even", "mark", "space" };
static const char* const ps_flow_names[]   = { "none", "rtscts", "xonxoff" };
static const char* const ps_policy_names[] = { "none", "deadline", "fifo",
                                               "time_critical", "normal" };

/* Turn a negative PSYS_ERR_* code into a MATLAB error. The identifier names
 * the code so a script can branch on it (a listener catching
 * psy_serial:interrupted is the shutdown path, not a failure). */
static const char* ps_errid(int code) {
    switch (code) {
        case PSYS_ERR_IO:           return "psy_serial:io";
        case PSYS_ERR_DISCONNECTED: return "psy_serial:disconnected";
        case PSYS_ERR_CLOSED:       return "psy_serial:closed";
        case PSYS_ERR_INTERRUPTED:  return "psy_serial:interrupted";
        case PSYS_ERR_ARG:          return "psy_serial:arg";
        default:                    return "psy_serial:error";
    }
}

static int ps_check(int rc) {
    if (rc < 0) mexErrMsgIdAndTxt(ps_errid(rc), "%s", psys_strerror(rc));
    return rc;
}

/* Counts (bytes written, bytes read) only land in ans when the caller asked
 * for them; a bare psy_serial('write', ...) should print nothing. */
static void ps_return_count(int nlhs, mxArray* plhs[], int value) {
    if (nlhs >= 1) plhs[0] = mxCreateDoubleScalar((double)value);
}

/* ---- enumeration -------------------------------------------------------- */

static mxArray* ps_info_struct(const psys_port_info* v, int n) {
    const char* fields[] = { "name", "description", "serial_number",
                             "location", "vid", "pid" };
    mxArray* s = mxCreateStructMatrix(n ? 1 : 0, n ? (size_t)n : 0, 6, fields);
    for (int i = 0; i < n; i++) {
        mxSetField(s, i, "name",          mxCreateString(v[i].name));
        mxSetField(s, i, "description",   mxCreateString(v[i].description));
        mxSetField(s, i, "serial_number", mxCreateString(v[i].serial_number));
        mxSetField(s, i, "location",      mxCreateString(v[i].location));
        mxSetField(s, i, "vid",           mxCreateDoubleScalar((double)v[i].vid));
        mxSetField(s, i, "pid",           mxCreateDoubleScalar((double)v[i].pid));
    }
    return s;
}

/* Count, allocate, enumerate. The count can shrink between the two calls when
 * a port vanishes, so the struct array is sized from the second one. */
static mxArray* ps_enumerate(const psys_port_filter* filter) {
    int n = filter ? psys_find_ports(filter, NULL, 0) : psys_list_ports(NULL, 0);
    if (n < 0) mexErrMsgIdAndTxt(ps_errid(n), "%s", psys_strerror(n));
    psys_port_info* v = NULL;
    if (n > 0) {
        v = (psys_port_info*)mxMalloc((size_t)n * sizeof(*v));
        int got = filter ? psys_find_ports(filter, v, n) : psys_list_ports(v, n);
        if (got < 0) { mxFree(v); mexErrMsgIdAndTxt(ps_errid(got), "%s", psys_strerror(got)); }
        if (got < n) n = got;
    }
    mxArray* s = ps_info_struct(v, n);
    if (v) mxFree(v);
    return s;
}

/* 'find': a struct of filter fields. The string fields are borrowed by the
 * filter, so their buffers must outlive the psys_find_ports calls; they are
 * freed by the caller. */
static void ps_read_filter(const mxArray* s, psys_port_filter* f, char** owned, int* nowned) {
    static const char* const str_fields[] = { "serial_number", "location",
                                              "description", "name" };
    const char** slots[4];
    slots[0] = &f->serial_number;
    slots[1] = &f->location;
    slots[2] = &f->description;
    slots[3] = &f->name;

    const mxArray* fv;
    if ((fv = mxGetField(s, 0, "vid"))) f->vid = (uint16_t)ps_u32(fv, "vid");
    if ((fv = mxGetField(s, 0, "pid"))) f->pid = (uint16_t)ps_u32(fv, "pid");
    for (int i = 0; i < 4; i++) {
        fv = mxGetField(s, 0, str_fields[i]);
        if (!fv) continue;
        if (!mxIsChar(fv))
            mexErrMsgIdAndTxt("psy_serial:arg", "%s must be a string", str_fields[i]);
        char* v = mxArrayToString(fv);
        owned[(*nowned)++] = v;
        *slots[i] = v;
    }
}

/* ---- 'open' ------------------------------------------------------------- */

static void ps_read_desc(const mxArray* s, psys_desc* d) {
    const mxArray* f;
    if ((f = mxGetField(s, 0, "baud")))             d->baud = ps_u32(f, "baud");
    if ((f = mxGetField(s, 0, "data_bits")))        d->data_bits = (uint8_t)ps_u32(f, "data_bits");
    if ((f = mxGetField(s, 0, "write_timeout_ms"))) d->write_timeout_ms = ps_u32(f, "write_timeout_ms");
    if ((f = mxGetField(s, 0, "stop_bits"))) {
        uint32_t sb = ps_u32(f, "stop_bits");
        if (sb != 1 && sb != 2) mexErrMsgIdAndTxt("psy_serial:arg", "stop_bits must be 1 or 2");
        d->stop_bits = (sb == 2) ? PSYS_STOP_BITS_2 : PSYS_STOP_BITS_1;
    }
    int parity = (int)d->parity, flow = (int)d->flow;
    ps_field_enum(s, "parity", ps_parity_names, 5, &parity);
    ps_field_enum(s, "flow",   ps_flow_names,   3, &flow);
    d->parity = (psys_parity)parity;
    d->flow = (psys_flow)flow;

    if ((f = mxGetField(s, 0, "exclusive")))         d->exclusive = ps_bool(f, "exclusive");
    if ((f = mxGetField(s, 0, "low_latency")))       d->low_latency = ps_bool(f, "low_latency");
    if ((f = mxGetField(s, 0, "dtr_low_on_open")))   d->dtr_low_on_open = ps_bool(f, "dtr_low_on_open");
    if ((f = mxGetField(s, 0, "rts_low_on_open")))   d->rts_low_on_open = ps_bool(f, "rts_low_on_open");
    if ((f = mxGetField(s, 0, "keep_dtr_on_close"))) d->keep_dtr_on_close = ps_bool(f, "keep_dtr_on_close");
    /* Async-worker RT reservation (ns); missing fields stay 0, which psys_open
     * resolves -- a 0 period means "same as deadline". */
    if ((f = mxGetField(s, 0, "rt_runtime_ns")))  d->sched.runtime_ns  = (uint64_t)mxGetScalar(f);
    if ((f = mxGetField(s, 0, "rt_deadline_ns"))) d->sched.deadline_ns = (uint64_t)mxGetScalar(f);
    if ((f = mxGetField(s, 0, "rt_period_ns")))   d->sched.period_ns   = (uint64_t)mxGetScalar(f);
}

static void cmd_open(int nlhs, mxArray* plhs[], int nrhs, const mxArray* prhs[]) {
    psys_desc desc;
    memset(&desc, 0, sizeof(desc));

    if (nrhs < 2 || !mxIsChar(prhs[1]))
        mexErrMsgIdAndTxt("psy_serial:usage",
            "open needs a device name, e.g. 'COM3' or '/dev/ttyUSB0'");
    if (nrhs >= 3) {
        if (!mxIsStruct(prhs[2]) || mxGetNumberOfElements(prhs[2]) != 1)
            mexErrMsgIdAndTxt("psy_serial:arg", "open: settings must be a 1x1 struct");
        ps_read_desc(prhs[2], &desc);
    }
    char* devbuf = mxArrayToString(prhs[1]);
    desc.device = devbuf;

    psys_port* port = (psys_port*)calloc(1, sizeof(*port));
    if (!port) { mxFree(devbuf); mexErrMsgIdAndTxt("psy_serial:mem", "out of memory"); }
    bool ok = psys_open(port, &desc);
    mxFree(devbuf);
    if (!ok) {
        /* Copy the message out before the handle is freed. */
        char msg[300];
        snprintf(msg, sizeof(msg), "%s", psys_error(port));
        free(port);
        mexErrMsgIdAndTxt("psy_serial:open", "%s", msg);
    }
    ps_register(port);

    mxArray* h = mxCreateNumericMatrix(1, 1, mxUINT64_CLASS, mxREAL);
    *(uint64_t*)mxGetData(h) = (uint64_t)(uintptr_t)port;
    plhs[0] = h;
    (void)nlhs;
}

/* ---- 'write' ------------------------------------------------------------ */

/* Accepts a uint8 array as-is and any other numeric array element by element,
 * so both psy_serial('write', h, uint8([1 2 3])) and ('write', h, [1 2 3])
 * work. mxMalloc'd scratch is released by MATLAB when the mex call returns,
 * including on the error paths below. */
static const uint8_t* ps_write_bytes(const mxArray* a, int* len_out) {
    if (!mxIsNumeric(a) || mxIsComplex(a))
        mexErrMsgIdAndTxt("psy_serial:arg", "data must be a real numeric or uint8 array");
    size_t n = mxGetNumberOfElements(a);
    if (n > (size_t)0x7FFFFFFF)
        mexErrMsgIdAndTxt("psy_serial:arg", "data is too long");
    *len_out = (int)n;
    if (n == 0) return NULL;
    if (mxIsUint8(a)) return (const uint8_t*)mxGetData(a);

    if (!mxIsDouble(a))
        mexErrMsgIdAndTxt("psy_serial:arg", "data must be a uint8 or double array");
    const double* src = (const double*)mxGetData(a);
    uint8_t* buf = (uint8_t*)mxMalloc(n);
    for (size_t i = 0; i < n; i++) {
        double d = src[i];
        if (d < 0 || d > 255 || d != (double)(uint8_t)d)
            mexErrMsgIdAndTxt("psy_serial:arg", "data elements must be integers in 0..255");
        buf[i] = (uint8_t)d;
    }
    return buf;
}

/* ---- 'read' ------------------------------------------------------------- */

static void cmd_read(int nlhs, mxArray* plhs[], int nrhs, const mxArray* prhs[],
                     psys_port* port) {
    if (nrhs < 4)
        mexErrMsgIdAndTxt("psy_serial:usage", "read needs n, timeout_ms");
    uint32_t cap = ps_u32(prhs[2], "n");
    uint32_t timeout_ms = ps_u32(prhs[3], "timeout_ms");
    if (cap > (uint32_t)0x7FFFFFFF)
        mexErrMsgIdAndTxt("psy_serial:arg", "n is too large");
    int flags = PSYS_READ_ANY;
    if (nrhs >= 5) {
        if (!mxIsChar(prhs[4]))
            mexErrMsgIdAndTxt("psy_serial:arg", "read: the 4th argument must be 'all'");
        char* mode = mxArrayToString(prhs[4]);
        int all = (strcmp(mode, "all") == 0);
        mxFree(mode);
        if (!all) mexErrMsgIdAndTxt("psy_serial:arg", "read: the 4th argument must be 'all'");
        flags = PSYS_READ_ALL;
    }
    /* Read into scratch and size the result from the count: a short read must
     * not return trailing zeros that the device never sent. n == 0 answers
     * itself, and it reaches here from the natural
     * n = available(); read(n, 0) pattern. */
    uint8_t* buf = cap ? (uint8_t*)mxMalloc(cap) : NULL;
    int got = cap ? ps_check(psys_read(port, buf, (int)cap, timeout_ms, flags)) : 0;
    mxArray* out = mxCreateNumericMatrix(1, (size_t)got, mxUINT8_CLASS, mxREAL);
    if (got > 0) memcpy(mxGetData(out), buf, (size_t)got);
    if (buf) mxFree(buf);
    plhs[0] = out;
    (void)nlhs;
}

/* ---- 'info' ------------------------------------------------------------- */

static mxArray* ps_info(const psys_port* p) {
    const char* fields[] = { "device", "baud", "data_bits", "parity", "stop_bits",
                             "flow", "write_timeout_ms", "low_latency",
                             "keep_dtr_on_close", "async_policy",
                             "rd_oserr", "wr_oserr", "misc_oserr" };
    mxArray* s = mxCreateStructMatrix(1, 1, 13, fields);
    int parity = (int)p->parity, flow = (int)p->flow, policy = (int)p->async_policy;
    if (parity < 0 || parity > 4) parity = 0;
    if (flow < 0 || flow > 2) flow = 0;
    if (policy < 0 || policy > 4) policy = 0;
    mxSetField(s, 0, "device",            mxCreateString(p->device));
    mxSetField(s, 0, "baud",              mxCreateDoubleScalar((double)p->baud));
    mxSetField(s, 0, "data_bits",         mxCreateDoubleScalar((double)p->data_bits));
    mxSetField(s, 0, "parity",            mxCreateString(ps_parity_names[parity]));
    mxSetField(s, 0, "stop_bits",
               mxCreateDoubleScalar(p->stop_bits == PSYS_STOP_BITS_2 ? 2.0 : 1.0));
    mxSetField(s, 0, "flow",              mxCreateString(ps_flow_names[flow]));
    mxSetField(s, 0, "write_timeout_ms",  mxCreateDoubleScalar((double)p->write_timeout_ms));
    mxSetField(s, 0, "low_latency",       mxCreateLogicalScalar(p->low_latency));
    mxSetField(s, 0, "keep_dtr_on_close", mxCreateLogicalScalar(p->keep_dtr_on_close));
    /* The policy the worker actually obtained, as a string so logs are
     * readable without a lookup table. */
    mxSetField(s, 0, "async_policy",      mxCreateString(ps_policy_names[policy]));
    mxSetField(s, 0, "rd_oserr",          mxCreateDoubleScalar((double)p->rd_oserr));
    mxSetField(s, 0, "wr_oserr",          mxCreateDoubleScalar((double)p->wr_oserr));
    mxSetField(s, 0, "misc_oserr",        mxCreateDoubleScalar((double)p->misc_oserr));
    return s;
}

/* ---- dispatch ----------------------------------------------------------- */

void mexFunction(int nlhs, mxArray* plhs[], int nrhs, const mxArray* prhs[]) {
    if (nrhs < 1 || !mxIsChar(prhs[0]))
        mexErrMsgIdAndTxt("psy_serial:usage", "first argument must be a command string");

    char* cmd = mxArrayToString(prhs[0]);

    if (strcmp(cmd, "open") == 0) {
        cmd_open(nlhs, plhs, nrhs, prhs);
        mxFree(cmd);
        return;
    }

    if (strcmp(cmd, "list") == 0) {
        plhs[0] = ps_enumerate(NULL);
        mxFree(cmd);
        return;
    }

    if (strcmp(cmd, "find") == 0) {
        psys_port_filter f;
        memset(&f, 0, sizeof(f));
        char* owned[4];
        int nowned = 0;
        if (nrhs >= 2) {
            if (!mxIsStruct(prhs[1]) || mxGetNumberOfElements(prhs[1]) != 1)
                mexErrMsgIdAndTxt("psy_serial:arg", "find: the filter must be a 1x1 struct");
            ps_read_filter(prhs[1], &f, owned, &nowned);
        }
        mxArray* s = ps_enumerate(&f);
        for (int i = 0; i < nowned; i++) mxFree(owned[i]);
        plhs[0] = s;
        mxFree(cmd);
        return;
    }

    /* all other commands take a handle as the 2nd argument */
    if (nrhs < 2) mexErrMsgIdAndTxt("psy_serial:usage", "'%s' needs a port handle", cmd);
    psys_port* port = ps_handle(prhs[1]);

    if (strcmp(cmd, "write") == 0) {
        if (nrhs < 3) mexErrMsgIdAndTxt("psy_serial:usage", "write needs data");
        int len = 0;
        const uint8_t* data = ps_write_bytes(prhs[2], &len);
        /* An empty write is a no-op here; psys_write would see a NULL buffer
         * and call it a bad argument. */
        ps_return_count(nlhs, plhs, len ? ps_check(psys_write(port, data, len)) : 0);

    } else if (strcmp(cmd, "writebyte") == 0) {
        if (nrhs < 3) mexErrMsgIdAndTxt("psy_serial:usage", "writebyte needs a value");
        ps_return_count(nlhs, plhs, ps_check(psys_write_byte(port, ps_byte(prhs[2], "value"))));

    } else if (strcmp(cmd, "read") == 0) {
        cmd_read(nlhs, plhs, nrhs, prhs, port);

    } else if (strcmp(cmd, "available") == 0) {
        plhs[0] = mxCreateDoubleScalar((double)ps_check(psys_available(port)));

    } else if (strcmp(cmd, "drain") == 0) {
        ps_check(psys_drain(port));

    } else if (strcmp(cmd, "purge") == 0) {
        int which = PSYS_PURGE_RX;
        if (nrhs >= 3) {
            if (!mxIsChar(prhs[2]))
                mexErrMsgIdAndTxt("psy_serial:arg", "purge takes 'rx', 'tx' or 'both'");
            char* w = mxArrayToString(prhs[2]);
            if (strcmp(w, "rx") == 0)        which = PSYS_PURGE_RX;
            else if (strcmp(w, "tx") == 0)   which = PSYS_PURGE_TX;
            else if (strcmp(w, "both") == 0) which = PSYS_PURGE_RX | PSYS_PURGE_TX;
            else { mxFree(w); mexErrMsgIdAndTxt("psy_serial:arg", "purge takes 'rx', 'tx' or 'both'"); }
            mxFree(w);
        }
        ps_check(psys_purge(port, which));

    } else if (strcmp(cmd, "interrupt") == 0) {
        ps_check(psys_interrupt(port));

    } else if (strcmp(cmd, "pulse") == 0) {
        if (nrhs < 5) mexErrMsgIdAndTxt("psy_serial:usage", "pulse needs on, off, usec");
        ps_return_count(nlhs, plhs,
            ps_check(psys_pulse(port, ps_byte(prhs[2], "on"), ps_byte(prhs[3], "off"),
                                ps_u32(prhs[4], "usec"))));

    } else if (strcmp(cmd, "pulseasync") == 0) {
        if (nrhs < 5) mexErrMsgIdAndTxt("psy_serial:usage", "pulseasync needs on, off, usec");
        ps_return_count(nlhs, plhs,
            ps_check(psys_pulse_async(port, ps_byte(prhs[2], "on"), ps_byte(prhs[3], "off"),
                                      ps_u32(prhs[4], "usec"))));

    } else if (strcmp(cmd, "setdtr") == 0) {
        if (nrhs < 3) mexErrMsgIdAndTxt("psy_serial:usage", "setdtr needs on");
        ps_check(psys_set_dtr(port, ps_bool(prhs[2], "on")));

    } else if (strcmp(cmd, "setrts") == 0) {
        if (nrhs < 3) mexErrMsgIdAndTxt("psy_serial:usage", "setrts needs on");
        ps_check(psys_set_rts(port, ps_bool(prhs[2], "on")));

    } else if (strcmp(cmd, "lines") == 0) {
        int mask = ps_check(psys_get_lines(port));
        const char* fields[] = { "mask", "cts", "dsr", "ri", "dcd" };
        mxArray* s = mxCreateStructMatrix(1, 1, 5, fields);
        mxSetField(s, 0, "mask", mxCreateDoubleScalar((double)mask));
        mxSetField(s, 0, "cts",  mxCreateLogicalScalar((mask & PSYS_LINE_CTS) != 0));
        mxSetField(s, 0, "dsr",  mxCreateLogicalScalar((mask & PSYS_LINE_DSR) != 0));
        mxSetField(s, 0, "ri",   mxCreateLogicalScalar((mask & PSYS_LINE_RI) != 0));
        mxSetField(s, 0, "dcd",  mxCreateLogicalScalar((mask & PSYS_LINE_DCD) != 0));
        plhs[0] = s;

    } else if (strcmp(cmd, "break") == 0) {
        if (nrhs < 3) mexErrMsgIdAndTxt("psy_serial:usage", "break needs ms");
        ps_check(psys_send_break(port, ps_u32(prhs[2], "ms")));

    } else if (strcmp(cmd, "info") == 0) {
        plhs[0] = ps_info(port);

    } else if (strcmp(cmd, "close") == 0) {
        psys_close(port);
        ps_unregister(port);
        free(port);

    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "unknown command '%s'", cmd);
        mxFree(cmd);
        mexErrMsgIdAndTxt("psy_serial:usage", "%s", msg);
    }
    mxFree(cmd);
}
