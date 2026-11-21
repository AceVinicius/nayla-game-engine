/* Window conformance: PLAT-INV-9, -10, -11, -12 (PLATFORM-100..102).
 *
 * Written only against api.h, native.h, logging.h and tests/check.h, and
 * linked against engine_platform so it runs the selected backend unchanged.
 * Assertions never branch on the backend (PLATFORM-102). The only #if is the
 * stdout/stderr capture helper for the NULL-logger check; that is harness code.
 *
 * Skip (PLATFORM-101): the test exits with SKIP_RETURN_CODE (77) only when
 * platform_init returns NOT_SUPPORTED AND NAYLA_TEST_NO_DISPLAY is "1". Every
 * other NOT_SUPPORTED is a failure. The checks that need no Platform run first
 * and never skip.
 *
 * Every Platform-based test runs through with_platform() and every window
 * through with_window(), so the Platform and window are shut down or destroyed
 * even when a check fails. A failed check therefore never leaves a second
 * Platform live (PLATFORM-036). */

#if !defined(_WIN32)
/* dup, dup2, close, fileno are POSIX; the project builds with -std=c23 and
 * extensions off, which hides them unless this is defined before any include. */
#define _POSIX_C_SOURCE 200809L
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/platform/api.h"
#include "src/platform/native.h"
#include "src/core/logging.h"
#include "tests/check.h"

#define SKIP_RETURN_CODE_NO_DISPLAY 77
#define NO_DISPLAY_ENV "NAYLA_TEST_NO_DISPLAY"
#define POISON_BYTE 0xA5
/* Upper bound for drains and pumps, so a queue that never empties fails
 * instead of hanging the suite. */
#define DRAIN_LIMIT 1024u
#define TRACK_CAPACITY 16

/* Out-parameter poison (PLAT-INV-10): a pointer the API must overwrite with
 * NULL. It is never dereferenced. */
static char poison_storage;
#define POISON_PTR(T) ((T *)(void *)&poison_storage)

static const char window_title[] = "conformance";

/* ---- Leak-tracking allocator (ALLOC-INV-12 pattern) ------------------------ */

typedef struct {
    void *live[TRACK_CAPACITY];   /* blocks returned and not yet freed */
    int   allocs;
    int   frees;
    int   foreign_frees;          /* frees of a pointer not currently live */
    int   overflow;               /* more blocks live at once than TRACK_CAPACITY */
} Tracker;

static void *tracked_alloc(const EngineAllocator *allocator, size_t size, size_t alignment) {
    Tracker *t = allocator->user_data;
    const EngineAllocator *base = engine_default_allocator();
    void *p = (base->alloc)(base, size, alignment);

    if (p != NULL) {
        t->allocs++;
        int placed = 0;
        for (int i = 0; i < TRACK_CAPACITY && !placed; i++) {
            if (t->live[i] == NULL) {
                t->live[i] = p;
                placed = 1;
            }
        }
        t->overflow += !placed;
    }
    return p;
}

static void tracked_free(const EngineAllocator *allocator, void *memory) {
    Tracker *t = allocator->user_data;
    const EngineAllocator *base = engine_default_allocator();

    if (memory != NULL) {
        int found = 0;
        for (int i = 0; i < TRACK_CAPACITY && !found; i++) {
            if (t->live[i] == memory) {
                t->live[i] = NULL;
                found = 1;
            }
        }
        t->foreign_frees += !found;
        t->frees++;
    }
    (base->free)(base, memory);
}

/* ---- Always-NULL allocator (PLAT-INV-9 OOM) ------------------------------- */

typedef struct {
    int calls;
} NullAllocState;

static void *null_alloc(const EngineAllocator *allocator, size_t size, size_t alignment) {
    (void)size;
    (void)alignment;
    ((NullAllocState *)allocator->user_data)->calls++;
    return NULL;
}

static void null_free(const EngineAllocator *allocator, void *memory) {
    (void)allocator;
    (void)memory;
}

/* ---- Shared fixtures -------------------------------------------------------- */

/* PLATFORM-041 invalid descriptors. Each differs from a valid one in exactly
 * one field, so a failure points at that field. */
typedef struct {
    const char        *name;
    PlatformWindowDesc desc;
} InvalidDescCase;

