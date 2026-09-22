/* psy_serial_ext.c - CPython extension wrapping psy_serial.h (module psy.serial)
 *
 * A thin, dependency-free binding (no nanobind/pybind/Cython): it needs only
 * Python.h, matching the single-header library's zero-dependency style. The
 * library implementation is compiled directly into this module.
 *
 * Built against the stable ABI / Limited API (Py_LIMITED_API), so one compiled
 * psy/serial.abi3.so works across CPython >= 3.8 without recompiling per
 * version. That rules out the static PyTypeObject layout, so the Port type is
 * a heap type created with PyType_FromSpec.
 *
 *     import psy.serial as ps
 *     for info in ps.list_ports():
 *         print(info)                       # {'name':..., 'vid':..., ...}
 *     with ps.Port("/dev/ttyUSB0", baud=115200) as port:
 *         port.write_byte(0x55)             # raise a trigger
 *         port.pulse(0x55, 0x00, 2000)      # 0x55, hold 2 ms, then 0x00
 *         data = port.read(6, 500, all=True)   # a 6-byte frame within 500 ms
 *
 * THREADING: the header's one-reader-one-writer rule applies to Python threads
 * exactly as it does to C ones. read/write/drain/pulse/send_break and
 * pulse_async's onset write release the GIL, so a listener thread blocked in
 * read() does not stall the interpreter, and interrupt() from another thread
 * ends that read with psy.serial.Interrupted.
 */
#ifndef Py_LIMITED_API
#define Py_LIMITED_API 0x03080000   /* target the CPython 3.8+ stable ABI */
#endif
#define PY_SSIZE_T_CLEAN
#include <Python.h>

/* Python.h first is safe here: pyconfig.h already defines _GNU_SOURCE on Linux
 * and raises _WIN32_WINNT past 0x0600 on Windows, which is exactly what the
 * header asks for when it is not the first include. */
#define PSY_SERIAL_IMPLEMENTATION
#include "psy_serial.h"

#include <limits.h>
#include <string.h>

typedef struct {
    PyObject_HEAD
    psys_port port;
} PortObject;

/* module-level exception types; Error is the base of the other three */
static PyObject* PsError;
static PyObject* PsDisconnected;
static PyObject* PsInterrupted;
static PyObject* PsClosed;

#define AS_PORT(self) (&((PortObject*)(self))->port)

/* Raise the exception matching a negative PSYS_ERR_* code. `oserr` is the
 * per-role slot of the call that failed (rd_oserr, wr_oserr or misc_oserr);
 * it is read before any Python call, because another thread in the same role
 * could overwrite it. */
static PyObject* ps_fail(int code, int oserr) {
    PyObject* exc = PsError;
    if (code == PSYS_ERR_DISCONNECTED)     exc = PsDisconnected;
    else if (code == PSYS_ERR_INTERRUPTED) exc = PsInterrupted;
    else if (code == PSYS_ERR_CLOSED)      exc = PsClosed;
    PyErr_Format(exc, "%s; os error %d", psys_strerror(code), oserr);
    return NULL;
}

static int Port_init(PyObject* self, PyObject* args, PyObject* kwds) {
    static char* kw[] = { "device", "baud", "data_bits", "parity", "stop_bits",
                          "flow", "write_timeout_ms", "exclusive", "low_latency",
                          "dtr_low_on_open", "rts_low_on_open", "keep_dtr_on_close",
                          "rt_runtime_ns", "rt_deadline_ns", "rt_period_ns", NULL };
    const char* device = NULL;
    unsigned int baud = 0, write_timeout_ms = 0;
    unsigned char data_bits = 0;
    int parity = PSYS_PARITY_NONE, stop_bits = PSYS_STOP_BITS_1, flow = PSYS_FLOW_NONE;
    int exclusive = 0, low_latency = 0, dtr_low = 0, rts_low = 0, keep_dtr = 0;
    unsigned long long rt_runtime = 0, rt_deadline = 0, rt_period = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|IbiiiIpppppKKK", kw,
                                     &device, &baud, &data_bits, &parity, &stop_bits,
                                     &flow, &write_timeout_ms, &exclusive, &low_latency,
                                     &dtr_low, &rts_low, &keep_dtr,
                                     &rt_runtime, &rt_deadline, &rt_period))
        return -1;

    psys_desc desc;
    memset(&desc, 0, sizeof(desc));
    desc.device = device;
    desc.baud = baud;
    desc.data_bits = data_bits;
    desc.parity = (psys_parity)parity;
    desc.stop_bits = (psys_stop_bits)stop_bits;
    desc.flow = (psys_flow)flow;
    desc.write_timeout_ms = write_timeout_ms;
    desc.exclusive = exclusive ? true : false;
    desc.low_latency = low_latency ? true : false;
    desc.dtr_low_on_open = dtr_low ? true : false;
    desc.rts_low_on_open = rts_low ? true : false;
    desc.keep_dtr_on_close = keep_dtr ? true : false;
    /* Async-worker SCHED_DEADLINE reservation (Linux); all-zero -> defaults.
     * psys_open validates 0 < runtime <= deadline <= period and raises here. */
    desc.sched.runtime_ns = rt_runtime;
    desc.sched.deadline_ns = rt_deadline;
    desc.sched.period_ns = rt_period;

    /* Python can call __init__ on a live object; psys_open would memset over
     * the open descriptor and orphan the async worker. The object memory is
     * zeroed at allocation, so is_open is valid here and close is a no-op on
     * a fresh instance. */
    psys_close(AS_PORT(self));
    if (!psys_open(AS_PORT(self), &desc)) {
        PyErr_SetString(PsError, psys_error(AS_PORT(self)));
        return -1;
    }
    return 0;
}

