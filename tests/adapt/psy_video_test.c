/* psy_video_test.c - self-checking test for psy_video.h's core: the due-frame
 * rule against an exact model, the cadence of every rate on 60 and 30 Hz,
 * onset noise (and the never-early control that fails under it), slips
 * against a model of the clock and the grid, display drops, decode stalls
 * and the catch-up seek, seeks across GOPs, loop, end, manual mode, pause
 * and resume, the timeline's annotations against the frames shown, the
 * follow clock, the records field by field, the canonical check at open,
 * the frame sequence container in every format (raw and QOI, file, memory
 * and reader), the QOI codec, the YUV conversion against a double
 * reference, XXH64 against its published vectors, replay, and the heap.
 * No framework: it returns 0 when every check passed and 1 after printing
 * each failure.
 *
 * No GPU, no display, no pl_mpeg: a scripted decoder (the Video decoder
 * extension) and scripted display frames on a virtual clock, and psy_gfx.h
 * on the simulated display with a recording backend, so every upload is
 * checked byte for byte. One case runs the decode thread for real.
 *
 *     gcc -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Werror -O2 -I. \
 *         -o video_test tests/adapt/psy_video_test.c -lm -pthread -ldl && ./video_test
 *     cl /nologo /W4 /WX /I. tests\adapt\psy_video_test.c
 *
 * With PSYVID_TEST_QOI_REF and qoi.h (phoboslab/qoi ffb2d2c) on the include
 * path, the encoder's bytes are compared with the reference encoder's.
 */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef PSYSCR_NO_SDL
#define PSYSCR_NO_SDL
#endif
#ifndef PSYVID_NO_PL_MPEG
#define PSYVID_NO_PL_MPEG
#endif
static int g_virtual = 1;
static long long g_vt = 1000000000000LL;
static long long vnow(void);
#define PSYVID__NOW() ((int64_t)vnow())

#define PSY_VIDEO_IMPLEMENTATION
#include "psy_video.h"

#ifdef PSYVID_TEST_QOI_REF
    #if defined(_MSC_VER)
        #pragma warning(push)
        #pragma warning(disable: 4244 4267)
    #endif
    #define QOI_IMPLEMENTATION
    #include "qoi.h"
    #if defined(_MSC_VER)
        #pragma warning(pop)
    #endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static long long vnow(void) { return g_virtual ? g_vt : (long long)psyrt_now_ns(); }

/* ---------------------------------------------------------------- harness */

static int g_failures = 0;
static const char* g_case = "";
static void fail(int line, const char* what) {
    fprintf(stderr, "psy_video_test [%s]: FAIL at line %d: %s\n", g_case, line, what);
    g_failures++;
}
static void fail_i(int line, const char* what, long long got, long long want) {
    fprintf(stderr, "psy_video_test [%s]: FAIL at line %d: %s (got %lld, want %lld)\n", g_case, line, what, got, want);
    g_failures++;
}
#define CHECK(c) do { if (!(c)) fail(__LINE__, #c); } while (0)
#define CHECK_I(got, want) do { long long g_ = (long long)(got), w_ = (long long)(want); \
    if (g_ != w_) fail_i(__LINE__, #got " == " #want, g_, w_); } while (0)

static uint64_t g_rng = 88172645463325252ull;
static uint64_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }
static double urnd(void) { return (double)(rnd() >> 11) / 9007199254740992.0; }

/* ------------------------------------------------------- scripted decoder */

#define FW 4
#define FH 2
#define FB (FW * FH * 4)

typedef struct sdec {
    int32_t  num, den, gop, format;
    int64_t  frames;          /* the stream's length                           */
    int      report_frames;   /* the stream reports its length                 */
    int      random_access;
    int      timescale;       /* 0: out.index; else pts in these units         */
    uint8_t  matrix, range, transfer, primaries, siting;
    int32_t  w, h;
    int64_t  pos;
    int      fail_open;
    /* faults */
    int64_t  stall_frame;     /* this frame is not decodable until ...         */
    int64_t  stall_until;     /* ... this virtual time                         */
    int64_t  bad_ts_frame;    /* this frame reports the next frame's time      */
    int64_t  bad_hash_frame;
    int64_t  end_early;       /* the stream ends here (< frames)               */
    int64_t  fail_frame;
    /* counts */
    int64_t  seeks, discards, decodes, last_key;
} sdec;

static sdec g_sd;

static void sd_fill(int64_t i, uint8_t* p) {
    int k;
    for (k = 0; k < 8; k++) p[k] = (uint8_t)((uint64_t)i >> (8 * k));
    for (k = 8; k < FB; k++) p[k] = (uint8_t)(i * 7 + k);
}

static int sd_open(void* ctx, const psyvid_decoder_open* in, psyvid_stream* out, char* err, size_t cap) {
    sdec* s = (sdec*)ctx;
    (void)in;
    if (s->fail_open) { snprintf(err, cap, "scripted failure"); return PSYVID_ERR_DECODER; }
    out->w = s->w; out->h = s->h; out->format = s->format;
    out->fps_num = s->num; out->fps_den = s->den;
    out->frames = s->report_frames ? s->frames : -1;
    out->gop = s->gop; out->codec = PSYVID_CODEC_CUSTOM;
    out->matrix = s->matrix; out->range = s->range; out->transfer = s->transfer;
    out->primaries = s->primaries; out->siting = s->siting;
    out->timescale = s->timescale;
    out->caps = PSYVID_DEC_CPU | (s->random_access ? PSYVID_DEC_RANDOM_ACCESS : 0u);
    s->pos = 0;
    return PSYVID_OK;
}

static int sd_next(void* ctx, psyvid_planes* dst, psyvid_out* out) {
    sdec* s = (sdec*)ctx;
    int64_t i = s->pos;
    if (i >= s->frames || (s->end_early > 0 && i >= s->end_early)) return PSYVID_ENDED;
    if (i == s->stall_frame && vnow() < s->stall_until) return PSYVID_PENDING;
    if (i == s->fail_frame) return PSYVID_ERR_DECODER;
    s->pos++;
    if (!dst) { s->discards++; return PSYVID_OK; }
    s->decodes++;
    sd_fill(i, dst->data[0]);
    {
        int64_t ti = i == s->bad_ts_frame ? i + 1 : i;
        if (s->timescale > 0) {
            out->index = -1;
            /* the container's truncated time, as Media Foundation gives */
            out->pts = ti * s->den * s->timescale / s->num;
        } else {
            out->index = ti;
        }
    }
    out->hash = psyvid_xxh64(dst->data[0], FB, 0) ^ (i == s->bad_hash_frame ? 1u : 0u);
    out->flags = PSYVID_OUT_HAS_HASH | (i % s->gop == 0 ? PSYVID_OUT_KEYFRAME : 0u);
    return PSYVID_OK;
}

static int sd_seek(void* ctx, int64_t key, int64_t key_pts) {
    sdec* s = (sdec*)ctx;
    (void)key_pts;
    if (!s->random_access && key % s->gop != 0) return PSYVID_ERR_ARG;
    s->pos = key;
    s->seeks++;
    s->last_key = key;
    return PSYVID_OK;
}

static void sd_close(void* ctx) { (void)ctx; }

static const psyvid_decoder g_dec = { PSYVID_DECODER_VERSION, "scripted", sd_open, sd_next, sd_seek, sd_close, NULL };

static void sd_default(sdec* s, int32_t num, int32_t den, int64_t frames, int gop) {
    memset(s, 0, sizeof *s);
    s->num = num; s->den = den; s->frames = frames; s->gop = gop;
    s->format = PSYVID_FMT_RGBA8; s->w = FW; s->h = FH;
    s->report_frames = 1;
    s->random_access = gop == 1;
    s->stall_frame = -1; s->bad_ts_frame = -1; s->bad_hash_frame = -1; s->fail_frame = -1;
}

/* ------------------------------------------------ gfx with a recording backend */

static psyscr_screen g_scr;
static psygfx_gfx g_gfx;
static psygfx_backend g_rec;
static psygfx__null g_null_ctx;
static int64_t g_up_n;
static int64_t g_up_frame;         /* the index coded in the last RGBA8 upload */
static uint64_t g_up_hash;
static size_t g_up_bytes;          /* w x h x bpp of the texture, set by the case */
static uint8_t g_up_copy[64 * 64 * 16];

static int rec_update(void* c, uint32_t id, int x, int y, int w, int h, const void* data, size_t stride) {
    const uint8_t* p = (const uint8_t*)data;
    int k;
    (void)c; (void)id; (void)x; (void)y; (void)w; (void)h; (void)stride;
    g_up_n++;
    g_up_frame = 0;
    for (k = 0; k < 8; k++) g_up_frame = (int64_t)((uint64_t)g_up_frame | (uint64_t)p[k] << (8 * k));
    g_up_hash = psyvid_xxh64(p, g_up_bytes, 0);
    if (g_up_bytes <= sizeof g_up_copy) memcpy(g_up_copy, p, g_up_bytes);
    return PSYGFX_OK;
}

static void gfx_open(void) {
    psyscr_desc d;
    psygfx_desc gd;
    memset(&d, 0, sizeof d);
    d.backend = PSYSCR_BACKEND_SIM;
    if (!psyscr_open(&g_scr, &d)) { fprintf(stderr, "sim screen: %s\n", psyscr_error(&g_scr)); exit(1); }
    g_rec = psygfx__null_backend;
    g_rec.texture_update = rec_update;
    memset(&gd, 0, sizeof gd);
    gd.screen = &g_scr;
    gd.backend = &g_rec;
    gd.backend_ctx = &g_null_ctx;
    gd.width = 64; gd.height = 64;
    if (!psygfx_open(&g_gfx, &gd)) { fprintf(stderr, "gfx: %s\n", psygfx_error(&g_gfx)); exit(1); }
}

/* ------------------------------------------------------------ records */

static unsigned char g_ring_mem[PSYRT_RING_BYTES(65536)];
static psyrt_ring g_ring;

#define MAXD 120000
typedef struct frec {
    int64_t t, frame, due, mt;
    uint32_t flags, shows;
    int dec, why, tier, have;
} frec;
static frec g_fr[MAXD];
typedef struct drec { int64_t t, first, count; int why; int64_t display; } drec;
static drec g_dr[20000];
static int g_ndr;
static psyrt_event g_oth[4096];
static int g_noth;

static void rec_reset(void) {
    psyrt_ring_desc rd;
    memset(g_fr, 0, sizeof g_fr);
    g_ndr = 0;
    g_noth = 0;
    rd.memory = g_ring_mem;
    rd.bytes = sizeof g_ring_mem;
    psyrt_ring_open(&g_ring, &rd);
}