static const InvalidDescCase invalid_descs[] = {
    {"zero width",          {window_title, 0, 240, 0}},
    {"zero height",         {window_title, 320, 0, 0}},
    {"NULL title",          {NULL, 320, 240, 0}},
    {"unknown flag bit 0x2",  {window_title, 320, 240, 0x2u}},
    {"unknown flag bit 0x80000000", {window_title, 320, 240, 0x80000000u}},
};

/* PLATFORM-071 table, written once as data. A NULL/0 column means that field
 * must hold NULL/0; the other columns must be non-NULL/non-zero. */
typedef struct {
    int32_t     type;
    const char *name;
    bool        display_null;
    bool        window_null;
    bool        window_id_zero;
} NativeRow;

static const NativeRow native_table[] = {
    {PLATFORM_NATIVE_WINDOW_WIN32,   "WIN32",   false, false, true},
    {PLATFORM_NATIVE_WINDOW_COCOA,   "COCOA",   true,  false, true},
    {PLATFORM_NATIVE_WINDOW_X11,     "X11",     false, true,  false},
    {PLATFORM_NATIVE_WINDOW_WAYLAND, "WAYLAND", false, false, true},
};

static const NativeRow *native_row_for(int32_t type) {
    for (size_t i = 0; i < sizeof native_table / sizeof native_table[0]; i++) {
        if (native_table[i].type == type) {
            return &native_table[i];
        }
    }
    return NULL;
}

/* Runs body with a live Platform, then shuts it down whatever body returned. */
typedef int (*PlatformBody)(Platform *platform);

static int with_platform(PlatformBody body) {
    Platform *platform = NULL;
    EngineResult r = platform_init(&platform, NULL, NULL);
    CHECK(r == ENGINE_RESULT_SUCCESS && platform != NULL, "platform_init returned %s",
          engine_result_name(r));
    int rc = body(platform);
    platform_shutdown(platform);
    return rc;
}

/* Runs body with a live Platform and window, then destroys the window and shuts
 * the Platform down whatever body returned. */
typedef int (*WindowBody)(Platform *platform, PlatformWindow *window);

static int with_platform_window(const PlatformWindowDesc *desc, WindowBody body) {
    Platform *platform = NULL;
    EngineResult pr = platform_init(&platform, NULL, NULL);
    CHECK(pr == ENGINE_RESULT_SUCCESS && platform != NULL, "platform_init returned %s",
          engine_result_name(pr));
    PlatformWindow *window = NULL;
    EngineResult wr = platform_window_create(&window, platform, NULL, desc);
    int rc = 0;
    if (wr == ENGINE_RESULT_SUCCESS && window != NULL) {
        rc = body(platform, window);
        platform_window_destroy(window);
    } else {
        fprintf(stderr, "✗ platform_window_create returned %s\n", engine_result_name(wr));
        rc = 1;
    }
    platform_shutdown(platform);
    return rc;
}

/* ---- Pure-argument checks: no Platform, run before the skip decision ------- */

/* PLAT-INV-9: platform_init(NULL, ...) is INVALID_ARGUMENT. */
static int test_init_rejects_null_out_platform(void) {
    EXPECT(platform_init(NULL, NULL, NULL), ENGINE_RESULT_INVALID_ARGUMENT);
    return 0;
}

/* PLAT-INV-10 (PLATFORM-041): a NULL platform is INVALID_ARGUMENT and the
 * poisoned out-parameter is left NULL. */
static int test_window_create_rejects_null_platform(void) {
    PlatformWindow *out = POISON_PTR(PlatformWindow);
    PlatformWindowDesc desc = {window_title, 320, 240, 0};
    EngineResult got = platform_window_create(&out, NULL, NULL, &desc);
    CHECK(got == ENGINE_RESULT_INVALID_ARGUMENT, "NULL platform returned %s",
          engine_result_name(got));
    CHECK(out == NULL, "NULL platform left *out_window non-NULL");
    return 0;
}

/* ---- PLAT-INV-9: init, OOM, leaks, logger ----------------------------------- */

/* Runs only after the probe succeeded, so the window-system probe has passed
 * and the only remaining failure source is the allocator. */
