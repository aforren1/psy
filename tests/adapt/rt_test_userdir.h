/* rt_test_userdir.h - the per-user folders (yrt_user_dir, PER-USER FOLDERS),
 * included by rt_test.c, which provides CHECK and g_failures.
 *
 * The process's environment is set to known values and put back. The
 * folders named do not exist (POSIX) or are only strings (Windows): the
 * function makes no folder, so nothing is created. ysp/gfx.h's test checks
 * ygfx_default_cache_dir(), the CACHE wrapper, with its own cases. */
#if defined(YRT__POSIX)
#include <stdlib.h>
#endif

static void test_user_dir_args(void) {
    char d[512], few[8];
    static const char* const bad[] = { "/a", "a/", "a//b", ".", "..", "a/../b", "./a",
                                       "a\\b", "c:x", "a\001b" };
    size_t i;
    CHECK(yrt_user_dir(YRT_DIR_CACHE, NULL, NULL, sizeof d) == YRT_ERR_ARG, "user dir: NULL out");
    d[0] = 'x';
    CHECK(yrt_user_dir(YRT_DIR_CACHE, NULL, d, 0) == YRT_ERR_ARG && d[0] == 'x', "user dir: cap 0 leaves out alone");
    few[0] = 'x';
    CHECK(yrt_user_dir(YRT_DIR_CONFIG, "rig", few, sizeof few) == YRT_ERR_ARG && few[0] == '\0',
          "user dir: a path that does not fit leaves out empty");
    CHECK(yrt_user_dir(2, NULL, d, sizeof d) == YRT_ERR_ARG && d[0] == '\0', "user dir: kind 2");
    CHECK(yrt_user_dir(-1, NULL, d, sizeof d) == YRT_ERR_ARG && d[0] == '\0', "user dir: kind -1");
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        d[0] = 'x';
        CHECK(yrt_user_dir(YRT_DIR_CONFIG, bad[i], d, sizeof d) == YRT_ERR_ARG && d[0] == '\0',
              "user dir: a refused subdir");
    }
}

