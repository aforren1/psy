/* gfx_rdk.c - random dot kinematograms from psy_rdk.h, drawn by psy_gfx.h.
 *
 * Four pages in a 1000 x 760 window, units px:
 *   1  a classic RDK: 300 dots in a 400 px circle. Up and Down change the
 *      coherence by 0.05, Left and Right the direction by 45 deg. N cycles
 *      the noise rule, S the signal rule, E the edge (WRAP or REPLOT: with
 *      REPLOT and 100 % coherence the dots thin out toward the trailing
 *      edge), L the lifetime (none, 0.2 s, 0.5 s), P the presets (WN, MN,
 *      LL, BM, then custom again).
 *   2  the same motion carried by 300 instanced gabors (a global-motion
 *      gabor array): each element rides its dot, its carrier turned and
 *      drifting along the dot's direction at 4 Hz; an element restarts its
 *      carrier when its dot is placed (lifetime 0.4 s). Up, Down, Left,
 *      Right as on page 1.
 *   3  transparent motion: two fields of 150 dots, two streams of one
 *      seed, 180 deg apart; Left and Right change the angle between them.
 *   4  trials on a psy_timeline.h sequence: 1 s of motion at 25.6 %, the
 *      direction turning 90 deg over the last 300 ms (a tween on a channel
 *      bound to the field's direction through psygfx_bind.field), then 300
 *      ms of dynamic noise (a NOISE stimulus with a new seed each frame),
 *      then a blank; every 2 s a new trial with a new seed. Each update's
 *      inputs are logged (rdk.last), and at the end of a trial the log is
 *      replayed into a second field with psyrdk_replay(): the digests must
 *      agree, or the program says so and exits 1.
 * Space, Page Down: next page; Page Up: previous; 1 to 4: that page;
 * Shift+Esc or closing the window: quit. Settings print to stdout and the
 * title bar when they change.
 *
 * Contrast is low and the motion slow, because the display may be
 * somebody's working screen. No calibration: scene values are device
 * values here, and nothing is a measurement of light.
 *
 * Usage: gfx_rdk [--sim] [--page N] [--frames N] [--composition] [--shots PREFIX]
 *   --sim          the simulated display (240 Hz) and the null backend, for
 *                  CI: 30 frames of each page, then two trials of page 4
 *                  with their replays checked
 *   --shots PREFIX show each page for 90 frames, read the output back, write
 *                  PREFIX-pN.ppm, then quit
 * Exit code: 0; 1 when the screen or the gfx did not open, a call failed,
 * or a replay differed; 2 for a bad argument.
 * On Windows set PSYSCR_ANGLE_DIR to ANGLE's directory.
 */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#define PSY_GFX_IMPLEMENTATION
#include "psy_gfx.h"
#define PSY_TIMELINE_IMPLEMENTATION
#include "psy_timeline.h"
#define PSY_RDK_IMPLEMENTATION
#include "psy_rdk.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN_W 1000
#define WIN_H 760
#define N_PAGES 4
#define SIM_FRAMES 30
#define MAX_DOTS 300
#define MAX_STEPS 1024  /* a trial is 2 s: 120 updates at 60 Hz, 480 at 240 */
#define S_NS PSYTL_NS_PER_S

static psyscr_screen scr;
static psygfx_gfx gfx;
static psytl_timeline tl;
static psytl_event tl_events[16];
static psytl_key tl_keys[16];

static psyrdk_field rdk, rdk2, replay;
static psyrdk_desc rdesc, rdesc2;
static psygfx_buf buf, buf2;
static psygfx_stim dots, dots2, gabors, mask, gabor_tmpl;
static psygfx_inst items[MAX_DOTS];
static float xy[2 * MAX_DOTS], xy2[2 * MAX_DOTS];
static float age[MAX_DOTS];
static uint8_t ev[MAX_DOTS];
static float carrier[MAX_DOTS];   /* each element's carrier phase, cycles */

static int page = 0, quit = 0, sim = 0, failures = 0, trials_done = 0;
static int preset = 0, life_choice = 0, transparent_angle = 180;
static uint64_t trial_seed = 1;
static int64_t page_t0, trial_t0, last_t;
static psyrdk_step steps[MAX_STEPS];
static int n_steps;

