/* Compile check: ysp/box.h as a C translation unit, with its implementation.
 * CMake and CI build this as C11 with warnings as errors; box.cpp builds the
 * same source as C++17. It decodes one line-protocol edge and returns 0. */
#define YSP_BOX_IMPLEMENTATION
#include "ysp/box.h"

#include <string.h>

int main(void) {
    static const char line[] = "E 1000 1 1\n";
    ybox_decoder d;
    yin_event ev[4];
    ybox_pair pair[4];
    ybox_out out;
    memset(&out, 0, sizeof out);
    out.ev = ev; out.ev_cap = 4; out.pair = pair; out.pair_cap = 4;
    if (!ybox_init(&d, YBOX_LINE, YIN_KIND_SYNC, 1)) return 1;
    (void)ybox_decode(&d, (const uint8_t*)line, sizeof line - 1, 5, &out);
    return out.n_ev == 1 && ev[0].ticks == 1000u && out.n_pair == 1 ? 0 : 1;
}
