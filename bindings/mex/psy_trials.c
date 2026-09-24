/* psy_trials.c - MATLAB/Octave MEX binding for psy_trials.h (builds to psy_trials.<mexext>)
 *
 * Trial sequencing: conditions and repetitions, sequential, random and
 * constrained orders, interleaved adaptive tracks, blocks, practice, warmup,
 * catch and re-queued trials, tallies and a history that replays. Command
 * dispatch in the style of widmann's ppdev-mex.
 *
 *     d = struct('factors', {{{'orientation', 2}, {'contrast', 5}}}, ...
 *                'reps', 20, 'order', 'constrained', 'rng', 20260923);
 *     d.constraints = psy_trials('max_run', 'orientation', 'any', 3);
 *     h = psy_trials('open', d);
 *     ti = psy_trials('next', h);
 *     while ~isempty(ti)
 *         psy_trials('update', h, run_trial(ti.levels));
 *         ti = psy_trials('next', h);
 *     end
 *     psy_trials('close', h);
 *
 * Build with build.m. See README.md for the command reference.
 *
 * TRACKS. A track is {'stair', h}, {'quest', h} or {'gp', h}, a handle of one
 * of the other MEX modules, or a function handle that returns true when the
 * track is done. The header asks a track is_done() from inside psytr_next()
 * and psytr_done(). Each MEX module is a separate shared library with its own
 * handle table, so this module cannot reach psy_stair's table in C without
 * exporting symbols across libraries, which neither MATLAB nor Octave
 * supports portably. Instead is_done() calls psy_stair('done', h) (or quest,
 * gp) through mexCallMATLABWithTrap. That is safe because is_done() runs on
 * the interpreter thread, inside the next() or done() call the script made,
 * and it costs one MEX call per live track per trial, microseconds.
 */
#define PSY_TRIALS_IMPLEMENTATION
#include "psy_trials.h"

#define PM_MOD "psy_trials"
#include "psy_mex_util.h"

struct tn;
typedef struct tn_track {
    struct tn* n;
    mxArray*   fn;       /* persistent: the track's function handle, or the
                          * MEX module's (@psy_stair)                       */
    mxArray*   h;        /* persistent: the module handle, or NULL          */
} tn_track;

typedef struct tn {
    psytr_trials t;
    int          busy;
    pm_rng       rng;
    uint8_t*     records;     /* record_size x PSYTR_MAX_TRIALS               */
    uint8_t*     rec_tmp;     /* one record, for update()                    */
    size_t       record_size;
    char*        names[PSYTR_MAX_FACTORS];
    tn_track     tracks[PSYTR_MAX_TRACKS];
    int          n_tracks;
    mxArray*     done_str;    /* persistent 'done', the command a track gets  */
} tn;

static void tn_destroy(void* obj) {
    tn* n = (tn*)obj;
    int i;
    for (i = 0; i < PSYTR_MAX_FACTORS; i++) free(n->names[i]);
    for (i = 0; i < n->n_tracks; i++) {
        if (n->tracks[i].fn) mxDestroyArray(n->tracks[i].fn);
        if (n->tracks[i].h) mxDestroyArray(n->tracks[i].h);
    }
    if (n->done_str) mxDestroyArray(n->done_str);
    pm_rng_free(&n->rng);
    free(n->records);
    free(n->rec_tmp);
    free(n);
}

static const char* const t_order_names[] = { "sequential", "random", "full_random", "constrained" };
static const char* const t_inter_names[] = { "random", "round_robin" };
static const char* const t_rule_names[]  = { "max_run", "max_in_window", "min_gap", "no_transition", "first_not" };

static void t_check(int rc) {
    if (rc >= 0 || rc == PSYTR_DONE) return;
    switch (rc) {
        case PSYTR_ERR_ARG:    pm_err("arg", "%s", psytr_strerror(rc)); break;
        case PSYTR_ERR_CLOSED: pm_err("closed", "%s", psytr_strerror(rc)); break;
        case PSYTR_ERR_ORDER:  pm_err("order", "%s", psytr_strerror(rc)); break;
        case PSYTR_ERR_FULL:   pm_err("full", "%s", psytr_strerror(rc)); break;
        default:               pm_err("error", "%s", psytr_strerror(rc)); break;
    }
}

/* ---- tracks ------------------------------------------------------------- */

static bool tn_is_done(void* ctx) {
    tn_track* tr = (tn_track*)ctx;
    mxArray* out = NULL;
    mxArray* in[3];
    int ok;
    bool done = false;
    if (tr->h) {
        in[0] = tr->fn;
        in[1] = tr->n->done_str;
        in[2] = tr->h;
        ok = pm_feval(1, &out, 3, in);
    } else {
        in[0] = tr->fn;
        ok = pm_feval(1, &out, 1, in);
    }
    /* On an error the track reads as not done; the error is raised when the
     * header call returns. */
    if (ok && out && (mxIsLogical(out) || mxIsNumeric(out)) && mxGetNumberOfElements(out) >= 1)
        done = mxGetScalar(out) != 0.0;
    if (out) mxDestroyArray(out);
    return done;
}

