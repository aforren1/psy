/* trials_jitter_dump.c - prints "Y F" for ysp/trials.h's fixed-point
 * -ln(Y / 2^53) (F in Q58) at N values of Y, for
 * tests/compare/trials_jitter_ref.py to check against mpmath:
 *
 *     cc -O2 -Iinclude tests/compare/trials_jitter_dump.c -o dump -lm
 *     ./dump 1000000 > dump.txt
 *     uv run tests/compare/trials_jitter_ref.py check dump.txt
 *
 * Half the values are Y = 2^53 - floor(u 2^53) for a uniform u (what a
 * draw sees), half are spread evenly over the exponents 0..52. */
#define YSP_TRIALS_IMPLEMENTATION
#include "ysp/trials.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    long n = argc > 1 ? atol(argv[1]) : 1000000, i;
    uint64_t st = 777, y;
    for (i = 0; i < n; i++) {
        double u = ytr_splitmix(&st);
        if (i % 2 == 0) {
            y = ((uint64_t)1 << 53) - ytr__u53(u);
        } else {
            int e = (int)(i / 2 % 53);
            uint64_t r = (uint64_t)(ytr_splitmix(&st) * 9007199254740992.0);
            y = ((uint64_t)1 << e) | (e ? (r >> (53 - e)) & (((uint64_t)1 << e) - 1u) : 0);
        }
        printf("%llu %llu\n", (unsigned long long)y, (unsigned long long)ytr__neglog(y));
    }
    return 0;
}
