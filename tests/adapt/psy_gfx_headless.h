/* psy_gfx_headless.h - a psy_screen.h presenter with a GL ES 3.0 context and
 * no window, for tests, CI and benchmarks.
 *
 * It is the Presenter extension interface (psyscr_presenter), so it opens
 * through PSYSCR_BACKEND_CUSTOM and can become a psy_screen.h backend with
 * no change to its callers. The context is an EGL pbuffer of w x h:
 *   Windows  ANGLE's libEGL.dll (PSYSCR_ANGLE_DIR, angle_dir, or the DLL
 *            search path) on D3D11 hardware, D3D11 WARP, or Vulkan
 *            SwiftShader (vk_swiftshader.dll and its ICD json beside
 *            libGLESv2.dll)
 *   Linux    Mesa's libEGL.so.1 on the surfaceless platform; MESA_SOFTWARE
 *            sets LIBGL_ALWAYS_SOFTWARE first, which picks llvmpipe
 * EGL is loaded at run time, so nothing links and no Khronos header is
 * needed. The vblank grid is the simulated display's: a fixed period on the
 * psy_rt clock (sim_period_ns), every frame shown on the vblank it was
 * planned for. Framebuffer 0's rows are GL's, bottom-up.
 *
 *     static psygfx_headless hl = { 320, 240, PSYGFX_HL_WARP };
 *     psyscr_desc d = { 0 };
 *     d.backend = PSYSCR_BACKEND_CUSTOM;
 *     d.presenter = &psygfx_headless_presenter;
 *     d.presenter_ctx = &hl;
 *     d.sim_period_ns = 1000000;
 *     psyscr_open(&scr, &d);   // psyscr_error() names the EGL step that failed
 *
 * Include it after psy_screen.h's implementation (or psy_gfx.h's). Only one
 * device kind per process on Linux: Mesa reads the variable at load.
 */
#ifndef PSY_GFX_HEADLESS_H
#define PSY_GFX_HEADLESS_H

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #define PSYGFX_HL__API __stdcall
#else
    #include <dlfcn.h>
    #include <stdlib.h>
    #define PSYGFX_HL__API
#endif
#include <stdio.h>
#include <string.h>

typedef enum psygfx_hl_device {
    PSYGFX_HL_HARDWARE = 0,      /* ANGLE on the D3D11 hardware adapter       */
    PSYGFX_HL_WARP,              /* ANGLE on D3D11 WARP (software)            */
    PSYGFX_HL_SWIFTSHADER,       /* ANGLE on Vulkan SwiftShader (software)    */
    PSYGFX_HL_MESA_SOFTWARE,     /* Mesa llvmpipe (Linux)                     */
    PSYGFX_HL_MESA               /* Mesa's default driver (Linux)             */
} psygfx_hl_device;

typedef psyscr_proc (PSYGFX_HL__API *psygfx_hl__gpa_fn)(const char*);
typedef void* (PSYGFX_HL__API *psygfx_hl__gpd_fn)(unsigned, void*, const int32_t*);
typedef unsigned (PSYGFX_HL__API *psygfx_hl__init_fn)(void*, int32_t*, int32_t*);
typedef unsigned (PSYGFX_HL__API *psygfx_hl__term_fn)(void*);
typedef unsigned (PSYGFX_HL__API *psygfx_hl__bind_fn)(unsigned);
typedef unsigned (PSYGFX_HL__API *psygfx_hl__choose_fn)(void*, const int32_t*, void**, int32_t, int32_t*);
typedef void* (PSYGFX_HL__API *psygfx_hl__ctx_fn)(void*, void*, void*, const int32_t*);
typedef void* (PSYGFX_HL__API *psygfx_hl__pb_fn)(void*, void*, const int32_t*);
typedef unsigned (PSYGFX_HL__API *psygfx_hl__cur_fn)(void*, void*, void*, void*);
typedef unsigned (PSYGFX_HL__API *psygfx_hl__destroy_fn)(void*, void*);
typedef int32_t (PSYGFX_HL__API *psygfx_hl__err_fn)(void);
typedef void (PSYGFX_HL__API *psygfx_hl__flush_fn)(void);
typedef const unsigned char* (PSYGFX_HL__API *psygfx_hl__str_fn)(unsigned);

