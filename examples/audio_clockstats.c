/* audio_clockstats.c - record psy_audio.h's device clock and callback on this
 * machine. Plays silence only.
 *
 *     audio_clockstats [--null] [--device NAME] [--exclusive] [--period N]
 *                      [--seconds S] [--load N] [--frame-work MS]
 *                      [--voices N] [--stall-at S --stall MS]
 *                      [--allocs | --allocs-control] [--csv PREFIX]
 *
 *   --device NAME  the playback device whose name contains NAME
 *   --exclusive    WASAPI exclusive mode; at most 30 s, because every other
 *                  program is silent meanwhile
 *   --period N     frames per callback asked for (default: the header's)
 *   --seconds S    run length (default 10)
 *   --load N       N threads spin at normal priority
 *   --frame-work MS  a 60 Hz frame loop on the main thread (elevated), which
 *                  spins MS ms per frame and hands a silent 10 ms sound to
 *                  psyau_play_at() each frame; its psyau cost is measured
 *   --voices N     N silent sounds looping for the whole run (mixer cost)
 *   --stall-at S --stall MS  once, S seconds in, the callback sleeps MS ms:
 *                  an injected underrun
 *   --allocs       count C runtime heap calls after the warm-up (MSVC debug
 *                  build) and miniaudio's own; --allocs-control adds one
 *                  malloc per callback, which the count must see
 *   --csv PREFIX   PREFIX_ticks.csv (one row per callback) and
 *                  PREFIX_ring.csv (every ring record)
 *
 * Prints the describe line and a summary: callback intervals, underruns,
 * GetPosition and render cost, the fit, the frame thread's cost.
 *
 * Exit: 0 when the run completed, 1 when the device did not open, 2 usage.
 */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "psy_rt.h"

typedef struct cs_tick {
    int64_t t_entry, pos, pos_t, w, t_hook, t_end;
} cs_tick;

#define CS_MAX 400000
static cs_tick g_ticks[CS_MAX];
static volatile long g_nticks = 0;
static int g_allocs_control = 0;

static void cs_on_tick(const void* au, const void* tk);
static void cs_on_end(void);
#define PSYAU__ON_TICK(au, tk) cs_on_tick((const void*)(au), (const void*)(tk))
#define PSYAU__ON_RENDER_END(au) cs_on_end()

#define PSY_AUDIO_IMPLEMENTATION
#include "psy_audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <crtdbg.h>
#include <process.h>
#else
#include <pthread.h>
#endif

static void cs_on_tick(const void* aup, const void* tkp) {
    const psyau_audio* au = (const psyau_audio*)aup;
    const psyau_tick* tk = (const psyau_tick*)tkp;
    long i = g_nticks;
    if (i >= CS_MAX) return;
    g_ticks[i].t_entry = tk->t_entry;
    g_ticks[i].pos = tk->pos;
    g_ticks[i].pos_t = tk->pos_t;
    g_ticks[i].w = au->w;
    g_ticks[i].t_hook = (int64_t)psyrt_now_ns();
}
static void cs_on_end(void) {
    long i = g_nticks;
    if (i >= CS_MAX) return;
    g_ticks[i].t_end = (int64_t)psyrt_now_ns();
    g_nticks = i + 1;
    if (g_allocs_control) free(malloc(16));
}

/* --- the injected stall, as an Audio source ---------------------------------- */
static int64_t g_stall_at = 0, g_stall_ns = 0;
static void stall_render(void* ctx, float* out, int32_t frames, const psyau_clock* clk) {
    (void)ctx; (void)out; (void)frames; (void)clk;
    if (g_stall_ns && (int64_t)psyrt_now_ns() >= g_stall_at) {
        int64_t ns = g_stall_ns;
        g_stall_ns = 0;
        psyrt_sleep_until(psyrt_now_ns() + (uint64_t)ns, 0);
    }
}
static const psyau_source g_stall_src = { PSYAU_SOURCE_VERSION, "stall", stall_render };

