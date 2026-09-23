/* psy_rt_test.c - self-checking test for psyrt_pump in psy_rt.h. No
 * framework: it returns 0 when every check passed and 1 after printing each
 * failure.
 *
 *     gcc -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Werror -pthread -I. \
 *         -o pump_test tests/adapt/psy_rt_test.c
 *     gcc ... -fsanitize=address,undefined ...
 *     gcc ... -fsanitize=thread ...
 *     cl /nologo /O2 /W4 /WX /I. tests\adapt\psy_rt_test.c
 *
 * The ThreadSanitizer run is the point of the publish-lock check: the pump
 * writes a struct while the main thread reads it, several thousand times, and
 * the only thing between them is psyrt_pump_lock(). A race there is a bug in
 * the header, not in the test. The same goes for the wait-versus-stop hammer:
 * two threads call psyrt_pump_wait() in a loop while the main thread stops and
 * restarts the pump 300 times, which is the guarantee v0.3.1 makes. A copy of
 * the header whose stop skips the waiter drain fails that check under TSan.
 * On Linux kernels with high mmap entropy run the TSan binary under
 * `setarch $(uname -m) -R`, or TSan aborts before main.
 *
 * It lives in tests/adapt/ rather than tests/compile/ because the pump exists
 * for the adaptive-method headers (docs/psy_adapt.md, "Inference on a thread")
 * and because it is a behavior test, not a compile check. It is deliberately
 * NOT registered in CMakeLists.txt yet.
 *
 * Timing: the test spends about 1.5 s in deliberate sleeps, which is how it
 * gets deterministic answers out of a thread it does not control. Every margin
 * is at least a factor of ten, so a sanitizer's slowdown does not reach them.
 */
#define PSY_RT_IMPLEMENTATION
#include "psy_rt.h"

#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------- harness */

static int g_failures = 0;

static void fail(int line, const char* what) {
    fprintf(stderr, "psy_rt_pump_test: FAIL at line %d: %s\n", line, what);
    g_failures++;
}

static void fail_i(int line, const char* what, long got, long want) {
    fprintf(stderr, "psy_rt_pump_test: FAIL at line %d: %s (got %ld, want %ld)\n",
            line, what, got, want);
    g_failures++;
}

#define CHECK(cond, what)     do { if (!(cond)) fail(__LINE__, (what)); } while (0)
#define CHECK_I(got, want, what) do { long g_ = (long)(got), w_ = (long)(want); \
        if (g_ != w_) fail_i(__LINE__, (what), g_, w_); } while (0)

/* A pure OS wait, no spin window: the test sleeps for hundreds of
 * milliseconds at a time and must not burn a core doing it. */
static void nap_ms(unsigned ms) {
    (void)psyrt_sleep_until(psyrt_now_ns() + (uint64_t)ms * 1000000ull, 0);
}

/* Submit, treating a full ring as back pressure rather than an error, which is
 * what a caller that has nothing else to do with the message does. */
static int submit_blocking(psyrt_pump* p, const void* m) {
    int i;
    for (i = 0; i < 20000; i++) {
        int rc = psyrt_pump_submit(p, m);
        if (rc != PSYRT_ERR_FULL) return rc;
        nap_ms(1);
    }
    return PSYRT_ERR_FULL;
}

/* ------------------------------------------------ order, seq, done_seq, wait */

#define ORDER_N 200

typedef struct order_msg {
    uint32_t tag;
    uint32_t pad[3];   /* a message wider than a word, so a short copy shows */
} order_msg;

typedef struct order_ctx {
    uint32_t seen_seq[ORDER_N];
    uint32_t seen_tag[ORDER_N];
    int      n;
    int      out_of_order;
    int      concurrent;   /* two on_msg calls overlapping: must never happen */
    int      in_msg;
    uint32_t last_seq;
} order_ctx;

static void order_on_msg(void* ctx, const void* msg, uint32_t seq) {
    order_ctx* o = (order_ctx*)ctx;
    const order_msg* m = (const order_msg*)msg;
    if (o->in_msg) o->concurrent++;
    o->in_msg = 1;
    if (seq != o->last_seq + 1u) o->out_of_order++;
    o->last_seq = seq;
    if (o->n < ORDER_N) {
        o->seen_seq[o->n] = seq;
        o->seen_tag[o->n] = m->tag;
        o->n++;
    }
    o->in_msg = 0;
}

