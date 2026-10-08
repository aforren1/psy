/* psy_input.h - v0.3.0 - public domain single-header input event
 *
 *   One timestamped input event for every device a rig reads: keyboards,
 *   mice, touch, pens, gamepads, response and button boxes, eye trackers.
 *   Producers (psy_screen.h's SDL events and raw mice, a serial box reader,
 *   an eye tracker) make these events; consumers (psy_response.h's
 *   collector, a data log, the player's input.key) read them, and neither
 *   side needs the other's header. Also: the timing quality of a source
 *   (the tier scale of psy_screen.h and psy_audio.h), and adapters from
 *   SDL3 events and psy_screen.h's raw mouse reports.
 *
 *   Types and inline functions only: nothing to implement, nothing to
 *   link, no heap, no OS calls. C99 is the floor: it builds as C99, C11
 *   and C++17, and in the C dialect MSVC compiles by default. The SDL3
 *   adapter appears when SDL3's SDL_events.h was included first (or when
 *   this header is included again after it).
 *
 *   ---------------------------------------------------------------------
 *   CHANGELOG
 *   ---------------------------------------------------------------------
 *   v0.3.0 - For specialized devices (docs/devices_spec.md): PSYIN_KIND_SYNC
 *          (7) for timing events that are not a participant's response
 *          (scanner pulses, TTL inputs, photodiode and sound-key edges);
 *          unc_us, the event's own uncertainty, in the 2 bytes that were
 *          reserved_ (offset 26); ticks, the device clock's raw count, in
 *          the 4 bytes of tail padding (offset 44), valid with the new flag
 *          PSYIN_DEVTICKS; the eye controls PSYIN_EYE_* and eye codes. The
 *          size (48) and every existing offset and value are unchanged.
 *   v0.2.0 - The raw mouse report (psyin_mouse_report, PSYIN_MOUSE_*) moved
 *          here from psy_screen.h, so psyin_from_mouse() needs no other
 *          header; psy_screen.h's psyscr_mouse_event and PSYSCR_MOUSE_*
 *          are its aliases. The SDL3 adapter is defined when this header
 *          is included again after SDL3, so psy_screen.h's implementation
 *          uses it for psyscr_event_input().
 *   v0.1.0 - split out of psy_response.h v0.1.2: the event, its kinds,
 *          types, flags, controls and axes, the source entry and tiers,
 *          the SDL3 adapter and the raw mouse adapter, renamed from
 *          psyrsp_ to psyin_ (psy_response.h keeps its names as aliases).
 *
 *   STATUS: v0.3.0, 2026-10-08 (v0.2.0's checks, and the new fields' offsets
 *   and constants). Built with MinGW-w64 gcc 16.1 as C11, C99
 *   and C++17 under -Wall -Wextra -Wpedantic -Wshadow -Werror and with
 *   MSVC 19.44 under /W4 /WX as C11 and C++17. tests/adapt/psy_input_test.c
 *   checks the layout (48 bytes, the field offsets producers write), the
 *   constants' values (they are in data files), and both adapters on
 *   events filled by hand; psy_response.h's test runs every adapter case
 *   again through its aliases.
 *
 *   ---------------------------------------------------------------------
 *   USAGE
 *   ---------------------------------------------------------------------
 *   Include it; there is no implementation to define. A producer:
 *
 *       psyin_event e = { .t = psyrt_now_ns(), .kind = PSYIN_KIND_BOX,
 *                         .device = port, .control = button,
 *                         .type = PSYIN_PRESS, .value = 1 };
 *
 *   From SDL3 (include SDL3 or psy_screen.h first):
 *
 *       while (psyscr_poll(&scr, &ev, &t))
 *           if (psyin_from_sdl(&ev, t, NULL, &e)) psyrsp_feed(&rsp, &e);
 *
 *   ---------------------------------------------------------------------
 *   THE EVENT (psyin_event)
 *   ---------------------------------------------------------------------
 *   One 48-byte struct for every device:
 *     t        psy_rt ns
 *     kind     PSYIN_KIND_KEYBOARD, _MOUSE, _TOUCH, _PEN, _GAMEPAD, _BOX
 *              (response box, button box, pedal, an Arduino-style device),
 *              _EYE, _SYNC (a timing event that is not a response: a
 *              scanner pulse, a TTL input, a photodiode or sound-key edge;
 *              control = the line or channel), _USER + 0..7
 *     device   the instance within the kind (SDL's `which`, a port index);
 *              0 = unknown
 *     type     PRESS, RELEASE, SAMPLE (a position or an axis value),
 *              PROXIMITY (value 1 in range, 0 out)
 *     control  keyboard: the SDL scancode (the USB HID usage: the physical
 *              key); mouse, pen, gamepad, box: the button; touch: the
 *              contact (finger) id, low 32 bits; a SAMPLE: an axis
 *              (PSYIN_AXIS_POSITION, PSYIN_AXIS_PEN + SDL_PenAxis,
 *              PSYIN_AXIS_GAMEPAD + SDL_GamepadAxis), except touch, whose
 *              SAMPLE control is the contact
 *     code     keyboard: the SDL keycode (the layout's key); else yours
 *     mods     keyboard: SDL_Keymod
 *     flags    PSYIN_REPEAT (the OS's auto-repeat of a held key),
 *              PSYIN_SYNTHETIC (made by the OS or SDL from another
 *              device), PSYIN_ERASER (the pen's eraser end),
 *              PSYIN_UNLISTED (a raw mouse SDL does not list),
 *              PSYIN_DEVTICKS (ticks is set)
 *     stamp    the stamp's tier (SOURCES); 0 = the source table's
 *     unc_us   this event's own uncertainty in us: a device clock fit's
 *              spread at this time, a bracket's width, a stamp's known
 *              quantizer; 0 = the source entry's bounds; 65535 = 65.5 ms
 *              or more
 *     x, y     position: window coordinates for mouse and pen, 0..1 for
 *              touch; pen tilt
 *     value    an axis value, pressure; 1 for a press, 0 for a release
 *     aux      yours
 *     ticks    with PSYIN_DEVTICKS: the low 32 bits of the device's own
 *              clock for this event, in the source's unit, so the analysis
 *              can map the event again with an offline fit of the clock
 *              pairs in the log (psy_rt.h DEVICE CLOCK FIT). 32 bits are
 *              enough: the pairs carry the whole count
 *   Eye trackers (kind EYE): control PSYIN_EYE_GAZE with type SAMPLE (x, y
 *   window pixels, value the pupil); PSYIN_EYE_FIXATION, _SACCADE and
 *   _BLINK with PRESS at the start and RELEASE at the end; PROXIMITY with
 *   value 0 when the tracker loses the eye and 1 when it finds it again;
 *   code the eye (PSYIN_EYE_LEFT, _RIGHT, _BOTH).
 *   Consumers take them in arrival order. Their times need not rise:
 *   sources stamp differently (a response box's device clock, SDL's raw
 *   and message paths), so a consumer reads t, not the order
 *   (psy_response.h does).
 *
 *   ---------------------------------------------------------------------
 *   SOURCES AND TIMING QUALITY (psyin_source)
 *   ---------------------------------------------------------------------
 *   Each source states how its stamps relate to the physical event, on
 *   the tier scale of psy_screen.h and psy_audio.h, for the whole chain
 *   from the finger to the stamp:
 *     PSYIN_TIER_UNKNOWN (0) not measured end to end
 *     PSYIN_TIER_1  a device clock mapped to psy_rt by a fit that a
 *                    loopback test checked
 *     PSYIN_TIER_2  good under stated conditions, measured
 *     PSYIN_TIER_3  development only: a tick-quantized OS time, a poll
 *     PSYIN_TIER_SIM synthetic input, exact by construction
 *   An entry also gives measured bounds (lo_us, hi_us: stamp minus event)
 *   and `partial` when they cover the host part only (the OS saw it ->
 *   the stamp), not the device's scan, debounce and USB poll. The entry of
 *   a kind with device 0 covers every device of that kind; an entry with
 *   the device's id wins. event.stamp, when set, overrides the tier.
 *   psy_response.h's result copies the responding source's tier, bounds
 *   and partial flag next to the onset's tier; it computes no combined
 *   uncertainty, because the two rest on different evidence.
 *   Measured on the one Windows 11 machine of psy_screen.h's STATUS
 *   (docs/psy_response.md and docs/psy_screen.md have the tables): SDL
 *   3.4's raw keyboard path stamped keys sent with SendInput 0.16 to 0.67
 *   ms after the call; the message path 6.2 ms before to 12.1 ms after. Mouse, touch and pen
 *   events are stamped with the message time (read in SDL 3.4's source),
 *   not measured. psyin_sdl_source() returns these as entries.
 *
 *   ---------------------------------------------------------------------
 *   SDL3 (psyin_from_sdl, defined when SDL_events.h came first)
 *   ---------------------------------------------------------------------
 *   Converts keyboard, mouse button and motion, touch, pen and gamepad
 *   events. It reads fields only and calls no SDL function. It drops the
 *   mouse events SDL makes from pen and touch input (which =
 *   SDL_PEN_MOUSEID, SDL_TOUCH_MOUSEID) and the touch events it makes from
 *   a pen (SDL_PEN_TOUCHID) and a mouse, unless ctx.keep_synthetic.
 *   Keys come on the message path, stamped later and coarser, while SDL
 *   text input is on for the window, which another library can start (Dear
 *   ImGui's SDL3 backend does): set ctx.raw_keyboard from psy_screen.h's
 *   caps.raw_keyboard each frame, and those keys get tier 3. Mouse events
 *   with which 0 (SDL's absolute mode, message-timed) get tier 3
 *   (psyin_sdl_mouse_stamp). Gamepad axes become -1..1 (triggers 0..1).
 *   Mouse and pen positions are SDL's window coordinates; touch positions
 *   are 0..1. psy_screen.h starts SDL's video subsystem only: for gamepads
 *   set its desc.gamepads, or call SDL_InitSubSystem(SDL_INIT_GAMEPAD) and
 *   open each gamepad (SDL_OpenGamepad) yourself.
 *
 *   ---------------------------------------------------------------------
 *   RAW MICE (psyin_mouse_report, psyin_from_mouse)
 *   ---------------------------------------------------------------------
 *   psy_screen.h's desc.raw_mice (Windows) reads each mouse's Raw Input on
 *   a thread into psyin_mouse_reports. psyin_from_mouse() turns one report
 *   into up to 12 events of PSYIN_KIND_MOUSE with the
 *   mouse's device id, in this order: presses, releases (buttons numbered
 *   as SDL's: 1 left, 2 middle, 3 right, 4 and 5 the side buttons), a
 *   PSYIN_AXIS_DELTA sample (x, y in counts, unaccelerated) or a
 *   PSYIN_AXIS_ABSOLUTE sample (a tablet, remote desktop: x, y 0..1), a
 *   PSYIN_AXIS_WHEEL sample (value in notches, x the horizontal wheel). A
 *   mouse SDL does not list has PSYIN_UNLISTED. psyin_raw_mouse_source() is
 *   its source entry (stamped at read; host part measured with injected
 *   input only).
 *
 *   ---------------------------------------------------------------------
 *   LICENSE: public domain / MIT-0, see end of file.
 */
