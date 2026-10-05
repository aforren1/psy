/* rt_ring_csv.c - log a frame loop and an audio callback to CSV through
 * psy_rt.h's event ring.
 *
 * Two producers push into one ring. A deadline worker stands in for an
 * audio callback: every 5.33 ms (256 frames at 48 kHz) it pushes an onset
 * record and a plot point. The main thread runs a 60 Hz frame loop: it
 * times its phases, pushes a flip record in the layout psy_screen.h will
 * use, marks the frame, and at the end of every frame drains the ring to
 * CSV. The program is built in PSYRT_TRACE_RING mode, so the zones, the
 * plots and the frame marks go into the same ring, and so do the zones
 * psy_rt.h puts in its own worker and waits.
 *
 * Nothing here needs hardware. The "flip" is a psyrt_sleep_until() to the
 * next 16.67 ms boundary, and the "work" is a spin of a few hundred
 * microseconds per phase.
 *
 * Build (from the repository root):
 *     cc -O2 -pthread -I. -o rt_ring_csv examples/rt_ring_csv.c   # Linux / macOS
 *     cl /O2 /I. examples\rt_ring_csv.c                           # Windows (MSVC)
 *     emcc -O2 -pthread -sPROXY_TO_PTHREAD=1 -sEXIT_RUNTIME=1 -I. \
 *          -o rt_ring_csv.js examples/rt_ring_csv.c && node rt_ring_csv.js
 * or:  cmake -B build && cmake --build build
 *
 * With PSYRT_NO_THREADS there is no worker, and the frame loop pushes the
 * audio records itself. Under Emscripten, write to stdout: a wasm module
 * sees no host file unless it is linked with -sNODERAWFS=1.
 *
 * Usage: rt_ring_csv [frames] [out.csv]
 *     rt_ring_csv              # 120 frames, CSV on stdout
 *     rt_ring_csv 600 log.csv  # 10 s, to a file
 *
 * Columns: seq,t_ns,tid,source,kind,aux,label,v0,v1,v2. label is the zone or
 * plot name, the message text, "flip", "onset" or "loss". v0..v2 depend on
 * the kind: a zone's duration and value, a plot's value, a flip's target,
 * dropped count and busy time, an onset's frame count and lateness, the
 * records a LOSS row says were lost and the running total.
 *
 * Exit code: 0, also when records were lost; the loss is in the CSV and in
 * the summary on stderr.
 */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
/* fopen() is C4996 under /W4 /WX, and fopen_s() is not portable. */
#define _CRT_SECURE_NO_WARNINGS
#endif
#define PSYRT_TRACE_RING
#define PSY_RT_IMPLEMENTATION
#include "psy_rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* This program's own source number and kinds. */
#define SRC_DEMO   PSYRT_SRC_USER
#define KIND_FLIP  1u
#define KIND_ONSET 2u

#define FRAME_NS   16666667ull
#define AUDIO_NS    5333333ull   /* 256 frames at 48 kHz */

static unsigned char g_mem[PSYRT_RING_BYTES(4096)];
static psyrt_ring g_ring;
static uint64_t g_audio_frames;

/* What a real callback would push: the device frame count at the start of
 * the buffer, and how late the callback ran. */
static void push_onset(int64_t late_ns) {
    psyrt_event ev;
    memset(&ev, 0, sizeof(ev));        /* C++-safe; in C a compound literal does */
    ev.source = SRC_DEMO;
    ev.kind = KIND_ONSET;
    ev.u.u64[0] = g_audio_frames;
    ev.u.i64[1] = late_ns;
    (void)psyrt_ring_push(&g_ring, &ev);
    PSYRT_PLOT("audio fill", (double)(g_audio_frames % 1024u) / 1024.0);
    g_audio_frames += 256u;
}

#ifndef PSYRT_NO_THREADS
typedef struct audio_sim {
    psyrt_worker w;
    uint64_t next;
} audio_sim;

/* Re-arms itself: a worker job may submit to its own worker. */
static void audio_cb(void* ctx, const psyrt_job_info* info) {
    audio_sim* a = (audio_sim*)ctx;
    if (info->flushed) return;          /* the stop, not a deadline */
    {
        PSYRT_ZONE(z, "audio callback");
        push_onset(info->late_ns);
        PSYRT_ZONE_END(z);
    }
    a->next += AUDIO_NS;
    (void)psyrt_worker_submit(&a->w, a->next, audio_cb, a);
}
#endif

