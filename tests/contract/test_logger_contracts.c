#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "src/core/logging.h"

/* ============================================================================
 * Minimal capture sink for contract tests (same as unit tests)
 * ============================================================================
 */

typedef struct CaptureSinkRecord {
    EngineLogLevel level;
    const char *subsystem;
    char message[ENGINE_LOG_MESSAGE_MAX];
    size_t message_length;
} CaptureSinkRecord;

typedef struct CaptureSinkState {
    CaptureSinkRecord records[16];
    size_t count;
} CaptureSinkState;

static void capture_sink_write(const EngineLogSink *sink,
                               const EngineLogRecord *record) {
    CaptureSinkState *state = (CaptureSinkState *)sink->user_data;
    if (state == NULL || state->count >= 16) {
        return;
    }

    CaptureSinkRecord *captured = &state->records[state->count];
    captured->level = record->level;
    captured->subsystem = record->subsystem;

    size_t copy_len = record->message_length;
    if (copy_len >= ENGINE_LOG_MESSAGE_MAX) {
        copy_len = ENGINE_LOG_MESSAGE_MAX - 1;
    }
    memcpy(captured->message, record->message, copy_len);
    captured->message[copy_len] = '\0';
    captured->message_length = record->message_length;

    state->count++;
}

static EngineLogSink capture_sink_create(CaptureSinkState *state,
                                         EngineLogLevel min_level) {
    EngineLogSink sink;
    sink.write = capture_sink_write;
    sink.flush = NULL;
    sink.user_data = (void *)state;
    sink.min_level = (int32_t)min_level;
    return sink;
}

/* ============================================================================
 * Test LOG-INV-8: engine_log_add_sink and engine_log_remove_sink contracts
 * ============================================================================
 * CORE-LOG-036/037: exact error codes for various failure cases.
 */

static int test_add_sink_null_inputs(void) {
    printf("Testing LOG-INV-8 (part 1): add_sink with NULL inputs...\n");

    EngineLogger logger;
    if (engine_log_init(&logger, ENGINE_LOG_LEVEL_TRACE) != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_log_init failed\n");
        return 1;
    }

    CaptureSinkState state = {0};
    EngineLogSink sink = capture_sink_create(&state, ENGINE_LOG_LEVEL_TRACE);

    /* NULL logger. */
    EngineResult res = engine_log_add_sink(NULL, &sink);
    if (res != ENGINE_RESULT_INVALID_ARGUMENT) {
        fprintf(stderr, "✗ add_sink(NULL, sink) should return INVALID_ARGUMENT; got %d\n",
                (int)res);
        return 1;
    }
    printf("  ✓ add_sink(NULL, sink) returns INVALID_ARGUMENT\n");

    /* NULL sink. */
    res = engine_log_add_sink(&logger, NULL);
    if (res != ENGINE_RESULT_INVALID_ARGUMENT) {
        fprintf(stderr, "✗ add_sink(logger, NULL) should return INVALID_ARGUMENT; got %d\n",
                (int)res);
        return 1;
    }
    printf("  ✓ add_sink(logger, NULL) returns INVALID_ARGUMENT\n");

    /* Sink with NULL write. */
    EngineLogSink bad_sink;
    bad_sink.write = NULL;
    bad_sink.flush = NULL;
    bad_sink.user_data = NULL;
    bad_sink.min_level = ENGINE_LOG_LEVEL_TRACE;

    res = engine_log_add_sink(&logger, &bad_sink);
    if (res != ENGINE_RESULT_INVALID_ARGUMENT) {
        fprintf(stderr, "✗ add_sink(logger, sink_with_null_write) should return INVALID_ARGUMENT; got %d\n",
                (int)res);
        return 1;
    }
    printf("  ✓ add_sink(logger, sink_with_null_write) returns INVALID_ARGUMENT\n");

    engine_log_shutdown(&logger);
    return 0;
}

static int test_add_sink_out_of_range_level(void) {
    printf("Testing LOG-INV-8 (part 2): add_sink with out-of-range level...\n");

    EngineLogger logger;
    if (engine_log_init(&logger, ENGINE_LOG_LEVEL_TRACE) != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_log_init failed\n");
        return 1;
    }

    CaptureSinkState state = {0};
    EngineLogSink sink;
    sink.write = capture_sink_write;
    sink.flush = NULL;
    sink.user_data = (void *)&state;
    sink.min_level = (int32_t)999;  /* Out of range */

    EngineResult res = engine_log_add_sink(&logger, &sink);
    if (res != ENGINE_RESULT_INVALID_ARGUMENT) {
        fprintf(stderr, "✗ add_sink with out-of-range level should return INVALID_ARGUMENT; got %d\n",
                (int)res);
        return 1;
    }
    printf("  ✓ add_sink with out-of-range level returns INVALID_ARGUMENT\n");

    engine_log_shutdown(&logger);
    return 0;
}