static int test_init_oom_with_always_null_allocator(void) {
    NullAllocState state = {0};
    EngineAllocator allocator = {.alloc = null_alloc, .free = null_free, .user_data = &state};
    Platform *platform = POISON_PTR(Platform);

    EngineResult got = platform_init(&platform, &allocator, NULL);
    if (got == ENGINE_RESULT_SUCCESS && platform != NULL) {
        platform_shutdown(platform);   /* never leave a Platform live on failure */
    }
    CHECK(state.calls > 0, "the always-NULL allocator was never consulted");
    CHECK(got == ENGINE_RESULT_OUT_OF_MEMORY, "platform_init returned %s, expected OUT_OF_MEMORY",
          engine_result_name(got));
    CHECK(platform == NULL, "platform_init left *out_platform non-NULL");
    return 0;
}

/* Capture-sink logger (PLATFORM-100): counts every record and the WARN-or-worse
 * ones. */
typedef struct {
    size_t total;
    size_t warn_or_worse;
} LogCount;

static void count_write(const EngineLogSink *sink, const EngineLogRecord *record) {
    LogCount *c = sink->user_data;
    c->total++;
    if (record->level >= ENGINE_LOG_LEVEL_WARN) {
        c->warn_or_worse++;
    }
}

/* One init, one window, destroy, shutdown, through the tracked allocator and
 * the capture logger. */
/* What one cycle observed. Validity is recorded before anything is released, so
 * the checks never read a pointer after it is freed. */
typedef struct {
    EngineResult platform_result;
    bool         window_created;   /* result SUCCESS and a non-NULL window */
    int          window_allocs;    /* tracked allocations made by window_create alone */
} CycleReport;

static void leak_cycle(const EngineAllocator *allocator, const EngineLogger *logger,
                       const Tracker *tracker, CycleReport *report) {
    memset(report, 0, sizeof *report);
    Platform *platform = NULL;
    report->platform_result = platform_init(&platform, allocator, logger);
    if (report->platform_result != ENGINE_RESULT_SUCCESS || platform == NULL) {
        return;
    }

    PlatformWindowDesc desc = {window_title, 320, 240, 0};
    PlatformWindow *window = NULL;
    int allocs_before_create = tracker->allocs;
    EngineResult wr = platform_window_create(&window, platform, allocator, &desc);
    report->window_allocs = tracker->allocs - allocs_before_create;
    report->window_created = wr == ENGINE_RESULT_SUCCESS && window != NULL;
    if (window != NULL) {
        platform_window_destroy(window);
    }
    platform_shutdown(platform);
}

/* PLAT-INV-9: equal alloc/free counts after each cycle, across two cycles
 * (PLATFORM-036 sequential re-initialization). Each cycle must also allocate
 * through the injected allocator, so the per-cycle growth check is what proves
 * cycle 2 ran through it; the cumulative count cannot show that. The clean path
 * must log no WARN or ERROR records. */
static int test_leaks_balanced_across_two_cycles(void) {
    Tracker tracker = {0};
    EngineAllocator allocator = {.alloc = tracked_alloc, .free = tracked_free, .user_data = &tracker};
    LogCount logs = {0};
    EngineLogSink sink = {.write = count_write, .user_data = &logs, .min_level = ENGINE_LOG_LEVEL_TRACE};
    EngineLogger logger;
    CHECK_OK(engine_log_init(&logger, ENGINE_LOG_LEVEL_TRACE));
    CHECK_OK(engine_log_add_sink(&logger, &sink));

    /* Control: the capture sink receives a record, so a zero count below means
     * "clean", not "not wired". */
    engine_log_write(&logger, ENGINE_LOG_LEVEL_WARN, "Platform", NULL, 0, "capture control");
    CHECK(logs.warn_or_worse == 1, "capture sink did not receive the control record");
    logs = (LogCount){0};

    for (int cycle = 1; cycle <= 2; cycle++) {
        int allocs_before = tracker.allocs;
        CycleReport report;
        leak_cycle(&allocator, &logger, &tracker, &report);
        CHECK(report.platform_result == ENGINE_RESULT_SUCCESS, "cycle %d: platform_init returned %s",
              cycle, engine_result_name(report.platform_result));
        CHECK(report.window_created, "cycle %d: platform_window_create did not create a window", cycle);
        CHECK(report.window_allocs > 0, "cycle %d: window_create made no allocation through the injected allocator",
              cycle);
        CHECK(tracker.allocs > allocs_before, "cycle %d: the injected allocator was not used", cycle);
        CHECK(tracker.allocs == tracker.frees, "cycle %d: %d allocations but %d frees", cycle,
              tracker.allocs, tracker.frees);
        CHECK(tracker.foreign_frees == 0, "cycle %d: %d free(s) of a pointer not live", cycle,
              tracker.foreign_frees);
        CHECK(tracker.overflow == 0, "cycle %d: more than %d blocks live at once", cycle,
              TRACK_CAPACITY);
        CHECK(logs.warn_or_worse == 0, "cycle %d: %zu WARN-or-worse record(s) on the clean path",
              cycle, logs.warn_or_worse);
    }
    engine_log_shutdown(&logger);
    return 0;
}

