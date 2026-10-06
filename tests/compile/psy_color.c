/* Compile check: psy_color.h as a C translation unit with the implementation
 * enabled. CMake and CI build this as C11 with warnings as errors;
 * psy_color.cpp builds the same source as C++17. The header has no threads,
 * so there is no PSYCOL_NO_THREADS variant. It runs and returns 0 to prove it
 * links. */
#define PSY_COLOR_IMPLEMENTATION
#include "psy_color.h"

int main(void) { return psycol_version()[0] == '0' ? 0 : 1; }