static void drain(void) {
    static psyrt_event ev[2048];
    int n, i;
    while ((n = psyrt_ring_drain(&g_ring, ev, 2048)) > 0) {
        for (i = 0; i < n; i++) {
            const psyrt_event* e = &ev[i];
            if (e->source != PSYRT_SRC_VIDEO) continue;
            if (e->kind == PSYVID_EV_FRAME) {
                uint32_t d = e->u.u32[9];
                if (d < MAXD) {
                    frec* r = &g_fr[d];
                    r->t = (int64_t)e->t_ns; r->frame = e->u.i64[0]; r->due = e->u.i64[1]; r->mt = e->u.i64[2];
                    r->flags = e->u.u32[6]; r->shows = e->u.u32[8];
                    r->dec = (int)PSYVID_EV_DECISION_OF(e->u.u16[14]);
                    r->why = (int)PSYVID_EV_WHY_OF(e->u.u16[14]);
                    r->tier = (int)PSYVID_EV_TIER_OF(e->u.u16[14]);
                    r->have++;
                }
            } else if (e->kind == PSYVID_EV_DROP) {
                if (g_ndr < 20000) {
                    drec* r = &g_dr[g_ndr++];
                    r->t = (int64_t)e->t_ns; r->first = e->u.i64[0]; r->count = e->u.i64[1];
                    r->why = e->u.u16[14]; r->display = e->u.u32[9];
                }
            } else if (g_noth < 4096) {
                g_oth[g_noth++] = *e;
            }
        }
    }
}

static int count_other(uint16_t kind) {
    int i, n = 0;
    for (i = 0; i < g_noth; i++) n += g_oth[i].kind == kind;
    return n;
}
static const psyrt_event* find_other(uint16_t kind, int nth) {
    int i;
    for (i = 0; i < g_noth; i++) if (g_oth[i].kind == kind && nth-- == 0) return &g_oth[i];
    return NULL;
}

/* ------------------------------------------------------------ display */

typedef struct disp {
    int64_t  T0;
    double   P;               /* true period, ns                               */
    int64_t  P_report;        /* f.period                                      */
    double   noise_ns;        /* uniform +-                                    */
    int64_t  k, vb;
    psyscr_record cur;        /* the flip of the frame being drawn             */
    psyscr_record prev;       /* the previous flip's record, completed          */
    int      has_prev;
    int      late_next;       /* this frame's flip shows this many vblanks late */
    int      not_shown;       /* this frame's flip is skipped                  */
    int      tier;
    uint16_t flags;           /* for this frame's flip record                  */
    int      started;
    int      hold;            /* this frame's flip record arrives a frame late */
    psyscr_record held;
    int      has_held;
    psyscr_record done[2];    /* f.done: every record completed since the last */
    int      n_done;
} disp;

static int64_t disp_t(const disp* d, int64_t vb) { return d->T0 + (int64_t)floor((double)vb * d->P + 0.5); }

static void disp_init(disp* d, double hz, double ppm) {
    memset(d, 0, sizeof *d);
    d->T0 = 2000000000000LL;
    d->P = 1e9 / hz / (1.0 + ppm * 1e-6);
    d->P_report = (int64_t)floor(d->P + 0.5);
    d->tier = PSYSCR_TIER_SIM;
}

/* The next display frame. The previous flip's record arrives with it, as
 * psyscr_begin() delivers it; late_next, not_shown and flags set before the
 * call describe the flip of the frame drawn before. */
static void disp_next(disp* d, psyscr_frame* f) {
    int64_t onset;
    if (d->started) {
        int late = d->late_next;
        d->prev = d->cur;
        d->prev.dropped = (uint32_t)late;
        d->prev.onset = d->not_shown ? 0 : disp_t(d, d->vb + late);
        d->prev.planned = disp_t(d, d->vb);
        d->prev.tier = (uint8_t)d->tier;
        d->prev.flags = (uint16_t)(d->flags | (d->not_shown ? PSYSCR_FLIP_SKIPPED : 0));
        d->late_next = 0;
        d->not_shown = 0;
        d->flags = 0;
        d->has_prev = 1;
        d->vb += 1 + late;
        d->k++;
    }
    {
        /* f.done: a held record first, then the previous frame's, unless
         * that one is held in turn */
        int nd = 0, hold = d->hold;
        d->hold = 0;
        if (d->has_held) { d->done[nd++] = d->held; d->has_held = 0; }
        if (d->has_prev && hold) { d->held = d->prev; d->has_held = 1; }
        else if (d->has_prev) d->done[nd++] = d->prev;
        d->n_done = nd;
    }
    d->started = 1;
    onset = disp_t(d, d->vb);
    if (d->noise_ns > 0) onset += (int64_t)floor((urnd() * 2.0 - 1.0) * d->noise_ns + 0.5);
    memset(f, 0, sizeof *f);
    f->onset = onset;
    f->period = d->P_report;
    f->index = d->k;
    f->vblank = d->vb;
    f->last = d->n_done > 0 ? &d->done[d->n_done - 1] : NULL;
    f->done = d->n_done > 0 ? d->done : NULL;
    f->n_done = d->n_done;
    memset(&d->cur, 0, sizeof d->cur);
    d->cur.index = d->k;
    d->cur.target = onset;
    g_vt = onset - d->P_report / 2;
}

/* ------------------------------------------------------------ movie */

static psyvid_movie g_mv;

static void desc_default(psyvid_desc* vd) {
    memset(vd, 0, sizeof *vd);
    vd->decoder = &g_dec;
    vd->decoder_ctx = &g_sd;
    vd->inline_decode = true;
    vd->ring = &g_ring;
    vd->refresh_num = 60; vd->refresh_den = 1;
    vd->ahead = 6;
}

static int open_mv(const psyvid_desc* vd) {
    g_up_bytes = FB;
    if (!psyvid_open(&g_mv, &g_gfx, vd)) { fprintf(stderr, "psy_video_test [%s]: open: %s\n", g_case, psyvid_error(&g_mv)); return 0; }
    return 1;
}

/* Runs n display frames; returns the last update's code. */
static psytl_event g_tl_ev[8192];
static psytl_timeline g_tl;
static psytl_timeline* g_eval_tl;   /* run() and settle() evaluate it */
static void tl_open(void);

static void eval_tl(const psyscr_frame* f) {
    psytl_frame tf;
    if (!g_eval_tl) return;
    tf.onset = f->onset; tf.period = f->period; tf.index = f->index;
    psytl_evaluate(g_eval_tl, &tf, NULL, 0);
}

static int run(disp* d, int64_t n, int* codes_bad) {
    psyscr_frame f;
    int64_t i;
    int rc = PSYVID_OK;
    for (i = 0; i < n; i++) {
        disp_next(d, &f);
        rc = psyvid_update(&g_mv, &f);
        eval_tl(&f);
        if (rc < 0 && codes_bad) (*codes_bad)++;
        if ((i & 255) == 0) drain();
    }
    drain();
    return rc;
}

/* One more display frame, so the last frame's flip record arrives. */
static void settle(disp* d) {
    psyscr_frame f;
    disp_next(d, &f);
    psyvid_update(&g_mv, &f);
    eval_tl(&f);
    drain();
}

/* An independent model of the due frame: exact rationals in long double
 * would round, so in integers with the definition itself: the largest i
 * with ceil(i den 1e9 / num) <= x. */
static int64_t model_due(int32_t num, int32_t den, int64_t x) {
    int64_t i;
    if (x < 0) return -1;
    i = (int64_t)floor((double)x * num / ((double)den * 1e9));
    while (i > 0 && psyvid_frame_time(num, den, i) > x) i--;
    while (psyvid_frame_time(num, den, i + 1) <= x) i++;
    return i;
}

/* ================================================================ cases */

static void test_pure(void) {
    int64_t i;
    int k;
    g_case = "pure";
    /* frame times: the ceiling of the exact value */
    CHECK_I(psyvid_frame_time(30, 1, 1), 33333334);
    CHECK_I(psyvid_frame_time(30, 1, 3), 100000000);
    CHECK_I(psyvid_frame_time(24000, 1001, 1), 41708334);
    CHECK_I(psyvid_frame_time(24000, 1001, 24000), INT64_C(1001000000000));
    CHECK_I(psyvid_frame_time(60000, 1001, 432000), INT64_C(7207200000000));
    /* due: against the definition at and around every boundary */
    for (k = 0; k < 4; k++) {
        static const int32_t nums[4] = { 30, 24000, 25, 60000 }, dens[4] = { 1, 1001, 1, 1001 };
        for (i = 0; i < 3000; i++) {
            int64_t t = psyvid_frame_time(nums[k], dens[k], i);
            CHECK_I(psyvid_due(nums[k], dens[k], t, 0), i);
            CHECK_I(psyvid_due(nums[k], dens[k], t - 1, 0), i - 1);
            CHECK_I(psyvid_due(nums[k], dens[k], t + 8333333, 0), model_due(nums[k], dens[k], t + 8333333));
        }
        /* far out: two hours and 2^62 ns */
        for (i = 0; i < 2000; i++) {
            int64_t x = (int64_t)(urnd() * 7.2e12);
            CHECK_I(psyvid_due(nums[k], dens[k], x, 0), model_due(nums[k], dens[k], x));
        }
        CHECK(psyvid_due(nums[k], dens[k], (INT64_C(1) << 62) - 1, 0) > 0);
    }
    CHECK_I(psyvid_due(30, 1, -1, 0), -1);
    CHECK_I(psyvid_lead_ns(0.0, 16666667), 8333333);
    CHECK_I(psyvid_lead_ns(PSYVID_LEAD_NONE, 16666667), 0);
    CHECK_I(psyvid_lead_ns(0.25, 16666667), 4166666);
    CHECK_I(psyvid_lead_ns(0.5, 0), 0);
    /* the lead rule is psy_timeline.h's own: the same truncation */
    CHECK_I(psyvid_lead_ns(0.5, 16666667), (int64_t)(0.5 * (double)16666667));
    /* XXH64: python-xxhash 3 vectors */
    {
        uint8_t b[768];
        for (k = 0; k < 768; k++) b[k] = (uint8_t)(k & 255);
        CHECK(psyvid_xxh64("", 0, 0) == UINT64_C(0xef46db3751d8e999));
        CHECK(psyvid_xxh64("a", 1, 0) == UINT64_C(0xd24ec4f1a98c6e5b));
        CHECK(psyvid_xxh64("abc", 3, 0) == UINT64_C(0x44bc2cf5ad770999));
        CHECK(psyvid_xxh64(b, 100, 0) == UINT64_C(0x6ac1e58032166597));
        CHECK(psyvid_xxh64(b, 768, 0) == UINT64_C(0x8e03c838c596036f));
        CHECK(psyvid_xxh64("", 0, 12345) == UINT64_C(0x95584af7701f808d));
        CHECK(psyvid_xxh64(b, 768, 12345) == UINT64_C(0x6ecac135863f12b2));
        /* streaming in uneven pieces gives the same */
        {
            psyvid__xxh s;
            psyvid__xxh_init(&s, 0);
            psyvid__xxh_update(&s, b, 1); psyvid__xxh_update(&s, b + 1, 30); psyvid__xxh_update(&s, b + 31, 2);
            psyvid__xxh_update(&s, b + 33, 700); psyvid__xxh_update(&s, b + 733, 35);
            CHECK(psyvid__xxh_digest(&s) == UINT64_C(0x8e03c838c596036f));
        }
    }
}