/* One pass of the order test, so the inline ring and a caller-owned ring are
 * checked by the same code. `ring` NULL asks for the inline buffer. */
static void test_order(void* ring, const char* label) {
    psyrt_pump p;
    psyrt_pump_desc d;
    static order_ctx o;   /* static: 1.6 KB of arrays, and the pump thread
                           * writes it while main only reads it after the join */
    int i, rc = 0;
    memset(&o, 0, sizeof(o));
    memset(&p, 0, sizeof(p));
    memset(&d, 0, sizeof(d));
    d.msg_size = sizeof(order_msg);
    d.capacity = 8;
    d.ring     = ring;
    d.on_msg   = order_on_msg;
    d.ctx      = &o;
    if (!psyrt_pump_start(&p, &d)) {
        fail(__LINE__, psyrt_pump_error(&p));
        printf("  (%s)\n", label);
        return;
    }
    CHECK(psyrt_pump_is_running(&p), "pump should be running after start");
    CHECK_I(psyrt_pump_done_seq(&p), 0, "done_seq before any message");

    for (i = 0; i < ORDER_N; i++) {
        order_msg m;
        memset(&m, 0, sizeof(m));
        m.tag = (uint32_t)(i * 3 + 1);
        rc = submit_blocking(&p, &m);
        if (rc < 0) { fail(__LINE__, psyrt_strerror(rc)); break; }
        CHECK_I(rc, i + 1, "submit should return a seq of 1, 2, 3, ...");
    }
    /* wait and done_seq must agree about the last message. */
    CHECK_I(psyrt_pump_wait(&p, (uint32_t)rc, 5000000000ull), PSYRT_OK,
            "wait for the last seq");
    CHECK(psyrt_pump_done_seq(&p) >= (uint32_t)rc,
          "done_seq must have passed the seq wait returned for");
    CHECK_I(psyrt_pump_pending(&p), 0, "nothing should be pending after the wait");
    /* A seq that will never be reached must time out, not hang or lie. */
    CHECK_I(psyrt_pump_wait(&p, (uint32_t)rc + 1000u, 20000000ull),
            PSYRT_ERR_TIMEOUT, "wait for a seq never submitted");
    psyrt_pump_stop(&p);

    CHECK_I(o.n, ORDER_N, "every message must reach on_msg");
    CHECK_I(o.out_of_order, 0, "on_msg must see seq in submit order");
    CHECK_I(o.concurrent, 0, "on_msg must never run concurrently with itself");
    for (i = 0; i < o.n; i++) {
        if (o.seen_seq[i] != (uint32_t)(i + 1)) {
            fail_i(__LINE__, "seq at index", (long)o.seen_seq[i], (long)(i + 1));
            break;
        }
        if (o.seen_tag[i] != (uint32_t)(i * 3 + 1)) {
            fail_i(__LINE__, "payload at index", (long)o.seen_tag[i],
                   (long)(i * 3 + 1));
            break;
        }
    }
    printf("  order/seq/payload over %d messages, %s ring: ok\n", o.n, label);
}

/* --------------------------------------------------------------- ERR_FULL */

typedef struct slow_ctx {
    uint64_t hold_ns;    /* how long on_msg holds its slot                  */
    uint32_t hold_seq;   /* only this seq is held; 0 holds every one        */
    int      delivered;
} slow_ctx;

static void slow_on_msg(void* ctx, const void* msg, uint32_t seq) {
    slow_ctx* s = (slow_ctx*)ctx;
    (void)msg;
    s->delivered++;
    if (s->hold_seq == 0u || s->hold_seq == seq)
        (void)psyrt_sleep_until(psyrt_now_ns() + s->hold_ns, 0);
}

