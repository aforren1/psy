/* psy_trials_ext.c - CPython extension wrapping psy_trials.h (module psy.trials)
 *
 * A thin, dependency-free binding (no nanobind/pybind/Cython): it needs only
 * Python.h. The library implementation is compiled directly into this module.
 *
 * Built against the stable ABI / Limited API (Py_LIMITED_API 3.8), so one
 * compiled psy/trials.abi3.so works across CPython >= 3.8; the Trials type is
 * a heap type made with PyType_FromSpec.
 *
 *     import psy.trials as pt
 *     t = pt.Trials(factors=[("orientation", 2), ("contrast", 5)], reps=20,
 *                   order=pt.ORDER_CONSTRAINED,
 *                   constraints=[pt.max_run(0, pt.ANY_LEVEL, 3)], rng=20260923)
 *     while (ti := t.next()) is not None:
 *         t.update(run_trial(t.level(ti.condition, 0), t.level(ti.condition, 1)))
 *
 * CALLBACKS. A track's is_done and a callable rng are Python; the header calls
 * them from next(), done, restore() and (rng) open() and requeue(). Those calls
 * keep the GIL. open() and load() release it when the generator is the
 * binding's own splitmix, since open() calls no track and the repair of a
 * tight design can take tens of milliseconds. An exception in a callback is
 * held until the C call returns and then raised; the callbacks after it in the
 * same call get a neutral answer (not done; u = 0).
 */
#ifndef Py_LIMITED_API
#define Py_LIMITED_API 0x03080000   /* target the CPython 3.8+ stable ABI */
#endif
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#define PSY_TRIALS_IMPLEMENTATION
#include "psy_trials.h"

#include <string.h>

static PyObject* TError;
static PyObject* TArgumentError;
static PyObject* TClosed;
static PyObject* TOutOfOrder;
static PyObject* TFull;

static PyObject* TInfoType;     /* namedtuple TrialInfo */
static PyObject* TTrialType;    /* namedtuple Trial     */

/* Heap-type instances visit their type in tp_traverse from 3.9 on only. */
static int g_visit_type = 1;

static PyObject* t_fail(int code) {
    PyObject* exc = TError;
    switch (code) {
    case PSYTR_ERR_ARG:    exc = TArgumentError; break;
    case PSYTR_ERR_CLOSED: exc = TClosed; break;
    case PSYTR_ERR_ORDER:  exc = TOutOfOrder; break;
    case PSYTR_ERR_FULL:   exc = TFull; break;
    default: break;
    }
    PyErr_SetString(exc, psytr_strerror(code));
    return NULL;
}

/* PyUnicode_AsUTF8 is Limited API only from 3.10. */
static const char* t_utf8(PyObject* s, PyObject** keep) {
    *keep = PyUnicode_AsUTF8String(s);
    return *keep ? PyBytes_AsString(*keep) : NULL;
}

/* ======================================================================= *
 *  Object
 * ======================================================================= */

struct TrialsObject;

/* What a track's is_done trampoline gets as ctx: the owner and the index. */
typedef struct {
    struct TrialsObject* owner;
    int index;
} t_track_ctx;

typedef struct TrialsObject {
    PyObject_HEAD
    PyObject* tracks[PSYTR_MAX_TRACKS];   /* the caller's track objects     */
    PyObject* rng_obj;                    /* callable rng, or NULL          */
    PyObject* names;                      /* list of bytes kept for names   */
    uint64_t  rng_state;                  /* splitmix state for an int rng  */
    int       own_rng;                    /* rng is the binding's splitmix  */
    int*      cond_reps;
    int*      warmup;
    unsigned char* records;               /* record_size x PSYTR_MAX_TRIALS */
    size_t    record_size;
    int       busy;
    PyObject *cb_type, *cb_value, *cb_tb;
    t_track_ctx track_ctx[PSYTR_MAX_TRACKS];
    psytr_trials t;
} TrialsObject;

#define TO(self) ((TrialsObject*)(self))

static int t_traverse(PyObject* self, visitproc visit, void* arg) {
    TrialsObject* o = TO(self);
    int i;
    for (i = 0; i < PSYTR_MAX_TRACKS; i++) Py_VISIT(o->tracks[i]);
    Py_VISIT(o->rng_obj);
    Py_VISIT(o->names);
    Py_VISIT(o->cb_type);
    Py_VISIT(o->cb_value);
    Py_VISIT(o->cb_tb);
    if (g_visit_type) Py_VISIT((PyObject*)Py_TYPE(self));
    return 0;
}

static int t_clear(PyObject* self) {
    TrialsObject* o = TO(self);
    int i;
    /* The handle points at the objects below; a cleared object must not be
     * driven again. */
    o->t.open = false;
    for (i = 0; i < PSYTR_MAX_TRACKS; i++) Py_CLEAR(o->tracks[i]);
    Py_CLEAR(o->rng_obj);
    Py_CLEAR(o->names);
    Py_CLEAR(o->cb_type);
    Py_CLEAR(o->cb_value);
    Py_CLEAR(o->cb_tb);
    return 0;
}

static void t_free_bufs(TrialsObject* o) {
    PyMem_Free(o->cond_reps); o->cond_reps = NULL;
    PyMem_Free(o->warmup);    o->warmup = NULL;
    PyMem_Free(o->records);   o->records = NULL;
    o->record_size = 0;
}

static void Trials_dealloc(PyObject* self) {
    PyTypeObject* tp = Py_TYPE(self);
    freefunc tp_free;
    PyObject_GC_UnTrack(self);
    t_clear(self);
    t_free_bufs(TO(self));
    tp_free = (freefunc)PyType_GetSlot(tp, Py_tp_free);
    tp_free(self);
    Py_DECREF(tp);
}

static void t_cb_store(TrialsObject* o) {
    if (o->cb_type || o->cb_value) { PyErr_Clear(); return; }
    PyErr_Fetch(&o->cb_type, &o->cb_value, &o->cb_tb);
}

static int t_enter(TrialsObject* o) {
    if (o->busy) {
        PyErr_SetString(TError, "this Trials is already in a call (another thread, or a "
                                "callback re-entering the Trials it runs under)");
        return -1;
    }
    o->busy = 1;
    return 0;
}

static int t_leave(TrialsObject* o) {
    o->busy = 0;
    if (o->cb_type || o->cb_value) {
        PyErr_Restore(o->cb_type, o->cb_value, o->cb_tb);
        o->cb_type = o->cb_value = o->cb_tb = NULL;
        return -1;
    }
    return 0;
}

/* --- trampolines -------------------------------------------------------- */

static double t_tramp_rng(void* ctx) {
    TrialsObject* o = (TrialsObject*)ctx;
    PyObject* r;
    double u;
    if (o->cb_type || o->cb_value) return 0.0;
    r = PyObject_CallObject(o->rng_obj, NULL);
    if (!r) { t_cb_store(o); return 0.0; }
    u = PyFloat_AsDouble(r);
    Py_DECREF(r);
    if (u == -1.0 && PyErr_Occurred()) { t_cb_store(o); return 0.0; }
    return u;
}

/* A track is an object with a callable is_done(), or with a `done`
 * attribute (psy.stair.Staircase, psy.quest.Quest, psy.gp.GP), which may
 * itself be callable. */
static bool t_tramp_done(void* ctx) {
    t_track_ctx* c = (t_track_ctx*)ctx;
    TrialsObject* o = c->owner;
    PyObject* obj = o->tracks[c->index];
    PyObject* r = NULL;
    int v;
    if (o->cb_type || o->cb_value || !obj) return false;
    if (PyObject_HasAttrString(obj, "is_done")) {
        r = PyObject_CallMethod(obj, "is_done", NULL);
    } else {
        r = PyObject_GetAttrString(obj, "done");
        if (r && PyCallable_Check(r)) {
            PyObject* called = PyObject_CallObject(r, NULL);
            Py_DECREF(r);
            r = called;
        }
    }
    if (!r) { t_cb_store(o); return false; }
    v = PyObject_IsTrue(r);
    Py_DECREF(r);
    if (v < 0) { t_cb_store(o); return false; }
    return v != 0;
}

