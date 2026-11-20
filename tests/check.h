/* Minimal shared test helpers. A test is `int fn(void)` returning 0 on
 * success; CHECK returns 1 from the enclosing test on the first failure. */

#ifndef NAYLA_TESTS_CHECK_H
#define NAYLA_TESTS_CHECK_H

#include <stddef.h>
#include <stdio.h>
#include "src/core/result.h"

#define CHECK(cond, ...) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "✗ " __VA_ARGS__); \
            fputc('\n', stderr); \
            return 1; \
        } \
    } while (0)

#define CHECK_OK(expr) CHECK((expr) == ENGINE_RESULT_SUCCESS, #expr " failed")

/* Assert the exact result code, not merely "not success". */
#define EXPECT(call, code) \
    do { \
        EngineResult got_ = (call); \
        CHECK(got_ == (code), #call " returned %s, expected %s", \
              engine_result_name(got_), engine_result_name(code)); \
    } while (0)

typedef struct {
    const char *name;
    int (*run)(void);
} TestCase;

/* Run every case, report each by name; returns nonzero if any failed. */
static inline int run_tests(const TestCase *tests, size_t count) {
    int failed = 0;
    for (size_t i = 0; i < count; i++) {
        if (tests[i].run() == 0) {
            printf("✓ %s\n", tests[i].name);
        } else {
            fprintf(stderr, "✗ %s\n", tests[i].name);
            failed++;
        }
    }
    return failed != 0;
}

#define RUN_TESTS(tests) run_tests((tests), sizeof(tests) / sizeof((tests)[0]))

#endif /* NAYLA_TESTS_CHECK_H */
