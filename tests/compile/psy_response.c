/* Compile check: psy_response.h as a C translation unit with the
 * implementation. CMake and CI build this as C11 with warnings as errors;
 * psy_response.cpp builds the same source as C++17. The header has no
 * threads. One press in a window returns 0 to prove it links and runs. */
#define PSY_RESPONSE_IMPLEMENTATION
#include "psy_response.h"

#include <string.h>

int main(void) {
    static psyrsp_collector c;
    psyrsp_desc d;
    psyrsp_onset o;
    psyrsp_input in;
    psyrsp_result r;
    memset(&d, 0, sizeof(d));
    memset(&o, 0, sizeof(o));
    memset(&in, 0, sizeof(in));
    if (!psyrsp_init(&c, &d)) return 1;
    psyrsp_arm(&c, 0);
    o.t = 1000;
    o.final = 1;
    psyrsp_set_onset(&c, &o);
    in.t = 5000;
    in.type = PSYRSP_PRESS;
    in.control = 9;
    if (psyrsp_feed(&c, &in) != PSYRSP_ENDED) return 1;
    psyrsp_finish(&c, &r);
    return r.t_response == 5000 ? 0 : 1;
}