#ifndef PSY_INPUT_H_INCLUDED
#define PSY_INPUT_H_INCLUDED

#define PSYIN_VERSION_MAJOR 0
#define PSYIN_VERSION_MINOR 3
#define PSYIN_VERSION_PATCH 0
#define PSYIN_VERSION_STRING "0.3.0"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>   /* memset in the inline adapters */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum psyin_kind {
    PSYIN_KIND_KEYBOARD = 0,  /* zero, so a zeroed choice is a key        */
    PSYIN_KIND_MOUSE    = 1,
    PSYIN_KIND_TOUCH    = 2,
    PSYIN_KIND_PEN      = 3,  /* drawing tablet, stylus                   */
    PSYIN_KIND_GAMEPAD  = 4,
    PSYIN_KIND_BOX      = 5,  /* response box, button box, pedal, device  */
    PSYIN_KIND_EYE      = 6,  /* eye tracker                              */
    PSYIN_KIND_SYNC     = 7,  /* a timing event, not a response: scanner
                               * pulse, TTL input, photodiode edge        */
    PSYIN_KIND_USER     = 8   /* 8..15: yours                             */
} psyin_kind;

#define PSYIN_KIND_BIT(k) ((uint16_t)(1u << (k)))
#define PSYIN_KINDS_ALL   ((uint16_t)0xFFFFu)

