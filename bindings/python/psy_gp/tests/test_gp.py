"""Tests for the psy.gp binding. Run with pytest from anywhere once psy-gp is
installed. NumPy is optional: the tests that need it skip without it.

The numerical tolerances are the ones tests/adapt/psy_gp_test.c enforces on
the same observers, so a pass here says the binding did not lose anything on
the way through, not that the model got better.
"""
import array
import math
import os
import random
import re

import pytest

import psy.gp as pg

try:
    import numpy as np
except ImportError:  # the binding has no NumPy dependency
    np = None

SQRT2 = math.sqrt(2.0)


def phi(z):
    return 0.5 * math.erfc(-z / SQRT2)


# --- the 1-D probit observer of the C test ----------------------------------

SLOPE, X50 = 8.0, 0.40
THR75 = X50 + 0.67448975019608171 / SLOPE


def run_1d(acq, stream, trials=100):
    """The C test's run_observer(): same desc, same observer, a different
    uniform stream (Python's Mersenne Twister, not splitmix64)."""
    u = random.Random(1000 + stream)
    g = pg.GP(lo=[0.0], hi=[1.0], acq=acq, target_p=0.75, grid=[65],
              n_init=8, fit=True, fit_every=10, stop_trials=trials)
    while not g.done:
        idx, x = g.next()
        assert idx >= -1
        p = phi(SLOPE * (x[0] - X50))
        g.update(x, pg.simulate_outcome([1.0 - p, p], u.random()))
    assert g.n_trials == trials
    thr, lo, hi = g.threshold()
    assert lo <= thr + 1e-9 and thr <= hi + 1e-9
    return abs(thr - THR75)


@pytest.mark.parametrize("acq", [pg.ACQ_LSE, pg.ACQ_EAVC])
def test_1d_threshold_recovery(acq):
    errs = [run_1d(acq, s) for s in range(8)]
    mean = sum(errs) / len(errs)
    print(f"acq {acq}: mean abs threshold error {mean:.4f} over 8 streams")
    assert mean < 0.08


def test_string_enum_names_match_constants():
    a = pg.GP(lo=[0], hi=[1], acq="EAVC", kernel="rbf", link="probit",
              target_p=0.75, stop_trials=5)
    b = pg.GP(lo=[0], hi=[1], acq=pg.ACQ_EAVC, target_p=0.75, stop_trials=5)
    assert a.next() == b.next()


# --- the other likelihoods ----------------------------------------------------

def test_ordinal_40_trials():
    u = random.Random(55)
    cut = [0.0, 1.0, 2.0]
    g = pg.GP(lo=[0.0], hi=[1.0], lik=pg.LIK_ORDINAL, n_outcomes=4,
              target_outcome=2, target_p=0.5, acq=pg.ACQ_BALD, grid=[33],
              n_init=8, fit=True, fit_every=20, hyper={"cutpoint": cut},
              stop_trials=40)
    while not g.done:
        _, x = g.next()
        f = 4.0 * (x[0] - 0.5)
        c = [phi(cj - f) for cj in cut]
        p = [c[0], c[1] - c[0], c[2] - c[1], 1.0 - c[2]]
        g.update(x, pg.simulate_outcome(p, u.random()))
    lo, hi = g.predict_p(0.05), g.predict_p(0.95)   # P(y >= 2)
    assert 0.0 <= lo < hi <= 1.0
    assert hi - lo > 0.3
    probs = g.predict_outcomes(0.5)
    assert len(probs) == 4 and abs(sum(probs) - 1.0) < 1e-9
    # cutpoint[0] = 0 reads as "fit", so the vector moves; it stays ordered.
    c = g.hyper()["cutpoint"]
    assert len(c) == 3 and c[0] < c[1] < c[2]


def test_categorical_40_trials():
    u = random.Random(66)
    g = pg.GP(lo=[0.0], hi=[1.0], lik="categorical", n_outcomes=3,
              target_outcome=1, target_p=0.5, acq=pg.ACQ_BALV, grid=[25],
              n_init=10, stop_trials=40)
    while not g.done:
        _, x = g.next()
        fs = [2.0 - 4.0 * x[0], 0.0, -2.0 + 4.0 * x[0]]
        e = [math.exp(v) for v in fs]
        g.update(x, pg.simulate_outcome([v / sum(e) for v in e], u.random()))
    left, right = g.predict_outcomes(0.0), g.predict_outcomes(1.0)
    assert abs(sum(left) - 1.0) < 1e-9 and abs(sum(right) - 1.0) < 1e-9
    assert max(range(3), key=lambda k: left[k]) == 0
    assert max(range(3), key=lambda k: right[k]) == 2
    mu0, _ = g.predict_f(0.0, k=0)
    mu2, _ = g.predict_f(0.0, k=2)
    assert mu0 > mu2