static void Port_dealloc(PyObject* self) {
    /* Same reason as Port_close: psys_close joins the async worker and can
     * block in the driver for up to write_timeout_ms on a wedged device.
     * Holding the GIL through that stalls every other Python thread, and a
     * dealloc can run on any thread the last reference happens to die on. */
    Py_BEGIN_ALLOW_THREADS
    psys_close(AS_PORT(self));
    Py_END_ALLOW_THREADS
    /* Heap type: fetch tp_free via the stable ABI and release the type ref the
     * instance holds (PyType_GenericAlloc incref's the type since 3.8). */
    PyTypeObject* tp = Py_TYPE(self);
    freefunc tp_free = (freefunc)PyType_GetSlot(tp, Py_tp_free);
    tp_free(self);
    Py_DECREF(tp);
}

/* --- input (reader role) ------------------------------------------------ */

static PyObject* Port_read(PyObject* self, PyObject* args, PyObject* kwds) {
    static char* kw[] = { "n", "timeout_ms", "all", NULL };
    int n;
    PyObject* timeout_obj;
    int all = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "iO|p", kw, &n, &timeout_obj, &all))
        return NULL;
    if (n < 0) { PyErr_SetString(PyExc_ValueError, "n must be >= 0"); return NULL; }
    /* Not "I": PyArg_Parse does no range checking for it, so read(64, -1)
     * would wrap to TIMEOUT_INFINITE and block the caller forever. */
    unsigned long long t = PyLong_AsUnsignedLongLong(timeout_obj);
    if (t == (unsigned long long)-1 && PyErr_Occurred()) return NULL;
    if (t > 0xFFFFFFFFull) {
        PyErr_SetString(PyExc_OverflowError,
                        "timeout_ms must fit in 32 bits (TIMEOUT_INFINITE is the maximum)");
        return NULL;
    }
    unsigned int timeout_ms = (unsigned int)t;
    if (n == 0) return PyBytes_FromStringAndSize("", 0);

    char* buf = (char*)PyMem_Malloc((size_t)n);
    if (!buf) return PyErr_NoMemory();
    int got;
    /* Release the GIL: psys_read blocks for up to timeout_ms, and interrupt()
     * has to reach it from another Python thread. */
    Py_BEGIN_ALLOW_THREADS
    got = psys_read(AS_PORT(self), buf, n, timeout_ms,
                    all ? PSYS_READ_ALL : PSYS_READ_ANY);
    Py_END_ALLOW_THREADS
    if (got < 0) {
        int oserr = AS_PORT(self)->rd_oserr;
        PyMem_Free(buf);
        return ps_fail(got, oserr);
    }
    PyObject* out = PyBytes_FromStringAndSize(buf, (Py_ssize_t)got);
    PyMem_Free(buf);
    return out;
}

static PyObject* Port_available(PyObject* self, PyObject* Py_UNUSED(ignored)) {
    int n = psys_available(AS_PORT(self));
    if (n < 0) return ps_fail(n, AS_PORT(self)->rd_oserr);
    return PyLong_FromLong(n);
}

/* --- output (writer role) ----------------------------------------------- */

