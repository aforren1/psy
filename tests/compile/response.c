/* Compile check: ysp/response.h as a C translation unit with the
 * implementation. CMake and CI build this as C11 with warnings as errors;
 * response.cpp builds the same source as C++17. The header has no
 * threads. One press in a window returns 0 to prove it links and runs. */
#define YSP_RESPONSE_IMPLEMENTATION
#include "ysp/response.h"

#include <string.h>

int main(void) {
    static yrsp_collector c;
    yrsp_desc d;
    yrsp_onset o;
    yrsp_input in;
    yrsp_result r;
    memset(&d, 0, sizeof(d));
    memset(&o, 0, sizeof(o));
    memset(&in, 0, sizeof(in));
    if (!yrsp_init(&c, &d)) return 1;
    yrsp_arm(&c, 0);
    o.t = 1000;
    o.final = 1;
    yrsp_set_onset(&c, &o);
    in.t = 5000;
    in.type = YRSP_PRESS;
    in.control = 9;
    if (yrsp_feed(&c, &in) != YRSP_ENDED) return 1;
    yrsp_finish(&c, &r);
    return r.t_response == 5000 ? 0 : 1;
}
