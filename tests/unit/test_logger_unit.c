#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <assert.h>
#include "src/core/logging.h"

/* ============================================================================
 * Capture Sink — a reusable test helper (LOG-INV-2: user_data state)
 * ============================================================================
 *
 * This sink captures records into a heap-allocated array stored in user_data.
 * CORE-LOG-022: every sink must copy record->message before returning.
 * We do NOT use a file-scope mutable EngineLogSink (CORE-LOG-001).
 */

#define CAPTURE_CAPACITY 64

typedef struct CaptureSinkRecord {
    EngineLogLevel level;
    const char *subsystem;
    char message[ENGINE_LOG_MESSAGE_MAX];
    size_t message_length;
    uint32_t flags;
    uint64_t timestamp_ns;
    uint64_t thread_id;
} CaptureSinkRecord;

typedef struct CaptureSinkState {
    CaptureSinkRecord records[CAPTURE_CAPACITY];
    size_t count;
    uint64_t call_count;  /* For testing time source call count (LOG-INV-12). */
} CaptureSinkState;

/* The capture sink's write callback. CORE-LOG-022: copies message.
 * Matches EngineLogSinkWriteFn signature.
 */
static void capture_sink_write(const EngineLogSink *sink,
                               const EngineLogRecord *record) {
    CaptureSinkState *state = (CaptureSinkState *)sink->user_data;
    if (state == NULL || state->count >= CAPTURE_CAPACITY) {
        return;
    }

    CaptureSinkRecord *captured = &state->records[state->count];
    captured->level = record->level;
    captured->subsystem = record->subsystem;

    /* CORE-LOG-022: COPY the message, never store the pointer. */
    size_t copy_len = record->message_length;
    if (copy_len >= ENGINE_LOG_MESSAGE_MAX) {
        copy_len = ENGINE_LOG_MESSAGE_MAX - 1;
    }
    memcpy(captured->message, record->message, copy_len);
    captured->message[copy_len] = '\0';
    captured->message_length = record->message_length;

    captured->flags = record->flags;
    captured->timestamp_ns = record->timestamp_ns;
    captured->thread_id = record->thread_id;

    state->count++;
}

/* Create a capture sink instance on the stack.
 * The sink struct is caller-owned; state is in user_data.
 * NOT a file-scope mutable (CORE-LOG-001).
 */
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
 * Test LOG-INV-3: engine_log_level_name
 * ============================================================================
 * CORE-LOG-014: returns non-NULL for all seven enumerators including OFF,
 * and for at least one out-of-range value.
 */
static int test_log_level_name(void) {
    printf("Testing LOG-INV-3: engine_log_level_name...\n");

    /* Test all seven levels. */
    const char *names[] = {
        engine_log_level_name(ENGINE_LOG_LEVEL_TRACE),
        engine_log_level_name(ENGINE_LOG_LEVEL_DEBUG),
        engine_log_level_name(ENGINE_LOG_LEVEL_INFO),
        engine_log_level_name(ENGINE_LOG_LEVEL_WARN),
        engine_log_level_name(ENGINE_LOG_LEVEL_ERROR),
        engine_log_level_name(ENGINE_LOG_LEVEL_FATAL),
        engine_log_level_name(ENGINE_LOG_LEVEL_OFF),
    };

    for (int i = 0; i < 7; i++) {
        if (names[i] == NULL) {
            fprintf(stderr, "✗ engine_log_level_name(%d) returned NULL\n", i);
            return 1;
        }
        printf("  ✓ Level %d: \"%s\"\n", i, names[i]);
    }

    /* Test out-of-range value. */
    const char *unknown = engine_log_level_name((EngineLogLevel)999);
    if (unknown == NULL) {
        fprintf(stderr, "✗ engine_log_level_name(999) returned NULL\n");
        return 1;
    }
    printf("  ✓ Out-of-range (999): \"%s\"\n", unknown);

    printf("✓ LOG-INV-3 verified\n");
    return 0;
}

/* ============================================================================
 * Test LOG-INV-4: NULL tolerance
 * ============================================================================
 * CORE-LOG-005: engine_log_write(NULL, ...), engine_log_flush(NULL),
 * engine_log_shutdown(NULL) all return normally and produce no output.
 */