static void test_full(void) {
    psyrt_pump p;
    psyrt_pump_desc d;
    slow_ctx s;
    order_msg probe, probe_copy;
    int i, accepted = 0, rc = 0;
    memset(&s, 0, sizeof(s));
    s.hold_ns  = 400000000ull;  /* 400 ms: long enough that the whole fill
                                 * happens while the first slot is held */
    s.hold_seq = 1u;
    memset(&p, 0, sizeof(p));
    memset(&d, 0, sizeof(d));
    d.msg_size = sizeof(order_msg);
    d.capacity = 8;
    d.on_msg   = slow_on_msg;
    d.ctx      = &s;
    if (!psyrt_pump_start(&p, &d)) { fail(__LINE__, psyrt_pump_error(&p)); return; }

    memset(&probe, 0xa5, sizeof(probe));
    probe.tag = 0xdeadbeefu;
    probe_copy = probe;
    for (i = 0; i < 64; i++) {
        rc = psyrt_pump_submit(&p, &probe);
        if (rc == PSYRT_ERR_FULL) break;
        if (rc < 0) { fail(__LINE__, psyrt_strerror(rc)); break; }
        accepted++;
    }
    CHECK_I(rc, PSYRT_ERR_FULL, "a ring of 8 must refuse the ninth message");
    /* A message being handled still holds its slot, so a full ring is exactly
     * capacity messages accepted. */
    CHECK_I(accepted, 8, "accepted before the ring filled");
    CHECK(memcmp(&probe, &probe_copy, sizeof(probe)) == 0,
          "PSYRT_ERR_FULL must not touch the caller's message");
    CHECK(psyrt_pump_pending(&p) > 0, "a full ring must report pending messages");
    psyrt_pump_stop(&p);                 /* drains the eight */
    CHECK_I(s.delivered, 8, "a drain must deliver every accepted message");
    printf("  ERR_FULL at capacity, message intact, drain of 8: ok\n");
}

/* ---------------------------------------------------------------- on_idle */

typedef struct idle_ctx {
    psyrt_pump* p;
    int calls;
    int budget;
    int msgs;
} idle_ctx;

static bool idle_on_idle(void* ctx) {
    idle_ctx* k = (idle_ctx*)ctx;
    bool again;
    psyrt_pump_lock(k->p);
    k->calls++;
    again = (k->budget > 0);
    if (again) k->budget--;
    psyrt_pump_unlock(k->p);
    return again;
}

static void idle_on_msg(void* ctx, const void* msg, uint32_t seq) {
    idle_ctx* k = (idle_ctx*)ctx;
    (void)msg; (void)seq;
    psyrt_pump_lock(k->p);
    k->msgs++;
    k->budget = 2;   /* there is more spare work now: two more idle calls */
    psyrt_pump_unlock(k->p);
}

static int idle_read_calls(psyrt_pump* p, idle_ctx* k) {
    int n;
    psyrt_pump_lock(p);
    n = k->calls;
    psyrt_pump_unlock(p);
    return n;
}

static void test_idle(void) {
    psyrt_pump p;
    psyrt_pump_desc d;
    idle_ctx k;
    uint32_t dummy = 1u;
    int c1, c2, c3;
    int rc;
    memset(&k, 0, sizeof(k));
    k.p = &p;
    k.budget = 3;   /* three "yes" answers, then one "no" */
    memset(&p, 0, sizeof(p));
    memset(&d, 0, sizeof(d));
    d.msg_size = sizeof(dummy);
    d.capacity = 4;
    d.on_msg   = idle_on_msg;
    d.on_idle  = idle_on_idle;
    d.ctx      = &k;
    if (!psyrt_pump_start(&p, &d)) { fail(__LINE__, psyrt_pump_error(&p)); return; }

    nap_ms(60);
    c1 = idle_read_calls(&p, &k);
    nap_ms(60);
    c2 = idle_read_calls(&p, &k);
    CHECK_I(c1, 4, "on_idle runs until it returns false (3 yes, 1 no)");
    CHECK_I(c2, c1, "on_idle must stop being called once it returned false");

    rc = psyrt_pump_submit(&p, &dummy);
    CHECK(rc > 0, "submit while the pump is blocked on the idle condvar");
    CHECK_I(psyrt_pump_wait(&p, (uint32_t)rc, 2000000000ull), PSYRT_OK,
            "a submit must wake a pump that is blocked");
    nap_ms(60);
    c3 = idle_read_calls(&p, &k);
    CHECK_I(c3, 7, "on_idle resumes after a message (2 yes, 1 no more)");
    psyrt_pump_stop(&p);
    CHECK_I(k.msgs, 1, "the one message must have been delivered");
    printf("  on_idle: 4 calls idle, stops on false, 3 more after a submit: ok\n");
}

/* ------------------------------------------------------------ stop: drain */

