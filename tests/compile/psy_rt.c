/* Compile check: psy_rt.h as a C translation unit with the implementation
 * enabled. CMake and CI build this as C11 with warnings as errors, once with
 * threading and once with PSYRT_NO_THREADS (which drops the deadline
 * worker); psy_rt.cpp builds the same source as C++17. It runs and returns 0
 * to prove it links. */
#define PSY_RT_IMPLEMENTATION
#include "psy_rt.h"

int main(void) { return 0; }
