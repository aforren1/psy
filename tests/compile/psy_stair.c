/* Compile check: psy_stair.h as a C translation unit with the implementation
 * enabled. CMake and CI build this as C11 with warnings as errors;
 * psy_stair.cpp builds the same source as C++17. The header has no threads,
 * so there is no PSYST_NO_THREADS variant. It runs and returns 0 to prove it
 * links. */
#define PSY_STAIR_IMPLEMENTATION
#include "psy_stair.h"

int main(void) { return 0; }