typedef enum psyin_type {
    PSYIN_NONE      = 0,      /* refused: a zeroed input is no event      */
    PSYIN_PRESS     = 1,      /* key, button, pen tip, contact down       */
    PSYIN_RELEASE   = 2,
    PSYIN_SAMPLE    = 3,      /* a position or an axis value              */
    PSYIN_PROXIMITY = 4       /* value 1 in range, 0 out                  */
} psyin_type;

#define PSYIN_REPEAT    0x01u  /* the OS's auto-repeat of a held key   */
#define PSYIN_SYNTHETIC 0x02u  /* made from another device's input     */
#define PSYIN_ERASER    0x04u  /* pen: the eraser end                  */
#define PSYIN_UNLISTED  0x08u  /* a raw mouse SDL does not list        */
#define PSYIN_DEVTICKS  0x10u  /* ticks holds the device clock's count */

/* SAMPLE controls. Pen axes follow SDL_PenAxis (pressure 0, x tilt 1, y
 * tilt 2, distance 3, rotation 4, slider 5, tangential pressure 6);
 * gamepad axes SDL_GamepadAxis (left x 0, left y 1, right x 2, right y 3,
 * left trigger 4, right trigger 5). */
#define PSYIN_AXIS_POSITION     0u
#define PSYIN_AXIS_PEN          16u
#define PSYIN_AXIS_PEN_PRESSURE 16u
#define PSYIN_AXIS_GAMEPAD      32u
/* psy_screen.h's raw mice (desc.raw_mice): x, y the movement in counts,
 * unaccelerated; x, y a position 0..1 on the display or the virtual
 * desktop (tablets, remote desktop); value the wheel in notches, x the
 * horizontal wheel. */
