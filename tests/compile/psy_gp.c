/* Compile check: psy_gp.h as a C translation unit with the implementation
 * enabled. CMake and CI build this as C11 with warnings as errors; psy_gp.cpp
 * builds the same source as C++17. The header has no threading and no platform
 * backend, so there is no second configuration to check. It runs and returns 0
 * to prove it links, which for this header also means libm resolved. */
#define PSY_GP_IMPLEMENTATION
#include "psy_gp.h"

int main(void) { return 0; }
