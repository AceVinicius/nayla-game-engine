#ifndef ENGINE_LOGGING_H
#define ENGINE_LOGGING_H

/* Portable diagnostic logging facility for the entire engine.
 * See docs/spec/core/logging.md for the full contract.
 *
 * Key invariants:
 * - No global logger; every macro takes a logger as its first argument.
 * - logger == NULL is valid and means discard (CORE-LOG-005).
 * - No allocation anywhere; fixed-capacity sink table (CORE-LOG-008).
 * - Two-stage filtering: compile-time and runtime (CORE-LOG-015/016).
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "src/core/result.h"

/* Compiler-dependent printf format checking.
 * Enables compile-time validation that format strings match arguments.
 * CORE-LOG-065: format is the 6th parameter, varargs the 7th.
 */
#if defined(__GNUC__) || defined(__clang__)
    #define ENGINE_PRINTF_FORMAT(fmt_pos, args_pos) \
        __attribute__((format(printf, fmt_pos, args_pos)))
#else
    #define ENGINE_PRINTF_FORMAT(fmt_pos, args_pos)
#endif

/* Compile-time minimum log level: levels below this produce no code.
 * CORE-LOG-015: defaults to TRACE (0) when NDEBUG is undefined,
 * INFO (2) when NDEBUG is defined. Overridable per build.
 */
#ifndef ENGINE_LOG_COMPILE_MIN_LEVEL
    #ifdef NDEBUG
        #define ENGINE_LOG_COMPILE_MIN_LEVEL 2
    #else
        #define ENGINE_LOG_COMPILE_MIN_LEVEL 0
    #endif
#endif

/* Source location inclusion: 1 includes __FILE__ and __LINE__, 0 omits.
 * CORE-LOG-024: defaults to 1 when NDEBUG undefined, 0 when defined.
 * Overridable per build.
 */
#ifndef ENGINE_LOG_SOURCE_LOCATION
    #ifdef NDEBUG
        #define ENGINE_LOG_SOURCE_LOCATION 0
    #else
        #define ENGINE_LOG_SOURCE_LOCATION 1
    #endif
#endif

/* Fixed capacity of the sink table (CORE-LOG-034).
 * Eight covers the common cases: console, rotating file, ring, IDE,
 * remote, test capture, and two spares.
 */
#define ENGINE_LOG_MAX_SINKS ((size_t)8)

/* Message buffer size, including terminator (CORE-LOG-025).
 * Large enough for device capability lines or shader error excerpts,
 * small enough that a per-call stack buffer is unremarkable.
 */
#define ENGINE_LOG_MESSAGE_MAX ((size_t)1024)

/* Log record flags (CORE-LOG-020, CORE-TYPES-012: uint32_t, not bool).
 */
#define ENGINE_LOG_RECORD_TRUNCATED ((uint32_t)0x00000001u)

/* Sentinel values for unavailable timestamp and thread ID (CORE-LOG-040).
 * CORE-LOG-025/040: sinks MUST render these as "unknown", not 0 or a number.
 */
#define ENGINE_LOG_TIMESTAMP_UNKNOWN UINT64_MAX
#define ENGINE_LOG_THREAD_UNKNOWN ((uint64_t)0)

/* Log severity levels, in ascending order.
 * CORE-LOG-010: TRACE < DEBUG < INFO < WARN < ERROR < FATAL < OFF.
 * CORE-LOG-012: numeric values are fixed and asserted; reordering or
 * renumbering is a spec amendment.
 * CORE-LOG-011: OFF is a filter sentinel only, never a record value.
 */
typedef enum EngineLogLevel {
    ENGINE_LOG_LEVEL_TRACE = 0,
    ENGINE_LOG_LEVEL_DEBUG = 1,
    ENGINE_LOG_LEVEL_INFO = 2,
    ENGINE_LOG_LEVEL_WARN = 3,
    ENGINE_LOG_LEVEL_ERROR = 4,
    ENGINE_LOG_LEVEL_FATAL = 5,
    ENGINE_LOG_LEVEL_OFF = 6
} EngineLogLevel;

/* LOG-INV-1: Level values are exactly as stated.
 * This binds the preprocessor's integer comparisons (CORE-LOG-060)
 * to the enumerator values so they can never drift.
 */
static_assert(ENGINE_LOG_LEVEL_TRACE == 0, "TRACE must be 0");
static_assert(ENGINE_LOG_LEVEL_DEBUG == 1, "DEBUG must be 1");
static_assert(ENGINE_LOG_LEVEL_INFO == 2, "INFO must be 2");
static_assert(ENGINE_LOG_LEVEL_WARN == 3, "WARN must be 3");
static_assert(ENGINE_LOG_LEVEL_ERROR == 4, "ERROR must be 4");
static_assert(ENGINE_LOG_LEVEL_FATAL == 5, "FATAL must be 5");
static_assert(ENGINE_LOG_LEVEL_OFF == 6, "OFF must be 6");

