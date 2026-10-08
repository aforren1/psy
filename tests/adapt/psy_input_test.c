/* psy_input_test.c - self-checking test for psy_input.h. No framework: it
 * returns 0 when every check passed and 1 after printing each failure.
 *
 *     gcc -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Werror -I. \
 *         -o input_test tests/adapt/psy_input_test.c
 *
 * The layout and the constants are in data files and in producers built
 * against older copies, so their values are pinned here. The adapters are
 * checked on events filled by hand: with PSYIN_TEST_SDL defined and SDL3's
 * headers on the path, the SDL adapter (nothing is linked); always the
 * raw mouse adapter, with psy_screen.h's declarations. psy_response.h's
 * test runs every adapter case again through its aliases.
 */
#if defined(PSYIN_TEST_SDL)
#include <SDL3/SDL_events.h>
#endif
#include "psy_screen.h"
#include "psy_input.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK_I(got, want) do { long long g_ = (long long)(got), w_ = (long long)(want); g_checks++; \
    if (g_ != w_) { fprintf(stderr, "psy_input_test: FAIL at line %d: %s (got %lld, want %lld)\n", \
                            __LINE__, #got, g_, w_); g_failures++; } } while (0)

static void check_layout(void) {
    CHECK_I(sizeof(psyin_event), 48);
    CHECK_I(offsetof(psyin_event, t), 0);
    CHECK_I(offsetof(psyin_event, device), 8);
    CHECK_I(offsetof(psyin_event, control), 12);
    CHECK_I(offsetof(psyin_event, code), 16);
    CHECK_I(offsetof(psyin_event, mods), 20);
    CHECK_I(offsetof(psyin_event, kind), 22);
    CHECK_I(offsetof(psyin_event, type), 23);
    CHECK_I(offsetof(psyin_event, flags), 24);
    CHECK_I(offsetof(psyin_event, stamp), 25);
    CHECK_I(offsetof(psyin_event, x), 28);
    CHECK_I(offsetof(psyin_event, y), 32);
    CHECK_I(offsetof(psyin_event, value), 36);
    CHECK_I(offsetof(psyin_event, aux), 40);
    /* v0.3.0: the two spare places, so every older offset stays */
    CHECK_I(offsetof(psyin_event, unc_us), 26);
    CHECK_I(sizeof(((psyin_event*)0)->unc_us), 2);
    CHECK_I(offsetof(psyin_event, ticks), 44);
    CHECK_I(sizeof(((psyin_event*)0)->ticks), 4);
}

static void check_constants(void) {
    CHECK_I(PSYIN_KIND_KEYBOARD, 0);
    CHECK_I(PSYIN_KIND_MOUSE, 1);
    CHECK_I(PSYIN_KIND_TOUCH, 2);
    CHECK_I(PSYIN_KIND_PEN, 3);
    CHECK_I(PSYIN_KIND_GAMEPAD, 4);
    CHECK_I(PSYIN_KIND_BOX, 5);
    CHECK_I(PSYIN_KIND_EYE, 6);
    CHECK_I(PSYIN_KIND_SYNC, 7);
    CHECK_I(PSYIN_KIND_USER, 8);
    CHECK_I(PSYIN_KIND_BIT(PSYIN_KIND_BOX), 32);
    CHECK_I(PSYIN_KINDS_ALL, 0xFFFF);
    CHECK_I(PSYIN_NONE, 0);
    CHECK_I(PSYIN_PRESS, 1);
    CHECK_I(PSYIN_RELEASE, 2);
    CHECK_I(PSYIN_SAMPLE, 3);
    CHECK_I(PSYIN_PROXIMITY, 4);
    CHECK_I(PSYIN_REPEAT, 1);
    CHECK_I(PSYIN_SYNTHETIC, 2);
    CHECK_I(PSYIN_ERASER, 4);
    CHECK_I(PSYIN_UNLISTED, 8);
    CHECK_I(PSYIN_DEVTICKS, 16);
    CHECK_I(PSYIN_EYE_GAZE, 0);
    CHECK_I(PSYIN_EYE_GAZE, PSYIN_AXIS_POSITION);
    CHECK_I(PSYIN_EYE_FIXATION, 1);
    CHECK_I(PSYIN_EYE_SACCADE, 2);
    CHECK_I(PSYIN_EYE_BLINK, 3);
    CHECK_I(PSYIN_EYE_LEFT, 0);
    CHECK_I(PSYIN_EYE_RIGHT, 1);
    CHECK_I(PSYIN_EYE_BOTH, 2);
    CHECK_I(PSYIN_UNC_MAX, 65535);
    CHECK_I(PSYIN_AXIS_POSITION, 0);
    CHECK_I(PSYIN_AXIS_DELTA, 1);
    CHECK_I(PSYIN_AXIS_ABSOLUTE, 2);
    CHECK_I(PSYIN_AXIS_WHEEL, 3);
    CHECK_I(PSYIN_AXIS_PEN, 16);
    CHECK_I(PSYIN_AXIS_PEN_PRESSURE, 16);
    CHECK_I(PSYIN_AXIS_GAMEPAD, 32);
    CHECK_I(PSYIN_TIER_UNKNOWN, 0);
    CHECK_I(PSYIN_TIER_1, 1);
    CHECK_I(PSYIN_TIER_2, 2);
    CHECK_I(PSYIN_TIER_3, 3);
    CHECK_I(PSYIN_TIER_SIM, 4);
    CHECK_I(psyin_sdl_mouse_stamp(0), PSYIN_TIER_3);
    CHECK_I(psyin_sdl_mouse_stamp(65661), 0);
}

static void check_raw_mouse(void) {
    psyscr_mouse_event m;
    psyin_event e[12];
    psyin_source s;
    int n;
    memset(&m, 0, sizeof m);
    m.t = 123;
    m.device = 65661;
    m.down = PSYSCR_MOUSE_RIGHT;
    m.dx = -7;
    memset(e, 0xA5, sizeof e);   /* the adapter must write every byte it owns */
    n = psyin_from_mouse(&m, e, 12);
    CHECK_I(n, 2);
    CHECK_I(e[0].unc_us, 0);
    CHECK_I(e[0].ticks, 0);
    CHECK_I(e[0].flags & PSYIN_DEVTICKS, 0);
    CHECK_I(e[1].ticks, 0);
    CHECK_I(e[0].kind, PSYIN_KIND_MOUSE);
    CHECK_I(e[0].type, PSYIN_PRESS);
    CHECK_I(e[0].control, 3);
    CHECK_I(e[0].device, 65661);
    CHECK_I(e[0].t, 123);
    CHECK_I(e[1].control, PSYIN_AXIS_DELTA);
    CHECK_I((long long)e[1].x, -7);
    CHECK_I(psyin_from_mouse(&m, e, 1), 1);
    s = psyin_raw_mouse_source();
    CHECK_I(s.kind, PSYIN_KIND_MOUSE);
    CHECK_I(s.tier, PSYIN_TIER_UNKNOWN);
}

#if defined(PSYIN_TEST_SDL)
static void check_sdl(void) {
    SDL_Event ev;
    psyin_event e;
    memset(&ev, 0, sizeof ev);
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.down = true;
    ev.key.scancode = SDL_SCANCODE_J;
    ev.key.key = SDLK_J;
    ev.key.which = 9;
    CHECK_I(psyin_from_sdl(&ev, 55, NULL, &e), 1);
    CHECK_I(e.kind, PSYIN_KIND_KEYBOARD);
    CHECK_I(e.control, 13);
    CHECK_I(e.code, 'j');
    CHECK_I(e.device, 9);
    CHECK_I(e.t, 55);
    memset(&ev, 0, sizeof ev);
    ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    ev.button.down = true;
    ev.button.button = 1;
    CHECK_I(psyin_from_sdl(&ev, 1, NULL, &e), 1);
    CHECK_I(e.stamp, PSYIN_TIER_3);
    ev.type = SDL_EVENT_WINDOW_SHOWN;
    CHECK_I(psyin_from_sdl(&ev, 1, NULL, &e), 0);
}
#endif

int main(void) {
    printf("psy_input %s\n", PSYIN_VERSION_STRING);
    check_layout();
    check_constants();
    check_raw_mouse();
#if defined(PSYIN_TEST_SDL)
    check_sdl();
#endif
    if (g_failures) {
        fprintf(stderr, "psy_input_test: %d of %d checks FAILED\n", g_failures, g_checks);
        return 1;
    }
    printf("psy_input_test: all %d checks passed\n", g_checks);
    return 0;
}
