/* psy_screen.h - v0.2.0 - public domain single-header display library
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
 *   STATUS: v0.2.0. Two swap paths run, DXGI_FLIP and COMPOSITION, on one
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
 *   timeout). Fullscreen on AC, 1 minute each on the quiet machine: v0.2
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
 *   mode picker, preemption) is checked without SDL or a display by
 *   tests/adapt/psy_screen_test.c against a scripted swap path on a
 *   virtual clock, so the host's load cannot change a result, on MSVC,
 *   MinGW gcc 16.1, gcc 11.4 (WSL2; also as C99 at -O3, as C++17, and
 *   under ASan and UBSan, and 8 of 8 runs with a busy loop on its CPU) and
 *   clang (emcc, run in node); fourteen deliberate mutations of the header
 *   each make it fail.
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
 *       while (psyscr_begin(&scr, &f) == PSYSCR_OK) {         // Esc ends it
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
 *     flip is enough after the path changes. open() presents black frames
 *     until three flips agree, because a window that has just gone
 *     fullscreen is composed for a few frames.
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
 *   Not implemented in v0.2; open() refuses them with a message:
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
 *   the psy_rt clock. The core calls a presenter from the thread that calls
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
 *   heap calls from the header in a debug build). The handle is about 2.7
 *   KB. One thread calls begin, flip and the rest for a screen. open() and
 *   close() of different screens must not run at the same time, because
 *   ANGLE's libEGL is loaded once per process and never unloaded. No
 *   function starts a thread. Raise the frame thread with
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
#define PSYSCR_VERSION_MINOR 2
#define PSYSCR_VERSION_PATCH 0
#define PSYSCR_VERSION_STRING "0.2.0"

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
#define PSYSCR_QUIT                  1  /* psyscr_begin: close, quit or Esc  */
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
} psyscr_record;

/* What the frame loop draws for. */
typedef struct psyscr_frame {
    int64_t onset;       /* predicted onset of this frame's flip, RT ns       */
    int64_t period;      /* ns; 0 when there is no fixed period                */
    int64_t index;       /* frame number, 0 for the first                     */
    int64_t vblank;      /* the vblank count it is planned for                */
    const psyscr_record* last; /* newest completed flip record; NULL before one */
} psyscr_frame;

/* --- presenter (the swap-path interface) ------------------------------- */

/* A GL or EGL entry point. Cast it to the function's own type to call it;
 * ISO C converts between function pointer types, not to and from void*. */
typedef void (*psyscr_proc)(void);

#define PSYSCR_PRESENTER_VERSION 1

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
    int32_t  reserved_;
} psyscr_present_req;

typedef struct psyscr_presenter_open {
    struct SDL_Window*  window;    /* NULL when the presenter needs none     */
    uint32_t            display;
    const psyscr_mode*  mode;
    const char*         angle_dir;
    int64_t             sim_period_ns;
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
    bool           no_esc_quit;      /* Esc does not end psyscr_begin()        */
    bool           vrr;              /* refused                                */
    int32_t        min_tier;         /* 0 = off; else flag flips worse than it */
    int64_t        onset_offset_ns;
    int64_t        sim_period_ns;    /* BACKEND_SIM; 0 = 1e9 / 60              */
    const char*    angle_dir;
    const psyscr_presenter* presenter;   /* BACKEND_CUSTOM                     */
    void*          presenter_ctx;
} psyscr_desc;

/* Private. One flip between flip_at() and its completion. */
typedef struct psyscr__pend {
    psyscr_record rec;
    uint64_t    id;
    int64_t     planned_count;
    int64_t     count_at_present;
    int64_t     t_ret;            /* when the present call returned         */
    int32_t     asap;
    int32_t     used;
} psyscr__pend;

#define PSYSCR__MAX_PEND 8
/* Slack: the planned vblank's time minus the present call's return, in
 * bins of a 32nd of a period over 4 periods, per path. */