enum { DOTS_ON, COH, DIR, MASK_ON, N_CH };
enum { TRIAL = 1 };

static psygfx_bind binds[4];

static const char* noise_name[] = { "DIRECTION", "POSITION", "WALK" };
static const char* signal_name[] = { "SAME", "DIFFERENT", "LEAST_RECENT" };
static const char* preset_name[] = { "custom", "WN", "MN", "LL", "BM" };

static void say(const char* what) {
    char line[256];
    snprintf(line, sizeof line, "gfx_rdk %d/%d: %s", page + 1, N_PAGES, what);
    printf("%s\n", line);
    if (psyscr_window(&scr)) SDL_SetWindowTitle(psyscr_window(&scr), line);   /* may allocate: key presses only */
}

static void describe_field(void) {
    char line[200];
    snprintf(line, sizeof line, "%s, coherence %.2f, direction %.0f, noise %s, signal %s, edge %s, lifetime %s",
             preset_name[preset], rdk.coherence, rdk.direction,
             noise_name[preset ? (preset == 4 ? 2 : 1) : rdesc.noise],
             signal_name[preset ? (preset == 3 ? 2 : 1) : rdesc.signal], rdesc.edge ? "REPLOT" : "WRAP",
             life_choice == 0 ? "none" : life_choice == 1 ? "0.2 s" : "0.5 s");
    say(line);
}

/* Opens the main field from rdesc, keeping the bound floats. Setup and key
 * presses only: it allocates. */
static int reopen(psyrdk_field* f, psyrdk_desc* d, int64_t t) {
    float c = f->coherence, dir = f->direction, sp = f->speed;
    int had = f->n > 0;
    psyrdk_close(f);
    if (psyrdk_open(f, d) < 0) { fprintf(stderr, "gfx_rdk: %s\n", psyrdk_error(f)); failures++; return -1; }
    if (had) { f->coherence = c; f->direction = dir; f->speed = sp; }
    psyrdk_start(f, trial_seed, t);
    return 0;
}

static void apply_page1_desc(void) {
    static const psyrdk_algorithm algs[5] = { PSYRDK_CUSTOM, PSYRDK_WN, PSYRDK_MN, PSYRDK_LL, PSYRDK_BM };
    rdesc.algorithm = algs[preset];
    if (preset) { rdesc.signal = PSYRDK_SIGNAL_SAME; rdesc.select = PSYRDK_EXACT; rdesc.noise = PSYRDK_NOISE_DIRECTION; rdesc.sets = 0; }
    rdesc.lifetime = life_choice == 0 ? 0.0f : life_choice == 1 ? 0.2f : 0.5f;
    /* MN and LL show a third of the dots on each frame: 100 shown */
    rdesc.count = (preset == 2 || preset == 3) ? MAX_DOTS / 3 : MAX_DOTS;
}

static void start_page(int p, const psyscr_frame* f) {
    page = p;
    page_t0 = f->onset;
    trial_seed++;
    memset(&rdesc, 0, sizeof rdesc);
    rdesc.w = 400; rdesc.coherence = 0.5f; rdesc.direction = 0; rdesc.speed = 80; rdesc.count = MAX_DOTS;
    preset = 0; life_choice = 0;
    if (p == 1) { rdesc.lifetime = 0.4f; rdesc.w = 500; }
    if (p == 2) { rdesc.count = MAX_DOTS / 2; rdesc.coherence = 1; rdesc.signal = PSYRDK_SIGNAL_SAME; }
    if (p == 3) { rdesc.coherence = 0.256f; rdesc.signal = PSYRDK_SIGNAL_DIFFERENT; rdesc.noise = PSYRDK_NOISE_POSITION; }
    rdk.n = 0;
    reopen(&rdk, &rdesc, f->onset);
    if (p == 2) {
        rdesc2 = rdesc;
        rdesc2.stream = 1;
        rdk2.n = 0;
        reopen(&rdk2, &rdesc2, f->onset);
        rdk2.direction = rdk.direction + (float)transparent_angle;
    }
    psyrdk_close(&replay);
    if (p == 3) {
        if (psyrdk_open(&replay, &rdesc) < 0) { fprintf(stderr, "gfx_rdk: %s\n", psyrdk_error(&replay)); failures++; }
        trial_t0 = f->onset;
        psytl_anchor(&tl, TRIAL, f->onset, 0);
        n_steps = 0;
    }
    memset(carrier, 0, sizeof carrier);
    if (p == 0) describe_field();
    else if (p == 1) say("global-motion gabor array: Up, Down coherence; Left, Right direction");
    else if (p == 2) say("transparent motion: Left, Right change the angle between the fields");
    else say("trials: 25.6 % coherence, the direction turns 90 deg in the last 300 ms, then dynamic noise");
}