/* Output capture for the NULL-logger check. POSIX redirects fd 1 and 2 to a
 * temporary file; Windows uses the CRT equivalents. */
typedef struct {
    FILE *sink;
    int   saved_out;
    int   saved_err;
} Capture;

#if defined(_WIN32)
#include <io.h>
#define capture_dup _dup
#define capture_dup2 _dup2
#define capture_close _close
#define capture_fileno _fileno
#else
#include <unistd.h>
#define capture_dup dup
#define capture_dup2 dup2
#define capture_close close
#define capture_fileno fileno
#endif

#define CAPTURE_STDOUT_FD 1
#define CAPTURE_STDERR_FD 2

static int capture_begin(Capture *c) {
    c->sink = NULL;
    fflush(stdout);
    fflush(stderr);
    c->sink = tmpfile();
    if (c->sink == NULL) {
        return -1;
    }
    c->saved_out = capture_dup(CAPTURE_STDOUT_FD);
    c->saved_err = capture_dup(CAPTURE_STDERR_FD);
    if (c->saved_out < 0 || c->saved_err < 0) {
        return -1;
    }
    if (capture_dup2(capture_fileno(c->sink), CAPTURE_STDOUT_FD) < 0 ||
        capture_dup2(capture_fileno(c->sink), CAPTURE_STDERR_FD) < 0) {
        return -1;
    }
    return 0;
}

/* Restores stdout/stderr and returns the number of bytes written meanwhile. */
static long capture_end(Capture *c) {
    if (c->sink == NULL) {
        return -1;
    }
    fflush(stdout);
    fflush(stderr);
    capture_dup2(c->saved_out, CAPTURE_STDOUT_FD);
    capture_dup2(c->saved_err, CAPTURE_STDERR_FD);
    capture_close(c->saved_out);
    capture_close(c->saved_err);
    fseek(c->sink, 0, SEEK_END);
    long bytes = ftell(c->sink);
    fclose(c->sink);
    c->sink = NULL;
    return bytes;
}

/* PLAT-INV-9: a NULL logger discards diagnostics: no output, no crash, for a
 * full init, window, pump, destroy, shutdown run. All CHECKs run after the
 * streams are restored, so failures are visible. */
static int test_null_logger_is_silent(void) {
    Capture cap;
    int began = capture_begin(&cap);
    if (began != 0) {
        capture_end(&cap);
        CHECK(0, "could not redirect stdout and stderr for the output check");
    }

    Platform *platform = NULL;
    EngineResult pr = platform_init(&platform, NULL, NULL);
    EngineResult wr = ENGINE_RESULT_INVALID_ARGUMENT;
    if (pr == ENGINE_RESULT_SUCCESS && platform != NULL) {
        PlatformWindowDesc desc = {window_title, 320, 240, 0};
        PlatformWindow *window = NULL;
        wr = platform_window_create(&window, platform, NULL, &desc);
        platform_pump_events(platform);
        if (window != NULL) {
            platform_window_destroy(window);
        }
        platform_shutdown(platform);
    }
    long bytes = capture_end(&cap);

    CHECK(pr == ENGINE_RESULT_SUCCESS, "platform_init with NULL logger returned %s",
          engine_result_name(pr));
    CHECK(wr == ENGINE_RESULT_SUCCESS, "platform_window_create with NULL logger returned %s",
          engine_result_name(wr));
    CHECK(bytes == 0, "%ld byte(s) written with a NULL logger", bytes);
    return 0;
}

