/* psy_stair.h - v0.1.1 - public domain single-header adaptive staircase library
 *
 *   Nonparametric adaptive procedures for psychophysics: transformed and
 *   weighted up/down staircases (Wetherill & Levitt 1965; Levitt 1971;
 *   Kaernbach 1991) and accelerated stochastic approximation (Kesten 1958;
 *   Robbins & Monro 1951). One handle is one staircase; interleave several
 *   by owning several handles.
 *
 *   Written in the single-header style of the stb / sokol libraries. Pure
 *   computation: no OS calls, no threads, no heap. Needs nothing but libm.
 *   Not a transport header, so it does not include psy_rt.h.
 *
 *   Targets every platform the compiler does. C++17, C11, or the pre-C11 C
 *   dialect MSVC compiles with by default.
 *
 *   ---------------------------------------------------------------------
 *   CHANGELOG
 *   ---------------------------------------------------------------------
 *   v0.1.1 - version macros (PSYST_VERSION_MAJOR / MINOR / PATCH / STRING)
 *          and psyst_version(), so a program can log which header it was
 *          built from. No behavior changed.
 *   v0.1 - first implementation. Defining PSY_STAIR_IMPLEMENTATION now
 *          compiles the library instead of failing. The manual gained the
 *          detail a specification could leave open and an implementation
 *          cannot: which step size the reversal trial itself takes (the new
 *          one, as in PsychoPy), the sign convention of PSYST_RULE_ASA and
 *          what its m_k counts, which recorded level each estimator
 *          averages, the order the stop criteria are tested in, and the
 *          rest of the descs psyst_open() rejects. PSYST_EVENT_STEP now
 *          means "the rule stepped", not "the value changed", so a step
 *          held by a limit still reports one.
 *   v0.0 - specification. Declarations and the manual, no implementation.
 *
 *   STATUS: v0.1.1. Built and run on Linux (WSL2) with gcc 11.4 as C11, C99 and
 *   C++17 under -Wall -Wextra -Wpedantic -Wshadow -Werror, and once under
 *   -fsanitize=address,undefined -fno-sanitize-recover=all with no
 *   diagnostic. Also built and run on Windows 11 with MSVC 14.51 under
 *   /W4 /WX, both in its default (pre-C11) C dialect and as /std:c++17;
 *   every number the test prints is identical on the two compilers.
 *   tests/adapt/psy_stair_test.c, at PSYST_MAX_TRIALS 128 and
 *   PSYST_MAX_REVERSALS 32, checks hand-derived tracks for 1-up-1-down,
 *   1-up-2-down, 1-up-3-down and 2-up-1-down (proposals, reversal indices,
 *   reversal levels), the step schedule, the initial rule, LIN / LOG / DB steps, the
 *   limits and every stop reason, the weighted rule, ASA, all four
 *   estimators, PSYST_ERR_FULL at PSYST_MAX_TRIALS, and every desc
 *   psyst_open() is documented to reject. A simulated Weibull observer over
 *   204000 pooled trials puts the empirical proportion correct within 0.007
 *   of psyst_convergence_p for 1-up-2-down (0.7105 against 0.7071),
 *   1-up-3-down (0.7970 against 0.7937) and a weighted 1-up-1-down (0.7565
 *   against 0.7500), against a 0.01 tolerance. All three sit ABOVE the
 *   target, which is the finite-step bias of the rule and not noise; shrink
 *   the step to shrink it.
 *   What is NOT done: no run against PsychoPy's StairHandler or Palamedes'
 *   PAL_AMUD. The reversal, schedule and initial-rule semantics here are
 *   read off their sources and re-derived by hand, not replayed against
 *   them; docs/psy_adapt.md makes that comparison the acceptance test and
 *   it needs the bindings. Nothing here is a timing measurement. No
 *   estimator is a fit and none carries a confidence interval. A
 *   -DPSYST_API=static build needs a translation unit that calls every
 *   function, or -Wno-unused-function.
 *
 *   ---------------------------------------------------------------------
 *   USAGE
 *   ---------------------------------------------------------------------
 *   Do this:
 *
 *       #define PSY_STAIR_IMPLEMENTATION
 *
 *   in *one* C or C++ file before including this header to create the
 *   implementation. Every other file just includes the header normally.
 *
 *       #define PSY_STAIR_IMPLEMENTATION
 *       #include "psy_stair.h"
 *
 *       psyst_desc d = {0};
 *       d.start          = 0.5;          // starting level, caller's units
 *       d.n_up           = 1;            // 1-up ...
 *       d.n_down         = 3;            // ... 3-down: converges on 79.4%
 *       d.step_type      = PSYST_STEP_LOG;
 *       d.steps[0]       = 0.3;          // log10 units: a factor of 2
 *       d.steps[1]       = 0.15;         // halved after the first reversal
 *       d.steps[2]       = 0.075;        // and again after the second
 *       d.n_steps        = 3;
 *       d.min            = 0.001;
 *       d.max            = 1.0;
 *       d.stop_reversals = 10;
 *
 *       psyst_stair s;
 *       if (!psyst_open(&s, &d)) { fputs(psyst_error(&s), stderr); return 1; }
 *
 *       while (!psyst_done(&s)) {
 *           double level = psyst_next(&s);
 *           int correct  = run_trial(level);          // 1 = correct, 0 = not
 *           psyst_update(&s, level, correct);
 *       }
 *       double thr = psyst_estimate(&s, PSYST_EST_REVERSALS);
 *
 *   ---------------------------------------------------------------------
 *   MODEL
 *   ---------------------------------------------------------------------
 *   A staircase is a rule that maps the trial history to the next level.
 *   The level is a double in the caller's units (contrast, dB, degrees,
 *   log10 of any of those). The header does not know what the units mean;
 *   it only knows that a CORRECT response (or a detection, a "yes") makes
 *   the task HARDER, which by default means a SMALLER level. Set
 *   desc.harder_is_up when a larger level is the harder one (a delay to
 *   detect, a noise amplitude, a masker contrast).
 *
 *   THE LOOP
 *     psyst_next(s)              the level the rule proposes for this trial
 *     psyst_update(s, x, r)      what was actually shown, and the outcome
 *     psyst_done(s)              has a stop criterion been met
 *     psyst_estimate(s, how)     the threshold from the history so far
 *
 *   next() and update() are separate on purpose. The level you can show is
 *   not always the level the rule asked for: a display quantizes contrast,
 *   a device clips, an interleaved design shows a different staircase's
 *   trial. Pass the level you SHOWED to update(); the rule steps from the
 *   level it PROPOSED, so the two need not agree, and the history records
 *   both. update() with a level the rule did not propose does not reset
 *   anything.
 *
 *   DIRECTION
 *     "Down" and "up" in this manual are directions of the LEVEL VALUE, not
 *     of difficulty: down is a smaller number. psyst_trial.direction is the
 *     sign of the change the rule made to its own level after that trial
 *     (-1, +1, or 0 for no step). With the default harder_is_up = false a
 *     correct response steps down; with harder_is_up = true it steps up, and
 *     then desc.step_down_scale still scales the HARDER step, which is the
 *     one a correct response takes. Reversals do not care: they are a change
 *     of sign either way.
 *
 *   RULES (desc.rule)
 *     PSYST_RULE_UPDOWN  (default)
 *       Transformed up/down: after n_down consecutive correct responses the
 *       level steps down (harder); after n_up consecutive incorrect
 *       responses it steps up (easier). The counters reset on every step
 *       and on every change of direction. 1-up-1-down converges on 50%,
 *       1-up-2-down on 70.7%, 1-up-3-down on 79.4%, 1-up-4-down on 84.1%
 *       (Levitt 1971, Table 1; the value is 0.5^(1/n_down) for n_up = 1).
 *       Weighted up/down (Kaernbach 1991): when step_down_scale != 1 the
 *       down step is step * step_down_scale. With n_up = n_down = 1 the
 *       procedure converges on p = 1 / (1 + step_down_scale), so a
 *       target p is reached with step_down_scale = (1 - p) / p;
 *       psyst_weighted_scale(p) computes it. Unlike the transformed rule,
 *       the weighted rule converges on any p in (0, 1) with one response
 *       per step.
 *     PSYST_RULE_ASA
 *       Accelerated stochastic approximation (Kesten 1958). After each
 *       trial the level moves TOWARD THE HARDER END by c / m_k *
 *       (r - p_target), where r is the response (0 or 1) and c is
 *       desc.steps[0]. So with the default harder_is_up = false the level
 *       changes by -c / m_k * (r - p_target), and a correct response at a
 *       target below 1 lowers it. m_k is 1 for the first two trials and
 *       thereafter 1 + the number of reversals so far, COUNTING a reversal
 *       on this trial, so the step shrinks from the reversal on rather than
 *       from the trial after it. There is no schedule: n_steps must be 1
 *       and step_down_scale must be 0 or 1. Converges on desc.target_p,
 *       which is required for this rule and must be in (0, 1). Use
 *       PSYST_STEP_LIN or PSYST_STEP_LOG; a log step moves log10(level).
 *
 *   STEPS (desc.step_type, desc.steps[], desc.n_steps)
 *     PSYST_STEP_LIN   level += step, level -= step
 *     PSYST_STEP_LOG   log10(level) += step, i.e. level *= 10^step. The
 *                      level must stay positive; open() rejects a start,
 *                      min or max <= 0 under this type, and update()
 *                      returns PSYST_ERR_ARG for a shown level <= 0.
 *     PSYST_STEP_DB    20*log10(level) += step, i.e. level *= 10^(step/20).
 *                      Same positivity rule.
 *     steps[] is the step schedule: steps[0] until the first reversal,
 *     steps[1] until the second, and so on; after n_steps - 1 reversals the
 *     last entry stays in force. The trial that CAUSES a reversal already
 *     steps by the new size, which is PsychoPy's StairHandler order and
 *     what psyst_trial.step_index records. This is PsychoPy's StairHandler
 *     semantic and Palamedes' PAL_AMUD stepSizeUp/Down with a schedule on
 *     top. One entry is a fixed step. The schedule counts reversals, not
 *     trials, so a run of correct responses at the start does not consume
 *     it. Every entry up to n_steps must be finite and > 0.
 *     PSYST_MAX_STEPS bounds n_steps (16).
 *
 *   INITIAL RULE (desc.initial_rule)
 *     Until the first reversal, step after every response as 1-up-1-down
 *     would, regardless of n_up and n_down. This gets a start far from
 *     threshold to threshold in fewer trials, at the cost of a first
 *     reversal that is not part of the equilibrium. PsychoPy's
 *     applyInitialRule (default true there); off by default here, because
 *     it changes which reversals count, and the estimate section says how.
 *
 *   LIMITS (desc.min, desc.max)
 *     A proposed level is clamped to [min, max] when both are nonzero (or
 *     when desc.use_limits is set; see the desc). The clamp is on the
 *     rule's own level, so it does not accumulate: a staircase held at max
 *     leaves max on the first step away from it. desc.start is clamped the
 *     same way. A clamped step is still a step: PSYST_EVENT_STEP is
 *     reported, the direction changes, and the schedule advances on a
 *     reversal, even when the level did not move. A proposal sitting
 *     exactly on min or max for desc.stop_at_limit consecutive trials ends
 *     the staircase with PSYST_STOP_LIMIT, because a staircase stuck at max
 *     is an observer who cannot do the task, not a threshold. The count is
 *     of trials proposed at a limit, whether a clamp put the level there or
 *     the rule simply has not stepped away yet.
 *
 *   STOPPING (desc.stop_reversals, desc.stop_trials)
 *     Either or both. The staircase is done when the reversal count reaches
 *     stop_reversals, or the trial count reaches stop_trials, whichever is
 *     first; 0 disables a criterion, and open() rejects both zero.
 *     psyst_stop_reason() says which fired. When more than one criterion
 *     comes true on the same trial the reason reported is the first of
 *     REVERSALS, TRIALS, LIMIT, FULL, and it never changes afterward.
 *     Trials the caller submits after done() is true are accepted and
 *     recorded; done() just keeps saying yes. PSYST_MAX_TRIALS (1024) is
 *     the hard ceiling: the staircase is done with PSYST_STOP_FULL once
 *     that many trials are recorded, and update() returns PSYST_ERR_FULL
 *     past it. PSYST_MAX_REVERSALS bounds stop_reversals, and
 *     PSYST_MAX_TRIALS bounds stop_trials, so open() rejects a criterion
 *     the handle could not reach. Reversals past PSYST_MAX_REVERSALS are
 *     still counted but their levels are not recorded.
 *
 *   REVERSALS
 *     A reversal is a change in the direction of the step. The level at
 *     which the direction changed (the last level before the turn, as the
 *     rule PROPOSED it) is what the reversal records, which is the
 *     convention of Levitt, Palamedes and PsychoPy. The first step has no
 *     direction to reverse from; a clamp at a limit that would have stepped
 *     past it counts as a step in the limit's direction.
 *
 *   ESTIMATES (psyst_estimate)
 *     PSYST_EST_REVERSALS      mean of the last desc.est_reversals reversal
 *                              levels (default, est_reversals = 0: all
 *                              reversals after the step schedule was
 *                              exhausted, and excluding the first reversal
 *                              when the initial rule was on). Even counts
 *                              pair peaks with troughs; an odd count biases
 *                              toward one. Levitt's recommendation, and the
 *                              default. A nonzero est_reversals is a plain
 *                              "last N reversals" with neither exclusion.
 *     PSYST_EST_TRIALS         mean of the levels of the last est_trials
 *                              trials (default: every trial after the
 *                              schedule was exhausted).
 *     PSYST_EST_MEDIAN_REV     median of the same reversals as above, for a
 *                              staircase with an outlier reversal. An even
 *                              count averages the two middle levels.
 *     PSYST_EST_LAST           the level the rule would propose next, which
 *                              is psyst_next(); what ASA reports.
 *     PSYST_EST_REVERSALS and PSYST_EST_MEDIAN_REV summarize the rule's own
 *     track, so they use the PROPOSED level at each turn. PSYST_EST_TRIALS
 *     summarizes what the observer saw, so it uses the SHOWN level the
 *     caller passed to update(). The two agree unless the caller showed
 *     something other than the proposal.
 *     Means are taken in the step's units: a LOG or DB staircase averages
 *     log levels and returns the antilog (the geometric mean). The estimate
 *     is a summary of the track, not a fit; for a fit with a slope and
 *     confidence intervals, replay the history through psy_quest.h or fit
 *     it offline.
 *
 *   ---------------------------------------------------------------------
 *   RETURN VALUES AND ERRORS
 *   ---------------------------------------------------------------------
 *   psyst_open() returns bool and fills psyst_error() on failure. Every
 *   other function returns a value or a code and never touches the message
 *   buffer, matching the collection's convention. update() returns 0, or a
 *   positive PSYST_EVENT_* mask for a step or a reversal, or a negative
 *   PSYST_ERR_*. psyst_strerror() names a code.
 *
 *     PSYST_ERR_ARG      null handle, response not 0 or 1, level not finite,
 *                        or a level <= 0 under PSYST_STEP_LOG / _DB
 *     PSYST_ERR_CLOSED   the handle was never opened
 *     PSYST_ERR_FULL     PSYST_MAX_TRIALS trials recorded
 *
 *   A closed handle answers every other call without touching it: next()
 *   and estimate() give NaN, estimate_count() and n_trials() give 0,
 *   history() gives NULL, done() and is_open() give false, stop_reason()
 *   gives PSYST_STOP_NONE, reversal_level() gives NaN and
 *   reversal_trial() gives -1. So does a null handle.
 *
 *   ---------------------------------------------------------------------
 *   MEMORY AND THREADS
 *   ---------------------------------------------------------------------
 *   The handle holds everything inline: the full trial history
 *   (PSYST_MAX_TRIALS levels and responses), the reversal list
 *   (PSYST_MAX_REVERSALS) and the counters. About 28 KB at the defaults
 *   (28168 bytes on a 64-bit ABI).
 *   Nothing allocates, ever. Define PSYST_MAX_TRIALS or
 *   PSYST_MAX_REVERSALS before the include to resize both the handle and
 *   the ceilings. A handle is not thread-safe; one thread drives one
 *   staircase, and interleaved staircases on one thread are just several
 *   handles. Every function is deterministic: the same desc and the same
 *   history give the same proposals, so a saved history (psyst_history())
 *   replayed through psyst_update() restores the state exactly.
 *
 *   ---------------------------------------------------------------------
 *   SIMULATION
 *   ---------------------------------------------------------------------
 *   psyst_simulate_response(p_correct, u) returns 1 when u < p_correct and
 *   0 otherwise; u is a uniform variate in [0, 1) that the caller draws.
 *   The header has no random generator, so an example's seed policy is the
 *   example's. examples/stair_sim.c runs a 3-down-1-up against a Weibull
 *   observer and prints the track and the estimate against the truth.
 *
 *   ---------------------------------------------------------------------
 *   BUILDING
 *   ---------------------------------------------------------------------
 *   Nothing to link but libm (-lm on Linux when not using a C++ driver).
 *   Define PSYST_API to override the default `extern` linkage; it is on the
 *   definitions too, so -DPSYST_API=static gives one translation unit a
 *   private copy.
 *
 *       cc -O2 -I. -o stair_sim examples/stair_sim.c -lm
 *       cl /O2 /I. examples\stair_sim.c
 *
 *   ---------------------------------------------------------------------
 *   LICENSE: public domain / MIT-0, see end of file.
 */
