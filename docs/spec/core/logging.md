# Core Logging — Contract Specification

**Status:** Accepted
**Date:** 2026-09-02
**Author:** `spec-writer`
**Scope:** the diagnostic logging facility used by every engine subsystem — core, renderer, platform API, GPU API, and all backends — plus the built-in console sink.
**Supersedes:** specs.md §11 ("Logging") and specs.md §66 CORE-004 ("Logging"). specs.md remains a historical draft; where it disagrees with this document — on the shape of the logging macros, on whether the logging system is "centralized", and on where timestamps and thread identifiers come from — **this document wins**. See §2.4 and §5.3.
**Depends on:** `docs/spec/core/types.md` (Accepted) — CORE-TYPES-004 (`const char *` is NUL-terminated UTF-8), CORE-TYPES-010/011 (`bool` in APIs, not in public layouts), CORE-TYPES-012 (bit flags are `uint32_t`), CORE-TYPES-016 (`void *` sanctioned for callback `user_data`), CORE-TYPES-017/018 (`size_t` for in-memory sizes, `uint64_t` for timestamps), CORE-TYPES-021/022/023/024/025 (naming and enum width). `docs/spec/core/result.md` (Accepted) — CORE-RESULT-001 (one result type), CORE-RESULT-005 (closed code set), CORE-RESULT-010 (static-storage strings), CORE-RESULT-015 (`ENGINE_NODISCARD`), CORE-RESULT-016 (no global error state), CORE-RESULT-017 (infallible operations return `void`). `docs/spec/core/allocator.md` (Accepted) — CORE-ALLOC-021 (the thread-safety vocabulary), CORE-ALLOC-060..067 (the borrow-and-store ownership pattern), CORE-ALLOC-040/041 (the `static const` singleton pattern), CORE-ALLOC-080/081 (`engine_foundation` placement).
**Related:** CLAUDE.md invariants #3, #4, **#5**, #7; `rules/core.md`; ADR-009 §5 ("no ambient lookup"); result.md §5 (which delegates contextual failure detail to this spec).

---

## 1. Problem

result.md §5 removed `EngineStatus{code, message}` and sent every request for contextual failure detail here: *"log at the point of failure, where the context is in scope, and return the code upward."* That obligation is now due, and nothing in the tree can discharge it — `main.c` reports failures with `fprintf(stderr, …)`, and there is no mechanism a renderer or a backend could use instead.

CORE-004 says only "implement initial logging system". specs.md §11 supplies more, but what it supplies is the problem:

```c
ENGINE_LOG_INFO("GPU", "Created device");
```

**There is no logger in that call site's scope.** The macro can only work by reaching for state that is not passed to it: a global minimum level, a global sink list, a global "current subsystem". That is precisely the hidden global state CLAUDE.md invariant #5 forbids, that ADR-009 §5 refused for the allocator, and that CORE-RESULT-016 refused for error reporting — three times now, the same shape has been rejected. A fourth rejection cannot be a reflex; logging is genuinely different from allocation and error reporting in ways §2 has to weigh honestly, because logging is the one subsystem that must be callable from *anywhere*, including from places whose signatures the engine does not control.

specs.md §11 also asks for timestamps and thread identifiers. Core cannot obtain either: a monotonic clock and a thread identity are PLATFORM-005 and PLATFORM-008, neither of which exists, and neither of which the foundation leaf may depend on (`rules/platform.md`; CORE-ALLOC-081). §5.3 resolves that too, rather than quietly dropping the requirement.

---

## 2. Decision 1 — The logger is an explicit, borrowed handle; the macros take it as their first argument

**There is no global logger, no default logger, and no ambient lookup. Every logging macro takes a `const EngineLogger *` as its first argument. `NULL` is a valid value and means *discard*.**

```c
ENGINE_LOG_INFO(engine->logger, "GPU", "Created device %u", device_index);
```

### 2.1 Rationale — weighing the global honestly

**For a global (or singleton) logger — the real arguments, not strawmen:**

1. **Reachability is the whole point of a logger.** An allocator is needed by the handful of functions that own memory; an error code is produced by the function that fails. A log line is wanted at *hundreds* of sites, many of them three or four frames below anything that holds a subsystem object — a static helper, a parsing loop, a shader-reflection walk. Threading a parameter to reach them is a cost the allocator never had to pay.
2. **Some signatures cannot take a logger at all.** `EngineAllocatorAllocFn` is fixed by an Accepted spec at `(allocator, size, alignment)`. A future GPU debug-message callback, a sort comparator, a job entry point — the engine does not control all of these. A design that can only log where it can add a parameter cannot log from them.
3. **Adding diagnostics must not be an API change.** If `ENGINE_LOG_WARN` requires a logger in scope, then adding one warning line to a leaf function edits that function's signature, its declaration, and every caller. That friction is paid on exactly the change engineers should be encouraged to make, and the predictable outcome is that people stop adding logs.
4. **`engine_default_allocator()` shows the engine already sanctions one blessed instance.** Why not `engine_default_logger()`?

**Against, decisively:**

1. **Point 4 does not survive inspection, and it is the argument worth killing first.** `engine_default_allocator()` returns a pointer to a `static const EngineAllocator` — a compile-time constant aggregate with no mutable state, no initialization, and no synchronization (CORE-ALLOC-040). It is not a global *variable* in any sense invariant #5 cares about; it is a constant, exactly like a string literal. A logger's minimum level and sink table are **mutable, runtime-configured, and written after startup**. The pattern does not generalize to it. It does generalize to something in this spec — the console *sink* (§6), which is stateless and immutable — and stating precisely where the analogy holds and where it breaks is what keeps it from being used as a rubber stamp.
2. **A mutable global re-creates every failure mode already rejected.** Two loggers in one process become impossible (a test harness capturing records while the application logs to a console; an editor hosting two engine instances). Test isolation is lost: a test that installs a capture sink changes the behaviour of every test that runs after it, and `rules/testing.md`'s architecture tests would be asserting against shared mutable state. Registration ordering against `main()` becomes a lifetime question with no owner — the same question ADR-009 §3 answered for the allocator by making ownership explicit.
3. **Reachability, measured rather than assumed, is not the problem it looks like.** The genuinely unreachable sites fall into three classes and each already has an answer:
   - *Static helpers inside a `.c`* — pass the logger down within the file. This is local, mechanical, and invisible outside the translation unit.
   - *Fixed-signature callbacks* — every callback interface in this engine carries a `void *user_data` the implementation owns (CORE-TYPES-016, CORE-ALLOC-004). An allocator that wants to log puts a logger pointer in its own `user_data` struct. The reach is already there; it just is not free.
   - *`main()` before any subsystem exists* — `main()` constructs the logger, so it holds it by definition.
   The remaining case, a pure leaf that should not have a logger parameter, is answered by CORE-LOG-005: `NULL` is legal and discards. The line compiles and costs nothing until someone decides the diagnostic is worth threading a parameter for.
4. **The convenience being defended is one token.** specs.md's `ENGINE_LOG_INFO("GPU", …)` versus this spec's `ENGINE_LOG_INFO(engine->logger, "GPU", …)`. At the overwhelming majority of real call sites the subsystem object is already `self`/`engine`/`renderer`, so the added text is a field access. That is the entire ergonomic loss, and it buys back invariant #5, test isolation, and multi-instance support.

**The honest cost, stated plainly:** subsystems that log now carry a second borrowed pointer beside the allocator, and their construction signatures grow. §11 costs that concretely. And a leaf that logs to `NULL` is silent — a real trap, mitigated only by review, since the compiler cannot distinguish "deliberately no logger" from "forgot to thread one".

### 2.2 Rules

