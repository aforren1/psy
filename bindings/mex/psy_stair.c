/* psy_stair.c - MATLAB/Octave MEX binding for psy_stair.h (builds to psy_stair.<mexext>)
 *
 * Command dispatch in the style of widmann's ppdev-mex: the first argument is
 * a command string, and 'open' returns an opaque uint64 handle.
 *
 *     h = psy_stair('open', struct('start', 0.5, 'n_down', 3, ...
 *                   'step_type', 'log', 'steps', [0.3 0.15 0.075], ...
 *                   'min', 0.001, 'max', 1, 'stop_reversals', 10));
 *     while ~psy_stair('done', h)
 *         x = psy_stair('next', h);
 *         psy_stair('update', h, x, run_trial(x));
 *     end
 *     thr = psy_stair('estimate', h, 'reversals');
 *     psy_stair('close', h);
 *
 * Build with build.m. See README.md for the command reference.
 *
 * The handle is about 28 KB and is malloc'd once at open; nothing else
 * allocates except the mxArrays a command returns.
 */
#define PM_MOD "psy_stair"
#include "psy_mex_util.h"

#define PSY_STAIR_IMPLEMENTATION
#include "psy_stair.h"

static void st_destroy(void* obj) { free(obj); }

static const char* const st_rule_names[] = { "updown", "asa" };
static const char* const st_step_names[] = { "lin", "log", "db" };
static const char* const st_est_names[]  = { "reversals", "trials", "median_rev", "last" };
static const char* const st_stop_names[] = { "none", "reversals", "trials", "limit", "full" };

static void st_check(int rc) {
    if (rc >= 0) return;
    switch (rc) {
        case PSYST_ERR_ARG:    pm_err("arg", "%s", psyst_strerror(rc)); break;
        case PSYST_ERR_CLOSED: pm_err("closed", "%s", psyst_strerror(rc)); break;
        case PSYST_ERR_FULL:   pm_err("full", "%s", psyst_strerror(rc)); break;
        default:               pm_err("error", "%s", psyst_strerror(rc)); break;
    }
}

static const char* const st_fields[] = {
    "start", "rule", "n_up", "n_down", "step_type", "steps", "step_down_scale",
    "target_p", "min", "max", "use_limits", "harder_is_up", "initial_rule",
    "stop_reversals", "stop_trials", "stop_at_limit", "est_reversals", "est_trials"
};

static void st_read_desc(const mxArray* s, psyst_desc* d) {
    const mxArray* f;
    pm_check_fields(s, st_fields, (int)(sizeof(st_fields) / sizeof(st_fields[0])), "desc");
    if ((f = pm_field(s, "start")))           d->start = pm_scalar(f, "start");
    if ((f = pm_field(s, "rule")))            d->rule = (psyst_rule)pm_enum(f, "rule", st_rule_names, 2);
    if ((f = pm_field(s, "n_up")))            d->n_up = pm_int(f, "n_up");
    if ((f = pm_field(s, "n_down")))          d->n_down = pm_int(f, "n_down");
    if ((f = pm_field(s, "step_type")))       d->step_type = (psyst_step_type)pm_enum(f, "step_type", st_step_names, 3);
    if ((f = pm_field(s, "steps")))           d->n_steps = pm_vector(f, "steps", d->steps, PSYST_MAX_STEPS);
    if ((f = pm_field(s, "step_down_scale"))) d->step_down_scale = pm_scalar(f, "step_down_scale");
    if ((f = pm_field(s, "target_p")))        d->target_p = pm_scalar(f, "target_p");
    if ((f = pm_field(s, "min")))             d->min = pm_scalar(f, "min");
    if ((f = pm_field(s, "max")))             d->max = pm_scalar(f, "max");
    if ((f = pm_field(s, "use_limits")))      d->use_limits = pm_bool(f, "use_limits");
    if ((f = pm_field(s, "harder_is_up")))    d->harder_is_up = pm_bool(f, "harder_is_up");
    if ((f = pm_field(s, "initial_rule")))    d->initial_rule = pm_bool(f, "initial_rule");
    if ((f = pm_field(s, "stop_reversals")))  d->stop_reversals = pm_int(f, "stop_reversals");
    if ((f = pm_field(s, "stop_trials")))     d->stop_trials = pm_int(f, "stop_trials");
    if ((f = pm_field(s, "stop_at_limit")))   d->stop_at_limit = pm_int(f, "stop_at_limit");
    if ((f = pm_field(s, "est_reversals")))   d->est_reversals = pm_int(f, "est_reversals");
    if ((f = pm_field(s, "est_trials")))      d->est_trials = pm_int(f, "est_trials");
}