#ifndef PSY_STAIR_H_INCLUDED
#define PSY_STAIR_H_INCLUDED

/* The version of this header, for a log line or a compile-time check. The
 * string is the three numbers, and the test asserts that it stays so. */
#define PSYST_VERSION_MAJOR  0
#define PSYST_VERSION_MINOR  1
#define PSYST_VERSION_PATCH  1
#define PSYST_VERSION_STRING "0.1.1"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PSYST_API
#define PSYST_API extern
#endif

/* Handle capacities. Both are compile-time so the handle has no heap and a
 * fixed size; raise them before the include for a long session. */
#ifndef PSYST_MAX_TRIALS
#define PSYST_MAX_TRIALS 1024
#endif
#ifndef PSYST_MAX_REVERSALS
#define PSYST_MAX_REVERSALS 256
#endif
#define PSYST_MAX_STEPS 16

/* --- codes ------------------------------------------------------------- */

#define PSYST_OK          0
#define PSYST_ERR_ARG    (-1)  /* null handle, bad response, bad level      */
#define PSYST_ERR_CLOSED (-2)  /* psyst_open() has not succeeded on this handle */
#define PSYST_ERR_FULL   (-3)  /* PSYST_MAX_TRIALS reached                    */

/* Bits in a positive psyst_update() return. */
#define PSYST_EVENT_STEP     1  /* the rule stepped (a limit may have held
                                 * the level where it was)                  */
