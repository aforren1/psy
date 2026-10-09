/* box_test.c - self-checking test for ysp/box.h. No framework: it returns
 * 0 when every check passed and 1 after printing each failure.
 *
 * Each family decodes a generated stream whose frames are known: whole, a
 * byte at a time and in random chunks; with garbage between frames that
 * cannot start one (counted exactly), with broken frames whose rest holds
 * a real frame (found again), with output arrays too small for a read, and
 * as random bytes (no crash, progress, counts that add up). The XID bytes
 * follow pyxid2's struct formats ('<cBI' frames, '<cccI' timer answers);
 * no byte from a Cedrus device was available.
 *
 *     gcc -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Werror -Iinclude \
 *         -o box_test tests/adapt/box_test.c
 */
#define YSP_BOX_IMPLEMENTATION
#include "ysp/box.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond) do { g_checks++; if (!(cond)) { \
    fprintf(stderr, "box_test: FAIL at line %d: %s\n", __LINE__, #cond); g_failures++; } } while (0)
#define CHECK_I(got, want) do { long long g_ = (long long)(got), w_ = (long long)(want); g_checks++; \
    if (g_ != w_) { fprintf(stderr, "box_test: FAIL at line %d: %s (got %lld, want %lld)\n", \
                            __LINE__, #got, g_, w_); g_failures++; } } while (0)

static uint64_t g_rng = 0x2545F4914F6CDD1Dull;
static uint32_t rnd(uint32_t n) {
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17;
    return n ? (uint32_t)(g_rng % n) : 0;
}

/* --- expected frames ------------------------------------------------------- */

typedef struct expect {
    int      is_event;
    uint8_t  type;
    uint32_t control, code;
    float    value;
    uint64_t ticks;
    uint32_t probe;
} expect;

#define MAXF 4096
static expect   g_exp[MAXF];
static int      g_nexp;
static uint8_t  g_bytes[1 << 20];
static size_t   g_nbytes;

static void put(const void* b, size_t n) { memcpy(g_bytes + g_nbytes, b, n); g_nbytes += n; }
static void put_le32(uint32_t v) {
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    put(b, 4);
}

/* Bytes that start no frame in the family: garbage the decoder must skip
 * one by one. */
static uint8_t garbage_byte(int family) {
    for (;;) {
        uint8_t b = (uint8_t)rnd(256);
        if (family == YBOX_XID && (b == 'k' || b == '_')) continue;
        if (family == YBOX_PHOTO && (b == 0xA5 || b == 0x5A)) continue;
        if (family == YBOX_LINE && b == '\n') continue;
        return b;
    }
}

static void gen_xid(int frames, int garbage) {
    int i;
    for (i = 0; i < frames; i++) {
        expect* e = &g_exp[g_nexp++];
        int k;
        memset(e, 0, sizeof *e);
        e->ticks = rnd(0xFFFFFFFFu);
        for (k = 0; k < garbage; k++) { uint8_t b = garbage_byte(YBOX_XID); put(&b, 1); }
        if (rnd(4) == 0) {
            put("_e5", 3);
            put_le32((uint32_t)e->ticks);
            e->probe = 1;
        } else {
            uint32_t key = 1 + rnd(8), port = rnd(4), pressed = rnd(2);
            uint8_t hdr[2];
            hdr[0] = 'k';
            hdr[1] = (uint8_t)(((key & 7u) << 5) | (pressed << 4) | port);   /* key 8 is 0 */
            put(hdr, 2);
            put_le32((uint32_t)e->ticks);
            e->is_event = 1;
            e->type = pressed ? YIN_PRESS : YIN_RELEASE;
            e->control = key;
            e->code = port;
            e->value = (float)pressed;
        }
    }
}

static void gen_line(int frames, int garbage) {
    int i;
    for (i = 0; i < frames; i++) {
        expect* e = &g_exp[g_nexp++];
        char s[128];
        int n = 0, k, what = (int)rnd(5);
        memset(e, 0, sizeof *e);
        e->ticks = ((uint64_t)rnd(0xFFFFFFFFu) << (rnd(2) ? 20 : 0)) + rnd(1000);
        if (garbage) {
            /* a garbage line: text that is no frame */
            for (k = 0; k < garbage; k++) s[k] = (char)('a' + rnd(26));
            s[garbage] = '\n';
            put(s, (size_t)garbage + 1);
        }
        switch (what) {
        case 0:
            n = snprintf(s, sizeof s, "S %llu\n", (unsigned long long)e->ticks);
            break;
        case 1:
            e->probe = 1 + rnd(0xFFFFFFFEu);
            n = snprintf(s, sizeof s, "Q %lu %llu\r\n", (unsigned long)e->probe, (unsigned long long)e->ticks);
            break;
        case 2: case 3: {
            uint32_t ch = 1 + rnd(255), lvl = rnd(2);
            n = snprintf(s, sizeof s, "E %llu %lu %lu\n", (unsigned long long)e->ticks, (unsigned long)ch,
                         (unsigned long)lvl);
            e->is_event = 1;
            e->type = lvl ? YIN_PRESS : YIN_RELEASE;
            e->control = ch;
            e->value = (float)lvl;
            break;
        }
        default: {
            uint32_t ch = 1 + rnd(255);
            double v = ((double)rnd(2000001) - 1000000.0) / 1000.0;
            n = snprintf(s, sizeof s, "A %llu %lu %.3f\n", (unsigned long long)e->ticks, (unsigned long)ch, v);
            e->is_event = 1;
            e->type = YIN_SAMPLE;
            e->control = ch;
            e->value = (float)v;
            break;
        }
        }
        put(s, (size_t)n);
    }
}

static void gen_photo(int frames, int garbage) {
    int i;
    for (i = 0; i < frames; i++) {
        expect* e = &g_exp[g_nexp++];
        uint8_t hdr[2];
        int k;
        memset(e, 0, sizeof *e);
        e->ticks = rnd(0xFFFFFFFFu);
        for (k = 0; k < garbage; k++) { uint8_t b = garbage_byte(YBOX_PHOTO); put(&b, 1); }
        if (rnd(3) == 0) {
            hdr[0] = 0x5A; hdr[1] = 0;
        } else {
            uint8_t lvl = (uint8_t)rnd(2);
            hdr[0] = 0xA5; hdr[1] = lvl;
            e->is_event = 1;
            e->type = lvl ? YIN_PRESS : YIN_RELEASE;
            e->control = 1;
            e->value = lvl;
        }
        put(hdr, 2);
        put_le32((uint32_t)e->ticks);
    }
}

/* --- decoding --------------------------------------------------------------- */

static yin_event g_ev[MAXF * 2];
static ybox_pair g_pair[MAXF * 2];
static int g_nev, g_npair;
static uint64_t g_garbage;

/* chunk 0: whole; 1: a byte at a time; 2: random sizes. cap: the output
 * arrays' size per call; the decoder must never write past it. */
static void decode_all(int family, int chunk, int cap) {
    ybox_decoder d;
    yin_event ev[64];
    ybox_pair pr[64];
    ybox_out o;
    size_t at = 0;
    g_nev = g_npair = 0;
    g_garbage = 0;
    CHECK(ybox_init(&d, family, YIN_KIND_BOX, 7));
    memset(&o, 0, sizeof o);
    o.ev = ev; o.ev_cap = cap; o.pair = pr; o.pair_cap = cap;
    while (at < g_nbytes) {
        size_t n = chunk == 0 ? g_nbytes - at : chunk == 1 ? 1 : 1 + rnd(40);
        size_t used = 0;
        if (n > g_nbytes - at) n = g_nbytes - at;
        while (used < n) {
            size_t u;
            int i;
            ybox_out_clear(&o);
            u = ybox_decode(&d, g_bytes + at + used, n - used, 1000 + (int64_t)at, &o);
            if (o.n_ev > cap || o.n_pair > cap) { CHECK(o.n_ev <= cap && o.n_pair <= cap); return; }
            if (u == 0) { CHECK(u > 0); return; }
            used += u;
            for (i = 0; i < o.n_ev; i++) g_ev[g_nev++] = ev[i];
            for (i = 0; i < o.n_pair; i++) g_pair[g_npair++] = pr[i];
            g_garbage += o.garbage;
        }
        at += n;
    }
    CHECK_I(d.garbage, g_garbage);
}

/* Every expected frame in order, every field. */
static void compare(int family, const char* what) {
    int i, ie = 0, ip = 0, bad = 0;
    for (i = 0; i < g_nexp; i++) {
        const expect* e = &g_exp[i];
        if (ip >= g_npair || g_pair[ip].ticks != e->ticks || g_pair[ip].probe != e->probe) bad++;
        ip++;
        if (!e->is_event) continue;
        if (ie >= g_nev) { bad++; continue; }
        {
            const yin_event* v = &g_ev[ie++];
            if (v->type != e->type || v->control != e->control || v->kind != YIN_KIND_BOX || v->device != 7 ||
                v->ticks != (uint32_t)e->ticks || !(v->flags & YIN_DEVTICKS) || fabsf(v->value - e->value) > 1e-3f ||
                (family == YBOX_XID && v->code != e->code))
                bad++;
        }
    }
    if (ie != g_nev || ip != g_npair) bad++;
    if (bad) fprintf(stderr, "box_test: %s %s: %d frames wrong (%d events, %d pairs decoded)\n",
                     ybox_family_name(family), what, bad, g_nev, g_npair);
    CHECK_I(bad, 0);
}

typedef void (*gen_fn)(int, int);

static void test_family(int family, gen_fn gen) {
    int chunk;
    /* clean stream, three ways of splitting it, two output sizes */
    for (chunk = 0; chunk < 3; chunk++) {
        g_nexp = 0; g_nbytes = 0;
        gen(1000, 0);
        decode_all(family, chunk, 64);
        compare(family, chunk == 0 ? "whole" : chunk == 1 ? "byte by byte" : "random chunks");
        CHECK_I(g_garbage, 0);
        decode_all(family, chunk, 1);
        compare(family, "one at a time");
    }
    /* garbage between frames, counted exactly for the byte families */
    g_nexp = 0; g_nbytes = 0;
    gen(1000, 3);
    decode_all(family, 2, 64);
    compare(family, "with garbage");
    if (family == YBOX_LINE) CHECK_I(g_garbage, 1000 * 4);
    else CHECK_I(g_garbage, 1000 * 3);
    printf("  %s: 1000 frames whole, byte by byte, in chunks, 1 per call, with garbage: ok\n",
           ybox_family_name(family));
}

/* A frame that breaks after its start, with a real frame inside its bytes. */
static void test_resync(void) {
    static const uint8_t xid[] = { 'k', 0x04,   /* port bit 2: no frame */
                                   'k', 0x30, 0x10, 0x00, 0x00, 0x00,   /* key 1, pressed, 16 ms */
                                   '_', 'e', 'x',                       /* not a timer answer */
                                   '_', 'e', '5', 0x01, 0x02, 0x00, 0x00 };
    static const uint8_t photo[] = { 0xA5, 0x07,   /* level 7: no frame */
                                     0x5A, 0x00, 0x10, 0x00, 0x00, 0x00,
                                     0x5A, 0x01,   /* sync with level 1: no frame */
                                     0xA5, 0x01, 0x20, 0x00, 0x00, 0x00 };
    g_nbytes = 0; put(xid, sizeof xid);
    decode_all(YBOX_XID, 1, 64);
    CHECK_I(g_nev, 1);
    CHECK_I(g_npair, 2);
    CHECK(g_nev == 1 && g_ev[0].control == 1 && g_ev[0].type == YIN_PRESS && g_ev[0].ticks == 16);
    CHECK(g_npair == 2 && g_pair[1].ticks == 0x0201 && g_pair[1].probe == 1);
    CHECK_I(g_garbage, 5);   /* 'k' 0x04, then '_' 'e' 'x' as three */
    g_nbytes = 0; put(xid, sizeof xid);
    decode_all(YBOX_XID, 0, 64);
    CHECK(g_nev == 1 && g_npair == 2);
    g_nbytes = 0; put(photo, sizeof photo);
    decode_all(YBOX_PHOTO, 1, 64);
    CHECK(g_nev == 1 && g_ev[0].type == YIN_PRESS && g_ev[0].ticks == 0x20);
    CHECK(g_npair == 2 && g_pair[0].ticks == 0x10);
    CHECK_I(g_garbage, 4);
    /* key 8 is sent as 0 */
    {
        static const uint8_t k8[] = { 'k', 0x03, 1, 0, 0, 0 };
        g_nbytes = 0; put(k8, sizeof k8);
        decode_all(YBOX_XID, 0, 64);
        CHECK(g_nev == 1 && g_ev[0].control == 8 && g_ev[0].type == YIN_RELEASE && g_ev[0].code == 3);
    }
    printf("  resync after broken frames: ok\n");
}

static void test_line_rules(void) {
    static const char good[] =
        "# a comment\n"
        "\n"
        "I ysp-line 1 teensy41 photodiode\r\n"
        "A 5 2 -1.5e-3\n"
        "E 18446744073709551615 255 0\n";
    static const char bad[] =
        "Q 0 5\n"                         /* seq 0 */
        "Q 4294967296 5\n"                /* seq past 32 bits */
        "E 5 0 1\n"                       /* channel 0 */
        "E 5 256 1\n"                     /* channel 256 */
        "E 5 1 2\n"                       /* level 2 */
        "E 5 1\n"                         /* missing field */
        "E 5 1 1 9\n"                     /* extra field */
        "S 18446744073709551616\n"        /* past 64 bits */
        "S -5\n"
        "A 5 1 x\n"
        "A 5 1\n"
        "X 5\n"
        "S5\n";
    ybox_decoder d;
    yin_event ev[16];
    ybox_pair pr[16];
    ybox_out o;
    char longline[300];
    memset(&o, 0, sizeof o);
    o.ev = ev; o.ev_cap = 16; o.pair = pr; o.pair_cap = 16;
    ybox_init(&d, YBOX_LINE, YIN_KIND_SYNC, 3);
    ybox_out_clear(&o);
    CHECK_I(ybox_decode(&d, (const uint8_t*)good, sizeof good - 1, 9, &o), sizeof good - 1);
    CHECK(o.has_text && strcmp(o.text, "ysp-line 1 teensy41 photodiode") == 0);
    CHECK(o.n_ev == 2 && ev[0].type == YIN_SAMPLE && fabsf(ev[0].value + 1.5e-3f) < 1e-7f && ev[0].control == 2);
    CHECK(o.n_ev == 2 && ev[1].ticks == 0xFFFFFFFFu && ev[1].control == 255 && ev[1].kind == YIN_KIND_SYNC);
    CHECK(o.n_pair == 2 && pr[1].ticks == 18446744073709551615ull);
    CHECK_I(o.garbage, 0);
    ybox_out_clear(&o);
    CHECK_I(ybox_decode(&d, (const uint8_t*)bad, sizeof bad - 1, 9, &o), sizeof bad - 1);
    CHECK_I(o.n_ev, 0);
    CHECK_I(o.n_pair, 0);
    CHECK_I(o.garbage, sizeof bad - 1);
    /* an overlong line and a line with a control byte are dropped whole */
    memset(longline, 'S', sizeof longline);
    longline[sizeof longline - 1] = '\n';
    ybox_out_clear(&o);
    (void)ybox_decode(&d, (const uint8_t*)longline, sizeof longline, 9, &o);
    (void)ybox_decode(&d, (const uint8_t*)"S 1\x01" "2\nS 7\n", 10, 9, &o);
    CHECK(o.n_pair == 1 && pr[0].ticks == 7);
    CHECK_I(o.garbage, sizeof longline + 6);   /* each dropped line with its newline */
    /* the tail of an overlong line is not a line of its own */
    memset(longline, 'x', 96);
    memcpy(longline + 96, "S 5\n", 4);
    ybox_out_clear(&o);
    (void)ybox_decode(&d, (const uint8_t*)longline, 100, 9, &o);
    CHECK_I(o.n_pair, 0);
    printf("  line protocol rules: ok\n");
}

static void test_queries(void) {
    uint8_t b[32];
    CHECK_I(ybox_probe(YBOX_XID, 0, b, 32), 3);
    CHECK(memcmp(b, "_e5", 3) == 0);
    CHECK_I(ybox_probe(YBOX_XID, 0, b, 2), 0);
    CHECK_I(ybox_probe(YBOX_LINE, 42, b, 32), 5);
    CHECK(memcmp(b, "q 42\n", 5) == 0);
    CHECK_I(ybox_probe(YBOX_LINE, 0, b, 32), 0);
    CHECK_I(ybox_probe(YBOX_LINE, 4294967295u, b, 32), 13);
    CHECK_I(ybox_probe(YBOX_LINE, 4294967295u, b, 12), 0);
    CHECK_I(ybox_probe(YBOX_PHOTO, 1, b, 32), 0);
    CHECK_I(ybox_identify(YBOX_XID, b, 32), 3);
    CHECK(memcmp(b, "_c1", 3) == 0);
    CHECK_I(ybox_identify(YBOX_LINE, b, 32), 2);
    CHECK(memcmp(b, "i\n", 2) == 0);
    CHECK_I(ybox_identify(YBOX_PHOTO, b, 32), 0);
    CHECK(ybox_ns_per_tick(YBOX_XID) == 1e6 && ybox_ns_per_tick(YBOX_LINE) == 1e3);
    CHECK(ybox_slow_write(YBOX_XID) && !ybox_slow_write(YBOX_LINE));
    CHECK(!ybox_init(NULL, YBOX_XID, 0, 0));
    {
        ybox_decoder d;
        CHECK(!ybox_init(&d, 0, 0, 0) && !ybox_init(&d, 4, 0, 0));
    }
    CHECK(strcmp(ybox_family_name(YBOX_LINE), "line") == 0);
    CHECK(strcmp(ybox_version(), YBOX_VERSION_STRING) == 0);
    printf("  queries and names: ok\n");
}

/* Random bytes: no crash, every call progresses while there is room, and
 * the counts add up (each frame gives one pair; events never outnumber
 * pairs). */
static void test_fuzz(void) {
    int family;
    for (family = YBOX_XID; family <= YBOX_PHOTO; family++) {
        size_t i;
        g_nbytes = 0;
        for (i = 0; i < 200000; i++) {
            uint8_t b = (uint8_t)rnd(256);
            if (family == YBOX_LINE && rnd(4) == 0) b = (uint8_t)" SEQAI0123456789\n"[rnd(17)];
            if (family == YBOX_XID && rnd(8) == 0) b = 'k';
            put(&b, 1);
        }
        decode_all(family, 2, 1 + (int)rnd(8));
        CHECK(g_nev <= g_npair);
        printf("  fuzz %s: 200000 bytes, %d events, %d pairs, %llu garbage\n", ybox_family_name(family), g_nev,
               g_npair, (unsigned long long)g_garbage);
    }
}

int main(void) {
    printf("box_test: ysp/box.h %s\n", ybox_version());
    test_family(YBOX_XID, gen_xid);
    test_family(YBOX_LINE, gen_line);
    test_family(YBOX_PHOTO, gen_photo);
    test_resync();
    test_line_rules();
    test_queries();
    test_fuzz();
    if (g_failures) {
        fprintf(stderr, "box_test: %d of %d checks FAILED\n", g_failures, g_checks);
        return 1;
    }
    printf("box_test: all %d checks passed\n", g_checks);
    return 0;
}
