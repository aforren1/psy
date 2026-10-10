/* Compile check: ysp/net.h as a C translation unit, with its implementation
 * (which implements ysp/rt.h). CMake and CI build this as C11 with warnings
 * as errors; net.cpp builds the same source as C++17. It loads liblsl from a
 * path that does not exist (the loader must report it, not crash), checks a
 * key, and starts nothing. Returns 0. */
#define YSP_NET_IMPLEMENTATION
#include "ysp/net.h"

#include <string.h>

int main(void) {
    static ynet_lsl lib;
    static unsigned char mem[YNET_STREAM_BYTES(16, 4)];
    static ynet_stream s;
    static ynet_inlet in;
    ynet_inlet_desc d;
    ynet_key k;
    char pred[200];
    int rc = ynet_lsl_load(&lib, "ysp-compile-check-no-such-liblsl");
    int ok = rc == YNET_ERR_NOLIB && lib.error[0] != '\0';
    ok = ok && ynet_key_parse("lsl:Data::LabStreamer:", &k) && ynet_key_predicate(&k, pred, sizeof pred) > 0;
    ok = ok && strcmp(pred, "name='Data' and source_id='LabStreamer'") == 0;
    ok = ok && ynet_stream_init(&s, mem, sizeof mem, 4) && ynet_stream_capacity(&s) == 16;
    memset(&d, 0, sizeof d);
    d.lib = &lib;   /* not loaded: refused */
    d.role = "check";
    d.key = "lsl:Data::";
    d.device = 1;
    ok = ok && !ynet_inlet_start(&in, &d);
    return ok ? 0 : 1;
}