static void test_qoi(void) {
    static uint8_t img[61 * 37 * 4], back[61 * 37 * 4], enc[61 * 37 * 5 + 64];
    int t, i;
    g_case = "qoi";
    for (t = 0; t < 7; t++) {
        int64_t n;
        int w = t == 0 ? 1 : 61, h = t == 0 ? 1 : 37;
        if (t == 6) {
            /* steps around every DIFF and LUMA bound: g by -34..33, r and b
             * within -9..8 of g's step */
            int g = 128, r = 128, b = 128;
            for (i = 0; i < w * h; i++) {
                int dg = (int)(rnd() % 68) - 34;
                g = (g + dg) & 255;
                r = (r + dg + (int)(rnd() % 18) - 9) & 255;
                b = (b + dg + (int)(rnd() % 18) - 9) & 255;
                img[4 * i] = (uint8_t)r; img[4 * i + 1] = (uint8_t)g; img[4 * i + 2] = (uint8_t)b; img[4 * i + 3] = 255;
            }
        } else
        for (i = 0; i < w * h * 4; i++) {
            switch (t) {
            case 0: case 1: img[i] = (uint8_t)rnd(); break;                       /* noise: RGBA */
            case 2: img[i] = (uint8_t)((i / 4) % 7 == 0 ? 200 : 10); break;       /* runs */
            case 3: img[i] = (uint8_t)(i / 4 + (i & 3)); break;                   /* small steps: DIFF, LUMA */
            case 4: img[i] = (uint8_t)((i & 3) == 3 ? 255 : ((i / 4) * 37) & 255); break;
            default: img[i] = (uint8_t)((i / 4) % 64 < 3 ? 0 : (i & 3) == 3 ? 255 : (i / 256) * 11); break;
            }
        }
        n = psyvid_qoi_encode(img, w, h, 4, enc, sizeof enc);
        CHECK(n > 22);
        CHECK_I(psyvid_qoi_decode(enc, (size_t)n, back, w, h), PSYVID_OK);
        CHECK(memcmp(img, back, (size_t)(w * h * 4)) == 0);
#ifdef PSYVID_TEST_QOI_REF
        {
            qoi_desc qd;
            int rn = 0;
            void* ref;
            qd.width = (unsigned)w; qd.height = (unsigned)h; qd.channels = 4; qd.colorspace = 1;
            ref = qoi_encode(img, &qd, &rn);
            CHECK(ref != NULL);
            CHECK_I(rn, n);
            CHECK(ref && rn == n && memcmp(ref, enc, (size_t)n) == 0);
            free(ref);
        }
#endif
        /* every truncation fails cleanly, never overruns */
        for (i = 0; i < n - 8; i += 1 + (int)(n / 50)) CHECK(psyvid_qoi_decode(enc, (size_t)i, back, w, h) < 0);
        CHECK(psyvid_qoi_decode(enc, (size_t)n, back, w + 1, h) < 0);
    }
#ifdef PSYVID_TEST_QOI_REF
    printf("psy_video_test: QOI bytes compared with the reference qoi.h\n");
#endif
}

/* The conversion against the same chroma weights and matrix in double. */
static void test_yuv(void) {
    static uint8_t planes[37 * 23 * 2], rgba[37 * 23 * 4];
    static int16_t rows[256];
    int fm, mx, rg, st, ch, worst = 0;
    g_case = "yuv";
    CHECK(sizeof rows >= psyvid_yuv_rows_bytes(37));
    for (fm = 0; fm < 2; fm++)
    for (mx = PSYVID_MATRIX_BT601; mx <= PSYVID_MATRIX_BT2020; mx++)
    for (rg = PSYVID_RANGE_LIMITED; rg <= PSYVID_RANGE_FULL; rg++)
    for (st = PSYVID_SITING_LEFT; st <= PSYVID_SITING_CENTER; st++)
    for (ch = 0; ch < 2; ch++) {
        int32_t format = fm ? PSYVID_FMT_NV12 : PSYVID_FMT_I420;
        int32_t w = 37, h = 23, cw = 19, chh = 12, x, y, i;
        psyvid_planes p;
        double kr = mx == PSYVID_MATRIX_BT601 ? 0.299 : mx == PSYVID_MATRIX_BT709 ? 0.2126 : 0.2627;
        double kb = mx == PSYVID_MATRIX_BT601 ? 0.114 : mx == PSYVID_MATRIX_BT709 ? 0.0722 : 0.0593;
        psyvid__planes_layout(format, w, h, planes, &p);
        for (i = 0; i < (int)sizeof planes; i++) planes[i] = (uint8_t)rnd();
        CHECK_I(psyvid_yuv_to_rgba(&p, format, w, h, mx, rg, st, ch, rgba, w * 4, rows), PSYVID_OK);
        for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
            double Y = planes[y * w + x], U, V, yp, up, vp, rgb[3];
            int c;
            /* chroma at this pixel */
            {
                double su = 0, sv = 0;
                int dy, dx;
                for (dy = 0; dy < 2; dy++) for (dx = 0; dx < 3; dx++) {
                    double wy, wx;
                    int cy, cx;
                    if (ch == PSYVID_CHROMA_NEAREST) { wy = dy == 0; wx = dx == 0; cy = y / 2; cx = x / 2; }
                    else {
                        cy = dy == 0 ? y / 2 : ((y & 1) ? y / 2 + 1 : y / 2 - 1);
                        wy = dy == 0 ? 0.75 : 0.25;
                        if (st == PSYVID_SITING_LEFT) {
                            cx = dx == 0 ? x / 2 : x / 2 + 1;
                            wx = (x & 1) ? (dx < 2 ? 0.5 : 0) : (dx == 0 ? 1.0 : 0);
                        } else {
                            cx = dx == 0 ? x / 2 : ((x & 1) ? x / 2 + 1 : x / 2 - 1);
                            wx = dx == 0 ? 0.75 : dx == 1 ? 0.25 : 0;
                        }
                    }
                    if (cy < 0) cy = 0;
                    if (cy >= chh) cy = chh - 1;
                    if (cx < 0) cx = 0;
                    if (cx >= cw) cx = cw - 1;
                    if (wx * wy == 0) continue;
                    if (fm) { su += wx * wy * p.data[1][cy * p.stride[1] + 2 * cx]; sv += wx * wy * p.data[1][cy * p.stride[1] + 2 * cx + 1]; }
                    else { su += wx * wy * p.data[1][cy * p.stride[1] + cx]; sv += wx * wy * p.data[2][cy * p.stride[2] + cx]; }
                }
                U = su; V = sv;
            }
            if (rg == PSYVID_RANGE_LIMITED) { yp = (Y - 16) / 219; up = (U - 128) / 224; vp = (V - 128) / 224; }
            else { yp = Y / 255; up = (U - 128) / 255; vp = (V - 128) / 255; }
            rgb[0] = yp + 2 * (1 - kr) * vp;
            rgb[2] = yp + 2 * (1 - kb) * up;
            rgb[1] = (yp - kr * rgb[0] - kb * rgb[2]) / (1 - kr - kb);
            for (c = 0; c < 3; c++) {
                double v = floor(rgb[c] * 255.0 + 0.5);
                int d;
                if (v < 0) v = 0;
                if (v > 255) v = 255;
                d = abs((int)v - (int)rgba[(y * w + x) * 4 + c]);
                if (d > worst) worst = d;
            }
            CHECK(rgba[(y * w + x) * 4 + 3] == 255);
        }
    }
    CHECK(worst <= 1);
    printf("psy_video_test: YUV to RGBA8 worst difference from double: %d code\n", worst);
}

/* 30 on 60: every frame on screen exactly twice, DUE; the same under onset
 * noise of 2 and 20 us; never-early under 2 us of noise misplaces frames. */
static void test_multiple_and_noise(void) {
    static const double noise[3] = { 0, 2000, 20000 };
    int t;
    for (t = 0; t < 4; t++) {
        psyvid_desc vd;
        disp d;
        int64_t k, wrong = 0, bad_why = 0;
        g_case = t < 3 ? "30 on 60" : "30 on 60, never early";
        rec_reset();
        sd_default(&g_sd, 30, 1, 400, 1);
        desc_default(&vd);
        if (t == 3) vd.lead = PSYVID_LEAD_NONE;
        if (!open_mv(&vd)) return;
        disp_init(&d, 60.0, 0);
        d.P = 1e9 / 60.0;
        d.noise_ns = t < 3 ? noise[t] : 2000;
        CHECK(psyvid_play_at(&g_mv, PSYVID_ASAP) > 0);
        g_up_n = 0;
        run(&d, 700, NULL);
        settle(&d);
        CHECK_I(g_up_n, (int64_t)g_mv.info.shown);   /* one upload per frame shown, none per repeat */
        for (k = 0; k < 700; k++) {
            const frec* r = &g_fr[k];
            int64_t want = k / 2;
            if (!r->have) { wrong++; continue; }
            if (r->frame != want) wrong++;
            if (r->why != PSYVID_WHY_DUE) bad_why++;
            if (r->dec != (k % 2 == 0 ? PSYVID_SHOWN : PSYVID_REPEATED)) bad_why++;
        }
        if (t < 3) {
            CHECK_I(wrong, 0);
            CHECK_I(bad_why, 0);
            CHECK_I(g_ndr, 0);
            CHECK_I(g_mv.info.multiple, 2);
        } else {
            CHECK(wrong > 0);   /* the control: noise moves frames under never-early */
            printf("psy_video_test: never-early under 2 us noise: %lld of 700 display frames wrong (lead 0.5: 0)\n", (long long)wrong);
        }
        psyvid_close(&g_mv);
    }
}

/* A cadence that judders, against the exact model with the nominal grid. */
static void run_cadence(const char* name, int32_t num, int32_t den, double hz, int64_t n_disp,
                        int expect_lo, int expect_hi) {
    psyvid_desc vd;
    disp d;
    int64_t k, wrong = 0, drift = 0, cad_rep = 0, cad_drop = 0, dropped = 0;
    int64_t shows[4096];
    int64_t frames = 4000;
    char line[1024];
    g_case = name;
    rec_reset();
    sd_default(&g_sd, num, den, frames, 1);
    desc_default(&vd);
    vd.refresh_num = (int32_t)hz; vd.refresh_den = 1;
    if (!open_mv(&vd)) return;
    disp_init(&d, hz, 0.01);   /* 10 ppb: no exact tie of onset and boundary */
    CHECK(psyvid_play_at(&g_mv, PSYVID_ASAP) > 0);
    run(&d, n_disp, NULL);
    settle(&d);
    memset(shows, 0, sizeof shows);
    for (k = 0; k < n_disp; k++) {
        const frec* r = &g_fr[k];
        int64_t L = psyvid_lead_ns(0.5, d.P_report);
        int64_t m = disp_t(&d, k) - disp_t(&d, 0);
        int64_t want = model_due(num, den, m + L);
        if (!r->have || r->frame != want) wrong++;
        if (r->why == PSYVID_WHY_DRIFT) drift++;
        if (r->dec == PSYVID_REPEATED && r->why == PSYVID_WHY_CADENCE) cad_rep++;
        if (r->have && r->frame < 4096) shows[r->frame]++;
    }
    for (k = 0; k < g_ndr; k++) {
        dropped += g_dr[k].count;
        if (g_dr[k].why == PSYVID_WHY_CADENCE) cad_drop += g_dr[k].count;
    }
    CHECK_I(wrong, 0);
    CHECK_I(drift, 0);
    CHECK_I(dropped, cad_drop);
    for (k = 1; k < 4096 && shows[k + 1]; k++) {
        if (shows[k] && (shows[k] < expect_lo || shows[k] > expect_hi)) { fail_i(__LINE__, "showings outside the cadence", shows[k], expect_hi); break; }
    }
    if (hz / ((double)num / den) > 1.0001) {
        /* one CADENCE repeat per frame shown more than floor(R/r) times */
        int64_t q = (int64_t)floor(hz / ((double)num / den)), extra = 0;
        for (k = 0; k < 4096; k++) if (shows[k] > q) extra += shows[k] - q;
        CHECK_I(cad_rep, extra);
        CHECK(cad_rep > 0);
    } else if (hz / ((double)num / den) < 0.9999) {
        CHECK(cad_drop > 0);
    } else {
        CHECK_I(cad_rep + cad_drop, 0);
    }
    psyvid_describe(&g_mv, line, sizeof line);
    printf("psy_video_test: %s: %s\n", name, strstr(line, "display") ? strstr(line, "display") : line);
    psyvid_close(&g_mv);
}