/* ---- constraints (module commands) ------------------------------------- */

static const char* const t_con_fields[] = { "rule", "factor", "level", "level2", "n", "window" };

/* The struct a helper returns. factor and level stay as the caller wrote them
 * (a name or 'condition', 'any') and are resolved at open, where the factor
 * names are known. */
static mxArray* t_con_make(int rule, const mxArray* factor, const mxArray* level,
                           const mxArray* level2, double nval, double window) {
    mxArray* s = mxCreateStructMatrix(1, 1, 6, (const char**)t_con_fields);
    mxSetField(s, 0, "rule", mxCreateString(t_rule_names[rule]));
    mxSetField(s, 0, "factor", mxDuplicateArray(factor));
    mxSetField(s, 0, "level", mxDuplicateArray(level));
    mxSetField(s, 0, "level2", level2 ? mxDuplicateArray(level2) : mxCreateDoubleMatrix(0, 0, mxREAL));
    mxSetField(s, 0, "n", mxCreateDoubleScalar(nval));
    mxSetField(s, 0, "window", mxCreateDoubleScalar(window));
    return s;
}

/* Factor: a 1-based index, a factor name, or 'condition' for the row. */
static int t_factor(const mxArray* a, const psytr_desc* d, tn* n) {
    int i;
    if (mxIsChar(a)) {
        char* v = mxArrayToString(a);
        if (pm_lower_eq(v, "condition")) return PSYTR_CONDITION;
        for (i = 0; i < d->n_factors; i++)
            if (n->names[i] && strcmp(n->names[i], v) == 0) return i;
        pm_err("arg", "constraint names unknown factor '%s'", v);
    }
    i = pm_int(a, "constraint factor");
    if (i < 1) pm_err("arg", "constraint factor must be >= 1, a name, or 'condition'");
    return i - 1;
}

static int t_level(const mxArray* a) {
    int i;
    if (mxIsChar(a)) {
        char* v = mxArrayToString(a);
        if (pm_lower_eq(v, "any")) return PSYTR_ANY_LEVEL;
        pm_err("arg", "a constraint level is a 1-based index or 'any'");
    }
    i = pm_int(a, "constraint level");
    if (i < 1) pm_err("arg", "constraint level must be >= 1 or 'any'");
    return i - 1;
}

static void t_constraint(const mxArray* s, size_t e, const psytr_desc* d, tn* n, psytr_constraint* c) {
    const mxArray* f;
    memset(c, 0, sizeof(*c));
    if (!(f = mxGetField(s, e, "rule"))) pm_err("arg", "a constraint needs a rule; build it with psy_trials('max_run', ...)");
    c->rule = (psytr_rule)pm_enum(f, "rule", t_rule_names, 5);
    if (!(f = mxGetField(s, e, "factor"))) pm_err("arg", "a constraint needs a factor");
    c->factor = t_factor(f, d, n);
    if (!(f = mxGetField(s, e, "level"))) pm_err("arg", "a constraint needs a level");
    c->level = t_level(f);
    if ((f = mxGetField(s, e, "level2")) && !mxIsEmpty(f)) c->level2 = t_level(f);
    if ((f = mxGetField(s, e, "n")) && !mxIsEmpty(f)) c->n = pm_int(f, "n");
    if ((f = mxGetField(s, e, "window")) && !mxIsEmpty(f)) c->window = pm_int(f, "window");
}

/* ---- desc --------------------------------------------------------------- */

static const char* const t_fields[] = {
    "n_conditions", "factors", "reps", "cond_reps", "order", "constraints",
    "max_swaps", "tracks", "track_weights", "interleave", "track_rate",
    "block_size", "constraints_span_blocks", "n_practice", "n_warmup",
    "warmup_conditions", "requeue_gap", "rng", "record_size"
};

/* Reads the desc into d and fills the node's owned copies (factor names,
 * tracks, records). Everything that can raise on a bad argument runs before
 * the first malloc into the node, or leaves only what tn_destroy frees. */
