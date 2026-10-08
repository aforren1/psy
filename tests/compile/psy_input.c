/* Compile check: psy_input.h as a C translation unit. CMake and CI build
 * this as C11 with warnings as errors; psy_input.cpp builds the same
 * source as C++17. The header has no implementation and no threads. It
 * fills one event and returns 0 to prove it builds and runs. */
#include "psy_input.h"

#include <string.h>

int main(void) {
    psyin_event e;
    memset(&e, 0, sizeof(e));
    e.kind = PSYIN_KIND_BOX;
    e.type = PSYIN_PRESS;
    e.control = 1;
    e.value = 1.0f;
    return sizeof(e) == 48 && psyin_sdl_mouse_stamp(0) == PSYIN_TIER_3 ? 0 : 1;
}