static int test_null_tolerance(void) {
    printf("Testing LOG-INV-4: NULL logger tolerance...\n");

    /* engine_log_write with NULL logger should not crash or output anything.
     * We just verify it returns normally (void function).
     */
    engine_log_write(NULL, ENGINE_LOG_LEVEL_ERROR, "Core", NULL, 0, "test");
    printf("  ✓ engine_log_write(NULL, ...) returned normally\n");

    /* engine_log_flush with NULL logger. */
    engine_log_flush(NULL);
    printf("  ✓ engine_log_flush(NULL) returned normally\n");

    /* engine_log_shutdown with NULL logger. */
    engine_log_shutdown(NULL);
    printf("  ✓ engine_log_shutdown(NULL) returned normally\n");

    printf("✓ LOG-INV-4 verified\n");
    return 0;
}

/* ============================================================================
 * Test LOG-INV-6: Level-based gating
 * ============================================================================
 * A logger at WARN delivers WARN/ERROR/FATAL and rejects TRACE/DEBUG/INFO.
 * A logger at OFF delivers nothing.
 * CORE-LOG-010/011/016.
 *
 * Per Requirement 5: "Build the test so it is not silently vacuous under
 * a compile-time gate: assert the ENGINE_LOG_COMPILE_MIN_LEVEL in effect is 0
 * for this translation unit, or call engine_log_write directly."
 */
static int test_level_gating(void) {
    printf("Testing LOG-INV-6: level-based gating...\n");

    /* Verify compile-time gate is at TRACE for this test (allowing direct calls). */
    if (ENGINE_LOG_COMPILE_MIN_LEVEL != 0) {
        fprintf(stderr, "✗ ENGINE_LOG_COMPILE_MIN_LEVEL is %d, expected 0 for this test\n",
                ENGINE_LOG_COMPILE_MIN_LEVEL);
        return 1;
    }
    printf("  ✓ ENGINE_LOG_COMPILE_MIN_LEVEL is 0 (not vacuous)\n");

    EngineLogger logger;
    if (engine_log_init(&logger, ENGINE_LOG_LEVEL_WARN) != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_log_init failed\n");
        return 1;
    }

    CaptureSinkState state = {0};
    EngineLogSink sink = capture_sink_create(&state, ENGINE_LOG_LEVEL_TRACE);
    if (engine_log_add_sink(&logger, &sink) != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_log_add_sink failed\n");
        return 1;
    }

    /* Call at levels below logger's minimum (should be rejected). */
    engine_log_write(&logger, ENGINE_LOG_LEVEL_TRACE, "Core", NULL, 0, "trace");
    engine_log_write(&logger, ENGINE_LOG_LEVEL_DEBUG, "Core", NULL, 0, "debug");
    engine_log_write(&logger, ENGINE_LOG_LEVEL_INFO, "Core", NULL, 0, "info");

    if (state.count != 0) {
        fprintf(stderr, "✗ TRACE/DEBUG/INFO were not rejected (count=%zu)\n", state.count);
        return 1;
    }
    printf("  ✓ TRACE/DEBUG/INFO rejected (count=0)\n");

    /* Call at levels at or above logger's minimum (should be delivered). */
    engine_log_write(&logger, ENGINE_LOG_LEVEL_WARN, "Core", NULL, 0, "warn");
    engine_log_write(&logger, ENGINE_LOG_LEVEL_ERROR, "Core", NULL, 0, "error");
    engine_log_write(&logger, ENGINE_LOG_LEVEL_FATAL, "Core", NULL, 0, "fatal");

    if (state.count != 3) {
        fprintf(stderr, "✗ WARN/ERROR/FATAL not delivered (count=%zu, expected 3)\n", state.count);
        return 1;
    }
    printf("  ✓ WARN/ERROR/FATAL delivered (count=3)\n");

    /* Test logger at OFF: nothing delivered. */
    engine_log_shutdown(&logger);
    state.count = 0;

    if (engine_log_init(&logger, ENGINE_LOG_LEVEL_OFF) != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_log_init at OFF failed\n");
        return 1;
    }

    if (engine_log_add_sink(&logger, &sink) != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_log_add_sink failed\n");
        return 1;
    }

    engine_log_write(&logger, ENGINE_LOG_LEVEL_FATAL, "Core", NULL, 0, "fatal");
    if (state.count != 0) {
        fprintf(stderr, "✗ FATAL delivered at OFF level (count=%zu)\n", state.count);
        return 1;
    }
    printf("  ✓ OFF delivers nothing\n");

    engine_log_shutdown(&logger);
    printf("✓ LOG-INV-6 verified\n");
    return 0;
}