#define PSYIN_AXIS_DELTA        1u
#define PSYIN_AXIS_ABSOLUTE     2u
#define PSYIN_AXIS_WHEEL        3u

/* Eye trackers (kind EYE): controls, and the eye in `code`. GAZE is a
 * position, so it is PSYIN_AXIS_POSITION's value. */
#define PSYIN_EYE_GAZE          0u
#define PSYIN_EYE_FIXATION      1u
#define PSYIN_EYE_SACCADE       2u
#define PSYIN_EYE_BLINK         3u
#define PSYIN_EYE_LEFT          0u
#define PSYIN_EYE_RIGHT         1u
#define PSYIN_EYE_BOTH          2u

/* unc_us when the uncertainty is 65.5 ms or more. */
#define PSYIN_UNC_MAX           65535u

typedef enum psyin_tier {
    PSYIN_TIER_UNKNOWN = 0,
    PSYIN_TIER_1       = 1,
    PSYIN_TIER_2       = 2,
    PSYIN_TIER_3       = 3,
    PSYIN_TIER_SIM     = 4
} psyin_tier;

/* One timestamped input event. 48 bytes. */
typedef struct psyin_event {
    int64_t  t;          /* psy_rt ns                                      */
    uint32_t device;     /* instance within the kind; 0 = unknown          */
    uint32_t control;    /* scancode, button, contact, axis (THE EVENT)    */
    uint32_t code;       /* keyboard: SDL keycode; else yours              */
    uint16_t mods;       /* keyboard: SDL_Keymod                           */
    uint8_t  kind;       /* psyin_kind                                    */
    uint8_t  type;       /* psyin_type                                    */
    uint8_t  flags;      /* PSYIN_*                                    */
    uint8_t  stamp;      /* psyin_tier; 0 = the source table's            */
    uint16_t unc_us;     /* this event's uncertainty, us; 0 = the source's */
    float    x, y;
    float    value;
    float    aux;
    uint32_t ticks;      /* PSYIN_DEVTICKS: the device clock, low 32 bits  */
} psyin_event;