typedef struct psygfx_headless {
    int32_t          w, h;           /* the pbuffer, pixels                     */
    psygfx_hl_device device;
    const char*      angle_dir;      /* Windows; NULL = PSYSCR_ANGLE_DIR or path */
    /* private */
    void*            lib;
    psygfx_hl__gpa_fn     gpa;
    psygfx_hl__cur_fn     make_current;
    psygfx_hl__destroy_fn destroy_surface, destroy_context;
    psygfx_hl__term_fn    terminate;
    psygfx_hl__flush_fn   flush;
    void*            dpy;
    void*            ctx;
    void*            surf;
    int64_t          t0, period;
    uint64_t         pend_id;
    int64_t          pend_count;
    int              has_pend;
    psyscr_vblank    done;
    int              has_done;
    char             renderer[160];
} psygfx_headless;

/* ISO C converts between function pointer types only, so dlsym's object
 * pointer is copied, not cast. */
static psyscr_proc psygfx_hl__sym(psygfx_headless* h, const char* name) {
    psyscr_proc p = NULL;
#if defined(_WIN32)
    p = (psyscr_proc)GetProcAddress((HMODULE)h->lib, name);
#else
    void* o = dlsym(h->lib, name);
    if (o) memcpy(&p, &o, sizeof p);
#endif
    if (!p && h->gpa) p = h->gpa(name);
    return p;
}

static int psygfx_hl__load(psygfx_headless* h, char* err, size_t cap) {
#if defined(_WIN32)
    wchar_t path[MAX_PATH];
    int n = 0;
    HMODULE lib = NULL;
    if (h->angle_dir && h->angle_dir[0]) n = MultiByteToWideChar(CP_UTF8, 0, h->angle_dir, -1, path, MAX_PATH - 16) - 1;
    else n = (int)GetEnvironmentVariableW(L"PSYSCR_ANGLE_DIR", path, MAX_PATH - 16);
    if (n > 0 && n < MAX_PATH - 16) {
        static const wchar_t name[] = L"\\libEGL.dll";
        memcpy(path + n, name, sizeof name);
        lib = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    }
    if (!lib) lib = LoadLibraryW(L"libEGL.dll");
    if (!lib) { snprintf(err, cap, "headless: ANGLE's libEGL.dll not found (PSYSCR_ANGLE_DIR)"); return -1; }
    h->lib = (void*)lib;
#else
    if (h->device == PSYGFX_HL_MESA_SOFTWARE) setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
    h->lib = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!h->lib) { snprintf(err, cap, "headless: libEGL.so.1 not found (install libegl1 and Mesa)"); return -1; }
#endif
    h->gpa = (psygfx_hl__gpa_fn)psygfx_hl__sym(h, "eglGetProcAddress");
    return 0;
}

