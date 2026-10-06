/* psy_audio_test.c - self-checking test for psy_audio.h's core: the plan of
 * a start frame from the fit, sample-exact placement, the device-clock fit
 * under drift and one-sided callback jitter, confirmation and tiers,
 * underruns, cancel, stop, gain, loops, routing, clipping, the ring
 * records, the queues, the strict open, synthesis and the output formats.
 * No framework: it returns 0 when every check passed and 1 after printing
 * each failure.
 *
 * It needs no miniaudio and no sound hardware: PSYAU_NO_MINIAUDIO builds
 * the core alone, and a scripted device (the Audio device extension) plays
 * a device whose clock runs at a chosen drift against the psy_rt clock,
 * with seeded late-only callback jitter, a pipeline of L frames between a
 * rendered frame and the output, position reports stamped at the call (as
 * WASAPI's were measured), and injected stalls. Its output goes on a tape
 * indexed by stream frame, so the test checks where every sample landed.
 * The frame-thread side runs on a virtual clock through PSYAU__NOW. One
 * case runs the device on a real thread against the frame thread, for
 * ThreadSanitizer.
 *
 *     gcc -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Werror -O2 -I. \
 *         -o audio_test tests/adapt/psy_audio_test.c -lm -pthread && ./audio_test
 *     cl /nologo /W4 /WX /I. tests\adapt\psy_audio_test.c
 */
#ifndef PSYAU_NO_MINIAUDIO
#define PSYAU_NO_MINIAUDIO
#endif
static int g_virtual = 1;
static long long g_vt = 1000000000000LL;
static long long vnow(void);
static void vsleep_until(long long t);
#define PSYAU__NOW() ((int64_t)vnow())
#define PSYAU__SLEEP_UNTIL(t) vsleep_until((long long)(t))

#define PSY_AUDIO_IMPLEMENTATION
#include "psy_audio.h"

