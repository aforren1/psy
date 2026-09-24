"""Tests for psy.quest. The key cases of tests/adapt/psy_quest_test.c,
replayed through the binding. NumPy is optional: the checks that need it are
skipped without it."""
import array
import math
import os
import random
import re
import sys
import threading
import time

import pytest

import psy.quest as pq

try:
    import numpy as np
except ImportError:  # the binding itself never needs NumPy
    np = None

needs_numpy = pytest.mark.skipif(np is None, reason="numpy is not installed")

# A Psi grid: threshold and slope free, guess and lapse fixed.
PSI_STIM = [(-3.0, 0.0, 31)]
PSI_PARAMS = [[-3.0 + 3.0 * i / 40.0 for i in range(41)],
              [0.5 + 5.5 * i / 14.0 for i in range(15)], 0.5, 0.02]
TRUTH = [-1.72, 3.1, 0.5, 0.02]


def gumbel_p1(x, a, b, g, lam):
    """psy_quest.h's PSYQ_PF_GUMBEL, written the same way as the header."""
    f = 1.0 - math.exp(-math.pow(10.0, b * (x - a)))
    p1 = g + (1.0 - g - lam) * f
    return min(max(p1, 0.0), 1.0)


def gumbel_batch(stim, params):
    """pf_batch in pure Python: one stimulus, every parameter row."""
    assert isinstance(params, memoryview)
    assert params.format == "d" and params.ndim == 2 and params.shape[1] == 4
    x = stim[0]
    out = array.array("f")
    for a, b, g, lam in params.tolist():
        p1 = gumbel_p1(x, a, b, g, lam)
        out.append(1.0 - p1)
        out.append(p1)
    return out


def gumbel_cell(stim, params):
    p1 = gumbel_p1(stim[0], *params)
    return [1.0 - p1, p1]


def run(q, outcomes):
    proposals = []
    for k in outcomes:
        i = q.next()
        proposals.append(i)
        q.update(i, k)
    return proposals


def test_psi_run_recovers_threshold():
    rng = random.Random(0x5EEDF00D)
    errs, biases = [], []
    for _ in range(20):
        q = pq.Quest(PSI_STIM, PSI_PARAMS, stop_trials=100)
        while not q.done:
            i = q.next()
            q.update(i, q.simulate(i, TRUTH, rng.random()))
        assert q.n_trials == 100 and q.stop_reason == pq.Stop.TRIALS
        est = q.estimate(pq.EST_MEAN)
        assert len(est) == 4
        errs.append(abs(est[0] - TRUTH[0]))
        biases.append(est[0] - TRUTH[0])
        assert q.sd(0) < 0.25
    assert sum(errs) / len(errs) < 0.12
    assert abs(sum(biases) / len(biases)) < 0.10


def test_pf_batch_matches_builtin_gumbel():
    rng = random.Random(7)
    outcomes = [1 if rng.random() < 0.7 else 0 for _ in range(40)]
    qg = pq.Quest(PSI_STIM, PSI_PARAMS, stop_trials=1000)
    qb = pq.Quest(PSI_STIM, PSI_PARAMS, pf_batch=gumbel_batch, stop_trials=1000)
    assert qb.releases_gil["next"] and not qb.releases_gil["update_values"]
    assert run(qg, outcomes) == run(qb, outcomes)
    assert bytes(qg.posterior()) == bytes(qb.posterior())
    assert qg.p(3, TRUTH) == pytest.approx(qb.p(3, TRUTH), abs=1e-7)


def test_pf_fn_matches_builtin_gumbel_on_a_small_grid():
    stim = [(-2.5, -0.5, 5)]
    params = [(-2.0, -0.5, 4), (1.0, 4.0, 3), 0.5, 0.03]
    qg = pq.Quest(stim, params, stop_trials=100)
    qc = pq.Quest(stim, params, pf_fn=gumbel_cell, stop_trials=100)
    outcomes = [1, 1, 0, 1, 0, 0, 1, 1, 1, 0, 1, 1]
    assert run(qg, outcomes) == run(qc, outcomes)
    assert list(qg.posterior()) == pytest.approx(list(qc.posterior()), abs=1e-12)


