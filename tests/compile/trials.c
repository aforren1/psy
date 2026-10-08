/* Compile check: ysp/trials.h as a C translation unit with the implementation
 * enabled. CMake and CI build this as C11 with warnings as errors;
 * trials.cpp builds the same source as C++17. The header has no threads,
 * so there is no YTR_NO_THREADS variant. It runs and returns 0 to prove it
 * links. */
#define YSP_TRIALS_IMPLEMENTATION
#include "ysp/trials.h"

int main(void) { return 0; }
