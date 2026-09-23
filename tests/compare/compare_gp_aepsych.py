#!/usr/bin/env python3
"""psy.gp beside AEPsych on the audiometric benchmark of Owen et al. 2021.

The protocol of examples/gp_audiometric.c, run through the Python binding and
through AEPsych's server-free API in the same process tree, on one response
stream:

- The observer: the metabolic phenotype of Dubno et al. 2013 (thresholds at 8
  audiometric frequencies), a not-a-knot cubic spline in kHz between them and
  the end tangent beyond them, and p = Phi((xi - theta(f)) / beta) with
  beta = 2 dB. The box is log2(f / kHz) in [-3, 4] by intensity in
  [-20, 120] dB HL.
- The session: 5 Sobol trials, then 145 adaptive ones, target p = 0.75.
  Both libraries get the SAME 5 init points (AEPsych's SobolGenerator with the
  replication's seed, which psy.gp is fed through update()) and the SAME
  uniform variate on every trial, drawn once per replication from a seeded
  numpy Generator: the response is 1 when u >= 1 - p. So the two runs differ
  only in where the adaptive trials go and in what the model makes of them.
- psy.gp: RBF kernel, probit Bernoulli, candidates on an 11 x 21 grid
  (M = 231, the C benchmark's), hyperparameters fitted every 20 trials.
- AEPsych: GPClassificationModel with its defaults (variational GP, scaled RBF
  with its priors, 100 inducing points), refit from scratch every trial (the
  Strategy default), OptimizeAcqfGenerator with its defaults, on the unit
  square, which is what AEPsych's own config path does with its normalizing
  parameter transform. The acquisitions are MCLevelSetEstimation (LSE), EAVC
  and MCPosteriorVariance (BALV), each with the probit objective and target
  0.75.
- The metrics, as the paper defines them, on a 30 x 30 grid: the mean absolute
  error of E[p] against the true p, and the mean absolute error of the 0.75
  threshold found by bracketing along intensity per frequency column (the
  first bracketing pair, linearly interpolated, identically for truth and
  model), over the columns where both cross.
- Wall time per trial: next + update for psy.gp; gen (which includes the
  model fit) + add_data for AEPsych. The metric grid is not charged.

Replications run in worker processes, one job per (library, method,
replication), each pinned to one torch thread so the per-trial times are
single-core numbers and the jobs do not fight over cores.

Usage (from the repository root, in a venv with psy-gp, numpy, scipy and
aepsych installed):

    python tests/compare/compare_gp_aepsych.py                 # 10 reps, both
    python tests/compare/compare_gp_aepsych.py --reps 3 --libs psy
    python tests/compare/compare_gp_aepsych.py --csv curves.csv --workers 8

Not a CI test: AEPsych pulls PyTorch. The output table is what goes into
psy_gp.h's STATUS block.
"""
import argparse
import math
import os
import sys
import time
from concurrent.futures import ProcessPoolExecutor, as_completed

import numpy as np
from scipy.interpolate import CubicSpline
from scipy.special import ndtr

# --- the test field (examples/gp_audiometric.c) -----------------------------

AUDIO_F = np.array([0.25, 0.5, 1.0, 2.0, 3.0, 4.0, 6.0, 8.0])
PHENO = {
    "older-normal":      [6.82, 5.49, 3.51, 5.91, 6.70, 10.09, 13.47, 12.97],
    "sensory":           [5.52, 4.19, 5.62, 19.84, 42.00, 53.33, 62.05, 66.09],
    "metabolic":         [21.23, 22.01, 24.24, 33.93, 41.36, 47.17, 54.12, 58.31],
    "metabolic+sensory": [20.26, 20.71, 21.97, 37.49, 53.18, 64.02, 75.01, 76.61],
}
XC_LO, XC_HI = -3.0, 4.0
XI_LO, XI_HI = -20.0, 120.0
LO = np.array([XC_LO, XI_LO])
HI = np.array([XC_HI, XI_HI])
NGRID = 30
N_INIT = 5
TARGET_P = 0.75
MARKS = (25, 50, 100, 150)
METHODS = ("lse", "eavc", "balv")