#define PSYST_EVENT_REVERSAL 2  /* the step direction changed                */
#define PSYST_EVENT_DONE     4  /* a stop criterion fired on this trial      */

/* Static description of a PSYST_ERR_* code ("ok" for values >= 0). */
PSYST_API const char* psyst_strerror(int code);

/* PSYST_VERSION_STRING, as compiled into the implementation. Beside the macro,
 * which is what the CALLER was compiled against, it tells a program that links
 * a prebuilt implementation which one it got. */
PSYST_API const char* psyst_version(void);

/* --- description ------------------------------------------------------- */

typedef enum psyst_rule {
    PSYST_RULE_UPDOWN = 0, /* transformed / weighted up-down (default)     */
    PSYST_RULE_ASA         /* accelerated stochastic approximation         */
} psyst_rule;

typedef enum psyst_step_type {
    PSYST_STEP_LIN = 0,    /* level += step                                 */
    PSYST_STEP_LOG,        /* log10(level) += step                          */
    PSYST_STEP_DB          /* 20 log10(level) += step                       */
} psyst_step_type;

typedef enum psyst_estimator {
    PSYST_EST_REVERSALS = 0, /* mean of the counted reversal levels         */
    PSYST_EST_TRIALS,        /* mean of the counted trial levels            */
    PSYST_EST_MEDIAN_REV,    /* median of the counted reversal levels       */
    PSYST_EST_LAST           /* the next proposed level                     */
} psyst_estimator;