@needs_numpy
def test_numpy_pf_batch_and_views():
    def batch(stim, params):
        P = np.asarray(params)          # views the copy; no second copy
        assert P.shape == (41 * 15, 4) and P.dtype == np.float64
        x = stim[0]
        p1 = P[:, 2] + (1 - P[:, 2] - P[:, 3]) * (1 - np.exp(-10.0 ** (P[:, 1] * (x - P[:, 0]))))
        return np.stack([1 - p1, p1], axis=1)       # float64 is accepted too
    q = pq.Quest(PSI_STIM, PSI_PARAMS, pf_batch=batch, stop_trials=100)
    qg = pq.Quest(PSI_STIM, PSI_PARAMS, stop_trials=100)
    assert run(q, [1, 0, 1, 1, 0, 1]) == run(qg, [1, 0, 1, 1, 0, 1])
    post = np.frombuffer(q.posterior(), dtype=np.float64)
    assert post.shape == (41 * 15,) and not post.flags.writeable
    assert np.asarray(q.posterior(shaped=True)).shape == (41, 15, 1, 1)
    assert np.allclose(post.reshape(41, 15).sum(axis=1), np.frombuffer(q.marginal(0)))


def test_memoryview_shapes():
    q = pq.Quest(PSI_STIM, PSI_PARAMS, stop_trials=10)
    post = q.posterior()
    assert post.format == "d" and post.readonly and post.shape == (41 * 15,)
    assert sum(post) == pytest.approx(1.0)
    shaped = q.posterior(shaped=True)
    assert shaped.shape == (41, 15, 1, 1) == (*q.param_shape,)
    m = q.marginal(1)
    assert m.format == "d" and m.shape == (15,)
    assert q.stim_shape == (31,)
    assert (q.n_stim, q.n_param, q.n_outcomes) == (31, 41 * 15, 2)


def test_grids_and_model():
    q = pq.Quest([(-3.0, 0.0, 31), [0.0, 1.0]], PSI_PARAMS, stop_trials=10)
    assert q.n_stim == 62
    i = q.stim_index([10, 1])
    assert i == 21 and q.stim_values(i) == pytest.approx((-2.0, 1.0))
    assert q.stim_value(i, 1) == 1.0
    assert q.stim_nearest([-1.97, 0.9]) == i
    t = q.param_index([20, 7, 0, 0])
    assert q.param_values(t) == pytest.approx((-1.5, 0.5 + 5.5 * 7 / 14, 0.5, 0.02))
    p = q.p(i, TRUTH)
    assert len(p) == 2 and sum(p) == pytest.approx(1.0)
    assert q.p_values([-2.0, 1.0], TRUTH) == pytest.approx(p, abs=1e-12)
    assert q.simulate(i, TRUTH, 0.0) == 0 and q.simulate(i, TRUTH, 0.999999) == 1
    h0 = q.expected_entropy(q.next())
    assert all(q.expected_entropy(s) >= h0 - 1e-9 for s in range(q.n_stim))


def test_update_values_and_history():
    q = pq.Quest(PSI_STIM, PSI_PARAMS, stop_trials=10)
    q.update(q.next(), 1)
    q.update_values([-1.23], 0)
    h = q.history()
    assert len(h) == 2 and h[1].stim_index == -1 and h[1].stim == (-1.23,)
    assert h[0].proposed_index == h[0].stim_index and h[0].outcome == 1
    assert pq.Trial._fields == ("stim", "stim_index", "proposed_index", "outcome")


