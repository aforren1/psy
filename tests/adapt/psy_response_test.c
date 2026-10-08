/* psy_response_test.c - self-checking test for psy_response.h. No framework:
 * it returns 0 when every check passed and 1 after printing each failure.
 *
 *     gcc -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Werror -I. \
 *         -o response_test tests/adapt/psy_response_test.c -lm
 *
 * Every case is a synthetic stream of inputs and onsets with the result it
 * must give: no display, no SDL library. With PSYRSP_TEST_SDL defined and
 * SDL3's headers on the include path, the SDL adapter is checked too, on
 * SDL_Event structs filled by hand (it calls no SDL function, so nothing is
 * linked). psy_screen.h, psy_audio.h and psy_timeline.h are included for
 * their structs only, for the onset helpers.
 */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#if defined(PSYRSP_TEST_SDL)
#include <SDL3/SDL_events.h>
#endif
#include "psy_screen.h"
#include "psy_audio.h"
#include "psy_timeline.h"
#define PSY_RESPONSE_IMPLEMENTATION
#include "psy_response.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- harness */

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond) do { g_checks++; if (!(cond)) { \
    fprintf(stderr, "psy_response_test: FAIL at line %d: %s\n", __LINE__, #cond); g_failures++; } } while (0)
#define CHECK_I(got, want) do { long long g_ = (long long)(got), w_ = (long long)(want); g_checks++; \
    if (g_ != w_) { fprintf(stderr, "psy_response_test: FAIL at line %d: %s (got %lld, want %lld)\n", \
                            __LINE__, #got, g_, w_); g_failures++; } } while (0)
#define CHECK_D(got, want) do { double g_ = (got), w_ = (want); g_checks++; \
    if (!(fabs(g_ - w_) <= 1e-12)) { fprintf(stderr, "psy_response_test: FAIL at line %d: %s (got %.12g, " \
                                             "want %.12g)\n", __LINE__, #got, g_, w_); g_failures++; } } while (0)
#define CHECK_S(got, want) do { const char* g_ = (got); const char* w_ = (want); g_checks++; \
    if (!g_ || !w_ || strcmp(g_, w_) != 0) { fprintf(stderr, "psy_response_test: FAIL at line %d: %s " \
        "(got [%s], want [%s])\n", __LINE__, #got, g_ ? g_ : "(null)", w_ ? w_ : "(null)"); g_failures++; } } while (0)
#define CHECK_HAS(str, sub) do { const char* s_ = (str); g_checks++; \
    if (!s_ || !strstr(s_, (sub))) { fprintf(stderr, "psy_response_test: FAIL at line %d: [%s] lacks [%s]\n", \
                                             __LINE__, s_ ? s_ : "(null)", (sub)); g_failures++; } } while (0)

#define MS(x) ((int64_t)(x) * 1000000)
#define SC_F 9u
#define SC_J 13u
#define SC_K 14u

static psyrsp_collector g_c;

static psyrsp_input ev(int kind, int type, int64_t t, uint32_t control) {
    psyrsp_input in;
    memset(&in, 0, sizeof in);
    in.kind = (uint8_t)kind;
    in.type = (uint8_t)type;
    in.t = t;
    in.control = control;
    in.value = type == PSYRSP_PRESS ? 1.0f : 0.0f;
    if (kind == PSYRSP_KIND_KEYBOARD) in.code = control == SC_F ? 'f' : control == SC_J ? 'j' : 'k';
    return in;
}

static int key(int64_t t, uint32_t sc, int down) {
    psyrsp_input in = ev(PSYRSP_KIND_KEYBOARD, down ? PSYRSP_PRESS : PSYRSP_RELEASE, t, sc);
    return psyrsp_feed(&g_c, &in);
}

static int key_repeat(int64_t t, uint32_t sc) {
    psyrsp_input in = ev(PSYRSP_KIND_KEYBOARD, PSYRSP_PRESS, t, sc);
    in.flags = PSYRSP_IN_REPEAT;
    return psyrsp_feed(&g_c, &in);
}

static int tap(int64_t t, uint32_t sc) {
    key(t, sc, 1);
    return key(t + MS(80), sc, 0);
}

static int sample(int kind, int64_t t, uint32_t control, float v, float x, float y) {
    psyrsp_input in = ev(kind, PSYRSP_SAMPLE, t, control);
    in.value = v;
    in.x = x;
    in.y = y;
    return psyrsp_feed(&g_c, &in);
}

static int onset(int64_t t, int final) {
    psyrsp_onset o;
    memset(&o, 0, sizeof o);
    o.t = t;
    o.final = (uint8_t)final;
    o.tier = (uint8_t)(final ? 1 : 0);
    o.src = (uint8_t)(final ? PSYRSP_ONSET_FLIP : PSYRSP_ONSET_PLAN);
    o.frame = 42;
    o.residual = 1234;
    return psyrsp_set_onset(&g_c, &o);
}

static const psyrsp_choice g_fj[] = { { "f", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
                                      { "j", "right", 0, 0, 0, 0, 0, 0, 0, 0, 0 } };

static psyrsp_desc fj_desc(double duration, double min_rt) {
    psyrsp_desc d;
    memset(&d, 0, sizeof d);
    d.choices = g_fj;
    d.n_choices = 2;
    d.duration = duration;
    d.minimum_valid_rt = min_rt;
    return d;
}

static psyrsp_result finish(void) {
    psyrsp_result r;
    CHECK_I(psyrsp_finish(&g_c, &r), PSYRSP_ENDED);
    return r;
}

/* ------------------------------------------------------------------ cases */

static void check_names(void) {
    CHECK_I(psyrsp_scancode("f"), 9);
    CHECK_I(psyrsp_scancode("F"), 9);
    CHECK_I(psyrsp_scancode("a"), 4);
    CHECK_I(psyrsp_scancode("z"), 29);
    CHECK_I(psyrsp_scancode("1"), 30);
    CHECK_I(psyrsp_scancode("0"), 39);
    CHECK_I(psyrsp_scancode(" "), 44);
    CHECK_I(psyrsp_scancode("Space"), 44);
    CHECK_I(psyrsp_scancode("ArrowLeft"), 80);
    CHECK_I(psyrsp_scancode("left"), 80);
    CHECK_I(psyrsp_scancode("Enter"), 40);
    CHECK_I(psyrsp_scancode("/"), 56);
    CHECK_I(psyrsp_scancode("f12"), 69);
    CHECK_I(psyrsp_scancode("ff"), -1);
    CHECK_I(psyrsp_scancode(""), -1);
    CHECK_I(psyrsp_scancode(NULL), -1);
    CHECK_S(psyrsp_key_name(9), "f");
    CHECK_S(psyrsp_key_name(39), "0");
    CHECK_S(psyrsp_key_name(44), "space");
    CHECK_S(psyrsp_key_name(80), "left");
    CHECK_S(psyrsp_key_name(40), "enter");
    CHECK(psyrsp_key_name(200) == NULL);
    CHECK_S(psyrsp_version(), PSYRSP_VERSION_STRING);
}

static void check_validation(void) {
    psyrsp_desc d;
    psyrsp_choice bad[2];
    psyrsp_source src;
    psyrsp_input trace[4];
    memset(bad, 0, sizeof bad);
    d = fj_desc(1.5, 0.1);
    CHECK(psyrsp_init(&g_c, &d));
    CHECK_S(psyrsp_error(&g_c), "");
    d = fj_desc(1.5, 0.1);
    d.n_choices = PSYRSP_MAX_CHOICES + 1;
    CHECK(!psyrsp_init(&g_c, &d));
    CHECK_HAS(psyrsp_error(&g_c), "n_choices");
    d = fj_desc(1.5, 0.1);
    d.choices = NULL;
    CHECK(!psyrsp_init(&g_c, &d));
    d = fj_desc(-1, 0);
    CHECK(!psyrsp_init(&g_c, &d));
    CHECK_HAS(psyrsp_error(&g_c), "duration");
    d = fj_desc(1.5, 0.1);
    d.minimum_valid_rt = (double)NAN;
    CHECK(!psyrsp_init(&g_c, &d));
    d = fj_desc(0.5, 0.5);
    CHECK(!psyrsp_init(&g_c, &d));
    CHECK_HAS(psyrsp_error(&g_c), "minimum_valid_rt");
    d = fj_desc(0, 0.5);                       /* no deadline: any minimum */
    CHECK(psyrsp_init(&g_c, &d));
    d = fj_desc(1.001, 0);                     /* 1.001 x 1e9 is 1000999999.99... */
    CHECK(psyrsp_init(&g_c, &d));
    CHECK_I(g_c.duration_ns, 1001000000);
    d = fj_desc(1, 0);
    d.mode = PSYRSP_CHOICES_NONE;
    CHECK(!psyrsp_init(&g_c, &d));
    CHECK_HAS(psyrsp_error(&g_c), "NONE");
    d = fj_desc(1, 0);
    d.mode = 7;
    CHECK(!psyrsp_init(&g_c, &d));
    memset(&d, 0, sizeof d);
    d.mode = PSYRSP_CHOICES_LIST;
    CHECK(!psyrsp_init(&g_c, &d));
    /* key names */
    memset(&d, 0, sizeof d);
    bad[0].key = "ff";
    d.choices = bad;
    d.n_choices = 1;
    CHECK(!psyrsp_init(&g_c, &d));
    CHECK_HAS(psyrsp_error(&g_c), "'ff'");
    bad[0].key = "f";
    bad[0].control = 13;
    CHECK(!psyrsp_init(&g_c, &d));
    CHECK_HAS(psyrsp_error(&g_c), "scancode 9");
    bad[0].control = 9;
    CHECK(psyrsp_init(&g_c, &d));
    bad[0].key = NULL;
    bad[0].control = 0;
    CHECK(!psyrsp_init(&g_c, &d));
    CHECK_HAS(psyrsp_error(&g_c), "no key");
    bad[0].match = PSYRSP_MATCH_KEYCODE;
    CHECK(!psyrsp_init(&g_c, &d));
    bad[0].key = "f";
    bad[0].code = 'g';
    CHECK(!psyrsp_init(&g_c, &d));
    bad[0].code = 0;
    CHECK(psyrsp_init(&g_c, &d));
    CHECK_I(g_c.ch[0].code, 'f');
    bad[0].kind = PSYRSP_KIND_BOX;
    CHECK(!psyrsp_init(&g_c, &d));            /* a key name on a box */
    /* crossings */
    memset(bad, 0, sizeof bad);
    bad[0].kind = PSYRSP_KIND_GAMEPAD;
    bad[0].cross = PSYRSP_CROSS_RISING;
    bad[0].level = (float)NAN;
    CHECK(!psyrsp_init(&g_c, &d));
    bad[0].level = 0.5f;
    bad[0].hysteresis = -0.1f;
    CHECK(!psyrsp_init(&g_c, &d));
    bad[0].hysteresis = 0.1f;
    CHECK(psyrsp_init(&g_c, &d));
    bad[0].kind = PSYRSP_KIND_KEYBOARD;
    CHECK(!psyrsp_init(&g_c, &d));
    bad[0].kind = PSYRSP_KIND_MOUSE;
    bad[0].cross = PSYRSP_CROSS_DISTANCE;
    bad[0].level = 0;
    CHECK(!psyrsp_init(&g_c, &d));
    bad[0].cross = 9;
    CHECK(!psyrsp_init(&g_c, &d));
    bad[0].cross = 0;
    bad[0].kind = 16;
    CHECK(!psyrsp_init(&g_c, &d));
    /* sources, trace, counts */
    d = fj_desc(1, 0);
    memset(&src, 0, sizeof src);
    src.lo_us = 5;
    src.hi_us = 1;
    d.sources = &src;
    d.n_sources = 1;
    CHECK(!psyrsp_init(&g_c, &d));
    d.n_sources = PSYRSP_MAX_SOURCES + 1;
    CHECK(!psyrsp_init(&g_c, &d));
    d = fj_desc(1, 0);
    d.trace_cap = 4;
    CHECK(!psyrsp_init(&g_c, &d));
    d.trace = trace;
    CHECK(psyrsp_init(&g_c, &d));
    d.trace_cap = -1;
    CHECK(!psyrsp_init(&g_c, &d));
    d = fj_desc(1, 0);
    d.max_responses = -1;
    CHECK(!psyrsp_init(&g_c, &d));
    d = fj_desc(1, 0);
    d.dedup = (double)NAN;
    CHECK(!psyrsp_init(&g_c, &d));
    CHECK(!psyrsp_init(&g_c, NULL));
    CHECK(!psyrsp_init(NULL, &d));
    /* a collector that failed init refuses calls */
    CHECK_I(psyrsp_feed(&g_c, NULL), PSYRSP_ERR_STATE);
    {
        psyrsp_input in = ev(0, PSYRSP_PRESS, 0, SC_F);
        psyrsp_result r;
        CHECK_I(psyrsp_feed(&g_c, &in), PSYRSP_ERR_STATE);
        CHECK_I(psyrsp_update(&g_c, 0), PSYRSP_ERR_STATE);
        CHECK_I(psyrsp_finish(&g_c, &r), PSYRSP_ERR_STATE);
        d = fj_desc(1, 0);
        CHECK(psyrsp_init(&g_c, &d));
        in.type = 0;
        CHECK_I(psyrsp_feed(&g_c, &in), PSYRSP_ERR_ARG);
        in.type = 9;
        CHECK_I(psyrsp_feed(&g_c, &in), PSYRSP_ERR_ARG);
        in.type = PSYRSP_PRESS;
        in.kind = 16;
        CHECK_I(psyrsp_feed(&g_c, &in), PSYRSP_ERR_ARG);
        CHECK_I(psyrsp_finish(&g_c, NULL), PSYRSP_ERR_ARG);
        CHECK_I(psyrsp_set_onset(&g_c, NULL), PSYRSP_ERR_ARG);
    }
}

/* The basic trial: rt from the onset, the first press ends it. */
static void check_basic(void) {
    psyrsp_desc d = fj_desc(1.5, 0.1);
    psyrsp_result r;
    const psyrsp_entry* e;
    int n;
    CHECK(psyrsp_init(&g_c, &d));
    CHECK_I(key(MS(1), SC_F, 1), PSYRSP_IDLE);          /* before arm: held state only */
    CHECK_I(key(MS(50), SC_F, 0), PSYRSP_IDLE);
    psyrsp_arm(&g_c, MS(100));
    CHECK_I(psyrsp_update(&g_c, MS(5000)), PSYRSP_OPEN); /* no onset, no deadline */
    CHECK_I(onset(MS(600), 0), PSYRSP_OPEN);
    CHECK_I(onset(MS(601), 1), PSYRSP_OPEN);
    CHECK_I(key(MS(1001), SC_J, 1), PSYRSP_ENDED);
    CHECK_I(key(MS(1101), SC_J, 0), PSYRSP_ENDED);
    r = finish();
    CHECK_D(r.rt, 0.4);
    CHECK_D(r.rt_key_duration, 0.1);
    CHECK_I(r.response, 1);
    CHECK_S(r.response_name, "right");
    CHECK_I(r.control, SC_J);
    CHECK_I(r.code, 'j');
    CHECK_I(r.t_response, MS(1001));
    CHECK_I(r.t_onset, MS(601));
    CHECK_I(r.onset_tier, 1);
    CHECK_I(r.onset_src, PSYRSP_ONSET_FLIP);
    CHECK_I(r.onset_frame, 42);
    CHECK_I(r.onset_residual, 1234);
    CHECK_I(r.flags, PSYRSP_R_RESPONDED);
    CHECK_I(r.n_responses, 1);
    e = psyrsp_entries(&g_c, &n);
    CHECK_I(n, 1);
    CHECK_I(e[0].flags, PSYRSP_E_VALID | PSYRSP_E_RELEASED | PSYRSP_E_ENDED);
    /* the name defaults to the key */
    psyrsp_arm(&g_c, MS(2000));
    onset(MS(2100), 1);
    tap(MS(2400), SC_F);
    r = finish();
    CHECK_S(r.response_name, "f");
    CHECK_I(r.response, 0);
}

static void check_bounds(void) {
    psyrsp_desc d = fj_desc(1.5, 0.1);
    psyrsp_result r;
    const psyrsp_entry* e;
    int n;
    CHECK(psyrsp_init(&g_c, &d));
    d.persist = true;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    tap(MS(500), SC_F);                                  /* before the onset arrives: EARLY */
    onset(MS(1000), 1);
    tap(MS(1000) + MS(100) - 1, SC_F);                   /* 1 ns short of the minimum */
    tap(MS(1000) + MS(300), SC_J);
    tap(MS(1000) + MS(1500) - 1, SC_F);                  /* last ns of the window */
    tap(MS(1000) + MS(1500), SC_J);                      /* at the deadline: LATE */
    tap(MS(1000) - 1, SC_J);                             /* just before the onset */
    r = finish();
    e = psyrsp_entries(&g_c, &n);
    CHECK_I(n, 6);
    CHECK_I(e[0].flags & PSYRSP_E_CLASS_MASK, PSYRSP_E_EARLY);
    CHECK_I(e[1].flags & PSYRSP_E_CLASS_MASK, PSYRSP_E_ANTICIPATION);
    CHECK_I(e[2].flags & PSYRSP_E_CLASS_MASK, PSYRSP_E_VALID);
    CHECK_I(e[3].flags & PSYRSP_E_CLASS_MASK, PSYRSP_E_VALID);
    CHECK_I(e[4].flags & PSYRSP_E_CLASS_MASK, PSYRSP_E_LATE);
    CHECK_I(e[5].flags & PSYRSP_E_CLASS_MASK, PSYRSP_E_EARLY);
    CHECK_I(r.n_responses, 2);
    CHECK_I(r.n_anticipations, 1);
    CHECK_I(r.n_early, 2);
    CHECK_I(r.n_late, 1);
    CHECK_D(r.rt, 0.3);
    /* exactly at the minimum is valid; and a negative rt is EARLY */
    d = fj_desc(1.5, 0.1);
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(1000), 1);
    CHECK_I(key(MS(1100), SC_F, 1), PSYRSP_ENDED);
    r = finish();
    CHECK_D(r.rt, 0.1);
    /* duration 0: no deadline */
    d = fj_desc(0, 0);
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(10), 1);
    CHECK_I(psyrsp_update(&g_c, MS(100000)), PSYRSP_OPEN);
    CHECK_I(key(MS(90000), SC_F, 1), PSYRSP_ENDED);
    r = finish();
    CHECK_D(r.rt, 89.99);
    CHECK(!(r.flags & PSYRSP_R_TIMEOUT));
    /* no deadline and no response: no timeout either */
    key(MS(90100), SC_F, 0);
    psyrsp_arm(&g_c, MS(200000));
    onset(MS(200010), 1);
    r = finish();
    CHECK_I(r.flags, 0);
}

static void check_deadline(void) {
    psyrsp_desc d = fj_desc(1.0, 0);
    psyrsp_result r;
    d.settle = 0.020;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    CHECK_I(psyrsp_update(&g_c, MS(3000)), PSYRSP_OPEN);   /* no onset yet */
    onset(MS(500), 1);
    CHECK_I(psyrsp_update(&g_c, MS(1519)), PSYRSP_OPEN);
    CHECK_I(psyrsp_update(&g_c, MS(1520)), PSYRSP_ENDED);
    /* a press stamped before the deadline, delivered after ENDED: valid */
    CHECK_I(key(MS(1499), SC_F, 1), PSYRSP_ENDED);
    key(MS(1550), SC_J, 1);                                 /* late */
    r = finish();
    CHECK_D(r.rt, 0.999);
    CHECK_I(r.n_late, 1);
    CHECK(r.flags & PSYRSP_R_RESPONDED);
    /* no response: timeout, NaN */
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(500), 1);
    CHECK_I(psyrsp_update(&g_c, MS(1600)), PSYRSP_ENDED);
    r = finish();
    CHECK(r.rt != r.rt);
    CHECK(r.rt_key_duration != r.rt_key_duration);
    CHECK_I(r.response, -1);
    CHECK(r.response_name == NULL);
    CHECK_I(r.flags, PSYRSP_R_TIMEOUT);
    /* update after finish stays ENDED; a later press is logged, late */
    CHECK_I(psyrsp_update(&g_c, MS(9999)), PSYRSP_ENDED);
    tap(MS(1700), SC_F);
    r = finish();
    CHECK_I(r.n_entries, 1);
    CHECK_I(r.n_late, 1);
    CHECK_I(r.n_responses, 0);
    /* finish() before any end: every later press is AFTER_END */
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(500), 1);
    r = finish();
    CHECK_I(key(MS(700), SC_F, 1), PSYRSP_ENDED);
    r = finish();
    CHECK_I(r.n_after_end, 1);
    CHECK_I(r.n_responses, 0);
    /* a report delivered after finish(), stamped before the deadline */
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(500), 1);
    psyrsp_update(&g_c, MS(1600));
    r = finish();
    CHECK(r.flags & PSYRSP_R_TIMEOUT);
    key(MS(1400), SC_J, 1);
    r = finish();
    CHECK_D(r.rt, 0.9);
}

static void check_ending(void) {
    psyrsp_desc d = fj_desc(2.0, 0.1);
    psyrsp_result r;
    const psyrsp_entry* e;
    int n;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    CHECK_I(key(MS(150), SC_F, 1), PSYRSP_OPEN);           /* anticipation: does not end */
    key(MS(160), SC_F, 0);
    CHECK_I(key(MS(500), SC_J, 1), PSYRSP_ENDED);
    key(MS(600), SC_J, 0);
    tap(MS(700), SC_F);                                     /* after the end */
    r = finish();
    e = psyrsp_entries(&g_c, &n);
    CHECK_I(n, 3);
    CHECK(e[2].flags & PSYRSP_E_AFTER_END);
    CHECK(!(e[2].flags & PSYRSP_E_VALID));
    CHECK_I(r.n_responses, 1);
    CHECK_I(r.n_after_end, 1);
    CHECK_I(r.n_anticipations, 1);
    CHECK_I(r.response, 1);
    /* persist: three responses, the first is reported */
    d.persist = true;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    CHECK_I(tap(MS(400), SC_F), PSYRSP_OPEN);
    CHECK_I(tap(MS(800), SC_J), PSYRSP_OPEN);
    CHECK_I(tap(MS(1200), SC_F), PSYRSP_OPEN);
    r = finish();
    CHECK_I(r.n_responses, 3);
    CHECK_D(r.rt, 0.3);
    CHECK_I(r.response, 0);
    /* persist with max_responses 2: ends at the second */
    d.max_responses = 2;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    CHECK_I(tap(MS(400), SC_F), PSYRSP_OPEN);
    CHECK_I(key(MS(800), SC_J, 1), PSYRSP_ENDED);
    tap(MS(1200), SC_F);
    r = finish();
    CHECK_I(r.n_responses, 2);
    CHECK_I(r.n_after_end, 1);
    e = psyrsp_entries(&g_c, &n);
    CHECK(e[1].flags & PSYRSP_E_ENDED);
    /* max_responses 1 with persist is the default trial */
    d.max_responses = 1;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    CHECK_I(key(MS(400), SC_F, 1), PSYRSP_ENDED);
}

static void check_modes(void) {
    psyrsp_desc d;
    psyrsp_result r;
    psyrsp_input in;
    const psyrsp_entry* e;
    int n;
    static const psyrsp_choice kc[] = { { "z", NULL, 0, PSYRSP_MATCH_KEYCODE, 0, 0, 0, 0, 0, 0, 0 } };
    static const psyrsp_choice boxes[] = { { NULL, "left", PSYRSP_KIND_BOX, 0, 0, 0, 2, 1, 0, 0, 0 },
                                           { NULL, "right", PSYRSP_KIND_BOX, 0, 0, 0, 0, 2, 0, 0, 0 } };
    /* ALL with kinds 0: the keyboard only, a click is no response */
    memset(&d, 0, sizeof d);
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    in = ev(PSYRSP_KIND_MOUSE, PSYRSP_PRESS, MS(200), 1);
    CHECK_I(psyrsp_feed(&g_c, &in), PSYRSP_OPEN);
    CHECK_I(key(MS(300), 77, 1), PSYRSP_ENDED);
    r = finish();
    CHECK_I(r.n_not_choice, 1);
    CHECK_I(r.response, -1);
    CHECK_I(r.control, 77);
    CHECK_D(r.rt, 0.2);
    /* ALL with every kind */
    d.kinds = PSYRSP_KINDS_ALL;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    CHECK_I(psyrsp_feed(&g_c, &in), PSYRSP_ENDED);
    r = finish();
    CHECK_I(r.kind, PSYRSP_KIND_MOUSE);
    /* every kind but SYNC: a scanner pulse is logged, not a response */
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    in = ev(PSYRSP_KIND_SYNC, PSYRSP_PRESS, MS(150), 1);
    CHECK_I(psyrsp_feed(&g_c, &in), PSYRSP_OPEN);
    in = ev(PSYRSP_KIND_MOUSE, PSYRSP_PRESS, MS(200), 1);
    CHECK_I(psyrsp_feed(&g_c, &in), PSYRSP_ENDED);
    r = finish();
    CHECK_I(r.kind, PSYRSP_KIND_MOUSE);
    CHECK_I(r.n_not_choice, 1);
    /* the SYNC bit named: the pulse is the response (wait for the scanner) */
    d.kinds = PSYRSP_KIND_BIT(PSYRSP_KIND_SYNC);
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    CHECK_I(key(MS(120), SC_F, 1), PSYRSP_OPEN);
    in = ev(PSYRSP_KIND_SYNC, PSYRSP_PRESS, MS(150), 1);
    CHECK_I(psyrsp_feed(&g_c, &in), PSYRSP_ENDED);
    r = finish();
    CHECK_I(r.kind, PSYRSP_KIND_SYNC);
    /* every bit but one named by hand is a mask, not "every kind" */
    d.kinds = (uint16_t)(PSYRSP_KINDS_ALL & ~PSYRSP_KIND_BIT(PSYRSP_KIND_PEN));
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    CHECK_I(psyrsp_feed(&g_c, &in), PSYRSP_ENDED);
    in = ev(PSYRSP_KIND_MOUSE, PSYRSP_PRESS, MS(200), 1);
    /* one kind bit */
    d.kinds = PSYRSP_KIND_BIT(PSYRSP_KIND_MOUSE);
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    CHECK_I(key(MS(150), SC_F, 1), PSYRSP_OPEN);
    CHECK_I(psyrsp_feed(&g_c, &in), PSYRSP_ENDED);
    /* NONE: logged, never a response, runs to the deadline */
    memset(&d, 0, sizeof d);
    d.mode = PSYRSP_CHOICES_NONE;
    d.duration = 1;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    CHECK_I(tap(MS(300), SC_F), PSYRSP_OPEN);
    CHECK_I(psyrsp_update(&g_c, MS(1100)), PSYRSP_ENDED);
    r = finish();
    CHECK_I(r.n_not_choice, 1);
    CHECK_I(r.flags, PSYRSP_R_TIMEOUT);
    /* KEYCODE: the layout's z is on the US y key (QWERTZ) */
    memset(&d, 0, sizeof d);
    d.choices = kc;
    d.n_choices = 1;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    in = ev(PSYRSP_KIND_KEYBOARD, PSYRSP_PRESS, MS(200), 29);   /* US z key, types y */
    in.code = 'y';
    CHECK_I(psyrsp_feed(&g_c, &in), PSYRSP_OPEN);
    in = ev(PSYRSP_KIND_KEYBOARD, PSYRSP_PRESS, MS(300), 28);   /* US y key, types z */
    in.code = 'z';
    CHECK_I(psyrsp_feed(&g_c, &in), PSYRSP_ENDED);
    r = finish();
    CHECK_I(r.control, 28);
    CHECK_I(r.code, 'z');
    CHECK_S(r.response_name, "z");
    /* scancode (default): the US f key whatever it types */
    d = fj_desc(1, 0);
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    in = ev(PSYRSP_KIND_KEYBOARD, PSYRSP_PRESS, MS(200), SC_F);
    in.code = 'u';                                              /* Dvorak */
    in.mods = 0x0001;
    CHECK_I(psyrsp_feed(&g_c, &in), PSYRSP_ENDED);
    e = psyrsp_entries(&g_c, &n);
    CHECK_I(e[0].code, 'u');
    CHECK_I(e[0].control, SC_F);
    CHECK_I(e[0].mods, 1);
    /* box buttons with a device filter */
    memset(&d, 0, sizeof d);
    d.choices = boxes;
    d.n_choices = 2;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    in = ev(PSYRSP_KIND_BOX, PSYRSP_PRESS, MS(200), 1);
    in.device = 3;                                              /* button 1 of the wrong box */
    CHECK_I(psyrsp_feed(&g_c, &in), PSYRSP_OPEN);
    in = ev(PSYRSP_KIND_BOX, PSYRSP_PRESS, MS(300), 2);
    in.device = 3;                                              /* button 2, any box */
    CHECK_I(psyrsp_feed(&g_c, &in), PSYRSP_ENDED);
    r = finish();
    CHECK_S(r.response_name, "right");
    CHECK_I(r.n_not_choice, 1);
    CHECK_I(r.device, 3);
}

static void check_held(void) {
    psyrsp_desc d = fj_desc(2.0, 0);
    psyrsp_result r;
    const psyrsp_entry* e;
    int n;
    CHECK(psyrsp_init(&g_c, &d));
    key(MS(10), SC_F, 1);                                  /* down before the window */
    psyrsp_arm(&g_c, MS(100));
    onset(MS(200), 1);
    CHECK_I(key_repeat(MS(600), SC_F), PSYRSP_OPEN);       /* its OS repeats: no response */
    CHECK_I(key_repeat(MS(633), SC_F), PSYRSP_OPEN);
    CHECK_I(key(MS(700), SC_F, 1), PSYRSP_OPEN);           /* R2: down again, no up: held */
    key(MS(800), SC_F, 0);
    CHECK_I(key(MS(900), SC_F, 1), PSYRSP_ENDED);          /* a new press */
    r = finish();
    CHECK(r.flags & PSYRSP_R_HELD_AT_OPEN);
    CHECK_I(r.n_held, 3);
    CHECK_D(r.rt, 0.7);
    /* a repeat whose press was never seen (the window got the focus while
     * the key was down) is held too */
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, MS(100));
    onset(MS(200), 1);
    CHECK_I(key_repeat(MS(600), SC_F), PSYRSP_OPEN);
    r = finish();
    CHECK_I(r.n_held, 1);
    CHECK_I(r.n_entries, 0);
    /* allow_held_key: the press is the response; its repeats are the same
     * press, and the release closes them all */
    d.allow_held_key = true;
    d.persist = true;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, MS(100));
    onset(MS(200), 1);
    key(MS(500), SC_F, 1);
    key_repeat(MS(1000), SC_F);
    key_repeat(MS(1033), SC_F);
    key(MS(1200), SC_F, 0);
    r = finish();
    CHECK_D(r.rt, 0.3);
    CHECK_D(r.rt_key_duration, 0.7);
    e = psyrsp_entries(&g_c, &n);
    CHECK_I(n, 3);
    CHECK_I(e[2].t_release, MS(1200));
    d.persist = false;
    /* allow_held_key: the first repeat is the response, flagged HELD */
    d.allow_held_key = true;
    CHECK(psyrsp_init(&g_c, &d));
    key(MS(10), SC_F, 1);
    psyrsp_arm(&g_c, MS(100));
    onset(MS(200), 1);
    CHECK_I(key_repeat(MS(600), SC_F), PSYRSP_ENDED);
    r = finish();
    e = psyrsp_entries(&g_c, &n);
    CHECK(e[0].flags & PSYRSP_E_HELD);
    CHECK_D(r.rt, 0.4);
    CHECK(r.flags & PSYRSP_R_HELD_AT_OPEN);
    /* a held key that is no choice: not logged without allow_held_key */
    d.allow_held_key = false;
    CHECK(psyrsp_init(&g_c, &d));
    key(MS(10), SC_K, 1);
    psyrsp_arm(&g_c, MS(100));
    onset(MS(200), 1);
    key_repeat(MS(600), SC_K);
    r = finish();
    CHECK_I(r.n_entries, 0);
    CHECK(!(r.flags & PSYRSP_R_HELD_AT_OPEN));
    /* the held state survives arm(): released before the next window */
    CHECK(psyrsp_init(&g_c, &d));
    key(MS(10), SC_J, 1);
    key(MS(50), SC_J, 0);
    psyrsp_arm(&g_c, MS(100));
    onset(MS(200), 1);
    CHECK_I(key(MS(400), SC_J, 1), PSYRSP_ENDED);
    r = finish();
    CHECK(!(r.flags & PSYRSP_R_HELD_AT_OPEN));
    /* a box button held across windows */
    {
        static const psyrsp_choice b1[] = { { NULL, NULL, PSYRSP_KIND_BOX, 0, 0, 0, 0, 1, 0, 0, 0 } };
        psyrsp_input in = ev(PSYRSP_KIND_BOX, PSYRSP_PRESS, MS(10), 1);
        memset(&d, 0, sizeof d);
        d.choices = b1;
        d.n_choices = 1;
        CHECK(psyrsp_init(&g_c, &d));
        in.device = 7;
        psyrsp_feed(&g_c, &in);
        psyrsp_arm(&g_c, MS(100));
        r = finish();
        CHECK(r.flags & PSYRSP_R_HELD_AT_OPEN);
        psyrsp_arm(&g_c, MS(200));
        onset(MS(300), 1);
        in.t = MS(400);
        CHECK_I(psyrsp_feed(&g_c, &in), PSYRSP_OPEN);        /* held */
        in.device = 8;                                        /* another box's button 1 */
        in.t = MS(500);
        CHECK_I(psyrsp_feed(&g_c, &in), PSYRSP_ENDED);
    }
}

/* R3 and the measured patterns of SDL 3.4's double report. */
static void check_double(void) {
    psyrsp_desc d = fj_desc(2.0, 0);
    psyrsp_result r;
    const psyrsp_entry* e;
    psyrsp_input in;
    int n;
    d.persist = true;
    CHECK(psyrsp_init(&g_c, &d));
    CHECK_I(g_c.dedup_ns, MS(50));
    /* a virtual-key tap: down up (raw), down up (message) 9 ms later */
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    key(MS(500), SC_F, 1);
    key(MS(500) + 30000, SC_F, 0);
    key(MS(509), SC_F, 1);
    key(MS(509) + 30000, SC_F, 0);
    r = finish();
    e = psyrsp_entries(&g_c, &n);
    CHECK_I(n, 2);
    CHECK_I(r.n_responses, 1);
    CHECK_I(r.n_duplicates, 1);
    CHECK(e[1].flags & PSYRSP_E_DUPLICATE);
    CHECK(!(e[1].flags & PSYRSP_E_RELEASED));
    CHECK_D(r.rt_key_duration, 0.00003);
    CHECK_I(g_c.n_stray, 1);
    /* the second report stamped earlier (message tick): the first is kept */
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    key(MS(500), SC_F, 1);
    key(MS(495), SC_F, 1);
    key(MS(600), SC_F, 0);
    r = finish();
    CHECK_D(r.rt, 0.4);
    CHECK_I(r.n_duplicates, 1);
    CHECK_D(r.rt_key_duration, 0.1);
    /* a 100 ms hold: SDL marks the second report a repeat */
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    key(MS(500), SC_F, 1);
    key_repeat(MS(511), SC_F);
    key(MS(600), SC_F, 0);
    r = finish();
    CHECK_I(r.n_responses, 1);
    CHECK_I(r.n_held, 1);
    CHECK_I(r.n_duplicates, 0);
    /* the bounds: 49.999999 ms merged, 50 ms two presses */
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    tap(MS(1000), SC_F);
    key(MS(1050) - 1, SC_F, 1);
    key(MS(1050) - 1 + MS(80), SC_F, 0);
    tap(MS(1500), SC_J);
    tap(MS(1550), SC_J);
    r = finish();
    CHECK_I(r.n_duplicates, 1);
    CHECK_I(r.n_responses, 3);
    /* two different keys 2 ms apart both count */
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    key(MS(500), SC_F, 1);
    key(MS(502), SC_J, 1);
    r = finish();
    CHECK_I(r.n_responses, 2);
    /* any device: the raw report has the keyboard's id, the message one 0 */
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    in = ev(PSYRSP_KIND_KEYBOARD, PSYRSP_PRESS, MS(500), SC_F);
    in.device = 65537;
    psyrsp_feed(&g_c, &in);
    in.type = PSYRSP_RELEASE;
    in.t = MS(501);
    psyrsp_feed(&g_c, &in);
    in.device = 0;
    in.type = PSYRSP_PRESS;
    in.t = MS(508);
    psyrsp_feed(&g_c, &in);
    in.type = PSYRSP_RELEASE;
    in.t = MS(509);
    psyrsp_feed(&g_c, &in);
    r = finish();
    CHECK_I(r.n_responses, 1);
    CHECK_I(r.n_duplicates, 1);
    CHECK_I(r.device, 65537);
    CHECK_D(r.rt_key_duration, 0.001);
    /* dedup < 0: off */
    d.dedup = -1;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    key(MS(500), SC_F, 1);
    key(MS(500) + 30000, SC_F, 0);
    key(MS(509), SC_F, 1);
    key(MS(509) + 30000, SC_F, 0);
    r = finish();
    CHECK_I(r.n_responses, 2);
    /* dedup set: 10 ms */
    d.dedup = 0.010;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    tap(MS(500), SC_F);
    tap(MS(511), SC_F);
    r = finish();
    CHECK_I(r.n_responses, 2);
}

static int dkey(int64_t t, uint32_t sc, uint32_t dev, int type, int repeat) {
    psyrsp_input in = ev(PSYRSP_KIND_KEYBOARD, type, t, sc);
    in.device = dev;
    if (repeat) in.flags = PSYRSP_IN_REPEAT;
    return psyrsp_feed(&g_c, &in);
}

/* v0.1.1: two keyboards are two devices; a raw report and its message
 * report (device 0) are one press. */
static void check_devices(void) {
    psyrsp_desc d = fj_desc(5.0, 0);
    psyrsp_result r;
    const psyrsp_entry* e;
    int n;
    static const psyrsp_choice only22[] = { { "f", NULL, 0, 0, 0, 0, 22, 0, 0, 0, 0 } };
    d.persist = true;
    /* two keyboards press F 5 ms apart: two responses */
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    dkey(MS(500), SC_F, 11, PSYRSP_PRESS, 0);
    dkey(MS(505), SC_F, 22, PSYRSP_PRESS, 0);
    r = finish();
    CHECK_I(r.n_responses, 2);
    CHECK_I(r.n_duplicates, 0);
    /* as SDL gives it: B's press comes as a repeat while A holds F */
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    dkey(MS(1000), SC_F, 11, PSYRSP_PRESS, 0);
    dkey(MS(1005), SC_F, 22, PSYRSP_PRESS, 1);
    dkey(MS(1100), SC_F, 22, PSYRSP_RELEASE, 0);
    r = finish();
    e = psyrsp_entries(&g_c, &n);
    CHECK_I(r.n_responses, 2);
    CHECK_I(n, 2);
    CHECK_I(e[0].t_release, 0);              /* SDL dropped A's release */
    CHECK_I(e[1].t_release, MS(1100));
    CHECK_I(e[1].device, 22);
    /* A's own OS repeat while A holds: held */
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    dkey(MS(3000), SC_F, 11, PSYRSP_PRESS, 0);
    dkey(MS(3500), SC_F, 11, PSYRSP_PRESS, 1);
    r = finish();
    CHECK_I(r.n_responses, 1);
    CHECK_I(r.n_held, 1);
    /* a raw report (the keyboard's id) and its message report (0): one */
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    dkey(MS(2000), SC_F, 11, PSYRSP_PRESS, 0);
    dkey(MS(2000) + 30000, SC_F, 11, PSYRSP_RELEASE, 0);
    dkey(MS(2009), SC_F, 0, PSYRSP_PRESS, 0);
    dkey(MS(2009) + 30000, SC_F, 0, PSYRSP_RELEASE, 0);
    r = finish();
    CHECK_I(r.n_responses, 1);
    CHECK_I(r.n_duplicates, 1);
    CHECK_I(r.device, 11);
    /* releases per device */
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    dkey(MS(4000), SC_J, 11, PSYRSP_PRESS, 0);
    dkey(MS(4010), SC_J, 22, PSYRSP_PRESS, 0);
    dkey(MS(4100), SC_J, 22, PSYRSP_RELEASE, 0);
    e = psyrsp_entries(&g_c, &n);
    CHECK_I(n, 2);
    CHECK_I(e[0].t_release, 0);
    CHECK_I(e[1].t_release, MS(4100));
    dkey(MS(4200), SC_J, 11, PSYRSP_RELEASE, 0);
    CHECK_I(e[0].t_release, MS(4200));
    /* a choice on keyboard 22: held at open only when 22 holds it */
    memset(&d, 0, sizeof d);
    d.choices = only22;
    d.n_choices = 1;
    CHECK(psyrsp_init(&g_c, &d));
    dkey(MS(10), SC_F, 11, PSYRSP_PRESS, 0);
    psyrsp_arm(&g_c, MS(100));
    onset(MS(200), 1);
    CHECK_I(dkey(MS(300), SC_F, 11, PSYRSP_PRESS, 1), PSYRSP_OPEN);   /* keyboard 11: no choice */
    CHECK_I(dkey(MS(400), SC_F, 22, PSYRSP_PRESS, 1), PSYRSP_ENDED);  /* 22's first press */
    r = finish();
    CHECK(!(r.flags & PSYRSP_R_HELD_AT_OPEN));
    CHECK_I(r.device, 22);
    CHECK(psyrsp_init(&g_c, &d));
    dkey(MS(10), SC_F, 22, PSYRSP_PRESS, 0);
    psyrsp_arm(&g_c, MS(100));
    r = finish();
    CHECK(r.flags & PSYRSP_R_HELD_AT_OPEN);
}

/* v0.1.2: psy_screen.h's raw mouse reports, per device; the cursor. */
static void check_raw_mice(void) {
    psyscr_mouse_event m;
    psyrsp_input in[12];
    psyrsp_cursor cur;
    psyrsp_source src;
    psyrsp_desc d;
    psyrsp_result r;
    int n, i;
    static const psyrsp_choice left77[] = { { NULL, "left", PSYRSP_KIND_MOUSE, 0, 0, 0, 0x77, 1, 0, 0, 0 } };
    memset(&m, 0, sizeof m);
    m.t = MS(500);
    m.device = 0x77;
    m.down = PSYSCR_MOUSE_LEFT | PSYSCR_MOUSE_RIGHT;
    m.up = PSYSCR_MOUSE_X2;
    m.dx = 5;
    m.dy = -2;
    m.wheel = -120;
    m.hwheel = 240;
    m.flags = PSYSCR_MOUSE_UNLISTED;
    n = psyrsp_from_mouse(&m, in, 12);
    CHECK_I(n, 5);
    CHECK_I(in[0].type, PSYRSP_PRESS);
    CHECK_I(in[0].control, 1);
    CHECK_I(in[1].type, PSYRSP_PRESS);
    CHECK_I(in[1].control, 3);
    CHECK_I(in[2].type, PSYRSP_RELEASE);
    CHECK_I(in[2].control, 5);
    CHECK_I(in[3].type, PSYRSP_SAMPLE);
    CHECK_I(in[3].control, PSYRSP_AXIS_DELTA);
    CHECK(in[3].x == 5.0f && in[3].y == -2.0f);
    CHECK_I(in[4].control, PSYRSP_AXIS_WHEEL);
    CHECK(in[4].value == -1.0f && in[4].x == 2.0f);
    for (i = 0; i < n; i++) {
        CHECK_I(in[i].kind, PSYRSP_KIND_MOUSE);
        CHECK_I(in[i].device, 0x77);
        CHECK_I(in[i].t, MS(500));
        CHECK_I(in[i].flags, PSYRSP_IN_UNLISTED);
    }
    CHECK_I(psyrsp_from_mouse(&m, in, 2), 2);
    memset(&m, 0, sizeof m);
    m.down = PSYSCR_MOUSE_MIDDLE | PSYSCR_MOUSE_X1;
    n = psyrsp_from_mouse(&m, in, 12);
    CHECK_I(n, 2);
    CHECK_I(in[0].control, 2);
    CHECK_I(in[1].control, 4);
    memset(&m, 0, sizeof m);
    CHECK_I(psyrsp_from_mouse(&m, in, 12), 0);
    m.flags = PSYSCR_MOUSE_ABSOLUTE;
    m.dx = 65535;
    n = psyrsp_from_mouse(&m, in, 12);
    CHECK_I(n, 1);
    CHECK_I(in[0].control, PSYRSP_AXIS_ABSOLUTE);
    CHECK(in[0].x == 1.0f && in[0].y == 0.0f);
    /* a choice on one mouse: the other mouse's click is no choice */
    memset(&d, 0, sizeof d);
    d.choices = left77;
    d.n_choices = 1;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    memset(&m, 0, sizeof m);
    m.device = 0x88;
    m.down = PSYSCR_MOUSE_LEFT;
    m.t = MS(300);
    n = psyrsp_from_mouse(&m, in, 12);
    CHECK_I(psyrsp_feed(&g_c, &in[0]), PSYRSP_OPEN);
    m.device = 0x77;
    m.t = MS(350);
    n = psyrsp_from_mouse(&m, in, 12);
    CHECK_I(psyrsp_feed(&g_c, &in[0]), PSYRSP_ENDED);
    r = finish();
    CHECK_I(r.device, 0x77);
    CHECK_I(r.n_not_choice, 1);
    CHECK_D(r.rt, 0.25);
    /* the cursor: a gain, clamped, one device */
    memset(&cur, 0, sizeof cur);
    cur.device = 0x77;
    cur.gain = 2;
    cur.w = 100;
    cur.h = 50;
    cur.x = cur.y = 10;
    memset(&m, 0, sizeof m);
    m.device = 0x77;
    m.dx = 5;
    m.dy = -10;
    psyrsp_from_mouse(&m, in, 12);
    CHECK_I(psyrsp_cursor_feed(&cur, &in[0]), 1);
    CHECK(cur.x == 20.0f && cur.y == 0.0f);
    m.dx = 100;
    m.dy = 100;
    psyrsp_from_mouse(&m, in, 12);
    psyrsp_cursor_feed(&cur, &in[0]);
    CHECK(cur.x == 100.0f && cur.y == 50.0f);
    m.device = 0x88;
    m.dx = -50;
    psyrsp_from_mouse(&m, in, 12);
    CHECK_I(psyrsp_cursor_feed(&cur, &in[0]), 0);
    cur.device = 0;
    cur.gain = 0;                              /* 0 = 1 */
    CHECK_I(psyrsp_cursor_feed(&cur, &in[0]), 1);
    CHECK(cur.x == 50.0f);
    m.flags = PSYSCR_MOUSE_ABSOLUTE;
    psyrsp_from_mouse(&m, in, 12);
    CHECK_I(psyrsp_cursor_feed(&cur, &in[0]), 0);
    CHECK_I(psyrsp_sdl_mouse_stamp(0), PSYRSP_TIER_3);
    CHECK_I(psyrsp_sdl_mouse_stamp(65661), 0);
    src = psyrsp_raw_mouse_source();
    CHECK_I(src.kind, PSYRSP_KIND_MOUSE);
    CHECK_I(src.partial, 1);
    CHECK(src.note != NULL);
}

static void check_onset_refine(void) {
    psyrsp_desc d = fj_desc(1.0, 0.1);
    psyrsp_result r;
    const psyrsp_entry* e;
    int n;
    CHECK(psyrsp_init(&g_c, &d));
    /* the plan, then the final onset a frame later */
    psyrsp_arm(&g_c, 0);
    onset(MS(1000), 0);
    CHECK_I(key(MS(1400), SC_F, 1), PSYRSP_ENDED);
    onset(MS(1017), 1);
    r = finish();
    CHECK_D(r.rt, 0.383);
    CHECK(!(r.flags & PSYRSP_R_ONSET_PLAN));
    CHECK(!(r.flags & PSYRSP_R_RECLASSIFIED));
    /* a plan does not replace the final onset */
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(1017), 1);
    onset(MS(1000), 0);
    key(MS(1400), SC_F, 1);
    r = finish();
    CHECK_I(r.t_onset, MS(1017));
    /* the press that ended the window becomes an anticipation */
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(1000), 0);
    CHECK_I(key(MS(1105), SC_F, 1), PSYRSP_ENDED);
    tap(MS(1300), SC_J);                                   /* after the end */
    onset(MS(1017), 1);
    r = finish();
    e = psyrsp_entries(&g_c, &n);
    CHECK(e[0].flags & PSYRSP_E_ANTICIPATION);
    CHECK(e[0].flags & PSYRSP_E_RECLASSIFIED);
    CHECK(e[0].flags & PSYRSP_E_ENDED);
    CHECK(r.flags & PSYRSP_R_ENDED_EARLY);
    CHECK(r.flags & PSYRSP_R_RECLASSIFIED);
    CHECK(!(r.flags & PSYRSP_R_RESPONDED));
    CHECK(r.rt != r.rt);
    /* only a plan: flagged */
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(1000), 0);
    key(MS(1400), SC_F, 1);
    r = finish();
    CHECK(r.flags & PSYRSP_R_ONSET_PLAN);
    CHECK_I(r.onset_src, PSYRSP_ONSET_PLAN);
    CHECK_D(r.rt, 0.4);
    /* no onset at all */
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    key(MS(1400), SC_J, 1);
    r = finish();
    CHECK(r.flags & PSYRSP_R_NO_ONSET);
    CHECK(!(r.flags & PSYRSP_R_RESPONDED));
    CHECK_I(r.n_early, 1);
    /* an onset given after a press that falls in its window: ends then */
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    key(MS(1400), SC_J, 1);
    CHECK_I(onset(MS(1000), 1), PSYRSP_ENDED);
    r = finish();
    CHECK_D(r.rt, 0.4);
    CHECK(r.flags & PSYRSP_R_RECLASSIFIED);
    /* set_onset before arm is ignored */
    CHECK(psyrsp_init(&g_c, &d));
    CHECK_I(onset(MS(1), 1), PSYRSP_IDLE);
}