class Field:
    """theta(f) and the truth on the metric grid for one phenotype and beta."""

    def __init__(self, pheno="metabolic", beta=2.0):
        thr = np.array(PHENO[pheno])
        # not-a-knot in kHz, as the C file; its end extrapolation is the end
        # TANGENT, not the end cubic, so SciPy's own extrapolation is off.
        self.cs = CubicSpline(AUDIO_F, thr, bc_type="not-a-knot", extrapolate=False)
        self.f0, self.f1 = AUDIO_F[0], AUDIO_F[-1]
        self.v0, self.v1 = self.cs(self.f0), self.cs(self.f1)
        self.d0, self.d1 = self.cs(self.f0, 1), self.cs(self.f1, 1)
        self.beta = beta
        self.xc = np.linspace(XC_LO, XC_HI, NGRID)
        self.xi = np.linspace(XI_LO, XI_HI, NGRID)
        # context-major, intensity-minor, as truth_p in the C file
        cc, ii = np.meshgrid(self.xc, self.xi, indexing="ij")
        self.xs = np.column_stack([cc.ravel(), ii.ravel()])
        self.truth_p = self.p(self.xs[:, 0], self.xs[:, 1]).reshape(NGRID, NGRID)
        self.truth_thr = [column_threshold(self.truth_p[i], self.xi) for i in range(NGRID)]

    def theta(self, xc):
        f = np.exp2(np.asarray(xc, dtype=float))
        out = np.where(f <= self.f0, self.v0 + self.d0 * (f - self.f0),
                       np.where(f >= self.f1, self.v1 + self.d1 * (f - self.f1),
                                self.cs(np.clip(f, self.f0, self.f1))))
        return out

    def p(self, xc, xi):
        return ndtr((np.asarray(xi) - self.theta(xc)) / self.beta)

    def score(self, model_p):
        """(MAE of p, MAE of the 0.75 threshold in dB, columns not scored)."""
        model_p = np.asarray(model_p).reshape(NGRID, NGRID)
        mae_p = float(np.mean(np.abs(model_p - self.truth_p)))
        errs, miss = [], 0
        for i in range(NGRID):
            t = column_threshold(model_p[i], self.xi)
            if t is None or self.truth_thr[i] is None:
                miss += 1
            else:
                errs.append(abs(t - self.truth_thr[i]))
        return mae_p, (float(np.mean(errs)) if errs else float("nan")), miss


def column_threshold(p, xi, target=TARGET_P):
    for j in range(len(p) - 1):
        a, b = p[j] - target, p[j + 1] - target
        if (a <= 0.0 <= b) or (a >= 0.0 >= b):
            if b == a:
                continue
            return xi[j] + (xi[j + 1] - xi[j]) * (-a) / (b - a)
    return None


def spline_selfcheck(field):
    """The knots, and agreement with the C file's end-tangent rule."""
    thr = np.array(PHENO["metabolic"])
    assert np.allclose(field.theta(np.log2(AUDIO_F)), thr, atol=1e-9)
    # just outside each end, the tangent line, continuous with the spline
    eps = 1e-6
    assert abs(field.theta(np.log2(field.f0) - eps) - field.v0) < 1e-3
    assert abs(field.theta(np.log2(field.f1) + eps) - field.v1) < 1e-3


def respond(p, u):
    """psygp_simulate_outcome([1 - p, p], u), shared by both libraries."""
    return 0 if u < 1.0 - p else 1


# --- the stream ---------------------------------------------------------------

def stream(seed, rep, n_trials):
    """One uniform per trial, the same for both libraries."""
    return np.random.default_rng([seed, rep]).random(n_trials)


def sobol_init(rep):
    """AEPsych's own Sobol init points on the unit square, or SciPy's when
    AEPsych is not installed (then the psy.gp half cannot be paired anyway)."""
    try:
        import torch
        from aepsych.generators import SobolGenerator
        gen = SobolGenerator(lb=torch.zeros(2, dtype=torch.float64),
                             ub=torch.ones(2, dtype=torch.float64), seed=rep)
        return gen.gen(N_INIT).numpy().astype(float), "aepsych"
    except ImportError:
        from scipy.stats import qmc
        import warnings
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            return qmc.Sobol(2, scramble=True, seed=rep).random(N_INIT), "scipy"


def to_box(unit):
    return LO + (HI - LO) * np.asarray(unit)


# --- the two runners ------------------------------------------------------------