static void test_stop(bool drop, int expect, const char* label) {
    psyrt_pump p;
    psyrt_pump_desc d;
    slow_ctx s;
    uint32_t v;
    int i, submitted = 0;
    memset(&s, 0, sizeof(s));
    /* Drop needs the pump to still be inside the first message when stop
     * arrives; drain only needs the queue to be non-empty. */
    s.hold_ns  = drop ? 600000000ull : 5000000ull;
    s.hold_seq = 0u;
    memset(&p, 0, sizeof(p));
    memset(&d, 0, sizeof(d));
    d.msg_size     = sizeof(v);
    d.capacity     = 32;
    d.on_msg       = slow_on_msg;
    d.ctx          = &s;
    d.drop_on_stop = drop;
    if (!psyrt_pump_start(&p, &d)) { fail(__LINE__, psyrt_pump_error(&p)); return; }
    for (i = 0; i < 20; i++) {
        v = (uint32_t)i;
        if (psyrt_pump_submit(&p, &v) > 0) submitted++;
    }
    CHECK_I(submitted, 20, "20 messages into a ring of 32");
    if (drop) {
        /* Wait until the pump has taken the first message and nothing more, so
         * the count after the stop is exactly "the one in flight". pending
         * counts what has NOT been handed over, so 19 is that state. */
        for (i = 0; i < 2000 && psyrt_pump_pending(&p) != 19; i++) nap_ms(1);
        CHECK_I(psyrt_pump_pending(&p), 19, "one in flight, nineteen queued");
    }
    psyrt_pump_stop(&p);
    CHECK_I(s.delivered, expect, label);
    CHECK_I(psyrt_pump_pending(&p), 0, "pending is 0 once the pump is stopped");
    printf("  stop %s: %d of %d messages delivered\n",
           drop ? "with drop_on_stop" : "draining", s.delivered, submitted);
}

/* ------------------------------------------------- the publish lock (TSan) */

#define CK_N 4000

typedef struct ck_pub {
    uint32_t a, b, sum, seq;
} ck_pub;

typedef struct ck_ctx {
    psyrt_pump* p;
    ck_pub      pub;
} ck_ctx;

static void ck_on_msg(void* ctx, const void* msg, uint32_t seq) {
    ck_ctx* k = (ck_ctx*)ctx;
    const uint32_t* v = (const uint32_t*)msg;
    uint32_t a = v[0], b = v[1];
    /* The "inference" runs with no lock held; only the publish takes it, which
     * is the discipline the manual asks of on_msg. */
    psyrt_pump_lock(k->p);
    k->pub.a   = a;
    k->pub.b   = b;
    k->pub.sum = a + b;
    k->pub.seq = seq;
    psyrt_pump_unlock(k->p);
}

static void test_publish_lock(void) {
    psyrt_pump p;
    psyrt_pump_desc d;
    static ck_ctx k;
    int i, reads = 0, torn = 0, seen = 0, rc = 0;
    memset(&k, 0, sizeof(k));
    k.p = &p;
    memset(&p, 0, sizeof(p));
    memset(&d, 0, sizeof(d));
    d.msg_size = 2 * sizeof(uint32_t);
    d.capacity = 16;
    d.on_msg   = ck_on_msg;
    d.ctx      = &k;
    if (!psyrt_pump_start(&p, &d)) { fail(__LINE__, psyrt_pump_error(&p)); return; }
    for (i = 0; i < CK_N; i++) {
        uint32_t v[2];
        ck_pub got;
        v[0] = (uint32_t)i;
        v[1] = (uint32_t)(i * 7 + 13);
        rc = submit_blocking(&p, v);
        if (rc < 0) { fail(__LINE__, psyrt_strerror(rc)); break; }
        psyrt_pump_lock(&p);
        got = k.pub;
        psyrt_pump_unlock(&p);
        reads++;
        if (got.seq != 0u) {
            seen++;
            if (got.sum != got.a + got.b) torn++;
            if (got.b != got.a * 7u + 13u) torn++;
        }
    }
    CHECK_I(psyrt_pump_wait(&p, (uint32_t)rc, 5000000000ull), PSYRT_OK,
            "wait for the last of the checksum messages");
    psyrt_pump_stop(&p);
    CHECK_I(reads, CK_N, "every read under the lock");
    CHECK_I(torn, 0, "the publish lock must never expose a half-written struct");
    CHECK(seen > CK_N / 10, "the main thread should see most publications");
    CHECK_I(k.pub.seq, (uint32_t)rc, "the last publication is the last message");
    printf("  publish lock: %d reads, %d saw a publication, %d torn\n",
           reads, seen, torn);
}