#if defined(YRT__WINDOWS)
static void test_user_dir_env(void) {
    char d[512];
    wchar_t old_l[512], old_a[512];
    DWORD kl = GetEnvironmentVariableW(L"LOCALAPPDATA", old_l, 512);
    DWORD ka = GetEnvironmentVariableW(L"APPDATA", old_a, 512);
    static const char cache[] = "C:\\Users\\t\xc3\xa9st\\AppData\\Local\\ysp\\progcache";
    static const char config[] = "C:\\Users\\t\xc3\xa9st\\AppData\\Roaming\\ysp\\rig\\bindings";
    /* a non-ASCII user name comes out as UTF-8; trailing slashes go */
    SetEnvironmentVariableW(L"LOCALAPPDATA", L"C:\\Users\\t\u00e9st\\AppData\\Local\\");
    SetEnvironmentVariableW(L"APPDATA", L"C:\\Users\\t\u00e9st\\AppData\\Roaming//");
    CHECK(yrt_user_dir(YRT_DIR_CACHE, "progcache", d, sizeof d) == YRT_OK && strcmp(d, cache) == 0,
          "user dir: CACHE is %LOCALAPPDATA%\\ysp\\progcache, in UTF-8");
    CHECK(yrt_user_dir(YRT_DIR_CONFIG, "rig/bindings", d, sizeof d) == YRT_OK && strcmp(d, config) == 0,
          "user dir: CONFIG is %APPDATA%\\ysp, '/' becomes '\\'");
    CHECK(yrt_user_dir(YRT_DIR_CONFIG, NULL, d, sizeof d) == YRT_OK &&
          strcmp(d, "C:\\Users\\t\xc3\xa9st\\AppData\\Roaming\\ysp") == 0, "user dir: NULL subdir is the ysp folder");
    CHECK(yrt_user_dir(YRT_DIR_CONFIG, "", d, sizeof d) == YRT_OK &&
          strcmp(d, "C:\\Users\\t\xc3\xa9st\\AppData\\Roaming\\ysp") == 0, "user dir: empty subdir is the ysp folder");
    /* exactly the size with its NUL fits; one byte less does not */
    CHECK(yrt_user_dir(YRT_DIR_CACHE, "progcache", d, sizeof cache) == YRT_OK && strcmp(d, cache) == 0,
          "user dir: an exact fit");
    CHECK(yrt_user_dir(YRT_DIR_CACHE, "progcache", d, sizeof cache - 1) == YRT_ERR_ARG && d[0] == '\0',
          "user dir: one byte short");
    SetEnvironmentVariableW(L"APPDATA", L"\\\\server\\share");
    CHECK(yrt_user_dir(YRT_DIR_CONFIG, "rig", d, sizeof d) == YRT_OK && strcmp(d, "\\\\server\\share\\ysp\\rig") == 0,
          "user dir: a UNC base");
    SetEnvironmentVariableW(L"APPDATA", L"relative\\x");
    CHECK(yrt_user_dir(YRT_DIR_CONFIG, "rig", d, sizeof d) == YRT_ERR_ARG && d[0] == '\0', "user dir: a relative base");
    CHECK(yrt_user_dir(YRT_DIR_CACHE, "rig", d, sizeof d) == YRT_OK, "user dir: CACHE does not read APPDATA");
    SetEnvironmentVariableW(L"APPDATA", NULL);
    CHECK(yrt_user_dir(YRT_DIR_CONFIG, "rig", d, sizeof d) == YRT_ERR_ARG && d[0] == '\0', "user dir: no APPDATA");
    SetEnvironmentVariableW(L"LOCALAPPDATA", NULL);
    CHECK(yrt_user_dir(YRT_DIR_CACHE, "rig", d, sizeof d) == YRT_ERR_ARG && d[0] == '\0', "user dir: no LOCALAPPDATA");
    SetEnvironmentVariableW(L"LOCALAPPDATA", kl > 0 && kl < 512 ? old_l : NULL);
    SetEnvironmentVariableW(L"APPDATA", ka > 0 && ka < 512 ? old_a : NULL);
    if (kl > 0 && kl < 512) CHECK(yrt_user_dir(YRT_DIR_CACHE, "progcache", d, sizeof d) == YRT_OK, "user dir: this machine's CACHE");
    if (ka > 0 && ka < 512) CHECK(yrt_user_dir(YRT_DIR_CONFIG, "rig", d, sizeof d) == YRT_OK, "user dir: this machine's CONFIG");
    printf("  user dir: Windows cases run; this machine's CONFIG: %s\n", d);
}
#elif defined(YRT__EMSCRIPTEN)
static void test_user_dir_env(void) {
    char d[512];
    /* the web has no per-user folder, whatever the environment says */
    CHECK(yrt_user_dir(YRT_DIR_CACHE, "progcache", d, sizeof d) == YRT_ERR_ARG && d[0] == '\0', "user dir: no CACHE on the web");
    CHECK(yrt_user_dir(YRT_DIR_CONFIG, "rig", d, sizeof d) == YRT_ERR_ARG && d[0] == '\0', "user dir: no CONFIG on the web");
    printf("  user dir: the web refuses both kinds\n");
}
#else
static void user_dir_env_save(const char* name, char* buf, size_t cap, int* had) {
    const char* e = getenv(name);
    *had = e != NULL;
    if (e) snprintf(buf, cap, "%s", e);
}

static void user_dir_env_restore(const char* name, const char* buf, int had) {
    if (had) setenv(name, buf, 1); else unsetenv(name);
}