static int test_add_sink_table_full(void) {
    printf("Testing LOG-INV-8 (part 3): add_sink when table is full...\n");

    EngineLogger logger;
    if (engine_log_init(&logger, ENGINE_LOG_LEVEL_TRACE) != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_log_init failed\n");
        return 1;
    }

    /* Fill the table with ENGINE_LOG_MAX_SINKS entries. */
    CaptureSinkState states[ENGINE_LOG_MAX_SINKS];
    EngineLogSink sinks[ENGINE_LOG_MAX_SINKS];

    for (size_t i = 0; i < ENGINE_LOG_MAX_SINKS; i++) {
        states[i].count = 0;
        sinks[i] = capture_sink_create(&states[i], ENGINE_LOG_LEVEL_TRACE);

        EngineResult res = engine_log_add_sink(&logger, &sinks[i]);
        if (res != ENGINE_RESULT_SUCCESS) {
            fprintf(stderr, "✗ Failed to add sink %zu: %d\n", i, (int)res);
            return 1;
        }
    }
    printf("  ✓ Added ENGINE_LOG_MAX_SINKS=%zu sinks\n", ENGINE_LOG_MAX_SINKS);

    /* Try to add one more; should fail with INVALID_STATE. */
    CaptureSinkState extra_state = {0};
    EngineLogSink extra_sink = capture_sink_create(&extra_state, ENGINE_LOG_LEVEL_TRACE);

    EngineResult res = engine_log_add_sink(&logger, &extra_sink);
    if (res != ENGINE_RESULT_INVALID_STATE) {
        fprintf(stderr, "✗ add_sink on full table should return INVALID_STATE; got %d\n",
                (int)res);
        return 1;
    }
    printf("  ✓ add_sink on full table returns INVALID_STATE\n");

    engine_log_shutdown(&logger);
    return 0;
}

static int test_add_sink_duplicate(void) {
    printf("Testing LOG-INV-8 (part 4): add_sink with duplicate pointer...\n");

    EngineLogger logger;
    if (engine_log_init(&logger, ENGINE_LOG_LEVEL_TRACE) != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_log_init failed\n");
        return 1;
    }

    CaptureSinkState state = {0};
    EngineLogSink sink = capture_sink_create(&state, ENGINE_LOG_LEVEL_TRACE);

    /* Add the sink once. */
    EngineResult res = engine_log_add_sink(&logger, &sink);
    if (res != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ First add_sink failed: %d\n", (int)res);
        return 1;
    }

    /* Try to add it again; should fail with INVALID_STATE. */
    res = engine_log_add_sink(&logger, &sink);
    if (res != ENGINE_RESULT_INVALID_STATE) {
        fprintf(stderr, "✗ add_sink(duplicate) should return INVALID_STATE; got %d\n",
                (int)res);
        return 1;
    }
    printf("  ✓ add_sink(duplicate pointer) returns INVALID_STATE\n");

    engine_log_shutdown(&logger);
    return 0;
}

static int test_remove_sink_null_inputs(void) {
    printf("Testing LOG-INV-8 (part 5): remove_sink with NULL inputs...\n");

    EngineLogger logger;
    if (engine_log_init(&logger, ENGINE_LOG_LEVEL_TRACE) != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_log_init failed\n");
        return 1;
    }

    CaptureSinkState state = {0};
    EngineLogSink sink = capture_sink_create(&state, ENGINE_LOG_LEVEL_TRACE);

    /* NULL logger. */
    EngineResult res = engine_log_remove_sink(NULL, &sink);
    if (res != ENGINE_RESULT_INVALID_ARGUMENT) {
        fprintf(stderr, "✗ remove_sink(NULL, sink) should return INVALID_ARGUMENT; got %d\n",
                (int)res);
        return 1;
    }
    printf("  ✓ remove_sink(NULL, sink) returns INVALID_ARGUMENT\n");

    /* NULL sink. */
    res = engine_log_remove_sink(&logger, NULL);
    if (res != ENGINE_RESULT_INVALID_ARGUMENT) {
        fprintf(stderr, "✗ remove_sink(logger, NULL) should return INVALID_ARGUMENT; got %d\n",
                (int)res);
        return 1;
    }
    printf("  ✓ remove_sink(logger, NULL) returns INVALID_ARGUMENT\n");

    engine_log_shutdown(&logger);
    return 0;
}

