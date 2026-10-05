/* Compile check: psy_timeline.h as a C translation unit with the
 * implementation enabled. CMake and CI build this as C11 with warnings as
 * errors; psy_timeline.cpp builds the same source as C++17. The header has
 * no threads, so there is no PSYTL_NO_THREADS variant. It runs and returns 0
 * to prove it links. */
#define PSY_TIMELINE_IMPLEMENTATION
#include "psy_timeline.h"

int main(void) { return 0; }