static int psygfx_hl__open(void* vctx, const psyscr_presenter_open* in, psyscr_caps* caps, char* err, size_t cap) {
    psygfx_headless* h = (psygfx_headless*)vctx;
    psygfx_hl__gpd_fn gpd;
    psygfx_hl__init_fn init;
    psygfx_hl__bind_fn bind_api;
    psygfx_hl__choose_fn choose;
    psygfx_hl__ctx_fn create_context;
    psygfx_hl__pb_fn create_pbuffer;
    psygfx_hl__err_fn get_error;
    psygfx_hl__str_fn get_string;
    int32_t major = 0, minor = 0, n = 0;
    void* cfg = NULL;
    int32_t attrs[16];
    if (h->w <= 0 || h->h <= 0) { snprintf(err, cap, "headless: no size"); return PSYSCR_ERR_ARG; }
    if (psygfx_hl__load(h, err, cap) < 0) return PSYSCR_ERR_NOT_IMPLEMENTED;
    gpd = (psygfx_hl__gpd_fn)psygfx_hl__sym(h, "eglGetPlatformDisplayEXT");
    init = (psygfx_hl__init_fn)psygfx_hl__sym(h, "eglInitialize");
    h->terminate = (psygfx_hl__term_fn)psygfx_hl__sym(h, "eglTerminate");
    bind_api = (psygfx_hl__bind_fn)psygfx_hl__sym(h, "eglBindAPI");
    choose = (psygfx_hl__choose_fn)psygfx_hl__sym(h, "eglChooseConfig");
    create_context = (psygfx_hl__ctx_fn)psygfx_hl__sym(h, "eglCreateContext");
    create_pbuffer = (psygfx_hl__pb_fn)psygfx_hl__sym(h, "eglCreatePbufferSurface");
    h->make_current = (psygfx_hl__cur_fn)psygfx_hl__sym(h, "eglMakeCurrent");
    h->destroy_surface = (psygfx_hl__destroy_fn)psygfx_hl__sym(h, "eglDestroySurface");
    h->destroy_context = (psygfx_hl__destroy_fn)psygfx_hl__sym(h, "eglDestroyContext");
    get_error = (psygfx_hl__err_fn)psygfx_hl__sym(h, "eglGetError");
    if (!gpd || !init || !choose || !create_context || !create_pbuffer || !h->make_current || !get_error) {
        snprintf(err, cap, "headless: the EGL library lacks EGL 1.4 and EGL_EXT_platform_base");
        return PSYSCR_ERR_NOT_IMPLEMENTED;
    }
#if defined(_WIN32)
    attrs[0] = 0x3203;                                  /* EGL_PLATFORM_ANGLE_TYPE_ANGLE */
    attrs[1] = h->device == PSYGFX_HL_SWIFTSHADER ? 0x3450 : 0x3208;   /* VULKAN : D3D11 */
    attrs[2] = 0x3209;                                  /* ..._DEVICE_TYPE_ANGLE */
    attrs[3] = h->device == PSYGFX_HL_WARP ? 0x320B : 0x3487;          /* WARP : SWIFTSHADER */
    /* D3D11 hardware is the default device; naming it was refused (0x3004) */
    if (h->device == PSYGFX_HL_HARDWARE) attrs[2] = 0x3038;
    attrs[4] = 0x3038;
    h->dpy = gpd(0x3202, NULL, attrs);                  /* EGL_PLATFORM_ANGLE_ANGLE */
#else
    h->dpy = gpd(0x31DD, NULL, NULL);                   /* EGL_PLATFORM_SURFACELESS_MESA */
#endif
    if (!h->dpy || !init(h->dpy, &major, &minor)) {
        snprintf(err, cap, "headless: eglInitialize failed (0x%x)", (unsigned)get_error());
        return PSYSCR_ERR_NOT_IMPLEMENTED;
    }
    if (bind_api) bind_api(0x30A0);                     /* EGL_OPENGL_ES_API */
    attrs[0] = 0x3024; attrs[1] = 8; attrs[2] = 0x3023; attrs[3] = 8;   /* R, G */
    attrs[4] = 0x3022; attrs[5] = 8; attrs[6] = 0x3021; attrs[7] = 8;   /* B, A */
    attrs[8] = 0x3033; attrs[9] = 0x0001;               /* PBUFFER */
    attrs[10] = 0x3040; attrs[11] = 0x0040;             /* ES3 */
    attrs[12] = 0x3038;
    if (!choose(h->dpy, attrs, &cfg, 1, &n) || n < 1) {
        snprintf(err, cap, "headless: no RGBA8 ES 3.0 pbuffer config");
        return PSYSCR_ERR_NOT_IMPLEMENTED;
    }
    attrs[0] = 0x3098; attrs[1] = 3; attrs[2] = 0x30FB; attrs[3] = 0; attrs[4] = 0x3038;
    h->ctx = create_context(h->dpy, cfg, NULL, attrs);
    attrs[0] = 0x3057; attrs[1] = h->w; attrs[2] = 0x3056; attrs[3] = h->h; attrs[4] = 0x3038;
    h->surf = h->ctx ? create_pbuffer(h->dpy, cfg, attrs) : NULL;
    if (!h->ctx || !h->surf || !h->make_current(h->dpy, h->surf, h->surf, h->ctx)) {
        snprintf(err, cap, "headless: ES 3.0 context on a %dx%d pbuffer failed (0x%x)", h->w, h->h, (unsigned)get_error());
        return PSYSCR_ERR_NOT_IMPLEMENTED;
    }
    h->flush = (psygfx_hl__flush_fn)psygfx_hl__sym(h, "glFlush");
    get_string = (psygfx_hl__str_fn)psygfx_hl__sym(h, "glGetString");
    snprintf(h->renderer, sizeof h->renderer, "%s", get_string ? (const char*)get_string(0x1F01) : "?");
    h->period = in->sim_period_ns > 0 ? in->sim_period_ns : 16666667;
    h->t0 = (int64_t)psyrt_now_ns();
    h->has_pend = h->has_done = 0;
    caps->kind = PSYSCR_FIXED_GRID;
    caps->period_ns = h->period;
    caps->native_target = true;
    caps->hw_onset = false;
    caps->max_in_flight = 1;
    caps->mode.w = h->w;
    caps->mode.h = h->h;
    caps->mode.refresh_num = 1000000000;
    caps->mode.refresh_den = (int32_t)(h->period > 0x7fffffff ? 0x7fffffff : h->period);
    caps->mode.period_ns = h->period;
    return PSYSCR_OK;
}

