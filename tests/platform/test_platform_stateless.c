/* Stateless platform conformance: PLAT-INV-6, -7, -8, -13 (PLATFORM-100..102).
 *
 * Written only against api.h and logging.h, and linked against engine_platform
 * so it runs the selected backend unchanged. It never creates a Platform, so
 * every check holds before platform_init, and it never skips (PLATFORM-101).
 * Assertions never branch on the backend; the only #if is in the thread helper
 * (PLATFORM-102). */

#include <stdatomic.h>
#include <stdint.h>

#include "src/platform/api.h"
#include "src/core/logging.h"
#include "tests/check.h"

#define MONOTONIC_CALLS 1000000u
/* Upper bound for the sleep-free busy loop (PLAT-INV-6). A frozen clock fails
 * here rather than hanging; a real monotonic clock advances long before it. */
#define BUSY_LOOP_LIMIT 100000000u
/* Upper bound for each cross-thread wait. A thread that never reaches its
 * handshake fails the check instead of hanging the suite. */
#define HANDSHAKE_LIMIT 1000000000u

/* ---- Test-only thread helper (PLATFORM-102) ------------------------------- */

typedef void (*TestThreadBody)(void *arg);

#if defined(_WIN32)
#include <windows.h>

typedef struct TestThread {
    HANDLE         handle;
    TestThreadBody body;
    void          *arg;
} TestThread;

static DWORD WINAPI test_thread_entry(LPVOID param) {
    TestThread *t = param;
    t->body(t->arg);
    return 0;
}

static int test_thread_start(TestThread *t, TestThreadBody body, void *arg) {
    t->body = body;
    t->arg = arg;
    t->handle = CreateThread(NULL, 0, test_thread_entry, t, 0, NULL);
    return t->handle ? 0 : -1;
}

static void test_thread_join(TestThread *t) {
    WaitForSingleObject(t->handle, INFINITE);
    CloseHandle(t->handle);
}
#else
#include <pthread.h>

typedef struct TestThread {
    pthread_t      handle;
    TestThreadBody body;
    void          *arg;
} TestThread;

static void *test_thread_entry(void *param) {
    TestThread *t = param;
    t->body(t->arg);
    return NULL;
}

static int test_thread_start(TestThread *t, TestThreadBody body, void *arg) {
    t->body = body;
    t->arg = arg;
    return pthread_create(&t->handle, NULL, test_thread_entry, t) == 0 ? 0 : -1;
}

static void test_thread_join(TestThread *t) {
    pthread_join(t->handle, NULL);
}
#endif

/* ---- PLAT-INV-6: time ----------------------------------------------------- */

/* 10^6 consecutive calls: non-decreasing and never ENGINE_LOG_TIMESTAMP_UNKNOWN. */
static int test_time_monotonic_single_thread(void) {
    uint64_t previous = platform_time_now_ns();
    CHECK(previous != ENGINE_LOG_TIMESTAMP_UNKNOWN, "first call returned UINT64_MAX");
    for (uint32_t i = 1; i < MONOTONIC_CALLS; i++) {
        uint64_t now = platform_time_now_ns();
        CHECK(now != ENGINE_LOG_TIMESTAMP_UNKNOWN, "call %u returned UINT64_MAX", i);
        CHECK(now >= previous, "call %u went backwards: %llu after %llu", i,
              (unsigned long long)now, (unsigned long long)previous);
        previous = now;
    }
    return 0;
}

/* Sleep-free busy loop: some call must observe a later value than the first. */
static int test_time_busy_loop_observes_increase(void) {
    uint64_t start = platform_time_now_ns();
    CHECK(start != ENGINE_LOG_TIMESTAMP_UNKNOWN, "first call returned UINT64_MAX");
    for (uint32_t i = 0; i < BUSY_LOOP_LIMIT; i++) {
        uint64_t now = platform_time_now_ns();
        CHECK(now >= start, "clock went backwards during busy loop");
        if (now > start) {
            return 0;
        }
    }
    CHECK(0, "clock did not advance within %u calls (frozen clock)", BUSY_LOOP_LIMIT);
}