/* LOG-INV-2: Size constraints and capacity checks. */
static_assert(sizeof(EngineLogLevel) == sizeof(int32_t),
              "EngineLogLevel must be exactly 32 bits");
static_assert(ENGINE_LOG_MESSAGE_MAX >= 256,
              "MESSAGE_MAX must be at least 256 bytes");
static_assert(ENGINE_LOG_MAX_SINKS >= 1,
              "MAX_SINKS must be at least 1");

/* Callback for obtaining a monotonic timestamp.
 * CORE-LOG-043: source MUST return nanoseconds, MUST be monotonic
 * non-decreasing, MUST NOT return ENGINE_LOG_TIMESTAMP_UNKNOWN.
 */
typedef uint64_t (*EngineLogTimeFn)(void *user_data);

/* Callback for obtaining a thread identifier.
 * CORE-LOG-043: source MUST return a value stable per thread
 * and distinct between concurrently live threads,
 * MUST NOT return ENGINE_LOG_THREAD_UNKNOWN.
 */
typedef uint64_t (*EngineLogThreadIdFn)(void *user_data);

/* A single formatted log record delivered to sinks.
 * CORE-LOG-020: field types are fixed; no bool in this struct.
 * CORE-LOG-022: message pointer is call-scoped; sinks must copy to retain.
 * CORE-LOG-027: records are values, not objects; no lifetime beyond the call.
 */
typedef struct EngineLogRecord {
    EngineLogLevel  level;              /* never ENGINE_LOG_LEVEL_OFF */
    const char     *subsystem;          /* non-NULL, static storage, NUL-terminated UTF-8 */
    const char     *message;            /* non-NULL, NUL-terminated UTF-8, CALL-SCOPED */
    size_t          message_length;     /* bytes before the NUL */
    const char     *file;               /* static storage, or NULL when unavailable */
    uint32_t        line;               /* 0 when unavailable */
    uint32_t        flags;              /* ENGINE_LOG_RECORD_* bit flags */
    uint64_t        timestamp_ns;       /* ENGINE_LOG_TIMESTAMP_UNKNOWN when unavailable */
    uint64_t        thread_id;          /* ENGINE_LOG_THREAD_UNKNOWN when unavailable */
} EngineLogRecord;

/* Forward declaration for the sink's write callback. */
typedef struct EngineLogSink EngineLogSink;

/* Sink callback invoked for each record at or above the sink's level.
 * CORE-LOG-031: write is required (never NULL), flush is optional.
 * CORE-LOG-038: sink MUST NOT log, block indefinitely, or retain message.
 * CORE-LOG-039: no failure mode; write returns void.
 */
typedef void (*EngineLogSinkWriteFn)(const EngineLogSink *sink,
                                     const EngineLogRecord *record);

/* Sink callback to flush buffered output. Optional, may be NULL.
 */
typedef void (*EngineLogSinkFlushFn)(const EngineLogSink *sink);

/* A log sink: a consumer of formatted records.
 * CORE-LOG-030: mirrors EngineAllocator's structure: function pointers + user_data.
 * CORE-LOG-032: min_level is int32_t, not EngineLogLevel, for layout stability.
 * CORE-LOG-033: sinks are caller-owned and borrowed; stored by pointer identity.
 */
struct EngineLogSink {
    EngineLogSinkWriteFn  write;        /* required, never NULL */
    EngineLogSinkFlushFn  flush;        /* optional, may be NULL */
    void                 *user_data;    /* sink-owned, opaque to the engine */
    int32_t               min_level;    /* an EngineLogLevel; immutable after registration */
};

/* A logger: a caller-placed, fixed-capacity collection of sinks.
 * CORE-LOG-002: owned by whoever defined it; engine never allocates/frees.
 * CORE-LOG-008: defined struct with fixed capacity; no allocation.
 * CORE-LOG-034: sink table is fixed-size inside the logger.
 * CORE-LOG-063: min_level is readable directly by engine_log_is_enabled.
 */
typedef struct EngineLogger {
    const EngineLogSink  *sinks[ENGINE_LOG_MAX_SINKS];
    size_t                sink_count;
    int32_t               min_level;
    EngineLogTimeFn       time_source;
    EngineLogThreadIdFn   thread_id_source;
    void                 *context_user_data;
} EngineLogger;

