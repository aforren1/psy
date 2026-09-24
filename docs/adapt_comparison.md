# The adaptive methods compared on published test problems

This page explains how the three adaptive-method headers compare when they
run on the same problems. The headers are `psy_stair.h` (staircases),
`psy_quest.h` (QUEST+ and its special cases) and `psy_gp.h` (the Gaussian
process model and the two-latent psychometric model). The problems come
from two papers: Watson (2017), which introduced QUEST+, and Owen et al.
(2021), which introduced AEPsych. The page gives the numbers and says what
they mean for the choice of a method. It does not tell you how to configure
a method. The header manuals do that. The design decisions are in
[psy_adapt.md](psy_adapt.md).

## What was compared

The script is [tests/compare/methods_compare.py](../tests/compare/methods_compare.py).
It drives the three Python bindings. The compiled headers were
psy_stair.h 0.1.1, psy_quest.h 0.4.1 and psy_gp.h 0.13.1. Every GP-model
and psychometric-model row comes from psy_gp.h 0.13.1. The staircase and
QUEST+ rows come from the first run, because those headers did not change.
[What changed between psy_gp.h 0.4.1 and 0.13.1](#what-changed-between-psy_gph-041-and-0131)
compares the two runs. The current headers are psy_stair.h 0.1.2,
psy_quest.h 0.5.2 and psy_gp.h 0.14.0. By their CHANGELOGs, none of the
later versions changes what a default configuration computes, so the
tables hold for them.

### The problems

| Key | Source | Stimulus | What is estimated | Trials |
|---|---|---|---|---|
| w1 | Watson, threshold example | contrast, -40 to 0 dB, 1 dB steps | threshold (truth -20 dB, slope 3.5, guess 0.5, lapse 0.02) | 32 in the paper, 128 run |
| w2 | Watson, threshold and slope example | the same | threshold and slope (truth -20, 3, 0.5, 0.02) | 64, 128 run |
| w3 | Watson, threshold, slope and lapse example | the same | threshold, slope, lapse (truth -20, 3, 0.5, 0.03) | 128 |
| w4 | Watson, mean, sd and lapse example | orientation, -10 to 10 deg | mean, sd, lapse of a cumulative normal (truth 1, 3, 0.02) | 128 |
| audio-* | Owen et al., audiometric field | log2 kHz -3 to 4 by -20 to 120 dB HL | the 0.75 threshold curve; 4 phenotypes, beta 2 and 0.5 dB | 150 |
| novel-det, novel-dis | Owen et al., novel detection and discrimination | context and intensity, each -1 to 1 | the 0.75 threshold curve | 150 |
| csf | Watson, spatial CSF example | frequency 0 to 40 c/deg by contrast -50 to 0 dB | threshold max(t, c0 + cf f) dB (truth -35, -50, 1.2) | 32 in the paper, 150 run |
| circ | Watson, circular categorization example | 18 directions, 20 deg apart | 3 categories: concentration, first boundary (truth 4, 60 deg, widths 120 deg) | 64 in the paper, 128 run |
| csf6 | Letham et al. 2022, real-data CSF | log10 contrast, log10 pedestal, temporal frequency, spatial frequency, size, eccentricity | the 0.75 level set, a 5-D surface in a 6-D box | 750 in the paper, 300 run |

The specification of each Watson example comes from Watson's own QUEST+
Mathematica notebook, the code behind the paper. It gives the grids, the
psychometric functions and the true observers exactly. Two facts in it
matter here. First, the Weibull is in dB with the slope divided by 20:
P(correct) = 1 - lapse - (1 - guess - lapse) exp(-10^(slope (x - threshold) / 20)).
That is `PSYQ_PF_GUMBEL` with the slope axis scaled by 1/20. Second, the
paper's CSF example is not a log-parabola. It is a threshold that is flat at
t below a corner frequency and linear in frequency above it. The notebook
also has a spatiotemporal CSF, Thurstone scaling, a blur increment
threshold and two rating examples. Those run only in the replay check
below.

The AEPsych functions are the paper's equations 7 to 9: a context term
2 (0.05 + 0.4 (-1 + 0.2 f)^2 f^2), a detection latent 4 (a + 1) / context - 4,
a discrimination latent 2 (a + 1) / context, and p = Phi(latent), on the box
[-1, 1] x [-1, 1] with target p 0.75. Both latents increase with a
everywhere on the box, so both functions are monotone in intensity. The
discrimination function has a floor of 0.5 at a = -1, and its 0.75
threshold lies between -0.97 and -0.58, near the bottom of the box.

### The methods

| Method | Where it runs | Configuration |
|---|---|---|
| 1-up-2-down, 1-up-3-down | 1-D | steps 8, 4, 2 dB (4, 2, 1 deg for w4), start at the top of the range, reversal mean after the schedule |
| weighted 1-up-1-down | 1-D | the same steps, down step scaled to converge on the target p |
| interleaved staircases | 2-D | weighted 1-up-1-down at fixed contexts, round robin: 8 audiometric frequencies (as `gp_audiometric.c`), 6 contexts for the others |
| QUEST | 1-D | QUEST+ with the threshold axis free and every other parameter fixed at the truth |
| Psi | 1-D | threshold and slope free (slope grid 2 to 5), guess and lapse fixed |
| QUEST+ joint | w3, w4 | Watson's grid for the example, every parameter joint |
| Psi-marginal | 1-D | threshold and slope free, lapse 0 to 0.04 as a nuisance axis |
| QUEST+ | csf, circ | Watson's model and grids, through a NumPy `pf_batch` |
| GP | all | `model="gp"`, RBF kernel, probit, LSE, EAVC or BALV |
| psychometric | 1-D, 2-D | `model="psychometric"`, LSE, EAVC, BALV or BALD; with n_dims = 1 the threshold and log slope are two scalars |
| GP categorical | circ | `lik="categorical"`, 3 outcomes, BALD or BALV, the 18 directions as candidates |

The GP methods on the 2-D problems use an 11 x 21 candidate grid with
`refine_steps=2`, `fit_every=20` and 5 scrambled Sobol init trials. On the
1-D problems they use an 81-point grid and `fit_every=10`. On the 2AFC
problems (w1 to w3, csf) the GP methods get the true guess and lapse. On the
others the floor and ceiling are 0 and 1, as in AEPsych.

The audiogram and the two novel functions have no parametric model, so
QUEST+ does not run on them. That is the AEPsych paper's argument for a
nonparametric method.

### The protocol and the metrics

- Every method on a problem sees one response stream. A seeded NumPy
  generator draws one uniform u per trial and replication, and trial t of
  every method uses u[t]. The response is 1 when u >= 1 - p.
- Replications: 100 for the staircases and QUEST+, 50 for the 1-D GP
  methods, 20 for the 2-D GP methods and the categorical GP.
- Intervals are +-1.96 sd / sqrt(n) over replications.
- 1-D threshold error: estimate minus truth. The truth for a staircase is
  the true level at the p its rule converges on. For the other methods it is
  Watson's threshold parameter, and the GP methods target the p that the
  true function has there. QUEST+ reports the marginal posterior mean.
  Watson's own estimator, the joint mode, is in a second line.
- 2-D metrics, as the AEPsych paper defines them, on a 30 x 30 grid: the
  mean absolute error of p (MAE(p)), and the mean absolute error of the
  target-p threshold, found by bracketing along intensity per column (thr
  MAE). "No-cross" counts the columns where the model never crosses the
  target; thr MAE averages over the other columns only.
- Band MAE: the MAE of p over the cells with true p between 0.05 and 0.95,
  on 400 intensity points per column. On the discrimination function the
  band includes the 0.5 floor.
- The staircases have no field. They are scored through an informed model:
  the true psychometric function at each context, shifted so that it crosses
  the target at the staircase's estimate. So the staircase gets the slope
  for free. This is a best case.
- Trials to criterion: the first trial count from which the replication-mean
  threshold error stays at or below the criterion to the end of the run.
  The criteria are 2 dB (0.1 log units) for w1 to w3, csf and the
  audiogram, 0.5 deg for w4, 0.1 intensity units for the novel functions,
  and 0.05 of MAE(p) for circ. A first touch would credit Watson's grids:
  their uniform priors put the posterior mean on the true threshold (-20 dB)
  before the first trial.
- ms/trial: `next` plus `update`, without the simulated observer, from one
  serial replication per method in one process. The machine was a 13th Gen
  Intel Core i7-1360P laptop (4 performance and 8 efficiency cores, 16
  threads). Other work kept it at 44 to 64 percent CPU load during the
  first timing pass and at 6 to 77 percent during the 0.13.1 pass, and a
  single process can land on either core type. So a time is good to a
  factor of about two, and a time that moved between the two runs with
  identical accuracy numbers moved because of the load.

## Watson's own runs, replayed

Watson's paper and notebook report single simulated runs. They give no
means, sds or biases over replications. So a check of our means against his
is not possible. The notebook does save the stimulus and outcome of every
trial of 17 example runs. The replay check (`--replay`) rebuilds each
example's grids and model, asks psy.quest for its selection at every trial,
and then feeds Watson's recorded stimulus and outcome. A selection that
differs counts as a tie when psy.quest's expected entropies of the two
stimuli differ by less than 1e-6 bits.

| Run | Trials | Same | Tie | Different | Final mode, ours and Watson's |
|---|---|---|---|---|---|
| threshold (3 runs) | 96 | 96 | 0 | 0 | identical |
| threshold, slope | 64 | 64 | 0 | 0 | -20, 3 |
| threshold, slope, lapse | 128 | 126 | 2 | 0 | -20, 5, 0.5, 0.04 |
| mean, sd, lapse | 128 | 128 | 0 | 0 | 0, 4, 0.01 |
| spatial CSF (4 runs) | 128 | 128 | 0 | 0 | identical, for example -32, -56, 1.4 |
| spatiotemporal CSF (2 runs) | 512 | 505 | 7 | 0 | -35, -50, 1.2, 1 |
| Thurstone scaling | 128 | 123 | 5 | 0 | 6, 0.2, 0.4 |
| blur increment threshold | 64 | 64 | 0 | 0 | 1.2, 1 |
| rating, 3 categories | 64 | 64 | 0 | 0 | 0.25, 1, 1 |
| rating, 4 categories | 128 | 128 | 0 | 0 | 0.45, 1, 2, 3 |
| circular categorization | 64 | 26 | 38 | 0 | 4, 60 deg, 120 deg, 120 deg |
| total | 1504 | 1452 | 52 | 0 | 17 of 17 identical |

Every tie is within 1.7e-8 bits, which is the float table. The circular
ties are exact: the three categories have equal widths, so stimuli 120 deg
apart have the same expected entropy. So psy.quest reproduces Watson's
reference implementation trial for trial on every example in his notebook.

mQUESTPlus is not in this replay: its demos draw from MATLAB's generator
and differ from the paper in true values and trial counts (for example 128
CSF trials, not 32). The replay checks against the code that mQUESTPlus
ports. mQUESTPlus itself runs in MATLAB through the MEX binding, in
[tests/compare/compare_quest_mquestplus.m](../tests/compare/compare_quest_mquestplus.m),
with mQUESTPlus choosing every stimulus: on the paper's figure 2, 3 and 4
examples and a marginalized case, 0 of 424 selections differ, and the
posteriors agree to 4.3e-7.

## Results by problem

### w1: threshold only (Watson's example 1)

Truth -20 dB. Watson's saved runs end at a mode of -21, -18 and -20 dB after
32 trials. Bias in dB at 32, 64 and 128 trials, with the sd over
replications in brackets.

| Method | Reps | Bias at 32 | Bias at 64 | Bias at 128 | MAE at 32 | Trials to 2 dB | ms/trial |
|---|---|---|---|---|---|---|---|
| 1-up-2-down | 100 | -1.09 ± 0.59 [2.99] | -0.82 ± 0.32 [1.62] | -0.66 ± 0.18 [0.93] | 2.27 | 39 | 0.002 |
| 1-up-3-down | 100 | -0.49 ± 0.45 [2.31] | -0.28 ± 0.23 [1.16] | -0.25 ± 0.13 [0.64] | 1.83 | 31 | 0.001 |
| weighted 1-up-1-down | 100 | +0.21 ± 0.72 [3.67] | +0.19 ± 0.31 [1.58] | +0.21 ± 0.14 [0.74] | 2.35 | 37 | 0.001 |
| QUEST | 100 | +0.00 ± 0.30 [1.55] | +0.06 ± 0.18 [0.91] | +0.10 ± 0.11 [0.57] | 1.17 | 19 | 0.006 |
| Psi | 100 | -0.10 ± 0.28 [1.44] | -0.02 ± 0.16 [0.80] | +0.03 ± 0.11 [0.55] | 1.14 | 19 | 0.018 |
| Psi-marginal | 100 | -0.09 ± 0.29 [1.47] | -0.02 ± 0.16 [0.82] | +0.08 ± 0.11 [0.56] | 1.15 | 17 | 0.20 |
| GP, LSE | 50 | -0.11 ± 0.88 [3.16] | +0.05 ± 0.34 [1.22] | +0.06 ± 0.22 [0.81] | 2.00 | 32 | 3.7 |
| GP, EAVC | 50 | -0.25 ± 0.68 [2.44] | -0.13 ± 0.30 [1.07] | -0.05 ± 0.22 [0.79] | 1.93 | 32 | 5.4 |
| psychometric, LSE | 50 | -0.56 ± 0.53 [1.90] | +0.07 ± 0.34 [1.23] | +0.00 ± 0.19 [0.67] | 1.46 | 21 | 5.5 |
| psychometric, EAVC | 50 | +0.05 ± 0.49 [1.77] | +0.11 ± 0.25 [0.91] | +0.10 ± 0.20 [0.72] | 1.49 | 25 | 8.7 |

With Watson's estimator, the joint mode, QUEST has a bias of -0.09 dB and
an sd of 1.58 dB at 32 trials. Psi's slope estimate at 32 trials is 3.51
[0.25] against a truth of 3.5.

The three QUEST+ configurations reach 2 dB in 17 to 19 trials and have
half the sd of a staircase at every trial count. The psychometric model
comes next, and the GP model is the slowest. The transformed
staircases have a real bias at
128 trials: the 1-up-2-down reversal mean sits 0.66 dB below its own
convergence point. Two caveats favor the Bayesian methods here. The truth
is at the middle of Watson's grid, where a uniform prior puts its mean. And
the psychometric model's threshold prior is centered on the middle of the
axis too.

### w2: threshold and slope (Watson's threshold-and-slope example)

Truth -20 dB, slope 3. Watson's saved run ends at a mode of -20, 3 and an
ML fit of -19.5, 3.64 after 64 trials. Slope columns are at 64 trials.

| Method | Reps | Bias at 32 | Bias at 64 | Bias at 128 | MAE at 64 | Slope at 64 | Trials to 2 dB | ms/trial |
|---|---|---|---|---|---|---|---|---|
| 1-up-2-down | 100 | -0.46 ± 0.55 [2.83] | -0.69 ± 0.33 [1.66] | -0.61 ± 0.22 [1.10] | 1.23 | | 32 | 0.001 |
| 1-up-3-down | 100 | -0.36 ± 0.70 [3.57] | -0.34 ± 0.28 [1.45] | -0.20 ± 0.18 [0.89] | 1.10 | | 35 | 0.001 |
| weighted 1-up-1-down | 100 | +1.01 ± 0.70 [3.56] | +0.45 ± 0.35 [1.79] | +0.24 ± 0.16 [0.82] | 1.25 | | 38 | 0.002 |
| QUEST (slope given) | 100 | +0.08 ± 0.32 [1.66] | -0.01 ± 0.18 [0.93] | +0.07 ± 0.13 [0.65] | 0.72 | | 19 | 0.005 |
| Psi (the paper's configuration) | 100 | +0.13 ± 0.33 [1.68] | +0.09 ± 0.17 [0.89] | +0.11 ± 0.13 [0.67] | 0.70 | 3.48 [0.38] | 20 | 0.015 |
| Psi-marginal | 100 | +0.00 ± 0.30 [1.55] | +0.06 ± 0.17 [0.88] | +0.11 ± 0.13 [0.66] | 0.69 | 3.46 [0.38] | 19 | 0.14 |
| GP, LSE | 50 | +0.36 ± 0.45 [1.61] | -0.16 ± 0.32 [1.16] | +0.27 ± 0.28 [1.01] | 0.93 |  | 25 | 4.5 |
| GP, EAVC | 50 | +0.30 ± 0.60 [2.15] | +0.02 ± 0.30 [1.08] | +0.13 ± 0.21 [0.76] | 0.83 |  | 27 | 6.0 |
| psychometric, LSE | 50 | +0.03 ± 0.39 [1.41] | -0.17 ± 0.32 [1.16] | +0.03 ± 0.22 [0.78] | 0.86 | 1.64 [0.81] | 15 | 5.3 |
| psychometric, EAVC | 50 | +0.56 ± 0.40 [1.46] | +0.05 ± 0.30 [1.09] | +0.15 ± 0.20 [0.72] | 0.89 | 2.59 [1.43] | 15 | 9.0 |

The slope of the psychometric model is the Weibull slope with the same 25
to 75 percent spread as its probit. With Watson's estimator, Psi's joint
mode, the threshold bias is +0.21 [1.01] dB and the slope is 4.12 [1.10]:
on a slope grid of 2, 3, 4, 5 the mode is biased up by one grid step, and
the marginal mean is not.

Psi, Psi-marginal and QUEST with the true slope are equal on threshold. So
the free slope costs nothing here. The psychometric model reaches the
criterion first, but its slope is half the truth under LSE. psy_gp.h 0.4.1
documents that shrinkage: it comes from the prior on the mean log slope.
The GP model reaches 2 dB in 25 to 27 trials, against 19 to 20 for QUEST+.

### w3: threshold, slope and lapse (Watson's threshold-slope-lapse example)

Truth -20 dB, slope 3, lapse 0.03. Watson's saved run ends at a mode of
-20, 5, 0.5, 0.04 and an ML fit of -19.5, 5.0, 0.035 after 128 trials. Slope
columns are at 128 trials.

| Method | Reps | Bias at 32 | Bias at 64 | Bias at 128 | MAE at 128 | Slope at 128 | Trials to 2 dB | ms/trial |
|---|---|---|---|---|---|---|---|---|
| 1-up-2-down | 100 | -1.15 ± 0.70 [3.57] | -1.02 ± 0.46 [2.36] | -0.84 ± 0.26 [1.31] | 1.14 | | 47 | 0.001 |
| 1-up-3-down | 100 | +0.07 ± 0.82 [4.17] | -0.08 ± 0.34 [1.74] | -0.20 ± 0.17 [0.89] | 0.67 | | 37 | 0.001 |
| weighted 1-up-1-down | 100 | +1.36 ± 0.88 [4.50] | +0.43 ± 0.36 [1.85] | +0.19 ± 0.16 [0.80] | 0.63 | | 47 | 0.001 |
| QUEST (slope and lapse given) | 100 | -0.13 ± 0.33 [1.67] | -0.08 ± 0.19 [0.96] | +0.01 ± 0.12 [0.60] | 0.47 | | 18 | 0.004 |
| Psi, lapse fixed at 0.02 | 100 | +0.16 ± 0.41 [2.10] | +0.18 ± 0.18 [0.94] | +0.20 ± 0.14 [0.72] | 0.58 | 3.35 [0.55] | 19 | 0.012 |
| QUEST+ joint (the paper's configuration) | 100 | +0.21 ± 0.40 [2.04] | +0.24 ± 0.17 [0.88] | +0.25 ± 0.13 [0.64] | 0.54 | 3.44 [0.56] | 18 | 0.059 |
| Psi-marginal | 100 | +0.07 ± 0.39 [1.98] | +0.11 ± 0.19 [0.96] | +0.18 ± 0.14 [0.70] | 0.57 | 3.47 [0.56] | 19 | 0.11 |
| GP, LSE | 50 | +0.16 ± 0.64 [2.31] | -0.19 ± 0.44 [1.59] | +0.06 ± 0.20 [0.73] | 0.59 |  | 38 | 2.9 |
| GP, EAVC | 50 | -0.28 ± 0.85 [3.08] | -0.19 ± 0.46 [1.65] | -0.09 ± 0.22 [0.79] | 0.59 |  | 45 | 4.7 |
| psychometric, LSE | 50 | +0.07 ± 0.46 [1.65] | -0.21 ± 0.37 [1.33] | -0.04 ± 0.19 [0.67] | 0.53 | 2.67 [1.58] | 16 | 5.3 |
| psychometric, EAVC | 50 | +0.51 ± 0.55 [1.98] | -0.15 ± 0.34 [1.23] | -0.02 ± 0.19 [0.68] | 0.51 | 3.19 [1.61] | 16 | 8.3 |

With Watson's estimator the joint QUEST+ run has a threshold bias of +0.32
[0.74] dB and a slope of 3.60 [1.10] at 128 trials.

The joint and marginal QUEST+ configurations have a small positive bias,
0.18 to 0.25 dB, outside their intervals. Their sd is the same as QUEST's
with the truth given. At 128 trials the 1-up-3-down and weighted
staircases are within 0.2 dB of QUEST+ in MAE, but they need twice the
trials to reach 2 dB and stay there. The psychometric model is as good as
QUEST+ on threshold, with a wide slope spread.

### w4: mean, sd and lapse of a cumulative normal (Watson's orientation example)

Truth: mean 1 deg, sd 3 deg, lapse 0.02. Watson's saved run ends at a mode
of 0, 4, 0.01 and an ML fit of -0.16, 4.17, 0 after 128 trials. The target
p is 0.5, so the weighted rule is a plain 1-up-1-down. "Slope" is the sd.

| Method | Reps | Bias at 32 | Bias at 64 | Bias at 128 | MAE at 128 | sd at 128 | Trials to 0.5 deg | ms/trial |
|---|---|---|---|---|---|---|---|---|
| 1-up-2-down | 100 | -0.03 ± 0.18 [0.92] | +0.01 ± 0.12 [0.60] | +0.03 ± 0.08 [0.43] | 0.33 | | 62 | 0.001 |
| 1-up-3-down | 100 | +0.14 ± 0.24 [1.25] | +0.18 ± 0.14 [0.72] | +0.12 ± 0.10 [0.49] | 0.38 | | 84 | 0.001 |
| 1-up-1-down | 100 | -0.12 ± 0.16 [0.81] | -0.02 ± 0.11 [0.54] | +0.01 ± 0.08 [0.42] | 0.32 | | 47 | 0.001 |
| QUEST (sd and lapse given) | 100 | -0.08 ± 0.12 [0.62] | +0.00 ± 0.09 [0.48] | +0.03 ± 0.06 [0.32] | 0.20 | | 31 | 0.002 |
| QUEST+ joint (the paper's configuration) | 100 | -0.02 ± 0.18 [0.90] | -0.01 ± 0.13 [0.66] | +0.02 ± 0.08 [0.43] | 0.31 | 3.21 [0.52] | 62 | 0.028 |
| Psi-marginal | 100 | -0.04 ± 0.18 [0.90] | +0.04 ± 0.14 [0.71] | +0.03 ± 0.08 [0.43] | 0.32 | 3.17 [0.43] | 72 | 0.068 |
| GP, LSE | 50 | -0.08 ± 0.24 [0.88] | +0.07 ± 0.18 [0.65] | +0.02 ± 0.13 [0.47] | 0.37 |  | 75 | 2.0 |
| GP, EAVC | 50 | -0.09 ± 0.25 [0.92] | +0.06 ± 0.17 [0.61] | +0.05 ± 0.11 [0.39] | 0.32 |  | 72 | 2.4 |
| psychometric, LSE | 50 | -0.10 ± 0.22 [0.78] | +0.07 ± 0.16 [0.59] | +0.00 ± 0.12 [0.44] | 0.34 | 2.87 [1.07] | 64 | 7.9 |
| psychometric, EAVC | 50 | -0.04 ± 0.21 [0.76] | +0.06 ± 0.17 [0.60] | +0.05 ± 0.12 [0.44] | 0.35 | 2.78 [1.20] | 72 | 7.1 |

On a symmetric yes/no task every method finds the 50 percent point equally
well, 0.30 to 0.38 deg of MAE at 128 trials, and none is biased. Only
QUEST, which is given the true sd, is better. A plain 1-up-1-down is as
good as QUEST+ here and costs nothing.

### The audiogram (Owen et al., 8 cells)

The paper reports these results as curves, not numbers, so no paper number
stands beside ours. Each table cell is at 150 trials unless the column says
otherwise.

| Phenotype, beta | Method | Reps | MAE(p) | Band MAE | thr MAE at 50, dB | thr MAE, dB | Trials to 2 dB | ms/trial |
|---|---|---|---|---|---|---|---|---|
| older-normal, 2 | interleaved staircases | 100 | 0.0123 ± 0.0008 | 0.205 ± 0.010 | 10.04 ± 0.00 | 1.80 ± 0.12 | 140 | 0.003 |
|  | GP, LSE | 20 | 0.074 ± 0.006 | 0.237 ± 0.009 | 2.84 ± 0.42 | 2.08 ± 0.26 | > 150 | 2.7 |
|  | GP, EAVC | 20 | 0.069 ± 0.003 | 0.231 ± 0.007 | 2.26 ± 0.29 | 1.61 ± 0.17 | 75 | 7.6 |
|  | GP, BALV | 20 | 0.046 ± 0.002 | 0.214 ± 0.006 | 5.26 ± 0.49 | 3.12 ± 0.26 | > 150 | 3.0 |
|  | psychometric, LSE | 20 | 0.019 ± 0.003 | 0.186 ± 0.011 | 2.35 ± 0.57 | 1.27 ± 0.05 | 55 | 8.5 |
|  | psychometric, EAVC | 20 | 0.018 ± 0.003 | 0.172 ± 0.011 | 2.03 ± 0.25 | 1.27 ± 0.15 | 55 | 23 |
|  | psychometric, BALV | 20 | 0.0089 ± 0.0012 | 0.154 ± 0.018 | 1.74 ± 0.20 | 1.10 ± 0.14 | 45 | 11 |
|  | psychometric, BALD | 20 | 0.0091 ± 0.0016 | 0.151 ± 0.016 | 1.58 ± 0.21 | 1.20 ± 0.23 | 45 | 10 |
| older-normal, 0.5 | interleaved staircases | 100 | 0.0136 ± 0.0005 | 0.450 ± 0.006 | 11.60 ± 0.00 | 2.04 ± 0.08 | > 150 | 0.002 |
|  | GP, LSE | 20 | 0.070 ± 0.002 | 0.299 ± 0.003 | 2.09 ± 0.19 | 1.38 ± 0.07 | 60 | 3.2 |
|  | GP, EAVC | 20 | 0.077 ± 0.006 | 0.285 ± 0.003 | 2.15 ± 0.25 | 1.91 ± 0.41 | 140 | 6.6 |
|  | GP, BALV | 20 | 0.050 ± 0.000 | 0.298 ± 0.003 | 5.40 ± 0.34 | 3.40 ± 0.08 | > 150 | 2.9 |
|  | psychometric, LSE | 20 | 0.015 ± 0.006 | 0.304 ± 0.011 | 1.62 ± 0.18 | 1.33 ± 0.64 | 45 | 10.0 |
|  | psychometric, EAVC | 20 | 0.017 ± 0.003 | 0.281 ± 0.022 | 1.52 ± 0.18 | 1.06 ± 0.15 | 45 | 25 |
|  | psychometric, BALV | 20 | 0.0043 ± 0.0010 | 0.217 ± 0.017 | 1.38 ± 0.17 | 0.53 ± 0.09 | 45 | 17 |
|  | psychometric, BALD | 20 | 0.0043 ± 0.0010 | 0.227 ± 0.021 | 1.12 ± 0.03 | 0.55 ± 0.11 | 40 | 20 |
| sensory, 2 | interleaved staircases | 100 | 0.0146 ± 0.0007 | 0.226 ± 0.008 | 7.38 ± 0.10 | 1.94 ± 0.10 | 145 | 0.003 |
|  | GP, LSE | 20 | 0.095 ± 0.004 | 0.238 ± 0.005 | 5.25 ± 0.59 | 1.81 ± 0.17 | 135 | 3.9 |
|  | GP, EAVC | 20 | 0.079 ± 0.003 | 0.229 ± 0.006 | 4.78 ± 0.55 | 1.48 ± 0.11 | 105 | 18 |
|  | GP, BALV | 20 | 0.062 ± 0.002 | 0.200 ± 0.006 | 14.51 ± 0.63 | 4.34 ± 0.39 | > 150 | 8.4 |
|  | psychometric, LSE | 20 | 0.023 ± 0.005 | 0.202 ± 0.021 | 4.82 ± 0.52 | 1.91 ± 0.24 | 145 | 33 |
|  | psychometric, EAVC | 20 | 0.023 ± 0.006 | 0.192 ± 0.015 | 4.53 ± 0.28 | 1.64 ± 0.20 | 130 | 84 |
|  | psychometric, BALV | 20 | 0.0081 ± 0.0029 | 0.117 ± 0.018 | 6.20 ± 1.26 | 0.98 ± 0.46 | 105 | 27 |
|  | psychometric, BALD | 20 | 0.0077 ± 0.0021 | 0.127 ± 0.021 | 4.21 ± 0.56 | 0.95 ± 0.22 | 85 | 34 |
| sensory, 0.5 | interleaved staircases | 100 | 0.0132 ± 0.0002 | 0.444 ± 0.004 | 7.80 ± 0.05 | 1.79 ± 0.04 | 130 | 0.001 |
|  | GP, LSE | 20 | 0.098 ± 0.012 | 0.283 ± 0.003 | 6.14 ± 0.75 | 2.35 ± 0.30 | > 150 | 9.4 |
|  | GP, EAVC | 20 | 0.078 ± 0.002 | 0.279 ± 0.003 | 4.84 ± 0.61 | 1.77 ± 0.13 | 105 | 22 |
|  | GP, BALV | 20 | 0.067 ± 0.001 | 0.266 ± 0.001 | 13.39 ± 1.16 | 4.82 ± 0.21 | > 150 | 8.3 |
|  | psychometric, LSE | 20 | 0.024 ± 0.006 | 0.255 ± 0.012 | 5.06 ± 0.57 | 1.91 ± 0.46 | 135 | 31 |
|  | psychometric, EAVC | 20 | 0.014 ± 0.004 | 0.247 ± 0.011 | 4.51 ± 0.20 | 1.37 ± 0.28 | 110 | 30 |
|  | psychometric, BALV | 20 | 0.0048 ± 0.0009 | 0.226 ± 0.013 | 5.51 ± 1.12 | 0.65 ± 0.13 | 85 | 10 |
|  | psychometric, BALD | 20 | 0.0037 ± 0.0008 | 0.201 ± 0.013 | 3.63 ± 0.62 | 0.55 ± 0.11 | 70 | 12 |
| metabolic, 2 | interleaved staircases | 100 | 0.0118 ± 0.0006 | 0.189 ± 0.007 | 3.49 ± 0.19 | 1.84 ± 0.09 | 125 | 0.001 |
|  | GP, LSE | 20 | 0.104 ± 0.010 | 0.250 ± 0.006 | 4.47 ± 0.98 | 1.80 ± 0.48 | 95 | 2.5 |
|  | GP, EAVC | 20 | 0.074 ± 0.003 | 0.233 ± 0.006 | 4.22 ± 0.52 | 1.26 ± 0.16 | 85 | 7.0 |
|  | GP, BALV | 20 | 0.049 ± 0.001 | 0.195 ± 0.002 | 10.99 ± 0.74 | 4.21 ± 0.32 | > 150 | 3.2 |
|  | psychometric, LSE | 20 | 0.016 ± 0.003 | 0.177 ± 0.016 | 2.42 ± 0.46 | 1.49 ± 0.17 | 60 | 9.4 |
|  | psychometric, EAVC | 20 | 0.013 ± 0.002 | 0.152 ± 0.011 | 2.24 ± 0.33 | 1.27 ± 0.11 | 65 | 29 |
|  | psychometric, BALV | 20 | 0.010 ± 0.001 | 0.140 ± 0.007 | 2.52 ± 0.76 | 1.20 ± 0.12 | 65 | 9.5 |
|  | psychometric, BALD | 20 | 0.0084 ± 0.0005 | 0.140 ± 0.005 | 1.48 ± 0.19 | 1.02 ± 0.05 | 45 | 12 |
| metabolic, 0.5 | interleaved staircases | 100 | 0.0117 ± 0.0004 | 0.436 ± 0.004 | 2.98 ± 0.03 | 1.80 ± 0.07 | 115 | 0.001 |
|  | GP, LSE | 20 | 0.120 ± 0.024 | 0.301 ± 0.003 | 3.88 ± 0.61 | 1.89 ± 0.26 | 95 | 2.7 |
|  | GP, EAVC | 20 | 0.084 ± 0.007 | 0.291 ± 0.004 | 3.86 ± 0.43 | 1.45 ± 0.27 | 80 | 8.1 |
|  | GP, BALV | 20 | 0.056 ± 0.001 | 0.257 ± 0.000 | 10.72 ± 0.62 | 4.87 ± 0.23 | > 150 | 2.6 |
|  | psychometric, LSE | 20 | 0.012 ± 0.002 | 0.274 ± 0.014 | 3.00 ± 0.60 | 1.26 ± 0.16 | 65 | 11 |
|  | psychometric, EAVC | 20 | 0.012 ± 0.003 | 0.244 ± 0.019 | 1.79 ± 0.21 | 0.99 ± 0.15 | 45 | 27 |
|  | psychometric, BALV | 20 | 0.0050 ± 0.0012 | 0.214 ± 0.034 | 1.49 ± 0.19 | 0.68 ± 0.18 | 45 | 11 |
|  | psychometric, BALD | 20 | 0.0044 ± 0.0012 | 0.198 ± 0.030 | 1.36 ± 0.11 | 0.61 ± 0.18 | 45 | 15 |
| metabolic+sensory, 2 | interleaved staircases | 100 | 0.0144 ± 0.0007 | 0.209 ± 0.007 | 3.12 ± 0.17 | 2.09 ± 0.10 | > 150 | 0.001 |
|  | GP, LSE | 20 | 0.090 ± 0.004 | 0.234 ± 0.006 | 4.48 ± 0.65 | 2.10 ± 0.20 | > 150 | 2.4 |
|  | GP, EAVC | 20 | 0.076 ± 0.003 | 0.220 ± 0.006 | 5.28 ± 0.66 | 1.73 ± 0.23 | 135 | 6.6 |
|  | GP, BALV | 20 | 0.064 ± 0.001 | 0.203 ± 0.003 | 9.53 ± 0.78 | 3.77 ± 0.28 | > 150 | 2.7 |
|  | psychometric, LSE | 20 | 0.017 ± 0.008 | 0.175 ± 0.023 | 4.22 ± 0.64 | 1.89 ± 0.84 | 145 | 6.8 |
|  | psychometric, EAVC | 20 | 0.011 ± 0.002 | 0.142 ± 0.017 | 3.76 ± 0.43 | 1.04 ± 0.19 | 80 | 24 |
|  | psychometric, BALV | 20 | 0.0070 ± 0.0010 | 0.122 ± 0.018 | 4.06 ± 1.30 | 0.83 ± 0.12 | 65 | 8.1 |
|  | psychometric, BALD | 20 | 0.0059 ± 0.0008 | 0.105 ± 0.014 | 2.13 ± 0.51 | 0.66 ± 0.09 | 55 | 11 |
| metabolic+sensory, 0.5 | interleaved staircases | 100 | 0.0128 ± 0.0003 | 0.459 ± 0.005 | 2.25 ± 0.04 | 1.87 ± 0.04 | 115 | 0.001 |
|  | GP, LSE | 20 | 0.103 ± 0.013 | 0.297 ± 0.003 | 4.35 ± 0.76 | 1.92 ± 0.20 | 145 | 4.7 |
|  | GP, EAVC | 20 | 0.083 ± 0.002 | 0.284 ± 0.003 | 4.35 ± 0.40 | 1.78 ± 0.17 | 120 | 7.3 |
|  | GP, BALV | 20 | 0.071 ± 0.001 | 0.263 ± 0.003 | 9.68 ± 0.82 | 3.98 ± 0.19 | > 150 | 3.5 |
|  | psychometric, LSE | 20 | 0.0081 ± 0.0025 | 0.227 ± 0.014 | 3.80 ± 0.53 | 0.71 ± 0.21 | 80 | 17 |
|  | psychometric, EAVC | 20 | 0.0074 ± 0.0026 | 0.204 ± 0.017 | 3.71 ± 0.49 | 0.69 ± 0.24 | 65 | 28 |
|  | psychometric, BALV | 20 | 0.0032 ± 0.0006 | 0.190 ± 0.019 | 3.15 ± 1.49 | 0.37 ± 0.05 | 65 | 11 |
|  | psychometric, BALD | 20 | 0.0023 ± 0.0004 | 0.185 ± 0.015 | 1.45 ± 0.60 | 0.32 ± 0.04 | 50 | 12 |

The psychometric model with BALD or BALV is the best method on threshold
in every cell (0.32 to 1.10 dB). BALD is best in 6 of 8 cells and BALV in
the other 2, within each other's intervals. BALD reaches 2 dB in 40 to 85
trials. The
informed staircase needs 115 to more than 150 trials for the same
criterion, and it has the true slope for free. The RBF GP is between the
two on threshold and 10 to 30 times worse than the psychometric model on
MAE(p): a stationary kernel cannot hold a rise of 1 to 5 dB inside a 140
dB axis. BALV on the RBF GP gives the lowest RBF field error and the worst
threshold, as the paper found.

No replication collapses under 0.13.1. The worst single replications are
9.5 dB (psychometric, LSE, metabolic+sensory, beta 2, replication 14) and
7.6 dB (psychometric, LSE, older-normal, beta 0.5, replication 10). Both
were the same under 0.4.1, and in both the model keeps a crossing in every
column.

### Novel detection and discrimination (Owen et al.)

The paper plots these as curves on axes labeled RMSE, and its text calls
them MAE. So no paper number stands beside ours. Criterion 0.1 intensity
units, 5 percent of the axis.

| Problem | Method | Reps | MAE(p) | Band MAE | thr MAE at 50 | thr MAE | No-cross | Trials to 0.1 | ms/trial |
|---|---|---|---|---|---|---|---|---|---|
| detection | interleaved staircases | 100 | 0.021 ± 0.001 | 0.112 ± 0.006 | 0.10 ± 0.00 | 0.04 ± 0.00 | 0 | 50 | 0.003 |
|  | GP, LSE | 20 | 0.093 ± 0.004 | 0.183 ± 0.007 | 0.10 ± 0.02 | 0.04 ± 0.00 | 0 | 55 | 2.4 |
|  | GP, EAVC | 20 | 0.081 ± 0.006 | 0.165 ± 0.011 | 0.10 ± 0.01 | 0.03 ± 0.00 | 0 | 55 | 6.1 |
|  | GP, BALV | 20 | 0.057 ± 0.004 | 0.111 ± 0.006 | 0.19 ± 0.02 | 0.08 ± 0.01 | 0 | 120 | 4.1 |
|  | psychometric, LSE | 20 | 0.032 ± 0.003 | 0.166 ± 0.022 | 0.08 ± 0.01 | 0.04 ± 0.00 | 0 | 45 | 8.8 |
|  | psychometric, EAVC | 20 | 0.033 ± 0.006 | 0.136 ± 0.012 | 0.07 ± 0.02 | 0.03 ± 0.00 | 0 | 40 | 24 |
|  | psychometric, BALV | 20 | 0.022 ± 0.002 | 0.100 ± 0.010 | 0.12 ± 0.04 | 0.03 ± 0.01 | 0 | 65 | 8.8 |
|  | psychometric, BALD | 20 | 0.019 ± 0.002 | 0.098 ± 0.017 | 0.09 ± 0.03 | 0.03 ± 0.01 | 0 | 45 | 11 |
| discrimination | interleaved staircases | 100 | 0.022 ± 0.002 | 0.107 ± 0.010 | 0.17 ± 0.01 | 0.07 ± 0.01 | 0.01 | 85 | 0.004 |
|  | GP, LSE | 20 | 0.050 ± 0.006 | 0.066 ± 0.011 | 0.13 ± 0.02 | 0.05 ± 0.02 | 0.40 | 80 | 2.7 |
|  | GP, EAVC | 20 | 0.044 ± 0.003 | 0.074 ± 0.009 | 0.10 ± 0.03 | 0.05 ± 0.01 | 1.20 | 45 | 6.7 |
|  | GP, BALV | 20 | 0.060 ± 0.004 | 0.088 ± 0.008 | 0.16 ± 0.03 | 0.12 ± 0.01 | 0.35 | > 150 | 2.9 |
|  | psychometric, LSE | 20 | 0.026 ± 0.004 | 0.108 ± 0.022 | 0.09 ± 0.02 | 0.04 ± 0.01 | 0 | 40 | 8.6 |
|  | psychometric, EAVC | 20 | 0.024 ± 0.003 | 0.102 ± 0.021 | 0.07 ± 0.01 | 0.04 ± 0.01 | 0.60 | 25 | 81 |
|  | psychometric, BALV | 20 | 0.028 ± 0.007 | 0.086 ± 0.011 | 0.21 ± 0.03 | 0.07 ± 0.01 | 0 | 110 | 21 |
|  | psychometric, BALD | 20 | 0.016 ± 0.002 | 0.059 ± 0.008 | 0.14 ± 0.03 | 0.04 ± 0.01 | 0 | 70 | 32 |

Both functions are exactly of the psychometric model's form: a probit in
intensity whose location and slope change with the context. So that model
fits them, and it has the best field error. On detection every method
reaches 0.03 to 0.04 at 150 trials except GP BALV.

The discrimination function is monotone in intensity, so the monotonicity
assumption of the psychometric model holds and nothing breaks. The problem
is harder for the RBF GP, because the thresholds lie between -0.97 and
-0.58, near the bottom of the box. Under 0.4.1 the GP model with EAVC
failed here (22 of 30 columns with no crossing). Under 0.13.1 it has 1.2
no-cross columns on average and the best RBF threshold error, 0.05. The
thr MAE of a row averages only the columns that cross, so read it with the
no-cross column.

### Watson's spatial CSF

Watson's saved 32-trial runs end at modes of (-32, -56, 1.4), (-36, -48,
1.2) and (-30, -50, 1.2) against the truth (-35, -50, 1.2). Criterion 2 dB.

| Method | Reps | MAE(p) | Band MAE | thr MAE at 50, dB | thr MAE, dB | No-cross | Trials to 2 dB | ms/trial |
|---|---|---|---|---|---|---|---|---|
| interleaved staircases (6 frequencies) | 100 | 0.028 ± 0.003 | 0.031 ± 0.003 | 11.30 ± 0.22 | 2.91 ± 0.36 | 0 | > 150 | 0.001 |
| QUEST+ (Watson's model and grid) | 100 | 0.0059 ± 0.0008 | 0.0096 ± 0.0014 | 1.36 ± 0.14 | 0.63 ± 0.09 | 0.06 | 25 | 0.53 |
| GP, LSE | 20 | 0.099 ± 0.005 | 0.118 ± 0.006 | 3.80 ± 1.13 | 1.65 ± 0.24 | 0.35 | 100 | 8.2 |
| GP, EAVC | 20 | 0.086 ± 0.008 | 0.104 ± 0.010 | 4.32 ± 1.31 | 1.65 ± 0.58 | 0.90 | 135 | 25 |
| GP, BALV | 20 | 0.074 ± 0.008 | 0.082 ± 0.010 | 5.91 ± 1.44 | 2.14 ± 0.31 | 0.10 | > 150 | 17 |
| psychometric, LSE | 20 | 0.032 ± 0.007 | 0.040 ± 0.008 | 3.11 ± 1.05 | 1.16 ± 0.23 | 0 | 80 | 28 |
| psychometric, EAVC | 20 | 0.024 ± 0.007 | 0.031 ± 0.008 | 2.87 ± 0.79 | 1.14 ± 0.18 | 0 | 85 | 69 |
| psychometric, BALV | 20 | 0.028 ± 0.008 | 0.035 ± 0.008 | 4.30 ± 0.89 | 1.97 ± 0.48 | 0.25 | 150 | 9.4 |
| psychometric, BALD | 20 | 0.027 ± 0.010 | 0.038 ± 0.014 | 3.39 ± 1.01 | 1.23 ± 0.27 | 0 | 75 | 9.8 |

QUEST+ parameter estimates, mean [sd] over 100 replications:

| Trials | Estimator | t (truth -35) | c0 (truth -50) | cf (truth 1.2) |
|---|---|---|---|---|
| 32 | marginal mean | -35.01 [2.87] | -49.95 [3.45] | 1.207 [0.130] |
| 32 | joint mode (Watson's) | -34.52 [3.37] | -50.14 [5.47] | 1.206 [0.190] |
| 150 | marginal mean | -35.27 [0.91] | -50.27 [1.51] | 1.207 [0.048] |
| 150 | joint mode (Watson's) | -35.36 [1.06] | -50.14 [1.61] | 1.202 [0.053] |

Watson's three saved runs lie within 1.5 sd of our 32-trial spread of the
mode. With the right parametric model QUEST+ has half the threshold error
and a quarter of the field error of the next best method, and it reaches
2 dB in 25 trials. The
psychometric model is the best nonparametric method. The staircases do
worst: 25 trials per frequency is few, and a line between the staircase
frequencies cuts the corner of the true curve at 12.5 c/deg.

### Watson's circular categorization

MAE(p) is the mean over 72 directions and 3 categories. Criterion 0.05.

| Method | Reps | MAE(p) at 64 | MAE(p) at 128 | First boundary error at 64, deg | Concentration bias at 64 | Trials to 0.05 | ms/trial |
|---|---|---|---|---|---|---|---|
| QUEST+ (Watson's grid, joint mode) | 100 | 0.018 ± 0.005 | 0.011 ± 0.004 | 1.0 ± 0.9 | +0.42 ± 0.23 | 20 | 0.02 |
| GP categorical, BALD | 20 | 0.085 ± 0.008 | 0.066 ± 0.006 |  |  | > 128 | 21 |
| GP categorical, BALV | 20 | 0.084 ± 0.007 | 0.067 ± 0.005 |  |  | > 128 | 27 |

QUEST+ with the true model finds the boundary on its 20 deg grid in almost
every replication. The categorical GP is the nonparametric counterpart. Its
RBF kernel is not periodic, so it does not know that 0 and 360 deg are the
same direction, and it does not reach 0.05 in 128 trials. psy_gp.h has no
periodic kernel. The categorical fit uses central differences and costs
20 to 30 ms per trial.

### Letham et al.'s real-data contrast sensitivity function (csf6)

This problem comes from Letham et al. (2022), the look-ahead acquisition
paper (`ContrastSensitivity6d` in its repository, bernoulli_lse). The data
are 1001 trials of a human contrast detection study. The truth is a GP
classifier fit to all of them: p(x) = Phi(max(mu(x), 0)), with mu the
posterior mean of the latent. The clamp at 0 puts a floor of 0.5 on p, as
in a two-alternative task. The box is log10 contrast -1.5 to 0, log10
pedestal -1.5 to 0, temporal frequency 0 to 20 Hz, spatial frequency 0.5 to
7 c/deg, size 1 to 10 deg and eccentricity 0 to 10 deg. The target is 0.75,
and the level set is a 5-D surface. The script downloads the CSV into a
cache directory at run time and cites it, because the data license is not
stated.

The truth is not the paper's surface. The paper fit AEPsych's 2022 model on
raw inputs. Here AEPsych 0.8.0 fits `GPClassificationModel(dim=6,
inducing_size=100)` with k-means++ inducing points on the unit cube, because
its lengthscale prior assumes the unit cube. A second truth comes from
psy.gp's GP model (RBF ARD, probit). The binding holds at most 512 trials,
so that fit uses the first 512 of the 1001 trials. With psy_gp.h 0.13.1
the two truths differ by 0.033 in MAE(p) over 2000 Sobol points. Their
0.75 thresholds along contrast differ by 0.22 log10 units on average, and
by up to 1.39, at the 104 of 200 random contexts where both cross. At 24
contexts only one of them crosses. (Under 0.4.1 the psy.gp truth gave
0.033, 0.23 and 1.35.) So two reasonable fits to the same data disagree
about the threshold by 0.22 log10 units, which is more than twice the 0.1
criterion.
The tables use the AEPsych truth.

Staircases and QUEST+ do not apply. A staircase needs fixed contexts, and
this problem has a 5-D context. QUEST+ needs a parametric model, and there
is none.

Protocol: 10 scrambled Sobol trials, then 290 adaptive trials, 10
replications, metrics every 10 trials. The paper ran 740 adaptive trials
and 300 replications. One replication at 500 trials took 8 to 16 minutes
per method on the loaded machine, so 10 replications of one method would
have taken more than an hour. The run was cut to 300 trials. Candidates
are 1000 Halton points with `refine_steps=2`, `fit_every=20`, and
`guess=0.5` for every psy.gp method. The psychometric model uses contrast
(`intensity_dim=0`) as intensity. "Sobol (random)" is the GP model fed
Sobol points, which is the paper's baseline. MAE(p) is over 2000 Sobol
points. The threshold MAE brackets 200 contrast points at each of 200
random contexts. Only the 118 contexts where the truth crosses 0.75 count.
The band MAE is over the points of those lines with true p between 0.05
and 0.95. Because of the 0.5 floor, that is 85 percent of them. ms/trial
comes from the loaded parallel run, 8 workers, not from a serial pass.

| Method | Reps | MAE(p) at 150 | MAE(p) at 300 | Band MAE at 300 | thr MAE at 150 | thr MAE at 300 | No-cross of 118 | ms/trial |
|---|---|---|---|---|---|---|---|---|
| Sobol (random), GP model | 10 | 0.093 ± 0.014 | 0.071 ± 0.007 | 0.076 ± 0.008 | 0.319 ± 0.046 | 0.275 ± 0.047 | 29 | 93 |
| GP, LSE | 10 | 0.108 ± 0.010 | 0.080 ± 0.012 | 0.080 ± 0.011 | 0.409 ± 0.045 | 0.280 ± 0.051 | 40 | 103 |
| GP, EAVC | 10 | 0.089 ± 0.009 | 0.071 ± 0.008 | 0.071 ± 0.008 | 0.323 ± 0.066 | 0.252 ± 0.035 | 27 | 302 |
| GP, BALV | 10 | 0.101 ± 0.008 | 0.088 ± 0.009 | 0.092 ± 0.009 | 0.362 ± 0.039 | 0.292 ± 0.046 | 45 | 92 |
| psychometric, LSE | 10 | 0.095 ± 0.022 | 0.061 ± 0.009 | 0.068 ± 0.011 | 0.345 ± 0.076 | 0.236 ± 0.033 | 10 | 152 |
| psychometric, EAVC | 10 | 0.095 ± 0.014 | 0.068 ± 0.010 | 0.075 ± 0.009 | 0.311 ± 0.037 | 0.237 ± 0.029 | 2 | 2893 |
| psychometric, BALV | 10 | 0.107 ± 0.018 | 0.063 ± 0.016 | 0.070 ± 0.018 | 0.406 ± 0.083 | 0.239 ± 0.052 | 12 | 155 |
| psychometric, BALD | 10 | 0.092 ± 0.016 | 0.055 ± 0.005 | 0.062 ± 0.005 | 0.313 ± 0.043 | 0.209 ± 0.026 | 7 | 176 |

No method reaches 0.1 log10 units in 300 trials.

Under psy_gp.h 0.4.1, every GP-model run in replication 4 failed,
including the Sobol baseline, and EAVC failed in replication 5 too: the
fit ran the mean down until p was 0.5 everywhere (see
[Problems found](#problems-found)). Under 0.13.1 no replication fails that
way. Replication 4 now ends at MAE(p) 0.054 to 0.084. The worst GP
replication is LSE in replication 5: MAE(p) 0.124, with no crossing at 82
of the 118 contexts. The psychometric rows are identical in the two
versions.

The paper's figure for this problem shows EAVC and the other look-ahead
acquisitions ahead of LSE and the global acquisitions (BALD, BALV). The
paper scores classification error, runs 750 trials and uses 300
replications. The GP model here now shows the same order. EAVC has the
lowest field error of the adaptive GP acquisitions at 150 and 300 trials
(0.071 at 300, against 0.080 for LSE and 0.088 for BALV), and the lowest
threshold error (0.25). It ties with Sobol on MAE(p) at 300 trials, but it
is ahead of it at 150 (0.089 against 0.093). The psychometric model is
better than the GP model on field error: 0.055 to 0.068 against 0.071 to
0.088. It also leaves almost every context
with a crossing. BALD is its best acquisition on both metrics. The
threshold differences between all methods, 0.21 to 0.29 log10 units, are
the same size as the 0.22 by which the two truth surfaces disagree. So on
real data with a 5-D context, the threshold metric at 300 trials does not
separate the methods. The field error does.

Cost is the real limit in 6-D. The psychometric model with EAVC costs
about 3 s per trial at up to 300 trials with 1000 candidates, because its
look-ahead scores every pair of candidates. That is far past a frame and
needs the `Async` thread and a smaller candidate set. The other methods
cost 0.1 to 0.3 s per trial on the loaded machine, and the cost grows with
the trial count: a GP model replication at 500 trials under 0.4.1
averaged 0.6 to 1.0 s per trial.

## What changed between psy_gp.h 0.4.1 and 0.13.1

The tables above are from psy_gp.h 0.13.1, with the same seeds, response
streams and replication counts as the first run on 0.4.1. The new header
puts a prior on the latent mean, rejects a fit step that makes the
objective worse, keeps a floor under the lengthscales, and guards EAVC when
the level set is empty. It also has new options. This comparison leaves
them at their defaults, so the rows stay comparable.

The psychometric model gives the same numbers in both versions, within
0.01 in every cell, except in the two cells that collapsed under 0.4.1.
Its csf6 rows are identical. The GP model changed. The table lists the
cells that moved outside their intervals, and the cells that changed
because a collapsed replication no longer fails (0.4.1, then 0.13.1):

| Problem | Method | Metric | 0.4.1 | 0.13.1 |
|---|---|---|---|---|
| audiogram older-normal, beta 2 | psychometric, BALV | thr MAE, dB | 3.43 ± 4.55 (1.4 no-cross) | 1.10 ± 0.14 |
| audiogram older-normal, beta 2 | psychometric, BALV | trials to 2 dB | > 150 | 45 |
| audiogram older-normal, beta 0.5 | psychometric, BALD | MAE(p) | 0.036 ± 0.043 | 0.0043 ± 0.0010 |
| audiogram older-normal, beta 0.5 | psychometric, BALD | thr MAE, dB | 0.88 ± 0.62 (2.2 no-cross) | 0.55 ± 0.11 |
| audiogram metabolic, beta 0.5 | GP, LSE | thr MAE, dB | 4.09 ± 3.51 | 1.89 ± 0.26 |
| audiogram older-normal, beta 2 | GP, BALV | MAE(p) | 0.051 ± 0.001 | 0.046 ± 0.002 |
| audiogram older-normal, beta 2 | GP, BALV | band MAE | 0.233 ± 0.007 | 0.214 ± 0.006 |
| audiogram older-normal, beta 0.5 | GP, LSE | MAE(p) | 0.082 ± 0.004 | 0.070 ± 0.002 |
| audiogram older-normal, beta 0.5 | GP, BALV | MAE(p) | 0.058 ± 0.001 | 0.050 ± 0.000 |
| audiogram older-normal, beta 0.5 | GP, EAVC | thr MAE at 50, dB | 3.41 ± 0.86 | 2.15 ± 0.25 |
| audiogram sensory, beta 0.5 | GP, EAVC | MAE(p) | 0.086 ± 0.002 | 0.078 ± 0.002 |
| audiogram sensory, beta 2 | GP, BALV | thr MAE at 50, dB | 12.93 ± 0.85 | 14.51 ± 0.63 |
| novel detection | GP, LSE | band MAE | 0.167 ± 0.007 | 0.183 ± 0.007 |
| novel discrimination | GP, LSE | band MAE | 0.103 ± 0.019 | 0.066 ± 0.011 |
| novel discrimination | GP, EAVC | MAE(p) | 0.052 ± 0.003 | 0.044 ± 0.003 |
| novel discrimination | GP, EAVC | band MAE | 0.177 ± 0.020 | 0.074 ± 0.009 |
| novel discrimination | GP, EAVC | thr MAE | 0.09 ± 0.03 (21.95 no-cross) | 0.05 ± 0.01 (1.20 no-cross) |

On the 1-D problems the GP model's changes are inside the intervals
cell by cell, but they share one direction. Under 0.4.1 it had 1 to 6
runs of 50 with no threshold at 32, 64 and 128 trials on w1 to w3. Under
0.13.1 it has none, and its trials to 2 dB fell from 40 to 68 to 25 to 45.
On w4 the GP rows are unchanged within their intervals.

Most of the GP model's gains come from runs that no longer fail. Two
cells got worse outside their intervals: the band MAE of GP LSE on novel
detection, and the 50-trial threshold of GP BALV on the sensory audiogram
at beta 2. GP BALV on novel discrimination got worse at the edge of its
interval (threshold 0.09 to 0.12 units), and it no longer reaches the
criterion in 150 trials. On csf6 the GP rows moved inside their intervals,
except through replication 4 (see the csf6 section). The times moved in both
directions by up to a factor of three. The psychometric rows have
identical accuracy and different times, so those time changes are machine
load, not the header.

The first run also had a collapse that its tables did not show: on the
CSF, replication 15 of GP LSE, EAVC and BALV ended with no crossing in any
of the 30 columns. Under 0.13.1 every CSF replication crosses, and the GP
model's no-cross average there fell from 1.5 to 1.9 columns to 0.1 to 0.9.

## What the numbers say

- **A parametric model you trust: use QUEST+.** On the Watson problems the
  QUEST+ family reaches the criterion in 17 to 20 trials on w1 to w3, 2 to
  4 trials behind the psychometric model at most, with an sd about half a
  staircase's at 32 trials, for well under 1 ms per trial. On the CSF it
  has half the threshold error and a quarter of the field error of the next
  best method. Freeing the slope, or marginalizing the lapse, costs almost
  nothing in threshold precision.
- **Joint mode or marginal mean.** Watson's estimator, the joint mode on the
  grid, is biased where the grid is coarse: the slope comes out one step
  high on a 4-point grid. Report the marginal mean for a slope.
- **No parametric model, threshold wanted: use the psychometric model.** On
  the audiogram and the novel functions it beats the RBF GP on field error
  by 2 to 30 times, and with BALD it beats a staircase that knows the true
  slope on threshold in every audiogram cell. Its cost is 7 to 85 ms per
  trial on this loaded machine, so run it on the `Async` thread.
- **The RBF GP** is the method to use when the threshold is not the point
  and nothing is monotone. On these problems it is never the best. Under
  0.13.1 it no longer collapses. On w1 to w3 it reaches 2 dB in 25 to 45
  trials, against 17 to 20 for QUEST+.
- **Staircases** remain competitive late on a 1-D threshold: at 128 trials a
  1-up-3-down or weighted staircase is within 0.2 dB of QUEST+. They need
  about twice the trials to get there. On a 2-D field, interleaved
  staircases need 115 trials or more for 2 dB even with the slope given,
  and they fail where the threshold curve has a corner between their
  contexts.
- **Where the methods tie.** On a symmetric yes/no task (w4), a plain
  1-up-1-down finds the 50 percent point as well as QUEST+ does.
- **High-dimensional real data (csf6).** The psychometric model with BALD
  gives the best field in 300 trials. No method pins down a 5-D level set
  to 0.1 log10 units in 300 trials. Two fits of the truth itself differ by
  0.22 log10 units. Look-ahead acquisitions cost seconds per trial at 1000
  candidates in 6-D.

Caveats. The staircase scores in 2-D are a best case, because the scoring
model has the true slope. The truth in Watson's 1-D problems is at the
middle of the grid, which a uniform prior favors. The GP methods have 20
replications in 2-D and 50 in 1-D, against 100 for the others, so their
intervals are wider. The times come from a loaded laptop with two core
types; treat them as a factor-of-two guide.

## Problems found

The first run, on psy_gp.h 0.4.1, found four defects. The rerun on 0.13.1,
with the same seeds, shows all four fixed:

| Defect under 0.4.1 | Case | Under 0.13.1 |
|---|---|---|
| A scheduled fit of the psychometric model left a log marginal likelihood near -1e49 and a field that never recovered | audiogram older-normal, beta 2, BALV, replication 14 (collapse at trial 85); older-normal, beta 0.5, BALD, replications 6 and 18 | fixed: no replication collapses, and every column crosses; the two cells are 1.10 and 0.55 dB |
| A GP-model fit moved to short lengthscales (0.62 log2 kHz, 7.5 dB) and interpolated the trials | audiogram metabolic, beta 0.5, LSE, replication 4 (37.8 dB at 150 trials); also the CSF, replication 15, LSE, EAVC and BALV, with no crossing in any column | fixed: 1.89 dB for the cell, and every CSF replication crosses |
| GP model with EAVC drove the mean to its bound and predicted p near 1 everywhere | novel discrimination, 21.95 of 30 columns with no crossing on average | fixed: 1.20 no-cross columns, threshold 0.05 |
| GP model with `guess=0.5`: the fit ran the mean down until p was 0.5 everywhere | csf6, replication 4 (every GP method and the Sobol baseline), replication 5 (EAVC) | fixed: replication 4 ends at MAE(p) 0.054 to 0.084 |

The configurations of the first run, for the record: the audiogram cases
use `lo=[-3, -20]`, `hi=[4, 120]`, `intensity_dim=1`, `grid=[11, 21]`,
`refine_steps=2`, `n_init=5` (init points fed through `update`),
`fit=True`, `fit_every=20` and `target_p=0.75`. The csf6 case uses
`lo=[-1.5, -1.5, 0, 0.5, 1, 0]`, `hi=[0, 0, 20, 7, 10, 10]`,
`intensity_dim=0`, `n_candidates=1000`, `n_init=10`, `fit_every=20` and
`guess=0.5`. The response stream is `default_rng([20170310, problem id,
replication])`, with problem ids 201 to 208 for the audiogram cells in
table order, 221 and 222 for the novel functions, 231 for the CSF and 401
for csf6.

What remains:

- The GP model with LSE on csf6, replication 5, ends with MAE(p) 0.124 and
  no crossing at 82 of the 118 contexts. That is a poor fit, not a
  collapse: the field still varies. It is the worst GP replication on
  csf6.
- The psychometric model with LSE has two long single replications on the
  audiogram: 9.5 dB (metabolic+sensory, beta 2, replication 14) and 7.6 dB
  (older-normal, beta 0.5, replication 10). They are the same in both
  versions and keep a crossing in every column.
- `PSYGP_MAX_TRIALS` is 512 in the default binding build, so the psy.gp
  truth for csf6 uses only 512 of the 1001 trials. A build with
  `PSY_GP_MAX_TRIALS=1024` (see the psy_gp binding's README) lets the same
  fit use all of them. The run above did not use one.

## Reproduce

From the repository root:

```sh
uv venv venv-methods
source venv-methods/bin/activate        # Windows: venv-methods\Scripts\activate
uv pip install ./bindings/python/psy_stair ./bindings/python/psy_quest \
    ./bindings/python/psy_gp numpy scipy
python tests/compare/methods_compare.py --out results
python tests/compare/methods_compare.py --replay path/to/QuestPlus.nb
```

The csf6 problem runs only when named, because it takes about an hour:

```sh
python tests/compare/methods_compare.py --only csf6 --csf6-trials 300 --out results-csf6
```

It downloads the data into `--cache` (default `$PSY_COMPARE_CACHE` or
`~/.cache/psy-compare`). The AEPsych truth needs AEPsych in the
interpreter. Build it once with `--build-truth aepsych` in an environment
that has AEPsych, and later runs read it from the cache. `--truth psygp`
uses psy.gp's fit instead. When both truths are in the cache, the run
reports how much they differ.

The first run writes one raw CSV per problem and `tables.txt` to `results`. It took
26 minutes on 8 worker processes, plus the serial timing pass. The replay
needs Watson's QUEST+ Mathematica notebook, which this repository does not
include.

## References

- Letham, B., Guan, P., Tymms, C., Bakshy, E., & Shvartsman, M. (2022).
  Look-ahead acquisition functions for Bernoulli level set estimation.
  AISTATS. Code and data: github.com/facebookresearch/bernoulli_lse.
- Owen, L., et al. (2021). Adaptive nonparametric psychophysics.
  arXiv:2104.09549.
- Watson, A. B. (2017). QUEST+: a general multidimensional Bayesian adaptive
  psychometric method. Journal of Vision 17(3):10.
- Dubno, J. R., et al. (2013). Classifying human audiometric phenotypes of
  age-related hearing loss from animal models. JARO 14, 687-701.