**CORE-LOG-001 (no global logger state).** No engine translation unit may hold a file-scope mutable `EngineLogger`, a pointer to one, an `EngineLogSink` table, or a log level. There is no `engine_default_logger()`, no `engine_log_set_global()`, no thread-local logger, and no lazily initialized logger. Adding any of these requires superseding this section.

**CORE-LOG-002 (caller owns the logger).** An `EngineLogger` is owned by whoever defined it — typically `main()`, on its stack or in its own static storage. The engine never allocates one, never frees one, and never takes ownership. This is CORE-ALLOC-060/061/062 restated for the logger.

**CORE-LOG-003 (borrowed, stored once).** Every subsystem that logs takes `const EngineLogger *logger` in its init/create entry point and stores it in its own `.c`-private struct. That pointer is set once at construction and never reassigned, exactly as CORE-ALLOC-067 requires of the allocator. There is no "change an object's logger" operation.

**CORE-LOG-004 (outlives everything that logs through it).** The `EngineLogger`, and every sink registered on it, MUST remain valid until after the last object holding the logger has been destroyed. Destroying a logger or a live sink while a subsystem still holds it is undefined behaviour and is not detectable by the engine (CORE-ALLOC-065/066, same reasoning).

**CORE-LOG-005 (`NULL` means discard, and never means "the default").** `logger == NULL` is legal at every logging macro, at `engine_log_write`, at `engine_log_flush`, and at every subsystem construction site that documents it. It discards the record: no output, no diagnostic, no crash. It MUST NOT be resolved to any default logger, because no default exists. This is deliberately *not* CORE-ALLOC-068's `NULL`-selects-default rule — that rule exists because a program without an allocator cannot run, whereas a program without a logger runs fine and silently.

**CORE-LOG-006 (`const` is load-bearing).** `engine_log_write` and `engine_log_flush` take `const EngineLogger *`. Every operation that reconfigures the logger — `engine_log_init`, `engine_log_add_sink`, `engine_log_remove_sink`, `engine_log_set_level`, `engine_log_set_context_source`, `engine_log_shutdown` — takes a non-`const` `EngineLogger *`. Subsystems therefore store the `const` form and **can emit records but cannot reconfigure**; configuration authority belongs to whoever owns the non-`const` pointer. This is CORE-ALLOC-006's argument reapplied, and it is the structural reason the logger is not "a global with extra steps": the mutable surface is reachable from exactly one place.

**CORE-LOG-007 (`user_data` is opaque).** Every `void *user_data` in this spec — a sink's, the context source's — is the implementation's own state. The engine never reads it, writes it, copies it, or frees it (CORE-ALLOC-004, CORE-TYPES-016).

### 2.3 Rules

**CORE-LOG-008 (the logger allocates nothing).** `EngineLogger` is a **defined**, caller-placed struct with a fixed-capacity sink table; the message buffer is a stack buffer inside `engine_log_write` (CORE-LOG-025). `logging.h` does **not** include `allocator.h`, `EngineAllocator` does not appear anywhere in this spec's interface, and no logging operation can return `ENGINE_RESULT_OUT_OF_MEMORY`.

*Rationale.* The subsystem you need most under memory pressure must not be the one that fails under memory pressure. A heap-backed logger inverts that: `engine_init`'s out-of-memory branch is exactly the branch that most wants a log line, and a logger that had to allocate to emit it would fail there. Making the logger allocation-free also removes an init-ordering dependency (§9) and keeps the foundation leaf's include set minimal in the same spirit as CORE-ALLOC-005/081.

The cost is real and is accepted: the sink capacity is a compile-time constant (CORE-LOG-034), and `EngineLogger`'s layout is public, so changing that capacity is an ABI change. `EngineAllocator` is already a public defined struct (CORE-ALLOC-001), so this is a precedent the engine has taken before, not a new one.

**CORE-LOG-009 (naming).** `EngineLogger`, `EngineLogLevel`, `EngineLogRecord`, `EngineLogSink`, `EngineLogSinkWriteFn`, `EngineLogSinkFlushFn`, `EngineLogTimeFn`, `EngineLogThreadIdFn` — PascalCase, `Engine`-prefixed, no `_t` (CORE-TYPES-021/022/023). Enumerators are `ENGINE_LOG_LEVEL_*`; constants and macros are `UPPER_CASE`; functions are `snake_case` with an `engine_log_` prefix; fields are `snake_case` (CORE-TYPES-024).

### 2.4 What this supersedes in specs.md §11

specs.md §11's example macros are **not adopted in that form**. `ENGINE_LOG_INFO("GPU", "Created device")` becomes `ENGINE_LOG_INFO(logger, "GPU", "Created device")`. Its phrase "centralized logging system" is superseded by "one logger instance per program, explicitly constructed and explicitly threaded" — centralized in *configuration*, not in *storage*.

---

## 3. Decision 2 — Levels, ordering, and two-stage filtering

```c
typedef enum EngineLogLevel {
    ENGINE_LOG_LEVEL_TRACE = 0,
    ENGINE_LOG_LEVEL_DEBUG,   /* 1 */
    ENGINE_LOG_LEVEL_INFO,    /* 2 */
    ENGINE_LOG_LEVEL_WARN,    /* 3 */
    ENGINE_LOG_LEVEL_ERROR,   /* 4 */
    ENGINE_LOG_LEVEL_FATAL,   /* 5 */
    ENGINE_LOG_LEVEL_OFF      /* 6 — a filter value only, never a record value */
} EngineLogLevel;
```

**CORE-LOG-010 (ordering is numeric and ascending in severity).** `TRACE < DEBUG < INFO < WARN < ERROR < FATAL < OFF`. Comparison is ordinary integer comparison on the enumerators; a record is admitted when `record_level >= min_level`. The six levels of specs.md §11 are adopted unchanged in name and order.

**CORE-LOG-011 (`OFF` is a filter sentinel).** `ENGINE_LOG_LEVEL_OFF` MUST NOT appear as the level of a record. Setting a logger's or a sink's minimum level to `OFF` discards everything, because no record's level satisfies `level >= OFF`. It exists so "disable this sink" needs no second mechanism (no `enabled` flag, no removal-and-reregistration dance).

**CORE-LOG-012 (values are stable; ordering is contractual).** The numeric values `0..6` are fixed by this spec and are asserted (`LOG-INV-1`) because the compile-time filter (§8) is a preprocessor comparison and the preprocessor cannot see enumerator names. Levels MUST NOT be inserted between existing ones, renumbered, or reordered. A new level is appended before `OFF` only by superseding this rule, and `OFF` is renumbered with it.

**CORE-LOG-013 (`FATAL` does not terminate).** Logging at `ENGINE_LOG_LEVEL_FATAL` records a severity and nothing more. The logger MUST NOT `abort`, `exit`, `longjmp`, raise a signal, or break into a debugger at any level. Termination is CORE-003's job (`ENGINE_ASSERT`/`ENGINE_VERIFY`), and a logger that ends the process would (a) put program lifetime under the control of the diagnostic subsystem and (b) prevent a caller from completing the cleanup that CORE-RESULT-013 obliges it to perform on a failure path — the exact path where `FATAL` is most likely to be used.

**CORE-LOG-014 (level names).** `const char *engine_log_level_name(EngineLogLevel level)` returns `"TRACE"`, `"DEBUG"`, `"INFO"`, `"WARN"`, `"ERROR"`, `"FATAL"`, `"OFF"`, or `"UNKNOWN"` for an out-of-range value. It never returns `NULL`, and every returned pointer has **static storage duration** — CORE-RESULT-010's rule, engine-wide, applies here verbatim: the caller MUST NOT free or modify it and MAY retain it indefinitely. This mirrors `engine_result_name` deliberately, so diagnostics have one string-lifetime rule rather than two.

