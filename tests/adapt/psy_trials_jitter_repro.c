/* psy_trials_jitter_repro.c - the jitter golden digests alone, for builds
 * the full test cannot take: -Ofast (whose finite-math assumption breaks
 * the full test's NaN checks) and -march=native. Exit 0 when every digest
 * matches. CI:
 *
 *     cc -std=c11 -O3 -march=native -I. tests/adapt/psy_trials_jitter_repro.c -o r -lm && ./r
 *     cc -std=c11 -Ofast -I. tests/adapt/psy_trials_jitter_repro.c -o r -lm && ./r
 */
#define PSY_TRIALS_IMPLEMENTATION
#include "psy_trials.h"
#include "psy_trials_jitter_golden.h"

int main(void) {
    int bad = jg_check(1);
    printf("psy_trials_jitter_repro: %s\n", bad ? "DIGESTS DIFFER" : "all digests match");
    return bad ? 1 : 0;
}
