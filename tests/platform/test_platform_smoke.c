/* Smoke test for the selected platform backend. It proves the backend is
 * compiled, linked and exercised, and checks the stateless and NULL-argument
 * rules that every backend shares. The full conformance suite is a separate
 * task. */

#if defined(__linux__)
#define _GNU_SOURCE /* setenv/unsetenv under strict C23 */
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    /* NULL allocator resolves to the default. The window-system probe decides the
     * result: SUCCESS where a display is reachable (macOS), NOT_SUPPORTED where
     * none is (Linux and Win32 stubs, headless hosts). */
    p = (Platform *)&sentinel;
    EngineResult result = platform_init(&p, NULL, NULL);
    CHECK(result == ENGINE_RESULT_SUCCESS || result == ENGINE_RESULT_NOT_SUPPORTED,
          "platform_init(NULL allocator) returned %s", engine_result_name(result));
    if (result == ENGINE_RESULT_SUCCESS) {
        CHECK(p != NULL, "platform_init succeeded but left *out_platform NULL");
        platform_shutdown(p);
    } else {
        CHECK(p == NULL, "platform_init left *out_platform non-NULL on failure");
        fprintf(stderr, "skip: no window system reachable; platform_init returned NOT_SUPPORTED\n");
    }
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

    /* A window needs a real Platform, which the fake pointer above is not. Where
     * no window system is reachable, init fails first and there is nothing to test. */
    Platform *real = NULL;
    EngineResult init = platform_init(&real, NULL, NULL);
    CHECK(init == ENGINE_RESULT_SUCCESS || init == ENGINE_RESULT_NOT_SUPPORTED,
          "platform_init returned %s", engine_result_name(init));
    if (init == ENGINE_RESULT_SUCCESS) {
        w = poison;
        EXPECT(platform_window_create(&w, real, NULL, &desc), ENGINE_RESULT_SUCCESS);
        CHECK(w != NULL, "platform_window_create succeeded but left *out_window NULL");
        platform_window_destroy(w);
        platform_shutdown(real);
    } else {
        fprintf(stderr, "skip: no window system reachable; window create checked only for validation\n");
#if !defined(__APPLE__)
        /* The stub never dereferences the Platform, so the sentinel is safe here.
         * The macOS backend dereferences it after creating an NSWindow, so the
         * NOT_SUPPORTED check is not run with the sentinel on macOS. */
        w = poison;
        EXPECT(platform_window_create(&w, platform, NULL, &desc), ENGINE_RESULT_NOT_SUPPORTED);
        CHECK(w == NULL, "platform_window_create left *out_window non-NULL");
#endif
    }
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

/* PLATFORM-041 (ADR-012 Decision 3): every invalid UTF-8 form becomes one U+FFFD. */
static int test_title_sanitize(void) {
    static const struct { const char *in; const char *out; } rows[] = {
        {"plain", "plain"},
        {"\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80", "\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80"},   /* valid 2, 3, 4 bytes */
        {"a\x80z", "a\xEF\xBF\xBDz"},                  /* stray continuation */
        {"\xC0\xAF", "\xEF\xBF\xBD\xEF\xBF\xBD"},        /* overlong */
        {"\xED\xA0\x80", "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD"}, /* surrogate */
        {"\xF4\x90\x80\x80", "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD"}, /* > U+10FFFF */
        {"ab\xE2\x82", "ab\xEF\xBF\xBD\xEF\xBF\xBD"},    /* truncated at the NUL */
        {"", ""},
    };
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        char *got = platform_title_sanitize(engine_default_allocator(), rows[i].in);
        CHECK(got != NULL, "row %zu: sanitize returned NULL", i);
        CHECK(strcmp(got, rows[i].out) == 0, "row %zu: wrong output", i);
        engine_free(engine_default_allocator(), got);
    }
    return 0;
}