static psyst_estimator st_how(int nrhs, const mxArray* prhs[], int at) {
    if (nrhs <= at) return PSYST_EST_REVERSALS;
    return (psyst_estimator)pm_enum(prhs[at], "estimator", st_est_names, 4);
}

/* History as a struct of column vectors, one field per psyst_trial member,
 * which struct2table turns into a table directly. */
static mxArray* st_history(const psyst_stair* s) {
    static const char* fields[] = { "proposed", "shown", "response", "reversal",
                                    "direction", "step_index" };
    int n = 0, i;
    const psyst_trial* h = psyst_history(s, &n);
    mxArray* out = mxCreateStructMatrix(1, 1, 6, fields);
    mxArray* c[6];
    double* p[6];
    for (i = 0; i < 6; i++) {
        c[i] = mxCreateDoubleMatrix((size_t)n, 1, mxREAL);
        p[i] = mxGetPr(c[i]);
    }
    for (i = 0; i < n; i++) {
        p[0][i] = h[i].proposed;
        p[1][i] = h[i].shown;
        p[2][i] = h[i].response;
        p[3][i] = h[i].reversal;
        p[4][i] = h[i].direction;
        p[5][i] = h[i].step_index + 1;   /* 1-based into desc.steps */
    }
    for (i = 0; i < 6; i++) mxSetField(out, 0, fields[i], c[i]);
    return out;
}

