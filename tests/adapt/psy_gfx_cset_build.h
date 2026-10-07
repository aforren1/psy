/* psy_gfx_cset_build.h - a minimal curve-set builder for psy_gfx.h's test and
 * bench. Not part of the library: the pack tool's outline builder is the
 * real one. This one writes psy_gfx.h's curve-set format, version 1
 * (docs/psy_gfx.md, "Curve sets: the format"), from closed contours of
 * quadratic Beziers, so the test needs no font file.
 *
 * A contour is 2k points (x, y pairs, set units, y down): on-curve point,
 * control point, on-curve point, control point, ... ; segment j runs from
 * on-curve j through control j to on-curve j + 1, and the last returns to
 * the first. A line has its control point equal to its start (p1 = p0).
 *
 * Public domain / MIT-0, as psy_gfx.h.
 */
#ifndef PSY_GFX_CSET_BUILD_H
#define PSY_GFX_CSET_BUILD_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct tcs_contour { const double* pts; int n; } tcs_contour;   /* n points, even */

typedef struct tcs_set {
    float*    texels;  uint32_t n_texels, cap_t;
    uint32_t* words;   uint32_t n_words, cap_w;
    int       fail;    /* out of memory or a bad argument */
} tcs_set;

static void tcs__tgrow(tcs_set* s, uint32_t n) {
    if (s->n_texels + n <= s->cap_t) return;
    while (s->n_texels + n > s->cap_t) s->cap_t = s->cap_t ? 2 * s->cap_t : 256;
    s->texels = (float*)realloc(s->texels, 16u * (size_t)s->cap_t);
    if (!s->texels) s->fail = 1;
}
static void tcs__wgrow(tcs_set* s, uint32_t n) {
    if (s->n_words + n <= s->cap_w) return;
    while (s->n_words + n > s->cap_w) s->cap_w = s->cap_w ? 2 * s->cap_w : 1024;
    s->words = (uint32_t*)realloc(s->words, 4u * (size_t)s->cap_w);
    if (!s->words) s->fail = 1;
}
static void tcs__wput(tcs_set* s, uint32_t v) { tcs__wgrow(s, 1); if (!s->fail) s->words[s->n_words++] = v; }
static void tcs__pad4(tcs_set* s) { while (s->n_words % 4) tcs__wput(s, 0); }
static uint32_t tcs__f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

/* A set with G glyph table entries, every one absent. */
static void tcs_init(tcs_set* s, uint32_t G) {
    uint32_t i;
    memset(s, 0, sizeof *s);
    tcs__wput(s, 0x43595350u); tcs__wput(s, 1u); tcs__wput(s, G);
    for (i = 3; i < 8; i++) tcs__wput(s, 0);
    for (i = 0; i < G; i++) tcs__wput(s, 0);
    tcs__pad4(s);
}

static void tcs_free(tcs_set* s) { free(s->texels); free(s->words); memset(s, 0, sizeof *s); }

/* sorting a band's references by key, with the texels the keys come from */
static const float* tcs__sort_t;
static int tcs__sort_axis, tcs__sort_bwd;
static float tcs__key(uint32_t r) {
    const float* t = tcs__sort_t + 4 * (size_t)r;
    float a = t[tcs__sort_axis], b = t[2 + tcs__sort_axis], c = t[4 + tcs__sort_axis];
    if (tcs__sort_bwd) return a < b ? (a < c ? a : c) : (b < c ? b : c);
    return a > b ? (a > c ? a : c) : (b > c ? b : c);
}
static int tcs__cmp(const void* pa, const void* pb) {
    uint32_t a = *(const uint32_t*)pa, b = *(const uint32_t*)pb;
    float ka = tcs__key(a), kb = tcs__key(b);
    if (ka != kb) return tcs__sort_bwd ? (ka < kb ? -1 : 1) : (ka > kb ? -1 : 1);
    return a < b ? -1 : (a > b ? 1 : 0);
}

/* Glyph g from nc contours with nh x nv bands (0 = sqrt of the curve count,
 * 1 to 16); flags: 1 even-odd, 2 backward lists. Returns 0 on success. */