/* ============================================================================
 * Test LOG-INV-7: Sink-specific level gating and order
 * ============================================================================
 * Two sinks at TRACE and ERROR: one WARN reaches first only.
 * One ERROR reaches both in registration order.
 * CORE-LOG-017/035.
 */
static int test_sink_levels_and_order(void) {
    printf("Testing LOG-INV-7: sink-level gating and order...\n");

    EngineLogger logger;
    if (engine_log_init(&logger, ENGINE_LOG_LEVEL_TRACE) != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_log_init failed\n");
        return 1;
    }

    /* Create two capture sinks with different min levels. */
    CaptureSinkState state1 = {0};
    CaptureSinkState state2 = {0};

    EngineLogSink sink1 = capture_sink_create(&state1, ENGINE_LOG_LEVEL_TRACE);
    EngineLogSink sink2 = capture_sink_create(&state2, ENGINE_LOG_LEVEL_ERROR);

    if (engine_log_add_sink(&logger, &sink1) != ENGINE_RESULT_SUCCESS ||
        engine_log_add_sink(&logger, &sink2) != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_log_add_sink failed\n");
        return 1;
    }

    /* Log a WARN: should reach sink1 (TRACE) but not sink2 (ERROR). */
    engine_log_write(&logger, ENGINE_LOG_LEVEL_WARN, "Core", NULL, 0, "warn");

    if (state1.count != 1 || state2.count != 0) {
        fprintf(stderr, "✗ WARN: expected state1=1, state2=0; got %zu, %zu\n",
                state1.count, state2.count);
        return 1;
    }
    printf("  ✓ WARN reached sink1 (TRACE) but not sink2 (ERROR)\n");

    /* Log an ERROR: should reach both sinks. */
    engine_log_write(&logger, ENGINE_LOG_LEVEL_ERROR, "Core", NULL, 0, "error");

    if (state1.count != 2 || state2.count != 1) {
        fprintf(stderr, "✗ ERROR: expected state1=2, state2=1; got %zu, %zu\n",
                state1.count, state2.count);
        return 1;
    }
    printf("  ✓ ERROR reached both sinks\n");

    /* Verify registration order: state1 should see first, then state2. */
    if (state1.records[0].level != ENGINE_LOG_LEVEL_WARN ||
        state1.records[1].level != ENGINE_LOG_LEVEL_ERROR ||
        state2.records[0].level != ENGINE_LOG_LEVEL_ERROR) {
        fprintf(stderr, "✗ Record levels don't match expected order\n");
        return 1;
    }
    printf("  ✓ Registration order verified\n");

    engine_log_shutdown(&logger);
    printf("✓ LOG-INV-7 verified\n");
    return 0;
}

/* ============================================================================
 * Test LOG-INV-11: Truncation handling
 * ============================================================================
 * A message longer than ENGINE_LOG_MESSAGE_MAX is still delivered,
 * is NUL-terminated, has message_length < ENGINE_LOG_MESSAGE_MAX,
 * and carries ENGINE_LOG_RECORD_TRUNCATED.
 * CORE-LOG-025.
 */
