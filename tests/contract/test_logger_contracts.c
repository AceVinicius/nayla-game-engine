/* Logger contract tests: LOG-INV-8, the exact result codes of
 * engine_log_add_sink / engine_log_remove_sink (CORE-LOG-033/036/037). */

#include <stdio.h>
#include "src/core/logging.h"

#define CHECK(cond, ...) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "✗ " __VA_ARGS__); \
            fputc('\n', stderr); \
            return 1; \
        } \
    } while (0)

/* Assert the exact result code, not merely "not success". */
#define EXPECT(call, code) \
    do { \
        EngineResult got_ = (call); \
        CHECK(got_ == (code), #call " returned %s, expected %s", \
              engine_result_name(got_), engine_result_name(code)); \
    } while (0)

/* A sink that counts the records it receives in its user_data. */
static void count_write(const EngineLogSink *sink, const EngineLogRecord *record) {
    (void)record;
    ++*(size_t *)sink->user_data;
}

static EngineLogSink counting_sink(size_t *count) {
    return (EngineLogSink){.write = count_write, .user_data = count,
                           .min_level = ENGINE_LOG_LEVEL_TRACE};
}

static int test_add_sink_rejects_bad_input(void) {
    EngineLogger logger;
    size_t n = 0;
    EngineLogSink good = counting_sink(&n);
    EngineLogSink no_write = {.min_level = ENGINE_LOG_LEVEL_TRACE};
    EngineLogSink bad_level = counting_sink(&n);
    bad_level.min_level = 999;

    EXPECT(engine_log_init(&logger, ENGINE_LOG_LEVEL_TRACE), ENGINE_RESULT_SUCCESS);
    EXPECT(engine_log_add_sink(NULL, &good), ENGINE_RESULT_INVALID_ARGUMENT);
    EXPECT(engine_log_add_sink(&logger, NULL), ENGINE_RESULT_INVALID_ARGUMENT);
    EXPECT(engine_log_add_sink(&logger, &no_write), ENGINE_RESULT_INVALID_ARGUMENT);
    EXPECT(engine_log_add_sink(&logger, &bad_level), ENGINE_RESULT_INVALID_ARGUMENT);

    engine_log_shutdown(&logger);
    return 0;
}

static int test_add_sink_rejects_full_table_and_duplicate(void) {
    EngineLogger logger;
    size_t counts[ENGINE_LOG_MAX_SINKS + 1] = {0};
    EngineLogSink sinks[ENGINE_LOG_MAX_SINKS + 1];

    EXPECT(engine_log_init(&logger, ENGINE_LOG_LEVEL_TRACE), ENGINE_RESULT_SUCCESS);
    for (size_t i = 0; i <= ENGINE_LOG_MAX_SINKS; i++) {
        sinks[i] = counting_sink(&counts[i]);
    }

    /* A duplicate on a non-full table, so the full-table check cannot mask it. */
    EXPECT(engine_log_add_sink(&logger, &sinks[0]), ENGINE_RESULT_SUCCESS);
    EXPECT(engine_log_add_sink(&logger, &sinks[0]), ENGINE_RESULT_INVALID_STATE);

    for (size_t i = 1; i < ENGINE_LOG_MAX_SINKS; i++) {
        EXPECT(engine_log_add_sink(&logger, &sinks[i]), ENGINE_RESULT_SUCCESS);
    }
    EXPECT(engine_log_add_sink(&logger, &sinks[ENGINE_LOG_MAX_SINKS]), ENGINE_RESULT_INVALID_STATE);

    engine_log_shutdown(&logger);
    return 0;
}

static int test_remove_sink_contract(void) {
    EngineLogger logger;
    size_t n1 = 0, n2 = 0;
    EngineLogSink registered = counting_sink(&n1);
    EngineLogSink other = counting_sink(&n2);

    EXPECT(engine_log_init(&logger, ENGINE_LOG_LEVEL_TRACE), ENGINE_RESULT_SUCCESS);
    EXPECT(engine_log_add_sink(&logger, &registered), ENGINE_RESULT_SUCCESS);

    EXPECT(engine_log_remove_sink(NULL, &registered), ENGINE_RESULT_INVALID_ARGUMENT);
    EXPECT(engine_log_remove_sink(&logger, NULL), ENGINE_RESULT_INVALID_ARGUMENT);
    EXPECT(engine_log_remove_sink(&logger, &other), ENGINE_RESULT_INVALID_STATE);

    engine_log_shutdown(&logger);
    return 0;
}

/* Two structurally identical sinks at different addresses are different
 * sinks: identity is the pointer (CORE-LOG-033). */
static int test_sink_identity_is_the_pointer(void) {
    EngineLogger logger;
    size_t n1 = 0, n2 = 0;
    EngineLogSink first = counting_sink(&n1);
    EngineLogSink second = counting_sink(&n2);

    EXPECT(engine_log_init(&logger, ENGINE_LOG_LEVEL_TRACE), ENGINE_RESULT_SUCCESS);
    EXPECT(engine_log_add_sink(&logger, &first), ENGINE_RESULT_SUCCESS);
    EXPECT(engine_log_add_sink(&logger, &second), ENGINE_RESULT_SUCCESS);

    engine_log_write(&logger, ENGINE_LOG_LEVEL_INFO, "Core", NULL, 0, "x");
    CHECK(n1 == 1 && n2 == 1, "both sinks should receive the record, got %zu and %zu", n1, n2);

    EXPECT(engine_log_remove_sink(&logger, &first), ENGINE_RESULT_SUCCESS);
    engine_log_write(&logger, ENGINE_LOG_LEVEL_INFO, "Core", NULL, 0, "y");
    CHECK(n1 == 1 && n2 == 2, "only the second sink should remain, got %zu and %zu", n1, n2);

    engine_log_shutdown(&logger);
    return 0;
}

int main(void) {
    static const struct {
        const char *name;
        int (*run)(void);
    } tests[] = {
        {"add_sink rejects NULL, missing write, bad level", test_add_sink_rejects_bad_input},
        {"add_sink rejects duplicate and full table", test_add_sink_rejects_full_table_and_duplicate},
        {"remove_sink contract", test_remove_sink_contract},
        {"sink identity is the pointer", test_sink_identity_is_the_pointer},
    };

    int failed = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        if (tests[i].run() == 0) {
            printf("✓ %s\n", tests[i].name);
        } else {
            fprintf(stderr, "✗ %s\n", tests[i].name);
            failed++;
        }
    }
    return failed != 0;
}