static long long vnow(void) { return g_virtual ? g_vt : (long long)psyrt_now_ns(); }
static void vsleep_until(long long t) {
    if (!g_virtual) { psyrt_sleep_until((uint64_t)t, 0); return; }
    if (t > g_vt) g_vt = t;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#if defined(_WIN32)
#include <process.h>
#else
#include <pthread.h>
#endif

/* ---------------------------------------------------------------- harness */

static int g_failures = 0;
static void fail(int line, const char* what) {
    fprintf(stderr, "psy_audio_test: FAIL at line %d: %s\n", line, what);
    g_failures++;
}
static void fail_i(int line, const char* what, long long got, long long want) {
    fprintf(stderr, "psy_audio_test: FAIL at line %d: %s (got %lld, want %lld)\n", line, what, got, want);
    g_failures++;
}
#define CHECK(c) do { if (!(c)) fail(__LINE__, #c); } while (0)
#define CHECK_I(got, want) do { long long g_ = (long long)(got), w_ = (long long)(want); \
    if (g_ != w_) fail_i(__LINE__, #got " == " #want, g_, w_); } while (0)

/* ---------------------------------------------------------- scripted device */

#define TAPE_FRAMES (48000 * 20)

typedef struct sdev {
    /* what the device is */
    uint32_t rate;
    uint16_t channels, out;
    int32_t  period, buffer, pos_source, tier;
    uint8_t  map[PSYAU_MAX_CHANNELS];
    double   drift_ppm;     /* the device clock runs fast by this much         */
    int64_t  L;             /* frames from render to output                    */
    double   jitter_ns;     /* mean callback lateness (exponential), 0 = none  */
    double   report_noise_ns;  /* mean lateness of a report's position         */
    int64_t  pos_jump;      /* frames added to the reports only               */
    int      stall_halts;   /* a stall halts the position (else it counts)    */
    uint64_t rng;
    int      fail_open;
    /* the run */
    int64_t  T0;            /* psy_rt time of stream frame -L's output         */
    int64_t  g;             /* silent frames the device inserted (stalls)      */
    int64_t  k;             /* blocks rendered                                 */
    int64_t  W;
    int64_t  stall_ns;      /* the next block is this late                     */
    void*    host;
    psyau_render_fn render;
    float*   tape;          /* channels floats per stream frame, or NULL       */
    unsigned char out_mem[8192 * 8 * 4];
    int      started, stopped, closed;
} sdev;

static sdev g_sd;

static double sd_kd(const sdev* s) { return 1e9 / (double)s->rate / (1.0 + s->drift_ppm * 1e-6); }

/* The true output time of stream frame w. */
static int64_t sd_truth(const sdev* s, int64_t w) {
    return s->T0 + (int64_t)floor(((double)(w + s->L + s->g)) * sd_kd(s) + 0.5);
}
/* The frame on which t falls by the header's rule, from the truth. */
static int64_t sd_frame_of(const sdev* s, int64_t t) {
    double x = (double)(t - s->T0) / sd_kd(s) - (double)(s->L + s->g);
    return (int64_t)ceil(x - 0.5);
}

/* The device's request time for stream frame w (the callback-time line). */
static int64_t sd_event(const sdev* s, int64_t w) {
    return s->T0 + (int64_t)floor(((double)(w + s->g)) * sd_kd(s) + 0.5);
}

static double sd_rand(sdev* s) {
    s->rng = s->rng * 6364136223846793005ull + 1442695040888963407ull;
    return ((double)(s->rng >> 11) + 0.5) / 9007199254740992.0;
}

static int sd_open(void* ctx, const psyau_device_open* in, psyau_device_caps* caps, char* err, size_t cap) {
    sdev* s = (sdev*)ctx;
    (void)in;
    if (s->fail_open) { snprintf(err, cap, "scripted: refused"); return PSYAU_ERR_LOST; }
    memset(caps, 0, sizeof *caps);
    caps->rate = s->rate;
    caps->channels = s->channels;
    caps->out = s->out;
    memcpy(caps->map, s->map, sizeof caps->map);
    caps->period = s->period;
    caps->buffer = s->buffer;
    caps->pos_source = s->pos_source;
    caps->tier = s->tier;
    snprintf(caps->name, sizeof caps->name, "scripted");
    return 0;
}
static int sd_start(void* ctx, psyau_render_fn r, void* host) {
    sdev* s = (sdev*)ctx;
    s->render = r; s->host = host; s->started = 1;
    return 0;
}
static void sd_stop(void* ctx) { ((sdev*)ctx)->stopped = 1; }
static void sd_close(void* ctx) { ((sdev*)ctx)->closed = 1; }
static const psyau_device g_dev = { PSYAU_DEVICE_VERSION, "scripted", sd_open, sd_start, sd_stop, sd_close, NULL };

static size_t sd_bytes(const sdev* s) {
    return s->out == PSYAU_OUT_S16 ? 2u : s->out == PSYAU_OUT_S24 ? 3u : 4u;
}

/* Render one block: the device's event for block k, the callback late by
 * the jitter, a report of the engine's position at the latest pass, stamped
 * at the call. */
static void sd_block(sdev* s) {
    double kd = sd_kd(s);
    int64_t e = s->T0 + (int64_t)floor(((double)(s->k * s->period + s->L + s->g) - (double)s->L) * kd + 0.5);
    int64_t late = s->jitter_ns > 0 ? (int64_t)(-s->jitter_ns * log(sd_rand(s))) : 0;
    int64_t t = e + late + s->stall_ns, pass;
    int halted = 0;
    psyau_tick tk;
    int64_t i;
    if (t > g_vt) g_vt = t;
    /* the report: the position a little before its stamp (an engine pass
     * the call reads), late by an exponential with a small mean */
    pass = t - (s->report_noise_ns > 0 ? (int64_t)(-s->report_noise_ns * log(sd_rand(s))) : 0);
    if (s->stall_ns) {
        /* The device ran dry and played silence until this block came; its
         * position counted the silence (the case the header must handle). */
        int64_t gap = (int64_t)floor((double)(pass - s->T0) / kd + 1e-3) - s->L - (s->k * s->period + s->g);
        if (gap > 0) {
            if (s->stall_halts) {
                /* WASAPI's way (measured): the position halts at the frames
                 * written, and later frames play later by the time it stood */
                s->T0 += (int64_t)floor((double)gap * kd + 0.5);
                pass = t;
                halted = 1;
            } else {
                s->g += gap;
            }
        }
        s->stall_ns = 0;
    }
    memset(&tk, 0, sizeof tk);
    tk.t_entry = t;
    if (s->pos_source == PSYAU_POS_DEVICE) {
        /* frames played, a count; its stamp is that frame's true time plus
         * the report's own lateness (pass = t - lateness), so the least late
         * report of a bucket is close to the truth */
        int64_t f = (int64_t)floor((double)(pass - s->T0) / kd + 1e-3);
        tk.pos = f - s->L + s->pos_jump;
        if (tk.pos < 0) tk.pos = 0;
        tk.pos_t = s->T0 + (int64_t)floor((double)f * kd + 0.5) + (t - pass);
        if (halted) { tk.pos = s->W + s->g; tk.pos_t = t; }   /* stood at the frames written */
    } else {
        tk.pos = -1;
    }
    s->render(s->host, s->out_mem, s->period, &tk);
    if (s->tape && s->W + s->period <= TAPE_FRAMES) {
        for (i = 0; i < (int64_t)s->period * s->channels; i++) {
            float v;
            if (s->out == PSYAU_OUT_F32) v = ((float*)(void*)s->out_mem)[i];
            else if (s->out == PSYAU_OUT_S16) v = (float)((int16_t*)(void*)s->out_mem)[i] / 32768.0f;
            else if (s->out == PSYAU_OUT_S32) v = (float)((double)((int32_t*)(void*)s->out_mem)[i] / 2147483648.0);
            else {
                const unsigned char* b = s->out_mem + i * 3;
                int32_t x = (int32_t)((uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16));
                if (x & 0x800000) x -= 0x1000000;
                v = (float)x / 8388608.0f;
            }
            s->tape[s->W * s->channels + i] = v;
        }
    }
    s->W += s->period;
    s->k++;
    (void)sd_bytes;
}

static void sd_run(sdev* s, int64_t blocks) { int64_t i; for (i = 0; i < blocks; i++) sd_block(s); }
static void sd_run_s(sdev* s, double sec) { sd_run(s, (int64_t)(sec * s->rate / s->period + 0.5)); }

static float* g_tape;
static psyau_audio g_au;
static unsigned char g_ring_mem[PSYRT_RING_BYTES(16384)];
static psyrt_ring g_ring;
static float g_arena[1 << 20];

static void sd_default(sdev* s) {
    memset(s, 0, sizeof *s);
    s->rate = 48000; s->channels = 2; s->out = PSYAU_OUT_F32;
    s->period = 480; s->buffer = 1440; s->pos_source = PSYAU_POS_DEVICE; s->tier = PSYAU_TIER_2;
    s->map[0] = PSYAU_CH_FL; s->map[1] = PSYAU_CH_FR;
    s->drift_ppm = 37; s->L = 1900; s->jitter_ns = 300000; s->rng = 12345;
    s->report_noise_ns = 20000;
    s->T0 = g_vt + 5000000;
    s->tape = g_tape;
    if (g_tape) memset(g_tape, 0, sizeof(float) * TAPE_FRAMES * 2);
}

static bool open_dev(sdev* s, psyau_desc* d) {
    psyau_desc dd;
    if (d) dd = *d; else memset(&dd, 0, sizeof dd);
    dd.backend = PSYAU_BACKEND_CUSTOM;
    dd.dev = &g_dev;
    dd.dev_ctx = s;
    if (!dd.ring) { psyrt_ring_open(&g_ring, &(psyrt_ring_desc){ .memory = g_ring_mem, .bytes = sizeof g_ring_mem }); dd.ring = &g_ring; }
    if (!dd.arena) { dd.arena = g_arena; dd.arena_bytes = sizeof g_arena; }
    return psyau_open(&g_au, &dd);
}

static float g_buf[48000 * 2];

/* ------------------------------------------------------------------- cases */

/* Plans: every start frame is the frame the truth puts the target on, and
 * the samples are on the tape exactly there. */
static void test_plan_and_placement(void) {
    sdev* s = &g_sd;
    int i, exact = 0, within1 = 0, n = 200;
    int64_t worst = 0;
    sd_default(s);
    CHECK(open_dev(s, NULL));
    sd_run_s(s, 12.0);   /* past the slope span */
    for (i = 0; i < 48000; i++) g_buf[i] = (float)((i % 997) + 1) / 2000.0f;   /* never 0 */
    for (i = 0; i < n; i++) {
        psyau_buf b;
        psyau_onset r = { 0 };
        int64_t t, want, got;
        int rc;
        memset(&b, 0, sizeof b);
        b.frames = g_buf; b.n = 100; b.channels = 1; b.id = 77;
        /* targets between frames, at every phase, 60 to 160 ms ahead */
        t = g_vt + 60000000 + (int64_t)(sd_rand(s) * 100000000.0);
        CHECK(psyau_play_at(&g_au, b, t) > 0);
        sd_run_s(s, 0.25);
        rc = psyau_result(&g_au, (psyau_id)(g_au.next_id), &r);
        CHECK_I(rc, PSYAU_OK);
        want = sd_frame_of(s, t);
        got = r.start_frame;
        if (got == want) exact++;
        else {
            /* a miss is allowed only for a target within 2 us of a tie */
            double x = (double)(t - s->T0) / sd_kd(s) - (double)(s->L + s->g);
            double tie = fabs((x - floor(x)) - 0.5) * sd_kd(s);
            if (tie < 2000.0) exact++;
            else fail_i(__LINE__, "start frame off the truth, target far from a tie", got, want);
        }
        if (llabs(got - want) <= 1) within1++;
        if (llabs(sd_truth(s, got) - r.onset) > worst) worst = llabs(sd_truth(s, got) - r.onset);
        CHECK(r.tier == PSYAU_TIER_2);
        CHECK(!(r.flags & (PSYAU_ONSET_PENDING | PSYAU_ONSET_LATE | PSYAU_ONSET_UNCONFIRMED)));
        CHECK_I(r.buffer_id, 77);
        CHECK(r.end_frame == got + 100);
        /* the tape: zero before, the buffer from `got` on */
        if (got > 0 && got + 100 < TAPE_FRAMES) {
            CHECK(g_tape[(got - 1) * 2] == 0.0f);
            CHECK(g_tape[got * 2] == g_buf[0]);
            CHECK(g_tape[got * 2 + 1] == g_buf[0]);
            CHECK(g_tape[(got + 99) * 2] == g_buf[99]);
            CHECK(g_tape[(got + 100) * 2] == 0.0f);
        }
    }
    printf("plan: %d of %d start frames exact, %d within one; onset vs truth worst %.1f us\n",
           exact, n, within1, (double)worst / 1e3);
    CHECK_I(exact, n);
    CHECK_I(within1, n);
    CHECK(worst < 20833 / 2);
    {
        psyau_caps c;
        psyau_get_caps(&g_au, &c);
        printf("plan: drift %.3f ppm (scripted 37), spread %.0f ns\n", c.drift_ppm, c.fit_spread_ns);
        CHECK(fabs(c.drift_ppm - 37.0) < 1.0);
        CHECK(c.lead_ns > 0);
        CHECK(psyau_frame_at(&g_au, psyau_time_of(&g_au, 123456)) == 123456);
    }
    psyau_close(&g_au);
}

/* The fit under drift. Callback times (late, never early): the envelope
 * recovers the rate and the offset with 1 ms of one-sided jitter, and least
 * squares on the same points misses the offset by their mean lateness,
 * which is why the header uses the envelope there. Position reports, late
 * by 20 us on average: least squares on the bucket minima recovers both. */
static void test_fit_drift(void) {
    sdev* s = &g_sd;
    double drifts[3] = { 100.0, -100.0, 0.0 };
    int j;
    for (j = 0; j < 3; j++) {
        psyau_caps c;
        int64_t probe, err_env;
        double sxx = 0, sxy = 0, mx = 0, my = 0, b, a;
        int32_t i, n;
        sd_default(s);
        s->tape = NULL;
        s->drift_ppm = drifts[j];
        s->jitter_ns = 1000000;
        s->pos_source = PSYAU_POS_CALLBACK;
        s->tier = 0;
        CHECK(open_dev(s, NULL));
        sd_run_s(s, 60.0);
        psyau_update(&g_au);
        psyau_get_caps(&g_au, &c);
        probe = g_au.w + 4800;
        /* callback times map a block's first frame to its device event */
        err_env = psyau_time_of(&g_au, probe) - sd_event(s, probe);
        /* least squares on the same points */
        n = g_au.pt_n;
        for (i = 0; i < n; i++) {
            int32_t q = (g_au.pt_head + i) % PSYAU__FIT_PTS;
            mx += (double)(g_au.pt_w[q] - g_au.pt_w[g_au.pt_head]);
            my += (double)(g_au.pt_t[q] - g_au.pt_t[g_au.pt_head]);
        }
        mx /= n; my /= n;
        for (i = 0; i < n; i++) {
            int32_t q = (g_au.pt_head + i) % PSYAU__FIT_PTS;
            double x = (double)(g_au.pt_w[q] - g_au.pt_w[g_au.pt_head]) - mx;
            double y = (double)(g_au.pt_t[q] - g_au.pt_t[g_au.pt_head]) - my;
            sxx += x * x; sxy += x * y;
        }
        b = sxy / sxx;
        a = my - b * mx;   /* least squares, at x = mx */
        {
            double x = (double)(probe - g_au.pt_w[g_au.pt_head]);
            int64_t t_ols = g_au.pt_t[g_au.pt_head] + (int64_t)(a + b * x);
            int64_t err_ols = t_ols - sd_event(s, probe);
            printf("fit: drift %+.0f ppm -> %+.3f ppm; offset error envelope %+.1f us, least squares %+.1f us\n",
                   drifts[j], c.drift_ppm, (double)err_env / 1e3, (double)err_ols / 1e3);
            CHECK(fabs(c.drift_ppm - drifts[j]) < 0.5);
            CHECK(llabs(err_env) < 20000);
            CHECK(err_ols > 2 * llabs(err_env));
        }
        psyau_close(&g_au);
        /* position reports */
        sd_default(s);
        s->tape = NULL;
        s->drift_ppm = drifts[j];
        CHECK(open_dev(s, NULL));
        sd_run_s(s, 60.0);
        psyau_update(&g_au);
        psyau_get_caps(&g_au, &c);
        probe = g_au.w + 4800;
        err_env = psyau_time_of(&g_au, probe) - sd_truth(s, probe);
        printf("fit: reports, drift %+.0f ppm -> %+.3f ppm; offset error %+.1f us\n",
               drifts[j], c.drift_ppm, (double)err_env / 1e3);
        CHECK(fabs(c.drift_ppm - drifts[j]) < 0.5);
        CHECK(llabs(err_env) < 5000);
        psyau_close(&g_au);
    }
}

/* LATE, cancel, stop with a ramp, gain, loops, routing, clipping. */
static void test_controls(void) {
    sdev* s = &g_sd;
    psyau_buf b;
    psyau_play_desc pd;
    psyau_onset r = { 0 };
    psyau_id id, id2;
    int64_t f, i;
    sd_default(s);
    s->jitter_ns = 0;
    CHECK(open_dev(s, NULL));
    sd_run_s(s, 8.0);   /* past the slope span */
    for (i = 0; i < 48000 * 2; i++) g_buf[i] = 0.5f;
    memset(&b, 0, sizeof b);
    b.frames = g_buf; b.n = 4800; b.channels = 1;

    /* a target in the past: LATE, started on the first reachable frame */
    id = psyau_play_at(&g_au, b, g_vt - 50000000);
    sd_run(s, 1);
    sd_run_s(s, 0.2);
    CHECK_I(psyau_result(&g_au, id, &r), PSYAU_OK);
    CHECK(r.flags & PSYAU_ONSET_LATE);
    CHECK(r.residual > 40000000);
    sd_run_s(s, 0.2);

    /* cancel before the start: nothing plays */
    id = psyau_play_at(&g_au, b, g_vt + 500000000);
    CHECK(psyau_cancel(&g_au, id) == 0);
    sd_run_s(s, 1.0);
    CHECK_I(psyau_result(&g_au, id, &r), PSYAU_OK);
    CHECK(r.flags & PSYAU_ONSET_CANCELED);
    CHECK_I(r.start_frame, -1);
    f = sd_frame_of(s, r.target);
    CHECK(g_tape[f * 2] == 0.0f);

    /* stop at t with a 5 ms ramp: the fade ends on t's frame */
    id = psyau_play_at(&g_au, b, g_vt + 100000000);
    {
        int64_t stop_t = g_vt + 150000000, sf;
        CHECK(psyau_stop_at(&g_au, id, stop_t, PSYAU_MS(5)) == 0);
        sd_run_s(s, 0.4);
        CHECK_I(psyau_result(&g_au, id, &r), PSYAU_OK);
        CHECK(r.flags & PSYAU_ONSET_STOPPED);
        sf = sd_frame_of(s, stop_t);
        CHECK(llabs(r.end_frame - sf) <= 1);
        CHECK(g_tape[(sf - 240) * 2] < 0.5f + 1e-6f && g_tape[(sf - 241) * 2] > 0.4999f);
        CHECK(fabs(g_tape[(sf - 120) * 2] - 0.25f) < 0.01f);   /* half way down the cosine */
        CHECK(fabs(g_tape[(sf - 180) * 2] - 0.4268f) < 0.01f);  /* a quarter: 0.5 (1 + cos(pi / 4)) / 2 */
        CHECK(g_tape[(sf + 1) * 2] == 0.0f);
    }

    /* gain: -6.02 dB from t, no ramp */
    id = psyau_play_at(&g_au, b, g_vt + 100000000);
    {
        int64_t gt = g_vt + 130000000, gf;
        CHECK(psyau_gain_at(&g_au, id, gt, -6.0206f, 0) == 0);
        sd_run_s(s, 0.3);
        gf = sd_frame_of(s, gt);
        if (fabs(g_tape[(gf - 2) * 2] - 0.5f) >= 1e-6f)
            printf("gain: %g %g %g %g at gf-2..gf+1\n", g_tape[(gf - 2) * 2], g_tape[(gf - 1) * 2],
                   g_tape[gf * 2], g_tape[(gf + 1) * 2]);
        CHECK(fabs(g_tape[(gf - 2) * 2] - 0.5f) < 1e-6f);
        CHECK(fabs(g_tape[(gf + 2) * 2] - 0.25f) < 1e-4f);
        /* on the frame itself, unless gt is within 2 us of a tie */
        if (fabs(((double)(gt - s->T0) / sd_kd(s) - (double)(s->L + s->g))
                 - floor((double)(gt - s->T0) / sd_kd(s) - (double)(s->L + s->g)) - 0.5) * sd_kd(s) > 2000.0) {
            CHECK(fabs(g_tape[(gf - 1) * 2] - 0.5f) < 1e-6f);
            CHECK(fabs(g_tape[gf * 2] - 0.25f) < 1e-4f);
        }
    }

    /* loops: three passes; routing: right channel only */
    memset(&pd, 0, sizeof pd);
    pd.buf = b; pd.buf.n = 100; pd.loops = 2; pd.channels = 2; pd.at = g_vt + 100000000;
    id = psyau_play(&g_au, &pd);
    sd_run_s(s, 0.3);
    CHECK_I(psyau_result(&g_au, id, &r), PSYAU_OK);
    CHECK_I(r.end_frame - r.start_frame, 300);
    CHECK(g_tape[r.start_frame * 2] == 0.0f && g_tape[r.start_frame * 2 + 1] == 0.5f);
    CHECK(g_tape[(r.start_frame + 299) * 2 + 1] == 0.5f && g_tape[(r.start_frame + 300) * 2 + 1] == 0.0f);

    /* forever, until stopped */
    pd.loops = PSYAU_FOREVER; pd.channels = 0; pd.at = g_vt + 100000000;
    id = psyau_play(&g_au, &pd);
    sd_run_s(s, 0.5);
    CHECK_I(psyau_result(&g_au, id, &r), PSYAU_OK);
    CHECK_I(r.end_frame, 0);
    CHECK(psyau_stop_at(&g_au, 0, 0, 0) == 0);
    sd_run_s(s, 0.2);
    CHECK_I(psyau_result(&g_au, id, &r), PSYAU_OK);
    CHECK(r.end_frame > 0 && (r.flags & PSYAU_ONSET_STOPPED));

    /* clipping: two overlapping sounds at 0.5 + 0.6 */
    for (i = 0; i < 1000; i++) g_buf[48000 + i] = 0.6f;
    id = psyau_play_at(&g_au, b, g_vt + 100000000);
    {
        psyau_buf b2 = b;
        b2.frames = g_buf + 48000; b2.n = 1000;
        id2 = psyau_play_at(&g_au, b2, g_vt + 100000000);
    }
    sd_run_s(s, 0.3);
    CHECK_I(psyau_result(&g_au, id, &r), PSYAU_OK);
    CHECK(r.flags & PSYAU_ONSET_CLIPPED);
    CHECK(g_tape[r.start_frame * 2] == 1.0f);
    CHECK_I(psyau_result(&g_au, id2, &r), PSYAU_OK);
    CHECK(r.flags & PSYAU_ONSET_CLIPPED);
    sd_run_s(s, 0.2);
    psyau_close(&g_au);
}

/* An underrun: the record of the sound that played through it is XRUN and
 * UNCONFIRMED, tier 3; the device counted its silence, the header shifted its
 * map, and sounds after it land on the truth again. */
/* A plan made before the slope is known is made again in every callback
 * until the sound starts: handed over 6 s ahead in the first second, at 100
 * ppm, it still lands on the truth. */
static void test_replan(void) {
    sdev* s = &g_sd;
    psyau_buf b;
    psyau_onset r = { 0 };
    psyau_id id;
    int64_t t;
    sd_default(s);
    s->drift_ppm = 100;
    CHECK(open_dev(s, NULL));
    sd_run_s(s, 1.0);
    memset(&b, 0, sizeof b);
    b.frames = g_buf; b.n = 100; b.channels = 1;
    t = g_vt + 6000000000LL + 7777;
    id = psyau_play_at(&g_au, b, t);
    sd_run_s(s, 6.5);
    CHECK_I(psyau_result(&g_au, id, &r), PSYAU_OK);
    CHECK(llabs(r.start_frame - sd_frame_of(s, t)) <= 1);
    printf("replan: start frame %lld, truth %lld\n", (long long)r.start_frame, (long long)sd_frame_of(s, t));
    psyau_close(&g_au);
}

static void test_xrun(void) {
    sdev* s = &g_sd;
    psyau_buf b;
    psyau_onset r = { 0 };
    psyau_id id;
    int64_t t;
    sd_default(s);
    s->jitter_ns = 100000;
    CHECK(open_dev(s, NULL));
    sd_run_s(s, 12.0);
    for (t = 0; t < 48000; t++) g_buf[t] = 0.25f;
    memset(&b, 0, sizeof b);
    b.frames = g_buf; b.n = 24000; b.channels = 1;
    id = psyau_play_at(&g_au, b, g_vt + 60000000);
    sd_run_s(s, 0.1);
    s->stall_ns = 80000000;   /* 80 ms against a 40 ms pipeline */
    sd_run_s(s, 1.0);
    CHECK_I(psyau_result(&g_au, id, &r), PSYAU_OK);
    CHECK(r.flags & PSYAU_ONSET_XRUN);
    CHECK(s->g > 0);
    CHECK_I(g_au.p_off, s->g);
    CHECK(g_au.xruns_ft >= 1);
    sd_run_s(s, 2.0);
    t = g_vt + 100000000;
    id = psyau_play_at(&g_au, b, t);
    sd_run_s(s, 0.5);
    CHECK_I(psyau_result(&g_au, id, &r), PSYAU_OK);
    CHECK(llabs(r.start_frame - sd_frame_of(s, t)) <= 1);
    CHECK(!(r.flags & PSYAU_ONSET_XRUN) && r.tier == PSYAU_TIER_2);
    /* The position jumps ahead of the fit with no late callback (measured
     * under load on WASAPI, by up to tens of ms, less than the queue). A short jump:
     * flagged, kept out of the fit, the fit unharmed. */
    {
        psyau_caps c0, c1;
        uint32_t x0;
        psyau_update(&g_au);
        psyau_get_caps(&g_au, &c0);
        x0 = g_au.xruns_ft;
        t = g_vt + 80000000;
        id = psyau_play_at(&g_au, b, t);
        s->pos_jump = 1400;
        sd_run_s(s, 0.5);
        s->pos_jump = 0;
        sd_run_s(s, 1.5);
        psyau_update(&g_au);
        psyau_get_caps(&g_au, &c1);
        CHECK(g_au.xruns_ft >= x0 + 1);
        CHECK_I(psyau_result(&g_au, id, &r), PSYAU_OK);
        CHECK(r.flags & PSYAU_ONSET_XRUN);
        CHECK(fabs(c1.drift_ppm - c0.drift_ppm) < 1.0);
        t = g_vt + 100000000;
        id = psyau_play_at(&g_au, b, t);
        sd_run_s(s, 0.5);
        CHECK_I(psyau_result(&g_au, id, &r), PSYAU_OK);
        CHECK(llabs(r.start_frame - sd_frame_of(s, t)) <= 1);
        /* a jump that lasts: the fit starts over from the reports as they are */
        x0 = g_au.xruns_ft;
        s->pos_jump = 1400;
        sd_run_s(s, 4.0);
        psyau_update(&g_au);
        CHECK(g_au.xruns_ft == x0 + 1);
        CHECK(g_au.fit.ready);
        CHECK(llabs(psyau__fit_frame(&g_au.fit, g_vt) - (sd_frame_of(s, g_vt) + 1400)) <= 2);
        s->pos_jump = 0;
    }
    psyau_close(&g_au);

    /* The WASAPI case, measured: the position halts during the stall and the
     * timing moves; the fit starts over and later sounds land on the truth. */
    sd_default(s);
    s->stall_halts = 1;
    CHECK(open_dev(s, NULL));
    sd_run_s(s, 8.0);
    s->stall_ns = 80000000;
    sd_run_s(s, 0.2);
    psyau_update(&g_au);
    CHECK(g_au.xruns_ft >= 1);
    CHECK_I(g_au.p_off, 0);
    sd_run_s(s, 7.0);
    t = g_vt + 100000000;
    id = psyau_play_at(&g_au, b, t);
    sd_run_s(s, 0.5);
    CHECK_I(psyau_result(&g_au, id, &r), PSYAU_OK);
    CHECK(llabs(r.start_frame - sd_frame_of(s, t)) <= 1);
    psyau_close(&g_au);
}

/* A device with callback times only: planned onsets, tier 3; an underrun
 * from a gap between callbacks. */
static void test_callback_source(void) {
    sdev* s = &g_sd;
    psyau_buf b;
    psyau_onset r = { 0 };
    psyau_id id;
    sd_default(s);
    s->pos_source = PSYAU_POS_CALLBACK;
    s->tier = 0;
    CHECK(open_dev(s, NULL));
    sd_run_s(s, 12.0);
    memset(&b, 0, sizeof b);
    b.frames = g_buf; b.n = 100; b.channels = 1;
    id = psyau_play_at(&g_au, b, g_vt + 80000000);
    sd_run_s(s, 0.2);
    CHECK_I(psyau_result(&g_au, id, &r), PSYAU_OK);
    CHECK(r.flags & PSYAU_ONSET_UNCONFIRMED);
    CHECK_I(r.tier, PSYAU_TIER_3);
    CHECK_I(r.device_pos, -1);
    /* the plan is the callback-time line: frame for t is the one rendered
     * at about t (the offset to the output is not known) */
    s->stall_ns = 100000000;
    sd_run_s(s, 0.2);
    psyau_update(&g_au);
    CHECK(g_au.xruns_ft >= 1);
    psyau_close(&g_au);
}

/* The records in the ring, field by field. */
static void test_ring(void) {
    sdev* s = &g_sd;
    psyau_buf b;
    psyau_onset r = { 0 };
    psyau_id id;
    psyrt_event ev[512];
    int n, i, seen_onset = 0, seen_end = 0, seen_fit = 0, seen_open = 0, seen_clock = 0;
    psyau_desc d;
    sd_default(s);
    memset(&d, 0, sizeof d);
    d.device_index = 5;
    d.min_tier = 2;   /* the scripted path is tier 2: not below it */
    CHECK(open_dev(s, &d));
    sd_run_s(s, 2.0);
    memset(&b, 0, sizeof b);
    b.frames = g_buf; b.n = 480; b.channels = 1; b.id = 4242;
    id = psyau_play_at(&g_au, b, g_vt + 80000000);
    sd_run_s(s, 0.3);
    CHECK_I(psyau_result(&g_au, id, &r), PSYAU_OK);
    while ((n = psyrt_ring_drain(&g_ring, ev, 512)) > 0) {
        for (i = 0; i < n; i++) {
            const psyrt_event* e = &ev[i];
            if (e->source == PSYRT_SRC_RT && e->kind == PSYRT_KIND_CLOCK && e->aux == 0x41550005u) seen_clock++;
            if (e->source != PSYRT_SRC_AUDIO) continue;
            if (e->kind == PSYAU_EV_ONSET && e->aux == (uint32_t)id) {
                seen_onset++;
                CHECK_I((int64_t)e->t_ns, r.onset);
                CHECK_I(e->u.i64[0], r.target);
                CHECK_I(e->u.i64[1], r.start_frame);
                CHECK(e->u.i64[2] > r.start_frame);
                CHECK_I(e->u.i64[3], r.rendered_at);
                CHECK_I(e->u.u32[8], 4242);
                CHECK_I(e->u.u16[18], r.flags);
                CHECK_I(PSYAU_EV_TIER_OF(e->u.u16[19]), PSYAU_TIER_2);
                CHECK_I(PSYAU_EV_DEVICE_OF(e->u.u16[19]), 5);
            }
            if (e->kind == PSYAU_EV_END && e->aux == (uint32_t)id) { seen_end++; CHECK_I(e->u.i64[0], r.start_frame + 480); CHECK_I(e->u.i64[1], 480); }
            if (e->kind == PSYAU_EV_FIT) { seen_fit++; CHECK(e->u.f64[1] > 47990 && e->u.f64[1] < 48010); }
            if (e->kind == PSYAU_EV_OPEN) { seen_open++; CHECK_I(e->u.i32[0], 48000); CHECK_I(e->u.i32[1], 2); CHECK_I(e->u.i32[8], PSYAU_POS_DEVICE); }
        }
    }
    CHECK_I(seen_onset, 1);
    CHECK_I(seen_end, 1);
    CHECK(seen_fit > 0);
    CHECK_I(seen_open, 1);
    CHECK(seen_clock >= 1);
    CHECK(!(r.flags & PSYAU_ONSET_BELOW_TIER));
    psyau_close(&g_au);
    /* min_tier 1: a tier-2 onset is flagged, and the run goes on */
    sd_default(s);
    d.min_tier = 1;
    CHECK(open_dev(s, &d));
    sd_run_s(s, 1.0);
    id = psyau_play_at(&g_au, b, g_vt + 80000000);
    sd_run_s(s, 0.3);
    CHECK_I(psyau_result(&g_au, id, &r), PSYAU_OK);
    CHECK(r.flags & PSYAU_ONSET_BELOW_TIER);
    psyau_close(&g_au);
}

/* The queues and the voices, the results table, close. */
static void test_limits(void) {
    sdev* s = &g_sd;
    psyau_buf b;
    psyau_desc d;
    psyau_onset r = { 0 };
    psyau_id first, id;
    int i;
    sd_default(s);
    memset(&d, 0, sizeof d);
    d.voices = 4;
    CHECK(open_dev(s, &d));
    sd_run_s(s, 1.0);
    memset(&b, 0, sizeof b);
    b.frames = g_buf; b.n = 480; b.channels = 1;
    first = psyau_play_at(&g_au, b, g_vt + 500000000);
    for (i = 1; i < 4; i++) CHECK(psyau_play_at(&g_au, b, g_vt + 500000000) > 0);
    CHECK_I(psyau_play_at(&g_au, b, g_vt + 500000000), PSYAU_ERR_FULL);
    /* bad buffers */
    b.channels = 3;
    CHECK_I(psyau_play_at(&g_au, b, 0), PSYAU_ERR_FORMAT);
    b.channels = 1; b.frames = NULL;
    CHECK_I(psyau_play_at(&g_au, b, 0), PSYAU_ERR_ARG);
    b.frames = g_buf;
    /* close: never-started sounds are CANCELED */
    psyau_close(&g_au);
    CHECK(s->stopped && s->closed);
    CHECK(!psyau_is_open(&g_au));

    /* 256 commands fill the queue when nothing renders */
    sd_default(s);
    memset(&d, 0, sizeof d);
    d.voices = 64;
    CHECK(open_dev(s, &d));
    for (i = 0; i < PSYAU__CMDS; i++) CHECK(psyau_stop_at(&g_au, 0, 0, 0) == 0);
    CHECK_I(psyau_stop_at(&g_au, 0, 0, 0), PSYAU_ERR_FULL);
    sd_run(s, 10);
    /* the results table keeps the last 256 */
    first = 0;
    for (i = 0; i < 300; i++) {
        id = psyau_play_at(&g_au, b, 0);
        if (i == 0) first = id;
        sd_run(s, 1);
    }
    sd_run_s(s, 0.5);
    CHECK_I(psyau_result(&g_au, first, &r), PSYAU_ERR_NOT_FOUND);
    CHECK_I(psyau_result(&g_au, id, &r), PSYAU_OK);
    CHECK_I(g_au.outstanding, 0);
    psyau_close(&g_au);
    (void)first;
}

/* STRICT OPEN: each mismatch is refused with a message that names it. */
static void test_strict_open(void) {
    sdev* s = &g_sd;
    psyau_desc d;
    static const uint8_t lr[2] = { PSYAU_CH_FL, PSYAU_CH_FR }, rl[2] = { PSYAU_CH_FR, PSYAU_CH_FL };
    sd_default(s);
    s->rate = 44100;
    CHECK(!open_dev(s, NULL));
    CHECK(strstr(psyau_error(&g_au), "44100") && strstr(psyau_error(&g_au), "48000"));
    CHECK(s->closed);
    sd_default(s);
    s->channels = 6;
    CHECK(!open_dev(s, NULL));
    CHECK(strstr(psyau_error(&g_au), "6 channels") != NULL);
    sd_default(s);
    s->out = PSYAU_OUT_S16;
    memset(&d, 0, sizeof d);
    d.format.sample = PSYAU_S24;
    CHECK(!open_dev(s, &d));
    CHECK(strstr(psyau_error(&g_au), "s16") != NULL);
    sd_default(s);
    d.format.sample = PSYAU_F32;
    d.format.map = rl;
    CHECK(!open_dev(s, &d));
    CHECK(strstr(psyau_error(&g_au), "channel 0") != NULL);
    d.format.map = lr;
    CHECK(open_dev(s, &d));
    psyau_close(&g_au);
    sd_default(s);
    s->fail_open = 1;
    CHECK(!open_dev(s, NULL));
    CHECK(strstr(psyau_error(&g_au), "refused") != NULL);
    memset(&d, 0, sizeof d);
    d.backend = PSYAU_BACKEND_WASAPI;
    CHECK(!psyau_open(&g_au, &d));   /* PSYAU_NO_MINIAUDIO */
    CHECK(!psyau_open(&g_au, NULL));
}

/* Output formats: one sound at unity gain reproduces 16- and 24-bit values
 * bit for bit. */
static void test_formats(void) {
    sdev* s = &g_sd;
    int outs[3] = { PSYAU_OUT_S16, PSYAU_OUT_S32, PSYAU_OUT_S24 };
    int j;
    for (j = 0; j < 3; j++) {
        psyau_buf b;
        psyau_onset r = { 0 };
        psyau_id id;
        int i, bad = 0;
        double scale = outs[j] == PSYAU_OUT_S16 ? 32768.0 : 8388608.0;
        psyau_desc d;
        memset(&d, 0, sizeof d);
        if (outs[j] == PSYAU_OUT_S16) d.format.sample = PSYAU_S16;
        sd_default(s);
        s->out = (uint16_t)outs[j];
        if (!open_dev(s, &d)) { fail(__LINE__, psyau_error(&g_au)); continue; }
        sd_run_s(s, 1.0);
        for (i = 0; i < 4000; i++) g_buf[i] = (float)((double)((i * 7919) % (int)(2 * scale) - (int)scale) / scale);
        memset(&b, 0, sizeof b);
        b.frames = g_buf; b.n = 4000; b.channels = 1;
        id = psyau_play_at(&g_au, b, g_vt + 80000000);
        sd_run_s(s, 0.3);
        CHECK_I(psyau_result(&g_au, id, &r), PSYAU_OK);
        for (i = 0; i < 4000; i++) if (g_tape[(r.start_frame + i) * 2] != g_buf[i]) bad++;
        CHECK_I(bad, 0);
        CHECK(!(r.flags & PSYAU_ONSET_CLIPPED));
        psyau_close(&g_au);
    }
}

/* Synthesis: levels, ramps, seeds, the noise refusal, the arena. */
static void test_synthesis(void) {
    sdev* s = &g_sd;
    psyau_tone_desc td;
    psyau_noise_desc nd;
    psyau_click_desc cd;
    psyau_buf t, n1, n2;
    double ss = 0, mx = 0;
    int64_t i;
    float pk = 0;
    psyau_id id;
    psyau_onset r = { 0 };
    sd_default(s);
    CHECK(open_dev(s, NULL));
    sd_run_s(s, 1.0);
    CHECK(fabs(psyau_db(-20.0f) - 0.1f) < 1e-6f);
    memset(&td, 0, sizeof td);
    td.hz = 1000; td.dur = PSYAU_MS(100); td.peak = 0.5f; td.ramp = PSYAU_MS(10);
    t = psyau_tone(&g_au, &td);
    CHECK(t.frames && t.n == 4800 && t.channels == 1);
    if (t.frames) {
        CHECK(t.frames[0] == 0.0f);
        for (i = 480; i < 4320; i++) if (fabs(t.frames[i]) > mx) mx = fabs(t.frames[i]);
        CHECK(fabs(mx - 0.5) < 1e-4);
        CHECK(fabs(t.frames[480 + 12]) > 0.4999f);   /* a quarter cycle in */
        CHECK(fabs(t.frames[4799]) < 1e-3f);
    }
    td.hz = 30000;
    CHECK(psyau_tone(&g_au, &td).frames == NULL);
    td.hz = 1000; td.peak = 0;
    CHECK(psyau_tone(&g_au, &td).frames == NULL);
    memset(&nd, 0, sizeof nd);
    nd.dur = PSYAU_S(1); nd.rms = 0.1f; nd.seed = 99;
    n1 = psyau_noise(&g_au, &nd);
    n2 = psyau_noise(&g_au, &nd);
    CHECK(n1.frames && n2.frames);
    if (n1.frames && n2.frames) {
        CHECK(memcmp(n1.frames, n2.frames, sizeof(float) * (size_t)n1.n) == 0);
        for (i = 0; i < n1.n; i++) ss += (double)n1.frames[i] * n1.frames[i];
        CHECK(fabs(sqrt(ss / (double)n1.n) - 0.1) < 0.002);
    }
    nd.seed = 100;
    n2 = psyau_noise(&g_au, &nd);
    CHECK(n2.frames && n2.frames[0] != n1.frames[0]);
    nd.rms = 0.3f;
    CHECK(psyau_noise(&g_au, &nd).frames == NULL);
    CHECK(strstr(psyau_error(&g_au), "peaks at") != NULL);
    printf("synthesis: refusal says: %s\n", psyau_error(&g_au));
    nd.dist = PSYAU_UNIFORM;
    n2 = psyau_noise(&g_au, &nd);
    CHECK(n2.frames != NULL);
    CHECK(psyau_fill_noise(g_buf, 1000, 1, 48000, &nd, &pk) == 0 && pk <= 0.3f * 1.7321f);
    memset(&cd, 0, sizeof cd);
    cd.peak = -0.25f;
    n2 = psyau_click(&g_au, &cd);
    CHECK(n2.frames && n2.n == 1 && n2.frames[0] == -0.25f);
    /* the arena: busy while one of its buffers waits or plays */
    id = psyau_play_at(&g_au, t, g_vt + 80000000);
    CHECK_I(psyau_arena_reset(&g_au), PSYAU_ERR_BUSY);
    sd_run_s(s, 0.4);
    CHECK_I(psyau_result(&g_au, id, &r), PSYAU_OK);
    CHECK_I(psyau_arena_reset(&g_au), 0);
    CHECK(psyau_alloc(&g_au, (int64_t)sizeof g_arena, 1).frames == NULL);   /* too big */
    CHECK(psyau_mix(g_buf, 10, 1, g_buf + 100, 20, 5, 1.0f) == 0);
    psyau_close(&g_au);
}

/* The device on a real thread against the frame thread: plays, cancels and
 * updates race with renders. ThreadSanitizer checks the two queues. */
static uint32_t g_thr_stop = 0;   /* atomic: the header's own helpers */
#if defined(_WIN32)
static unsigned __stdcall thr_main(void* p)
#else
static void* thr_main(void* p)
#endif
{
    sdev* s = (sdev*)p;
    psyau_rt_thread_init(s->host);
    while (!psyau__ld32(&g_thr_stop)) {
        psyau_tick tk;
        memset(&tk, 0, sizeof tk);
        tk.t_entry = (int64_t)psyrt_now_ns();
        tk.pos = s->W > s->L ? s->W - s->L : 0;
        tk.pos_t = tk.t_entry;
        s->render(s->host, s->out_mem, s->period, &tk);
        s->W += s->period;
        psyrt_sleep_until(psyrt_now_ns() + 1000000, 0);
    }
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static void test_threads(void) {
    sdev* s = &g_sd;
    psyau_buf b;
    psyau_id ids[400];
    int i, done = 0, n = 400;
    sd_default(s);
    s->tape = NULL;
    s->period = 48;
    s->L = 96;
    g_virtual = 0;
    CHECK(open_dev(s, NULL));
    memset(&b, 0, sizeof b);
    b.frames = g_buf; b.n = 48; b.channels = 1;
    psyau__st32(&g_thr_stop, 0);
    {
#if defined(_WIN32)
        HANDLE th = (HANDLE)_beginthreadex(NULL, 0, thr_main, s, 0, NULL);
#else
        pthread_t th;
        pthread_create(&th, NULL, thr_main, s);
#endif
        for (i = 0; i < n; i++) {
            ids[i] = psyau_play_at(&g_au, b, 0);
            if (ids[i] == PSYAU_ERR_FULL) { i--; psyrt_sleep_until(psyrt_now_ns() + 200000, 0); continue; }
            if (i % 3 == 0) (void)psyau_cancel(&g_au, ids[i]);
            if (i % 50 == 0) (void)psyau_stop_at(&g_au, 0, 0, 0);
            (void)psyau_update(&g_au);
        }
        for (i = 0; i < 2000 && done < n; i++) {
            int j;
            psyrt_sleep_until(psyrt_now_ns() + 1000000, 0);
            done = 0;
            for (j = 0; j < n; j++) {
                psyau_onset r = { 0 };
                int rc = psyau_result(&g_au, ids[j], &r);
                if (rc == PSYAU_OK || rc == PSYAU_ERR_NOT_FOUND) done++;
            }
        }
        psyau__st32(&g_thr_stop, 1);
#if defined(_WIN32)
        WaitForSingleObject(th, INFINITE);
        CloseHandle(th);
#else
        pthread_join(th, NULL);
#endif
    }
    printf("threads: %d of %d sounds completed\n", done, n);
    CHECK_I(done, n);
    psyau_close(&g_au);
    CHECK_I(g_au.outstanding, 0);
    g_virtual = 1;
}

static void test_misc(void) {
    int n = 0;
    const psyau_param* p = psyau_params(&n);
    CHECK(p && n >= 10);
    CHECK(strcmp(psyau_strerror(PSYAU_ERR_FULL), "ok") != 0);
    CHECK(strcmp(psyau_strerror(0), "ok") == 0);
    CHECK(strcmp(psyau_version(), PSYAU_VERSION_STRING) == 0);
    {
        psyau_buf z;
        memset(&z, 0, sizeof z);
        CHECK_I(psyau_play_at(NULL, z, 0), PSYAU_ERR_ARG);
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    g_tape = (float*)calloc((size_t)TAPE_FRAMES * 2, sizeof(float));
    if (!g_tape) return 2;
    test_plan_and_placement();
    test_fit_drift();
    test_controls();
    test_replan();
    test_xrun();
    test_callback_source();
    test_ring();
    test_limits();
    test_strict_open();
    test_formats();
    test_synthesis();
    test_threads();
    test_misc();
    free(g_tape);
    if (g_failures) { fprintf(stderr, "psy_audio_test: %d failure(s)\n", g_failures); return 1; }
    printf("psy_audio_test: all checks passed\n");
    return 0;
}