static void t_read_desc(const mxArray* s, psytr_desc* d, tn* n, int** cond_reps, int** warm) {
    const mxArray* f;
    int i;
    pm_check_fields(s, t_fields, (int)(sizeof(t_fields) / sizeof(t_fields[0])), "desc");
    if ((f = pm_field(s, "n_conditions"))) d->n_conditions = pm_int(f, "n_conditions");
    if ((f = pm_field(s, "factors"))) {
        /* A cell of {name, n_levels} pairs, or an n x 2 cell. */
        int nf;
        int two_col = mxIsCell(f) && mxGetN(f) == 2 && mxGetM(f) >= 1 && !mxIsCell(mxGetCell(f, 0));
        if (!mxIsCell(f)) pm_err("arg", "desc.factors must be a cell of {name, n_levels} pairs");
        nf = two_col ? (int)mxGetM(f) : (int)mxGetNumberOfElements(f);
        if (nf > PSYTR_MAX_FACTORS) pm_err("arg", "at most %d factors", PSYTR_MAX_FACTORS);
        for (i = 0; i < nf; i++) {
            const mxArray *nm, *nl;
            if (two_col) {
                nm = mxGetCell(f, (size_t)i);
                nl = mxGetCell(f, (size_t)(i + nf));
            } else {
                const mxArray* pair = mxGetCell(f, (size_t)i);
                if (!pair || !mxIsCell(pair) || mxGetNumberOfElements(pair) != 2)
                    pm_err("arg", "desc.factors{%d} must be {name, n_levels}", i + 1);
                nm = mxGetCell(pair, 0);
                nl = mxGetCell(pair, 1);
            }
            d->factors[i].n_levels = pm_int(nl, "n_levels");
            if (nm && !mxIsEmpty(nm)) {
                char* v = pm_string(nm, "factor name");
                n->names[i] = (char*)malloc(strlen(v) + 1);
                if (!n->names[i]) pm_err("memory", "out of memory");
                strcpy(n->names[i], v);
            }
            d->factors[i].name = n->names[i];
        }
        d->n_factors = nf;
    }
    if ((f = pm_field(s, "reps"))) d->reps = pm_int(f, "reps");
    if ((f = pm_field(s, "cond_reps"))) {
        int m = (int)mxGetNumberOfElements(f);
        double* v = (double*)mxMalloc((size_t)m * sizeof(double));
        int* c = (int*)mxMalloc((size_t)m * sizeof(int));
        pm_vector(f, "cond_reps", v, m);
        for (i = 0; i < m; i++) c[i] = (int)v[i];
        *cond_reps = c;
        d->cond_reps = c;
    }
    if ((f = pm_field(s, "order")))     d->order = (psytr_order)pm_enum(f, "order", t_order_names, 4);
    if ((f = pm_field(s, "max_swaps"))) d->max_swaps = pm_int(f, "max_swaps");
    if ((f = pm_field(s, "constraints"))) {
        /* A struct array from the helpers, or a cell of them. */
        size_t m = mxGetNumberOfElements(f), e;
        if (m > PSYTR_MAX_CONSTRAINTS) pm_err("arg", "at most %d constraints", PSYTR_MAX_CONSTRAINTS);
        for (e = 0; e < m; e++) {
            if (mxIsStruct(f)) t_constraint(f, e, d, n, &d->constraints[e]);
            else if (mxIsCell(f) && mxIsStruct(mxGetCell(f, e))) t_constraint(mxGetCell(f, e), 0, d, n, &d->constraints[e]);
            else pm_err("arg", "desc.constraints must be structs from the constraint helpers");
        }
        d->n_constraints = (int)m;
    }
    if ((f = pm_field(s, "tracks"))) {
        int m;
        if (!mxIsCell(f)) f = NULL;
        if (!f) pm_err("arg", "desc.tracks must be a cell of {'stair'|'quest'|'gp', handle} or function handles");
        m = (int)mxGetNumberOfElements(f);
        if (m > PSYTR_MAX_TRACKS) pm_err("arg", "at most %d tracks", PSYTR_MAX_TRACKS);
        /* Validate them all first; persistent copies come after. */
        for (i = 0; i < m; i++) {
            const mxArray* tr = mxGetCell(f, (size_t)i);
            if (tr && mxIsClass(tr, "function_handle")) continue;
            if (!tr || !mxIsCell(tr) || mxGetNumberOfElements(tr) != 2 || !mxIsChar(mxGetCell(tr, 0)))
                pm_err("arg", "desc.tracks{%d} must be {'stair'|'quest'|'gp', handle} or a function handle", i + 1);
            {
                char* mod = mxArrayToString(mxGetCell(tr, 0));
                if (!pm_lower_eq(mod, "stair") && !pm_lower_eq(mod, "quest") && !pm_lower_eq(mod, "gp") &&
                    !pm_lower_eq(mod, "psy_stair") && !pm_lower_eq(mod, "psy_quest") && !pm_lower_eq(mod, "psy_gp"))
                    pm_err("arg", "desc.tracks{%d}: unknown module '%s'", i + 1, mod);
            }
        }
        n->done_str = mxCreateString("done");
        mexMakeArrayPersistent(n->done_str);
        for (i = 0; i < m; i++) {
            const mxArray* tr = mxGetCell(f, (size_t)i);
            tn_track* t = &n->tracks[i];
            t->n = n;
            if (mxIsClass(tr, "function_handle")) {
                t->fn = mxDuplicateArray(tr);
            } else {
                char* mod = mxArrayToString(mxGetCell(tr, 0));
                char full[16];
                snprintf(full, sizeof(full), "%s%s", strncmp(mod, "psy_", 4) == 0 ? "" : "psy_", mod);
                {
                    /* A function handle, not a name: feval and Octave's
                     * cellfun wrapper both take one. */
                    mxArray* nm = mxCreateString(full);
                    mexCallMATLAB(1, &t->fn, 1, &nm, "str2func");
                    mxDestroyArray(nm);
                }
                t->h = mxDuplicateArray(mxGetCell(tr, 1));
                mexMakeArrayPersistent(t->h);
            }
            mexMakeArrayPersistent(t->fn);
            n->n_tracks = i + 1;
            d->tracks[i] = psytr_track(t, tn_is_done);
        }
        d->n_tracks = m;
    }
    if ((f = pm_field(s, "track_weights"))) {
        double w[PSYTR_MAX_TRACKS];
        pm_vector_n(f, "track_weights", w, d->n_tracks);
        for (i = 0; i < d->n_tracks; i++) d->tracks[i].weight = w[i];
    }
    if ((f = pm_field(s, "interleave")))  d->interleave = (psytr_interleave)pm_enum(f, "interleave", t_inter_names, 2);
    if ((f = pm_field(s, "track_rate")))  d->track_rate = pm_scalar(f, "track_rate");
    if ((f = pm_field(s, "block_size")))  d->block_size = pm_int(f, "block_size");
    if ((f = pm_field(s, "constraints_span_blocks"))) d->constraints_span_blocks = pm_bool(f, "constraints_span_blocks") != 0;
    if ((f = pm_field(s, "n_practice")))  d->n_practice = pm_int(f, "n_practice");
    if ((f = pm_field(s, "n_warmup")))    d->n_warmup = pm_int(f, "n_warmup");
    if ((f = pm_field(s, "warmup_conditions"))) {
        int m = (int)mxGetNumberOfElements(f);
        double* v = (double*)mxMalloc((size_t)m * sizeof(double));
        int* c = (int*)mxMalloc((size_t)m * sizeof(int));
        pm_vector(f, "warmup_conditions", v, m);
        for (i = 0; i < m; i++) {
            if (v[i] != floor(v[i]) || v[i] < 1) pm_err("arg", "warmup_conditions are 1-based condition indices");
            c[i] = (int)v[i] - 1;
        }
        *warm = c;
        d->warmup_conditions = c;
        d->n_warmup_conditions = m;
    }
    if ((f = pm_field(s, "requeue_gap"))) d->requeue_gap = pm_int(f, "requeue_gap");
    if ((f = pm_field(s, "record_size"))) {
        double r = pm_scalar(f, "record_size");
        if (r < 0 || r != floor(r) || r > 65536) pm_err("arg", "record_size must be an integer in 0..65536");
        n->record_size = (size_t)r;
    }
    if ((f = pm_field(s, "rng"))) {
        pm_rng_parse(f, &n->rng);
        d->rng = pm_rng_call;
        d->rng_ctx = &n->rng;
    }
    if (n->record_size) {
        n->records = (uint8_t*)calloc(PSYTR_MAX_TRIALS, n->record_size);
        n->rec_tmp = (uint8_t*)calloc(1, n->record_size);
        if (!n->records || !n->rec_tmp) pm_err("memory", "out of memory");
        d->records = n->records;
        d->record_size = n->record_size;
    }
}

