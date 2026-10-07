/* Compile check: psy_table.h as a C translation unit with the implementation
 * and the opt-in file loader enabled. CMake and CI build this as C11 with
 * warnings as errors; psy_table.cpp builds the same source as C++17. The
 * header has no threads. It parses a two-line table and returns 0 to prove
 * it links and runs. */
#define PSY_TABLE_IMPLEMENTATION
#define PSYTB_STDIO
#include "psy_table.h"

#include <string.h>

int main(void) {
    static uint64_t arena[1024];
    psytb_table t;
    psytb_csv_desc d;
    static const char csv[] = "a,b\n1,x\n";
    memset(&d, 0, sizeof(d));
    d.text = csv;
    d.len = sizeof(csv) - 1;
    d.arena = arena;
    d.arena_size = sizeof(arena);
    return psytb_csv(&t, &d) && psytb_int(&t, 0, 0) == 1 ? 0 : 1;
}