/* ======================================================================= *
 *  Desc parsing
 * ======================================================================= */

static int t_int_list(PyObject* seq, int** out, int* n_out, const char* what) {
    Py_ssize_t n = PySequence_Size(seq), i;
    int* v;
    if (n < 0) {
        PyErr_Clear();
        PyErr_Format(PyExc_TypeError, "%s must be a sequence of ints", what);
        return -1;
    }
    v = (int*)PyMem_Malloc((size_t)(n > 0 ? n : 1) * sizeof(int));
    if (!v) { PyErr_NoMemory(); return -1; }
    for (i = 0; i < n; i++) {
        PyObject* it = PySequence_GetItem(seq, i);
        long x;
        if (!it) { PyMem_Free(v); return -1; }
        x = PyLong_AsLong(it);
        Py_DECREF(it);
        if (x == -1 && PyErr_Occurred()) { PyMem_Free(v); return -1; }
        v[i] = (int)x;
    }
    *out = v;
    *n_out = (int)n;
    return 0;
}

/* Index of a factor named `name` in the desc, or -1. */
static int t_factor_by_name(const psytr_desc* d, const char* name) {
    int f;
    for (f = 0; f < d->n_factors; f++)
        if (d->factors[f].name && strcmp(d->factors[f].name, name) == 0) return f;
    return -1;
}

static int t_dict_int(PyObject* dict, const char* key, int* out, int dflt) {
    PyObject* v = PyDict_GetItemString(dict, key);
    long x;
    if (!v) { *out = dflt; return 0; }
    x = PyLong_AsLong(v);
    if (x == -1 && PyErr_Occurred()) return -1;
    *out = (int)x;
    return 0;
}

/* A constraint is the dict a helper returns: rule, factor (index or factor
 * name), level, level2, n, window. */
static int t_parse_constraint(PyObject* c, const psytr_desc* d, psytr_constraint* out, int ci) {
    PyObject* f;
    int rule;
    if (!PyDict_Check(c)) {
        PyErr_Format(PyExc_TypeError, "constraints[%d] must come from max_run(), "
                     "max_in_window(), min_gap(), no_transition() or first_not()", ci);
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (t_dict_int(c, "rule", &rule, -1) < 0) return -1;
    out->rule = (psytr_rule)rule;
    f = PyDict_GetItemString(c, "factor");
    if (f && PyUnicode_Check(f)) {
        PyObject* keep;
        const char* name = t_utf8(f, &keep);
        if (!name) return -1;
        out->factor = t_factor_by_name(d, name);
        if (out->factor < 0) {
            PyErr_Format(TArgumentError, "constraints[%d] names factor '%s', which is not in "
                         "factors", ci, name);
            Py_DECREF(keep);
            return -1;
        }
        Py_DECREF(keep);
    } else if (t_dict_int(c, "factor", &out->factor, PSYTR_CONDITION) < 0) {
        return -1;
    }
    if (t_dict_int(c, "level", &out->level, 0) < 0 ||
        t_dict_int(c, "level2", &out->level2, 0) < 0 ||
        t_dict_int(c, "n", &out->n, 0) < 0 ||
        t_dict_int(c, "window", &out->window, 0) < 0)
        return -1;
    return 0;
}

static int t_build(TrialsObject* o, PyObject* args, PyObject* kwds, psytr_desc* d) {
    static char* kw[] = { "n_conditions", "factors", "reps", "cond_reps", "order",
                          "constraints", "max_swaps", "tracks", "interleave",
                          "track_rate", "block_size", "constraints_span_blocks",
                          "n_practice", "n_warmup", "warmup_conditions", "requeue_gap",
                          "rng", "record_size", NULL };
    PyObject *factors = Py_None, *cond_reps = Py_None, *constraints = Py_None,
             *tracks = Py_None, *warmup = Py_None, *rng = Py_None;
    int order = 0, interleave = 0, span = 0;
    Py_ssize_t record_size = 0, n, i;

    memset(d, 0, sizeof(*d));
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|$iOiOiOiOidipiiOiOn", kw,
                                     &d->n_conditions, &factors, &d->reps, &cond_reps,
                                     &order, &constraints, &d->max_swaps, &tracks,
                                     &interleave, &d->track_rate, &d->block_size, &span,
                                     &d->n_practice, &d->n_warmup, &warmup,
                                     &d->requeue_gap, &rng, &record_size))
        return -1;
    d->order = (psytr_order)order;
    d->interleave = (psytr_interleave)interleave;
    d->constraints_span_blocks = span ? true : false;

    /* Factors: (name, n_levels) pairs. The names are kept as bytes on the
     * object because the header reads them after open. */
    o->names = PyList_New(0);
    if (!o->names) return -1;
    if (factors != Py_None) {
        n = PySequence_Size(factors);
        if (n < 0) { PyErr_Clear(); PyErr_SetString(PyExc_TypeError, "factors must be a list of (name, n_levels)"); return -1; }
        if (n > PSYTR_MAX_FACTORS) {
            PyErr_Format(TArgumentError, "%zd factors; the maximum is %d", n, PSYTR_MAX_FACTORS);
            return -1;
        }
        for (i = 0; i < n; i++) {
            PyObject* it = PySequence_GetItem(factors, i);
            PyObject *name_o, *nl_o;
            long nl;
            if (!it) return -1;
            if (PySequence_Size(it) != 2) {
                Py_DECREF(it);
                PyErr_Clear();
                PyErr_Format(PyExc_TypeError, "factors[%zd] must be (name, n_levels)", i);
                return -1;
            }
            name_o = PySequence_GetItem(it, 0);
            nl_o = PySequence_GetItem(it, 1);
            Py_DECREF(it);
            if (!name_o || !nl_o) { Py_XDECREF(name_o); Py_XDECREF(nl_o); return -1; }
            nl = PyLong_AsLong(nl_o);
            Py_DECREF(nl_o);
            if (nl == -1 && PyErr_Occurred()) { Py_DECREF(name_o); return -1; }
            d->factors[i].n_levels = (int)nl;
            if (name_o != Py_None) {
                PyObject* b;
                if (!PyUnicode_Check(name_o)) {
                    Py_DECREF(name_o);
                    PyErr_Format(PyExc_TypeError, "factors[%zd]: the name must be a str or None", i);
                    return -1;
                }
                b = PyUnicode_AsUTF8String(name_o);
                Py_DECREF(name_o);
                if (!b) return -1;
                if (PyList_Append(o->names, b) < 0) { Py_DECREF(b); return -1; }
                d->factors[i].name = PyBytes_AsString(b);
                Py_DECREF(b);   /* the list holds it */
            } else {
                Py_DECREF(name_o);
            }
        }
        d->n_factors = (int)n;
    }

    if (cond_reps != Py_None) {
        int nc;
        if (t_int_list(cond_reps, &o->cond_reps, &nc, "cond_reps") < 0) return -1;
        /* The header reads one entry per condition; a short list would be
         * read past its end, so the count is checked against the rows. */
        {
            int rows = d->n_conditions, f;
            if (rows == 0 && d->n_factors > 0) {
                rows = 1;
                for (f = 0; f < d->n_factors; f++) rows *= d->factors[f].n_levels > 0 ? d->factors[f].n_levels : 1;
            }
            if (nc != rows) {
                PyErr_Format(TArgumentError, "cond_reps has %d entries, the design has %d conditions",
                             nc, rows);
                return -1;
            }
        }
        d->cond_reps = o->cond_reps;
    }
    if (warmup != Py_None) {
        if (t_int_list(warmup, &o->warmup, &d->n_warmup_conditions, "warmup_conditions") < 0)
            return -1;
        d->warmup_conditions = o->warmup;
    }

    if (constraints != Py_None) {
        n = PySequence_Size(constraints);
        if (n < 0) { PyErr_Clear(); PyErr_SetString(PyExc_TypeError, "constraints must be a list"); return -1; }
        if (n > PSYTR_MAX_CONSTRAINTS) {
            PyErr_Format(TArgumentError, "%zd constraints; the maximum is %d", n, PSYTR_MAX_CONSTRAINTS);
            return -1;
        }
        for (i = 0; i < n; i++) {
            PyObject* it = PySequence_GetItem(constraints, i);
            int rc;
            if (!it) return -1;
            rc = t_parse_constraint(it, d, &d->constraints[i], (int)i);
            Py_DECREF(it);
            if (rc < 0) return -1;
        }
        d->n_constraints = (int)n;
    }

    /* Tracks: an object, or (object, weight). */
    if (tracks != Py_None) {
        n = PySequence_Size(tracks);
        if (n < 0) { PyErr_Clear(); PyErr_SetString(PyExc_TypeError, "tracks must be a list"); return -1; }
        if (n > PSYTR_MAX_TRACKS) {
            PyErr_Format(TArgumentError, "%zd tracks; the maximum is %d", n, PSYTR_MAX_TRACKS);
            return -1;
        }
        for (i = 0; i < n; i++) {
            PyObject* it = PySequence_GetItem(tracks, i);
            PyObject* obj = it;
            double w = 0.0;
            if (!it) return -1;
            if (PyTuple_Check(it) && PyTuple_Size(it) == 2) {
                obj = PyTuple_GetItem(it, 0);
                w = PyFloat_AsDouble(PyTuple_GetItem(it, 1));
                if (w == -1.0 && PyErr_Occurred()) { Py_DECREF(it); return -1; }
            }
            if (!PyObject_HasAttrString(obj, "is_done") && !PyObject_HasAttrString(obj, "done")) {
                Py_DECREF(it);
                PyErr_Format(PyExc_TypeError, "tracks[%zd] has neither is_done() nor done", i);
                return -1;
            }
            Py_INCREF(obj);
            o->tracks[i] = obj;
            Py_DECREF(it);
            o->track_ctx[i].owner = o;
            o->track_ctx[i].index = (int)i;
            d->tracks[i] = psytr_track(&o->track_ctx[i], t_tramp_done);
            d->tracks[i].weight = w;
        }
        d->n_tracks = (int)n;
    }

    /* rng: a callable, or an int that seeds the binding's splitmix state. */
    if (rng != Py_None) {
        if (PyLong_Check(rng)) {
            unsigned long long s = PyLong_AsUnsignedLongLongMask(rng);
            if (s == (unsigned long long)-1 && PyErr_Occurred()) return -1;
            o->rng_state = (uint64_t)s;
            o->own_rng = 1;
            d->rng = psytr_splitmix;
            d->rng_ctx = &o->rng_state;
        } else if (PyCallable_Check(rng)) {
            Py_INCREF(rng);
            o->rng_obj = rng;
            d->rng = t_tramp_rng;
            d->rng_ctx = o;
        } else {
            PyErr_SetString(PyExc_TypeError, "rng must be a callable or an int seed");
            return -1;
        }
    }

    if (record_size < 0) { PyErr_SetString(TArgumentError, "record_size must be >= 0"); return -1; }
    if (record_size > 0) {
        if ((size_t)record_size > ((size_t)-1) / PSYTR_MAX_TRIALS / 2) {
            PyErr_SetString(TArgumentError, "record_size is too large");
            return -1;
        }
        o->records = (unsigned char*)PyMem_Calloc(PSYTR_MAX_TRIALS, (size_t)record_size);
        if (!o->records) { PyErr_NoMemory(); return -1; }
        o->record_size = (size_t)record_size;
        d->records = o->records;
        d->record_size = (size_t)record_size;
    }
    return 0;
}

