/* Compile check: psy_outline.h as a C translation unit with the
 * implementation enabled. CMake and CI build this as C11 with warnings as
 * errors; psy_outline.cpp builds the same source as C++17. The header has no
 * threads, so there is no PSYOL_NO_THREADS variant. It runs and returns 0 to
 * prove it links. */
#define PSY_OUTLINE_IMPLEMENTATION
#include "psy_outline.h"

int main(void) { return psyol_version()[0] == '0' ? 0 : 1; }