/* The node is registered before the desc is read, so an error in the middle
 * of parsing leaves nothing that the handle table would not free: the node
 * is found by pm_cleanup, and unregistered again here on the normal paths. */
static tn* t_make(const mxArray* s, const uint8_t* bytes, size_t len) {
    psytr_desc d;
    int* cond_reps = NULL;
    int* warm = NULL;
    tn* n;
    bool ok;
    memset(&d, 0, sizeof(d));
    n = (tn*)calloc(1, sizeof(*n));
    if (!n) pm_err("memory", "out of memory");
    pm_register(n);
    t_read_desc(s, &d, n, &cond_reps, &warm);
    n->busy = 1;
    ok = bytes ? psytr_load(&n->t, &d, bytes, len) : psytr_open(&n->t, &d);
    n->busy = 0;
    pm_unregister(n);
    if (!ok || pm_pending) {
        char msg[300];
        snprintf(msg, sizeof(msg), "%s", ok ? "" : psytr_error(&n->t));
        tn_destroy(n);
        pm_rethrow();
        pm_err(bytes ? "load" : "open", "%s", msg);
    }
    return n;
}

/* ---- outputs ------------------------------------------------------------ */

static mxArray* t_info(const tn* n, const psytr_trial_info* ti) {
    static const char* fields[] = { "index", "condition", "track", "rep", "block", "levels",
                                    "first_in_block", "after_break", "practice", "warmup", "requeued" };
    mxArray* s = mxCreateStructMatrix(1, 1, 11, fields);
    int nf = psytr_n_factors(&n->t), i;
    mxArray* lv = mxCreateDoubleMatrix(1, (size_t)(ti->condition >= 0 ? nf : 0), mxREAL);
    if (ti->condition >= 0)
        for (i = 0; i < nf; i++) mxGetPr(lv)[i] = psytr_level(&n->t, ti->condition, i) + 1.0;
    mxSetField(s, 0, "index", mxCreateDoubleScalar(ti->index + 1.0));
    mxSetField(s, 0, "condition", mxCreateDoubleScalar(ti->condition + 1.0));
    mxSetField(s, 0, "track", mxCreateDoubleScalar(ti->track + 1.0));
    mxSetField(s, 0, "rep", mxCreateDoubleScalar(ti->rep + 1.0));
    mxSetField(s, 0, "block", mxCreateDoubleScalar(ti->block + 1.0));
    mxSetField(s, 0, "levels", lv);
    mxSetField(s, 0, "first_in_block", mxCreateLogicalScalar(ti->first_in_block));
    mxSetField(s, 0, "after_break", mxCreateLogicalScalar(ti->after_break));
    mxSetField(s, 0, "practice", mxCreateLogicalScalar(ti->practice));
    mxSetField(s, 0, "warmup", mxCreateLogicalScalar(ti->warmup));
    mxSetField(s, 0, "requeued", mxCreateLogicalScalar(ti->requeued));
    return s;
}