/* Reset the Python-side state before a (re)build. */
static void t_reset(TrialsObject* o) {
    t_clear((PyObject*)o);
    t_free_bufs(o);
    o->own_rng = 0;
    o->rng_state = 0;
    memset(o->track_ctx, 0, sizeof(o->track_ctx));
}

static int Trials_init(PyObject* self, PyObject* args, PyObject* kwds) {
    TrialsObject* o = TO(self);
    psytr_desc d;
    bool ok;
    if (t_enter(o) < 0) return -1;
    o->busy = 0;
    t_reset(o);
    if (t_build(o, args, kwds, &d) < 0) { t_reset(o); return -1; }
    o->busy = 1;
    if (o->rng_obj) {
        ok = psytr_open(&o->t, &d);
    } else {
        /* open() calls no track, and the rng is C: nothing needs the GIL. */
        Py_BEGIN_ALLOW_THREADS
        ok = psytr_open(&o->t, &d);
        Py_END_ALLOW_THREADS
    }
    if (t_leave(o) < 0) { o->t.open = false; t_reset(o); return -1; }
    if (!ok) {
        /* An unmet constraint is a property of the design, not a bad
         * argument: it is Error, as the header's message says. */
        const char* msg = psytr_error(&o->t);
        PyErr_SetString(strstr(msg, "is still broken") ? TError : TArgumentError, msg);
        t_reset(o);
        return -1;
    }
    return 0;
}

/* ======================================================================= *
 *  Methods
 * ======================================================================= */

static int t_open_enter(TrialsObject* o) {
    if (t_enter(o) < 0) return -1;
    if (!psytr_is_open(&o->t)) {
        o->busy = 0;
        t_fail(PSYTR_ERR_CLOSED);
        return -1;
    }
    return 0;
}

static PyObject* t_info(const psytr_trial_info* ti) {
    return PyObject_CallFunction(TInfoType, "iiiiiOOOOO", ti->index, ti->condition, ti->track,
                                 ti->rep, ti->block,
                                 ti->first_in_block ? Py_True : Py_False,
                                 ti->after_break ? Py_True : Py_False,
                                 ti->practice ? Py_True : Py_False,
                                 ti->warmup ? Py_True : Py_False,
                                 ti->requeued ? Py_True : Py_False);
}

static PyObject* Trials_next(PyObject* self, PyObject* Py_UNUSED(ignored)) {
    TrialsObject* o = TO(self);
    psytr_trial_info ti;
    int rc;
    if (t_open_enter(o) < 0) return NULL;
    rc = psytr_next(&o->t, &ti);
    if (t_leave(o) < 0) return NULL;
    if (rc == PSYTR_DONE) Py_RETURN_NONE;
    if (rc < 0) return t_fail(rc);
    return t_info(&ti);
}

/* Borrow the bytes of a record; NULL (no error) for None. */
static int t_record_arg(TrialsObject* o, PyObject* rec, PyObject** keep, const char** p) {
    *keep = NULL;
    *p = NULL;
    if (!rec || rec == Py_None) return 0;
    if (o->record_size == 0) return 0;   /* the header ignores it; so do we */
    *keep = PyBytes_FromObject(rec);
    if (!*keep) return -1;
    if ((size_t)PyBytes_Size(*keep) != o->record_size) {
        PyErr_Format(TArgumentError, "the record has %zd bytes; record_size is %zu",
                     PyBytes_Size(*keep), o->record_size);
        Py_CLEAR(*keep);
        return -1;
    }
    *p = PyBytes_AsString(*keep);
    return 0;
}

static PyObject* Trials_update(PyObject* self, PyObject* args, PyObject* kwds) {
    static char* kw[] = { "outcome", "rec", NULL };
    TrialsObject* o = TO(self);
    int outcome, rc;
    PyObject *rec = Py_None, *keep;
    const char* p;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "i|O", kw, &outcome, &rec)) return NULL;
    if (t_record_arg(o, rec, &keep, &p) < 0) return NULL;
    if (t_open_enter(o) < 0) { Py_XDECREF(keep); return NULL; }
    rc = psytr_update(&o->t, outcome, p);
    o->busy = 0;
    Py_XDECREF(keep);
    if (rc < 0) return t_fail(rc);
    Py_RETURN_NONE;
}