#define PSYSCR__SLACK_BINS  128
#define PSYSCR__SLACK_PATHS 5
#define PSYSCR__SLACK_DECAY 4096   /* flips between halvings of the counts  */
#define PSYSCR__BACKEND_WORDS 128

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
    bool                    esc_quits;
    /* the grid */
    int                     have_anchor;
    int64_t                 vb_t, vb_count;
    int64_t                 ref_t, ref_count;
    double                  period_f;
    double                  nominal_f;
    int64_t                 margin_ns;
    int32_t                 depth, depth_cand, depth_votes, depth_need;
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
 * frame can make. Returns PSYSCR_OK; PSYSCR_QUIT when the window was closed,
 * the program was asked to quit, or Esc is pending (unless desc.no_esc_quit),
 * without starting a frame. It sees only the events still queued: after a
 * frame that read events with psyscr_poll(), it does not pump again, and
 * the Esc or close you read is yours to act on. PSYSCR_ERR_ORDER after a
 * begin without a flip;
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

/* begin() and flip_at() on n screens. Each screen keeps its own grid, so
 * f[i].onset differ unless the displays are genlocked; flip_group_at snaps
 * t on each grid. PSYSCR_ERR_NOT_IMPLEMENTED for a swap path whose wait
 * blocks a thread (none in v0.1). */
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

/* One desc field a designer sets. */
typedef struct psyscr_param {
    const char* name;     /* the desc field, dotted for nested fields       */
    const char* type;     /* "u32", "i32", "i64", "f64", "bool", "enum"      */
    double      min, max; /* inclusive                                      */
    double      def;      /* the value a zero field means                   */
    const char* unit;     /* "", "px", "ns", "Hz", "frame"                  */
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
    #include <d3d11_1.h>
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
static int64_t psyscr__now(void) { return PSYSCR__NOW(); }

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
    psyscr__sim_completions, NULL, NULL, psyscr__sim_describe
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
                       D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 4, D3D11_SDK_VERSION, &d->dev, NULL, &d->dctx);
    if (hr == E_INVALIDARG)   /* 11.1 unknown to the runtime */
        hr = create_device((IDXGIAdapter*)pick, pick ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, NULL,
                           D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels + 1, 3, D3D11_SDK_VERSION, &d->dev, NULL, &d->dctx);
    PSYSCR__RELEASE(pick);
    if (FAILED(hr)) {
        PSYSCR__RELEASE(fac1);
        psyscr__set_error(err, err_cap, "psy_screen: D3D11CreateDevice 0x%08lx", (unsigned long)hr);
        return PSYSCR_ERR_LOST;
    }
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
    PSYSCR__RELEASE(tex);
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
    if (req->patch_on && d->dctx1 && d->rtv) {
        D3D11_RECT r;
        float c[4];
        r.left = req->patch_x; r.top = req->patch_y;
        r.right = req->patch_x + req->patch_w; r.bottom = req->patch_y + req->patch_h;
        c[0] = c[1] = c[2] = req->patch_value; c[3] = 1.0f;
        PSYSCR__CALL(d->dctx1, ClearView, (ID3D11View*)d->rtv, c, &r, 1);
    }
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

static const psyscr_presenter psyscr__dxgi_presenter = {
    PSYSCR_PRESENTER_VERSION, "dxgi_flip", true, false, true,
    psyscr__dxgi_open, psyscr__dxgi_close, psyscr__dxgi_acquire, psyscr__dxgi_present,
    psyscr__dxgi_completions, psyscr__dxgi_gl_proc, psyscr__dxgi_bind, psyscr__dxgi_describe
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
                       D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2, D3D11_SDK_VERSION, &c->dev, NULL, &c->dctx);
    PSYSCR__RELEASE(pick);
    if (FAILED(hr)) { psyscr__set_error(err, err_cap, "psy_screen: D3D11CreateDevice 0x%08lx", (unsigned long)hr); return PSYSCR_ERR_LOST; }
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
    return PSYSCR_OK;
#endif
}