typedef enum psyst_stop {
    PSYST_STOP_NONE = 0,   /* still running                                 */
    PSYST_STOP_REVERSALS,  /* desc.stop_reversals reached                   */
    PSYST_STOP_TRIALS,     /* desc.stop_trials reached                      */
    PSYST_STOP_LIMIT,      /* pinned at min or max for stop_at_limit trials */
    PSYST_STOP_FULL        /* PSYST_MAX_TRIALS reached                      */
} psyst_stop;

/* Staircase description. Zero-initialize it and set only what you need.
 * Required: start, at least one of stop_reversals / stop_trials, and
 * steps[0] with n_steps >= 1. Under PSYST_RULE_ASA also target_p. */
typedef struct psyst_desc {
    double     start;           /* first proposed level, caller's units     */
    psyst_rule rule;            /* PSYST_RULE_UPDOWN (0) or ASA             */
    int        n_up;            /* incorrect responses per up step; 0 = 1   */
    int        n_down;          /* correct responses per down step; 0 = 1   */
    psyst_step_type step_type;  /* LIN (0), LOG or DB                       */
    double     steps[PSYST_MAX_STEPS]; /* step schedule, one per reversal    */
    int        n_steps;         /* entries in steps[]; 1 = fixed step       */
    double     step_down_scale; /* harder ("down") step = step * this; 0 = 1
                                 * (equal). != 1 makes a weighted staircase */
    double     target_p;        /* ASA only: the proportion to converge on  */
    double     min, max;        /* proposal clamp; both 0 = no limits unless
                                 * use_limits is set (a real 0 bound)       */
    bool       use_limits;      /* honor min/max even when both are 0       */
    bool       harder_is_up;    /* a correct response RAISES the level      */
    bool       initial_rule;    /* 1-up-1-down until the first reversal     */
    int        stop_reversals;  /* stop after this many reversals; 0 = off  */
    int        stop_trials;     /* stop after this many trials; 0 = off     */
    int        stop_at_limit;   /* stop after this many consecutive trials
                                 * pinned at min or max; 0 = never          */
    int        est_reversals;   /* reversals PSYST_EST_REVERSALS averages;
                                 * 0 = all after the schedule ran out       */
    int        est_trials;      /* trials PSYST_EST_TRIALS averages; 0 = all
                                 * after the schedule ran out               */
} psyst_desc;

/* --- handle ------------------------------------------------------------ */

/* One recorded trial. psyst_history() hands out the array. */
typedef struct psyst_trial {
    double  proposed;   /* what psyst_next() returned before this trial     */
    double  shown;      /* what the caller passed to psyst_update()         */
    uint8_t response;   /* 0 or 1                                           */
    uint8_t reversal;   /* 1 when this trial's step reversed direction      */
    int8_t  direction;  /* step taken after this trial: -1 down, +1 up, 0   */
    uint8_t step_index; /* index into desc.steps in force for this trial    */
} psyst_trial;

/* Staircase handle. The caller allocates it and treats every field as
 * opaque. Must be zeroed or previously opened before psyst_open(). Sized by
 * PSYST_MAX_TRIALS and PSYST_MAX_REVERSALS; see MEMORY AND THREADS. */
