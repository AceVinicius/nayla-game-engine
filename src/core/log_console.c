#include "src/core/logging.h"

#include <stdio.h>

/* Console sink: writes formatted records to the standard error stream.
 * See docs/spec/core/logging.md §6 for the contract.
 *
 * CORE-LOG-052: This is the SOLE translation unit in src/core and src/renderer
 * permitted to name printf, fprintf, puts, fputs, fwrite, stdout, or stderr.
 * All other logging output is routed through sinks.
 *
 * CORE-LOG-053: One fprintf call per record for line atomicity.
 * CORE-LOG-054: THREAD_SAFE because it holds no mutable state and relies on
 * the platform C library's stdio locking.
 */

static void console_log_sink_write(const EngineLogSink *sink,
                                   const EngineLogRecord *record) {
    (void)sink;  /* Unused; the sink carries no state. */

    /* CORE-LOG-053: One fprintf call per record for line atomicity.
     * CORE-LOG-055: The exact format is not contractual, only that it writes
     * to the standard error stream and does not interleave mid-line.
     * Timestamp and thread id are not printed, so the CORE-LOG-040 "unknown"
     * sentinels never appear as numbers.
     */
    fprintf(stderr,
            "[%s] %s (%s): %s\n",
            engine_log_level_name(record->level),
            record->subsystem ? record->subsystem : "?",
            record->file ? record->file : "unknown",
            record->message);
}

/* THREAD_SAFE: The console sink is thread-safe by inheriting the stdio
 * stream's own lock semantics. CORE-LOG-054.
 */
static const EngineLogSink console_log_sink_instance = {
    .write = console_log_sink_write,
    .flush = NULL,  /* No buffering in our console output. */
    .user_data = NULL,
    .min_level = ENGINE_LOG_LEVEL_TRACE  /* Console takes everything; filtering is done by the logger. */
};

const EngineLogSink *engine_console_log_sink(void) {
    /* CORE-LOG-050/051: Returns a pointer to the static const sink.
     * Never NULL, identical on every call, callable anytime from any thread.
     */
    return &console_log_sink_instance;
}