/* ------------------------------------------------- priority, on_start, edges */

static bool refuse_start(void* ctx, char* err, size_t cap) {
    (void)ctx;
    snprintf(err, cap, "test refuses to start");
    return false;
}

static bool count_start(void* ctx, char* err, size_t cap) {
    (void)err; (void)cap;
    *(int*)ctx += 1;
    return true;
}

static void no_op_msg(void* ctx, const void* msg, uint32_t seq) {
    (void)ctx; (void)msg; (void)seq;
}

static void test_priority_and_start(void) {
    psyrt_pump p;
    psyrt_pump_desc d;
    uint32_t v = 7u;
    int starts = 0;
    int rc;

    /* below_normal, plus a pin that may or may not be granted: neither is
     * allowed to fail the start. */
    memset(&p, 0, sizeof(p));
    memset(&d, 0, sizeof(d));
    d.msg_size     = sizeof(v);
    d.capacity     = 2;
    d.on_msg       = no_op_msg;
    d.below_normal = true;
    d.pin_cpu      = 1;
    d.on_start     = count_start;
    d.start_ctx    = &starts;
    if (!psyrt_pump_start(&p, &d)) {
        fail(__LINE__, psyrt_pump_error(&p));
    } else {
        psyrt_policy pol = psyrt_pump_policy(&p);
        CHECK(pol == PSYRT_POLICY_BELOW_NORMAL || pol == PSYRT_POLICY_NORMAL,
              "a pump reports NORMAL or BELOW_NORMAL and nothing else");
        CHECK_I(starts, 1, "on_start runs exactly once");
        printf("  below_normal start: policy=%s (a refusal is not an error)\n",
               psyrt_policy_name(pol));
        rc = psyrt_pump_submit(&p, &v);
        CHECK(rc > 0, "submit to a below-normal pump");
        CHECK_I(psyrt_pump_wait(&p, (uint32_t)rc, 2000000000ull), PSYRT_OK,
                "a below-normal pump still runs its messages");
        psyrt_pump_stop(&p);
        CHECK_I(psyrt_pump_policy(&p), PSYRT_POLICY_NONE,
                "policy is NONE after a stop");
    }

    /* A default start must be NORMAL, never anything higher: the pump does not
     * climb the ladder. */
    memset(&p, 0, sizeof(p));
    memset(&d, 0, sizeof(d));
    d.msg_size = sizeof(v);
    d.capacity = 2;
    d.on_msg   = no_op_msg;
    if (!psyrt_pump_start(&p, &d)) fail(__LINE__, psyrt_pump_error(&p));
    CHECK_I(psyrt_pump_policy(&p), PSYRT_POLICY_NORMAL,
            "a default pump runs at normal priority");
    psyrt_pump_stop(&p);

    /* A refused on_start fails the start and leaves the hook's message. */
    memset(&p, 0, sizeof(p));
    memset(&d, 0, sizeof(d));
    d.msg_size = sizeof(v);
    d.capacity = 2;
    d.on_msg   = no_op_msg;
    d.on_start = refuse_start;
    CHECK(!psyrt_pump_start(&p, &d), "a refused on_start must fail the start");
    CHECK(strstr(psyrt_pump_error(&p), "test refuses") != NULL,
          "the hook's own message must reach psyrt_pump_error");
    CHECK(!psyrt_pump_is_running(&p), "nothing runs after a refused start");
    CHECK_I(psyrt_pump_policy(&p), PSYRT_POLICY_NONE,
            "no policy after a refused start");
    CHECK_I(psyrt_pump_submit(&p, &v), PSYRT_ERR_STOPPED,
            "submit after a refused start");
    psyrt_pump_stop(&p);   /* must be safe */
    printf("  priority, on_start and its refusal: ok\n");
}

