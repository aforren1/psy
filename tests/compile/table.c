/* Compile check: ysp/table.h as a C translation unit with the implementation
 * and the opt-in file loader enabled. CMake and CI build this as C11 with
 * warnings as errors; table.cpp builds the same source as C++17. The
 * header has no threads. It parses a two-line table and returns 0 to prove
 * it links and runs. */
#define YSP_TABLE_IMPLEMENTATION
#define YTB_STDIO
#include "ysp/table.h"

#include <string.h>

int main(void) {
    static uint64_t arena[1024];
    ytb_table t;
    ytb_csv_desc d;
    static const char csv[] = "a,b\n1,x\n";
    memset(&d, 0, sizeof(d));
    d.text = csv;
    d.len = sizeof(csv) - 1;
    d.arena = arena;
    d.arena_size = sizeof(arena);
    return ytb_csv(&t, &d) && ytb_int(&t, 0, 0) == 1 ? 0 : 1;
}