/* How a kind (or one device of it) is stamped. Zero: UNKNOWN, no bounds. */
typedef struct psyin_source {
    uint8_t     kind;
    uint8_t     tier;          /* psyin_tier                              */
    uint8_t     partial;       /* 1: the bounds cover the host part only   */
    uint8_t     reserved_;
    uint32_t    device;        /* 0 = every device of the kind             */
    int32_t     lo_us, hi_us;  /* stamp minus event time; both 0 = unknown */
    const char* note;          /* one line for the data file               */
} psyin_source;

/* The stamp tier of an SDL mouse event by its `which`: 0 is SDL's absolute
 * mode, stamped with the window message's time (tier 3); another id is a
 * Raw Input device (relative mode), the source table's. */
static inline uint8_t psyin_sdl_mouse_stamp(uint32_t which) {
    return (uint8_t)(which ? 0 : PSYIN_TIER_3);
}

/* One raw mouse report: what a Raw Input reader (psy_screen.h's
 * desc.raw_mice) reads for one mouse at once. 32 bytes. */
#define PSYIN_MOUSE_LEFT   0x01u   /* .down and .up: buttons                    */
#define PSYIN_MOUSE_RIGHT  0x02u
#define PSYIN_MOUSE_MIDDLE 0x04u
#define PSYIN_MOUSE_X1     0x08u
#define PSYIN_MOUSE_X2     0x10u
#define PSYIN_MOUSE_ABSOLUTE        0x01u  /* .flags: dx, dy are a position, 0..65535 */
#define PSYIN_MOUSE_VIRTUAL_DESKTOP 0x02u  /* ... over the virtual desktop         */
#define PSYIN_MOUSE_UNLISTED        0x04u  /* a device SDL does not list           */
typedef struct psyin_mouse_report {
    int64_t  t;          /* psy_rt ns: when the reader read the report      */
    uint32_t device;     /* the Raw Input handle: SDL's mouse id; 0 = injected */
    int32_t  dx, dy;     /* counts, unaccelerated; with ABSOLUTE a position */
    int16_t  wheel;      /* 120 per notch, away from the user positive      */
    int16_t  hwheel;     /* 120 per notch, right positive                   */
    uint8_t  down, up;   /* PSYIN_MOUSE_* pressed, released in this report  */
    uint8_t  flags;      /* PSYIN_MOUSE_ABSOLUTE, _VIRTUAL_DESKTOP, _UNLISTED */
    uint8_t  reserved_;
    uint32_t reserved2_;
} psyin_mouse_report;

/* One raw mouse report (psy_screen.h's desc.raw_mice) into up to 12
 * inputs, in this order: presses, releases, the movement (a DELTA or
 * ABSOLUTE sample), the wheel (a WHEEL sample). Buttons are numbered as
 * SDL's: left 1, middle 2, right 3, X1 4, X2 5. Returns how many, at most
 * cap. The stamp tier comes from the source table (psyin_raw_mouse_source). */