/* Stand-in for a phase of the frame: spin, and return how long it took. */
static uint32_t phase(uint64_t ns) {
    uint64_t t0 = psyrt_now_ns();
    (void)psyrt_spin_until(t0 + ns);
    return (uint32_t)(psyrt_now_ns() - t0);
}

/* A label in quotes, with quotes doubled, so a message with a comma stays
 * one field. */
static void csv_text(FILE* f, const char* s) {
    fputc('"', f);
    for (; s && *s; s++) {
        if (*s == '"') fputc('"', f);
        fputc(*s, f);
    }
    fputc('"', f);
}

static void csv_row(FILE* f, const psyrt_event* e) {
    fprintf(f, "%lu,%llu,%lu,%u,%u,%lu,", (unsigned long)e->seq,
            (unsigned long long)e->t_ns, (unsigned long)e->tid,
            (unsigned)e->source, (unsigned)e->kind, (unsigned long)e->aux);
    if (e->source == PSYRT_SRC_RT) {
        switch (e->kind) {
        case PSYRT_KIND_ZONE:
            csv_text(f, e->u.zone.loc->name);
            fprintf(f, ",%llu,%llu,\n", (unsigned long long)e->u.zone.dur_ns,
                    (unsigned long long)e->u.zone.value);
            return;
        case PSYRT_KIND_PLOT:
            csv_text(f, e->u.plot.name);
            fprintf(f, ",%.9g,,\n", e->u.plot.value);
            return;
        case PSYRT_KIND_FRAME:
            csv_text(f, e->u.plot.name ? e->u.plot.name : "frame");
            fputs(",,,\n", f);
            return;
        case PSYRT_KIND_MESSAGE:
        case PSYRT_KIND_THREAD_NAME:
            csv_text(f, e->u.text);
            fputs(",,,\n", f);
            return;
        case PSYRT_KIND_LOSS:
            fprintf(f, "\"loss\",%llu,%llu,\n", (unsigned long long)e->u.u64[0],
                    (unsigned long long)e->u.u64[1]);
            return;
        default:
            break;
        }
    } else if (e->source == SRC_DEMO && e->kind == KIND_FLIP) {
        uint64_t busy = 0;
        int i;
        for (i = 0; i < 6; i++) busy += e->u.u32[3 + i];
        fprintf(f, "\"flip\",%llu,%u,%llu\n", (unsigned long long)e->u.u64[0],
                (unsigned)e->u.u16[4], (unsigned long long)busy);
        return;
    } else if (e->source == SRC_DEMO && e->kind == KIND_ONSET) {
        fprintf(f, "\"onset\",%llu,%lld,\n", (unsigned long long)e->u.u64[0],
                (long long)e->u.i64[1]);
        return;
    }
    fprintf(f, "\"\",%llu,%llu,%llu\n", (unsigned long long)e->u.u64[0],
            (unsigned long long)e->u.u64[1], (unsigned long long)e->u.u64[2]);
}