/* ---- PLAT-INV-10: window_create argument rejection -------------------------- */

static int check_invalid_descs(Platform *platform) {
    /* Control: the valid shape succeeds, so the rejections below are caused by
     * the one field each case changes, not by a broken Platform. */
    PlatformWindowDesc valid = {window_title, 320, 240, PLATFORM_WINDOW_RESIZABLE};
    PlatformWindow *control = NULL;
    EngineResult cr = platform_window_create(&control, platform, NULL, &valid);
    if (control != NULL) {
        platform_window_destroy(control);
    }
    CHECK(cr == ENGINE_RESULT_SUCCESS, "control: valid descriptor returned %s",
          engine_result_name(cr));

    for (size_t i = 0; i < sizeof invalid_descs / sizeof invalid_descs[0]; i++) {
        PlatformWindow *out = POISON_PTR(PlatformWindow);
        EngineResult got = platform_window_create(&out, platform, NULL, &invalid_descs[i].desc);
        if (got == ENGINE_RESULT_SUCCESS && out != NULL) {
            platform_window_destroy(out);   /* do not leave the window behind */
        }
        CHECK(got == ENGINE_RESULT_INVALID_ARGUMENT, "%s: returned %s, expected INVALID_ARGUMENT",
              invalid_descs[i].name, engine_result_name(got));
        CHECK(out == NULL, "%s: left *out_window non-NULL", invalid_descs[i].name);
    }

    PlatformWindow *out = POISON_PTR(PlatformWindow);
    EngineResult got = platform_window_create(&out, platform, NULL, NULL);
    CHECK(got == ENGINE_RESULT_INVALID_ARGUMENT, "NULL desc returned %s",
          engine_result_name(got));
    CHECK(out == NULL, "NULL desc left *out_window non-NULL");
    return 0;
}

static int test_window_create_rejects_invalid_descs(void) {
    return with_platform(check_invalid_descs);
}

/* ---- PLAT-INV-11: fresh window state and native record ----------------------- */

static int check_fresh_window_state(Platform *platform, PlatformWindow *window) {
    CHECK(platform_window_should_close(window) == false, "a new window reports should_close");

    uint32_t width = 0;
    uint32_t height = 0;
    EXPECT(platform_window_get_framebuffer_size(window, &width, &height), ENGINE_RESULT_SUCCESS);
    CHECK(width > 0 && height > 0, "a new window has framebuffer %ux%u", width, height);

    /* Pump once, then drain: creation queued nothing, so the drain must be empty. */
    platform_pump_events(platform);
    PlatformEvent event = {0};
    CHECK(!platform_next_event(platform, &event),
          "a new window left a queued event (type %d)", event.type);
    return 0;
}

static int test_fresh_window_state(void) {
    PlatformWindowDesc desc = {window_title, 320, 240, 0};
    return with_platform_window(&desc, check_fresh_window_state);
}

/* PLAT-INV-11: the native record matches the table row for its type. The
 * record is poisoned first, so a field the backend never writes cannot pass as
 * NULL/0 by accident. */
static int check_native_record(Platform *platform, PlatformWindow *window) {
    (void)platform;
    /* Two queries from different initial contents, all-zero and poisoned. A
     * getter that leaves a field unwritten returns that field's initial value,
     * so the two results differ and the check fails. Only then is the row
     * checked, against the all-zero result. */
    PlatformNativeWindow native = {0};
    PlatformNativeWindow poisoned;
    memset(&poisoned, POISON_BYTE, sizeof poisoned);
    EngineResult r = platform_window_get_native(window, &native);
    EngineResult rp = platform_window_get_native(window, &poisoned);
    CHECK(r == ENGINE_RESULT_SUCCESS, "platform_window_get_native returned %s",
          engine_result_name(r));
    CHECK(rp == ENGINE_RESULT_SUCCESS, "platform_window_get_native (poisoned) returned %s",
          engine_result_name(rp));
    CHECK(native.type == poisoned.type && native.display == poisoned.display &&
              native.window == poisoned.window && native.window_id == poisoned.window_id,
          "native record depends on its initial contents: a field was not written");
    CHECK(native.type != PLATFORM_NATIVE_WINDOW_NONE, "native type is NONE");

    const NativeRow *row = native_row_for(native.type);
    CHECK(row != NULL, "native type %d is not in the PLATFORM-071 table", (int)native.type);
    CHECK((native.display == NULL) == row->display_null, "%s: display is %s", row->name,
          native.display == NULL ? "NULL" : "non-NULL");
    CHECK((native.window == NULL) == row->window_null, "%s: window is %s", row->name,
          native.window == NULL ? "NULL" : "non-NULL");
    CHECK((native.window_id == 0) == row->window_id_zero, "%s: window_id is %llu", row->name,
          (unsigned long long)native.window_id);
    return 0;
}

