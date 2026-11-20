#include "src/core/logging.h"

#include <stdio.h>

/* Engine logging implementation.
 * See docs/spec/core/logging.md for the full contract.
 *
 * Key rules enforced in this file:
 * - No allocation (CORE-LOG-008).
 * - Message buffer is stack-allocated and call-scoped (CORE-LOG-025).
 * - Timestamp and thread ID read once per record (CORE-LOG-018, CORE-LOG-042).
 * - Five-step evaluation order: null check, level gate, read context,
 *   format message once, deliver to sinks (CORE-LOG-018).
 * - NULL logger tolerance everywhere (CORE-LOG-005).
 */

ENGINE_NODISCARD EngineResult engine_log_init(EngineLogger *logger,
                                              EngineLogLevel min_level) {
    if (logger == NULL) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }

    if ((int32_t)min_level < ENGINE_LOG_LEVEL_TRACE ||
        (int32_t)min_level > ENGINE_LOG_LEVEL_OFF) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }

    logger->sink_count = 0;
    logger->min_level = (int32_t)min_level;
    logger->time_source = NULL;
    logger->thread_id_source = NULL;
    logger->context_user_data = NULL;

    for (size_t i = 0; i < ENGINE_LOG_MAX_SINKS; i++) {
        logger->sinks[i] = NULL;
    }

    return ENGINE_RESULT_SUCCESS;
}

void engine_log_shutdown(EngineLogger *logger) {
    if (logger == NULL) {
        return;
    }

    /* Flush every sink that has a flush function (CORE-LOG-081). */
    for (size_t i = 0; i < logger->sink_count; i++) {
        const EngineLogSink *sink = logger->sinks[i];
        if (sink != NULL && sink->flush != NULL) {
            sink->flush(sink);
        }
    }

    /* Clear the sink table and context sources. */
    logger->sink_count = 0;
    for (size_t i = 0; i < ENGINE_LOG_MAX_SINKS; i++) {
        logger->sinks[i] = NULL;
    }
    logger->time_source = NULL;
    logger->thread_id_source = NULL;
    logger->context_user_data = NULL;
}

ENGINE_NODISCARD EngineResult engine_log_add_sink(EngineLogger *logger,
                                                  const EngineLogSink *sink) {
    if (logger == NULL || sink == NULL) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }

    if (sink->write == NULL) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }

    if ((int32_t)sink->min_level < ENGINE_LOG_LEVEL_TRACE ||
        (int32_t)sink->min_level > ENGINE_LOG_LEVEL_OFF) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }

    /* Check if the table is full. */
    if (logger->sink_count >= ENGINE_LOG_MAX_SINKS) {
        return ENGINE_RESULT_INVALID_STATE;
    }

    /* Check for duplicate pointer (CORE-LOG-036). */
    for (size_t i = 0; i < logger->sink_count; i++) {
        if (logger->sinks[i] == sink) {
            return ENGINE_RESULT_INVALID_STATE;
        }
    }

    logger->sinks[logger->sink_count] = sink;
    logger->sink_count++;

    return ENGINE_RESULT_SUCCESS;
}

ENGINE_NODISCARD EngineResult engine_log_remove_sink(EngineLogger *logger,
                                                     const EngineLogSink *sink) {
    if (logger == NULL || sink == NULL) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }

    /* Find the sink by pointer equality (CORE-LOG-033). */
    for (size_t i = 0; i < logger->sink_count; i++) {
        if (logger->sinks[i] == sink) {
            /* Remove by shifting later sinks down. */
            for (size_t j = i; j < logger->sink_count - 1; j++) {
                logger->sinks[j] = logger->sinks[j + 1];
            }
            logger->sinks[logger->sink_count - 1] = NULL;
            logger->sink_count--;
            return ENGINE_RESULT_SUCCESS;
        }
    }

    return ENGINE_RESULT_INVALID_STATE;
}

ENGINE_NODISCARD EngineResult engine_log_set_level(EngineLogger *logger,
                                                   EngineLogLevel min_level) {
    if (logger == NULL) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }

    if ((int32_t)min_level < ENGINE_LOG_LEVEL_TRACE ||
        (int32_t)min_level > ENGINE_LOG_LEVEL_OFF) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }

    logger->min_level = (int32_t)min_level;
    return ENGINE_RESULT_SUCCESS;
}

EngineLogLevel engine_log_get_level(const EngineLogger *logger) {
    if (logger == NULL) {
        return ENGINE_LOG_LEVEL_OFF;
    }
    return (EngineLogLevel)logger->min_level;
}

ENGINE_NODISCARD EngineResult engine_log_set_context_source(
    EngineLogger *logger,
    EngineLogTimeFn time_source,
    EngineLogThreadIdFn thread_id_source,
    void *user_data) {
    if (logger == NULL) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }

    logger->time_source = time_source;
    logger->thread_id_source = thread_id_source;
    logger->context_user_data = user_data;

    return ENGINE_RESULT_SUCCESS;
}