static void test_edges(void) {
    psyrt_pump p;
    psyrt_pump_desc d;
    uint32_t v = 1u;
    int rc;

    /* A zeroed handle is the documented starting state. */
    memset(&p, 0, sizeof(p));
    CHECK(!psyrt_pump_is_running(&p), "a zeroed handle is not running");
    CHECK_I(psyrt_pump_done_seq(&p), 0, "a zeroed handle has no done_seq");
    CHECK_I(psyrt_pump_pending(&p), 0, "a zeroed handle has nothing pending");
    CHECK_I(psyrt_pump_policy(&p), PSYRT_POLICY_NONE, "a zeroed handle has no policy");
    CHECK_I(psyrt_pump_submit(&p, &v), PSYRT_ERR_STOPPED, "submit to a zeroed handle");
    CHECK_I(psyrt_pump_wait(&p, 1u, 0), PSYRT_ERR_STOPPED, "wait on a zeroed handle");
    psyrt_pump_lock(&p);      /* a no-op, not a crash */
    psyrt_pump_unlock(&p);
    psyrt_pump_stop(&p);      /* a no-op */
    CHECK_I(psyrt_pump_submit(NULL, &v), PSYRT_ERR_ARG, "submit with a null handle");
    CHECK_I(psyrt_pump_wait(NULL, 1u, 0), PSYRT_ERR_ARG, "wait with a null handle");
    psyrt_pump_stop(NULL);
    psyrt_pump_lock(NULL);
    psyrt_pump_unlock(NULL);

    /* Descs that cannot work must fail at start, with a message. */
    CHECK(!psyrt_pump_start(&p, NULL), "a NULL desc has no on_msg");
    CHECK(psyrt_pump_error(&p)[0] != '\0', "a failed start leaves a message");
    memset(&d, 0, sizeof(d));
    d.msg_size = sizeof(v);
    d.capacity = 4;
    CHECK(!psyrt_pump_start(&p, &d), "on_msg is required");
    d.on_msg = no_op_msg;
    d.msg_size = 0;
    CHECK(!psyrt_pump_start(&p, &d), "msg_size is required");
    d.msg_size = sizeof(v);
    d.capacity = 0;
    CHECK(!psyrt_pump_start(&p, &d), "capacity is required");
    d.capacity = 4;
    d.msg_size = PSYRT_PUMP_INLINE_BYTES;   /* 4 x that does not fit inline */
    CHECK(!psyrt_pump_start(&p, &d), "a ring too big for the inline buffer");
    CHECK(strstr(psyrt_pump_error(&p), "PSYRT_PUMP_INLINE_BYTES") != NULL,
          "the message must name the macro that fixes it");

    /* Stop twice, and submit after a stop. */
    memset(&p, 0, sizeof(p));
    memset(&d, 0, sizeof(d));
    d.msg_size = sizeof(v);
    d.capacity = 4;
    d.on_msg   = no_op_msg;
    if (!psyrt_pump_start(&p, &d)) { fail(__LINE__, psyrt_pump_error(&p)); return; }
    rc = psyrt_pump_submit(&p, &v);
    CHECK(rc > 0, "one message before the stop");
    CHECK_I(psyrt_pump_wait(&p, (uint32_t)rc, 2000000000ull), PSYRT_OK, "wait");
    psyrt_pump_stop(&p);
    psyrt_pump_stop(&p);   /* the second one must be a no-op */
    CHECK(!psyrt_pump_is_running(&p), "not running after a stop");
    CHECK_I(psyrt_pump_submit(&p, &v), PSYRT_ERR_STOPPED, "submit after a stop");
    /* A seq the pump did reach is still a yes after the stop; one it never
     * reached is PSYRT_ERR_STOPPED, not a hang. */
    CHECK_I(psyrt_pump_wait(&p, (uint32_t)rc, 0), PSYRT_OK,
            "a seq already done is done after a stop");
    CHECK_I(psyrt_pump_wait(&p, (uint32_t)rc + 1u, 10000000ull), PSYRT_ERR_STOPPED,
            "a seq never reached, on a stopped pump");
    CHECK(psyrt_pump_done_seq(&p) == (uint32_t)rc,
          "done_seq survives the stop, for the caller's last check");
    psyrt_pump_lock(&p);   /* a no-op after the stop, per the manual */
    psyrt_pump_unlock(&p);

    CHECK(strcmp(psyrt_strerror(PSYRT_ERR_FULL), "unknown error") != 0,
          "psyrt_strerror names PSYRT_ERR_FULL");
    CHECK(strcmp(psyrt_strerror(PSYRT_ERR_TIMEOUT), "unknown error") != 0,
          "psyrt_strerror names PSYRT_ERR_TIMEOUT");
    printf("  zeroed handle, bad descs, double stop, submit after stop: ok\n");
}

/* --------------------------------------- a stop releases a blocked wait */

