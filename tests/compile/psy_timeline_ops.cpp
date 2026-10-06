/* Compile check: psy_timeline.h's op table macros (SEQUENCES) as C++20.
 * They are designated initializers, which C++ has from C++20 on, and only
 * in declaration order. CMake builds this one target as C++20, and with
 * -Wno-missing-field-initializers on g++, which warns about every field a
 * designated initializer leaves out even though zero is the default. Built
 * as an earlier C++, it compiles to an empty program. */
#define PSY_TIMELINE_IMPLEMENTATION
#include "psy_timeline.h"

#if __cplusplus >= 202002L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
static const psytl_op trial[] = {
    PSYTL_ON(0),
    PSYTL_WAIT(PSYTL_MS(500)),
    PSYTL_OFF(0), PSYTL_SET_VALUE(1, 2.0f), PSYTL_TRIGGER(12), PSYTL_MARK(3, 7),
    PSYTL_TO(2, .to = 0.5f, .duration = PSYTL_MS(100), .ease = PSYTL_EASE_COSINE),
    PSYTL_WAIT(PSYTL_MS(600)),
    PSYTL_THEN(2, .to = 0.0f, .duration = PSYTL_MS(100), .ease = PSYTL_EASE_COSINE),
    PSYTL_AT(0),
};

int main() {
    static psytl_event ev[16];
    static psytl_key keys[32];
    static psytl_timeline tl;
    psytl_desc d = {};
    d.events = ev;
    d.event_capacity = 16;
    d.n_channels = 3;
    d.keys = keys;
    d.key_capacity = 32;
    if (!psytl_open(&tl, &d)) return 1;
    psytl_seq q = psytl_seq_on(&tl, 1);
    return psytl_run(&q, trial, PSYTL_COUNT(trial)) != 0;
}
#else
int main() { return 0; }
#endif