/* Initialize a logger.
 * CORE-LOG-080: sets level, empties sink table, clears context sources.
 * Returns ENGINE_RESULT_INVALID_ARGUMENT for a NULL logger or a level
 * outside [TRACE, OFF] (CORE-LOG-080).
 * CORE-LOG-008: returns no allocation error (none can occur).
 * EXTERNALLY_SYNCHRONIZED: must not run concurrently with any logging.
 */
ENGINE_NODISCARD EngineResult engine_log_init(EngineLogger *logger,
                                              EngineLogLevel min_level);

/* Shut down a logger.
 * CORE-LOG-081: flushes every sink with a flush function, then clears table.
 * CORE-LOG-005: NULL logger is a no-op.
 * CORE-RESULT-017: returns void (no failure mode).
 * EXTERNALLY_SYNCHRONIZED: must not run concurrently with any logging.
 */
void engine_log_shutdown(EngineLogger *logger);

/* Register a sink.
 * CORE-LOG-036: validates logger, sink, sink->write, and min_level.
 * Returns ENGINE_RESULT_INVALID_ARGUMENT for NULL/invalid inputs,
 * ENGINE_RESULT_INVALID_STATE for full table or duplicate pointer.
 * EXTERNALLY_SYNCHRONIZED: must not run concurrently with any logging.
 */
ENGINE_NODISCARD EngineResult engine_log_add_sink(EngineLogger *logger,
                                                  const EngineLogSink *sink);

/* Unregister a sink.
 * CORE-LOG-037: searches for exact pointer match; does NOT flush.
 * Returns ENGINE_RESULT_INVALID_ARGUMENT for NULL inputs,
 * ENGINE_RESULT_INVALID_STATE if not registered.
 * EXTERNALLY_SYNCHRONIZED: must not run concurrently with any logging.
 */
ENGINE_NODISCARD EngineResult engine_log_remove_sink(EngineLogger *logger,
                                                     const EngineLogSink *sink);

/* Set the logger's runtime minimum level.
 * CORE-LOG-006/016: runtime gate after compile-time; readable via min_level field.
 * Returns ENGINE_RESULT_INVALID_ARGUMENT for NULL logger or out-of-range level,
 * otherwise ENGINE_RESULT_SUCCESS.
 * NOTE: Return type (ENGINE_NODISCARD EngineResult) follows CORE-LOG-080's
 * precedent, validating level against [TRACE, OFF]. If review disagrees,
 * that is a spec-writer question (CLAUDE.md § Escalation Rules).
 * EXTERNALLY_SYNCHRONIZED: must not run concurrently with any logging.
 */
ENGINE_NODISCARD EngineResult engine_log_set_level(EngineLogger *logger,
                                                   EngineLogLevel min_level);

/* Get the logger's current runtime minimum level.
 * EXTERNALLY_SYNCHRONIZED against engine_log_set_level: min_level is a plain
 * int32_t, not an atomic (CORE-LOG-072/073).
 */
EngineLogLevel engine_log_get_level(const EngineLogger *logger);

/* Install optional context sources for timestamps and thread IDs.
 * CORE-LOG-040: both default to absent (NULL).
 * CORE-LOG-042: called at most once per admitted record.
 * Returns ENGINE_RESULT_INVALID_ARGUMENT for NULL logger,
 * ENGINE_RESULT_SUCCESS otherwise.
 * EXTERNALLY_SYNCHRONIZED: must not run concurrently with any logging.
 */
ENGINE_NODISCARD EngineResult engine_log_set_context_source(
    EngineLogger *logger,
    EngineLogTimeFn time_source,
    EngineLogThreadIdFn thread_id_source,
    void *user_data);

/* Check if a record at the given level would be delivered.
 * CORE-LOG-063: static inline to avoid call overhead; reads min_level directly.
 * Returns (logger != NULL) && ((int32_t)level >= logger->min_level).
 * Used by the logging macros to gate argument evaluation.
 */
static inline bool engine_log_is_enabled(const EngineLogger *logger,
                                         EngineLogLevel level) {
    return logger != NULL && (int32_t)level >= logger->min_level;
}

/* Emit a formatted log record.
 * CORE-LOG-065: signature pinned by spec; format is 6th param, varargs 7th.
 * CORE-LOG-018: executes five steps: null check, level gate, read context,
 * format message (once), deliver to sinks (once per sink).
 * CORE-LOG-005: NULL logger is accepted and discards.
 * CORE-LOG-039: returns void (no failure mode).
 * THREAD_SAFE iff every registered sink is THREAD_SAFE.
 */
void engine_log_write(const EngineLogger *logger,
                      EngineLogLevel level,
                      const char *subsystem,
                      const char *file,
                      uint32_t line,
                      const char *format,
                      ...) ENGINE_PRINTF_FORMAT(6, 7);