def run_psy(method, rep, args):
    import psy.gp as pg
    field = Field(args.pheno, args.beta)
    u = stream(args.seed, rep, args.trials)
    init, _ = sobol_init(rep)
    g = pg.GP(lo=list(LO), hi=list(HI), intensity_dim=1, kernel=pg.KERNEL_RBF,
              acq=method, target_p=TARGET_P, grid=list(args.grid), n_init=N_INIT,
              fit=True, fit_every=args.fit_every, stop_trials=args.trials,
              max_trials=args.trials, refine_steps=args.refine)
    rows, spent, numeric = [], 0.0, 0
    for t in range(args.trials):
        t0 = time.perf_counter()
        if t < N_INIT:
            x = list(to_box(init[t]))
        else:
            _, x = g.next()
        y = respond(float(field.p(x[0], x[1])), u[t])
        try:
            g.update(x, y)
        except pg.Numeric:
            numeric += 1        # recorded; the previous posterior stays
        spent += time.perf_counter() - t0
        n = t + 1
        if n % args.every == 0 or n in MARKS:
            mp = np.frombuffer(g.predict_p_many(field.xs))
            rows.append((n,) + field.score(mp) + (1000.0 * spent / n,))
    extra = {"log_marginal": g.log_marginal, "numeric": numeric,
             "hyper": g.hyper()}
    return rows, extra


def run_aepsych(method, rep, args):
    import logging
    import warnings
    import torch
    warnings.filterwarnings("ignore")
    torch.set_num_threads(1)
    torch.manual_seed(rep)
    from aepsych.acquisition import EAVC, MCLevelSetEstimation, MCPosteriorVariance
    from aepsych.acquisition.objective import ProbitObjective
    from aepsych.generators import OptimizeAcqfGenerator, SobolGenerator
    from aepsych.models import GPClassificationModel
    from aepsych.strategy import SequentialStrategy, Strategy
    # AEPsych configures the ROOT logger at INFO on import and logs every fit
    # and gen; 4500 trials of that bury the table.
    logging.getLogger().setLevel(logging.WARNING)

    field = Field(args.pheno, args.beta)
    u = stream(args.seed, rep, args.trials)
    init, _ = sobol_init(rep)
    lb = torch.zeros(2, dtype=torch.float64)
    ub = torch.ones(2, dtype=torch.float64)
    if method == "lse":
        acqf, kw = MCLevelSetEstimation, {"target": TARGET_P, "beta": 3.84,
                                          "objective": ProbitObjective()}
    elif method == "eavc":
        acqf, kw = EAVC, {"lb": lb, "ub": ub, "target": TARGET_P}
    else:
        acqf, kw = MCPosteriorVariance, {"objective": ProbitObjective()}
    init_strat = Strategy(generator=SobolGenerator(lb=lb, ub=ub, seed=rep),
                          lb=lb, ub=ub, outcome_types=["binary"],
                          min_asks=N_INIT, min_total_outcome_occurrences=0,
                          name="init")
    opt_strat = Strategy(generator=OptimizeAcqfGenerator(lb=lb, ub=ub, acqf=acqf,
                                                         acqf_kwargs=kw),
                         lb=lb, ub=ub, outcome_types=["binary"],
                         model=GPClassificationModel(dim=2),
                         min_asks=args.trials - N_INIT,
                         min_total_outcome_occurrences=0, name="opt")
    strat = SequentialStrategy([init_strat, opt_strat])
    grid = torch.tensor((field.xs - LO) / (HI - LO), dtype=torch.float64)
    rows, spent = [], 0.0
    init_match = True
    for t in range(args.trials):
        t0 = time.perf_counter()
        xu = strat.gen(1)
        xn = xu.detach().numpy().reshape(-1).astype(float)
        if t < N_INIT and not np.allclose(xn, init[t], atol=1e-12):
            init_match = False
        x = to_box(xn)
        y = respond(float(field.p(x[0], x[1])), u[t])
        strat.add_data(xu.reshape(1, -1), torch.tensor([float(y)], dtype=torch.float64))
        spent += time.perf_counter() - t0
        n = t + 1
        if (n % args.every == 0 or n in MARKS) and strat.model is not None and n > N_INIT:
            # The model is refit lazily at the next gen(); score what an
            # experiment would read now by running that fit here. It replaces
            # the fit gen() would have run, so it is charged to the trial.
            t0 = time.perf_counter()
            strat._strat.fit()
            strat._strat._model_is_fresh = True
            spent += time.perf_counter() - t0
            mp, _ = strat.model.predict(grid, probability_space=True)
            rows.append((n,) + field.score(mp.detach().numpy()) + (1000.0 * spent / n,))
    return rows, {"init_match": init_match}


