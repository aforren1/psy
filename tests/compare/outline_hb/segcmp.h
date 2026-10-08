/* Closed contours as cycles of quadratic segments: equal when one is a
 * rotation of the other, bit for bit, because HarfBuzz may start a contour
 * whose first point is off the curve at another on-curve point. Zero-length
 * segments and contours of one repeated point are skipped: HarfBuzz passes
 * them on, ysp/outline.h drops them, and neither has an area. */
#define SEGCMP_MAX 65536

static double segcmp_a[6 * SEGCMP_MAX], segcmp_b[6 * SEGCMP_MAX];

/* The contour's segments that have a length, into s; -1 if more than
 * SEGCMP_MAX. */
static int segs_of(const yol_path* p, int k, double* s) {
    int f = (int)(p->contours[k] & ~YOL_OPEN), l, i, j, n = 0;
    l = (k + 1 < p->n_contours ? (int)(p->contours[k + 1] & ~YOL_OPEN) : p->n_pts) - 1;
    for (i = f; i + 2 <= l; i += 2) {
        const double* q = p->pts + 2 * i;
        if (q[0] == q[2] && q[2] == q[4] && q[1] == q[3] && q[3] == q[5]) continue;
        if (n == SEGCMP_MAX) return -1;
        for (j = 0; j < 6; j++) s[6 * n + j] = q[j];
        n++;
    }
    return n;
}

/* 1 if equal. *dmax grows to the largest, over unequal contours, of the
 * smallest difference over rotations; HUGE_VAL if the contour or segment
 * counts differ. */
static int seg_same(const yol_path* a, const yol_path* b, double* dmax) {
    int ka = 0, kb = 0, ok = 1;
    for (;;) {
        int na = 0, nb = 0, r, i, j, found = 0;
        double best = HUGE_VAL;
        while (ka < a->n_contours && (na = segs_of(a, ka, segcmp_a)) == 0) ka++;
        while (kb < b->n_contours && (nb = segs_of(b, kb, segcmp_b)) == 0) kb++;
        if (ka >= a->n_contours || kb >= b->n_contours) {
            if (ka < a->n_contours || kb < b->n_contours) { *dmax = HUGE_VAL; return 0; }
            return ok;
        }
        if (na < 0 || na != nb) { *dmax = HUGE_VAL; return 0; }
        for (r = 0; r < na && !found; r++) {
            double d = 0;
            int ex = 1;
            for (i = 0; i < na; i++)
                for (j = 0; j < 6; j++) {
                    double x = segcmp_a[6 * i + j], y = segcmp_b[6 * ((i + r) % na) + j], e = fabs(x - y);
                    if (x != y) ex = 0;
                    if (e > d) d = e;
                }
            if (d < best) best = d;
            found = ex;
        }
        if (!found) { ok = 0; if (best > *dmax) *dmax = best; }
        ka++; kb++;
    }
}