/* The stop has to come from another thread, and psy_rt.h already has one: a
 * deadline worker fires it while the main thread sits in psyrt_pump_wait() for
 * a seq that will never arrive. This is the one exception the declaration of
 * psyrt_pump_stop() allows to its "do not race another call" rule. */
static void stop_job(void* ctx, const psyrt_job_info* info) {
    (void)info;
    psyrt_pump_stop((psyrt_pump*)ctx);
}

static void test_stop_releases_wait(void) {
    psyrt_pump p;
    psyrt_pump_desc d;
    psyrt_worker w;
    psyrt_worker_desc wd;
    uint64_t t0, waited;
    int rc;
    memset(&p, 0, sizeof(p));
    memset(&d, 0, sizeof(d));
    d.msg_size = sizeof(uint32_t);
    d.capacity = 4;
    d.on_msg   = no_op_msg;
    if (!psyrt_pump_start(&p, &d)) { fail(__LINE__, psyrt_pump_error(&p)); return; }
    memset(&w, 0, sizeof(w));
    memset(&wd, 0, sizeof(wd));
    wd.no_elevate = true;   /* a test host is not a rig; do not ask for FIFO */
    if (!psyrt_worker_start(&w, &wd)) {
        fail(__LINE__, psyrt_worker_error(&w));
        psyrt_pump_stop(&p);
        return;
    }
    if (psyrt_worker_submit(&w, psyrt_now_ns() + 200000000ull, stop_job, &p) < 0)
        fail(__LINE__, "worker submit");
    t0 = psyrt_now_ns();
    rc = psyrt_pump_wait(&p, 99u, 10000000000ull);   /* 10 s it must not use */
    waited = psyrt_now_ns() - t0;
    psyrt_worker_stop(&w);
    CHECK_I(rc, PSYRT_ERR_STOPPED, "a stop must release a blocked wait");
    CHECK(waited < 3000000000ull, "the wait must return with the stop, not the timeout");
    CHECK(!psyrt_pump_is_running(&p), "the pump is stopped");
    psyrt_pump_stop(&p);   /* already stopped: a no-op */
    printf("  a stop releases a blocked wait after %.0f ms\n",
           (double)waited / 1e6);
}

/* ------------------------------------------ wait against stop, hammered */

/* The stop flag the main thread raises for the hammer threads. The test's own
 * atomics, so ThreadSanitizer judges the header and not a sloppy flag. */
