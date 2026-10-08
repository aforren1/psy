/* Compile check: ysp/serial.h as a C translation unit with the implementation
 * enabled. CMake and CI build this as C11 with warnings as errors, once with
 * threading and once with YSER_NO_THREADS (which drops the async-pulse
 * worker); serial.cpp builds the same source as C++17. It runs and
 * returns 0 to prove it links. */
#define YSP_SERIAL_IMPLEMENTATION
#include "ysp/serial.h"

int main(void) { return 0; }
