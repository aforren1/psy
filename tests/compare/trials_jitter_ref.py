# /// script
# requires-python = ">=3.10"
# dependencies = ["mpmath"]
# ///
"""Reference values for psy_trials.h's fixed-point jitter logarithm.

psytr__neglog(Y) returns -ln(Y / 2^53) in Q58 for Y in [1, 2^53], from
integer arithmetic only: Y = m 2^e with m in [1, 2), m multiplied by a
table reciprocal INV[i] (Q63, rounded up) of 1 + i/128 chosen by m's next
7 bits, so m x INV[i] = 1 + t with 0 <= t < 2^-7, and

    -ln(Y / 2^53) = (53 - e) ln 2 - T[i] - ln(1 + t),   T[i] = -ln(INV[i] / 2^63)

with ln(1 + t) by an integer Horner series of degree 8.

    uv run tests/compare/trials_jitter_ref.py tables    # the C tables
    uv run tests/compare/trials_jitter_ref.py points    # the test's reference points
    uv run tests/compare/trials_jitter_ref.py check FILE # compare a dump

`check` reads lines "Y F" (decimal) written by tests/compare/trials_jitter_dump.c
and reports the largest error in units of 2^-58.
"""
import random
import sys

import mpmath

mpmath.mp.prec = 300
TWO53 = 1 << 53


def inv(i):
    # ceil(2^63 x 128 / (128 + i))
    return -((-(1 << 70)) // (128 + i))


def t_entry(i):
    return int(mpmath.nint(-mpmath.log(mpmath.mpf(inv(i)) / mpmath.mpf(2) ** 63) * mpmath.mpf(2) ** 63))


def ref_q58(y):
    return -mpmath.log(mpmath.mpf(y) / TWO53) * mpmath.mpf(2) ** 58


def tables():
    ln2 = int(mpmath.nint(mpmath.log(2) * mpmath.mpf(2) ** 63))
    print("#define PSYTR__LN2_Q63 0x%016xULL" % ln2)
    print("static const uint64_t psytr__inv_q63[128] = {")
    for i in range(0, 128, 4):
        print("    " + ", ".join("0x%016xULL" % inv(k) for k in range(i, i + 4)) + ",")
    print("};")
    print("static const uint64_t psytr__nlinv_q63[128] = {")
    for i in range(0, 128, 4):
        print("    " + ", ".join("0x%016xULL" % t_entry(k) for k in range(i, i + 4)) + ",")
    print("};")


def points():
    rng = random.Random(20261007)
    ys = [1, 2, 3, 4, 5, 7, 1 << 26, (1 << 52) - 1, 1 << 52, (1 << 52) + 1, TWO53 - 2, TWO53 - 1, TWO53]
    for _ in range(40):
        ys.append(rng.randrange(1, TWO53 + 1))
    for _ in range(40):
        e = rng.randrange(0, 53)
        ys.append(rng.randrange(1 << e, 1 << (e + 1)))
    for k in range(1, 128, 9):          # near each table boundary
        ys.append((TWO53 >> 1) + ((TWO53 >> 1) * k) // 128)
        ys.append((TWO53 >> 1) + ((TWO53 >> 1) * k) // 128 - 1)
    print("static const struct { uint64_t y, f; } jt_log_ref[%d] = {" % len(ys))
    for y in ys:
        print("    { %dULL, %dULL }," % (y, int(mpmath.nint(ref_q58(y)))))
    print("};")


def check(path):
    worst = mpmath.mpf(0)
    n = 0
    with open(path) as f:
        for line in f:
            y, got = (int(v) for v in line.split())
            err = abs(mpmath.mpf(got) - ref_q58(y))
            if err > worst:
                worst = err
            n += 1
    print("%d values, largest error %s units of 2^-58" % (n, mpmath.nstr(worst, 6)))


if __name__ == "__main__":
    {"tables": tables, "points": points}.get(sys.argv[1], lambda: check(sys.argv[2]))() \
        if sys.argv[1] != "check" else check(sys.argv[2])