def test_gaussian_40_trials():
    u = random.Random(44)
    g = pg.GP(lo=[0.0], hi=[1.0], lik=pg.LIK_GAUSSIAN, acq=pg.ACQ_BALV,
              target_value=0.5, grid=[33], n_init=6, fit=True, fit_every=20,
              stop_trials=40)
    while not g.done:
        _, x = g.next()
        g.update_real(x, math.sin(4.0 * x[0]) + 0.05 * (u.random() - 0.5))
    xs = [i / 20 for i in range(21)]
    rms = math.sqrt(sum((g.predict_p(x) - math.sin(4 * x)) ** 2 for x in xs) / len(xs))
    assert rms < 0.05
    with pytest.raises(pg.Error):
        g.update([0.5], 1)          # the discrete update under GAUSSIAN
    with pytest.raises(pg.Error):
        g.predict_outcomes(0.5)     # no outcome probabilities under GAUSSIAN


# --- batch prediction ---------------------------------------------------------

def fitted_2d(trials=40, seed=3, **extra):
    u = random.Random(seed)
    desc = dict(lo=[0.0, 0.0], hi=[1.0, 1.0], intensity_dim=1, target_p=0.75,
                grid=[9, 17], n_init=6, fit=True, fit_every=10,
                stop_trials=trials)
    desc.update(extra)
    g = pg.GP(**desc)
    while not g.done:
        _, x = g.next()
        p = phi(8.0 * (x[1] - (0.3 + 0.3 * x[0])))
        g.update(x, pg.simulate_outcome([1 - p, p], u.random()))
    return g


def grid_points(n=12):
    return [[i / (n - 1), j / (n - 1)] for i in range(n) for j in range(n)]


def test_predict_p_many_equals_predict_p():
    g = fitted_2d()
    pts = grid_points()
    one = [g.predict_p(x) for x in pts]
    flat = array.array("d", [v for x in pts for v in x])
    for xs in (pts, tuple(tuple(x) for x in pts), flat, memoryview(flat)):
        many = g.predict_p_many(xs)
        assert isinstance(many, memoryview) and many.format == "d"
        assert many.tolist() == one            # to the last bit, as in C
    mu, sd = g.predict_f_many(pts)
    for i, x in enumerate(pts):
        assert (mu[i], sd[i]) == g.predict_f(x)
    assert g.predict_p_many([]).tolist() == []


@pytest.mark.skipif(np is None, reason="NumPy not installed")
def test_predict_many_numpy():
    g = fitted_2d()
    pts = np.array(grid_points())
    ref = [g.predict_p(list(x)) for x in pts]
    out = np.frombuffer(g.predict_p_many(pts))
    assert out.tolist() == ref
    # Fortran order and float32 take the copying paths and still agree.
    assert np.frombuffer(g.predict_p_many(np.asfortranarray(pts))).tolist() == ref
    f32 = np.frombuffer(g.predict_p_many(pts.astype(np.float32)))
    assert np.allclose(f32, ref, atol=1e-5)
    with pytest.raises(ValueError):
        g.predict_p_many(np.zeros((4, 3)))


# --- the async layer ----------------------------------------------------------

def responder(seed):
    u = random.Random(seed)

    def respond(x):
        p = phi(8.0 * (x[1] - (0.3 + 0.3 * x[0])))
        return pg.simulate_outcome([1 - p, p], u.random())
    return respond


ASYNC_DESC = dict(lo=[0.0, 0.0], hi=[1.0, 1.0], intensity_dim=1,
                  target_p=0.75, acq="eavc", grid=[9, 17], n_init=6, fit=True,
                  fit_every=10, stop_trials=40)