static PyObject* Port_write(PyObject* self, PyObject* args, PyObject* kwds) {
    static char* kw[] = { "data", NULL };
    PyObject* obj;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O", kw, &obj)) return NULL;

    /* Not "y#": it rejects a bytearray, and its pointer into one would dangle
     * if another thread resized the object while we hold no GIL. This borrows
     * an immutable bytes (the same object when `obj` already is one) and
     * copies anything else, so the buffer cannot move under the driver. */
    PyObject* data = PyBytes_FromObject(obj);
    if (!data) return NULL;
    Py_ssize_t len = PyBytes_Size(data);
    const char* p = PyBytes_AsString(data);
    if (!p || len > INT_MAX) {
        Py_DECREF(data);
        if (len > INT_MAX) PyErr_SetString(PyExc_ValueError, "data is too long");
        return NULL;
    }

    int n;
    Py_BEGIN_ALLOW_THREADS
    n = psys_write(AS_PORT(self), p, (int)len);
    Py_END_ALLOW_THREADS
    Py_DECREF(data);
    if (n < 0) return ps_fail(n, AS_PORT(self)->wr_oserr);
    return PyLong_FromLong(n);
}

static PyObject* Port_write_byte(PyObject* self, PyObject* arg) {
    unsigned long v = PyLong_AsUnsignedLong(arg);
    if (v == (unsigned long)-1 && PyErr_Occurred()) return NULL;
    if (v > 0xFF) { PyErr_SetString(PyExc_ValueError, "value must be 0..255"); return NULL; }
    int n;
    Py_BEGIN_ALLOW_THREADS
    n = psys_write_byte(AS_PORT(self), (uint8_t)v);
    Py_END_ALLOW_THREADS
    if (n < 0) return ps_fail(n, AS_PORT(self)->wr_oserr);
    return PyLong_FromLong(n);
}

static PyObject* Port_drain(PyObject* self, PyObject* Py_UNUSED(ignored)) {
    int rc;
    Py_BEGIN_ALLOW_THREADS
    rc = psys_drain(AS_PORT(self));
    Py_END_ALLOW_THREADS
    if (rc < 0) return ps_fail(rc, AS_PORT(self)->wr_oserr);
    Py_RETURN_NONE;
}

static PyObject* Port_pulse(PyObject* self, PyObject* args, PyObject* kwds) {
    static char* kw[] = { "on", "off", "usec", NULL };
    unsigned int on, off, usec;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "III", kw, &on, &off, &usec)) return NULL;
    if (on > 0xFF || off > 0xFF) {
        PyErr_SetString(PyExc_ValueError, "on and off must be 0..255");
        return NULL;
    }
    int n;
    /* Release the GIL: psys_pulse holds the line for `usec`. */
    Py_BEGIN_ALLOW_THREADS
    n = psys_pulse(AS_PORT(self), (uint8_t)on, (uint8_t)off, usec);
    Py_END_ALLOW_THREADS
    if (n < 0) return ps_fail(n, AS_PORT(self)->wr_oserr);
    return PyLong_FromLong(n);
}

static PyObject* Port_pulse_async(PyObject* self, PyObject* args, PyObject* kwds) {
    static char* kw[] = { "on", "off", "usec", NULL };
    unsigned int on, off, usec;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "III", kw, &on, &off, &usec)) return NULL;
    if (on > 0xFF || off > 0xFF) {
        PyErr_SetString(PyExc_ValueError, "on and off must be 0..255");
        return NULL;
    }
    int n;
    /* The call returns before the width elapses, but its onset write still
     * goes through the driver and can block up to write_timeout_ms. */
    Py_BEGIN_ALLOW_THREADS
    n = psys_pulse_async(AS_PORT(self), (uint8_t)on, (uint8_t)off, usec);
    Py_END_ALLOW_THREADS
    if (n < 0) return ps_fail(n, AS_PORT(self)->wr_oserr);
    return PyLong_FromLong(n);
}

static PyObject* Port_send_break(PyObject* self, PyObject* args, PyObject* kwds) {
    static char* kw[] = { "ms", NULL };
    unsigned int ms;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "I", kw, &ms)) return NULL;
    int rc;
    /* Release the GIL: the break is held for `ms` on the calling thread. */
    Py_BEGIN_ALLOW_THREADS
    rc = psys_send_break(AS_PORT(self), ms);
    Py_END_ALLOW_THREADS
    if (rc < 0) return ps_fail(rc, AS_PORT(self)->wr_oserr);
    Py_RETURN_NONE;
}

/* --- any role ----------------------------------------------------------- */

static PyObject* Port_purge(PyObject* self, PyObject* args, PyObject* kwds) {
    static char* kw[] = { "rx", "tx", NULL };
    int rx = 1, tx = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|pp", kw, &rx, &tx)) return NULL;
    int which = (rx ? PSYS_PURGE_RX : 0) | (tx ? PSYS_PURGE_TX : 0);
    int rc = psys_purge(AS_PORT(self), which);
    if (rc < 0) {
        /* The OS error lands in the slot of the role that owns the direction,
         * and in wr_oserr when both are purged. */
        int oserr = tx ? AS_PORT(self)->wr_oserr : AS_PORT(self)->rd_oserr;
        return ps_fail(rc, oserr);
    }
    Py_RETURN_NONE;
}

