/* Allocator unit tests: ALLOC-INV-3, 4, 5, 6. */

#include <stdint.h>
#include "src/core/allocator.h"
#include "tests/check.h"

/* ALLOC-INV-3: the default allocator is a non-NULL, stable singleton. */
static int test_singleton(void) {
    const EngineAllocator *a = engine_default_allocator();

    CHECK(a != NULL, "engine_default_allocator() returned NULL");
    CHECK(a == engine_default_allocator(), "engine_default_allocator() pointer is not stable");
    CHECK(a->alloc != NULL && a->free != NULL, "default allocator alloc/free is NULL");
    return 0;
}

/* ALLOC-INV-4: every power-of-two alignment up to the maximum is honored. */
static int test_alignment(void) {
    const EngineAllocator *a = engine_default_allocator();

    for (size_t alignment = 1; alignment <= ENGINE_ALLOCATOR_MAX_ALIGNMENT; alignment <<= 1) {
        void *p = engine_alloc(a, 1, alignment);
        CHECK(p != NULL, "engine_alloc(_, 1, %zu) returned NULL", alignment);
        size_t misalignment = (uintptr_t)p % alignment;
        engine_free(a, p);
        CHECK(misalignment == 0, "alignment %zu: pointer off by %zu", alignment, misalignment);
    }
    return 0;
}

/* ALLOC-INV-5: invalid requests return NULL. */
static int test_rejection(void) {
    const EngineAllocator *a = engine_default_allocator();
    const size_t ok = ENGINE_ALLOCATOR_DEFAULT_ALIGNMENT;

    CHECK(engine_alloc(a, 0, ok) == NULL, "size 0 accepted");
    CHECK(engine_alloc(a, 1, 0) == NULL, "alignment 0 accepted");
    CHECK(engine_alloc(a, 1, 3) == NULL, "non-power-of-two alignment accepted");
    CHECK(engine_alloc(a, 1, ENGINE_ALLOCATOR_MAX_ALIGNMENT * 2) == NULL, "alignment above max accepted");
    CHECK(engine_alloc(a, SIZE_MAX, ok) == NULL, "size SIZE_MAX accepted");
    return 0;
}

/* ALLOC-INV-6: NULL arguments are tolerated and never resolve to a default. */
static int test_null_handling(void) {
    const EngineAllocator *a = engine_default_allocator();
    void *p = engine_alloc(a, 1, ENGINE_ALLOCATOR_DEFAULT_ALIGNMENT);
    CHECK(p != NULL, "could not allocate a test block");

    engine_free(a, NULL);     /* no-op */
    engine_free(NULL, p);     /* no-op: leaks rather than crashes, so free p properly below */
    CHECK(engine_alloc(NULL, 1, 1) == NULL, "engine_alloc(NULL, ...) did not return NULL");

    engine_free(a, p);
    return 0;
}

int main(void) {
    static const TestCase tests[] = {
        {"ALLOC-INV-3 singleton", test_singleton},
        {"ALLOC-INV-4 alignment", test_alignment},
        {"ALLOC-INV-5 rejection", test_rejection},
        {"ALLOC-INV-6 NULL handling", test_null_handling},
    };
    return RUN_TESTS(tests);
}
