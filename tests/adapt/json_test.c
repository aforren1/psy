/* json_test.c - self-checking test for ysp/json.h. No framework: it
 * returns 0 when every check passed and 1 after printing each failure.
 *
 *     gcc -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Werror -Iinclude \
 *         -o json_test tests/adapt/json_test.c -lm
 *
 * The double accessor is the conversion of ysp/table.h, copied: it is
 * checked against ytb_parse_number() and strtod() in the C locale on a
 * random corpus (YJS_TEST_NUMBERS sets its size; default 200000) built to
 * hit the hard cases, as table_test.c does.
 */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#define YSP_JSON_IMPLEMENTATION
#include "ysp/json.h"
#define YSP_TABLE_IMPLEMENTATION
#include "ysp/table.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- harness */

static int g_failures = 0;
static int g_checks = 0;

static void fail_s(int line, const char* what, const char* got, const char* want) {
    fprintf(stderr, "json_test: FAIL at line %d: %s\n  got:  [%s]\n  want: [%s]\n", line, what,
            got ? got : "(null)", want ? want : "(null)");
    g_failures++;
}

#define CHECK(cond) do { g_checks++; if (!(cond)) { \
    fprintf(stderr, "json_test: FAIL at line %d: %s\n", __LINE__, #cond); g_failures++; } } while (0)
#define CHECK_I(got, want) do { long long g_ = (long long)(got), w_ = (long long)(want); g_checks++; \
    if (g_ != w_) { fprintf(stderr, "json_test: FAIL at line %d: %s (got %lld, want %lld)\n", \
                            __LINE__, #got, g_, w_); g_failures++; } } while (0)
#define CHECK_S(got, want) do { const char* g_ = (got); const char* w_ = (want); g_checks++; \
    if (!g_ || !w_ || strcmp(g_, w_) != 0) fail_s(__LINE__, #got, g_, w_); } while (0)
#define CHECK_HAS(str, sub) do { const char* s_ = (str); g_checks++; \
    if (!s_ || !strstr(s_, (sub))) fail_s(__LINE__, "message lacks a substring", s_, (sub)); } while (0)

static uint64_t g_rng = 0x9E3779B97F4A7C15ULL;
static uint64_t rnd64(void) {
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17;
    return g_rng;
}
static int rnd(int n) { return (int)(rnd64() % (uint64_t)n); }

static uint64_t g_mem64[(4u << 20) / 8];

/* Parse into a fresh heap arena (the caller frees it). */
static int parse_heap(yjs_arena* a, const char* text, yjs_value** v, yjs_error* e) {
    yjs_arena_init_heap(a, 0);
    return yjs_parse(a, text, strlen(text), NULL, v, e);
}

static char g_out[1 << 20];
static const char* canon(const yjs_value* v, int flags) {
    size_t n = yjs_write_mem(v, flags, g_out, sizeof g_out);
    return n < sizeof g_out ? g_out : "(too long)";
}

/* ------------------------------------------------------------ the grammar */

static void accept_case(int line, const char* text, const char* compact) {
    yjs_arena a;
    yjs_value* v;
    yjs_error e;
    int rc = parse_heap(&a, text, &v, &e);
    g_checks++;
    if (rc != YJS_OK) {
        fprintf(stderr, "json_test: FAIL at line %d: refused [%s]: line %u, column %u: %s\n", line, text, e.line, e.column, e.msg);
        g_failures++;
    } else if (compact) {
        const char* got = canon(v, YJS_WRITE_COMPACT);
        if (strcmp(got, compact) != 0) fail_s(line, text, got, compact);
    }
    yjs_arena_free(&a);
}

static void refuse_case(int line, const char* text, size_t n, int code, unsigned eline, unsigned ecol, const char* says) {
    yjs_arena a;
    yjs_value* v = (yjs_value*)&a;
    yjs_error e;
    int rc;
    yjs_arena_init_heap(&a, 0);
    rc = yjs_parse(&a, text, n, NULL, &v, &e);
    g_checks++;
    if (rc != code || e.code != code || v != NULL || (eline && (e.line != eline || e.column != ecol)) || (says && !strstr(e.msg, says))) {
        fprintf(stderr, "json_test: FAIL at line %d: [%.60s] rc %d (want %d), at %u:%u (want %u:%u), '%s' (want '%s')\n", line, text, rc, code,
                e.line, e.column, eline, ecol, e.msg, says ? says : "");
        g_failures++;
    }
    yjs_arena_free(&a);
}
#define ACCEPT(t, c) accept_case(__LINE__, t, c)
#define REFUSE(t, code, l, c, says) refuse_case(__LINE__, t, strlen(t), code, l, c, says)

static void check_grammar(void) {
    ACCEPT("0", "0");
    ACCEPT("-0", "0");
    ACCEPT(" \t\r\n[ ] \n", "[]");
    ACCEPT("{}", "{}");
    ACCEPT("\xEF\xBB\xBF{\"a\":1}", "{\"a\":1}");
    ACCEPT("[true,false,null]", "[true,false,null]");
    ACCEPT("[1.5e-3, -2E+10, 0.0, 1e0]", "[1.5e-3,-2E+10,0.0,1e0]");
    ACCEPT("\"\\\"\\\\\\/\\b\\f\\n\\r\\t\"", "\"\\\"\\\\/\\u0008\\u000c\\u000a\\u000d\\u0009\"");
    ACCEPT("\"\\u00e9\\u20AC\\ud83d\\ude00\"", "\"\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\"");
    ACCEPT("\"\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\xF4\x8F\xBF\xBF\xEF\xBF\xBF\"", NULL);
    ACCEPT("{\"b\":1,\"a\":{\"d\":[],\"c\":{}},\"ab\":2,\"\":0}", "{\"\":0,\"a\":{\"c\":{},\"d\":[]},\"ab\":2,\"b\":1}");
    ACCEPT("[[[[[[[[[[1]]]]]]]]]]", "[[[[[[[[[[1]]]]]]]]]]");
    ACCEPT("123456789012345678901234567890", "123456789012345678901234567890");
    ACCEPT("\"\x7F\"", "\"\x7F\"");

    REFUSE("", YJS_ERR_SYNTAX, 1, 1, "missing");
    REFUSE("   ", YJS_ERR_SYNTAX, 1, 4, "missing");
    REFUSE("[1,]", YJS_ERR_SYNTAX, 1, 4, "not a JSON value");
    REFUSE("{\"a\":1,}", YJS_ERR_SYNTAX, 1, 8, "key");
    REFUSE("[1 2]", YJS_ERR_SYNTAX, 1, 4, "','");
    REFUSE("{\"a\" 1}", YJS_ERR_SYNTAX, 1, 6, "':'");
    REFUSE("{'a':1}", YJS_ERR_SYNTAX, 1, 2, "key");
    REFUSE("{a:1}", YJS_ERR_SYNTAX, 1, 2, "key");
    REFUSE("[1]\n// c", YJS_ERR_SYNTAX, 2, 1, "after the value");
    REFUSE("/* c */ 1", YJS_ERR_SYNTAX, 1, 1, "comment");
    REFUSE("[1] [2]", YJS_ERR_SYNTAX, 1, 5, "after the value");
    REFUSE("01", YJS_ERR_SYNTAX, 1, 1, "leading zero");
    REFUSE("-01", YJS_ERR_SYNTAX, 1, 1, "leading zero");
    REFUSE("+1", YJS_ERR_SYNTAX, 1, 1, "not a JSON value");
    REFUSE(".5", YJS_ERR_SYNTAX, 1, 1, NULL);
    REFUSE("1.", YJS_ERR_SYNTAX, 1, 1, "after '.'");
    REFUSE("1e", YJS_ERR_SYNTAX, 1, 1, "exponent");
    REFUSE("1e+", YJS_ERR_SYNTAX, 1, 1, "exponent");
    REFUSE("-", YJS_ERR_SYNTAX, 1, 1, "no digits");
    REFUSE("[-]", YJS_ERR_SYNTAX, 1, 2, "no digits");
    REFUSE("NaN", YJS_ERR_SYNTAX, 1, 1, NULL);
    REFUSE("Infinity", YJS_ERR_SYNTAX, 1, 1, NULL);
    REFUSE("[-Infinity]", YJS_ERR_SYNTAX, 1, 2, NULL);
    REFUSE("0x10", YJS_ERR_SYNTAX, 1, 2, "runs into");
    REFUSE("truex", YJS_ERR_SYNTAX, 1, 5, "runs into");
    REFUSE("tru", YJS_ERR_SYNTAX, 1, 1, NULL);
    REFUSE("nul", YJS_ERR_SYNTAX, 1, 1, NULL);
    REFUSE("[true false]", YJS_ERR_SYNTAX, 1, 7, "','");
    REFUSE("1.5.3", YJS_ERR_SYNTAX, 1, 4, "runs into");
    REFUSE("\"abc", YJS_ERR_SYNTAX, 1, 1, "does not end");
    REFUSE("\"a\tb\"", YJS_ERR_SYNTAX, 1, 3, "control character");
    REFUSE("\"a\nb\"", YJS_ERR_SYNTAX, 1, 3, "control character");
    REFUSE("\"\\x41\"", YJS_ERR_SYNTAX, 1, 2, "unknown escape");
    REFUSE("\"\\u12G4\"", YJS_ERR_SYNTAX, 1, 2, "\\u");
    REFUSE("\"\\u123\"", YJS_ERR_SYNTAX, 1, 2, "\\u");
    REFUSE("\"\\", YJS_ERR_SYNTAX, 1, 2, "does not end");
    REFUSE("\"\\ud800\"", YJS_ERR_UTF8, 1, 2, "lone surrogate");
    REFUSE("\"\\ud800\\u0041\"", YJS_ERR_UTF8, 1, 2, "lone surrogate");
    REFUSE("\"\\udc00\\ud800\"", YJS_ERR_UTF8, 1, 2, "lone surrogate");
    REFUSE("\"\\ud800\\ud800\"", YJS_ERR_UTF8, 1, 2, "lone surrogate");
    REFUSE("\"\\u0000\"", YJS_ERR_UTF8, 1, 2, "u0000");
    REFUSE("\"\xC0\xAF\"", YJS_ERR_UTF8, 1, 2, "UTF-8");          /* overlong '/' */
    REFUSE("\"\xE0\x80\xAF\"", YJS_ERR_UTF8, 1, 2, "UTF-8");      /* overlong, 3 bytes */
    REFUSE("\"\xF0\x80\x80\xAF\"", YJS_ERR_UTF8, 1, 2, "UTF-8");  /* overlong, 4 bytes */
    REFUSE("\"\xED\xA0\x80\"", YJS_ERR_UTF8, 1, 2, "UTF-8");      /* U+D800 encoded */
    REFUSE("\"\xF4\x90\x80\x80\"", YJS_ERR_UTF8, 1, 2, "UTF-8");  /* above U+10FFFF */
    REFUSE("\"\xF5\x80\x80\x80\"", YJS_ERR_UTF8, 1, 2, "UTF-8");
    REFUSE("\"\x80\"", YJS_ERR_UTF8, 1, 2, "UTF-8");              /* a lone continuation */
    REFUSE("\"\xC3\"", YJS_ERR_UTF8, 1, 2, "UTF-8");              /* truncated */
    REFUSE("\"\xE2\x82\"", YJS_ERR_UTF8, 1, 2, "UTF-8");
    REFUSE("\"\xC3\x28\"", YJS_ERR_UTF8, 1, 2, "UTF-8");
    REFUSE("\"\xFF\"", YJS_ERR_UTF8, 1, 2, "UTF-8");
    REFUSE("{\"a\":1,\"a\":2}", YJS_ERR_DUPLICATE, 1, 8, "duplicate key \"a\"");
    REFUSE("{\"a\":1,\"b\":{\"x\":1,\n\"x\":[]}}", YJS_ERR_DUPLICATE, 2, 1, "duplicate key \"x\"");
    REFUSE("{\"\\u0061\":1,\"a\":2}", YJS_ERR_DUPLICATE, 1, 13, "duplicate");
    refuse_case(__LINE__, "[1]\0", 4, YJS_ERR_SYNTAX, 1, 4, "after the value");
    refuse_case(__LINE__, "\"a\0b\"", 5, YJS_ERR_SYNTAX, 1, 3, "control character");
    REFUSE("{\n  \"a\": [\n    1,\n    x\n  ]\n}", YJS_ERR_SYNTAX, 4, 5, "not a JSON value");
    REFUSE("\xEF\xBB", YJS_ERR_SYNTAX, 1, 1, "not a JSON value");
    REFUSE("\xEF\xBB\xBF", YJS_ERR_SYNTAX, 1, 4, "missing");
    REFUSE("[\xEF\xBB\xBF" "1]", YJS_ERR_SYNTAX, 1, 2, "not a JSON value");

    /* a duplicate in a large object: found after the sort, at the later key */
    {
        char* t = (char*)malloc(200000);
        size_t p = 0;
        int i;
        t[p++] = '{';
        for (i = 0; i < 5000; i++) p += (size_t)sprintf(t + p, "%s\"k%05d\":%d", i ? "," : "", i == 4321 ? 17 : i, i);
        t[p++] = '}';
        t[p] = 0;
        refuse_case(__LINE__, t, p, YJS_ERR_DUPLICATE, 1, (unsigned)(strstr(t, "\"k00017\":4321") - t + 1), "duplicate key \"k00017\"");
        free(t);
    }
}

/* ----------------------------------------------------------------- limits */

static void check_limits(void) {
    char t[2200];
    yjs_arena a;
    yjs_value* v;
    yjs_error e;
    yjs_opts o;
    int d;
    for (d = 0; d < 64; d++) t[d] = '[';
    for (d = 0; d < 64; d++) t[64 + d] = ']';
    t[128] = 0;
    CHECK_I(parse_heap(&a, t, &v, &e), YJS_OK);
    yjs_arena_free(&a);
    memmove(t + 1, t, 129);
    t[0] = '[';
    t[130] = ']';
    t[131] = 0;
    CHECK_I(parse_heap(&a, t, &v, &e), YJS_ERR_LIMIT);
    CHECK_I(e.column, 65);
    CHECK_HAS(e.msg, "deeper");
    yjs_arena_free(&a);
    memset(&o, 0, sizeof o);
    o.max_depth = 2;
    yjs_arena_init_heap(&a, 0);
    CHECK_I(yjs_parse(&a, "[[1]]", 5, &o, &v, &e), YJS_OK);
    CHECK_I(yjs_parse(&a, "[[[1]]]", 7, &o, &v, &e), YJS_ERR_LIMIT);
    CHECK_I(yjs_parse(&a, "{\"a\":{\"b\":{}}}", 14, &o, &v, &e), YJS_ERR_LIMIT);
    o.max_depth = 5000;                                   /* clamped to 1024 */
    for (d = 0; d < 1025; d++) t[d] = '[';
    for (d = 0; d < 1025; d++) t[1025 + d] = ']';
    CHECK_I(yjs_parse(&a, t, 2050, &o, &v, &e), YJS_ERR_LIMIT);
    CHECK_I(yjs_parse(&a, t + 1, 2048, &o, &v, &e), YJS_OK);
    o.max_depth = 0;
    o.max_bytes = 4;
    CHECK_I(yjs_parse(&a, "1234", 4, &o, &v, &e), YJS_OK);
    CHECK_I(yjs_parse(&a, "12345", 5, &o, &v, &e), YJS_ERR_LIMIT);
    CHECK_HAS(e.msg, "size limit");
    CHECK_I(yjs_parse(&a, NULL, 3, NULL, &v, &e), YJS_ERR_ARG);
    CHECK_I(yjs_parse(NULL, "1", 1, NULL, &v, &e), YJS_ERR_ARG);
    CHECK_I(yjs_parse(&a, "1", 1, NULL, NULL, &e), YJS_ERR_ARG);
    CHECK_I(yjs_parse(&a, "1", 1, NULL, &v, NULL), YJS_OK);  /* no error struct: fine */
    yjs_arena_free(&a);
}

/* ------------------------------------------------------------------ values */

static void check_values(void) {
    static const char doc[] =
        "{\n"
        "  \"name\": \"rig \\u00e9\",\n"
        "  \"n\": 42,\n"
        "  \"neg\": -9223372036854775808,\n"
        "  \"max\": 9223372036854775807,\n"
        "  \"over\": 9223372036854775808,\n"
        "  \"under\": -9223372036854775809,\n"
        "  \"real\": 0.0015,\n"
        "  \"exp\": 1.5e-3,\n"
        "  \"one\": 1.0,\n"
        "  \"e3\": 1e3,\n"
        "  \"t\": true, \"f\": false, \"z\": null,\n"
        "  \"arr\": [1, \"two\", [3]]\n"
        "}";
    yjs_arena a;
    yjs_value* v;
    yjs_value* x;
    yjs_error e;
    int64_t i = 0;
    double d = 0;
    bool b = false;
    size_t n = 0;
    CHECK_I(parse_heap(&a, doc, &v, &e), YJS_OK);
    CHECK_I(v->type, YJS_OBJECT);
    CHECK_I(v->n, 14);
    CHECK_S(yjs_string(yjs_get(v, "name"), &n), "rig \xC3\xA9");
    CHECK_I(n, 6);
    CHECK_I(yjs_get(v, "name")->pos, 12);
    CHECK_I(yjs_member_n(v, "name", 4)->key_pos, 4);
    CHECK_I(yjs_int64(yjs_get(v, "n"), &i), YJS_OK);
    CHECK_I(i, 42);
    CHECK_I(yjs_get(v, "n")->flags, YJS_NUM_INT | YJS_NUM_I64);
    CHECK_I(yjs_int64(yjs_get(v, "neg"), &i), YJS_OK);
    CHECK(i == INT64_MIN);
    CHECK_I(yjs_int64(yjs_get(v, "max"), &i), YJS_OK);
    CHECK(i == INT64_MAX);
    CHECK_I(yjs_int64(yjs_get(v, "over"), &i), YJS_ERR_RANGE);
    CHECK_I(yjs_get(v, "over")->flags, YJS_NUM_INT);
    CHECK_I(yjs_int64(yjs_get(v, "under"), &i), YJS_ERR_RANGE);
    CHECK_I(yjs_int64(yjs_get(v, "real"), &i), YJS_ERR_TYPE);
    CHECK_I(yjs_int64(yjs_get(v, "one"), &i), YJS_ERR_TYPE);
    CHECK_I(yjs_int64(yjs_get(v, "e3"), &i), YJS_ERR_TYPE);
    CHECK_I(yjs_int64(yjs_get(v, "name"), &i), YJS_ERR_TYPE);
    CHECK_I(yjs_int64(yjs_get(v, "missing"), &i), YJS_ERR_TYPE);
    CHECK_I(yjs_int64(yjs_get(v, "n"), NULL), YJS_ERR_ARG);
    CHECK_I(yjs_double(yjs_get(v, "real"), &d), YJS_OK);
    CHECK(d == 0.0015);
    CHECK_I(yjs_double(yjs_get(v, "over"), &d), YJS_OK);
    CHECK(d == 9223372036854775808.0);
    CHECK_I(yjs_double(yjs_get(v, "t"), &d), YJS_ERR_TYPE);
    CHECK_I(yjs_fixed(yjs_get(v, "real"), 9, &i), YJS_OK);
    CHECK_I(i, 1500000);
    CHECK_I(yjs_fixed(yjs_get(v, "exp"), 9, &i), YJS_OK);
    CHECK_I(i, 1500000);
    CHECK_I(yjs_fixed(yjs_get(v, "e3"), 9, &i), YJS_OK);
    CHECK_I(i, 1000000000000LL);
    CHECK_I(yjs_fixed(yjs_get(v, "max"), 0, &i), YJS_OK);
    CHECK_I(yjs_fixed(yjs_get(v, "max"), 1, &i), YJS_ERR_RANGE);
    CHECK_I(yjs_fixed(yjs_get(v, "neg"), 0, &i), YJS_OK);
    CHECK(i == INT64_MIN);
    CHECK_I(yjs_fixed(yjs_get(v, "z"), 9, &i), YJS_ERR_TYPE);
    CHECK_I(yjs_bool(yjs_get(v, "t"), &b), YJS_OK);
    CHECK(b);
    CHECK_I(yjs_bool(yjs_get(v, "f"), &b), YJS_OK);
    CHECK(!b);
    CHECK_I(yjs_bool(yjs_get(v, "z"), &b), YJS_ERR_TYPE);
    CHECK_I(yjs_get(v, "z")->type, YJS_NULL);
    x = yjs_get(v, "arr");
    CHECK(x && x->type == YJS_ARRAY && x->n == 3);
    CHECK_S(yjs_string(yjs_at(x, 1), NULL), "two");
    CHECK(yjs_at(x, 3) == NULL);
    CHECK(yjs_at(v, 0) == NULL);
    CHECK(yjs_get(x, "a") == NULL);
    CHECK(yjs_get(NULL, "a") == NULL);
    CHECK(yjs_get(v, NULL) == NULL);
    CHECK(yjs_string(yjs_get(v, "n"), NULL) == NULL);
    CHECK_I(yjs_int64(yjs_at(yjs_at(x, 2), 0), &i), YJS_OK);
    CHECK_I(i, 3);
    /* every key of the object is found by the binary search */
    {
        static const char* const keys[] = { "name", "n", "neg", "max", "over", "under", "real", "exp", "one", "e3", "t", "f", "z", "arr" };
        size_t k;
        for (k = 0; k < sizeof keys / sizeof keys[0]; k++) CHECK(yjs_get(v, keys[k]) != NULL);
        CHECK(yjs_get(v, "") == NULL && yjs_get(v, "nam") == NULL && yjs_get(v, "names") == NULL && yjs_get(v, "zz") == NULL);
    }
    yjs_arena_free(&a);

    /* a large object: every key found, none extra */
    {
        char* t = (char*)malloc(400000);
        size_t p = 0;
        int k, found = 0;
        t[p++] = '{';
        for (k = 0; k < 10000; k++) p += (size_t)sprintf(t + p, "%s\"%d\":%d", k ? "," : "", (k * 7919) % 10007, k);
        t[p++] = '}';
        t[p] = 0;
        CHECK_I(parse_heap(&a, t, &v, &e), YJS_OK);
        for (k = 0; k < 10007; k++) {
            char key[16];
            sprintf(key, "%d", k);
            if (yjs_get(v, key)) found++;
        }
        CHECK_I(found, 10000);
        for (k = 1; v && k < (int)v->n; k++)
            if (strcmp(v->u.members[k - 1].key, v->u.members[k].key) >= 0) break;
        CHECK_I(k, 10000);
        yjs_arena_free(&a);
        free(t);
    }
}

/* ---------------------------------------------------------------- numbers */

static void fixed_case(int line, const char* s, int d, int rc, int64_t want) {
    int64_t got = -12345;
    int r = yjs_parse_fixed(s, strlen(s), d, &got);
    g_checks++;
    if (r != rc || (rc == YJS_OK && got != want)) {
        fprintf(stderr, "json_test: FAIL at line %d: fixed(\"%s\", %d) = %d, %lld (want %d, %lld)\n", line, s, d, r, (long long)got, rc,
                (long long)want);
        g_failures++;
    }
}
#define FIXED(s, d, rc, want) fixed_case(__LINE__, s, d, rc, want)

static int g_num_disagree = 0;
static void num_case(const char* s) {
    double a = 0, b = 0, c;
    int ra = yjs_parse_double(s, strlen(s), &a);
    int rb;
    unsigned f;
    char* end;
    if (!yjs_number_ok(s, strlen(s), &f)) {
        if (ra != YJS_ERR_TYPE) { g_num_disagree++; fprintf(stderr, "number [%s]: not JSON, rc %d\n", s, ra); }
        return;
    }
    rb = ytb_parse_number(s, strlen(s), &b);
    if ((ra == YJS_OK) != (rb == YTB_NUM_OK) || (ra == YJS_OK && memcmp(&a, &b, sizeof a) != 0)) {
        g_num_disagree++;
        if (g_num_disagree < 10) fprintf(stderr, "number [%s]: json %d %.17g, table %d %.17g\n", s, ra, a, rb, b);
        return;
    }
    if (ra == YJS_OK) {
        c = strtod(s, &end);
        if (memcmp(&a, &c, sizeof a) != 0) {
            g_num_disagree++;
            if (g_num_disagree < 10) fprintf(stderr, "number [%s]: json %.17g, strtod %.17g\n", s, a, c);
        }
    }
}

static void check_numbers(void) {
    static const char* const fixed[] = {
        "0", "-0", "0.0", "1", "-1", "1.5", "1e10", "1E-5", "0.1", "0.2", "0.3", "0.30000000000000004",
        "123456789012345678", "1234567890123456789", "9007199254740993", "1.7976931348623157e308",
        "1.7976931348623159e308", "4.9406564584124654e-324", "2.4703282292062328e-324", "2.4703282292062327e-324",
        "2.2250738585072011e-308", "1e-400", "1e400", "1e309", "1e-323", "1e-324", "1e23", "8.533e-7", "7e22", "7e23",
        "12345678901234567890", "0.12345678901234567890", "1e100000000", "1e-100000000", "1.5e0300",
        "9007199254740993.5", "4503599627370498.75", "+3", ".5", "5.", "1,5", "01",
    };
    char buf[96];
    size_t k;
    int n, extra = 200000;
    const char* env = getenv("YJS_TEST_NUMBERS");
    if (env) extra = atoi(env);
    for (k = 0; k < sizeof fixed / sizeof fixed[0]; k++) num_case(fixed[k]);
    for (n = 0; n < extra; n++) {
        switch (n % 4) {
        case 0: {
            int nd = 1 + rnd(19), j, p = 0;
            if (rnd(2)) buf[p++] = '-';
            for (j = 0; j < nd; j++) buf[p++] = (char)('0' + (j == 0 ? 1 + rnd(9) : rnd(10)));
            if (rnd(2) && nd > 1) { memmove(buf + p - nd + 2, buf + p - nd + 1, (size_t)nd - 1); buf[p - nd + 1] = '.'; p++; }
            p += sprintf(buf + p, "e%d", rnd(700) - 350);
            buf[p] = 0;
        } break;
        case 1: {
            uint64_t a = (rnd64() >> 11) | ((uint64_t)1 << 52);
            int sh = rnd(10), d = rnd(3) - 1;
            uint64_t h = ((2 * a + 1) << sh);
            if (h >= 10000000000000000000ULL) h >>= 1;
            sprintf(buf, "%llu", (unsigned long long)(h + (uint64_t)(int64_t)d));
        } break;
        case 2: {
            uint64_t u = rnd64();
            double v;
            if (rnd(4) == 0) u &= 0x000FFFFFFFFFFFFFULL;
            memcpy(&v, &u, sizeof v);
            if (v != v || v == HUGE_VAL || v == -HUGE_VAL) v = 1.0;
            sprintf(buf, "%.*e", 13 + rnd(5), v);
        } break;
        default: {
            int nd = 17 + rnd(3), j, p = 0;
            for (j = 0; j < nd; j++) buf[p++] = (char)('0' + (j == 0 ? 1 + rnd(9) : (rnd(3) ? 0 : rnd(10))));
            p += sprintf(buf + p, "e%d", rnd(80) - 40);
            buf[p] = 0;
        } break;
        }
        num_case(buf);
    }
    g_checks++;
    if (g_num_disagree) { fprintf(stderr, "json_test: FAIL: %d numbers disagree\n", g_num_disagree); g_failures++; }
    printf("numbers: %d fixed and %d random strings against ytb_parse_number and strtod, %d disagree\n",
           (int)(sizeof fixed / sizeof fixed[0]), extra, g_num_disagree);

    FIXED("0", 9, YJS_OK, 0);
    FIXED("-0", 9, YJS_OK, 0);
    FIXED("1", 9, YJS_OK, 1000000000);
    FIXED("0.000000001", 9, YJS_OK, 1);
    FIXED("0.0000000015", 9, YJS_OK, 2);      /* tie: to even */
    FIXED("0.0000000025", 9, YJS_OK, 2);      /* tie: to even */
    FIXED("0.00000000250001", 9, YJS_OK, 3);  /* above the tie */
    FIXED("0.0000000024999", 9, YJS_OK, 2);
    FIXED("-0.0000000015", 9, YJS_OK, -2);
    FIXED("-0.0000000016", 9, YJS_OK, -2);
    FIXED("0.0000000004", 9, YJS_OK, 0);
    FIXED("0.0000000005", 9, YJS_OK, 0);      /* tie to even: 0 */
    FIXED("0.00000000051", 9, YJS_OK, 1);
    FIXED("1e-30", 9, YJS_OK, 0);
    FIXED("9e-10", 9, YJS_OK, 1);
    FIXED("5e-10", 9, YJS_OK, 0);
    FIXED("1.5e-3", 9, YJS_OK, 1500000);
    FIXED("15e-4", 9, YJS_OK, 1500000);
    FIXED("0.0015E0", 9, YJS_OK, 1500000);
    FIXED("2.5", 0, YJS_OK, 2);
    FIXED("3.5", 0, YJS_OK, 4);
    FIXED("-2.5", 0, YJS_OK, -2);
    FIXED("9.223372036854775807e9", 9, YJS_OK, INT64_MAX);
    FIXED("9.223372036854775808e9", 9, YJS_ERR_RANGE, 0);
    FIXED("-9.223372036854775808e9", 9, YJS_OK, INT64_MIN);
    FIXED("-9.223372036854775809e9", 9, YJS_ERR_RANGE, 0);
    FIXED("1e10", 9, YJS_ERR_RANGE, 0);
    FIXED("1e99999999", 9, YJS_ERR_RANGE, 0);
    FIXED("0e99999999", 9, YJS_OK, 0);
    FIXED("1e-99999999", 9, YJS_OK, 0);
    FIXED("123456789012345678901234567890e-30", 9, YJS_OK, 123456789);
    FIXED("0.0000000000000000000000000001234567890123", 37 - 19, YJS_OK, 0);
    FIXED("1.0", 0, YJS_OK, 1);
    FIXED("1.", 9, YJS_ERR_TYPE, 0);
    FIXED("abc", 9, YJS_ERR_TYPE, 0);
    FIXED("", 9, YJS_ERR_TYPE, 0);
    FIXED("1", 19, YJS_ERR_ARG, 0);
    FIXED("1", -1, YJS_ERR_ARG, 0);
    {
        int64_t out;
        CHECK_I(yjs_parse_fixed("1.25 trailing", 4, 2, &out), YJS_OK);   /* n bounds the text */
        CHECK_I(out, 125);
        CHECK_I(yjs_parse_fixed(NULL, 1, 2, &out), YJS_ERR_ARG);
        CHECK_I(yjs_parse_double("1.25x", 4, NULL), YJS_ERR_ARG);
    }
    /* every int64 of a few shapes back through fixed */
    {
        int k2;
        for (k2 = 0; k2 < 20000; k2++) {
            int64_t x = (int64_t)(rnd64() >> rnd(64)), got = 0;
            int dd = rnd(10);
            yjs_arena a;
            yjs_value* v;
            if (rnd(2)) x = -x;
            yjs_arena_init(&a, g_mem64, 4096);
            v = yjs_new_fixed(&a, x, dd);
            if (!v || yjs_fixed(v, dd, &got) != YJS_OK || got != x || strchr(v->u.s, 'e') ||
                (v->n > 1 && strchr(v->u.s, '.') && v->u.s[v->n - 1] == '0')) {
                fprintf(stderr, "json_test: FAIL: fixed round trip %lld / 10^%d: '%s'\n", (long long)x, dd, v ? v->u.s : "(null)");
                g_failures++;
                break;
            }
        }
        g_checks++;
    }
}

/* ---------------------------------------------------------------- builder */

static void check_builder(void) {
    yjs_arena a;
    yjs_value* o;
    yjs_value* arr;
    yjs_value* inner;
    yjs_value* c;
    yjs_value* v;
    yjs_error e;
    int k;
    yjs_arena_init_heap(&a, 0);
    o = yjs_new(&a, YJS_OBJECT);
    CHECK(o != NULL);
    CHECK_I(yjs_set(&a, o, "b", yjs_new_int(&a, 2)), YJS_OK);
    CHECK_I(yjs_set(&a, o, "a", yjs_new_stringz(&a, "x\"y\\z\n")), YJS_OK);
    CHECK_I(yjs_set(&a, o, "c", yjs_new(&a, YJS_TRUE)), YJS_OK);
    CHECK_I(yjs_set(&a, o, "ab", yjs_new(&a, YJS_NULL)), YJS_OK);
    CHECK_I(yjs_set(&a, o, "b", yjs_new_int(&a, -0)), YJS_OK);          /* replaces */
    CHECK_I(o->n, 4);
    arr = yjs_new(&a, YJS_ARRAY);
    for (k = 0; k < 9; k++) CHECK_I(yjs_push(&a, arr, yjs_new_int(&a, k)), YJS_OK);
    CHECK_I(arr->n, 9);
    CHECK(arr->cap >= 9);
    CHECK_I(yjs_set(&a, o, "arr", arr), YJS_OK);
    inner = yjs_new(&a, YJS_OBJECT);
    CHECK_I(yjs_set(&a, inner, "fixed", yjs_new_fixed(&a, 1500000, 9)), YJS_OK);
    CHECK_I(yjs_set(&a, inner, "neg", yjs_new_fixed(&a, -2000000000, 9)), YJS_OK);
    CHECK_I(yjs_set(&a, inner, "small", yjs_new_fixed(&a, -1, 9)), YJS_OK);
    CHECK_I(yjs_set(&a, inner, "dbl", yjs_new_double(&a, 0.1)), YJS_OK);
    CHECK_I(yjs_set(&a, inner, "big", yjs_new_double(&a, 1e300)), YJS_OK);
    CHECK_I(yjs_set(&a, inner, "tiny", yjs_new_double(&a, -5e-324)), YJS_OK);
    CHECK_I(yjs_set(&a, inner, "third", yjs_new_double(&a, 1.0 / 3.0)), YJS_OK);
    CHECK_I(yjs_set(&a, inner, "int", yjs_new_double(&a, 100.0)), YJS_OK);
    CHECK_I(yjs_set(&a, inner, "txt", yjs_new_number(&a, "1.50E+3", 7)), YJS_OK);
    CHECK_I(yjs_set(&a, inner, "list", yjs_new(&a, YJS_ARRAY)), YJS_OK);
    CHECK_I(yjs_set(&a, inner, "obj", yjs_new(&a, YJS_OBJECT)), YJS_OK);
    CHECK_I(yjs_set(&a, o, "inner", inner), YJS_OK);
    c = yjs_new(&a, YJS_ARRAY);
    CHECK_I(yjs_push(&a, c, inner), YJS_OK);
    CHECK_I(yjs_push(&a, c, yjs_new_stringz(&a, "\x01")), YJS_OK);
    CHECK_I(yjs_set(&a, o, "mixed", c), YJS_OK);
    CHECK_S(canon(o, YJS_WRITE_PRETTY),
            "{\n"
            "  \"a\": \"x\\\"y\\\\z\\u000a\",\n"
            "  \"ab\": null,\n"
            "  \"arr\": [0, 1, 2, 3, 4, 5, 6, 7, 8],\n"
            "  \"b\": 0,\n"
            "  \"c\": true,\n"
            "  \"inner\": {\n"
            "    \"big\": 1e300,\n"
            "    \"dbl\": 0.1,\n"
            "    \"fixed\": 0.0015,\n"
            "    \"int\": 100,\n"
            "    \"list\": [],\n"
            "    \"neg\": -2,\n"
            "    \"obj\": {},\n"
            "    \"small\": -0.000000001,\n"
            "    \"third\": 0.3333333333333333,\n"
            "    \"tiny\": -4.94065645841247e-324,\n"
            "    \"txt\": 1.50E+3\n"
            "  },\n"
            "  \"mixed\": [\n"
            "    {\n"
            "      \"big\": 1e300,\n"
            "      \"dbl\": 0.1,\n"
            "      \"fixed\": 0.0015,\n"
            "      \"int\": 100,\n"
            "      \"list\": [],\n"
            "      \"neg\": -2,\n"
            "      \"obj\": {},\n"
            "      \"small\": -0.000000001,\n"
            "      \"third\": 0.3333333333333333,\n"
            "      \"tiny\": -4.94065645841247e-324,\n"
            "      \"txt\": 1.50E+3\n"
            "    },\n"
            "    \"\\u0001\"\n"
            "  ]\n"
            "}");
    /* idempotent: the canonical text parses to the same canonical text */
    {
        size_t n = yjs_write_mem(o, YJS_WRITE_PRETTY, NULL, 0);
        char* t1 = (char*)malloc(n + 1);
        yjs_arena b;
        CHECK_I(yjs_write_mem(o, YJS_WRITE_PRETTY, t1, n + 1), n);
        CHECK_I(parse_heap(&b, t1, &v, &e), YJS_OK);
        CHECK_S(canon(v, YJS_WRITE_PRETTY), t1);
        c = yjs_copy(&a, v);                         /* deep copy into another arena */
        yjs_arena_free(&b);
        CHECK_S(canon(c, YJS_WRITE_PRETTY), t1);
        free(t1);
    }
    /* refusals */
    CHECK(yjs_new(&a, YJS_NUMBER) == NULL);
    CHECK(yjs_new(&a, YJS_STRING) == NULL);
    CHECK(yjs_new(&a, 99) == NULL);
    CHECK(yjs_new_string(&a, "\xC0\xAF", 2) == NULL);
    CHECK(yjs_new_string(&a, "a\0b", 3) == NULL);
    CHECK(yjs_new_string(&a, NULL, 1) == NULL);
    CHECK(yjs_new_stringz(&a, NULL) == NULL);
    CHECK(yjs_new_string(&a, NULL, 0) != NULL);
    CHECK(yjs_new_number(&a, "01", 2) == NULL);
    CHECK(yjs_new_number(&a, "1.", 2) == NULL);
    CHECK(yjs_new_number(&a, "", 0) == NULL);
    CHECK(yjs_new_double(&a, HUGE_VAL) == NULL);
    CHECK(yjs_new_double(&a, -HUGE_VAL) == NULL);
    {
        volatile double z = 0.0;
        CHECK(yjs_new_double(&a, z / z) == NULL);
    }
    CHECK(yjs_new_fixed(&a, 1, 19) == NULL);
    CHECK(yjs_new_fixed(&a, 1, -1) == NULL);
    CHECK_I(yjs_set(&a, o, "\xFF", yjs_new_int(&a, 1)), YJS_ERR_UTF8);
    CHECK_I(yjs_set(&a, arr, "a", yjs_new_int(&a, 1)), YJS_ERR_ARG);
    CHECK_I(yjs_set(&a, o, "a", NULL), YJS_ERR_ARG);
    CHECK_I(yjs_set(&a, o, NULL, o), YJS_ERR_ARG);
    CHECK_I(yjs_push(&a, o, arr), YJS_ERR_ARG);
    CHECK_I(yjs_push(&a, arr, NULL), YJS_ERR_ARG);
    /* doubles round trip through their text */
    {
        int bad = 0;
        for (k = 0; k < 20000; k++) {
            uint64_t u = rnd64();
            double x, back = 0;
            yjs_value* nv;
            memcpy(&x, &u, sizeof x);
            if (x != x || x == HUGE_VAL || x == -HUGE_VAL) continue;
            nv = yjs_new_double(&a, x);
            if (!nv || yjs_double(nv, &back) != YJS_OK || memcmp(&back, &x, sizeof x) != 0) {
                if (x != 0.0 || !nv) bad++;      /* -0.0 is written "0" */
            }
        }
        CHECK_I(bad, 0);
    }
    /* a cycle is refused by the writer, not followed forever */
    {
        yjs_value* cyc = yjs_new(&a, YJS_ARRAY);
        CHECK_I(yjs_push(&a, cyc, cyc), YJS_OK);
        CHECK_I(yjs_write_mem(cyc, YJS_WRITE_PRETTY, g_out, sizeof g_out), 0);
        CHECK(yjs_copy(&a, cyc) == NULL);
    }
    yjs_arena_free(&a);
}

/* ----------------------------------------------------------------- writer */

static int g_calls = 0;
static int fail_third(void* ctx, const char* s, size_t n) {
    (void)ctx; (void)s; (void)n;
    return ++g_calls >= 3;
}

static void check_writer(void) {
    /* docs/pack.md 3's abridged manifest, in its canonical form */
    static const char manifest[] =
        "{\n"
        "  \"audio\": {\n"
        "    \"channels\": 2,\n"
        "    \"rate\": 48000\n"
        "  },\n"
        "  \"entries\": [\n"
        "    {\n"
        "      \"derivation\": {\n"
        "        \"allow_empty\": false,\n"
        "        \"types\": {\n"
        "          \"word\": \"string\"\n"
        "        }\n"
        "      },\n"
        "      \"kind\": \"table\",\n"
        "      \"size\": 400,\n"
        "      \"sources\": [\n"
        "        {\n"
        "          \"path\": \"conditions.csv\"\n"
        "        }\n"
        "      ]\n"
        "    }\n"
        "  ],\n"
        "  \"format\": \"ysp-pack\",\n"
        "  \"version\": 1\n"
        "}";
    static const char shuffled[] =
        "{\"version\":1,\"format\":\"ysp-pack\",\"entries\":[{\"sources\":[{\"path\":\"conditions.csv\"}],\"size\":400,"
        "\"kind\":\"table\",\"derivation\":{\"types\":{\"word\":\"string\"},\"allow_empty\":false}}],"
        "\"audio\":{\"rate\":48000,\"channels\":2}}";
    yjs_arena a;
    yjs_value* v;
    yjs_error e;
    size_t n;
    char small[8];
    CHECK_I(parse_heap(&a, shuffled, &v, &e), YJS_OK);
    CHECK_S(canon(v, YJS_WRITE_PRETTY), manifest);
    yjs_arena_free(&a);
    CHECK_I(parse_heap(&a, manifest, &v, &e), YJS_OK);
    CHECK_S(canon(v, YJS_WRITE_PRETTY), manifest);
    CHECK_S(canon(v, YJS_WRITE_COMPACT),
            "{\"audio\":{\"channels\":2,\"rate\":48000},\"entries\":[{\"derivation\":{\"allow_empty\":false,\"types\":{\"word\":\"string\"}},"
            "\"kind\":\"table\",\"size\":400,\"sources\":[{\"path\":\"conditions.csv\"}]}],\"format\":\"ysp-pack\",\"version\":1}");
    /* the length without a buffer, a truncated buffer, NUL-terminated */
    n = yjs_write_mem(v, YJS_WRITE_PRETTY, NULL, 0);
    CHECK_I(n, sizeof manifest - 1);
    memset(small, 'x', sizeof small);
    CHECK_I(yjs_write_mem(v, YJS_WRITE_PRETTY, small, sizeof small), n);
    CHECK(memcmp(small, "{\n  \"au\0", 8) == 0);
    CHECK_I(yjs_write(v, YJS_WRITE_PRETTY, NULL, NULL), YJS_ERR_ARG);
    CHECK_I(yjs_write(NULL, YJS_WRITE_PRETTY, fail_third, NULL), YJS_ERR_ARG);
    /* a long string goes past the batch buffer; a failing fn stops it */
    {
        static char big[3000];
        yjs_value* s;
        memset(big, 'q', sizeof big - 1);
        s = yjs_new_string(&a, big, sizeof big - 1);
        CHECK_I(yjs_write_mem(s, YJS_WRITE_PRETTY, NULL, 0), sizeof big + 1);
        g_calls = 0;
        CHECK_I(yjs_write(v, YJS_WRITE_PRETTY, fail_third, NULL), YJS_OK);   /* one batch, fewer than 3 calls */
        CHECK(g_calls < 3);
        g_calls = 0;                     /* '"', then the long run directly, then '"' */
        CHECK_I(yjs_write(s, YJS_WRITE_PRETTY, fail_third, NULL), YJS_ERR_WRITE);
        CHECK_I(g_calls, 3);
    }
    /* -0 and integer text in canonical decimal; other numbers as written */
    yjs_arena_free(&a);
    CHECK_I(parse_heap(&a, "[-0, 0, -0.0, 1E5, 007e0]", &v, &e), YJS_ERR_SYNTAX);
    yjs_arena_free(&a);
    CHECK_I(parse_heap(&a, "[-0, 0, -0.0, 1E5, 7e0, 18446744073709551616]", &v, &e), YJS_OK);
    CHECK_S(canon(v, YJS_WRITE_PRETTY), "[0, 0, -0.0, 1E5, 7e0, 18446744073709551616]");
    yjs_arena_free(&a);
}

/* ------------------------------------------------------------------ arena */

static void check_arena(void) {
    static const char doc[] = "{\"a\": [1, 2, 3, {\"b\": \"cc\"}], \"d\": \"e\", \"f\": [[[]]]}";
    yjs_arena a;
    yjs_value* v;
    yjs_error e;
    size_t need = 0, cap;
    unsigned char* base = (unsigned char*)g_mem64;
    /* a fixed buffer: find the least that works; one byte less is NOMEM */
    for (cap = 64; cap < 4096; cap += 8) {
        yjs_arena_init(&a, base, cap);
        if (yjs_parse(&a, doc, sizeof doc - 1, NULL, &v, &e) == YJS_OK) { need = cap; break; }
        CHECK_I(e.code, YJS_ERR_NOMEM);
        CHECK(a.need > 0);
    }
    CHECK(need > 0 && need < 1024);
    yjs_arena_init(&a, base, need - 8);
    CHECK_I(yjs_parse(&a, doc, sizeof doc - 1, NULL, &v, &e), YJS_ERR_NOMEM);
    CHECK_HAS(e.msg, "out of memory");
    /* the same at an odd address: the arena aligns itself */
    yjs_arena_init(&a, base + 3, need + 8);
    CHECK_I(yjs_parse(&a, doc, sizeof doc - 1, NULL, &v, &e), YJS_OK);
    CHECK(((uintptr_t)v & 7u) == 0);
    CHECK(((uintptr_t)a.p & 7u) == 0 && (a.cap & 7u) == 0);
    CHECK_S(canon(v, YJS_WRITE_COMPACT), "{\"a\":[1,2,3,{\"b\":\"cc\"}],\"d\":\"e\",\"f\":[[[]]]}");
    /* reset gives the whole buffer back */
    CHECK(a.used > 0);
    yjs_arena_reset(&a);
    CHECK_I(a.used, 0);
    CHECK_I(a.top, 0);
    CHECK_I(yjs_parse(&a, doc, sizeof doc - 1, NULL, &v, &e), YJS_OK);
    /* a buffer too small to hold anything */
    yjs_arena_init(&a, base + 1, 7);
    CHECK(a.p == NULL && a.cap == 0);
    CHECK_I(yjs_parse(&a, "1", 1, NULL, &v, &e), YJS_ERR_NOMEM);
    CHECK(yjs_alloc(&a, 1) == NULL);
    yjs_arena_init(&a, NULL, 100);
    CHECK(yjs_alloc(&a, 1) == NULL);
    /* the heap: a limit, then free */
    yjs_arena_init_heap(&a, 64 * 1024);
    {
        char* t = (char*)malloc(300000);
        size_t p = 0;
        int k;
        t[p++] = '[';
        for (k = 0; k < 20000; k++) p += (size_t)sprintf(t + p, "%s%d", k ? "," : "", k);
        t[p++] = ']';
        t[p] = 0;
        CHECK_I(yjs_parse(&a, t, p, NULL, &v, &e), YJS_ERR_NOMEM);
        CHECK(a.heap_bytes <= 64 * 1024);
        yjs_arena_free(&a);
        CHECK(a.heap == NULL && a.heap_bytes == 0 && a.heap_limit == 64 * 1024);
        yjs_arena_init_heap(&a, 0);
        CHECK_I(yjs_parse(&a, t, p, NULL, &v, &e), YJS_OK);
        CHECK_I(v->n, 20000);
        CHECK(a.heap && a.heap->next);                  /* grew past one block */
        yjs_arena_reset(&a);
        CHECK(a.heap && !a.heap->next);                 /* keeps the first */
        CHECK_I(yjs_parse(&a, "[1,2]", 5, NULL, &v, &e), YJS_OK);
        yjs_arena_free(&a);
        /* the same document in one fixed buffer: the scratch stack at the top */
        yjs_arena_init(&a, base, sizeof g_mem64);
        CHECK_I(yjs_parse(&a, t, p, NULL, &v, &e), YJS_OK);
        CHECK_I(v->n, 20000);
        CHECK_I(a.top, 0);
        CHECK(a.used < 20000u * 64u);
        printf("arena: 20000 numbers in %u bytes (%.1f per value)\n", (unsigned)a.used, (double)a.used / 20001.0);
        free(t);
    }
    /* a request larger than the limit */
    yjs_arena_init_heap(&a, 4096);
    CHECK(yjs_alloc(&a, 8192) == NULL);
    CHECK_I(a.need, 8192 + 8);
    CHECK(yjs_alloc(&a, 1000) != NULL);
    yjs_arena_free(&a);
    yjs_arena_free(NULL);
    yjs_arena_reset(NULL);
}

/* ---------------------------------------------- random damage, round trips */

static void check_damage(void) {
    static const char seed[] =
        "{\"format\": \"ysp-rig 1\", \"roles\": {\"resp\": {\"family\": \"xid\", \"key\": \"serial:0403:6001:FT4ABC:\", "
        "\"latency\": {\"n\": 200, \"median_s\": 0.000577, \"p95_s\": 1.5e-3, \"date\": \"2026-10-09T12:00:00Z\"}, "
        "\"options\": {\"buttons\": {\"1\": \"left\", \"2\": \"right\"}, \"latched\": false}}}, \"display\": null, "
        "\"s\": \"\\u00e9\\ud83d\\ude00\\n\", \"arr\": [1, -2, 3.25, [true, null]]}";
    char buf[512];
    int k, ok = 0, bad = 0;
    for (k = 0; k < 40000; k++) {
        size_t n = sizeof seed - 1, j;
        int edits = 1 + rnd(4), m;
        yjs_arena a;
        yjs_value* v;
        yjs_error e;
        memcpy(buf, seed, n);
        for (m = 0; m < edits; m++) {
            j = (size_t)rnd((int)n);
            switch (rnd(4)) {
            case 0: buf[j] = (char)rnd(256); break;
            case 1: {
                static const char tok[] = "{}[]\",:\\ 0e-.tfn";
                buf[j] = tok[rnd((int)sizeof tok - 1)];
            } break;
            case 2: if (n > 1) { memmove(buf + j, buf + j + 1, n - j - 1); n--; } break;
            default: if (n < sizeof buf - 1) { memmove(buf + j + 1, buf + j, n - j); buf[j] = (char)rnd(128); n++; } break;
            }
        }
        yjs_arena_init(&a, g_mem64, 8192);
        if (yjs_parse(&a, buf, n, NULL, &v, &e) == YJS_OK) {
            /* what parses writes canonical text that parses to itself */
            static char t1[8192], t2[8192];
            size_t n1 = yjs_write_mem(v, YJS_WRITE_PRETTY, t1, sizeof t1), n2;
            yjs_arena b;
            yjs_value* w;
            yjs_arena_init(&b, (unsigned char*)g_mem64 + 16384, 16384);
            if (yjs_parse(&b, t1, n1, NULL, &w, &e) != YJS_OK) { bad++; continue; }
            n2 = yjs_write_mem(w, YJS_WRITE_PRETTY, t2, sizeof t2);
            if (n1 != n2 || memcmp(t1, t2, n1) != 0) bad++;
            ok++;
        } else if (e.code == YJS_OK || e.line < 1 || e.column < 1 || e.offset > n || !e.msg[0]) {
            bad++;
        }
    }
    CHECK_I(bad, 0);
    printf("damage: 40000 edits of a profile-like document, %d still JSON, each canonical and stable\n", ok);
}

int main(void) {
    printf("ysp_json %s\n", yjs_version());
    CHECK_S(yjs_version(), YJS_VERSION_STRING);
    check_grammar();
    check_limits();
    check_values();
    check_numbers();
    check_builder();
    check_writer();
    check_arena();
    check_damage();
    {
        uint32_t l, c;
        yjs_line_col("ab\ncd\n", 6, 4, &l, &c);
        CHECK(l == 2 && c == 2);
        yjs_line_col("ab", 2, 99, &l, &c);
        CHECK(l == 1 && c == 3);
        yjs_line_col(NULL, 5, 3, &l, &c);
        CHECK(l == 1 && c == 1);
        CHECK(yjs_utf8_ok("", 0) && !yjs_utf8_ok(NULL, 1) && yjs_utf8_ok("\xF0\x9F\x98\x80", 4) && !yjs_utf8_ok("\xF0\x9F\x98", 3));
    }
    if (g_failures) {
        fprintf(stderr, "json_test: %d of %d checks FAILED\n", g_failures, g_checks);
        return 1;
    }
    printf("json_test: all %d checks passed\n", g_checks);
    return 0;
}