static void check_out_of_order(void) {
    static const psyrsp_choice ch[] = { { "f", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
                                        { NULL, "box", PSYRSP_KIND_BOX, 0, 0, 0, 0, 1, 0, 0, 0 } };
    psyrsp_desc d;
    psyrsp_result r;
    psyrsp_input in;
    memset(&d, 0, sizeof d);
    d.choices = ch;
    d.n_choices = 2;
    d.duration = 2;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    CHECK_I(key(MS(500), SC_F, 1), PSYRSP_ENDED);
    in = ev(PSYRSP_KIND_BOX, PSYRSP_PRESS, MS(450), 1);   /* read late, stamped earlier */
    psyrsp_feed(&g_c, &in);
    r = finish();
    CHECK_S(r.response_name, "box");
    CHECK_D(r.rt, 0.35);
    CHECK_I(r.n_responses, 2);
    CHECK_I(r.n_after_end, 0);
    /* persist with max 1 arriving out of order before the end: the earliest ends */
    d.persist = true;
    d.max_responses = 2;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    key(MS(600), SC_F, 1);                                 /* before the onset is known */
    key(MS(700), SC_F, 0);
    in.t = MS(300);
    psyrsp_feed(&g_c, &in);
    CHECK_I(onset(MS(100), 1), PSYRSP_ENDED);
    r = finish();
    CHECK_I(r.n_responses, 2);
    CHECK_S(r.response_name, "box");
}

static void check_release(void) {
    psyrsp_desc d = fj_desc(2.0, 0);
    psyrsp_result r;
    d.wait_for_key_release = true;
    d.max_hold = 0.5;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    CHECK_I(key(MS(400), SC_F, 1), PSYRSP_OPEN);           /* waits for the release */
    CHECK_I(key(MS(450), SC_J, 1), PSYRSP_OPEN);           /* after the end */
    CHECK_I(psyrsp_update(&g_c, MS(600)), PSYRSP_OPEN);     /* not yet max_hold */
    CHECK_I(key(MS(650), SC_F, 0), PSYRSP_ENDED);
    r = finish();
    CHECK_D(r.rt, 0.3);
    CHECK_D(r.rt_key_duration, 0.25);
    CHECK_I(r.n_after_end, 1);
    CHECK(!(r.flags & PSYRSP_R_HOLD_TIMEOUT));
    /* max_hold gives up */
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    CHECK_I(key(MS(400), SC_F, 1), PSYRSP_OPEN);
    CHECK_I(psyrsp_update(&g_c, MS(899)), PSYRSP_OPEN);
    CHECK_I(psyrsp_update(&g_c, MS(900)), PSYRSP_ENDED);
    r = finish();
    CHECK(r.flags & PSYRSP_R_HOLD_TIMEOUT);
    CHECK(r.rt_key_duration != r.rt_key_duration);
    /* a release after finish() pairs; finish() again reports it */
    key(MS(1000), SC_F, 0);
    r = finish();
    CHECK_D(r.rt_key_duration, 0.6);
    /* the default max_hold is 2 s */
    d.max_hold = 0;
    CHECK(psyrsp_init(&g_c, &d));
    CHECK_I(g_c.max_hold_ns, MS(2000));
    /* the release pairs with the newest press of that key */
    d = fj_desc(2.0, 0);
    d.persist = true;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    tap(MS(300), SC_F);
    key(MS(600), SC_F, 1);
    key(MS(610), SC_J, 1);
    key(MS(650), SC_J, 0);
    key(MS(900), SC_F, 0);
    r = finish();
    {
        int n;
        const psyrsp_entry* e = psyrsp_entries(&g_c, &n);
        CHECK_I(n, 3);
        CHECK_I(e[0].t_release, MS(380));
        CHECK_I(e[1].t_release, MS(900));
        CHECK_I(e[2].t_release, MS(650));
    }
    /* a stray release (never down) is dropped */
    key(MS(950), SC_K, 0);
    CHECK_I(g_c.n_stray, 1);
}

static void check_crossings(void) {
    static const psyrsp_choice trig[] = {
        { NULL, "trigger", PSYRSP_KIND_GAMEPAD, 0, PSYRSP_CROSS_RISING, 0, 0, PSYRSP_AXIS_GAMEPAD + 5, 0, 0.5f, 0.2f } };
    static const psyrsp_choice fall[] = {
        { NULL, "stick", PSYRSP_KIND_GAMEPAD, 0, PSYRSP_CROSS_FALLING, 0, 0, PSYRSP_AXIS_GAMEPAD + 1, 0, -0.5f, 0.0f } };
    static const psyrsp_choice move[] = {
        { NULL, "move", PSYRSP_KIND_MOUSE, 0, PSYRSP_CROSS_DISTANCE, 0, 0, PSYRSP_AXIS_POSITION, 0, 10.0f, 0.0f } };
    static const psyrsp_choice pen[] = {
        { NULL, "pen", PSYRSP_KIND_PEN, 0, PSYRSP_CROSS_RISING, 0, 0, PSYRSP_AXIS_PEN_PRESSURE, 0, 0.05f, 0.0f } };
    psyrsp_desc d;
    psyrsp_result r;
    const psyrsp_entry* e;
    int n;
    const int G = PSYRSP_KIND_GAMEPAD;
    const uint32_t RT = PSYRSP_AXIS_GAMEPAD + 5;
    memset(&d, 0, sizeof d);
    d.choices = trig;
    d.n_choices = 1;
    d.duration = 2;
    d.persist = true;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    sample(G, MS(200), RT, 0.1f, 0, 0);
    sample(G, MS(300), RT, 0.49f, 0, 0);
    sample(G, MS(400), RT, 0.5f, 0, 0);                    /* crossing, at this sample */
    sample(G, MS(410), RT, 0.45f, 0, 0);                   /* jitter around the level: no re-arm */
    sample(G, MS(420), RT, 0.55f, 0, 0);
    sample(G, MS(500), RT, 0.29f, 0, 0);                   /* below 0.3: re-armed */
    sample(G, MS(600), RT, 0.6f, 0, 0);                    /* the second crossing */
    sample(G, MS(610), RT, 0.6f, 0, 0);
    sample(G + 0, MS(620), RT - 1, 0.9f, 0, 0);           /* another axis */
    r = finish();
    e = psyrsp_entries(&g_c, &n);
    CHECK_I(n, 2);
    CHECK(e[0].flags & PSYRSP_E_CROSSING);
    CHECK_I(e[0].t, MS(400));
    CHECK_I(e[1].t, MS(600));
    CHECK_D(r.rt, 0.3);
    CHECK_S(r.response_name, "trigger");
    CHECK(r.rt_key_duration != r.rt_key_duration);
    /* past the level at arm: held, until it re-arms */
    d.persist = false;
    CHECK(psyrsp_init(&g_c, &d));
    sample(G, MS(10), RT, 0.8f, 0, 0);
    psyrsp_arm(&g_c, MS(50));
    onset(MS(100), 1);
    CHECK_I(sample(G, MS(200), RT, 0.9f, 0, 0), PSYRSP_OPEN);
    CHECK_I(sample(G, MS(300), RT, 0.1f, 0, 0), PSYRSP_OPEN);
    CHECK_I(sample(G, MS(400), RT, 0.7f, 0, 0), PSYRSP_ENDED);
    r = finish();
    CHECK(r.flags & PSYRSP_R_HELD_AT_OPEN);
    CHECK_D(r.rt, 0.3);
    /* the end needs a crossing, not a release, with wait_for_key_release */
    d.wait_for_key_release = true;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    CHECK_I(sample(G, MS(400), RT, 1.0f, 0, 0), PSYRSP_ENDED);
    /* falling */
    memset(&d, 0, sizeof d);
    d.choices = fall;
    d.n_choices = 1;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    CHECK_I(sample(G, MS(200), PSYRSP_AXIS_GAMEPAD + 1, -0.4f, 0, 0), PSYRSP_OPEN);
    CHECK_I(sample(G, MS(300), PSYRSP_AXIS_GAMEPAD + 1, -0.5f, 0, 0), PSYRSP_ENDED);
    /* movement onset: 10 px from the position at arm */
    memset(&d, 0, sizeof d);
    d.choices = move;
    d.n_choices = 1;
    CHECK(psyrsp_init(&g_c, &d));
    sample(PSYRSP_KIND_MOUSE, MS(10), 0, 0, 100, 100);
    psyrsp_arm(&g_c, MS(50));
    onset(MS(100), 1);
    CHECK_I(sample(PSYRSP_KIND_MOUSE, MS(200), 0, 0, 106, 107.9f), PSYRSP_OPEN);
    CHECK_I(sample(PSYRSP_KIND_MOUSE, MS(300), 0, 0, 106, 108), PSYRSP_ENDED);
    r = finish();
    CHECK_D(r.rt, 0.2);
    e = psyrsp_entries(&g_c, &n);
    CHECK(fabs(e[0].value - 10.0f) < 1e-4);
    /* no position before arm: the first sample after it is the reference */
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, MS(50));
    onset(MS(100), 1);
    CHECK_I(sample(PSYRSP_KIND_MOUSE, MS(150), 0, 0, 500, 500), PSYRSP_OPEN);
    CHECK_I(sample(PSYRSP_KIND_MOUSE, MS(200), 0, 0, 509, 500), PSYRSP_OPEN);
    CHECK_I(sample(PSYRSP_KIND_MOUSE, MS(250), 0, 0, 500, 490), PSYRSP_ENDED);
    /* pen pressure onset from no sample: the first contact crosses */
    memset(&d, 0, sizeof d);
    d.choices = pen;
    d.n_choices = 1;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    CHECK_I(sample(PSYRSP_KIND_PEN, MS(350), PSYRSP_AXIS_PEN_PRESSURE, 0.3f, 10, 10), PSYRSP_ENDED);
    /* crossings in ALL mode do not exist */
    memset(&d, 0, sizeof d);
    d.kinds = PSYRSP_KINDS_ALL;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    CHECK_I(sample(G, MS(400), RT, 1.0f, 0, 0), PSYRSP_OPEN);
}