### Two-stage filtering — both compile-time and runtime, and why both

**CORE-LOG-015 (compile-time gate).** `ENGINE_LOG_COMPILE_MIN_LEVEL` is an integer macro, `0`–`6`, matching CORE-LOG-012's values. It defaults to `0` (`TRACE`) when `NDEBUG` is undefined and `2` (`INFO`) when it is defined. A level below it produces a macro that expands to `((void)0)`: **the call is not compiled, its arguments are not evaluated, and the format string is left unreferenced.**

**CORE-LOG-016 (runtime gate).** Each `EngineLogger` stores a minimum level, readable with `engine_log_get_level` and settable with `engine_log_set_level`. A record whose level is below it is discarded before any formatting occurs.

**CORE-LOG-017 (per-sink gate).** Each `EngineLogSink` carries its own `min_level`, fixed at registration. It is applied **after** formatting, per sink. The common configuration it exists for — a console at `INFO` beside a file at `TRACE` — is otherwise impossible without two loggers.

**CORE-LOG-018 (evaluation order, normative).** In `engine_log_write`: (1) reject `logger == NULL`; (2) reject `level < logger->min_level`; (3) read the timestamp and thread id once; (4) format the message once into a stack buffer; (5) for each registered sink, if `level >= sink->min_level`, call its `write`. Steps 3 and 4 happen **once per record, not once per sink** — that is what makes cross-sink timestamps identical and what keeps the cost of a second sink near zero.

*Rationale for keeping both gates.* Compile-time alone cannot help a shipped build whose user is reproducing a bug; runtime alone leaves `TRACE` call sites — the highest-volume ones, in inner loops — costing a load, a compare, and a branch each in release, plus their format strings and `__FILE__` literals in the binary. Neither subsumes the other, and the two together are three lines of macro. The known cost is that a `TRACE` line compiled out in release cannot be re-enabled without a rebuild; that is the intended trade, and `ENGINE_LOG_COMPILE_MIN_LEVEL` is overridable per build so a release-with-trace configuration remains available.

---

## 4. Decision 3 — The log record

```c
typedef struct EngineLogRecord {
    EngineLogLevel  level;           /* never ENGINE_LOG_LEVEL_OFF */
    const char     *subsystem;       /* non-NULL, static storage, NUL-terminated UTF-8 */
    const char     *message;         /* non-NULL, NUL-terminated UTF-8, CALL-SCOPED */
    size_t          message_length;  /* bytes before the NUL */
    const char     *file;            /* static storage, or NULL when unavailable */
    uint32_t        line;            /* 0 when unavailable */
    uint32_t        flags;           /* ENGINE_LOG_RECORD_* bit flags */
    uint64_t        timestamp_ns;    /* ENGINE_LOG_TIMESTAMP_UNKNOWN when unavailable */
    uint64_t        thread_id;       /* ENGINE_LOG_THREAD_UNKNOWN when unavailable */
} EngineLogRecord;

#define ENGINE_LOG_RECORD_TRUNCATED   ((uint32_t)0x00000001u)
#define ENGINE_LOG_TIMESTAMP_UNKNOWN  UINT64_MAX
#define ENGINE_LOG_THREAD_UNKNOWN     ((uint64_t)0)
#define ENGINE_LOG_MESSAGE_MAX        ((size_t)1024)
```

**CORE-LOG-020 (types are not negotiable).** `line` is `uint32_t`, not `int` (CORE-TYPES-001/002). `message_length` is `size_t` — an in-memory byte count (CORE-TYPES-017). `timestamp_ns` and `thread_id` are `uint64_t` — host-independent quantities, and timestamps are named explicitly by CORE-TYPES-018. `flags` is `uint32_t`, not a set of `bool` fields and not a bitfield (CORE-TYPES-012). No `bool` appears in this struct (CORE-TYPES-011).

**CORE-LOG-021 (`subsystem` is a static string).** The subsystem name MUST be a string literal or otherwise have static storage duration, MUST be non-`NULL`, and MUST NOT be caller-formatted or heap-owned. A sink MAY retain the pointer indefinitely. Enforcing this at the source (rather than copying the name into the record) is what lets a sink index or intern subsystem names without a lifetime question. `"GPU"`, `"Vulkan"`, `"Renderer"`, `"Core"` are the shape intended; capitalization and vocabulary are a review matter, not a rule.

**CORE-LOG-022 (`message` is call-scoped — the single most important lifetime rule here).** `record->message` points into a stack buffer inside `engine_log_write` and is valid **only for the duration of the sink's `write` call**. A sink that queues, batches, or defers a record MUST copy the bytes before returning. This is the direct analogue of CORE-RESULT-010's reasoning: a dynamically allocated message would need an owner and would fail under memory pressure (CORE-LOG-008), and a callee-owned buffer that outlives the call would either be a hidden global or a per-logger buffer that concurrent writers would race on.

**CORE-LOG-023 (`file` is a static string or `NULL`).** When source location is compiled in it is `__FILE__` — a static string literal, retainable. When it is compiled out, `file` is `NULL` and `line` is `0`. Sinks MUST handle `NULL` and MUST NOT infer anything from `line == 0` other than "unavailable".

**CORE-LOG-024 (source location is debug-only by default).** `ENGINE_LOG_SOURCE_LOCATION` defaults to `1` when `NDEBUG` is undefined and `0` when it is defined, and is overridable per build. Release builds omit it because `__FILE__` literals both enlarge the binary and embed absolute build-machine paths in a shipped artifact.

**CORE-LOG-025 (formatting, truncation, and the buffer).** The message is produced by `vsnprintf` into a stack buffer of `ENGINE_LOG_MESSAGE_MAX` bytes including the terminator. If the formatted text would exceed it, the message is truncated, remains NUL-terminated, `message_length` is the truncated length, and `ENGINE_LOG_RECORD_TRUNCATED` is set in `flags`. Truncation is never an error and never suppresses the record: a truncated diagnostic beats no diagnostic. `ENGINE_LOG_MESSAGE_MAX` is `1024` — large enough for a device-capability line or a shader-error excerpt, small enough that a per-call stack buffer is unremarkable on any supported thread stack.

**CORE-LOG-026 (format strings are literals, and `%n` is prohibited).** The `format` argument MUST be a string literal known at compile time. It MUST NOT be caller data, asset data, or a value received over any boundary, and the `%n` conversion MUST NOT be used. A non-literal format string defeats the compiler's `format` diagnostics and is the classic format-string vulnerability; this rule is what makes `ENGINE_PRINTF_FORMAT` (§8) worth having.

**CORE-LOG-027 (records are values, not objects).** An `EngineLogRecord` has no lifetime beyond the `write` call, no destructor, no identity, and is never allocated. Sinks receive `const EngineLogRecord *` and MUST NOT modify it.

---

## 5. Decision 4 — Sinks

### 5.1 Shape

```c
typedef struct EngineLogSink EngineLogSink;

typedef void (*EngineLogSinkWriteFn)(const EngineLogSink *sink,
                                     const EngineLogRecord *record);
typedef void (*EngineLogSinkFlushFn)(const EngineLogSink *sink);

struct EngineLogSink {
    EngineLogSinkWriteFn  write;      /* required, never NULL */
    EngineLogSinkFlushFn  flush;      /* optional, may be NULL */
    void                 *user_data;  /* sink-owned, opaque to the engine */
    int32_t               min_level;  /* an EngineLogLevel; immutable after registration */
};
```

