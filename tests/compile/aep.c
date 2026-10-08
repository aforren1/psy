/* Compile check: ysp/aep.h as a C translation unit with the implementation
 * enabled. CMake and CI build this as C11 with warnings as errors; aep.cpp
 * builds the same source as C++17. The header has no threading and no platform
 * backend, so there is no second configuration to check. It runs and returns 0
 * to prove it links, which for this header also means libm resolved. */
#define YSP_AEP_IMPLEMENTATION
#include "ysp/aep.h"

int main(void) { return 0; }
