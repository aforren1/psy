/* Compile check: psy_parallel.h as a C translation unit with the implementation
 * enabled. CMake and CI build this with warnings as errors, once as C11 and
 * once with threading disabled. It runs and returns 0 to prove it links. */
#define PSY_PARALLEL_IMPLEMENTATION
#include "psy_parallel.h"

int main(void) { return 0; }