static int test_message_truncation(void) {
    printf("Testing LOG-INV-11: message truncation...\n");

    EngineLogger logger;
    if (engine_log_init(&logger, ENGINE_LOG_LEVEL_TRACE) != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_log_init failed\n");
        return 1;
    }

    CaptureSinkState state = {0};
    EngineLogSink sink = capture_sink_create(&state, ENGINE_LOG_LEVEL_TRACE);
    if (engine_log_add_sink(&logger, &sink) != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_log_add_sink failed\n");
        return 1;
    }

    /* Log a message that will be truncated. Format string produces ~2000 chars. */
    char big_format[256];
    snprintf(big_format, sizeof(big_format),
             "%%0%du", (unsigned)(ENGINE_LOG_MESSAGE_MAX + 100));

    engine_log_write(&logger, ENGINE_LOG_LEVEL_INFO, "Core", NULL, 0,
                     big_format, 0);

    if (state.count != 1) {
        fprintf(stderr, "✗ Truncated message not delivered (count=%zu)\n", state.count);
        return 1;
    }
    printf("  ✓ Truncated message delivered\n");

    CaptureSinkRecord *record = &state.records[0];

    /* Verify NUL-termination. */
    if (record->message[record->message_length] != '\0') {
        fprintf(stderr, "✗ Message not NUL-terminated at position %zu\n",
                record->message_length);
        return 1;
    }
    printf("  ✓ Message NUL-terminated at position %zu\n", record->message_length);

    /* Verify message_length < ENGINE_LOG_MESSAGE_MAX. */
    if (record->message_length >= ENGINE_LOG_MESSAGE_MAX) {
        fprintf(stderr, "✗ message_length (%zu) >= MESSAGE_MAX (%zu)\n",
                record->message_length, ENGINE_LOG_MESSAGE_MAX);
        return 1;
    }
    printf("  ✓ message_length (%zu) < MESSAGE_MAX (%zu)\n",
           record->message_length, ENGINE_LOG_MESSAGE_MAX);

    /* Verify TRUNCATED flag. */
    if (!(record->flags & ENGINE_LOG_RECORD_TRUNCATED)) {
        fprintf(stderr, "✗ ENGINE_LOG_RECORD_TRUNCATED flag not set\n");
        return 1;
    }
    printf("  ✓ ENGINE_LOG_RECORD_TRUNCATED flag set\n");

    engine_log_shutdown(&logger);
    printf("✓ LOG-INV-11 verified\n");
    return 0;
}

/* ============================================================================
 * Test LOG-INV-12: Context sources and call count
 * ============================================================================
 * With no context source: records carry TIMESTAMP_UNKNOWN and THREAD_UNKNOWN.
 * With an injected source: two sinks receive identical timestamps,
 * and the source is called exactly once per record.
 * CORE-LOG-040/042. This is the critical assertion proving CORE-LOG-018's
 * "steps 3 and 4 happen once per record, not per sink".
 */

typedef struct TimeSourceState {
    uint64_t call_count;
    uint64_t next_timestamp;
} TimeSourceState;

static uint64_t test_time_source(void *user_data) {
    TimeSourceState *state = (TimeSourceState *)user_data;
    state->call_count++;
    return state->next_timestamp++;
}

