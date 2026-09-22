/* Compile check: psy_serial.h as a C translation unit with the implementation
 * enabled. CMake and CI build this as C11 with warnings as errors, once with
 * threading and once with PSYS_NO_THREADS (which drops the async-pulse
 * worker); psy_serial.cpp builds the same source as C++17. It runs and
 * returns 0 to prove it links. */
#define PSY_SERIAL_IMPLEMENTATION
#include "psy_serial.h"

int main(void) { return 0; }
