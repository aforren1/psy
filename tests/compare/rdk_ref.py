"""An independent model of ysp/rdk.h, written from the manual's
REPRODUCIBILITY section: Python integers and fractions, selection by
sorting, no 128-bit helpers, no radix select.

    uv run --with mpmath python tests/compare/rdk_ref.py          digests
    uv run --with mpmath python tests/compare/rdk_ref.py --c      as C

The scenarios are the ones tests/adapt/rdk_test.c runs (rdk_golden);
the test holds the digests this prints. A difference is a bug in one of
the two, never a tolerance.
"""
import math
import struct
import sys
from fractions import Fraction

sys.path.insert(0, __file__.rsplit("rdk_ref.py", 1)[0])
from rdk_tables import table  # noqa: E402

COARSE, SFINE, CFINE, PIH = table()
S = 1 << 30
M32 = 0xFFFFFFFF
M64 = (1 << 64) - 1

CIRCLE, RECT = 0, 1
WRAP, REPLOT = 0, 1
ONSET, FRAMES = 0, 1
SAME, DIFFERENT, LEAST_RECENT = 0, 1, 2
EXACT, BERNOULLI = 0, 1
NDIR, NPOS, NWALK = 0, 1, 2
CUSTOM, WN, MN, LL, BM = 0, 1, 2, 3, 4
EV_SIGNAL, EV_WRAPPED, EV_PLACED = 1, 2, 4


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def rnd(r):
    return math.floor(r + Fraction(1, 2))


def rnd2(z):
    return (z + (1 << 29)) >> 30   # Python's >> floors


def philox(seed, c0, c1, c2, c3):
    k0, k1 = seed & M32, (seed >> 32) & M32
    for _ in range(10):
        p0 = 0xD2511F53 * c0
        p1 = 0xCD9E8D57 * c2
        n0 = (p1 >> 32) ^ c1 ^ k0
        n2 = (p0 >> 32) ^ c3 ^ k1
        c0, c1, c2, c3 = n0, p1 & M32, n2, p0 & M32
        k0 = (k0 + 0x9E3779B9) & M32
        k1 = (k1 + 0xBB67AE85) & M32
    return c0, c1, c2, c3


def finite(v):
    return not (math.isnan(v) or math.isinf(v))


def angle(deg):
    deg = f32(deg)
    if not finite(deg):
        return 0
    return rnd(Fraction(deg) * (1 << 32) / 360) % (1 << 32)


def sincos(a):
    r = a & 0x3FFFFFFF
    i, j, e = r >> 22, (r >> 14) & 255, r & 16383
    sC, cC, sF, cF = COARSE[i], COARSE[256 - i], SFINE[j], CFINE[j]
    sAB = rnd2(sC * cF + cC * sF)
    cAB = rnd2(cC * cF - sC * sF)
    te = (e * PIH) >> 32
    s = sAB + rnd2(cAB * te)
    c = cAB - rnd2(sAB * te)
    q = a >> 30
    return [(s, c), (c, -s), (-s, -c), (-c, s)][q]


class Desc:
    def __init__(self, **kw):
        self.algorithm = CUSTOM
        self.aperture = CIRCLE
        self.w = self.h = 0.0
        self.count = 0
        self.density = 0.0
        self.coherence = self.direction = self.speed = 0.0
        self.signal = self.select = self.noise = self.edge = 0
        self.sets = 0
        self.lifetime = 0.0
        self.lifetime_frames = 0
        self.clock = ONSET
        self.frame_ns = 0
        self.stream = 0
        for k, v in kw.items():
            setattr(self, k, v)


