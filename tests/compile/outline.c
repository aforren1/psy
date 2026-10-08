/* Compile check: ysp/outline.h as a C translation unit with the
 * implementation enabled. CMake and CI build this as C11 with warnings as
 * errors; outline.cpp builds the same source as C++17. The header has no
 * threads, so there is no YOL_NO_THREADS variant. It runs and returns 0 to
 * prove it links. */
#define YSP_OUTLINE_IMPLEMENTATION
#include "ysp/outline.h"

int main(void) { return yol_version()[0] == '0' ? 0 : 1; }
