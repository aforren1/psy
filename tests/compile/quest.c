/* Compile check: ysp/quest.h as a C translation unit with the implementation
 * enabled. CMake and CI build this as C11 with warnings as errors;
 * quest.cpp builds the same source as C++17. The header owns no threads,
 * so there is no no-threads variant. It runs and returns 0 to prove it
 * links. */
#define YSP_QUEST_IMPLEMENTATION
#include "ysp/quest.h"

int main(void) { return 0; }