static PyObject* Port_interrupt(PyObject* self, PyObject* Py_UNUSED(ignored)) {
    int rc = psys_interrupt(AS_PORT(self));
    if (rc < 0) return ps_fail(rc, AS_PORT(self)->misc_oserr);
    Py_RETURN_NONE;
}

static PyObject* Port_set_dtr(PyObject* self, PyObject* args, PyObject* kwds) {
    static char* kw[] = { "on", NULL };
    int on;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "p", kw, &on)) return NULL;
    int rc = psys_set_dtr(AS_PORT(self), on ? true : false);
    if (rc < 0) return ps_fail(rc, AS_PORT(self)->misc_oserr);
    Py_RETURN_NONE;
}

static PyObject* Port_set_rts(PyObject* self, PyObject* args, PyObject* kwds) {
    static char* kw[] = { "on", NULL };
    int on;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "p", kw, &on)) return NULL;
    int rc = psys_set_rts(AS_PORT(self), on ? true : false);
    if (rc < 0) return ps_fail(rc, AS_PORT(self)->misc_oserr);
    Py_RETURN_NONE;
}

static PyObject* Port_get_lines(PyObject* self, PyObject* Py_UNUSED(ignored)) {
    int mask = psys_get_lines(AS_PORT(self));
    if (mask < 0) return ps_fail(mask, AS_PORT(self)->misc_oserr);
    return PyLong_FromLong(mask);
}

/* --- lifecycle / context manager ---------------------------------------- */

static PyObject* Port_close(PyObject* self, PyObject* Py_UNUSED(ignored)) {
    /* psys_close joins the async worker and writes any pending trailing byte,
     * so it can block in the driver; do not hold the GIL through that. */
    Py_BEGIN_ALLOW_THREADS
    psys_close(AS_PORT(self));
    Py_END_ALLOW_THREADS
    Py_RETURN_NONE;
}

static PyObject* Port_enter(PyObject* self, PyObject* Py_UNUSED(ignored)) {
    Py_INCREF(self);
    return self;
}

static PyObject* Port_exit(PyObject* self, PyObject* Py_UNUSED(args)) {
    Py_BEGIN_ALLOW_THREADS
    psys_close(AS_PORT(self));
    Py_END_ALLOW_THREADS
    Py_RETURN_FALSE; /* don't suppress exceptions */
}

/* --- read-only properties ------------------------------------------------ */

static PyObject* Port_get_is_open(PyObject* self, void* Py_UNUSED(closure)) {
    return PyBool_FromLong(psys_is_open(AS_PORT(self)));
}
static PyObject* Port_get_device(PyObject* self, void* Py_UNUSED(closure)) {
    return PyUnicode_FromString(AS_PORT(self)->device);
}
static PyObject* Port_get_baud(PyObject* self, void* Py_UNUSED(closure)) {
    return PyLong_FromUnsignedLong(AS_PORT(self)->baud);
}
static PyObject* Port_get_data_bits(PyObject* self, void* Py_UNUSED(closure)) {
    return PyLong_FromLong(AS_PORT(self)->data_bits);
}
static PyObject* Port_get_parity(PyObject* self, void* Py_UNUSED(closure)) {
    return PyLong_FromLong((long)AS_PORT(self)->parity);
}
static PyObject* Port_get_stop_bits(PyObject* self, void* Py_UNUSED(closure)) {
    return PyLong_FromLong((long)AS_PORT(self)->stop_bits);
}
static PyObject* Port_get_flow(PyObject* self, void* Py_UNUSED(closure)) {
    return PyLong_FromLong((long)AS_PORT(self)->flow);
}
static PyObject* Port_get_write_timeout_ms(PyObject* self, void* Py_UNUSED(closure)) {
    return PyLong_FromUnsignedLong(AS_PORT(self)->write_timeout_ms);
}
static PyObject* Port_get_low_latency(PyObject* self, void* Py_UNUSED(closure)) {
    return PyBool_FromLong(AS_PORT(self)->low_latency);
}
static PyObject* Port_get_keep_dtr(PyObject* self, void* Py_UNUSED(closure)) {
    return PyBool_FromLong(AS_PORT(self)->keep_dtr_on_close);
}
static PyObject* Port_get_async_policy(PyObject* self, void* Py_UNUSED(closure)) {
    return PyLong_FromLong((long)AS_PORT(self)->async_policy);
}
static PyObject* Port_get_rd_oserr(PyObject* self, void* Py_UNUSED(closure)) {
    return PyLong_FromLong(AS_PORT(self)->rd_oserr);
}
static PyObject* Port_get_wr_oserr(PyObject* self, void* Py_UNUSED(closure)) {
    return PyLong_FromLong(AS_PORT(self)->wr_oserr);
}
static PyObject* Port_get_misc_oserr(PyObject* self, void* Py_UNUSED(closure)) {
    return PyLong_FromLong(AS_PORT(self)->misc_oserr);
}

