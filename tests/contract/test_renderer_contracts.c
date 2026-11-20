/* Renderer API contract: renderer_init/renderer_shutdown at runtime.
 * Links engine_renderer only, never engine_core (ALLOC-INV-11). */

#include <stddef.h>
#include "src/renderer/api.h"
#include "tests/check.h"

/* Tracking allocator over the default one; all state in user_data. */
typedef struct {
    int allocs;
    int frees;
    void *last;
} Tracker;

static void *tracked_alloc(const EngineAllocator *allocator, size_t size, size_t alignment) {
    Tracker *t = allocator->user_data;
    const EngineAllocator *base = engine_default_allocator();
    void *p = (base->alloc)(base, size, alignment);
    if (p != NULL) {
        t->allocs++;
        t->last = p;
    }
    return p;
}

static void tracked_free(const EngineAllocator *allocator, void *memory) {
    Tracker *t = allocator->user_data;
    const EngineAllocator *base = engine_default_allocator();
    if (memory != NULL) {
        t->frees++;
    }
    (base->free)(base, memory);
}

static void *always_fails(const EngineAllocator *allocator, size_t size, size_t alignment) {
    (void)allocator;
    (void)size;
    (void)alignment;
    return NULL;
}

/* Counts ERROR records; proves each failure path logged (CORE-LOG-005 says a
 * NULL logger discards, so a real logger is needed to observe this). */
static void count_errors(const EngineLogSink *sink, const EngineLogRecord *record) {
    int *errors = sink->user_data;
    *errors += record->level == ENGINE_LOG_LEVEL_ERROR;
}

static int test_lifecycle_defaults(void) {
    Renderer *r = NULL;
    EXPECT(renderer_init(&r, NULL, NULL), ENGINE_RESULT_SUCCESS);
    CHECK(r != NULL, "renderer_init succeeded but returned no renderer");
    EXPECT(renderer_shutdown(r), ENGINE_RESULT_SUCCESS);
    return 0;
}

/* CORE-ALLOC-067: the injected allocator is stored and used for the release. */
static int test_uses_injected_allocator(void) {
    Tracker t = {0};
    EngineAllocator allocator = {.alloc = tracked_alloc, .free = tracked_free, .user_data = &t};
    Renderer *r = NULL;

    EXPECT(renderer_init(&r, &allocator, NULL), ENGINE_RESULT_SUCCESS);
    CHECK(t.allocs == 1 && t.last == r, "renderer was not allocated through the injected allocator");
    EXPECT(renderer_shutdown(r), ENGINE_RESULT_SUCCESS);
    CHECK(t.frees == t.allocs, "%d allocations but %d frees", t.allocs, t.frees);
    return 0;
}

static int test_failure_paths(void) {
    EngineLogger logger;
    int errors = 0;
    EngineLogSink sink = {.write = count_errors, .user_data = &errors, .min_level = ENGINE_LOG_LEVEL_TRACE};
    CHECK_OK(engine_log_init(&logger, ENGINE_LOG_LEVEL_TRACE));
    CHECK_OK(engine_log_add_sink(&logger, &sink));

    EngineAllocator no_free = {.alloc = always_fails, .free = NULL};
    EngineAllocator failing = {.alloc = always_fails, .free = tracked_free};
    Renderer *r = NULL;

    EXPECT(renderer_init(NULL, NULL, &logger), ENGINE_RESULT_INVALID_ARGUMENT);
    EXPECT(renderer_init(&r, &no_free, &logger), ENGINE_RESULT_INVALID_ARGUMENT);
    CHECK(r == NULL, "invalid allocator left the out-parameter non-NULL");
    EXPECT(renderer_init(&r, &failing, &logger), ENGINE_RESULT_OUT_OF_MEMORY);
    CHECK(r == NULL, "allocation failure left the out-parameter non-NULL");
    CHECK(errors == 3, "expected one ERROR record per failure path, got %d", errors);

    EXPECT(renderer_shutdown(NULL), ENGINE_RESULT_INVALID_ARGUMENT);
    engine_log_shutdown(&logger);
    return 0;
}

int main(void) {
    static const TestCase tests[] = {
        {"renderer lifecycle with default allocator, NULL logger", test_lifecycle_defaults},
        {"renderer allocates and releases through the injected allocator", test_uses_injected_allocator},
        {"renderer failure paths return exact codes and log", test_failure_paths},
    };
    return RUN_TESTS(tests);
}
