/* Logger unit tests: LOG-INV-3, 4, 6, 7, 11, 12, 14.
 * Every call goes to engine_log_write directly (never through a gated
 * ENGINE_LOG_* macro), so runtime behavior is what is under test whatever
 * ENGINE_LOG_COMPILE_MIN_LEVEL the build uses. */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "src/core/logging.h"
#include "tests/check.h"

/* Capture sink: records every delivered record. All state lives in user_data
 * (never a file-scope mutable sink), and the message is COPIED because the
 * record's pointer is call-scoped (CORE-LOG-022). */
#define CAPTURE_CAPACITY 8

typedef struct {
    EngineLogLevel level;
    char message[ENGINE_LOG_MESSAGE_MAX];
    size_t message_length;
    uint32_t flags;
    uint64_t timestamp_ns;
    uint64_t thread_id;
} Captured;

typedef struct {
    Captured records[CAPTURE_CAPACITY];
    size_t count;
} Capture;

static void capture_write(const EngineLogSink *sink, const EngineLogRecord *record) {
    Capture *cap = sink->user_data;
    if (cap->count >= CAPTURE_CAPACITY) {
        return;
    }
    Captured *c = &cap->records[cap->count++];
    c->level = record->level;
    c->message_length = record->message_length;
    memcpy(c->message, record->message, record->message_length);
    c->message[record->message_length] = '\0';
    c->flags = record->flags;
    c->timestamp_ns = record->timestamp_ns;
    c->thread_id = record->thread_id;
}

static EngineLogSink capture_sink(Capture *cap, EngineLogLevel min_level) {
    return (EngineLogSink){.write = capture_write, .user_data = cap, .min_level = min_level};
}

/* LOG-INV-3: a name for every level, and for an out-of-range value. */
static int test_level_name(void) {
    for (int level = ENGINE_LOG_LEVEL_TRACE; level <= ENGINE_LOG_LEVEL_OFF; level++) {
        CHECK(engine_log_level_name((EngineLogLevel)level) != NULL, "level %d has no name", level);
    }
    CHECK(engine_log_level_name((EngineLogLevel)999) != NULL, "out-of-range level has no name");
    return 0;
}

/* LOG-INV-4: a NULL logger is accepted everywhere and discards. */
static int test_null_tolerance(void) {
    engine_log_write(NULL, ENGINE_LOG_LEVEL_ERROR, "Core", NULL, 0, "x");
    ENGINE_LOG_ERROR(NULL, "Core", "x");
    engine_log_flush(NULL);
    engine_log_shutdown(NULL);
    return 0;
}

/* LOG-INV-6: runtime level gating. */
static int test_level_gating(void) {
    EngineLogger logger;
    Capture cap = {0};
    EngineLogSink sink = capture_sink(&cap, ENGINE_LOG_LEVEL_TRACE);

    CHECK_OK(engine_log_init(&logger, ENGINE_LOG_LEVEL_WARN));
    CHECK_OK(engine_log_add_sink(&logger, &sink));

    engine_log_write(&logger, ENGINE_LOG_LEVEL_TRACE, "Core", NULL, 0, "trace");
    engine_log_write(&logger, ENGINE_LOG_LEVEL_DEBUG, "Core", NULL, 0, "debug");
    engine_log_write(&logger, ENGINE_LOG_LEVEL_INFO, "Core", NULL, 0, "info");
    CHECK(cap.count == 0, "TRACE/DEBUG/INFO delivered at WARN (count=%zu)", cap.count);

    engine_log_write(&logger, ENGINE_LOG_LEVEL_WARN, "Core", NULL, 0, "warn");
    engine_log_write(&logger, ENGINE_LOG_LEVEL_ERROR, "Core", NULL, 0, "error");
    engine_log_write(&logger, ENGINE_LOG_LEVEL_FATAL, "Core", NULL, 0, "fatal");
    CHECK(cap.count == 3, "WARN/ERROR/FATAL not delivered (count=%zu)", cap.count);

    engine_log_shutdown(&logger);
    cap.count = 0;
    CHECK_OK(engine_log_init(&logger, ENGINE_LOG_LEVEL_OFF));
    CHECK_OK(engine_log_add_sink(&logger, &sink));
    engine_log_write(&logger, ENGINE_LOG_LEVEL_FATAL, "Core", NULL, 0, "fatal");
    CHECK(cap.count == 0, "FATAL delivered at OFF (count=%zu)", cap.count);

    engine_log_shutdown(&logger);
    return 0;
}

/* LOG-INV-7: per-sink gating, and delivery in registration order. */
static int test_sink_levels_and_order(void) {
    EngineLogger logger;
    Capture low = {0}, high = {0};
    EngineLogSink sink_low = capture_sink(&low, ENGINE_LOG_LEVEL_TRACE);
    EngineLogSink sink_high = capture_sink(&high, ENGINE_LOG_LEVEL_ERROR);

    CHECK_OK(engine_log_init(&logger, ENGINE_LOG_LEVEL_TRACE));
    CHECK_OK(engine_log_add_sink(&logger, &sink_low));
    CHECK_OK(engine_log_add_sink(&logger, &sink_high));

    engine_log_write(&logger, ENGINE_LOG_LEVEL_WARN, "Core", NULL, 0, "warn");
    CHECK(low.count == 1 && high.count == 0, "WARN: expected 1/0, got %zu/%zu", low.count, high.count);

    engine_log_write(&logger, ENGINE_LOG_LEVEL_ERROR, "Core", NULL, 0, "error");
    CHECK(low.count == 2 && high.count == 1, "ERROR: expected 2/1, got %zu/%zu", low.count, high.count);

    CHECK(low.records[0].level == ENGINE_LOG_LEVEL_WARN &&
          low.records[1].level == ENGINE_LOG_LEVEL_ERROR &&
          high.records[0].level == ENGINE_LOG_LEVEL_ERROR,
          "records out of order");

    engine_log_shutdown(&logger);
    return 0;
}