static void psygfx_hl__close(void* vctx) {
    psygfx_headless* h = (psygfx_headless*)vctx;
    if (h->dpy) {
        h->make_current(h->dpy, NULL, NULL, NULL);
        if (h->surf && h->destroy_surface) h->destroy_surface(h->dpy, h->surf);
        if (h->ctx && h->destroy_context) h->destroy_context(h->dpy, h->ctx);
        if (h->terminate) h->terminate(h->dpy);
    }
    h->dpy = h->ctx = h->surf = NULL;
    /* the library stays loaded: EGL keeps per-process state */
}

static int64_t psygfx_hl__count(const psygfx_headless* h, int64_t t) {
    int64_t d = t - h->t0;
    return d >= 0 ? d / h->period : -((-d + h->period - 1) / h->period);
}

static int psygfx_hl__acquire(void* vctx, int64_t deadline_ns, psyscr_vblank* newest) {
    psygfx_headless* h = (psygfx_headless*)vctx;
    int64_t c;
    (void)deadline_ns;
    if (h->has_pend) {
        int64_t t = h->t0 + h->pend_count * h->period;
        if (t > (int64_t)psyrt_now_ns()) psyrt_sleep_until((uint64_t)t, PSYRT_DEFAULT_SPIN_NS);
        h->done.present_id = h->pend_id;
        h->done.t_ns = t;
        h->done.count = h->pend_count;
        h->done.path = PSYSCR_PATH_SIMULATED;
        h->done.tier = PSYSCR_TIER_SIM;
        h->done.flags = 0;
        h->has_done = 1;
        h->has_pend = 0;
    }
    c = psygfx_hl__count(h, (int64_t)psyrt_now_ns());
    newest->present_id = 0;
    newest->count = c;
    newest->t_ns = h->t0 + c * h->period;
    newest->path = PSYSCR_PATH_SIMULATED;
    newest->tier = PSYSCR_TIER_SIM;
    newest->flags = 0;
    h->make_current(h->dpy, h->surf, h->surf, h->ctx);
    return PSYSCR_OK;
}

static int psygfx_hl__present(void* vctx, const psyscr_present_req* req) {
    psygfx_headless* h = (psygfx_headless*)vctx;
    int64_t next = psygfx_hl__count(h, (int64_t)psyrt_now_ns()) + 1;
    if (h->flush) h->flush();
    h->pend_id = req->present_id;
    h->pend_count = req->target_count > next ? req->target_count : next;
    h->has_pend = 1;
    return PSYSCR_OK;
}

static int psygfx_hl__completions(void* vctx, psyscr_vblank* out, int cap) {
    psygfx_headless* h = (psygfx_headless*)vctx;
    if (!h->has_done || cap < 1) return 0;
    out[0] = h->done;
    h->has_done = 0;
    return 1;
}

static psyscr_proc psygfx_hl__gl_proc(void* vctx, const char* name) {
    return psygfx_hl__sym((psygfx_headless*)vctx, name);
}

static void psygfx_hl__bind(void* vctx) {
    psygfx_headless* h = (psygfx_headless*)vctx;
    if (h->dpy) h->make_current(h->dpy, h->surf, h->surf, h->ctx);
}

static int psygfx_hl__describe(void* vctx, char* buf, size_t cap) {
    psygfx_headless* h = (psygfx_headless*)vctx;
    return snprintf(buf, cap, "headless %dx%d, %s", h->w, h->h, h->renderer);
}

static const psyscr_presenter psygfx_headless_presenter = {
    PSYSCR_PRESENTER_VERSION, "headless", false, false, false,
    psygfx_hl__open, psygfx_hl__close, psygfx_hl__acquire, psygfx_hl__present,
    psygfx_hl__completions, psygfx_hl__gl_proc, psygfx_hl__bind, psygfx_hl__describe
#if PSYSCR_PRESENTER_VERSION >= 2
    , NULL                                  /* gpu_done: not known */
#endif
};

#endif /* PSY_GFX_HEADLESS_H */