static PyGetSetDef Port_getset[] = {
    { "is_open",          Port_get_is_open,          NULL, "True while the port is open.", NULL },
    { "device",           Port_get_device,           NULL, "Device name as opened.", NULL },
    { "baud",             Port_get_baud,             NULL, "Effective baud rate.", NULL },
    { "data_bits",        Port_get_data_bits,        NULL, "Effective data bits (5..8).", NULL },
    { "parity",           Port_get_parity,           NULL, "Effective parity (PARITY_*).", NULL },
    { "stop_bits",        Port_get_stop_bits,        NULL, "Effective stop bits (STOP_BITS_*).", NULL },
    { "flow",             Port_get_flow,             NULL, "Effective flow control (FLOW_*).", NULL },
    { "write_timeout_ms", Port_get_write_timeout_ms, NULL,
      "Write timeout in ms; immutable while the port is open.", NULL },
    { "low_latency",      Port_get_low_latency,      NULL,
      "True if the driver accepted the low-latency request.", NULL },
    { "keep_dtr_on_close", Port_get_keep_dtr,        NULL,
      "True if DTR stays asserted after close (POSIX only).", NULL },
    { "async_policy",     Port_get_async_policy,     NULL,
      "Scheduling policy the async-pulse worker obtained (ASYNC_*). Log it "
      "with timing data: DEADLINE is refused without CAP_SYS_NICE or under "
      "CPU pinning, and the library falls back silently.", NULL },
    { "rd_oserr",         Port_get_rd_oserr,         NULL,
      "OS error number of the last reader-side failure.", NULL },
    { "wr_oserr",         Port_get_wr_oserr,         NULL,
      "OS error number of the last writer-side failure.", NULL },
    { "misc_oserr",       Port_get_misc_oserr,       NULL,
      "OS error number of the last any-role failure (interrupt, lines, DTR/RTS).", NULL },
    { NULL }
};

static PyMethodDef Port_methods[] = {
    { "read",        (PyCFunction)Port_read,        METH_VARARGS | METH_KEYWORDS,
      "read(n, timeout_ms, all=False) -> bytes: read up to n bytes. all=False "
      "returns on the first bytes available (b'' on timeout); all=True keeps "
      "reading until n bytes or the timeout. timeout_ms=0 polls, "
      "TIMEOUT_INFINITE blocks. Releases the GIL." },
    { "write",       (PyCFunction)Port_write,       METH_VARARGS | METH_KEYWORDS,
      "write(data) -> int: write a bytes-like object (or any iterable of ints "
      "0..255); returns the count the driver accepted (< len on write "
      "timeout). Releases the GIL." },
    { "write_byte",  Port_write_byte,               METH_O,
      "write_byte(value) -> int: write one byte (0..255); returns 1, or 0 on "
      "write timeout." },
    { "pulse",       (PyCFunction)Port_pulse,       METH_VARARGS | METH_KEYWORDS,
      "pulse(on, off, usec) -> int: write on, block usec microseconds, write "
      "off. Returns 2 on success. Releases the GIL." },
    { "pulse_async", (PyCFunction)Port_pulse_async, METH_VARARGS | METH_KEYWORDS,
      "pulse_async(on, off, usec) -> int: write on now and return; the worker "
      "thread writes off after usec. Returns 1 when the onset was accepted." },
    { "available",   Port_available,                METH_NOARGS,
      "available() -> int: bytes waiting in the driver's receive buffer." },
    { "drain",       Port_drain,                    METH_NOARGS,
      "drain(): block until the driver's transmit buffer is empty." },
    { "purge",       (PyCFunction)Port_purge,       METH_VARARGS | METH_KEYWORDS,
      "purge(rx=True, tx=False): discard buffered bytes." },
    { "interrupt",   Port_interrupt,                METH_NOARGS,
      "interrupt(): wake a read() blocked on another thread; that read raises "
      "Interrupted. The wake is latched until one read consumes it." },
    { "set_dtr",     (PyCFunction)Port_set_dtr,     METH_VARARGS | METH_KEYWORDS,
      "set_dtr(on): drive the DTR output line." },
    { "set_rts",     (PyCFunction)Port_set_rts,     METH_VARARGS | METH_KEYWORDS,
      "set_rts(on): drive the RTS output line (ignored under FLOW_RTSCTS)." },
    { "get_lines",   Port_get_lines,                METH_NOARGS,
      "get_lines() -> int: input lines as a LINE_* mask." },
    { "send_break",  (PyCFunction)Port_send_break,  METH_VARARGS | METH_KEYWORDS,
      "send_break(ms): hold TX in the break state for ms milliseconds." },
    { "close",       Port_close,                    METH_NOARGS,
      "close(): release the port (idempotent). Both roles must have returned." },
    { "__enter__",   Port_enter,                    METH_NOARGS, NULL },
    { "__exit__",    Port_exit,                     METH_VARARGS, NULL },
    { NULL }
};

