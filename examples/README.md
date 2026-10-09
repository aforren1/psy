# Examples

One program per file. CMake builds `examples/<lib>/<name>.c` as the
program `<lib>_<name>` (for example `examples/gfx/gallery.c` is
`gfx_gallery`). Six examples sit in the folder of the header they mainly
show but use another header's build; they keep their own file name as
their program name (`YSP_EXAMPLES_ACROSS` in `CMakeLists.txt`):
`rdk/gfx_rdk.c`, `rdk/gfx_rdk_bench.c`, `timeline/gfx_trial.c`,
`layout/gfx_layout.c`, `response/trial_keyboard.c` and
`device/photodiode_check.c`.

Run a program from the build folder (`build/<preset>/Release/` with MSVC,
`build/<preset>/` otherwise). "Needs" says what a run needs beyond the
build: SDL3 (a window), ANGLE (GL ES on Windows), a font, a device, or
nothing. `--sim` runs a windowed example on the simulated display, which
needs nothing; CI runs the commands marked "CI".

## aep

| Program | Shows | Headers | Run | Needs |
|---|---|---|---|---|
| `aep_sim` | Level-set and global acquisitions against a 2-D simulated observer | aep | `aep_sim` (CI) | nothing |
| `aep_bench` | The cost per trial against one display frame | rt, aep | `aep_bench` (CI) | nothing |
| `aep_audiometric` | The audiometric test function of Owen et al. 2021 | aep, stair | `aep_audiometric` (CI) | nothing |
| `aep_async` | The GP on the pump thread beside a 16 ms frame loop | aep | `aep_async` (CI) | nothing |
| `aep_optimize` | Finding the stimulus rated highest (optimization acquisitions) | aep | `aep_optimize` (CI) | nothing |
| `aep_pairwise` | Preferences from paired comparisons | aep | `aep_pairwise` (CI) | nothing |

## audio

| Program | Shows | Headers | Run | Needs |
|---|---|---|---|---|
| `audio_tone` | One quiet tone at a time and its onset record | audio | `audio_tone --null` (CI); `audio_tone` on the default device | miniaudio; a sound device without `--null` |
| `audio_clockstats` | The device clock and the callback on this machine; plays silence | rt, audio | `audio_clockstats --seconds 10` | miniaudio; a sound device |

## color

| Program | Shows | Headers | Run | Needs |
|---|---|---|---|---|
| `color_convert` | Conversions, gamut questions and gamut mapping on a calibration | color | `color_convert` (CI) | nothing |
| `color_bench` | The cost of the conversions | rt, color | `color_bench` | nothing |

## device

| Program | Shows | Headers | Run | Needs |
|---|---|---|---|---|
| `photodiode_check` | The MCU photodiode on the device layer: ports and keys, a console monitor, or edge minus flip onset in a window | device, screen | `photodiode_check --list`; `photodiode_check --key serial:16C0:0483:: --monitor 10`; `photodiode_check --key <key>` | SDL3; the board of `firmware/ysp_line/` (none for `--list`) |

## gfx

| Program | Shows | Headers | Run | Needs |
|---|---|---|---|---|
| `gfx_hello` | A drifting gabor: the manual's frame loop | gfx | `gfx_hello --sim --frames 30` (CI); `gfx_hello` | SDL3, ANGLE (Windows) |
| `gfx_gallery` | One labeled tile per feature, eight pages | gfx, timeline, outline | `gfx_gallery --sim` (CI); `gfx_gallery` | SDL3, ANGLE |
| `gfx_text` | Slug text from real fonts | gfx, timeline, outline | `gfx_text --sim` (CI); `gfx_text` | SDL3, ANGLE; system fonts (a bitmap font without them) |
| `gfx_load` | A frame loop under load, measured by the flip records | gfx | `gfx_load --sim --gabors 10 --dots 100 --seconds 1` (CI) | SDL3, ANGLE |
| `gfx_calib` | Photometer readings in, a calibration out | gfx | `gfx_calib` (CI: a synthetic display) | SDL3 to build; readings for a real display |
| `gfx_bench` | The cost per frame, offscreen | gfx, outline | `gfx_bench` | SDL3, ANGLE; a GL ES 3.0 device |

## layout

| Program | Shows | Headers | Run | Needs |
|---|---|---|---|---|
| `gfx_layout` | Paragraphs in seven scripts, bidi strings, BudouX breaking and an editor, through pack/layout | gfx, outline, pack/layout | `gfx_layout` | SDL3, ANGLE, `YSP_BUILD_LAYOUT=ON`, Windows fonts |

## outline

| Program | Shows | Headers | Run | Needs |
|---|---|---|---|---|
| `outline_font` | A label from a font file: exact alpha (a PGM) and a curve set | outline | `outline_font <font.ttf> "label" 48 out.pgm` | a font file (CI checks the usage exit) |
| `outline_bench` | The cost of the builds against their bars | rt, outline | `outline_bench 1 <font folder>` | fonts (CI checks the "no fonts" exit) |

## parallel

| Program | Shows | Headers | Run | Needs |
|---|---|---|---|---|
| `parallel_trigger` | A trigger byte on the parallel port | parallel | `parallel_trigger` | an LPT port; InpOut on Windows (CI checks the "no port" exit) |

## quest