static int tcs_glyph(tcs_set* s, uint32_t g, const tcs_contour* c, int nc, int nh, int nv, uint32_t flags) {
    uint32_t first = s->n_texels, ncurves = 0, k, nb, o, rec;
    uint32_t* refs;
    float bb[4] = { 0, 0, 0, 0 };
    int i, j, any = 0;
    if (g >= s->words[2]) return -1;
    for (i = 0; i < nc; i++) {
        int n = c[i].n;
        if (n < 2 || n % 2) return -1;
        tcs__tgrow(s, (uint32_t)n / 2 + 1);
        if (s->fail) return -1;
        for (j = 0; j < n / 2; j++) {
            float* t = s->texels + 4 * (size_t)s->n_texels++;
            t[0] = (float)c[i].pts[4 * j];     t[1] = (float)c[i].pts[4 * j + 1];
            t[2] = (float)c[i].pts[4 * j + 2]; t[3] = (float)c[i].pts[4 * j + 3];
        }
        {   /* the contour's end: the first point again, bit for bit */
            float* t = s->texels + 4 * (size_t)s->n_texels++;
            t[0] = (float)c[i].pts[0]; t[1] = (float)c[i].pts[1]; t[2] = 0.0f; t[3] = 0.0f;
        }
        ncurves += (uint32_t)n / 2;
    }
    /* the bbox: the control points' hull */
    for (k = first; k < s->n_texels; k++) {
        const float* t = s->texels + 4 * (size_t)k;
        int m;
        for (m = 0; m < 2; m++) {
            float x = t[2 * m], y = t[2 * m + 1];
            if (m == 1) {   /* zw of a contour's end texel is not a point */
                int end = 0, ci;
                uint32_t at = first;
                for (ci = 0; ci < nc; ci++) { at += (uint32_t)c[ci].n / 2; if (k == at) { end = 1; break; } at++; }
                if (end) continue;
            }
            if (!any) { bb[0] = bb[2] = x; bb[1] = bb[3] = y; any = 1; }
            if (x < bb[0]) bb[0] = x;
            if (x > bb[2]) bb[2] = x;
            if (y < bb[1]) bb[1] = y;
            if (y > bb[3]) bb[3] = y;
        }
    }
    if (ncurves == 0 || !(bb[2] > bb[0] && bb[3] > bb[1])) {   /* empty */
        tcs__pad4(s);
        rec = s->n_words;
        for (k = 0; k < 8; k++) tcs__wput(s, 0);
        s->words[8 + g] = rec;
        return s->fail ? -1 : 0;
    }
    if (nh <= 0 || nv <= 0) {
        int q = (int)floor(sqrt((double)ncurves) + 0.5);
        if (q < 1) q = 1;
        if (q > 16) q = 16;
        if (nh <= 0) nh = q;
        if (nv <= 0) nv = q;
    }
    /* the curve starts: every texel of a contour but its end */
    refs = (uint32_t*)malloc(4u * (size_t)ncurves);
    if (!refs) return -1;
    {
        uint32_t at = first, m = 0;
        int ci, jj;
        for (ci = 0; ci < nc; ci++) {
            for (jj = 0; jj < c[ci].n / 2; jj++) refs[m++] = at++;
            at++;
        }
    }
    tcs__pad4(s);
    rec = s->n_words;
    tcs__wput(s, tcs__f2u(bb[0])); tcs__wput(s, tcs__f2u(bb[1])); tcs__wput(s, tcs__f2u(bb[2])); tcs__wput(s, tcs__f2u(bb[3]));
    tcs__wput(s, (uint32_t)nh | ((uint32_t)nv << 16)); tcs__wput(s, flags);
    tcs__wput(s, first); tcs__wput(s, s->n_texels - first);
    nb = (uint32_t)(nh + nv);
    o = s->n_words;
    for (k = 0; k < 4 * nb; k++) tcs__wput(s, 0);
    for (k = 0; k < nb; k++) {
        int axis = k < (uint32_t)nh ? 1 : 0;   /* horizontal bands split y */
        int count = k < (uint32_t)nh ? nh : nv;
        int kk = k < (uint32_t)nh ? (int)k : (int)k - nh;
        double lo0 = axis ? bb[1] : bb[0], hi0 = axis ? bb[3] : bb[2];
        double h = (hi0 - lo0) / count, lo = lo0 + kk * h - h / 256, hi = lo0 + (kk + 1) * h + h / 256;
        uint32_t m, nl = 0, half;
        uint32_t* lst = (uint32_t*)malloc(4u * (size_t)ncurves);
        if (!lst) { free(refs); return -1; }
        for (m = 0; m < ncurves; m++) {
            const float* t = s->texels + 4 * (size_t)refs[m];
            double a = t[axis], b = t[2 + axis], cc = t[4 + axis];
            double mn = a < b ? (a < cc ? a : cc) : (b < cc ? b : cc), mx = a > b ? (a > cc ? a : cc) : (b > cc ? b : cc);
            if (mx >= lo && mn <= hi) lst[nl++] = refs[m];
        }
        for (half = 0; half < ((flags & 2u) ? 2u : 1u); half++) {
            tcs__sort_t = s->texels; tcs__sort_axis = 1 - axis; tcs__sort_bwd = (int)half;
            qsort(lst, nl, 4, tcs__cmp);
            s->words[o + 4 * k + 2 * half] = s->n_words;
            s->words[o + 4 * k + 2 * half + 1] = nl;
            for (m = 0; m < nl; m++) tcs__wput(s, lst[m]);
            if (s->fail) break;
        }
        free(lst);
    }
    free(refs);
    tcs__pad4(s);
    s->words[8 + g] = rec;
    return s->fail ? -1 : 0;
}

#endif /* PSY_GFX_CSET_BUILD_H */