static PyType_Slot Port_slots[] = {
    { Py_tp_doc,     (void*)"Serial port handle. Port(device, baud=0, data_bits=0, "
                            "parity=PARITY_NONE, stop_bits=STOP_BITS_1, flow=FLOW_NONE, "
                            "write_timeout_ms=0, exclusive=False, low_latency=False, "
                            "dtr_low_on_open=False, rts_low_on_open=False, "
                            "keep_dtr_on_close=False, rt_runtime_ns=0, "
                            "rt_deadline_ns=0, rt_period_ns=0). A zero field means "
                            "the library default." },
    { Py_tp_new,     (void*)PyType_GenericNew },
    { Py_tp_init,    (void*)Port_init },
    { Py_tp_dealloc, (void*)Port_dealloc },
    { Py_tp_methods, Port_methods },
    { Py_tp_getset,  Port_getset },
    { 0, NULL }
};

static PyType_Spec Port_spec = {
    .name = "psy.serial.Port",
    .basicsize = sizeof(PortObject),
    .itemsize = 0,
    .flags = Py_TPFLAGS_DEFAULT,
    .slots = Port_slots,
};

/* --- module-level functions --------------------------------------------- */

/* Build the list[dict] both enumeration entry points return. */
static PyObject* ps_info_list(const psys_port_info* v, int n) {
    PyObject* list = PyList_New(n > 0 ? n : 0);
    if (!list) return NULL;
    for (int i = 0; i < n; i++) {
        PyObject* d = Py_BuildValue("{s:s,s:s,s:s,s:s,s:i,s:i}",
                                    "name",          v[i].name,
                                    "description",   v[i].description,
                                    "serial_number", v[i].serial_number,
                                    "location",      v[i].location,
                                    "vid",           (int)v[i].vid,
                                    "pid",           (int)v[i].pid);
        if (!d) { Py_DECREF(list); return NULL; }
        PyList_SetItem(list, i, d); /* steals ref to d */
    }
    return list;
}

/* Count, allocate, enumerate. `n` can shrink between the two calls when a port
 * vanishes, and the list must be sized from the final count: a list created
 * from the first count and then trimmed would keep NULL slots, which crash on
 * iteration. */
static PyObject* ps_enumerate(const psys_port_filter* filter) {
    int n = filter ? psys_find_ports(filter, NULL, 0) : psys_list_ports(NULL, 0);
    if (n < 0) { PyErr_SetString(PsError, psys_strerror(n)); return NULL; }
    psys_port_info* v = NULL;
    if (n > 0) {
        v = (psys_port_info*)PyMem_Malloc((size_t)n * sizeof(*v));
        if (!v) return PyErr_NoMemory();
        int got = filter ? psys_find_ports(filter, v, n) : psys_list_ports(v, n);
        if (got < 0) { PyMem_Free(v); PyErr_SetString(PsError, psys_strerror(got)); return NULL; }
        if (got < n) n = got;
    }
    PyObject* list = ps_info_list(v, n);
    PyMem_Free(v);
    return list;
}

static PyObject* mod_list_ports(PyObject* Py_UNUSED(self), PyObject* Py_UNUSED(args)) {
    return ps_enumerate(NULL);
}