static void key(SDL_Keycode k, const psyscr_frame* f) {
    char line[96];
    if (k == SDLK_SPACE || k == SDLK_PAGEDOWN) { start_page((page + 1) % N_PAGES, f); return; }
    if (k == SDLK_PAGEUP) { start_page((page + N_PAGES - 1) % N_PAGES, f); return; }
    if (k >= SDLK_1 && k < SDLK_1 + N_PAGES) { start_page((int)(k - SDLK_1), f); return; }
    if (page == 3) return;
    if (k == SDLK_UP || k == SDLK_DOWN) {
        float c = rdk.coherence + (k == SDLK_UP ? 0.05f : -0.05f);
        rdk.coherence = c < 0 ? 0 : c > 1 ? 1 : c;
    } else if (k == SDLK_LEFT || k == SDLK_RIGHT) {
        if (page == 2) {
            transparent_angle = (transparent_angle + (k == SDLK_RIGHT ? 45 : 315)) % 360;
            rdk2.direction = rdk.direction + (float)transparent_angle;
            snprintf(line, sizeof line, "transparent motion: %d deg between the fields", transparent_angle);
            say(line);
            return;
        }
        rdk.direction = fmodf(rdk.direction + (k == SDLK_RIGHT ? 45.0f : 315.0f), 360.0f);
    } else if (page == 0 && (k == SDLK_N || k == SDLK_S || k == SDLK_E || k == SDLK_L || k == SDLK_P)) {
        if (k == SDLK_P) preset = (preset + 1) % 5;
        if (k == SDLK_L) life_choice = (life_choice + 1) % 3;
        if (k == SDLK_N) { preset = 0; rdesc.noise = (psyrdk_noise)((rdesc.noise + 1) % 3); }
        if (k == SDLK_S) { preset = 0; rdesc.signal = (psyrdk_signal)((rdesc.signal + 1) % 3); }
        if (k == SDLK_E) rdesc.edge = (psyrdk_edge)((rdesc.edge + 1) % 2);
        if (!preset) rdesc.algorithm = PSYRDK_CUSTOM;
        apply_page1_desc();
        rdesc.coherence = rdk.coherence; rdesc.direction = rdk.direction;
        reopen(&rdk, &rdesc, f->onset);
        dots.count = (uint32_t)rdk.n;
    } else {
        return;
    }
    if (page == 0) describe_field();
    else {
        snprintf(line, sizeof line, "coherence %.2f, direction %.0f", rdk.coherence, rdk.direction);
        say(line);
    }
}