class Field:
    def __init__(self, d):
        presets = {WN: (DIFFERENT, BERNOULLI, NPOS, 1), MN: (DIFFERENT, BERNOULLI, NPOS, 3),
                   LL: (LEAST_RECENT, EXACT, NPOS, 3), BM: (DIFFERENT, EXACT, NWALK, 1)}
        if d.algorithm != CUSTOM:
            self.signal, self.select, self.noise, self.sets = presets[d.algorithm]
        else:
            self.signal, self.select, self.noise, self.sets = d.signal, d.select, d.noise, d.sets or 1
        self.aperture, self.edge, self.clock = d.aperture, d.edge, d.clock
        w = f32(d.w)
        h = f32(d.h) if d.aperture == RECT else w
        mx = max(w, h)
        self.rmax = Fraction(mx) / 2
        if d.aperture == RECT:
            self.hx = rnd(S * Fraction(w) / Fraction(mx))
            self.hy = rnd(S * Fraction(h) / Fraction(mx))
        else:
            self.hx = self.hy = S
        if d.count:
            self.n = d.count
        else:
            if d.aperture == CIRCLE:
                r = f32(w * 0.5)
                area = 3.141592653589793 * r * r
            else:
                area = w * h
            self.n = int(round_half_away(f32(d.density) * area))
        self.total = self.n * self.sets
        self.frame_ns = d.frame_ns
        if d.lifetime_frames:
            self.L = d.lifetime_frames * d.frame_ns
        elif d.lifetime:
            self.L = int(round_half_away(f32(d.lifetime) * 1e9))
        else:
            self.L = 0
        self.stream = d.stream
        self.coherence, self.direction, self.speed = f32(d.coherence), f32(d.direction), f32(d.speed)

    def words(self, dot, u, slot):
        return philox(self.seed, dot, u, slot, self.stream)

    def place(self, dot, u, w0):
        hx, hy = self.hx, self.hy
        w = None
        for k in range(33):
            if k == 0:
                a, b = w0[2], w0[3]
            else:
                if k & 1:
                    w = self.words(dot, u, (k + 1) // 2)
                a, b = (w[0], w[1]) if k & 1 else (w[2], w[3])
            x = ((a * 2 * hx) >> 32) - hx
            y = ((b * 2 * hy) >> 32) - hy
            if self.aperture == RECT or x * x + y * y <= S * S:
                self.x[dot], self.y[dot] = x, y
                return
        self.x[dot], self.y[dot] = 0, 0

    def start(self, seed, t):
        self.seed = seed
        self.x = [0] * self.total
        self.y = [0] * self.total
        self.ang = [0] * self.total
        self.flags = [0] * self.total
        self.run = [0] * self.total
        self.death = [0] * self.total
        for i in range(self.total):
            w = self.words(i, 0, 0)
            self.place(i, 0, w)
            self.ang[i] = w[1]
            self.flags[i] = EV_PLACED
            if self.L:
                v = self.words(i, 0, 17)
                self.death[i] = t + 1 + (((v[0] | (v[1] << 32)) * self.L) >> 64)
        self.u = 0
        self.t0 = t
        self.T = [t] * self.sets
        self.U = [0] * self.sets
        self.last_t = t

    def step_len(self, v, dt):
        v = f32(v)
        if dt <= 0 or v == 0 or not finite(v):
            return 0
        D = rnd(abs(Fraction(v)) * dt * S / (self.rmax * 10**9))
        return min(D, 1 << 62)

    def move(self, i, u, a, D):
        if D == 0:
            return
        s, c = sincos(a)
        x, y = self.x[i], self.y[i]
        dx, dy = rnd2(D * c), rnd2(D * s)
        if self.aperture == RECT:
            hx, hy = self.hx, self.hy
            qx, qy = x + dx, y + dy
            if -hx <= qx < hx and -hy <= qy < hy:
                self.x[i], self.y[i] = qx, qy
                return
            if self.edge == REPLOT:
                self.place(i, u, self.words(i, u, 0))
                self.flags[i] |= EV_PLACED
                return
            self.x[i] = (x + hx + dx) % (2 * hx) - hx
            self.y[i] = (y + hy + dy) % (2 * hy) - hy
            self.flags[i] |= EV_WRAPPED
            return
        qx, qy = x + dx, y + dy
        if D <= 2 * S and qx * qx + qy * qy <= S * S:
            self.x[i], self.y[i] = qx, qy
            return
        if self.edge == REPLOT:
            self.place(i, u, self.words(i, u, 0))
            self.flags[i] |= EV_PLACED
            return
        self.flags[i] |= EV_WRAPPED
        along = rnd2(x * c + y * s)
        e = max(0, x * x + y * y - along * along)
        h = 0 if e >= S * S else math.isqrt(S * S - e)
        L = 2 * h
        if L < 4:
            return
        u0 = min(max(along + h, 0), L)
        u1 = (u0 + D) % L
        nx, ny = x + rnd2((u1 - u0) * c), y + rnd2((u1 - u0) * s)
        if nx * nx + ny * ny > S * S:
            u1 = min(max(u1, 2), L - 2)
            nx, ny = x + rnd2((u1 - u0) * c), y + rnd2((u1 - u0) * s)
            if nx * nx + ny * ny > S * S:
                return
        self.x[i], self.y[i] = nx, ny

    def update(self, t):
        assert t >= self.last_t
        self.u += 1
        u = self.u
        s = (u - 1) % self.sets
        n = self.n
        lo = s * n
        if self.clock == FRAMES:
            dt, now = (u - self.U[s]) * self.frame_ns, self.t0 + u * self.frame_ns
        else:
            dt, now = t - self.T[s], t
        v = f32(self.speed)
        D = self.step_len(v, dt)
        sa = angle(self.direction)
        if v < 0:
            sa = (sa + (1 << 31)) % (1 << 32)
        c = f32(self.coherence)
        if not c > 0:
            c = 0.0
        c = min(c, 1.0)
        dots = range(lo, lo + n)
        keyu = 0 if self.signal == SAME else u
        key = {i: self.words(i, keyu, 0)[0] for i in dots}
        if self.select == BERNOULLI:
            thr = math.floor(Fraction(c) * (1 << 32))
            sig = {i: key[i] < thr for i in dots}
        else:
            m = rnd(Fraction(c) * n)
            if self.signal == LEAST_RECENT:
                order = sorted(dots, key=lambda i: (self.run[i], key[i], i))
            else:
                order = sorted(dots, key=lambda i: (key[i], i))
            chosen = set(order[:m])
            sig = {i: i in chosen for i in dots}
        if self.signal == LEAST_RECENT:
            for i in dots:
                self.run[i] = min(self.run[i] + 1, 65535) if sig[i] else 0
        for i in dots:
            self.flags[i] = EV_SIGNAL if sig[i] else 0
            if self.L and self.death[i] <= now:
                w = self.words(i, u, 0)
                self.place(i, u, w)
                if self.noise == NDIR:
                    self.ang[i] = w[1]
                self.death[i] += self.L * (1 + (now - self.death[i]) // self.L)
                self.flags[i] |= EV_PLACED
                continue
            if sig[i]:
                self.move(i, u, sa, D)
            elif self.noise == NPOS:
                self.place(i, u, self.words(i, u, 0))
                self.flags[i] |= EV_PLACED
            elif self.noise == NWALK:
                self.ang[i] = self.words(i, u, 0)[1]
                self.move(i, u, self.ang[i], D)
            else:
                self.move(i, u, self.ang[i], D)
        self.T[s] = t
        self.U[s] = u
        self.last_t = t

    def digest(self):
        h = 0xCBF29CE484222325

        def fnv(h, v, nbytes):
            for k in range(nbytes):
                h ^= (v >> (8 * k)) & 255
                h = (h * 0x100000001B3) & M64
            return h
        for i in range(self.total):
            h = fnv(h, self.x[i] & M32, 4)
            h = fnv(h, self.y[i] & M32, 4)
            h = fnv(h, self.ang[i], 4)
            h = fnv(h, self.flags[i], 1)
            if self.signal == LEAST_RECENT:
                h = fnv(h, self.run[i], 2)
            if self.L:
                h = fnv(h, self.death[i] & M64, 8)
        h = fnv(h, self.u, 8)
        for s in range(self.sets):
            h = fnv(h, self.T[s] & M64, 8)
            h = fnv(h, self.U[s] & M64, 8)
        return h


def round_half_away(v):
    return math.floor(v + 0.5) if v >= 0 else -math.floor(-v + 0.5)


# --- the golden scenarios (the same table is in rdk_test.c) -------------

P = 16666667
VARY_C, VARY_DIR, VARY_SPEED = 1, 2, 4


def scenarios():
    return [
        ("same_dir_circle", Desc(w=10, count=200, coherence=0.3125, direction=30, speed=7.5), 1, 50, -1, 0, 240),
        ("wn", Desc(algorithm=WN, w=8, density=2.5, coherence=0.25, direction=200, speed=6), 2, -1, -1, 0, 240),
        ("mn_frames", Desc(algorithm=MN, w=12, count=100, coherence=0.5, direction=45, speed=10,
                           clock=FRAMES, frame_ns=P, lifetime_frames=6), 3, 70, -1, 0, 300),
        ("ll", Desc(algorithm=LL, w=9, count=90, coherence=0.6875, direction=-30, speed=4), 4, -1, -1, 0, 300),
        ("bm_rect", Desc(algorithm=BM, aperture=RECT, w=12, h=5, count=150, coherence=0.4375, direction=100,
                         speed=9, stream=7), 5, -1, 120, 0, 240),
        ("replot_life", Desc(signal=DIFFERENT, noise=NPOS, edge=REPLOT, w=10, count=120, lifetime=0.25,
                             coherence=0.5, direction=0, speed=12), 6, -1, -1, VARY_C, 240),
        ("walk_rect_life", Desc(select=BERNOULLI, noise=NWALK, aperture=RECT, w=7, h=11, edge=REPLOT,
                                lifetime_frames=5, frame_ns=P, count=80, coherence=0.375, speed=5),
         7, -1, -1, VARY_DIR | VARY_SPEED, 300),
        ("stall_circle", Desc(signal=DIFFERENT, w=6, count=64, lifetime=1.3, coherence=0.5, direction=77.25,
                              speed=3.25), 0xDEADBEEFCAFEF00D, 40, 60, 0, 200),
    ]


def schedule(k, drop, stall):
    """Update k's time (k >= 1), as rdk_test.c computes it."""
    t = 1000000000 + k * P + ((k * 7919) % 101 - 50) * 1000
    if drop >= 0 and k >= drop:
        t += P
    if stall >= 0 and k >= stall:
        t += 37000000000
    return t


def params(k, d, vary):
    c, dr, sp = d.coherence, d.direction, d.speed
    if vary & VARY_C:
        c = (k % 17) / 16.0
    if vary & VARY_DIR:
        dr = (k % 8) * 45 + 0.5
    if vary & VARY_SPEED and 100 <= k < 140:
        sp = -sp
    return c, dr, sp


def run(sc):
    name, d, seed, drop, stall, vary, nup = sc
    f = Field(d)
    f.start(seed, 1000000000)
    for k in range(1, nup + 1):
        f.coherence, f.direction, f.speed = params(k, d, vary)
        f.update(schedule(k, drop, stall))
    return f.digest(), f.n


if __name__ == "__main__":
    as_c = "--c" in sys.argv
    for sc in scenarios():
        dg, n = run(sc)
        if as_c:
            print('    0x%016xull,   /* %s (n %d) */' % (dg, sc[0], n))
        else:
            print("%-16s n %4d  %016x" % (sc[0], n, dg))