static void psyscr__comp_close(void* vctx) {
    psyscr__comp* c = (psyscr__comp*)vctx;
    int i;
    if (c->pm && c->last_sys_id) PSYSCR__C(c->pm, CancelPresentsFrom, c->last_sys_id + 1);
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
    if (req->patch_on && c->dctx1 && c->rtv[c->cur]) {
        D3D11_RECT r;
        float col[4];
        r.left = req->patch_x; r.top = req->patch_y;
        r.right = req->patch_x + req->patch_w; r.bottom = req->patch_y + req->patch_h;
        col[0] = col[1] = col[2] = req->patch_value; col[3] = 1.0f;
        PSYSCR__CALL(c->dctx1, ClearView, (ID3D11View*)c->rtv[c->cur], col, &r, 1);
    }
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

static const psyscr_presenter psyscr__comp_presenter = {
    PSYSCR_PRESENTER_VERSION, "composition", true, false, true,
    psyscr__comp_open, psyscr__comp_close, psyscr__comp_acquire, psyscr__comp_present,
    psyscr__comp_completions, psyscr__dxgi_gl_proc, psyscr__comp_bind, psyscr__comp_describe
};

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
    psyscr__push_flip(s, r);
    PSYRT_FRAME_MARK();
    PSYRT_PLOT("psyscr residual us", (double)r->residual / 1000.0);
    s->last = *r;
    s->have_last = 1;
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
            s->depth_votes = 0;
            s->depth_need = 3;
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
    }
    s->sdl_corr_t = psyscr__now();
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

    memset(&want, 0, sizeof want);
    memset(&in, 0, sizeof in);
    in.display = desc->display;
    in.angle_dir = desc->angle_dir;
    in.sim_period_ns = desc->sim_period_ns;
    in.mode = &want;
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
        if (!psyscr__video_up()) {
            psyscr__set_error(s->error, sizeof s->error, "psy_screen: SDL video: %s", SDL_GetError());
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
    }
#endif

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
    s->esc_quits = !desc->no_esc_quit;
    s->nominal_f = (double)s->caps.period_ns;
    s->period_f = s->nominal_f;
    s->depth = 1;
    s->depth_need = 1;
    s->path = PSYSCR_PATH_UNKNOWN;
    s->min_tier = desc->min_tier;
    psyscr__update_lead(s);
    psyscr__gl_load(s);
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
    if (s->pr && s->pr_open && s->pr->close) s->pr->close(s->pr_ctx);
    s->pr = NULL;
    s->pr_open = 0;
#if !defined(PSYSCR_NO_SDL)
    if (s->cursor_hidden) SDL_ShowCursor();
    if (s->window) SDL_DestroyWindow(s->window);
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
    char extra[192];
    double hz, ppm;
    if (!s || !buf || cap == 0) return PSYSCR_ERR_ARG;
    if (!s->open) return snprintf(buf, cap, "psy_screen: closed");
    extra[0] = '\0';
    if (s->pr->describe) s->pr->describe(s->pr_ctx, extra, sizeof extra);
    hz = s->period_f > 0 ? 1e9 / s->period_f : 0;
    ppm = (s->period_f / s->nominal_f - 1.0) * 1e6;
    return snprintf(buf, cap, "psy_screen %s: backend=%s %s mode=%dx%d@%d/%d measured=%.4fHz(%+.0fppm) "
                    "path=%s depth=%d lead=%.2f worst_tier=%d%s%s",
                    PSYSCR_VERSION_STRING, s->pr->name, extra, s->caps.mode.w, s->caps.mode.h,
                    s->caps.mode.refresh_num, s->caps.mode.refresh_den, hz, ppm,
                    psyscr__path_name(s->path), s->depth, s->lead < 0 ? -1.0 : s->lead, s->worst_tier,
                    s->unstable ? " WARNING=off-grid-vblanks" : "",
                    (ppm > 200 || ppm < -200) ? " WARNING=period-differs-from-mode" : "");
}

