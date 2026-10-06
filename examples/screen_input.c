/* screen_input.c - how good psy_screen.h's input timestamps are here.
 *
 *   1. The restamp's clock correlation: psyrt_correlate() of SDL_GetTicksNS
 *      against the psy_rt clock, 1000 times: width and cost.
 *   2. Windows only: key events injected with SendInput() at known psy_rt
 *      times, read back with psyscr_poll(), with SDL's message-loop keyboard
 *      and then with SDL_HINT_WINDOWS_RAW_KEYBOARD. When the window cannot
 *      take the keyboard focus (a program started in the background does
 *      not get it), WM_KEYDOWN is posted to the window instead, which tests
 *      the message-loop path only. For each event: the
 *      restamped time minus the injection time, and the sub-millisecond part
 *      of SDL's own timestamp, which shows the granularity of the OS path.
 *
 * The key is F24, which nothing maps by default, and it is injected only
 * while this program's window has the keyboard focus, so it cannot type into
 * another program. To get the foreground, the program sends one zero-size
 * mouse move, which does not move the pointer.
 *
 * Usage: screen_input [--timer-res] [--panic] [injections]   (default 200 per path)
 *   --timer-res  call psyrt_timer_resolution_begin() first (timeBeginPeriod)
 *   --panic      arm the panic watchdog (its keyboard hook sees every key of
 *                the session) and test the raw path only: the hook's cost
 * Esc or closing the window ends the run at once.
 * Exit code: 0, 1 when the screen did not open, 2 for a bad argument.
 */
/* --panic arms the watchdog in this program's window (a test seam). */
#define PSYSCR__PANIC_WINDOWED 1
#define PSY_SCREEN_IMPLEMENTATION
#include "psy_screen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
    #include <windows.h>
#endif

static int cmp_d(const void* a, const void* b) {
    double x = *(const double*)a, y = *(const double*)b;
    return x < y ? -1 : (x > y);
}

static void row(const char* name, double* v, int n) {
    double sum = 0;
    int i;
    if (n <= 0) { printf("  %-40s n=0\n", name); return; }
    qsort(v, (size_t)n, sizeof *v, cmp_d);
    for (i = 0; i < n; i++) sum += v[i];
    printf("  %-40s n=%5d mean %9.3f p50 %9.3f p99 %9.3f max %9.3f min %9.3f\n", name, n, sum / n,
           v[n / 2], v[(int)((double)n * 0.99)], v[n - 1], v[0]);
}

static double g_a[1000], g_b[1000];

#if defined(_WIN32)
static void inject_f24(WORD flags) {
    INPUT in;
    memset(&in, 0, sizeof in);
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = VK_F24;
    in.ki.dwFlags = flags;
    SendInput(1, &in, sizeof in);
}

/* One keyboard path: SendInput through the message loop (0) or raw input
 * (1), or a WM_KEYDOWN posted to the window (2), which needs no keyboard
 * focus and reaches the message loop only. Returns 1 when Esc or close
 * ended the run. */
static int run_path(psyscr_screen* s, int mode, int count) {
    static double delta[4096], subms[4096];
    static const char* const names[3] = { "SendInput, message loop", "SendInput, raw input",
                                          "posted WM_KEYDOWN, message loop" };
    int nd = 0, injected = 0, waiting = 0, quit = 0;
    int64_t t_inj = 0, give_up = 0;
    uint32_t rng = 777u + (uint32_t)mode;
    int64_t end = (int64_t)psyrt_now_ns() + (int64_t)count * 60000000 + 5000000000LL;
    HWND hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(psyscr_window(s)),
                                             SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    SDL_SetHint(SDL_HINT_WINDOWS_RAW_KEYBOARD, mode == 1 ? "1" : "0");
    while (injected < count && !quit && (int64_t)psyrt_now_ns() < end) {
        psyscr_frame f;
        SDL_Event ev;
        int64_t t_ev;
        if (psyscr_begin(s, &f) != PSYSCR_OK) { quit = 1; break; }
        while (psyscr_poll(s, &ev, &t_ev)) {
            if (ev.type == SDL_EVENT_QUIT || ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
                (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE)) quit = 1;
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_F24 && waiting && nd < 4096) {
                delta[nd] = (double)(t_ev - t_inj) / 1e6;
                subms[nd] = (double)(ev.common.timestamp % 1000000u) / 1e3;
                nd++;
                waiting = 0;
            }
        }
        if (waiting && psyrt_now_ns() > (uint64_t)give_up) waiting = 0;   /* lost: not counted */
        if (!waiting && !quit && (mode == 2 || SDL_GetKeyboardFocus() == psyscr_window(s))) {
            /* a random phase inside the frame, so the frame loop is no clock */
            rng = rng * 1664525u + 1013904223u;
            psyrt_sleep_until(psyrt_now_ns() + (rng >> 8) % 8000000u, 0);
            t_inj = (int64_t)psyrt_now_ns();
            if (mode == 2) {
                PostMessageW(hwnd, WM_KEYDOWN, VK_F24, 1 | (0x76u << 16));
                PostMessageW(hwnd, WM_KEYUP, VK_F24, 1 | (0x76u << 16) | (3u << 30));
            } else {
                inject_f24(0);
                inject_f24(KEYEVENTF_KEYUP);
            }
            give_up = t_inj + 500000000;
            waiting = 1;
            injected++;
        }
        psyscr_flip(s);
    }
    printf("%s: %d injected, %d received\n", names[mode], injected, nd);
    row("restamped event - injection, ms", delta, nd);
    row("SDL timestamp mod 1 ms, us", subms, nd);
    return quit;
}
#endif

