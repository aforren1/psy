/* Compile check: ysp/rdk.h as a C translation unit with the implementation
 * enabled. CMake and CI build this as C11 with warnings as errors;
 * rdk.cpp builds the same source as C++17. The header has no threads,
 * so there is no YRDK_NO_THREADS variant. It runs and returns 0 to prove
 * it links and that a field opens, starts and moves. */
#define YSP_RDK_IMPLEMENTATION
#include "ysp/rdk.h"

#include <string.h>

int main(void) {
    static yrdk_field f;
    static float xy[2 * 50];
    yrdk_desc d;
    memset(&d, 0, sizeof d);
    d.w = 10;
    d.count = 50;
    d.coherence = 0.5f;
    d.speed = 2;
    if (yrdk_open(&f, &d) < 0) return 1;
    if (yrdk_start(&f, 1, 0) < 0) return 1;
    if (yrdk_update(&f, 16666667) != 50) return 1;
    if (yrdk_xy(&f, xy) != 50) return 1;
    yrdk_close(&f);
    return yrdk_version()[0] == '0' ? 0 : 1;
}