static inline int psyin_from_mouse(const psyin_mouse_report* m, psyin_event* out, int cap) {
    static const uint8_t bit[5] = { PSYIN_MOUSE_LEFT, PSYIN_MOUSE_MIDDLE, PSYIN_MOUSE_RIGHT,
                                    PSYIN_MOUSE_X1, PSYIN_MOUSE_X2 };
    psyin_event in;
    int n = 0, pass, i;
    memset(&in, 0, sizeof in);
    in.t = m->t;
    in.kind = (uint8_t)PSYIN_KIND_MOUSE;
    in.device = m->device;
    in.flags = (uint8_t)((m->flags & PSYIN_MOUSE_UNLISTED) ? PSYIN_UNLISTED : 0u);
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < 5; i++)
            if ((pass ? m->up : m->down) & bit[i]) {
                if (n >= cap) return n;
                out[n] = in;
                out[n].type = (uint8_t)(pass ? PSYIN_RELEASE : PSYIN_PRESS);
                out[n].control = (uint32_t)(i + 1);
                out[n].value = pass ? 0.0f : 1.0f;
                n++;
            }
    if ((m->dx || m->dy || (m->flags & PSYIN_MOUSE_ABSOLUTE)) && n < cap) {
        out[n] = in;
        out[n].type = (uint8_t)PSYIN_SAMPLE;
        if (m->flags & PSYIN_MOUSE_ABSOLUTE) {
            out[n].control = PSYIN_AXIS_ABSOLUTE;
            out[n].x = (float)m->dx / 65535.0f;
            out[n].y = (float)m->dy / 65535.0f;
        } else {
            out[n].control = PSYIN_AXIS_DELTA;
            out[n].x = (float)m->dx;
            out[n].y = (float)m->dy;
        }
        n++;
    }
    if ((m->wheel || m->hwheel) && n < cap) {
        out[n] = in;
        out[n].type = (uint8_t)PSYIN_SAMPLE;
        out[n].control = PSYIN_AXIS_WHEEL;
        out[n].value = (float)m->wheel / 120.0f;
        out[n].x = (float)m->hwheel / 120.0f;
        n++;
    }
    return n;
}

/* The source table entry for raw mice: stamped like SDL's raw keyboard,
 * when the reader read the report; bounds not stated (measured means of
 * 0.15 to 0.40 ms after SendInput, injected input only). */
static inline psyin_source psyin_raw_mouse_source(void) {
    psyin_source s;
    memset(&s, 0, sizeof s);
    s.kind = (uint8_t)PSYIN_KIND_MOUSE;
    s.tier = (uint8_t)PSYIN_TIER_UNKNOWN;
    s.partial = 1;
    s.note = "psy_screen raw mice: stamped at read; host part 0.15 to 0.40 ms (means, injected); USB poll not measured";
    return s;
}




#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PSY_INPUT_H_INCLUDED */

/* The SDL3 adapter, when SDL_events.h came first. Outside the include
 * guard: a file that includes SDL3 after this header includes it again
 * and gets the adapter then (psy_screen.h's implementation does). */
#if defined(SDL_events_h_) && !defined(PSY_INPUT_SDL_INCLUDED)
#define PSY_INPUT_SDL_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

typedef struct psyin_sdl_ctx {
    bool raw_keyboard;     /* SDL's raw keyboard path is on (psy_screen.h's
                            * caps.raw_keyboard, read each frame: another
                            * library can start text input): keys get the
                            * source table's tier; off, the message path's
                            * tier 3                                      */
    bool keep_synthetic;   /* keep pen- and touch-made mouse events and
                            * pen- and mouse-made touch events, flagged   */
} psyin_sdl_ctx;

/* One SDL event at t_rt (psyscr_poll()'s time) into *out. Returns 1 for
 * keyboard, mouse button and motion, touch, pen and gamepad events; 0 for
 * the rest and for dropped synthetic ones. ctx may be NULL (raw keyboard
 * path, synthetic events dropped). Calls no SDL function. */
