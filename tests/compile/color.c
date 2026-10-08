/* Compile check: ysp/color.h as a C translation unit with the implementation
 * enabled. CMake and CI build this as C11 with warnings as errors;
 * color.cpp builds the same source as C++17. The header has no threads,
 * so there is no YCOL_NO_THREADS variant. It runs and returns 0 to prove it
 * links. */
#define YSP_COLOR_IMPLEMENTATION
#include "ysp/color.h"

int main(void) { return ycol_version()[0] == '0' ? 0 : 1; }