static void test_user_dir_env(void) {
    char d[512], home[512] = "", xc[512] = "", xg[512] = "";
    int had_home, had_xc, had_xg;
    user_dir_env_save("HOME", home, sizeof home, &had_home);
    user_dir_env_save("XDG_CACHE_HOME", xc, sizeof xc, &had_xc);
    user_dir_env_save("XDG_CONFIG_HOME", xg, sizeof xg, &had_xg);
    setenv("HOME", "/nonexistent-yrt/home//", 1);
    setenv("XDG_CACHE_HOME", "/nonexistent-yrt/xc/", 1);
    setenv("XDG_CONFIG_HOME", "/nonexistent-yrt/xg/", 1);
#if defined(YRT__DARWIN)
    CHECK(yrt_user_dir(YRT_DIR_CACHE, "progcache", d, sizeof d) == YRT_OK &&
          strcmp(d, "/nonexistent-yrt/home/Library/Caches/ysp/progcache") == 0, "user dir: macOS CACHE");
    CHECK(yrt_user_dir(YRT_DIR_CONFIG, "rig/bindings", d, sizeof d) == YRT_OK &&
          strcmp(d, "/nonexistent-yrt/home/Library/Application Support/ysp/rig/bindings") == 0, "user dir: macOS CONFIG");
#else
    CHECK(yrt_user_dir(YRT_DIR_CACHE, "progcache", d, sizeof d) == YRT_OK &&
          strcmp(d, "/nonexistent-yrt/xc/ysp/progcache") == 0, "user dir: XDG_CACHE_HOME");
    CHECK(yrt_user_dir(YRT_DIR_CONFIG, "rig/bindings", d, sizeof d) == YRT_OK &&
          strcmp(d, "/nonexistent-yrt/xg/ysp/rig/bindings") == 0, "user dir: XDG_CONFIG_HOME");
    CHECK(yrt_user_dir(YRT_DIR_CONFIG, NULL, d, sizeof d) == YRT_OK &&
          strcmp(d, "/nonexistent-yrt/xg/ysp") == 0, "user dir: NULL subdir is the ysp folder");
    setenv("XDG_CACHE_HOME", "relative", 1);    /* the XDG spec: ignored */
    setenv("XDG_CONFIG_HOME", "relative", 1);
    CHECK(yrt_user_dir(YRT_DIR_CACHE, "progcache", d, sizeof d) == YRT_OK &&
          strcmp(d, "/nonexistent-yrt/home/.cache/ysp/progcache") == 0, "user dir: HOME/.cache");
    CHECK(yrt_user_dir(YRT_DIR_CONFIG, "rig", d, sizeof d) == YRT_OK &&
          strcmp(d, "/nonexistent-yrt/home/.config/ysp/rig") == 0, "user dir: HOME/.config");
    setenv("XDG_CONFIG_HOME", "/tmp", 1);       /* 1777: refused, no other folder tried */
    CHECK(yrt_user_dir(YRT_DIR_CONFIG, "rig", d, sizeof d) == YRT_ERR_ARG && d[0] == '\0', "user dir: /tmp refused");
    unsetenv("XDG_CACHE_HOME");
    unsetenv("XDG_CONFIG_HOME");
#endif
    setenv("HOME", "/tmp", 1);
    CHECK(yrt_user_dir(YRT_DIR_CACHE, "progcache", d, sizeof d) == YRT_ERR_ARG && d[0] == '\0', "user dir: HOME=/tmp refused");
    CHECK(yrt_user_dir(YRT_DIR_CONFIG, "rig", d, sizeof d) == YRT_ERR_ARG && d[0] == '\0', "user dir: HOME=/tmp refused");
    setenv("HOME", "home", 1);
    CHECK(yrt_user_dir(YRT_DIR_CONFIG, "rig", d, sizeof d) == YRT_ERR_ARG && d[0] == '\0', "user dir: relative HOME");
    unsetenv("HOME");
    CHECK(yrt_user_dir(YRT_DIR_CONFIG, "rig", d, sizeof d) == YRT_ERR_ARG && d[0] == '\0', "user dir: no HOME");
    user_dir_env_restore("HOME", home, had_home);
    user_dir_env_restore("XDG_CACHE_HOME", xc, had_xc);
    user_dir_env_restore("XDG_CONFIG_HOME", xg, had_xg);
    printf("  user dir: POSIX cases run\n");
}
#endif

static void test_user_dir(void) {
    test_user_dir_args();
    test_user_dir_env();
}