static void test_cadences(void) {
    run_cadence("24000/1001 on 60", 24000, 1001, 60.0, 3000, 2, 3);
    run_cadence("25 on 60", 25, 1, 60.0, 3000, 2, 3);
    run_cadence("60 on 60", 60, 1, 60.0, 3000, 1, 1);
    run_cadence("60 on 30", 60, 1, 30.0, 1500, 1, 1);
    run_cadence("50 on 60", 50, 1, 60.0, 3000, 1, 2);
    /* the describe line states the 3:2 cadence and its durations */
    {
        psyvid_desc vd;
        char line[1024];
        g_case = "describe cadence";
        rec_reset();
        sd_default(&g_sd, 24000, 1001, 100, 1);
        desc_default(&vd);
        if (open_mv(&vd)) {
            psyvid_describe(&g_mv, line, sizeof line);
            CHECK(strstr(line, "cadence 3:2:3:2") != NULL);
            CHECK(strstr(line, "33.3 to 50.0 ms") != NULL);
            CHECK(g_mv.info.multiple == 0);
            psyvid_close(&g_mv);
        }
        /* strict: refused, with the reason */
        vd.strict_cadence = true;
        CHECK(!psyvid_open(&g_mv, &g_gfx, &vd));
        CHECK(strstr(psyvid_error(&g_mv), "not a multiple") != NULL);
        sd_default(&g_sd, 30, 1, 100, 1);
        CHECK(open_mv(&vd));
        psyvid_close(&g_mv);
    }
}

/* Slips: the display runs off its nominal rate against the psy_rt clock.
 * Each DRIFT record must sit where an exact model of the clock and the grid
 * puts a frame on screen once or three times. */
static void test_drift(void) {
    /* not 500: at 500 ppm the slips fall exactly on frame boundaries */
    static const double ppms[2] = { 487.3, -487.3 };
    int t;
    for (t = 0; t < 2; t++) {
        psyvid_desc vd;
        disp d;
        int64_t k, n = 9000, wrong = 0, drift_rec = 0, model_slips = 0, first_slip = -1;
        int64_t prev_due = -1, run_len = 0;
        g_case = t == 0 ? "drift +487.3 ppm" : "drift -487.3 ppm";
        rec_reset();
        sd_default(&g_sd, 30, 1, 20000, 1);
        desc_default(&vd);
        if (!open_mv(&vd)) return;
        disp_init(&d, 60.0, ppms[t]);
        CHECK(psyvid_play_at(&g_mv, PSYVID_ASAP) > 0);
        run(&d, n, NULL);
        settle(&d);
        for (k = 0; k < n; k++) {
            const frec* r = &g_fr[k];
            int64_t L = psyvid_lead_ns(0.5, d.P_report);
            int64_t m = disp_t(&d, k) - disp_t(&d, 0);
            int64_t want = model_due(30, 1, m + L);
            if (!r->have || r->frame != want) wrong++;
            if (r->why == PSYVID_WHY_DRIFT) drift_rec++;
            if (want != prev_due) {
                /* the frame before ran run_len display frames: a slip if not 2
                 * (the first frame's run starts at the anchor, so it counts) */
                if (prev_due >= 0 && (run_len != 2 || want != prev_due + 1)) {
                    model_slips += want - prev_due > 1 ? want - prev_due - 1 + (run_len != 2) : 1;
                    if (first_slip < 0) first_slip = k;
                }
                run_len = 1;
                prev_due = want;
            } else {
                run_len++;
            }
        }
        for (k = 0; k < g_ndr; k++) if (g_dr[k].why == PSYVID_WHY_DRIFT) drift_rec++;
        if (getenv("PSYVID_TEST_DEBUG")) {
            int shown = 0;
            for (k = 0; k < n && shown < 12; k++) if (g_fr[k].why == PSYVID_WHY_DRIFT) { printf("  drift at %lld frame %lld dec %d\n", (long long)k, (long long)g_fr[k].frame, g_fr[k].dec); shown++; }
        }
        CHECK_I(wrong, 0);
        CHECK(model_slips > 0);
        CHECK_I(drift_rec, model_slips);
        /* first slip: half a period of phase at 487.3 ppm, 1026 frames */
        CHECK(first_slip > 1000 && first_slip < 1050);
        printf("psy_video_test: %s: %lld slips in %lld display frames, first at %lld (predicted 1026)\n",
               g_case, (long long)model_slips, (long long)n, (long long)first_slip);
        psyvid_close(&g_mv);
    }
}

/* A flip that misses its vblank: DISPLAY_LATE drops, LATE on its record. */
static void test_display_late(void) {
    psyvid_desc vd;
    disp d;
    psyscr_frame f;
    int64_t k;
    int late_flags = 0, n_late_drop = 0;
    g_case = "display late";
    rec_reset();
    sd_default(&g_sd, 60, 1, 1000, 1);
    desc_default(&vd);
    if (!open_mv(&vd)) return;
    disp_init(&d, 60.0, 0);
    CHECK(psyvid_play_at(&g_mv, PSYVID_ASAP) > 0);
    for (k = 0; k < 300; k++) {
        disp_next(&d, &f);
        psyvid_update(&g_mv, &f);
        if (k == 100) d.late_next = 1;     /* flip 100 shows a vblank late */
        if (k == 200) d.late_next = 2;
    }
    settle(&d);
    drain();
    for (k = 0; k < 300; k++) if (g_fr[k].flags & PSYVID_F_LATE) late_flags++;
    for (k = 0; k < g_ndr; k++) {
        CHECK_I(g_dr[k].why, PSYVID_WHY_DISPLAY_LATE);
        n_late_drop += (int)g_dr[k].count;
    }
    CHECK_I(late_flags, 2);
    CHECK(g_fr[100].flags & PSYVID_F_LATE);
    CHECK_I(n_late_drop, 3);
    CHECK_I(g_ndr, 2);
    CHECK_I(g_fr[101].frame, g_fr[100].frame + 2);
    CHECK_I(g_fr[101].why, PSYVID_WHY_DUE);
    /* the flip's own onset is the record's time */
    CHECK_I(g_fr[100].t, disp_t(&d, 101));
    psyvid_close(&g_mv);
}

/* Decode stalls of 1, 3 and 20 frames; a GOP decoder seeks to catch up. */
static void test_decode_stall(void) {
    static const int stalls[3] = { 1, 3, 20 };
    int t, gopc;
    for (gopc = 0; gopc < 2; gopc++)
    for (t = 0; t < 3; t++) {
        psyvid_desc vd;
        disp d;
        int64_t k, late_rep = 0, late_drop = 0, dues_late = 0, shown_late = 0;
        char name[64];
        snprintf(name, sizeof name, "decode stall %d%s", stalls[t], gopc ? ", GOP 10" : "");
        g_case = name;
        rec_reset();
        sd_default(&g_sd, 60, 1, 2000, gopc ? 10 : 1);
        g_sd.stall_frame = 300;
        desc_default(&vd);
        if (!open_mv(&vd)) return;
        disp_init(&d, 60.0, 0);
        CHECK(psyvid_play_at(&g_mv, PSYVID_ASAP) > 0);
        /* the stall begins when frame 300 would be decoded and lasts until
         * stalls[t] frames after it was due */
        g_sd.stall_until = disp_t(&d, 300) + (int64_t)(stalls[t] - 1) * d.P_report + d.P_report / 4;
        run(&d, 600, NULL);
        settle(&d);
        for (k = 0; k < 600; k++) {
            const frec* r = &g_fr[k];
            if (r->why == PSYVID_WHY_DECODE_LATE && r->dec == PSYVID_REPEATED) late_rep++;
            if (r->why == PSYVID_WHY_DECODE_LATE && r->dec == PSYVID_SHOWN) shown_late++;
            /* a frame shown before its time or on time is DUE; never late as DUE */
            if (r->dec == PSYVID_SHOWN && r->why == PSYVID_WHY_DUE && r->frame != k) dues_late++;
        }
        for (k = 0; k < g_ndr; k++) {
            CHECK_I(g_dr[k].why, PSYVID_WHY_DECODE_LATE);
            late_drop += g_dr[k].count;
        }
        /* random access skips the stalled frame once it is older than due;
         * a GOP decoder waits for its keyframe, then catches up */
        CHECK_I(late_rep, gopc ? (stalls[t] < 10 ? stalls[t] : 10) : 1);
        CHECK_I(late_drop, gopc ? (stalls[t] < 10 ? stalls[t] : 10) : 1);
        CHECK_I(shown_late, 0);
        CHECK_I(dues_late, 0);
        CHECK_I(g_mv.info.drops_by[PSYVID_WHY_DECODE_LATE], late_drop);
        if (gopc && stalls[t] == 20) { CHECK_I(g_sd.seeks, 2); CHECK_I(g_sd.discards, 0); }   /* open, then 310 */
        if (gopc && stalls[t] == 3) { CHECK_I(g_sd.seeks, 1); CHECK_I(g_sd.discards, 3); }
        CHECK_I(g_fr[599].frame, 599);   /* back on schedule */
        psyvid_close(&g_mv);
    }
}