/* Happens-before edge: read in A, thread B started and joined, read in A again.
 * Thread creation and join order B's read between A's two reads. */
typedef struct {
    uint64_t sampled;
} TimeSample;

static void sample_time_body(void *arg) {
    TimeSample *sample = arg;
    sample->sampled = platform_time_now_ns();
}

static int test_time_happens_before_across_threads(void) {
    uint64_t before = platform_time_now_ns();
    TimeSample sample = {0};
    TestThread thread;
    CHECK(test_thread_start(&thread, sample_time_body, &sample) == 0, "could not start thread");
    test_thread_join(&thread);
    uint64_t after = platform_time_now_ns();

    CHECK(sample.sampled != ENGINE_LOG_TIMESTAMP_UNKNOWN, "thread read returned UINT64_MAX");
    CHECK(before <= sample.sampled, "thread read %llu precedes earlier read %llu",
          (unsigned long long)sample.sampled, (unsigned long long)before);
    CHECK(sample.sampled <= after, "later read %llu precedes thread read %llu",
          (unsigned long long)after, (unsigned long long)sample.sampled);
    return 0;
}

/* ---- PLAT-INV-7: thread identity ----------------------------------------- */

/* Two concurrently live threads. B publishes its id, signals ready, and stays
 * alive until released, so A's comparison happens while both exist. */
typedef struct {
    atomic_int  ready;
    atomic_int  release;
    uint64_t    first_id;
    uint64_t    second_id;
} Rendezvous;

static int wait_for_flag(atomic_int *flag) {
    for (uint32_t i = 0; i < HANDSHAKE_LIMIT; i++) {
        if (atomic_load(flag)) {
            return 1;
        }
    }
    return 0;
}

static void rendezvous_body(void *arg) {
    Rendezvous *r = arg;
    r->first_id = platform_thread_current_id();
    r->second_id = platform_thread_current_id();
    atomic_store(&r->ready, 1);
    wait_for_flag(&r->release);
}

static int test_thread_id_nonzero_and_stable(void) {
    uint64_t main_first = platform_thread_current_id();
    uint64_t main_second = platform_thread_current_id();
    CHECK(main_first != ENGINE_LOG_THREAD_UNKNOWN, "calling thread id is 0");
    CHECK(main_first == main_second, "id changed between calls on one thread");

    Rendezvous r = {0};
    TestThread thread;
    CHECK(test_thread_start(&thread, rendezvous_body, &r) == 0, "could not start thread");
    int ready = wait_for_flag(&r.ready);
    atomic_store(&r.release, 1);
    test_thread_join(&thread);   /* join before any check: r lives on this frame */

    CHECK(ready, "helper thread never reached its handshake");
    CHECK(r.first_id != ENGINE_LOG_THREAD_UNKNOWN, "helper thread id is 0");
    CHECK(r.first_id == r.second_id, "id changed between calls on the helper thread");
    return 0;
}

static int test_thread_id_distinct_between_live_threads(void) {
    uint64_t main_id = platform_thread_current_id();
    Rendezvous r = {0};
    TestThread thread;
    CHECK(test_thread_start(&thread, rendezvous_body, &r) == 0, "could not start thread");
    int ready = wait_for_flag(&r.ready);
    /* first_id is published before ready is set, so it is read only once ready is observed. */
    uint64_t helper_id = ready ? r.first_id : ENGINE_LOG_THREAD_UNKNOWN;
    atomic_store(&r.release, 1);
    test_thread_join(&thread);   /* join before any check: r lives on this frame */

    CHECK(ready, "helper thread never reached its handshake");
    CHECK(helper_id != ENGINE_LOG_THREAD_UNKNOWN, "helper thread id is 0");
    CHECK(helper_id != main_id, "two concurrently live threads share id %llu",
          (unsigned long long)main_id);
    return 0;
}

/* ---- PLAT-INV-8: logger adapters without a Platform ----------------------- */

typedef struct {
    uint64_t timestamp_ns;
    uint64_t thread_id;
    size_t   count;
} Capture;