int main(int argc, char** argv) {
    int frames = (argc > 1) ? atoi(argv[1]) : 120;
    FILE* out = stdout;
    psyrt_ring_desc rd;
    psyrt_event ev[64];
    uint64_t target, rows = 0, lost = 0, worst_drain = 0;
    uint16_t dropped = 0;
    int f, n, i;
#ifndef PSYRT_NO_THREADS
    static audio_sim audio;
    psyrt_worker_desc wd;
    int audio_on = 0;
#endif

    if (frames < 1) frames = 1;
    if (argc > 2 && !(out = fopen(argv[2], "w"))) {
        fprintf(stderr, "rt_ring_csv: cannot open %s\n", argv[2]);
        return 1;
    }

    memset(&rd, 0, sizeof(rd));
    rd.memory = g_mem;
    rd.bytes = sizeof(g_mem);
    if (!psyrt_ring_open(&g_ring, &rd)) {
        fprintf(stderr, "rt_ring_csv: %s\n", psyrt_ring_error(&g_ring));
        return 1;
    }
    psyrt_trace_set_ring(&g_ring);
    PSYRT_THREAD_INIT("frame loop");
    fputs("seq,t_ns,tid,source,kind,aux,label,v0,v1,v2\n", out);

    target = psyrt_now_ns() + FRAME_NS;
#ifndef PSYRT_NO_THREADS
    /* No elevation: an example should not ask for FIFO on a shared machine.
     * A rig's audio thread belongs to the audio library anyway. */
    memset(&wd, 0, sizeof(wd));
    wd.no_elevate = true;
    if (psyrt_worker_start(&audio.w, &wd)) {
        audio.next = psyrt_now_ns() + AUDIO_NS;
        audio_on = psyrt_worker_submit(&audio.w, audio.next, audio_cb, &audio) > 0;
    } else {
        fprintf(stderr, "rt_ring_csv: no audio thread (%s); the frame loop "
                        "pushes the onsets\n", psyrt_worker_error(&audio.w));
    }
#endif
    PSYRT_MESSAGEF("session start, %d frames", frames);

    for (f = 0; f < frames; f++) {
        psyrt_event flip;
        uint32_t ph[6];
        uint64_t onset, t0;
        PSYRT_ZONE(fz, "frame");
        PSYRT_ZONE_VALUE(fz, f);
        ph[0] = phase(50000);    /* timeline evaluate */
        ph[1] = phase(100000);   /* script callback   */
        {
            PSYRT_ZONE(dz, "draw");
            ph[2] = phase(400000);
            PSYRT_ZONE_END(dz);
        }
        ph[3] = phase(30000);    /* texture upload    */
        t0 = psyrt_now_ns();
        (void)psyrt_sleep_until(target, PSYRT_DEFAULT_SPIN_NS);   /* "swap" */
        onset = psyrt_now_ns();
        ph[4] = (uint32_t)(onset - t0);
        ph[5] = 0;               /* no GPU timer here */
        while (onset > target + FRAME_NS / 2) {   /* a missed vblank */
            target += FRAME_NS;
            dropped++;
        }

        /* The per-flip record: t_ns = estimated onset, aux = display index,
         * payload = target, dropped, mode, six phase durations in ns. */
        memset(&flip, 0, sizeof(flip));
        flip.t_ns = onset;
        flip.source = SRC_DEMO;
        flip.kind = KIND_FLIP;
        flip.aux = 0;
        flip.u.u64[0] = target;
        flip.u.u16[4] = dropped;
        flip.u.u16[5] = 0;
        for (i = 0; i < 6; i++) flip.u.u32[3 + i] = ph[i];
        (void)psyrt_ring_push(&g_ring, &flip);
        PSYRT_FRAME_MARK();
#ifndef PSYRT_NO_THREADS
        if (!audio_on)
#endif
        {
            push_onset(0);
            push_onset(0);
            push_onset(0);
        }
        PSYRT_ZONE_END(fz);

        /* The drain: once per frame, on the frame thread, after the flip. */
        t0 = psyrt_now_ns();
        while ((n = psyrt_ring_drain(&g_ring, ev, 64)) > 0) {
            for (i = 0; i < n; i++) {
                if (ev[i].source == PSYRT_SRC_RT && ev[i].kind == PSYRT_KIND_LOSS)
                    lost += ev[i].u.u64[0];
                csv_row(out, &ev[i]);
            }
            rows += (uint64_t)n;
            if (n < 64) break;
        }
        if (psyrt_now_ns() - t0 > worst_drain) worst_drain = psyrt_now_ns() - t0;
        target += FRAME_NS;
    }

#ifndef PSYRT_NO_THREADS
    psyrt_worker_stop(&audio.w);
#endif
    psyrt_trace_set_ring(NULL);
    while ((n = psyrt_ring_drain(&g_ring, ev, 64)) > 0) {
        for (i = 0; i < n; i++) csv_row(out, &ev[i]);
        rows += (uint64_t)n;
    }
    if (out != stdout) fclose(out);
    fprintf(stderr, "rt_ring_csv: %d frames, %llu rows, %llu records lost, %u dropped "
                    "frames, slowest drain+write %.0f us\n", frames,
            (unsigned long long)rows, (unsigned long long)lost, (unsigned)dropped,
            (double)worst_drain / 1e3);
    return 0;
}