def test_axes_priors_and_nuisance():
    thr = pq.linspace(-3.0, 0.0, 41)
    prior = pq.prior_normal(thr, -1.5, 0.5)
    assert len(prior) == 41
    params = [pq.linspace(-3.0, 0.0, 41, prior=prior), pq.values([2.0, 3.0, 4.0]),
              pq.values([0.45, 0.5, 0.55], nuisance=True), pq.fixed(0.02)]
    q = pq.Quest(PSI_STIM, params, stop_trials=5)
    assert q.param_shape == (41, 3, 3, 1)
    m = q.marginal(0)
    assert m[20] > m[0]                         # the Gaussian prior took
    with pytest.raises(pq.ArgumentError, match="prior has 3 weights"):
        pq.Quest(PSI_STIM, [pq.linspace(-3, 0, 41, prior=[1, 1, 1])] + PSI_PARAMS[1:],
                 stop_trials=5)
    with pytest.raises(TypeError, match="lo, hi, n"):
        pq.Quest([(-3.0, 0.0, 31.0)], PSI_PARAMS, stop_trials=5)
    with pytest.raises(TypeError, match="unknown axis key"):
        pq.Quest([{"valuez": [1.0]}], PSI_PARAMS, stop_trials=5)


def test_memory_size_is_static_and_validates():
    kw = dict(stop_trials=10)
    n = pq.Quest.memory_size(PSI_STIM, PSI_PARAMS, **kw)
    assert n > 31 * 41 * 15 * 2 * 4
    assert pq.Quest.memory_size(PSI_STIM, PSI_PARAMS) == 0      # no stop criterion
    nb = pq.Quest.memory_size(PSI_STIM, PSI_PARAMS, pf_batch=gumbel_batch, **kw)
    assert nb > n                                               # the batch buffers


def test_errors_map_to_the_hierarchy():
    for cls in (pq.ArgumentError, pq.Closed, pq.Full, pq.OutOfMemory, pq.Busy, pq.Timeout):
        assert issubclass(cls, pq.Error)
    assert issubclass(pq.ArgumentError, ValueError)
    assert issubclass(pq.Timeout, TimeoutError)
    with pytest.raises(pq.ArgumentError, match="stop"):
        pq.Quest(PSI_STIM, PSI_PARAMS)
    with pytest.raises(pq.ArgumentError):
        pq.Quest(PSI_STIM, PSI_PARAMS[:3], stop_trials=5)      # built-in needs 4
    q = pq.Quest(PSI_STIM, PSI_PARAMS, stop_trials=5)
    with pytest.raises(pq.ArgumentError):
        q.update(0, 2)                                         # outcome >= K
    with pytest.raises(pq.ArgumentError):
        q.update(31, 1)                                        # index >= S
    with pytest.raises(IndexError):
        q.marginal(4)
    q.close()
    q.close()
    with pytest.raises(pq.Closed):
        q.next()
    with pytest.raises(pq.Closed):
        q.update(0, 1)


def test_full_history():
    q = pq.Quest([(-1.0, 0.0, 2)], [(-1.0, 0.0, 2), 2.0, 0.5, 0.02], stop_trials=1)
    for _ in range(pq.MAX_TRIALS):
        q.update(0, 1)
    assert q.stop_reason == pq.Stop.TRIALS
    with pytest.raises(pq.Full):
        q.update(0, 1)


def test_callback_exceptions_propagate():
    def bad_batch(stim, params):
        raise RuntimeError("model failed")
    with pytest.raises(RuntimeError, match="model failed"):
        pq.Quest(PSI_STIM, PSI_PARAMS, pf_batch=bad_batch, stop_trials=5)

    def short_batch(stim, params):
        return [0.5, 0.5]
    with pytest.raises(pq.ArgumentError, match="P\\*K"):
        pq.Quest(PSI_STIM, PSI_PARAMS, pf_batch=short_batch, stop_trials=5)

    def int_batch(stim, params):
        return array.array("i", [0] * (len(params) * 2))
    with pytest.raises(TypeError, match="float64"):
        pq.Quest(PSI_STIM, PSI_PARAMS, pf_batch=int_batch, stop_trials=5)

    calls = []

    def rng():
        calls.append(1)
        if len(calls) > 3:
            raise KeyError("rng done")
        return 0.25
    q = pq.Quest(PSI_STIM, PSI_PARAMS, rng=rng, subset_size=4, stop_trials=5)
    assert not q.releases_gil["next"]
    with pytest.raises(KeyError):
        q.next()
    # The handle survives a failed call.
    calls.clear()
    assert 0 <= q.next() < 31