static mxArray* t_history(const psytr_trials* t) {
    static const char* fields[] = { "condition", "track", "rep", "block", "outcome", "flags",
                                    "practice", "requeued", "done", "warmup", "after_break",
                                    "first_in_block", "violation" };
    static const int bits[] = { PSYTR_FLAG_PRACTICE, PSYTR_FLAG_REQUEUED, PSYTR_FLAG_DONE,
                                PSYTR_FLAG_WARMUP, PSYTR_FLAG_AFTER_BREAK,
                                PSYTR_FLAG_FIRST_IN_BLOCK, PSYTR_FLAG_VIOLATION };
    int n = 0, i, b;
    const psytr_trial* h = psytr_history(t, &n);
    mxArray* s = mxCreateStructMatrix(1, 1, 13, fields);
    mxArray* col[6];
    mxArray* flag[7];
    for (i = 0; i < 6; i++) col[i] = mxCreateDoubleMatrix((size_t)n, 1, mxREAL);
    for (b = 0; b < 7; b++) flag[b] = mxCreateLogicalMatrix((size_t)n, 1);
    for (i = 0; i < n; i++) {
        mxGetPr(col[0])[i] = h[i].condition + 1.0;   /* 0 = a track trial     */
        mxGetPr(col[1])[i] = h[i].track + 1.0;       /* 0 = a condition trial */
        mxGetPr(col[2])[i] = h[i].rep + 1.0;         /* 0 = none              */
        mxGetPr(col[3])[i] = h[i].block + 1.0;       /* 0 = practice          */
        mxGetPr(col[4])[i] = h[i].outcome;           /* a value, not an index */
        mxGetPr(col[5])[i] = h[i].flags;
        for (b = 0; b < 7; b++) mxGetLogicals(flag[b])[i] = (h[i].flags & bits[b]) != 0;
    }
    for (i = 0; i < 6; i++) mxSetField(s, 0, fields[i], col[i]);
    for (b = 0; b < 7; b++) mxSetField(s, 0, fields[6 + b], flag[b]);
    return s;
}

/* snprintf-style formatter into an exact-size MATLAB string. */
typedef int (*t_fmt_fn)(const psytr_trials*, int, char*, size_t);
static int t_fmt_header(const psytr_trials* t, int i, char* b, size_t c) { (void)i; return psytr_format_header(t, b, c); }
static int t_fmt_meta(const psytr_trials* t, int i, char* b, size_t c) { (void)i; return psytr_format_meta(t, b, c); }

static mxArray* t_format(const psytr_trials* t, t_fmt_fn fn, int i) {
    char small[512];
    char* buf;
    int len = fn(t, i, small, sizeof(small));
    t_check(len);
    if ((size_t)len < sizeof(small)) return mxCreateString(small);
    buf = (char*)mxMalloc((size_t)len + 1);
    fn(t, i, buf, (size_t)len + 1);
    return mxCreateString(buf);
}

