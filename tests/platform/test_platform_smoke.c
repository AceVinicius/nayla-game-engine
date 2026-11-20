/* Smoke test for the selected platform backend. It proves the backend is
 * compiled, linked and exercised, and checks the stateless and NULL-argument
 * rules that every backend shares. The full conformance suite is a separate
 * task. */

#include <stdint.h>
#include <stdio.h>

#include "src/platform/api.h"
#include "src/platform/native.h"
#include "src/platform/common/platform_common.h"
#include "tests/check.h"

/* PLATFORM-020: non-decreasing, never UINT64_MAX, across many calls. */
static int test_time_non_decreasing(void) {
    uint64_t prev = platform_time_now_ns();
    CHECK(prev != UINT64_MAX, "time returned UINT64_MAX");
    for (int i = 0; i < 100000; i++) {
        uint64_t now = platform_time_now_ns();
        CHECK(now != UINT64_MAX, "time returned UINT64_MAX");
        CHECK(now >= prev, "time went backwards: %llu after %llu",
              (unsigned long long)now, (unsigned long long)prev);
        prev = now;
    }
    return 0;
}

/* PLATFORM-023: non-zero and stable on one thread. */
static int test_thread_id_non_zero_and_stable(void) {
    uint64_t first = platform_thread_current_id();
    CHECK(first != 0, "thread id is 0");
    for (int i = 0; i < 1000; i++) {
        CHECK(platform_thread_current_id() == first, "thread id changed on one thread");
    }
    return 0;
}

/* PLATFORM-025: each adapter ignores user_data and returns its function's value. */
static int test_adapters_match_functions(void) {
    int unused = 0;
    uint64_t t0 = platform_time_now_ns();
    uint64_t via = platform_log_time_source(&unused);
    uint64_t t1 = platform_time_now_ns();
    CHECK(via != UINT64_MAX && via >= t0 && via <= t1, "time adapter outside the bracketing reads");
    CHECK(platform_log_thread_id_source(NULL) == platform_thread_current_id(),
          "thread id adapter differs from platform_thread_current_id");
    return 0;
}

/* PLATFORM-022: no overflow. 1e17 ticks at 1e7 Hz is 1e10 s, and the naive
 * ticks * 1e9 would overflow uint64_t. */
static int test_tick_conversion_no_overflow(void) {
    CHECK(platform_ticks_to_ns(UINT64_C(3) * 10000000u + 5u, 10000000u) == UINT64_C(3000000500),
          "3 s plus 5 ticks converted wrongly");
    CHECK(platform_ticks_to_ns(UINT64_C(100000000000000000), 10000000u) == UINT64_C(10000000000000000000),
          "large tick count overflowed");
    return 0;
}

/* PLATFORM-033 and PLAT-INV-9: argument order and NULL substitution. */
static int test_platform_init_rules(void) {
    EXPECT(platform_init(NULL, NULL, NULL), ENGINE_RESULT_INVALID_ARGUMENT);

    /* A resolved allocator with a NULL callback is invalid. */
    EngineAllocator broken = {NULL, NULL, NULL};
    char sentinel = 0;
    Platform *p = (Platform *)&sentinel;
    EXPECT(platform_init(&p, &broken, NULL), ENGINE_RESULT_INVALID_ARGUMENT);
    CHECK(p == NULL, "platform_init left *out_platform non-NULL on INVALID_ARGUMENT");

    /* NULL allocator resolves to the default and the probe reports NOT_SUPPORTED. */
    p = (Platform *)&sentinel;
    EXPECT(platform_init(&p, NULL, NULL), ENGINE_RESULT_NOT_SUPPORTED);
    CHECK(p == NULL, "platform_init left *out_platform non-NULL on NOT_SUPPORTED");
    return 0;
}