def test_rng_subset_is_reproducible():
    def make(seed):
        r = random.Random(seed)
        return pq.Quest(PSI_STIM, PSI_PARAMS, rng=r.random, subset_size=8,
                        tiebreak=pq.TIE_RANDOM, stop_trials=100)
    a, b = make(3), make(3)
    outcomes = [1, 0, 1, 1, 1, 0, 1, 1]
    assert run(a, outcomes) == run(b, outcomes)


def test_reentrant_callback_is_refused():
    holder = {}

    def batch(stim, params):
        if "q" in holder:
            holder["q"].next()
        return gumbel_batch(stim, params)
    q = pq.Quest(PSI_STIM, PSI_PARAMS, pf_batch=batch, stop_trials=5)
    holder["q"] = q
    with pytest.raises(pq.Error, match="already in a call"):
        q.update_values([-1.0], 1)


def test_async_reproduces_the_synchronous_run():
    stim = [(-2.5, -0.5, 5)]
    params = [(-2.0, -0.5, 4), (1.0, 4.0, 3), 0.5, 0.03]
    q = pq.Quest(stim, params, stop_trials=10000)
    a = pq.Async(q, estimator=pq.EST_MEAN)
    proposals, outcomes = [], []
    with a:
        seq, snap = a.poll()
        assert seq == 0 and snap.seq == 0 and snap.n_trials == 0
        assert len(snap.stim) == 1 and snap.stop == pq.Stop.NONE
        with pytest.raises(pq.Error, match="owned"):
            q.next()
        for i in range(12):
            proposals.append(snap.proposed)
            outcomes.append(1 if (i * 5 + 1) % 3 else 0)
            s = a.submit(snap.proposed, outcomes[-1])
            assert s == i + 1
            got, snap = a.wait(s, 5.0)
            assert got >= s and snap.seq == s and snap.n_trials == i + 1
            assert snap.update_rc == 0
            assert a.pending == 0
        assert a.policy in (pq.ASYNC_NORMAL, pq.ASYNC_BELOW_NORMAL)
    assert not a.running and a.policy == pq.ASYNC_NONE
    assert q.n_trials == 12
    q2 = pq.Quest(stim, params, stop_trials=10000)
    assert run(q2, outcomes) == proposals
    assert bytes(q.posterior()) == bytes(q2.posterior())
    assert list(snap.estimate) == q.estimate(pq.EST_MEAN)
    assert snap.entropy == q.entropy() and snap.sd == q.sd(0)
    # The snapshot is still readable after the stop.
    assert a.poll()[0] == 12


def test_async_busy_timeout_and_refusals():
    # Psi-marginal with no table: a selection is tens of milliseconds, so the
    # queue fills and a zero timeout expires.
    params = [(-3.0, 0.0, 61), (0.5, 6.0, 12), pq.values([0.45, 0.5, 0.55], nuisance=True),
              pq.values([0.0, 0.03, 0.06], nuisance=True)]
    q = pq.Quest(PSI_STIM, params, no_table=True, stop_trials=10000)
    with pq.Async(q) as a:
        seqs, busy = [], False
        for _ in range(4 * pq.ASYNC_QUEUE):
            try:
                seqs.append(a.submit(5, 1))
            except pq.Busy:
                busy = True
                break
        assert busy
        with pytest.raises(pq.Timeout):
            a.wait(seqs[-1], 0.0)
    # stop() drained every accepted response.
    assert q.n_trials == len(seqs)

    with pytest.raises(pq.ArgumentError, match="rng"):
        pq.Async(pq.Quest(PSI_STIM, PSI_PARAMS, rng=random.random, subset_size=4,
                          stop_trials=5)).start()
    qb = pq.Quest(PSI_STIM, PSI_PARAMS, pf_batch=gumbel_batch, stop_trials=5)
    with pq.Async(qb) as a:
        a.submit(0, 1)
        with pytest.raises(pq.ArgumentError, match="Python"):
            a.submit_values([-1.5], 1)
    with pytest.raises(pq.Closed):
        a.submit(0, 1)
    with pytest.raises(TypeError):
        pq.Async(object())