static int test_native_record_matches_table(void) {
    PlatformWindowDesc desc = {window_title, 320, 240, 0};
    return with_platform_window(&desc, check_native_record);
}

/* ---- PLAT-INV-16: wait(0) and bounded short waits (PLATFORM-055/056) ---------- */

/* Upper bounds only: early return is allowed (PLATFORM-055). A wait(0) that
 * blocks fails the first bound. A wait that truly blocks forever hangs here and
 * CTest's TIMEOUT fails the test in bounded time; on macOS a wait that merely
 * ignores its timeout is still woken by AppKit's own events, so that case is
 * only a manual check ("wakes promptly", PLAT-INV-16). */
#define WAIT_ZERO_BOUND_NS 100000000u
#define WAIT_SHORT_NS 20000000u
#define WAIT_SHORT_BOUND_NS 2000000000u

/* PLAT-INV-16 times a short wait "with no pending input": AppKit posts
 * window-state events shortly after creation, and the pump time spent on them is
 * outside PLATFORM-056's bound. Pumping for a while first drains them so the
 * measured call is the blocking phase. */
#define WAIT_SETTLE_NS 300000000u
#define WAIT_SETTLE_STEP_NS 10000000u

static int body_wait_bounds(Platform *platform, PlatformWindow *window) {
    (void)window;
    uint64_t settle_start = platform_time_now_ns();
    while (platform_time_now_ns() - settle_start < WAIT_SETTLE_NS) {
        platform_wait_events(platform, WAIT_SETTLE_STEP_NS);
    }

    uint64_t start = platform_time_now_ns();
    platform_wait_events(platform, 0);
    uint64_t elapsed = platform_time_now_ns() - start;
    CHECK(elapsed < WAIT_ZERO_BOUND_NS, "wait(0) took %llu ns", (unsigned long long)elapsed);

    start = platform_time_now_ns();
    platform_wait_events(platform, WAIT_SHORT_NS);
    elapsed = platform_time_now_ns() - start;
    CHECK(elapsed < WAIT_SHORT_BOUND_NS, "wait(20 ms) took %llu ns", (unsigned long long)elapsed);
    return 0;
}

static int test_wait_returns_within_bounds(void) {
    static const PlatformWindowDesc desc = {
        .title = window_title, .width = 320, .height = 240, .flags = PLATFORM_WINDOW_RESIZABLE,
    };
    return with_platform_window(&desc, body_wait_bounds);
}

/* ---- PLAT-INV-12: drain on an empty queue, purge on destroy ------------------ */

/* The queue is emptied first, so the drain below is on an empty queue. The
 * output is poisoned, and an unchanged output shows the drain wrote nothing. */
static int body_empty_drain(Platform *platform) {
    platform_pump_events(platform);
    PlatformEvent scratch = {0};
    uint32_t drained = 0;
    while (platform_next_event(platform, &scratch)) {
        CHECK(++drained <= DRAIN_LIMIT, "the queue never empties");
    }

    /* Poison the whole object, padding included, and snapshot its bytes with
     * memcpy: struct assignment need not copy padding, so memcmp on a copy made
     * by assignment would compare indeterminate bytes. */
    PlatformEvent event;
    memset(&event, POISON_BYTE, sizeof event);
    unsigned char before[sizeof event];
    memcpy(before, &event, sizeof event);
    CHECK(platform_next_event(platform, &event) == false, "drain on an empty queue returned true");
    CHECK(memcmp(&event, before, sizeof event) == 0, "drain on an empty queue wrote *out_event");
    CHECK(platform_next_event(platform, NULL) == false, "drain with NULL out_event returned true");
    return 0;
}