#if defined(_MSC_VER)
static int t_load(const int* p) {
    return (int)InterlockedCompareExchange((volatile LONG*)(uintptr_t)p, 0, 0);
}
static void t_store(int* p, int v) { (void)InterlockedExchange((volatile LONG*)p, (LONG)v); }
#else
static int t_load(const int* p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
static void t_store(int* p, int v) { __atomic_store_n(p, v, __ATOMIC_RELEASE); }
#endif

#define HAMMER_CYCLES  300
#define HAMMER_THREADS 2

typedef struct hammer_ctx {
    psyrt_pump* p;
    const int*  quit;
    long calls, ok, timeout, stopped, other;
} hammer_ctx;

/* Runs on a deadline worker for the whole test: psy_rt.h's own thread is the
 * portable way to get a second one. Alternates a seq the pump reaches (1,
 * after a cycle's first message) with one it never will, with a timeout short
 * enough that most calls are in flight when a stop lands. */
static void hammer_job(void* ctx, const psyrt_job_info* info) {
    hammer_ctx* h = (hammer_ctx*)ctx;
    (void)info;
    while (!t_load(h->quit)) {
        uint32_t seq = (h->calls & 1) ? 1u : 0x7fff0000u;
        int rc = psyrt_pump_wait(h->p, seq, 300000ull);   /* 0.3 ms */
        h->calls++;
        if (rc == PSYRT_OK) h->ok++;
        else if (rc == PSYRT_ERR_TIMEOUT) h->timeout++;
        else if (rc == PSYRT_ERR_STOPPED) h->stopped++;
        else h->other++;
    }
}

static void test_hammer_wait_vs_stop(void) {
    static psyrt_pump p;
    psyrt_pump_desc d;
    psyrt_worker w[HAMMER_THREADS];
    psyrt_worker_desc wd;
    hammer_ctx h[HAMMER_THREADS];
    int quit = 0;
    int i, started = 0, restarts = 0;
    uint32_t v = 1u;
    long calls = 0, ok = 0, timeout = 0, stopped = 0, other = 0;
    uint64_t t0 = psyrt_now_ns();

    memset(&p, 0, sizeof(p));
    memset(&wd, 0, sizeof(wd));
    wd.no_elevate = true;
    for (i = 0; i < HAMMER_THREADS; i++) {
        memset(&h[i], 0, sizeof(h[i]));
        h[i].p = &p;
        h[i].quit = &quit;
        memset(&w[i], 0, sizeof(w[i]));
        if (!psyrt_worker_start(&w[i], &wd)) {
            fail(__LINE__, psyrt_worker_error(&w[i]));
            break;
        }
        started++;
        if (psyrt_worker_submit(&w[i], 0, hammer_job, &h[i]) < 0)
            fail(__LINE__, "hammer submit");
    }

    /* Stop and restart under the waiters, in every shape a stop can have:
     * idle, with a queue to drain, with a queue to drop, and at once. */
    for (i = 0; i < HAMMER_CYCLES; i++) {
        memset(&d, 0, sizeof(d));
        d.msg_size     = sizeof(v);
        d.capacity     = 4;
        d.on_msg       = no_op_msg;
        d.drop_on_stop = (i % 3 == 2);
        if (!psyrt_pump_start(&p, &d)) { fail(__LINE__, psyrt_pump_error(&p)); break; }
        restarts++;
        if (i % 2) {
            (void)psyrt_pump_submit(&p, &v);
            (void)psyrt_pump_submit(&p, &v);
        }
        if (i % 5 == 0) nap_ms(1);
        psyrt_pump_stop(&p);
    }

    t_store(&quit, 1);
    for (i = 0; i < started; i++) psyrt_worker_stop(&w[i]);   /* joins */
    for (i = 0; i < started; i++) {
        calls += h[i].calls; ok += h[i].ok; timeout += h[i].timeout;
        stopped += h[i].stopped; other += h[i].other;
    }
    CHECK_I(restarts, HAMMER_CYCLES, "every start in the hammer must succeed");
    CHECK_I(other, 0, "a racing wait returns only 0, TIMEOUT or STOPPED");
    CHECK(calls > HAMMER_CYCLES, "the waiters must have been busy throughout");
    CHECK(stopped > 0, "some waits must have met a stopped pump");
    CHECK(ok + timeout > 0, "some waits must have met a running pump");
    CHECK(!psyrt_pump_is_running(&p), "the pump ends stopped");
    printf("  wait vs stop/restart: %d cycles, %ld waits on %d threads "
           "(ok %ld, timeout %ld, stopped %ld), %.0f ms\n",
           restarts, calls, started, ok, timeout, stopped,
           (double)(psyrt_now_ns() - t0) / 1e6);
}

/* ---------------------------------------------------------------- version */

static void test_version(void) {
    char built[32];
    snprintf(built, sizeof(built), "%d.%d.%d", PSYRT_VERSION_MAJOR,
             PSYRT_VERSION_MINOR, PSYRT_VERSION_PATCH);
    CHECK(strcmp(built, PSYRT_VERSION_STRING) == 0,
          "PSYRT_VERSION_STRING must match the three numbers");
    CHECK(strcmp(psyrt_version(), PSYRT_VERSION_STRING) == 0,
          "psyrt_version() must return the header's string");
    printf("  version %s: ok\n", psyrt_version());
}

/* -------------------------------------------------------------------- main */

int main(void) {
    static unsigned char caller_ring[8 * sizeof(order_msg)];
    uint64_t t0 = psyrt_now_ns();

    printf("psy_rt_pump_test: psyrt_pump, %d-byte handle\n",
           (int)sizeof(psyrt_pump));

    test_order(NULL, "inline");
    test_order(caller_ring, "caller");
    test_full();
    test_idle();
    test_stop(false, 20, "a drain must deliver every message");
    test_stop(true, 1, "drop_on_stop must keep only the message in flight");
    test_publish_lock();
    test_priority_and_start();
    test_stop_releases_wait();
    test_hammer_wait_vs_stop();
    test_edges();
    test_version();

    printf("psy_rt_pump_test: %s (%d failures, %.1f s)\n",
           g_failures ? "FAILED" : "all checks passed", g_failures,
           (double)(psyrt_now_ns() - t0) / 1e9);
    return g_failures ? 1 : 0;
}