/* --- load ---------------------------------------------------------------------- */
static volatile int g_stop = 0;
static volatile unsigned long g_sink = 0;
#if defined(_WIN32)
static unsigned __stdcall spin(void* p) { (void)p; while (!g_stop) g_sink++; return 0; }
#else
static void* spin(void* p) { (void)p; while (!g_stop) g_sink++; return NULL; }
#endif

/* --- allocation hook --------------------------------------------------------------- */
#if defined(_WIN32) && defined(_DEBUG)
static volatile long g_crt = 0;
static int g_counting = 0;
static int __cdecl alloc_hook(int type, void* d, size_t n, int bt, long r, const unsigned char* f, int l) {
    (void)type; (void)d; (void)n; (void)bt; (void)r; (void)f; (void)l;
    if (g_counting) InterlockedIncrement(&g_crt);
    return 1;
}
#endif

static int cmp_i64(const void* a, const void* b) {
    int64_t x = *(const int64_t*)a, y = *(const int64_t*)b;
    return x < y ? -1 : x > y;
}
static int64_t pct(int64_t* v, long n, double p) {
    long k;
    if (n <= 0) return 0;
    k = (long)(p * (double)(n - 1) + 0.5);
    return v[k];
}

static psyau_audio au;
static unsigned char g_ring_mem[PSYRT_RING_BYTES(65536)];
static psyrt_ring g_ring;
static psyrt_event g_ev[1024];
static int64_t g_tmp[CS_MAX];
static float g_silence[48000 * 2];
static int64_t g_frame_cost[60 * 700];

int main(int argc, char** argv) {
    psyau_desc d;
    const char* csv = NULL;
    double seconds = 10, stall_at = 0, frame_work = -1;
    int i, load = 0, voices = 0, allocs = 0;
    long n, nframes = 0;
    char line[512];
    FILE* fr = NULL;
    uint32_t ma0 = 0;
    memset(&d, 0, sizeof d);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--null")) d.backend = PSYAU_BACKEND_NULL;
        else if (!strcmp(argv[i], "--device") && i + 1 < argc) d.device = argv[++i];
        else if (!strcmp(argv[i], "--exclusive")) d.exclusive = true;
        else if (!strcmp(argv[i], "--period") && i + 1 < argc) d.period = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--load") && i + 1 < argc) load = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--frame-work") && i + 1 < argc) frame_work = atof(argv[++i]);
        else if (!strcmp(argv[i], "--voices") && i + 1 < argc) voices = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--stall-at") && i + 1 < argc) stall_at = atof(argv[++i]);
        else if (!strcmp(argv[i], "--stall") && i + 1 < argc) g_stall_ns = (int64_t)(atof(argv[++i]) * 1e6);
        else if (!strcmp(argv[i], "--allocs")) allocs = 1;
        else if (!strcmp(argv[i], "--allocs-control")) { allocs = 1; g_allocs_control = 1; }
        else if (!strcmp(argv[i], "--csv") && i + 1 < argc) csv = argv[++i];
        else { fprintf(stderr, "audio_clockstats: unknown argument %s (see the top of the file)\n", argv[i]); return 2; }
    }
    if (d.exclusive && seconds > 30) { fprintf(stderr, "audio_clockstats: --exclusive runs at most 30 s\n"); return 2; }
    if (seconds <= 0 || seconds > 660 || voices < 0 || voices > 32 || load < 0 || load > 32) { fprintf(stderr, "audio_clockstats: bad --seconds, --voices or --load\n"); return 2; }
    psyrt_ring_open(&g_ring, &(psyrt_ring_desc){ .memory = g_ring_mem, .bytes = sizeof g_ring_mem });
    d.ring = &g_ring;
    d.arena_bytes = 1 << 16;
    if (allocs) { static float arena[1 << 14]; d.arena = arena; d.arena_bytes = sizeof arena; }
    if (g_stall_ns) {
        d.source = &g_stall_src;
        g_stall_at = (int64_t)psyrt_now_ns() + (int64_t)(stall_at * 1e9);   /* from before open */
    }
    else g_stall_ns = 0;
