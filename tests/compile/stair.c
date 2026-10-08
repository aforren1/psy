/* Compile check: ysp/stair.h as a C translation unit with the implementation
 * enabled. CMake and CI build this as C11 with warnings as errors;
 * stair.cpp builds the same source as C++17. The header has no threads,
 * so there is no YST_NO_THREADS variant. It runs and returns 0 to prove it
 * links. */
#define YSP_STAIR_IMPLEMENTATION
#include "ysp/stair.h"

int main(void) { return 0; }