/* Seek forward and back across GOPs, resume ASAP and stay paused. */
static void test_seek(void) {
    static const int64_t targets[4] = { 133, 37, 40, 199 };
    psyvid_desc vd;
    disp d;
    psyscr_frame f;
    int s, k;
    int64_t skipped_first = -1;
    g_case = "seek";
    rec_reset();
    /* an annotation at every frame on the movie base: a seek skips the
     * ones it passes over, none fires late */
    tl_open();
    for (k = 0; k < 200; k++) {
        psytl_event e;
        memset(&e, 0, sizeof e);
        e.base = 4; e.kind = PSYTL_MARK; e.time = psyvid_frame_time(30, 1, k); e.code = k;
        psytl_add(&g_tl, &e);
    }
    g_eval_tl = &g_tl;
    sd_default(&g_sd, 30, 1, 200, 8);
    desc_default(&vd);
    vd.timeline = &g_tl;
    vd.base = 4;
    if (!open_mv(&vd)) { g_eval_tl = NULL; return; }
    disp_init(&d, 60.0, 0);
    CHECK(psyvid_play_at(&g_mv, PSYVID_ASAP) > 0);
    run(&d, 50, NULL);
    for (s = 0; s < 4; s++) {
        int64_t id = psyvid_seek_frame(&g_mv, targets[s], PSYVID_ASAP);
        int64_t before = d.k + 1, first = -1;
        const psyrt_event* ev;
        CHECK(id > 0);
        CHECK_I(psyvid_result(&g_mv, id, NULL), PSYVID_PENDING);
        run(&d, 20, NULL);
        settle(&d);
        for (k = (int)before; k < (int)before + 20; k++) if (g_fr[k].have) { first = k; break; }
        CHECK(first >= 0);
        if (first >= 0) {
            CHECK_I(g_fr[first].frame, targets[s]);
            CHECK_I(g_fr[first].why, PSYVID_WHY_SEEK);
            CHECK_I(g_fr[first].dec, PSYVID_SHOWN);
        }
        CHECK_I(g_up_frame >= targets[s], 1);
        ev = find_other((uint16_t)PSYVID_EV_SEEK, s);
        CHECK(ev != NULL);
        if (ev) {
            CHECK_I(ev->u.i64[0], targets[s]);
            CHECK_I(ev->u.i64[1], targets[s] - targets[s] % 8);
            CHECK_I(ev->u.i64[2], targets[s] % 8);
            CHECK_I(ev->u.u32[8], id);
            if (first >= 0) CHECK_I((int64_t)ev->t_ns, disp_t(&d, first));
            if (s == 0) {
                /* forward from about frame 25: every annotation before 133
                 * not yet fired is skipped, and the record says how many */
                int ne = 0, i, n_sk = 0;
                const psytl_event* te = psytl_events(&g_tl, 4, &ne);
                for (i = 0; i < ne; i++) n_sk += (te[i].flags & PSYTL_EV_SKIPPED) != 0;
                skipped_first = ev->u.u32[9];
                CHECK_I(ev->u.u32[9], n_sk);
                CHECK(n_sk > 90 && n_sk < 120);
                printf("psy_video_test: a seek to frame 133 skipped %d annotations; none fired late\n", n_sk);
            }
        }
        CHECK_I(psyvid_result(&g_mv, id, NULL), PSYVID_OK);
    }
    /* the seek to the last frame then ends */
    CHECK_I(psyvid_update(&g_mv, (disp_next(&d, &f), &f)), PSYVID_ENDED);
    /* stay paused: the target is shown and held; play resumes from it */
    {
        int64_t id = psyvid_seek_frame(&g_mv, 100, PSYVID_STAY_PAUSED);
        CHECK(id > 0);
        run(&d, 10, NULL);
        CHECK_I(g_up_frame, 100);
        CHECK_I(g_mv.state, PSYVID__PAUSED);
        k = (int)d.k;
        CHECK(psyvid_play_at(&g_mv, PSYVID_ASAP) > 0);
        run(&d, 10, NULL);
        settle(&d);
        {
            int j, first = -1;
            for (j = k + 1; j < k + 11; j++) if (g_fr[j].have) { first = j; break; }
            CHECK(first >= 0);
            if (first >= 0) { CHECK_I(g_fr[first].frame, 100); CHECK_I(g_fr[first + 2].frame, 101); }
        }
    }
    CHECK_I(g_mv.info.ts_mismatch, 0);
    /* no annotation fired late: each within a period of its time */
    {
        int ne = 0, i, late = 0;
        const psytl_event* te = psytl_events(&g_tl, 4, &ne);
        for (i = 0; i < ne; i++)
            if ((te[i].flags & PSYTL_EV_FIRED) && (te[i].residual >= d.P_report || te[i].residual < -d.P_report)) late++;
        CHECK_I(late, 0);
        CHECK((int64_t)g_mv.info.skipped >= skipped_first);
    }
    g_eval_tl = NULL;
    psyvid_close(&g_mv);
}

/* Loop with a timeline: frame 0 follows N-1, LOOP records, and the movie
 * base rewinds so an annotation at frame 0 fires again each cycle. */

static void tl_open(void) {
    psytl_desc td;
    memset(&td, 0, sizeof td);
    td.events = g_tl_ev;
    td.event_capacity = 8192;
    if (!psytl_open(&g_tl, &td)) { fprintf(stderr, "timeline: %s\n", psytl_error(&g_tl)); exit(1); }
}

static void test_loop_end(void) {
    psyvid_desc vd;
    disp d;
    psyscr_frame f;
    int64_t k;
    int fired0 = 0, loops = 0, rc = 0;
    psytl_event e;
    g_case = "loop";
    rec_reset();
    tl_open();
    memset(&e, 0, sizeof e);
    e.base = 1; e.kind = PSYTL_MARK; e.time = 0; e.code = 1000;
    psytl_add(&g_tl, &e);
    sd_default(&g_sd, 30, 1, 10, 1);
    desc_default(&vd);
    vd.loop = true;
    vd.timeline = &g_tl;
    vd.base = 1;
    if (!open_mv(&vd)) return;
    disp_init(&d, 60.0, 0);
    CHECK(psyvid_play_at(&g_mv, PSYVID_ASAP) > 0);
    for (k = 0; k < 100; k++) {
        psytl_event fired[8];
        psytl_frame tf;
        int n, i;
        disp_next(&d, &f);
        CHECK_I(psyvid_update(&g_mv, &f), PSYVID_OK);
        tf.onset = f.onset; tf.period = f.period; tf.index = f.index;
        n = psytl_evaluate(&g_tl, &tf, fired, 8);
        for (i = 0; i < n && i < 8; i++) {
            if (fired[i].code == 1000) {
                fired0++;
                /* fires on the display frame that shows frame 0 */
                CHECK_I(g_mv.last.frame, 0);
                CHECK_I(g_mv.last.decision, PSYVID_SHOWN);
            }
        }
    }
    settle(&d);
    for (k = 0; k < 100; k++) {
        CHECK_I(g_fr[k].frame, (k / 2) % 10);
        if (g_fr[k].frame == 0 && g_fr[k].dec == PSYVID_SHOWN && k > 0) { CHECK_I(g_fr[k].why, PSYVID_WHY_LOOP); loops++; }
    }
    CHECK_I(fired0, 5);
    CHECK_I(loops, 4);
    {
        int i, n = 0;
        for (i = 0; i < g_noth; i++) n += g_oth[i].kind == PSYVID_EV_LOOP && (int64_t)g_oth[i].t_ns < disp_t(&d, 100);
        CHECK_I(n, 4);
    }
    CHECK_I(g_ndr, 0);
    psyvid_close(&g_mv);

    /* end: the last frame stays, ENDED from the frame whose due is N */
    g_case = "end";
    rec_reset();
    sd_default(&g_sd, 30, 1, 10, 1);
    desc_default(&vd);
    if (!open_mv(&vd)) return;
    disp_init(&d, 60.0, 0);
    CHECK(psyvid_play_at(&g_mv, PSYVID_ASAP) > 0);
    for (k = 0; k < 30; k++) {
        disp_next(&d, &f);
        rc = psyvid_update(&g_mv, &f);
        CHECK_I(rc, k < 20 ? PSYVID_OK : PSYVID_ENDED);
    }
    settle(&d);
    CHECK_I(g_fr[19].frame, 9);
    CHECK_I(g_fr[20].have, 0);
    CHECK_I(count_other((uint16_t)PSYVID_EV_END), 1);
    CHECK_I(g_up_frame, 9);
    CHECK_I(psyvid_play_at(&g_mv, PSYVID_ASAP), PSYVID_ERR_ORDER);
    psyvid_close(&g_mv);
}

/* Manual mode: any frame on this display frame; the movie base is held at
 * its time, so its annotation fires there. */
static void test_manual(void) {
    static const int64_t seq[8] = { 5, 6, 7, 2, 2, 150, 151, 0 };
    psyvid_desc vd;
    disp d;
    psyscr_frame f;
    int s, gopc;
    for (gopc = 0; gopc < 2; gopc++) {
        g_case = gopc ? "manual, GOP 16" : "manual";
        rec_reset();
        tl_open();
        {
            int i;
            for (i = 0; i < 200; i++) {
                psytl_event e;
                memset(&e, 0, sizeof e);
                e.base = 2; e.kind = PSYTL_MARK; e.time = psyvid_frame_time(30, 1, i); e.code = i;
                psytl_add(&g_tl, &e);
            }
            {
                /* between frames 5 and 6: a step to 6 fires it */
                psytl_event e;
                memset(&e, 0, sizeof e);
                e.base = 2; e.kind = PSYTL_MARK; e.time = psyvid_frame_time(30, 1, 6) - 1000000; e.code = 1000;
                psytl_add(&g_tl, &e);
            }
        }
        sd_default(&g_sd, 30, 1, 200, gopc ? 16 : 1);
        desc_default(&vd);
        vd.timeline = &g_tl;
        vd.base = 2;
        if (!open_mv(&vd)) return;
        disp_init(&d, 60.0, 0);
        for (s = 0; s < 8; s++) {
            psytl_event fired[16];
            psytl_frame tf;
            int n, i, saw = 0;
            CHECK_I(psyvid_show(&g_mv, seq[s]), PSYVID_OK);
            disp_next(&d, &f);
            CHECK_I(psyvid_update(&g_mv, &f), PSYVID_OK);
            CHECK_I(g_up_frame, seq[s]);
            CHECK_I(g_mv.last.frame, seq[s]);
            CHECK_I(g_mv.last.why, PSYVID_WHY_MANUAL);
            tf.onset = f.onset; tf.period = f.period; tf.index = f.index;
            n = psytl_evaluate(&g_tl, &tf, fired, 16);
            /* a jump skips what it passes over: from 2 to 150 the 147
             * annotations 3 .. 149, so only 150's fires; a step fires one */
            {
                int ne = 0;
                const psytl_event* ev = psytl_events(&g_tl, 2, &ne);
                for (i = 0; i < ne; i++) if (ev[i].code == seq[s] && ev[i].frame == f.index) saw = 1;
            }
            /* skipped: 0 .. 4 at the first show; at the jump from 2 to 150
             * the 147 between and the one before 6 (pending again after the
             * step back to 2) */
            if (s == 1) { int k2, hit = 0; for (k2 = 0; k2 < n && k2 < 16; k2++) hit |= fired[k2].code == 1000; CHECK(hit); }
            if (s == 5) { CHECK_I(n, 1); CHECK_I(g_mv.info.skipped, 5 + 148); }
            if (s == 6) CHECK_I(n, 1);
            if (s != 4 && !saw) { fprintf(stderr, "  step %d frame %lld: %d fired\n", s, (long long)seq[s], n); fail(__LINE__, "annotation of the frame shown"); }
        }
        settle(&d);
        CHECK_I(g_mv.info.shown, 7);
        psyvid_close(&g_mv);
    }
}