static PyObject* Trials_requeue(PyObject* self, PyObject* Py_UNUSED(ignored)) {
    TrialsObject* o = TO(self);
    int rc;
    if (t_open_enter(o) < 0) return NULL;
    rc = psytr_requeue(&o->t);
    if (t_leave(o) < 0) return NULL;
    if (rc < 0) return t_fail(rc);
    Py_RETURN_NONE;
}

static PyObject* Trials_mark_break(PyObject* self, PyObject* Py_UNUSED(ignored)) {
    TrialsObject* o = TO(self);
    int rc;
    if (t_open_enter(o) < 0) return NULL;
    rc = psytr_mark_break(&o->t);
    o->busy = 0;
    if (rc < 0) return t_fail(rc);
    Py_RETURN_NONE;
}

static PyObject* Trials_level(PyObject* self, PyObject* args, PyObject* kwds) {
    static char* kw[] = { "condition", "factor", NULL };
    TrialsObject* o = TO(self);
    int c, f, rc;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "ii", kw, &c, &f)) return NULL;
    if (t_open_enter(o) < 0) return NULL;
    o->busy = 0;
    rc = psytr_level(&o->t, c, f);
    if (rc < 0) return t_fail(rc);
    return PyLong_FromLong(rc);
}

static PyObject* Trials_levels(PyObject* self, PyObject* arg) {
    TrialsObject* o = TO(self);
    long c = PyLong_AsLong(arg);
    int nf, f;
    PyObject* t;
    if (c == -1 && PyErr_Occurred()) return NULL;
    if (t_open_enter(o) < 0) return NULL;
    o->busy = 0;
    nf = psytr_n_factors(&o->t);
    t = PyTuple_New(nf);
    if (!t) return NULL;
    for (f = 0; f < nf; f++) {
        int v = psytr_level(&o->t, (int)c, f);
        if (v < 0) { Py_DECREF(t); return t_fail(v); }
        PyTuple_SetItem(t, f, PyLong_FromLong(v));
    }
    return t;
}

static PyObject* Trials_condition_from_levels(PyObject* self, PyObject* arg) {
    TrialsObject* o = TO(self);
    int* v = NULL;
    int n, rc;
    if (t_open_enter(o) < 0) return NULL;
    o->busy = 0;
    if (t_int_list(arg, &v, &n, "levels") < 0) return NULL;
    if (n != psytr_n_factors(&o->t)) {
        PyMem_Free(v);
        PyErr_Format(TArgumentError, "levels has %d entries; the design has %d factors",
                     n, psytr_n_factors(&o->t));
        return NULL;
    }
    rc = psytr_condition_from_levels(&o->t, v);
    PyMem_Free(v);
    if (rc < 0) return t_fail(rc);
    return PyLong_FromLong(rc);
}

static PyObject* Trials_condition_at(PyObject* self, PyObject* arg) {
    TrialsObject* o = TO(self);
    long i = PyLong_AsLong(arg);
    int rc;
    if (i == -1 && PyErr_Occurred()) return NULL;
    if (t_open_enter(o) < 0) return NULL;
    o->busy = 0;
    if (i < 0 || i > 0x7FFFFFFF) return t_fail(PSYTR_ERR_ARG);
    rc = psytr_condition_at(&o->t, (int)i);
    if (rc < 0) return t_fail(rc);
    return PyLong_FromLong(rc);
}

static PyObject* Trials_schedule(PyObject* self, PyObject* Py_UNUSED(ignored)) {
    TrialsObject* o = TO(self);
    int n, i;
    PyObject* list;
    if (t_open_enter(o) < 0) return NULL;
    o->busy = 0;
    n = psytr_n_scheduled(&o->t);
    list = PyList_New(n);
    if (!list) return NULL;
    for (i = 0; i < n; i++) PyList_SetItem(list, i, PyLong_FromLong(psytr_condition_at(&o->t, i)));
    return list;
}

static int t_cond_arg(TrialsObject* o, PyObject* arg, int* c) {
    long v = PyLong_AsLong(arg);
    if (v == -1 && PyErr_Occurred()) return -1;
    if (v < 0 || v >= psytr_n_conditions(&o->t)) {
        PyErr_Format(PyExc_IndexError, "condition %ld out of range", v);
        return -1;
    }
    *c = (int)v;
    return 0;
}

static PyObject* Trials_n_valid(PyObject* self, PyObject* arg) {
    TrialsObject* o = TO(self);
    int c;
    if (t_open_enter(o) < 0) return NULL;
    o->busy = 0;
    if (t_cond_arg(o, arg, &c) < 0) return NULL;
    return PyLong_FromLong(psytr_n_valid(&o->t, c));
}

static PyObject* Trials_count(PyObject* self, PyObject* args, PyObject* kwds) {
    static char* kw[] = { "condition", "outcome", NULL };
    TrialsObject* o = TO(self);
    PyObject* c_o;
    int c, outcome = 1, rc;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|i", kw, &c_o, &outcome)) return NULL;
    if (t_open_enter(o) < 0) return NULL;
    o->busy = 0;
    if (t_cond_arg(o, c_o, &c) < 0) return NULL;
    rc = psytr_count(&o->t, c, outcome);
    if (rc < 0) return t_fail(rc);
    return PyLong_FromLong(rc);
}

static PyObject* Trials_proportion(PyObject* self, PyObject* args, PyObject* kwds) {
    static char* kw[] = { "condition", "outcome", NULL };
    TrialsObject* o = TO(self);
    PyObject* c_o;
    int c, outcome = 1;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|i", kw, &c_o, &outcome)) return NULL;
    if (t_open_enter(o) < 0) return NULL;
    o->busy = 0;
    if (t_cond_arg(o, c_o, &c) < 0) return NULL;
    return PyFloat_FromDouble(psytr_proportion(&o->t, c, outcome));
}

static PyObject* Trials_history(PyObject* self, PyObject* Py_UNUSED(ignored)) {
    TrialsObject* o = TO(self);
    const psytr_trial* h;
    int n = 0, i;
    PyObject* list;
    if (t_open_enter(o) < 0) return NULL;
    o->busy = 0;
    h = psytr_history(&o->t, &n);
    list = PyList_New(n);
    if (!list) return NULL;
    for (i = 0; i < n; i++) {
        unsigned f = h[i].flags;
        PyObject* tr = PyObject_CallFunction(
            TTrialType, "iiiiiiIOOOOOOO", i, (int)h[i].condition, (int)h[i].track,
            (int)h[i].rep, (int)h[i].block, (int)h[i].outcome, f,
            (f & PSYTR_FLAG_PRACTICE) ? Py_True : Py_False,
            (f & PSYTR_FLAG_WARMUP) ? Py_True : Py_False,
            (f & PSYTR_FLAG_REQUEUED) ? Py_True : Py_False,
            (f & PSYTR_FLAG_AFTER_BREAK) ? Py_True : Py_False,
            (f & PSYTR_FLAG_FIRST_IN_BLOCK) ? Py_True : Py_False,
            (f & PSYTR_FLAG_VIOLATION) ? Py_True : Py_False,
            (f & PSYTR_FLAG_DONE) ? Py_True : Py_False);
        if (!tr) { Py_DECREF(list); return NULL; }
        PyList_SetItem(list, i, tr);
    }
    return list;
}

