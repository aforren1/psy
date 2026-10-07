"""Tests for psy.trials: the key properties of tests/adapt/psy_trials_test.c,
through the binding."""
import collections
import math
import os
import re
import struct

import pytest

import psy.trials as pt

try:
    import psy.stair as st
except ImportError:  # psy-stair is a separate distribution
    st = None

needs_stair = pytest.mark.skipif(st is None, reason="psy-stair is not installed")


def run_all(t, outcome=1):
    order = []
    while (ti := t.next()) is not None:
        order.append(ti.condition)
        t.update(outcome)
    return order


def test_splitmix_matches_the_reference():
    u, s = pt.splitmix(0)
    assert u == (0xE220A8397B1DCDAF >> 11) / 9007199254740992.0
    u, s = pt.splitmix(s)
    assert u == (0x6E789E6AA1B965F4 >> 11) / 9007199254740992.0
    assert s == (2 * 0x9E3779B97F4A7C15) % 2 ** 64


def test_factorial_round_trip():
    t = pt.Trials(factors=[("a", 2), ("b", 3), (None, 4)], reps=1)
    assert t.n_conditions == 24 and t.n_factors == 3
    for c in range(24):
        lv = t.levels(c)
        assert lv == (c // 12, (c // 4) % 3, c % 4)          # last factor fastest
        assert t.condition_from_levels(list(lv)) == c
        assert t.level(c, pt.CONDITION) == c


def test_sequential_is_repetition_major():
    t = pt.Trials(n_conditions=4, reps=3)
    assert run_all(t) == [0, 1, 2, 3] * 3
    t = pt.Trials(n_conditions=3, cond_reps=[1, 3, 2])
    assert run_all(t) == [0, 1, 2, 1, 2, 1]


@pytest.mark.parametrize("seed", range(50))
def test_random_orders(seed):
    t = pt.Trials(n_conditions=5, reps=4, order=pt.ORDER_RANDOM, rng=seed)
    order = t.schedule()
    for r in range(4):
        assert sorted(order[5 * r:5 * r + 5]) == list(range(5))
    t = pt.Trials(n_conditions=5, cond_reps=[1, 2, 3, 0, 6], order=pt.ORDER_FULL_RANDOM,
                  rng=seed)
    assert collections.Counter(t.schedule()) == {0: 1, 1: 2, 2: 3, 4: 6}


def test_fisher_yates_draw_order():
    # u = 0: for i = 3..1 swap i and 0: [0 1 2 3] -> [1 2 3 0].
    t = pt.Trials(n_conditions=4, reps=1, order=pt.ORDER_FULL_RANDOM, rng=lambda: 0.0)
    assert t.schedule() == [1, 2, 3, 0]
    t = pt.Trials(n_conditions=4, reps=1, order=pt.ORDER_FULL_RANDOM,
                  rng=lambda: 1.0 - 2 ** -53)
    assert t.schedule() == [0, 1, 2, 3]
    calls = []

    def counting():
        calls.append(1)
        return 0.5
    pt.Trials(n_conditions=10, reps=1, order=pt.ORDER_FULL_RANDOM, n_practice=3, rng=counting)
    assert len(calls) == 3 + 9


def test_int_seed_equals_splitmix_callable():
    state = [12345]

    def rng():
        u, state[0] = pt.splitmix(state[0])
        return u
    a = pt.Trials(n_conditions=6, reps=5, order=pt.ORDER_FULL_RANDOM, rng=12345)
    b = pt.Trials(n_conditions=6, reps=5, order=pt.ORDER_FULL_RANDOM, rng=rng)
    assert a.schedule() == b.schedule() and a.rng_state == state[0]


def max_run_ok(seq, key, n):
    run, prev = 0, object()
    for x in seq:
        k = key(x)
        run = run + 1 if k == prev else 1
        prev = k
        if k is not None and run > n:
            return False
    return True


@pytest.mark.parametrize("seed", range(30))
def test_constraints_hold_on_every_trial(seed):
    t = pt.Trials(factors=[("orientation", 2), ("contrast", 5)], reps=10,
                  order=pt.ORDER_CONSTRAINED,
                  constraints=[pt.max_run("orientation", pt.ANY_LEVEL, 3),
                               pt.no_transition(pt.CONDITION, 3, 3),
                               pt.first_not(1, 0)],
                  rng=seed)
    order = run_all(t)
    assert len(order) == 100
    ori = [c // 5 for c in order]
    assert max_run_ok(ori, lambda x: x, 3)
    assert all(not (a == 3 and b == 3) for a, b in zip(order, order[1:]))
    assert order[0] % 5 != 0
    assert not any(h.violation for h in t.history())


def test_min_gap_and_window():
    t = pt.Trials(n_conditions=4, cond_reps=[6, 14, 14, 14], order=pt.ORDER_CONSTRAINED,
                  constraints=[pt.min_gap(pt.CONDITION, 0, 2),
                               pt.max_in_window(pt.CONDITION, 1, 5, 2)], rng=9)
    order = run_all(t)
    zeros = [i for i, c in enumerate(order) if c == 0]
    assert all(b - a > 2 for a, b in zip(zeros, zeros[1:]))
    assert all(order[i:i + 5].count(1) <= 2 for i in range(len(order)))


def test_impossible_design_is_error_with_the_header_message():
    with pytest.raises(pt.Error, match=r"constraint 0, max_run\(cond,any,1\).*after 10 swaps") as e:
        pt.Trials(n_conditions=2, cond_reps=[3, 5], order=pt.ORDER_CONSTRAINED,
                  constraints=[pt.max_run(pt.CONDITION, pt.ANY_LEVEL, 1)], max_swaps=10, rng=5)
    assert not isinstance(e.value, pt.ArgumentError)


def test_bad_descs_are_argument_errors():
    with pytest.raises(pt.ArgumentError):
        pt.Trials()                                             # nothing to run
    with pytest.raises(pt.ArgumentError, match="rng"):
        pt.Trials(n_conditions=3, reps=1, order=pt.ORDER_RANDOM)
    with pytest.raises(pt.ArgumentError):
        pt.Trials(n_conditions=3, reps=1, constraints=[pt.max_run(pt.CONDITION, 0, 1)])
    with pytest.raises(pt.ArgumentError, match="cond_reps has 2"):
        pt.Trials(n_conditions=3, cond_reps=[1, 1])
    with pytest.raises(pt.ArgumentError, match="factor 'size'"):
        pt.Trials(factors=[("a", 2)], reps=1, order=pt.ORDER_CONSTRAINED, rng=1,
                  constraints=[pt.max_run("size", 0, 1)])
    with pytest.raises(TypeError):
        pt.Trials(n_conditions=1, reps=1, tracks=[object()], track_rate=0.5)


class CountingTrack:
    def __init__(self, n):
        self.left, self.asked = n, 0

    def is_done(self):
        self.asked += 1
        return self.left <= 0


def test_tracks_round_robin_and_finished_never_asked():
    tr = [CountingTrack(2), CountingTrack(3)]
    t = pt.Trials(tracks=tr, interleave=pt.INTERLEAVE_ROUND_ROBIN)
    seq = []
    while (ti := t.next()) is not None:
        seq.append(ti.track)
        tr[ti.track].left -= 1
        t.update(1)
    assert seq == [0, 1, 0, 1, 1]
    asked = [x.asked for x in tr]
    assert t.done
    assert [x.asked for x in tr] == asked       # finished tracks are not asked again


@needs_stair
def test_interleaved_staircases_with_catch_trials():
    stairs = [st.Staircase(start=10.0 + i, steps=1.0, stop_trials=8) for i in range(3)]
    t = pt.Trials(n_conditions=1, reps=6, order=pt.ORDER_CONSTRAINED,
                  constraints=[pt.min_gap(pt.CONDITION, 0, 1)],
                  tracks=stairs, track_rate=0.9, rng=20260923, record_size=8)
    while (ti := t.next()) is not None:
        if ti.track >= 0:
            s = stairs[ti.track]
            x = s.next()
            s.update(x, 1)
            t.update(1, struct.pack("<d", x))
        else:
            t.update(0)
    assert all(s.done for s in stairs) and t.done
    h = t.history()
    assert len(h) == 3 * 8 + 6
    assert sum(1 for x in h if x.track >= 0) == 24
    # A catch trial that follows another only happens once the tracks ran out.
    for i in range(1, len(h)):
        if h[i].condition == 0 and h[i - 1].condition == 0:
            assert h[i].violation
    track_trials = [i for i, x in enumerate(h) if x.track == 0]
    levels = [struct.unpack("<d", t.record(i))[0] for i in track_trials]
    assert levels == [x.shown for x in stairs[0].history()]
    assert t.record(next(i for i, x in enumerate(h) if x.track < 0)) == bytes(8)


def test_requeue_and_tallies():
    t = pt.Trials(n_conditions=2, reps=2)
    t.next()
    t.requeue()                                  # condition 0 goes to the end
    outcomes = []
    while (ti := t.next()) is not None:
        t.update(ti.condition)                   # outcome = the row, for the tally
        outcomes.append(ti.condition)
    h = t.history()
    assert h[0].outcome == pt.REQUEUE and h[-1].requeued and h[-1].condition == 0
    assert t.n_valid(0) == 2 and t.count(0, pt.REQUEUE) == 1
    assert t.proportion(1, 1) == 1.0 and math.isnan(t.proportion(0, pt.INVALID))
    with pytest.raises(pt.OutOfOrder):
        t.update(1)
    t = pt.Trials(tracks=[CountingTrack(1)])
    t.next()
    with pytest.raises(pt.ArgumentError):
        t.requeue()                              # the track decides, not the sequencer
    with pytest.raises(pt.ArgumentError):
        t.update(-3)                             # below INVALID


def _session(rng, tracks=None):
    return pt.Trials(factors=[("side", 2), ("size", 3)], reps=6, order=pt.ORDER_CONSTRAINED,
                     constraints=[pt.max_run("side", pt.ANY_LEVEL, 2)], block_size=12,
                     n_practice=2, n_warmup=1, requeue_gap=3, rng=rng, record_size=4)


def _drive(t, n, k0=0):
    for k in range(k0, k0 + n):
        ti = t.next()
        if ti is None:
            return False
        if k % 7 == 3 and not (ti.practice or ti.warmup):
            t.requeue()
        else:
            t.update(k % 2, struct.pack("<i", k))
    return True


def test_save_load_round_trip():
    ref = _session(77)
    _drive(ref, 1000)
    for cut in (0, 1, 5, 17, 30):
        a = _session(77)
        _drive(a, cut)
        snap, state = a.save(), a.rng_state
        b = pt.Trials.load(snap, factors=[("side", 2), ("size", 3)], reps=6,
                           order=pt.ORDER_CONSTRAINED,
                           constraints=[pt.max_run("side", pt.ANY_LEVEL, 2)], block_size=12,
                           n_practice=2, n_warmup=1, requeue_gap=3, rng=state, record_size=4)
        assert b.save() == snap
        _drive(b, 1000, cut)
        assert b.history() == ref.history()
        assert [b.record(i) for i in range(b.n_run)] == [ref.record(i) for i in range(ref.n_run)]
        assert b.save() == ref.save()
    with pytest.raises(pt.Error, match="reps"):
        pt.Trials.load(ref.save(), factors=[("side", 2), ("size", 3)], reps=5,
                       order=pt.ORDER_CONSTRAINED,
                       constraints=[pt.max_run("side", pt.ANY_LEVEL, 2)], block_size=12,
                       n_practice=2, n_warmup=1, requeue_gap=3, rng=1, record_size=4)
    with pytest.raises(pt.Error):
        pt.Trials.load(b"PSTR garbage", n_conditions=1, reps=1)


def test_restore_reproduces_the_run():
    ref = _session(31)
    _drive(ref, 1000)
    h = ref.history()
    recs = [ref.record(i) for i in range(ref.n_run)]
    t = _session(31)
    t.restore([x.outcome for x in h], recs)
    assert t.history() == h and t.rng_state == ref.rng_state
    assert t.save() == ref.save()
    with pytest.raises(pt.OutOfOrder):
        t.restore([1])


def test_format_functions():
    tr = CountingTrack(1)
    t = pt.Trials(factors=[("orientation", 2), (None, 3), ('a,"b"', 1)], reps=1,
                  n_practice=1, tracks=[tr], track_rate=1.0, record_size=8)
    assert t.format_header() == ("index,block,rep,condition,track,practice,warmup,requeued,"
                                 "after_break,outcome,orientation,factor1,\"a,\"\"b\"\"\"\n")
    t.next()
    assert t.format_row(0) == "0,-1,-1,0,-1,1,0,0,0,,0,0,0\n"
    t.update(1)
    assert t.format_row(0) == "0,-1,-1,0,-1,1,0,0,0,1,0,0,0\n"
    t.next()
    tr.left = 0
    t.update(pt.INVALID)
    assert t.format_row(1) == "1,0,-1,-1,0,0,0,0,0,-1,,,\n"
    assert t.format_meta().startswith("psy_trials=" + pt.__version__ + " conditions=6 factors=3 levels=2x3x1")
    with pytest.raises(pt.ArgumentError):
        t.format_row(9)


def test_callback_exceptions_propagate():
    class Broken:
        def is_done(self):
            raise RuntimeError("track failed")
    t = pt.Trials(tracks=[Broken()])
    with pytest.raises(RuntimeError, match="track failed"):
        t.next()

    def bad_rng():
        raise KeyError("rng failed")
    with pytest.raises(KeyError):
        pt.Trials(n_conditions=3, reps=2, order=pt.ORDER_FULL_RANDOM, rng=bad_rng)


def test_version_matches_pyproject():
    # pyproject.toml's version is a copy; __version__ comes from the header
    # compiled into the extension. Fail when the two drift.
    path = os.path.join(os.path.dirname(__file__), "..", "pyproject.toml")
    if not os.path.exists(path):
        pytest.skip("pyproject.toml is not beside the tests")
    with open(path, encoding="utf-8") as f:
        m = re.search(r'^version\s*=\s*"([^"]+)"', f.read(), re.M)
    assert m, "no version in pyproject.toml"
    assert pt.__version__ == m.group(1)


# --------------------------------------------------------------------- v0.2
# Tables (psy_table.h), order lists, draws, subsets, groups, units, balance,
# Latin squares and rules text, through the binding. The CSV parser and the
# number parser are checked against two oracles written by other people:
# Python's csv module and float().

import csv
import io
import random

COND_CSV = (
    "target,contrast,word,list,n\n"
    "a,0.25,cat,1,2\na,0.5,dog,1,1\nb,0.25,\"new york\",2,2\nb,0.5,ox,2,1\n"
    "catch,0,emu,1,1\ncatch,0,yak,2,1\n"
)


def test_table_basics_and_bytes():
    tab = pt.Table(COND_CSV)
    assert tab.columns == ["target", "contrast", "word", "list", "n"]
    assert tab.types == ["string", "number", "string", "integer", "integer"]
    assert len(tab) == tab.n_rows == 6
    assert tab.value(2, "word") == "new york"
    assert tab.value(1, 1) == 0.5 and tab.value(0, "n") == 2
    assert tab.levels("target") == ["a", "b", "catch"]
    assert tab.find("contrast", "0.50") == 1
    assert tab.row(4) == {"target": "catch", "contrast": 0.0, "word": "emu", "list": 1, "n": 1}
    b = tab.to_bytes()
    assert b[:4] == b"PSTB" and len(b) % 8 == 0
    t2 = pt.Table.from_bytes(b)
    assert t2.hash == tab.hash and t2.row(2) == tab.row(2)
    bad = bytearray(b)
    bad[60] ^= 1
    with pytest.raises(pt.ArgumentError, match="hash"):
        pt.Table.from_bytes(bytes(bad))
    with pytest.raises(pt.ArgumentError,
                       match="line 3, row 2, column 'x' \\(1\\): '0,5' is not a number; the decimal"):
        pt.Table('x,y\n1,a\n"0,5",b\n', types={"x": "number"})
    t3 = pt.Table("code\n007\n7\n", types={"code": "string"})
    assert t3.levels("code") == ["007", "7"]


def _random_field(rng):
    alphabet = ["a", "b", "z", " ", ",", '"', "\n", "\r\n", "1", ".", "é", "中", "x"]
    return "".join(rng.choice(alphabet) for _ in range(rng.randint(0, 8)))


@pytest.mark.parametrize("seed", range(40))
def test_csv_against_the_csv_module(seed):
    rng = random.Random(seed)
    ncol = rng.randint(1, 6)
    rows = []
    while len(rows) < rng.randint(1, 30):
        row = [_random_field(rng) for _ in range(ncol)]
        if any(row):                                   # all-empty rows are skipped by design
            rows.append(row)
    buf = io.StringIO()
    w = csv.writer(buf, lineterminator=rng.choice(["\n", "\r\n"]))
    w.writerow(["c%d" % i for i in range(ncol)])
    w.writerows(rows)
    text = buf.getvalue()
    if rng.random() < 0.3:
        text = "﻿" + text                         # a byte order mark
    tab = pt.Table(text, types={"c%d" % i: "string" for i in range(ncol)})
    want = list(csv.reader(io.StringIO(text.lstrip("﻿"), newline="")))[1:]
    assert tab.n_rows == len(want)
    for r, row in enumerate(want):
        assert [tab.value(r, c) for c in range(ncol)] == row


def _random_decimal(rng):
    nd = rng.randint(1, 19)
    digits = str(rng.randint(1, 9)) + "".join(str(rng.randint(0, 9)) for _ in range(nd - 1))
    pt_at = rng.randint(0, nd)
    s = digits[:pt_at] + ("." if pt_at < nd and rng.random() < 0.7 else "") + digits[pt_at:]
    if s.startswith("."):
        s = "0" + s
    if rng.random() < 0.5:
        s += "e%d" % rng.randint(-330, 300)
    if rng.random() < 0.3:
        s = "-" + s
    return s


@pytest.mark.parametrize("seed", range(10))
def test_numbers_against_float(seed):
    rng = random.Random(1000 + seed)
    vals = []
    while len(vals) < 500:
        s = _random_decimal(rng)
        v = float(s)
        if v == 0.0 or math.isinf(v):
            continue                                   # range errors are tested in C
        vals.append((s, v))
    text = "v\n" + "\n".join(s for s, _ in vals) + "\n"
    tab = pt.Table(text, types={"v": "number"})
    for r, (s, v) in enumerate(vals):
        got = tab.value(r, 0)
        assert got == v and math.copysign(1, got) == math.copysign(1, v), s


def test_table_conditions_and_names():
    tab = pt.Table(COND_CSV)
    t = pt.Trials(table=tab, reps=4, order=pt.ORDER_CONSTRAINED, rng=3,
                  constraints=[pt.max_run("target", pt.ANY_LEVEL, 1),
                               pt.max_in_window("target", "catch", 2, 1)])
    assert t.n_conditions == 6 and t.n_factors == 5 and t.table is tab
    s = t.schedule()
    lv = [tab.level(c, "target") for c in s]
    assert all(a != b for a, b in zip(lv, lv[1:]))
    assert t.values(2)["word"] == "new york"
    assert t.format_header().endswith("target,contrast,word,list,n\n")
    with pytest.raises(pt.ArgumentError, match="no level 'd'"):
        pt.Trials(table=tab, reps=2, order=pt.ORDER_CONSTRAINED, rng=1,
                  constraints=[pt.max_run("target", "d", 1)])


def test_rules_give_the_c_schedule():
    tab = pt.Table(COND_CSV)
    a = pt.Trials(table=tab, reps=6, order=pt.ORDER_CONSTRAINED, rng=99, block_size=12,
                  constraints=[pt.max_run("target", pt.ANY_LEVEL, 2),
                               pt.max_in_window("target", "catch", 2, 1),
                               pt.first_not("target", "catch"),
                               pt.min_gap("word", "new york", 3)])
    rules = ("order constrained\nreps 6\nmax_run target 2\nmax_in_window target=catch 2 1\n"
             "first_not target=catch\nmin_gap word=\"new york\" 3\nblock_size 12\n")
    b = pt.Trials(table=tab, rules=rules, rng=99)
    assert a.schedule() == b.schedule()
    c = pt.Trials(table=tab, rules=b.format_rules(), rng=99)
    assert c.schedule() == b.schedule()
    with pytest.raises(pt.ArgumentError, match="rules line 2.*did you mean 'max_run'"):
        pt.Trials(table=tab, rules="reps 2\nmax_runn target 2\n", rng=1)
    with pytest.raises(pt.ArgumentError, match="the statement here is 'min_gap'"):
        pt.Trials(table=tab, rules="mindist target 2\n", rng=1)


def test_list_draws_subset():
    t = pt.Trials(n_conditions=4, order=pt.ORDER_LIST, order_list=[3, 0, 0, 2])
    assert t.schedule() == [3, 0, 0, 2]
    t = pt.Trials(n_conditions=3, order=pt.ORDER_WITH_REPLACEMENT, draws=3000,
                  weights=[1, 0, 3], rng=5)
    s = t.schedule()
    assert s.count(1) == 0 and abs(s.count(2) / 3000 - 0.75) < 0.04
    t = pt.Trials(n_conditions=10, reps=1, subset=4, rng=8)
    s = t.schedule()
    assert len(s) == 4 and s == sorted(s)


def test_latin():
    for n in range(1, 13):
        for bal in (False, True):
            rows = pt.latin_rows(n, bal)
            seen = collections.Counter()
            for r in range(rows):
                row = pt.latin(n, r, bal)
                assert sorted(row) == list(range(n))
                if bal:
                    seen.update(zip(row, row[1:]))
            if bal:
                want = 2 if n % 2 else 1
                assert all(seen[(a, b)] == want for a in range(n) for b in range(n) if a != b)
    assert pt.latin(4, 0, True) == [0, 1, 3, 2]


def test_groups_and_units():
    tab = pt.Table("block,kind\nA,prime\nA,target\nA,fill\nB,prime\nB,target\nB,fill\n")
    t = pt.Trials(table=tab, reps=3, order=pt.ORDER_CONSTRAINED, rng=4,
                  groups=pt.groups("block", "blocked", "balanced_latin", participant=1),
                  constraints=[pt.followed_by("kind", "prime", "target")])
    s = t.schedule()
    blocks = [tab.value(c, "block") for c in s]
    assert blocks == ["B"] * 9 + ["A"] * 9
    kinds = [tab.value(c, "kind") for c in s]
    for i, k in enumerate(kinds):
        if k == "prime":
            assert kinds[i + 1] == "target"


def test_balance_pairs_and_leadin():
    tab = pt.Table("lvl\nA\nB\nC\n")
    t = pt.Trials(table=tab, reps=6, order=pt.ORDER_CONSTRAINED, rng=12,
                  constraints=[pt.balance("lvl")])
    s = t.schedule()
    assert len(s) == 19
    pairs = collections.Counter(zip(s, s[1:]))
    assert all(pairs[(a, b)] == 2 for a in range(3) for b in range(3))
    run_all(t)
    h = t.history()
    assert h[0].leadin and h[0].rep == -1 and not any(x.leadin for x in h[1:])
    t = pt.Trials(table=tab, reps=4, order=pt.ORDER_CONSTRAINED, rng=12,
                  constraints=[pt.balance("lvl", no_repeat=True, no_leadin=True)])
    s = t.schedule()
    assert len(s) == 12 and all(a != b for a, b in zip(s, s[1:]))