/* PLATFORM-041: validation runs before NOT_SUPPORTED. */
static int test_window_create_rules(void) {
    PlatformWindowDesc desc = {"smoke", 640, 480, 0};
    char sentinel = 0;
    Platform *platform = (Platform *)&sentinel;     /* never dereferenced by the stub */
    PlatformWindow *const poison = (PlatformWindow *)&sentinel;
    PlatformWindow *w = poison; /* reset before every call so "left non-NULL" can fail */

    EXPECT(platform_window_create(NULL, platform, NULL, &desc), ENGINE_RESULT_INVALID_ARGUMENT);

    PlatformWindowDesc no_title = {NULL, 640, 480, 0};
    PlatformWindowDesc zero_width = {"smoke", 0, 480, 0};
    PlatformWindowDesc zero_height = {"smoke", 640, 0, 0};
    PlatformWindowDesc unknown_flag = {"smoke", 640, 480, 0x80000000u};
    struct { Platform *platform; const PlatformWindowDesc *desc; } invalid[] = {
        {NULL, &desc}, {platform, NULL}, {platform, &no_title},
        {platform, &zero_width}, {platform, &zero_height}, {platform, &unknown_flag},
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        w = poison;
        EXPECT(platform_window_create(&w, invalid[i].platform, NULL, invalid[i].desc),
               ENGINE_RESULT_INVALID_ARGUMENT);
        CHECK(w == NULL, "invalid case %zu left *out_window non-NULL", i);
    }

    EngineAllocator no_free = *engine_default_allocator();
    no_free.free = NULL;
    w = poison;
    EXPECT(platform_window_create(&w, platform, &no_free, &desc), ENGINE_RESULT_INVALID_ARGUMENT);
    CHECK(w == NULL, "bad allocator left *out_window non-NULL");

    w = poison;
    EXPECT(platform_window_create(&w, platform, NULL, &desc), ENGINE_RESULT_NOT_SUPPORTED);
    CHECK(w == NULL, "platform_window_create left *out_window non-NULL");
    return 0;
}

/* PLAT-INV-13 and the NULL rules for every other entry point. */
static int test_null_tolerance(void) {
    platform_shutdown(NULL);
    platform_window_destroy(NULL);
    platform_pump_events(NULL);
    CHECK(!platform_window_should_close(NULL), "should_close(NULL) returned true");

    uint32_t width = 7;
    uint32_t height = 9;
    EXPECT(platform_window_get_framebuffer_size(NULL, &width, &height), ENGINE_RESULT_INVALID_ARGUMENT);
    CHECK(width == 7 && height == 9, "framebuffer size written on INVALID_ARGUMENT");

    PlatformNativeWindow native = {77, NULL, NULL, 5};
    EXPECT(platform_window_get_native(NULL, &native), ENGINE_RESULT_INVALID_ARGUMENT);
    CHECK(native.type == 77 && native.window_id == 5, "native handle written on INVALID_ARGUMENT");

    PlatformEvent event = {77, NULL, 1, 2};
    CHECK(!platform_next_event(NULL, &event), "next_event(NULL) returned true");
    CHECK(!platform_next_event(NULL, NULL), "next_event(NULL, NULL) returned true");
    CHECK(event.type == 77 && event.width == 1 && event.height == 2,
          "next_event wrote to the event on an empty queue");
    return 0;
}

static const TestCase tests[] = {
    {"unit/platform_smoke: time is non-decreasing and never UINT64_MAX", test_time_non_decreasing},
    {"unit/platform_smoke: thread id is non-zero and stable", test_thread_id_non_zero_and_stable},
    {"unit/platform_smoke: adapters match the functions", test_adapters_match_functions},
    {"unit/platform_smoke: tick conversion does not overflow", test_tick_conversion_no_overflow},
    {"unit/platform_smoke: platform_init argument rules", test_platform_init_rules},
    {"unit/platform_smoke: platform_window_create validation order", test_window_create_rules},
    {"unit/platform_smoke: NULL tolerance", test_null_tolerance},
};

int main(void) {
    return RUN_TESTS(tests);
}