static int test_remove_sink_not_registered(void) {
    printf("Testing LOG-INV-8 (part 6): remove_sink with unregistered pointer...\n");

    EngineLogger logger;
    if (engine_log_init(&logger, ENGINE_LOG_LEVEL_TRACE) != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_log_init failed\n");
        return 1;
    }

    CaptureSinkState state1 = {0};
    CaptureSinkState state2 = {0};
    EngineLogSink sink1 = capture_sink_create(&state1, ENGINE_LOG_LEVEL_TRACE);
    EngineLogSink sink2 = capture_sink_create(&state2, ENGINE_LOG_LEVEL_TRACE);

    /* Register sink1. */
    EngineResult res = engine_log_add_sink(&logger, &sink1);
    if (res != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ add_sink(sink1) failed\n");
        return 1;
    }

    /* Try to remove sink2 (not registered); should return INVALID_STATE. */
    res = engine_log_remove_sink(&logger, &sink2);
    if (res != ENGINE_RESULT_INVALID_STATE) {
        fprintf(stderr, "✗ remove_sink(unregistered) should return INVALID_STATE; got %d\n",
                (int)res);
        return 1;
    }
    printf("  ✓ remove_sink(unregistered pointer) returns INVALID_STATE\n");

    engine_log_shutdown(&logger);
    return 0;
}

static int test_two_identical_sinks(void) {
    printf("Testing LOG-INV-8 (part 7): two structurally identical sinks...\n");

    EngineLogger logger;
    if (engine_log_init(&logger, ENGINE_LOG_LEVEL_TRACE) != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_log_init failed\n");
        return 1;
    }

    CaptureSinkState state1 = {0};
    CaptureSinkState state2 = {0};

    /* Create two structurally identical sinks with different addresses. */
    EngineLogSink sink1 = capture_sink_create(&state1, ENGINE_LOG_LEVEL_TRACE);
    EngineLogSink sink2 = capture_sink_create(&state2, ENGINE_LOG_LEVEL_TRACE);

    /* Register both. */
    if (engine_log_add_sink(&logger, &sink1) != ENGINE_RESULT_SUCCESS ||
        engine_log_add_sink(&logger, &sink2) != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ add_sink failed\n");
        return 1;
    }
    printf("  ✓ Registered two structurally identical sinks at different addresses\n");

    /* Log a record; both should receive it. */
    engine_log_write(&logger, ENGINE_LOG_LEVEL_INFO, "Core", NULL, 0, "test");

    if (state1.count != 1 || state2.count != 1) {
        fprintf(stderr, "✗ Both sinks should receive the record; got %zu and %zu\n",
                state1.count, state2.count);
        return 1;
    }
    printf("  ✓ Both sinks received the record\n");

    /* Remove sink1 by pointer; sink2 should remain registered. */
    if (engine_log_remove_sink(&logger, &sink1) != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ remove_sink(sink1) failed\n");
        return 1;
    }

    /* Log another record; only sink2 should receive it. */
    state1.count = 0;
    state2.count = 0;

    engine_log_write(&logger, ENGINE_LOG_LEVEL_INFO, "Core", NULL, 0, "test2");

    if (state1.count != 0 || state2.count != 1) {
        fprintf(stderr, "✗ After removing sink1, only sink2 should receive; got %zu and %zu\n",
                state1.count, state2.count);
        return 1;
    }
    printf("  ✓ After remove_sink(sink1), only sink2 receives records\n");

    engine_log_shutdown(&logger);
    return 0;
}

/* ============================================================================
 * Main test runner
 * ============================================================================
 */
int main(void) {
    printf("=== Logger Contract Tests ===\n\n");

    int failed = 0;

    failed += test_add_sink_null_inputs();
    printf("\n");

    failed += test_add_sink_out_of_range_level();
    printf("\n");

    failed += test_add_sink_table_full();
    printf("\n");

    failed += test_add_sink_duplicate();
    printf("\n");

    failed += test_remove_sink_null_inputs();
    printf("\n");

    failed += test_remove_sink_not_registered();
    printf("\n");

    failed += test_two_identical_sinks();
    printf("\n");

    if (failed == 0) {
        printf("=== All tests passed ===\n");
        return 0;
    } else {
        printf("=== %d test(s) failed ===\n", failed);
        return 1;
    }
}