static PyObject* Trials_record(PyObject* self, PyObject* arg) {
    TrialsObject* o = TO(self);
    long i = PyLong_AsLong(arg);
    const void* p;
    if (i == -1 && PyErr_Occurred()) return NULL;
    if (t_open_enter(o) < 0) return NULL;
    o->busy = 0;
    if (o->record_size == 0) { PyErr_SetString(TArgumentError, "the session has no records (record_size 0)"); return NULL; }
    if (i < 0 || i >= psytr_n_run(&o->t)) {
        PyErr_Format(PyExc_IndexError, "trial %ld out of range", i);
        return NULL;
    }
    p = psytr_record(&o->t, (int)i);
    if (!p) return t_fail(PSYTR_ERR_ARG);
    return PyBytes_FromStringAndSize((const char*)p, (Py_ssize_t)o->record_size);
}

/* Call a snprintf-semantics formatter into a buffer that grows to fit. */
typedef int (*t_fmt_fn)(const psytr_trials*, int, char*, size_t);

static int t_fmt_header(const psytr_trials* t, int i, char* b, size_t c) { (void)i; return psytr_format_header(t, b, c); }
static int t_fmt_meta(const psytr_trials* t, int i, char* b, size_t c) { (void)i; return psytr_format_meta(t, b, c); }

static PyObject* t_format(TrialsObject* o, t_fmt_fn fn, int i) {
    char small[512];
    int n;
    PyObject* r;
    if (t_open_enter(o) < 0) return NULL;
    o->busy = 0;
    n = fn(&o->t, i, small, sizeof(small));
    if (n < 0) return t_fail(n);
    if ((size_t)n < sizeof(small)) return PyUnicode_FromStringAndSize(small, n);
    {
        char* big = (char*)PyMem_Malloc((size_t)n + 1);
        if (!big) return PyErr_NoMemory();
        n = fn(&o->t, i, big, (size_t)n + 1);
        r = n < 0 ? t_fail(n) : PyUnicode_FromStringAndSize(big, n);
        PyMem_Free(big);
        return r;
    }
}

static PyObject* Trials_format_row(PyObject* self, PyObject* arg) {
    long i = PyLong_AsLong(arg);
    if (i == -1 && PyErr_Occurred()) return NULL;
    if (i < 0 || i > 0x7FFFFFFF) return t_fail(PSYTR_ERR_ARG);
    return t_format(TO(self), psytr_format_row, (int)i);
}
static PyObject* Trials_format_header(PyObject* self, PyObject* Py_UNUSED(ignored)) {
    return t_format(TO(self), t_fmt_header, 0);
}
static PyObject* Trials_format_meta(PyObject* self, PyObject* Py_UNUSED(ignored)) {
    return t_format(TO(self), t_fmt_meta, 0);
}

static PyObject* Trials_save(PyObject* self, PyObject* Py_UNUSED(ignored)) {
    TrialsObject* o = TO(self);
    size_t n;
    char* buf;
    int rc;
    PyObject* r;
    if (t_open_enter(o) < 0) return NULL;
    o->busy = 0;
    n = psytr_save_size(&o->t);
    buf = (char*)PyMem_Malloc(n > 0 ? n : 1);
    if (!buf) return PyErr_NoMemory();
    rc = psytr_save(&o->t, buf, n);
    if (rc < 0) { PyMem_Free(buf); return t_fail(rc); }
    r = PyBytes_FromStringAndSize(buf, rc);
    PyMem_Free(buf);
    return r;
}

/* Trials.load(data, **desc): a new Trials rebuilt from a snapshot. */
static PyObject* Trials_load(PyObject* cls, PyObject* args, PyObject* kwds) {
    PyObject *data, *bytes, *self, *rest;
    TrialsObject* o;
    psytr_desc d;
    bool ok;
    Py_ssize_t na = PyTuple_Size(args);
    if (na != 1) {
        PyErr_SetString(PyExc_TypeError, "load(data, **desc) takes the snapshot bytes and the desc as keywords");
        return NULL;
    }
    data = PyTuple_GetItem(args, 0);
    bytes = PyBytes_FromObject(data);
    if (!bytes) return NULL;
    self = PyObject_CallMethod(cls, "__new__", "O", cls);
    if (!self) { Py_DECREF(bytes); return NULL; }
    o = TO(self);
    rest = PyTuple_New(0);
    if (!rest) { Py_DECREF(bytes); Py_DECREF(self); return NULL; }
    if (t_build(o, rest, kwds, &d) < 0) {
        Py_DECREF(rest); Py_DECREF(bytes); Py_DECREF(self);
        return NULL;
    }
    Py_DECREF(rest);
    o->busy = 1;
    /* load() draws nothing and calls no track. */
    {
        const char* p = PyBytes_AsString(bytes);
        size_t len = (size_t)PyBytes_Size(bytes);
        Py_BEGIN_ALLOW_THREADS
        ok = psytr_load(&o->t, &d, p, len);
        Py_END_ALLOW_THREADS
    }
    o->busy = 0;
    Py_DECREF(bytes);
    if (!ok) {
        PyErr_SetString(TError, psytr_error(&o->t));
        Py_DECREF(self);
        return NULL;
    }
    return self;
}

static PyObject* Trials_restore(PyObject* self, PyObject* args, PyObject* kwds) {
    static char* kw[] = { "outcomes", "records", NULL };
    TrialsObject* o = TO(self);
    PyObject *outs, *recs = Py_None, *blob = NULL;
    int* v = NULL;
    int n, rc;
    const char* rp = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|O", kw, &outs, &recs)) return NULL;
    if (t_int_list(outs, &v, &n, "outcomes") < 0) return NULL;
    if (recs != Py_None && o->record_size > 0) {
        /* One bytes-like of n * record_size, or a sequence of n records. */
        if (PyBytes_Check(recs) || PyByteArray_Check(recs) || PyMemoryView_Check(recs)) {
            blob = PyBytes_FromObject(recs);
        } else {
            PyObject* empty = PyBytes_FromStringAndSize("", 0);
            PyObject* joined = empty ? PyObject_CallMethod(empty, "join", "O", recs) : NULL;
            Py_XDECREF(empty);
            blob = joined;
        }
        if (!blob) { PyMem_Free(v); return NULL; }
        if ((size_t)PyBytes_Size(blob) != (size_t)n * o->record_size) {
            PyErr_Format(TArgumentError, "records hold %zd bytes; %d trials of %zu need %zu",
                         PyBytes_Size(blob), n, o->record_size, (size_t)n * o->record_size);
            Py_DECREF(blob);
            PyMem_Free(v);
            return NULL;
        }
        rp = PyBytes_AsString(blob);
    }
    if (t_open_enter(o) < 0) { Py_XDECREF(blob); PyMem_Free(v); return NULL; }
    rc = psytr_restore(&o->t, v, rp, n);
    PyMem_Free(v);
    Py_XDECREF(blob);
    if (t_leave(o) < 0) return NULL;
    if (rc < 0) return t_fail(rc);
    Py_RETURN_NONE;
}

/* --- properties --------------------------------------------------------- */

static PyObject* Trials_get_done(PyObject* self, void* Py_UNUSED(c)) {
    TrialsObject* o = TO(self);
    bool d;
    if (t_enter(o) < 0) return NULL;
    d = psytr_done(&o->t);
    if (t_leave(o) < 0) return NULL;
    return PyBool_FromLong(d);
}
#define T_INT_GETTER(name, expr)                                              \
    static PyObject* Trials_get_##name(PyObject* self, void* Py_UNUSED(c)) {  \
        return PyLong_FromLong((long)(expr));                                 \
    }
T_INT_GETTER(n_conditions, psytr_n_conditions(&TO(self)->t))
T_INT_GETTER(n_factors, psytr_n_factors(&TO(self)->t))
T_INT_GETTER(n_scheduled, psytr_n_scheduled(&TO(self)->t))
T_INT_GETTER(n_run, psytr_n_run(&TO(self)->t))
T_INT_GETTER(n_done, psytr_n_done(&TO(self)->t))
T_INT_GETTER(swaps, psytr_is_open(&TO(self)->t) ? TO(self)->t.swaps : 0)

