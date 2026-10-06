/* Compile check: psy_timeline.h as a C translation unit with the
 * implementation enabled. CMake and CI build this as C11 with warnings as
 * errors; psy_timeline.cpp builds the same source as C++17. The header has
 * no threads, so there is no PSYTL_NO_THREADS variant. It runs and returns 0
 * to prove it links, and that a sequence built by calls and one built from
 * an op table both apply. */
#define PSY_TIMELINE_IMPLEMENTATION
#include "psy_timeline.h"

#include <string.h>

static int open_tl(psytl_timeline* tl, psytl_event* ev, psytl_key* keys) {
    psytl_desc d;
    memset(&d, 0, sizeof d);
    d.events = ev;
    d.event_capacity = 16;
    d.n_channels = 3;
    d.keys = keys;
    d.key_capacity = 32;
    return psytl_open(tl, &d) ? 0 : 1;
}

int main(void) {
    static psytl_event ev[16];
    static psytl_key keys[32];
    static psytl_timeline tl;
    psytl_tween_desc up;
    psytl_seq q;
    if (open_tl(&tl, ev, keys)) return 1;
    /* The builder: C99 and C++17 alike. */
    memset(&up, 0, sizeof up);
    up.to = 0.5f;
    up.duration = PSYTL_MS(100);
    up.ease = PSYTL_EASE_COSINE;
    q = psytl_seq_on(&tl, 1);
    psytl_on(&q, 0);
    psytl_wait(&q, PSYTL_MS(500));
    psytl_off(&q, 0);
    psytl_trigger(&q, 12);
    psytl_to(&q, 2, &up);
    psytl_wait(&q, PSYTL_MS(600));
    up.to = 0.0f;
    psytl_then(&q, 2, &up);
    if (q.err != 0) return 1;
#ifndef __cplusplus
    /* The op table: designated initializers, C99 (C++20 in
     * psy_timeline_ops.cpp). */
    {
        static const psytl_op trial[] = {
            PSYTL_ON(0),
            PSYTL_WAIT(PSYTL_MS(500)),
            PSYTL_OFF(0), PSYTL_SET_VALUE(1, 2.0f), PSYTL_TRIGGER(12), PSYTL_MARK(3, 7),
            PSYTL_TO(2, .to = 0.5f, .duration = PSYTL_MS(100), .ease = PSYTL_EASE_COSINE),
            PSYTL_WAIT(PSYTL_MS(600)),
            PSYTL_THEN(2, .to = 0.0f, .duration = PSYTL_MS(100), .ease = PSYTL_EASE_COSINE),
            PSYTL_AT(0),
        };
        int bad = 0;
        if (open_tl(&tl, ev, keys)) return 1;
        if (psytl_check_ops(&tl, 1, trial, PSYTL_COUNT(trial), &bad) != 0) return 1;
        q = psytl_seq_on(&tl, 1);
        if (psytl_run(&q, trial, PSYTL_COUNT(trial)) != 0) return 1;
    }
#endif
    return 0;
}