static int setup(void) {
    psytl_desc td;
    psytl_seq q;
    psygfx_dots_desc dd;
    psygfx_gabor_desc gd;
    psygfx_noise_desc nd;
    psygfx_instances_desc id;
    psytl_tween_desc turn;
    static const float white[3] = { 0.85f, 0.85f, 0.85f };

    memset(&td, 0, sizeof td);
    td.events = tl_events; td.event_capacity = 16; td.n_channels = N_CH;
    td.keys = tl_keys; td.key_capacity = 16;
    if (!psytl_open(&tl, &td)) { fprintf(stderr, "gfx_rdk: %s\n", psytl_error(&tl)); return -1; }
    /* The trial, in the order it plays (GSAP: tl.set().to()...). */
    q = psytl_seq_on(&tl, TRIAL);
    psytl_set_value(&q, COH, 0.256f);
    psytl_on(&q, DOTS_ON);
    psytl_wait(&q, PSYTL_MS(700));
    memset(&turn, 0, sizeof turn);
    turn.from = 0; turn.from_set = true; turn.to = 90; turn.duration = PSYTL_MS(300); turn.ease = PSYTL_EASE_COSINE;
    psytl_then(&q, DIR, &turn);
    psytl_off(&q, DOTS_ON);
    psytl_on(&q, MASK_ON);
    psytl_wait(&q, PSYTL_MS(300));
    psytl_off(&q, MASK_ON);
    if (q.err < 0) { fprintf(stderr, "gfx_rdk: the trial's sequence: %s at call %d\n", psytl_strerror(q.err), q.err_call); return -1; }

    memset(&rdesc, 0, sizeof rdesc);
    rdesc.w = 400; rdesc.count = MAX_DOTS; rdesc.coherence = 0.5f; rdesc.speed = 80;
    if (psyrdk_open(&rdk, &rdesc) < 0) { fprintf(stderr, "gfx_rdk: %s\n", psyrdk_error(&rdk)); return -1; }
    buf = psygfx_buffer(&gfx, sizeof xy);
    buf2 = psygfx_buffer(&gfx, sizeof xy2);
    if (!buf.id || !buf2.id) { fprintf(stderr, "gfx_rdk: %s\n", psygfx_error(&gfx)); return -1; }

    memset(&dd, 0, sizeof dd);
    dd.buf = buf; dd.count = MAX_DOTS; dd.dot_size = 5; dd.edge = PSYGFX_EDGE_COSINE; dd.edge_width = 1.5f;
    memcpy(dd.color, white, sizeof dd.color);
    dd.aperture = PSYGFX_CIRCLE; dd.w = 410;   /* the field's own edge keeps every center inside */
    dots = psygfx_dots(&dd);
    dd.buf = buf2;
    dots2 = psygfx_dots(&dd);

    memset(&gd, 0, sizeof gd);
    gd.sf = 1 / 10.0f; gd.sigma = 5; gd.contrast = 0.35f;
    gabor_tmpl = psygfx_gabor(&gd);
    memset(&id, 0, sizeof id);
    id.inst = items; id.n = MAX_DOTS; id.fields = PSYGFX_I_XY | PSYGFX_I_ORI | PSYGFX_I_PHASE;
    psygfx_inst_grid(items, MAX_DOTS, 1, 0, 0);
    gabors = psygfx_instances(&gfx, &gabor_tmpl, &id);
    if (gabors.n_inst == 0) { fprintf(stderr, "gfx_rdk: instances: %s\n", psygfx_error(&gfx)); return -1; }

    memset(&nd, 0, sizeof nd);
    nd.w = 410; nd.h = 410; nd.check = 4; nd.contrast = 0.25f; nd.aperture = PSYGFX_CIRCLE;
    mask = psygfx_noise(&nd);

    memset(binds, 0, sizeof binds);
    binds[0].stim = &dots; binds[0].param = PSYGFX_P_VISIBLE; binds[0].channel = DOTS_ON;
    binds[1].field = &rdk.coherence; binds[1].channel = COH;
    binds[2].field = &rdk.direction; binds[2].channel = DIR;
    binds[3].stim = &mask; binds[3].param = PSYGFX_P_VISIBLE; binds[3].channel = MASK_ON;
    return 0;
}

static int write_ppm(const char* path, uint8_t* rgba, int w, int h) {
    FILE* fp = fopen(path, "wb");
    int i, ok;
    if (!fp) return -1;
    for (i = 0; i < w * h; i++) {   /* RGBA to RGB in place: each write is behind its read */
        rgba[3 * i] = rgba[4 * i]; rgba[3 * i + 1] = rgba[4 * i + 1]; rgba[3 * i + 2] = rgba[4 * i + 2];
    }
    fprintf(fp, "P6\n%d %d\n255\n", w, h);
    ok = fwrite(rgba, 3, (size_t)w * (size_t)h, fp) == (size_t)w * (size_t)h;
    return fclose(fp) == 0 && ok ? 0 : -1;
}