def test_async_reproduces_sync():
    respond = responder(7)
    g1 = pg.GP(**ASYNC_DESC)
    sync_x = []
    while not g1.done:
        _, x = g1.next()
        sync_x.append(x)
        g1.update(x, respond(x))

    respond = responder(7)
    g2 = pg.GP(**ASYNC_DESC)
    async_x = []
    with pg.Async(g2, context=[0.5]) as a:
        assert a.running
        assert a.policy in (pg.POLICY_NORMAL, pg.POLICY_BELOW_NORMAL)
        seq, snap = a.poll()
        assert seq == 0
        with pytest.raises(pg.Busy):
            g2.predict_p([0.5, 0.5])   # the thread owns the handle
        while not snap.done:
            async_x.append(snap.x)
            s = a.submit(snap.x, respond(snap.x))
            got, snap = a.wait(s, timeout_s=30.0)
            assert got >= s and snap.update_rc == 0
        last = snap
    assert not a.running
    assert async_x == sync_x
    assert g2.history() == g1.history()
    assert g2.log_marginal == g1.log_marginal
    assert g2.hyper() == g1.hyper()
    thr = g2.threshold([0.5])
    assert thr == g1.threshold([0.5])
    assert (last.threshold, last.threshold_lo, last.threshold_hi) == thr
    assert last.n_trials == 40 and last.stop == pg.STOP_TRIALS


def test_async_busy_timeout_and_rules():
    g = pg.GP(lo=[0.0, 0.0], hi=[1.0, 1.0], intensity_dim=1, target_p=0.75,
              acq="eavc", grid=[33, 33], n_init=2, stop_trials=200)
    a = pg.Async(g, context=0.5)
    a.start()
    try:
        with pytest.raises(pg.Busy):
            a.start()
        busy = 0
        seqs = []
        for k in range(40):     # far faster than an EAVC update at M = 1089
            try:
                seqs.append(a.submit([0.5, (k % 10) / 10], k % 2))
            except pg.Busy as e:
                assert e.code == pg.ERR_BUSY
                busy += 1
        assert busy > 0
        assert a.pending <= pg.ASYNC_QUEUE
        with pytest.raises(pg.Timeout):
            a.wait(seqs[-1] + 100, timeout_s=0.01)
        with pytest.raises(pg.Error):
            a.submit([2.0, 0.5], 1)     # outside the box
    finally:
        a.stop()
    # stop() drained every accepted response, and the GP is ours again.
    assert g.n_trials == len(seqs)
    g.predict_p([0.5, 0.5])


def test_async_refuses_python_rng():
    g = pg.GP(lo=[0], hi=[1], target_p=0.75, stop_trials=5,
              rng=random.Random(1).random)
    with pytest.raises(ValueError):
        pg.Async(g).start()
    with pytest.raises(ValueError):
        pg.Async(g, context=[0.5])           # a 1-D GP has no context


# --- errors -------------------------------------------------------------------

def test_error_hierarchy():
    for e in (pg.Numeric, pg.NoCross, pg.Full, pg.Busy, pg.Timeout):
        assert issubclass(e, pg.Error)


def test_bad_desc():
    with pytest.raises(pg.Error):
        pg.GP(lo=[0], hi=[1], target_p=0.75)          # no stop criterion
    with pytest.raises(TypeError):
        pg.GP(lo=[0], hi=[1], stop_trials=5, bogus=1)
    with pytest.raises(TypeError):
        pg.GP([0], [1])
    with pytest.raises(ValueError):
        pg.GP(lo=[0, 0], hi=[1], stop_trials=5)
    with pytest.raises(ValueError):
        pg.GP(lo=[0], hi=[1], stop_trials=5, acq="nope")


def test_nocross_and_full():
    g = pg.GP(lo=[0.0], hi=[1.0], target_p=0.75, stop_trials=3, max_trials=3)
    with pytest.raises(pg.NoCross) as ei:
        g.threshold()                  # the prior is flat at p = 0.5
    assert ei.value.code == pg.ERR_NOCROSS
    for k in range(3):
        g.update([0.5], k % 2)
    assert g.done and g.stop_reason in (pg.STOP_TRIALS, pg.STOP_FULL)
    with pytest.raises(pg.Full):
        g.update([0.5], 1)
    with pytest.raises(pg.Full):
        g.next()