def test_async_submit_values_with_a_builtin_model():
    q = pq.Quest(PSI_STIM, PSI_PARAMS, stop_trials=100)
    with pq.Async(q) as a:
        s = a.submit_values([-1.234], 1)
        a.wait(s, 5.0)
    assert q.history()[0].stim == (-1.234,) and q.history()[0].stim_index == -1


def test_gil_is_released_by_a_builtin_model():
    # A second Python thread runs while the main thread is inside a no_table
    # selection. With a switch interval far longer than the selection, the
    # interpreter never forces a switch inside the call, so a tick stamped
    # inside it means next() released the GIL.
    params = [(-3.0, 0.0, 61), (0.5, 6.0, 12), pq.values([0.45, 0.5, 0.55]),
              pq.values([0.0, 0.03, 0.06])]
    q = pq.Quest(PSI_STIM, params, no_table=True, stop_trials=100)
    assert q.releases_gil == {"next": True, "update": True, "update_values": True}
    ticks = []
    stop = threading.Event()

    def spin():
        while not stop.is_set():
            ticks.append(time.perf_counter())
    old = sys.getswitchinterval()
    sys.setswitchinterval(0.25)
    t = threading.Thread(target=spin)
    windows = []
    try:
        t.start()
        for k in (1, 0, 1):
            t0 = time.perf_counter()
            i = q.next()
            t1 = time.perf_counter()
            windows.append((t0, t1))
            q.update(i, k)
    finally:
        stop.set()
        t.join()
        sys.setswitchinterval(old)
    inside = sum(1 for x in ticks for (t0, t1) in windows if t0 < x < t1)
    assert inside > 0


def test_version_matches_pyproject():
    # pyproject.toml's version is a copy; __version__ comes from the header
    # compiled into the extension. Fail when the two drift.
    path = os.path.join(os.path.dirname(__file__), "..", "pyproject.toml")
    if not os.path.exists(path):
        pytest.skip("pyproject.toml is not beside the tests")
    with open(path, encoding="utf-8") as f:
        m = re.search(r'^version\s*=\s*"([^"]+)"', f.read(), re.M)
    assert m, "no version in pyproject.toml"
    assert pq.__version__ == m.group(1)


def test_wait_racing_a_draining_stop():
    # psy_rt.h 0.3.1: a wait on another thread survives a stop. A seq already
    # queued is drained and the wait returns it; a seq never submitted gives
    # Closed (STOPPED).
    params = [(-3.0, 0.0, 61), (0.5, 6.0, 12), pq.values([0.45, 0.5, 0.55], nuisance=True),
              pq.values([0.0, 0.03, 0.06], nuisance=True)]
    q = pq.Quest(PSI_STIM, params, no_table=True, stop_trials=10000)
    a = pq.Async(q)
    a.start()
    seqs = [a.submit(5, 1) for _ in range(4)]
    got, errs = {}, {}

    def waiter(name, seq):
        try:
            got[name] = a.wait(seq, 30.0)[0]
        except pq.Error as e:
            errs[name] = e
    ts = [threading.Thread(target=waiter, args=("queued", seqs[-1])),
          threading.Thread(target=waiter, args=("never", seqs[-1] + 5))]
    for t in ts:
        t.start()
    time.sleep(0.01)
    a.stop()                     # drains the four
    for t in ts:
        t.join(30.0)
    assert got.get("queued", -1) >= seqs[-1]
    assert isinstance(errs.get("never"), pq.Closed)
    assert q.n_trials == 4


# --- snapshots (psy_quest.h 0.5.0) -------------------------------------------

SNAP_PARAMS = [(-3.0, 0.0, 41), pq.values([1.0, 2.0, 3.0, 4.0], nuisance=True), 0.5, 0.02]


def _snap_run(q, rng, n, start=0):
    """Drive trials start..n-1 against a fixed observer; proposals and posteriors."""
    props, posts = [], []
    for k in range(start, n):
        i = q.next()
        props.append(i)
        q.update(i, q.simulate(i, TRUTH, rng.random()))
        posts.append(bytes(q.posterior()))
    return props, posts