| Program | Shows | Headers | Run | Needs |
|---|---|---|---|---|
| `quest_sim` | Psi and QUEST+ against a simulated observer | quest | `quest_sim` (CI) | nothing |
| `quest_qcsf` | The quick CSF as a custom model | quest | `quest_qcsf` (CI) | nothing |
| `quest_bench` | The cost of `yqst_next()` and `yqst_update()` | rt, quest | `quest_bench` (CI) | nothing |
| `quest_async` | QUEST+ on a thread beside a frame loop | quest | `quest_async 20 1` (CI) | nothing |

## rdk

| Program | Shows | Headers | Run | Needs |
|---|---|---|---|---|
| `rdk_bench` | The cost per frame, CPU only | rt, rdk | `rdk_bench --quick` (CI) | nothing |
| `gfx_rdk` | Random dot kinematograms drawn, four pages, replayed from logs | gfx, timeline, rdk | `gfx_rdk --sim` (CI); `gfx_rdk` | SDL3, ANGLE |
| `gfx_rdk_bench` | An RDK's cost per frame end to end, offscreen | gfx, rdk | `gfx_rdk_bench` | SDL3, ANGLE; a GL ES 3.0 device |

## response

| Program | Shows | Headers | Run | Needs |
|---|---|---|---|---|
| `trial_keyboard` | jsPsych's html-keyboard-response as a small experiment: RT from the flip onset | gfx, timeline, trials, response | `trial_keyboard --sim --out trials.csv` (CI); `trial_keyboard` | SDL3, ANGLE |

## rt

| Program | Shows | Headers | Run | Needs |
|---|---|---|---|---|
| `rt_jitter` | What the waits are worth on this machine | rt | `rt_jitter 40 1000` (CI) | nothing |
| `rt_pump` | A slow computation moved off the frame loop | rt | `rt_pump 40` (CI, also under emcc) | nothing |
| `rt_ring_csv` | A frame loop and an audio-like callback logged to CSV | rt | `rt_ring_csv 30 rt_ring.csv` (CI, also under emcc) | nothing |
| `rt_ring_bench` | The cost of the ring, the trace macros and the correlation | rt | `rt_ring_bench 100000 2 200` (CI) | nothing |

## screen

| Program | Shows | Headers | Run | Needs |
|---|---|---|---|---|
| `screen_hello` | The frame loop with a timeline trial | screen, timeline | `screen_hello --sim` (CI); `screen_hello` | SDL3 |
| `screen_flipstats` | The swap path measured on this machine | screen | `screen_flipstats --sim --frames 120` (CI); `screen_flipstats --frames 600` | SDL3; a display |
| `screen_input` | How good the input timestamps are; devices; raw mice | screen | `screen_input`; `screen_input --devices`; `screen_input --mice` | SDL3; Windows for the injected keys |
| `screen_abort` | The abort combination and the panic watchdog | screen | `screen_abort` | SDL3; Windows |
| `screen_gamma` | The OS gamma ramp, checked without changing what is shown | screen | `screen_gamma` | SDL3; Windows |

## serial

| Program | Shows | Headers | Run | Needs |
|---|---|---|---|---|
| `serial_trigger` | List ports, send a trigger byte, echo the answer | serial | `serial_trigger COM3 0x2A` | a serial port (CI checks the usage and "open fails" exits) |

## stair

| Program | Shows | Headers | Run | Needs |
|---|---|---|---|---|
| `stair_sim` | A 3-down-1-up staircase against a simulated observer | stair | `stair_sim` (CI) | nothing |

## timeline

| Program | Shows | Headers | Run | Needs |
|---|---|---|---|---|
| `timeline_trial` | One trial's timeline on a simulated 60 Hz display | timeline | `timeline_trial` (CI) | nothing |
| `timeline_tracking` | A sum-of-sines target, a flicker and a tween at 500 Hz | timeline | `timeline_tracking` (CI) | nothing |
| `timeline_bench` | The cost of `ytl_evaluate()` and the loads | rt, timeline | `timeline_bench 500` (CI) | nothing |
| `gfx_trial` | The timeline trial with gfx stimuli | gfx, timeline | `gfx_trial --sim` (CI); `gfx_trial` | SDL3, ANGLE |

## trials

| Program | Shows | Headers | Run | Needs |
|---|---|---|---|---|
| `trials_mocs` | Constant stimuli in a constrained order | trials | `trials_mocs` (CI) | nothing |
| `trials_interleave` | Three staircases and catch trials, interleaved | trials, stair | `trials_interleave` (CI) | nothing |
| `trials_bench` | CSV parse rate, block views, open and next() costs | rt, trials | `trials_bench` | nothing |

## video

| Program | Shows | Headers | Run | Needs |
|---|---|---|---|---|
| `video_play` | A movie in a window, and the frame-thread cost | video | `video_play --sim` (CI); `video_play --file movie.mpg` | SDL3, ANGLE; a frame sequence or MPEG-1 file for `--file` |
| `video_bench` | The decode thread's cost per frame | video | `video_bench --size 64x48 --reps 3` (CI); `video_bench --mpg clip.mpg` | SDL3 to build; a clip for MPEG-1 |
| `video_check` | The GPU paths against the upload path, frame by frame | video | `video_check --file clip.mp4` | SDL3, ANGLE; Windows (Media Foundation); the test clips |