static int test_context_sources(void) {
    printf("Testing LOG-INV-12: context sources and call count...\n");

    EngineLogger logger;
    if (engine_log_init(&logger, ENGINE_LOG_LEVEL_TRACE) != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_log_init failed\n");
        return 1;
    }

    /* Test 1: No context source installed. */
    printf("  Part 1: No context source\n");

    CaptureSinkState state = {0};
    EngineLogSink sink = capture_sink_create(&state, ENGINE_LOG_LEVEL_TRACE);
    if (engine_log_add_sink(&logger, &sink) != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_log_add_sink failed\n");
        return 1;
    }

    engine_log_write(&logger, ENGINE_LOG_LEVEL_INFO, "Core", NULL, 0, "test");

    if (state.count != 1) {
        fprintf(stderr, "✗ Record not captured (count=%zu)\n", state.count);
        return 1;
    }

    if (state.records[0].timestamp_ns != ENGINE_LOG_TIMESTAMP_UNKNOWN) {
        fprintf(stderr, "✗ timestamp_ns should be UNKNOWN; got %" PRIu64 "\n",
                state.records[0].timestamp_ns);
        return 1;
    }
    printf("    ✓ No source: timestamp_ns == UNKNOWN\n");

    if (state.records[0].thread_id != ENGINE_LOG_THREAD_UNKNOWN) {
        fprintf(stderr, "✗ thread_id should be UNKNOWN; got %" PRIu64 "\n",
                state.records[0].thread_id);
        return 1;
    }
    printf("    ✓ No source: thread_id == UNKNOWN\n");

    /* Test 2: Install a deterministic time source; verify it's called exactly once. */
    printf("  Part 2: Injected time source\n");

    engine_log_shutdown(&logger);
    if (engine_log_init(&logger, ENGINE_LOG_LEVEL_TRACE) != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_log_init failed\n");
        return 1;
    }

    /* Create two capture sinks. */
    CaptureSinkState state1 = {0};
    CaptureSinkState state2 = {0};

    EngineLogSink sink1 = capture_sink_create(&state1, ENGINE_LOG_LEVEL_TRACE);
    EngineLogSink sink2 = capture_sink_create(&state2, ENGINE_LOG_LEVEL_TRACE);

    if (engine_log_add_sink(&logger, &sink1) != ENGINE_RESULT_SUCCESS ||
        engine_log_add_sink(&logger, &sink2) != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_log_add_sink failed\n");
        return 1;
    }

    /* Install the time source. */
    TimeSourceState time_state = {.call_count = 0, .next_timestamp = 1000};
    if (engine_log_set_context_source(&logger, test_time_source, NULL, &time_state) !=
        ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_log_set_context_source failed\n");
        return 1;
    }

    /* Log one record. The time source should be called exactly once. */
    engine_log_write(&logger, ENGINE_LOG_LEVEL_INFO, "Core", NULL, 0, "test");

    if (state1.count != 1 || state2.count != 1) {
        fprintf(stderr, "✗ Records not captured by both sinks\n");
        return 1;
    }

    /* Verify both sinks see the SAME timestamp. */
    if (state1.records[0].timestamp_ns != state2.records[0].timestamp_ns) {
        fprintf(stderr, "✗ Sinks see different timestamps: %" PRIu64 " vs %" PRIu64 "\n",
                state1.records[0].timestamp_ns, state2.records[0].timestamp_ns);
        return 1;
    }
    printf("    ✓ Both sinks see identical timestamp: %" PRIu64 "\n",
           state1.records[0].timestamp_ns);

    /* Verify the time source was called exactly once. */
    if (time_state.call_count != 1) {
        fprintf(stderr, "✗ Time source called %llu times, expected 1\n",
                (unsigned long long)time_state.call_count);
        return 1;
    }
    printf("    ✓ Time source called exactly once\n");

    engine_log_shutdown(&logger);
    printf("✓ LOG-INV-12 verified\n");
    return 0;
}

/* ============================================================================
 * Test LOG-INV-14: Console sink singleton
 * ============================================================================
 * engine_console_log_sink() never returns NULL, returns the identical pointer
 * on repeated calls, and its write is non-NULL while its user_data is NULL.
 * CORE-LOG-050/051. The analogue of ALLOC-INV-3.
 */
static int test_console_sink_singleton(void) {
    printf("Testing LOG-INV-14: console sink singleton...\n");

    const EngineLogSink *sink1 = engine_console_log_sink();
    const EngineLogSink *sink2 = engine_console_log_sink();

    if (sink1 == NULL) {
        fprintf(stderr, "✗ engine_console_log_sink() returned NULL\n");
        return 1;
    }
    printf("  ✓ engine_console_log_sink() never returns NULL\n");

    if (sink1 != sink2) {
        fprintf(stderr, "✗ engine_console_log_sink() returned different pointers\n");
        return 1;
    }
    printf("  ✓ engine_console_log_sink() returns identical pointer\n");

    if (sink1->write == NULL) {
        fprintf(stderr, "✗ console sink's write is NULL\n");
        return 1;
    }
    printf("  ✓ console sink's write is non-NULL\n");

    if (sink1->user_data != NULL) {
        fprintf(stderr, "✗ console sink's user_data is non-NULL\n");
        return 1;
    }
    printf("  ✓ console sink's user_data is NULL\n");

    printf("✓ LOG-INV-14 verified\n");
    return 0;
}

/* ============================================================================
 * Main test runner
 * ============================================================================
 */
int main(void) {
    printf("=== Logger Unit Tests ===\n\n");

    int failed = 0;

    failed += test_log_level_name();
    printf("\n");

    failed += test_null_tolerance();
    printf("\n");

    failed += test_level_gating();
    printf("\n");

    failed += test_sink_levels_and_order();
    printf("\n");

    failed += test_message_truncation();
    printf("\n");

    failed += test_context_sources();
    printf("\n");

    failed += test_console_sink_singleton();
    printf("\n");

    if (failed == 0) {
        printf("=== All tests passed ===\n");
        return 0;
    } else {
        printf("=== %d test(s) failed ===\n", failed);
        return 1;
    }
}