static int test_empty_drain_writes_nothing(void) {
    return with_platform(body_empty_drain);
}

/* PLAT-INV-12, second clause. Non-vacuous only when an event for the window
 * was pending at destroy: the API has no portable way to synthesize one, so
 * this check passes trivially when the pump queued nothing for the window.
 * The purge itself (PLATFORM-044) is review-enforced. The window is identified
 * by its address as an integer, taken before destroy, so no freed pointer is
 * compared. */
static int body_destroy_purges(Platform *platform) {
    PlatformWindowDesc desc = {window_title, 320, 240, 0};
    PlatformWindow *doomed = NULL;
    EngineResult r = platform_window_create(&doomed, platform, NULL, &desc);
    CHECK(r == ENGINE_RESULT_SUCCESS && doomed != NULL, "platform_window_create returned %s",
          engine_result_name(r));

    uintptr_t doomed_id = (uintptr_t)doomed;
    platform_pump_events(platform);
    platform_window_destroy(doomed);

    PlatformEvent event = {0};
    uint32_t drained = 0;
    while (platform_next_event(platform, &event)) {
        CHECK(++drained <= DRAIN_LIMIT, "the queue never empties");
        CHECK((uintptr_t)event.window != doomed_id,
              "event %u (type %d) refers to the destroyed window", drained, event.type);
    }
    return 0;
}

static int test_destroy_purges_window_events(void) {
    return with_platform(body_destroy_purges);
}

/* ---- Skip decision (PLATFORM-101) ------------------------------------------- */

static int no_display_declared(void) {
    const char *value = getenv(NO_DISPLAY_ENV);
    return value != NULL && strcmp(value, "1") == 0;
}

int main(void) {
    static const TestCase pure[] = {
        {"PLAT-INV-9: platform_init(NULL out) is INVALID_ARGUMENT", test_init_rejects_null_out_platform},
        {"PLAT-INV-10: NULL platform is INVALID_ARGUMENT, out NULL", test_window_create_rejects_null_platform},
    };
    if (RUN_TESTS(pure) != 0) {
        return 1;
    }

    /* Probe once: decides skip versus fail before any Platform-based check. */
    Platform *probe = NULL;
    EngineResult pr = platform_init(&probe, NULL, NULL);
    /* Recorded before shutdown: probe is not read after it is released. */
    bool probe_ok = pr == ENGINE_RESULT_SUCCESS && probe != NULL;
    if (probe_ok) {
        platform_shutdown(probe);
    }
    if (pr == ENGINE_RESULT_NOT_SUPPORTED) {
        if (no_display_declared()) {
            printf("SKIPPED: platform_init returned NOT_SUPPORTED and " NO_DISPLAY_ENV
                   "=1 declares a headless host\n");
            return SKIP_RETURN_CODE_NO_DISPLAY;
        }
        fprintf(stderr, "✗ platform_init returned NOT_SUPPORTED: a window session is required "
                        "(set " NO_DISPLAY_ENV "=1 only on a declared headless host)\n");
        return 1;
    }
    if (!probe_ok) {
        fprintf(stderr, "✗ platform_init probe returned %s\n", engine_result_name(pr));
        return 1;
    }

    static const TestCase window[] = {
        {"PLAT-INV-9: always-NULL allocator gives OUT_OF_MEMORY, out NULL", test_init_oom_with_always_null_allocator},
        {"PLAT-INV-9: alloc/free balanced across two init/shutdown cycles", test_leaks_balanced_across_two_cycles},
        {"PLAT-INV-9: NULL logger is silent and does not crash", test_null_logger_is_silent},
        {"PLAT-INV-10: invalid descriptors rejected, out NULL", test_window_create_rejects_invalid_descs},
        {"PLAT-INV-11: fresh window state, no queued event", test_fresh_window_state},
        {"PLAT-INV-11: native record matches its table row", test_native_record_matches_table},
        {"PLAT-INV-12: drain on empty queue writes nothing", test_empty_drain_writes_nothing},
        {"PLAT-INV-12: destroy purges the window's events", test_destroy_purges_window_events},
        {"PLAT-INV-16: wait(0) and short wait return within bounds", test_wait_returns_within_bounds},
    };
    return RUN_TESTS(window);
}