int main(int argc, char** argv) {
    psyscr_screen s;
    psyscr_desc d;
    int i, count = 200, timer_res = 0, panic = 0;
    char line[512];
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--timer-res")) timer_res = 1;
        else if (!strcmp(argv[i], "--panic")) panic = 1;
        else {
            count = atoi(argv[i]);
            if (count < 1 || count > 4000) {
                fprintf(stderr, "usage: screen_input [--timer-res] [--panic] [injections 1..4000]\n");
                return 2;
            }
        }
    }
    if (timer_res) printf("timeBeginPeriod(1): %s\n", psyrt_timer_resolution_begin() ? "granted" : "refused");
    memset(&s, 0, sizeof s);
    memset(&d, 0, sizeof d);
    d.windowed = true;
    d.window_w = 400;
    d.window_h = 300;
    d.panic = panic != 0;
    if (!psyscr_open(&s, &d)) { fprintf(stderr, "screen_input: %s\n", psyscr_error(&s)); return 1; }
    psyscr_describe(&s, line, sizeof line);
    printf("%s\n", line);
    for (i = 0; i < 1000; i++) {
        psyrt_corr_desc cd;
        psyrt_corr c;
        uint64_t t0;
        memset(&cd, 0, sizeof cd);
        cd.read = SDL_GetTicksNS;
        t0 = psyrt_now_ns();
        psyrt_correlate(&cd, &c);
        g_b[i] = (double)(psyrt_now_ns() - t0) / 1e3;
        g_a[i] = (double)c.width_ns / 1e3;
    }
    printf("correlation of SDL_GetTicksNS with the psy_rt clock, 16 tries each:\n");
    row("width us", g_a, 1000);
    row("cost of one psyrt_correlate us", g_b, 1000);
#if defined(_WIN32)
    {
        /* SendInput reaches only the foreground window. Windows gives the
         * foreground to a program started from the background after the
         * program sends one input event: a zero-size mouse move. */
        int64_t until = (int64_t)psyrt_now_ns() + 2000000000LL;
        {
            HWND hw = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(psyscr_window(&s)),
                                                   SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
            INPUT in;
            memset(&in, 0, sizeof in);
            in.type = INPUT_MOUSE;
            in.mi.dwFlags = MOUSEEVENTF_MOVE;
            SendInput(1, &in, sizeof in);
            SetForegroundWindow(hw);
        }
        while (SDL_GetKeyboardFocus() != psyscr_window(&s) && (int64_t)psyrt_now_ns() < until) {
            psyscr_frame f;
            if (psyscr_begin(&s, &f) != PSYSCR_OK) break;
            psyscr_flip(&s);
        }
        if (SDL_GetKeyboardFocus() == psyscr_window(&s)) {
            if (panic) run_path(&s, 1, count);
            else if (!run_path(&s, 0, count)) run_path(&s, 1, count);
        } else {
            printf("the window has no keyboard focus: raw input is not tested, posted messages are\n");
            run_path(&s, 2, count);
        }
    }
#else
    printf("key injection is Windows only here\n");
#endif
    psyscr_close(&s);
    if (timer_res) psyrt_timer_resolution_end();
    return 0;
}