def test_numeric_keeps_the_trial():
    # A noiseless Gaussian model with duplicate stimuli: the kernel matrix is
    # singular, which is the one reliable way to make a Cholesky fail.
    g = pg.GP(lo=[0.0], hi=[1.0], lik="gaussian", acq="balv", stop_trials=10,
              hyper={"noise_sd": 1e-200, "outputscale": 1.0,
                     "lengthscale": [0.5], "mean": 1e-12}, jitter=1e-300)
    g.update_real([0.5], 0.0)
    with pytest.raises(pg.Numeric) as ei:
        g.update_real([0.5], 1.0)
    assert ei.value.code == pg.ERR_NUMERIC
    assert g.n_trials == 2
    assert math.isfinite(g.predict_p(0.5))


def test_argument_errors_and_close():
    g = pg.GP(lo=[0.0], hi=[1.0], target_p=0.75, stop_trials=5)
    with pytest.raises(pg.Error) as ei:
        g.update([1.5], 1)
    assert ei.value.code == pg.ERR_ARG
    with pytest.raises(ValueError):
        g.update([0.1, 0.2], 1)
    with pytest.raises(pg.Error):
        g.predict_p(2.0)
    g.close()
    with pytest.raises(pg.Error) as ei:
        g.next()
    assert ei.value.code == pg.ERR_CLOSED


# --- odds and ends --------------------------------------------------------------

def test_memory_size_is_the_layout():
    base = dict(lo=[0, 0], hi=[1, 1], n_candidates=512, max_trials=512,
                target_p=0.75, stop_trials=5)
    lse = pg.GP.memory_size(**base)
    eavc = pg.memory_size(acq="eavc", **base)
    # EAVC adds exactly the M x M look-ahead covariance and its M x N block.
    assert eavc - lse == 512 * 512 * 8 + 512 * 512 * 8
    with pytest.raises(pg.Error):
        pg.GP.memory_size(lo=[0], hi=[1])


def test_hyper_object_and_candidates():
    h = pg.Hyper(lengthscale=[0.2], outputscale=2.0, mean=0.5)
    cand = [[i / 20] for i in range(21)]
    g = pg.GP(lo=[0.0], hi=[1.0], hyper=h, candidates=cand, target_p=0.75,
              n_init=2, stop_trials=10)
    assert g.hyper()["lengthscale"] == [0.2]
    assert g.hyper()["outputscale"] == 2.0
    assert g.n_candidates == 21
    assert g.candidate(20) == [1.0]
    del cand                  # the binding keeps its own copy
    for k in range(4):
        i, x = g.next()
        g.update(x, k % 2)
    i, x = g.next()
    assert 0 <= i < 21 and x == g.candidate(i)
    assert math.isfinite(g.acq_score(i))
    g.update(x, 1)            # an unanswered proposal would be handed out again
    j, y = g.next_subset([3, 4, 5])
    assert j in (3, 4, 5) and y == g.candidate(j)


def test_rng_callable():
    r = random.Random(5)
    g = pg.GP(lo=[0.0], hi=[1.0], acq="random", stop_trials=10, rng=r.random)
    idx, x = g.next()
    assert idx == -1 and 0.0 <= x[0] <= 1.0

    def broken():
        raise RuntimeError("generator failed")
    g = pg.GP(lo=[0.0], hi=[1.0], acq="random", stop_trials=10, rng=broken,
              n_init=0)
    for k in range(4):
        g.update([k / 4], k % 2)
    with pytest.raises(RuntimeError):
        g.next()


def test_fit_step_and_refit():
    g = fitted_2d(trials=30, fit=False, refit_every=5)
    before = g.log_marginal
    steps = 0
    while g.fit_step():
        steps += 1
        assert steps < 100
    assert g.log_marginal != before
    g.refit()
    g.fit()


def test_simulate_outcome():
    assert pg.simulate_outcome([0.25, 0.75], 0.2) == 0
    assert pg.simulate_outcome([0.25, 0.75], 0.3) == 1
    assert pg.GP.simulate_outcome([0.1, 0.2, 0.7], 0.25) == 1


def test_second_async_cannot_take_or_release_the_gp():
    g = pg.GP(lo=[0.0], hi=[1.0], target_p=0.75, stop_trials=20)
    a, b = pg.Async(g), pg.Async(g)
    a.start()
    try:
        with pytest.raises(pg.Busy):
            b.start()
        b.stop()                        # never started: must not free the GP
        with pytest.raises(pg.Busy):
            g.next()
        seq = a.submit([0.5], 1)
        got, snap = a.wait(seq, timeout_s=10.0)
        assert got >= seq and snap.n_trials == 1
    finally:
        a.stop()
    assert g.n_trials == 1