static inline int psyin_from_sdl(const SDL_Event* ev, int64_t t_rt, const psyin_sdl_ctx* ctx,
                                  psyin_event* out) {
    psyin_event in;
    bool keep = ctx && ctx->keep_synthetic;
    memset(&in, 0, sizeof in);
    in.t = t_rt;
    switch (ev->type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        in.kind = (uint8_t)PSYIN_KIND_KEYBOARD;
        in.type = (uint8_t)(ev->key.down ? PSYIN_PRESS : PSYIN_RELEASE);
        in.device = (uint32_t)ev->key.which;
        in.control = (uint32_t)ev->key.scancode;
        in.code = (uint32_t)ev->key.key;
        in.mods = (uint16_t)ev->key.mod;
        in.flags = (uint8_t)(ev->key.repeat ? PSYIN_REPEAT : 0u);
        in.stamp = (uint8_t)(ctx && !ctx->raw_keyboard ? PSYIN_TIER_3 : 0);
        in.value = ev->key.down ? 1.0f : 0.0f;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (ev->button.which == SDL_TOUCH_MOUSEID || ev->button.which == SDL_PEN_MOUSEID) {
            if (!keep) return 0;
            in.flags = (uint8_t)PSYIN_SYNTHETIC;
        }
        in.kind = (uint8_t)PSYIN_KIND_MOUSE;
        in.type = (uint8_t)(ev->button.down ? PSYIN_PRESS : PSYIN_RELEASE);
        in.device = (uint32_t)ev->button.which;
        in.stamp = psyin_sdl_mouse_stamp((uint32_t)ev->button.which);
        in.control = ev->button.button;
        in.x = ev->button.x;
        in.y = ev->button.y;
        in.value = ev->button.down ? 1.0f : 0.0f;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (ev->motion.which == SDL_TOUCH_MOUSEID || ev->motion.which == SDL_PEN_MOUSEID) {
            if (!keep) return 0;
            in.flags = (uint8_t)PSYIN_SYNTHETIC;
        }
        in.kind = (uint8_t)PSYIN_KIND_MOUSE;
        in.type = (uint8_t)PSYIN_SAMPLE;
        in.device = (uint32_t)ev->motion.which;
        in.stamp = psyin_sdl_mouse_stamp((uint32_t)ev->motion.which);
        in.control = PSYIN_AXIS_POSITION;
        in.x = ev->motion.x;
        in.y = ev->motion.y;
        break;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_CANCELED:
        if (ev->tfinger.touchID == SDL_PEN_TOUCHID || ev->tfinger.touchID == SDL_MOUSE_TOUCHID) {
            if (!keep) return 0;
            in.flags = (uint8_t)PSYIN_SYNTHETIC;
        }
        in.kind = (uint8_t)PSYIN_KIND_TOUCH;
        in.type = (uint8_t)(ev->type == SDL_EVENT_FINGER_DOWN ? PSYIN_PRESS
                            : ev->type == SDL_EVENT_FINGER_MOTION ? PSYIN_SAMPLE : PSYIN_RELEASE);
        in.device = (uint32_t)ev->tfinger.touchID;
        in.control = (uint32_t)ev->tfinger.fingerID;
        in.x = ev->tfinger.x;
        in.y = ev->tfinger.y;
        in.value = ev->tfinger.pressure;
        break;
    case SDL_EVENT_PEN_DOWN:
    case SDL_EVENT_PEN_UP:
        in.kind = (uint8_t)PSYIN_KIND_PEN;
        in.type = (uint8_t)(ev->ptouch.down ? PSYIN_PRESS : PSYIN_RELEASE);
        in.device = (uint32_t)ev->ptouch.which;
        in.control = 0;
        in.flags = (uint8_t)(ev->ptouch.eraser ? PSYIN_ERASER : 0u);
        in.x = ev->ptouch.x;
        in.y = ev->ptouch.y;
        in.value = ev->ptouch.down ? 1.0f : 0.0f;
        break;
    case SDL_EVENT_PEN_BUTTON_DOWN:
    case SDL_EVENT_PEN_BUTTON_UP:
        in.kind = (uint8_t)PSYIN_KIND_PEN;
        in.type = (uint8_t)(ev->pbutton.down ? PSYIN_PRESS : PSYIN_RELEASE);
        in.device = (uint32_t)ev->pbutton.which;
        in.control = ev->pbutton.button;
        in.x = ev->pbutton.x;
        in.y = ev->pbutton.y;
        in.value = ev->pbutton.down ? 1.0f : 0.0f;
        break;
    case SDL_EVENT_PEN_MOTION:
        in.kind = (uint8_t)PSYIN_KIND_PEN;
        in.type = (uint8_t)PSYIN_SAMPLE;
        in.device = (uint32_t)ev->pmotion.which;
        in.control = PSYIN_AXIS_POSITION;
        in.x = ev->pmotion.x;
        in.y = ev->pmotion.y;
        break;
    case SDL_EVENT_PEN_AXIS:
        in.kind = (uint8_t)PSYIN_KIND_PEN;
        in.type = (uint8_t)PSYIN_SAMPLE;
        in.device = (uint32_t)ev->paxis.which;
        in.control = PSYIN_AXIS_PEN + (uint32_t)ev->paxis.axis;
        in.x = ev->paxis.x;
        in.y = ev->paxis.y;
        in.value = ev->paxis.value;
        break;
    case SDL_EVENT_PEN_PROXIMITY_IN:
    case SDL_EVENT_PEN_PROXIMITY_OUT:
        in.kind = (uint8_t)PSYIN_KIND_PEN;
        in.type = (uint8_t)PSYIN_PROXIMITY;
        in.device = (uint32_t)ev->pproximity.which;
        in.value = ev->type == SDL_EVENT_PEN_PROXIMITY_IN ? 1.0f : 0.0f;
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        in.kind = (uint8_t)PSYIN_KIND_GAMEPAD;
        in.type = (uint8_t)(ev->gbutton.down ? PSYIN_PRESS : PSYIN_RELEASE);
        in.device = (uint32_t)ev->gbutton.which;
        in.control = ev->gbutton.button;
        in.value = ev->gbutton.down ? 1.0f : 0.0f;
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        in.kind = (uint8_t)PSYIN_KIND_GAMEPAD;
        in.type = (uint8_t)PSYIN_SAMPLE;
        in.device = (uint32_t)ev->gaxis.which;
        in.control = PSYIN_AXIS_GAMEPAD + ev->gaxis.axis;
        /* -32768 would map past -1; SDL's triggers run 0..32767 */
        in.value = ev->gaxis.value <= -32767 ? -1.0f : (float)ev->gaxis.value / 32767.0f;
        break;
    default:
        return 0;
    }
    *out = in;
    return 1;
}