/* A record: the raw bytes of any numeric array, at most record_size of them,
 * zero-padded. So a double scalar is an 8-byte record, and typecast() gets it
 * back from 'record'. */
static const void* t_record_arg(tn* n, const mxArray* a) {
    size_t bytes;
    if (!n->record_size || !a || mxIsEmpty(a)) return NULL;
    if ((!mxIsNumeric(a) && !mxIsLogical(a) && !mxIsChar(a)) || mxIsComplex(a))
        pm_err("arg", "a record must be a real numeric array");
    bytes = mxGetNumberOfElements(a) * mxGetElementSize(a);
    if (bytes > n->record_size)
        pm_err("arg", "a record is at most record_size (%d) bytes, got %d", (int)n->record_size, (int)bytes);
    memset(n->rec_tmp, 0, n->record_size);
    memcpy(n->rec_tmp, mxGetData(a), bytes);
    return n->rec_tmp;
}

/* ---- dispatch ----------------------------------------------------------- */

static const char* const t_con_usage[] = {
    "c = psy_trials('max_run', factor, level, n)",
    "c = psy_trials('max_in_window', factor, level, window, n)",
    "c = psy_trials('min_gap', factor, level, gap)",
    "c = psy_trials('no_transition', factor, from_level, to_level)",
    "c = psy_trials('first_not', factor, level)"
};