static PyObject* mod_find_ports(PyObject* Py_UNUSED(self), PyObject* args, PyObject* kwds) {
    static char* kw[] = { "vid", "pid", "serial_number", "location",
                          "description", "name", NULL };
    unsigned int vid = 0, pid = 0;
    const char *serial_number = NULL, *location = NULL, *description = NULL, *name = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|IIzzzz", kw, &vid, &pid,
                                     &serial_number, &location, &description, &name))
        return NULL;
    if (vid > 0xFFFFu || pid > 0xFFFFu) {
        PyErr_SetString(PyExc_ValueError, "vid and pid must fit in 16 bits");
        return NULL;
    }
    psys_port_filter f;
    memset(&f, 0, sizeof(f));
    f.vid = (uint16_t)vid;
    f.pid = (uint16_t)pid;
    f.serial_number = serial_number;
    f.location = location;
    f.description = description;
    f.name = name;
    return ps_enumerate(&f);
}

/* The clock the library times its own deadlines against. Exposed so a caller
 * can bracket a write with the same time base instead of mixing in
 * time.monotonic(), which is a different clock on Windows. */
static PyObject* mod_now_us(PyObject* Py_UNUSED(self), PyObject* Py_UNUSED(args)) {
    return PyLong_FromUnsignedLongLong((unsigned long long)psys_now_us());
}

static PyMethodDef module_methods[] = {
    { "now_us", mod_now_us, METH_NOARGS,
      "now_us() -> int: monotonic microseconds from the clock the library uses "
      "for its own deadlines (CLOCK_MONOTONIC / QueryPerformanceCounter). "
      "Bracket a write with it: the physical onset lies after the first "
      "reading, and the difference bounds the syscall cost." },
    { "list_ports", mod_list_ports, METH_NOARGS,
      "list_ports() -> list[dict]: enumerate serial ports without opening them. "
      "Each dict has 'name', 'description', 'serial_number', 'location', "
      "'vid', 'pid'; fields the platform cannot determine are '' or 0." },
    { "find_ports", (PyCFunction)mod_find_ports, METH_VARARGS | METH_KEYWORDS,
      "find_ports(vid=0, pid=0, serial_number=None, location=None, "
      "description=None, name=None) -> list[dict]: the subset of list_ports() "
      "that matches. Zero/None matches anything; serial_number is an exact "
      "match, location a prefix, description and name substrings, all "
      "case-insensitive." },
    { NULL }
};

static PyModuleDef psy_serial_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "psy.serial",
    .m_doc = "Serial-port byte I/O for trigger and response boxes.",
    .m_size = -1,
    .m_methods = module_methods,
};

/* Add an exception type, keeping one reference for the static pointer and
 * giving one to the module. */
static PyObject* ps_add_exc(PyObject* m, const char* qualname, const char* attr,
                            PyObject* base) {
    PyObject* e = PyErr_NewException(qualname, base, NULL);
    if (!e) return NULL;
    Py_INCREF(e);
    if (PyModule_AddObject(m, attr, e) < 0) { Py_DECREF(e); Py_DECREF(e); return NULL; }
    return e;
}

/* PyModule_AddIntConstant takes a long, which is 32-bit and signed on Windows;
 * the unsigned constants go in as objects instead. */
static int ps_add_uint(PyObject* m, const char* name, unsigned long long v) {
    PyObject* o = PyLong_FromUnsignedLongLong(v);
    if (!o) return -1;
    if (PyModule_AddObject(m, name, o) < 0) { Py_DECREF(o); return -1; }
    return 0;
}

/* The init function is named after the last dotted component: the loader looks
 * for PyInit_serial in psy/serial.abi3.so. */