/* Pause and resume: no records while paused, the movie time continues. */
static void test_pause(void) {
    psyvid_desc vd;
    disp d;
    int64_t k, id, paused_at = -1, n_have = 0;
    const psyrt_event* ev;
    g_case = "pause";
    rec_reset();
    sd_default(&g_sd, 30, 1, 1000, 1);
    desc_default(&vd);
    if (!open_mv(&vd)) return;
    disp_init(&d, 60.0, 0);
    CHECK(psyvid_play_at(&g_mv, PSYVID_ASAP) > 0);
    run(&d, 100, NULL);
    id = psyvid_pause_at(&g_mv, disp_t(&d, 110) + 3000000);   /* nearest is 110 */
    CHECK(id > 0);
    run(&d, 50, NULL);
    ev = find_other((uint16_t)PSYVID_EV_PAUSE, 0);
    CHECK(ev != NULL);
    if (ev) { paused_at = (int64_t)ev->t_ns; CHECK_I(paused_at, disp_t(&d, 110)); CHECK_I(ev->u.i64[1], id); }
    for (k = 111; k < 150; k++) n_have += g_fr[k].have;
    CHECK_I(n_have, 0);
    CHECK(psyvid_play_at(&g_mv, PSYVID_ASAP) > 0);
    run(&d, 20, NULL);
    settle(&d);
    /* display 110 showed frame 55 (its first showing); the resume shows 55's
     * second showing, then 56 */
    CHECK_I(g_fr[110].frame, 55);
    for (k = 150; k < 170; k++) if (g_fr[k].have) break;
    CHECK(k < 170);
    if (k < 170) { CHECK_I(g_fr[k].frame, 55); CHECK_I(g_fr[k + 1].frame, 56); CHECK_I(g_fr[k].dec, PSYVID_REPEATED); }
    CHECK_I(g_ndr, 0);
    psyvid_close(&g_mv);
}

/* The timeline and the video use one rule: an annotation at frame i fires
 * on the display frame that first shows frame i (or, for a frame the
 * cadence drops, the first display frame past it), under onset noise. */
static void run_agree(const char* name, int32_t num, int32_t den, double hz, double noise) {
    psyvid_desc vd;
    disp d;
    psyscr_frame f;
    int64_t k, n = 2000, frames = 3000, mism = 0, fired_n = 0, twice = 0;
    static int64_t fired_at[4000];
    static int64_t first_show[4000];
    g_case = name;
    rec_reset();
    tl_open();
    for (k = 0; k < frames; k++) {
        psytl_event e;
        memset(&e, 0, sizeof e);
        e.base = 3; e.kind = PSYTL_MARK; e.time = psyvid_frame_time(num, den, k); e.code = (int32_t)k;
        psytl_add(&g_tl, &e);
        fired_at[k] = -1;
        first_show[k] = -1;
    }
    sd_default(&g_sd, num, den, frames, 1);
    desc_default(&vd);
    vd.refresh_num = (int32_t)hz;
    vd.timeline = &g_tl;
    vd.base = 3;
    if (!open_mv(&vd)) return;
    disp_init(&d, hz, 0);
    d.noise_ns = noise;
    CHECK(psyvid_play_at(&g_mv, PSYVID_ASAP) > 0);
    for (k = 0; k < n; k++) {
        psytl_event fired[16];
        psytl_frame tf;
        int nf, i;
        disp_next(&d, &f);
        psyvid_update(&g_mv, &f);
        if (g_mv.has_last && g_mv.last.display == f.index && g_mv.last.decision == PSYVID_SHOWN && g_mv.last.frame < 4000)
            first_show[g_mv.last.frame] = f.index;
        tf.onset = f.onset; tf.period = f.period; tf.index = f.index;
        nf = psytl_evaluate(&g_tl, &tf, fired, 16);
        for (i = 0; i < nf && i < 16; i++) {
            int64_t c = fired[i].code;
            if (fired_at[c] >= 0) twice++;
            fired_at[c] = f.index;
            fired_n++;
        }
        if ((k & 255) == 0) drain();
    }
    for (k = 0; k < frames; k++) {
        int64_t want = first_show[k];
        if (fired_at[k] < 0 && want < 0) continue;
        if (want < 0) {
            /* dropped by the cadence: the first display frame showing a later one */
            int64_t j;
            for (j = k + 1; j < frames && first_show[j] < 0; j++) {}
            want = j < frames ? first_show[j] : -1;
        }
        if (fired_at[k] != want) mism++;
    }
    CHECK(fired_n > 100);
    CHECK_I(mism, 0);
    CHECK_I(twice, 0);   /* the base never rewound in play */
    psyvid_close(&g_mv);
}

static void test_timeline_agreement(void) {
    run_agree("agree 23.976 on 60, 20 us noise", 24000, 1001, 60.0, 20000);
    run_agree("agree 30 on 60, 2 us noise", 30, 1, 60.0, 2000);
    run_agree("agree 60 on 30, 20 us noise", 60, 1, 30.0, 20000);
    /* a timeline with another lead: the movie takes it, and a desc.lead that
     * differs is refused */
    {
        psyvid_desc vd;
        psytl_desc td;
        g_case = "agree lead";
        memset(&td, 0, sizeof td);
        td.events = g_tl_ev; td.event_capacity = 16; td.lead = PSYTL_LEAD_NONE;
        CHECK(psytl_open(&g_tl, &td));
        sd_default(&g_sd, 30, 1, 100, 1);
        desc_default(&vd);
        vd.timeline = &g_tl; vd.base = 1;
        CHECK(open_mv(&vd));
        CHECK(g_mv.info.lead == 0.0);
        psyvid_close(&g_mv);
        vd.lead = 0.5;
        CHECK(!psyvid_open(&g_mv, &g_gfx, &vd));
        CHECK(strstr(psyvid_error(&g_mv), "one rule") != NULL);
    }
}

/* Following another clock (a soundtrack's fit), 200 ppm fast: the movie
 * time is the clock's; the residual stays within half a period. */
typedef struct sclock { int64_t t0; double ns_per_unit; } sclock;
static int64_t sc_unit_at(void* ctx, int64_t t) {
    const sclock* c = (const sclock*)ctx;
    return (int64_t)floor((double)(t - c->t0) / c->ns_per_unit + 0.5);
}

static void test_follow(void) {
    psyvid_desc vd;
    disp d;
    psyscr_frame f;
    sclock c;
    int64_t k, n = 6000, worst = 0, drift = 0, flagged = 0, first_drift = -1;
    g_case = "follow";
    rec_reset();
    sd_default(&g_sd, 30, 1, 10000, 1);
    desc_default(&vd);
    if (!open_mv(&vd)) return;
    disp_init(&d, 60.0, 0);
    c.t0 = d.T0 - 12345678;
    c.ns_per_unit = 1e9 / 48000.0 / (1.0 + 200e-6);    /* the device runs fast */
    CHECK(psyvid_play_at(&g_mv, PSYVID_ASAP) > 0);
    for (k = 0; k < n; k++) {
        disp_next(&d, &f);
        CHECK_I(psyvid_follow(&g_mv, &f, sc_unit_at, &c, 48000), PSYVID_OK);
        psyvid_update(&g_mv, &f);
        if ((k & 255) == 0) drain();
    }
    settle(&d);
    for (k = 10; k < n; k++) {
        const frec* r = &g_fr[k];
        int64_t res;
        if (!r->have) continue;
        res = r->t - r->due;    /* how late on screen against the clock */
        if (r->dec == PSYVID_SHOWN && llabs(res) > worst) worst = llabs(res);
        if (r->why == PSYVID_WHY_DRIFT) { if (!drift) first_drift = k; drift++; }
        if (r->flags & PSYVID_F_AUDIO_CLOCK) flagged++;
        /* the movie time is the clock's */
        if (k == 3000) {
            int64_t want = (int64_t)floor((double)(sc_unit_at(&c, r->t) - sc_unit_at(&c, g_fr[0].t)) * 1e9 / 48000.0 + 0.5);
            CHECK(llabs(r->mt - want) < 30000);
        }
    }
    CHECK(worst <= d.P_report / 2 + 30000);
    /* 200 ppm of a 16.7 ms period is 3.3 us of phase per display frame: the
     * first slip after half a period (2500 frames), then one each 5000 */
    CHECK(drift >= 1 && drift <= 3);
    CHECK(first_drift > 2400 && first_drift < 2600);
    CHECK(flagged > n / 2);
    CHECK_I(count_other((uint16_t)PSYVID_EV_CLOCK), 1);
    printf("psy_video_test: follow 200 ppm: worst |onset - due| of a shown frame %.3f ms, %lld slips in %.0f s\n",
           worst / 1e6, (long long)drift, n / 60.0);
    psyvid_close(&g_mv);
}

/* The FRAME record field by field, and the flags from the flip records. */
static void test_records(void) {
    psyvid_desc vd;
    disp d;
    psyscr_frame f;
    int64_t k;
    g_case = "records";
    rec_reset();
    sd_default(&g_sd, 30, 1, 1000, 1);
    g_sd.timescale = 90000;
    g_sd.bad_ts_frame = 20;
    g_sd.bad_hash_frame = 25;
    desc_default(&vd);
    vd.movie_index = 7;
    vd.min_tier = 2;
    if (!open_mv(&vd)) return;
    disp_init(&d, 60.0, 0);
    d.tier = PSYSCR_TIER_SIM;
    CHECK(psyvid_play_at(&g_mv, PSYVID_ASAP) > 0);
    for (k = 0; k < 80; k++) {
        disp_next(&d, &f);
        psyvid_update(&g_mv, &f);
        if (k == 30) d.flags = PSYSCR_FLIP_ESTIMATED;
        if (k == 31) d.not_shown = 1;
        if (k == 60) d.hold = 1;   /* its record arrives with frame 62's, before 61's */
    }
    settle(&d);
    {
        const frec* r = &g_fr[10];
        CHECK_I(r->frame, 5);
        CHECK_I(r->dec, PSYVID_SHOWN);
        CHECK_I(r->t, disp_t(&d, 10));
        CHECK_I(r->due, disp_t(&d, 0) + psyvid_frame_time(30, 1, 5));
        CHECK_I(r->mt, disp_t(&d, 10) - disp_t(&d, 0));
        CHECK_I(r->shows, 1);
        CHECK_I(r->tier, PSYSCR_TIER_SIM);
        CHECK(r->flags & PSYVID_F_BELOW_TIER);
        CHECK_I(g_fr[11].shows, 2);
        CHECK_I(g_fr[11].dec, PSYVID_REPEATED);
    }
    CHECK(g_fr[40].flags & PSYVID_F_TS_MISMATCH);
    CHECK(!(g_fr[42].flags & PSYVID_F_TS_MISMATCH));
    CHECK(g_fr[50].flags & PSYVID_F_HASH_MISMATCH);
    CHECK(g_fr[30].flags & PSYVID_F_ESTIMATED);
    /* a record that came a frame late, in f.done ahead of the newer one,
     * still completes its own frame with its own onset */
    CHECK(!(g_fr[60].flags & PSYVID_F_ESTIMATED));
    CHECK_I(g_fr[60].t, disp_t(&d, 60));
    CHECK(!(g_fr[61].flags & PSYVID_F_ESTIMATED));
    CHECK(g_fr[31].flags & PSYVID_F_NOT_SHOWN);
    CHECK_I(g_mv.info.ts_mismatch, 1);
    CHECK_I(g_mv.info.hash_mismatch, 1);
    {
        char line[1024];
        psyvid_describe(&g_mv, line, sizeof line);
        CHECK(strstr(line, "NOT CANONICAL") != NULL);
    }
    /* every record's aux is the movie index; the OPEN record first */
    {
        const psyrt_event* o = find_other((uint16_t)PSYVID_EV_OPEN, 0);
        CHECK(o != NULL);
        if (o) { CHECK_I(o->aux, 7); CHECK_I(o->u.i32[0], FW); CHECK_I(o->u.i32[3], 30); CHECK_I(o->u.i32[7], 2); }
    }
    {
        psyvid_record r;
        memset(&r, 0, sizeof r);
        CHECK_I(psyvid_last(&g_mv, &r), PSYVID_OK);
        CHECK_I(r.display, 80);
    }
    psyvid_close(&g_mv);
}