void mexFunction(int nlhs, mxArray* plhs[], int nrhs, const mxArray* prhs[]) {
    char* cmd;
    tn* n;
    psytr_trials* t;
    int r;
    pm_destroy = tn_destroy;
    pm_pending = NULL;
    if (nrhs < 1 || !mxIsChar(prhs[0]))
        pm_err("usage", "first argument must be a command string");
    cmd = mxArrayToString(prhs[0]);

    /* ---- module commands ---- */
    if (strcmp(cmd, "open") == 0) {
        pm_nargs(nrhs, 2, "h = psy_trials('open', desc)");
        n = t_make(prhs[1], NULL, 0);
        plhs[0] = pm_handle_out(pm_register(n));
        return;
    }
    if (strcmp(cmd, "load") == 0) {
        size_t len;
        const uint8_t* b;
        pm_nargs(nrhs, 3, "h = psy_trials('load', bytes, desc)");
        b = pm_bytes_in(prhs[1], "bytes", &len);
        n = t_make(prhs[2], b, len);
        plhs[0] = pm_handle_out(pm_register(n));
        return;
    }
    if (strcmp(cmd, "version") == 0) { plhs[0] = mxCreateString(psytr_version()); return; }
    if (strcmp(cmd, "strerror") == 0) {
        pm_nargs(nrhs, 2, "psy_trials('strerror', code)");
        plhs[0] = mxCreateString(psytr_strerror(pm_int(prhs[1], "code")));
        return;
    }
    if (strcmp(cmd, "splitmix") == 0) {
        /* One step of the generator an integer seed drives: [u, state]. */
        uint64_t st;
        pm_nargs(nrhs, 2, "[u, state] = psy_trials('splitmix', state)");
        st = pm_u64(prhs[1], "state");
        plhs[0] = mxCreateDoubleScalar(pm_splitmix(&st));
        if (nlhs >= 2) plhs[1] = pm_u64_out(st);
        return;
    }
    for (r = 0; r < 5; r++) {
        if (strcmp(cmd, t_rule_names[r]) == 0) {
            static const int need[] = { 4, 5, 4, 4, 3 };
            pm_nargs(nrhs, need[r], t_con_usage[r]);
            switch (r) {
                case PSYTR_RULE_MAX_RUN:
                    plhs[0] = t_con_make(r, prhs[1], prhs[2], NULL, pm_int(prhs[3], "n"), 0); break;
                case PSYTR_RULE_MAX_IN_WINDOW:
                    plhs[0] = t_con_make(r, prhs[1], prhs[2], NULL, pm_int(prhs[4], "n"), pm_int(prhs[3], "window")); break;
                case PSYTR_RULE_MIN_GAP:
                    plhs[0] = t_con_make(r, prhs[1], prhs[2], NULL, pm_int(prhs[3], "gap"), 0); break;
                case PSYTR_RULE_NO_TRANSITION:
                    plhs[0] = t_con_make(r, prhs[1], prhs[2], prhs[3], 0, 0); break;
                default:
                    plhs[0] = t_con_make(r, prhs[1], prhs[2], NULL, 0, 0); break;
            }
            return;
        }
    }

    /* ---- handle commands ---- */
    if (nrhs < 2) pm_err("usage", "'%s' needs a handle", cmd);
    n = (tn*)pm_lookup(prhs[1]);
    t = &n->t;
    if (n->busy) pm_err("busy", "a track or rng callback may not call into the Trials it runs under");

    if (strcmp(cmd, "close") == 0) {
        pm_unregister(n);
        tn_destroy(n);
    } else if (strcmp(cmd, "next") == 0) {
        psytr_trial_info ti;
        int rc;
        n->busy = 1;
        rc = psytr_next(t, &ti);
        n->busy = 0;
        pm_rethrow();
        t_check(rc);
        plhs[0] = rc == PSYTR_DONE ? mxCreateDoubleMatrix(0, 0, mxREAL) : t_info(n, &ti);
    } else if (strcmp(cmd, "update") == 0) {
        pm_nargs(nrhs, 3, "psy_trials('update', h, outcome [, record])");
        {
            int k = pm_int(prhs[2], "outcome");
            const void* rec = t_record_arg(n, nrhs >= 4 ? prhs[3] : NULL);
            t_check(psytr_update(t, k, rec));
        }
    } else if (strcmp(cmd, "requeue") == 0) {
        int rc;
        n->busy = 1;
        rc = psytr_requeue(t);
        n->busy = 0;
        pm_rethrow();
        t_check(rc);
    } else if (strcmp(cmd, "mark_break") == 0) {
        t_check(psytr_mark_break(t));
    } else if (strcmp(cmd, "done") == 0) {
        bool d;
        n->busy = 1;
        d = psytr_done(t);
        n->busy = 0;
        pm_rethrow();
        plhs[0] = mxCreateLogicalScalar(d);
    } else if (strcmp(cmd, "level") == 0) {
        int rc;
        pm_nargs(nrhs, 4, "l = psy_trials('level', h, condition, factor)");
        rc = psytr_level(t, pm_index(prhs[2], "condition", psytr_n_conditions(t)),
                         pm_index(prhs[3], "factor", psytr_n_factors(t)));
        t_check(rc);
        plhs[0] = mxCreateDoubleScalar(rc + 1.0);
    } else if (strcmp(cmd, "levels") == 0) {
        int c, i, nf = psytr_n_factors(t);
        mxArray* out;
        pm_nargs(nrhs, 3, "l = psy_trials('levels', h, condition)");
        c = pm_index(prhs[2], "condition", psytr_n_conditions(t));
        out = mxCreateDoubleMatrix(1, (size_t)nf, mxREAL);
        for (i = 0; i < nf; i++) mxGetPr(out)[i] = psytr_level(t, c, i) + 1.0;
        plhs[0] = out;
    } else if (strcmp(cmd, "condition_from_levels") == 0) {
        double v[PSYTR_MAX_FACTORS];
        int lv[PSYTR_MAX_FACTORS], i, nf = psytr_n_factors(t), rc;
        pm_nargs(nrhs, 3, "c = psy_trials('condition_from_levels', h, levels)");
        pm_vector_n(prhs[2], "levels", v, nf);
        for (i = 0; i < nf; i++) {
            if (v[i] != floor(v[i])) pm_err("arg", "levels must be integers");
            lv[i] = (int)v[i] - 1;
        }
        rc = psytr_condition_from_levels(t, lv);
        t_check(rc);
        plhs[0] = mxCreateDoubleScalar(rc + 1.0);
    } else if (strcmp(cmd, "condition_at") == 0) {
        int rc;
        pm_nargs(nrhs, 3, "c = psy_trials('condition_at', h, slot)");
        rc = psytr_condition_at(t, pm_index(prhs[2], "slot", psytr_n_scheduled(t) > 0 ? psytr_n_scheduled(t) : 1));
        t_check(rc);
        plhs[0] = mxCreateDoubleScalar(rc + 1.0);
    } else if (strcmp(cmd, "schedule") == 0) {
        int m = psytr_n_scheduled(t), i;
        mxArray* out = mxCreateDoubleMatrix((size_t)m, 1, mxREAL);
        for (i = 0; i < m; i++) mxGetPr(out)[i] = psytr_condition_at(t, i) + 1.0;
        plhs[0] = out;
    } else if (strcmp(cmd, "n_scheduled") == 0) {
        plhs[0] = mxCreateDoubleScalar((double)psytr_n_scheduled(t));
    } else if (strcmp(cmd, "n_conditions") == 0) {
        plhs[0] = mxCreateDoubleScalar((double)psytr_n_conditions(t));
    } else if (strcmp(cmd, "n_factors") == 0) {
        plhs[0] = mxCreateDoubleScalar((double)psytr_n_factors(t));
    } else if (strcmp(cmd, "n_run") == 0) {
        plhs[0] = mxCreateDoubleScalar((double)psytr_n_run(t));
    } else if (strcmp(cmd, "n_done") == 0) {
        plhs[0] = mxCreateDoubleScalar((double)psytr_n_done(t));
    } else if (strcmp(cmd, "swaps") == 0) {
        plhs[0] = mxCreateDoubleScalar((double)t->swaps);
    } else if (strcmp(cmd, "n_valid") == 0) {
        pm_nargs(nrhs, 3, "n = psy_trials('n_valid', h, condition)");
        plhs[0] = mxCreateDoubleScalar((double)psytr_n_valid(t, pm_index(prhs[2], "condition", psytr_n_conditions(t))));
    } else if (strcmp(cmd, "count") == 0 || strcmp(cmd, "proportion") == 0) {
        int c, k;
        pm_nargs(nrhs, 3, "psy_trials('count', h, condition [, outcome])");
        c = pm_index(prhs[2], "condition", psytr_n_conditions(t));
        k = nrhs >= 4 ? pm_int(prhs[3], "outcome") : 1;
        if (cmd[0] == 'c') {
            int rc = psytr_count(t, c, k);
            t_check(rc);
            plhs[0] = mxCreateDoubleScalar((double)rc);
        } else {
            plhs[0] = mxCreateDoubleScalar(psytr_proportion(t, c, k));
        }
    } else if (strcmp(cmd, "history") == 0) {
        plhs[0] = t_history(t);
    } else if (strcmp(cmd, "record") == 0) {
        const void* rec;
        pm_nargs(nrhs, 3, "bytes = psy_trials('record', h, trial)");
        rec = psytr_record(t, pm_index(prhs[2], "trial", psytr_n_run(t) > 0 ? psytr_n_run(t) : 1));
        if (!rec && n->record_size) pm_err("arg", "no record for that trial");
        plhs[0] = pm_bytes_out(rec, rec ? n->record_size : 0);
    } else if (strcmp(cmd, "format_row") == 0) {
        pm_nargs(nrhs, 3, "line = psy_trials('format_row', h, trial)");
        plhs[0] = t_format(t, psytr_format_row, pm_index(prhs[2], "trial", psytr_n_run(t) > 0 ? psytr_n_run(t) : 1));
    } else if (strcmp(cmd, "format_header") == 0) {
        plhs[0] = t_format(t, t_fmt_header, 0);
    } else if (strcmp(cmd, "format_meta") == 0) {
        plhs[0] = t_format(t, t_fmt_meta, 0);
    } else if (strcmp(cmd, "save") == 0) {
        size_t sz = psytr_save_size(t);
        mxArray* out = mxCreateNumericMatrix(1, sz, mxUINT8_CLASS, mxREAL);
        t_check(psytr_save(t, mxGetData(out), sz));
        plhs[0] = out;
    } else if (strcmp(cmd, "restore") == 0) {
        /* outcomes: a vector; records: an n x record_size uint8 matrix, or []. */
        int m, i, rc;
        double* v;
        int* oc;
        uint8_t* recs = NULL;
        pm_nargs(nrhs, 3, "psy_trials('restore', h, outcomes [, records])");
        m = (int)mxGetNumberOfElements(prhs[2]);
        v = (double*)mxMalloc((size_t)(m > 0 ? m : 1) * sizeof(double));
        oc = (int*)mxMalloc((size_t)(m > 0 ? m : 1) * sizeof(int));
        pm_vector(prhs[2], "outcomes", v, m);
        for (i = 0; i < m; i++) {
            if (v[i] != floor(v[i])) pm_err("arg", "outcomes must be integers");
            oc[i] = (int)v[i];
        }
        if (nrhs >= 4 && !mxIsEmpty(prhs[3])) {
            size_t rs = n->record_size, j;
            const uint8_t* src;
            if (!mxIsUint8(prhs[3]) || mxGetM(prhs[3]) != (size_t)m || mxGetN(prhs[3]) != rs)
                pm_err("arg", "records must be a %d x %d uint8 matrix", m, (int)rs);
            src = (const uint8_t*)mxGetData(prhs[3]);
            recs = (uint8_t*)mxMalloc((size_t)m * rs + 1);
            for (i = 0; i < m; i++)
                for (j = 0; j < rs; j++) recs[(size_t)i * rs + j] = src[j * (size_t)m + i];
        }
        n->busy = 1;
        rc = psytr_restore(t, oc, recs, m);
        n->busy = 0;
        pm_rethrow();
        t_check(rc);
    } else if (strcmp(cmd, "rng_state") == 0) {
        if (!n->rng.has_seed) pm_err("arg", "rng_state needs an integer-seed rng");
        if (nrhs >= 3) n->rng.state = pm_u64(prhs[2], "state");
        else plhs[0] = pm_u64_out(n->rng.state);
    } else if (strcmp(cmd, "record_size") == 0) {
        plhs[0] = mxCreateDoubleScalar((double)n->record_size);
    } else {
        pm_err("usage", "unknown command '%s'", cmd);
    }
    (void)nlhs;
}