def job(lib, method, rep, args):
    t0 = time.perf_counter()
    try:
        rows, extra = (run_psy if lib == "psy" else run_aepsych)(method, rep, args)
        err = None
    except Exception as e:  # report and keep the other jobs
        import traceback
        rows, extra, err = [], {}, "".join(traceback.format_exception_only(type(e), e)).strip()
    return lib, method, rep, rows, extra, err, time.perf_counter() - t0


# --- reporting ------------------------------------------------------------------

def mean_ci(v):
    v = np.array([x for x in v if x == x], dtype=float)
    if len(v) == 0:
        return float("nan"), float("nan"), 0
    ci = 1.96 * v.std(ddof=1) / math.sqrt(len(v)) if len(v) > 1 else 0.0
    return float(v.mean()), ci, len(v)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--reps", type=int, default=10)
    ap.add_argument("--methods", default=",".join(METHODS))
    ap.add_argument("--libs", default="psy,aepsych")
    ap.add_argument("--trials", type=int, default=150)
    ap.add_argument("--every", type=int, default=5, help="score every this many trials")
    ap.add_argument("--fit-every", type=int, default=20, dest="fit_every")
    ap.add_argument("--refine", type=int, default=0,
                    help="psy.gp refine_steps: golden-section rounds off the grid (0 = off)")
    ap.add_argument("--grid", default="11x21", type=parse_grid,
                    help="psy.gp candidate grid, frequency x intensity (default 11x21, M = 231)")
    ap.add_argument("--pheno", default="metabolic", choices=sorted(PHENO))
    ap.add_argument("--beta", type=float, default=2.0)
    ap.add_argument("--seed", type=int, default=20210419)
    ap.add_argument("--workers", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    ap.add_argument("--csv", default=None, help="per-trial curves")
    args = ap.parse_args()
    if args.csv:
        args.csv = os.path.abspath(args.csv)
    # Importing aepsych creates ./logs/aepsych_server.log in the working
    # directory. Keep that out of the repository; worker processes inherit the
    # directory at spawn.
    import tempfile
    # Spawned workers re-import this script by the path it was started with,
    # so a relative path has to be made absolute before the directory moves.
    sys.modules["__main__"].__file__ = os.path.abspath(__file__)
    os.chdir(tempfile.mkdtemp(prefix="compare_gp_aepsych_"))

    field = Field(args.pheno, args.beta)
    spline_selfcheck(field)
    methods = [m for m in args.methods.split(",") if m]
    libs = [l for l in args.libs.split(",") if l]
    for m in methods:
        if m not in METHODS:
            ap.error(f"unknown method {m}")
    if "aepsych" in libs:
        try:
            import aepsych  # noqa: F401
        except Exception as e:
            print(f"AEPsych did not import ({e!r}); running psy.gp only", file=sys.stderr)
            libs = [l for l in libs if l != "aepsych"]
    _, init_src = sobol_init(0)

    print("compare_gp_aepsych: Owen et al. 2021 audiometric benchmark")
    print(f"observer: {args.pheno}, beta = {args.beta} dB; {args.trials} trials "
          f"({N_INIT} Sobol from {init_src}, shared); target p = {TARGET_P}")
    print(f"psy.gp candidates: {args.grid[0]} x {args.grid[1]} grid "
          f"(M = {args.grid[0] * args.grid[1]}), refine_steps {args.refine}; "
          f"hyperparameters fitted every "
          f"{args.fit_every} trials")
    print(f"{args.reps} replications x {len(methods)} methods x {libs}; "
          f"{args.workers} worker processes, one thread each; seed {args.seed}")
    try:
        import importlib.metadata as md
        vers = {k: md.version(k) for k in ("psy-gp", "aepsych", "torch", "botorch", "gpytorch")
                if _has_dist(md, k)}
        print("versions:", ", ".join(f"{k} {v}" for k, v in vers.items()))
    except Exception:
        pass
    sys.stdout.flush()

    jobs = [(lib, m, r) for r in range(args.reps) for m in methods for lib in libs]
    results = {}
    t_start = time.perf_counter()
    with ProcessPoolExecutor(max_workers=args.workers) as ex:
        futs = [ex.submit(job, lib, m, r, args) for lib, m, r in jobs]
        for f in as_completed(futs):
            lib, m, r, rows, extra, err, secs = f.result()
            results[(lib, m, r)] = (rows, extra, err)
            status = "FAILED: " + err if err else f"{secs:6.1f} s"
            print(f"  {lib:8s} {m:5s} rep {r:2d}  {status}", flush=True)
    wall = time.perf_counter() - t_start

    if args.csv:
        with open(args.csv, "w") as fh:
            fh.write("lib,method,rep,trial,mae_p,mae_thr_db,nocross_cols,ms_per_trial\n")
            for (lib, m, r), (rows, _, _) in sorted(results.items()):
                for row in rows:
                    fh.write(f"{lib},{m},{r},{row[0]},{row[1]:.6f},{row[2]:.4f},{row[3]},{row[4]:.3f}\n")

    print(f"\n== {args.pheno}, beta = {args.beta}, {args.reps} replications; "
          f"mean +- 1.96 sd / sqrt(n) over replications ==")
    print("method lib      trial   MAE(p)              MAE(thr, dB)       nocross  ms/trial")
    for m in methods:
        for lib in libs:
            first = True
            for mark in MARKS:
                if mark > args.trials:
                    continue
                vals = [row for (l, mm, r), (rows, _, _) in results.items()
                        if l == lib and mm == m for row in rows if row[0] == mark]
                if not vals:
                    continue
                p, pc, n = mean_ci([v[1] for v in vals])
                t, tc, nt = mean_ci([v[2] for v in vals])
                miss = np.mean([v[3] for v in vals])
                ms = np.mean([v[4] for v in vals])
                print(f"{m if first else '':6s} {lib if first else '':8s} {mark:5d}   "
                      f"{p:.4f} +- {pc:.4f}   {t:6.2f} +- {tc:5.2f}"
                      f"{'' if nt == n else f' (n={nt})':8s} {miss:5.2f}   {ms:8.2f}")
                first = False
        print()
    fails = [(k, e) for k, (_, _, e) in results.items() if e]
    for k, e in fails:
        print(f"failed: {k}: {e}")
    if "psy" in libs:
        nums = sum(results[k][1].get("numeric", 0) for k in results if k[0] == "psy")
        lms = [results[k][1]["log_marginal"] for k in results if k[0] == "psy" and results[k][1]]
        print(f"psy.gp: PSYGP_ERR_NUMERIC {nums} times; mean final log marginal "
              f"{np.mean(lms):.1f}")
        # The fitted hyperparameters at the end of each run, in the box's own
        # units (dB for intensity, log2 kHz for frequency), so they can be put
        # beside another model's without guessing the scaling.
        for m in methods:
            hs = [results[k][1]["hyper"] for k in sorted(results)
                  if k[0] == "psy" and k[1] == m and results[k][1]]
            if not hs:
                continue
            ls = np.array([h["lengthscale"][:2] for h in hs])
            os_ = np.array([h["outputscale"] for h in hs])
            mn = np.array([h["mean"] for h in hs])
            print(f"psy.gp {m}: final lengthscale freq {ls[:, 0].mean():.3f} "
                  f"[{ls[:, 0].min():.3f}, {ls[:, 0].max():.3f}] log2 kHz, "
                  f"intensity {ls[:, 1].mean():.2f} [{ls[:, 1].min():.2f}, "
                  f"{ls[:, 1].max():.2f}] dB, outputscale {os_.mean():.3f} "
                  f"[{os_.min():.3f}, {os_.max():.3f}], mean {mn.mean():.3f}")
    if "aepsych" in libs:
        bad = [k for k in results if k[0] == "aepsych" and results[k][1]
               and not results[k][1].get("init_match", True)]
        print(f"aepsych: init points matched the shared Sobol set in "
              f"{sum(1 for k in results if k[0] == 'aepsych') - len(bad)} of "
              f"{sum(1 for k in results if k[0] == 'aepsych')} runs")
    print(f"total wall time {wall:.0f} s")
    return 1 if fails else 0


def parse_grid(text):
    try:
        c, i = (int(v) for v in text.lower().split("x"))
    except ValueError:
        raise argparse.ArgumentTypeError(f"grid must look like 11x21, not {text!r}")
    if c < 2 or i < 2:
        raise argparse.ArgumentTypeError("each grid count must be at least 2")
    return (c, i)


def _has_dist(md, name):
    try:
        md.version(name)
        return True
    except md.PackageNotFoundError:
        return False


if __name__ == "__main__":
    sys.exit(main())