**CORE-LOG-030 (shape mirrors `EngineAllocator` deliberately).** A sink is a **defined struct of function pointers plus `user_data`**, not a bare callback. The reason is the one CORE-ALLOC-004 gives: a bare `void (*)(const EngineLogRecord *)` has nowhere to put the sink's own state (the `FILE *`, the ring buffer, the socket), forcing every non-trivial sink back onto a static variable — the hidden global this spec exists to avoid. Callers construct sink instances, so the layout is public API; fields MUST NOT be reordered or removed, and a later field is appended.

**CORE-LOG-031 (`write` is required, `flush` is optional).** `write` MUST be non-`NULL`; this is verified once, at registration (CORE-LOG-036), not on every record — CORE-ALLOC-003's argument, applied to a per-record path. `flush` MAY be `NULL`, meaning the sink has nothing to flush; the logger skips it.

**CORE-LOG-032 (`min_level` is stored as `int32_t`).** The field holds an `EngineLogLevel` value but is declared `int32_t` because `EngineLogSink`'s layout is public and a C17 enum's underlying type is implementation-defined (CORE-TYPES-025). It is set before registration and MUST NOT be modified while the sink is registered — that would be a write racing every logging thread, with no synchronization available (§7).

### 5.2 Ownership, registration, and coexistence

**CORE-LOG-033 (sinks are caller-owned and borrowed).** The engine stores `const EngineLogSink *` and never copies the struct, never allocates one, and never frees one. Identity is the **pointer**, exactly as CORE-ALLOC-063 defines allocator identity: two structurally identical sinks are two sinks, and `engine_log_remove_sink` matches on pointer equality. A `static const EngineLogSink` in the application's own file is the expected shape and requires no allocation at all.

*Why not engine-owned copies?* A copy would make the engine the owner of a struct whose `user_data` points at something the engine cannot free (an open `FILE *`, a socket), so ownership would be split down the middle of one object — the ambiguity CORE-ALLOC-060/061 exists to prevent. Borrowing keeps one owner for the whole thing.

**CORE-LOG-034 (fixed capacity, `ENGINE_LOG_MAX_SINKS == 8`).** The sink table is a fixed-size array inside `EngineLogger`. There is no growth, no reallocation, and therefore no allocation (CORE-LOG-008). Eight covers every combination the engine can currently justify — console, rotating file, in-memory ring for a crash dump, IDE/debugger output, remote/telemetry, test capture, and two spare.

**CORE-LOG-035 (registration order is delivery order).** Sinks are invoked in the order they were added. Removing a sink preserves the relative order of the rest. Deterministic order is what allows a test to assert on interleaved output.

**CORE-LOG-036 (`engine_log_add_sink` contract).** `ENGINE_NODISCARD EngineResult engine_log_add_sink(EngineLogger *logger, const EngineLogSink *sink)`:
- `ENGINE_RESULT_INVALID_ARGUMENT` if `logger` is `NULL`, `sink` is `NULL`, `sink->write` is `NULL`, or `sink->min_level` is outside `[TRACE, OFF]`;
- `ENGINE_RESULT_INVALID_STATE` if the logger is uninitialized, if the table already holds `ENGINE_LOG_MAX_SINKS` entries, or if this exact pointer is already registered;
- otherwise `ENGINE_RESULT_SUCCESS`.

*On the capacity code.* result.md's code set is closed (CORE-RESULT-005) and this spec does not extend it. `ENGINE_RESULT_OUT_OF_MEMORY` was considered and rejected: result.md §4 binds it specifically to "an allocation through the `EngineAllocator` returned `NULL`", and CORE-LOG-008 guarantees no such allocation exists here, so returning it would report a fiction. `ENGINE_RESULT_INVALID_STATE` — "the object exists but is not in a state where this operation is meaningful" — describes a full table exactly. If a future amendment to result.md adds a capacity-exhausted code under CORE-RESULT-008, **this is a named call site for it.**

**CORE-LOG-037 (`engine_log_remove_sink` contract).** Returns `ENGINE_RESULT_INVALID_ARGUMENT` for a `NULL` logger or sink, `ENGINE_RESULT_INVALID_STATE` if that pointer is not registered, otherwise `ENGINE_RESULT_SUCCESS`. Removal does not flush; a caller that wants the sink's buffered output MUST call `engine_log_flush` first. Making removal implicitly flush would make an error path do I/O the caller did not ask for.

**CORE-LOG-038 (sink obligations).** A sink's `write`:
- MUST return; it MUST NOT block indefinitely, `longjmp`, `abort`, or terminate the process;
- MUST NOT call any `engine_log_*` function on any logger — there is no recursion guard, and there will not be one, because a guard requires thread-local mutable state that CORE-LOG-001 forbids. This is CORE-ALLOC-020's no-reentrancy rule restated for the sink;
- MUST NOT retain `record->message` past return without copying (CORE-LOG-022);
- MUST NOT modify the record (CORE-LOG-027);
- has no failure channel. A sink that cannot write drops the record silently or handles it internally.

**CORE-LOG-039 (`engine_log_write` returns `void`).** Logging has no reportable failure mode. Per CORE-RESULT-017, an operation with no failure mode returns `void` rather than `EngineResult` for symmetry — and here the argument is stronger than usual: a checkable logging result would require error handling around error reporting, which either recurses or is ignored. A dropped record is not an engine error.

### 5.3 Timestamps and thread identifiers — installed, not assumed

specs.md §11 asks for both. Core cannot produce either without violating a boundary, so they arrive through borrowed callbacks:

```c
typedef uint64_t (*EngineLogTimeFn)(void *user_data);      /* monotonic nanoseconds */
typedef uint64_t (*EngineLogThreadIdFn)(void *user_data);  /* opaque, stable per thread */

EngineResult engine_log_set_context_source(EngineLogger *logger,
                                           EngineLogTimeFn      time_source,
                                           EngineLogThreadIdFn  thread_id_source,
                                           void                *user_data);
```

**CORE-LOG-040 (both sources are optional and default to absent).** After `engine_log_init` both are `NULL`. A record produced with no time source carries `ENGINE_LOG_TIMESTAMP_UNKNOWN`; with no thread-id source it carries `ENGINE_LOG_THREAD_UNKNOWN`. Sinks MUST render both sentinels as "unknown" rather than as `0` or as a huge number.

**CORE-LOG-041 (why callbacks rather than a clock in core).** Three reasons, and the first is the decisive one:
1. A **monotonic** timer is PLATFORM-005's contract and a thread identity is PLATFORM-008's. The foundation leaf depends on nothing (CORE-ALLOC-081) and core may not reach the platform layer's backends; hard-wiring either would be the dependency inversion `rules/platform.md` forbids.
2. C's portable `timespec_get(TIME_UTC)` is available to core, and is still the wrong answer: it is wall-clock, so an NTP step reorders the log. Log ordering must not depend on the system clock being well-behaved.
3. An injectable clock lets a test produce byte-identical expected output. A hard-wired one makes every record's timestamp untestable.

**CORE-LOG-042 (read once per record).** Both sources are called at most once per admitted record, before formatting (CORE-LOG-018 step 3), and never for a record rejected by the logger's level gate. All sinks therefore see identical values for one record, which is what makes cross-sink correlation possible.

**CORE-LOG-043 (source obligations).** A time source MUST be monotonic non-decreasing within a program run, MUST return nanoseconds, and MUST NOT return `ENGINE_LOG_TIMESTAMP_UNKNOWN`. A thread-id source MUST return a value stable for the life of a thread and distinct between concurrently live threads, and MUST NOT return `ENGINE_LOG_THREAD_UNKNOWN`. Both MUST be safe to call concurrently from any thread and MUST NOT log. The engine does not verify any of this; these are caller obligations for the same unenforceability reason CORE-ALLOC-032 gives.

---

## 6. Decision 5 — The built-in console sink

