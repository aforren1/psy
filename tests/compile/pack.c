/* Compile check: ysp/pack.h as a C translation unit with the implementation
 * and the file source. CMake and CI build this as C11 with warnings as
 * errors; pack.cpp builds the same source as C++17. It checks two hash
 * vectors and returns 0 to prove it links and runs. */
#define YSP_PACK_IMPLEMENTATION
#include "ysp/pack.h"

int main(void) {
    uint8_t d[32];
    ypak_sha256("abc", 3, d);
    if (d[0] != 0xBA || d[31] != 0xAD) return 1;
    if (ypak_xxh64("", 0, 0) != 0xEF46DB3751D8E999ULL) return 2;
    if (ypak_crc32(0, "123456789", 9) != 0xCBF43926u) return 3;
    return ypak_kind_id("table") == YPAK_KIND_TABLE ? 0 : 4;
}
