"""The sine tables of psy_rdk.h, at 50 digits.

    uv run --with mpmath python tests/compare/rdk_tables.py

Prints the three C arrays the header holds as literals (psyrdk__sin_coarse,
psyrdk__sin_fine, psyrdk__cos_fine) and the residual constant. Each entry is
floor(v * 2^30 + 1/2) of the exact value, so no platform's libm takes part.
tests/compare/rdk_ref.py imports table() from here; the C test compares
the literals with libm in double, which catches a copy error but is not
the reference.
"""
import mpmath

mpmath.mp.dps = 50
Q = 2**30


def q30(v):
    return int(mpmath.floor(v * Q + mpmath.mpf(1) / 2))


def table():
    """coarse[257] = sin(i pi/512); fine sin and cos of j pi/131072, j < 256;
    PIH = floor(pi/2 * 2^32 + 1/2), the residual's Q30 scale per 2^-30 turn
    quarter step."""
    coarse = [q30(mpmath.sin(i * mpmath.pi / 512)) for i in range(257)]
    sfine = [q30(mpmath.sin(j * mpmath.pi / 131072)) for j in range(256)]
    cfine = [q30(mpmath.cos(j * mpmath.pi / 131072)) for j in range(256)]
    pih = int(mpmath.floor(mpmath.pi / 2 * 2**32 + mpmath.mpf(1) / 2))
    return coarse, sfine, cfine, pih


def emit(name, vals, per=8):
    print("static const int32_t %s[%d] = {" % (name, len(vals)))
    for i in range(0, len(vals), per):
        print("    " + ", ".join("%d" % v for v in vals[i:i + per]) + ",")
    print("};")


if __name__ == "__main__":
    c, s, k, pih = table()
    emit("psyrdk__sin_coarse", c)
    emit("psyrdk__sin_fine", s)
    emit("psyrdk__cos_fine", k)
    print("#define PSYRDK__PIH %dull" % pih)