/* LOG-INV-11: an over-long message is truncated, never dropped. */
static int test_truncation(void) {
    EngineLogger logger;
    Capture cap = {0};
    EngineLogSink sink = capture_sink(&cap, ENGINE_LOG_LEVEL_TRACE);
    char format[32];

    CHECK_OK(engine_log_init(&logger, ENGINE_LOG_LEVEL_TRACE));
    CHECK_OK(engine_log_add_sink(&logger, &sink));

    /* "%0<N>u" prints a number zero-padded to N digits: longer than the buffer. */
    snprintf(format, sizeof(format), "%%0%zuu", (size_t)ENGINE_LOG_MESSAGE_MAX + 100);
    engine_log_write(&logger, ENGINE_LOG_LEVEL_INFO, "Core", NULL, 0, format, 0u);

    CHECK(cap.count == 1, "truncated message not delivered (count=%zu)", cap.count);
    const Captured *r = &cap.records[0];
    CHECK(r->message_length < ENGINE_LOG_MESSAGE_MAX, "message_length %zu >= MESSAGE_MAX", r->message_length);
    CHECK(r->message[r->message_length] == '\0', "message not NUL-terminated");
    CHECK(r->flags & ENGINE_LOG_RECORD_TRUNCATED, "TRUNCATED flag not set");

    engine_log_shutdown(&logger);
    return 0;
}

typedef struct {
    uint64_t call_count;
    uint64_t next_timestamp;
} TimeSource;

static uint64_t time_source(void *user_data) {
    TimeSource *ts = user_data;
    ts->call_count++;
    return ts->next_timestamp++;
}

/* LOG-INV-12: with no source, records carry the "unknown" sentinels; with a
 * source, it is read exactly once per record and every sink sees the same
 * value (CORE-LOG-018: once per record, not once per sink). */
static int test_context_sources(void) {
    EngineLogger logger;
    Capture a = {0}, b = {0};
    EngineLogSink sink_a = capture_sink(&a, ENGINE_LOG_LEVEL_TRACE);
    EngineLogSink sink_b = capture_sink(&b, ENGINE_LOG_LEVEL_TRACE);
    TimeSource ts = {.call_count = 0, .next_timestamp = 1000};

    CHECK_OK(engine_log_init(&logger, ENGINE_LOG_LEVEL_TRACE));
    CHECK_OK(engine_log_add_sink(&logger, &sink_a));
    engine_log_write(&logger, ENGINE_LOG_LEVEL_INFO, "Core", NULL, 0, "x");
    CHECK(a.count == 1, "record not captured");
    CHECK(a.records[0].timestamp_ns == ENGINE_LOG_TIMESTAMP_UNKNOWN, "timestamp should be UNKNOWN");
    CHECK(a.records[0].thread_id == ENGINE_LOG_THREAD_UNKNOWN, "thread_id should be UNKNOWN");

    engine_log_shutdown(&logger);
    a.count = 0;
    CHECK_OK(engine_log_init(&logger, ENGINE_LOG_LEVEL_TRACE));
    CHECK_OK(engine_log_add_sink(&logger, &sink_a));
    CHECK_OK(engine_log_add_sink(&logger, &sink_b));
    CHECK_OK(engine_log_set_context_source(&logger, time_source, NULL, &ts));

    engine_log_write(&logger, ENGINE_LOG_LEVEL_INFO, "Core", NULL, 0, "x");
    CHECK(a.count == 1 && b.count == 1, "record not captured by both sinks");
    CHECK(a.records[0].timestamp_ns == b.records[0].timestamp_ns,
          "sinks saw different timestamps: %" PRIu64 " vs %" PRIu64,
          a.records[0].timestamp_ns, b.records[0].timestamp_ns);
    CHECK(ts.call_count == 1, "time source called %" PRIu64 " times, expected 1", ts.call_count);

    engine_log_shutdown(&logger);
    return 0;
}

/* LOG-INV-14: the console sink is a stable, stateless singleton. */
static int test_console_sink(void) {
    const EngineLogSink *sink = engine_console_log_sink();

    CHECK(sink != NULL, "console sink is NULL");
    CHECK(sink == engine_console_log_sink(), "console sink pointer is not stable");
    CHECK(sink->write != NULL, "console sink write is NULL");
    CHECK(sink->user_data == NULL, "console sink user_data is not NULL");
    return 0;
}

int main(void) {
    static const TestCase tests[] = {
        {"LOG-INV-3  level names", test_level_name},
        {"LOG-INV-4  NULL tolerance", test_null_tolerance},
        {"LOG-INV-6  level gating", test_level_gating},
        {"LOG-INV-7  sink levels and order", test_sink_levels_and_order},
        {"LOG-INV-11 truncation", test_truncation},
        {"LOG-INV-12 context sources", test_context_sources},
        {"LOG-INV-14 console sink", test_console_sink},
    };

    return RUN_TESTS(tests);
}