/* Variant of engine_log_write taking a va_list.
 * CORE-LOG-067: used by subsystem-specific logging wrappers.
 * THREAD_SAFE iff every registered sink is THREAD_SAFE.
 */
void engine_log_write_v(const EngineLogger *logger,
                        EngineLogLevel level,
                        const char *subsystem,
                        const char *file,
                        uint32_t line,
                        const char *format,
                        va_list args);

/* Flush all registered sinks.
 * CORE-LOG-083: calls flush on each sink that has one, in order.
 * CORE-LOG-005: NULL logger is a no-op.
 * CORE-RESULT-017: returns void.
 * THREAD_SAFE iff every registered sink is THREAD_SAFE.
 */
void engine_log_flush(const EngineLogger *logger);

/* Return the name of a log level for diagnostics.
 * CORE-LOG-014: returns "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "OFF",
 * or "UNKNOWN" for out-of-range values.
 * Never returns NULL; returned pointer has static storage duration.
 */
const char *engine_log_level_name(EngineLogLevel level);

/* Accessor for the built-in console sink.
 * CORE-LOG-050/051: returns a pointer to a static const EngineLogSink
 * that writes to the standard error stream.
 * THREAD_SAFE: never NULL, identical pointer on every call, callable anytime.
 */
const EngineLogSink *engine_console_log_sink(void);

/* ----- The six logging macros -----
 * CORE-LOG-060/061/062: each is selected by #if on literal level numbers
 * (never enumerator names, which the preprocessor cannot see). Enabled, it
 * expands to a do-while statement; below ENGINE_LOG_COMPILE_MIN_LEVEL it
 * expands to ((void)0), so the call and its arguments are never compiled.
 * CORE-LOG-026: format string MUST be a literal.
 */
#define ENGINE_LOG_AT_(level, logger, subsystem, ...) \
    do { \
        if (engine_log_is_enabled((logger), (level))) { \
            engine_log_write((logger), (level), (subsystem), \
                             ENGINE_LOG_SOURCE_LOCATION ? __FILE__ : NULL, \
                             ENGINE_LOG_SOURCE_LOCATION ? __LINE__ : 0, \
                             __VA_ARGS__); \
        } \
    } while (0)

#if 0 >= ENGINE_LOG_COMPILE_MIN_LEVEL
    #define ENGINE_LOG_TRACE(logger, subsystem, ...) \
        ENGINE_LOG_AT_(ENGINE_LOG_LEVEL_TRACE, logger, subsystem, __VA_ARGS__)
#else
    #define ENGINE_LOG_TRACE(logger, subsystem, ...) ((void)0)
#endif

#if 1 >= ENGINE_LOG_COMPILE_MIN_LEVEL
    #define ENGINE_LOG_DEBUG(logger, subsystem, ...) \
        ENGINE_LOG_AT_(ENGINE_LOG_LEVEL_DEBUG, logger, subsystem, __VA_ARGS__)
#else
    #define ENGINE_LOG_DEBUG(logger, subsystem, ...) ((void)0)
#endif

#if 2 >= ENGINE_LOG_COMPILE_MIN_LEVEL
    #define ENGINE_LOG_INFO(logger, subsystem, ...) \
        ENGINE_LOG_AT_(ENGINE_LOG_LEVEL_INFO, logger, subsystem, __VA_ARGS__)
#else
    #define ENGINE_LOG_INFO(logger, subsystem, ...) ((void)0)
#endif

#if 3 >= ENGINE_LOG_COMPILE_MIN_LEVEL
    #define ENGINE_LOG_WARN(logger, subsystem, ...) \
        ENGINE_LOG_AT_(ENGINE_LOG_LEVEL_WARN, logger, subsystem, __VA_ARGS__)
#else
    #define ENGINE_LOG_WARN(logger, subsystem, ...) ((void)0)
#endif

#if 4 >= ENGINE_LOG_COMPILE_MIN_LEVEL
    #define ENGINE_LOG_ERROR(logger, subsystem, ...) \
        ENGINE_LOG_AT_(ENGINE_LOG_LEVEL_ERROR, logger, subsystem, __VA_ARGS__)
#else
    #define ENGINE_LOG_ERROR(logger, subsystem, ...) ((void)0)
#endif

#if 5 >= ENGINE_LOG_COMPILE_MIN_LEVEL
    #define ENGINE_LOG_FATAL(logger, subsystem, ...) \
        ENGINE_LOG_AT_(ENGINE_LOG_LEVEL_FATAL, logger, subsystem, __VA_ARGS__)
#else
    #define ENGINE_LOG_FATAL(logger, subsystem, ...) ((void)0)
#endif

#endif /* ENGINE_LOGGING_H */