@pytest.mark.parametrize("cut", [0, 1, 7, 23])
@pytest.mark.parametrize("pending", [False, True])
def test_resume_matches_the_uninterrupted_run(cut, pending):
    # A random subset and random ties go through rng, so the generator's
    # position is part of what has to survive the cut.
    def make(gen):
        return pq.Quest(PSI_STIM, SNAP_PARAMS, rng=gen.random, subset_size=8,
                        tiebreak=pq.TIE_RANDOM, stop_trials=1000)
    ref_gen, obs_ref = random.Random(11), random.Random(99)
    ref = make(ref_gen)
    ref_props, ref_posts = _snap_run(ref, obs_ref, 24)

    gen, obs = random.Random(11), random.Random(99)
    a = make(gen)
    _snap_run(a, obs, cut)
    if pending:
        a.next()                 # a proposal made, no update yet
    snap, gen_state = a.save(), gen.getstate()
    del a

    gen2 = random.Random()
    gen2.setstate(gen_state)     # the generator is the caller's
    b = pq.Quest.load(snap, PSI_STIM, SNAP_PARAMS, rng=gen2.random, subset_size=8,
                      tiebreak=pq.TIE_RANDOM, stop_trials=1000)
    assert b.n_trials == cut
    props, posts = _snap_run(b, obs, 24, cut)
    assert props == ref_props[cut:]
    assert posts == ref_posts[cut:]
    assert b.estimate(pq.EST_MEAN) == ref.estimate(pq.EST_MEAN)
    assert b.history() == ref.history()
    assert gen2.getstate() == ref_gen.getstate()


def test_resume_with_pf_batch():
    q = pq.Quest(PSI_STIM, PSI_PARAMS, pf_batch=gumbel_batch, stop_trials=100)
    ref = pq.Quest(PSI_STIM, PSI_PARAMS, pf_batch=gumbel_batch, stop_trials=100)
    outcomes = [1, 1, 0, 1, 0, 1, 1, 1, 0, 1]
    run(q, outcomes[:4])
    run(ref, outcomes)
    q2 = pq.Quest.load(q.save(), PSI_STIM, PSI_PARAMS, pf_batch=gumbel_batch, stop_trials=100)
    run(q2, outcomes[4:])
    assert bytes(q2.posterior()) == bytes(ref.posterior())


def test_load_refuses_a_mismatched_desc():
    q = pq.Quest(PSI_STIM, PSI_PARAMS, stop_trials=50)
    run(q, [1, 0, 1])
    snap = q.save()
    with pytest.raises(pq.Error, match="stop_trials") as e:
        pq.Quest.load(snap, PSI_STIM, PSI_PARAMS, stop_trials=60)
    assert not isinstance(e.value, pq.ArgumentError)
    with pytest.raises(pq.Error):
        pq.Quest.load(snap, [(-3.0, 0.0, 30)], PSI_PARAMS, stop_trials=50)
    with pytest.raises(pq.Error):
        pq.Quest.load(snap[:-5], PSI_STIM, PSI_PARAMS, stop_trials=50)
    # The priors are not compared: the saved posterior replaces them.
    thr = pq.values(PSI_PARAMS[0], prior=[1.0] * 20 + [5.0] + [1.0] * 20)
    q2 = pq.Quest.load(snap, PSI_STIM, [thr] + PSI_PARAMS[1:], stop_trials=50)
    assert bytes(q2.posterior()) == bytes(q.posterior())


def test_queue_depth_one_pushes_back():
    params = [(-3.0, 0.0, 61), (0.5, 6.0, 12), pq.values([0.45, 0.5, 0.55], nuisance=True),
              pq.values([0.0, 0.03, 0.06], nuisance=True)]
    q = pq.Quest(PSI_STIM, params, no_table=True, stop_trials=10000)
    with pq.Async(q, queue_depth=1) as a:
        accepted = 0
        with pytest.raises(pq.Busy):
            for _ in range(3):   # one in flight, one queued, the next refused
                a.submit(5, 1)
                accepted += 1
        assert accepted <= 2
    assert q.n_trials == accepted
    with pytest.raises(pq.ArgumentError):
        pq.Async(q, queue_depth=pq.ASYNC_QUEUE + 1)