typedef struct psyst_stair {
    psyst_desc  desc;
    double      level;          /* next proposal, in step units (log for
                                 * LOG/DB) so a step is an add              */
    int         n_trials;
    int         n_reversals;
    int         run_correct;    /* consecutive correct since the last step  */
    int         run_incorrect;
    int         direction;      /* last step direction; 0 before the first  */
    int         step_index;
    int         at_limit_run;   /* consecutive trials pinned at a limit     */
    psyst_stop  stop;
    bool        open;
    psyst_trial history[PSYST_MAX_TRIALS];
    double      reversals[PSYST_MAX_REVERSALS]; /* level at each reversal,
                                                 * caller's units          */
    int         reversal_trial[PSYST_MAX_REVERSALS]; /* trial index of each */
    char        error[256];
} psyst_stair;

/* --- lifecycle --------------------------------------------------------- */

/* Validate `desc` and reset `s` to its first trial. Returns false and fills
 * psyst_error() on a bad desc: a null handle or desc, no stop criterion, no
 * step (n_steps < 1) or n_steps > PSYST_MAX_STEPS, a step that is not finite
 * and positive, a non-finite start, LOG/DB with a non-positive start, min or
 * max, min > max with limits on, a negative n_up, n_down, step_down_scale,
 * stop_* or est_*, stop_reversals > PSYST_MAX_REVERSALS, stop_trials >
 * PSYST_MAX_TRIALS, an out-of-range rule or step_type, ASA without a
 * target_p in (0, 1), and ASA with n_steps != 1 or step_down_scale not 0 or
 * 1. The only function that writes the message buffer. */
PSYST_API bool psyst_open(psyst_stair* s, const psyst_desc* desc);

/* Last psyst_open() message for this handle ("" if none). */
PSYST_API const char* psyst_error(const psyst_stair* s);

/* True after a successful psyst_open(). */
PSYST_API bool psyst_is_open(const psyst_stair* s);

/* --- the loop ---------------------------------------------------------- */

/* The level the rule proposes for the next trial, in the caller's units and
 * inside [min, max] when limits are on. Calling it twice without an update
 * returns the same value. NaN on a closed handle. */
PSYST_API double psyst_next(const psyst_stair* s);

/* Record a trial shown at `level` with `response` (1 = correct / detected,
 * 0 = incorrect / missed) and advance the rule. Returns a PSYST_EVENT_*
 * mask (0 when nothing but the counters moved) or a negative PSYST_ERR_*.
 * `level` is recorded but does not alter the rule's own level; see MODEL. */
PSYST_API int psyst_update(psyst_stair* s, double level, int response);

/* True once a stop criterion has fired. Stays true; later updates are still
 * recorded. */
PSYST_API bool psyst_done(const psyst_stair* s);

/* Which criterion ended the staircase, PSYST_STOP_NONE while running. */
PSYST_API psyst_stop psyst_stop_reason(const psyst_stair* s);

/* --- estimates --------------------------------------------------------- */

/* Threshold estimate per `how`; see ESTIMATES. NaN when the history has
 * nothing to average (no counted reversal yet). Cheap; call it every trial
 * if you like. */
PSYST_API double psyst_estimate(const psyst_stair* s, psyst_estimator how);

/* Number of reversals / trials the PSYST_EST_REVERSALS, PSYST_EST_TRIALS or
 * PSYST_EST_MEDIAN_REV window would average right now; 1 for
 * PSYST_EST_LAST. Zero means the estimate is NaN. Log these with the
 * estimate. */
PSYST_API int psyst_estimate_count(const psyst_stair* s, psyst_estimator how);

/* step_down_scale that makes a 1-up-1-down weighted staircase converge on
 * `target_p`: (1 - p) / p. Kaernbach 1991. NaN outside (0, 1). */
PSYST_API double psyst_weighted_scale(double target_p);

/* The proportion correct a transformed n_up / n_down staircase converges
 * on, with step_down_scale applied (0 means 1). For n_up = 1 this is
 * (1 / (1 + scale))^(1 / n_down); for n_down = 1 it is
 * 1 - (scale / (1 + scale))^(1 / n_up). NaN when the closed form does not
 * apply (n_up > 1 with n_down > 1); such rules exist but are not
 * tabulated. */
PSYST_API double psyst_convergence_p(int n_up, int n_down, double step_down_scale);

/* --- history ----------------------------------------------------------- */

PSYST_API int psyst_n_trials(const psyst_stair* s);
PSYST_API int psyst_n_reversals(const psyst_stair* s);

/* The recorded trials, oldest first. `*n` receives the count. The pointer
 * is into the handle and stays valid until the handle is reopened. NULL
 * with *n = 0 on a closed handle. */
PSYST_API const psyst_trial* psyst_history(const psyst_stair* s, int* n);

/* Level (caller's units) and trial index of reversal `i`, or NaN / -1. */
PSYST_API double psyst_reversal_level(const psyst_stair* s, int i);
PSYST_API int    psyst_reversal_trial(const psyst_stair* s, int i);

/* --- simulation -------------------------------------------------------- */

/* 1 when u < p_correct, else 0. `u` is a uniform variate in [0, 1) drawn by
 * the caller; the header has no generator. */
PSYST_API int psyst_simulate_response(double p_correct, double u);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PSY_STAIR_H_INCLUDED */

/* ======================================================================= *
 *                             IMPLEMENTATION                              *
 * ======================================================================= */
#ifdef PSY_STAIR_IMPLEMENTATION
#ifndef PSY_STAIR_IMPLEMENTATION_GUARD
#define PSY_STAIR_IMPLEMENTATION_GUARD

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef NAN
    #define PSYST__NAN ((double)NAN)
#else
    /* No NAN macro means a pre-C99 library; inf - inf is the portable
     * spelling and this branch never compiles on the supported toolchains. */
    #define PSYST__NAN (HUGE_VAL - HUGE_VAL)
#endif

/* isfinite() is C99 and MSVC's default C dialect does not declare it, so the
 * test is written in comparisons a NaN loses on its own. */
static bool psyst__finite(double x) {
    return x > -HUGE_VAL && x < HUGE_VAL;
}

/* --- unit conversion ---------------------------------------------------
 * The handle keeps the level in STEP units so every rule is an addition.
 * These two are the only places the caller's units are entered or left. */

