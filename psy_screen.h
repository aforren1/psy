/* psy_screen.h - v0.3.1 - public domain single-header display library
 *
 *   The window, the GL ES 3.0 context, the display mode and the swap path
 *   of a stimulus display, with flip at a time: each frame learns the
 *   predicted onset of the flip it draws for, presents for a target time
 *   on the psy_rt.h clock, and gets a record of the vblank it was shown on,
 *   the residual, the drops, the composition path and where the frame's
 *   time went. Also: display and mode lists, the mode whose refresh is a
 *   multiple of a video's rate, a photodiode patch, input timestamps on
 *   the psy_rt.h clock, and several displays.
 *
 *   REQUIRES psy_rt.h beside it (the clock, the waits, the event ring, the
 *   instrumentation macros), and SDL3 (3.2 or later) to link. On Windows it
 *   also needs ANGLE's libEGL.dll and libGLESv2.dll at run time; nothing
 *   ANGLE or Khronos is needed to compile. Copy psy_screen.h and psy_rt.h.
 *
 *   Written in the single-header style of the stb / sokol libraries. C99 is
 *   the floor: it builds as C99, C11 and C++17, and in the C dialect MSVC
 *   compiles by default.
 *
 *   ---------------------------------------------------------------------
 *   CHANGELOG
 *   ---------------------------------------------------------------------
 *   v0.3.1 - Aborts (ABORT): Shift+Esc, a close request, Alt+F4, SDL's
 *          quit event and psyscr_request_abort() each make one begin()
 *          return PSYSCR_QUIT with f.abort, f.abort_presses and f.abort_ns,
 *          and a PSYSCR_EV_ABORT ring record; the header sees them through
 *          an SDL event watch, also when the caller reads the events first.
 *          The panic watchdog (PANIC, desc.panic, off by default): Shift+Esc
 *          3 times in 2 s while the frame loop reports no abort puts back
 *          the gamma ramps and ends the process (exit code 99); Windows
 *          puts back a switched mode as it ends. The window icon
 *          (WINDOW ICON): Escher's impossible cube at 16, 32 and 48
 *          pixels; desc.icon_rgba, desc.icon_sdl.
 *          desc.d3d11_video (NATIVE HANDLES): a D3D11 device with video
 *          support and multithread protection; psyscr_native_info gains
 *          video and mt_protected. psyscr_begin_group() reports an abort on
 *          every member and begins none. close(), exit and the watchdog
 *          claim a gamma entry atomically, so one of them puts it back.
 *          Breaking: Esc alone no longer
 *          ends the loop; desc.no_esc_quit is gone (desc.abort_keys);
 *          psyscr_frame gains abort, abort_presses and abort_ns;
 *          psyscr_presenter_open gains d3d11_video (the presenter version
 *          stays 2).
 *   v0.3.0 - Flip hooks for triggers. Codes (CODES): exact device values in
 *          the frame, per flip or held, as rectangles or rows, drawn last,
 *          with presets for VPixx Pixel Mode and pixel sync, a self test at
 *          open, an optional read-back, record.code_risk and a CODE ring
 *          record. Triggers (TRIGGERS): callbacks on a deadline worker at
 *          a flip's planned vblank, armed when the present returns, moved
 *          when the header learns the flip will be late, a TRIGGER ring
 *          record, and an optional GPU fence check; the worker runs on the
 *          E-cores of a hybrid CPU on Windows and arms itself for the next
 *          vblank after a flip trigger. After flip (AFTER
 *          FLIP): psyscr_on_flip(); psyscr_frame.done lists every
 *          record completed since the last begin(). psyscr_native()
 *          (NATIVE HANDLES). Fixed: after a flip shown early when the
 *          depth fell, begin() planned the next frame for the same vblank
 *          (DEPTH). Before present (GL STATE):
 *          psyscr_on_present(), psyscr_gl_epoch(), psyscr_gl_generation().
 *          OS GAMMA: a fullscreen screen on Windows takes the OS gamma ramp
 *          to identity and gives it back at close and at exit. Breaking:
 *          psyscr_record gains code_risk; psyscr_frame gains done, n_done
 *          and done_lost; psyscr_present_req, the presenter
 *          and its open struct gain fields (PSYSCR_PRESENTER_VERSION 2).
 *   v0.2.0 - The Windows 11 composition swapchain (PSYSCR_BACKEND_COMPOSITION)
 *          with present at a target time. Tiers: each flip record carries
 *          psyscr_tier, caps.worst_tier and the describe line report the
 *          worst since open, and desc.min_tier flags flips below it. New
 *          flags SKIPPED, CANCELED, ONSET_PLANNED and BELOW_TIER. Breaking:
 *          psyscr_record.path and psyscr_vblank.path are uint8_t and a tier
 *          byte follows each; the ring's mode word is path in bits 0..2,
 *          flags in bits 3..12 and the tier in bits 13..15 (read it with
 *          PSYSCR_EV_PATH_OF, _FLAGS_OF and _TIER_OF). On Windows the raw
 *          keyboard path is on (SDL_HINT_WINDOWS_RAW_KEYBOARD, at default
 *          priority) and SDL text input is stopped. AUTO stays DXGI_FLIP.
 *          The depth on a backend that holds frames to their target is
 *          raised by misses and lowered only on slack evidence, per path,
 *          never by a try (DEPTH); begin() never predicts a vblank a flip
 *          in flight already has. Fixed: on DXGI_FLIP a flip_at() call
 *          within the margin before a vblank presented at once, and the
 *          frame showed a vblank before the one planned (EARLY set); it now
 *          waits for that vblank.
 *   v0.1.0 - first release: the DXGI flip swap path (Windows 10 and 11)
 *          through ANGLE, the simulated display, the presenter interface,
 *          prediction, the snap, the per-flip record, phases, the patch,
 *          groups, input restamping, mode lists and the parameter table.
 *          The other swap paths are stubs that refuse to open.
 *
 *   STATUS: v0.3.1. Two swap paths run, DXGI_FLIP and COMPOSITION, on one
 *   machine: a Windows 11 25H2 laptop whose Intel Iris Xe drives a 1920 x
 *   1200 panel at 60.0008 Hz (60 Hz is its only rate), with the ANGLE that
 *   ships in Docker Desktop's Electron front end (2.1.23876, git
 *   fffbc739779a; not a pinned build) and SDL3 3.4.0. Built with MSVC 19.4
 *   under /W4 /WX as C11 and C++17 and with MinGW-w64 gcc 16.1 under
 *   -Werror, and run from the MSVC build. No photodiode has been attached:
 *   tests/loopback/psy_screen_loopback.c is built and has never run, so the
 *   delay from a reported vblank to light is UNMEASURED, and
 *   desc.onset_offset_ns stays 0 until it is.
 *   DXGI_FLIP (v0.1.0, unchanged; docs/psy_screen.md has the tables):
 *   fullscreen on the overlay path, the onset was within 2.3 us of the
 *   prediction at p99 over 7200 frames, no frame dropped or late; in a
 *   plain window (composed) for 10 minutes, 5.7 us p99, no drop. In v0.2
 *   runs a window kept on top moved between the overlay and the composed
 *   path every 5 to 11 s; per minute that gave 27 to 67 drops, 9 to 15
 *   flips a vblank early (each flagged EARLY) and 13 to 96 flips with no
 *   statistic, and twice the waitable object freed no slot for 333 ms
 *   (begin() returned PSYSCR_ERR_TIMEOUT). The cause was not found.
 *   COMPOSITION (new; Windows 11, x64): in a window kept on top every flip
 *   but 2 to 9 per run was an independent flip, tier 1 (the rest were
 *   overlay frames, tier 3). For 10 minutes (36000 frames): 2.3 us
 *   p99 and 8.1 us max from the prediction, 12 drops, no flip without a
 *   statistic. Six 1-minute runs that alternated with DXGI_FLIP: 0 to 24
 *   drops for COMPOSITION against 27 to 67, 0 to 15 flips without a
 *   statistic against 13 to 96, no early flip against 9 to 15, 2.1 to 2.5
 *   us p99 against 4.5 to 5.4 us (the two DXGI_FLIP runs without a
 *   timeout). These window runs, and the 10-minute run, came before the
 *   depth rule that lowers only on evidence and before the fix for a flip
 *   called inside the margin before a vblank, which showed one vblank
 *   early on DXGI_FLIP. That fault may explain some of DXGI_FLIP's early
 *   flips above. None of them was run again.
 *   Fullscreen on AC, 1 minute each on the quiet machine: v0.2
 *   DXGI_FLIP 2.33 us p99, COMPOSITION 2.36 us, a v0.1 control 2.32 us, no
 *   drop. (While another program ran load threads on the machine, both
 *   backends gave 75 to 110 us p99 and 21 to 29 drops in fullscreen.) From
 *   the present call's return to the reported onset (scanout start, not
 *   light): 15.96 ms p50 on DXGI_FLIP overlay, 15.63 ms on COMPOSITION
 *   independent flip, every frame on the next vblank; a composed DXGI_FLIP
 *   frame took a vblank more (32.6 ms). Holds 0 to 4 vblanks ahead: 1800
 *   of 1800 fullscreen and 2979 of 2998 in a window on the vblank asked for,
 *   19 late, 0 early. Overruns of 17 and 20 ms: 84 of 84 flagged
 *   LATE_TARGET with the draw phase charged, no miss without a flag. The
 *   header's CPU time per frame in the 10-minute window run: begin() 21.8
 *   us mean (SDL_PumpEvents() 11.0 us, statistics 7.9 us), flip_at() 1.8
 *   us; that misses the 20 us bar, on the OS calls. The present call costs
 *   more than on DXGI_FLIP: 530 to 1230 us mean against 380 to 730 us
 *   in the alternating runs. No C runtime heap call per frame in a debug
 *   build (2 once per run, made by the runtime for an OS component's C++
 *   exception on the frame thread). With one frame in flight, the next
 *   frame waits for the last one's statistic, and the loop used about 99%
 *   of vblanks
 *   in a window (16.85 ms mean frame time).
 *   AUTO stays DXGI_FLIP: COMPOSITION did better in a window, but only as
 *   well in fullscreen on the quiet machine, its present call costs more,
 *   it needs Windows 11, and its composed onset is tier 3 where
 *   DXGI_FLIP's is tier 2.
 *   What the onset is (measured against the scanout's own vblank entry,
 *   polled with D3DKMTGetScanLine): an independent flip's DisplayedTime
 *   was 13 us after it at p50 and 77 us at p99, came 0.3 ms later, and
 *   moved with a late frame: an observation, tier 1. A composed frame's
 *   time from DComp's target statistics also sat on the vblank (26 us p50)
 *   and moved with a late frame, but came about 15 ms BEFORE that vblank:
 *   it is DWM's plan, so it carries PSYSCR_FLIP_ONSET_PLANNED and tier 3.
 *   A second window loading the GPU did not make DWM miss a frame (its own
 *   counters stayed 0), so whether a DWM miss shows in the record is
 *   UNMEASURED: on COMPOSITION, a composed onset is DWM's claim. Present
 *   targets and displayed times are QPC time in 100 ns units here (QPC
 *   runs at 10 MHz); interrupt time is 20.03 ms away and was wrong. On a
 *   machine whose QPC runs at another rate this is unverified.
 *   Tiers, from these runs: DXGI_FLIP overlay and COMPOSITION independent
 *   flip are tier 1; DXGI_FLIP composed is tier 2 (DWM's vblank, measured
 *   against nothing else); a flip off the grid (the panel stretched frames
 *   after a late one, up to 4.9 ms) is tier 2; COMPOSITION composed or
 *   overlay frames and every ESTIMATED flip are tier 3.
 *   Input: SDL's tick clock correlates to the psy_rt clock within 0.1 us.
 *   Keys sent with SendInput(): on the raw path, stamped 0.18 to 0.67 ms
 *   after the call (mean 0.39 ms, 199 keys); through the message loop,
 *   from 6.2 ms before to 12.1 ms after it.
 *   The core (prediction, snap, drops, depth, estimated, skipped and
 *   canceled records, tiers, the ring record, phases, groups, errors, the
 *   mode picker, preemption, codes, triggers and their moves, the
 *   after-flip and present callbacks, aborts, the panic rule, the icon
 *   data) is checked without SDL or a display by
 *   tests/adapt/psy_screen_test.c against a scripted swap path on a
 *   virtual clock, so the host's load cannot change a result, on MSVC,
 *   MinGW gcc 16.1, gcc 11.4 (WSL2; also as C99 at -O3, as C++17, and
 *   under ASan and UBSan, and 8 of 8 runs with a busy loop on its CPU) and
 *   clang (emcc, run in node); 41 deliberate mutations of the header
 *   each make it fail (v0.3.1's 12 checked with MinGW only).
 *   CODES and TRIGGERS (v0.3.0, measured on battery, Balanced plan; the
 *   tables are in docs/psy_screen.md): the open-time self test passed on
 *   DXGI_FLIP and COMPOSITION, and 1800 code read-backs in fullscreen
 *   differed 0 times. Drawing a 1-pixel and an 8-pixel code cost 3.8 us
 *   mean on the GPU context with one ClearView per pixel, 5.3 us with
 *   UpdateSubresource, so rows of up to 8 pixels use ClearView. This
 *   laptop runs auto color management (color=wcg in the describe line),
 *   so every code here is at risk (CODE_RISK_ADVANCED_COLOR); no device
 *   has read one. A trigger fired p50 1.0 to 2.1 us, p99 59 to 300 us and
 *   at most 0.6 to 7.9 ms after its deadline (TIME_CRITICAL worker; the
 *   larger numbers in a window), on the P-cores where psy_rt.h puts the
 *   worker. The cause, measured: the display's vblank DPC, 50 to 400 us
 *   from about 75 us before the vblank, ran on the P-core the worker spun
 *   on; the trigger lock and the spin window were not the cause. The
 *   worker now runs on the E-cores of a hybrid CPU: this machine's way
 *   to keep it off the vblank DPC's CPU, not a rule (TRIGGERS). On AC, 1
 *   minute each: fullscreen p99 1.6 to 2.6 us on both backends (31 and 29
 *   us before); in a window 1.0 to 3.2 us (32 to 169 us before). The
 *   tail is the OS's and stays: 1 to 15 triggers a minute fired over 200
 *   us late (1 to 22 before), the largest 0.35 to 43 ms (0.37 to 12.6 ms
 *   before), so the 200 us bar for the largest is missed. Without a
 *   display, examples/rt_jitter.c on the same cores woke up to 2.6 and
 *   32.6 ms late with the same 1.2 ms spin window (on the P-cores, 3.0
 *   ms and 20 us). With 3 frames of 25 ms GPU work every
 *   300 frames: without the GPU check 72 of 72 triggers fired a frame
 *   early; with desc.trigger_fence 140 of 142 moved in time and 0 moved
 *   in error, and no move in 10800 idle frames. The fence stays off by
 *   default until it runs on more than one GPU. Arming a trigger cost the
 *   frame thread about 8 us on battery and 3.8 us on AC in flip_at(), most
 *   of it the worker's wake-up; the worker now arms itself for the next
 *   vblank after each flip trigger, so a trigger every frame arms with no
 *   wake-up: 0.6 to 0.9 us mean on AC, flip_at() 1.7 to 2.6 us in all.
 *   The OS gamma ramp: this laptop's is the identity, so
 *   open() set nothing; setting the same ramp again and reading it back
 *   was exact; one GetDeviceGammaRamp costs 0.8 ms, so it is read at open
 *   only. Whether the ramp acts on independent-flip and overlay frames,
 *   and under Night Light, was not measured.
 *   ABORT, PANIC, ICON and d3d11_video (v0.3.1, AC, keys sent with
 *   SendInput; docs/psy_screen.md has the tables): Shift+Esc gave one
 *   abort per press, stamped 0.12 to 0.70 ms after the call, also when the
 *   loop read its events before begin(); Esc alone gave none; a held
 *   Shift+Esc gave one. SDL 3.4 reports each key twice on the raw path
 *   (INPUT); the header counts one. A hung program ended 28 to 85 ms after
 *   the third press, after its panic_fn ran and a gamma entry was put
 *   back; after a 7 s hang, with Windows' ghost window in front, too; a
 *   held Shift+Esc did not panic; a responsive loop got 5 aborts for 5
 *   presses and no panic. Fullscreen at 1680 x 1050 on the 1920 x 1200
 *   desktop, hung: the desktop mode was back 1.2 s after press 3, as the
 *   process ended. With the watchdog's hook armed, keys on the raw path
 *   were stamped no later (p50 0.30 to
 *   0.31 ms against 0.35 to 0.46 ms, 3 interleaved runs of 199 keys). The
 *   header's CPU time per frame, begin() without its wait, 3 interleaved
 *   rounds of 1800 frames in a window on top: 22.5 to 25.2 us mean with
 *   d3d11_video off, 19.6 to 26.3 us with it on, 20.0 to 29.5 us for the
 *   v0.3.0 header; flip_at() 1.5 to 2.2 us in all. No difference shows
 *   beyond the spread between rounds. Alt+F4 was not sent (if the focus
 *   moved, it would close another program); the core test checks it
 *   through the feed. The icons reach the window at 16 and 32 pixels as
 *   drawn (read back, 96 dpi).
 *   tests/compile/psy_screen_com.cpp checks every COM slot and struct the
 *   header declares against the Windows SDK (MSVC; MinGW for DComp only).
 *   NOT done: any rate but 60 Hz, any other GPU, Windows 10 (COMPOSITION
 *   needs 11), ARM64 (COMPOSITION refuses it: the hidden-pointer returns
 *   are checked only on x64), two physical displays, variable refresh
 *   (refused), macOS, X11 and Wayland swap paths (stubs). Linux runs the
 *   core and the simulated display only.
 *   Outside this STATUS block and docs/psy_screen.md, a number in this
 *   header is a measurement only where the text says "measured".
 *
 *   ---------------------------------------------------------------------
 *   USAGE
 *   ---------------------------------------------------------------------
 *   Do this:
 *
 *       #define PSY_SCREEN_IMPLEMENTATION
 *
 *   in *one* C or C++ file before including this header to create the
 *   implementation. Every other file just includes the header normally.
 *   psy_rt.h's implementation comes with it, once, as with the other psy
 *   headers. Link SDL3.
 *
 *   A trial of psy_timeline.h on the display (examples/screen_hello.c runs
 *   it):
 *
 *       #define PSY_SCREEN_IMPLEMENTATION
 *       #include "psy_screen.h"
 *
 *       static psyscr_screen scr;                 // zeroed
 *       if (!psyscr_open(&scr, &(psyscr_desc){ .ring = &ring }))
 *           die(psyscr_error(&scr));              // fullscreen, primary display
 *       psyscr_frame f;
 *       while (psyscr_begin(&scr, &f) == PSYSCR_OK) {   // Shift+Esc ends it
 *           if (f.index == 0) psytl_anchor(&tl, TRIAL, f.onset, 0);
 *           int n = psytl_evaluate(&tl, &(psytl_frame){ f.onset, f.period, f.index },
 *                                  fired, 8);
 *           draw(psytl_values(&tl));
 *           psyscr_flip(&scr);                    // at f.onset
 *       }
 *       psyscr_close(&scr);
 *
 *   The timeline evaluates at f.onset, the predicted onset of the flip the
 *   frame draws for, so what a frame shows is a function of when it
 *   appears. The measured record of each flip arrives one frame later, in
 *   f.last and in the ring. Compound literals and designated initializers
 *   are C99; in C++17 zero a psyscr_desc and set its fields.
 *
 *   Psychtoolbox and PsychoPy, for comparison:
 *
 *       vbl = Screen('Flip', win);                % PTB
 *       for i = 1:n
 *           Screen('FillRect', win, contrast(i) * 255, rect);
 *           [vbl, onset, ~, missed] = Screen('Flip', win, vbl + 0.5 * ifi);
 *       end
 *
 *       for frame in range(n):                    # PsychoPy
 *           grating.contrast = contrast[frame]
 *           grating.draw()
 *           win.flip()
 *
 *   Both draw frame i for "the next flip" and leave the caller to work out
 *   when that is. PTB's Flip blocks until the vblank and returns its time;
 *   psyscr_wait_flip() does the same here, at the cost of one frame of
 *   pipelining.
 *
 *   Present at a later time, and mark the phases of a frame:
 *
 *       psyscr_begin(&scr, &f);
 *       psytl_evaluate(...);
 *       psyscr_mark(&scr, PSYSCR_PHASE_EVALUATE);
 *       upload_textures();
 *       psyscr_mark(&scr, PSYSCR_PHASE_UPLOAD);
 *       draw(...);
 *       psyscr_flip_at(&scr, f.onset + 3 * f.period, NULL);   // 3 frames later
 *
 *   ---------------------------------------------------------------------
 *   MODEL
 *   ---------------------------------------------------------------------
 *   TIME
 *     Every time is an int64_t count of nanoseconds on the psy_rt.h clock
 *     (psyrt_now_ns()), the unit psy_timeline.h takes. A backend that
 *     stamps on another clock converts at the boundary: DXGI's vblank times
 *     are QueryPerformanceCounter values, the counter psyrt_now_ns() reads,
 *     and psyrt_ticks_to_ns() converts them with no correlation.
 *
 *   THE GRID
 *     A fixed-refresh display flips on a grid of vblanks. The header keeps
 *     the newest vblank the OS reported (time and count) and the period:
 *     the mode's rational period at first, then the period fitted from the
 *     OS's vblank times over at least 64 vblanks. The describe line shows
 *     the fitted refresh and its difference from the mode's (this laptop's
 *     panel: 60.0008 Hz). A vblank more than 1% of a period off the grid
 *     sets PSYSCR_FLIP_GRID_UNSTABLE on its record; one more than a quarter
 *     period off restarts the fit.
 *
 *   DEPTH
 *     The depth is the number of vblanks from a present call to the flip
 *     that shows it: 1 on an overlay or independent-flip path, 2 when the
 *     compositor copies the frame (measured, docs/psy_screen.md). The
 *     header learns it from the flips: three flips in a row at a new depth
 *     change it, so one late flip is a drop and not a new depth, and one
 *     flip is enough after the path changes. A flip shown on the vblank
 *     right after the flip before it may have waited behind that flip, so
 *     its count says "this depth or less"; up to 3 such flips do not use up
 *     a path change's one-flip window. open() presents black frames
 *     until three flips agree, because a window that has just gone
 *     fullscreen is composed for a few frames.
 *     When the depth falls (composed to overlay), the flip planned at the
 *     old depth shows a vblank early (EARLY). The next frame is planned a
 *     vblank after the early one's plan, never on it, so the early frame
 *     stays on the screen two vblanks; f.onset and f.vblank always rise
 *     from one frame to the next. When the depth rises, a flip planned at
 *     the old depth is shown late (a drop), and the next frame's f.vblank
 *     is 2 on: begin() runs before that drop's record completes.
 *     A backend that shows a frame at its target (COMPOSITION) holds an
 *     early present back, so an on-time flip cannot show that a smaller
 *     depth would do, and a try at one less drops a frame when it fails.
 *     There the header never tries. Three misses in a row raise the depth
 *     by one. It lowers the depth only on evidence: per path, it counts
 *     the slack of each flip (the planned vblank's time minus the return
 *     of the present call), on time and missed. On 4 flips in a row, the
 *     slack one depth less would have had must have been seen on time at
 *     least 8 times as often as a miss with that slack or more. A miss
 *     within 4 flips of a lowering clears that path's on-time counts, so
 *     the same evidence cannot cost a second frame; this can happen only
 *     when the system's deadline changed after the evidence was taken.
 *     Counts halve every 4096 flips. Each path keeps its last depth, so a
 *     window that moves between composed and independent flip does not
 *     learn again. open() does not learn the depth on these backends; it
 *     starts at 1.
 *
 *   PREDICTION
 *     psyscr_begin() predicts the first vblank a present made now can
 *     reach: the vblank at or before now plus a margin (an eighth of a
 *     period, at most 1 ms), plus the depth, and never one at or before
 *     the last flip. f.onset is that vblank's time plus desc.onset_offset_ns.
 *
 *   FLIP AT A TIME
 *     psyscr_flip_at(t) snaps t to a vblank with psy_timeline.h's rule: the
 *     first vblank at or after t - lead x period (lead x period truncated to
 *     whole ns). The default lead is 0.5, the nearest vblank, because a
 *     predicted onset carries noise: with "at or after", a t one
 *     microsecond past a vblank goes a whole frame late. A flip and a
 *     timeline event at the same time land on the same frame.
 *     PSYSCR_LEAD_NONE is never early: the first vblank at or after t.
 *     When the snapped vblank is before the first one the present can
 *     still make, the flip goes to that one and its record has
 *     PSYSCR_FLIP_LATE_TARGET: the caller was late. When it is later, the
 *     header sleeps on the psy_rt clock until just after vblank
 *     (planned - depth), and presents then; DXGI shows a frame at the first
 *     vblank it can, so this is what puts it on the planned one. The same
 *     wait applies when the call comes within the margin before a vblank,
 *     which the plan already put the flip after. (DXGI's
 *     own SyncInterval was measured as the other way to hold a frame and
 *     put 795 of 1800 held frames early; see docs/psy_screen.md.)
 *
 *   THE RECORD (psyscr_record)
 *     index     the frame number
 *     target    t as given to flip_at
 *     planned   the vblank time the flip was planned for, + offset
 *     onset     the time of the vblank it was shown on, + offset: the OS's
 *               vblank timestamp on DXGI, not a photon. The photodiode
 *               test (tests/loopback/) measures the difference, which goes
 *               in desc.onset_offset_ns.
 *     residual  onset - target
 *     dropped   vblanks between planned and shown: the system's lateness,
 *               not the caller's (that is LATE_TARGET)
 *     path      PSYSCR_PATH_COMPOSED, _OVERLAY or _INDEPENDENT on Windows
 *     tier      how far the onset can be trusted (TIERS)
 *     flags     PENDING (not shown yet), ESTIMATED (no OS time for this
 *               present; onset is the planned vblank), LATE_TARGET,
 *               OCCLUDED (no statistic came: the window was covered),
 *               GRID_UNSTABLE (the vblank was off the grid), EARLY (shown
 *               before the planned vblank), SKIPPED and CANCELED (never
 *               shown: a later present took the vblank; onset 0, residual
 *               0), ONSET_PLANNED (the onset is the vblank the system
 *               planned the frame for, not one it observed), BELOW_TIER
 *               (tier worse than desc.min_tier)
 *     phase_ns  where the frame's time went, below
 *     A record completes when the OS reports its flip, normally in the next
 *     begin(). DXGI reports only the newest flip, so the header reads the
 *     statistics again before a present and once per vblank while it holds
 *     a frame; a flip that is never reported still gets a record, flagged
 *     ESTIMATED. psyscr_wait_flip() waits for all of them.
 *     f.last is the newest completed record. f.done and f.n_done list
 *     every record completed since the begin() before, in completion
 *     order (the ring's), whether it completed in begin(), in flip_at()
 *     while a frame was held, or in wait_flip(); the last one is f.last.
 *     The list is valid until the next begin(), and holds at most
 *     PSYSCR_MAX_DONE (16; f.done_lost counts any that did not fit). A
 *     record completed in close() reaches the ring and on_flip only.
 *
 *   PHASES
 *     phase_ns[PSYSCR_PHASE_EVALUATE, _SCRIPT, _DRAW, _UPLOAD] is the time
 *     charged by psyscr_mark(); the time not marked before flip_at() goes
 *     to _DRAW. _SWAP is begin()'s wait for a slot plus the present call,
 *     not flip_at()'s planned wait for a later vblank. _GPU is
 *     PSYSCR_PHASE_UNKNOWN in v0.1: a GL_EXT_disjoint_timer_query per frame
 *     was built and measured, and it tracked the GPU work but cost about
 *     250 us of CPU per frame through ANGLE, so it was taken out
 *     (docs/psy_screen.md). A frame that was late shows which phase took
 *     the time, with no tool attached.
 *
 *   THE RING
 *     With desc.ring set, each completed record is one psyrt_event:
 *       source  PSYRT_SRC_SCREEN        kind  PSYSCR_EV_FLIP
 *       t_ns    onset                   aux   desc.display_index
 *       u.i64[0]  target
 *       u.u16[4]  dropped, at most 65535
 *       u.u16[5]  path in bits 0..2, flags in bits 3..12, tier in bits
 *                 13..15: PSYSCR_EV_PATH_OF(), _FLAGS_OF(), _TIER_OF()
 *       u.u32[3..8]  phase_ns[0..5]
 *       u.u32[9]  the frame index, low 32 bits (psytl_event.frame)
 *     Residual is t_ns - u.i64[0]. A flip never shown carries its planned
 *     vblank in t_ns, because the ring reads 0 as "now". Also
 *     PSYSCR_EV_PATH when the path
 *     changes (u.u16[0] old, u.u16[1] new) and PSYSCR_EV_MODE at open
 *     (u.i32[0..4] w, h, refresh_num, refresh_den, backend). Records arrive
 *     in completion order, which is not always frame order.
 *
 *   ONE FRAME IN FLIGHT
 *     begin() waits until the swap path takes a frame, so the frame drawn
 *     is never more than one present behind the screen. DXGI: a frame
 *     latency waitable object with SetMaximumFrameLatency(1) and 2 buffers.
 *     COMPOSITION: the previous present has a statistic that settles it
 *     (shown, skipped or canceled) and one of 3 buffers is free. Present at
 *     a time could queue several future frames; that is not done, so each
 *     frame is drawn against a fresh prediction and fresh input.
 *
 *   GL STATE (what a renderer on this header, such as psy_gfx.h, relies on)
 *     begin() makes the context current on the calling thread with the back
 *     buffer as framebuffer 0. caps.mode.w and caps.mode.h are the back
 *     buffer's size in pixels, in a window too. On DXGI_FLIP and
 *     COMPOSITION, framebuffer 0 is ANGLE's pbuffer on a D3D11 texture, and
 *     its row 0 is the top row of the screen, not the bottom as in GL
 *     (measured, docs/psy_gfx.md); a renderer that writes rows in screen
 *     order must flip them. flip_at() on DXGI_FLIP and COMPOSITION changes
 *     no GL state (the patch is a ClearView after ANGLE's flush); on a
 *     presenter without draws_patch, the GL patch puts back what it changes
 *     (PHOTODIODE PATCH). Nothing else here touches GL state, so a renderer
 *     sets what it needs once per frame, except the present callback:
 *     psyscr_on_present(fn) calls fn inside flip_at(), after your drawing
 *     and before the header's flush and present, with the context current
 *     and framebuffer 0 bound. fn may draw with GL and change any state;
 *     the header puts nothing back for it. Two counters tell a renderer
 *     that caches GL state what happened behind its back:
 *       psyscr_gl_epoch()       changes after every present callback: GL
 *                               state may differ from your cache. Drop the
 *                               cache and set state again.
 *       psyscr_gl_generation()  changes when the context is new: every GL
 *                               object made before is gone (0 while
 *                               closed). Today only open() makes a context,
 *                               and a lost device ends the screen with
 *                               PSYSCR_ERR_LOST, so it is constant from
 *                               open to close.
 *     PSYSCR_HAS_GL_EPOCH is defined when both exist.
 *
 *   TIERS (psyscr_tier, record.tier, caps.worst_tier, desc.min_tier)
 *     Each flip gets a tier from its backend, its path and the source of its
 *     onset, as measured on the one machine of STATUS:
 *       1  the OS observed the flip: DXGI_FLIP on the overlay or
 *          independent-flip path, COMPOSITION on independent flip
 *          (IndependentFlipFrame's displayed time sat within 0.1 ms of the
 *          scanout's own vblank, and moved with a late frame)
 *       2  good under conditions: DXGI_FLIP composed (DWM's vblank, one
 *          display), and any flip off the vblank grid (GRID_UNSTABLE)
 *       3  for task development only: an ESTIMATED or ONSET_PLANNED onset,
 *          such as every composed or overlay frame of COMPOSITION, whose
 *          time is DWM's plan for a vblank about 15 ms ahead
 *       PSYSCR_TIER_SIM  the simulated display
 *     A custom presenter may set the tier in each psyscr_vblank; 0 takes the
 *     path's default. caps.worst_tier and the describe line give the worst
 *     tier since open. desc.min_tier (1 to 3) sets BELOW_TIER on each flip
 *     worse than it; the run goes on, and the caller decides what to do
 *     with those trials.
 *
 *   ---------------------------------------------------------------------
 *   BACKENDS
 *   ---------------------------------------------------------------------
 *   PSYSCR_BACKEND_DXGI_FLIP (Windows 10 and 11; desc.backend 0 picks it)
 *     SDL3 makes the window. The header makes a D3D11 device on the adapter
 *     whose output shows the window (on a laptop with two GPUs, the one
 *     wired to the panel; any other means a copy between adapters and a
 *     composed path), a DXGI_SWAP_EFFECT_FLIP_DISCARD swapchain on the
 *     window, and an ANGLE display on that device
 *     (EGL_ANGLE_device_creation_d3d11). ANGLE renders into swapchain
 *     buffer 0 through a pbuffer (EGL_ANGLE_d3d_texture_client_buffer); in
 *     D3D11's flip model buffer 0 always names the current back buffer.
 *     The header does not use SDL's GL path on Windows: through it ANGLE
 *     makes a blt-model swapchain, which gives no vblank statistics, no
 *     frame latency control and no independent flip (docs/psy_screen.md).
 *     libEGL.dll is loaded at run time from desc.angle_dir, else from the
 *     directory in the PSYSCR_ANGLE_DIR environment variable, else from the
 *     program's directory and the DLL search path; with a directory, the
 *     libGLESv2.dll beside it is the one used. The context is GL ES 3.0
 *     with an RGBA8 back buffer and no depth buffer. DXGI discards the back
 *     buffer at each present, so draw every pixel of every frame.
 *     A fullscreen screen is a borderless window over the display in its
 *     current mode, which DXGI promotes to an overlay plane or to
 *     independent flip; the record's path says which. A window is composed.
 *   PSYSCR_BACKEND_COMPOSITION (Windows 11, x64; open fails elsewhere)
 *     The same device, adapter choice and ANGLE context as DXGI_FLIP, but
 *     no swapchain: a presentation manager (IPresentationManager) shows our
 *     own 3 textures, each a client-buffer pbuffer for ANGLE, through a
 *     DirectComposition visual on the window. Each present carries a target
 *     time, so flip_at() never waits: measured, the system shows the frame
 *     on the vblank nearest the target, ties to the earlier one, which is
 *     psy_timeline.h's rule, and the header passes the planned vblank's
 *     time itself. On independent flip the shortest lead that met its
 *     vblank was the next vblank; on the composed path, the one after. The
 *     target is QPC time in 100 ns units, not interrupt time, which is 20
 *     ms away from it on this machine (STATUS). Statistics: an independent
 *     flip reports its displayed time (tier 1); a composed or overlay frame
 *     reports a CompositionFrame, whose time the header reads from
 *     DComp's target statistics and flags ONSET_PLANNED (tier 3). A window
 *     that another window covers gets no statistic at all: such a flip
 *     completes after 3 periods as ESTIMATED and OCCLUDED. Without
 *     IPresentationSurface::SetSourceRect nothing showed; the header sets
 *     it. The header declares the COM interfaces itself, because dcomp.h
 *     is C++ only, MinGW-w64 has no presentation.h, and presentation.h's C
 *     declarations return SystemInterruptTime and LUID by value where the
 *     C++ methods return them through a hidden pointer;
 *     tests/compile/psy_screen_com.cpp checks every slot against the SDK.
 *     AUTO does not pick it (STATUS gives the reasons).
 *   PSYSCR_BACKEND_SIM
 *     No window, no GL: a vblank grid on the psy_rt clock with
 *     desc.sim_period_ns (default 1/60 s), which shows every frame on the
 *     vblank it was planned for. For tests, CI and dry runs of a timeline.
 *   PSYSCR_BACKEND_CUSTOM
 *     desc.presenter and desc.presenter_ctx: your swap path (PRESENTER).
 *   Not implemented in v0.3; open() refuses them with a message:
 *     PSYSCR_BACKEND_GLX_OML (Linux X11),
 *     PSYSCR_BACKEND_WAYLAND (presentation-time), PSYSCR_BACKEND_METAL
 *     (macOS through ANGLE), PSYSCR_BACKEND_WEB (requestAnimationFrame).
 *     On Linux and macOS only SIM and CUSTOM open.
 *
 *   ---------------------------------------------------------------------
 *   PRESENTER (the swap-path interface)
 *   ---------------------------------------------------------------------
 *   A psyscr_presenter is a table of functions, the Presenter extension
 *   of the rig: open, close, acquire (wait for a slot, bind the context),
 *   present (queue the frame for a vblank count and time), completions
 *   (the flips since the last call, oldest first, each with its present id,
 *   vblank time on the psy_rt clock, vblank count, path, tier and flags;
 *   a time of 0 means "none", and SKIPPED or CANCELED means "never
 *   shown"), gl_proc, bind
 *   and describe. The core owns the window, the prediction, the snap, the
 *   decision when to present, the patch (unless the presenter sets
 *   draws_patch and paints the rectangle in present), drops, records and
 *   the ring. The presenter owns its OS objects and converts its times to
 *   the psy_rt clock. Version 2 adds: psyscr_present_req.codes, n_codes and
 *   verify (drawn after the patch by a presenter with draws_patch); in the
 *   open struct n_codes, want_gpu_done and three counters a presenter that
 *   reads codes back writes; and gpu_done(), the highest present id whose
 *   GPU work has finished, called from the trigger worker thread. The core calls a presenter from the thread that calls
 *   begin and flip, never from two threads at once; acquire may block,
 *   present and completions must not block past their OS call. A presenter
 *   with native_target in its caps shows a frame on req->target_count
 *   itself; otherwise the core waits before present. The header's timing
 *   statements end at this boundary for a presenter that is not its own.
 *   tests/adapt/psy_screen_test.c has a complete one.
 *
 *   ---------------------------------------------------------------------
 *   PHOTODIODE PATCH
 *   ---------------------------------------------------------------------
 *   desc.patch.on draws a square (desc.patch.size pixels, default 32) in
 *   desc.patch.corner, at the gray level psyscr_set_patch() last set, on
 *   every frame, last, so it is on top. It is a scissored clear, never a
 *   draw call. On DXGI_FLIP and COMPOSITION it is
 *   ID3D11DeviceContext1::ClearView with one rectangle on the back buffer,
 *   after ANGLE's flush, and flip_at()
 *   changes no GL state at all. On a presenter without draws_patch the core
 *   clears through GL and changes, then restores, the draw framebuffer
 *   binding, GL_SCISSOR_TEST, the scissor box, the clear color, the color
 *   write mask and GL_RASTERIZER_DISCARD; nothing else. The patch is for
 *   the loopback test: toggle it with the stimulus, and a photodiode on
 *   that corner measures when the frame appeared.
 *
 *   ---------------------------------------------------------------------
 *   CODES (desc.codes, psyscr_code, psyscr_code_frames, psyscr_code_row)
 *   ---------------------------------------------------------------------
 *   A code is a pixel value a device reads from the video signal, so the
 *   display itself times the trigger. desc.codes declares up to
 *   PSYSCR_MAX_CODES slots at open: a SOLID rectangle of one value, or a
 *   ROW of w pixels each with its own value (PSYSCR_CODE_ROW_PIXELS in all
 *   ROW slots together). Values are 0xBBGGRR, red in bits 0 to 7, exact
 *   device values (8 bits per channel). Each slot shows its rest value
 *   until you set it: psyscr_code() for the next flip only,
 *   psyscr_code_frames() for n flips or PSYSCR_CODE_HOLD, and
 *   psyscr_code_row() for a ROW. Codes are drawn last, after your frame
 *   and after the patch, on every flip.
 *     Pixel Mode (VPixx VIEWPixx, VIEWPixx /EEG and /3D, DATAPixx and
 *       DATAPixx3, PROPixx): the device sets its digital outputs from the
 *       top-left pixel of each frame, bits 0 to 7 from red, 8 to 15 from
 *       green, 16 to 23 from blue, and holds them while the pixel stays.
 *       psyscr_slot_pixel_mode() is that pixel; psyscr_pixel_mode_bits()
 *       gives the value for 24 output bits. Turn the mode on with VPixx's
 *       own tools (Datapixx('EnablePixelMode') in Psychtoolbox).
 *     Pixel sync (VPixx): a register write on the device waits for a
 *       sequence of pixels on a chosen raster line (VPixx recommends at
 *       least 8). psyscr_slot_psync() is where Psychtoolbox's PsychDataPixx
 *       draws it, 8 pixels on scanline 0 from x = 10, and
 *       psyscr_psync_pattern() its values. The USB register side is not in
 *       this header.
 *     Bits# T-Lock packets fit a ROW slot; this header builds no packet.
 *   Drawing. DXGI_FLIP and COMPOSITION: a SOLID slot is one
 *   ID3D11DeviceContext1::ClearView; a ROW of up to 8 pixels is one
 *   ClearView per pixel, a longer one an UpdateSubresource (CODES in
 *   STATUS has the costs); after ANGLE's flush, so no GL state changes. On
 *   a presenter without draws_patch the core clears through GL, one
 *   scissored clear per SOLID slot and per ROW pixel, with the patch's
 *   save-and-restore list. open() refuses slots that overlap each other or
 *   the patch (the default top-left patch is on the Pixel Mode pixel: move
 *   it with desc.patch.corner), slots outside the display, and codes on a
 *   windowed screen, which is not at the display's origin.
 *   Exactness. At open, DXGI_FLIP and COMPOSITION draw all 256 values of
 *   each channel through both drawing paths into a test texture and read
 *   them back (selftest= in the describe line). desc.verify_codes = N
 *   copies the first row of each code to a staging texture every N flips
 *   and compares it one flip later (psyscr_code_verify()). Both check the
 *   back buffer only. What happens after it, the header reports where it
 *   can, in record.code_risk (and PSYSCR_FLIP_CODE_AT_RISK in the flags):
 *     COMPOSED        this flip went through DWM
 *     ADVANCED_COLOR  HDR or auto color management is on (Windows
 *                     DisplayConfig); DWM and the display kernel convert
 *                     colors then
 *     GAMMA           the OS gamma ramp is not identity (OS GAMMA)
 *     SCALED          the mode is not the panel's preferred mode
 *     UNVERIFIED      the self test did not run (SIM, a custom presenter)
 *     VERIFY_FAILED   a read-back differed (sticky)
 *     STATE_UNKNOWN   the display state could not be read
 *   It cannot see Night Light, accessibility color filters, the MHC
 *   calibration pipeline (matrix and LUT at scanout), GPU dithering, or the
 *   display's own processing; the describe line says mhc=unknown and
 *   nightlight=unknown. Check on the rig with the device's own read-back
 *   (VPixx: the top line through vline). desc.codes_strict makes open()
 *   fail when a display-wide risk is set.
 *   The ring gets one PSYSCR_EV_CODE record per flip on a screen with
 *   codes: t_ns the onset, u.u32[0] the frame index, u.u16[2] code_risk,
 *   u.u16[3] the slot count, u.u32[2..9] the value of each slot (a ROW's
 *   first pixel).
 *
 *   ---------------------------------------------------------------------
 *   OS GAMMA (desc.keep_os_gamma)
 *   ---------------------------------------------------------------------
 *   A device value is only exact if nothing changes it after the back
 *   buffer. A fullscreen screen on Windows reads the display's gamma ramp
 *   at open (GetDeviceGammaRamp). If it is not the 8-bit identity, it sets
 *   the identity (SetDeviceGammaRamp) and reads it back. At close, at
 *   exit() (atexit) and when SDL posts its quit event, it puts back the
 *   ramp it read. A crash leaves the identity ramp until the next mode
 *   change, logoff or another program sets one. A window never touches
 *   the ramp; desc.keep_os_gamma = true leaves a fullscreen screen's ramp
 *   alone too. The panic watchdog puts the ramp back before it ends a hung
 *   process (PANIC). The describe line says os_gamma=identity (it was),
 *   set, refused (Windows did not take it), unreadable, or the same with
 *   (kept); anything but identity or set sets CODE_RISK_GAMMA.
 *   Microsoft documents limits on these calls: SetDeviceGammaRamp may
 *   return success and not set a ramp, other programs and the OS may
 *   overwrite it at any time, a display event resets it, and its behavior
 *   is undefined in HDR. The ramp is checked at open only: one read costs
 *   0.8 ms (measured), too much for the frame loop. Whether the ramp acts
 *   on frames shown by independent flip or on an overlay plane, and
 *   whether Night Light replaces it, was not measured (it needs light or
 *   a capture card); do not rely on it there. Out of reach of this header:
 *   the MHC calibration pipeline, Night Light, HDR and auto color
 *   management. Later work: CGSetDisplayTransferByTable (macOS), the RandR
 *   CRTC gamma (X11), and wlr-gamma-control on wlroots compositors
 *   (Wayland has no general protocol).
 *
 *   ---------------------------------------------------------------------
 *   TRIGGERS (desc.triggers, psyscr_trigger, psyscr_trigger_at)
 *   ---------------------------------------------------------------------
 *   desc.triggers declares up to PSYSCR_MAX_TRIGGERS channels: a callback,
 *   its context and an offset. psyscr_trigger(s, channel, code), between
 *   begin() and flip_at(), runs that callback at this flip's planned
 *   vblank + desc.onset_offset_ns + the channel's offset, on a deadline
 *   worker (psy_rt.h WORKER) that open() starts when desc.n_triggers > 0.
 *   The callback runs on that thread, elevated; keep it to a port write:
 *       static void ttl(void* port, const psyscr_trigger_info* i) {
 *           psyp_pulse_async((psyp_port*)port, (uint8_t)i->code, 2000);
 *       }
 *   psyp_pulse_async() writes the leading edge on the calling thread, here
 *   the worker at the deadline. psyscr_trigger_at(s, channel, code, t)
 *   fires at time t, tied to no flip.
 *   When the header learns that a flip will be late, its triggers move:
 *     the caller was late    flip_at() planned a later vblank before the
 *                            trigger was armed (LATE_TARGET); no move
 *     the present returned   past the planned vblank's latch: the trigger
 *                            is armed for the vblank the frame can still
 *                            make (PSYSCR_TRIG_MOVED)
 *     the record completes   before the trigger fired, on another vblank:
 *                            it moves to that vblank (COMPOSITION's
 *                            composed path reports before the vblank)
 *     desc.trigger_fence     the worker checks a D3D11 fence 1 ms before
 *                            the deadline; GPU work not done means the
 *                            frame will miss, and the trigger moves one
 *                            vblank (PSYSCR_TRIG_GPU_MOVED). Off by
 *                            default (TRIGGERS in STATUS)
 *   A miss learned only from the statistic, after the trigger fired, is a
 *   mismatch: the result says FIRED_EARLY and by how many vblanks. A move
 *   never fires a trigger twice: a trigger the worker has taken cannot
 *   move, under one lock. close() stops the worker, which runs anything
 *   still armed at once with PSYSCR_TRIG_FLUSHED: a callback should not
 *   pulse then. A frame takes at most PSYSCR_MAX_JOBS triggers, and as
 *   many can be pending (PSYSCR_ERR_REFUSED beyond).
 *   Where the worker runs (desc.trigger_cpu). The rule: keep the worker
 *   off the CPUs that service the display's vblank DPC. A DPC runs ahead
 *   of every thread, at any priority, so a worker spinning on that CPU at
 *   the vblank waits for it. Which CPUs those are depends on the machine:
 *   the GPU, its driver, the interrupt affinity policy, the core types.
 *   This header knows one machine. On the laptop of STATUS (i7-1360P,
 *   Iris Xe) the DPC took 50 to 400 us from about 75 us before the vblank,
 *   2 to 3% of one P-core, on the P-core the worker spun on, and made
 *   about 1 trigger in 30 over 20 us late; on the E-cores it did not reach
 *   the worker (p99 1 to 3 us). So the default, on Windows on a hybrid
 *   CPU, is a hard affinity mask of the E-cores (with a soft E-core
 *   preference Windows still ran the worker on P-cores). That is this
 *   machine's answer, not a rule: on a CPU of one core type there are no
 *   E-cores and the default is psy_rt.h's placement, and on another GPU or
 *   interrupt policy the E-cores may be the busy ones. Check it: run
 *   examples/screen_flipstats.c --trigger, which prints the late triggers
 *   per CPU, and read "% DPC Time" per processor (typeperf) during a run.
 *   k > 0 pins the worker to logical CPU k - 1 (a pinned worker cannot
 *   leave a busy CPU: measured up to 48 ms late); -1 leaves it where
 *   psy_rt.h puts it (the P-cores of a hybrid CPU). The describe line
 *   says where: worker=TIME_CRITICAL/E-cores:cpus=0xff00, .../pinned:
 *   cpus=0x..., or .../psy_rt. Later: at open, sample the DPC count of
 *   each CPU for about 0.5 s and place the worker away from the busiest.
 *   desc.trigger_spin_ns is the worker's spin window (psy_rt.h WAITS).
 *   After a flip trigger fires, the worker arms itself 50 us before the
 *   next vblank's deadline, so the next frame's trigger needs no wake-up
 *   from the frame thread; when no trigger comes, that costs the worker
 *   one spin window for nothing.
 *   The ring gets one PSYSCR_EV_TRIGGER record per trigger when its flip
 *   has completed and it fired or was canceled: t_ns the fired time (the
 *   deadline if not fired), u.i64[0] the deadline, u.i64[1] the flip's
 *   onset, u.u32[4] the code, u.u32[5] the frame index, u.u16[12] the
 *   channel, u.u16[13] the flags, u.i32[7] the mismatch in vblanks,
 *   u.i32[8] fired minus deadline in ns.
 *
 *   ---------------------------------------------------------------------
 *   AFTER FLIP (psyscr_on_flip)
 *   ---------------------------------------------------------------------
 *   psyscr_on_flip(s, fn, ctx) calls fn with each completed record, oldest
 *   first, and the results of its triggers, on the frame thread, wherever
 *   records complete: begin(), wait_flip(), close(). For network markers
 *   (stamp them with the record's onset, not with "now"), logs, and
 *   trigger mismatches. It is late by design, by how long the record takes
 *   to complete (TRIGGERS in STATUS); on COMPOSITION's composed path it
 *   runs before the frame is on the screen, because the statistic is DWM's
 *   plan (ONSET_PLANNED). A trigger not fired yet when its record
 *   completes has PSYSCR_TRIG_PENDING. fn's time counts in begin().
 *
 *   ---------------------------------------------------------------------
 *   NATIVE HANDLES (psyscr_native)
 *   ---------------------------------------------------------------------
 *   psyscr_native(s, &out) gives a DXGI_FLIP or COMPOSITION screen's
 *   D3D11 device, its immediate context, ANGLE's EGL display and the LUID
 *   of the adapter the device is on, for a library that shares textures
 *   with the screen (psy_video.h's GPU path). Any other backend or
 *   platform: PSYSCR_ERR_NOT_IMPLEMENTED and zeros.
 *     Lifetime  the pointers are borrowed: no reference is added. They are
 *               valid from open() to close(); keep none past close(), and
 *               release any object you made on the device before it.
 *     Threads   the device is free-threaded (created without
 *               D3D11_CREATE_DEVICE_SINGLETHREADED): another thread may
 *               create resources on it. The immediate context is the frame
 *               thread's, shared with ANGLE: use it only on the frame
 *               thread, between begin() and flip_at(), and leave no state
 *               bound in it, because ANGLE caches the context's state and
 *               does not know of your calls.
 *     Video     desc.d3d11_video makes the device with
 *               D3D11_CREATE_DEVICE_VIDEO_SUPPORT and turns on multithread
 *               protection (ID3D11Multithread::SetMultithreadProtected), so
 *               a decoder thread can use the device's video interfaces and
 *               its calls on the immediate context take turns with the
 *               frame thread's and ANGLE's. out.video and out.mt_protected
 *               say what the device has, read from the device. An adapter
 *               without D3D11 video support makes open() fail with a
 *               message; there is no fallback. The frame thread may queue
 *               ID3D11DeviceContext4::Wait() on a fence the decoder
 *               signals, between begin() and flip_at(): it holds back the
 *               GPU queue, not the thread. Cost, measured with no decoder
 *               on the device: no difference in the header's CPU time per
 *               frame (STATUS). With a decoder, each immediate-context call
 *               takes a lock the decoder may hold; psy_video.h measures
 *               that. Off by default.
 *
 *   ---------------------------------------------------------------------
 *   INPUT
 *   ---------------------------------------------------------------------
 *   psyscr_poll() is SDL_PollEvent() plus the event's time on the psy_rt
 *   clock: SDL's event timestamp (SDL_GetTicksNS() units) moved onto the
 *   psy_rt clock through a psyrt_correlate() made at open and again every
 *   10 s. The correlation is exact to its width (0.1 us here); the event's
 *   own timestamp is what SDL gives.
 *   On Windows the header turns on SDL's raw keyboard path
 *   (SDL_HINT_WINDOWS_RAW_KEYBOARD, at default priority, so your own setting
 *   of the hint wins) and stops SDL text input, which would route keys
 *   through the message path. On the raw path SDL reads Raw Input on its
 *   own thread and stamps a key when it arrives: measured, keys sent with
 *   SendInput() were stamped 0.18 to 0.67 ms after the call. That
 *   stamp does not include the keyboard's scan, its USB polling interval
 *   (1 to 8 ms on common keyboards) or the HID stack, which were not
 *   measured. With the raw path off, SDL uses the message time, which
 *   counts in the system tick: measured, keys came out stamped from 6.2 ms
 *   before to 12.1 ms after the call. On X11 the server's 1 ms
 *   time, on Wayland the compositor's. Keyboards are not response boxes;
 *   use psy_serial.h for reaction times.
 *   With the raw path on, SDL 3.4 reports each key twice: once from its
 *   raw-input thread and once from the message loop, 0.1 to 11 ms apart
 *   (measured with SendInput, docs/psy_screen.md). To count presses, count
 *   a key-down only after a key-up of the same key.
 *
 *   ---------------------------------------------------------------------
 *   ABORT (desc.abort_keys, psyscr_request_abort, f.abort)
 *   ---------------------------------------------------------------------
 *   An abort asks the frame loop to stop; the header never stops it. Each
 *   abort is reported once: the next begin() returns PSYSCR_QUIT with
 *   f.abort (the reasons, ORed), f.abort_presses and f.abort_ns (the first
 *   one's time), and every other field of f 0. The begin() after that
 *   starts a frame again. The caller decides: save and close, or ask the
 *   operator and go on. The header calls no exit() and closes no window.
 *     PSYSCR_ABORT_KEY      the abort combination, Shift+Esc by default.
 *                           Esc alone does nothing, so a participant who
 *                           hits Esc does not end the session. A held key
 *                           is one press: repeats do not count
 *     PSYSCR_ABORT_CLOSE    a close request for a screen's window: the
 *                           close button, the taskbar, Task Manager's "End
 *                           task"
 *     PSYSCR_ABORT_ALT_F4   Alt+F4. The header sets
 *                           SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4 to "0" (at
 *                           default priority), so it is not a close request
 *     PSYSCR_ABORT_QUIT     SDL's quit event: Ctrl+C in the console, a
 *                           logoff, the last window closed (with CLOSE)
 *     PSYSCR_ABORT_REQUEST  psyscr_request_abort(), from any thread: an
 *                           operator console, a response-box button
 *   desc.abort_keys sets the combination: .key is an SDL keycode (0 =
 *   Esc); .mods are the PSYSCR_MOD_* that must be held (0 = Shift;
 *   PSYSCR_MOD_NONE = the key alone, for development; more modifiers held
 *   still count); .off turns the key off (the other reasons stay). There is
 *   one combination per process: open() refuses a desc that differs from
 *   an open screen's. The describe line says abort=shift+esc.
 *   The header sees each event as SDL queues it (SDL_AddEventWatch), so
 *   an abort counts although your code read the event first with
 *   psyscr_poll() or SDL_PollEvent(). An SDL event filter that drops the
 *   event hides it. Reports of one press within 30 ms (SDL's two, INPUT,
 *   and the panic watchdog's) are one press. The aborts wait in a log of
 *   16 per process; a screen opened later does not see the earlier ones.
 *   A group reports an abort in every f[i], and no member begins a frame.
 *   The ring gets one PSYSCR_EV_ABORT record per abort, when begin()
 *   reports it: t_ns the press (or the request), aux desc.display_index,
 *   u.u32[0] the reason, u.u32[1] who saw it (1 the watchdog's hook, 2
 *   injected input, 4 SDL), u.u32[2] the key, u.u32[3] the modifiers,
 *   u.i64[2] when begin() reported it.
 *
 *   ---------------------------------------------------------------------
 *   PANIC (desc.panic): Shift+Esc 3 times when the program hangs
 *   ---------------------------------------------------------------------
 *   An abort works only while the frame loop calls begin(). A program that
 *   hangs in fullscreen leaves a fullscreen window, a switched display
 *   mode and the OS gamma ramp the header set. desc.panic arms a watchdog
 *   for that: a thread with a low-level keyboard hook (WH_KEYBOARD_LL),
 *   which sees the abort combination whatever the frame thread does. The
 *   hook passes every key on, so SDL still sees the same keys, and press 1
 *   is an abort either way.
 *   The rule: desc.panic_presses (0 = 3) presses within
 *   desc.panic_window_ms (0 = 2000), with a window of this process in
 *   front (or the "Ghost" window Windows puts in front of a hung one after
 *   about 5 s, measured), and no abort reported by a begin() for
 *   desc.panic_grace_ms (0 = 10000). A responsive loop reports press 1
 *   within a frame, so presses 2 and 3 are more aborts, and a program that
 *   saves after press 1 is not killed. A program outside its frame loop
 *   (loading, saving) for longer than the grace time looks hung.
 *   The panic, on the watchdog thread: it removes the hook, so no key of
 *   the session waits on it; writes a PSYSCR_EV_PANIC record to each armed
 *   screen's ring (t_ns the panic, u.u32[0] the presses needed, u.i64[1]
 *   the last report); puts back each gamma ramp the header set; calls
 *   desc.panic_fn(desc.panic_ctx) on a thread of its own, at most 1 s; and
 *   ends the process with TerminateProcess(PSYSCR_PANIC_EXIT_CODE = 99).
 *   Windows puts back a display mode the screen switched as the process
 *   ends (measured: 1.2 s after press 3, teardown included).
 *   ChangeDisplaySettingsExW from the watchdog was measured too: it waited
 *   over 1 s on the hung window and brought the mode back no sooner, so
 *   the watchdog does not call it.
 *   Not exit(): exit runs atexit handlers and DLL detach, which can wait
 *   on locks the hung thread holds, and only the end of the process takes
 *   down a window and a swapchain that a hung thread owns.
 *   What another thread can put back: the gamma ramps (GDI, by the
 *   display's name) and ring records. What it cannot: the window,
 *   fullscreen and the display mode (a call on a window, or a display
 *   change, waits for the hung thread; the end of the process ends them);
 *   the D3D11 device, ANGLE and
 *   SDL (not safe while the frame thread is inside them); data not yet
 *   written (panic_fn may save some, but runs while the frame thread holds
 *   what it holds, so it must take no lock the frame loop uses); hardware
 *   a trigger left set (panic_fn may reset it).
 *   When it is armed: DXGI_FLIP and COMPOSITION in fullscreen, on
 *   Windows. A window has no ramp or mode to put back and its user has the
 *   desktop: the describe line says panic=idle(windowed); on SIM, CUSTOM
 *   and other platforms, panic=n/a. open() refuses desc.panic with
 *   abort_keys.off and with a key the hook cannot name (it takes Esc, F1
 *   to F12, a letter or a digit). The first armed screen's numbers hold
 *   until the last armed screen closes.
 *   Cost: while it is armed, every key of the session goes through the
 *   watchdog thread, which otherwise sleeps in GetMessage. Windows skips a
 *   hook that takes longer than LowLevelHooksTimeout (at most 1 s); this
 *   hook does a few compares and takes no lock the frame thread holds. A
 *   test hook that stalled for 1.5 s once was still called afterwards, so
 *   the header does not install it again. Keys on SDL's raw path were not
 *   stamped later with the hook armed (STATUS).
 *   Measured (examples/screen_abort.c; a window armed through a test
 *   seam, and fullscreen in a switched mode): see STATUS.
 *   Later, X11: the watchdog opens its own display connection and selects
 *   XI_RawKeyPress and XI_RawKeyRelease on the root window (XInput 2.1),
 *   which arrive whatever has the focus and are not consumed. It checks
 *   _NET_ACTIVE_WINDOW, puts back the CRTC gamma through RandR on its own
 *   connection (and the mode, unless the X server does as the process
 *   ends, to be measured), and ends with _exit(). Wayland has no global key
 *   events: no panic there; the compositor undoes a dead client's
 *   fullscreen.
 *
 *   ---------------------------------------------------------------------
 *   WINDOW ICON (desc.icon_rgba, desc.icon_sdl)
 *   ---------------------------------------------------------------------
 *   open() gives each window the header's own icon, Escher's impossible
 *   cube, drawn for this header (public domain, like the rest), at 16, 32
 *   and 48 pixels: SDL_SetWindowIcon with alternate images, so Windows
 *   takes the size its display scale needs. It is stored as a 5-color
 *   palette and runs of 4-bit indices (1312 bytes in all), decoded at
 *   open. desc.icon_rgba (icon_w x icon_h RGBA, top row first, 1 to 256 a
 *   side) sets yours; desc.icon_sdl keeps SDL's.
 *
 *   ---------------------------------------------------------------------
 *   DISPLAYS AND MODES
 *   ---------------------------------------------------------------------
 *   psyscr_displays() and psyscr_modes() list what SDL reports, the
 *   refresh as a rational (refresh_num / refresh_den Hz) and as a period.
 *   desc.mode all zero opens in the desktop mode and never switches. A
 *   desc.mode with any field set must match a mode psyscr_modes() lists
 *   (a refresh is compared as a rational), or open() fails: there is no
 *   nearest-mode fallback. A matching mode other than the desktop's
 *   switches the display for the life of the screen. psyscr_caps.mode is
 *   the mode read back from the OS after the switch. For a video at 23.976
 *   or 59.94 Hz, psyscr_mode_multiple() finds the mode whose refresh is a
 *   multiple of it, and its error in ppm.
 *
 *   ---------------------------------------------------------------------
 *   MULTIPLE DISPLAYS
 *   ---------------------------------------------------------------------
 *   One psyscr_screen per display (desc.display), each with its own device,
 *   context, swap path, grid and records; desc.display_index tells their
 *   records apart. psyscr_begin_group() and psyscr_flip_group_at() run a
 *   frame on all of them from one thread: a DXGI wait is a kernel object,
 *   so waiting on each in turn costs the longest wait, not the sum. Two
 *   displays that flip together is a claim about hardware: only genlock
 *   on a workstation GPU makes it, and two photodiodes measure it. The
 *   group calls do not synchronize anything; each record has its own onset.
 *
 *   ---------------------------------------------------------------------
 *   VARIABLE REFRESH
 *   ---------------------------------------------------------------------
 *   A capability, never a default. psyscr_caps.vrr_capable reports whether
 *   DXGI allows tearing presents, which a variable-refresh panel needs and
 *   which does not prove the panel has it. desc.vrr makes open() fail:
 *   v0.1 does not run variable refresh, which needs the photodiode
 *   interval sweep and the luminance-versus-interval sweep on that panel
 *   first. The header always presents with a sync interval of 1 and never
 *   with tearing. A driver that varies the refresh on its own shows up as
 *   off-grid vblanks (PSYSCR_FLIP_GRID_UNSTABLE) and as a warning in the
 *   describe line.
 *
 *   ---------------------------------------------------------------------
 *   PARAMETERS
 *   ---------------------------------------------------------------------
 *   psyscr_params() returns a table of the desc fields a designer sets:
 *   name, type, range, the value a zero field means, unit and one line of
 *   text.
 *
 *   ---------------------------------------------------------------------
 *   MEMORY AND THREADS
 *   ---------------------------------------------------------------------
 *   The handle holds everything; nothing is allocated after open(), and
 *   the frame loop makes no allocation of its own (measured: 0 C runtime
 *   heap calls from the header in a debug build). The handle is about 22
 *   KB, most of it the code rows and the depth evidence. One thread calls
 *   begin, flip and the rest for a screen. open() and close() of different
 *   screens must not run at the same time, because ANGLE's libEGL is
 *   loaded once per process and never unloaded. open() starts a thread
 *   only when desc.n_triggers > 0: the trigger worker (TRIGGERS), which
 *   close() stops; and, once per process, the panic watchdog when an armed
 *   screen opens (PANIC), which the last armed screen's close() stops. The
 *   abort state is one per process (ABORT). Raise the frame thread with
 *   psyrt_thread_elevate() before open.
 *
 *   ---------------------------------------------------------------------
 *   BUILDING
 *   ---------------------------------------------------------------------
 *   Link SDL3 (3.2 or later); with CMake, find_package(SDL3) and link
 *   SDL3::SDL3. The implementation includes <SDL3/SDL.h>, and on Windows
 *   <d3d11_1.h> and <dxgi1_5.h> from the Windows SDK or MinGW-w64. It
 *   links nothing else: d3d11.dll, dxgi.dll, user32.dll and libEGL.dll are
 *   loaded at run time. It does not include SDL_main.h, so your main() is
 *   yours. Put libEGL.dll and libGLESv2.dll from an ANGLE build beside the
 *   program, or name their directory in desc.angle_dir or PSYSCR_ANGLE_DIR.
 *   Link libm on Linux, and include psy_screen.h (or psy_rt.h) before any
 *   system header in the implementation file: psy_rt.h sets the
 *   feature-test macro that glibc reads at the first one.
 *   Define PSYSCR_NO_SDL to build without SDL: the core, SIM and CUSTOM
 *   only, with no window, no input and no GL. tests/adapt/psy_screen_test.c
 *   builds this way.
 *   Define PSYSCR_API to change the linkage of every function.
 *   Hot paths carry psy_rt.h's instrumentation macros: zones psyscr.begin,
 *   psyscr.wait, psyscr.pump, psyscr.stats, psyscr.flip, psyscr.patch,
 *   psyscr.present and psyscr.hold; a frame mark when a flip's record
 *   completes; plots of the residual and the depth.
 *
 *   LICENSE: public domain / MIT-0, see end of file.
 */
#ifndef PSY_SCREEN_H_INCLUDED
#define PSY_SCREEN_H_INCLUDED

#define PSYSCR_VERSION_MAJOR 0
#define PSYSCR_VERSION_MINOR 3
#define PSYSCR_VERSION_PATCH 1
#define PSYSCR_VERSION_STRING "0.3.1"

#include "psy_rt.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PSYSCR_API
#define PSYSCR_API extern
#endif

/* SDL types appear only behind pointers, so a translation unit that calls
 * the API needs no SDL include path. */
struct SDL_Window;
union SDL_Event;

/* --- codes -------------------------------------------------------------- */

#define PSYSCR_OK                    0
#define PSYSCR_QUIT                  1  /* psyscr_begin: an abort; f.abort says why (ABORT) */
#define PSYSCR_ERR_ARG             (-1)
#define PSYSCR_ERR_CLOSED          (-2)  /* the screen is not open           */
#define PSYSCR_ERR_TIMEOUT         (-3)  /* the swap path freed no slot      */
#define PSYSCR_ERR_LOST            (-4)  /* the device or window was lost    */
#define PSYSCR_ERR_ORDER           (-5)  /* begin and flip out of turn       */
#define PSYSCR_ERR_NOT_IMPLEMENTED (-6)
#define PSYSCR_ERR_REFUSED         (-7)  /* a request done only on purpose   */

#define PSYSCR_LEAD_NONE (-1.0)   /* desc.lead: never before the target     */

/* Phases of a frame, for psyscr_mark() and psyscr_record.phase_ns[]. */
#define PSYSCR_PHASE_EVALUATE 0   /* timeline evaluate                      */
#define PSYSCR_PHASE_SCRIPT   1   /* script callback                        */
#define PSYSCR_PHASE_DRAW     2   /* draw submission; unmarked time too     */
#define PSYSCR_PHASE_UPLOAD   3   /* texture upload                         */
#define PSYSCR_PHASE_SWAP     4   /* begin's wait for a slot + present call */
#define PSYSCR_PHASE_GPU      5   /* GPU time: always UNKNOWN in v0.1       */
#define PSYSCR_N_PHASES       6
#define PSYSCR_PHASE_UNKNOWN  0xFFFFFFFFu

/* Flip record flags. */
#define PSYSCR_FLIP_PENDING       0x001u /* not shown yet                    */
#define PSYSCR_FLIP_ESTIMATED     0x002u /* no OS statistic for this present;
                                          * onset = the planned vblank      */
#define PSYSCR_FLIP_LATE_TARGET   0x004u /* t was behind the next reachable
                                          * vblank when flip_at ran         */
#define PSYSCR_FLIP_OCCLUDED      0x008u /* the OS said the window is hidden */
#define PSYSCR_FLIP_GRID_UNSTABLE 0x010u /* this vblank was off the grid     */
#define PSYSCR_FLIP_EARLY         0x020u /* shown before the planned vblank  */
#define PSYSCR_FLIP_SKIPPED       0x040u /* never shown: a later present took
                                          * its vblank; onset 0             */
#define PSYSCR_FLIP_CANCELED      0x080u /* never shown: canceled; onset 0  */
#define PSYSCR_FLIP_ONSET_PLANNED 0x100u /* onset is the vblank the system
                                          * planned, not one it observed    */
#define PSYSCR_FLIP_BELOW_TIER    0x200u /* tier worse than desc.min_tier   */
#define PSYSCR_FLIP_CODE_AT_RISK  0x400u /* record.code_risk is not 0; not in
                                          * the ring's mode word (CODES)    */

/* How far a flip's onset can be trusted, from its backend, path and the
 * source of its time (TIERS in the manual). Lower is better. */
typedef enum psyscr_tier {
    PSYSCR_TIER_UNKNOWN = 0,
    PSYSCR_TIER_1       = 1,  /* an OS vblank time for a flip it observed   */
    PSYSCR_TIER_2       = 2,  /* good under stated conditions only          */
    PSYSCR_TIER_3       = 3,  /* runs for development; onset not a measurement */
    PSYSCR_TIER_SIM     = 4   /* the simulated display                      */
} psyscr_tier;

/* The ring record's mode word, u.u16[5]. */
#define PSYSCR_EV_PATH_OF(w)  ((unsigned)(w) & 0x7u)
#define PSYSCR_EV_FLAGS_OF(w) (((unsigned)(w) >> 3) & 0x3FFu)
#define PSYSCR_EV_TIER_OF(w)  ((unsigned)(w) >> 13)

/* Ring record kinds under PSYRT_SRC_SCREEN. */
#define PSYSCR_EV_FLIP      1u
#define PSYSCR_EV_PATH      2u
#define PSYSCR_EV_MODE      3u
#define PSYSCR_EV_REPRESENT 4u   /* reserved for variable refresh           */
#define PSYSCR_EV_TRIGGER   5u   /* one at-onset trigger (TRIGGERS)         */
#define PSYSCR_EV_CODE      6u   /* the codes in one flip (CODES)           */
#define PSYSCR_EV_ABORT     7u   /* one abort, as begin() reports it (ABORT) */
#define PSYSCR_EV_PANIC     8u   /* the watchdog ends the process (PANIC)   */

/* --- abort and panic (ABORT, PANIC) --------------------------------------- */

/* psyscr_frame.abort: why begin() returned PSYSCR_QUIT. Several can be set. */
#define PSYSCR_ABORT_KEY     0x01u /* the abort combination (desc.abort_keys)  */
#define PSYSCR_ABORT_CLOSE   0x02u /* the window's close request: close button,
                                    * taskbar, Task Manager's "End task"     */
#define PSYSCR_ABORT_ALT_F4  0x04u /* Alt+F4                                   */
#define PSYSCR_ABORT_QUIT    0x08u /* SDL's quit event: Ctrl+C in the console,
                                    * logoff, the last window closed          */
#define PSYSCR_ABORT_REQUEST 0x10u /* psyscr_request_abort()                   */

/* psyscr_abort_keys.mods. Either side's key counts. */
#define PSYSCR_MOD_SHIFT 0x01u
#define PSYSCR_MOD_CTRL  0x02u
#define PSYSCR_MOD_ALT   0x04u
#define PSYSCR_MOD_GUI   0x08u
#define PSYSCR_MOD_NONE  0x80u     /* the key alone: no modifier needed        */

#define PSYSCR_KEY_ESCAPE 0x1Bu    /* SDLK_ESCAPE, so this header needs no SDL */

/* The exit code of a process the panic watchdog ends. */
#define PSYSCR_PANIC_EXIT_CODE 99

/* The abort combination. Zero: Shift+Esc. */
typedef struct psyscr_abort_keys {
    bool     off;   /* no key aborts; close, Alt+F4, quit and requests still do */
    uint32_t key;   /* an SDL keycode (SDL_Keycode); 0 = PSYSCR_KEY_ESCAPE      */
    uint32_t mods;  /* PSYSCR_MOD_* that must all be held; 0 = PSYSCR_MOD_SHIFT */
} psyscr_abort_keys;

typedef void (*psyscr_panic_fn)(void* ctx);

/* Patch corners. */
#define PSYSCR_TOP_LEFT     0
#define PSYSCR_TOP_RIGHT    1
#define PSYSCR_BOTTOM_LEFT  2
#define PSYSCR_BOTTOM_RIGHT 3
#define PSYSCR_PATCH_DEFAULT_SIZE 32

typedef enum psyscr_backend {
    PSYSCR_BACKEND_AUTO = 0,
    PSYSCR_BACKEND_DXGI_FLIP,      /* Windows 10 and 11                      */
    PSYSCR_BACKEND_COMPOSITION,    /* Windows 11 composition swapchain       */
    PSYSCR_BACKEND_GLX_OML,        /* Linux X11: stub                        */
    PSYSCR_BACKEND_WAYLAND,        /* stub                                   */
    PSYSCR_BACKEND_METAL,          /* macOS: stub                            */
    PSYSCR_BACKEND_WEB,            /* stub                                   */
    PSYSCR_BACKEND_SIM,            /* no window, a vblank grid on psy_rt     */
    PSYSCR_BACKEND_CUSTOM          /* desc.presenter                         */
} psyscr_backend;

typedef enum psyscr_kind {
    PSYSCR_FIXED_GRID = 1,
    PSYSCR_CONTINUOUS = 2,
    PSYSCR_CALLBACK   = 3
} psyscr_kind;

typedef enum psyscr_path {
    PSYSCR_PATH_UNKNOWN     = 0,
    PSYSCR_PATH_COMPOSED    = 1,
    PSYSCR_PATH_OVERLAY     = 2,
    PSYSCR_PATH_INDEPENDENT = 3,
    PSYSCR_PATH_SIMULATED   = 4
} psyscr_path;

/* A display mode. The refresh is a rational in Hz. */
typedef struct psyscr_mode {
    int32_t  w, h;           /* pixels                                      */
    int32_t  refresh_num;    /* 0 = unknown                                  */
    int32_t  refresh_den;
    int64_t  period_ns;      /* refresh_den * 1e9 / refresh_num, rounded     */
    uint32_t format;         /* SDL pixel format                             */
    float    density;
} psyscr_mode;

typedef struct psyscr_display_info {
    uint32_t    id;          /* SDL_DisplayID                                */
    char        name[64];
    int32_t     x, y, w, h;  /* desktop bounds                               */
    psyscr_mode desktop;
    bool        primary;
} psyscr_display_info;

typedef struct psyscr_caps {
    psyscr_kind    kind;
    psyscr_backend backend;
    int64_t  period_ns;        /* FIXED_GRID, CALLBACK: the mode's period     */
    int64_t  min_interval_ns;  /* CONTINUOUS                                  */
    int64_t  max_interval_ns;
    bool     native_target;    /* the OS takes a target vblank or time        */
    bool     hw_onset;         /* onsets are OS vblank timestamps             */
    bool     vrr_capable;      /* necessary, not sufficient                   */
    int32_t  max_in_flight;    /* 1                                           */
    psyscr_mode mode;          /* the mode obtained, read back from the OS    */
    psyscr_tier worst_tier;    /* the worst tier of any flip since open       */
} psyscr_caps;

typedef struct psyscr_patch {
    bool    on;
    int32_t size;              /* pixels, square; 0 = PSYSCR_PATCH_DEFAULT_SIZE */
    int32_t corner;            /* PSYSCR_TOP_LEFT .. PSYSCR_BOTTOM_RIGHT        */
} psyscr_patch;

/* --- codes: exact device values in the frame (CODES) ---------------------- */

#define PSYSCR_MAX_CODES        8
#define PSYSCR_CODE_ROW_PIXELS  1024   /* all ROW slots together             */
#define PSYSCR_CODE_HOLD        (-1)   /* frames: until changed              */

typedef enum psyscr_code_kind {
    PSYSCR_CODE_SOLID = 0,     /* a rectangle of one value                     */
    PSYSCR_CODE_ROW   = 1      /* w pixels of one row, each its own value      */
} psyscr_code_kind;

/* A code slot, declared at open. Values are 0xBBGGRR: red in bits 0 to 7. */
typedef struct psyscr_code_slot {
    int32_t  kind;             /* psyscr_code_kind                             */
    int32_t  x, y;             /* display pixels, top-left origin              */
    int32_t  w, h;             /* ROW: w pixels; h is taken as 1               */
    uint32_t rest;             /* the value between codes, every pixel         */
} psyscr_code_slot;

/* One code as the swap path draws it this flip. */
typedef struct psyscr_code_draw {
    int32_t         x, y, w, h;
    uint32_t        value;     /* SOLID                                        */
    const uint32_t* px;        /* ROW: w values; NULL for SOLID                */
} psyscr_code_draw;

/* record.code_risk: why a code pixel may not reach the display exactly. */
#define PSYSCR_CODE_RISK_COMPOSED       0x0001u /* this flip went through DWM */
#define PSYSCR_CODE_RISK_ADVANCED_COLOR 0x0002u /* HDR or auto color management */
#define PSYSCR_CODE_RISK_GAMMA          0x0004u /* the OS ramp is not identity  */
#define PSYSCR_CODE_RISK_SCALED         0x0008u /* mode is not the panel's own  */
#define PSYSCR_CODE_RISK_UNVERIFIED     0x0010u /* the open-time self test did not run */
#define PSYSCR_CODE_RISK_VERIFY_FAILED  0x0020u /* a read-back differed (sticky) */
#define PSYSCR_CODE_RISK_STATE_UNKNOWN  0x0040u /* the display state was not readable */

/* --- triggers at onset (TRIGGERS) ----------------------------------------- */

#define PSYSCR_MAX_TRIGGERS 8
#define PSYSCR_MAX_JOBS     16

/* psyscr_trigger_info.flags and psyscr_trigger_result.flags */
#define PSYSCR_TRIG_MOVED       0x0001u /* armed for a later vblank than planned */
#define PSYSCR_TRIG_FIRED_EARLY 0x0002u /* fired, then the frame was shown later */
#define PSYSCR_TRIG_FIRED_LATE  0x0004u /* fired, then the frame was shown earlier */
#define PSYSCR_TRIG_WORKER_LATE 0x0008u /* fired over 1 ms after its deadline    */
#define PSYSCR_TRIG_CANCELED    0x0010u /* never fired: its flip was not shown   */
#define PSYSCR_TRIG_FLUSHED     0x0020u /* run early by close()                  */
#define PSYSCR_TRIG_NOT_SHOWN   0x0040u /* its flip was skipped or canceled      */
#define PSYSCR_TRIG_PENDING     0x0080u /* not fired yet when reported           */
#define PSYSCR_TRIG_GPU_MOVED   0x0100u /* moved: the GPU had not finished       */
#define PSYSCR_TRIG_ESTIMATED   0x0200u /* its flip has no OS time               */

typedef struct psyscr_trigger_info {
    int64_t  deadline_ns;      /* planned vblank + onset offset + channel offset */
    int64_t  fired_ns;         /* the clock as the callback starts            */
    int64_t  woke_ns;          /* the clock as psy_rt.h's worker entered the
                                * job, after its spin: woke_ns - deadline_ns
                                * is psy_rt.h's own lateness                */
    int64_t  lock_ns;          /* then the wait for the trigger lock         */
    int64_t  frame;            /* flip index; -1 for psyscr_trigger_at()      */
    uint32_t code;
    uint16_t channel;
    uint16_t flags;            /* MOVED, GPU_MOVED, FLUSHED                   */
} psyscr_trigger_info;

typedef void (*psyscr_trigger_fn)(void* ctx, const psyscr_trigger_info* info);

typedef struct psyscr_trigger_desc {
    psyscr_trigger_fn fn;
    void*       ctx;
    int64_t     offset_ns;     /* added to the planned vblank + onset offset  */
    const char* name;
} psyscr_trigger_desc;

typedef struct psyscr_trigger_result {
    int64_t  deadline_ns;
    int64_t  fired_ns;         /* 0 = not fired                               */
    int64_t  onset_ns;         /* the flip's onset; 0 = not shown             */
    int64_t  frame;
    uint32_t code;
    uint16_t channel;
    uint16_t flags;
    int32_t  mismatch;         /* vblank shown minus vblank fired for         */
    int32_t  reserved_;
} psyscr_trigger_result;

/* What psyscr_on_present()'s callback gets. */
typedef struct psyscr_present_info {
    int64_t index;             /* frame number                                */
    int64_t onset;             /* begin()'s predicted onset                   */
    int32_t w, h;              /* drawable pixels                             */
    int32_t rows_top_down;     /* 1: GL row 0 is the top of the screen        */
    int32_t reserved_;
} psyscr_present_info;

/* The record of one flip. */
typedef struct psyscr_record {
    int64_t  index;      /* frame number                                      */
    int64_t  target;     /* t passed to flip_at, RT ns                         */
    int64_t  planned;    /* the vblank time the flip was planned for (+offset) */
    int64_t  onset;      /* estimated onset, RT ns (+offset); 0 while pending */
    int64_t  residual;   /* onset - target                                    */
    uint32_t dropped;    /* vblanks between planned and actual                */
    uint8_t  path;       /* psyscr_path                                       */
    uint8_t  tier;       /* psyscr_tier                                       */
    uint16_t flags;      /* PSYSCR_FLIP_*                                     */
    uint32_t phase_ns[PSYSCR_N_PHASES];
    uint16_t code_risk;  /* PSYSCR_CODE_RISK_*; 0 on a screen with no codes   */
    uint16_t reserved_;
} psyscr_record;

typedef void (*psyscr_flip_fn)(void* ctx, const psyscr_record* r,
                               const psyscr_trigger_result* trig, int n_trig);
typedef void (*psyscr_present_fn)(void* ctx, const psyscr_present_info* info);

/* What the frame loop draws for. */
typedef struct psyscr_frame {
    int64_t onset;       /* predicted onset of this frame's flip, RT ns       */
    int64_t period;      /* ns; 0 when there is no fixed period                */
    int64_t index;       /* frame number, 0 for the first                     */
    int64_t vblank;      /* the vblank count it is planned for                */
    const psyscr_record* last; /* newest completed flip record; NULL before one */
    /* Every flip record completed since the begin() before this one, in
     * completion order (records complete in begin(), flip_at(),
     * wait_flip()); last is the final one. Valid until the next begin(). */
    const psyscr_record* done;
    int32_t  n_done;
    uint32_t done_lost;  /* records that did not fit (PSYSCR_MAX_DONE), since open */
    /* Set only when begin() returns PSYSCR_QUIT (every other field is then
     * 0): the aborts since the last report (ABORT). */
    uint32_t abort;          /* PSYSCR_ABORT_*                               */
    int32_t  abort_presses;  /* presses of the abort combination in it       */
    int64_t  abort_ns;       /* the first one's time, RT ns                  */
} psyscr_frame;

/* The records one psyscr_frame can carry in done. One frame in flight
 * completes at most a few per begin(); more means begin() was not called
 * while many flips completed. */
#define PSYSCR_MAX_DONE 16

/* Native handles of a swap path on Windows (psyscr_native()). */
typedef struct psyscr_native_info {
    void*    d3d11_device;   /* ID3D11Device*, the device ANGLE renders with  */
    void*    d3d11_context;  /* its immediate ID3D11DeviceContext*            */
    void*    egl_display;    /* ANGLE's EGLDisplay                            */
    uint32_t luid_low;       /* the adapter's LUID (DXGI_ADAPTER_DESC.AdapterLuid) */
    int32_t  luid_high;
    int32_t  video;          /* 1: made with D3D11_CREATE_DEVICE_VIDEO_SUPPORT */
    int32_t  mt_protected;   /* 1: multithread protection is on               */
} psyscr_native_info;

/* --- presenter (the swap-path interface) ------------------------------- */

/* A GL or EGL entry point. Cast it to the function's own type to call it;
 * ISO C converts between function pointer types, not to and from void*. */
typedef void (*psyscr_proc)(void);

#define PSYSCR_PRESENTER_VERSION 2

typedef struct psyscr_vblank {
    uint64_t present_id;     /* the present it completes; 0 = a bare vblank */
    int64_t  t_ns;           /* vblank time on the psy_rt clock              */
    int64_t  count;          /* vblank counter                               */
    uint8_t  path;           /* psyscr_path                                  */
    uint8_t  tier;           /* psyscr_tier; 0 = from the path               */
    uint16_t flags;          /* OCCLUDED, SKIPPED, CANCELED, ONSET_PLANNED;
                              * t_ns 0 = no time (the core estimates)       */
    uint32_t reserved_;
} psyscr_vblank;

typedef struct psyscr_present_req {
    uint64_t present_id;     /* from the core, increasing                    */
    int64_t  target_count;   /* the vblank to show on; 0 = the next one      */
    int64_t  target_ns;      /* the same vblank as a time                    */
    int32_t  hold;           /* 1                                            */
    int32_t  patch_on;       /* paint the patch, when draws_patch            */
    int32_t  patch_x, patch_y, patch_w, patch_h;   /* pixels, top-left origin */
    float    patch_value;    /* gray, 0..1                                   */
    int32_t  verify;         /* 1: read the codes back (draws_patch)         */
    const psyscr_code_draw* codes;      /* drawn after the patch, when draws_patch */
    int32_t  n_codes;
    int32_t  reserved_;
} psyscr_present_req;

typedef struct psyscr_presenter_open {
    struct SDL_Window*  window;    /* NULL when the presenter needs none     */
    uint32_t            display;
    const psyscr_mode*  mode;
    const char*         angle_dir;
    int64_t             sim_period_ns;
    int32_t             n_codes;   /* code slots: run the code self test      */
    int32_t             want_gpu_done;  /* desc.trigger_fence: set up gpu_done */
    /* Written by a presenter that draws codes and reads them back: */
    uint32_t*           code_checked;  /* codes compared                     */
    uint32_t*           code_failed;   /* codes that differed                */
    int32_t*            code_selftest; /* 1 passed, -1 failed, 0 not run     */
    int32_t             d3d11_video;   /* desc.d3d11_video                   */
} psyscr_presenter_open;

typedef struct psyscr_presenter {
    uint32_t    version;           /* PSYSCR_PRESENTER_VERSION               */
    const char* name;
    bool        needs_window;      /* the core creates the SDL window        */
    bool        waits_block;       /* acquire() blocks the thread            */
    bool        draws_patch;       /* present() paints the patch itself      */
    int   (*open)(void* ctx, const psyscr_presenter_open* in, psyscr_caps* caps,
                  char* err, size_t err_cap);
    void  (*close)(void* ctx);
    int   (*acquire)(void* ctx, int64_t deadline_ns, psyscr_vblank* newest);
    int   (*present)(void* ctx, const psyscr_present_req* req);
    int   (*completions)(void* ctx, psyscr_vblank* out, int cap);
    psyscr_proc (*gl_proc)(void* ctx, const char* name); /* may be NULL        */
    void  (*bind)(void* ctx);                         /* may be NULL           */
    int   (*describe)(void* ctx, char* buf, size_t cap); /* may be NULL        */
    /* The highest present_id whose GPU work has finished, or 0 if unknown.
     * Called from the trigger worker thread; may be NULL. */
    uint64_t (*gpu_done)(void* ctx);
} psyscr_presenter;

/* --- open ---------------------------------------------------------------- */

typedef struct psyscr_desc {
    uint32_t       display;          /* SDL_DisplayID; 0 = primary            */
    psyscr_mode    mode;             /* zero = the desktop mode                */
    bool           windowed;
    int32_t        window_w, window_h;  /* windowed; 0 = 800 x 600            */
    psyscr_backend backend;          /* 0 = AUTO                               */
    double         lead;             /* 0 = 0.5; PSYSCR_LEAD_NONE              */
    psyrt_ring*    ring;             /* flip records; NULL = none              */
    uint32_t       display_index;    /* aux of every record                    */
    psyscr_patch   patch;
    bool           show_cursor;
    psyscr_abort_keys abort_keys;    /* zero: Shift+Esc (ABORT)                */
    bool           vrr;              /* refused                                */
    int32_t        min_tier;         /* 0 = off; else flag flips worse than it */
    int64_t        onset_offset_ns;
    int64_t        sim_period_ns;    /* BACKEND_SIM; 0 = 1e9 / 60              */
    const char*    angle_dir;
    const psyscr_presenter* presenter;   /* BACKEND_CUSTOM                     */
    void*          presenter_ctx;
    /* codes (CODES) */
    psyscr_code_slot codes[PSYSCR_MAX_CODES];
    int32_t        n_codes;
    int32_t        verify_codes;     /* read the codes back every N flips; 0 = off */
    bool           codes_strict;     /* open() fails when codes are at risk   */
    /* the OS gamma ramp (OS GAMMA) */
    bool           keep_os_gamma;    /* fullscreen: leave the OS ramp alone   */
    /* triggers at onset (TRIGGERS) */
    bool           trigger_fence;    /* move a trigger when the GPU is late   */
    int32_t        trigger_cpu;      /* 0 = default: E-cores on a hybrid CPU (TRIGGERS);
                                      * k > 0 = logical CPU k - 1; -1 = psy_rt.h's */
    uint32_t       trigger_spin_ns;  /* the worker's spin window; 0 = psy_rt's default */
    const psyscr_trigger_desc* triggers;  /* copied at open                   */
    int32_t        n_triggers;
    /* the panic watchdog (PANIC); Windows, fullscreen */
    bool           panic;            /* arm it; off by default                 */
    int32_t        panic_presses;    /* abort presses that panic; 0 = 3        */
    int32_t        panic_window_ms;  /* ... within this time; 0 = 2000         */
    int32_t        panic_grace_ms;   /* no panic this long after begin()
                                      * reported an abort; 0 = 10000          */
    psyscr_panic_fn panic_fn;        /* last words, on another thread; NULL = none */
    void*          panic_ctx;
    /* the window icon (WINDOW ICON) */
    const uint8_t* icon_rgba;        /* icon_w x icon_h RGBA, top row first;
                                      * NULL = the header's impossible cube   */
    int32_t        icon_w, icon_h;   /* 1 to 256                               */
    bool           icon_sdl;         /* keep SDL's own icon                    */
    /* the D3D11 device for a video decoder (NATIVE HANDLES) */
    bool           d3d11_video;      /* VIDEO_SUPPORT and multithread protection */
} psyscr_desc;

/* Private. One flip between flip_at() and its completion. */
typedef struct psyscr__pend {
    psyscr_record rec;
    uint64_t    id;
    int64_t     planned_count;
    int64_t     count_at_present;
    int64_t     t_ret;            /* when the present call returned         */
    uint32_t    code_vals[PSYSCR_MAX_CODES];  /* for the CODE record        */
    int32_t     asap;
    int32_t     used;
} psyscr__pend;

#define PSYSCR__MAX_PEND 8
/* Slack: the planned vblank's time minus the present call's return, in
 * bins of a 32nd of a period over 4 periods, per path. */
#define PSYSCR__SLACK_BINS  128
#define PSYSCR__SLACK_PATHS 5
#define PSYSCR__SLACK_DECAY 4096   /* flips between halvings of the counts  */
#define PSYSCR__BACKEND_WORDS 192

/* Private. One at-onset trigger job. */
typedef struct psyscr__job {
    int32_t  state;            /* 0 free, 1 armed, 2 firing, 3 fired, 4 canceled */
    uint16_t channel, flags;
    uint32_t code;
    int32_t  mismatch;
    uint64_t pend_id;          /* the flip's present id; 0 = trigger_at        */
    int64_t  frame;
    int64_t  count;            /* the vblank it fires for                      */
    int64_t  deadline;
    int64_t  wake;             /* deadline, or the GPU check before it         */
    int64_t  fired;
    int64_t  onset;
    int64_t  period;           /* for a move the worker makes                  */
    int32_t  flip_done;
    int32_t  check;            /* 1: check the GPU at wake, then fire          */
} psyscr__job;

/* The screen. Caller-allocated and zeroed; every field is private. */
typedef struct psyscr_screen {
    int                     open;
    int                     begun;
    int                     slot_held;
    int                     warming;
    int                     pr_open;
    int                     polled;
    psyscr_backend          backend;
    const psyscr_presenter* pr;
    void*                   pr_ctx;
    struct SDL_Window*      window;
    int                     sdl_video;
    int                     cursor_hidden;
    psyscr_caps             caps;
    psyrt_ring*             ring;
    uint32_t                display_index;
    double                  lead;
    int64_t                 lead_ns;
    int64_t                 offset;
    psyscr_patch            patch;
    float                   patch_value;
    /* abort and panic */
    int32_t                 abort_seen;   /* the last abort log entry reported */
    int32_t                 abort_user;   /* holds the process's abort state   */
    int32_t                 abort_slot;   /* + 1: in the window and panic table */
    int32_t                 panic_armed;
    void*                   hwnd;         /* the window's HWND, for the watchdog */
    void*                   win_icon[2];  /* HICONs the header set: small, big */
    /* the grid */
    int                     have_anchor;
    int64_t                 vb_t, vb_count;
    int64_t                 ref_t, ref_count;
    double                  period_f;
    double                  nominal_f;
    int64_t                 margin_ns;
    int32_t                 depth, depth_cand, depth_votes, depth_need;
    int32_t                 adopt_left;   /* queued flips a path change's window survives */
    int32_t                 last_obs, obs_streak;
    int32_t                 depth_of[PSYSCR__SLACK_PATHS];   /* 0 = not known yet */
    int32_t                 lower_streak, fresh_lower;
    uint32_t                slack_n;
    uint16_t                slack_ok[PSYSCR__SLACK_PATHS][PSYSCR__SLACK_BINS];
    uint16_t                slack_miss[PSYSCR__SLACK_PATHS][PSYSCR__SLACK_BINS];
    uint8_t                 path;
    uint32_t                unstable;
    int32_t                 min_tier;
    int32_t                 worst_tier;
    int64_t                 prev_shown;
    int64_t                 last_planned; /* the vblank flip_at() last planned */
    psyscr_record           fin[PSYSCR_MAX_DONE];   /* completed since begin() returned */
    psyscr_record           fin_out[PSYSCR_MAX_DONE]; /* the list the frame shows */
    int32_t                 n_fin, n_fin_out;
    uint32_t                fin_lost;
    /* this frame */
    int64_t                 index;
    int64_t                 pred_count;
    int64_t                 mark_t;
    uint32_t                acc[PSYSCR_N_PHASES];
    uint64_t                next_id;
    psyscr__pend            pend[PSYSCR__MAX_PEND];
    psyscr_record           last;
    int                     have_last;
    /* input */
    int64_t                 sdl_rt, sdl_ticks, sdl_corr_t;
    uint64_t                sdl_width;
    /* GL, through the presenter */
    psyscr_proc             gl[16];
    /* hooks */
    psyscr_flip_fn          on_flip;
    void*                   on_flip_ctx;
    psyscr_present_fn       on_present;
    void*                   on_present_ctx;
    uint32_t                gl_epoch;
    uint32_t                gl_generation;
    /* codes */
    int32_t                 n_codes;
    psyscr_code_slot        code_slot[PSYSCR_MAX_CODES];
    uint32_t                code_val[PSYSCR_MAX_CODES];
    int32_t                 code_left[PSYSCR_MAX_CODES];  /* flips; -1 hold; 0 rest */
    int32_t                 code_off[PSYSCR_MAX_CODES];   /* ROW: start in code_px */
    int32_t                 code_buf;                     /* which frame buffer   */
    uint32_t                code_px[PSYSCR_CODE_ROW_PIXELS];
    uint32_t                code_frame_px[2][PSYSCR_CODE_ROW_PIXELS];
    psyscr_code_draw        code_draw[2][PSYSCR_MAX_CODES];
    int32_t                 verify_every, code_selftest;
    uint32_t                code_checked, code_failed;
    uint16_t                code_risk;    /* display-wide part                  */
    char                    os_color[160]; /* for the describe line             */
    int32_t                 gamma_owned;
    /* triggers */
    int32_t                 n_trig, trig_on, trig_fence, worker_on, trig_cpu;
    char                    trig_cores;   /* 'E' E-cores, 'N' pinned, 0 psy_rt.h's choice */
    uint64_t                trig_mask;    /* the worker's CPUs when trig_cores is set */
    uint32_t                trig_spin;
    psyscr_trigger_desc     trig[PSYSCR_MAX_TRIGGERS];
    psyscr__job             job[PSYSCR_MAX_JOBS];
    int32_t                 n_req;
    uint32_t                req_code[PSYSCR_MAX_JOBS];
    uint16_t                req_ch[PSYSCR_MAX_JOBS];
    int64_t                 armed_wake;
    int64_t                 woke;           /* the worker's wake, for its callback */
    uint32_t                trig_lost;      /* no free job at arm time         */
    uint64_t                lock_mem[16];   /* a CRITICAL_SECTION or a mutex   */
    int32_t                 lock_ok;
#if !defined(PSYRT_NO_THREADS)
    psyrt_worker            worker;
#endif
    char                    error[256];
    uint64_t                backend_mem[PSYSCR__BACKEND_WORDS];
} psyscr_screen;

/* --- API ----------------------------------------------------------------- */

/* PSYSCR_VERSION_STRING of the implementation that was compiled. */
PSYSCR_API const char* psyscr_version(void);

/* Static text for a PSYSCR_* code ("ok" for 0 and other positive values). */
PSYSCR_API const char* psyscr_strerror(int code);

/* The connected displays: fills up to cap entries and returns the count, or
 * a negative code. Starts SDL's video subsystem for the call when no screen
 * holds it, which takes about 0.1 s: call it at setup, not in a trial. */
PSYSCR_API int psyscr_displays(psyscr_display_info* out, int cap);

/* The fullscreen modes of a display (0 = primary), in SDL's order: largest
 * and fastest first. Returns the count, which can be larger than cap. */
PSYSCR_API int psyscr_modes(uint32_t display, psyscr_mode* out, int cap);

/* The mode whose refresh is an integer multiple of num/den Hz (24000/1001
 * for NTSC film), at w x h (0 x 0 = the desktop size). Of the modes within
 * tol_ppm (0 = 200 ppm) of a multiple, the one with the highest refresh.
 * Returns the multiple (>= 1) and fills *out and *err_ppm (the refresh's
 * error against the exact multiple). Returns PSYSCR_ERR_REFUSED when no
 * mode qualifies, with the nearest mode and its error in *out and *err_ppm.
 * Nothing switches mode: open with desc.mode = *out to do that. */
PSYSCR_API int psyscr_mode_multiple(uint32_t display, int32_t num, int32_t den,
                                    int32_t w, int32_t h, double tol_ppm,
                                    psyscr_mode* out, double* err_ppm);

/* The same rule on a list you give (w or h 0 = any size). For a designer
 * that has no display, and for tests. */
PSYSCR_API int psyscr_mode_multiple_in(const psyscr_mode* modes, int n,
                                       int32_t num, int32_t den, int32_t w, int32_t h,
                                       double tol_ppm, psyscr_mode* out, double* err_ppm);

/* Opens the window, the context and the swap path, presents a few black
 * frames to find the vblank grid and the depth (about 0.1 to 0.7 s), and
 * returns true. On false, psyscr_error() says why. The handle must be zeroed
 * or closed. Refuses desc.vrr, a desc.mode that is not a listed mode of the
 * display, a lead outside 0, (0, 1) or PSYSCR_LEAD_NONE, and a backend that
 * v0.1 does not have. */
PSYSCR_API bool        psyscr_open(psyscr_screen* s, const psyscr_desc* desc);

/* Lets the last flip complete (its record reaches the ring), then closes
 * everything open() opened. Safe on a closed or zeroed handle. */
PSYSCR_API void        psyscr_close(psyscr_screen* s);

/* The last open() message; "" after a successful open. */
PSYSCR_API const char* psyscr_error(const psyscr_screen* s);
PSYSCR_API bool        psyscr_is_open(const psyscr_screen* s);

/* The capability kind, period, mode obtained and the rest; zeroes when the
 * screen is not open. */
PSYSCR_API void        psyscr_get_caps(const psyscr_screen* s, psyscr_caps* out);

/* One line for the log: backend, adapter, ANGLE version, mode, measured
 * refresh, path, depth, lead, and warnings. Returns snprintf's count. */
PSYSCR_API int         psyscr_describe(const psyscr_screen* s, char* buf, size_t cap);

/* Starts a frame. Pumps SDL's events (read them with psyscr_poll()), waits
 * until the swap path takes a frame (at most one is in flight), completes
 * the records of the flips that happened, binds the context and its back
 * buffer, and fills *f with the predicted onset of the first vblank this
 * frame can make. Returns PSYSCR_OK; PSYSCR_QUIT, without starting a frame,
 * once for each abort since the last report (ABORT): the abort
 * combination (Shift+Esc unless desc.abort_keys), a close request, Alt+F4,
 * SDL's quit event or psyscr_request_abort(). f.abort says which; every
 * other field of *f is 0. The next begin() starts a frame: you decide how
 * to stop. Aborts read with psyscr_poll() or SDL_PollEvent() before begin()
 * count too. PSYSCR_ERR_ORDER after a begin without a flip;
 * PSYSCR_ERR_TIMEOUT or PSYSCR_ERR_LOST from the swap path. */
PSYSCR_API int  psyscr_begin(psyscr_screen* s, psyscr_frame* f);

/* Ends a phase of this frame (PSYSCR_PHASE_EVALUATE .. _UPLOAD): the time
 * since begin() or the previous mark goes to that phase. Optional; ignored
 * outside a frame. */
PSYSCR_API void psyscr_mark(psyscr_screen* s, int phase);

/* Ends the frame: draws the patch, and presents the frame for the vblank t
 * snaps to (see FLIP AT A TIME), never before the first one it can make.
 * Waits on the psy_rt clock first when that vblank is later. Does not wait
 * for the flip: *out (may be NULL) gets index, target, planned, phases and
 * PSYSCR_FLIP_PENDING, and the complete record arrives with a later
 * begin() (f.last), with psyscr_wait_flip(), and in desc.ring. Returns
 * PSYSCR_OK, PSYSCR_ERR_ORDER without a begin, or the swap path's error. */
PSYSCR_API int  psyscr_flip_at(psyscr_screen* s, int64_t t, psyscr_record* out);

/* psyscr_flip_at() at the onset begin() predicted for this frame. */
PSYSCR_API int  psyscr_flip(psyscr_screen* s);

/* Blocks until every flip so far has its record, as Psychtoolbox's Flip
 * does, and copies the newest into *out (may be NULL). Call it between
 * frames, not between begin() and flip. PSYSCR_ERR_ORDER when no frame was
 * flipped yet. */
PSYSCR_API int  psyscr_wait_flip(psyscr_screen* s, psyscr_record* out);

/* The patch's gray level for this and the next frames, clamped to 0..1. */
PSYSCR_API void psyscr_set_patch(psyscr_screen* s, float v);

/* --- codes (CODES) ---------------------------------------------------------
 * A value for code slot `slot` (desc.codes[slot]), 0xBBGGRR, drawn on the
 * next `frames` flips and then the slot's rest value. psyscr_code() is one
 * flip; PSYSCR_CODE_HOLD holds it until the next call; 0 returns to rest
 * now. psyscr_code_row() sets the n pixels of a ROW slot (the rest of the
 * row takes the rest value). They return PSYSCR_OK or PSYSCR_ERR_ARG. */
PSYSCR_API int psyscr_code(psyscr_screen* s, int slot, uint32_t rgb);
PSYSCR_API int psyscr_code_frames(psyscr_screen* s, int slot, uint32_t rgb, int frames);
PSYSCR_API int psyscr_code_row(psyscr_screen* s, int slot, const uint32_t* px, int n, int frames);
/* VPixx Pixel Mode: the display's top-left pixel; digital out bits 0 to 7
 * are red, 8 to 15 green, 16 to 23 blue, so the code is the 24-bit value. */
PSYSCR_API psyscr_code_slot psyscr_slot_pixel_mode(void);
PSYSCR_API uint32_t psyscr_pixel_mode_bits(uint32_t ttl24);   /* the code for 24 output bits */
/* VPixx pixel sync as Psychtoolbox's PsychDataPixx draws it: 8 pixels on
 * scanline 0 from x = 10. psyscr_psync_pattern() fills its 8 values. */
PSYSCR_API psyscr_code_slot psyscr_slot_psync(void);
PSYSCR_API void psyscr_psync_pattern(uint32_t out[8], uint8_t counter);
/* The display-wide code risk (PSYSCR_CODE_RISK_*) and the read-back counts. */
PSYSCR_API uint16_t psyscr_code_risk(const psyscr_screen* s);
PSYSCR_API void psyscr_code_verify(const psyscr_screen* s, uint32_t* checked, uint32_t* failed);

/* --- triggers (TRIGGERS) ---------------------------------------------------
 * Fires desc.triggers[channel] for this frame's flip, at its planned vblank
 * + desc.onset_offset_ns + the channel's offset, on the screen's deadline
 * worker. Call between begin() and flip_at(). PSYSCR_ERR_REFUSED when the
 * frame already has PSYSCR_MAX_JOBS triggers or the queue is full. */
PSYSCR_API int psyscr_trigger(psyscr_screen* s, int channel, uint32_t code);
/* The same at time t on the psy_rt clock, not tied to a flip. */
PSYSCR_API int psyscr_trigger_at(psyscr_screen* s, int channel, uint32_t code, int64_t t);

/* --- hooks -----------------------------------------------------------------
 * After flip: fn gets each completed record, oldest first, with the results
 * of its triggers, on the frame thread (AFTER FLIP). NULL removes it. */
PSYSCR_API void psyscr_on_flip(psyscr_screen* s, psyscr_flip_fn fn, void* ctx);
/* Before present: fn may draw into the back buffer with GL (PRESENT
 * CALLBACK). NULL removes it. */
PSYSCR_API void psyscr_on_present(psyscr_screen* s, psyscr_present_fn fn, void* ctx);
/* Changes after every psyscr_on_present() callback, so a renderer that
 * caches GL state knows to drop it. */
PSYSCR_API uint32_t psyscr_gl_epoch(const psyscr_screen* s);
/* Changes when the GL context is new: every GL object made before is gone.
 * 0 while closed. Today only open() makes a context, so it is constant
 * from open to close (a lost device ends the screen: PSYSCR_ERR_LOST). */
PSYSCR_API uint32_t psyscr_gl_generation(const psyscr_screen* s);
#define PSYSCR_HAS_GL_EPOCH 1

/* --- native handles ---------------------------------------------------------
 * The D3D11 device and immediate context, ANGLE's EGL display and the
 * adapter LUID of a DXGI_FLIP or COMPOSITION screen (NATIVE HANDLES).
 * PSYSCR_OK; PSYSCR_ERR_NOT_IMPLEMENTED on any other backend and platform,
 * PSYSCR_ERR_CLOSED on a closed screen; out is zeroed then. */
PSYSCR_API int psyscr_native(const psyscr_screen* s, psyscr_native_info* out);

/* begin() and flip_at() on n screens. Each screen keeps its own grid, so
 * f[i].onset differ unless the displays are genlocked; flip_group_at snaps
 * t on each grid. PSYSCR_ERR_NOT_IMPLEMENTED for a swap path whose wait
 * blocks a thread (none in v0.1). An abort pending is reported in every
 * f[i] with PSYSCR_QUIT, and no member begins a frame. */
PSYSCR_API int  psyscr_begin_group(psyscr_screen* const* s, int n, psyscr_frame* f);
PSYSCR_API int  psyscr_flip_group_at(psyscr_screen* const* s, int n, int64_t t);

/* A GL ES 3.0 or EGL entry point of this screen's context; NULL on SIM.
 * Cast it to the function's type. */
PSYSCR_API psyscr_proc psyscr_gl_proc(const psyscr_screen* s, const char* name);

/* The SDL window; NULL on SIM and on presenters that need none. */
PSYSCR_API struct SDL_Window* psyscr_window(const psyscr_screen* s);

/* Makes this screen's context current on this thread, with its back
 * buffer. begin() does it; call it to draw to one screen of a group. */
PSYSCR_API void  psyscr_bind(psyscr_screen* s);

/* SDL_PollEvent(), and the event's time on the psy_rt clock in *t_rt (may
 * be NULL). See INPUT for what that time is worth. */
PSYSCR_API bool    psyscr_poll(psyscr_screen* s, union SDL_Event* ev, int64_t* t_rt);

/* An SDL_GetTicksNS() value on the psy_rt clock; 0 on a screen with no
 * window. */
PSYSCR_API int64_t psyscr_restamp(const psyscr_screen* s, uint64_t sdl_ticks_ns);

/* Asks every open screen to abort: the next begin() of each returns
 * PSYSCR_QUIT with PSYSCR_ABORT_REQUEST. Any thread; no screen needed (a
 * network command, a response box button). Not a panic. */
PSYSCR_API void    psyscr_request_abort(void);

/* One desc field a designer sets. */
typedef struct psyscr_param {
    const char* name;     /* the desc field, dotted for nested fields       */
    const char* type;     /* "u32", "i32", "i64", "f64", "bool", "enum"      */
    double      min, max; /* inclusive                                      */
    double      def;      /* the value a zero field means                   */
    const char* unit;     /* "", "px", "ns", "ms", "Hz", "frame"            */
    const char* doc;      /* one line                                       */
} psyscr_param;

/* The table of desc fields a designer sets; *n gets the count. The pointer,
 * the presenter and the ANGLE path are not in it. */
PSYSCR_API const psyscr_param* psyscr_params(int* n);

#ifdef __cplusplus
}
#endif

#endif /* PSY_SCREEN_H_INCLUDED */

/* ======================================================================= *
 *                            IMPLEMENTATION                               *
 * ======================================================================= */
#ifdef PSY_SCREEN_IMPLEMENTATION
#ifndef PSY_SCREEN_IMPLEMENTATION_GUARD
#define PSY_SCREEN_IMPLEMENTATION_GUARD

#ifndef PSY_RT_IMPLEMENTATION_GUARD
    #define PSY_RT_IMPLEMENTATION
    #include "psy_rt.h"
#endif
#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#elif !defined(PSYRT_NO_THREADS)
    #include <pthread.h>
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>

#if !defined(PSYSCR_NO_SDL)
    #include <SDL3/SDL.h>
#endif

#if defined(_WIN32) && !defined(PSYSCR_NO_SDL)
    #define PSYSCR__DXGI 1
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <d3d11_4.h>
    #include <dxgi1_5.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque GL and EGL handles, so no Khronos header is needed. */
typedef void*        psyscr__EGLDisplay;
typedef void*        psyscr__EGLContext;
typedef void*        psyscr__EGLSurface;
typedef void*        psyscr__EGLConfig;
typedef int32_t      psyscr__EGLint;
typedef unsigned int psyscr__GLenum;

#if defined(_WIN32)
    #define PSYSCR__APIENTRY __stdcall
#else
    #define PSYSCR__APIENTRY
#endif

typedef void (PSYSCR__APIENTRY *psyscr__glClearColor_fn)(float, float, float, float);
typedef void (PSYSCR__APIENTRY *psyscr__glClear_fn)(unsigned int);
typedef void (PSYSCR__APIENTRY *psyscr__glScissor_fn)(int, int, int, int);
typedef void (PSYSCR__APIENTRY *psyscr__glEnable_fn)(psyscr__GLenum);
typedef unsigned char (PSYSCR__APIENTRY *psyscr__glIsEnabled_fn)(psyscr__GLenum);
typedef void (PSYSCR__APIENTRY *psyscr__glGetIntegerv_fn)(psyscr__GLenum, int*);
typedef void (PSYSCR__APIENTRY *psyscr__glGetFloatv_fn)(psyscr__GLenum, float*);
typedef void (PSYSCR__APIENTRY *psyscr__glGetBooleanv_fn)(psyscr__GLenum, unsigned char*);
typedef void (PSYSCR__APIENTRY *psyscr__glColorMask_fn)(unsigned char, unsigned char, unsigned char, unsigned char);
typedef void (PSYSCR__APIENTRY *psyscr__glBindFramebuffer_fn)(psyscr__GLenum, unsigned int);
typedef const unsigned char* (PSYSCR__APIENTRY *psyscr__glGetString_fn)(psyscr__GLenum);
typedef void (PSYSCR__APIENTRY *psyscr__glFlush_fn)(void);

enum {
    PSYSCR__GL_CLEARCOLOR, PSYSCR__GL_CLEAR, PSYSCR__GL_SCISSOR, PSYSCR__GL_ENABLE,
    PSYSCR__GL_DISABLE, PSYSCR__GL_ISENABLED, PSYSCR__GL_GETINTEGERV,
    PSYSCR__GL_GETFLOATV, PSYSCR__GL_GETBOOLEANV, PSYSCR__GL_COLORMASK,
    PSYSCR__GL_BINDFRAMEBUFFER
};

#define PSYSCR__GL_COLOR_BUFFER_BIT     0x4000u
#define PSYSCR__GL_SCISSOR_TEST         0x0C11u
#define PSYSCR__GL_SCISSOR_BOX          0x0C10u
#define PSYSCR__GL_COLOR_CLEAR_VALUE    0x0C22u
#define PSYSCR__GL_COLOR_WRITEMASK      0x0C23u
#define PSYSCR__GL_RASTERIZER_DISCARD   0x8C89u
#define PSYSCR__GL_DRAW_FRAMEBUFFER     0x8CA9u
#define PSYSCR__GL_DRAW_FB_BINDING      0x8CA6u
#define PSYSCR__GL_VERSION              0x1F02u

/* --- small helpers ------------------------------------------------------- */

/* Test-only seam, not API: tests/adapt/psy_screen_test.c defines these
 * before the implementation to run the core on a virtual clock, so no
 * check there depends on how the host schedules the test. */
#ifndef PSYSCR__NOW
#define PSYSCR__NOW() ((int64_t)psyrt_now_ns())
#endif
#ifndef PSYSCR__SLEEP_UNTIL
#define PSYSCR__SLEEP_UNTIL(t, spin) psyrt_sleep_until((uint64_t)(t), (spin))
#endif
/* examples/screen_abort.c sets it to 1 to arm the panic watchdog in a
 * window, so its test needs no fullscreen. */
#ifndef PSYSCR__PANIC_WINDOWED
#define PSYSCR__PANIC_WINDOWED 0
#endif
static int64_t psyscr__now(void) { return PSYSCR__NOW(); }


static void psyscr__push_code(psyscr_screen* s, const psyscr__pend* p);
static void psyscr__trig_flip_done(psyscr_screen* s, const psyscr__pend* p, int64_t shown);
static void psyscr__trig_emit(psyscr_screen* s);

/* The trigger lock: frame thread and trigger worker. Stored in the handle's
 * lock_mem, so the public header needs no OS type. */
#if defined(PSYRT_NO_THREADS)
static int  psyscr__lock_init(psyscr_screen* s) { (void)s; return 1; }
static void psyscr__lock_free(psyscr_screen* s) { (void)s; }
static void psyscr__lock(psyscr_screen* s) { (void)s; }
static void psyscr__unlock(psyscr_screen* s) { (void)s; }
#elif defined(_WIN32)
typedef char psyscr__cs_fits[sizeof(CRITICAL_SECTION) <= sizeof(((psyscr_screen*)0)->lock_mem) ? 1 : -1];
static int  psyscr__lock_init(psyscr_screen* s) { InitializeCriticalSection((CRITICAL_SECTION*)(void*)s->lock_mem); return 1; }
static void psyscr__lock_free(psyscr_screen* s) { DeleteCriticalSection((CRITICAL_SECTION*)(void*)s->lock_mem); }
/* A wait for the lock is a zone, so a trace shows who held whom up. */
static void psyscr__lock(psyscr_screen* s) {
    CRITICAL_SECTION* cs = (CRITICAL_SECTION*)(void*)s->lock_mem;
    if (!s->lock_ok || TryEnterCriticalSection(cs)) return;
    {
        PSYRT_ZONE(z_wait, "psyscr.lockwait");
        EnterCriticalSection(cs);
        PSYRT_ZONE_END(z_wait);
    }
}
static void psyscr__unlock(psyscr_screen* s) { if (s->lock_ok) LeaveCriticalSection((CRITICAL_SECTION*)(void*)s->lock_mem); }
#else
typedef char psyscr__mx_fits[sizeof(pthread_mutex_t) <= sizeof(((psyscr_screen*)0)->lock_mem) ? 1 : -1];
static int  psyscr__lock_init(psyscr_screen* s) { return pthread_mutex_init((pthread_mutex_t*)(void*)s->lock_mem, NULL) == 0; }
static void psyscr__lock_free(psyscr_screen* s) { pthread_mutex_destroy((pthread_mutex_t*)(void*)s->lock_mem); }
static void psyscr__lock(psyscr_screen* s) {
    pthread_mutex_t* m = (pthread_mutex_t*)(void*)s->lock_mem;
    if (!s->lock_ok || pthread_mutex_trylock(m) == 0) return;
    {
        PSYRT_ZONE(z_wait, "psyscr.lockwait");
        pthread_mutex_lock(m);
        PSYRT_ZONE_END(z_wait);
    }
}
static void psyscr__unlock(psyscr_screen* s) { if (s->lock_ok) pthread_mutex_unlock((pthread_mutex_t*)(void*)s->lock_mem); }
#endif

static void psyscr__set_error(char* buf, size_t cap, const char* fmt, ...) {
    va_list ap;
    if (!buf || cap == 0) return;
    va_start(ap, fmt);
    vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
}

static void psyscr__copy(char* dst, size_t cap, const char* src) {
    size_t n;
    if (!dst || cap == 0) return;
    if (!src) src = "";
    n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static uint32_t psyscr__sat32(int64_t v) {
    if (v < 0) return 0;
    if (v > (int64_t)0xFFFFFFFEu) return 0xFFFFFFFEu;
    return (uint32_t)v;
}

/* --- abort (ABORT) ---------------------------------------------------------
 * One state per process: SDL's event watch (on the frame thread or on SDL's
 * raw-input thread), the panic watchdog's keyboard hook and
 * psyscr_request_abort() write it from any thread, and each screen's begin()
 * reads it. */
#if defined(_WIN32)
static int32_t psyscr__a_inc(volatile int32_t* p) { return (int32_t)InterlockedIncrement((volatile LONG*)p); }
static int32_t psyscr__a_load(volatile int32_t* p) { return (int32_t)InterlockedCompareExchange((volatile LONG*)p, 0, 0); }
static void psyscr__a_store(volatile int32_t* p, int32_t v) { (void)InterlockedExchange((volatile LONG*)p, (LONG)v); }
static int psyscr__a_cas(volatile int32_t* p, int32_t want, int32_t v) {
    return InterlockedCompareExchange((volatile LONG*)p, (LONG)v, (LONG)want) == (LONG)want;
}
static int64_t psyscr__a_load64(volatile int64_t* p) { return (int64_t)InterlockedCompareExchange64((volatile LONG64*)p, 0, 0); }
static void psyscr__a_store64(volatile int64_t* p, int64_t v) { (void)InterlockedExchange64((volatile LONG64*)p, (LONG64)v); }
#else
static int32_t psyscr__a_inc(volatile int32_t* p) { return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST); }
static int32_t psyscr__a_load(volatile int32_t* p) { return __atomic_load_n(p, __ATOMIC_SEQ_CST); }
static void psyscr__a_store(volatile int32_t* p, int32_t v) { __atomic_store_n(p, v, __ATOMIC_SEQ_CST); }
static int psyscr__a_cas(volatile int32_t* p, int32_t want, int32_t v) {
    return __atomic_compare_exchange_n(p, &want, v, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
static int64_t psyscr__a_load64(volatile int64_t* p) { return __atomic_load_n(p, __ATOMIC_SEQ_CST); }
static void psyscr__a_store64(volatile int64_t* p, int64_t v) { __atomic_store_n(p, v, __ATOMIC_SEQ_CST); }
#endif

/* psyscr__abort_entry.flags: who saw it */
#define PSYSCR__AB_HOOK     0x1u   /* the panic watchdog's keyboard hook         */
#define PSYSCR__AB_INJECTED 0x2u   /* the key came from SendInput or the like    */
#define PSYSCR__AB_SDL      0x4u   /* SDL's event watch                          */
#define PSYSCR__ABORT_LOG   16
/* Reports of one key press: the hook sees it as Windows reads it, SDL's
 * raw path 0.1 to 0.7 ms later, and SDL's message path, which with the raw
 * keyboard on reports each key a second time (measured, docs/psy_screen.md),
 * up to 12 ms either side (INPUT). A person cannot press twice within this. */
#define PSYSCR__AB_SAME_NS  30000000

typedef struct psyscr__abort_entry {
    volatile int32_t seq;      /* the entry's number, written last; 0 while written */
    uint32_t reason, flags, key, mods;
    int64_t  t;                /* RT ns                                          */
} psyscr__abort_entry;

static struct {
    psyscr__abort_entry log[PSYSCR__ABORT_LOG];
    volatile int32_t seq;      /* the last entry's number                        */
    volatile int32_t lock;     /* writers of last_t and press[]                  */
    volatile int64_t ack;      /* when a begin() last reported an abort          */
    volatile int64_t sdl_off;  /* RT ns minus SDL ticks                          */
    int32_t  users;            /* open screens                                   */
    int32_t  off;
    uint32_t key, mods;        /* the combination; mods 0 = the key alone        */
    int64_t  last_t;           /* the last press counted                         */
    int64_t  press[8];         /* the last presses, for the panic rule           */
    int32_t  n_press;
    int32_t  sdl_held;         /* the abort key, as SDL's events say          */
    int32_t  presses;          /* the panic rule; 0 = no watchdog armed          */
    int64_t  window_ns, grace_ns;
} psyscr__ab;

static int32_t psyscr__abort_push(uint32_t reason, uint32_t flags, uint32_t key, uint32_t mods, int64_t t) {
    int32_t n = psyscr__a_inc(&psyscr__ab.seq);
    psyscr__abort_entry* e = &psyscr__ab.log[(uint32_t)n % PSYSCR__ABORT_LOG];
    psyscr__a_store(&e->seq, 0);
    e->reason = reason;
    e->flags = flags;
    e->key = key;
    e->mods = mods;
    e->t = t;
    psyscr__a_store(&e->seq, n);
    return n;
}

/* The panic rule on a press at t: enough presses within the window, and no
 * begin() has reported an abort for the grace time, so the frame loop looks
 * hung. A loop that reported press 1 is saving, and must not be killed. */
static int psyscr__panic_due(int64_t t) {
    int i, n = 0;
    int64_t ack = psyscr__a_load64(&psyscr__ab.ack);
    psyscr__ab.press[(uint32_t)psyscr__ab.n_press++ % 8u] = t;
    if (psyscr__ab.presses <= 0) return 0;
    for (i = 0; i < 8; i++)
        if (psyscr__ab.press[i] && psyscr__ab.press[i] <= t && t - psyscr__ab.press[i] < psyscr__ab.window_ns) n++;
    return n >= psyscr__ab.presses && (ack == 0 || t - ack > psyscr__ab.grace_ns);
}

/* A press is a key-down after an up: Windows repeats key-downs while a key
 * is held, and a held combination must not count as several presses. */
static int psyscr__abort_edge(int32_t* held, int down) {
    int press = down && !*held;
    *held = down ? 1 : 0;
    return press;
}

/* A key-down that is not a repeat, at RT time t, with mods (PSYSCR_MOD_*)
 * held, from the source in flags. Returns 1 when the panic rule is met. */
static int psyscr__abort_key(uint32_t key, uint32_t mods, int64_t t, uint32_t flags) {
    int panic = 0;
    if (psyscr__ab.users <= 0 || psyscr__ab.off || key != psyscr__ab.key) return 0;
    if ((mods & psyscr__ab.mods) != psyscr__ab.mods) return 0;
    while (!psyscr__a_cas(&psyscr__ab.lock, 0, 1)) { }
    if (!(psyscr__ab.last_t && t - psyscr__ab.last_t < PSYSCR__AB_SAME_NS && psyscr__ab.last_t - t < PSYSCR__AB_SAME_NS)) {
        psyscr__ab.last_t = t;
        psyscr__abort_push(PSYSCR_ABORT_KEY, flags, key, mods, t);
        panic = psyscr__panic_due(t);
    }
    psyscr__a_store(&psyscr__ab.lock, 0);
    return panic;
}

/* The panic rule's numbers (desc.panic_*); presses 0 turns it off. */
static void psyscr__panic_config(int32_t presses, int32_t window_ms, int32_t grace_ms) {
    psyscr__ab.presses = presses;
    psyscr__ab.window_ns = (int64_t)window_ms * 1000000;
    psyscr__ab.grace_ns = (int64_t)grace_ms * 1000000;
    memset(psyscr__ab.press, 0, sizeof psyscr__ab.press);
}

static const char* psyscr__abort_open(psyscr_screen* s, const psyscr_desc* d) {
    uint32_t key = d->abort_keys.key ? d->abort_keys.key : PSYSCR_KEY_ESCAPE;
    uint32_t m = d->abort_keys.mods;
    int32_t off = d->abort_keys.off ? 1 : 0;
    if (m & ~(PSYSCR_MOD_SHIFT | PSYSCR_MOD_CTRL | PSYSCR_MOD_ALT | PSYSCR_MOD_GUI | PSYSCR_MOD_NONE))
        return "desc.abort_keys.mods has bits that are not PSYSCR_MOD_*";
    m = m == 0 ? PSYSCR_MOD_SHIFT : (m & PSYSCR_MOD_NONE) ? 0u : m;
    if (psyscr__ab.users > 0 && (psyscr__ab.key != key || psyscr__ab.mods != m || psyscr__ab.off != off))
        return "desc.abort_keys differs from another open screen's: there is one combination per process";
    psyscr__ab.key = key;
    psyscr__ab.mods = m;
    psyscr__ab.off = off;
    psyscr__ab.users++;
    s->abort_user = 1;
    s->abort_seen = psyscr__a_load(&psyscr__ab.seq);   /* not the aborts of before */
    return NULL;
}

/* Keys the panic watchdog's hook can name from an SDL keycode: Esc, F1 to
 * F12 (SDLK_F1 is 0x4000003A), a letter, a digit. */
static int psyscr__vk_known(uint32_t key) {
    return key == PSYSCR_KEY_ESCAPE || (key >= 0x4000003Au && key <= 0x40000045u) ||
           (key >= 'a' && key <= 'z') || (key >= '0' && key <= '9');
}

static void psyscr__abort_close(psyscr_screen* s) {
    if (s->abort_user && --psyscr__ab.users == 0) psyscr__panic_config(0, 0, 0);
    s->abort_user = 0;
}

static int psyscr__abort_pending(const psyscr_screen* s) {
    return psyscr__a_load(&psyscr__ab.seq) != s->abort_seen;
}

/* Reports the log entries after the screen's last one: f gets the reasons,
 * the ring one PSYSCR_EV_ABORT record each. Returns 1 if there were any. */
static int psyscr__abort_take(psyscr_screen* s, psyscr_frame* f) {
    int32_t cur = psyscr__a_load(&psyscr__ab.seq), k;
    uint32_t mask = 0;
    int32_t presses = 0;
    int64_t first = 0, now;
    if (cur == s->abort_seen) return 0;
    now = psyscr__now();
    for (k = s->abort_seen + 1; k - cur <= 0; k++) {
        psyscr__abort_entry* e = &psyscr__ab.log[(uint32_t)k % PSYSCR__ABORT_LOG];
        psyscr__abort_entry c;
        int32_t s1 = psyscr__a_load(&e->seq);
        if (s1 - k < 0) break;              /* still being written: the next begin() */
        c.reason = e->reason; c.flags = e->flags; c.key = e->key; c.mods = e->mods; c.t = e->t;
        if (s1 != k || psyscr__a_load(&e->seq) != k) continue;   /* written over: lost */
        mask |= c.reason;
        if (c.reason & PSYSCR_ABORT_KEY) presses++;
        if (!first) first = c.t ? c.t : now;
        if (s->ring) {
            psyrt_event ev;
            memset(&ev, 0, sizeof ev);
            ev.source = (uint16_t)PSYRT_SRC_SCREEN;
            ev.kind = (uint16_t)PSYSCR_EV_ABORT;
            ev.t_ns = (uint64_t)(c.t ? c.t : now);
            ev.aux = s->display_index;
            ev.u.u32[0] = c.reason;
            ev.u.u32[1] = c.flags;
            ev.u.u32[2] = c.key;
            ev.u.u32[3] = c.mods;
            ev.u.i64[2] = now;
            psyrt_ring_push(s->ring, &ev);
        }
    }
    s->abort_seen = k - 1;
    if (!mask) return 0;
    psyscr__a_store64(&psyscr__ab.ack, now);
    memset(f, 0, sizeof *f);
    f->abort = mask;
    f->abort_presses = presses;
    f->abort_ns = first;
    return 1;
}

/* Open screens with a window, for the close request's window id and for
 * the panic watchdog. Written by open() and close(), read on other threads
 * only to compare ids and, in a panic, to find rings and switched modes. */
#define PSYSCR__SLOTS 8
static struct { psyscr_screen* s; uint32_t win; } psyscr__slot[PSYSCR__SLOTS];

static void psyscr__slot_take(psyscr_screen* s, uint32_t win) {
    int i;
    for (i = 0; i < PSYSCR__SLOTS; i++)
        if (!psyscr__slot[i].s) { psyscr__slot[i].win = win; psyscr__slot[i].s = s; s->abort_slot = i + 1; return; }
}

static void psyscr__slot_free(psyscr_screen* s) {
    if (s->abort_slot) { psyscr__slot[s->abort_slot - 1].s = NULL; psyscr__slot[s->abort_slot - 1].win = 0; }
    s->abort_slot = 0;
}

PSYSCR_API void psyscr_request_abort(void) {
    psyscr__abort_push(PSYSCR_ABORT_REQUEST, 0, 0, 0, psyscr__now());
}

/* --- the default window icon (WINDOW ICON) ---------------------------------
 * Escher's impossible cube, drawn for this header: public domain like the
 * rest. Each byte is a run, (length - 1) << 4 | palette index; index 0 is
 * transparent. */
static const uint8_t psyscr__icon_pal[5][4] = {
    {0,0,0,0}, {38,42,56,255}, {232,234,240,255}, {150,162,186,255}, {84,96,126,255}
};
static const uint8_t psyscr__icon_rle[1292] = {
    0x40,0x03,0x01,0x13,0x90,0x23,0x11,0x43,0x40,0x13,0x01,0x03,0x01,0x00,0x41,0x13,0x01,0x00,0x13,0x01,0x00,0x03,0x01,0x50,
    0x03,0x11,0x00,0x23,0x00,0x03,0x01,0x30,0x13,0x01,0x03,0x01,0x00,0x21,0x00,0x03,0x01,0x00,0x33,0x01,0x00,0x03,0x01,0x00,
    0x03,0x01,0x10,0x03,0x01,0x00,0x01,0x13,0x01,0x10,0x03,0x01,0x00,0x03,0x01,0x10,0x03,0x01,0x10,0x01,0x03,0x01,0x10,0x03,
    0x01,0x00,0x03,0x01,0x10,0x03,0x01,0x20,0x03,0x01,0x10,0x03,0x01,0x00,0x03,0x01,0x10,0x03,0x01,0x20,0x03,0x01,0x10,0x03,
    0x01,0x00,0x03,0x01,0x00,0x43,0x00,0x03,0x01,0x10,0x03,0x01,0x00,0x03,0x01,0x03,0x41,0x00,0x03,0x01,0x00,0x13,0x01,0x00,
    0x03,0x11,0x50,0x03,0x01,0x00,0x13,0x10,0x13,0x60,0x03,0x01,0x13,0x01,0x10,0x11,0x43,0x10,0x03,0x01,0x03,0x01,0x40,0x31,
    0x33,0x11,0x20,0xa0,0x21,0xf0,0xb0,0x01,0x02,0x03,0x51,0xf0,0x40,0x21,0x02,0x03,0x01,0x42,0x41,0xe0,0x01,0x12,0x01,0x02,
    0x03,0x01,0x43,0x42,0x41,0x80,0x01,0x02,0x13,0x01,0x02,0x03,0x41,0x53,0x32,0x11,0x60,0x01,0x02,0x13,0x11,0x02,0x03,0x01,
    0x30,0x41,0x33,0x01,0x12,0x03,0x01,0x20,0x11,0x02,0x13,0x01,0x00,0x01,0x02,0x03,0x01,0x80,0x31,0x02,0x23,0x01,0x10,0x11,
    0x02,0x03,0x11,0x10,0x01,0x02,0x03,0x01,0x90,0x01,0x12,0x03,0x11,0x03,0x01,0x00,0x01,0x12,0x41,0x00,0x01,0x02,0x03,0x01,
    0x80,0x01,0x02,0x13,0x11,0x02,0x03,0x01,0x00,0x01,0x13,0x42,0x00,0x01,0x02,0x03,0x01,0x70,0x01,0x02,0x13,0x01,0x00,0x01,
    0x02,0x03,0x01,0x00,0x21,0x43,0x00,0x01,0x02,0x03,0x01,0x00,0x31,0x00,0x11,0x02,0x13,0x01,0x10,0x01,0x02,0x03,0x01,0x00,
    0x01,0x02,0x03,0x41,0x00,0x01,0x02,0x03,0x01,0x00,0x32,0x11,0x02,0x03,0x11,0x20,0x01,0x02,0x03,0x01,0x00,0x01,0x02,0x03,
    0x01,0x40,0x01,0x02,0x03,0x01,0x00,0x33,0x01,0x02,0x03,0x01,0x40,0x01,0x02,0x03,0x01,0x00,0x01,0x02,0x03,0x01,0x40,0x01,
    0x02,0x03,0x01,0x00,0x21,0x03,0x01,0x02,0x03,0x01,0x40,0x01,0x02,0x03,0x01,0x00,0x01,0x02,0x03,0x01,0x40,0x01,0x02,0x03,
    0x01,0x30,0x11,0x02,0x03,0x01,0x40,0x01,0x02,0x03,0x01,0x00,0x01,0x02,0x03,0x01,0x40,0x01,0x02,0x03,0x01,0x40,0x01,0x02,
    0x03,0x01,0x40,0x01,0x02,0x03,0x01,0x00,0x01,0x02,0x03,0x01,0x40,0x01,0x02,0x03,0x01,0x40,0x01,0x02,0x03,0x01,0x40,0x01,
    0x02,0x03,0x01,0x00,0x01,0x02,0x03,0x01,0x40,0x01,0x02,0x03,0x01,0x40,0x01,0x02,0x03,0x01,0x40,0x01,0x02,0x03,0x01,0x00,
    0x01,0x02,0x03,0x01,0x40,0x01,0x02,0x03,0x01,0x40,0x01,0x02,0x03,0x01,0x40,0x01,0x02,0x03,0x01,0x00,0x01,0x02,0x03,0x01,
    0x40,0x01,0x02,0x03,0x41,0x00,0x01,0x02,0x03,0x01,0x40,0x01,0x02,0x03,0x01,0x00,0x01,0x02,0x03,0x01,0x30,0x01,0x02,0x23,
    0x32,0x00,0x01,0x02,0x03,0x01,0x40,0x01,0x02,0x03,0x01,0x00,0x01,0x02,0x03,0x01,0x10,0x11,0x02,0x13,0x01,0x43,0x00,0x01,
    0x02,0x03,0x01,0x00,0x41,0x02,0x03,0x01,0x00,0x01,0x02,0x03,0x01,0x00,0x01,0x12,0x03,0x11,0x00,0x41,0x00,0x01,0x02,0x03,
    0x01,0x00,0x32,0x11,0x03,0x01,0x00,0x01,0x02,0x03,0x11,0x02,0x13,0x01,0x80,0x01,0x02,0x03,0x01,0x00,0x23,0x01,0x12,0x03,
    0x01,0x00,0x01,0x02,0x03,0x01,0x02,0x13,0x01,0x90,0x01,0x02,0x03,0x01,0x00,0x21,0x02,0x13,0x20,0x31,0x13,0x01,0xa0,0x01,
    0x02,0x03,0x01,0x00,0x11,0x02,0x13,0x01,0x20,0x01,0x12,0x41,0x90,0x01,0x02,0x03,0x11,0x12,0x03,0x11,0x40,0x13,0x42,0x41,
    0x40,0x01,0x02,0x03,0x01,0x02,0x13,0x01,0x60,0x01,0x53,0x42,0x31,0x00,0x01,0x02,0x03,0x01,0x13,0x01,0x80,0x41,0x53,0x32,
    0x11,0x02,0x03,0x01,0x03,0x01,0xe0,0x41,0x43,0x02,0x01,0x02,0x03,0x11,0xf0,0x40,0x41,0x23,0x01,0x90,0xf0,0xf0,0xf0,0xf0,
    0x00,0x01,0xf0,0xf0,0xd0,0x01,0x02,0x41,0xf0,0xf0,0x60,0x11,0x62,0x41,0xf0,0xf0,0x00,0x01,0x32,0x04,0x33,0x42,0x41,0xf0,
    0xa0,0x01,0x32,0x14,0x83,0x42,0x41,0xf0,0x40,0x01,0x32,0x24,0xd3,0x42,0x41,0xd0,0x11,0x22,0x44,0x31,0xe3,0x42,0x11,0xa0,
    0x01,0x32,0x34,0x11,0x04,0x01,0x10,0x41,0x83,0x11,0x32,0x04,0x01,0x90,0x01,0x32,0x34,0x11,0x03,0x04,0x01,0x60,0x41,0x23,
    0x11,0x32,0x14,0x01,0x80,0x01,0x32,0x24,0x11,0x23,0x04,0x01,0xb0,0x31,0x32,0x24,0x01,0x70,0x01,0x32,0x01,0x14,0x01,0x00,
    0x01,0x23,0x04,0x01,0xc0,0x11,0x22,0x44,0x01,0x50,0x11,0x32,0x31,0x10,0x01,0x23,0x04,0x01,0xb0,0x01,0x32,0x54,0x01,0x50,
    0x01,0x23,0x52,0x11,0x33,0x04,0x01,0xa0,0x01,0x32,0x34,0x11,0x04,0x01,0x50,0x01,0x73,0x22,0x33,0x04,0x11,0x80,0x01,0x32,
    0x24,0x11,0x13,0x04,0x01,0x50,0x01,0xe3,0x14,0x02,0x41,0x20,0x01,0x22,0x34,0x11,0x23,0x04,0x01,0x50,0x01,0xe3,0x14,0x03,
    0x42,0x21,0x22,0x34,0x01,0x00,0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x41,0x63,0x14,0x53,0x42,0x34,0x01,0x10,0x01,0x23,0x04,
    0x01,0x50,0x01,0x23,0x04,0x01,0x20,0x21,0x33,0x14,0x93,0x24,0x11,0x20,0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x04,0x01,0x50,
    0x01,0x23,0x14,0x01,0x83,0x14,0x01,0x40,0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x04,0x01,0x00,0x41,
    0x33,0x04,0x01,0x50,0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x04,0x01,0x50,
    0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x04,0x01,
    0x50,0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x04,
    0x01,0x50,0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x04,0x01,0x50,0x01,0x23,
    0x04,0x01,0x50,0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x04,0x01,0x50,0x01,
    0x23,0x04,0x01,0x50,0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x04,0x02,0x41,0x00,0x01,0x23,0x04,0x01,
    0x50,0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x04,0x01,0x40,0x01,0x22,0x04,0x13,0x42,0x11,0x23,0x04,0x01,0x50,0x01,0x23,0x04,
    0x01,0x50,0x01,0x23,0x04,0x01,0x20,0x11,0x22,0x14,0x63,0x11,0x23,0x04,0x31,0x20,0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x04,
    0x01,0x10,0x01,0x32,0x24,0x63,0x11,0x23,0x04,0x11,0x12,0x31,0x23,0x04,0x01,0x50,0x01,0x23,0x04,0x01,0x00,0x01,0x32,0x24,
    0x21,0x43,0x11,0x23,0x04,0x11,0x13,0x22,0x01,0x23,0x04,0x01,0x50,0x01,0x23,0x04,0x11,0x32,0x24,0x01,0x20,0x61,0x23,0x04,
    0x11,0x33,0x01,0x12,0x13,0x04,0x01,0x50,0x01,0x23,0x04,0x11,0x12,0x34,0x01,0x80,0x11,0x23,0x04,0x11,0x13,0x11,0x32,0x14,
    0x01,0x50,0x01,0x23,0x04,0x11,0x02,0x34,0x01,0xa0,0x01,0x23,0x04,0x41,0x32,0x24,0x01,0x50,0x01,0x23,0x04,0x11,0x34,0x01,
    0xb0,0x01,0x23,0x04,0x01,0x10,0x01,0x32,0x24,0x11,0x50,0x01,0x23,0x14,0x41,0xc0,0x01,0x23,0x04,0x01,0x00,0x01,0x32,0x24,
    0x01,0x70,0x01,0x23,0x32,0x31,0xb0,0x01,0x23,0x04,0x11,0x22,0x34,0x01,0x80,0x01,0x53,0x42,0x41,0x60,0x01,0x23,0x04,0x11,
    0x12,0x34,0x01,0x90,0x01,0xa3,0x42,0x41,0x10,0x01,0x23,0x04,0x11,0x02,0x34,0x01,0xa0,0x11,0xe3,0x42,0x21,0x23,0x04,0x01,
    0x02,0x24,0x11,0xd0,0x41,0xe3,0x12,0x33,0x44,0x01,0xf0,0x40,0x41,0xf3,0x34,0x01,0xf0,0xa0,0x41,0xa3,0x24,0x01,0xf0,0xf0,
    0x00,0x41,0x53,0x04,0x11,0xf0,0xf0,0x60,0x41,0x03,0x01,0xf0,0xf0,0xd0,0x01,0xf0,0xf0,0xf0,0xf0,0x00
};
/* side, offset and length of each size in psyscr__icon_rle */
static const uint16_t psyscr__icon_size[3][3] = { { 16, 0, 147 }, { 32, 147, 497 }, { 48, 644, 648 } };

/* Size k of the icon (0: 16, 1: 32, 2: 48 pixels) as RGBA rows of pitch
 * bytes. Returns 1 when the runs fill the square exactly. */
static int psyscr__icon_decode(int k, uint8_t* px, int pitch) {
    int side = psyscr__icon_size[k][0], i, n = 0;
    const uint8_t* r = psyscr__icon_rle + psyscr__icon_size[k][1];
    for (i = 0; i < psyscr__icon_size[k][2]; i++) {
        int run = (r[i] >> 4) + 1, j;
        const uint8_t* c = psyscr__icon_pal[r[i] & 15u];
        if ((r[i] & 15u) >= sizeof psyscr__icon_pal / sizeof psyscr__icon_pal[0] || n + run > side * side) return 0;
        for (j = 0; j < run; j++, n++) memcpy(px + (n / side) * pitch + (n % side) * 4, c, 4);
    }
    return n == side * side;
}

PSYSCR_API const char* psyscr_version(void) { return PSYSCR_VERSION_STRING; }

PSYSCR_API const char* psyscr_strerror(int code) {
    switch (code) {
    case PSYSCR_OK:                  return "ok";
    case PSYSCR_QUIT:                return "quit";
    case PSYSCR_ERR_ARG:             return "bad argument";
    case PSYSCR_ERR_CLOSED:          return "screen not open";
    case PSYSCR_ERR_TIMEOUT:         return "the swap path freed no slot in time";
    case PSYSCR_ERR_LOST:            return "device or window lost";
    case PSYSCR_ERR_ORDER:           return "begin and flip out of turn";
    case PSYSCR_ERR_NOT_IMPLEMENTED: return "not implemented";
    case PSYSCR_ERR_REFUSED:         return "refused";
    default:                         return code > 0 ? "ok" : "unknown error";
    }
}

/* --- modes ----------------------------------------------------------------- */

PSYSCR_API int psyscr_mode_multiple_in(const psyscr_mode* modes, int n,
                                       int32_t num, int32_t den, int32_t w, int32_t h,
                                       double tol_ppm, psyscr_mode* out, double* err_ppm) {
    int i, best = -1, nearest = -1, best_k = 0;
    double rate, best_f = 0.0, nearest_err = 1e300;
    if (!modes || n < 0 || num <= 0 || den <= 0 || tol_ppm < 0) return PSYSCR_ERR_ARG;
    if (tol_ppm == 0) tol_ppm = 200.0;
    rate = (double)num / (double)den;
    for (i = 0; i < n; i++) {
        const psyscr_mode* m = &modes[i];
        double f, k, e;
        if (m->refresh_num <= 0 || m->refresh_den <= 0) continue;
        if ((w && m->w != w) || (h && m->h != h)) continue;
        f = (double)m->refresh_num / (double)m->refresh_den;
        k = floor(f / rate + 0.5);
        if (k < 1) k = 1;
        e = (f / (k * rate) - 1.0) * 1e6;
        if (fabs(e) < nearest_err) { nearest_err = fabs(e); nearest = i; }
        if (fabs(e) <= tol_ppm && f > best_f) { best_f = f; best = i; best_k = (int)k; }
    }
    if (best >= 0) {
        double f = (double)modes[best].refresh_num / (double)modes[best].refresh_den;
        if (out) *out = modes[best];
        if (err_ppm) *err_ppm = (f / (best_k * rate) - 1.0) * 1e6;
        return best_k;
    }
    if (nearest >= 0) {
        double f = (double)modes[nearest].refresh_num / (double)modes[nearest].refresh_den;
        double k = floor(f / rate + 0.5);
        if (k < 1) k = 1;
        if (out) *out = modes[nearest];
        if (err_ppm) *err_ppm = (f / (k * rate) - 1.0) * 1e6;
    }
    return PSYSCR_ERR_REFUSED;
}

#if !defined(PSYSCR_NO_SDL)

static int64_t psyscr__period_of(int32_t num, int32_t den) {
    if (num <= 0 || den <= 0) return 0;
    return ((int64_t)den * 1000000000 + num / 2) / num;
}

static void psyscr__mode_from_sdl(const SDL_DisplayMode* m, psyscr_mode* out) {
    memset(out, 0, sizeof *out);
    out->w = m->w;
    out->h = m->h;
    out->refresh_num = m->refresh_rate_numerator;
    out->refresh_den = m->refresh_rate_denominator;
    if (out->refresh_num <= 0 && m->refresh_rate > 0) {
        out->refresh_num = (int32_t)(m->refresh_rate * 1000.0f + 0.5f);
        out->refresh_den = 1000;
    }
    out->period_ns = psyscr__period_of(out->refresh_num, out->refresh_den);
    out->format = (uint32_t)m->format;
    out->density = m->pixel_density;
}

/* SDL's video subsystem is reference counted, so a call that needs it holds
 * it only for its own duration. */
static bool psyscr__video_up(void) { return SDL_InitSubSystem(SDL_INIT_VIDEO); }
static void psyscr__video_down(void) { SDL_QuitSubSystem(SDL_INIT_VIDEO); }

static SDL_DisplayID psyscr__display_id(uint32_t display) {
    return display ? (SDL_DisplayID)display : SDL_GetPrimaryDisplay();
}

PSYSCR_API int psyscr_displays(psyscr_display_info* out, int cap) {
    int i, n = 0, count = 0;
    SDL_DisplayID* ids;
    SDL_DisplayID primary;
    if (cap < 0 || (cap > 0 && !out)) return PSYSCR_ERR_ARG;
    if (!psyscr__video_up()) return PSYSCR_ERR_LOST;
    ids = SDL_GetDisplays(&n);
    primary = SDL_GetPrimaryDisplay();
    for (i = 0; ids && i < n; i++) {
        if (count < cap) {
            psyscr_display_info* d = &out[count];
            SDL_Rect r;
            const SDL_DisplayMode* dm = SDL_GetDesktopDisplayMode(ids[i]);
            memset(d, 0, sizeof *d);
            d->id = (uint32_t)ids[i];
            psyscr__copy(d->name, sizeof d->name, SDL_GetDisplayName(ids[i]));
            if (SDL_GetDisplayBounds(ids[i], &r)) { d->x = r.x; d->y = r.y; d->w = r.w; d->h = r.h; }
            if (dm) psyscr__mode_from_sdl(dm, &d->desktop);
            d->primary = ids[i] == primary;
        }
        count++;
    }
    SDL_free(ids);
    psyscr__video_down();
    return count;
}

PSYSCR_API int psyscr_modes(uint32_t display, psyscr_mode* out, int cap) {
    int i, n = 0;
    SDL_DisplayMode** modes;
    if (cap < 0 || (cap > 0 && !out)) return PSYSCR_ERR_ARG;
    if (!psyscr__video_up()) return PSYSCR_ERR_LOST;
    modes = SDL_GetFullscreenDisplayModes(psyscr__display_id(display), &n);
    if (!modes) { psyscr__video_down(); return PSYSCR_ERR_ARG; }
    for (i = 0; i < n && i < cap; i++) psyscr__mode_from_sdl(modes[i], &out[i]);
    SDL_free(modes);
    psyscr__video_down();
    return n;
}

PSYSCR_API int psyscr_mode_multiple(uint32_t display, int32_t num, int32_t den,
                                    int32_t w, int32_t h, double tol_ppm,
                                    psyscr_mode* out, double* err_ppm) {
    psyscr_mode list[256];
    int n = psyscr_modes(display, list, 256), rc;
    if (n < 0) return n;
    if (n > 256) n = 256;
    if (w == 0 && h == 0) {
        const SDL_DisplayMode* dm;
        if (!psyscr__video_up()) return PSYSCR_ERR_LOST;
        dm = SDL_GetDesktopDisplayMode(psyscr__display_id(display));
        if (dm) { w = dm->w; h = dm->h; }
        psyscr__video_down();
    }
    rc = psyscr_mode_multiple_in(list, n, num, den, w, h, tol_ppm, out, err_ppm);
    return rc;
}

#else /* PSYSCR_NO_SDL */

PSYSCR_API int psyscr_displays(psyscr_display_info* out, int cap) {
    (void)out; (void)cap; return PSYSCR_ERR_NOT_IMPLEMENTED;
}
PSYSCR_API int psyscr_modes(uint32_t display, psyscr_mode* out, int cap) {
    (void)display; (void)out; (void)cap; return PSYSCR_ERR_NOT_IMPLEMENTED;
}
PSYSCR_API int psyscr_mode_multiple(uint32_t display, int32_t num, int32_t den,
                                    int32_t w, int32_t h, double tol_ppm,
                                    psyscr_mode* out, double* err_ppm) {
    (void)display; (void)num; (void)den; (void)w; (void)h; (void)tol_ppm;
    (void)out; (void)err_ppm;
    return PSYSCR_ERR_NOT_IMPLEMENTED;
}

#endif /* PSYSCR_NO_SDL */

/* --- SIM presenter --------------------------------------------------------- */

typedef struct psyscr__sim {
    int64_t       t0, period;
    uint64_t      pend_id;
    int64_t       pend_count;
    int           has_pend;
    psyscr_vblank done;
    int           has_done;
} psyscr__sim;

typedef char psyscr__sim_fits[sizeof(psyscr__sim) <= sizeof(((psyscr_screen*)0)->backend_mem) ? 1 : -1];

static int64_t psyscr__sim_count(const psyscr__sim* m, int64_t t) {
    int64_t d = t - m->t0;
    return d >= 0 ? d / m->period : -((-d + m->period - 1) / m->period);
}

static int psyscr__sim_open(void* ctx, const psyscr_presenter_open* in, psyscr_caps* caps,
                            char* err, size_t err_cap) {
    psyscr__sim* m = (psyscr__sim*)ctx;
    (void)err; (void)err_cap;
    memset(m, 0, sizeof *m);
    m->period = in->sim_period_ns > 0 ? in->sim_period_ns : 16666667;
    m->t0 = psyscr__now();
    caps->kind = PSYSCR_FIXED_GRID;
    caps->period_ns = m->period;
    caps->native_target = true;
    caps->hw_onset = true;
    caps->max_in_flight = 1;
    caps->mode.refresh_num = 1000000000;
    caps->mode.refresh_den = (int32_t)(m->period > 0x7fffffff ? 0x7fffffff : m->period);
    caps->mode.period_ns = m->period;
    return PSYSCR_OK;
}

static void psyscr__sim_close(void* ctx) { (void)ctx; }

static int psyscr__sim_acquire(void* ctx, int64_t deadline_ns, psyscr_vblank* newest) {
    psyscr__sim* m = (psyscr__sim*)ctx;
    int64_t c;
    (void)deadline_ns;
    if (m->has_pend) {
        int64_t t = m->t0 + m->pend_count * m->period;
        if (t > psyscr__now()) PSYSCR__SLEEP_UNTIL((int64_t)t, PSYRT_DEFAULT_SPIN_NS);
        m->done.present_id = m->pend_id;
        m->done.t_ns = t;
        m->done.count = m->pend_count;
        m->done.path = PSYSCR_PATH_SIMULATED;
        m->done.flags = 0;
        m->has_done = 1;
        m->has_pend = 0;
    }
    c = psyscr__sim_count(m, psyscr__now());
    newest->present_id = 0;
    newest->count = c;
    newest->t_ns = m->t0 + c * m->period;
    newest->path = PSYSCR_PATH_SIMULATED;
    newest->flags = 0;
    return PSYSCR_OK;
}

static int psyscr__sim_present(void* ctx, const psyscr_present_req* req) {
    psyscr__sim* m = (psyscr__sim*)ctx;
    int64_t next = psyscr__sim_count(m, psyscr__now()) + 1;
    m->pend_id = req->present_id;
    m->pend_count = req->target_count > next ? req->target_count : next;
    m->has_pend = 1;
    return PSYSCR_OK;
}

static int psyscr__sim_completions(void* ctx, psyscr_vblank* out, int cap) {
    psyscr__sim* m = (psyscr__sim*)ctx;
    if (!m->has_done || cap < 1) return 0;
    out[0] = m->done;
    m->has_done = 0;
    return 1;
}

static int psyscr__sim_describe(void* ctx, char* buf, size_t cap) {
    psyscr__sim* m = (psyscr__sim*)ctx;
    return snprintf(buf, cap, "sim_period=%lldns", (long long)m->period);
}

static const psyscr_presenter psyscr__sim_presenter = {
    PSYSCR_PRESENTER_VERSION, "sim", false, false, false,
    psyscr__sim_open, psyscr__sim_close, psyscr__sim_acquire, psyscr__sim_present,
    psyscr__sim_completions, NULL, NULL, psyscr__sim_describe, NULL
};

/* --- DXGI flip presenter (Windows 10 and 11) ------------------------------ */

#if defined(PSYSCR__DXGI)

/* COM from C and from C++ without CINTERFACE, so the implementation file
 * may include other COM headers in either language. */
#ifdef __cplusplus
    #define PSYSCR__CALL(o, m, ...) ((o)->m(__VA_ARGS__))
    #define PSYSCR__CALL0(o, m)     ((o)->m())
    #define PSYSCR__IID(x)          (x)
#else
    #define PSYSCR__CALL(o, m, ...) ((o)->lpVtbl->m((o), __VA_ARGS__))
    #define PSYSCR__CALL0(o, m)     ((o)->lpVtbl->m(o))
    #define PSYSCR__IID(x)          (&(x))
#endif
#define PSYSCR__RELEASE(o) do { if (o) { PSYSCR__CALL0((o), Release); (o) = NULL; } } while (0)

/* Own copies of the IIDs, so nothing links dxguid. */
static const IID psyscr__IID_IDXGIFactory1 = {0x770aae78,0xf26f,0x4dba,{0xa8,0x29,0x25,0x3c,0x83,0xd1,0xb3,0x87}};
static const IID psyscr__IID_IDXGIFactory2 = {0x50c83a1c,0xe072,0x4c48,{0x87,0xb0,0x36,0x30,0xfa,0x36,0xa6,0xd0}};
static const IID psyscr__IID_IDXGIFactory5 = {0x7632e1f5,0xee65,0x4dca,{0x87,0xfd,0x84,0xcd,0x75,0xf8,0x83,0x8d}};
static const IID psyscr__IID_IDXGIDevice   = {0x54ec77fa,0x1377,0x44e6,{0x8c,0x32,0x88,0xfd,0x5f,0x44,0xc8,0x4c}};
static const IID psyscr__IID_IDXGISwapChain2 = {0xa8be2ac4,0x199f,0x4946,{0xb3,0x31,0x79,0x59,0x9f,0xb9,0x8d,0xe7}};
static const IID psyscr__IID_IDXGISwapChainMedia = {0xdd95b90b,0xf05f,0x4f6a,{0xbd,0x65,0x25,0xbf,0xb2,0x64,0xbd,0x84}};
static const IID psyscr__IID_ID3D11DeviceContext1 = {0xbb2c6faa,0xb5fb,0x4082,{0x8e,0x6b,0x38,0x8b,0x8c,0xfa,0x90,0xe1}};
static const IID psyscr__IID_ID3D11Texture2D = {0x6f15aaf2,0xd208,0x4e89,{0x9a,0xb4,0x48,0x95,0x35,0xd3,0x4f,0x9c}};

static const IID psyscr__IID_ID3D11Device5 = {0x8ffde202,0xa0e7,0x45df,{0x9e,0x01,0xe8,0x37,0x80,0x1b,0x5e,0xa0}};
static const IID psyscr__IID_ID3D11DeviceContext4 = {0x917600da,0xf58c,0x4c33,{0x98,0xd8,0x3e,0x15,0xb3,0x90,0xfa,0x24}};
static const IID psyscr__IID_ID3D11Fence = {0xaffde9d1,0x1df7,0x4bb7,{0x8a,0x34,0x0f,0x46,0x25,0x1d,0xab,0x80}};
/* ID3D10Multithread has the same IID and the same slots. */
static const IID psyscr__IID_ID3D11Multithread = {0x9b7e4e00,0x342c,0x4106,{0xa1,0x9f,0x4f,0x27,0x04,0xf6,0x89,0xf0}};

/* desc.d3d11_video: a device a decoder can share (NATIVE HANDLES). */
static UINT psyscr__device_flags(const psyscr_presenter_open* in) {
    return (UINT)D3D11_CREATE_DEVICE_BGRA_SUPPORT | (in->d3d11_video ? (UINT)D3D11_CREATE_DEVICE_VIDEO_SUPPORT : 0u);
}

static void psyscr__device_error(const psyscr_presenter_open* in, HRESULT hr, char* err, size_t err_cap) {
    if (in->d3d11_video)
        psyscr__set_error(err, err_cap, "psy_screen: desc.d3d11_video: D3D11CreateDevice with VIDEO_SUPPORT failed "
                          "0x%08lx; the adapter may have no D3D11 video support", (unsigned long)hr);
    else
        psyscr__set_error(err, err_cap, "psy_screen: D3D11CreateDevice 0x%08lx", (unsigned long)hr);
}

/* A decoder thread then shares the immediate context with the frame
 * thread; protection takes a lock on each call of either. */
static void psyscr__device_protect(const psyscr_presenter_open* in, ID3D11Device* dev) {
    ID3D11Multithread* mt = NULL;
    if (!in->d3d11_video) return;
    if (SUCCEEDED(PSYSCR__CALL(dev, QueryInterface, PSYSCR__IID(psyscr__IID_ID3D11Multithread), (void**)&mt)) && mt) {
        PSYSCR__CALL(mt, SetMultithreadProtected, TRUE);
        PSYSCR__RELEASE(mt);
    }
}

/* Codes, the read-back and the GPU fence: the same on both Windows swap
 * paths, so one copy. */
#define PSYSCR__STAGE_W 8192
typedef struct psyscr__d3dcodes {
    ID3D11DeviceContext4*   dctx4;
    ID3D11Fence*            fence;
    ID3D11Texture2D*        staging;         /* PSYSCR__STAGE_W x 1, read back */
    const psyscr_code_draw* staged;          /* what was copied last present   */
    int32_t                 n_staged;
    uint32_t*               checked;
    uint32_t*               failed;
} psyscr__d3dcodes;

/* Rows of up to this many pixels are drawn one ClearView per pixel; longer
 * ones with one UpdateSubresource (measured: see CODES in STATUS). */
static int psyscr__row_clear_max = 8;

static void psyscr__d3d_color(uint32_t v, float c[4]) {
    /* code / 255 in double, rounded once to float: the conversion back to
     * UNORM8 rounds to the code (checked at open by the self test) */
    c[0] = (float)((double)(v & 0xFFu) / 255.0);
    c[1] = (float)((double)((v >> 8) & 0xFFu) / 255.0);
    c[2] = (float)((double)((v >> 16) & 0xFFu) / 255.0);
    c[3] = 1.0f;
}

static void psyscr__d3d_draw_codes(ID3D11DeviceContext* dc, ID3D11DeviceContext1* dc1, ID3D11RenderTargetView* rtv,
                                   ID3D11Resource* tex, const psyscr_code_draw* codes, int n) {
    int i, k;
    for (i = 0; i < n; i++) {
        const psyscr_code_draw* c = &codes[i];
        D3D11_RECT r;
        float col[4];
        if (!c->px) {
            r.left = c->x; r.top = c->y; r.right = c->x + c->w; r.bottom = c->y + c->h;
            psyscr__d3d_color(c->value, col);
            PSYSCR__CALL(dc1, ClearView, (ID3D11View*)rtv, col, &r, 1);
        } else if (c->w <= psyscr__row_clear_max || !tex) {
            for (k = 0; k < c->w; k++) {
                r.left = c->x + k; r.top = c->y; r.right = c->x + k + 1; r.bottom = c->y + 1;
                psyscr__d3d_color(c->px[k], col);
                PSYSCR__CALL(dc1, ClearView, (ID3D11View*)rtv, col, &r, 1);
            }
        } else {
            uint32_t row[PSYSCR_CODE_ROW_PIXELS];
            D3D11_BOX b;
            int w = c->w < PSYSCR_CODE_ROW_PIXELS ? c->w : PSYSCR_CODE_ROW_PIXELS;
            for (k = 0; k < w; k++) row[k] = 0xFF000000u | (c->px[k] & 0xFFFFFFu);   /* RGBA8 in memory */
            b.left = (UINT)c->x; b.right = (UINT)(c->x + w); b.top = (UINT)c->y; b.bottom = (UINT)(c->y + 1);
            b.front = 0; b.back = 1;
            PSYSCR__CALL(dc, UpdateSubresource, tex, 0, &b, row, (UINT)(w * 4), 0);
        }
    }
}

/* The read-back: the first row of each code is copied to a staging texture
 * on one present and mapped on the next, when the GPU has long finished,
 * so the frame loop never waits on it. */
static void psyscr__d3d_verify_check(ID3D11DeviceContext* dc, psyscr__d3dcodes* q) {
    D3D11_MAPPED_SUBRESOURCE m;
    int i, k, x = 0;
    if (!q->staged || !q->staging) return;
    if (SUCCEEDED(PSYSCR__CALL(dc, Map, (ID3D11Resource*)q->staging, 0, D3D11_MAP_READ, 0, &m))) {
        const uint32_t* row = (const uint32_t*)m.pData;
        for (i = 0; i < q->n_staged; i++) {
            const psyscr_code_draw* c = &q->staged[i];
            int w = c->px ? c->w : (c->w < 64 ? c->w : 64), bad = 0;
            for (k = 0; k < w && x + k < PSYSCR__STAGE_W; k++) {
                uint32_t want = c->px ? c->px[k] : c->value;
                if ((row[x + k] & 0xFFFFFFu) != (want & 0xFFFFFFu)) bad = 1;
            }
            x += w;
            if (q->checked) (*q->checked)++;
            if (bad && q->failed) (*q->failed)++;
        }
        PSYSCR__CALL(dc, Unmap, (ID3D11Resource*)q->staging, 0);
    }
    q->staged = NULL;
}

static void psyscr__d3d_verify_copy(ID3D11DeviceContext* dc, psyscr__d3dcodes* q, ID3D11Resource* tex,
                                    const psyscr_code_draw* codes, int n) {
    int i, x = 0;
    if (!q->staging || !tex || n < 1) return;
    for (i = 0; i < n; i++) {
        const psyscr_code_draw* c = &codes[i];
        int w = c->px ? c->w : (c->w < 64 ? c->w : 64);
        D3D11_BOX b;
        if (x + w > PSYSCR__STAGE_W) w = PSYSCR__STAGE_W - x;
        if (w <= 0) break;
        b.left = (UINT)c->x; b.right = (UINT)(c->x + w); b.top = (UINT)c->y; b.bottom = (UINT)(c->y + 1);
        b.front = 0; b.back = 1;
        PSYSCR__CALL(dc, CopySubresourceRegion, (ID3D11Resource*)q->staging, 0, (UINT)x, 0, 0, tex, 0, &b);
        x += w;
    }
    q->staged = codes;
    q->n_staged = n;
}

/* Open: the fence, the staging texture, and the self test: every value of
 * every channel through both drawing paths, read back. */
static void psyscr__d3d_codes_open(ID3D11Device* dev, ID3D11DeviceContext* dc, ID3D11DeviceContext1* dc1,
                                   const psyscr_presenter_open* in, psyscr__d3dcodes* q) {
    ID3D11Device5* dev5 = NULL;
    memset(q, 0, sizeof *q);
    q->checked = in->code_checked;
    q->failed = in->code_failed;
    if (in->want_gpu_done) {
        PSYSCR__CALL(dev, QueryInterface, PSYSCR__IID(psyscr__IID_ID3D11Device5), (void**)&dev5);
        PSYSCR__CALL(dc, QueryInterface, PSYSCR__IID(psyscr__IID_ID3D11DeviceContext4), (void**)&q->dctx4);
        if (dev5 && q->dctx4)
            PSYSCR__CALL(dev5, CreateFence, 0, D3D11_FENCE_FLAG_NONE, PSYSCR__IID(psyscr__IID_ID3D11Fence), (void**)&q->fence);
        PSYSCR__RELEASE(dev5);
        if (!q->fence) PSYSCR__RELEASE(q->dctx4);
    }
    if (in->n_codes > 0 && dc1) {
        D3D11_TEXTURE2D_DESC td;
        ID3D11Texture2D *t = NULL, *st = NULL;
        ID3D11RenderTargetView* rv = NULL;
        memset(&td, 0, sizeof td);
        td.Width = PSYSCR__STAGE_W; td.Height = 1; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_STAGING; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        PSYSCR__CALL(dev, CreateTexture2D, &td, NULL, &q->staging);
        /* self test on a 256 x 2 target: row 0 by ClearView, row 1 by
         * UpdateSubresource, each pixel a different value per channel */
        td.Width = 256; td.Height = 2; td.Usage = D3D11_USAGE_DEFAULT; td.CPUAccessFlags = 0;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        PSYSCR__CALL(dev, CreateTexture2D, &td, NULL, &t);
        td.Usage = D3D11_USAGE_STAGING; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.BindFlags = 0;
        PSYSCR__CALL(dev, CreateTexture2D, &td, NULL, &st);
        if (t) PSYSCR__CALL(dev, CreateRenderTargetView, (ID3D11Resource*)t, NULL, &rv);
        if (t && st && rv && in->code_selftest) {
            uint32_t px[256];
            psyscr_code_draw d[2];
            D3D11_MAPPED_SUBRESOURCE m;
            int i, bad = 0, keep = psyscr__row_clear_max;
            for (i = 0; i < 256; i++)
                px[i] = (uint32_t)i | ((uint32_t)(255 - i) << 8) | ((uint32_t)((i * 37 + 11) & 255) << 16);
            d[0].x = 0; d[0].y = 0; d[0].w = 256; d[0].h = 1; d[0].value = 0; d[0].px = px;
            d[1] = d[0]; d[1].y = 1;
            psyscr__row_clear_max = 256;   /* row 0 by ClearView           */
            psyscr__d3d_draw_codes(dc, dc1, rv, (ID3D11Resource*)t, &d[0], 1);
            psyscr__row_clear_max = 0;     /* row 1 by UpdateSubresource   */
            psyscr__d3d_draw_codes(dc, dc1, rv, (ID3D11Resource*)t, &d[1], 1);
            psyscr__row_clear_max = keep;
            PSYSCR__CALL(dc, CopyResource, (ID3D11Resource*)st, (ID3D11Resource*)t);
            if (SUCCEEDED(PSYSCR__CALL(dc, Map, (ID3D11Resource*)st, 0, D3D11_MAP_READ, 0, &m))) {
                const unsigned char* b = (const unsigned char*)m.pData;
                int y;
                for (y = 0; y < 2; y++) {
                    const uint32_t* row = (const uint32_t*)(b + (size_t)y * m.RowPitch);
                    for (i = 0; i < 256; i++) if ((row[i] & 0xFFFFFFu) != px[i]) bad++;
                }
                PSYSCR__CALL(dc, Unmap, (ID3D11Resource*)st, 0);
                *in->code_selftest = bad ? -1 : 1;
            } else {
                *in->code_selftest = -1;
            }
        }
        PSYSCR__RELEASE(rv);
        PSYSCR__RELEASE(st);
        PSYSCR__RELEASE(t);
    }
}

static void psyscr__d3d_codes_close(psyscr__d3dcodes* q) {
    PSYSCR__RELEASE(q->staging);
    PSYSCR__RELEASE(q->fence);
    PSYSCR__RELEASE(q->dctx4);
    q->staged = NULL;
}

/* After ANGLE's flush: the patch, then the codes, the read-back, and the
 * fence that tells the trigger worker this present's GPU work is done. */
static void psyscr__d3d_finish_frame(ID3D11DeviceContext* dc, ID3D11DeviceContext1* dc1, ID3D11RenderTargetView* rtv,
                                     ID3D11Resource* tex, psyscr__d3dcodes* q, const psyscr_present_req* req) {
    psyscr__d3d_verify_check(dc, q);
    if (req->patch_on && dc1 && rtv) {
        D3D11_RECT r;
        float c[4];
        r.left = req->patch_x; r.top = req->patch_y;
        r.right = req->patch_x + req->patch_w; r.bottom = req->patch_y + req->patch_h;
        c[0] = c[1] = c[2] = req->patch_value; c[3] = 1.0f;
        PSYSCR__CALL(dc1, ClearView, (ID3D11View*)rtv, c, &r, 1);
    }
    if (req->n_codes > 0 && dc1 && rtv) {
        PSYRT_ZONE(z_codes, "psyscr.codes");
        psyscr__d3d_draw_codes(dc, dc1, rtv, tex, req->codes, req->n_codes);
        if (req->verify) psyscr__d3d_verify_copy(dc, q, tex, req->codes, req->n_codes);
        PSYRT_ZONE_END(z_codes);
    }
    if (q->fence) PSYSCR__CALL(q->dctx4, Signal, q->fence, req->present_id);
}

static uint64_t psyscr__d3d_gpu_done(psyscr__d3dcodes* q) {
    return q->fence ? (uint64_t)PSYSCR__CALL0(q->fence, GetCompletedValue) : 0;
}

typedef HRESULT (WINAPI *psyscr__D3D11CreateDevice_fn)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
typedef HRESULT (WINAPI *psyscr__CreateDXGIFactory1_fn)(REFIID, void**);
typedef HMONITOR (WINAPI *psyscr__MonitorFromWindow_fn)(HWND, DWORD);

/* EGL entry points, loaded once per process from ANGLE's libEGL. */
typedef psyscr_proc (PSYSCR__APIENTRY *psyscr__eglGetProcAddress_fn)(const char*);
typedef void* (PSYSCR__APIENTRY *psyscr__eglCreateDeviceANGLE_fn)(psyscr__EGLint, void*, const intptr_t*);
typedef unsigned (PSYSCR__APIENTRY *psyscr__eglReleaseDeviceANGLE_fn)(void*);
typedef psyscr__EGLDisplay (PSYSCR__APIENTRY *psyscr__eglGetPlatformDisplayEXT_fn)(unsigned, void*, const psyscr__EGLint*);
typedef unsigned (PSYSCR__APIENTRY *psyscr__eglInitialize_fn)(psyscr__EGLDisplay, psyscr__EGLint*, psyscr__EGLint*);
typedef unsigned (PSYSCR__APIENTRY *psyscr__eglTerminate_fn)(psyscr__EGLDisplay);
typedef unsigned (PSYSCR__APIENTRY *psyscr__eglChooseConfig_fn)(psyscr__EGLDisplay, const psyscr__EGLint*, psyscr__EGLConfig*, psyscr__EGLint, psyscr__EGLint*);
typedef psyscr__EGLContext (PSYSCR__APIENTRY *psyscr__eglCreateContext_fn)(psyscr__EGLDisplay, psyscr__EGLConfig, psyscr__EGLContext, const psyscr__EGLint*);
typedef unsigned (PSYSCR__APIENTRY *psyscr__eglDestroyContext_fn)(psyscr__EGLDisplay, psyscr__EGLContext);
typedef psyscr__EGLSurface (PSYSCR__APIENTRY *psyscr__eglCreatePbufferFromClientBuffer_fn)(psyscr__EGLDisplay, unsigned, void*, psyscr__EGLConfig, const psyscr__EGLint*);
typedef unsigned (PSYSCR__APIENTRY *psyscr__eglDestroySurface_fn)(psyscr__EGLDisplay, psyscr__EGLSurface);
typedef unsigned (PSYSCR__APIENTRY *psyscr__eglMakeCurrent_fn)(psyscr__EGLDisplay, psyscr__EGLSurface, psyscr__EGLSurface, psyscr__EGLContext);
typedef psyscr__EGLint (PSYSCR__APIENTRY *psyscr__eglGetError_fn)(void);

#define PSYSCR__EGL_NONE                 0x3038
#define PSYSCR__EGL_RED_SIZE             0x3024
#define PSYSCR__EGL_GREEN_SIZE           0x3023
#define PSYSCR__EGL_BLUE_SIZE            0x3022
#define PSYSCR__EGL_ALPHA_SIZE           0x3021
#define PSYSCR__EGL_SURFACE_TYPE         0x3033
#define PSYSCR__EGL_PBUFFER_BIT          0x0001
#define PSYSCR__EGL_RENDERABLE_TYPE      0x3040
#define PSYSCR__EGL_OPENGL_ES3_BIT       0x0040
#define PSYSCR__EGL_CONTEXT_MAJOR        0x3098
#define PSYSCR__EGL_CONTEXT_MINOR        0x30FB
#define PSYSCR__EGL_D3D11_DEVICE_ANGLE   0x33A1
#define PSYSCR__EGL_D3D_TEXTURE_ANGLE    0x33A3
#define PSYSCR__EGL_PLATFORM_DEVICE_EXT  0x313F

typedef struct psyscr__egl_api {
    HMODULE lib;
    int     tried;
    psyscr__eglGetProcAddress_fn               GetProcAddress;
    psyscr__eglCreateDeviceANGLE_fn            CreateDeviceANGLE;
    psyscr__eglReleaseDeviceANGLE_fn           ReleaseDeviceANGLE;
    psyscr__eglGetPlatformDisplayEXT_fn        GetPlatformDisplayEXT;
    psyscr__eglInitialize_fn                   Initialize;
    psyscr__eglTerminate_fn                    Terminate;
    psyscr__eglChooseConfig_fn                 ChooseConfig;
    psyscr__eglCreateContext_fn                CreateContext;
    psyscr__eglDestroyContext_fn               DestroyContext;
    psyscr__eglCreatePbufferFromClientBuffer_fn CreatePbufferFromClientBuffer;
    psyscr__eglDestroySurface_fn               DestroySurface;
    psyscr__eglMakeCurrent_fn                  MakeCurrent;
    psyscr__eglGetError_fn                     GetError;
} psyscr__egl_api;

/* Process-wide: libEGL is loaded once and never unloaded, because ANGLE
 * keeps per-process state that a reload would not restore. Open screens
 * from one thread. */
static psyscr__egl_api psyscr__egl;

static psyscr_proc psyscr__egl_sym(const char* name) {
    psyscr_proc p = psyscr__egl.GetProcAddress ? psyscr__egl.GetProcAddress(name) : NULL;
    if (!p && psyscr__egl.lib) p = (psyscr_proc)GetProcAddress(psyscr__egl.lib, name);
    return p;
}

static bool psyscr__egl_load(const char* dir, char* err, size_t cap) {
    if (psyscr__egl.lib) return true;
    {
        wchar_t path[MAX_PATH];
        HMODULE lib = NULL;
        int n = 0;
        if (dir && dir[0]) n = MultiByteToWideChar(CP_UTF8, 0, dir, -1, path, MAX_PATH - 16) - 1;
        else n = (int)GetEnvironmentVariableW(L"PSYSCR_ANGLE_DIR", path, MAX_PATH - 16);
        if (n > 0 && n < MAX_PATH - 16) {
            static const wchar_t name[] = L"\\libEGL.dll";
            memcpy(path + n, name, sizeof name);
            /* The libGLESv2.dll beside it, not one from PATH. */
            lib = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        }
        if (!lib && !(dir && dir[0])) lib = LoadLibraryW(L"libEGL.dll");
        if (!lib) {
            psyscr__set_error(err, cap, "psy_screen: ANGLE's libEGL.dll not found (desc.angle_dir, "
                              "PSYSCR_ANGLE_DIR, the program's directory); error %lu",
                              (unsigned long)GetLastError());
            return false;
        }
        psyscr__egl.lib = lib;
    }
    psyscr__egl.GetProcAddress = (psyscr__eglGetProcAddress_fn)(psyscr_proc)GetProcAddress(psyscr__egl.lib, "eglGetProcAddress");
    psyscr__egl.CreateDeviceANGLE = (psyscr__eglCreateDeviceANGLE_fn)psyscr__egl_sym("eglCreateDeviceANGLE");
    psyscr__egl.ReleaseDeviceANGLE = (psyscr__eglReleaseDeviceANGLE_fn)psyscr__egl_sym("eglReleaseDeviceANGLE");
    psyscr__egl.GetPlatformDisplayEXT = (psyscr__eglGetPlatformDisplayEXT_fn)psyscr__egl_sym("eglGetPlatformDisplayEXT");
    psyscr__egl.Initialize = (psyscr__eglInitialize_fn)psyscr__egl_sym("eglInitialize");
    psyscr__egl.Terminate = (psyscr__eglTerminate_fn)psyscr__egl_sym("eglTerminate");
    psyscr__egl.ChooseConfig = (psyscr__eglChooseConfig_fn)psyscr__egl_sym("eglChooseConfig");
    psyscr__egl.CreateContext = (psyscr__eglCreateContext_fn)psyscr__egl_sym("eglCreateContext");
    psyscr__egl.DestroyContext = (psyscr__eglDestroyContext_fn)psyscr__egl_sym("eglDestroyContext");
    psyscr__egl.CreatePbufferFromClientBuffer = (psyscr__eglCreatePbufferFromClientBuffer_fn)psyscr__egl_sym("eglCreatePbufferFromClientBuffer");
    psyscr__egl.DestroySurface = (psyscr__eglDestroySurface_fn)psyscr__egl_sym("eglDestroySurface");
    psyscr__egl.MakeCurrent = (psyscr__eglMakeCurrent_fn)psyscr__egl_sym("eglMakeCurrent");
    psyscr__egl.GetError = (psyscr__eglGetError_fn)psyscr__egl_sym("eglGetError");
    if (!psyscr__egl.CreateDeviceANGLE || !psyscr__egl.GetPlatformDisplayEXT || !psyscr__egl.Initialize ||
        !psyscr__egl.ChooseConfig || !psyscr__egl.CreateContext || !psyscr__egl.MakeCurrent ||
        !psyscr__egl.CreatePbufferFromClientBuffer || !psyscr__egl.GetError) {
        psyscr__set_error(err, cap, "psy_screen: the libEGL.dll found is not ANGLE with "
                          "EGL_ANGLE_device_creation_d3d11 and EGL_ANGLE_d3d_texture_client_buffer");
        psyscr__egl.lib = NULL;
        return false;
    }
    return true;
}

typedef struct psyscr__dxgi {
    HWND                 hwnd;
    ID3D11Device*        dev;
    ID3D11DeviceContext* dctx;
    ID3D11DeviceContext1* dctx1;
    ID3D11RenderTargetView* rtv;
    ID3D11Texture2D*     tex;      /* buffer 0: the current back buffer      */
    psyscr__d3dcodes     q;
    IDXGISwapChain1*     sc;
    IDXGISwapChainMedia* media;
    HANDLE               waitable;
    void*                edev;
    psyscr__EGLDisplay   dpy;
    psyscr__EGLContext   ctx;
    psyscr__EGLSurface   pb;
    psyscr__glFlush_fn   flush;
    UINT                 last_reported;
    UINT                 map_dxgi[8];
    uint64_t             map_id[8];
    int                  map_next;
    int64_t              period_ns;
    uint8_t              path;
    uint16_t             occluded;
    int                  w, h;
    char                 adapter[64];
    char                 angle[64];
} psyscr__dxgi;

typedef char psyscr__dxgi_fits[sizeof(psyscr__dxgi) <= sizeof(((psyscr_screen*)0)->backend_mem) ? 1 : -1];

static void psyscr__dxgi_close(void* ctx);

static int psyscr__dxgi_open(void* vctx, const psyscr_presenter_open* in, psyscr_caps* caps,
                             char* err, size_t err_cap) {
    psyscr__dxgi* d = (psyscr__dxgi*)vctx;
    HMODULE d3d11 = NULL, dxgi = NULL, user32 = NULL;
    psyscr__D3D11CreateDevice_fn create_device;
    psyscr__CreateDXGIFactory1_fn create_factory;
    psyscr__MonitorFromWindow_fn monitor_from_window;
    IDXGIFactory1* fac1 = NULL;
    IDXGIFactory2* fac2 = NULL;
    IDXGIFactory5* fac5 = NULL;
    IDXGIAdapter1* pick = NULL;
    IDXGISwapChain2* sc2 = NULL;
    ID3D11Texture2D* tex = NULL;
    HMONITOR mon;
    HRESULT hr;
    UINT i, j;
    DXGI_SWAP_CHAIN_DESC1 sd;
    D3D_FEATURE_LEVEL levels[4];
    psyscr__EGLint attrs[16], n_cfg = 0, egl_major = 0, egl_minor = 0;
    psyscr__EGLConfig cfg = NULL;
    psyscr__glGetString_fn get_string;

    memset(d, 0, sizeof *d);
    if (!in->window) { psyscr__set_error(err, err_cap, "psy_screen: no window"); return PSYSCR_ERR_ARG; }
    d->hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(in->window),
                                           SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    if (!d->hwnd) { psyscr__set_error(err, err_cap, "psy_screen: SDL gave no HWND"); return PSYSCR_ERR_LOST; }
    SDL_GetWindowSizeInPixels(in->window, &d->w, &d->h);
    d->period_ns = in->mode && in->mode->period_ns > 0 ? in->mode->period_ns : 16666667;


    d3d11 = LoadLibraryW(L"d3d11.dll");
    dxgi = LoadLibraryW(L"dxgi.dll");
    user32 = LoadLibraryW(L"user32.dll");
    create_device = d3d11 ? (psyscr__D3D11CreateDevice_fn)(psyscr_proc)GetProcAddress(d3d11, "D3D11CreateDevice") : NULL;
    create_factory = dxgi ? (psyscr__CreateDXGIFactory1_fn)(psyscr_proc)GetProcAddress(dxgi, "CreateDXGIFactory1") : NULL;
    monitor_from_window = user32 ? (psyscr__MonitorFromWindow_fn)(psyscr_proc)GetProcAddress(user32, "MonitorFromWindow") : NULL;
    if (!create_device || !create_factory || !monitor_from_window) {
        psyscr__set_error(err, err_cap, "psy_screen: d3d11.dll or dxgi.dll missing");
        return PSYSCR_ERR_NOT_IMPLEMENTED;
    }
    hr = create_factory(PSYSCR__IID(psyscr__IID_IDXGIFactory1), (void**)&fac1);
    if (FAILED(hr)) { psyscr__set_error(err, err_cap, "psy_screen: CreateDXGIFactory1 0x%08lx", (unsigned long)hr); return PSYSCR_ERR_LOST; }

    /* The adapter whose output holds the window: on a hybrid laptop that is
     * the one wired to the panel, and any other forces a cross-adapter copy
     * and composition. */
    mon = monitor_from_window(d->hwnd, 1 /* MONITOR_DEFAULTTOPRIMARY */);
    for (i = 0; !pick; i++) {
        IDXGIAdapter1* ad = NULL;
        if (PSYSCR__CALL(fac1, EnumAdapters1, i, &ad) != S_OK) break;
        for (j = 0; ; j++) {
            IDXGIOutput* o = NULL;
            DXGI_OUTPUT_DESC od;
            if (PSYSCR__CALL(ad, EnumOutputs, j, &o) != S_OK) break;
            PSYSCR__CALL(o, GetDesc, &od);
            PSYSCR__RELEASE(o);
            if (od.Monitor == mon) { pick = ad; break; }
        }
        if (pick != ad) PSYSCR__RELEASE(ad);
    }
    if (pick) {
        DXGI_ADAPTER_DESC1 ad;
        PSYSCR__CALL(pick, GetDesc1, &ad);
        WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1, d->adapter, (int)sizeof d->adapter - 1, NULL, NULL);
    }
    levels[0] = D3D_FEATURE_LEVEL_11_1; levels[1] = D3D_FEATURE_LEVEL_11_0;
    levels[2] = D3D_FEATURE_LEVEL_10_1; levels[3] = D3D_FEATURE_LEVEL_10_0;
    hr = create_device((IDXGIAdapter*)pick, pick ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, NULL,
                       psyscr__device_flags(in), levels, 4, D3D11_SDK_VERSION, &d->dev, NULL, &d->dctx);
    if (hr == E_INVALIDARG)   /* 11.1 unknown to the runtime */
        hr = create_device((IDXGIAdapter*)pick, pick ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, NULL,
                           psyscr__device_flags(in), levels + 1, 3, D3D11_SDK_VERSION, &d->dev, NULL, &d->dctx);
    PSYSCR__RELEASE(pick);
    if (FAILED(hr)) {
        PSYSCR__RELEASE(fac1);
        psyscr__device_error(in, hr, err, err_cap);
        return PSYSCR_ERR_LOST;
    }
    psyscr__device_protect(in, d->dev);
    {   /* the factory that owns the device's adapter */
        IDXGIDevice* dd = NULL;
        IDXGIAdapter* a = NULL;
        PSYSCR__CALL(d->dev, QueryInterface, PSYSCR__IID(psyscr__IID_IDXGIDevice), (void**)&dd);
        if (dd) PSYSCR__CALL(dd, GetAdapter, &a);
        if (a) PSYSCR__CALL(a, GetParent, PSYSCR__IID(psyscr__IID_IDXGIFactory2), (void**)&fac2);
        PSYSCR__RELEASE(a);
        PSYSCR__RELEASE(dd);
    }
    PSYSCR__RELEASE(fac1);
    if (!fac2) {
        psyscr__dxgi_close(d);
        psyscr__set_error(err, err_cap, "psy_screen: no DXGI 1.2 factory (Windows 8 or later needed)");
        return PSYSCR_ERR_NOT_IMPLEMENTED;
    }
    if (SUCCEEDED(PSYSCR__CALL(fac2, QueryInterface, PSYSCR__IID(psyscr__IID_IDXGIFactory5), (void**)&fac5)) && fac5) {
        BOOL tearing = FALSE;
        if (SUCCEEDED(PSYSCR__CALL(fac5, CheckFeatureSupport, DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, (UINT)sizeof tearing)))
            caps->vrr_capable = tearing != FALSE;
        PSYSCR__RELEASE(fac5);
    }

    memset(&sd, 0, sizeof sd);
    sd.Width = (UINT)d->w;
    sd.Height = (UINT)d->h;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.Scaling = DXGI_SCALING_NONE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    hr = PSYSCR__CALL(fac2, CreateSwapChainForHwnd, (IUnknown*)d->dev, d->hwnd, &sd, NULL, NULL, &d->sc);
    if (SUCCEEDED(hr)) PSYSCR__CALL(fac2, MakeWindowAssociation, d->hwnd, DXGI_MWA_NO_ALT_ENTER);
    PSYSCR__RELEASE(fac2);
    if (FAILED(hr)) {
        psyscr__dxgi_close(d);
        psyscr__set_error(err, err_cap, "psy_screen: CreateSwapChainForHwnd (flip discard) 0x%08lx", (unsigned long)hr);
        return PSYSCR_ERR_LOST;
    }
    PSYSCR__CALL(d->sc, QueryInterface, PSYSCR__IID(psyscr__IID_IDXGISwapChain2), (void**)&sc2);
    if (!sc2) {
        psyscr__dxgi_close(d);
        psyscr__set_error(err, err_cap, "psy_screen: no IDXGISwapChain2 (Windows 8.1 or later needed)");
        return PSYSCR_ERR_NOT_IMPLEMENTED;
    }
    /* One frame in flight: the waitable object signals when a slot frees. */
    PSYSCR__CALL(sc2, SetMaximumFrameLatency, 1);
    d->waitable = PSYSCR__CALL0(sc2, GetFrameLatencyWaitableObject);
    PSYSCR__RELEASE(sc2);
    PSYSCR__CALL(d->sc, QueryInterface, PSYSCR__IID(psyscr__IID_IDXGISwapChainMedia), (void**)&d->media);

    if (!psyscr__egl_load(in->angle_dir, err, err_cap)) { psyscr__dxgi_close(d); return PSYSCR_ERR_NOT_IMPLEMENTED; }
    d->edev = psyscr__egl.CreateDeviceANGLE(PSYSCR__EGL_D3D11_DEVICE_ANGLE, d->dev, NULL);
    d->dpy = d->edev ? psyscr__egl.GetPlatformDisplayEXT(PSYSCR__EGL_PLATFORM_DEVICE_EXT, d->edev, NULL) : NULL;
    if (!d->dpy || !psyscr__egl.Initialize(d->dpy, &egl_major, &egl_minor)) {
        int e = psyscr__egl.GetError();
        psyscr__dxgi_close(d);
        psyscr__set_error(err, err_cap, "psy_screen: ANGLE display on the D3D11 device failed (EGL 0x%x)", e);
        return PSYSCR_ERR_LOST;
    }
    attrs[0] = PSYSCR__EGL_RED_SIZE; attrs[1] = 8; attrs[2] = PSYSCR__EGL_GREEN_SIZE; attrs[3] = 8;
    attrs[4] = PSYSCR__EGL_BLUE_SIZE; attrs[5] = 8; attrs[6] = PSYSCR__EGL_ALPHA_SIZE; attrs[7] = 8;
    attrs[8] = PSYSCR__EGL_SURFACE_TYPE; attrs[9] = PSYSCR__EGL_PBUFFER_BIT;
    attrs[10] = PSYSCR__EGL_RENDERABLE_TYPE; attrs[11] = PSYSCR__EGL_OPENGL_ES3_BIT;
    attrs[12] = PSYSCR__EGL_NONE;
    if (!psyscr__egl.ChooseConfig(d->dpy, attrs, &cfg, 1, &n_cfg) || n_cfg < 1) {
        psyscr__dxgi_close(d);
        psyscr__set_error(err, err_cap, "psy_screen: no RGBA8 ES 3.0 pbuffer config");
        return PSYSCR_ERR_LOST;
    }
    attrs[0] = PSYSCR__EGL_CONTEXT_MAJOR; attrs[1] = 3; attrs[2] = PSYSCR__EGL_CONTEXT_MINOR; attrs[3] = 0;
    attrs[4] = PSYSCR__EGL_NONE;
    d->ctx = psyscr__egl.CreateContext(d->dpy, cfg, NULL, attrs);
    /* D3D11 flip model exposes only buffer 0, and it always names the
     * current back buffer, so one pbuffer follows the rotation. */
    PSYSCR__CALL(d->sc, GetBuffer, 0, PSYSCR__IID(psyscr__IID_ID3D11Texture2D), (void**)&tex);
    /* The patch is a ClearView with a rectangle on the same buffer, after
     * ANGLE's flush: a scissored clear that never touches GL state. */
    PSYSCR__CALL(d->dctx, QueryInterface, PSYSCR__IID(psyscr__IID_ID3D11DeviceContext1), (void**)&d->dctx1);
    if (tex && d->dctx1) PSYSCR__CALL(d->dev, CreateRenderTargetView, (ID3D11Resource*)tex, NULL, &d->rtv);
    attrs[0] = PSYSCR__EGL_NONE;
    d->pb = (d->ctx && tex) ? psyscr__egl.CreatePbufferFromClientBuffer(d->dpy, PSYSCR__EGL_D3D_TEXTURE_ANGLE, tex, cfg, attrs) : NULL;
    d->tex = tex;   /* kept: a ROW code is an UpdateSubresource on it */
    tex = NULL;
    if (!d->ctx || !d->pb || !psyscr__egl.MakeCurrent(d->dpy, d->pb, d->pb, d->ctx)) {
        int e = psyscr__egl.GetError();
        psyscr__dxgi_close(d);
        psyscr__set_error(err, err_cap, "psy_screen: ES 3.0 context on the swapchain buffer failed (EGL 0x%x)", e);
        return PSYSCR_ERR_LOST;
    }
    d->flush = (psyscr__glFlush_fn)psyscr__egl_sym("glFlush");
    get_string = (psyscr__glGetString_fn)psyscr__egl_sym("glGetString");
    if (get_string) {
        const char* v = (const char*)get_string(PSYSCR__GL_VERSION);
        const char* a = v ? strstr(v, "ANGLE ") : NULL;
        psyscr__copy(d->angle, sizeof d->angle, a ? a + 6 : (v ? v : "?"));
        {   /* "2.1.23876 git hash: fffbc739779a)" to "2.1.23876/fffbc739779a" */
            char* p = strstr(d->angle, " git hash: ");
            if (p) { memmove(p + 1, p + 11, strlen(p + 11) + 1); *p = '/'; }
            p = strchr(d->angle, ')');
            if (p) *p = '\0';
        }
    }
    caps->kind = PSYSCR_FIXED_GRID;
    caps->hw_onset = true;
    caps->native_target = false;
    caps->max_in_flight = 1;
    d->path = PSYSCR_PATH_UNKNOWN;
    psyscr__d3d_codes_open(d->dev, d->dctx, d->dctx1, in, &d->q);
    return PSYSCR_OK;
}

static void psyscr__dxgi_close(void* vctx) {
    psyscr__dxgi* d = (psyscr__dxgi*)vctx;
    if (d->dpy) {
        psyscr__egl.MakeCurrent(d->dpy, NULL, NULL, NULL);
        if (d->pb) psyscr__egl.DestroySurface(d->dpy, d->pb);
        if (d->ctx) psyscr__egl.DestroyContext(d->dpy, d->ctx);
        if (psyscr__egl.Terminate) psyscr__egl.Terminate(d->dpy);
    }
    if (d->edev && psyscr__egl.ReleaseDeviceANGLE) psyscr__egl.ReleaseDeviceANGLE(d->edev);
    if (d->waitable) CloseHandle(d->waitable);
    psyscr__d3d_codes_close(&d->q);
    PSYSCR__RELEASE(d->tex);
    PSYSCR__RELEASE(d->rtv);
    PSYSCR__RELEASE(d->dctx1);
    PSYSCR__RELEASE(d->media);
    PSYSCR__RELEASE(d->sc);
    PSYSCR__RELEASE(d->dctx);
    PSYSCR__RELEASE(d->dev);
    d->dpy = NULL; d->pb = NULL; d->ctx = NULL; d->edev = NULL; d->waitable = NULL;
}

static void psyscr__dxgi_bind(void* vctx) {
    psyscr__dxgi* d = (psyscr__dxgi*)vctx;
    psyscr__egl.MakeCurrent(d->dpy, d->pb, d->pb, d->ctx);
}

static int psyscr__dxgi_acquire(void* vctx, int64_t deadline_ns, psyscr_vblank* newest) {
    psyscr__dxgi* d = (psyscr__dxgi*)vctx;
    int64_t left = deadline_ns - psyscr__now();
    DWORD ms = left <= 0 ? 0 : (DWORD)((left + 999999) / 1000000);
    DWORD rc = WaitForSingleObjectEx(d->waitable, ms, FALSE);
    newest->t_ns = 0;
    psyscr__dxgi_bind(d);
    return rc == WAIT_OBJECT_0 ? PSYSCR_OK : PSYSCR_ERR_TIMEOUT;
}

static int psyscr__dxgi_present(void* vctx, const psyscr_present_req* req) {
    psyscr__dxgi* d = (psyscr__dxgi*)vctx;
    UINT count = 0, hold = req->hold < 1 ? 1u : (req->hold > 4 ? 4u : (UINT)req->hold);
    HRESULT hr;
    if (d->flush) d->flush();
    psyscr__d3d_finish_frame(d->dctx, d->dctx1, d->rtv, (ID3D11Resource*)d->tex, &d->q, req);
    hr = PSYSCR__CALL(d->sc, Present, hold, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) return PSYSCR_ERR_LOST;
    d->occluded = hr == DXGI_STATUS_OCCLUDED;
    if (FAILED(hr)) return PSYSCR_ERR_LOST;
    PSYSCR__CALL(d->sc, GetLastPresentCount, &count);
    d->map_dxgi[d->map_next & 7] = count;
    d->map_id[d->map_next & 7] = req->present_id;
    d->map_next++;
    return PSYSCR_OK;
}

static int psyscr__dxgi_completions(void* vctx, psyscr_vblank* out, int cap) {
    psyscr__dxgi* d = (psyscr__dxgi*)vctx;
    DXGI_FRAME_STATISTICS st;
    UINT present_count, present_refresh, sync_refresh;
    int64_t sync_qpc;
    int k;
    uint8_t path = d->path;
    if (cap < 1) return 0;
    if (d->media) {
        DXGI_FRAME_STATISTICS_MEDIA sm;
        if (FAILED(PSYSCR__CALL(d->media, GetFrameStatisticsMedia, &sm))) return 0;
        present_count = sm.PresentCount;
        present_refresh = sm.PresentRefreshCount;
        sync_refresh = sm.SyncRefreshCount;
        sync_qpc = (int64_t)sm.SyncQPCTime.QuadPart;
        switch (sm.CompositionMode) {
        case DXGI_FRAME_PRESENTATION_MODE_COMPOSED: path = PSYSCR_PATH_COMPOSED; break;
        case DXGI_FRAME_PRESENTATION_MODE_OVERLAY:  path = PSYSCR_PATH_OVERLAY; break;
        case DXGI_FRAME_PRESENTATION_MODE_NONE:     path = PSYSCR_PATH_INDEPENDENT; break;
        default:                                    path = PSYSCR_PATH_UNKNOWN; break;
        }
    } else {
        if (FAILED(PSYSCR__CALL(d->sc, GetFrameStatistics, &st))) return 0;
        present_count = st.PresentCount;
        present_refresh = st.PresentRefreshCount;
        sync_refresh = st.SyncRefreshCount;
        sync_qpc = (int64_t)st.SyncQPCTime.QuadPart;
    }
    if (present_count == 0 || present_count == d->last_reported) return 0;
    d->last_reported = present_count;
    d->path = path;
    out[0].present_id = 0;
    for (k = 0; k < 8; k++)
        if (d->map_dxgi[k] == present_count && d->map_id[k]) { out[0].present_id = d->map_id[k]; break; }
    /* SyncQPCTime is the vblank of SyncRefreshCount. The present's own
     * vblank is the same one unless the statistic was read late; then the
     * mode's period backs it out, and the core's grid check bounds it. */
    out[0].count = (int64_t)present_refresh;
    /* SyncQPCTime is QPC, the counter psyrt_now_ns() reads: the same clock,
     * converted by the same arithmetic, with no correlation. */
    out[0].t_ns = psyrt_ticks_to_ns(sync_qpc) -
                  (int64_t)(int32_t)(sync_refresh - present_refresh) * d->period_ns;
    out[0].flags = 0;
    out[0].path = path;
    if (d->occluded) out[0].flags |= PSYSCR_FLIP_OCCLUDED;
    out[0].reserved_ = 0;
    return 1;
}

static int psyscr__dxgi_describe(void* vctx, char* buf, size_t cap) {
    psyscr__dxgi* d = (psyscr__dxgi*)vctx;
    return snprintf(buf, cap, "adapter=\"%s\" angle=%s", d->adapter, d->angle);
}

static psyscr_proc psyscr__dxgi_gl_proc(void* vctx, const char* name) {
    (void)vctx;
    return psyscr__egl_sym(name);
}

static uint64_t psyscr__dxgi_gpu_done(void* vctx) { return psyscr__d3d_gpu_done(&((psyscr__dxgi*)vctx)->q); }

static const psyscr_presenter psyscr__dxgi_presenter = {
    PSYSCR_PRESENTER_VERSION, "dxgi_flip", true, false, true,
    psyscr__dxgi_open, psyscr__dxgi_close, psyscr__dxgi_acquire, psyscr__dxgi_present,
    psyscr__dxgi_completions, psyscr__dxgi_gl_proc, psyscr__dxgi_bind, psyscr__dxgi_describe,
    psyscr__dxgi_gpu_done
};

/* --- composition swapchain presenter (Windows 11) ------------------------- */

/* Our own declarations of the presentation and DirectComposition
 * interfaces: dcomp.h is C++ only, MinGW-w64 has no presentation.h, and the
 * SDK's C declarations return SystemInterruptTime and LUID by value, which
 * is not how a C++ method returns an aggregate on x64 (a hidden pointer
 * after `this`). Each table holds every slot in the SDK's order up to the
 * last one called; tests/compile/psy_screen_com.cpp checks them against
 * the SDK with spy objects. */
typedef struct psyscr__SIT { uint64_t value; } psyscr__SIT;
typedef struct psyscr__PT { float M11, M12, M21, M22, M31, M32; } psyscr__PT;
typedef struct psyscr__CFDI {               /* CompositionFrameDisplayInstance */
    LUID     displayAdapterLUID;
    UINT     displayVidPnSourceId;
    UINT     displayUniqueId;
    LUID     renderAdapterLUID;
    int      instanceKind;                  /* 0 composed, 1 scanout, 2 intermediate */
    psyscr__PT finalTransform;
    unsigned char requiredCrossAdapterCopy;
    int      colorSpace;
} psyscr__CFDI;

/* DirectComposition's frame statistics (dcomptypes.h), Windows 11. */
typedef struct psyscr__CFStats { uint64_t startTime, targetTime, framePeriod; } psyscr__CFStats;
typedef struct psyscr__CTargetId {
    LUID displayAdapterLuid;
    LUID renderAdapterLuid;
    UINT vidPnSourceId, vidPnTargetId, uniqueId;
} psyscr__CTargetId;
typedef struct psyscr__CStats { UINT presentCount, refreshCount, virtualRefreshCount; uint64_t time; } psyscr__CStats;
typedef struct psyscr__CTargetStats {
    UINT outstandingPresents;
    uint64_t presentTime;
    uint64_t vblankDuration;
    psyscr__CStats presentedStats;
    psyscr__CStats completedStats;
} psyscr__CTargetStats;
typedef HRESULT (WINAPI *psyscr__DCompositionGetStatistics_fn)(uint64_t frame, psyscr__CFStats* fs, UINT n,
                                                              psyscr__CTargetId* ids, UINT* actual);
typedef HRESULT (WINAPI *psyscr__DCompositionGetTargetStatistics_fn)(uint64_t frame, const psyscr__CTargetId* id,
                                                                    psyscr__CTargetStats* ts);

#define PSYSCR__IUNK_SLOTS \
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void* self, const IID* iid, void** out); \
    ULONG   (STDMETHODCALLTYPE *AddRef)(void* self); \
    ULONG   (STDMETHODCALLTYPE *Release)(void* self)

typedef struct psyscr__PFactoryV {          /* IPresentationFactory */
    PSYSCR__IUNK_SLOTS;
    unsigned char (STDMETHODCALLTYPE *IsPresentationSupported)(void* self);                     /* 3 */
    unsigned char (STDMETHODCALLTYPE *IsPresentationSupportedWithIndependentFlip)(void* self);  /* 4 */
    HRESULT (STDMETHODCALLTYPE *CreatePresentationManager)(void* self, void** manager);         /* 5 */
} psyscr__PFactoryV;

typedef struct psyscr__PManagerV {          /* IPresentationManager */
    PSYSCR__IUNK_SLOTS;
    HRESULT  (STDMETHODCALLTYPE *AddBufferFromResource)(void* self, void* resource, void** buffer);  /* 3 */
    HRESULT  (STDMETHODCALLTYPE *CreatePresentationSurface)(void* self, HANDLE h, void** surface);   /* 4 */
    uint64_t (STDMETHODCALLTYPE *GetNextPresentId)(void* self);                                      /* 5 */
    HRESULT  (STDMETHODCALLTYPE *SetTargetTime)(void* self, psyscr__SIT target);                     /* 6 */
    HRESULT  (STDMETHODCALLTYPE *SetPreferredPresentDuration)(void* self, psyscr__SIT d, psyscr__SIT tol); /* 7 */
    HRESULT  (STDMETHODCALLTYPE *ForceVSyncInterrupt)(void* self, unsigned char force);              /* 8 */
    HRESULT  (STDMETHODCALLTYPE *Present)(void* self);                                               /* 9 */
    HRESULT  (STDMETHODCALLTYPE *GetPresentRetiringFence)(void* self, const IID* iid, void** fence);/* 10 */
    HRESULT  (STDMETHODCALLTYPE *CancelPresentsFrom)(void* self, uint64_t id);                       /* 11 */
    HRESULT  (STDMETHODCALLTYPE *GetLostEvent)(void* self, HANDLE* h);                               /* 12 */
    HRESULT  (STDMETHODCALLTYPE *GetPresentStatisticsAvailableEvent)(void* self, HANDLE* h);         /* 13 */
    HRESULT  (STDMETHODCALLTYPE *EnablePresentStatisticsKind)(void* self, int kind, unsigned char on);/* 14 */
    HRESULT  (STDMETHODCALLTYPE *GetNextPresentStatistics)(void* self, void** stats);                /* 15 */
} psyscr__PManagerV;

typedef struct psyscr__PBufferV {           /* IPresentationBuffer */
    PSYSCR__IUNK_SLOTS;
    HRESULT (STDMETHODCALLTYPE *GetAvailableEvent)(void* self, HANDLE* h);                      /* 3 */
    HRESULT (STDMETHODCALLTYPE *IsAvailable)(void* self, unsigned char* available);             /* 4 */
} psyscr__PBufferV;

typedef struct psyscr__PSurfaceV {          /* IPresentationSurface : IPresentationContent */
    PSYSCR__IUNK_SLOTS;
    void    (STDMETHODCALLTYPE *SetTag)(void* self, UINT_PTR tag);                               /* 3 */
    HRESULT (STDMETHODCALLTYPE *SetBuffer)(void* self, void* buffer);                            /* 4 */
    HRESULT (STDMETHODCALLTYPE *SetColorSpace)(void* self, int color_space);                     /* 5 */
    HRESULT (STDMETHODCALLTYPE *SetAlphaMode)(void* self, int alpha_mode);                       /* 6 */
    HRESULT (STDMETHODCALLTYPE *SetSourceRect)(void* self, const RECT* r);                       /* 7 */
} psyscr__PSurfaceV;

#define PSYSCR__STATS_SLOTS \
    PSYSCR__IUNK_SLOTS; \
    uint64_t (STDMETHODCALLTYPE *GetPresentId)(void* self);                                       /* 3 */ \
    int      (STDMETHODCALLTYPE *GetKind)(void* self)                                             /* 4 */

typedef struct psyscr__PStatsV { PSYSCR__STATS_SLOTS; } psyscr__PStatsV;

typedef struct psyscr__PStatusStatsV {      /* IPresentStatusPresentStatistics */
    PSYSCR__STATS_SLOTS;
    uint64_t (STDMETHODCALLTYPE *GetCompositionFrameId)(void* self);                              /* 5 */
    int      (STDMETHODCALLTYPE *GetPresentStatus)(void* self);                                   /* 6 */
} psyscr__PStatusStatsV;

typedef struct psyscr__CompStatsV {         /* ICompositionFramePresentStatistics */
    PSYSCR__STATS_SLOTS;
    UINT_PTR (STDMETHODCALLTYPE *GetContentTag)(void* self);                                      /* 5 */
    uint64_t (STDMETHODCALLTYPE *GetCompositionFrameId)(void* self);                              /* 6 */
    void     (STDMETHODCALLTYPE *GetDisplayInstanceArray)(void* self, UINT* n, const psyscr__CFDI** a); /* 7 */
} psyscr__CompStatsV;

typedef struct psyscr__IFlipStatsV {        /* IIndependentFlipFramePresentStatistics */
    PSYSCR__STATS_SLOTS;
    LUID*        (STDMETHODCALLTYPE *GetOutputAdapterLUID)(void* self, LUID* ret);                /* 5 */
    UINT         (STDMETHODCALLTYPE *GetOutputVidPnSourceId)(void* self);                         /* 6 */
    UINT_PTR     (STDMETHODCALLTYPE *GetContentTag)(void* self);                                  /* 7 */
    psyscr__SIT* (STDMETHODCALLTYPE *GetDisplayedTime)(void* self, psyscr__SIT* ret);             /* 8 */
    psyscr__SIT* (STDMETHODCALLTYPE *GetPresentDuration)(void* self, psyscr__SIT* ret);           /* 9 */
} psyscr__IFlipStatsV;

typedef struct psyscr__DCDeviceV {          /* IDCompositionDevice */
    PSYSCR__IUNK_SLOTS;
    HRESULT (STDMETHODCALLTYPE *Commit)(void* self);                                             /* 3 */
    void*   unused_4_;                      /* WaitForCommitCompletion */
    void*   unused_5_;                      /* GetFrameStatistics */
    HRESULT (STDMETHODCALLTYPE *CreateTargetForHwnd)(void* self, HWND h, BOOL topmost, void** target); /* 6 */
    HRESULT (STDMETHODCALLTYPE *CreateVisual)(void* self, void** visual);                        /* 7 */
    void*   unused_8_;                      /* CreateSurface */
    void*   unused_9_;                      /* CreateVirtualSurface */
    HRESULT (STDMETHODCALLTYPE *CreateSurfaceFromHandle)(void* self, HANDLE h, void** surface);  /* 10 */
} psyscr__DCDeviceV;

typedef struct psyscr__DCTargetV {          /* IDCompositionTarget */
    PSYSCR__IUNK_SLOTS;
    HRESULT (STDMETHODCALLTYPE *SetRoot)(void* self, void* visual);                              /* 3 */
} psyscr__DCTargetV;

typedef struct psyscr__DCVisualV {          /* IDCompositionVisual */
    PSYSCR__IUNK_SLOTS;
    /* 3..14: SetOffsetX x2, SetOffsetY x2, SetTransform x2, SetTransformParent,
     * SetEffect, SetBitmapInterpolationMode, SetBorderMode, SetClip x2. Two
     * slots per overload pair whatever the compiler's order within it. */
    void*   unused_3_14_[12];
    HRESULT (STDMETHODCALLTYPE *SetContent)(void* self, void* content);                          /* 15 */
} psyscr__DCVisualV;

/* A COM object as C sees it: a pointer to its table. */
#define PSYSCR__OBJ(T) struct { const T* lpVtbl; }
typedef PSYSCR__OBJ(psyscr__PFactoryV)     psyscr__PFactory;
typedef PSYSCR__OBJ(psyscr__PManagerV)     psyscr__PManager;
typedef PSYSCR__OBJ(psyscr__PBufferV)      psyscr__PBuffer;
typedef PSYSCR__OBJ(psyscr__PSurfaceV)     psyscr__PSurface;
typedef PSYSCR__OBJ(psyscr__PStatsV)       psyscr__PStats;
typedef PSYSCR__OBJ(psyscr__PStatusStatsV) psyscr__PStatusStats;
typedef PSYSCR__OBJ(psyscr__CompStatsV)    psyscr__CompStats;
typedef PSYSCR__OBJ(psyscr__IFlipStatsV)   psyscr__IFlipStats;
typedef PSYSCR__OBJ(psyscr__DCDeviceV)     psyscr__DCDevice;
typedef PSYSCR__OBJ(psyscr__DCTargetV)     psyscr__DCTarget;
typedef PSYSCR__OBJ(psyscr__DCVisualV)     psyscr__DCVisual;
typedef PSYSCR__OBJ(psyscr__PStatsV)       psyscr__Unknown;   /* Release only */

#define PSYSCR__C(o, m, ...) ((o)->lpVtbl->m((o), __VA_ARGS__))
#define PSYSCR__C0(o, m)     ((o)->lpVtbl->m(o))
#define PSYSCR__CREL(o)      do { if (o) { (o)->lpVtbl->Release(o); (o) = NULL; } } while (0)

static const IID psyscr__IID_IPresentationFactory = {0x8fb37b58,0x1d74,0x4f64,{0xa4,0x9c,0x1f,0x97,0xa8,0x0a,0x2e,0xc0}};
static const IID psyscr__IID_IPresentStatusPresentStatistics = {0xc9ed2a41,0x79cb,0x435e,{0x96,0x4e,0xc8,0x55,0x30,0x55,0x42,0x0c}};
static const IID psyscr__IID_ICompositionFramePresentStatistics = {0xab41d127,0xc101,0x4c0a,{0x91,0x1d,0xf9,0xf2,0xe9,0xd0,0x8e,0x64}};
static const IID psyscr__IID_IIndependentFlipFramePresentStatistics = {0x8c93be27,0xad94,0x4da0,{0x8f,0xd4,0x24,0x13,0x13,0x2d,0x12,0x4e}};
static const IID psyscr__IID_IDCompositionDevice = {0xc37ea93a,0xe7aa,0x450d,{0xb1,0x6f,0x97,0x46,0xcb,0x04,0x07,0xf3}};

#define PSYSCR__KIND_STATUS 1
#define PSYSCR__KIND_COMPOSITION 2
#define PSYSCR__KIND_IFLIP 3
#define PSYSCR__STATUS_QUEUED 0
#define PSYSCR__STATUS_SKIPPED 1
#define PSYSCR__STATUS_CANCELED 2

typedef HRESULT (WINAPI *psyscr__CreatePresentationFactory_fn)(void* d3d_device, const IID* iid, void** out);
typedef HRESULT (WINAPI *psyscr__DCompositionCreateDevice_fn)(void* dxgi_device, const IID* iid, void** out);
typedef HRESULT (WINAPI *psyscr__DCompositionCreateSurfaceHandle_fn)(DWORD access, void* security, HANDLE* out);

#define PSYSCR__COMP_BUFFERS 3
#define PSYSCR__COMP_DONE 8

typedef struct psyscr__comp {
    HWND                  hwnd;
    ID3D11Device*         dev;
    ID3D11DeviceContext*  dctx;
    ID3D11DeviceContext1* dctx1;
    psyscr__PFactory*     pf;
    psyscr__PManager*     pm;
    psyscr__PSurface*     ps;
    HANDLE                surface_handle;
    psyscr__DCDevice*     dc;
    psyscr__DCTarget*     target;
    psyscr__DCVisual*     visual;
    psyscr__Unknown*      dsurf;
    HANDLE                stat_event, lost_event;
    psyscr__DCompositionGetStatistics_fn       get_stats;
    psyscr__DCompositionGetTargetStatistics_fn get_target_stats;
    ID3D11Texture2D*      tex[PSYSCR__COMP_BUFFERS];
    psyscr__PBuffer*      buf[PSYSCR__COMP_BUFFERS];
    HANDLE                avail[PSYSCR__COMP_BUFFERS];
    ID3D11RenderTargetView* rtv[PSYSCR__COMP_BUFFERS];
    psyscr__EGLSurface    pb[PSYSCR__COMP_BUFFERS];
    int                   cur;
    void*                 edev;
    psyscr__EGLDisplay    dpy;
    psyscr__EGLContext    ctx;
    psyscr__glFlush_fn    flush;
    uint64_t              sys_id[8], core_id[8];
    int                   map_next;
    uint64_t              last_sys_id;      /* the newest present */
    int                   last_resolved;    /* it has a statistic that settles it */
    int64_t               last_target_ns;
    psyscr_vblank         done[PSYSCR__COMP_DONE];
    int                   n_done;
    int64_t               ref_t, ref_count; /* vblank count from times */
    int                   have_ref;
    int64_t               period_ns;
    uint8_t               path;
    int                   w, h;
    psyscr__d3dcodes      q;
    char                  adapter[64];
    char                  angle[64];
} psyscr__comp;

typedef char psyscr__comp_fits[sizeof(psyscr__comp) <= sizeof(((psyscr_screen*)0)->backend_mem) ? 1 : -1];

static uint64_t psyscr__comp_core_id(const psyscr__comp* c, uint64_t sys) {
    int k;
    for (k = 0; k < 8; k++) if (c->sys_id[k] == sys && c->core_id[k]) return c->core_id[k];
    return 0;
}

static int64_t psyscr__comp_count(psyscr__comp* c, int64_t t) {
    int64_t k;
    if (!c->have_ref) { c->ref_t = t; c->ref_count = 1000; c->have_ref = 1; return c->ref_count; }
    k = c->ref_count + (int64_t)llround((double)(t - c->ref_t) / (double)c->period_ns);
    c->ref_t = t;
    c->ref_count = k;
    return k;
}

static void psyscr__comp_push(psyscr__comp* c, const psyscr_vblank* v) {
    if (c->n_done == PSYSCR__COMP_DONE) {   /* never with one present in flight */
        memmove(c->done, c->done + 1, sizeof c->done[0] * (PSYSCR__COMP_DONE - 1));
        c->n_done--;
    }
    c->done[c->n_done++] = *v;
}

/* Read every statistic waiting. The two time sources differ in what they
 * are (docs/psy_screen.md): an independent flip's DisplayedTime is the
 * vblank of the flip, a composed frame has only DWM's frame target. */
static void psyscr__comp_drain(psyscr__comp* c) {
    for (;;) {
        psyscr__PStats* st = NULL;
        psyscr_vblank v;
        uint64_t sys;
        int kind;
        if (FAILED(PSYSCR__C(c->pm, GetNextPresentStatistics, (void**)&st)) || !st) break;
        sys = PSYSCR__C0(st, GetPresentId);
        kind = PSYSCR__C0(st, GetKind);
        memset(&v, 0, sizeof v);
        v.present_id = psyscr__comp_core_id(c, sys);
        if (kind == PSYSCR__KIND_STATUS) {
            psyscr__PStatusStats* s2 = NULL;
            PSYSCR__C(st, QueryInterface, &psyscr__IID_IPresentStatusPresentStatistics, (void**)&s2);
            if (s2) {
                int status = PSYSCR__C0(s2, GetPresentStatus);
                if (status == PSYSCR__STATUS_SKIPPED || status == PSYSCR__STATUS_CANCELED) {
                    v.flags = (uint16_t)(status == PSYSCR__STATUS_SKIPPED ? PSYSCR_FLIP_SKIPPED : PSYSCR_FLIP_CANCELED);
                    v.path = c->path;
                    psyscr__comp_push(c, &v);
                    if (sys == c->last_sys_id) c->last_resolved = 1;
                }
                PSYSCR__CREL(s2);
            }
        } else if (kind == PSYSCR__KIND_IFLIP) {
            psyscr__IFlipStats* s3 = NULL;
            PSYSCR__C(st, QueryInterface, &psyscr__IID_IIndependentFlipFramePresentStatistics, (void**)&s3);
            if (s3) {
                psyscr__SIT shown;
                shown.value = 0;
                s3->lpVtbl->GetDisplayedTime(s3, &shown);
                /* QPC in 100 ns units, measured, not interrupt time: the
                 * value sits on the D3DKMT vblank, 20 ms from
                 * QueryInterruptTimePrecise on this machine. */
                v.t_ns = (int64_t)shown.value * 100;
                v.count = psyscr__comp_count(c, v.t_ns);
                v.path = (uint8_t)PSYSCR_PATH_INDEPENDENT;
                v.tier = (uint8_t)PSYSCR_TIER_1;
                c->path = v.path;
                psyscr__comp_push(c, &v);
                if (sys == c->last_sys_id) c->last_resolved = 1;
                PSYSCR__CREL(s3);
            }
        } else if (kind == PSYSCR__KIND_COMPOSITION) {
            psyscr__CompStats* s4 = NULL;
            PSYSCR__C(st, QueryInterface, &psyscr__IID_ICompositionFramePresentStatistics, (void**)&s4);
            if (s4) {
                UINT n = 0;
                const psyscr__CFDI* inst = NULL;
                s4->lpVtbl->GetDisplayInstanceArray(s4, &n, &inst);
                v.path = (uint8_t)((n && inst[0].instanceKind == 1) ? PSYSCR_PATH_OVERLAY : PSYSCR_PATH_COMPOSED);
                /* DWM's own time for the vblank it composed this frame for:
                 * it sits on the hardware vblank and follows a late frame to
                 * the vblank used, but it is reported about 15 ms before
                 * that vblank, so it is DWM's plan, not an observation
                 * (docs/psy_screen.md). */
                v.t_ns = 0;
                if (n && c->get_stats && c->get_target_stats) {
                    uint64_t frame = PSYSCR__C0(s4, GetCompositionFrameId);
                    psyscr__CFStats fs;
                    psyscr__CTargetId ids[8];
                    UINT k, nid = 0;
                    if (SUCCEEDED(c->get_stats(frame, &fs, 8, ids, &nid))) {
                        for (k = 0; k < nid && k < 8; k++) {
                            psyscr__CTargetStats ts;
                            if (ids[k].displayAdapterLuid.LowPart != inst[0].displayAdapterLUID.LowPart ||
                                ids[k].displayAdapterLuid.HighPart != inst[0].displayAdapterLUID.HighPart ||
                                ids[k].vidPnSourceId != inst[0].displayVidPnSourceId) continue;
                            if (SUCCEEDED(c->get_target_stats(frame, &ids[k], &ts)) && ts.presentedStats.time)
                                v.t_ns = psyrt_ticks_to_ns((int64_t)ts.presentedStats.time);
                            break;
                        }
                    }
                }
                v.flags = PSYSCR_FLIP_ONSET_PLANNED;
                v.tier = (uint8_t)PSYSCR_TIER_3;
                if (v.t_ns) v.count = psyscr__comp_count(c, v.t_ns);
                c->path = v.path;
                psyscr__comp_push(c, &v);
                if (sys == c->last_sys_id) c->last_resolved = 1;
                PSYSCR__CREL(s4);
            }
        }
        PSYSCR__CREL(st);
    }
}

static void psyscr__comp_close(void* vctx);

static int psyscr__comp_open(void* vctx, const psyscr_presenter_open* in, psyscr_caps* caps,
                             char* err, size_t err_cap) {
    psyscr__comp* c = (psyscr__comp*)vctx;
    HMODULE d3d11, dcomp, user32;
    psyscr__D3D11CreateDevice_fn create_device;
    psyscr__CreatePresentationFactory_fn create_pf;
    psyscr__DCompositionCreateDevice_fn create_dc;
    psyscr__DCompositionCreateSurfaceHandle_fn create_sh;
    psyscr__CreateDXGIFactory1_fn create_factory;
    psyscr__MonitorFromWindow_fn monitor_from_window;
    IDXGIFactory1* fac1 = NULL;
    IDXGIAdapter1* pick = NULL;
    IDXGIDevice* dxdev = NULL;
    D3D_FEATURE_LEVEL levels[2];
    HRESULT hr;
    UINT i, j;
    psyscr__EGLint attrs[16], n_cfg = 0, egl_major = 0, egl_minor = 0;
    psyscr__EGLConfig cfg = NULL;
    RECT src;

    memset(c, 0, sizeof *c);
#if defined(_M_ARM64) || defined(__aarch64__)
    (void)in; (void)caps;
    psyscr__set_error(err, err_cap, "psy_screen: COMPOSITION is x64 only: the struct-return ABI it depends on "
                      "differs on ARM64 and has not been tested there");
    return PSYSCR_ERR_NOT_IMPLEMENTED;
#else
    if (!in->window) { psyscr__set_error(err, err_cap, "psy_screen: no window"); return PSYSCR_ERR_ARG; }
    c->hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(in->window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    SDL_GetWindowSizeInPixels(in->window, &c->w, &c->h);
    c->period_ns = in->mode && in->mode->period_ns > 0 ? in->mode->period_ns : 16666667;
    d3d11 = LoadLibraryW(L"d3d11.dll");
    dcomp = LoadLibraryW(L"dcomp.dll");
    user32 = LoadLibraryW(L"user32.dll");
    create_device = d3d11 ? (psyscr__D3D11CreateDevice_fn)(psyscr_proc)GetProcAddress(d3d11, "D3D11CreateDevice") : NULL;
    create_pf = dcomp ? (psyscr__CreatePresentationFactory_fn)(psyscr_proc)GetProcAddress(dcomp, "CreatePresentationFactory") : NULL;
    create_dc = dcomp ? (psyscr__DCompositionCreateDevice_fn)(psyscr_proc)GetProcAddress(dcomp, "DCompositionCreateDevice") : NULL;
    create_sh = dcomp ? (psyscr__DCompositionCreateSurfaceHandle_fn)(psyscr_proc)GetProcAddress(dcomp, "DCompositionCreateSurfaceHandle") : NULL;
    c->get_stats = dcomp ? (psyscr__DCompositionGetStatistics_fn)(psyscr_proc)GetProcAddress(dcomp, "DCompositionGetStatistics") : NULL;
    c->get_target_stats = dcomp ? (psyscr__DCompositionGetTargetStatistics_fn)(psyscr_proc)GetProcAddress(dcomp, "DCompositionGetTargetStatistics") : NULL;
    create_factory = (psyscr__CreateDXGIFactory1_fn)(psyscr_proc)GetProcAddress(LoadLibraryW(L"dxgi.dll"), "CreateDXGIFactory1");
    monitor_from_window = user32 ? (psyscr__MonitorFromWindow_fn)(psyscr_proc)GetProcAddress(user32, "MonitorFromWindow") : NULL;
    if (!c->hwnd || !create_device || !create_pf || !create_dc || !create_sh || !create_factory || !monitor_from_window) {
        psyscr__set_error(err, err_cap, "psy_screen: the composition swapchain needs Windows 11 (dcomp.dll "
                          "CreatePresentationFactory)");
        return PSYSCR_ERR_NOT_IMPLEMENTED;
    }
    if (SUCCEEDED(create_factory(PSYSCR__IID(psyscr__IID_IDXGIFactory1), (void**)&fac1))) {
        HMONITOR mon = monitor_from_window(c->hwnd, 1);
        for (i = 0; !pick; i++) {
            IDXGIAdapter1* ad = NULL;
            if (PSYSCR__CALL(fac1, EnumAdapters1, i, &ad) != S_OK) break;
            for (j = 0; ; j++) {
                IDXGIOutput* o = NULL;
                DXGI_OUTPUT_DESC od;
                if (PSYSCR__CALL(ad, EnumOutputs, j, &o) != S_OK) break;
                PSYSCR__CALL(o, GetDesc, &od);
                PSYSCR__RELEASE(o);
                if (od.Monitor == mon) { pick = ad; break; }
            }
            if (pick != ad) PSYSCR__RELEASE(ad);
        }
        PSYSCR__RELEASE(fac1);
    }
    if (pick) {
        DXGI_ADAPTER_DESC1 ad;
        PSYSCR__CALL(pick, GetDesc1, &ad);
        WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1, c->adapter, (int)sizeof c->adapter - 1, NULL, NULL);
    }
    levels[0] = D3D_FEATURE_LEVEL_11_1; levels[1] = D3D_FEATURE_LEVEL_11_0;
    hr = create_device((IDXGIAdapter*)pick, pick ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, NULL,
                       psyscr__device_flags(in), levels, 2, D3D11_SDK_VERSION, &c->dev, NULL, &c->dctx);
    PSYSCR__RELEASE(pick);
    if (FAILED(hr)) { psyscr__device_error(in, hr, err, err_cap); return PSYSCR_ERR_LOST; }
    psyscr__device_protect(in, c->dev);
    PSYSCR__CALL(c->dctx, QueryInterface, PSYSCR__IID(psyscr__IID_ID3D11DeviceContext1), (void**)&c->dctx1);

    hr = create_pf(c->dev, &psyscr__IID_IPresentationFactory, (void**)&c->pf);
    if (FAILED(hr) || !PSYSCR__C0(c->pf, IsPresentationSupported)) {
        psyscr__comp_close(c);
        psyscr__set_error(err, err_cap, "psy_screen: this GPU and driver do not support the composition swapchain");
        return PSYSCR_ERR_NOT_IMPLEMENTED;
    }
    caps->vrr_capable = false;
    if (FAILED(PSYSCR__C(c->pf, CreatePresentationManager, (void**)&c->pm)) ||
        FAILED(create_sh(0x0003 /* COMPOSITIONOBJECT_ALL_ACCESS */, NULL, &c->surface_handle)) ||
        FAILED(PSYSCR__C(c->pm, CreatePresentationSurface, c->surface_handle, (void**)&c->ps))) {
        psyscr__comp_close(c);
        psyscr__set_error(err, err_cap, "psy_screen: presentation manager or surface failed");
        return PSYSCR_ERR_LOST;
    }
    PSYSCR__CALL(c->dev, QueryInterface, PSYSCR__IID(psyscr__IID_IDXGIDevice), (void**)&dxdev);
    hr = dxdev ? create_dc(dxdev, &psyscr__IID_IDCompositionDevice, (void**)&c->dc) : E_FAIL;
    PSYSCR__RELEASE(dxdev);
    if (FAILED(hr) ||
        FAILED(PSYSCR__C(c->dc, CreateTargetForHwnd, c->hwnd, TRUE, (void**)&c->target)) ||
        FAILED(PSYSCR__C(c->dc, CreateVisual, (void**)&c->visual)) ||
        FAILED(PSYSCR__C(c->dc, CreateSurfaceFromHandle, c->surface_handle, (void**)&c->dsurf)) ||
        FAILED(PSYSCR__C(c->visual, SetContent, (void*)c->dsurf)) ||
        FAILED(PSYSCR__C(c->target, SetRoot, (void*)c->visual)) ||
        FAILED(PSYSCR__C0(c->dc, Commit))) {
        psyscr__comp_close(c);
        psyscr__set_error(err, err_cap, "psy_screen: DirectComposition target or visual failed");
        return PSYSCR_ERR_LOST;
    }
    PSYSCR__C(c->pm, EnablePresentStatisticsKind, PSYSCR__KIND_STATUS, 1);
    PSYSCR__C(c->pm, EnablePresentStatisticsKind, PSYSCR__KIND_COMPOSITION, 1);
    PSYSCR__C(c->pm, EnablePresentStatisticsKind, PSYSCR__KIND_IFLIP, 1);
    PSYSCR__C(c->pm, GetPresentStatisticsAvailableEvent, &c->stat_event);
    PSYSCR__C(c->pm, GetLostEvent, &c->lost_event);
    PSYSCR__C(c->ps, SetAlphaMode, (int)DXGI_ALPHA_MODE_IGNORE);
    PSYSCR__C(c->ps, SetColorSpace, (int)DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    /* Without a source rectangle nothing shows and every other present is
     * skipped (measured; ntrviewer-hr and StreamLight found the same). */
    src.left = 0; src.top = 0; src.right = c->w; src.bottom = c->h;
    PSYSCR__C(c->ps, SetSourceRect, &src);

    if (!psyscr__egl_load(in->angle_dir, err, err_cap)) { psyscr__comp_close(c); return PSYSCR_ERR_NOT_IMPLEMENTED; }
    c->edev = psyscr__egl.CreateDeviceANGLE(PSYSCR__EGL_D3D11_DEVICE_ANGLE, c->dev, NULL);
    c->dpy = c->edev ? psyscr__egl.GetPlatformDisplayEXT(PSYSCR__EGL_PLATFORM_DEVICE_EXT, c->edev, NULL) : NULL;
    if (!c->dpy || !psyscr__egl.Initialize(c->dpy, &egl_major, &egl_minor)) {
        psyscr__comp_close(c);
        psyscr__set_error(err, err_cap, "psy_screen: ANGLE display on the D3D11 device failed");
        return PSYSCR_ERR_LOST;
    }
    attrs[0] = PSYSCR__EGL_RED_SIZE; attrs[1] = 8; attrs[2] = PSYSCR__EGL_GREEN_SIZE; attrs[3] = 8;
    attrs[4] = PSYSCR__EGL_BLUE_SIZE; attrs[5] = 8; attrs[6] = PSYSCR__EGL_ALPHA_SIZE; attrs[7] = 8;
    attrs[8] = PSYSCR__EGL_SURFACE_TYPE; attrs[9] = PSYSCR__EGL_PBUFFER_BIT;
    attrs[10] = PSYSCR__EGL_RENDERABLE_TYPE; attrs[11] = PSYSCR__EGL_OPENGL_ES3_BIT;
    attrs[12] = PSYSCR__EGL_NONE;
    if (!psyscr__egl.ChooseConfig(c->dpy, attrs, &cfg, 1, &n_cfg) || n_cfg < 1) {
        psyscr__comp_close(c);
        psyscr__set_error(err, err_cap, "psy_screen: no RGBA8 ES 3.0 pbuffer config");
        return PSYSCR_ERR_LOST;
    }
    attrs[0] = PSYSCR__EGL_CONTEXT_MAJOR; attrs[1] = 3; attrs[2] = PSYSCR__EGL_CONTEXT_MINOR; attrs[3] = 0;
    attrs[4] = PSYSCR__EGL_NONE;
    c->ctx = psyscr__egl.CreateContext(c->dpy, cfg, NULL, attrs);
    for (i = 0; i < PSYSCR__COMP_BUFFERS; i++) {
        D3D11_TEXTURE2D_DESC td;
        memset(&td, 0, sizeof td);
        td.Width = (UINT)c->w; td.Height = (UINT)c->h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        /* what AddBufferFromResource takes: displayable, shared by NT handle */
        td.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE | 0x100000 /* SHARED_DISPLAYABLE */;
        attrs[0] = PSYSCR__EGL_NONE;
        if (FAILED(PSYSCR__CALL(c->dev, CreateTexture2D, &td, NULL, &c->tex[i])) ||
            FAILED(PSYSCR__C(c->pm, AddBufferFromResource, (void*)c->tex[i], (void**)&c->buf[i])) ||
            FAILED(PSYSCR__C(c->buf[i], GetAvailableEvent, &c->avail[i])) ||
            !c->ctx ||
            !(c->pb[i] = psyscr__egl.CreatePbufferFromClientBuffer(c->dpy, PSYSCR__EGL_D3D_TEXTURE_ANGLE, c->tex[i], cfg, attrs))) {
            psyscr__comp_close(c);
            psyscr__set_error(err, err_cap, "psy_screen: composition buffer %u failed", i);
            return PSYSCR_ERR_LOST;
        }
        if (c->dctx1) PSYSCR__CALL(c->dev, CreateRenderTargetView, (ID3D11Resource*)c->tex[i], NULL, &c->rtv[i]);
    }
    c->flush = (psyscr__glFlush_fn)psyscr__egl_sym("glFlush");
    {
        psyscr__glGetString_fn gs = (psyscr__glGetString_fn)psyscr__egl_sym("glGetString");
        const char* v;
        psyscr__egl.MakeCurrent(c->dpy, c->pb[0], c->pb[0], c->ctx);
        v = gs ? (const char*)gs(PSYSCR__GL_VERSION) : NULL;
        if (v) {
            const char* a = strstr(v, "ANGLE ");
            char* p;
            psyscr__copy(c->angle, sizeof c->angle, a ? a + 6 : v);
            p = strstr(c->angle, " git hash: ");
            if (p) { memmove(p + 1, p + 11, strlen(p + 11) + 1); *p = '/'; }
            p = strchr(c->angle, ')');
            if (p) *p = '\0';
        }
    }
    caps->kind = PSYSCR_FIXED_GRID;
    caps->native_target = true;
    caps->hw_onset = true;
    caps->max_in_flight = 1;
    c->last_resolved = 1;
    psyscr__d3d_codes_open(c->dev, c->dctx, c->dctx1, in, &c->q);
    return PSYSCR_OK;
#endif
}

static void psyscr__comp_close(void* vctx) {
    psyscr__comp* c = (psyscr__comp*)vctx;
    int i;
    if (c->pm && c->last_sys_id) PSYSCR__C(c->pm, CancelPresentsFrom, c->last_sys_id + 1);
    psyscr__d3d_codes_close(&c->q);
    if (c->dpy) {
        psyscr__egl.MakeCurrent(c->dpy, NULL, NULL, NULL);
        for (i = 0; i < PSYSCR__COMP_BUFFERS; i++) if (c->pb[i]) psyscr__egl.DestroySurface(c->dpy, c->pb[i]);
        if (c->ctx) psyscr__egl.DestroyContext(c->dpy, c->ctx);
        if (psyscr__egl.Terminate) psyscr__egl.Terminate(c->dpy);
    }
    if (c->edev && psyscr__egl.ReleaseDeviceANGLE) psyscr__egl.ReleaseDeviceANGLE(c->edev);
    for (i = 0; i < PSYSCR__COMP_BUFFERS; i++) {
        PSYSCR__RELEASE(c->rtv[i]);
        PSYSCR__CREL(c->buf[i]);
        PSYSCR__RELEASE(c->tex[i]);
    }
    PSYSCR__CREL(c->dsurf);
    PSYSCR__CREL(c->visual);
    PSYSCR__CREL(c->target);
    PSYSCR__CREL(c->dc);
    PSYSCR__CREL(c->ps);
    PSYSCR__CREL(c->pm);
    PSYSCR__CREL(c->pf);
    if (c->surface_handle) CloseHandle(c->surface_handle);
    PSYSCR__RELEASE(c->dctx1);
    PSYSCR__RELEASE(c->dctx);
    PSYSCR__RELEASE(c->dev);
    memset(c, 0, sizeof *c);
}

static void psyscr__comp_bind(void* vctx) {
    psyscr__comp* c = (psyscr__comp*)vctx;
    psyscr__egl.MakeCurrent(c->dpy, c->pb[c->cur], c->pb[c->cur], c->ctx);
}

/* One frame in flight: wait until the newest present is settled by a
 * statistic, then for a free buffer. A present that gets no statistic in
 * time was not shown (the window was covered, measured): it completes as
 * OCCLUDED and the frame loop goes on. */
static int psyscr__comp_acquire(void* vctx, int64_t deadline_ns, psyscr_vblank* newest) {
    psyscr__comp* c = (psyscr__comp*)vctx;
    HANDLE h[2];
    int i;
    newest->t_ns = 0;
    psyscr__comp_drain(c);
    if (!c->last_resolved) {
        int64_t give_up = c->last_target_ns ? c->last_target_ns + 3 * c->period_ns : psyscr__now() + 3 * c->period_ns;
        if (give_up > deadline_ns) give_up = deadline_ns;
        h[0] = c->stat_event; h[1] = c->lost_event;
        while (!c->last_resolved) {
            int64_t left = give_up - psyscr__now();
            DWORD rc;
            if (left <= 0) {
                psyscr_vblank v;
                memset(&v, 0, sizeof v);
                v.present_id = psyscr__comp_core_id(c, c->last_sys_id);
                v.flags = PSYSCR_FLIP_OCCLUDED;
                v.path = c->path;
                psyscr__comp_push(c, &v);
                c->last_resolved = 1;
                break;
            }
            rc = WaitForMultipleObjects(2, h, FALSE, (DWORD)((left + 999999) / 1000000));
            if (rc == WAIT_OBJECT_0 + 1) return PSYSCR_ERR_LOST;
            psyscr__comp_drain(c);
        }
    }
    for (;;) {
        DWORD rc;
        int64_t left;
        for (i = 0; i < PSYSCR__COMP_BUFFERS; i++) {
            unsigned char ok = 0;
            int k = (c->cur + 1 + i) % PSYSCR__COMP_BUFFERS;
            if (SUCCEEDED(PSYSCR__C(c->buf[k], IsAvailable, &ok)) && ok) {
                c->cur = k;
                psyscr__comp_bind(c);
                return PSYSCR_OK;
            }
        }
        left = deadline_ns - psyscr__now();
        if (left <= 0) return PSYSCR_ERR_TIMEOUT;
        rc = WaitForMultipleObjects(PSYSCR__COMP_BUFFERS, c->avail, FALSE, (DWORD)((left + 999999) / 1000000));
        if (rc == WAIT_FAILED) return PSYSCR_ERR_LOST;
    }
}

static int psyscr__comp_present(void* vctx, const psyscr_present_req* req) {
    psyscr__comp* c = (psyscr__comp*)vctx;
    psyscr__SIT target;
    uint64_t sys;
    if (c->flush) c->flush();
    psyscr__d3d_finish_frame(c->dctx, c->dctx1, c->rtv[c->cur], (ID3D11Resource*)c->tex[c->cur], &c->q, req);
    if (FAILED(PSYSCR__C(c->ps, SetBuffer, (void*)c->buf[c->cur]))) return PSYSCR_ERR_LOST;
    /* QPC time in 100 ns units (measured: interrupt time is 20 ms off on
     * this machine and puts every frame 1.2 periods early). The target
     * persists across presents, so it is set on every one. */
    target.value = req->target_count ? (uint64_t)(req->target_ns / 100) : 0;
    if (FAILED(PSYSCR__C(c->pm, SetTargetTime, target))) return PSYSCR_ERR_LOST;
    sys = PSYSCR__C0(c->pm, GetNextPresentId);
    if (FAILED(PSYSCR__C0(c->pm, Present))) return PSYSCR_ERR_LOST;
    c->sys_id[c->map_next & 7] = sys;
    c->core_id[c->map_next & 7] = req->present_id;
    c->map_next++;
    c->last_sys_id = sys;
    c->last_resolved = 0;
    c->last_target_ns = req->target_count ? req->target_ns : 0;
    return PSYSCR_OK;
}

static int psyscr__comp_completions(void* vctx, psyscr_vblank* out, int cap) {
    psyscr__comp* c = (psyscr__comp*)vctx;
    int n;
    psyscr__comp_drain(c);
    n = c->n_done < cap ? c->n_done : cap;
    memcpy(out, c->done, sizeof out[0] * (size_t)n);
    memmove(c->done, c->done + n, sizeof c->done[0] * (size_t)(c->n_done - n));
    c->n_done -= n;
    return n;
}

static int psyscr__comp_describe(void* vctx, char* buf, size_t cap) {
    psyscr__comp* c = (psyscr__comp*)vctx;
    return snprintf(buf, cap, "adapter=\"%s\" angle=%s", c->adapter, c->angle);
}

static uint64_t psyscr__comp_gpu_done(void* vctx) { return psyscr__d3d_gpu_done(&((psyscr__comp*)vctx)->q); }

static const psyscr_presenter psyscr__comp_presenter = {
    PSYSCR_PRESENTER_VERSION, "composition", true, false, true,
    psyscr__comp_open, psyscr__comp_close, psyscr__comp_acquire, psyscr__comp_present,
    psyscr__comp_completions, psyscr__dxgi_gl_proc, psyscr__comp_bind, psyscr__comp_describe,
    psyscr__comp_gpu_done
};


/* --- the display's color state and the OS gamma ramp (Windows) ------------ */

/* Own layouts of the display config structs (wingdi.h, Windows 7 and
 * later), so neither the SDK version nor a _WIN32_WINNT another header set
 * decides whether this compiles. tests/compile/psy_screen_com.cpp checks
 * them against the SDK. */
typedef struct psyscr__dci_header {
    UINT32 type, size;
    LUID   adapterId;
    UINT32 id;
} psyscr__dci_header;
typedef struct psyscr__dc_path {      /* DISPLAYCONFIG_PATH_INFO           */
    LUID   src_adapter;
    UINT32 src_id, src_mode, src_status;
    LUID   tgt_adapter;
    UINT32 tgt_id;
    UINT32 tgt_rest[9];
    UINT32 flags;
} psyscr__dc_path;
typedef struct psyscr__dc_mode {      /* DISPLAYCONFIG_MODE_INFO           */
    UINT32 bytes[16];
} psyscr__dc_mode;
typedef struct psyscr__dc_source_name {
    psyscr__dci_header header;
    WCHAR  viewGdiDeviceName[32];
} psyscr__dc_source_name;
typedef struct psyscr__dc_preferred {
    psyscr__dci_header header;
    UINT32 width, height;
    UINT32 pad_;           /* targetMode holds a UINT64: 8-byte aligned */
    UINT32 targetMode[12];
} psyscr__dc_preferred;
typedef struct psyscr__aci {
    psyscr__dci_header header;
    UINT32 value;                /* bit 1: advanced color enabled; bit 2: wide color enforced */
    UINT32 colorEncoding;
    UINT32 bitsPerColorChannel;
} psyscr__aci;
typedef struct psyscr__aci2 {
    psyscr__dci_header header;
    UINT32 value;
    UINT32 colorEncoding;
    UINT32 bitsPerColorChannel;
    UINT32 activeColorMode;      /* 0 SDR, 1 WCG (auto color management), 2 HDR */
} psyscr__aci2;
#define PSYSCR__DCI_SOURCE_NAME      1
#define PSYSCR__DCI_PREFERRED_MODE   3
#define PSYSCR__DCI_ADVANCED_COLOR   9
#define PSYSCR__DCI_ADVANCED_COLOR_2 15

typedef LONG (WINAPI *psyscr__GetDisplayConfigBufferSizes_fn)(UINT32, UINT32*, UINT32*);
typedef LONG (WINAPI *psyscr__QueryDisplayConfig_fn)(UINT32, UINT32*, psyscr__dc_path*, UINT32*,
                                                     psyscr__dc_mode*, void*);
typedef LONG (WINAPI *psyscr__DisplayConfigGetDeviceInfo_fn)(psyscr__dci_header*);
typedef BOOL (WINAPI *psyscr__GetMonitorInfoW_fn)(HMONITOR, LPMONITORINFO);
typedef BOOL (WINAPI *psyscr__GammaRamp_fn)(HDC, LPVOID);
typedef HDC  (WINAPI *psyscr__CreateDCW_fn)(LPCWSTR, LPCWSTR, LPCWSTR, const DEVMODEW*);
typedef BOOL (WINAPI *psyscr__DeleteDC_fn)(HDC);
typedef HHOOK (WINAPI *psyscr__SetWindowsHookExW_fn)(int, HOOKPROC, HINSTANCE, DWORD);
typedef BOOL (WINAPI *psyscr__UnhookWindowsHookEx_fn)(HHOOK);
typedef LRESULT (WINAPI *psyscr__CallNextHookEx_fn)(HHOOK, int, WPARAM, LPARAM);
typedef BOOL (WINAPI *psyscr__GetMessageW_fn)(LPMSG, HWND, UINT, UINT);
typedef BOOL (WINAPI *psyscr__PeekMessageW_fn)(LPMSG, HWND, UINT, UINT, UINT);
typedef BOOL (WINAPI *psyscr__PostThreadMessageW_fn)(DWORD, UINT, WPARAM, LPARAM);
typedef SHORT (WINAPI *psyscr__GetAsyncKeyState_fn)(int);
typedef HWND (WINAPI *psyscr__GetForegroundWindow_fn)(void);
typedef DWORD (WINAPI *psyscr__GetWindowThreadProcessId_fn)(HWND, LPDWORD);
typedef BOOL (WINAPI *psyscr__IsHungAppWindow_fn)(HWND);
typedef HICON (WINAPI *psyscr__CreateIconIndirect_fn)(PICONINFO);
typedef BOOL (WINAPI *psyscr__DestroyIcon_fn)(HICON);
typedef LRESULT (WINAPI *psyscr__SendMessageW_fn)(HWND, UINT, WPARAM, LPARAM);
typedef UINT (WINAPI *psyscr__GetDpiForWindow_fn)(HWND);
typedef int (WINAPI *psyscr__GetSystemMetricsForDpi_fn)(int, UINT);
typedef HBITMAP (WINAPI *psyscr__CreateDIBSection_fn)(HDC, const BITMAPINFO*, UINT, void**, HANDLE, DWORD);
typedef HBITMAP (WINAPI *psyscr__CreateBitmap_fn)(int, int, UINT, UINT, const void*);
typedef BOOL (WINAPI *psyscr__DeleteObject_fn)(HGDIOBJ);
typedef int (WINAPI *psyscr__GetClassNameW_fn)(HWND, LPWSTR, int);

/* gdi32 and user32 by name, so nothing extra is linked (MinGW links no
 * gdi32 by default). */
static struct psyscr__winapi {
    int tried;
    psyscr__GetDisplayConfigBufferSizes_fn sizes;
    psyscr__QueryDisplayConfig_fn          query;
    psyscr__DisplayConfigGetDeviceInfo_fn  info;
    psyscr__GetMonitorInfoW_fn             monitor_info;
    psyscr__MonitorFromWindow_fn           monitor_from_window;
    psyscr__GammaRamp_fn                   get_ramp, set_ramp;
    psyscr__CreateDCW_fn                   create_dc;
    psyscr__DeleteDC_fn                    delete_dc;
    /* the panic watchdog */
    psyscr__SetWindowsHookExW_fn           set_hook;
    psyscr__UnhookWindowsHookEx_fn         unhook;
    psyscr__CallNextHookEx_fn              next_hook;
    psyscr__GetMessageW_fn                 get_message;
    psyscr__PeekMessageW_fn                peek_message;
    psyscr__PostThreadMessageW_fn          post_thread;
    psyscr__GetAsyncKeyState_fn            key_state;
    psyscr__GetForegroundWindow_fn         foreground;
    psyscr__GetWindowThreadProcessId_fn    window_pid;
    psyscr__IsHungAppWindow_fn             is_hung;
    psyscr__GetClassNameW_fn               class_name;
    /* the window icons */
    psyscr__CreateIconIndirect_fn          create_icon;
    psyscr__DestroyIcon_fn                 destroy_icon;
    psyscr__SendMessageW_fn                send_message;
    psyscr__GetDpiForWindow_fn             window_dpi;
    psyscr__GetSystemMetricsForDpi_fn      metrics_dpi;
    psyscr__CreateDIBSection_fn            create_dib;
    psyscr__CreateBitmap_fn                create_bitmap;
    psyscr__DeleteObject_fn                delete_object;
} psyscr__win;

static void psyscr__win_load(void) {
    HMODULE u, g;
    if (psyscr__win.tried) return;
    psyscr__win.tried = 1;
    u = LoadLibraryW(L"user32.dll");
    g = LoadLibraryW(L"gdi32.dll");
    if (u) {
        psyscr__win.sizes = (psyscr__GetDisplayConfigBufferSizes_fn)(psyscr_proc)GetProcAddress(u, "GetDisplayConfigBufferSizes");
        psyscr__win.query = (psyscr__QueryDisplayConfig_fn)(psyscr_proc)GetProcAddress(u, "QueryDisplayConfig");
        psyscr__win.info = (psyscr__DisplayConfigGetDeviceInfo_fn)(psyscr_proc)GetProcAddress(u, "DisplayConfigGetDeviceInfo");
        psyscr__win.monitor_info = (psyscr__GetMonitorInfoW_fn)(psyscr_proc)GetProcAddress(u, "GetMonitorInfoW");
        psyscr__win.monitor_from_window = (psyscr__MonitorFromWindow_fn)(psyscr_proc)GetProcAddress(u, "MonitorFromWindow");
        psyscr__win.set_hook = (psyscr__SetWindowsHookExW_fn)(psyscr_proc)GetProcAddress(u, "SetWindowsHookExW");
        psyscr__win.unhook = (psyscr__UnhookWindowsHookEx_fn)(psyscr_proc)GetProcAddress(u, "UnhookWindowsHookEx");
        psyscr__win.next_hook = (psyscr__CallNextHookEx_fn)(psyscr_proc)GetProcAddress(u, "CallNextHookEx");
        psyscr__win.get_message = (psyscr__GetMessageW_fn)(psyscr_proc)GetProcAddress(u, "GetMessageW");
        psyscr__win.peek_message = (psyscr__PeekMessageW_fn)(psyscr_proc)GetProcAddress(u, "PeekMessageW");
        psyscr__win.post_thread = (psyscr__PostThreadMessageW_fn)(psyscr_proc)GetProcAddress(u, "PostThreadMessageW");
        psyscr__win.key_state = (psyscr__GetAsyncKeyState_fn)(psyscr_proc)GetProcAddress(u, "GetAsyncKeyState");
        psyscr__win.foreground = (psyscr__GetForegroundWindow_fn)(psyscr_proc)GetProcAddress(u, "GetForegroundWindow");
        psyscr__win.window_pid = (psyscr__GetWindowThreadProcessId_fn)(psyscr_proc)GetProcAddress(u, "GetWindowThreadProcessId");
        psyscr__win.is_hung = (psyscr__IsHungAppWindow_fn)(psyscr_proc)GetProcAddress(u, "IsHungAppWindow");
        psyscr__win.class_name = (psyscr__GetClassNameW_fn)(psyscr_proc)GetProcAddress(u, "GetClassNameW");
        psyscr__win.create_icon = (psyscr__CreateIconIndirect_fn)(psyscr_proc)GetProcAddress(u, "CreateIconIndirect");
        psyscr__win.destroy_icon = (psyscr__DestroyIcon_fn)(psyscr_proc)GetProcAddress(u, "DestroyIcon");
        psyscr__win.send_message = (psyscr__SendMessageW_fn)(psyscr_proc)GetProcAddress(u, "SendMessageW");
        psyscr__win.window_dpi = (psyscr__GetDpiForWindow_fn)(psyscr_proc)GetProcAddress(u, "GetDpiForWindow");
        psyscr__win.metrics_dpi = (psyscr__GetSystemMetricsForDpi_fn)(psyscr_proc)GetProcAddress(u, "GetSystemMetricsForDpi");
    }
    if (g) {
        psyscr__win.get_ramp = (psyscr__GammaRamp_fn)(psyscr_proc)GetProcAddress(g, "GetDeviceGammaRamp");
        psyscr__win.set_ramp = (psyscr__GammaRamp_fn)(psyscr_proc)GetProcAddress(g, "SetDeviceGammaRamp");
        psyscr__win.create_dc = (psyscr__CreateDCW_fn)(psyscr_proc)GetProcAddress(g, "CreateDCW");
        psyscr__win.delete_dc = (psyscr__DeleteDC_fn)(psyscr_proc)GetProcAddress(g, "DeleteDC");
        psyscr__win.create_dib = (psyscr__CreateDIBSection_fn)(psyscr_proc)GetProcAddress(g, "CreateDIBSection");
        psyscr__win.create_bitmap = (psyscr__CreateBitmap_fn)(psyscr_proc)GetProcAddress(g, "CreateBitmap");
        psyscr__win.delete_object = (psyscr__DeleteObject_fn)(psyscr_proc)GetProcAddress(g, "DeleteObject");
    }
}

/* An 8-bit identity: every entry's high byte is its index. */
static int psyscr__ramp_identity(const WORD* r) {   /* 3 x 256, red first */
    int i;
    for (i = 0; i < 768; i++) if ((r[i] >> 8) != (i & 255)) return 0;
    return 1;
}

/* Ramps this process set, to restore at close, at exit and on SDL's quit. */
#define PSYSCR__GAMMA_MAX 8
static struct psyscr__gamma_entry {
    volatile int32_t used;
    WCHAR dev[32];
    WORD  saved[3][256];
} psyscr__gamma[PSYSCR__GAMMA_MAX];
static int psyscr__gamma_atexit, psyscr__gamma_watch;

/* close(), exit and the panic watchdog's thread can each get here: the
 * exchange lets one of them restore an entry. */
static void psyscr__gamma_restore(int k) {
    HDC dc;
    if (k < 0 || k >= PSYSCR__GAMMA_MAX) return;
    if (InterlockedExchange((volatile LONG*)&psyscr__gamma[k].used, 0) == 0) return;
    if (!psyscr__win.create_dc || !psyscr__win.set_ramp) return;
    dc = psyscr__win.create_dc(psyscr__gamma[k].dev, NULL, NULL, NULL);
    if (!dc) return;
    psyscr__win.set_ramp(dc, psyscr__gamma[k].saved);
    psyscr__win.delete_dc(dc);
}

static void psyscr__gamma_restore_all(void) {
    int k;
    for (k = 0; k < PSYSCR__GAMMA_MAX; k++) psyscr__gamma_restore(k);
}

static bool SDLCALL psyscr__gamma_quit_watch(void* ud, SDL_Event* e) {
    (void)ud;
    if (e && e->type == SDL_EVENT_QUIT) psyscr__gamma_restore_all();
    return true;
}

static void psyscr__copy_w(char* dst, size_t cap, const WCHAR* w) {
    size_t i;
    for (i = 0; i + 1 < cap && w[i]; i++) dst[i] = (char)(w[i] < 128 ? w[i] : '?');
    dst[i] = '\0';
}

/* Read what the header can of the display's color path, and take the OS
 * gamma ramp for a fullscreen screen unless desc.keep_os_gamma. */
static void psyscr__win_display_state(psyscr_screen* s, const psyscr_desc* desc) {
    HWND hwnd;
    HMONITOR mon;
    MONITORINFOEXW mi;
    psyscr__dc_path paths[32];
    psyscr__dc_mode modes[64];
    UINT32 np = 32, nm = 64, i;
    int found = 0, cm = -1, bpc = 0, pw = 0, ph = 0;
    const char* gamma = "kept";
    char dev[40];
    s->code_risk = 0;
    psyscr__win_load();
    hwnd = s->window ? (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(s->window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL) : NULL;
    mon = (hwnd && psyscr__win.monitor_from_window) ? psyscr__win.monitor_from_window(hwnd, MONITOR_DEFAULTTONEAREST) : NULL;
    memset(&mi, 0, sizeof mi);
    mi.cbSize = sizeof mi;
    if (!mon || !psyscr__win.monitor_info || !psyscr__win.monitor_info(mon, (LPMONITORINFO)&mi)) {
        s->code_risk |= PSYSCR_CODE_RISK_STATE_UNKNOWN;
        psyscr__copy(s->os_color, sizeof s->os_color, "os_color=unknown");
        return;
    }
    psyscr__copy_w(dev, sizeof dev, mi.szDevice);
    /* advanced color and the panel's own mode, through the display config */
    if (psyscr__win.query && psyscr__win.info &&
        psyscr__win.query(2u /* QDC_ONLY_ACTIVE_PATHS */, &np, paths, &nm, modes, NULL) == ERROR_SUCCESS) {
        for (i = 0; i < np && !found; i++) {
            psyscr__dc_source_name src;
            memset(&src, 0, sizeof src);
            src.header.type = PSYSCR__DCI_SOURCE_NAME;
            src.header.size = sizeof src;
            src.header.adapterId = paths[i].src_adapter;
            src.header.id = paths[i].src_id;
            if (psyscr__win.info(&src.header) != ERROR_SUCCESS || wcscmp(src.viewGdiDeviceName, mi.szDevice) != 0) continue;
            found = 1;
            {
                psyscr__aci2 a2;
                psyscr__aci a1;
                psyscr__dc_preferred pm;
                memset(&a2, 0, sizeof a2);
                a2.header.type = PSYSCR__DCI_ADVANCED_COLOR_2;
                a2.header.size = sizeof a2;
                a2.header.adapterId = paths[i].tgt_adapter;
                a2.header.id = paths[i].tgt_id;
                if (psyscr__win.info(&a2.header) == ERROR_SUCCESS) {
                    cm = (int)a2.activeColorMode;
                    bpc = (int)a2.bitsPerColorChannel;
                } else {
                    memset(&a1, 0, sizeof a1);
                    a1.header.type = PSYSCR__DCI_ADVANCED_COLOR;
                    a1.header.size = sizeof a1;
                    a1.header.adapterId = paths[i].tgt_adapter;
                    a1.header.id = paths[i].tgt_id;
                    if (psyscr__win.info(&a1.header) == ERROR_SUCCESS) {
                        cm = (a1.value & 2u) ? 2 : ((a1.value & 4u) ? 1 : 0);
                        bpc = (int)a1.bitsPerColorChannel;
                    }
                }
                memset(&pm, 0, sizeof pm);
                pm.header.type = PSYSCR__DCI_PREFERRED_MODE;
                pm.header.size = sizeof pm;
                pm.header.adapterId = paths[i].tgt_adapter;
                pm.header.id = paths[i].tgt_id;
                if (psyscr__win.info(&pm.header) == ERROR_SUCCESS) { pw = (int)pm.width; ph = (int)pm.height; }
            }
        }
    }
    if (cm < 0) s->code_risk |= PSYSCR_CODE_RISK_STATE_UNKNOWN;
    if (cm > 0) s->code_risk |= PSYSCR_CODE_RISK_ADVANCED_COLOR;
    if (!desc->windowed && pw && ph && (pw != s->caps.mode.w || ph != s->caps.mode.h)) s->code_risk |= PSYSCR_CODE_RISK_SCALED;
    /* the OS ramp: fullscreen only, never a window's, which shares the
     * display with everything else */
    if (!desc->windowed && !desc->keep_os_gamma) {
        HDC dc = psyscr__win.create_dc ? psyscr__win.create_dc(mi.szDevice, NULL, NULL, NULL) : NULL;
        WORD cur[3][256], id[3][256], back[3][256];
        int k, c;
        gamma = "unreadable";
        if (dc && psyscr__win.get_ramp && psyscr__win.get_ramp(dc, cur)) {
            if (psyscr__ramp_identity(&cur[0][0])) {
                gamma = "identity";
            } else {
                for (c = 0; c < 3; c++) for (k = 0; k < 256; k++) id[c][k] = (WORD)(k * 257);
                for (k = 0; k < PSYSCR__GAMMA_MAX && psyscr__gamma[k].used; k++) { }
                gamma = "refused";
                if (k < PSYSCR__GAMMA_MAX && psyscr__win.set_ramp) {
                    /* registered before the set, so an exit at any point
                     * puts the user's ramp back */
                    memcpy(psyscr__gamma[k].saved, cur, sizeof cur);
                    memcpy(psyscr__gamma[k].dev, mi.szDevice, sizeof psyscr__gamma[k].dev);
                    (void)InterlockedExchange((volatile LONG*)&psyscr__gamma[k].used, 1);
                    if (!psyscr__gamma_atexit) { psyscr__gamma_atexit = 1; atexit(psyscr__gamma_restore_all); }
                    if (!psyscr__gamma_watch) { psyscr__gamma_watch = SDL_AddEventWatch(psyscr__gamma_quit_watch, NULL) ? 1 : 0; }
                    if (psyscr__win.set_ramp(dc, id) && psyscr__win.get_ramp(dc, back) && psyscr__ramp_identity(&back[0][0])) {
                        s->gamma_owned = k + 1;
                        gamma = "set";
                    } else {
                        psyscr__gamma_restore(k);   /* Windows may accept and ignore a ramp */
                    }
                }
            }
        }
        if (dc) psyscr__win.delete_dc(dc);
        if (strcmp(gamma, "identity") != 0 && strcmp(gamma, "set") != 0) s->code_risk |= PSYSCR_CODE_RISK_GAMMA;
    } else {
        /* not ours to change: report it */
        HDC dc = psyscr__win.create_dc ? psyscr__win.create_dc(mi.szDevice, NULL, NULL, NULL) : NULL;
        WORD cur[3][256];
        if (dc && psyscr__win.get_ramp && psyscr__win.get_ramp(dc, cur)) {
            gamma = psyscr__ramp_identity(&cur[0][0]) ? "identity(kept)" : "not-identity(kept)";
            if (!psyscr__ramp_identity(&cur[0][0])) s->code_risk |= PSYSCR_CODE_RISK_GAMMA;
        } else {
            gamma = "unreadable(kept)";
            s->code_risk |= PSYSCR_CODE_RISK_GAMMA;
        }
        if (dc) psyscr__win.delete_dc(dc);
    }
    snprintf(s->os_color, sizeof s->os_color, "display=%s color=%s wire_bpc=%d panel=%dx%d os_gamma=%s mhc=unknown "
             "nightlight=unknown", dev, cm == 0 ? "sdr" : cm == 1 ? "wcg" : cm == 2 ? "hdr" : "unknown", bpc, pw, ph, gamma);
}

static void psyscr__win_gamma_release(psyscr_screen* s) {
    int k, any = 0;
    if (s->gamma_owned) psyscr__gamma_restore(s->gamma_owned - 1);
    s->gamma_owned = 0;
    for (k = 0; k < PSYSCR__GAMMA_MAX; k++) any |= psyscr__gamma[k].used;
    if (!any && psyscr__gamma_watch) { SDL_RemoveEventWatch(psyscr__gamma_quit_watch, NULL); psyscr__gamma_watch = 0; }
}

/* Size k of the header's icon as an HICON, or NULL. */
static HICON psyscr__win_icon(int k) {
    int side = psyscr__icon_size[k][0], i;
    BITMAPINFO bi;
    void* bits = NULL;
    HBITMAP color, mask;
    ICONINFO ii;
    HICON ic = NULL;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = side;
    bi.bmiHeader.biHeight = -side;   /* top row first */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    color = psyscr__win.create_dib(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    mask = psyscr__win.create_bitmap(side, side, 1, 1, NULL);
    if (color && mask && bits && psyscr__icon_decode(k, (uint8_t*)bits, side * 4)) {
        uint8_t* p = (uint8_t*)bits;
        for (i = 0; i < side * side; i++) { uint8_t t = p[i * 4]; p[i * 4] = p[i * 4 + 2]; p[i * 4 + 2] = t; }   /* BGRA */
        memset(&ii, 0, sizeof ii);
        ii.fIcon = TRUE;
        ii.hbmColor = color;
        ii.hbmMask = mask;
        ic = psyscr__win.create_icon(&ii);
    }
    if (color) psyscr__win.delete_object(color);
    if (mask) psyscr__win.delete_object(mask);
    return ic;
}

/* SDL gives a window one icon at its base size, 32 pixels, for the title
 * bar too, where Windows shrinks it (measured, docs/psy_screen.md); the
 * header's icon has a size drawn for each. The smallest size at least as
 * large as the window's icon metric. */
static void psyscr__win_icons(psyscr_screen* s) {
    HWND hwnd = (HWND)s->hwnd;
    UINT dpi;
    int which, k;
    psyscr__win_load();
    if (!hwnd || !psyscr__win.create_icon || !psyscr__win.send_message || !psyscr__win.create_dib ||
        !psyscr__win.create_bitmap || !psyscr__win.delete_object) return;
    dpi = psyscr__win.window_dpi ? psyscr__win.window_dpi(hwnd) : 96;
    for (which = 0; which < 2; which++) {
        int metric = which == 0 ? SM_CXSMICON : SM_CXICON;
        int want = psyscr__win.metrics_dpi ? psyscr__win.metrics_dpi(metric, dpi) : (which == 0 ? 16 : 32);
        HICON ic;
        for (k = 0; k < 2 && psyscr__icon_size[k][0] < want; k++) { }
        ic = psyscr__win_icon(k);
        if (!ic) continue;
        psyscr__win.send_message(hwnd, WM_SETICON, which == 0 ? ICON_SMALL : ICON_BIG, (LPARAM)ic);
        s->win_icon[which] = ic;
    }
}

/* --- the panic watchdog (PANIC) ------------------------------------------- */

static struct {
    HANDLE          thread;
    DWORD           tid;
    HHOOK           hook;
    HANDLE          ready;
    volatile LONG   panicking;
    int             armed;          /* screens armed                           */
    UINT            vk;
    int32_t         key_down;       /* the hook's own state of the key: repeats */
    psyscr_panic_fn fn;
    void*           ctx;
    volatile int64_t t_panic;       /* when the panic began, for a test        */
} psyscr__wd;

/* A key the hook can name: Esc, F1 to F12, a letter or a digit. */
static UINT psyscr__vk_of(uint32_t key) {
    if (!psyscr__vk_known(key)) return 0;
    if (key == PSYSCR_KEY_ESCAPE) return VK_ESCAPE;
    if (key >= 0x4000003Au && key <= 0x40000045u) return VK_F1 + (key - 0x4000003Au);   /* SDLK_F1..F12 */
    if (key >= 'a' && key <= 'z') return 'A' + (key - 'a');
    if (key >= '0' && key <= '9') return key;
    return 0;
}

/* Is the foreground window this process's? About 5 s into a hang Windows
 * puts a "Ghost" window of another process in front of a hung one
 * (measured, docs/psy_screen.md), so a ghost counts while one of the
 * screens' windows is hung. */
static int psyscr__wd_ours(void) {
    HWND h = psyscr__win.foreground();
    DWORD pid = 0;
    WCHAR cls[8];
    int i;
    if (!h) return 0;
    psyscr__win.window_pid(h, &pid);
    if (pid == GetCurrentProcessId()) return 1;
    if (!psyscr__win.class_name || !psyscr__win.is_hung || psyscr__win.class_name(h, cls, 8) != 5 ||
        wcscmp(cls, L"Ghost") != 0) return 0;
    for (i = 0; i < PSYSCR__SLOTS; i++) {
        psyscr_screen* s = psyscr__slot[i].s;
        if (s && s->hwnd && psyscr__win.is_hung((HWND)s->hwnd)) return 1;
    }
    return 0;
}

/* Every key of the session passes through here while the watchdog is
 * armed, and Windows skips a hook that takes longer than
 * LowLevelHooksTimeout: no lock but the abort state's, no I/O, no wait. */
static LRESULT CALLBACK psyscr__wd_hook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && lp) {
        const KBDLLHOOKSTRUCT* k = (const KBDLLHOOKSTRUCT*)lp;
        if (k->vkCode == psyscr__wd.vk) {
            if (psyscr__abort_edge(&psyscr__wd.key_down, wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) && psyscr__wd_ours()) {
                uint32_t m = 0;
                if (psyscr__win.key_state(VK_SHIFT) < 0) m |= PSYSCR_MOD_SHIFT;
                if (psyscr__win.key_state(VK_CONTROL) < 0) m |= PSYSCR_MOD_CTRL;
                if (psyscr__win.key_state(VK_MENU) < 0) m |= PSYSCR_MOD_ALT;
                if (psyscr__win.key_state(VK_LWIN) < 0 || psyscr__win.key_state(VK_RWIN) < 0) m |= PSYSCR_MOD_GUI;
                if (psyscr__abort_key(psyscr__ab.key, m, psyscr__now(),
                                      PSYSCR__AB_HOOK | ((k->flags & LLKHF_INJECTED) ? PSYSCR__AB_INJECTED : 0u)))
                    psyscr__win.post_thread(psyscr__wd.tid, WM_APP, 0, 0);
            }
        }
    }
    return psyscr__win.next_hook(NULL, code, wp, lp);
}

static DWORD WINAPI psyscr__wd_last_words(LPVOID arg) {
    (void)arg;
    if (psyscr__wd.fn) psyscr__wd.fn(psyscr__wd.ctx);
    return 0;
}

/* The caller's last words run on their own thread, for at most 1 s: they
 * may wait on something the hung thread holds. */
static void psyscr__wd_bounded(LPTHREAD_START_ROUTINE fn) {
    HANDLE h = CreateThread(NULL, 0, fn, NULL, 0, NULL);
    if (h) { WaitForSingleObject(h, 1000); CloseHandle(h); }
}

/* The frame loop looks hung: put back the gamma ramps, then end the
 * process. TerminateProcess, not exit(): exit runs atexit handlers and DLL
 * detach, which can wait on locks the hung thread holds, and only the end
 * of the process takes down a window and a swapchain that another thread
 * owns. Windows puts back a switched display mode as the process ends
 * (measured: ChangeDisplaySettingsExW from here blocked over 1 s on the
 * hung window and the mode came back no sooner). */
static void psyscr__panic(void) {
    int i;
    int64_t now = psyscr__now();
    if (InterlockedCompareExchange(&psyscr__wd.panicking, 1, 0) != 0) return;
    psyscr__wd.t_panic = now;
    /* no key of the session waits on this thread from here */
    if (psyscr__wd.hook) { psyscr__win.unhook(psyscr__wd.hook); psyscr__wd.hook = NULL; }
    for (i = 0; i < PSYSCR__SLOTS; i++) {
        psyscr_screen* s = psyscr__slot[i].s;
        if (s && s->panic_armed == 1 && s->ring) {
            psyrt_event ev;
            memset(&ev, 0, sizeof ev);
            ev.source = (uint16_t)PSYRT_SRC_SCREEN;
            ev.kind = (uint16_t)PSYSCR_EV_PANIC;
            ev.t_ns = (uint64_t)now;
            ev.aux = s->display_index;
            ev.u.u32[0] = (uint32_t)psyscr__ab.presses;
            ev.u.i64[1] = psyscr__a_load64(&psyscr__ab.ack);
            psyrt_ring_push(s->ring, &ev);
        }
    }
    psyscr__gamma_restore_all();
    if (psyscr__wd.fn) psyscr__wd_bounded(psyscr__wd_last_words);
    TerminateProcess(GetCurrentProcess(), PSYSCR_PANIC_EXIT_CODE);
}

static DWORD WINAPI psyscr__wd_main(LPVOID arg) {
    MSG m;
    HMODULE self = NULL;
    (void)arg;
    psyscr__win.peek_message(&m, NULL, WM_USER, WM_USER, PM_NOREMOVE);   /* the queue, before ready */
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)(void*)&psyscr__wd, &self);
    psyscr__wd.hook = psyscr__win.set_hook(WH_KEYBOARD_LL, psyscr__wd_hook, self, 0);
    SetEvent(psyscr__wd.ready);
    if (!psyscr__wd.hook) return 1;
    while (psyscr__win.get_message(&m, NULL, 0, 0) > 0)
        if (m.message == WM_APP) psyscr__panic();
    if (psyscr__wd.hook) { psyscr__win.unhook(psyscr__wd.hook); psyscr__wd.hook = NULL; }
    return 0;
}

/* The first armed screen starts the watchdog, with its desc's numbers. */
static const char* psyscr__wd_arm(psyscr_screen* s, const psyscr_desc* d) {
    if (psyscr__wd.armed == 0) {
        psyscr__win_load();
        if (!psyscr__win.set_hook || !psyscr__win.unhook || !psyscr__win.next_hook || !psyscr__win.get_message ||
            !psyscr__win.peek_message || !psyscr__win.post_thread || !psyscr__win.key_state ||
            !psyscr__win.foreground || !psyscr__win.window_pid)
            return "desc.panic: user32.dll lacks the keyboard hook calls";
        psyscr__wd.vk = psyscr__vk_of(psyscr__ab.key);
        psyscr__wd.key_down = 0;
        psyscr__wd.fn = d->panic_fn;
        psyscr__wd.ctx = d->panic_ctx;
        psyscr__wd.panicking = 0;
        psyscr__wd.ready = CreateEventW(NULL, TRUE, FALSE, NULL);
        psyscr__wd.thread = psyscr__wd.ready ? CreateThread(NULL, 0, psyscr__wd_main, NULL, 0, &psyscr__wd.tid) : NULL;
        if (!psyscr__wd.thread || WaitForSingleObject(psyscr__wd.ready, 2000) != WAIT_OBJECT_0 || !psyscr__wd.hook) {
            if (psyscr__wd.thread) { WaitForSingleObject(psyscr__wd.thread, 2000); CloseHandle(psyscr__wd.thread); }
            if (psyscr__wd.ready) CloseHandle(psyscr__wd.ready);
            psyscr__wd.thread = psyscr__wd.ready = NULL;
            return "desc.panic: the low-level keyboard hook was refused";
        }
        CloseHandle(psyscr__wd.ready);
        psyscr__wd.ready = NULL;
        /* it sleeps in GetMessage; raised so a busy frame thread cannot hold
         * up the session's keys behind it */
        SetThreadPriority(psyscr__wd.thread, THREAD_PRIORITY_HIGHEST);
        psyscr__panic_config(d->panic_presses ? d->panic_presses : 3, d->panic_window_ms ? d->panic_window_ms : 2000,
                             d->panic_grace_ms ? d->panic_grace_ms : 10000);
    }
    psyscr__wd.armed++;
    s->panic_armed = 1;
    return NULL;
}

static void psyscr__wd_disarm(psyscr_screen* s) {
    if (s->panic_armed != 1) return;
    s->panic_armed = 0;
    if (--psyscr__wd.armed > 0) return;
    psyscr__panic_config(0, 0, 0);
    psyscr__win.post_thread(psyscr__wd.tid, WM_QUIT, 0, 0);
    WaitForSingleObject(psyscr__wd.thread, 2000);
    CloseHandle(psyscr__wd.thread);
    psyscr__wd.thread = NULL;
}

#endif /* PSYSCR__DXGI */

/* --- the core -------------------------------------------------------------- */

static int64_t psyscr__time_of(const psyscr_screen* s, int64_t c) {
    return s->vb_t + (int64_t)llround((double)(c - s->vb_count) * s->period_f);
}

/* The count of the last vblank at or before t. */
static int64_t psyscr__count_at(const psyscr_screen* s, int64_t t) {
    int64_t c = s->vb_count + (int64_t)floor((double)(t - s->vb_t) / s->period_f);
    while (psyscr__time_of(s, c + 1) <= t) c++;
    while (psyscr__time_of(s, c) > t) c--;
    return c;
}

/* The vblank t lands on: the first one at or after t - lead x period, the
 * timeline's rule, so a flip and an event at one time land together. */
static int64_t psyscr__snap(const psyscr_screen* s, int64_t t) {
    int64_t edge = t - s->lead_ns;
    int64_t c = psyscr__count_at(s, edge);
    if (psyscr__time_of(s, c) < edge) c++;
    return c;
}

static void psyscr__update_lead(psyscr_screen* s) {
    s->lead_ns = s->lead < 0 ? 0 : (int64_t)(s->lead * s->period_f);
    s->margin_ns = (int64_t)(s->period_f / 8);
    if (s->margin_ns > 1000000) s->margin_ns = 1000000;
}

static void psyscr__push(psyscr_screen* s, const psyrt_event* ev) {
    if (s->ring) psyrt_ring_push(s->ring, ev);
}

static void psyscr__push_flip(psyscr_screen* s, const psyscr_record* r) {
    psyrt_event ev;
    int i;
    if (!s->ring) return;
    memset(&ev, 0, sizeof ev);
    /* A flip never shown has no onset; the ring takes 0 for "now", so the
     * record carries the planned vblank and its SKIPPED or CANCELED flag. */
    ev.t_ns = (uint64_t)(r->onset ? r->onset : r->planned);
    ev.source = (uint16_t)PSYRT_SRC_SCREEN;
    ev.kind = (uint16_t)PSYSCR_EV_FLIP;
    ev.aux = s->display_index;
    ev.u.i64[0] = r->target;
    ev.u.u16[4] = (uint16_t)(r->dropped > 65535u ? 65535u : r->dropped);
    ev.u.u16[5] = (uint16_t)((r->path & 0x7u) | ((uint32_t)(r->flags & 0x3FFu) << 3) |
                             ((uint32_t)(r->tier & 0x7u) << 13));
    for (i = 0; i < PSYSCR_N_PHASES; i++) ev.u.u32[3 + i] = r->phase_ns[i];
    ev.u.u32[9] = (uint32_t)(r->index & 0xFFFFFFFF);
    psyscr__push(s, &ev);
}

/* The tier of a flip: the presenter's, else its path's, and never 1 for a
 * time that is an estimate or a plan (TIERS in the manual). */
static uint8_t psyscr__tier(const psyscr_screen* s, const psyscr_record* r) {
    int t = r->tier;
    if (s->backend == PSYSCR_BACKEND_SIM) return PSYSCR_TIER_SIM;
    if (!t) {
        switch (r->path) {
        case PSYSCR_PATH_OVERLAY:
        case PSYSCR_PATH_INDEPENDENT: t = s->caps.hw_onset ? PSYSCR_TIER_1 : PSYSCR_TIER_3; break;
        case PSYSCR_PATH_COMPOSED:    t = s->caps.hw_onset ? PSYSCR_TIER_2 : PSYSCR_TIER_3; break;
        case PSYSCR_PATH_SIMULATED:   t = PSYSCR_TIER_SIM; break;
        default:                      t = PSYSCR_TIER_3; break;
        }
    }
    if ((r->flags & (PSYSCR_FLIP_ESTIMATED | PSYSCR_FLIP_ONSET_PLANNED)) && t < PSYSCR_TIER_3) t = PSYSCR_TIER_3;
    /* off the vblank grid: the panel stretched a frame (measured on this
     * laptop after a late frame on independent flip); the time is still
     * the system's, the grid is not */
    if ((r->flags & PSYSCR_FLIP_GRID_UNSTABLE) && t < PSYSCR_TIER_2) t = PSYSCR_TIER_2;
    return (uint8_t)t;
}

static void psyscr__finish(psyscr_screen* s, psyscr__pend* p, int64_t shown) {
    psyscr_record* r = &p->rec;
    r->flags = (uint16_t)(r->flags & ~PSYSCR_FLIP_PENDING);
    r->residual = (r->flags & (PSYSCR_FLIP_SKIPPED | PSYSCR_FLIP_CANCELED)) ? 0 : r->onset - r->target;
    if (shown > s->prev_shown) s->prev_shown = shown;
    p->used = 0;
    if (s->warming) return;
    if (!(r->flags & (PSYSCR_FLIP_SKIPPED | PSYSCR_FLIP_CANCELED))) {
        r->tier = psyscr__tier(s, r);
        if (r->tier > s->worst_tier) s->worst_tier = r->tier;
        if (s->min_tier && r->tier > s->min_tier) r->flags |= PSYSCR_FLIP_BELOW_TIER;
    }
    if (s->n_codes > 0) {
        r->code_risk = psyscr_code_risk(s);
        if (r->path == PSYSCR_PATH_COMPOSED) r->code_risk |= PSYSCR_CODE_RISK_COMPOSED;
        if (r->code_risk) r->flags |= PSYSCR_FLIP_CODE_AT_RISK;
    }
    psyscr__push_flip(s, r);
    psyscr__push_code(s, p);
    if (s->trig_on || s->on_flip) psyscr__trig_flip_done(s, p, shown);
    PSYRT_FRAME_MARK();
    PSYRT_PLOT("psyscr residual us", (double)r->residual / 1000.0);
    s->last = *r;
    s->have_last = 1;
    if (s->n_fin < PSYSCR_MAX_DONE) s->fin[s->n_fin++] = *r;
    else {   /* keep the newest */
        memmove(s->fin, s->fin + 1, sizeof(psyscr_record) * (PSYSCR_MAX_DONE - 1));
        s->fin[PSYSCR_MAX_DONE - 1] = *r;
        s->fin_lost++;
    }
}

static void psyscr__estimate(psyscr_screen* s, psyscr__pend* p) {
    p->rec.onset = psyscr__time_of(s, p->planned_count) + s->offset;
    p->rec.flags |= PSYSCR_FLIP_ESTIMATED;
    p->rec.dropped = 0;
    psyscr__finish(s, p, p->planned_count);
}

static void psyscr__anchor(psyscr_screen* s, int64_t t, int64_t count) {
    if (!s->have_anchor) {
        s->vb_t = t; s->vb_count = count;
        s->ref_t = t; s->ref_count = count;
        s->have_anchor = 1;
        return;
    }
    if (count < s->vb_count) return;
    {
        int64_t dev = t - psyscr__time_of(s, count);
        if (dev < 0) dev = -dev;
        if ((double)dev > s->period_f / 4) {   /* a jump: start the fit again */
            s->ref_t = t; s->ref_count = count;
            s->period_f = s->nominal_f;
        }
    }
    s->vb_t = t; s->vb_count = count;
    if (count - s->ref_count >= 64) {
        s->period_f = (double)(t - s->ref_t) / (double)(count - s->ref_count);
        psyscr__update_lead(s);
    }
}

static int psyscr__slack_bin(const psyscr_screen* s, int64_t slack) {
    double b = (double)slack * 32.0 / s->period_f;
    if (b < 0) return 0;
    if (b >= PSYSCR__SLACK_BINS - 1) return PSYSCR__SLACK_BINS - 1;
    return (int)b;
}

/* A present with a target waits for it, so an on-time flip shows only that
 * the depth it was given was enough: at depth 2 every flip "measured" 2,
 * and COMPOSITION ran at half rate after one miss. A try at one less drops
 * a frame whenever it fails. So the depth is lowered only on evidence: the
 * slack a present at one less would have had (this flip's slack minus a
 * period) has been seen on time, on this path, at least 8 times as often
 * as a miss was seen with as much slack or more, on 4 flips in a row. A
 * miss soon after a lowering refutes that evidence: the counts of on-time
 * flips on that path are cleared, so the same evidence cannot drop a
 * second frame. Three misses in a row raise the depth by one. */
static void psyscr__native_depth(psyscr_screen* s, psyscr__pend* p, int64_t count) {
    int path = p->rec.path < PSYSCR__SLACK_PATHS ? p->rec.path : 0;
    int64_t slack = psyscr__time_of(s, p->planned_count) - p->t_ret;
    int bin = psyscr__slack_bin(s, slack);
    int k;
    if (count < p->planned_count) return;   /* early: says nothing */
    if (++s->slack_n >= PSYSCR__SLACK_DECAY) {   /* old evidence fades */
        int q;
        s->slack_n = 0;
        for (q = 0; q < PSYSCR__SLACK_PATHS; q++)
            for (k = 0; k < PSYSCR__SLACK_BINS; k++) { s->slack_ok[q][k] >>= 1; s->slack_miss[q][k] >>= 1; }
    }
    if (count > p->planned_count) {
        if (s->slack_miss[path][bin] < 0xFFFF) s->slack_miss[path][bin]++;
        s->lower_streak = 0;
        if (!p->asap) return;   /* a held flip says nothing about the depth */
        if (s->fresh_lower > 0) {   /* the evidence was wrong: back at once */
            memset(s->slack_ok[path], 0, sizeof s->slack_ok[path]);
            s->fresh_lower = 0;
            s->depth_votes = 0;
            s->depth++;
        } else if (++s->depth_votes >= 3) {
            s->depth_votes = 0;
            if (s->depth < 8) s->depth++;
        } else {
            return;
        }
        s->depth_of[path] = s->depth;
        PSYRT_PLOT("psyscr depth", (double)s->depth);
        return;
    }
    if (s->slack_ok[path][bin] < 0xFFFF) s->slack_ok[path][bin]++;
    s->depth_votes = 0;
    if (s->fresh_lower > 0) s->fresh_lower--;
    if (!p->asap || s->depth <= 1 || p->planned_count - p->count_at_present < s->depth) return;
    {
        int lb = psyscr__slack_bin(s, slack - (int64_t)s->period_f);
        uint32_t ok = 0, miss = 0;
        for (k = 0; k <= lb; k++) ok += s->slack_ok[path][k];
        for (k = lb; k < PSYSCR__SLACK_BINS; k++) miss += s->slack_miss[path][k];
        if (slack - (int64_t)s->period_f > 0 && ok >= 8u * (1u + miss)) s->lower_streak++;
        else s->lower_streak = 0;
    }
    if (s->lower_streak >= 4) {
        s->lower_streak = 0;
        s->depth--;
        s->depth_of[path] = s->depth;
        s->fresh_lower = 4;
        PSYRT_PLOT("psyscr depth", (double)s->depth);
    }
}

static void psyscr__complete(psyscr_screen* s, const psyscr_vblank* v) {
    int i;
    psyscr__pend* p = NULL;
    int64_t count = v->count, t = v->t_ns;
    bool unstable = false;
    if (s->have_anchor) {
        int64_t dev = t - psyscr__time_of(s, count);
        if (dev < 0) dev = -dev;
        unstable = (double)dev > s->period_f / 100;
        if (unstable) s->unstable++;
    }
    for (i = 0; i < PSYSCR__MAX_PEND; i++) {
        psyscr__pend* q = &s->pend[i];
        if (!q->used) continue;
        if (q->id == v->present_id) p = q;
    }
    /* Older presents the statistics skipped: estimate them, oldest first. */
    for (;;) {
        psyscr__pend* oldest = NULL;
        for (i = 0; i < PSYSCR__MAX_PEND; i++) {
            psyscr__pend* q = &s->pend[i];
            if (q->used && v->present_id && q->id < v->present_id && (!oldest || q->id < oldest->id)) oldest = q;
        }
        if (!oldest) break;
        psyscr__estimate(s, oldest);
    }
    if (v->path != s->path) {
        psyrt_event ev;
        memset(&ev, 0, sizeof ev);
        ev.t_ns = (uint64_t)t;
        ev.source = (uint16_t)PSYRT_SRC_SCREEN;
        ev.kind = (uint16_t)PSYSCR_EV_PATH;
        ev.aux = s->display_index;
        ev.u.u16[0] = s->path;
        ev.u.u16[1] = v->path;
        psyscr__push(s, &ev);
        s->path = v->path;
        if (s->caps.native_target) {
            /* the depth this path had last time; a path seen first keeps
             * the current one until misses show otherwise */
            if (v->path < PSYSCR__SLACK_PATHS && s->depth_of[v->path]) s->depth = s->depth_of[v->path];
            s->depth_votes = 0;
            s->lower_streak = 0;
            s->fresh_lower = 0;
        } else {
            s->depth_need = 1;   /* a new path may have a new depth: adopt it */
            s->adopt_left = 3;
        }
    }
    if (v->flags & (PSYSCR_FLIP_SKIPPED | PSYSCR_FLIP_CANCELED)) {
        /* never shown: no onset, no anchor, no drop */
        if (!p) return;
        p->rec.onset = 0;
        p->rec.path = v->path;
        p->rec.flags |= (uint16_t)(v->flags & (PSYSCR_FLIP_SKIPPED | PSYSCR_FLIP_CANCELED));
        p->rec.dropped = 0;
        psyscr__finish(s, p, s->prev_shown);
        return;
    }
    if (t == 0) {   /* no time for this flip */
        if (!p) return;
        p->rec.flags |= (uint16_t)(v->flags & PSYSCR_FLIP_OCCLUDED);
        p->rec.path = v->path;
        psyscr__estimate(s, p);
        return;
    }
    psyscr__anchor(s, t, count);
    if (!p) return;
    p->rec.onset = t + s->offset;
    p->rec.path = v->path;
    p->rec.tier = v->tier;
    p->rec.flags |= (uint16_t)(v->flags & PSYSCR_FLIP_ONSET_PLANNED);
    if (unstable) p->rec.flags |= PSYSCR_FLIP_GRID_UNSTABLE;
    if (v->flags & PSYSCR_FLIP_OCCLUDED) p->rec.flags |= PSYSCR_FLIP_OCCLUDED;
    if (count < p->planned_count) {
        p->rec.flags |= PSYSCR_FLIP_EARLY;
        p->rec.dropped = 0;
    } else {
        p->rec.dropped = (uint32_t)(count - p->planned_count);
    }
    /* Depth: vblanks from the present call to the flip. */
    if (s->caps.native_target) {
        int32_t obs = (int32_t)(count - p->count_at_present);
        if (obs == s->last_obs) s->obs_streak++;
        else { s->last_obs = obs; s->obs_streak = 1; }
        if (!s->warming) psyscr__native_depth(s, p, count);
    } else if (p->asap) {
        int32_t obs = (int32_t)(count - p->count_at_present);
        if (obs == s->last_obs) s->obs_streak++;
        else { s->last_obs = obs; s->obs_streak = 1; }
        if (obs >= 1 && obs <= 8 && obs != s->depth) {
            if (obs == s->depth_cand) s->depth_votes++;
            else { s->depth_cand = obs; s->depth_votes = 1; }
            if (s->depth_votes >= s->depth_need) {
                s->depth = obs;
                s->depth_votes = 0;
                s->depth_need = 3;
                PSYRT_PLOT("psyscr depth", (double)obs);
            }
        } else if (obs == s->depth) {
            /* Shown on the vblank after the flip before it, it may have
             * waited behind that flip: then its obs says "this depth or
             * less", and must not close a path change's one-vote window
             * (at a fall from 2 to 1 it did, and 3 flips showed early). */
            if (s->depth_need == 1 && s->adopt_left > 0 && count == s->prev_shown + 1) {
                if (--s->adopt_left == 0) s->depth_need = 3;
            } else {
                s->depth_votes = 0;
                s->depth_need = 3;
            }
        }
    }
    psyscr__finish(s, p, count);
}

static void psyscr__drain(psyscr_screen* s) {
    psyscr_vblank v[4];
    int i, n;
    memset(v, 0, sizeof v);   /* a presenter that leaves a field unset means 0 */
    n = s->pr->completions(s->pr_ctx, v, 4);
    for (i = 0; i < n; i++) psyscr__complete(s, &v[i]);
}

/* DXGI reports only the newest flip, so a flip that happened since the
 * last read must be read before the next present can hide it. Only then,
 * because each read is an OS call. */
static int psyscr__overdue(const psyscr_screen* s, int64_t now) {
    int i;
    for (i = 0; i < PSYSCR__MAX_PEND; i++)
        if (s->pend[i].used && psyscr__time_of(s, s->pend[i].planned_count) <= now) return 1;
    return 0;
}

static void psyscr__drain_overdue(psyscr_screen* s) {
    if (psyscr__overdue(s, psyscr__now())) psyscr__drain(s);
}

static int psyscr__pending(const psyscr_screen* s) {
    int i, n = 0;
    for (i = 0; i < PSYSCR__MAX_PEND; i++) n += s->pend[i].used != 0;
    return n;
}

static void psyscr__gl_load(psyscr_screen* s) {
    static const char* const names[11] = {
        "glClearColor", "glClear", "glScissor", "glEnable", "glDisable", "glIsEnabled",
        "glGetIntegerv", "glGetFloatv", "glGetBooleanv", "glColorMask", "glBindFramebuffer"
    };
    int i;
    memset(s->gl, 0, sizeof s->gl);
    if (!s->pr->gl_proc) return;
    for (i = 0; i < 11; i++) s->gl[i] = s->pr->gl_proc(s->pr_ctx, names[i]);
    for (i = 0; i <= PSYSCR__GL_BINDFRAMEBUFFER; i++)
        if (!s->gl[i]) { memset(s->gl, 0, sizeof s->gl); return; }
}

static void psyscr__clear_black(psyscr_screen* s) {
    if (!s->gl[PSYSCR__GL_CLEAR]) return;
    ((psyscr__glClearColor_fn)s->gl[PSYSCR__GL_CLEARCOLOR])(0.0f, 0.0f, 0.0f, 1.0f);
    ((psyscr__glClear_fn)s->gl[PSYSCR__GL_CLEAR])(PSYSCR__GL_COLOR_BUFFER_BIT);
}

/* The patch square in pixels, top-left origin. */
static void psyscr__patch_rect(const psyscr_screen* s, int* x, int* y, int* size) {
    int w = s->caps.mode.w, h = s->caps.mode.h;
    *size = s->patch.size > 0 ? s->patch.size : PSYSCR_PATCH_DEFAULT_SIZE;
    *x = (s->patch.corner == PSYSCR_TOP_RIGHT || s->patch.corner == PSYSCR_BOTTOM_RIGHT) ? w - *size : 0;
    *y = (s->patch.corner == PSYSCR_BOTTOM_LEFT || s->patch.corner == PSYSCR_BOTTOM_RIGHT) ? h - *size : 0;
}

/* A scissored clear on the default framebuffer, so the patch costs no draw
 * call and leaves the caller's pipeline alone; the five pieces of state it
 * touches are put back. */
static void psyscr__draw_patch(psyscr_screen* s) {
    int box[4], fb = 0, size, x, y;
    float cc[4];
    unsigned char mask[4], scissor, discard;
    psyscr__glEnable_fn en = (psyscr__glEnable_fn)s->gl[PSYSCR__GL_ENABLE];
    psyscr__glEnable_fn dis = (psyscr__glEnable_fn)s->gl[PSYSCR__GL_DISABLE];
    psyscr__glIsEnabled_fn is = (psyscr__glIsEnabled_fn)s->gl[PSYSCR__GL_ISENABLED];
    psyscr__glGetIntegerv_fn geti = (psyscr__glGetIntegerv_fn)s->gl[PSYSCR__GL_GETINTEGERV];
    psyscr__glBindFramebuffer_fn bind = (psyscr__glBindFramebuffer_fn)s->gl[PSYSCR__GL_BINDFRAMEBUFFER];
    psyscr__glColorMask_fn cmask = (psyscr__glColorMask_fn)s->gl[PSYSCR__GL_COLORMASK];
    float v = s->patch_value;
    if (!s->patch.on || !s->gl[PSYSCR__GL_CLEAR]) return;
    psyscr__patch_rect(s, &x, &y, &size);
    y = s->caps.mode.h - y - size;   /* GL's origin is the bottom left */
    scissor = is(PSYSCR__GL_SCISSOR_TEST);
    discard = is(PSYSCR__GL_RASTERIZER_DISCARD);
    geti(PSYSCR__GL_SCISSOR_BOX, box);
    geti(PSYSCR__GL_DRAW_FB_BINDING, &fb);
    ((psyscr__glGetFloatv_fn)s->gl[PSYSCR__GL_GETFLOATV])(PSYSCR__GL_COLOR_CLEAR_VALUE, cc);
    ((psyscr__glGetBooleanv_fn)s->gl[PSYSCR__GL_GETBOOLEANV])(PSYSCR__GL_COLOR_WRITEMASK, mask);
    if (fb) bind(PSYSCR__GL_DRAW_FRAMEBUFFER, 0);
    if (!scissor) en(PSYSCR__GL_SCISSOR_TEST);
    if (discard) dis(PSYSCR__GL_RASTERIZER_DISCARD);
    cmask(1, 1, 1, 1);
    ((psyscr__glScissor_fn)s->gl[PSYSCR__GL_SCISSOR])(x, y, size, size);
    ((psyscr__glClearColor_fn)s->gl[PSYSCR__GL_CLEARCOLOR])(v, v, v, 1.0f);
    ((psyscr__glClear_fn)s->gl[PSYSCR__GL_CLEAR])(PSYSCR__GL_COLOR_BUFFER_BIT);
    ((psyscr__glClearColor_fn)s->gl[PSYSCR__GL_CLEARCOLOR])(cc[0], cc[1], cc[2], cc[3]);
    ((psyscr__glScissor_fn)s->gl[PSYSCR__GL_SCISSOR])(box[0], box[1], box[2], box[3]);
    cmask(mask[0], mask[1], mask[2], mask[3]);
    if (discard) en(PSYSCR__GL_RASTERIZER_DISCARD);
    if (!scissor) dis(PSYSCR__GL_SCISSOR_TEST);
    if (fb) bind(PSYSCR__GL_DRAW_FRAMEBUFFER, (unsigned int)fb);
}


/* --- codes ------------------------------------------------------------------ */

static int psyscr__rects_meet(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

/* Checks and copies desc.codes once the mode is known. 0, or a message. */
static const char* psyscr__codes_open(psyscr_screen* s, const psyscr_desc* desc) {
    int i, k, off = 0;
    if (desc->n_codes < 0 || desc->n_codes > PSYSCR_MAX_CODES) return "desc.n_codes must be 0 to PSYSCR_MAX_CODES";
    if (desc->n_codes == 0) return NULL;
    if (desc->windowed && s->pr->needs_window)
        return "codes need a fullscreen screen: a device reads display pixels, and a window is not at the display origin";
    for (i = 0; i < desc->n_codes; i++) {
        psyscr_code_slot c = desc->codes[i];
        if (c.kind == PSYSCR_CODE_ROW) c.h = 1;
        if ((c.kind != PSYSCR_CODE_SOLID && c.kind != PSYSCR_CODE_ROW) || c.w < 1 || c.h < 1 || c.x < 0 || c.y < 0 ||
            (s->caps.mode.w > 0 && (c.x + c.w > s->caps.mode.w || c.y + c.h > s->caps.mode.h)))   /* SIM has no pixels */
            return "a code slot is empty, of an unknown kind, or outside the display";
        if (c.kind == PSYSCR_CODE_ROW) {
            if (off + c.w > PSYSCR_CODE_ROW_PIXELS) return "ROW code slots hold more than PSYSCR_CODE_ROW_PIXELS pixels";
            s->code_off[i] = off;
            for (k = 0; k < c.w; k++) s->code_px[off + k] = c.rest & 0xFFFFFFu;
            off += c.w;
        }
        for (k = 0; k < i; k++) {
            const psyscr_code_slot* o = &s->code_slot[k];
            if (psyscr__rects_meet(c.x, c.y, c.w, c.h, o->x, o->y, o->w, o->h)) return "two code slots overlap";
        }
        if (desc->patch.on) {
            int px, py, ps;
            psyscr__patch_rect(s, &px, &py, &ps);
            if (psyscr__rects_meet(c.x, c.y, c.w, c.h, px, py, ps, ps))
                return "a code slot overlaps the photodiode patch (move the patch: desc.patch.corner)";
        }
        s->code_slot[i] = c;
        s->code_val[i] = c.rest & 0xFFFFFFu;
        s->code_left[i] = 0;
    }
    s->n_codes = desc->n_codes;
    s->verify_every = desc->verify_codes > 0 ? desc->verify_codes : 0;
    return NULL;
}

/* This flip's code list, in one of two buffers, so the list the swap path
 * read back on the previous present is still whole on this one. */
static const psyscr_code_draw* psyscr__codes_build(psyscr_screen* s, uint32_t* vals) {
    int i, b = s->code_buf;
    psyscr_code_draw* d = s->code_draw[b];
    uint32_t* px = s->code_frame_px[b];
    for (i = 0; i < s->n_codes; i++) {
        const psyscr_code_slot* c = &s->code_slot[i];
        int active = s->code_left[i] != 0;
        d[i].x = c->x; d[i].y = c->y; d[i].w = c->w; d[i].h = c->h;
        if (c->kind == PSYSCR_CODE_ROW) {
            int k, off = s->code_off[i];
            for (k = 0; k < c->w; k++) px[off + k] = active ? s->code_px[off + k] : (c->rest & 0xFFFFFFu);
            d[i].px = px + off;
            d[i].value = 0;
            vals[i] = px[off];
        } else {
            d[i].px = NULL;
            d[i].value = active ? s->code_val[i] : (c->rest & 0xFFFFFFu);
            vals[i] = d[i].value;
        }
    }
    return d;
}

/* After a present that went out: one flip less for each timed code. */
static void psyscr__codes_advance(psyscr_screen* s) {
    int i;
    for (i = 0; i < s->n_codes; i++) if (s->code_left[i] > 0) s->code_left[i]--;
    s->code_buf ^= 1;
}

/* Codes through GL, for a presenter that does not draw them: one
 * scissored clear per SOLID slot and per ROW pixel, with the patch's
 * save-and-restore list. glClearColor's float converts to the code. */
static void psyscr__gl_codes(psyscr_screen* s, const psyscr_code_draw* d, int n) {
    int box[4], fb = 0, i, k, h = s->caps.mode.h;
    float cc[4];
    unsigned char mask[4], scissor, discard;
    psyscr__glEnable_fn en = (psyscr__glEnable_fn)s->gl[PSYSCR__GL_ENABLE];
    psyscr__glEnable_fn dis = (psyscr__glEnable_fn)s->gl[PSYSCR__GL_DISABLE];
    psyscr__glIsEnabled_fn is = (psyscr__glIsEnabled_fn)s->gl[PSYSCR__GL_ISENABLED];
    psyscr__glGetIntegerv_fn geti = (psyscr__glGetIntegerv_fn)s->gl[PSYSCR__GL_GETINTEGERV];
    psyscr__glBindFramebuffer_fn bind = (psyscr__glBindFramebuffer_fn)s->gl[PSYSCR__GL_BINDFRAMEBUFFER];
    psyscr__glColorMask_fn cmask = (psyscr__glColorMask_fn)s->gl[PSYSCR__GL_COLORMASK];
    psyscr__glScissor_fn sc = (psyscr__glScissor_fn)s->gl[PSYSCR__GL_SCISSOR];
    psyscr__glClearColor_fn ccol = (psyscr__glClearColor_fn)s->gl[PSYSCR__GL_CLEARCOLOR];
    psyscr__glClear_fn clr = (psyscr__glClear_fn)s->gl[PSYSCR__GL_CLEAR];
    if (n < 1 || !clr) return;
    scissor = is(PSYSCR__GL_SCISSOR_TEST);
    discard = is(PSYSCR__GL_RASTERIZER_DISCARD);
    geti(PSYSCR__GL_SCISSOR_BOX, box);
    geti(PSYSCR__GL_DRAW_FB_BINDING, &fb);
    ((psyscr__glGetFloatv_fn)s->gl[PSYSCR__GL_GETFLOATV])(PSYSCR__GL_COLOR_CLEAR_VALUE, cc);
    ((psyscr__glGetBooleanv_fn)s->gl[PSYSCR__GL_GETBOOLEANV])(PSYSCR__GL_COLOR_WRITEMASK, mask);
    if (fb) bind(PSYSCR__GL_DRAW_FRAMEBUFFER, 0);
    if (!scissor) en(PSYSCR__GL_SCISSOR_TEST);
    if (discard) dis(PSYSCR__GL_RASTERIZER_DISCARD);
    cmask(1, 1, 1, 1);
    for (i = 0; i < n; i++) {
        int cnt = d[i].px ? d[i].w : 1;
        for (k = 0; k < cnt; k++) {
            uint32_t v = d[i].px ? d[i].px[k] : d[i].value;
            if (d[i].px) sc(d[i].x + k, h - d[i].y - 1, 1, 1);
            else sc(d[i].x, h - d[i].y - d[i].h, d[i].w, d[i].h);   /* GL's origin is the bottom left */
            ccol((float)((double)(v & 0xFFu) / 255.0), (float)((double)((v >> 8) & 0xFFu) / 255.0),
                 (float)((double)((v >> 16) & 0xFFu) / 255.0), 1.0f);
            clr(PSYSCR__GL_COLOR_BUFFER_BIT);
        }
    }
    ccol(cc[0], cc[1], cc[2], cc[3]);
    sc(box[0], box[1], box[2], box[3]);
    cmask(mask[0], mask[1], mask[2], mask[3]);
    if (discard) en(PSYSCR__GL_RASTERIZER_DISCARD);
    if (!scissor) dis(PSYSCR__GL_SCISSOR_TEST);
    if (fb) bind(PSYSCR__GL_DRAW_FRAMEBUFFER, (unsigned int)fb);
}

/* One CODE record per flip on a screen with codes: what was in the frame. */
static void psyscr__push_code(psyscr_screen* s, const psyscr__pend* p) {
    psyrt_event ev;
    int i;
    if (!s->ring || s->n_codes < 1) return;
    memset(&ev, 0, sizeof ev);
    ev.t_ns = (uint64_t)(p->rec.onset ? p->rec.onset : p->rec.planned);
    ev.source = (uint16_t)PSYRT_SRC_SCREEN;
    ev.kind = (uint16_t)PSYSCR_EV_CODE;
    ev.aux = s->display_index;
    ev.u.u32[0] = (uint32_t)(p->rec.index & 0xFFFFFFFF);
    ev.u.u16[2] = p->rec.code_risk;
    ev.u.u16[3] = (uint16_t)s->n_codes;
    for (i = 0; i < s->n_codes && i < 8; i++) ev.u.u32[2 + i] = p->code_vals[i];
    psyscr__push(s, &ev);
}

/* --- triggers ---------------------------------------------------------------- */

#define PSYSCR__J_FREE     0
#define PSYSCR__J_ARMED    1
#define PSYSCR__J_FIRING   2
#define PSYSCR__J_FIRED    3
#define PSYSCR__J_CANCELED 4
#define PSYSCR__FENCE_GUARD_NS 1000000   /* the GPU check runs this long before */
#define PSYSCR__WORKER_LATE_NS 1000000
/* The worker re-arms itself this long before the next vblank's deadline
 * after a flip trigger fires, so the frame thread's arm for that vblank
 * finds the worker armed early enough and needs no submit (TRIGGERS in
 * STATUS: the submit was most of the arming cost). Wider than the drift
 * of the grid from one frame to the next. */
#define PSYSCR__SPEC_EARLY_NS 50000

static void psyscr__trig_run(psyscr_screen* s, int64_t now, int flushed);

#if !defined(PSYRT_NO_THREADS) && !defined(PSYSCR__ARM)
#define PSYSCR__REAL_WORKER 1
static void psyscr__trig_job(void* ctx, const psyrt_job_info* info) {
    psyscr_screen* s = (psyscr_screen*)ctx;
    s->woke = (int64_t)info->at_ns;
    psyscr__trig_run(s, psyscr__now(), info->flushed ? 1 : 0);
}
#if defined(_WIN32)
/* The logical CPUs of group 0 below the highest efficiency class: the
 * E-cores of a hybrid CPU. 0 on a CPU with one class, or without CPU sets. */
static DWORD_PTR psyscr__ecore_mask(void) {
    typedef BOOL (WINAPI *info_fn)(void*, ULONG, PULONG, HANDLE, ULONG);
    unsigned char buf[256 * 32];   /* SYSTEM_CPU_SET_INFORMATION is 32 bytes */
    ULONG len = (ULONG)sizeof buf, off;
    BYTE top = 0, eff;
    DWORD_PTR mask = 0;
    HMODULE k = GetModuleHandleW(L"kernel32.dll");
    info_fn info = k ? (info_fn)(void (*)(void))GetProcAddress(k, "GetSystemCpuSetInformation") : NULL;
    if (!info || !info(buf, len, &len, GetCurrentProcess(), 0)) return 0;
    for (off = 0; off + 32 <= len && *(DWORD*)(void*)(buf + off) >= 32; off += *(DWORD*)(void*)(buf + off))
        if (*(DWORD*)(void*)(buf + off + 4) == 0 && buf[off + 18] > top) top = buf[off + 18];
    for (off = 0; off + 32 <= len && *(DWORD*)(void*)(buf + off) >= 32; off += *(DWORD*)(void*)(buf + off)) {
        eff = buf[off + 18];
        if (*(DWORD*)(void*)(buf + off + 4) == 0 && *(WORD*)(void*)(buf + off + 12) == 0 &&
            buf[off + 14] < 8 * sizeof(DWORD_PTR) && eff < top)
            mask |= (DWORD_PTR)1 << buf[off + 14];
    }
    return mask;
}
#endif
static bool psyscr__worker_on_start(void* ctx, char* err, size_t cap) {
    psyscr_screen* s = (psyscr_screen*)ctx;
    if (s->trig_cpu > 0) {
        if (!psyrt_thread_pin(s->trig_cpu - 1)) {
            snprintf(err, cap, "psy_screen: the trigger worker could not be pinned to CPU %d", s->trig_cpu - 1);
            return false;
        }
        s->trig_cores = 'N';
        s->trig_mask = (uint64_t)1 << ((s->trig_cpu - 1) & 63);
        return true;
    }
#if defined(_WIN32)
    /* Off the P-cores: there the display's vblank DPC took the CPU the
     * worker spun on, 50 to 400 us at the deadline (TRIGGERS in STATUS).
     * A hard mask, because Windows ran the worker on P-cores with E-core
     * CPU sets selected; a set of cores, not one, so a busy core does not
     * hold it. */
    if (s->trig_cpu == 0) {
        typedef BOOL (WINAPI *sel_fn)(HANDLE, const ULONG*, ULONG);
        HMODULE k = GetModuleHandleW(L"kernel32.dll");
        sel_fn sel = k ? (sel_fn)(void (*)(void))GetProcAddress(k, "SetThreadSelectedCpuSets") : NULL;
        DWORD_PTR m = psyscr__ecore_mask();
        if (m && SetThreadAffinityMask(GetCurrentThread(), m)) {
            /* psy_rt.h's P-core preference must not compete with the mask */
            if (sel) (void)sel(GetCurrentThread(), NULL, 0);
            s->trig_cores = 'E';
            s->trig_mask = (uint64_t)m;
        }
    }
#endif
    return true;
}
static int psyscr__worker_start(psyscr_screen* s) {
    psyrt_worker_desc wd;
    memset(&wd, 0, sizeof wd);
    wd.on_start = psyscr__worker_on_start;
    wd.start_ctx = s;
    wd.spin_ns = s->trig_spin;
    memset(&s->worker, 0, sizeof s->worker);
    return psyrt_worker_start(&s->worker, &wd) ? 1 : 0;
}
static void psyscr__worker_stop(psyscr_screen* s) { psyrt_worker_stop(&s->worker); }
static void psyscr__worker_arm(psyscr_screen* s, int64_t t) {
    psyrt_worker_submit(&s->worker, (uint64_t)(t > 0 ? t : 0), psyscr__trig_job, s);
}
#define PSYSCR__ARM(s, t)        psyscr__worker_arm((s), (t))
#define PSYSCR__WORKER_START(s)  psyscr__worker_start(s)
#define PSYSCR__WORKER_STOP(s)   psyscr__worker_stop(s)
#elif !defined(PSYSCR__ARM)
#define PSYSCR__ARM(s, t)        ((void)(s), (void)(t))
#define PSYSCR__WORKER_START(s)  ((void)(s), 0)
#define PSYSCR__WORKER_STOP(s)   ((void)(s))
#endif

/* Under the lock: keep the worker armed no later than the earliest wake.
 * An earlier wake is enough: the worker then fires nothing and re-arms for
 * the real one itself, off the frame thread. spec is a wake to arm when no
 * job is armed (0 for none). */
static void psyscr__trig_rearm_spec(psyscr_screen* s, int64_t spec) {
    int64_t w = INT64_MAX;
    int i;
    for (i = 0; i < PSYSCR_MAX_JOBS; i++)
        if (s->job[i].state == PSYSCR__J_ARMED && s->job[i].wake < w) w = s->job[i].wake;
    if (w == INT64_MAX && spec > 0) w = spec;
    if (w != INT64_MAX && w < s->armed_wake) {
        PSYRT_ZONE(z_sub, "psyscr.trigsubmit");
        s->armed_wake = w;
        PSYSCR__ARM(s, w);
        PSYRT_ZONE_END(z_sub);
    }
}
static void psyscr__trig_rearm(psyscr_screen* s) { psyscr__trig_rearm_spec(s, 0); }

static void psyscr__trig_set_count(psyscr_screen* s, psyscr__job* j, int64_t count) {
    j->count = count;
    j->deadline = psyscr__time_of(s, count) + s->offset + s->trig[j->channel].offset_ns;
    j->wake = j->check ? j->deadline - PSYSCR__FENCE_GUARD_NS : j->deadline;
}

/* The worker's callback, and the test's: fire what is due. A job chosen
 * here is FIRING under the lock, so a move from the frame thread cannot
 * touch it; the callbacks run with the lock released. */
static void psyscr__trig_run(psyscr_screen* s, int64_t now, int flushed) {
    int idx[PSYSCR_MAX_JOBS], n = 0, i, k;
    psyscr_trigger_info info[PSYSCR_MAX_JOBS];
    int64_t fired[PSYSCR_MAX_JOBS], woke = s->woke ? s->woke : now, t_lock, spec = 0;
    s->woke = 0;
    psyscr__lock(s);
    t_lock = psyscr__now() - now;
    s->armed_wake = INT64_MAX;
    for (i = 0; i < PSYSCR_MAX_JOBS; i++) {
        psyscr__job* j = &s->job[i];
        if (j->state != PSYSCR__J_ARMED) continue;
        if (!flushed && j->wake > now) continue;
        if (j->check && !flushed) {
            uint64_t done = s->pr && s->pr->gpu_done ? s->pr->gpu_done(s->pr_ctx) : UINT64_MAX;
            if (done < j->pend_id) {   /* not finished: the frame will miss */
                j->count++;
                j->deadline += j->period;
                j->wake = j->deadline - PSYSCR__FENCE_GUARD_NS;
                j->flags |= (uint16_t)(PSYSCR_TRIG_MOVED | PSYSCR_TRIG_GPU_MOVED);
                continue;
            }
            j->check = 0;
            j->wake = j->deadline;
            if (j->wake > now) continue;
        }
        j->state = PSYSCR__J_FIRING;
        if (flushed) j->flags |= PSYSCR_TRIG_FLUSHED;
        k = n++;
        while (k > 0 && s->job[idx[k - 1]].deadline > j->deadline) { idx[k] = idx[k - 1]; k--; }
        idx[k] = i;
    }
    for (k = 0; k < n; k++) {
        const psyscr__job* j = &s->job[idx[k]];
        info[k].deadline_ns = j->deadline;
        info[k].fired_ns = 0;
        info[k].woke_ns = woke;
        info[k].lock_ns = t_lock;
        info[k].frame = j->frame;
        info[k].code = j->code;
        info[k].channel = j->channel;
        info[k].flags = j->flags;
    }
    psyscr__unlock(s);
    for (k = 0; k < n; k++) {
        const psyscr_trigger_desc* t = &s->trig[info[k].channel];
        fired[k] = psyscr__now();
        info[k].fired_ns = fired[k];
        if (t->fn) t->fn(t->ctx, &info[k]);
    }
    psyscr__lock(s);
    for (k = 0; k < n; k++) {
        psyscr__job* j = &s->job[idx[k]];
        j->fired = fired[k];
        j->state = PSYSCR__J_FIRED;
        if (!flushed && fired[k] - j->deadline > PSYSCR__WORKER_LATE_NS) j->flags |= PSYSCR_TRIG_WORKER_LATE;
        /* a flip trigger: the next frame's trigger is likely one period on */
        if (!flushed && j->pend_id) {
            int64_t w = j->deadline + j->period - PSYSCR__SPEC_EARLY_NS;
            if (s->trig_fence && s->pr && s->pr->gpu_done) w -= PSYSCR__FENCE_GUARD_NS;
            if (w > spec) spec = w;
        }
    }
    psyscr__trig_rearm_spec(s, spec);
    psyscr__unlock(s);
}

/* After the present returned: arm this frame's triggers. A present that
 * returned past the planned vblank's latch cannot be shown on it, so its
 * triggers go to the first vblank it can still make (MOVED). */
static void psyscr__trig_arm_flip(psyscr_screen* s, const psyscr__pend* p) {
    int64_t c = p->planned_count, e;
    uint16_t moved = 0;
    int r, i;
    if (s->n_req < 1) return;
    e = psyscr__count_at(s, p->t_ret + s->margin_ns) + s->depth;
    if (e > c) { c = e; moved = PSYSCR_TRIG_MOVED; }
    psyscr__lock(s);
    for (r = 0; r < s->n_req; r++) {
        psyscr__job* j = NULL;
        for (i = 0; i < PSYSCR_MAX_JOBS; i++) if (s->job[i].state == PSYSCR__J_FREE) { j = &s->job[i]; break; }
        if (!j) { s->trig_lost++; continue; }
        memset(j, 0, sizeof *j);
        j->state = PSYSCR__J_ARMED;
        j->channel = s->req_ch[r];
        j->code = s->req_code[r];
        j->pend_id = p->id;
        j->frame = p->rec.index;
        j->period = (int64_t)llround(s->period_f);
        j->flags = moved;
        j->check = s->trig_fence && s->pr->gpu_done != NULL;
        psyscr__trig_set_count(s, j, c);
    }
    s->n_req = 0;
    psyscr__trig_rearm(s);
    psyscr__unlock(s);
}

/* Final jobs (fired or canceled, and their flip known) go to the ring and
 * free their slot. */
static void psyscr__trig_emit(psyscr_screen* s) {
    psyscr__job out[PSYSCR_MAX_JOBS];
    int i, n = 0;
    if (!s->trig_on) return;
    psyscr__lock(s);
    for (i = 0; i < PSYSCR_MAX_JOBS; i++) {
        psyscr__job* j = &s->job[i];
        if ((j->state == PSYSCR__J_FIRED || j->state == PSYSCR__J_CANCELED) && j->flip_done) {
            out[n++] = *j;
            j->state = PSYSCR__J_FREE;
        }
    }
    psyscr__unlock(s);
    for (i = 0; i < n; i++) {
        psyrt_event ev;
        if (!s->ring) break;
        memset(&ev, 0, sizeof ev);
        ev.t_ns = (uint64_t)(out[i].fired ? out[i].fired : out[i].deadline);
        ev.source = (uint16_t)PSYRT_SRC_SCREEN;
        ev.kind = (uint16_t)PSYSCR_EV_TRIGGER;
        ev.aux = s->display_index;
        ev.u.i64[0] = out[i].deadline;
        ev.u.i64[1] = out[i].onset;
        ev.u.u32[4] = out[i].code;
        ev.u.u32[5] = (uint32_t)(out[i].frame & 0xFFFFFFFF);
        ev.u.u16[12] = out[i].channel;
        ev.u.u16[13] = out[i].flags;
        ev.u.i32[7] = out[i].mismatch;
        ev.u.i32[8] = out[i].fired ? (int32_t)psyscr__sat32(out[i].fired - out[i].deadline) : 0;
        psyscr__push(s, &ev);
    }
}

/* A flip's record is complete: settle its triggers against the vblank it
 * was shown on, then the after-flip callback. A trigger not fired yet moves
 * to that vblank; one already fired is a mismatch. */
static void psyscr__trig_flip_done(psyscr_screen* s, const psyscr__pend* p, int64_t shown) {
    psyscr_trigger_result res[PSYSCR_MAX_JOBS];
    const psyscr_record* r = &p->rec;
    int i, n = 0, moved = 0;
    if (s->trig_on) {
        psyscr__lock(s);
        for (i = 0; i < PSYSCR_MAX_JOBS; i++) {
            psyscr__job* j = &s->job[i];
            if (j->state == PSYSCR__J_FREE || j->pend_id != p->id || j->flip_done) continue;
            j->flip_done = 1;
            if (r->flags & (PSYSCR_FLIP_SKIPPED | PSYSCR_FLIP_CANCELED)) {
                j->flags |= PSYSCR_TRIG_NOT_SHOWN;
                if (j->state == PSYSCR__J_ARMED) { j->state = PSYSCR__J_CANCELED; j->flags |= PSYSCR_TRIG_CANCELED; }
            } else {
                j->onset = r->onset;
                if (r->flags & PSYSCR_FLIP_ESTIMATED) {
                    j->flags |= PSYSCR_TRIG_ESTIMATED;
                } else if (shown != j->count) {
                    if (j->state == PSYSCR__J_ARMED) {
                        psyscr__trig_set_count(s, j, shown);
                        j->flags |= PSYSCR_TRIG_MOVED;
                        moved = 1;
                    } else {
                        j->mismatch = (int32_t)(shown - j->count);
                        j->flags |= (uint16_t)(shown > j->count ? PSYSCR_TRIG_FIRED_EARLY : PSYSCR_TRIG_FIRED_LATE);
                    }
                }
            }
            res[n].deadline_ns = j->deadline;
            res[n].fired_ns = j->state == PSYSCR__J_FIRED ? j->fired : 0;
            res[n].onset_ns = (r->flags & (PSYSCR_FLIP_SKIPPED | PSYSCR_FLIP_CANCELED)) ? 0 : r->onset;
            res[n].frame = j->frame;
            res[n].code = j->code;
            res[n].channel = j->channel;
            res[n].flags = (uint16_t)(j->flags | ((j->state == PSYSCR__J_ARMED || j->state == PSYSCR__J_FIRING)
                                                  ? PSYSCR_TRIG_PENDING : 0));
            res[n].mismatch = j->mismatch;
            res[n].reserved_ = 0;
            n++;
        }
        if (moved) psyscr__trig_rearm(s);
        psyscr__unlock(s);
    }
    if (s->on_flip) s->on_flip(s->on_flip_ctx, r, res, n);
    psyscr__trig_emit(s);
}

/* Present a few black frames at open, so the first begin() has a vblank
 * anchor and a measured depth, not a guess. */
static void psyscr__warmup(psyscr_screen* s) {
    int i;
    s->warming = 1;
    /* At least 6 frames, and on until three flips in a row agree with the
     * depth: a window that has just gone fullscreen is composed for a few
     * frames before it gets an overlay or independent flip. */
    for (i = 0; i < 40; i++) {
        /* a backend that holds a frame to its target learns its depth from
         * misses after open, so only the anchor and a steady path matter */
        if (i >= 6 && s->obs_streak >= 3 && (s->caps.native_target || s->last_obs == s->depth)) break;
        psyscr_present_req req;
        psyscr_vblank newest;
        int64_t now = psyscr__now();
        newest.t_ns = 0;
        if (s->pr->acquire(s->pr_ctx, now + 200000000, &newest) < 0) break;
        psyscr__drain(s);
        if (newest.t_ns && !s->have_anchor) psyscr__anchor(s, newest.t_ns, newest.count);
        psyscr__clear_black(s);
        memset(&req, 0, sizeof req);
        req.present_id = ++s->next_id;
        req.hold = 1;
        {
            psyscr__pend* p = &s->pend[i % PSYSCR__MAX_PEND];
            if (p->used) psyscr__estimate(s, p);
            memset(p, 0, sizeof *p);
            p->used = 1;
            p->id = req.present_id;
            p->rec.index = -1 - i;
            p->rec.flags = PSYSCR_FLIP_PENDING;
            if (s->have_anchor) {
                p->count_at_present = psyscr__count_at(s, psyscr__now());
                p->planned_count = p->count_at_present + s->depth;
                p->asap = 1;
            } else {
                p->planned_count = 0;
            }
            req.target_count = s->have_anchor ? p->planned_count : 0;
        }
        if (s->pr->present(s->pr_ctx, &req) < 0) break;
    }
    /* Wait for the last statistics, as a frame loop would. */
    {
        int64_t end = psyscr__now() + (int64_t)(4 * s->period_f) + 50000000;
        psyscr_vblank newest;
        newest.t_ns = 0;
        if (s->pr->acquire(s->pr_ctx, end, &newest) == PSYSCR_OK) s->slot_held = 1;
        while (psyscr__pending(s) > 0 && psyscr__now() < end) {
            psyscr__drain(s);
            if (psyscr__pending(s) == 0) break;
            PSYSCR__SLEEP_UNTIL((int64_t)(psyscr__now() + 200000), 0);
        }
    }
    for (i = 0; i < PSYSCR__MAX_PEND; i++) s->pend[i].used = 0;
    s->warming = 0;
    s->have_last = 0;
    memset(&s->last, 0, sizeof s->last);
    if (!s->have_anchor) {   /* the presenter gives no vblank times */
        s->vb_t = psyscr__now();
        s->vb_count = 0;
        s->ref_t = s->vb_t;
        s->ref_count = 0;
        s->have_anchor = 1;
        s->caps.hw_onset = false;
    }
}

#if !defined(PSYSCR_NO_SDL)
static void psyscr__restamp_correlate(psyscr_screen* s) {
    psyrt_corr_desc d;
    psyrt_corr c;
    memset(&d, 0, sizeof d);
    d.read = SDL_GetTicksNS;
    if (psyrt_correlate(&d, &c) > 0 && (s->sdl_width == 0 || c.width_ns <= 2 * s->sdl_width)) {
        s->sdl_rt = (int64_t)c.rt_ns;
        s->sdl_ticks = (int64_t)c.other;
        s->sdl_width = c.width_ns;
        psyscr__a_store64(&psyscr__ab.sdl_off, s->sdl_rt - s->sdl_ticks);
    }
    s->sdl_corr_t = psyscr__now();
}

static uint32_t psyscr__mods_of(SDL_Keymod m) {
    return ((m & SDL_KMOD_SHIFT) ? PSYSCR_MOD_SHIFT : 0u) | ((m & SDL_KMOD_CTRL) ? PSYSCR_MOD_CTRL : 0u) |
           ((m & SDL_KMOD_ALT) ? PSYSCR_MOD_ALT : 0u) | ((m & SDL_KMOD_GUI) ? PSYSCR_MOD_GUI : 0u);
}

/* SDL calls this as it queues each event, on the thread that queues it, so
 * the header sees every abort even when the caller reads the events before
 * begin(). It may run on SDL's raw-input thread: atomics only. */
static bool SDLCALL psyscr__abort_watch(void* ud, SDL_Event* e) {
    int64_t t;
    int i;
    (void)ud;
    if (!e) return true;
    t = (int64_t)e->common.timestamp + psyscr__a_load64(&psyscr__ab.sdl_off);
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if ((uint32_t)e->key.key == psyscr__ab.key) {
            if (psyscr__abort_edge(&psyscr__ab.sdl_held, e->type == SDL_EVENT_KEY_DOWN) &&
                psyscr__abort_key((uint32_t)e->key.key, psyscr__mods_of(e->key.mod), t, PSYSCR__AB_SDL)) {
#if defined(PSYSCR__DXGI)
                if (psyscr__wd.thread) psyscr__win.post_thread(psyscr__wd.tid, WM_APP, 0, 0);
#endif
            }
        } else if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat && e->key.key == SDLK_F4 && (e->key.mod & SDL_KMOD_ALT)) {
            psyscr__abort_push(PSYSCR_ABORT_ALT_F4, PSYSCR__AB_SDL, (uint32_t)e->key.key, psyscr__mods_of(e->key.mod), t);
        }
        break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        for (i = 0; i < PSYSCR__SLOTS; i++)
            if (psyscr__slot[i].s && psyscr__slot[i].win == (uint32_t)e->window.windowID) {
                psyscr__abort_push(PSYSCR_ABORT_CLOSE, PSYSCR__AB_SDL, 0, 0, t);
                break;
            }
        break;
    case SDL_EVENT_QUIT:
        psyscr__abort_push(PSYSCR_ABORT_QUIT, PSYSCR__AB_SDL, 0, 0, t);
        break;
    default:
        break;
    }
    return true;
}
static int psyscr__abort_windows;   /* open screens with a window: the watch's users */

/* The window icon: desc.icon_rgba, or the header's at 32 pixels with 16 and
 * 48 for other scales. SDL copies the surfaces. */
static const char* psyscr__set_icon(psyscr_screen* s, const psyscr_desc* d) {
    SDL_Surface* base = NULL;
    int k, ok = 1;
    if (d->icon_sdl) return NULL;
    if (d->icon_rgba) {
        base = SDL_CreateSurfaceFrom(d->icon_w, d->icon_h, SDL_PIXELFORMAT_RGBA32, (void*)(uintptr_t)d->icon_rgba, d->icon_w * 4);
        ok = base != NULL;
    } else {
        SDL_Surface* alt[3] = { NULL, NULL, NULL };
        for (k = 0; k < 3; k++) {
            alt[k] = SDL_CreateSurface(psyscr__icon_size[k][0], psyscr__icon_size[k][0], SDL_PIXELFORMAT_RGBA32);
            ok = ok && alt[k] && psyscr__icon_decode(k, (uint8_t*)alt[k]->pixels, alt[k]->pitch);
        }
        base = alt[1];
        if (ok) ok = SDL_AddSurfaceAlternateImage(base, alt[0]) && SDL_AddSurfaceAlternateImage(base, alt[2]);
        if (alt[0]) SDL_DestroySurface(alt[0]);
        if (alt[2]) SDL_DestroySurface(alt[2]);
    }
    if (ok) ok = SDL_SetWindowIcon(s->window, base);
    if (base) SDL_DestroySurface(base);
    return ok ? NULL : "the window icon";
}
#endif

PSYSCR_API bool psyscr_open(psyscr_screen* s, const psyscr_desc* desc) {
    psyscr_presenter_open in;
    psyscr_mode want;
    int rc;
    if (!s) return false;
    if (s->open) { psyscr__copy(s->error, sizeof s->error, "psy_screen: already open"); return false; }
    memset(s, 0, sizeof *s);
    if (!desc) { psyscr__copy(s->error, sizeof s->error, "psy_screen: NULL desc"); return false; }
    if (desc->vrr) {
        psyscr__copy(s->error, sizeof s->error, "psy_screen: variable refresh is refused until the panel "
                     "has passed the photodiode interval sweep and the luminance-versus-interval sweep");
        return false;
    }
    if (!(desc->lead == 0 || desc->lead == PSYSCR_LEAD_NONE || (desc->lead > 0 && desc->lead < 1))) {
        psyscr__copy(s->error, sizeof s->error, "psy_screen: desc.lead must be 0, in (0, 1) or PSYSCR_LEAD_NONE");
        return false;
    }
    if (desc->min_tier < 0 || desc->min_tier > PSYSCR_TIER_3) {
        psyscr__copy(s->error, sizeof s->error, "psy_screen: desc.min_tier must be 0 (off) or 1 to 3");
        return false;
    }
    if (desc->patch.size < 0 || desc->patch.corner < 0 || desc->patch.corner > 3 ||
        desc->sim_period_ns < 0 || desc->window_w < 0 || desc->window_h < 0) {
        psyscr__copy(s->error, sizeof s->error, "psy_screen: a negative size, period or corner in desc");
        return false;
    }
    if (desc->icon_rgba && (desc->icon_w < 1 || desc->icon_w > 256 || desc->icon_h < 1 || desc->icon_h > 256)) {
        psyscr__copy(s->error, sizeof s->error, "psy_screen: desc.icon_w and icon_h must be 1 to 256 with desc.icon_rgba");
        return false;
    }
    if (desc->panic && (desc->panic_presses < 0 || desc->panic_window_ms < 0 || desc->panic_grace_ms < 0)) {
        psyscr__copy(s->error, sizeof s->error, "psy_screen: a negative desc.panic_presses, panic_window_ms or panic_grace_ms");
        return false;
    }
    if (desc->panic && desc->abort_keys.off) {
        psyscr__copy(s->error, sizeof s->error, "psy_screen: desc.panic needs the abort combination (desc.abort_keys.off is set)");
        return false;
    }
    if (desc->panic && !psyscr__vk_known(desc->abort_keys.key ? desc->abort_keys.key : PSYSCR_KEY_ESCAPE)) {
        psyscr__copy(s->error, sizeof s->error, "psy_screen: desc.panic takes Esc, F1 to F12, a letter or a digit "
                     "as desc.abort_keys.key");
        return false;
    }
    s->backend = desc->backend;
    if (s->backend == PSYSCR_BACKEND_AUTO) {
#if defined(PSYSCR__DXGI)
        s->backend = PSYSCR_BACKEND_DXGI_FLIP;
#else
        psyscr__copy(s->error, sizeof s->error, "psy_screen: no display backend on this platform in v0.1 "
                     "(Linux X11, Wayland, macOS and the web are stubs); BACKEND_SIM runs without a display");
        return false;
#endif
    }
    switch (s->backend) {
    case PSYSCR_BACKEND_SIM:
        s->pr = &psyscr__sim_presenter;
        s->pr_ctx = s->backend_mem;
        break;
#if defined(PSYSCR__DXGI)
    case PSYSCR_BACKEND_DXGI_FLIP:
        s->pr = &psyscr__dxgi_presenter;
        s->pr_ctx = s->backend_mem;
        break;
    case PSYSCR_BACKEND_COMPOSITION:
        s->pr = &psyscr__comp_presenter;
        s->pr_ctx = s->backend_mem;
        break;
#endif
    case PSYSCR_BACKEND_CUSTOM:
        if (!desc->presenter || desc->presenter->version != PSYSCR_PRESENTER_VERSION ||
            !desc->presenter->open || !desc->presenter->acquire || !desc->presenter->present ||
            !desc->presenter->completions) {
            psyscr__copy(s->error, sizeof s->error, "psy_screen: desc.presenter missing, of another version, "
                         "or without open, acquire, present or completions");
            return false;
        }
        s->pr = desc->presenter;
        s->pr_ctx = desc->presenter_ctx;
        break;
    default:
        psyscr__set_error(s->error, sizeof s->error, "psy_screen: backend %d is not implemented in v0.1 "
                          "(DXGI_FLIP, SIM and CUSTOM are)", (int)s->backend);
        return false;
    }

    {
        const char* e = psyscr__abort_open(s, desc);
        if (e) {
            psyscr__set_error(s->error, sizeof s->error, "psy_screen: %s", e);
            s->pr = NULL;
            return false;
        }
    }
    memset(&want, 0, sizeof want);
    memset(&in, 0, sizeof in);
    in.d3d11_video = desc->d3d11_video ? 1 : 0;
    in.display = desc->display;
    in.angle_dir = desc->angle_dir;
    in.sim_period_ns = desc->sim_period_ns;
    in.mode = &want;
    in.n_codes = desc->n_codes;
    in.want_gpu_done = desc->trigger_fence ? 1 : 0;
    in.code_checked = &s->code_checked;
    in.code_failed = &s->code_failed;
    in.code_selftest = &s->code_selftest;
    s->caps.backend = s->backend;

#if !defined(PSYSCR_NO_SDL)
    if (s->pr->needs_window) {
        SDL_DisplayID id;
        const SDL_DisplayMode* dm;
        SDL_DisplayMode match;
        bool explicit_mode = desc->mode.w || desc->mode.h || desc->mode.refresh_num;
        SDL_PropertiesID props;
        /* The raw-input keyboard: stamped when Raw Input delivers the key,
         * not with the message time, which counts in the system tick and was
         * measured up to 15.7 ms early. Default priority, so a caller's own
         * setting of the hint wins. */
        SDL_SetHintWithPriority(SDL_HINT_WINDOWS_RAW_KEYBOARD, "1", SDL_HINT_DEFAULT);
        /* SDL would turn Alt+F4 into a close request; the header reports it
         * as its own reason (ABORT). */
        SDL_SetHintWithPriority(SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4, "0", SDL_HINT_DEFAULT);
        if (!psyscr__video_up()) {
            psyscr__set_error(s->error, sizeof s->error, "psy_screen: SDL video: %s", SDL_GetError());
            psyscr_close(s);
            return false;
        }
        s->sdl_video = 1;
        id = psyscr__display_id(desc->display);
        dm = SDL_GetDesktopDisplayMode(id);
        if (!dm) {
            psyscr__set_error(s->error, sizeof s->error, "psy_screen: display %u: %s", (unsigned)desc->display, SDL_GetError());
            psyscr_close(s);
            return false;
        }
        match = *dm;
        if (explicit_mode) {
            /* An exact listed mode or nothing: no implicit conversion. */
            int i, n = 0, found = 0;
            SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(id, &n);
            for (i = 0; modes && i < n; i++) {
                const SDL_DisplayMode* m = modes[i];
                if ((desc->mode.w && m->w != desc->mode.w) || (desc->mode.h && m->h != desc->mode.h)) continue;
                if (desc->mode.refresh_num &&
                    (int64_t)m->refresh_rate_numerator * (desc->mode.refresh_den ? desc->mode.refresh_den : 1) !=
                    (int64_t)desc->mode.refresh_num * m->refresh_rate_denominator) continue;
                match = *m; found = 1; break;
            }
            SDL_free(modes);
            if (!found) {
                psyscr__copy(s->error, sizeof s->error, "psy_screen: desc.mode is not a mode of this display "
                             "(psyscr_modes lists them)");
                psyscr_close(s);
                return false;
            }
        }
        props = SDL_CreateProperties();
        SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "psy_screen");
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, (Sint64)SDL_WINDOWPOS_CENTERED_DISPLAY(id));
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, (Sint64)SDL_WINDOWPOS_CENTERED_DISPLAY(id));
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER,
                              desc->windowed ? (desc->window_w ? desc->window_w : 800) : match.w);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER,
                              desc->windowed ? (desc->window_h ? desc->window_h : 600) : match.h);
        SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, !desc->windowed);
        SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
        s->window = SDL_CreateWindowWithProperties(props);
        SDL_DestroyProperties(props);
        if (!s->window) {
            psyscr__set_error(s->error, sizeof s->error, "psy_screen: window: %s", SDL_GetError());
            psyscr_close(s);
            return false;
        }
        if (!desc->windowed && explicit_mode &&
            (match.w != dm->w || match.h != dm->h || match.refresh_rate_numerator != dm->refresh_rate_numerator)) {
            if (!SDL_SetWindowFullscreenMode(s->window, &match)) {
                psyscr__set_error(s->error, sizeof s->error, "psy_screen: mode switch: %s", SDL_GetError());
                psyscr_close(s);
                return false;
            }
        }
        {
            const char* e = psyscr__set_icon(s, desc);
            if (e) {
                psyscr__set_error(s->error, sizeof s->error, "psy_screen: %s: %s", e, SDL_GetError());
                psyscr_close(s);
                return false;
            }
#if defined(PSYSCR__DXGI)
            if (!desc->icon_sdl && !desc->icon_rgba) {
                s->hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(s->window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
                psyscr__win_icons(s);
            }
#endif
        }
        if (!desc->windowed) SDL_SyncWindow(s->window);
        SDL_RaiseWindow(s->window);
        /* SDL routes keys through the message path while text input runs. */
        SDL_StopTextInput(s->window);
        if (!desc->windowed && !desc->show_cursor) { SDL_HideCursor(); s->cursor_hidden = 1; }
        {   /* the mode obtained, from the OS, not from the request */
            const SDL_DisplayMode* got = desc->windowed ? SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(s->window))
                                                        : SDL_GetWindowFullscreenMode(s->window);
            if (!got) got = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(s->window));
            if (got) psyscr__mode_from_sdl(got, &want);
            {
                int pw = 0, ph = 0;
                SDL_GetWindowSizeInPixels(s->window, &pw, &ph);
                want.w = pw; want.h = ph;
            }
        }
        in.window = s->window;
        psyscr__restamp_correlate(s);
        s->hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(s->window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
        psyscr__slot_take(s, (uint32_t)SDL_GetWindowID(s->window));
        if (s->abort_slot && psyscr__abort_windows++ == 0 && !SDL_AddEventWatch(psyscr__abort_watch, NULL)) {
            psyscr__set_error(s->error, sizeof s->error, "psy_screen: SDL_AddEventWatch: %s", SDL_GetError());
            psyscr_close(s);
            return false;
        }
    }
#endif
    if (!s->abort_slot) psyscr__slot_take(s, 0);

    rc = s->pr->open(s->pr_ctx, &in, &s->caps, s->error, sizeof s->error);
    s->pr_open = rc >= 0;
    if (rc < 0) {
        if (!s->error[0]) psyscr__set_error(s->error, sizeof s->error, "psy_screen: %s open: %s", s->pr->name, psyscr_strerror(rc));
        s->pr = NULL;
        psyscr_close(s);
        return false;
    }
    s->caps.backend = s->backend;
    if (s->caps.mode.period_ns == 0) s->caps.mode = want;
    if (s->caps.period_ns == 0) s->caps.period_ns = s->caps.mode.period_ns;
    if (s->caps.period_ns <= 0) s->caps.period_ns = 16666667;
    if (s->caps.kind == 0) s->caps.kind = PSYSCR_FIXED_GRID;
    s->open = 1;
    s->ring = desc->ring;
    s->display_index = desc->display_index;
    s->lead = desc->lead == 0 ? 0.5 : desc->lead;
    s->offset = desc->onset_offset_ns;
    s->patch = desc->patch;
    s->nominal_f = (double)s->caps.period_ns;
    s->period_f = s->nominal_f;
    s->depth = 1;
    s->depth_need = 1;
    s->path = PSYSCR_PATH_UNKNOWN;
    s->min_tier = desc->min_tier;
    psyscr__update_lead(s);
    psyscr__gl_load(s);
    {
        static uint32_t generation;
        s->gl_generation = ++generation;
    }
    s->lock_ok = psyscr__lock_init(s);
    s->armed_wake = INT64_MAX;
    {
        const char* e = psyscr__codes_open(s, desc);
        if (!e && (desc->n_triggers < 0 || desc->n_triggers > PSYSCR_MAX_TRIGGERS || (desc->n_triggers && !desc->triggers)))
            e = "desc.n_triggers must be 0 to PSYSCR_MAX_TRIGGERS, with desc.triggers";
        if (!e && desc->n_triggers > 0) {
            int i;
            for (i = 0; i < desc->n_triggers; i++) s->trig[i] = desc->triggers[i];
            s->n_trig = desc->n_triggers;
            s->trig_fence = desc->trigger_fence ? 1 : 0;
            s->trig_cpu = desc->trigger_cpu > 0 ? desc->trigger_cpu : desc->trigger_cpu < 0 ? -1 : 0;
            s->trig_spin = desc->trigger_spin_ns;
            s->worker_on = PSYSCR__WORKER_START(s);
            if (!s->worker_on) e = "the trigger worker did not start (built with PSYRT_NO_THREADS?)";
            s->trig_on = s->worker_on;
        }
#if defined(PSYSCR__DXGI)
        if (!e && (s->backend == PSYSCR_BACKEND_DXGI_FLIP || s->backend == PSYSCR_BACKEND_COMPOSITION)) {
            psyscr__win_display_state(s, desc);
            if (desc->codes_strict && s->n_codes &&
                (psyscr_code_risk(s) & ~(uint16_t)PSYSCR_CODE_RISK_COMPOSED))
                e = "desc.codes_strict: the display may change code pixels (see the describe line)";
        }
        /* Armed after the gamma ramp is taken, so a panic finds it. A
         * window has no ramp or mode to put back, and its user has the
         * desktop. */
        if (!e && desc->panic && (s->backend == PSYSCR_BACKEND_DXGI_FLIP || s->backend == PSYSCR_BACKEND_COMPOSITION)) {
            if (!desc->windowed || PSYSCR__PANIC_WINDOWED) e = psyscr__wd_arm(s, desc);
            else s->panic_armed = 2;
        }
#endif
        if (!e && desc->panic && !s->panic_armed) s->panic_armed = 3;
        if (e) {
            psyscr__set_error(s->error, sizeof s->error, "psy_screen: %s", e);
            psyscr_close(s);
            return false;
        }
    }
    {
        psyrt_event ev;
        memset(&ev, 0, sizeof ev);
        ev.source = (uint16_t)PSYRT_SRC_SCREEN;
        ev.kind = (uint16_t)PSYSCR_EV_MODE;
        ev.aux = s->display_index;
        ev.u.i32[0] = s->caps.mode.w;
        ev.u.i32[1] = s->caps.mode.h;
        ev.u.i32[2] = s->caps.mode.refresh_num;
        ev.u.i32[3] = s->caps.mode.refresh_den;
        ev.u.i32[4] = (int32_t)s->backend;
        psyscr__push(s, &ev);
    }
    psyscr__warmup(s);
    return true;
}

PSYSCR_API void psyscr_close(psyscr_screen* s) {
    if (!s) return;
    if (s->pr && s->pr_open && s->open) {
        int64_t end = psyscr__now() + 100000000;
        psyscr_vblank newest;
        /* Let the last flip complete, so its record reaches the ring. */
        while (psyscr__pending(s) > 0 && psyscr__now() < end) {
            if (!s->slot_held && s->pr->acquire(s->pr_ctx, end, &newest) == PSYSCR_OK) s->slot_held = 1;
            psyscr__drain(s);
            if (psyscr__pending(s) == 0) break;
            PSYSCR__SLEEP_UNTIL((int64_t)(psyscr__now() + 500000), 0);
        }
        {
            int i;
            for (i = 0; i < PSYSCR__MAX_PEND; i++) if (s->pend[i].used) psyscr__estimate(s, &s->pend[i]);
        }
    }
    if (s->worker_on) {
        PSYSCR__WORKER_STOP(s);
        psyscr__trig_run(s, psyscr__now(), 1);   /* anything still armed */
        s->worker_on = 0;
    }
    if (s->trig_on) {
        int i;
        for (i = 0; i < PSYSCR_MAX_JOBS; i++) s->job[i].flip_done = 1;
        psyscr__trig_emit(s);
        s->trig_on = 0;
    }
    if (s->lock_ok) { psyscr__lock_free(s); s->lock_ok = 0; }
#if defined(PSYSCR__DXGI)
    psyscr__wd_disarm(s);
    psyscr__win_gamma_release(s);
#endif
    s->panic_armed = 0;
#if !defined(PSYSCR_NO_SDL)
    if (s->abort_slot && psyscr__slot[s->abort_slot - 1].win && --psyscr__abort_windows == 0)
        SDL_RemoveEventWatch(psyscr__abort_watch, NULL);
#endif
    psyscr__slot_free(s);
    psyscr__abort_close(s);
    if (s->pr && s->pr_open && s->pr->close) s->pr->close(s->pr_ctx);
    s->pr = NULL;
    s->pr_open = 0;
#if !defined(PSYSCR_NO_SDL)
    if (s->cursor_hidden) SDL_ShowCursor();
    if (s->window) SDL_DestroyWindow(s->window);
#if defined(PSYSCR__DXGI)
    {   /* after the window that showed them */
        int k;
        for (k = 0; k < 2; k++) {
            if (s->win_icon[k] && psyscr__win.destroy_icon) psyscr__win.destroy_icon((HICON)s->win_icon[k]);
            s->win_icon[k] = NULL;
        }
    }
#endif
    if (s->sdl_video) psyscr__video_down();
#endif
    s->window = NULL;
    s->sdl_video = 0;
    s->cursor_hidden = 0;
    s->open = 0;
    s->begun = 0;
}

PSYSCR_API const char* psyscr_error(const psyscr_screen* s) { return s ? s->error : "psy_screen: NULL screen"; }
PSYSCR_API bool psyscr_is_open(const psyscr_screen* s) { return s && s->open; }

PSYSCR_API void psyscr_get_caps(const psyscr_screen* s, psyscr_caps* out) {
    if (!out) return;
    if (!s || !s->open) { memset(out, 0, sizeof *out); return; }
    *out = s->caps;
    out->worst_tier = (psyscr_tier)s->worst_tier;
}

static const char* psyscr__path_name(uint16_t p) {
    switch (p) {
    case PSYSCR_PATH_COMPOSED: return "composed";
    case PSYSCR_PATH_OVERLAY: return "overlay";
    case PSYSCR_PATH_INDEPENDENT: return "independent";
    case PSYSCR_PATH_SIMULATED: return "simulated";
    default: return "unknown";
    }
}

PSYSCR_API int psyscr_describe(const psyscr_screen* s, char* buf, size_t cap) {
    char extra[448], abort_s[40];
    double hz, ppm;
    if (!s || !buf || cap == 0) return PSYSCR_ERR_ARG;
    if (!s->open) return snprintf(buf, cap, "psy_screen: closed");
    extra[0] = '\0';
    if (s->pr->describe) s->pr->describe(s->pr_ctx, extra, sizeof extra);
    hz = s->period_f > 0 ? 1e9 / s->period_f : 0;
    ppm = (s->period_f / s->nominal_f - 1.0) * 1e6;
    if (s->n_codes > 0 || s->n_trig > 0 || s->os_color[0]) {
        size_t n = strlen(extra);
        uint16_t risk = psyscr_code_risk(s);
        if (s->os_color[0]) n += (size_t)snprintf(extra + n, n < sizeof extra ? sizeof extra - n : 0, " %s", s->os_color);
        if (s->n_codes > 0 && n < sizeof extra)
            n += (size_t)snprintf(extra + n, sizeof extra - n, " codes=%d selftest=%s%s", s->n_codes,
                                  s->code_selftest > 0 ? "pass" : s->code_selftest < 0 ? "FAIL" : "not-run",
                                  risk ? " WARNING=codes-at-risk" : "");
        if (s->n_trig > 0 && n < sizeof extra) {
            char cpus[32] = "";
            /* chosen before the call: a directive inside macro arguments is undefined (clang) */
#if defined(PSYSCR__REAL_WORKER)
            const char* wpol = psyrt_policy_name(psyrt_worker_policy(&s->worker));
#else
            const char* wpol = "test";
#endif
            if (s->trig_cores) snprintf(cpus, sizeof cpus, ":cpus=0x%llx", (unsigned long long)s->trig_mask);
            snprintf(extra + n, sizeof extra - n, " triggers=%d worker=%s%s%s fence=%s", s->n_trig, wpol,
                     s->trig_cores == 'E' ? "/E-cores" : s->trig_cores == 'N' ? "/pinned" : "/psy_rt",
                     cpus, !s->trig_fence ? "off" : (s->pr->gpu_done ? "on" : "unavailable"));
        }
    }
    {   /* the abort combination, as an operator reads it */
        uint32_t k = psyscr__ab.key;
        size_t n = 0;
        if (psyscr__ab.off) n = (size_t)snprintf(abort_s, sizeof abort_s, "off");
        else {
            n += (size_t)snprintf(abort_s + n, sizeof abort_s - n, "%s%s%s%s",
                                  (psyscr__ab.mods & PSYSCR_MOD_CTRL) ? "ctrl+" : "", (psyscr__ab.mods & PSYSCR_MOD_ALT) ? "alt+" : "",
                                  (psyscr__ab.mods & PSYSCR_MOD_SHIFT) ? "shift+" : "", (psyscr__ab.mods & PSYSCR_MOD_GUI) ? "gui+" : "");
            if (k == PSYSCR_KEY_ESCAPE) snprintf(abort_s + n, sizeof abort_s - n, "esc");
            else if (k >= 0x4000003Au && k <= 0x40000045u) snprintf(abort_s + n, sizeof abort_s - n, "f%u", (unsigned)(k - 0x4000003Au + 1));
            else if (k > 32 && k < 127) snprintf(abort_s + n, sizeof abort_s - n, "%c", (char)k);
            else snprintf(abort_s + n, sizeof abort_s - n, "key0x%x", (unsigned)k);
        }
    }
    return snprintf(buf, cap, "psy_screen %s: backend=%s %s mode=%dx%d@%d/%d measured=%.4fHz(%+.0fppm) "
                    "path=%s depth=%d lead=%.2f worst_tier=%d abort=%s panic=%s%s%s",
                    PSYSCR_VERSION_STRING, s->pr->name, extra, s->caps.mode.w, s->caps.mode.h,
                    s->caps.mode.refresh_num, s->caps.mode.refresh_den, hz, ppm,
                    psyscr__path_name(s->path), s->depth, s->lead < 0 ? -1.0 : s->lead, s->worst_tier, abort_s,
                    s->panic_armed == 1 ? "armed" : s->panic_armed == 2 ? "idle(windowed)" : s->panic_armed == 3 ? "n/a" : "off",
                    s->unstable ? " WARNING=off-grid-vblanks" : "",
                    (ppm > 200 || ppm < -200) ? " WARNING=period-differs-from-mode" : "");
}

static void psyscr__pump(psyscr_screen* s) {
#if !defined(PSYSCR_NO_SDL)
    if (s->window && !s->polled) {   /* psyscr_poll() pumped already this frame */
        PSYRT_ZONE(z_pump, "psyscr.pump");
        SDL_PumpEvents();
        PSYRT_ZONE_END(z_pump);
    }
#endif
    s->polled = 0;
}

PSYSCR_API int psyscr_begin(psyscr_screen* s, psyscr_frame* f) {
    int64_t t0, now, wait;
    int rc;
    PSYRT_ZONE(z_begin, "psyscr.begin");
    if (!s || !f) { PSYRT_ZONE_END(z_begin); return PSYSCR_ERR_ARG; }
    if (!s->open) { PSYRT_ZONE_END(z_begin); return PSYSCR_ERR_CLOSED; }
    if (s->begun) { PSYRT_ZONE_END(z_begin); return PSYSCR_ERR_ORDER; }
    psyscr__pump(s);
    if (psyscr__abort_take(s, f)) { PSYRT_ZONE_END(z_begin); return PSYSCR_QUIT; }
    t0 = psyscr__now();
    if (!s->slot_held) {
        psyscr_vblank newest;
        PSYRT_ZONE(z_wait, "psyscr.wait");
        newest.t_ns = 0;
        rc = s->pr->acquire(s->pr_ctx, t0 + (int64_t)(8 * s->period_f) + 200000000, &newest);
        PSYRT_ZONE_END(z_wait);
        if (rc < 0) { PSYRT_ZONE_END(z_begin); return rc; }
        if (newest.t_ns && newest.count > s->vb_count) psyscr__anchor(s, newest.t_ns, newest.count);
    } else if (s->pr->bind) {
        s->pr->bind(s->pr_ctx);
    }
    s->slot_held = 0;
    wait = psyscr__now() - t0;
    {
        PSYRT_ZONE(z_stats, "psyscr.stats");
        psyscr__drain(s);
        PSYRT_ZONE_END(z_stats);
    }
    if (psyscr__pending(s) >= 3) {   /* statistics stopped coming */
        int i;
        for (i = 0; i < PSYSCR__MAX_PEND; i++) if (s->pend[i].used) psyscr__estimate(s, &s->pend[i]);
    }
    psyscr__trig_emit(s);
#if !defined(PSYSCR_NO_SDL)
    if (s->window && t0 - s->sdl_corr_t > 10000000000LL) psyscr__restamp_correlate(s);
#endif
    now = psyscr__now();
    s->pred_count = psyscr__count_at(s, now + s->margin_ns) + s->depth;
    if (s->pred_count <= s->prev_shown) s->pred_count = s->prev_shown + 1;
    /* Never the vblank the last frame was planned for, even when that
     * frame is done: one shown a vblank early (EARLY, when the composed
     * path's depth falls) has left its planned vblank, and the next frame
     * planned there got the same onset (docs/psy_screen.md). */
    if (s->pred_count <= s->last_planned) s->pred_count = s->last_planned + 1;
    {   /* one frame per vblank, as flip_at() plans it */
        int i;
        for (i = 0; i < PSYSCR__MAX_PEND; i++)
            if (s->pend[i].used && s->pend[i].planned_count >= s->pred_count) s->pred_count = s->pend[i].planned_count + 1;
    }
    f->onset = psyscr__time_of(s, s->pred_count) + s->offset;
    f->period = (int64_t)llround(s->period_f);
    f->index = s->index;
    f->vblank = s->pred_count;
    f->last = s->have_last ? &s->last : NULL;
    memcpy(s->fin_out, s->fin, sizeof(psyscr_record) * (size_t)s->n_fin);
    s->n_fin_out = s->n_fin;
    s->n_fin = 0;
    f->done = s->fin_out;
    f->n_done = s->n_fin_out;
    f->done_lost = s->fin_lost;
    memset(s->acc, 0, sizeof s->acc);
    s->acc[PSYSCR_PHASE_SWAP] = psyscr__sat32(wait);
    s->acc[PSYSCR_PHASE_GPU] = PSYSCR_PHASE_UNKNOWN;
    s->mark_t = psyscr__now();
    s->begun = 1;
    PSYRT_ZONE_END(z_begin);
    return PSYSCR_OK;
}

PSYSCR_API void psyscr_mark(psyscr_screen* s, int phase) {
    int64_t now;
    if (!s || !s->begun || phase < 0 || phase >= PSYSCR_PHASE_GPU) return;
    now = psyscr__now();
    s->acc[phase] = psyscr__sat32((int64_t)s->acc[phase] + (now - s->mark_t));
    s->mark_t = now;
}

PSYSCR_API int psyscr_flip_at(psyscr_screen* s, int64_t t, psyscr_record* out) {
    int64_t now, tg, count_t, earliest, planned, t_call;
    psyscr__pend* p = NULL;
    psyscr_present_req req;
    const psyscr_code_draw* codes = NULL;
    uint32_t code_vals[PSYSCR_MAX_CODES];
    int i, rc;
    uint16_t flags = PSYSCR_FLIP_PENDING;
    PSYRT_ZONE(z_flip, "psyscr.flip");
    if (!s) { PSYRT_ZONE_END(z_flip); return PSYSCR_ERR_ARG; }
    if (!s->open) { PSYRT_ZONE_END(z_flip); return PSYSCR_ERR_CLOSED; }
    if (!s->begun) { PSYRT_ZONE_END(z_flip); return PSYSCR_ERR_ORDER; }
    now = psyscr__now();
    s->acc[PSYSCR_PHASE_DRAW] = psyscr__sat32((int64_t)s->acc[PSYSCR_PHASE_DRAW] + (now - s->mark_t));
    if (s->on_present) {
        psyscr_present_info pi;
        memset(&pi, 0, sizeof pi);
        pi.index = s->index;
        pi.onset = psyscr__time_of(s, s->pred_count) + s->offset;
        pi.w = s->caps.mode.w;
        pi.h = s->caps.mode.h;
        pi.rows_top_down = s->backend == PSYSCR_BACKEND_DXGI_FLIP || s->backend == PSYSCR_BACKEND_COMPOSITION;
        s->on_present(s->on_present_ctx, &pi);
        s->gl_epoch++;
    }
    {
        PSYRT_ZONE(z_patch, "psyscr.patch");
        if (s->n_codes > 0) codes = psyscr__codes_build(s, code_vals);
        if (!s->pr->draws_patch) {
            psyscr__draw_patch(s);
            if (codes) psyscr__gl_codes(s, codes, s->n_codes);
        }
        PSYRT_ZONE_END(z_patch);
    }

    tg = t - s->offset;
    count_t = psyscr__snap(s, tg);
    earliest = psyscr__count_at(s, psyscr__now() + s->margin_ns) + s->depth;
    if (earliest <= s->prev_shown) earliest = s->prev_shown + 1;
    if (earliest <= s->last_planned) earliest = s->last_planned + 1;
    for (i = 0; i < PSYSCR__MAX_PEND; i++)   /* one frame per vblank */
        if (s->pend[i].used && s->pend[i].planned_count >= earliest) earliest = s->pend[i].planned_count + 1;
    planned = count_t;
    if (count_t < earliest) { planned = earliest; flags |= PSYSCR_FLIP_LATE_TARGET; }

    /* Wait until the present can no longer show early: DXGI shows a frame
     * at the first vblank it can. That covers a later vblank asked for, and
     * also a call within the margin before a vblank: the margin planned the
     * flip after that vblank, and a present made before it showed a vblank
     * early (EARLY set; a preempted frame loop hit this). */
    if (!s->caps.native_target) {
        int64_t guard = (int64_t)(s->period_f / 8);
        int64_t wake;
        if (guard > 500000) guard = 500000;
        wake = psyscr__time_of(s, planned - s->depth) + guard;
        if (wake > psyscr__now()) {
            PSYRT_ZONE(z_hold, "psyscr.hold");
            /* DXGI keeps only the newest flip's statistic, and it lags the
             * vblank by up to about 2 ms on a composed path, so a long wait
             * reads it once per vblank or the next present would hide it. */
            for (;;) {
                int64_t now2 = psyscr__now();
                int64_t poll = psyscr__time_of(s, psyscr__count_at(s, now2)) + 2500000;
                if (poll <= now2) poll = psyscr__time_of(s, psyscr__count_at(s, now2) + 1) + 2500000;
                if (poll + 1000000 >= wake) break;
                PSYSCR__SLEEP_UNTIL((int64_t)poll, 0);
                psyscr__drain_overdue(s);
            }
            PSYSCR__SLEEP_UNTIL((int64_t)wake, PSYRT_DEFAULT_SPIN_NS);
            PSYRT_ZONE_END(z_hold);
        }
    }

    for (i = 0; i < PSYSCR__MAX_PEND; i++) if (!s->pend[i].used) { p = &s->pend[i]; break; }
    if (!p) {   /* never with one frame in flight; keep the newest */
        psyscr__pend* oldest = &s->pend[0];
        for (i = 1; i < PSYSCR__MAX_PEND; i++) if (s->pend[i].id < oldest->id) oldest = &s->pend[i];
        psyscr__estimate(s, oldest);
        p = oldest;
    }
    memset(p, 0, sizeof *p);
    p->used = 1;
    p->id = ++s->next_id;
    p->planned_count = planned;
    s->last_planned = planned;
    p->rec.index = s->index;
    p->rec.target = t;
    p->rec.planned = psyscr__time_of(s, planned) + s->offset;
    p->rec.flags = flags;
    /* A present without a target shows at the first vblank it can, held
     * or not, so every one measures the depth; one with a target only
     * when the target was the first vblank it could make. */
    p->asap = planned == earliest || !s->caps.native_target;

    memset(&req, 0, sizeof req);
    req.present_id = p->id;
    req.target_count = planned;
    req.target_ns = psyscr__time_of(s, planned);
    req.hold = 1;
    if (s->patch.on && s->pr->draws_patch) {
        int px, py, ps;
        psyscr__patch_rect(s, &px, &py, &ps);
        req.patch_on = 1;
        req.patch_x = px; req.patch_y = py; req.patch_w = ps; req.patch_h = ps;
        req.patch_value = s->patch_value;
    }
    if (codes && s->pr->draws_patch) {
        req.codes = codes;
        req.n_codes = s->n_codes;
        req.verify = s->verify_every > 0 && (s->index % s->verify_every) == 0;
    }
    if (codes) memcpy(p->code_vals, code_vals, sizeof(uint32_t) * (size_t)s->n_codes);
    psyscr__drain_overdue(s);
    t_call = psyscr__now();
    p->count_at_present = psyscr__count_at(s, t_call);
    {
        PSYRT_ZONE(z_present, "psyscr.present");
        rc = s->pr->present(s->pr_ctx, &req);
        PSYRT_ZONE_END(z_present);
    }
    p->t_ret = psyscr__now();
    s->acc[PSYSCR_PHASE_SWAP] = psyscr__sat32((int64_t)s->acc[PSYSCR_PHASE_SWAP] + (psyscr__now() - t_call));
    memcpy(p->rec.phase_ns, s->acc, sizeof s->acc);
    if (rc < 0) {
        p->used = 0;
        s->begun = 0;
        s->n_req = 0;
        s->index++;
        PSYRT_ZONE_END(z_flip);
        return rc;
    }
    if (s->n_codes > 0) psyscr__codes_advance(s);
    if (s->n_req > 0) {
        PSYRT_ZONE(z_arm, "psyscr.trigarm");
        psyscr__trig_arm_flip(s, p);
        PSYRT_ZONE_END(z_arm);
    }
    if (out) *out = p->rec;
    s->begun = 0;
    s->index++;
    PSYRT_ZONE_END(z_flip);
    return PSYSCR_OK;
}

PSYSCR_API int psyscr_flip(psyscr_screen* s) {
    if (!s) return PSYSCR_ERR_ARG;
    if (!s->open) return PSYSCR_ERR_CLOSED;
    if (!s->begun) return PSYSCR_ERR_ORDER;
    return psyscr_flip_at(s, psyscr__time_of(s, s->pred_count) + s->offset, NULL);
}

PSYSCR_API int psyscr_wait_flip(psyscr_screen* s, psyscr_record* out) {
    int64_t end;
    if (!s) return PSYSCR_ERR_ARG;
    if (!s->open) return PSYSCR_ERR_CLOSED;
    if (s->begun) return PSYSCR_ERR_ORDER;
    end = psyscr__now() + (int64_t)(8 * s->period_f) + 200000000;
    while (psyscr__pending(s) > 0) {
        if (!s->slot_held) {
            psyscr_vblank newest;
            int rc;
            newest.t_ns = 0;
            rc = s->pr->acquire(s->pr_ctx, end, &newest);
            if (rc < 0) return rc;
            s->slot_held = 1;
        }
        psyscr__drain(s);
        if (psyscr__pending(s) == 0) break;
        if (psyscr__now() >= end) {
            int i;
            for (i = 0; i < PSYSCR__MAX_PEND; i++) if (s->pend[i].used) psyscr__estimate(s, &s->pend[i]);
            break;
        }
        PSYSCR__SLEEP_UNTIL((int64_t)(psyscr__now() + 100000), 0);
    }
    if (out) {
        if (!s->have_last) return PSYSCR_ERR_ORDER;
        *out = s->last;
    }
    return PSYSCR_OK;
}

PSYSCR_API void psyscr_set_patch(psyscr_screen* s, float v) {
    if (!s) return;
    s->patch_value = v < 0 ? 0.0f : (v > 1 ? 1.0f : v);
}

/* --- codes, triggers and hooks: the API -------------------------------------- */

PSYSCR_API int psyscr_code_frames(psyscr_screen* s, int slot, uint32_t rgb, int frames) {
    if (!s || !s->open || slot < 0 || slot >= s->n_codes || frames < PSYSCR_CODE_HOLD) return PSYSCR_ERR_ARG;
    if (s->code_slot[slot].kind == PSYSCR_CODE_ROW) {
        int k, off = s->code_off[slot];
        for (k = 0; k < s->code_slot[slot].w; k++) s->code_px[off + k] = rgb & 0xFFFFFFu;
    }
    s->code_val[slot] = rgb & 0xFFFFFFu;
    s->code_left[slot] = frames;
    return PSYSCR_OK;
}

PSYSCR_API int psyscr_code(psyscr_screen* s, int slot, uint32_t rgb) {
    return psyscr_code_frames(s, slot, rgb, 1);
}

PSYSCR_API int psyscr_code_row(psyscr_screen* s, int slot, const uint32_t* px, int n, int frames) {
    int k, off;
    const psyscr_code_slot* c;
    if (!s || !s->open || slot < 0 || slot >= s->n_codes || !px || n < 0 || frames < PSYSCR_CODE_HOLD) return PSYSCR_ERR_ARG;
    c = &s->code_slot[slot];
    if (c->kind != PSYSCR_CODE_ROW || n > c->w) return PSYSCR_ERR_ARG;
    off = s->code_off[slot];
    for (k = 0; k < c->w; k++) s->code_px[off + k] = (k < n ? px[k] : c->rest) & 0xFFFFFFu;
    s->code_left[slot] = frames;
    return PSYSCR_OK;
}

PSYSCR_API psyscr_code_slot psyscr_slot_pixel_mode(void) {
    psyscr_code_slot c;
    memset(&c, 0, sizeof c);
    c.kind = PSYSCR_CODE_SOLID;
    c.w = 1; c.h = 1;
    return c;
}

PSYSCR_API uint32_t psyscr_pixel_mode_bits(uint32_t ttl24) { return ttl24 & 0xFFFFFFu; }

PSYSCR_API psyscr_code_slot psyscr_slot_psync(void) {
    psyscr_code_slot c;
    memset(&c, 0, sizeof c);
    c.kind = PSYSCR_CODE_ROW;
    c.x = 10; c.y = 0; c.w = 8; c.h = 1;
    return c;
}

PSYSCR_API void psyscr_psync_pattern(uint32_t out[8], uint8_t counter) {
    /* red, green, blue, yellow, magenta, cyan, white, then the counter in
     * green: Psychtoolbox's PsychDataPixx sequence */
    static const uint32_t seq[7] = { 0x0000FFu, 0x00FF00u, 0xFF0000u, 0x00FFFFu, 0xFF00FFu, 0xFFFF00u, 0xFFFFFFu };
    int i;
    if (!out) return;
    for (i = 0; i < 7; i++) out[i] = seq[i];
    out[7] = (uint32_t)counter << 8;
}

PSYSCR_API uint16_t psyscr_code_risk(const psyscr_screen* s) {
    uint16_t r;
    if (!s || !s->open) return 0;
    r = s->code_risk;
    if (s->code_selftest <= 0) r |= PSYSCR_CODE_RISK_UNVERIFIED;
    if (s->code_failed) r |= PSYSCR_CODE_RISK_VERIFY_FAILED;
    return r;
}

PSYSCR_API void psyscr_code_verify(const psyscr_screen* s, uint32_t* checked, uint32_t* failed) {
    if (checked) *checked = s ? s->code_checked : 0;
    if (failed) *failed = s ? s->code_failed : 0;
}

PSYSCR_API int psyscr_trigger(psyscr_screen* s, int channel, uint32_t code) {
    if (!s || !s->open || channel < 0 || channel >= s->n_trig) return PSYSCR_ERR_ARG;
    if (!s->begun) return PSYSCR_ERR_ORDER;
    if (s->n_req >= PSYSCR_MAX_JOBS) return PSYSCR_ERR_REFUSED;
    s->req_ch[s->n_req] = (uint16_t)channel;
    s->req_code[s->n_req] = code;
    s->n_req++;
    return PSYSCR_OK;
}

PSYSCR_API int psyscr_trigger_at(psyscr_screen* s, int channel, uint32_t code, int64_t t) {
    psyscr__job* j = NULL;
    int i;
    if (!s || !s->open || channel < 0 || channel >= s->n_trig) return PSYSCR_ERR_ARG;
    psyscr__lock(s);
    for (i = 0; i < PSYSCR_MAX_JOBS; i++) if (s->job[i].state == PSYSCR__J_FREE) { j = &s->job[i]; break; }
    if (!j) { psyscr__unlock(s); return PSYSCR_ERR_REFUSED; }
    memset(j, 0, sizeof *j);
    j->state = PSYSCR__J_ARMED;
    j->channel = (uint16_t)channel;
    j->code = code;
    j->frame = -1;
    j->flip_done = 1;   /* no flip to wait for */
    j->deadline = t + s->trig[channel].offset_ns;
    j->wake = j->deadline;
    psyscr__trig_rearm(s);
    psyscr__unlock(s);
    return PSYSCR_OK;
}

PSYSCR_API void psyscr_on_flip(psyscr_screen* s, psyscr_flip_fn fn, void* ctx) {
    if (!s) return;
    s->on_flip = fn;
    s->on_flip_ctx = ctx;
}

PSYSCR_API void psyscr_on_present(psyscr_screen* s, psyscr_present_fn fn, void* ctx) {
    if (!s) return;
    s->on_present = fn;
    s->on_present_ctx = ctx;
}

PSYSCR_API uint32_t psyscr_gl_epoch(const psyscr_screen* s) { return s ? s->gl_epoch : 0; }
PSYSCR_API uint32_t psyscr_gl_generation(const psyscr_screen* s) { return s && s->open ? s->gl_generation : 0; }

PSYSCR_API int psyscr_native(const psyscr_screen* s, psyscr_native_info* out) {
    if (!out) return PSYSCR_ERR_ARG;
    memset(out, 0, sizeof *out);
    if (!s || !s->open) return PSYSCR_ERR_CLOSED;
#if defined(PSYSCR__DXGI)
    if (s->backend == PSYSCR_BACKEND_DXGI_FLIP || s->backend == PSYSCR_BACKEND_COMPOSITION) {
        ID3D11Device* dev;
        IDXGIDevice* xd = NULL;
        IDXGIAdapter* ad = NULL;
        if (s->backend == PSYSCR_BACKEND_DXGI_FLIP) {
            const psyscr__dxgi* d = (const psyscr__dxgi*)(const void*)s->backend_mem;
            dev = d->dev;
            out->d3d11_context = d->dctx;
            out->egl_display = d->dpy;
        } else {
            const psyscr__comp* c = (const psyscr__comp*)(const void*)s->backend_mem;
            dev = c->dev;
            out->d3d11_context = c->dctx;
            out->egl_display = c->dpy;
        }
        out->d3d11_device = dev;
        if (dev) {   /* asked of the device, not of the desc */
            ID3D11Multithread* mt = NULL;
            out->video = (PSYSCR__CALL0(dev, GetCreationFlags) & (UINT)D3D11_CREATE_DEVICE_VIDEO_SUPPORT) ? 1 : 0;
            if (out->d3d11_context &&
                SUCCEEDED(PSYSCR__CALL((ID3D11DeviceContext*)out->d3d11_context, QueryInterface,
                                       PSYSCR__IID(psyscr__IID_ID3D11Multithread), (void**)&mt)) && mt) {
                out->mt_protected = PSYSCR__CALL0(mt, GetMultithreadProtected) ? 1 : 0;
                PSYSCR__RELEASE(mt);
            }
        }
        /* asked of the device, so it is the adapter the device is on even
         * when open() found no adapter for the monitor */
        if (dev && SUCCEEDED(PSYSCR__CALL(dev, QueryInterface, PSYSCR__IID(psyscr__IID_IDXGIDevice), (void**)&xd))) {
            if (SUCCEEDED(PSYSCR__CALL(xd, GetAdapter, &ad))) {
                DXGI_ADAPTER_DESC desc;
                if (SUCCEEDED(PSYSCR__CALL(ad, GetDesc, &desc))) {
                    out->luid_low = (uint32_t)desc.AdapterLuid.LowPart;
                    out->luid_high = (int32_t)desc.AdapterLuid.HighPart;
                }
                PSYSCR__RELEASE(ad);
            }
            PSYSCR__RELEASE(xd);
        }
        return PSYSCR_OK;
    }
#endif
    return PSYSCR_ERR_NOT_IMPLEMENTED;
}

PSYSCR_API int psyscr_begin_group(psyscr_screen* const* s, int n, psyscr_frame* f) {
    int i, rc;
    if (!s || !f || n < 1) return PSYSCR_ERR_ARG;
    for (i = 0; i < n; i++) {
        if (!s[i] || !s[i]->open) return PSYSCR_ERR_CLOSED;
        if (s[i]->pr->waits_block) return PSYSCR_ERR_NOT_IMPLEMENTED;
        if (s[i]->begun) return PSYSCR_ERR_ORDER;
    }
    /* SDL's pump is the process's: once for the group, then every member
     * sees the same aborts before any of them begins a frame. */
    for (i = 0; i < n && !s[i]->window; i++) { }
    if (i < n) psyscr__pump(s[i]);
    for (i = 0; i < n; i++) s[i]->polled = 1;
    for (i = 0; i < n && !psyscr__abort_pending(s[i]); i++) { }
    if (i < n) {
        for (i = 0; i < n; i++) if (!psyscr__abort_take(s[i], &f[i])) memset(&f[i], 0, sizeof f[i]);
        return PSYSCR_QUIT;
    }
    /* Each wait is on its own kernel object, so waiting in turn costs the
     * longest wait, not their sum. */
    for (i = 0; i < n; i++) {
        rc = psyscr_begin(s[i], &f[i]);
        if (rc != PSYSCR_OK) return rc;
    }
    return PSYSCR_OK;
}

PSYSCR_API int psyscr_flip_group_at(psyscr_screen* const* s, int n, int64_t t) {
    int i, rc, first = PSYSCR_OK;
    if (!s || n < 1) return PSYSCR_ERR_ARG;
    for (i = 0; i < n; i++) {
        if (!s[i]) return PSYSCR_ERR_ARG;
        if (s[i]->pr && s[i]->pr->bind) s[i]->pr->bind(s[i]->pr_ctx);
        rc = psyscr_flip_at(s[i], t, NULL);
        if (rc < 0 && first == PSYSCR_OK) first = rc;
    }
    return first;
}

PSYSCR_API psyscr_proc psyscr_gl_proc(const psyscr_screen* s, const char* name) {
    if (!s || !s->open || !name || !s->pr->gl_proc) return NULL;
    return s->pr->gl_proc(s->pr_ctx, name);
}

PSYSCR_API struct SDL_Window* psyscr_window(const psyscr_screen* s) { return s ? s->window : NULL; }

PSYSCR_API void psyscr_bind(psyscr_screen* s) {
    if (s && s->open && s->pr->bind) s->pr->bind(s->pr_ctx);
}

#if !defined(PSYSCR_NO_SDL)
PSYSCR_API int64_t psyscr_restamp(const psyscr_screen* s, uint64_t sdl_ticks_ns) {
    if (!s || !s->sdl_width) return 0;
    return s->sdl_rt + ((int64_t)sdl_ticks_ns - s->sdl_ticks);
}

PSYSCR_API bool psyscr_poll(psyscr_screen* s, union SDL_Event* ev, int64_t* t_rt) {
    if (!ev) return false;
    if (s) s->polled = 1;
    if (!SDL_PollEvent(ev)) return false;
    if (t_rt) *t_rt = psyscr_restamp(s, ev->common.timestamp);
    return true;
}
#else
PSYSCR_API int64_t psyscr_restamp(const psyscr_screen* s, uint64_t sdl_ticks_ns) {
    (void)s; (void)sdl_ticks_ns; return 0;
}
PSYSCR_API bool psyscr_poll(psyscr_screen* s, union SDL_Event* ev, int64_t* t_rt) {
    /* the key feed and the icon serve SDL's event watch and its window;
     * without SDL only tests/adapt/psy_screen_test.c calls them */
    (void)&psyscr__abort_key; (void)&psyscr__icon_decode; (void)&psyscr__abort_edge;
    (void)s; (void)ev; (void)t_rt; return false;
}
#endif

PSYSCR_API const psyscr_param* psyscr_params(int* n) {
    static const psyscr_param table[] = {
        { "display",         "u32",  0, 4294967295.0, 0, "",      "SDL display id; 0 = the primary display" },
        { "mode.w",          "i32",  0, 65535, 0, "px",           "mode width; 0 = the desktop mode" },
        { "mode.h",          "i32",  0, 65535, 0, "px",           "mode height; 0 = the desktop mode" },
        { "mode.refresh_num","i32",  0, 2147483647.0, 0, "Hz",    "refresh numerator; 0 = the desktop mode" },
        { "mode.refresh_den","i32",  0, 2147483647.0, 1, "",      "refresh denominator" },
        { "windowed",        "bool", 0, 1, 0, "",                 "a window instead of borderless fullscreen" },
        { "window_w",        "i32",  0, 65535, 800, "px",         "window width" },
        { "window_h",        "i32",  0, 65535, 600, "px",         "window height" },
        { "backend",         "enum", 0, 8, 0, "",                 "0 auto, 1 dxgi_flip, 2 composition, 7 sim, 8 custom" },
        { "lead",            "f64", -1, 0.999, 0.5, "frame",      "snap window; -1 = never before the target" },
        { "display_index",   "u32",  0, 4294967295.0, 0, "",      "display number in the flip records" },
        { "patch.on",        "bool", 0, 1, 0, "",                 "draw the photodiode patch" },
        { "patch.size",      "i32",  0, 4096, PSYSCR_PATCH_DEFAULT_SIZE, "px", "patch side" },
        { "patch.corner",    "enum", 0, 3, 0, "",                 "0 top left, 1 top right, 2 bottom left, 3 bottom right" },
        { "show_cursor",     "bool", 0, 1, 0, "",                 "keep the cursor visible in fullscreen" },
        { "abort_keys.off",  "bool", 0, 1, 0, "",                 "no key combination aborts the frame loop" },
        { "abort_keys.key",  "u32",  0, 4294967295.0, 27, "",     "SDL keycode of the abort combination; 0 = Esc" },
        { "abort_keys.mods", "u32",  0, 255, 1, "",               "modifiers held: 1 shift, 2 ctrl, 4 alt, 8 gui, 128 none; 0 = shift" },
        { "panic",           "bool", 0, 1, 0, "",                 "arm the panic watchdog in fullscreen (Windows)" },
        { "panic_presses",   "i32",  0, 16, 3, "",                "abort presses that end a hung program" },
        { "panic_window_ms", "i32",  0, 60000, 2000, "ms",        "the time those presses must fall in" },
        { "panic_grace_ms",  "i32",  0, 600000, 10000, "ms",      "no panic this long after the frame loop reported an abort" },
        { "icon_sdl",        "bool", 0, 1, 0, "",                 "keep SDL's window icon instead of the header's" },
        { "d3d11_video",     "bool", 0, 1, 0, "",                 "D3D11 device with video support and multithread protection" },
        { "onset_offset_ns", "i64", -1e9, 1e9, 0, "ns",           "added to every onset; from the photodiode test" },
        { "min_tier",        "i32",  0, 3, 0, "",                 "flag flips whose tier is worse; 0 = off" },
        { "sim_period_ns",   "i64",  0, 1e10, 16666667, "ns",     "frame period of the simulated display" }
    };
    if (n) *n = (int)(sizeof table / sizeof table[0]);
    return table;
}

#ifdef __cplusplus
}
#endif

#endif /* PSY_SCREEN_IMPLEMENTATION_GUARD */
#endif /* PSY_SCREEN_IMPLEMENTATION */

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