static PyObject* Trials_get_is_open(PyObject* self, void* Py_UNUSED(c)) {
    return PyBool_FromLong(psytr_is_open(&TO(self)->t));
}
static PyObject* Trials_get_record_size(PyObject* self, void* Py_UNUSED(c)) {
    return PyLong_FromSize_t(TO(self)->record_size);
}
static PyObject* Trials_get_rng_state(PyObject* self, void* Py_UNUSED(c)) {
    TrialsObject* o = TO(self);
    if (!o->own_rng) Py_RETURN_NONE;
    return PyLong_FromUnsignedLongLong(o->rng_state);
}
static int Trials_set_rng_state(PyObject* self, PyObject* v, void* Py_UNUSED(c)) {
    TrialsObject* o = TO(self);
    unsigned long long s;
    if (!v) { PyErr_SetString(PyExc_TypeError, "rng_state cannot be deleted"); return -1; }
    if (!o->own_rng) {
        PyErr_SetString(TError, "rng_state exists only when rng was an int seed");
        return -1;
    }
    s = PyLong_AsUnsignedLongLongMask(v);
    if (s == (unsigned long long)-1 && PyErr_Occurred()) return -1;
    o->rng_state = (uint64_t)s;
    return 0;
}

static PyGetSetDef Trials_getset[] = {
    { "done", Trials_get_done, NULL,
      "True when nothing is pending or scheduled and every track is done. "
      "Asks the tracks.", NULL },
    { "n_conditions", Trials_get_n_conditions, NULL, "Condition rows.", NULL },
    { "n_factors", Trials_get_n_factors, NULL, "Factors.", NULL },
    { "n_scheduled", Trials_get_n_scheduled, NULL,
      "Schedule slots: practice, then main, re-queued copies included.", NULL },
    { "n_run", Trials_get_n_run, NULL, "Trials handed out, a pending one included.", NULL },
    { "n_done", Trials_get_n_done, NULL, "Trials updated or re-queued.", NULL },
    { "swaps", Trials_get_swaps, NULL, "Swaps the constraint repair used at open.", NULL },
    { "is_open", Trials_get_is_open, NULL, "True after a successful open or load.", NULL },
    { "record_size", Trials_get_record_size, NULL, "Bytes per trial record.", NULL },
    { "rng_state", Trials_get_rng_state, Trials_set_rng_state,
      "The splitmix64 state when rng was an int seed (None otherwise). Save it "
      "beside save(); pass it as rng= to load().", NULL },
    { NULL }
};

static PyMethodDef Trials_methods[] = {
    { "next", Trials_next, METH_NOARGS,
      "next() -> TrialInfo | None: the next trial, or None when the session is "
      "done. Repeats the pending trial until update() or requeue()." },
    { "update", (PyCFunction)(void (*)(void))Trials_update, METH_VARARGS | METH_KEYWORDS,
      "update(outcome, rec=None): record the pending trial's outcome (>= 0, or "
      "INVALID) and its record_size-byte record (None zero-fills)." },
    { "requeue", Trials_requeue, METH_NOARGS,
      "requeue(): instead of update(), record REQUEUE and schedule the "
      "condition again. Scheduled trials only." },
    { "mark_break", Trials_mark_break, METH_NOARGS,
      "mark_break(): a break was taken; flags the pending or next trial and "
      "starts a constraint segment." },
    { "level", (PyCFunction)(void (*)(void))Trials_level, METH_VARARGS | METH_KEYWORDS,
      "level(condition, factor) -> int: the factor's level index in that row." },
    { "levels", Trials_levels, METH_O,
      "levels(condition) -> tuple[int]: every factor's level in that row." },
    { "condition_from_levels", Trials_condition_from_levels, METH_O,
      "condition_from_levels(levels) -> int: the row with these level indices." },
    { "condition_at", Trials_condition_at, METH_O,
      "condition_at(slot) -> int: the condition in schedule slot `slot`." },
    { "schedule", Trials_schedule, METH_NOARGS,
      "schedule() -> list[int]: condition_at() for every slot." },
    { "n_valid", Trials_n_valid, METH_O,
      "n_valid(condition) -> int: valid, non-practice, non-warmup trials." },
    { "count", (PyCFunction)(void (*)(void))Trials_count, METH_VARARGS | METH_KEYWORDS,
      "count(condition, outcome=1) -> int" },
    { "proportion", (PyCFunction)(void (*)(void))Trials_proportion, METH_VARARGS | METH_KEYWORDS,
      "proportion(condition, outcome=1) -> float: count / n_valid; NaN when "
      "n_valid is 0." },
    { "history", Trials_history, METH_NOARGS,
      "history() -> list[Trial]: every trial handed out, a pending one included." },
    { "record", Trials_record, METH_O, "record(i) -> bytes: trial i's record." },
    { "format_row", Trials_format_row, METH_O,
      "format_row(i) -> str: one newline-terminated CSV line." },
    { "format_header", Trials_format_header, METH_NOARGS,
      "format_header() -> str: the matching CSV header line." },
    { "format_meta", Trials_format_meta, METH_NOARGS,
      "format_meta() -> str: one key=value line describing the session." },
    { "save", Trials_save, METH_NOARGS, "save() -> bytes: a snapshot of the session." },
    { "load", (PyCFunction)(void (*)(void))Trials_load, METH_VARARGS | METH_KEYWORDS | METH_CLASS,
      "Trials.load(data, **desc) -> Trials: rebuild a session from save()'s "
      "bytes. The desc must match the saved one; it re-supplies the tracks, "
      "the rng (for an int seed, pass the rng_state saved with the snapshot) "
      "and record_size. Raises Error with the header's message on a mismatch." },
    { "restore", (PyCFunction)(void (*)(void))Trials_restore, METH_VARARGS | METH_KEYWORDS,
      "restore(outcomes, records=None): replay outcomes on a fresh session "
      "with the same desc and seed. REQUEUE re-queues." },
    { NULL }
};

static PyType_Slot Trials_slots[] = {
    { Py_tp_doc, (void*)
      "Trials(*, n_conditions=0, factors=None, reps=0, cond_reps=None, "
      "order=ORDER_SEQUENTIAL, constraints=None, max_swaps=0, tracks=None, "
      "interleave=INTERLEAVE_RANDOM, track_rate=0.0, block_size=0, "
      "constraints_span_blocks=False, n_practice=0, n_warmup=0, "
      "warmup_conditions=None, requeue_gap=0, rng=None, record_size=0)\n\n"
      "One psy_trials.h session. factors is a list of (name, n_levels); "
      "constraints come from max_run() and friends; tracks are objects with "
      "is_done() or a done attribute, or (object, weight); rng is a callable "
      "returning [0, 1) or an int seed for the header's splitmix64." },
    { Py_tp_new, (void*)PyType_GenericNew },
    { Py_tp_init, (void*)Trials_init },
    { Py_tp_dealloc, (void*)Trials_dealloc },
    { Py_tp_traverse, (void*)t_traverse },
    { Py_tp_clear, (void*)t_clear },
    { Py_tp_methods, Trials_methods },
    { Py_tp_getset, Trials_getset },
    { 0, NULL }
};

static PyType_Spec Trials_spec = {
    .name = "psy.trials.Trials",
    .basicsize = sizeof(TrialsObject),
    .itemsize = 0,
    .flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .slots = Trials_slots,
};

/* ======================================================================= *
 *  Module functions: constraint helpers and splitmix
 * ======================================================================= */

/* A constraint as a dict. `factor` may be a factor name, resolved at open. */
static PyObject* t_constraint(const psytr_constraint* c, PyObject* factor) {
    PyObject* d = Py_BuildValue("{s:i,s:O,s:i,s:i,s:i,s:i}", "rule", (int)c->rule,
                                "factor", factor, "level", c->level, "level2", c->level2,
                                "n", c->n, "window", c->window);
    return d;
}