/* The source table entry for an SDL event kind, from what was measured or
 * read (SOURCES). Windows only; elsewhere UNKNOWN with no bounds. */
static inline psyin_source psyin_sdl_source(const psyin_sdl_ctx* ctx, int kind) {
    psyin_source s;
    memset(&s, 0, sizeof s);
    s.kind = (uint8_t)kind;
#if defined(_WIN32)
    if (kind == PSYIN_KIND_KEYBOARD && (!ctx || ctx->raw_keyboard)) {
        s.tier = (uint8_t)PSYIN_TIER_UNKNOWN;
        s.partial = 1;
        s.lo_us = 160;
        s.hi_us = 670;
        s.note = "SDL 3.4 raw keyboard: host part measured with SendInput; scan and USB poll not measured";
    } else if (kind == PSYIN_KIND_KEYBOARD) {
        s.tier = (uint8_t)PSYIN_TIER_3;
        s.partial = 1;
        s.lo_us = -6200;
        s.hi_us = 12100;
        s.note = "SDL 3.4 keyboard message path: message tick, measured with SendInput";
    } else if (kind == PSYIN_KIND_MOUSE || kind == PSYIN_KIND_TOUCH || kind == PSYIN_KIND_PEN) {
        s.tier = (uint8_t)PSYIN_TIER_3;
        s.note = "SDL 3.4 window message time (ms tick), not measured";
    } else {
        s.note = "not measured";
    }
#else
    (void)ctx;
    s.note = "not measured on this platform";
#endif
    return s;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SDL_events_h_ */

/* ------------------------------------------------------------------------
 * This software is available under the MIT-0 (MIT No Attribution) license.
 *
 * Copyright (c) 2026 psy contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY.
 * ------------------------------------------------------------------------ */