static double psyst__to_step(const psyst_stair* s, double v) {
    switch (s->desc.step_type) {
    case PSYST_STEP_LOG: return log10(v);
    case PSYST_STEP_DB:  return 20.0 * log10(v);
    default:             return v;
    }
}

static double psyst__from_step(const psyst_stair* s, double v) {
    switch (s->desc.step_type) {
    case PSYST_STEP_LOG: return pow(10.0, v);
    case PSYST_STEP_DB:  return pow(10.0, v / 20.0);
    default:             return v;
    }
}

/* psyst_open() normalizes desc.use_limits to mean "limits are in force", so
 * the two-nonzero-bounds rule is decided once instead of at every clamp. */
static double psyst__lo(const psyst_stair* s) { return psyst__to_step(s, s->desc.min); }
static double psyst__hi(const psyst_stair* s) { return psyst__to_step(s, s->desc.max); }

static double psyst__clamp(const psyst_stair* s, double v) {
    double lo, hi;
    if (!s->desc.use_limits) return v;
    lo = psyst__lo(s);
    hi = psyst__hi(s);
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static bool psyst__at_limit(const psyst_stair* s, double v) {
    if (!s->desc.use_limits) return false;
    return v <= psyst__lo(s) || v >= psyst__hi(s);
}

/* --- codes ------------------------------------------------------------- */

PSYST_API const char* psyst_strerror(int code) {
    switch (code) {
    case PSYST_ERR_ARG:    return "invalid argument";
    case PSYST_ERR_CLOSED: return "staircase is not open";
    case PSYST_ERR_FULL:   return "trial history is full";
    default:               return code >= 0 ? "ok" : "unknown error";
    }
}

PSYST_API const char* psyst_version(void) { return PSYST_VERSION_STRING; }

/* --- lifecycle --------------------------------------------------------- */

#define PSYST__FAIL(s, msg) \
    do { snprintf((s)->error, sizeof((s)->error), "psy_stair: %s", (msg)); \
         return false; } while (0)

PSYST_API bool psyst_open(psyst_stair* s, const psyst_desc* desc) {
    bool limits;
    int i;

    if (!s) return false;
    memset(s, 0, sizeof(*s));
    if (!desc) PSYST__FAIL(s, "psyst_open: null desc");

    if (desc->rule != PSYST_RULE_UPDOWN && desc->rule != PSYST_RULE_ASA)
        PSYST__FAIL(s, "desc.rule is not a psyst_rule");
    if (desc->step_type != PSYST_STEP_LIN && desc->step_type != PSYST_STEP_LOG &&
        desc->step_type != PSYST_STEP_DB)
        PSYST__FAIL(s, "desc.step_type is not a psyst_step_type");

    if (!psyst__finite(desc->start)) PSYST__FAIL(s, "desc.start is not finite");
    if (desc->n_steps < 1) PSYST__FAIL(s, "desc.n_steps must be at least 1");
    if (desc->n_steps > PSYST_MAX_STEPS)
        PSYST__FAIL(s, "desc.n_steps is above PSYST_MAX_STEPS");
    for (i = 0; i < desc->n_steps; i++) {
        if (!psyst__finite(desc->steps[i]) || desc->steps[i] <= 0.0)
            PSYST__FAIL(s, "every desc.steps entry must be finite and positive");
    }
    if (desc->n_up < 0 || desc->n_down < 0)
        PSYST__FAIL(s, "desc.n_up and desc.n_down must not be negative");
    if (!psyst__finite(desc->step_down_scale) || desc->step_down_scale < 0.0)
        PSYST__FAIL(s, "desc.step_down_scale must be finite and not negative");
    if (desc->stop_reversals < 0 || desc->stop_trials < 0 ||
        desc->stop_at_limit < 0 || desc->est_reversals < 0 || desc->est_trials < 0)
        PSYST__FAIL(s, "desc counts must not be negative");
    if (desc->stop_reversals == 0 && desc->stop_trials == 0)
        PSYST__FAIL(s, "desc needs stop_reversals or stop_trials");
    if (desc->stop_reversals > PSYST_MAX_REVERSALS)
        PSYST__FAIL(s, "desc.stop_reversals is above PSYST_MAX_REVERSALS");
    if (desc->stop_trials > PSYST_MAX_TRIALS)
        PSYST__FAIL(s, "desc.stop_trials is above PSYST_MAX_TRIALS");

    limits = desc->use_limits || (desc->min != 0.0 && desc->max != 0.0);
    if (limits) {
        if (!psyst__finite(desc->min) || !psyst__finite(desc->max))
            PSYST__FAIL(s, "desc.min and desc.max must be finite");
        if (desc->min > desc->max) PSYST__FAIL(s, "desc.min is above desc.max");
    }
    if (desc->step_type != PSYST_STEP_LIN) {
        if (desc->start <= 0.0)
            PSYST__FAIL(s, "desc.start must be positive under a LOG or DB step");
        if (limits && (desc->min <= 0.0 || desc->max <= 0.0))
            PSYST__FAIL(s, "desc.min and desc.max must be positive under a LOG or DB step");
    }

    if (desc->rule == PSYST_RULE_ASA) {
        if (!psyst__finite(desc->target_p) || desc->target_p <= 0.0 || desc->target_p >= 1.0)
            PSYST__FAIL(s, "PSYST_RULE_ASA needs desc.target_p in (0, 1)");
        if (desc->n_steps != 1)
            PSYST__FAIL(s, "PSYST_RULE_ASA has no step schedule: desc.n_steps must be 1");
        if (desc->step_down_scale != 0.0 && desc->step_down_scale != 1.0)
            PSYST__FAIL(s, "PSYST_RULE_ASA needs desc.step_down_scale 0 or 1");
    }

    s->desc = *desc;
    /* Fold the "0 means default" fields once so the rules read plain values. */
    if (s->desc.n_up <= 0) s->desc.n_up = 1;
    if (s->desc.n_down <= 0) s->desc.n_down = 1;
    if (s->desc.step_down_scale <= 0.0) s->desc.step_down_scale = 1.0;
    s->desc.use_limits = limits;

    s->level = psyst__clamp(s, psyst__to_step(s, desc->start));
    s->stop = PSYST_STOP_NONE;
    s->open = true;
    s->error[0] = '\0';
    return true;
}

PSYST_API const char* psyst_error(const psyst_stair* s) {
    return s ? s->error : "psy_stair: null staircase handle";
}

PSYST_API bool psyst_is_open(const psyst_stair* s) {
    return s != NULL && s->open;
}

/* --- the loop ---------------------------------------------------------- */

PSYST_API double psyst_next(const psyst_stair* s) {
    if (!s || !s->open) return PSYST__NAN;
    return psyst__from_step(s, s->level);
}

PSYST_API int psyst_update(psyst_stair* s, double level, int response) {
    double proposed, step = 0.0, delta;
    int ti, dir, harder, events;
    bool reversal;

    if (!s) return PSYST_ERR_ARG;
    if (!s->open) return PSYST_ERR_CLOSED;
    if (response != 0 && response != 1) return PSYST_ERR_ARG;
    if (!psyst__finite(level)) return PSYST_ERR_ARG;
    /* A LOG or DB history is averaged as a logarithm, so a non-positive
     * shown level would poison the estimate rather than fail here. */
    if (s->desc.step_type != PSYST_STEP_LIN && level <= 0.0) return PSYST_ERR_ARG;
    if (s->n_trials >= PSYST_MAX_TRIALS) {
        if (s->stop == PSYST_STOP_NONE) s->stop = PSYST_STOP_FULL;
        return PSYST_ERR_FULL;
    }

    ti = s->n_trials;
    proposed = psyst__from_step(s, s->level);
    s->at_limit_run = psyst__at_limit(s, s->level) ? s->at_limit_run + 1 : 0;

    dir = 0;
    delta = 0.0;
    harder = 0;

    if (s->desc.rule == PSYST_RULE_ASA) {
        double m;
        /* The move is toward the harder end; see RULES. */
        delta = (double)response - s->desc.target_p;
        dir = (delta > 0.0) ? 1 : -1;               /* harder, in difficulty */
        if (!s->desc.harder_is_up) dir = -dir;      /* ... now in level units */
        reversal = (s->direction != 0 && dir != s->direction);
        if (reversal) {
            if (s->n_reversals < PSYST_MAX_REVERSALS) {
                s->reversals[s->n_reversals] = proposed;
                s->reversal_trial[s->n_reversals] = ti;
            }
            s->n_reversals++;
        }
        m = (ti < 2) ? 1.0 : (double)(1 + s->n_reversals);
        delta = s->desc.steps[0] / m * fabs(delta) * (double)dir;
    } else {
        int n_up = s->desc.n_up, n_down = s->desc.n_down;

        if (response) { s->run_correct++; s->run_incorrect = 0; }
        else          { s->run_incorrect++; s->run_correct = 0; }

        if (s->desc.initial_rule && s->n_reversals == 0) { n_up = 1; n_down = 1; }

        if (s->run_correct >= n_down) harder = 1;
        else if (s->run_incorrect >= n_up) harder = -1;

        reversal = false;
        if (harder != 0) {
            s->run_correct = 0;
            s->run_incorrect = 0;
            dir = s->desc.harder_is_up ? harder : -harder;
            reversal = (s->direction != 0 && dir != s->direction);
            if (reversal) {
                if (s->n_reversals < PSYST_MAX_REVERSALS) {
                    s->reversals[s->n_reversals] = proposed;
                    s->reversal_trial[s->n_reversals] = ti;
                }
                s->n_reversals++;
                /* PsychoPy's order: the reversal trial already steps by the
                 * schedule's next entry. */
                s->step_index = (s->n_reversals < s->desc.n_steps)
                              ? s->n_reversals : s->desc.n_steps - 1;
            }
            step = s->desc.steps[s->step_index];
            if (harder > 0) step *= s->desc.step_down_scale;
            delta = (double)dir * step;
        }
    }

    if (dir != 0) {
        s->level = psyst__clamp(s, s->level + delta);
        s->direction = dir;
    }

    s->history[ti].proposed   = proposed;
    s->history[ti].shown      = level;
    s->history[ti].response   = (uint8_t)response;
    s->history[ti].reversal   = (uint8_t)(reversal ? 1 : 0);
    s->history[ti].direction  = (int8_t)dir;
    s->history[ti].step_index = (uint8_t)s->step_index;
    s->n_trials = ti + 1;

    events = 0;
    if (dir != 0) events |= PSYST_EVENT_STEP;
    if (reversal) events |= PSYST_EVENT_REVERSAL;

    if (s->stop == PSYST_STOP_NONE) {
        if (s->desc.stop_reversals > 0 && s->n_reversals >= s->desc.stop_reversals)
            s->stop = PSYST_STOP_REVERSALS;
        else if (s->desc.stop_trials > 0 && s->n_trials >= s->desc.stop_trials)
            s->stop = PSYST_STOP_TRIALS;
        else if (s->desc.stop_at_limit > 0 && s->at_limit_run >= s->desc.stop_at_limit)
            s->stop = PSYST_STOP_LIMIT;
        else if (s->n_trials >= PSYST_MAX_TRIALS)
            s->stop = PSYST_STOP_FULL;
        if (s->stop != PSYST_STOP_NONE) events |= PSYST_EVENT_DONE;
    }
    return events;
}

PSYST_API bool psyst_done(const psyst_stair* s) {
    return s != NULL && s->open && s->stop != PSYST_STOP_NONE;
}

PSYST_API psyst_stop psyst_stop_reason(const psyst_stair* s) {
    return (s && s->open) ? s->stop : PSYST_STOP_NONE;
}

/* --- estimates --------------------------------------------------------- */

/* The reversal window [*first, *first + n). Only recorded reversals are in
 * it, so a run past PSYST_MAX_REVERSALS estimates from what it kept. */
static int psyst__rev_window(const psyst_stair* s, int* first) {
    int n = s->n_reversals;
    int f;
    if (n > PSYST_MAX_REVERSALS) n = PSYST_MAX_REVERSALS;
    if (s->desc.est_reversals > 0) {
        f = n - s->desc.est_reversals;
        if (f < 0) f = 0;
    } else {
        f = s->desc.n_steps - 1;
        if (s->desc.initial_rule && f < 1) f = 1;
        if (f > n) f = n;
    }
    *first = f;
    return n - f;
}

static int psyst__trial_window(const psyst_stair* s, int* first) {
    int n = s->n_trials;
    int f;
    if (s->desc.est_trials > 0) {
        f = n - s->desc.est_trials;
        if (f < 0) f = 0;
    } else {
        int last = s->desc.n_steps - 1;
        f = 0;
        while (f < n && (int)s->history[f].step_index < last) f++;
    }
    *first = f;
    return n - f;
}

/* k-th smallest of v[0..n), by rank counting. No buffer and no sort, which
 * keeps the header's "nothing allocates, ever" true for the median too; n is
 * at most PSYST_MAX_REVERSALS. */
static double psyst__kth(const double* v, int n, int k) {
    int i, j;
    for (i = 0; i < n; i++) {
        int less = 0, equal = 0;
        for (j = 0; j < n; j++) {
            if (v[j] < v[i]) less++;
            else if (v[j] <= v[i]) equal++;
        }
        if (k >= less && k < less + equal) return v[i];
    }
    return v[0];
}

PSYST_API double psyst_estimate(const psyst_stair* s, psyst_estimator how) {
    int first = 0, n = 0, i;
    double sum;

    if (!s || !s->open) return PSYST__NAN;

    switch (how) {
    case PSYST_EST_LAST:
        return psyst__from_step(s, s->level);

    case PSYST_EST_TRIALS:
        n = psyst__trial_window(s, &first);
        if (n <= 0) return PSYST__NAN;
        sum = 0.0;
        for (i = first; i < first + n; i++)
            sum += psyst__to_step(s, s->history[i].shown);
        return psyst__from_step(s, sum / (double)n);

    case PSYST_EST_REVERSALS:
        n = psyst__rev_window(s, &first);
        if (n <= 0) return PSYST__NAN;
        sum = 0.0;
        for (i = first; i < first + n; i++)
            sum += psyst__to_step(s, s->reversals[i]);
        return psyst__from_step(s, sum / (double)n);

    case PSYST_EST_MEDIAN_REV:
        n = psyst__rev_window(s, &first);
        if (n <= 0) return PSYST__NAN;
        if (n % 2 == 1) {
            sum = psyst__to_step(s, psyst__kth(s->reversals + first, n, n / 2));
        } else {
            sum = 0.5 * (psyst__to_step(s, psyst__kth(s->reversals + first, n, n / 2 - 1)) +
                         psyst__to_step(s, psyst__kth(s->reversals + first, n, n / 2)));
        }
        return psyst__from_step(s, sum);

    default:
        return PSYST__NAN;
    }
}

PSYST_API int psyst_estimate_count(const psyst_stair* s, psyst_estimator how) {
    int first = 0;
    if (!s || !s->open) return 0;
    switch (how) {
    case PSYST_EST_REVERSALS:
    case PSYST_EST_MEDIAN_REV: return psyst__rev_window(s, &first);
    case PSYST_EST_TRIALS:     return psyst__trial_window(s, &first);
    case PSYST_EST_LAST:       return 1;
    default:                   return 0;
    }
}

PSYST_API double psyst_weighted_scale(double target_p) {
    if (!psyst__finite(target_p) || target_p <= 0.0 || target_p >= 1.0)
        return PSYST__NAN;
    return (1.0 - target_p) / target_p;
}

PSYST_API double psyst_convergence_p(int n_up, int n_down, double step_down_scale) {
    double s = step_down_scale;
    if (n_up <= 0) n_up = 1;
    if (n_down <= 0) n_down = 1;
    if (!psyst__finite(s) || s < 0.0) return PSYST__NAN;
    if (s == 0.0) s = 1.0;
    /* Levitt's equilibrium: the weighted probability of a harder step equals
     * that of an easier one. Only the two single-sided families close. */
    if (n_up == 1) return pow(1.0 / (1.0 + s), 1.0 / (double)n_down);
    if (n_down == 1) return 1.0 - pow(s / (1.0 + s), 1.0 / (double)n_up);
    return PSYST__NAN;
}

/* --- history ----------------------------------------------------------- */

PSYST_API int psyst_n_trials(const psyst_stair* s) {
    return (s && s->open) ? s->n_trials : 0;
}

PSYST_API int psyst_n_reversals(const psyst_stair* s) {
    return (s && s->open) ? s->n_reversals : 0;
}

PSYST_API const psyst_trial* psyst_history(const psyst_stair* s, int* n) {
    if (!s || !s->open) {
        if (n) *n = 0;
        return NULL;
    }
    if (n) *n = s->n_trials;
    return s->history;
}

PSYST_API double psyst_reversal_level(const psyst_stair* s, int i) {
    if (!s || !s->open || i < 0 || i >= s->n_reversals || i >= PSYST_MAX_REVERSALS)
        return PSYST__NAN;
    return s->reversals[i];
}

PSYST_API int psyst_reversal_trial(const psyst_stair* s, int i) {
    if (!s || !s->open || i < 0 || i >= s->n_reversals || i >= PSYST_MAX_REVERSALS)
        return -1;
    return s->reversal_trial[i];
}

/* --- simulation -------------------------------------------------------- */

PSYST_API int psyst_simulate_response(double p_correct, double u) {
    return (u < p_correct) ? 1 : 0;
}

#undef PSYST__FAIL

#endif /* PSY_STAIR_IMPLEMENTATION_GUARD */
#endif /* PSY_STAIR_IMPLEMENTATION */

/* ------------------------------------------------------------------------
 * This software is available under the MIT-0 (MIT No Attribution) license.
 *
 * Copyright (c) 2026 psy contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY.
 * ------------------------------------------------------------------------ */