void mexFunction(int nlhs, mxArray* plhs[], int nrhs, const mxArray* prhs[]) {
    char* cmd;
    psyst_stair* s;
    (void)nlhs;
    pm_destroy = st_destroy;
    if (nrhs < 1 || !mxIsChar(prhs[0]))
        pm_err("usage", "first argument must be a command string");
    cmd = mxArrayToString(prhs[0]);

    /* ---- module commands: no handle ---- */
    if (strcmp(cmd, "open") == 0) {
        psyst_desc d;
        memset(&d, 0, sizeof(d));
        pm_nargs(nrhs, 2, "h = psy_stair('open', desc)");
        st_read_desc(prhs[1], &d);
        s = (psyst_stair*)calloc(1, sizeof(*s));
        if (!s) pm_err("memory", "out of memory");
        if (!psyst_open(s, &d)) {
            char msg[300];
            snprintf(msg, sizeof(msg), "%s", psyst_error(s));
            free(s);
            pm_err("open", "%s", msg);
        }
        plhs[0] = pm_handle_out(pm_register(s));
        return;
    }
    if (strcmp(cmd, "version") == 0) { plhs[0] = mxCreateString(psyst_version()); return; }
    if (strcmp(cmd, "weighted_scale") == 0) {
        pm_nargs(nrhs, 2, "psy_stair('weighted_scale', p)");
        plhs[0] = mxCreateDoubleScalar(psyst_weighted_scale(pm_scalar(prhs[1], "p")));
        return;
    }
    if (strcmp(cmd, "convergence_p") == 0) {
        pm_nargs(nrhs, 3, "psy_stair('convergence_p', n_up, n_down [, scale])");
        plhs[0] = mxCreateDoubleScalar(psyst_convergence_p(pm_int(prhs[1], "n_up"),
            pm_int(prhs[2], "n_down"), nrhs > 3 ? pm_scalar(prhs[3], "scale") : 0.0));
        return;
    }
    if (strcmp(cmd, "simulate_response") == 0) {
        pm_nargs(nrhs, 3, "psy_stair('simulate_response', p_correct, u)");
        plhs[0] = mxCreateDoubleScalar((double)psyst_simulate_response(
            pm_scalar(prhs[1], "p_correct"), pm_scalar(prhs[2], "u")));
        return;
    }
    if (strcmp(cmd, "strerror") == 0) {
        pm_nargs(nrhs, 2, "psy_stair('strerror', code)");
        plhs[0] = mxCreateString(psyst_strerror(pm_int(prhs[1], "code")));
        return;
    }

    /* ---- handle commands ---- */
    if (nrhs < 2) pm_err("usage", "'%s' needs a handle", cmd);
    s = (psyst_stair*)pm_lookup(prhs[1]);

    if (strcmp(cmd, "next") == 0) {
        plhs[0] = mxCreateDoubleScalar(psyst_next(s));
    } else if (strcmp(cmd, "update") == 0) {
        int r, rc;
        pm_nargs(nrhs, 4, "psy_stair('update', h, level, response)");
        r = pm_int(prhs[3], "response");
        rc = psyst_update(s, pm_scalar(prhs[2], "level"), r);
        st_check(rc);
        if (nlhs >= 1) plhs[0] = mxCreateDoubleScalar((double)rc);
    } else if (strcmp(cmd, "done") == 0) {
        plhs[0] = mxCreateLogicalScalar(psyst_done(s));
    } else if (strcmp(cmd, "stop_reason") == 0) {
        int r = (int)psyst_stop_reason(s);
        plhs[0] = mxCreateString(r >= 0 && r < 5 ? st_stop_names[r] : "none");
    } else if (strcmp(cmd, "estimate") == 0) {
        plhs[0] = mxCreateDoubleScalar(psyst_estimate(s, st_how(nrhs, prhs, 2)));
    } else if (strcmp(cmd, "estimate_count") == 0) {
        plhs[0] = mxCreateDoubleScalar((double)psyst_estimate_count(s, st_how(nrhs, prhs, 2)));
    } else if (strcmp(cmd, "n_trials") == 0) {
        plhs[0] = mxCreateDoubleScalar((double)psyst_n_trials(s));
    } else if (strcmp(cmd, "n_reversals") == 0) {
        plhs[0] = mxCreateDoubleScalar((double)psyst_n_reversals(s));
    } else if (strcmp(cmd, "history") == 0) {
        plhs[0] = st_history(s);
    } else if (strcmp(cmd, "reversal_level") == 0 || strcmp(cmd, "reversal_trial") == 0) {
        int n = psyst_n_reversals(s), rec = n < PSYST_MAX_REVERSALS ? n : PSYST_MAX_REVERSALS, i;
        int want_level = cmd[9] == 'l';
        if (nrhs >= 3) {
            /* One reversal, 1-based. */
            i = pm_index(prhs[2], "reversal", rec > 0 ? rec : 1);
            if (i >= rec) pm_err("arg", "no reversal recorded yet");
            plhs[0] = mxCreateDoubleScalar(want_level ? psyst_reversal_level(s, i)
                                                      : (double)(psyst_reversal_trial(s, i) + 1));
        } else {
            /* Every recorded reversal as a column. */
            mxArray* a = mxCreateDoubleMatrix((size_t)rec, 1, mxREAL);
            double* p = mxGetPr(a);
            for (i = 0; i < rec; i++)
                p[i] = want_level ? psyst_reversal_level(s, i) : (double)(psyst_reversal_trial(s, i) + 1);
            plhs[0] = a;
        }
    } else if (strcmp(cmd, "close") == 0) {
        pm_unregister(s);
        free(s);
    } else {
        pm_err("usage", "unknown command '%s'", cmd);
    }
}