static void check_trace(void) {
    static psyrsp_input tr[5];
    psyrsp_desc d = fj_desc(2.0, 0);
    psyrsp_result r;
    int i;
    d.trace = tr;
    d.trace_cap = 5;
    d.trace_kinds = PSYRSP_KIND_BIT(PSYRSP_KIND_MOUSE);
    CHECK(psyrsp_init(&g_c, &d));
    sample(PSYRSP_KIND_MOUSE, MS(1), 0, 0, 1, 1);           /* before arm: not traced */
    psyrsp_arm(&g_c, MS(10));
    for (i = 0; i < 4; i++) sample(PSYRSP_KIND_MOUSE, MS(20 + i), 0, 0, (float)i, 0);
    sample(PSYRSP_KIND_PEN, MS(30), 0, 0, 0, 0);            /* another kind */
    key(MS(40), SC_K, 1);
    r = finish();
    CHECK_I(r.n_trace, 4);
    CHECK(!(r.flags & PSYRSP_R_TRACE_FULL));
    CHECK_I(tr[3].t, MS(23));
    CHECK(tr[3].x == 3.0f);
    psyrsp_arm(&g_c, MS(100));
    for (i = 0; i < 7; i++) sample(PSYRSP_KIND_MOUSE, MS(120 + i), 0, 0, (float)i, 0);
    r = finish();
    CHECK_I(r.n_trace, 5);
    CHECK(r.flags & PSYRSP_R_TRACE_FULL);
    sample(PSYRSP_KIND_MOUSE, MS(300), 0, 0, 0, 0);         /* after finish: not traced */
    r = finish();
    CHECK_I(r.n_trace, 5);
    /* trace_kinds 0: every kind, keys too */
    d.trace_kinds = 0;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    key(MS(40), SC_K, 1);
    sample(PSYRSP_KIND_PEN, MS(30), 0, 0, 0, 0);
    r = finish();
    CHECK_I(r.n_trace, 2);
}