static PyObject* t_factor_arg(PyObject* f, int* idx) {
    if (PyUnicode_Check(f)) { *idx = 0; Py_INCREF(f); return f; }
    {
        long v = PyLong_AsLong(f);
        if (v == -1 && PyErr_Occurred()) return NULL;
        *idx = (int)v;
        return PyLong_FromLong(v);
    }
}

static PyObject* mod_max_run(PyObject* Py_UNUSED(m), PyObject* args, PyObject* kwds) {
    static char* kw[] = { "factor", "level", "n", NULL };
    PyObject *f, *fo, *r;
    int fi, level, n;
    psytr_constraint c;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "Oii", kw, &f, &level, &n)) return NULL;
    if (!(fo = t_factor_arg(f, &fi))) return NULL;
    c = psytr_max_run(fi, level, n);
    r = t_constraint(&c, fo);
    Py_DECREF(fo);
    return r;
}

static PyObject* mod_max_in_window(PyObject* Py_UNUSED(m), PyObject* args, PyObject* kwds) {
    static char* kw[] = { "factor", "level", "window", "n", NULL };
    PyObject *f, *fo, *r;
    int fi, level, w, n;
    psytr_constraint c;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "Oiii", kw, &f, &level, &w, &n)) return NULL;
    if (!(fo = t_factor_arg(f, &fi))) return NULL;
    c = psytr_max_in_window(fi, level, w, n);
    r = t_constraint(&c, fo);
    Py_DECREF(fo);
    return r;
}

static PyObject* mod_min_gap(PyObject* Py_UNUSED(m), PyObject* args, PyObject* kwds) {
    static char* kw[] = { "factor", "level", "gap", NULL };
    PyObject *f, *fo, *r;
    int fi, level, g;
    psytr_constraint c;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "Oii", kw, &f, &level, &g)) return NULL;
    if (!(fo = t_factor_arg(f, &fi))) return NULL;
    c = psytr_min_gap(fi, level, g);
    r = t_constraint(&c, fo);
    Py_DECREF(fo);
    return r;
}

static PyObject* mod_no_transition(PyObject* Py_UNUSED(m), PyObject* args, PyObject* kwds) {
    static char* kw[] = { "factor", "from_level", "to_level", NULL };
    PyObject *f, *fo, *r;
    int fi, a, b;
    psytr_constraint c;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "Oii", kw, &f, &a, &b)) return NULL;
    if (!(fo = t_factor_arg(f, &fi))) return NULL;
    c = psytr_no_transition(fi, a, b);
    r = t_constraint(&c, fo);
    Py_DECREF(fo);
    return r;
}

static PyObject* mod_first_not(PyObject* Py_UNUSED(m), PyObject* args, PyObject* kwds) {
    static char* kw[] = { "factor", "level", NULL };
    PyObject *f, *fo, *r;
    int fi, level;
    psytr_constraint c;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "Oi", kw, &f, &level)) return NULL;
    if (!(fo = t_factor_arg(f, &fi))) return NULL;
    c = psytr_first_not(fi, level);
    r = t_constraint(&c, fo);
    Py_DECREF(fo);
    return r;
}

static PyObject* mod_splitmix(PyObject* Py_UNUSED(m), PyObject* arg) {
    unsigned long long s = PyLong_AsUnsignedLongLongMask(arg);
    uint64_t st;
    double u;
    if (s == (unsigned long long)-1 && PyErr_Occurred()) return NULL;
    st = (uint64_t)s;
    u = psytr_splitmix(&st);
    return Py_BuildValue("(dK)", u, (unsigned long long)st);
}

static PyObject* mod_strerror(PyObject* Py_UNUSED(m), PyObject* arg) {
    long c = PyLong_AsLong(arg);
    if (c == -1 && PyErr_Occurred()) return NULL;
    return PyUnicode_FromString(psytr_strerror((int)c));
}

static PyMethodDef module_methods[] = {
    { "max_run", (PyCFunction)(void (*)(void))mod_max_run, METH_VARARGS | METH_KEYWORDS,
      "max_run(factor, level, n): no more than n consecutive trials with that "
      "level. factor is an index, a factor name, or CONDITION; level may be "
      "ANY_LEVEL." },
    { "max_in_window", (PyCFunction)(void (*)(void))mod_max_in_window, METH_VARARGS | METH_KEYWORDS,
      "max_in_window(factor, level, window, n): at most n in any window trials." },
    { "min_gap", (PyCFunction)(void (*)(void))mod_min_gap, METH_VARARGS | METH_KEYWORDS,
      "min_gap(factor, level, gap): at least gap other trials between two." },
    { "no_transition", (PyCFunction)(void (*)(void))mod_no_transition, METH_VARARGS | METH_KEYWORDS,
      "no_transition(factor, from_level, to_level): from_level never directly "
      "followed by to_level." },
    { "first_not", (PyCFunction)(void (*)(void))mod_first_not, METH_VARARGS | METH_KEYWORDS,
      "first_not(factor, level): the first main trial does not have it." },
    { "splitmix", mod_splitmix, METH_O,
      "splitmix(state) -> (u, new_state): one psytr_splitmix step, for a "
      "caller that wants to reproduce the header's generator." },
    { "strerror", mod_strerror, METH_O, "strerror(code) -> str" },
    { NULL }
};

static PyModuleDef psy_trials_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "psy.trials",
    .m_doc = "Trial sequencing: conditions, orders, constraints, interleaved "
             "adaptive tracks, blocks, re-queues, tallies (psy_trials.h).",
    .m_size = -1,
    .m_methods = module_methods,
};

static PyObject* t_add_exc(PyObject* m, const char* qualname, const char* attr, PyObject* base) {
    PyObject* e = PyErr_NewException(qualname, base, NULL);
    if (!e) return NULL;
    Py_INCREF(e);
    if (PyModule_AddObject(m, attr, e) < 0) { Py_DECREF(e); Py_DECREF(e); return NULL; }
    return e;
}

typedef struct { const char* name; long value; } t_member;

static PyObject* t_add_enum(PyObject* m, PyObject* enum_mod, const char* cls_name,
                            const char* prefix, const t_member* mem, int n) {
    PyObject *base = NULL, *members = NULL, *args = NULL, *kwargs = NULL, *cls = NULL;
    int i;
    base = PyObject_GetAttrString(enum_mod, "IntEnum");
    if (!base) goto fail;
    members = PyList_New(n);
    if (!members) goto fail;
    for (i = 0; i < n; i++) {
        PyObject* t = Py_BuildValue("(sl)", mem[i].name, mem[i].value);
        if (!t) goto fail;
        PyList_SetItem(members, i, t);
    }
    args = Py_BuildValue("(sO)", cls_name, members);
    kwargs = Py_BuildValue("{s:s}", "module", "psy.trials");
    if (!args || !kwargs) goto fail;
    cls = PyObject_Call(base, args, kwargs);
    if (!cls) goto fail;
    Py_INCREF(cls);
    if (PyModule_AddObject(m, cls_name, cls) < 0) { Py_DECREF(cls); goto fail; }
    for (i = 0; i < n; i++) {
        char full[64];
        PyObject* v = PyObject_GetAttrString(cls, mem[i].name);
        if (!v) goto fail;
        PyOS_snprintf(full, sizeof(full), "%s_%s", prefix, mem[i].name);
        if (PyModule_AddObject(m, full, v) < 0) { Py_DECREF(v); goto fail; }
    }
    Py_DECREF(base); Py_DECREF(members); Py_DECREF(args); Py_DECREF(kwargs);
    return cls;
fail:
    Py_XDECREF(base); Py_XDECREF(members); Py_XDECREF(args); Py_XDECREF(kwargs);
    Py_XDECREF(cls);
    return NULL;
}