void engine_log_write(const EngineLogger *logger,
                      EngineLogLevel level,
                      const char *subsystem,
                      const char *file,
                      uint32_t line,
                      const char *format,
                      ...) {
    va_list args;
    va_start(args, format);
    engine_log_write_v(logger, level, subsystem, file, line, format, args);
    va_end(args);
}

void engine_log_write_v(const EngineLogger *logger,
                        EngineLogLevel level,
                        const char *subsystem,
                        const char *file,
                        uint32_t line,
                        const char *format,
                        va_list args) {
    /* CORE-LOG-018 Step 1: Reject NULL logger. */
    if (logger == NULL) {
        return;
    }

    /* CORE-LOG-018 Step 2: Reject level below logger's minimum. */
    if ((int32_t)level < logger->min_level) {
        return;
    }

    /* CORE-LOG-005/CORE-LOG-012: reject OFF as a record level. */
    if (level == ENGINE_LOG_LEVEL_OFF) {
        return;
    }

    /* CORE-LOG-018 Step 3: Read timestamp and thread ID once per record. */
    uint64_t timestamp_ns = ENGINE_LOG_TIMESTAMP_UNKNOWN;
    uint64_t thread_id = ENGINE_LOG_THREAD_UNKNOWN;

    if (logger->time_source != NULL) {
        timestamp_ns = logger->time_source(logger->context_user_data);
    }
    if (logger->thread_id_source != NULL) {
        thread_id = logger->thread_id_source(logger->context_user_data);
    }

    /* CORE-LOG-018 Step 4: Format the message once into a stack buffer. */
    char message_buffer[ENGINE_LOG_MESSAGE_MAX];
    uint32_t flags = 0;

    /* CORE-LOG-012 edge case: subsystem NULL → substitute "?" */
    if (subsystem == NULL) {
        subsystem = "?";
    }

    int formatted = vsnprintf(message_buffer, ENGINE_LOG_MESSAGE_MAX, format, args);

    size_t message_length;
    if (formatted < 0) {
        /* CORE-LOG-012 edge case 4: encoding error → empty message, truncated flag. */
        message_buffer[0] = '\0';
        message_length = 0;
        flags |= ENGINE_LOG_RECORD_TRUNCATED;
    } else if ((size_t)formatted >= ENGINE_LOG_MESSAGE_MAX) {
        /* CORE-LOG-025: truncated, remains NUL-terminated, flag set. */
        message_length = ENGINE_LOG_MESSAGE_MAX - 1;
        flags |= ENGINE_LOG_RECORD_TRUNCATED;
    } else {
        message_length = (size_t)formatted;
    }

    /* Build the record. */
    EngineLogRecord record;
    record.level = level;
    record.subsystem = subsystem;
    record.message = message_buffer;
    record.message_length = message_length;
    record.file = file;
    record.line = line;
    record.flags = flags;
    record.timestamp_ns = timestamp_ns;
    record.thread_id = thread_id;

    /* CORE-LOG-018 Step 5: Deliver to each sink whose level admits it. */
    for (size_t i = 0; i < logger->sink_count; i++) {
        const EngineLogSink *sink = logger->sinks[i];
        if (sink == NULL) {
            continue;
        }

        /* CORE-LOG-017: per-sink level gate. */
        if ((int32_t)level >= sink->min_level) {
            sink->write(sink, &record);
        }
    }
}

void engine_log_flush(const EngineLogger *logger) {
    if (logger == NULL) {
        return;
    }

    /* CORE-LOG-083: call flush on every sink that has one. */
    for (size_t i = 0; i < logger->sink_count; i++) {
        const EngineLogSink *sink = logger->sinks[i];
        if (sink != NULL && sink->flush != NULL) {
            sink->flush(sink);
        }
    }
}

const char *engine_log_level_name(EngineLogLevel level) {
    /* CORE-LOG-014: returns string literal (static storage).
     * Mirror engine_result_name's shape exactly.
     */
    switch (level) {
    case ENGINE_LOG_LEVEL_TRACE:
        return "TRACE";
    case ENGINE_LOG_LEVEL_DEBUG:
        return "DEBUG";
    case ENGINE_LOG_LEVEL_INFO:
        return "INFO";
    case ENGINE_LOG_LEVEL_WARN:
        return "WARN";
    case ENGINE_LOG_LEVEL_ERROR:
        return "ERROR";
    case ENGINE_LOG_LEVEL_FATAL:
        return "FATAL";
    case ENGINE_LOG_LEVEL_OFF:
        return "OFF";
    default:
        return "UNKNOWN";
    }
}