PyMODINIT_FUNC PyInit_serial(void) {
    PyObject* m = PyModule_Create(&psy_serial_module);
    if (!m) return NULL;

    PyObject* type = PyType_FromSpec(&Port_spec);
    if (!type) { Py_DECREF(m); return NULL; }
    if (PyModule_AddObject(m, "Port", type) < 0) {
        Py_DECREF(type); Py_DECREF(m); return NULL;
    }

    /* Disconnected / Interrupted / Closed derive from Error, so `except
     * ps.Error` still catches everything the library raises. */
    PsError = ps_add_exc(m, "psy.serial.Error", "Error", NULL);
    if (!PsError) { Py_DECREF(m); return NULL; }
    PsDisconnected = ps_add_exc(m, "psy.serial.Disconnected", "Disconnected", PsError);
    if (!PsDisconnected) { Py_DECREF(m); return NULL; }
    PsInterrupted = ps_add_exc(m, "psy.serial.Interrupted", "Interrupted", PsError);
    if (!PsInterrupted) { Py_DECREF(m); return NULL; }
    PsClosed = ps_add_exc(m, "psy.serial.Closed", "Closed", PsError);
    if (!PsClosed) { Py_DECREF(m); return NULL; }

    /* parity */
    PyModule_AddIntConstant(m, "PARITY_NONE",  PSYS_PARITY_NONE);
    PyModule_AddIntConstant(m, "PARITY_ODD",   PSYS_PARITY_ODD);
    PyModule_AddIntConstant(m, "PARITY_EVEN",  PSYS_PARITY_EVEN);
    PyModule_AddIntConstant(m, "PARITY_MARK",  PSYS_PARITY_MARK);
    PyModule_AddIntConstant(m, "PARITY_SPACE", PSYS_PARITY_SPACE);
    /* stop bits */
    PyModule_AddIntConstant(m, "STOP_BITS_1", PSYS_STOP_BITS_1);
    PyModule_AddIntConstant(m, "STOP_BITS_2", PSYS_STOP_BITS_2);
    /* flow control */
    PyModule_AddIntConstant(m, "FLOW_NONE",    PSYS_FLOW_NONE);
    PyModule_AddIntConstant(m, "FLOW_RTSCTS",  PSYS_FLOW_RTSCTS);
    PyModule_AddIntConstant(m, "FLOW_XONXOFF", PSYS_FLOW_XONXOFF);
    /* input line mask (get_lines) */
    PyModule_AddIntConstant(m, "LINE_CTS", PSYS_LINE_CTS);
    PyModule_AddIntConstant(m, "LINE_DSR", PSYS_LINE_DSR);
    PyModule_AddIntConstant(m, "LINE_RI",  PSYS_LINE_RI);
    PyModule_AddIntConstant(m, "LINE_DCD", PSYS_LINE_DCD);
    /* purge flags (purge() takes rx/tx booleans; these name the raw bits) */
    PyModule_AddIntConstant(m, "PURGE_RX", PSYS_PURGE_RX);
    PyModule_AddIntConstant(m, "PURGE_TX", PSYS_PURGE_TX);
    /* read flags (read() takes all=; these name the raw values) */
    PyModule_AddIntConstant(m, "READ_ANY", PSYS_READ_ANY);
    PyModule_AddIntConstant(m, "READ_ALL", PSYS_READ_ALL);
    /* error codes, as carried by the exceptions' psys_strerror text */
    PyModule_AddIntConstant(m, "ERR_IO",           PSYS_ERR_IO);
    PyModule_AddIntConstant(m, "ERR_DISCONNECTED", PSYS_ERR_DISCONNECTED);
    PyModule_AddIntConstant(m, "ERR_CLOSED",       PSYS_ERR_CLOSED);
    PyModule_AddIntConstant(m, "ERR_INTERRUPTED",  PSYS_ERR_INTERRUPTED);
    PyModule_AddIntConstant(m, "ERR_ARG",          PSYS_ERR_ARG);
    /* async-worker policy actually obtained (Port.async_policy). The values
     * are psy_rt.h's psyrt_policy, which psy.parallel reports too, so one
     * constant means the same thing in both modules. TIME_CONSTRAINT is the
     * macOS rung. */
    PyModule_AddIntConstant(m, "ASYNC_NONE",            PSYRT_POLICY_NONE);
    PyModule_AddIntConstant(m, "ASYNC_DEADLINE",        PSYRT_POLICY_DEADLINE);
    PyModule_AddIntConstant(m, "ASYNC_TIME_CONSTRAINT", PSYRT_POLICY_TIME_CONSTRAINT);
    PyModule_AddIntConstant(m, "ASYNC_FIFO",            PSYRT_POLICY_FIFO);
    PyModule_AddIntConstant(m, "ASYNC_TIME_CRITICAL",   PSYRT_POLICY_TIME_CRITICAL);
    PyModule_AddIntConstant(m, "ASYNC_NORMAL",          PSYRT_POLICY_NORMAL);
    /* defaults */
    if (ps_add_uint(m, "TIMEOUT_INFINITE", PSYS_TIMEOUT_INFINITE) < 0 ||
        ps_add_uint(m, "DEFAULT_BAUD", PSYS_DEFAULT_BAUD) < 0 ||
        ps_add_uint(m, "DEFAULT_WRITE_TIMEOUT_MS", PSYS_DEFAULT_WRITE_TIMEOUT_MS) < 0 ||
        ps_add_uint(m, "DEFAULT_RT_RUNTIME_NS", PSYS_DEFAULT_RT_RUNTIME_NS) < 0 ||
        ps_add_uint(m, "DEFAULT_RT_DEADLINE_NS", PSYS_DEFAULT_RT_DEADLINE_NS) < 0 ||
        ps_add_uint(m, "DEFAULT_RT_PERIOD_NS", PSYS_DEFAULT_RT_PERIOD_NS) < 0) {
        Py_DECREF(m);
        return NULL;
    }

    return m;
}
