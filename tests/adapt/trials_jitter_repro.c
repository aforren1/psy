/* trials_jitter_repro.c - the jitter golden digests alone, for builds
 * the full test cannot take: -Ofast (whose finite-math assumption breaks
 * the full test's NaN checks) and -march=native. Exit 0 when every digest
 * matches. CI:
 *
 *     cc -std=c11 -O3 -march=native -Iinclude tests/adapt/trials_jitter_repro.c -o r -lm && ./r
 *     cc -std=c11 -Ofast -Iinclude tests/adapt/trials_jitter_repro.c -o r -lm && ./r
 */
#define YSP_TRIALS_IMPLEMENTATION
#include "ysp/trials.h"
#include "trials_jitter_golden.h"

int main(void) {
    int bad = jg_check(1);
    printf("trials_jitter_repro: %s\n", bad ? "DIGESTS DIFFER" : "all digests match");
    return bad ? 1 : 0;
}