static int upload(psygfx_buf b, const psyrdk_field* f, float* out) {
    if (psyrdk_xy(f, out) < 0) return -1;
    return psygfx_buffer_update(&gfx, b, 0, out, (size_t)f->n * 2 * sizeof(float));
}

/* Page 4: at the trial's end, replay its log into another field. */
static void end_trial(const psyscr_frame* f) {
    uint64_t want = psyrdk_digest(&rdk);
    if (psyrdk_replay(&replay, trial_seed, trial_t0, steps, n_steps) < 0) {
        fprintf(stderr, "gfx_rdk: replay: %s\n", psyrdk_error(&replay));
        failures++;
    } else if (psyrdk_digest(&replay) != want) {
        fprintf(stderr, "gfx_rdk: the replay of trial %llu differs (%016llx, live %016llx)\n",
                (unsigned long long)trial_seed, (unsigned long long)psyrdk_digest(&replay), (unsigned long long)want);
        failures++;
    } else {
        printf("trial seed %llu: %d updates, digest %016llx, replayed the same\n", (unsigned long long)trial_seed, n_steps,
               (unsigned long long)want);
    }
    trials_done++;
    trial_seed++;
    trial_t0 = f->onset;
    psytl_anchor(&tl, TRIAL, f->onset, 0);   /* base time 0 again: the trial replays */
    psyrdk_start(&rdk, trial_seed, f->onset);
    n_steps = 0;
}

static int frame(const psyscr_frame* f) {
    int rc = 0, i;
    if (page == 3) {
        if (f->onset - trial_t0 >= 2 * S_NS || n_steps >= MAX_STEPS) end_trial(f);
        psytl_evaluate(&tl, &(psytl_frame){ f->onset, f->period, f->index }, NULL, 0);
        psygfx_apply(binds, 4, psytl_values(&tl));
        mask.seed = (float)(uint32_t)(f->index & 0xffffff);   /* a new pattern each frame */
    }
    psyscr_mark(&scr, PSYSCR_PHASE_EVALUATE);
    if (psyrdk_update(&rdk, f->onset) < 0) return -1;
    if (page == 3) steps[n_steps++] = rdk.last;
    if (page == 2 && psyrdk_update(&rdk2, f->onset) < 0) return -1;
    if (page == 1) {
        /* elements ride the dots; the carrier drifts along each dot's
         * direction at 4 Hz and restarts when the dot is placed */
        psyrdk_out o;
        double dt = last_t ? (double)(f->onset - last_t) * 1e-9 : 0;
        memset(&o, 0, sizeof o);
        o.xy = &items[0].x; o.xy_stride = sizeof items[0];
        o.dir = &items[0].ori; o.dir_stride = sizeof items[0];
        o.event = ev; o.age = age;
        if (psyrdk_write(&rdk, &o) < 0) return -1;
        for (i = 0; i < rdk.n; i++) {
            carrier[i] = (ev[i] & PSYRDK_EV_PLACED) ? 0.0f : (float)fmod(carrier[i] - 4.0 * dt, 1.0);
            items[i].phase = carrier[i];   /* cos(2 pi (sf x + phase)): a falling phase moves it along +x */
        }
    } else {
        rc |= upload(buf, &rdk, xy);
        if (page == 2) rc |= upload(buf2, &rdk2, xy2);
    }
    last_t = f->onset;
    psyscr_mark(&scr, PSYSCR_PHASE_UPLOAD);
    dots.count = (uint32_t)rdk.n;
    dots2.count = (uint32_t)rdk2.n;
    psygfx_begin(&gfx, f);
    if (page == 1) rc |= psygfx_draw(&gfx, &gabors);
    else rc |= psygfx_draw(&gfx, &dots);
    if (page == 2) rc |= psygfx_draw(&gfx, &dots2);
    if (page == 3) rc |= psygfx_draw(&gfx, &mask);
    rc |= psygfx_end(&gfx);
    if (rc < 0) { fprintf(stderr, "gfx_rdk: %s\n", psygfx_error(&gfx)); return -1; }
    return 0;
}