#if defined(_WIN32) && defined(_DEBUG)
    /* open with the caller's arena: the C runtime calls during open, minus
     * miniaudio's own (it allocates through the header's callbacks), are
     * the header's */
    if (allocs) { _CrtSetAllocHook(alloc_hook); g_counting = 1; }
#endif
    if (!psyau_open(&au, &d)) { fprintf(stderr, "%s\n", psyau_error(&au)); return 1; }
#if defined(_WIN32) && defined(_DEBUG)
    if (allocs) {
        g_counting = 0;
        printf("allocs at open: C runtime %ld, of which miniaudio %u\n", (long)g_crt,
               (unsigned)((psyau__ma*)(void*)au.backend_mem)->allocs);
        g_crt = 0;
    }
#endif
    for (i = 0; i < voices; i++) {
        psyau_play_desc pd;
        memset(&pd, 0, sizeof pd);
        pd.buf.frames = g_silence; pd.buf.n = 48000; pd.buf.channels = 2;
        pd.loops = PSYAU_FOREVER;
        if (psyau_play(&au, &pd) <= 0) { fprintf(stderr, "voice %d refused\n", i); }
    }
    for (i = 0; i < load; i++) {
#if defined(_WIN32)
        _beginthreadex(NULL, 0, spin, NULL, 0, NULL);
#else
        pthread_t th; pthread_create(&th, NULL, spin, NULL); pthread_detach(th);
#endif
    }
    if (csv) {
        char path[512];
        snprintf(path, sizeof path, "%s_ring.csv", csv);
        fr = fopen(path, "w");
        if (fr) fprintf(fr, "source,kind,t_ns,aux,i0,i1,i2,i3,f1,f2,u6,u7,u16_18,u16_19\n");
    }
    {
        int64_t t_end = (int64_t)psyrt_now_ns() + (int64_t)(seconds * 1e9);
        int64_t next = (int64_t)psyrt_now_ns();
        psyrt_policy pol = frame_work >= 0 ? psyrt_thread_elevate(NULL) : PSYRT_POLICY_NONE;
        (void)pol;
        if (allocs) {
            ma0 = ((psyau__ma*)(void*)au.backend_mem)->allocs;
#if defined(_WIN32) && defined(_DEBUG)
            _CrtSetAllocHook(alloc_hook);
            g_counting = 1;
#endif
        }
        while ((int64_t)psyrt_now_ns() < t_end) {
            int k;
            next += 16666667;
            psyrt_sleep_until((uint64_t)next, PSYRT_DEFAULT_SPIN_NS);
            if (frame_work >= 0) {
                int64_t a = (int64_t)psyrt_now_ns(), b;
                psyau_buf sb;
                memset(&sb, 0, sizeof sb);
                sb.frames = g_silence; sb.n = 480; sb.channels = 2;
                (void)psyau_play_at(&au, sb, a + psyau_lead_ns(&au) + PSYAU_MS(20));
                (void)psyau_update(&au);
                b = (int64_t)psyrt_now_ns();
                if (nframes < (long)(sizeof g_frame_cost / sizeof g_frame_cost[0])) g_frame_cost[nframes++] = b - a;
                if (frame_work > 0) psyrt_spin_until((uint64_t)(a + (int64_t)(frame_work * 1e6)));
            } else {
                (void)psyau_update(&au);
            }
            while ((k = psyrt_ring_drain(&g_ring, g_ev, 1024)) > 0) {
                int j;
                if (!fr) continue;
                for (j = 0; j < k; j++) {
                    const psyrt_event* e = &g_ev[j];
                    fprintf(fr, "%u,%u,%llu,%u,%lld,%lld,%lld,%lld,%.9f,%.3f,%u,%u,%u,%u\n",
                            (unsigned)e->source, (unsigned)e->kind, (unsigned long long)e->t_ns, (unsigned)e->aux,
                            (long long)e->u.i64[0], (long long)e->u.i64[1], (long long)e->u.i64[2], (long long)e->u.i64[3],
                            e->u.f64[1], e->u.f64[2], (unsigned)e->u.u32[6], (unsigned)e->u.u32[7],
                            (unsigned)e->u.u16[18], (unsigned)e->u.u16[19]);
                }
            }
        }
#if defined(_WIN32) && defined(_DEBUG)
        g_counting = 0;
#endif
    }
    psyau_describe(&au, line, sizeof line);
    g_stop = 1;
    if (allocs) {
        uint32_t ma1 = ((psyau__ma*)(void*)au.backend_mem)->allocs;
#if defined(_WIN32) && defined(_DEBUG)
        printf("allocs: C runtime heap calls during the run %ld, miniaudio %u, callbacks %ld, frames %ld\n",
               (long)g_crt, (unsigned)(ma1 - ma0), (long)g_nticks, nframes);
#else
        printf("allocs: miniaudio %u (C runtime count needs an MSVC debug build)\n", (unsigned)(ma1 - ma0));
#endif
    }
    psyau_close(&au);
    if (fr) fclose(fr);
    puts(line);
    n = g_nticks;
    if (csv) {
        char path[512];
        FILE* ft;
        snprintf(path, sizeof path, "%s_ticks.csv", csv);
        ft = fopen(path, "w");
        if (ft) {
            fprintf(ft, "t_entry,pos,pos_t,w,getpos_ns,render_ns\n");
            for (i = 0; i < n; i++)
                fprintf(ft, "%lld,%lld,%lld,%lld,%lld,%lld\n", (long long)g_ticks[i].t_entry,
                        (long long)g_ticks[i].pos, (long long)g_ticks[i].pos_t, (long long)g_ticks[i].w,
                        (long long)(g_ticks[i].t_hook - g_ticks[i].t_entry),
                        (long long)(g_ticks[i].t_end - g_ticks[i].t_hook));
            fclose(ft);
        }
    }
    if (n > 2) {
        long m = 0;
        for (i = 1; i < n; i++) g_tmp[m++] = g_ticks[i].t_entry - g_ticks[i - 1].t_entry;
        qsort(g_tmp, (size_t)m, sizeof g_tmp[0], cmp_i64);
        printf("callbacks %ld; interval us p1 %.1f p50 %.1f p99 %.1f p99.9 %.1f max %.1f\n", n,
               pct(g_tmp, m, 0.01) / 1e3, pct(g_tmp, m, 0.5) / 1e3, pct(g_tmp, m, 0.99) / 1e3,
               pct(g_tmp, m, 0.999) / 1e3, g_tmp[m - 1] / 1e3);
        m = 0;
        for (i = 0; i < n; i++) g_tmp[m++] = g_ticks[i].t_hook - g_ticks[i].t_entry;
        qsort(g_tmp, (size_t)m, sizeof g_tmp[0], cmp_i64);
        printf("GetPosition us p50 %.2f p99 %.2f max %.2f\n", pct(g_tmp, m, 0.5) / 1e3, pct(g_tmp, m, 0.99) / 1e3, g_tmp[m - 1] / 1e3);
        m = 0;
        for (i = 0; i < n; i++) g_tmp[m++] = g_ticks[i].t_end - g_ticks[i].t_hook;
        qsort(g_tmp, (size_t)m, sizeof g_tmp[0], cmp_i64);
        {
            double mean = 0;
            for (i = 0; i < m; i++) mean += (double)g_tmp[i];
            mean /= (double)m;
            printf("render us mean %.2f p50 %.2f p99 %.2f max %.2f (%d voices)\n", mean / 1e3,
                   pct(g_tmp, m, 0.5) / 1e3, pct(g_tmp, m, 0.99) / 1e3, g_tmp[m - 1] / 1e3, voices);
        }
    }
    if (nframes > 0) {
        double mean = 0;
        for (i = 0; i < nframes; i++) mean += (double)g_frame_cost[i];
        mean /= (double)nframes;
        qsort(g_frame_cost, (size_t)nframes, sizeof g_frame_cost[0], cmp_i64);
        printf("frame thread play_at+update us mean %.2f p99 %.2f max %.2f (%ld frames)\n", mean / 1e3,
               pct(g_frame_cost, nframes, 0.99) / 1e3, g_frame_cost[nframes - 1] / 1e3, nframes);
    }
    return 0;
}