/* The canonical check: each field refused alone, with its message. */
static void test_open_refusals(void) {
    psyvid_desc vd;
    uint8_t idx[PSYVID__IDX_HDR + 8 * 100];
    psyvid__canon c;
    int k;
    g_case = "open refusals";
    rec_reset();
    sd_default(&g_sd, 30, 1, 100, 1);
    desc_default(&vd);
    CHECK(open_mv(&vd)); psyvid_close(&g_mv);
#define REFUSE(setup, needle) do { sd_default(&g_sd, 30, 1, 100, 1); desc_default(&vd); setup; \
        if (psyvid_open(&g_mv, &g_gfx, &vd)) { fail(__LINE__, "opened: " #setup); psyvid_close(&g_mv); } \
        else if (!strstr(psyvid_error(&g_mv), needle)) { fprintf(stderr, "  message: %s\n", psyvid_error(&g_mv)); fprintf(stderr, "  wanted: %s\n", needle); fail(__LINE__, "message lacks the reason"); } } while (0)
    REFUSE(g_sd.format = PSYVID_FMT_P010, "10-bit");
    REFUSE(g_sd.format = PSYVID_FMT_I420, "matrix is unspecified");
    REFUSE((g_sd.format = PSYVID_FMT_I420, g_sd.matrix = 2, g_sd.range = 1, g_sd.transfer = 2, g_sd.primaries = 2), "siting is unspecified");
    REFUSE((g_sd.format = PSYVID_FMT_NV12, g_sd.matrix = 2, g_sd.range = 0, g_sd.transfer = 2, g_sd.primaries = 2, g_sd.siting = 2), "range is unspecified");
    REFUSE(g_sd.num = 0, "rate");
    REFUSE((g_sd.num = 1000000, g_sd.den = 1), "rate");
    REFUSE(g_sd.gop = 0, "GOP");
    REFUSE(g_sd.report_frames = 0, "no frames");
    REFUSE(g_sd.fail_open = 1, "scripted failure");
    REFUSE(vd.gpu_path = PSYVID_PATH_SHARED, "psyscr_native");
    REFUSE(vd.light = PSYVID_LIGHT_EOTF, "planar");
    REFUSE(vd.ahead = 99, "ahead");
    REFUSE(vd.lead = 1.5, "lead");
    REFUSE(vd.backend = PSYVID_BACKEND_MF, "not in v0.1");
    /* an index that disagrees with the stream, field by field */
    memset(&c, 0, sizeof c);
    c.codec = PSYVID_CODEC_CUSTOM; c.w = FW; c.h = FH; c.format = PSYVID_FMT_RGBA8;
    c.fps_num = 30; c.fps_den = 1; c.frames = 100; c.gop = 1;
    c.matrix = 1; c.range = 2; c.transfer = 1; c.primaries = 1; c.siting = 1;
    psyvid__index_header(idx, &c, 0, 0, 0);
    memset(idx + PSYVID__IDX_HDR, 0, 800);
    sd_default(&g_sd, 30, 1, 100, 1);
    desc_default(&vd);
    vd.index = idx; vd.index_size = sizeof idx;
    CHECK(open_mv(&vd));
    if (g_mv.open) {
        /* the index's hashes are zero: every frame is flagged */
        disp d;
        disp_init(&d, 60.0, 0);
        CHECK(psyvid_play_at(&g_mv, PSYVID_ASAP) > 0);
        run(&d, 10, NULL);
        CHECK(g_mv.info.hash_mismatch == 0);   /* the stream's own hash wins */
        psyvid_close(&g_mv);
    }
    {
        static const struct { int off; const char* needle; } fields[] = {
            { 20, "width" }, { 24, "height" }, { 28, "format" }, { 32, "rate numerator" }, { 48, "GOP" }, { 40, "frames" } };
        for (k = 0; k < 6; k++) {
            uint8_t bad[sizeof idx];
            memcpy(bad, idx, sizeof idx);
            psyvid__w32(bad + fields[k].off, psyvid__r32(bad + fields[k].off) + (fields[k].off == 40 ? (uint32_t)-1 : 1u));
            psyvid__w64(bad + 120, psyvid_xxh64(bad, 120, 0));
            REFUSE((vd.index = bad, vd.index_size = sizeof bad), fields[k].needle);
        }
        {
            uint8_t bad[sizeof idx];
            memcpy(bad, idx, sizeof idx);
            bad[30] ^= 1;   /* without fixing the hash */
            REFUSE((vd.index = bad, vd.index_size = sizeof bad), "damaged");
            memcpy(bad, idx, sizeof idx);
            REFUSE((vd.index = bad, vd.index_size = 100), "not a psy_video index");
        }
    }
#undef REFUSE
}

/* A stream that ends before its index says, a decoder that fails. */
static void test_decoder_faults(void) {
    psyvid_desc vd;
    disp d;
    int rc;
    g_case = "decoder faults";
    rec_reset();
    sd_default(&g_sd, 30, 1, 100, 1);
    g_sd.end_early = 40;
    desc_default(&vd);
    if (!open_mv(&vd)) return;
    disp_init(&d, 60.0, 0);
    CHECK(psyvid_play_at(&g_mv, PSYVID_ASAP) > 0);
    rc = run(&d, 120, NULL);
    CHECK_I(rc, PSYVID_ERR_DECODER);
    CHECK(strstr(psyvid_error(&g_mv), "ended at frame 40") != NULL);
    CHECK_I(g_up_frame, 39);   /* the last good frame stays */
    psyvid_close(&g_mv);
    sd_default(&g_sd, 30, 1, 100, 1);
    g_sd.fail_frame = 10;
    desc_default(&vd);
    if (!open_mv(&vd)) return;
    CHECK(psyvid_play_at(&g_mv, PSYVID_ASAP) > 0);
    rc = run(&d, 40, NULL);
    CHECK_I(rc, PSYVID_ERR_DECODER);
    CHECK(strstr(psyvid_error(&g_mv), "frame 10") != NULL);
    psyvid_close(&g_mv);
}

/* The same inputs twice: records equal byte for byte. Also the heap: no
 * call per frame. */
static psyrt_event g_rep[2][8192];
static int g_nrep[2];

static void test_replay_and_heap(void) {
    int t;
    uint64_t h0 = 0, h1 = 0;
    for (t = 0; t < 2; t++) {
        psyvid_desc vd;
        disp d;
        psyscr_frame f;
        int64_t k;
        g_case = "replay";
        rec_reset();
        g_rng = 12345;
        sd_default(&g_sd, 24000, 1001, 5000, 4);
        g_sd.stall_frame = 200;
        desc_default(&vd);
        if (!open_mv(&vd)) return;
        disp_init(&d, 60.0, 30.0);
        g_sd.stall_until = disp_t(&d, 500);
        d.noise_ns = 20000;
        CHECK(psyvid_play_at(&g_mv, PSYVID_ASAP) > 0);
        for (k = 0; k < 3000; k++) {
            if (k == 100) h0 = psyvid_heap_calls();
            if (k == 1000) d.late_next = 1;
            if (k == 1500) psyvid_seek_frame(&g_mv, 1200, PSYVID_ASAP);
            disp_next(&d, &f);
            psyvid_update(&g_mv, &f);
            if (k == 2000) psyvid_pause_at(&g_mv, PSYVID_ASAP);
            if (k == 2100) psyvid_play_at(&g_mv, PSYVID_ASAP);
            if (g_nrep[t] < 8000) g_nrep[t] += psyrt_ring_drain(&g_ring, g_rep[t] + g_nrep[t], 8000 - g_nrep[t]);
        }
        h1 = psyvid_heap_calls();
        CHECK_I(h1 - h0, 0);
        psyvid_close(&g_mv);
    }
    g_case = "replay";
    CHECK(g_nrep[0] > 2000);
    CHECK_I(g_nrep[0], g_nrep[1]);
    {
        int i, diff = 0;
        for (i = 0; i < g_nrep[0] && i < g_nrep[1]; i++) {
            /* tid and seq are the ring's; the rest must match */
            psyrt_event a = g_rep[0][i], b = g_rep[1][i];
            a.tid = b.tid = 0; a.seq = b.seq = 0;
            if (a.kind == PSYVID_EV_OPEN || a.kind == PSYVID_EV_DECODE) a.t_ns = b.t_ns = 0;
            if (memcmp(&a, &b, sizeof a) != 0) diff++;
        }
        CHECK_I(diff, 0);
    }
}

/* ------------------------------------------------- frame sequence container */

static void fill_frame(int32_t format, int32_t w, int32_t h, int64_t i, uint8_t* p, size_t n) {
    size_t k;
    (void)w; (void)h;
    if (format == PSYVID_FMT_R16F || format == PSYVID_FMT_RGBA16F || format == PSYVID_FMT_R32F || format == PSYVID_FMT_RGBA32F) {
        float* fp = (float*)(void*)p;
        for (k = 0; k < n / 4; k++) fp[k] = (float)((double)((i * 131 + (int64_t)k * 7) % 1000) / 999.0);
        return;
    }
    for (k = 0; k < n; k++) p[k] = (uint8_t)(rnd() >> 13);
    (void)i;
}

static void test_seq(void) {
    static const int32_t formats[11] = { PSYVID_FMT_R8, PSYVID_FMT_RG8, PSYVID_FMT_RGBA8, PSYVID_FMT_R16F,
        PSYVID_FMT_RGBA16F, PSYVID_FMT_R32F, PSYVID_FMT_RGBA32F, PSYVID_FMT_R16, PSYVID_FMT_I420, PSYVID_FMT_NV12,
        PSYVID_FMT_RGBA8 };
    static uint8_t frames[12][21 * 13 * 16 + 64];
    static uint8_t padded[21 * 13 * 16 * 2];
    static uint8_t expect[21 * 13 * 16];
    static int16_t rows[1024];   /* >= psyvid_yuv_rows_bytes(21) */
    const char* path = "psy_video_test.psyseq";
    int t;
    g_case = "seq";
    CHECK(sizeof rows >= psyvid_yuv_rows_bytes(21));
    for (t = 0; t < 11; t++) {
        psyvid_seq w;
        psyvid_seq_desc sd;
        psyvid_desc vd;
        disp d;
        psyscr_frame f;
        int32_t W = 21, H = 13, fmt = formats[t];
        int qoi = t == 10, k, src;
        psyvid_planes pl;
        size_t fb = psyvid__planes_layout(fmt, W, H, NULL, &pl);
        char name[64];
        snprintf(name, sizeof name, "seq %s%s", psyvid__fmt_name(fmt), qoi ? " QOI" : "");
        g_case = name;
        memset(&sd, 0, sizeof sd);
        sd.path = path; sd.w = W; sd.h = H; sd.format = fmt; sd.compression = qoi ? PSYVID_SEQ_QOI : PSYVID_SEQ_RAW;
        sd.fps_num = 25; sd.fps_den = 1;
        if (psyvid__is_yuv(fmt)) { sd.matrix = PSYVID_MATRIX_BT709; sd.range = PSYVID_RANGE_LIMITED; sd.transfer = PSYVID_TRC_BT1886; sd.primaries = PSYVID_PRIM_BT709; sd.siting = PSYVID_SITING_LEFT; }
        if (!psyvid_seq_create(&w, &sd)) { fail(__LINE__, psyvid_seq_error(&w)); continue; }
        for (k = 0; k < 12; k++) {
            const void* planes[3];
            int32_t strides[3];
            psyvid_planes in;
            int p, y;
            fill_frame(fmt, W, H, k, frames[k], fb);
            /* hand it over with padded rows, to check the strides */
            psyvid__planes_layout(fmt, W, H, frames[k], &in);
            {
                uint8_t* dst = padded;
                for (p = 0; p < 3; p++) {
                    planes[p] = NULL; strides[p] = 0;
                    if (!in.data[p]) continue;
                    strides[p] = in.stride[p] + 8;
                    planes[p] = dst;
                    for (y = 0; y < in.h[p]; y++) memcpy(dst + (size_t)y * (size_t)strides[p], in.data[p] + (size_t)y * (size_t)in.stride[p], (size_t)in.stride[p]);
                    dst += (size_t)strides[p] * (size_t)in.h[p];
                }
            }
            CHECK_I(psyvid_seq_write(&w, planes, strides, NULL), PSYVID_OK);
        }
        CHECK_I(psyvid_seq_close(&w), PSYVID_OK);
        /* from the file and from memory (test_reader covers a reader) */
        for (src = 0; src < 2; src++) {
            static uint8_t filebuf[1 << 20];
            size_t fsize = 0;
            {
                FILE* fh = fopen(path, "rb");
                CHECK(fh != NULL);
                if (fh) { fsize = fread(filebuf, 1, sizeof filebuf, fh); fclose(fh); }
            }
            memset(&vd, 0, sizeof vd);
            vd.inline_decode = true;
            vd.refresh_num = 50; vd.refresh_den = 1;
            vd.ahead = 3;
            if (src == 0) vd.path = path;
            else { vd.data = filebuf; vd.size = fsize; }
            g_up_bytes = (size_t)W * (size_t)H * (size_t)psyvid__bpt(psyvid__is_yuv(fmt) ? PSYVID_FMT_RGBA8 : fmt);
            if (!psyvid_open(&g_mv, &g_gfx, &vd)) { fail(__LINE__, psyvid_error(&g_mv)); fprintf(stderr, "  %s\n", psyvid_error(&g_mv)); break; }
            CHECK_I(g_mv.info.frames, 12);
            CHECK_I(g_mv.info.format, fmt);
            disp_init(&d, 50.0, 0);
            /* random access in manual mode: every frame value-exact */
            for (k = 0; k < 24; k++) {
                int64_t i = (int64_t)(rnd() % 12);
                CHECK_I(psyvid_show(&g_mv, i), PSYVID_OK);
                disp_next(&d, &f);
                CHECK_I(psyvid_update(&g_mv, &f), PSYVID_OK);
                if (psyvid__is_yuv(fmt)) {
                    psyvid_planes in;
                    psyvid__planes_layout(fmt, W, H, frames[i], &in);
                    psyvid_yuv_to_rgba(&in, fmt, W, H, sd.matrix, sd.range, sd.siting, 0, expect, W * 4, rows);
                    CHECK(memcmp(g_up_copy, expect, g_up_bytes) == 0);
                } else {
                    CHECK(memcmp(g_up_copy, frames[i], g_up_bytes) == 0);
                    CHECK(g_up_hash == psyvid_xxh64(frames[i], g_up_bytes, 0));
                }
            }
            CHECK_I(g_mv.info.hash_mismatch, 0);
            CHECK_I(g_mv.info.ts_mismatch, 0);
            psyvid_close(&g_mv);
        }
    }
    /* damage: a flipped byte in a frame is a hash mismatch; a header that was
     * never written is refused */
    {
        static uint8_t buf[1 << 20];
        FILE* fh;
        size_t n;
        psyvid_desc vd;
        disp d;
        int k;
        g_case = "seq damage";
        fh = fopen(path, "rb");
        n = fh ? fread(buf, 1, sizeof buf, fh) : 0;
        if (fh) fclose(fh);
        CHECK(n > 8192);
        buf[4096 + 64 + 5] ^= 0x10;   /* frame 0's data (the RGBA8 QOI file: decodes, wrong) */
        memset(&vd, 0, sizeof vd);
        vd.inline_decode = true; vd.refresh_num = 50; vd.ahead = 2;
        vd.data = buf; vd.size = n;
        g_up_bytes = 21 * 13 * 4;
        if (psyvid_open(&g_mv, &g_gfx, &vd)) {
            psyscr_frame f;
            disp_init(&d, 50.0, 0);
            psyvid_show(&g_mv, 0);
            disp_next(&d, &f);
            k = psyvid_update(&g_mv, &f);
            CHECK(k == PSYVID_OK || k == PSYVID_ERR_DECODER);
            CHECK(g_mv.info.hash_mismatch == 1 || k == PSYVID_ERR_DECODER);
            psyvid_close(&g_mv);
        } else {
            fail(__LINE__, psyvid_error(&g_mv));
        }
        buf[4096 + 64 + 5] ^= 0x10;
        buf[72] ^= 1;   /* the header hash */
        CHECK(!psyvid_open(&g_mv, &g_gfx, &vd));
        CHECK(strstr(psyvid_error(&g_mv), "damaged") != NULL);
        buf[72] ^= 1;
        vd.size = 5000;   /* truncated: the index is past the end */
        CHECK(!psyvid_open(&g_mv, &g_gfx, &vd));
        /* probe reads the header alone */
        {
            psyvid_info info;
            char e[200];
            vd.size = n;
            CHECK(psyvid_probe(&vd, &info, e, sizeof e));
            CHECK_I(info.frames, 12);
            CHECK_I(info.w, 21);
            CHECK_I(info.fps_num, 25);
        }
    }
    remove(path);
}

/* A reader source: the frame sequence through byte ranges in odd pieces. */
static uint8_t g_rd_buf[1 << 20];
static int64_t rd_read(void* ctx, int64_t off, void* buf, int64_t n) {
    (void)ctx;
    if (n > 777) n = 777;
    memcpy(buf, g_rd_buf + off, (size_t)n);
    return n;
}

static void test_reader(void) {
    psyvid_seq w;
    psyvid_seq_desc sd;
    psyvid_desc vd;
    psyvid_reader rd;
    disp d;
    psyscr_frame f;
    static uint8_t px[9 * 7 * 4];
    const void* planes[3] = { px, NULL, NULL };
    int32_t strides[3] = { 9 * 4, 0, 0 };
    const char* path = "psy_video_test_rd.psyseq";
    FILE* fh;
    size_t n;
    int k;
    g_case = "reader";
    memset(&sd, 0, sizeof sd);
    sd.path = path; sd.w = 9; sd.h = 7; sd.format = PSYVID_FMT_RGBA8; sd.fps_num = 30;
    sd.max_frames = 5;
    CHECK(psyvid_seq_create(&w, &sd));
    for (k = 0; k < 5; k++) { memset(px, k + 1, sizeof px); CHECK_I(psyvid_seq_write(&w, planes, strides, NULL), PSYVID_OK); }
    CHECK_I(psyvid_seq_write(&w, planes, strides, NULL), PSYVID_ERR_FULL);   /* max_frames */
    CHECK_I(psyvid_seq_close(&w), PSYVID_OK);
    fh = fopen(path, "rb");
    n = fh ? fread(g_rd_buf, 1, sizeof g_rd_buf, fh) : 0;
    if (fh) fclose(fh);
    remove(path);
    memset(&vd, 0, sizeof vd);
    rd.read = rd_read; rd.size = (int64_t)n;
    vd.reader = &rd;
    vd.inline_decode = true; vd.refresh_num = 60; vd.ahead = 2;
    g_up_bytes = sizeof px;
    if (!psyvid_open(&g_mv, &g_gfx, &vd)) { fail(__LINE__, psyvid_error(&g_mv)); return; }
    disp_init(&d, 60.0, 0);
    for (k = 4; k >= 0; k--) {
        psyvid_show(&g_mv, k);
        disp_next(&d, &f);
        CHECK_I(psyvid_update(&g_mv, &f), PSYVID_OK);
        CHECK_I(g_up_copy[0], k + 1);
        CHECK_I(g_up_copy[sizeof px - 1], k + 1);
    }
    psyvid_close(&g_mv);
}

/* The decode thread for real, against the frame thread, on the psy_rt clock. */
static void test_threaded(void) {
#if !defined(PSYRT_NO_THREADS)
    psyvid_desc vd;
    psyscr_frame f;
    int64_t k, t0, P = 4000000, last = -1, order_bad = 0, shown = 0;
    int rc = PSYVID_OK;
    g_case = "threaded";
    g_virtual = 0;
    rec_reset();
    sd_default(&g_sd, 250, 1, 1000, 5);
    desc_default(&vd);
    vd.inline_decode = false;
    vd.refresh_num = 250;
    vd.ahead = 4;
    if (!open_mv(&vd)) { g_virtual = 1; return; }
    CHECK(psyvid_play_at(&g_mv, PSYVID_ASAP) > 0);
    t0 = (int64_t)psyrt_now_ns() + 20000000;
    for (k = 0; k < 400 && rc >= 0; k++) {
        memset(&f, 0, sizeof f);
        f.onset = t0 + k * P; f.period = P; f.index = k; f.vblank = k;
        psyrt_sleep_until((uint64_t)(f.onset - P / 2), 0);
        rc = psyvid_update(&g_mv, &f);
        if (g_mv.has_last && g_mv.last.display == k && g_mv.last.decision == PSYVID_SHOWN) {
            if (g_mv.last.frame <= last) order_bad++;
            last = g_mv.last.frame;
            shown++;
        }
        if (k == 200) psyvid_seek_frame(&g_mv, 50, PSYVID_ASAP), last = -1;
    }
    CHECK(rc >= 0);
    CHECK_I(order_bad, 0);
    CHECK(shown > 200);
    psyvid_close(&g_mv);
    g_virtual = 1;
#endif
}

int main(void) {
    gfx_open();
    test_pure();
    test_qoi();
    test_yuv();
    test_multiple_and_noise();
    test_cadences();
    test_drift();
    test_display_late();
    test_decode_stall();
    test_seek();
    test_loop_end();
    test_manual();
    test_pause();
    test_timeline_agreement();
    test_follow();
    test_records();
    test_open_refusals();
    test_decoder_faults();
    test_replay_and_heap();
    test_seq();
    test_reader();
    test_threaded();
    psygfx_close(&g_gfx);
    psyscr_close(&g_scr);
    if (g_failures) { fprintf(stderr, "psy_video_test: %d failure(s)\n", g_failures); return 1; }
    printf("psy_video_test: all checks passed\n");
    return 0;
}