**CORE-LOG-050 (accessor).** `const EngineLogSink *engine_console_log_sink(void);` returns a pointer to a single `static const EngineLogSink` with static storage duration, defined in `src/core/log_console.c`. It never returns `NULL`, is callable at any time from any thread including before any initialization, and returns the identical pointer on every call.

**CORE-LOG-051 (this is where CORE-ALLOC-040's singleton pattern *does* generalize).** §2.1 rejected `engine_default_logger()` because a logger has mutable state. The console sink has none: its `user_data` is `NULL`, its `min_level` is `ENGINE_LOG_LEVEL_TRACE` (the logger's and any wrapping filter's gates do the filtering), and its `write` resolves `stderr` internally. It is a compile-time constant aggregate, exactly like the default heap allocator, and it is a global constant rather than global *state*. A caller wanting a console at a different level defines its own `static const EngineLogSink` — three lines, no allocation.

**CORE-LOG-052 (composition and the sanctioned `<stdio.h>` site).** It writes to `stderr`, never `stdout`, so diagnostics never contaminate a program's data output. `src/core/log_console.c` is the **single** translation unit in `src/core` and `src/renderer` permitted to name `printf`, `fprintf`, `puts`, `fputs`, `fwrite`, `stdout`, or `stderr` — the exact structure CORE-ALLOC-042 uses for `malloc`, and enforced the same way (LOG-INV-9).

**CORE-LOG-053 (one call per record — line atomicity).** The sink emits each record with a **single** `fprintf` call. C17 requires stdio functions to lock the stream for the duration of the call, so one call per record is what keeps concurrently produced lines from interleaving mid-line. Two calls would be two lock intervals and would produce spliced output under load.

**CORE-LOG-054 (thread safety — stated, not implied).** The console sink is **`THREAD_SAFE`** (CORE-LOG-070), inheriting exactly the guarantee the platform C library gives `stderr` — no more and no less. It holds no mutable state of its own, so it neither strengthens nor weakens that guarantee. If a target's stdio turns out not to lock streams, that target needs an `EXTERNALLY_SYNCHRONIZED` wrapper sink; it is not papered over here. This is CORE-ALLOC-043's structure, applied to stdio instead of `malloc`.

**CORE-LOG-055 (format is not contractual).** The exact rendering — field order, separators, timestamp formatting — is an implementation detail of `log_console.c` and MUST NOT be asserted on by any test outside that file's own unit test. Tests that need to inspect records use a capture sink (LOG-INV-11), which sees the structured `EngineLogRecord` rather than a formatted line.

---

## 7. Thread safety

Stated in CORE-ALLOC-021's fixed vocabulary, because "documented" must not mean prose the caller has to interpret.

**CORE-LOG-070 (every sink MUST declare one of three levels), at its definition site:**
- **`THREAD_SAFE`** — `write` and `flush` may be called concurrently from any threads on the same sink instance. (The console sink, §6.)
- **`EXTERNALLY_SYNCHRONIZED`** — calls on one instance must not overlap; the caller provides mutual exclusion.
- **`THREAD_AFFINE`** — calls must come from one specific thread.

**CORE-LOG-071 (`engine_log_write` and `engine_log_flush` are `THREAD_SAFE` if and only if every registered sink is).** The logger performs **no locking of its own**. Concurrent `engine_log_write` calls on one logger read only immutable state (the level, the sink table) and stack-local storage, so the logger contributes no data race; whether the whole call is race-free is therefore decided entirely by the sinks.

*This is a constraint, not an oversight, and the reason is worth recording:* the foundation leaf has no mutex available. A portable mutex is PLATFORM-008, which does not exist and which core may not depend on in any case (CORE-ALLOC-081). C11 `<threads.h>` is an optional feature no major toolchain ships completely. A logger in `engine_foundation` therefore *cannot* lock today even if this spec wanted it to. Registering a non-`THREAD_SAFE` sink on a logger used by multiple threads is the caller's error, and the mitigation is a caller-written wrapping sink that holds the caller's own mutex — the same escape hatch CORE-ALLOC-043 names for a libc without thread-safe `malloc`.

**CORE-LOG-072 (all mutating operations are `EXTERNALLY_SYNCHRONIZED` against all logging).** `engine_log_init`, `engine_log_add_sink`, `engine_log_remove_sink`, `engine_log_set_level`, `engine_log_set_context_source`, and `engine_log_shutdown` MUST NOT run concurrently with any `engine_log_write` or `engine_log_flush` on the same logger, nor with each other. In practice: configure the logger on one thread before any other thread can reach it, and reconfigure only while the threads that log are quiesced.

**CORE-LOG-073 (`min_level` is a plain `int32_t`, not an atomic — decided, with its cost).** Making the runtime level atomic would allow lock-free level changes from a debug console while other threads log. It is rejected for now on three grounds: it would put `<stdatomic.h>` and an `_Atomic`-qualified field into a public header at a time when MSVC's C11 atomics support is behind an experimental switch; it would give the logger two different synchronization rules (one for the level, one for everything else) where CORE-LOG-072 gives it one; and — the deciding asymmetry, the same one CORE-ALLOC-017 used for zeroing — **relaxing `EXTERNALLY_SYNCHRONIZED` to `THREAD_SAFE` later is a source-compatible strengthening, while withdrawing a promised guarantee is not.** The honest cost is that "flip the log level at runtime from another thread" is not available today.

**CORE-LOG-074 (what no level implies).** None of the three makes the *content* a sink writes visible to another thread without that sink's own synchronization, and none makes concurrent `engine_log_remove_sink` of the same sink anything but a violation of CORE-LOG-072. (CORE-ALLOC-022, restated.)

**CORE-LOG-075 (records do not interleave within a sink).** A single record is delivered to a sink by exactly one `write` call. The logger never splits a record across calls, so per-record atomicity is entirely within the sink's power to guarantee (CORE-LOG-053).

---

## 8. Decision 6 — The macro surface, and exactly what disappears

```c
#define ENGINE_LOG_TRACE(logger, subsystem, ...)   /* ... */
#define ENGINE_LOG_DEBUG(logger, subsystem, ...)   /* ... */
#define ENGINE_LOG_INFO(logger, subsystem, ...)    /* ... */
#define ENGINE_LOG_WARN(logger, subsystem, ...)    /* ... */
#define ENGINE_LOG_ERROR(logger, subsystem, ...)   /* ... */
#define ENGINE_LOG_FATAL(logger, subsystem, ...)   /* ... */
```

Each expands, when its level is at or above `ENGINE_LOG_COMPILE_MIN_LEVEL`, to:

```c
do {                                                                       \
    if (engine_log_is_enabled((logger), ENGINE_LOG_LEVEL_INFO)) {          \
        engine_log_write((logger), ENGINE_LOG_LEVEL_INFO, (subsystem),     \
                         ENGINE_LOG_FILE_, ENGINE_LOG_LINE_, __VA_ARGS__); \
    }                                                                      \
} while (0)
```

and, when it is below, to `((void)0)`.

**CORE-LOG-060 (the compile-time selection is `#if` on integers, not on enumerators).** The preprocessor cannot see enumerator names — `#if ENGINE_LOG_LEVEL_INFO >= …` would compare `0 >= 0` and silently enable everything. Each macro is therefore selected by `#if <literal level number> >= ENGINE_LOG_COMPILE_MIN_LEVEL`, and CORE-LOG-012's numeric values are bound to the enumerators by `static_assert` (LOG-INV-1) so the two can never drift.

**CORE-LOG-061 (what "compiles away" means, precisely).** For a level below the compile-time threshold: the macro expands to `((void)0)`, so `engine_log_write` is not called, **the format string and every argument expression are not evaluated and not even part of the expansion**, and the `__FILE__` literal for that site is never produced. Any string literals that were written at the call site become unreferenced and are eliminated by the compiler or linker. For a level at or above the threshold but below the logger's runtime level: the call site *is* compiled, `engine_log_is_enabled` runs (a null check and one comparison), and the arguments are **not** evaluated because they sit inside the `if`.

**CORE-LOG-062 (arguments MUST NOT have side effects).** Because arguments are unevaluated in both filtered cases, `ENGINE_LOG_TRACE(logger, "Core", "n=%u", next_index())` is a defect: `next_index()` runs in a debug build and does not run in a release build. Log arguments MUST be pure expressions. This is the standard cost of a filtering macro and is stated as a rule so review can catch it.

**CORE-LOG-063 (`engine_log_is_enabled` is a `static inline` in the header).** `bool engine_log_is_enabled(const EngineLogger *logger, EngineLogLevel level)` returns `logger != NULL && (int32_t)level >= logger->min_level`. It is `static inline` rather than an out-of-line function specifically so the runtime gate costs a load and a branch instead of a cross-translation-unit call — the reason `EngineLogger` is a defined struct rather than an opaque one is largely this. It is the only code permitted to read `EngineLogger`'s fields directly from outside `logging.c`.

**CORE-LOG-064 (at least one variadic argument is required, and no compiler extension is used).** The format string is part of `__VA_ARGS__`, so `__VA_ARGS__` is never empty and neither GNU's `##__VA_ARGS__` nor C23's `__VA_OPT__` is needed. `ENGINE_LOG_INFO(logger, "Core", "started")` is well-formed under C17.

**CORE-LOG-065 (format checking).** `engine_log_write` is annotated with `ENGINE_PRINTF_FORMAT(6, 7)` — `__attribute__((format(printf, 6, 7)))` under GCC/Clang, empty elsewhere — defined in `logging.h` beside the macros. Combined with CORE-LOG-026's literal-format rule, this makes a mismatched conversion a compile-time diagnostic rather than undefined behaviour at runtime.

**CORE-LOG-066 (`do { … } while (0)`).** The multi-statement form is wrapped so `if (x) ENGINE_LOG_WARN(…); else …` parses correctly. The disabled form is `((void)0)` — an expression, not a statement — so both forms require a trailing semicolon identically.

**CORE-LOG-067 (the function surface is public too).** `engine_log_write` and a `va_list` variant `engine_log_write_v` are declared in the header and are callable directly. A wrapper that takes its own `...` (a subsystem-specific helper) needs the `va_list` form; forcing every such wrapper through the macros would be impossible.

**CORE-LOG-068 (no per-subsystem macro families).** There is no `GPU_LOG_INFO`, no `RENDERER_LOG_WARN`. The subsystem is a runtime argument, not a macro-name suffix, so a grep for one macro finds every log site and a sink can filter by subsystem string without knowing a compile-time list.

---

## 9. Initialization, shutdown, and lifecycle position

**CORE-LOG-080 (explicit init is required; zero-initialization is not enough).** `ENGINE_NODISCARD EngineResult engine_log_init(EngineLogger *logger, EngineLogLevel min_level)` sets the level, empties the sink table, and clears both context sources. It returns `ENGINE_RESULT_INVALID_ARGUMENT` for a `NULL` logger or a `min_level` outside `[TRACE, OFF]`, and `ENGINE_RESULT_SUCCESS` otherwise. It **cannot** return `ENGINE_RESULT_OUT_OF_MEMORY` (CORE-LOG-008). An `EngineLogger` that has not been through `engine_log_init` MUST NOT be used, even if it was zero-initialized.

**CORE-LOG-081 (shutdown returns `void`).** `void engine_log_shutdown(EngineLogger *logger)` flushes every registered sink that has a `flush`, then empties the sink table and clears the context sources. It is a no-op for `logger == NULL`. It returns `void` because it has no failure mode, per CORE-RESULT-017 — and returning `EngineResult` purely for symmetry with `engine_shutdown` is exactly what that rule forbids. It frees nothing, because the logger owns nothing (CORE-LOG-002/033).

**CORE-LOG-082 (position in the lifecycle — outermost).** The logger is constructed **first and destroyed last**, outside specs.md §37's sequence:

```
engine_log_init  →  add sinks  →  engine_create/engine_init  →  platform_initialize
                                        →  engine_log_set_context_source
                                        →  gpu_initialize → renderer_initialize → … → main loop
… → renderer_shutdown → gpu_shutdown → platform_shutdown → engine_destroy → engine_log_shutdown
```

Two consequences worth stating because they are the payoff for CORE-LOG-008:
- The logger needs **no allocator**, so it is available strictly before `engine_init` — which means `engine_init`'s own `ENGINE_RESULT_OUT_OF_MEMORY` branch is loggable. Had the logger required an allocator, the ordering question ("allocator first or logger first?") would have had no good answer.
- The context sources are installed **after** `platform_initialize`, because that is when a monotonic clock and thread identity first exist. Records emitted before that point carry the two "unknown" sentinels (CORE-LOG-040) rather than a wrong timestamp.

> **Amended by `platform.md` (PLATFORM-024/026/083).** The Platform API's time and thread-id sources are stateless and valid before any `Platform` exists, so they MAY be installed right after `engine_log_init`, and that is the recommended order. Installing after platform initialization remains conforming. Under the recommended order, records before `platform_init` carry real values; the sentinels appear only before the sources are installed.

**CORE-LOG-083 (`engine_log_flush`).** `void engine_log_flush(const EngineLogger *logger)` calls `flush` on every registered sink that has one, in registration order. It is a no-op for a `NULL` logger. It is `const` because flushing changes nothing about the logger itself (CORE-LOG-006).

**CORE-LOG-084 (construction parameters).** A subsystem that logs takes `const EngineLogger *logger` beside its `const EngineAllocator *allocator`, documents that `NULL` is permitted, and stores it (CORE-LOG-003/005). No validation is performed on it beyond that, because `NULL` is legal and a non-`NULL` logger's validity is CORE-LOG-004's caller obligation.

*Note for whoever writes `docs/spec/core/engine.md`:* this makes `engine_init(Engine **, const EngineAllocator *, const EngineLogger *)` a three-parameter constructor, and a fourth is plainly coming. A single `EngineInitDesc { allocator; logger; }` is the likely correct answer at that point. **It is out of scope here** — this spec fixes what the logger parameter *is*, not how the engine's constructor is shaped.

---

## 10. Placement and dependency direction

**CORE-LOG-090 (`logging.h`/`logging.c`/`log_console.c` join `engine_foundation`).** They extend the target table in allocator.md §8 without amending any of its rules: renderer, platform, GPU, and every backend must be able to log, and `engine_renderer` MUST NOT link `engine_core` (CORE-ALLOC-082), so logging cannot live in `engine_core`.

**CORE-LOG-091 (`logging.h`'s include set is exactly this).** `<stdarg.h>` (`va_list`), `<stdbool.h>` (`bool`), `<stddef.h>` (`size_t`), `<stdint.h>` (`int32_t`, `uint32_t`, `uint64_t`, `UINT64_MAX`), and `src/core/result.h` (`EngineResult`, `ENGINE_NODISCARD`). It MUST NOT include `allocator.h` (CORE-LOG-008), `<stdio.h>` (that belongs to `log_console.c` alone, CORE-LOG-052), `<stdatomic.h>` (CORE-LOG-073), or any OS or GPU header. Depending on `result.h` inverts nothing: `result.h` is a dependency-free leaf in the same target (CORE-ALLOC-081, result.md §10).

**CORE-LOG-092 (checks apply here too).** `forbid_os_gpu_includes` and `forbid_direct_allocation` already cover `engine_foundation`; LOG-INV-9 adds a `forbid_direct_output` check over `src/core` and `src/renderer` exempting `log_console.c`, in the shape `cmake/forbid-pattern.sh` already supports (one pattern, one exempt filename).

---

## 11. Invariants

Testable statements, in the style of `rules/testing.md`, types.md §7, result.md §8, and allocator.md §10.

**LOG-INV-1.** `ENGINE_LOG_LEVEL_TRACE == 0`, `DEBUG == 1`, `INFO == 2`, `WARN == 3`, `ERROR == 4`, `FATAL == 5`, `OFF == 6`. *(static_assert — this is what binds CORE-LOG-060's preprocessor integers to the enum.)*

**LOG-INV-2.** `sizeof(EngineLogLevel) == sizeof(int32_t)`, and `ENGINE_LOG_MESSAGE_MAX >= 256`, and `ENGINE_LOG_MAX_SINKS >= 1`. *(static_assert)*

**LOG-INV-3.** `engine_log_level_name()` returns non-`NULL` for every enumerator including `OFF`, and for at least one out-of-range value. *(unit test — the direct analogue of RESULT-INV-7.)*

**LOG-INV-4.** Every logging entry point tolerates `logger == NULL`: `ENGINE_LOG_ERROR(NULL, "Core", "x")`, `engine_log_write(NULL, …)`, `engine_log_flush(NULL)`, and `engine_log_shutdown(NULL)` all return normally and produce no output. *(unit test — CORE-LOG-005.)*

**LOG-INV-5.** No file-scope **mutable** variable in `src/core` or `src/renderer` holds an `EngineLogger`, an `EngineLogger *`, an `EngineLogSink *`, or an `EngineLogLevel`; and no symbol matching `*default_logger*`, `*global_log*`, or `*log_set_global*` exists anywhere. *(grep — CORE-LOG-001, CLAUDE.md invariant #5; the direct analogue of ALLOC-INV-10 and RESULT-INV-9.)*

**LOG-INV-6.** A logger at `ENGINE_LOG_LEVEL_WARN` delivers `WARN`/`ERROR`/`FATAL` records to a capture sink and delivers no `TRACE`/`DEBUG`/`INFO` record; a logger at `OFF` delivers nothing. *(unit test — CORE-LOG-010/011/016.)*

**LOG-INV-7.** With two sinks registered at `TRACE` and `ERROR`, one `ENGINE_LOG_WARN` reaches the first and not the second, and both are invoked in registration order for an `ENGINE_LOG_ERROR`. *(unit test — CORE-LOG-017/035.)*

**LOG-INV-8.** `engine_log_add_sink` returns `ENGINE_RESULT_INVALID_ARGUMENT` for a `NULL` sink and for a sink whose `write` is `NULL`; returns `ENGINE_RESULT_INVALID_STATE` on the `ENGINE_LOG_MAX_SINKS + 1`-th distinct sink and on a duplicate pointer; `engine_log_remove_sink` returns `ENGINE_RESULT_INVALID_STATE` for an unregistered pointer. *(contract test — CORE-LOG-036/037.)*

**LOG-INV-9.** No source under `src/core` or `src/renderer` other than `src/core/log_console.c` names `printf`, `fprintf`, `puts`, `fputs`, `fwrite`, `stdout`, or `stderr`. *(grep — CORE-LOG-052; the direct analogue of ALLOC-INV-7, and the check that makes §12's migration verifiable rather than aspirational.)*

**LOG-INV-10.** `src/core/logging.h` includes exactly the five headers of CORE-LOG-091 and mentions neither `EngineAllocator`, `malloc`, `<stdio.h>`, nor `_Atomic`. *(grep — the analogue of ALLOC-INV-8.)*

**LOG-INV-11.** A capture sink records a message longer than `ENGINE_LOG_MESSAGE_MAX`; the delivered `message` is NUL-terminated, `message_length < ENGINE_LOG_MESSAGE_MAX`, and `flags & ENGINE_LOG_RECORD_TRUNCATED` is set. *(unit test — CORE-LOG-025.)*

**LOG-INV-12.** With no context source installed, delivered records carry `timestamp_ns == ENGINE_LOG_TIMESTAMP_UNKNOWN` and `thread_id == ENGINE_LOG_THREAD_UNKNOWN`. With a deterministic injected time source, two sinks receiving one record observe **identical** `timestamp_ns`, and the source is called exactly once. *(unit test — CORE-LOG-040/042.)*

**LOG-INV-13.** Built with `-DENGINE_LOG_COMPILE_MIN_LEVEL=4`, a translation unit containing `ENGINE_LOG_TRACE(logger, "Core", "%u", counter_that_increments())` leaves the counter unchanged and delivers no record; the same source built with `=0` increments it once and delivers one. *(integration test — CORE-LOG-015/061/062, the only end-to-end check that the compile-time gate actually removes the call rather than merely filtering it.)*

**LOG-INV-14.** `engine_console_log_sink()` never returns `NULL`, returns the identical pointer on repeated calls from any thread, and its `write` is non-`NULL` and its `user_data` is `NULL`. *(unit test — CORE-LOG-050/051; the analogue of ALLOC-INV-3.)*

**LOG-INV-15.** No `EngineLogRecord` pointer or `record->message` pointer retained by a sink past its `write` call appears in any engine source. *(review-enforced — CORE-LOG-022; not greppable.)*

---

## 12. Edge cases

1. **`logger == NULL`** — every entry point discards and returns normally (CORE-LOG-005). It is never resolved to a default, because there is none.
2. **`subsystem == NULL`** — a caller bug. `engine_log_write` substitutes the literal `"?"` rather than dereferencing or dropping the record: a diagnostic path must not crash while reporting a failure (CORE-RESULT-011's principle, applied here).
3. **A message longer than `ENGINE_LOG_MESSAGE_MAX`** — truncated, terminated, flagged, still delivered (CORE-LOG-025). Never an error, never dropped.
4. **`vsnprintf` returns a negative value** (an encoding error) — the message becomes the empty string, `ENGINE_LOG_RECORD_TRUNCATED` is set, and the record is still delivered. Dropping it would hide the fact that a log site is malformed.
5. **A sink that logs** — infinite recursion, undefined behaviour, forbidden by CORE-LOG-038 and deliberately not guarded, because a guard needs thread-local mutable state (CORE-LOG-001).
6. **A sink registered twice** — rejected with `ENGINE_RESULT_INVALID_STATE` (CORE-LOG-036). Duplicate delivery is never useful and always confusing.
7. **Two structurally identical sinks at different addresses** — two sinks; both are delivered to, and `remove_sink` removes exactly the one whose pointer is passed (CORE-LOG-033). Identity is the pointer, exactly as in CORE-ALLOC-063.
8. **A sink removed while another thread is logging** — a violation of CORE-LOG-072, not a supported operation. Quiesce the logging threads first.
9. **A sink whose backing object is destroyed while still registered** — undefined behaviour, undetectable (CORE-LOG-004). Unregister before destroying.
10. **The table is full** — `ENGINE_RESULT_INVALID_STATE`; the ninth sink is not silently dropped and the earlier eight are unaffected (CORE-LOG-036).
11. **Logging on the out-of-memory path** — fully supported and is the reason for CORE-LOG-008: no allocation occurs anywhere in `engine_log_write`.
12. **Logging before `platform_initialize`** — supported; the record carries the two "unknown" sentinels (CORE-LOG-040/082). *Amended by `platform.md` PLATFORM-026:* with the sources installed right after `engine_log_init`, the record carries real values; the sentinels apply only before installation.
13. **A time source that goes backwards** (a caller installed a wall clock) — a violation of CORE-LOG-043. The engine does not check it; log order becomes misleading, and that is the caller's error, exactly as CORE-ALLOC-032's foreign pointer is.
14. **`ENGINE_LOG_LEVEL_OFF` passed as a record's level** — forbidden by CORE-LOG-011. The macros cannot produce it; a direct `engine_log_write` call that does is a caller bug and the record is discarded.
15. **A release build wanting `TRACE`** — define `ENGINE_LOG_COMPILE_MIN_LEVEL=0` for that build. The compile-time default is a default, not a rule (CORE-LOG-015).
16. **A toolchain warning on the always-false level comparison** — cannot arise: CORE-LOG-060 selects between two macro *definitions* with `#if`, so the disabled form contains no comparison at all.
17. **A subsystem that wants to log from an `EngineAllocatorAllocFn`** — put the logger pointer in the allocator's own `user_data` struct (§2.1). The callback signature is fixed by CORE-ALLOC-002 and is not changed by this spec.

---

## 13. Migration impact on existing code

Recorded with the same rigour as types.md §9, result.md §10, and allocator.md §12.

### 13.1 Are today's `printf`/`fprintf` sites a violation?

**No — and stating that explicitly is the point.** `main.c` lines 10/14/17/20/25/29/33 use `printf`/`fprintf(stderr, …)`; `engine.c` and `renderer.c` currently use none. Under this spec:

- **`main.c` is out of scope, permanently.** It is the application, not the engine. It is not under `src/core` or `src/renderer`, so LOG-INV-9's grep does not reach it, and an application writing to its own stdout is not an architectural violation. Migrating it to `ENGINE_LOG_*` is *recommended* — it is the natural first consumer and the natural demonstration of §9's ordering — but it is not required by any invariant here.
- **`src/core` and `src/renderer` are in scope from the moment LOG-INV-9's check is added**, and they pass it today with zero edits, because neither currently writes to a stream. This spec therefore creates **no retroactive violation anywhere**: it closes a door before anyone walks through it, which is the cheap moment to do it.

This mirrors how types.md handled `int` (a forward rule with named exceptions, not a repo-wide rewrite) and how result.md handled `err != 0` (correct today, migrated for style, not a correctness bug).

### 13.2 What adopting this spec actually costs

| File | Change | Cost |
|---|---|---|
| `src/core/logging.h` (new) | Levels, record, sink, logger struct, the function surface, `engine_log_is_enabled`, the six macros, `ENGINE_PRINTF_FORMAT`, LOG-INV-1/2's `static_assert`s. | new, ~140 lines |
| `src/core/logging.c` (new) | `engine_log_init/shutdown/add_sink/remove_sink/set_level/get_level/set_context_source/write/write_v/flush`, `engine_log_level_name`. | new, ~150 lines |
| `src/core/log_console.c` (new) | The `static const EngineLogSink` singleton and its one-`fprintf` `write`. The sole `<stdio.h>` consumer in core. | new, ~40 lines |
| `src/core/CMakeLists.txt` | Add the three sources to `engine_foundation`; add `forbid_direct_output(engine_foundation … "log_console.c")` and the same over `src/renderer`. | ~6 lines |
| `cmake/Common.cmake` | A `forbid_direct_output` macro beside `forbid_direct_allocation`, same `forbid-pattern.sh` shape (LOG-INV-9). | ~12 lines |
| `src/core/engine.h`, `engine.c` | `engine_init` gains `const EngineLogger *logger`; `struct Engine` gains a stored `const EngineLogger *`; the three existing failure returns gain a log line each. | ~12 lines |
| `src/renderer/api.h`, `renderer.c` | Same for `renderer_init` / `struct Renderer`. | ~10 lines |
| `main.c` | Define an `EngineLogger`, `engine_log_init`, add `engine_console_log_sink()`, pass it to `engine_init`/`renderer_init`, `engine_log_shutdown` last. Optionally replace the seven `printf`/`fprintf` sites. | ~15 lines |
| `tests/contract/test_api_contracts.c` | `engine_init(&e, NULL)` → `engine_init(&e, NULL, NULL)` — CORE-LOG-005 makes `NULL` the correct value for a test that does not care about output. | ~4 lines |
| `tests/` (new) | LOG-INV-3/4/6/7/8/11/12/14 as unit and contract tests over a capture sink; LOG-INV-13 as an integration test with two compile configurations; LOG-INV-5/9/10 as greps. | new |

**Total: three new files, one new CMake macro, and roughly 60 changed lines across six existing files. No behavioural change to anything that works today.**

### 13.3 Sequencing

This spec is **independent of ADR-009's follow-up chain and MUST NOT be folded into it.** Follow-ups #1–#5 are mid-flight and already rewrite `engine_init`'s and `renderer_init`'s signatures; adding a third parameter in the same edit would conflate two unrelated reviews. The correct order is:

1. **ADR-009 #1–#5 complete first.** They own the allocator parameter. This spec adds nothing to them.
2. **CORE-004a — author `logging.h`, `logging.c`, `log_console.c`** (`c-engineer`). No existing file changes; the new sources join `engine_foundation`. Independently reviewable and independently testable against LOG-INV-1/2/3/4/6/7/8/11/12/14.
3. **CORE-004b — the build checks** (`build-engineer`/`test-engineer`): `forbid_direct_output`, LOG-INV-5/9/10/13. Passes on an unmodified `src/core`/`src/renderer` (§13.1), so it can land before any call site adopts logging.
4. **CORE-004c — thread the logger through `engine_init` and `renderer_init`, and adopt it in `main.c`** (`c-engineer`, then `renderer-engineer`). This is the only step that edits shipped signatures, and it is the one that should be weighed against the `EngineInitDesc` question in CORE-LOG-084's note before it is written.

Steps 2 and 3 have no prerequisite beyond this spec; step 4 has no prerequisite beyond step 2.

---

## 14. Out of scope

Explicitly not decided here:

- **Assertions** (`ENGINE_ASSERT`, `ENGINE_VERIFY`, `ENGINE_UNREACHABLE`) — CORE-003. The boundary is stated by CORE-LOG-013: assertions terminate on an *engine* invariant violation; the logger only records, at any severity, including `FATAL`.
- **Concrete sinks other than the console** — file, rotating file, in-memory ring, remote/telemetry, IDE output. All are implementable today as caller-owned `EngineLogSink` values with no change to this contract; that is the point of §5. The one this spec would sanction next is a fixed-capacity in-memory ring for crash dumps.
- **Structured / key-value logging** (`{"device": 3}` fields rather than a formatted string). A real future requirement and a real extension of `EngineLogRecord`; it is not needed by anything in the tree, and adding speculative fields to a public struct is what CORE-RESULT-008 forbids for result codes, for the same reason.
- **Log-level configuration from files, environment variables, or the command line.** Configuration input is the application's job; it calls `engine_log_set_level`. Core reading an environment variable would be both an OS dependency and an ambient input.
- **Per-subsystem level filtering** (`"GPU"` at `TRACE` while everything else is at `WARN`). Implementable today as a caller-written filtering sink that inspects `record->subsystem`; a first-class mechanism would need a subsystem registry, which is a hidden global unless it is threaded like everything else here. Revisit only with a concrete need.
- **Asynchronous / deferred logging** (a queue drained by a worker). Requires PLATFORM-008's threading primitives and a copy of the message (CORE-LOG-022), so it belongs to a sink built above the platform layer, not to this contract.
- **A monotonic clock and a thread identity** — PLATFORM-005 and PLATFORM-008. This spec fixes only how they reach a record (CORE-LOG-040..043).
- **An atomic runtime level** — deferred by CORE-LOG-073, with the explicit note that adopting it later is a source-compatible strengthening.
- **The shape of `engine_init`'s parameter list** (three parameters versus an `EngineInitDesc`) — `docs/spec/core/engine.md`, flagged in CORE-LOG-084.