/* PLATFORM-052/053: coalescing, the capacity bound, purge and FIFO order. */
static int test_event_queue(void) {
    PlatformEventQueue queue;
    CHECK_OK(platform_queue_init(&queue, engine_default_allocator()));
    PlatformWindow *a = (PlatformWindow *)(uintptr_t)0x10;   /* identity only, never dereferenced */
    PlatformWindow *b = (PlatformWindow *)(uintptr_t)0x20;
    platform_queue_push(&queue, NULL, a, PLATFORM_EVENT_WINDOW_FRAMEBUFFER_RESIZED, 1, 1);
    platform_queue_push(&queue, NULL, b, PLATFORM_EVENT_WINDOW_FRAMEBUFFER_RESIZED, 2, 2);
    platform_queue_push(&queue, NULL, a, PLATFORM_EVENT_WINDOW_FRAMEBUFFER_RESIZED, 3, 3);
    CHECK(queue.count == 2, "a second resize for one window was not coalesced");
    platform_queue_push(&queue, NULL, a, PLATFORM_EVENT_WINDOW_CLOSE_REQUESTED, 0, 0);
    platform_queue_purge_window(&queue, b);
    CHECK(queue.count == 2, "purge removed the wrong events");

    PlatformEvent event = {0};
    CHECK(platform_queue_pop(&queue, &event) && event.window == a && event.width == 3, "first event is not a's updated resize");
    CHECK(platform_queue_pop(&queue, &event) && event.type == PLATFORM_EVENT_WINDOW_CLOSE_REQUESTED, "second event is not the close");
    CHECK(!platform_queue_pop(&queue, &event), "pop returned true on an empty queue");

    for (uint32_t i = 0; i < PLATFORM_EVENT_QUEUE_CAPACITY + 5; i++) {
        platform_queue_push(&queue, NULL, a, PLATFORM_EVENT_WINDOW_CLOSE_REQUESTED, 0, 0);
    }
    CHECK(queue.count == PLATFORM_EVENT_QUEUE_CAPACITY, "queue exceeded its capacity");
    platform_queue_destroy(&queue, engine_default_allocator());
    return 0;
}

#if defined(__linux__)
static void *failing_alloc(const EngineAllocator *allocator, size_t size, size_t alignment) {
    (void)size;
    (void)alignment;
    ++*(int *)allocator->user_data;   /* proves whether the allocator was reached */
    return NULL;
}

static void noop_free(const EngineAllocator *allocator, void *memory) {
    (void)allocator;
    (void)memory;
}

/* PLATFORM-033: validate, then probe without allocating, then allocate. With
 * no X server reachable the result is NOT_SUPPORTED even for an always-NULL
 * allocator, and the allocator is never called. Linux only: the other
 * backends cannot be made to lack a display from inside a test. */
static int test_init_probes_before_allocating(void) {
    const char *saved_display = getenv("DISPLAY");
    char *display_copy = saved_display ? strdup(saved_display) : NULL;
    const char *saved_system = getenv("NAYLA_LINUX_WINDOW_SYSTEM");
    char *system_copy = saved_system ? strdup(saved_system) : NULL;
    unsetenv("DISPLAY");
    CHECK(setenv("NAYLA_LINUX_WINDOW_SYSTEM", "x11", 1) == 0, "setenv failed");

    int calls = 0;
    EngineAllocator failing = {failing_alloc, noop_free, &calls};
    char sentinel = 0;
    Platform *p = (Platform *)&sentinel;   /* non-NULL, so the NULL check below is not vacuous */
    EngineResult result = platform_init(&p, &failing, NULL);

    if (display_copy) { setenv("DISPLAY", display_copy, 1); }
    if (system_copy) { setenv("NAYLA_LINUX_WINDOW_SYSTEM", system_copy, 1); } else { unsetenv("NAYLA_LINUX_WINDOW_SYSTEM"); }
    free(display_copy);
    free(system_copy);

    CHECK(result == ENGINE_RESULT_NOT_SUPPORTED, "platform_init returned %s, expected NOT_SUPPORTED", engine_result_name(result));
    CHECK(p == NULL, "platform_init left *out_platform non-NULL");
    CHECK(calls == 0, "the allocator was called %d times before the probe failed", calls);
    return 0;
}
#endif

static const TestCase tests[] = {
    {"unit/platform_smoke: time is non-decreasing and never UINT64_MAX", test_time_non_decreasing},
    {"unit/platform_smoke: thread id is non-zero and stable", test_thread_id_non_zero_and_stable},
    {"unit/platform_smoke: adapters match the functions", test_adapters_match_functions},
    {"unit/platform_smoke: tick conversion does not overflow", test_tick_conversion_no_overflow},
    {"unit/platform_smoke: platform_init argument rules", test_platform_init_rules},
    {"unit/platform_smoke: platform_window_create validation order", test_window_create_rules},
    {"unit/platform_smoke: NULL tolerance", test_null_tolerance},
    {"unit/platform_smoke: title sanitising replaces invalid UTF-8", test_title_sanitize},
    {"unit/platform_smoke: event queue coalesces, bounds and purges", test_event_queue},
#if defined(__linux__)
    {"unit/platform_smoke: platform_init probes before allocating", test_init_probes_before_allocating},
#endif
};

int main(void) {
    return RUN_TESTS(tests);
}