int main(int argc, char** argv) {
    psyscr_desc sd;
    psygfx_desc gd;
    psyscr_frame f;
    int i, start = 0, on_page = 0, started = 0;
    int64_t frames = -1;
    const char* shots = NULL;
    uint8_t* shot_px = NULL;
    float scr_w, scr_h;
    memset(&sd, 0, sizeof sd);
    sd.windowed = true; sd.window_w = WIN_W; sd.window_h = WIN_H;
    sd.sim_period_ns = 4166667;   /* 240 Hz when --sim */
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--sim")) { sd.backend = PSYSCR_BACKEND_SIM; sim = 1; }
        else if (!strcmp(argv[i], "--composition")) sd.backend = PSYSCR_BACKEND_COMPOSITION;
        else if (!strcmp(argv[i], "--page") && i + 1 < argc) { start = atoi(argv[++i]) - 1; if (start < 0 || start >= N_PAGES) start = -1; }
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) { frames = atoll(argv[++i]); if (frames < 1) start = -1; }
        else if (!strcmp(argv[i], "--shots") && i + 1 < argc) shots = argv[++i];
        else start = -1;
        if (start < 0) {
            fprintf(stderr, "usage: gfx_rdk [--sim] [--page 1..%d] [--frames N] [--composition] [--shots PREFIX]\n", N_PAGES);
            return 2;
        }
    }
    if (!psyscr_open(&scr, &sd)) { fprintf(stderr, "gfx_rdk: %s\n", psyscr_error(&scr)); return 1; }
    memset(&gd, 0, sizeof gd);
    gd.screen = &scr;
    gd.background[0] = gd.background[1] = gd.background[2] = 0.18f;
    gd.width = WIN_W; gd.height = WIN_H;   /* the simulated display's size */
    if (!psygfx_open(&gfx, &gd)) { fprintf(stderr, "gfx_rdk: %s\n", psygfx_error(&gfx)); psyscr_close(&scr); return 1; }
    if (setup() < 0) { psygfx_close(&gfx); psyscr_close(&scr); return 1; }
    psygfx_size(&gfx, &scr_w, &scr_h);
    if (shots) shot_px = (uint8_t*)malloc((size_t)scr_w * (size_t)scr_h * 4);

    while (psyscr_begin(&scr, &f) == PSYSCR_OK) {
        if (psyscr_window(&scr)) {
            SDL_Event e;
            while (psyscr_poll(&scr, &e, NULL)) {   /* psyscr_begin() reports Shift+Esc and close itself */
                if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) quit = 1;
                else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) { key(e.key.key, &f); on_page = 0; }
            }
        }
        if (!started) { start_page(start, &f); started = 1; }
        if (frame(&f) < 0) { failures++; break; }
        if (shot_px && on_page == 89) {   /* the back buffer, before the flip */
            char path[512];
            snprintf(path, sizeof path, "%s-p%d.ppm", shots, page + 1);
            if (psygfx_read_output(&gfx, 0, 0, (int)scr_w, (int)scr_h, shot_px) < 0 ||
                write_ppm(path, shot_px, (int)scr_w, (int)scr_h) < 0)
                fprintf(stderr, "gfx_rdk: could not write %s\n", path);
            else
                printf("wrote %s\n", path);
        }
        psyscr_flip(&scr);
        on_page++;
        if (shots && on_page >= 90) {
            if (page == N_PAGES - 1) break;
            start_page(page + 1, &f);
            on_page = 0;
        }
        if (sim && (page == 3 ? trials_done >= 2 : on_page >= SIM_FRAMES)) {
            if (page == N_PAGES - 1 || (page + 1) % N_PAGES == start) break;
            start_page(page + 1, &f);
            on_page = 0;
        }
        if (quit || (frames > 0 && f.index + 1 >= frames)) break;
    }
    psyrdk_close(&rdk);
    psyrdk_close(&rdk2);
    psyrdk_close(&replay);
    free(shot_px);
    psygfx_close(&gfx);
    psyscr_close(&scr);
    if (failures) { fprintf(stderr, "gfx_rdk: %d failures\n", failures); return 1; }
    return 0;
}