static PyObject* t_namedtuple(PyObject* coll, const char* name, PyObject* fields) {
    PyObject *nt, *a, *k, *r = NULL;
    nt = PyObject_GetAttrString(coll, "namedtuple");
    a = Py_BuildValue("(sO)", name, fields);
    k = Py_BuildValue("{s:s}", "module", "psy.trials");
    if (nt && a && k) r = PyObject_Call(nt, a, k);
    Py_XDECREF(nt); Py_XDECREF(a); Py_XDECREF(k);
    return r;
}

PyMODINIT_FUNC PyInit_trials(void) {
    static const t_member order_m[] = { { "SEQUENTIAL", PSYTR_ORDER_SEQUENTIAL },
                                        { "RANDOM", PSYTR_ORDER_RANDOM },
                                        { "FULL_RANDOM", PSYTR_ORDER_FULL_RANDOM },
                                        { "CONSTRAINED", PSYTR_ORDER_CONSTRAINED } };
    static const t_member il_m[] = { { "RANDOM", PSYTR_INTERLEAVE_RANDOM },
                                     { "ROUND_ROBIN", PSYTR_INTERLEAVE_ROUND_ROBIN } };
    static const t_member rule_m[] = { { "MAX_RUN", PSYTR_RULE_MAX_RUN },
                                       { "MAX_IN_WINDOW", PSYTR_RULE_MAX_IN_WINDOW },
                                       { "MIN_GAP", PSYTR_RULE_MIN_GAP },
                                       { "NO_TRANSITION", PSYTR_RULE_NO_TRANSITION },
                                       { "FIRST_NOT", PSYTR_RULE_FIRST_NOT } };
    PyObject *m, *type, *mod = NULL, *cls, *fields, *bases;

    m = PyModule_Create(&psy_trials_module);
    if (!m) return NULL;

    mod = PyImport_ImportModule("sys");
    if (!mod) goto fail;
    {
        PyObject* vi = PyObject_GetAttrString(mod, "version_info");
        PyObject* major = vi ? PySequence_GetItem(vi, 0) : NULL;
        PyObject* minor = vi ? PySequence_GetItem(vi, 1) : NULL;
        if (!major || !minor) { Py_XDECREF(vi); Py_XDECREF(major); Py_XDECREF(minor); goto fail; }
        g_visit_type = PyLong_AsLong(major) > 3 || PyLong_AsLong(minor) >= 9;
        Py_DECREF(vi); Py_DECREF(major); Py_DECREF(minor);
    }
    Py_CLEAR(mod);

    type = PyType_FromSpec(&Trials_spec);
    if (!type) goto fail;
    if (PyModule_AddObject(m, "Trials", type) < 0) { Py_DECREF(type); goto fail; }

    if (!(TError = t_add_exc(m, "psy.trials.Error", "Error", NULL))) goto fail;
    bases = PyTuple_Pack(2, TError, PyExc_ValueError);
    if (!bases) goto fail;
    TArgumentError = t_add_exc(m, "psy.trials.ArgumentError", "ArgumentError", bases);
    Py_DECREF(bases);
    if (!TArgumentError) goto fail;
    if (!(TClosed = t_add_exc(m, "psy.trials.Closed", "Closed", TError))) goto fail;
    if (!(TOutOfOrder = t_add_exc(m, "psy.trials.OutOfOrder", "OutOfOrder", TError))) goto fail;
    if (!(TFull = t_add_exc(m, "psy.trials.Full", "Full", TError))) goto fail;

    mod = PyImport_ImportModule("enum");
    if (!mod) goto fail;
    if (!(cls = t_add_enum(m, mod, "Order", "ORDER", order_m, 4))) goto fail;
    Py_DECREF(cls);
    if (!(cls = t_add_enum(m, mod, "Interleave", "INTERLEAVE", il_m, 2))) goto fail;
    Py_DECREF(cls);
    if (!(cls = t_add_enum(m, mod, "Rule", "RULE", rule_m, 5))) goto fail;
    Py_DECREF(cls);
    Py_CLEAR(mod);

    mod = PyImport_ImportModule("collections");
    if (!mod) goto fail;
    fields = Py_BuildValue("[ssssssssss]", "index", "condition", "track", "rep", "block",
                           "first_in_block", "after_break", "practice", "warmup", "requeued");
    TInfoType = fields ? t_namedtuple(mod, "TrialInfo", fields) : NULL;
    Py_XDECREF(fields);
    if (!TInfoType) goto fail;
    Py_INCREF(TInfoType);
    if (PyModule_AddObject(m, "TrialInfo", TInfoType) < 0) { Py_DECREF(TInfoType); goto fail; }
    fields = Py_BuildValue("[ssssssssssssss]", "index", "condition", "track", "rep", "block",
                           "outcome", "flags", "practice", "warmup", "requeued", "after_break",
                           "first_in_block", "violation", "done");
    TTrialType = fields ? t_namedtuple(mod, "Trial", fields) : NULL;
    Py_XDECREF(fields);
    if (!TTrialType) goto fail;
    Py_INCREF(TTrialType);
    if (PyModule_AddObject(m, "Trial", TTrialType) < 0) { Py_DECREF(TTrialType); goto fail; }
    Py_CLEAR(mod);

    /* From the compiled implementation, so it names the header actually built
     * in; the tests check pyproject.toml against it. */
    if (PyModule_AddStringConstant(m, "__version__", psytr_version()) < 0) goto fail;
    PyModule_AddIntConstant(m, "DONE", PSYTR_DONE);
    PyModule_AddIntConstant(m, "ERR_ARG", PSYTR_ERR_ARG);
    PyModule_AddIntConstant(m, "ERR_CLOSED", PSYTR_ERR_CLOSED);
    PyModule_AddIntConstant(m, "ERR_ORDER", PSYTR_ERR_ORDER);
    PyModule_AddIntConstant(m, "ERR_FULL", PSYTR_ERR_FULL);
    PyModule_AddIntConstant(m, "INVALID", PSYTR_INVALID);
    PyModule_AddIntConstant(m, "REQUEUE", PSYTR_REQUEUE);
    PyModule_AddIntConstant(m, "CONDITION", PSYTR_CONDITION);
    PyModule_AddIntConstant(m, "ANY_LEVEL", PSYTR_ANY_LEVEL);
    PyModule_AddIntConstant(m, "MAX_TRIALS", PSYTR_MAX_TRIALS);
    PyModule_AddIntConstant(m, "MAX_CONDITIONS", PSYTR_MAX_CONDITIONS);
    PyModule_AddIntConstant(m, "MAX_FACTORS", PSYTR_MAX_FACTORS);
    PyModule_AddIntConstant(m, "MAX_CONSTRAINTS", PSYTR_MAX_CONSTRAINTS);
    PyModule_AddIntConstant(m, "MAX_TRACKS", PSYTR_MAX_TRACKS);
    PyModule_AddIntConstant(m, "FLAG_PRACTICE", PSYTR_FLAG_PRACTICE);
    PyModule_AddIntConstant(m, "FLAG_REQUEUED", PSYTR_FLAG_REQUEUED);
    PyModule_AddIntConstant(m, "FLAG_DONE", PSYTR_FLAG_DONE);
    PyModule_AddIntConstant(m, "FLAG_WARMUP", PSYTR_FLAG_WARMUP);
    PyModule_AddIntConstant(m, "FLAG_AFTER_BREAK", PSYTR_FLAG_AFTER_BREAK);
    PyModule_AddIntConstant(m, "FLAG_FIRST_IN_BLOCK", PSYTR_FLAG_FIRST_IN_BLOCK);
    PyModule_AddIntConstant(m, "FLAG_VIOLATION", PSYTR_FLAG_VIOLATION);
    return m;

fail:
    Py_XDECREF(mod);
    Py_DECREF(m);
    return NULL;
}