static void capture_write(const EngineLogSink *sink, const EngineLogRecord *record) {
    Capture *cap = sink->user_data;
    cap->count++;
    cap->timestamp_ns = record->timestamp_ns;
    cap->thread_id = record->thread_id;
}

static int test_log_adapters_record_real_values_without_platform(void) {
    /* Junk user_data: the adapters must ignore it (PLATFORM-025). */
    static int junk;
    EngineLogger logger;
    Capture cap = {0};
    EngineLogSink sink = {.write = capture_write, .user_data = &cap, .min_level = ENGINE_LOG_LEVEL_TRACE};

    CHECK_OK(engine_log_init(&logger, ENGINE_LOG_LEVEL_TRACE));
    CHECK_OK(engine_log_add_sink(&logger, &sink));
    CHECK_OK(engine_log_set_context_source(&logger, platform_log_time_source,
                                           platform_log_thread_id_source, &junk));

    uint64_t before = platform_time_now_ns();
    engine_log_write(&logger, ENGINE_LOG_LEVEL_INFO, "Platform", NULL, 0, "stateless probe");
    uint64_t after = platform_time_now_ns();

    CHECK(cap.count == 1, "expected one delivered record, got %zu", cap.count);
    CHECK(cap.timestamp_ns != ENGINE_LOG_TIMESTAMP_UNKNOWN, "record timestamp is UNKNOWN");
    CHECK(before <= cap.timestamp_ns && cap.timestamp_ns <= after,
          "record timestamp %llu outside [%llu, %llu]", (unsigned long long)cap.timestamp_ns,
          (unsigned long long)before, (unsigned long long)after);
    CHECK(cap.thread_id != ENGINE_LOG_THREAD_UNKNOWN, "record thread id is UNKNOWN");
    CHECK(cap.thread_id == platform_thread_current_id(), "record thread id is not the calling thread");

    engine_log_shutdown(&logger);
    return 0;
}

static int test_log_adapters_ignore_user_data(void) {
    static int junk;
    void *junk_ptr = &junk;
    uint64_t before = platform_time_now_ns();
    uint64_t time_value = platform_log_time_source(junk_ptr);
    uint64_t thread_value = platform_log_thread_id_source(junk_ptr);
    uint64_t after = platform_time_now_ns();

    CHECK(time_value != ENGINE_LOG_TIMESTAMP_UNKNOWN, "time adapter returned UNKNOWN");
    CHECK(before <= time_value && time_value <= after,
          "time adapter %llu outside [%llu, %llu]", (unsigned long long)time_value,
          (unsigned long long)before, (unsigned long long)after);
    CHECK(thread_value != ENGINE_LOG_THREAD_UNKNOWN, "thread adapter returned UNKNOWN");
    CHECK(platform_log_time_source(NULL) != ENGINE_LOG_TIMESTAMP_UNKNOWN,
          "time adapter with NULL user_data returned UNKNOWN");
    CHECK(platform_log_thread_id_source(NULL) == thread_value,
          "thread adapter result depends on user_data");
    return 0;
}

/* ---- PLAT-INV-13: NULL tolerance ----------------------------------------- */

static int test_null_tolerance(void) {
    platform_shutdown(NULL);
    platform_window_destroy(NULL);
    platform_pump_events(NULL);
    CHECK(platform_window_should_close(NULL) == false, "should_close(NULL) is not false");
    return 0;
}

int main(void) {
    static const TestCase tests[] = {
        {"time: 10^6 calls non-decreasing, never UINT64_MAX", test_time_monotonic_single_thread},
        {"time: busy loop observes an increase", test_time_busy_loop_observes_increase},
        {"time: non-decreasing across a thread happens-before edge", test_time_happens_before_across_threads},
        {"thread id: non-zero and stable on one thread", test_thread_id_nonzero_and_stable},
        {"thread id: distinct between live threads", test_thread_id_distinct_between_live_threads},
        {"log adapters: real values with no Platform", test_log_adapters_record_real_values_without_platform},
        {"log adapters: ignore user_data", test_log_adapters_ignore_user_data},
        {"NULL tolerance", test_null_tolerance},
    };
    return RUN_TESTS(tests);
}
