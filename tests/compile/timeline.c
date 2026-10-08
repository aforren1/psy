/* Compile check: ysp/timeline.h as a C translation unit with the
 * implementation enabled. CMake and CI build this as C11 with warnings as
 * errors; timeline.cpp builds the same source as C++17. The header has
 * no threads, so there is no YTL_NO_THREADS variant. It runs and returns 0
 * to prove it links, and that a sequence built by calls and one built from
 * an op table both apply. */
#define YSP_TIMELINE_IMPLEMENTATION
#include "ysp/timeline.h"

#include <string.h>

static int open_tl(ytl_timeline* tl, ytl_event* ev, ytl_key* keys) {
    ytl_desc d;
    memset(&d, 0, sizeof d);
    d.events = ev;
    d.event_capacity = 16;
    d.n_channels = 3;
    d.keys = keys;
    d.key_capacity = 32;
    return ytl_open(tl, &d) ? 0 : 1;
}

int main(void) {
    static ytl_event ev[16];
    static ytl_key keys[32];
    static ytl_timeline tl;
    ytl_tween_desc up;
    ytl_seq q;
    if (open_tl(&tl, ev, keys)) return 1;
    /* The builder: C99 and C++17 alike. */
    memset(&up, 0, sizeof up);
    up.to = 0.5f;
    up.duration = YTL_MS(100);
    up.ease = YTL_EASE_COSINE;
    q = ytl_seq_on(&tl, 1);
    ytl_on(&q, 0);
    ytl_wait(&q, YTL_MS(500));
    ytl_off(&q, 0);
    ytl_trigger(&q, 12);
    ytl_to(&q, 2, &up);
    ytl_wait(&q, YTL_MS(600));
    up.to = 0.0f;
    ytl_then(&q, 2, &up);
    if (q.err != 0) return 1;
#ifndef __cplusplus
    /* The op table: designated initializers, C99 (C++20 in
     * timeline_ops.cpp). */
    {
        static const ytl_op trial[] = {
            YTL_ON(0),
            YTL_WAIT(YTL_MS(500)),
            YTL_OFF(0), YTL_SET_VALUE(1, 2.0f), YTL_TRIGGER(12), YTL_MARK(3, 7),
            YTL_TO(2, .to = 0.5f, .duration = YTL_MS(100), .ease = YTL_EASE_COSINE),
            YTL_WAIT(YTL_MS(600)),
            YTL_THEN(2, .to = 0.0f, .duration = YTL_MS(100), .ease = YTL_EASE_COSINE),
            YTL_AT(0),
        };
        int bad = 0;
        if (open_tl(&tl, ev, keys)) return 1;
        if (ytl_check_ops(&tl, 1, trial, YTL_COUNT(trial), &bad) != 0) return 1;
        q = ytl_seq_on(&tl, 1);
        if (ytl_run(&q, trial, YTL_COUNT(trial)) != 0) return 1;
    }
#endif
    return 0;
}