def test_version_matches_pyproject():
    # pyproject.toml's version is a copy; __version__ comes from the header
    # compiled into the extension. Fail when the two drift.
    path = os.path.join(os.path.dirname(__file__), "..", "pyproject.toml")
    if not os.path.exists(path):
        pytest.skip("pyproject.toml is not beside the tests")
    with open(path, encoding="utf-8") as f:
        m = re.search(r'^version\s*=\s*"([^"]+)"', f.read(), re.M)
    assert m, "no version in pyproject.toml"
    assert pg.__version__ == m.group(1)


def test_refine_steps():
    with pytest.raises(pg.Error):
        pg.GP(lo=[0], hi=[1], target_p=0.75, stop_trials=5, refine_steps=33)
    u = random.Random(9)
    g = pg.GP(lo=[0.0], hi=[1.0], target_p=0.75, grid=[9], n_init=4,
              stop_trials=30, refine_steps=2)
    off_grid = 0
    while not g.done:
        idx, x = g.next()
        if g.n_trials >= 4 and idx == -1:
            off_grid += 1
        p = phi(SLOPE * (x[0] - X50))
        g.update(x, pg.simulate_outcome([1 - p, p], u.random()))
    # The refined proposals leave the 9-point grid (0.125 apart).
    assert off_grid > 0
    assert any(abs(h["x"][0] * 8 - round(h["x"][0] * 8)) > 1e-9 for h in g.history())


def test_psychometric_model():
    # The header rejects what the model cannot carry, at open.
    base = dict(lo=[0.0, 0.0], hi=[1.0, 1.0], intensity_dim=1, target_p=0.75,
                stop_trials=60, model="psychometric")
    for bad in (dict(kernel="semip"), dict(lik="categorical", n_outcomes=3),
                dict(lik="gaussian")):
        with pytest.raises(pg.Error):
            pg.GP(**dict(base, **bad))
    # The look-ahead acquisitions are accepted since psy_gp.h 0.4.0.
    for ok in ("eavc", "localmi"):
        la = pg.GP(**dict(base, acq=ok, grid=[5, 9], n_init=4, stop_trials=8))
        uu = random.Random(3)
        while not la.done:
            _, x = la.next()
            p = phi(8.0 * (x[1] - 0.5))
            la.update(x, pg.simulate_outcome([1 - p, p], uu.random()))
        assert la.n_trials == 8
    assert pg.MODEL_PSYCHOMETRIC != pg.MODEL_GP

    u = random.Random(21)
    g = pg.GP(grid=[9, 17], n_init=8, fit=True, fit_every=20, acq="lse",
              **dict(base, model=pg.MODEL_PSYCHOMETRIC, stop_trials=100))
    while not g.done:
        _, x = g.next()
        p = phi(8.0 * (x[1] - (0.3 + 0.3 * x[0])))
        g.update(x, pg.simulate_outcome([1 - p, p], u.random()))
    h = g.hyper()
    assert {"lengthscale_g", "outputscale_g", "mean_g"} <= set(h)
    assert len(h["lengthscale_g"]) == 2 and h["outputscale_g"] > 0.0

    z75 = 0.67448975019608171
    errs = []
    for ctx in (0.2, 0.5, 0.8):
        thr, lo, hi = g.threshold([ctx])
        # m and log-slope g at this context: k = 0 and k = 1; intensity ignored
        mm, sm = g.predict_f([ctx, 0.5], k=0)
        mg, sg = g.predict_f([ctx, 0.5], k=1)
        # E[m + f_t exp(-g)] for (m, g) jointly Gaussian needs no covariance
        closed = mm + z75 * math.exp(-mg + sg * sg / 2.0)
        assert abs(thr - closed) < 1e-9 * max(1.0, abs(closed))
        assert lo <= thr <= hi
        if 0.0 < lo and hi < 1.0:          # unclamped: E -+ 1.96 sd
            assert abs((thr - lo) - (hi - thr)) < 1e-9
        errs.append(abs(thr - (0.3 + 0.3 * ctx + z75 / 8.0)))
    # One stream: a mean over three contexts at 100 trials, with the 1-D
    # test's tolerance, rather than a per-context bound one seed can break.
    assert sum(errs) / len(errs) < 0.08
    assert not g.threshold_multi_cross