static void check_capacity(void) {
    psyrsp_desc d = fj_desc(5.0, 0);
    psyrsp_result r;
    int i;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    for (i = 0; i < 100; i++) tap(MS(200) + (int64_t)i * MS(10), 50 + (uint32_t)(i % 40));  /* no choices */
    CHECK_I(key(MS(3000), SC_F, 1), PSYRSP_ENDED);
    r = finish();
    CHECK(r.flags & PSYRSP_R_LOG_FULL);
    CHECK_I(r.n_entries, PSYRSP_LOG_CAP - PSYRSP_LOG_RESERVE + 1);
    CHECK_I(r.n_lost, 100 - (PSYRSP_LOG_CAP - PSYRSP_LOG_RESERVE));
    CHECK_D(r.rt, 2.9);
    /* choices use every slot */
    d.persist = true;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    for (i = 0; i < 70; i++) tap(MS(200) + (int64_t)i * MS(60), i % 2 ? SC_F : SC_J);
    r = finish();
    CHECK_I(r.n_entries, PSYRSP_LOG_CAP);
    CHECK_I(r.n_lost, 6);
}

static void check_quality(void) {
    psyrsp_source src[3];
    psyrsp_desc d;
    psyrsp_result r;
    psyrsp_input in;
    static const psyrsp_choice b1[] = { { NULL, "b", PSYRSP_KIND_BOX, 0, 0, 0, 0, 1, 0, 0, 0 },
                                        { "f", NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
    memset(src, 0, sizeof src);
    src[0].kind = PSYRSP_KIND_KEYBOARD;
    src[0].tier = PSYRSP_TIER_UNKNOWN;
    src[0].partial = 1;
    src[0].lo_us = 160;
    src[0].hi_us = 670;
    src[1].kind = PSYRSP_KIND_BOX;
    src[1].tier = PSYRSP_TIER_3;
    src[1].lo_us = 1000;
    src[1].hi_us = 16000;
    src[2].kind = PSYRSP_KIND_BOX;
    src[2].device = 5;
    src[2].tier = PSYRSP_TIER_1;
    src[2].lo_us = -50;
    src[2].hi_us = 50;
    memset(&d, 0, sizeof d);
    d.choices = b1;
    d.n_choices = 2;
    d.sources = src;
    d.n_sources = 3;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    key(MS(300), SC_F, 1);
    r = finish();
    CHECK_I(r.stamp_tier, PSYRSP_TIER_UNKNOWN);
    CHECK_I(r.stamp_partial, 1);
    CHECK_I(r.stamp_lo_us, 160);
    CHECK_I(r.stamp_hi_us, 670);
    key(MS(400), SC_F, 0);
    /* the device's own entry wins over the kind's */
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    in = ev(PSYRSP_KIND_BOX, PSYRSP_PRESS, MS(300), 1);
    in.device = 5;
    psyrsp_feed(&g_c, &in);
    r = finish();
    CHECK_I(r.stamp_tier, PSYRSP_TIER_1);
    CHECK_I(r.stamp_lo_us, -50);
    /* another box: the kind's */
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    in.device = 6;
    in.t = MS(1300);
    psyrsp_feed(&g_c, &in);
    r = finish();
    CHECK_I(r.stamp_tier, PSYRSP_TIER_3);
    CHECK_I(r.stamp_hi_us, 16000);
    in.type = PSYRSP_RELEASE;                               /* box 6 lets go */
    in.t = MS(1400);
    psyrsp_feed(&g_c, &in);
    in.type = PSYRSP_PRESS;
    /* input.stamp overrides the tier; the table's bounds then do not apply */
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    in.device = 6;
    in.t = MS(2300);
    in.stamp = PSYRSP_TIER_SIM;
    psyrsp_feed(&g_c, &in);
    r = finish();
    CHECK_I(r.stamp_tier, PSYRSP_TIER_SIM);
    CHECK_I(r.stamp_lo_us, 0);
    CHECK_I(r.stamp_hi_us, 0);
}

static int count_fields(const char* s) {
    int n = 1, q = 0;
    for (; *s; s++) {
        if (*s == '"') q = !q;
        else if (*s == ',' && !q) n++;
    }
    return n;
}

static void check_format(void) {
    char head[512], row[512];
    psyrsp_desc d = fj_desc(1.0, 0);
    static const psyrsp_choice odd[] = { { "f", "a,\"b\"", 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
    psyrsp_result r;
    psyrsp_input in;
    int hn = psyrsp_format_header(head, sizeof head);
    CHECK(hn > 0 && hn < (int)sizeof head);
    CHECK_I(count_fields(head), 22);
    CHECK(strncmp(head, "rt,response,rt_key_duration,", 28) == 0);
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    key(MS(523), SC_F, 1);
    key(MS(600), SC_F, 0);
    r = finish();
    psyrsp_format_row(&r, row, sizeof row);
    CHECK_I(count_fields(row), 22);
    CHECK(strncmp(row, "0.423000000,f,0.077000000,keyboard,9,102,0,", 43) == 0);
    CHECK_HAS(row, ",1,1,0.000001234,0.000000000,42,1,0,0,0,0,1");
    /* a response with no release: an empty duration */
    psyrsp_arm(&g_c, MS(5000));
    onset(MS(5100), 1);
    key(MS(5600), SC_J, 1);
    r = finish();
    psyrsp_format_row(&r, row, sizeof row);
    CHECK(strncmp(row, "0.500000000,right,,keyboard,", 28) == 0);
    key(MS(5700), SC_J, 0);
    /* no response: empty rt, response and duration */
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    psyrsp_update(&g_c, MS(2000));
    r = finish();
    psyrsp_format_row(&r, row, sizeof row);
    CHECK_I(count_fields(row), 22);
    CHECK(strncmp(row, ",,,", 3) == 0);
    /* a name with a comma and a quote is quoted */
    d.choices = odd;
    d.n_choices = 1;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    key(MS(523), SC_F, 1);
    r = finish();
    psyrsp_format_row(&r, row, sizeof row);
    CHECK_HAS(row, ",\"a,\"\"b\"\"\",");
    CHECK_I(count_fields(row), 22);
    /* an unnamed box button and an ALL-mode key */
    memset(&d, 0, sizeof d);
    d.kinds = PSYRSP_KINDS_ALL;
    CHECK(psyrsp_init(&g_c, &d));
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    in = ev(PSYRSP_KIND_BOX, PSYRSP_PRESS, MS(200), 4);
    psyrsp_feed(&g_c, &in);
    r = finish();
    psyrsp_format_row(&r, row, sizeof row);
    CHECK_HAS(row, ",box:4,");
    psyrsp_arm(&g_c, 0);
    onset(MS(100), 1);
    key(MS(200), 44, 1);
    r = finish();
    psyrsp_format_row(&r, row, sizeof row);
    CHECK_HAS(row, ",space,");
    /* no onset: empty residual */
    psyrsp_arm(&g_c, 0);
    r = finish();
    psyrsp_format_row(&r, row, sizeof row);
    CHECK_I(count_fields(row), 22);
    CHECK_HAS(row, ",0,0,,,-1,");
    CHECK_I(psyrsp_format_row(NULL, row, sizeof row), -1);
    CHECK_S(psyrsp_strerror(PSYRSP_ENDED), "ended");
    CHECK_S(psyrsp_strerror(PSYRSP_ERR_ARG), "bad argument");
}

static void check_helpers(void) {
    psyscr_record fr;
    psyau_onset au;
    psytl_event le;
    psyrsp_onset o;
    memset(&fr, 0, sizeof fr);
    fr.index = 7;
    fr.onset = MS(500);
    fr.planned = MS(483);
    fr.residual = 250;
    fr.tier = 2;
    o = psyrsp_onset_flip(&fr);
    CHECK_I(o.t, MS(500));
    CHECK_I(o.frame, 7);
    CHECK_I(o.tier, 2);
    CHECK_I(o.final, 1);
    CHECK_I(o.src, PSYRSP_ONSET_FLIP);
    CHECK_I(o.residual, 250);
    fr.flags = PSYSCR_FLIP_PENDING;
    CHECK_I(psyrsp_onset_flip(&fr).final, 0);
    fr.flags = 0;
    fr.onset = 0;                       /* never shown */
    o = psyrsp_onset_flip(&fr);
    CHECK_I(o.t, MS(483));
    CHECK_I(o.final, 0);
    CHECK_I(o.src, PSYRSP_ONSET_PLAN);
    memset(&au, 0, sizeof au);
    au.onset = MS(900);
    au.residual = -10;
    au.tier = 2;
    au.flags = PSYAU_ONSET_PENDING;
    o = psyrsp_onset_audio(&au);
    CHECK_I(o.final, 0);
    CHECK_I(o.src, PSYRSP_ONSET_AUDIO);
    au.flags = 0;
    o = psyrsp_onset_audio(&au);
    CHECK_I(o.final, 1);
    CHECK_I(o.t, MS(900));
    CHECK_I(o.frame, -1);
    memset(&le, 0, sizeof le);
    le.onset = MS(1000);
    le.residual = 5;
    le.frame = 60;
    o = psyrsp_onset_landing(&le);
    CHECK_I(o.t, MS(1000));
    CHECK_I(o.final, 0);
    CHECK_I(o.frame, 60);
    CHECK_I(o.src, PSYRSP_ONSET_PLAN);
}

#if defined(PSYRSP_TEST_SDL)
static void check_sdl(void) {
    SDL_Event e;
    psyrsp_input in;
    psyrsp_sdl_ctx ctx;
    psyrsp_source s;
    memset(&ctx, 0, sizeof ctx);
    memset(&e, 0, sizeof e);
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.down = true;
    e.key.scancode = SDL_SCANCODE_F;
    e.key.key = SDLK_F;
    e.key.which = 12;
    e.key.mod = SDL_KMOD_LSHIFT;
    CHECK_I(psyrsp_from_sdl(&e, 777, NULL, &in), 1);
    CHECK_I(in.kind, PSYRSP_KIND_KEYBOARD);
    CHECK_I(in.type, PSYRSP_PRESS);
    CHECK_I(in.t, 777);
    CHECK_I(in.control, SC_F);
    CHECK_I(in.code, 'f');
    CHECK_I(in.device, 12);
    CHECK_I(in.mods, SDL_KMOD_LSHIFT);
    CHECK_I(in.stamp, 0);
    CHECK_I(in.flags, 0);
    e.key.repeat = true;
    psyrsp_from_sdl(&e, 777, &ctx, &in);           /* ctx: raw path off */
    CHECK_I(in.flags, PSYRSP_IN_REPEAT);
    CHECK_I(in.stamp, PSYRSP_TIER_3);
    ctx.raw_keyboard = true;
    psyrsp_from_sdl(&e, 777, &ctx, &in);
    CHECK_I(in.stamp, 0);
    e.type = SDL_EVENT_KEY_UP;
    e.key.down = false;
    psyrsp_from_sdl(&e, 777, &ctx, &in);
    CHECK_I(in.type, PSYRSP_RELEASE);
    /* mouse; synthetic from touch and pen dropped */
    memset(&e, 0, sizeof e);
    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.down = true;
    e.button.button = 3;
    e.button.x = 10.5f;
    e.button.y = 20;
    CHECK_I(psyrsp_from_sdl(&e, 1, NULL, &in), 1);
    CHECK_I(in.kind, PSYRSP_KIND_MOUSE);
    CHECK_I(in.control, 3);
    CHECK(in.x == 10.5f);
    CHECK_I(in.stamp, PSYRSP_TIER_3);            /* which 0: SDL's absolute mode */
    e.button.which = 5;                           /* relative mode: raw */
    psyrsp_from_sdl(&e, 1, NULL, &in);
    CHECK_I(in.stamp, 0);
    e.button.which = SDL_TOUCH_MOUSEID;
    CHECK_I(psyrsp_from_sdl(&e, 1, NULL, &in), 0);
    e.button.which = SDL_PEN_MOUSEID;
    CHECK_I(psyrsp_from_sdl(&e, 1, &ctx, &in), 0);
    ctx.keep_synthetic = true;
    CHECK_I(psyrsp_from_sdl(&e, 1, &ctx, &in), 1);
    CHECK_I(in.flags, PSYRSP_IN_SYNTHETIC);
    ctx.keep_synthetic = false;
    memset(&e, 0, sizeof e);
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = 5;
    e.motion.y = 6;
    CHECK_I(psyrsp_from_sdl(&e, 1, NULL, &in), 1);
    CHECK_I(in.type, PSYRSP_SAMPLE);
    CHECK_I(in.control, PSYRSP_AXIS_POSITION);
    e.motion.which = SDL_PEN_MOUSEID;
    CHECK_I(psyrsp_from_sdl(&e, 1, NULL, &in), 0);
    /* touch */
    memset(&e, 0, sizeof e);
    e.type = SDL_EVENT_FINGER_DOWN;
    e.tfinger.touchID = 9;
    e.tfinger.fingerID = 4;
    e.tfinger.x = 0.25f;
    e.tfinger.pressure = 0.5f;
    CHECK_I(psyrsp_from_sdl(&e, 1, NULL, &in), 1);
    CHECK_I(in.kind, PSYRSP_KIND_TOUCH);
    CHECK_I(in.type, PSYRSP_PRESS);
    CHECK_I(in.control, 4);
    CHECK_I(in.device, 9);
    CHECK(in.value == 0.5f);
    e.type = SDL_EVENT_FINGER_MOTION;
    psyrsp_from_sdl(&e, 1, NULL, &in);
    CHECK_I(in.type, PSYRSP_SAMPLE);
    e.type = SDL_EVENT_FINGER_CANCELED;
    psyrsp_from_sdl(&e, 1, NULL, &in);
    CHECK_I(in.type, PSYRSP_RELEASE);
    e.tfinger.touchID = SDL_PEN_TOUCHID;
    CHECK_I(psyrsp_from_sdl(&e, 1, NULL, &in), 0);
    /* pen */
    memset(&e, 0, sizeof e);
    e.type = SDL_EVENT_PEN_DOWN;
    e.ptouch.down = true;
    e.ptouch.eraser = true;
    e.ptouch.which = 2;
    CHECK_I(psyrsp_from_sdl(&e, 1, NULL, &in), 1);
    CHECK_I(in.kind, PSYRSP_KIND_PEN);
    CHECK_I(in.type, PSYRSP_PRESS);
    CHECK_I(in.control, 0);
    CHECK_I(in.flags, PSYRSP_IN_ERASER);
    memset(&e, 0, sizeof e);
    e.type = SDL_EVENT_PEN_AXIS;
    e.paxis.axis = SDL_PEN_AXIS_PRESSURE;
    e.paxis.value = 0.75f;
    psyrsp_from_sdl(&e, 1, NULL, &in);
    CHECK_I(in.type, PSYRSP_SAMPLE);
    CHECK_I(in.control, PSYRSP_AXIS_PEN_PRESSURE);
    CHECK(in.value == 0.75f);
    e.paxis.axis = SDL_PEN_AXIS_YTILT;
    psyrsp_from_sdl(&e, 1, NULL, &in);
    CHECK_I(in.control, PSYRSP_AXIS_PEN + 2);
    memset(&e, 0, sizeof e);
    e.type = SDL_EVENT_PEN_BUTTON_DOWN;
    e.pbutton.down = true;
    e.pbutton.button = 1;
    psyrsp_from_sdl(&e, 1, NULL, &in);
    CHECK_I(in.control, 1);
    CHECK_I(in.type, PSYRSP_PRESS);
    e.type = SDL_EVENT_PEN_PROXIMITY_OUT;
    psyrsp_from_sdl(&e, 1, NULL, &in);
    CHECK_I(in.type, PSYRSP_PROXIMITY);
    CHECK(in.value == 0.0f);
    e.type = SDL_EVENT_PEN_MOTION;
    psyrsp_from_sdl(&e, 1, NULL, &in);
    CHECK_I(in.type, PSYRSP_SAMPLE);
    /* gamepad */
    memset(&e, 0, sizeof e);
    e.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    e.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTX;
    e.gaxis.value = -32768;
    psyrsp_from_sdl(&e, 1, NULL, &in);
    CHECK(in.value == -1.0f);
    CHECK_I(in.control, PSYRSP_AXIS_GAMEPAD);
    e.gaxis.axis = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
    e.gaxis.value = 32767;
    psyrsp_from_sdl(&e, 1, NULL, &in);
    CHECK(in.value == 1.0f);
    CHECK_I(in.control, PSYRSP_AXIS_GAMEPAD + 5);
    e.gaxis.value = 0;
    psyrsp_from_sdl(&e, 1, NULL, &in);
    CHECK(in.value == 0.0f);
    memset(&e, 0, sizeof e);
    e.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    e.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    psyrsp_from_sdl(&e, 1, NULL, &in);
    CHECK_I(in.type, PSYRSP_RELEASE);
    CHECK_I(in.kind, PSYRSP_KIND_GAMEPAD);
    /* the rest */
    memset(&e, 0, sizeof e);
    e.type = SDL_EVENT_WINDOW_EXPOSED;
    CHECK_I(psyrsp_from_sdl(&e, 1, NULL, &in), 0);
    /* sources */
    s = psyrsp_sdl_source(&ctx, PSYRSP_KIND_KEYBOARD);
    CHECK_I(s.kind, PSYRSP_KIND_KEYBOARD);
#if defined(_WIN32)
    CHECK_I(s.partial, 1);
    CHECK_I(s.hi_us, 670);
    ctx.raw_keyboard = false;
    s = psyrsp_sdl_source(&ctx, PSYRSP_KIND_KEYBOARD);
    CHECK_I(s.tier, PSYRSP_TIER_3);
    s = psyrsp_sdl_source(&ctx, PSYRSP_KIND_PEN);
    CHECK_I(s.tier, PSYRSP_TIER_3);
#endif
    CHECK(s.note != NULL);
}
#endif

/* Random streams: the result agrees with a direct reading of the entries. */
static uint64_t g_rng = 0x9E3779B97F4A7C15ULL;
static uint32_t rnd(uint32_t n) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return (uint32_t)(g_rng % n);
}

static void check_property(void) {
    int round, i, bad = 0;
    for (round = 0; round < 20000 && bad < 5; round++) {
        psyrsp_desc d = fj_desc(rnd(3) ? 0.5 + rnd(1000) / 1000.0 : 0, rnd(200) / 1000.0);
        psyrsp_result r;
        const psyrsp_entry* e;
        int n, n_valid = 0, n_cls = 0, best = -1;
        int64_t on = MS(200 + rnd(400));
        if (d.duration > 0 && d.minimum_valid_rt >= d.duration) d.minimum_valid_rt = 0;
        d.persist = rnd(2) != 0;
        d.max_responses = (int)rnd(3);
        d.allow_held_key = rnd(2) != 0;
        d.wait_for_key_release = rnd(3) == 0;
        if (!psyrsp_init(&g_c, &d)) { bad++; CHECK(0); continue; }
        psyrsp_arm(&g_c, 0);
        if (rnd(4)) onset(on - MS(rnd(30)), 0);
        for (i = 0; i < 40; i++) {
            uint32_t k = rnd(4);
            uint32_t sc = k == 0 ? SC_F : k == 1 ? SC_J : k == 2 ? SC_K : 4 + rnd(3);
            int64_t t = MS(rnd(2500)) + rnd(1000000);
            switch (rnd(5)) {
            case 0: case 1: key(t, sc, 1); break;
            case 2: case 3: key(t, sc, 0); break;
            default: key_repeat(t, sc); break;
            }
            if (rnd(10) == 0) psyrsp_update(&g_c, MS(rnd(3000)));
            if (i == 20 && rnd(2)) onset(on, 1);
        }
        psyrsp_finish(&g_c, &r);
        e = psyrsp_entries(&g_c, &n);
        for (i = 0; i < n; i++) {
            uint16_t k = (uint16_t)(e[i].flags & PSYRSP_E_CLASS_MASK);
            int64_t rt = e[i].t - g_c.onset.t;
            /* one class at most, and only one that its rt allows */
            if ((k & (k - 1)) != 0) n_cls++;
            if (k == PSYRSP_E_VALID) {
                n_valid++;
                if (!g_c.have_onset || rt < g_c.min_rt_ns || (g_c.duration_ns && rt >= g_c.duration_ns)) n_cls++;
                if (e[i].flags & (PSYRSP_E_DUPLICATE | PSYRSP_E_NOT_CHOICE | PSYRSP_E_AFTER_END)) n_cls++;
                if (best < 0 || e[i].t < e[best].t) best = i;
            }
        }
        if (n_cls || n_valid != r.n_responses || best != r.entry ||
            (best >= 0 && r.rt != (double)(e[best].t - g_c.onset.t) / 1e9) ||
            r.n_entries != n || n > PSYRSP_LOG_CAP) {
            bad++;
            fprintf(stderr, "psy_response_test: property round %d: classes %d valid %d/%d best %d/%d\n", round,
                    n_cls, n_valid, r.n_responses, best, r.entry);
        }
        g_checks++;
    }
    if (bad) g_failures++;
}

int main(void) {
    printf("psy_response %s\n", psyrsp_version());
    check_names();
    check_validation();
    check_basic();
    check_bounds();
    check_deadline();
    check_ending();
    check_modes();
    check_held();
    check_double();
    check_devices();
    check_raw_mice();
    check_onset_refine();
    check_out_of_order();
    check_release();
    check_crossings();
    check_trace();
    check_capacity();
    check_quality();
    check_format();
    check_helpers();
#if defined(PSYRSP_TEST_SDL)
    check_sdl();
#endif
    check_property();
    if (g_failures) {
        fprintf(stderr, "psy_response_test: %d of %d checks FAILED\n", g_failures, g_checks);
        return 1;
    }
    printf("psy_response_test: all %d checks passed\n", g_checks);
    return 0;
}