#if !defined(PSYSCR_NO_SDL)
static int psyscr__quit_pending(psyscr_screen* s) {
    SDL_Event ev[8];
    int i, n;
    if (!s->window) return 0;
    if (!s->polled) {   /* psyscr_poll() pumped already this frame */
        PSYRT_ZONE(z_pump, "psyscr.pump");
        SDL_PumpEvents();
        PSYRT_ZONE_END(z_pump);
    }
    s->polled = 0;
    if (SDL_HasEvent(SDL_EVENT_QUIT) || SDL_HasEvent(SDL_EVENT_WINDOW_CLOSE_REQUESTED)) return 1;
    if (!s->esc_quits) return 0;
    n = SDL_PeepEvents(ev, 8, SDL_PEEKEVENT, SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_DOWN);
    for (i = 0; i < n; i++) if (ev[i].key.key == SDLK_ESCAPE) return 1;
    return 0;
}
#endif

PSYSCR_API int psyscr_begin(psyscr_screen* s, psyscr_frame* f) {
    int64_t t0, now, wait;
    int rc;
    PSYRT_ZONE(z_begin, "psyscr.begin");
    if (!s || !f) { PSYRT_ZONE_END(z_begin); return PSYSCR_ERR_ARG; }
    if (!s->open) { PSYRT_ZONE_END(z_begin); return PSYSCR_ERR_CLOSED; }
    if (s->begun) { PSYRT_ZONE_END(z_begin); return PSYSCR_ERR_ORDER; }
#if !defined(PSYSCR_NO_SDL)
    if (psyscr__quit_pending(s)) { PSYRT_ZONE_END(z_begin); return PSYSCR_QUIT; }
#endif
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
#if !defined(PSYSCR_NO_SDL)
    if (s->window && t0 - s->sdl_corr_t > 10000000000LL) psyscr__restamp_correlate(s);
#endif
    now = psyscr__now();
    s->pred_count = psyscr__count_at(s, now + s->margin_ns) + s->depth;
    if (s->pred_count <= s->prev_shown) s->pred_count = s->prev_shown + 1;
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
    int i, rc;
    uint16_t flags = PSYSCR_FLIP_PENDING;
    PSYRT_ZONE(z_flip, "psyscr.flip");
    if (!s) { PSYRT_ZONE_END(z_flip); return PSYSCR_ERR_ARG; }
    if (!s->open) { PSYRT_ZONE_END(z_flip); return PSYSCR_ERR_CLOSED; }
    if (!s->begun) { PSYRT_ZONE_END(z_flip); return PSYSCR_ERR_ORDER; }
    now = psyscr__now();
    s->acc[PSYSCR_PHASE_DRAW] = psyscr__sat32((int64_t)s->acc[PSYSCR_PHASE_DRAW] + (now - s->mark_t));
    {
        PSYRT_ZONE(z_patch, "psyscr.patch");
        if (!s->pr->draws_patch) psyscr__draw_patch(s);
        PSYRT_ZONE_END(z_patch);
    }

    tg = t - s->offset;
    count_t = psyscr__snap(s, tg);
    earliest = psyscr__count_at(s, psyscr__now() + s->margin_ns) + s->depth;
    if (earliest <= s->prev_shown) earliest = s->prev_shown + 1;
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
        s->index++;
        PSYRT_ZONE_END(z_flip);
        return rc;
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

PSYSCR_API int psyscr_begin_group(psyscr_screen* const* s, int n, psyscr_frame* f) {
    int i, rc;
    if (!s || !f || n < 1) return PSYSCR_ERR_ARG;
    for (i = 0; i < n; i++) {
        if (!s[i] || !s[i]->open) return PSYSCR_ERR_CLOSED;
        if (s[i]->pr->waits_block) return PSYSCR_ERR_NOT_IMPLEMENTED;
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
        { "no_esc_quit",     "bool", 0, 1, 0, "",                 "Esc does not end the frame loop" },
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
