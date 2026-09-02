# Core Result / Error — Contract Specification

**Status:** Accepted
**Date:** 2026-08-25
**Author:** `spec-writer`
**Scope:** the single result/error type returned by every fallible engine operation, in core, renderer, platform API, GPU API, and all backends.
**Supersedes:** specs.md §10 ("Error Handling") and specs.md §66 CORE-002 ("Result System"). specs.md remains a historical draft; where it disagrees with this document, **this document wins**.
**Depends on:** `docs/spec/core/types.md` (Accepted) — CORE-TYPES-021/022/023 fix the type name `EngineResult`, CORE-TYPES-024 fixes enumerator style, CORE-TYPES-025 governs enum width. This spec decides only what types.md left open: *the shape*.
**Related:** CLAUDE.md invariants #3, #4, #5, #6; `rules/core.md` ("Return explicit errors: `int error` or `error_t`, never silent"); ADR-009 (allocator ownership) — see §7.

---

## 1. Problem

Three sources describe the result type and none of them agree with the shipped code:

| Source | Shape | Codes |
|---|---|---|
| `rules/core.md` | "`int error` or `error_t`" — deliberately non-committal | — |
| specs.md §10 | `enum EngineResult` + optional `struct EngineStatus { code; message; }` | `ENGINE_SUCCESS`, `ENGINE_ERROR_*` (9 codes) |
| `src/core/engine.h:10` (shipped) | `typedef int32_t engine_error_t;` | none — informal `0` / `-1` / `1` |

The shipped convention is the real gap. `engine.c` returns `-1` for a null pointer (lines 18, 32, 44) and `1` for two entirely different failures — allocation failure (line 23) and calling `engine_tick` on an uninitialized engine (line 36). A caller cannot tell those apart, and neither can a test: `tests/contract/test_api_contracts.c:44` can only assert "returns *something* non-zero", which would pass even if the implementation returned the wrong failure.

CORE-002's acceptance criterion — "all core operations can return explicit failure" — is met only in the weakest possible sense by a value whose meaning is undocumented. This spec fixes the shape, the code set, and the producer/consumer contract around it.

---

## 2. Decision 1 — Shape: a named `enum`, not a bare `int32_t`

**`EngineResult` is a C enumeration with named constants. The bare `typedef int32_t engine_error_t` is not carried forward.**

```c
typedef enum EngineResult {
    ENGINE_RESULT_SUCCESS = 0,
    ENGINE_RESULT_INVALID_ARGUMENT,
    ENGINE_RESULT_INVALID_STATE,
    ENGINE_RESULT_OUT_OF_MEMORY,
    ENGINE_RESULT_NOT_SUPPORTED
} EngineResult;
```

### Rationale

**For keeping the bare `int32_t` (the cheapest option):** it is what ships; its width is guaranteed by the standard rather than implementation-defined; it cannot break a single line of existing code; and it lets any subsystem invent codes without editing a core header. These are real advantages and the third is the only one that survives scrutiny.

**Against it, decisively:**

1. **It cannot express the thing the invariant asks for.** "Never silent" is not satisfied by returning a number whose meaning lives in a comment. Distinguishing "you passed NULL" from "the engine is not initialized" from "allocation failed" is exactly what a caller must do to respond correctly — retry, abort, or fix the call — and today all three are indistinguishable at two of the three call sites.
2. **Tests can only assert what the type can express.** The enum lets `test_api_contracts.c` assert `engine_tick(NULL) == ENGINE_RESULT_INVALID_ARGUMENT` instead of `!= 0`. That converts a test that passes on a wrong answer into one that does not — the single largest concrete gain here.
3. **`-Wswitch` becomes an ally.** A `switch` over `EngineResult` that omits a code warns at compile time. Adding a code to a bare `int32_t` typedef notifies nobody.
4. **The subsystem-invents-its-own-codes advantage is one this architecture must refuse anyway.** CLAUDE.md invariant #4 keeps backend detail opaque to core and renderer; a numeric space that backends can extend at will is precisely how `VkResult` values leak upward as magic numbers. §5 makes the closed set a rule, not an accident.
5. **specs.md §10 already asked for this**, and no source anywhere asked for the bare typedef — it is an artifact of a stub, like `engine_set_allocators()` before ADR-009.

**The honest cost, and why it is smaller than it looks.** types.md's migration table anticipated that changing the shape "breaks every existing comparison against bare 0/1/-1". In C it does not: an enumerated type is compatible with an integer type, so `EngineResult err = 0;`, `if (err != 0)`, and `printf("%d", err)` all continue to compile and to mean the same thing. Every one of the six such sites in `main.c`, `test_api_contracts.c`, and `test_core_invariants.c` survives the shape change untouched; what breaks them is the *rename* from `engine_error_t`, which types.md already decided and costed. The shape change is close to free. §9 records this concretely rather than inheriting the pessimistic estimate.

The genuine costs are two: the underlying integer type of a C17 enum is implementation-defined (CORE-TYPES-025), addressed by CORE-RESULT-003; and every new code is now a public-header edit, which is intended friction, not overhead.

### Rules

**CORE-RESULT-001.** Every fallible engine operation returns `EngineResult` by value. There is exactly one result type across core, renderer, platform API, GPU API, and every backend. Per-subsystem result types (`RendererResult`, `GpuResult`) MUST NOT be introduced: a second type forces translation at every layer boundary and forces callers to know which one they hold, for codes that are generic in any case.

**CORE-RESULT-002.** `ENGINE_RESULT_SUCCESS` is `0` and is the only success value. All failure codes are **positive**. Negative values MUST NOT be used — the shipped `-1` convention is retired, because a mixed-sign space invites `if (err < 0)` checks that silently pass on positive failures.

**CORE-RESULT-003 (width).** The result header asserts once, with `static_assert`, that `sizeof(EngineResult) == sizeof(int32_t)`. On a target where this does not hold the build fails rather than silently changing the return ABI seen by a dynamically loaded backend. Where C23 is the baseline, the fixed-underlying-type form `enum EngineResult : int32_t` is permitted and preferred; it MUST NOT be *required*, since C17 is a supported baseline and both forms produce the same 32-bit value on every supported target.

**CORE-RESULT-004 (value stability).** Only `ENGINE_RESULT_SUCCESS = 0` carries an explicit value; the rest are implicit and sequential. New codes are **appended** at the end. Codes MUST NOT be removed, renumbered, or reordered. Numeric values are an in-process/in-image detail: they MUST NOT appear in any file format, wire protocol, or persisted log field. A textual name (CORE-RESULT-014) is used instead.

**CORE-RESULT-005 (closed set).** The enum is owned by core. Subsystems and backends MUST NOT define additional codes, reserve numeric ranges, or return values outside the enum. A backend translates its SDK's status (e.g. `VkResult`) into one of these codes inside the backend `.c`; the SDK value never crosses the boundary, in either its own type or as an integer (CLAUDE.md invariant #4, `rules/gpu.md`).

**CORE-RESULT-006 (no backend-specific codes).** No code may name a condition specific to one OS or one graphics SDK. `ENGINE_RESULT_DEVICE_LOST` and `ENGINE_RESULT_WIN32_*` are prohibited by construction — naming them in core would re-import the backend detail invariant #4 removes. Conditions common to *all three* GPU backends may be proposed as codes; conditions unique to one are mapped, logged, and reported generically.

---

## 3. Decision 2 — Naming: `ENGINE_RESULT_*`, UPPER_SNAKE

**CORE-RESULT-007.** The type is `EngineResult` (PascalCase, subsystem-prefixed, no `_t` — CORE-TYPES-021/022/023). Enumerators are `UPPER_SNAKE_CASE` prefixed with the SCREAMING form of the type name: `ENGINE_RESULT_<CONDITION>` (CORE-TYPES-024).

### Rationale

Against `EngineResultSuccess` (the Cocoa/Metal style):

1. **An enumerator in C is a global constant sharing one namespace with macros**, and this repository spells every such name in UPPER_SNAKE already: `NAYLA_CORE_SRC_DIR` and `NAYLA_FORBID_PATTERN_SCRIPT` (`cmake/Common.cmake`, `tests/architecture/CMakeLists.txt`), the `ENGINE_H` include guard, and specs.md's `ENGINE_ASSERT` / `ENGINE_VERIFY` / `ENGINE_UNREACHABLE`. Mixed-case enumerators would be the only global constants that do not look like the others.
2. **It preserves the one-glance rule types.md bought.** PascalCase means "a type", UPPER_SNAKE means "a constant", `snake_case` means "a function", lowercase-with-`_t` means "standard library". `EngineResultSuccess` collides visually with the type name it belongs to.
3. `-Wswitch` diagnostics, debugger displays, and grep all work identically either way, so nothing technical breaks the tie; consistency decides it.

### Superseding specs.md §10's spelling

specs.md §10 uses two prefixes for one enum: `ENGINE_SUCCESS` alongside `ENGINE_ERROR_OUT_OF_MEMORY`. **This spec uses `ENGINE_RESULT_` uniformly, including `ENGINE_RESULT_SUCCESS`.** Reasons: CORE-TYPES-024 requires the prefix to match the type; a split prefix means the value set of one type cannot be grepped with one pattern; and a future code that is neither plainly "success" nor plainly "error" (a timeout, a partial result) has no home under either prefix.

The honest cost: `ENGINE_ERROR_OUT_OF_MEMORY` announces failure in the name and `ENGINE_RESULT_OUT_OF_MEMORY` does not. Accepted — the condition names carry that information unambiguously on their own, and CORE-RESULT-002 makes "not `ENGINE_RESULT_SUCCESS`" the single test. A predicate helper (`engine_result_is_error()`) is deliberately **not** adopted: `result != ENGINE_RESULT_SUCCESS` is already the clearest possible spelling and a helper would give callers a second, divergable way to ask.

---

## 4. Decision 3 — The initial code set

Five codes. Each one is required by something already shipped or already normative; nothing is added speculatively.

| Code | Meaning | Justified by |
|---|---|---|
| `ENGINE_RESULT_SUCCESS` | The operation completed. Out-parameters are valid. | Required by CORE-RESULT-002. |
| `ENGINE_RESULT_INVALID_ARGUMENT` | A parameter violates the function's documented precondition: a NULL required pointer, a zero size, an out-of-range index. Caller bug. | `engine.c:18,32,44` (the `-1` cases). |
| `ENGINE_RESULT_INVALID_STATE` | The object exists but is not in a state where this operation is meaningful — chiefly *not initialized*. Caller sequencing bug. | `engine.c:36` (`!initialized`), specs.md §37's ordered lifecycle. |
| `ENGINE_RESULT_OUT_OF_MEMORY` | An allocation through the `EngineAllocator` returned NULL. | `engine.c:23`; ADR-009 §7 below. |
| `ENGINE_RESULT_NOT_SUPPORTED` | The requested capability or format is unavailable on this backend, device, or platform. Not a bug; the documented signal for graceful degradation. | CLAUDE.md invariant #6, `rules/gpu.md` ("Optional features use capability queries"). |

### Explicitly deferred, with reasons

- **`ENGINE_RESULT_ALREADY_SHUTDOWN` — rejected, cannot be implemented honestly.** `engine_shutdown` frees the handle (`engine.c:47`); any later call through that pointer is a use-after-free, i.e. undefined behaviour, and the engine cannot reliably detect it. Defining a code for a condition no implementation can detect would document a guarantee that does not exist. "Already shut down" is a caller contract (CORE-RESULT-012), not a return value. A debug-build poison-and-detect scheme could change this later; it does not exist today.
- **`ENGINE_ERROR_INITIALIZATION_FAILED`** (specs.md §10) — no producer. `engine_init`'s only failure modes are an invalid argument and an allocation failure, both already covered. A generic "init failed" would be strictly less informative than what the caller already gets.
- **`ENGINE_ERROR_IO`, `_PLATFORM`, `_GPU`** (specs.md §10) — no IO, platform, or GPU subsystem exists yet. They are the natural first additions when those land, under CORE-RESULT-004's append rule. Adding them now would create codes that nothing produces and that reviewers cannot check against real call sites.
- **`ENGINE_ERROR_INTERNAL`** (specs.md §10) — deliberately withheld. A catch-all code is the path by which a result enum degrades back into "non-zero means something went wrong"; an internal inconsistency is an assertion's job (CORE-003), not a value returned to a caller who can do nothing with it.

**CORE-RESULT-008.** Adding a code is an amendment to this spec, not an implementation decision. The amendment MUST name at least one concrete call site that produces the code and state what a caller does differently on receiving it.

---

## 5. Decision 4 — `EngineStatus{code, message}` is **rejected**, not deferred

specs.md §10 sketches an optional `struct { EngineResult code; const char *message; }`. **No engine API returns it, and it is not defined.** The requirement it served — specs.md §10's "errors MUST be actionable and SHOULD contain subsystem context" — is met by two other mechanisms named below.

### Rationale

1. **The `const char *` has no answerable owner, which is the whole problem.** Three options and each fails:
   - *Dynamic message* — allocated by whom, freed by whom? Freeing requires the `EngineAllocator`, so every fallible call would need an allocator in scope purely to report failure, and the message must be freed on a path the caller may take before checking the code. Worse, the most important message to produce is the out-of-memory one, produced at exactly the moment allocation cannot succeed.
   - *Pointer into callee-owned storage* — dangles as soon as the object that failed is destroyed, which for `engine_init` failure is immediately.
   - *Thread-local or static scratch buffer* — a hidden global. Forbidden outright by CLAUDE.md invariant #5 and `rules/core.md`, and it re-creates the `errno` model that CORE-002's own acceptance criterion ("no global error state is required") exists to prevent.
2. **A struct return changes the ABI of every fallible call** for a field that would be `NULL` at essentially every current call site. There is no subsystem in the tree that can produce a useful message today.
3. **Two return conventions is worse than one.** With both available, "which functions return `EngineStatus`?" becomes a per-function fact callers must memorize, and layers that mix them must translate.

### What replaces it

**CORE-RESULT-009 (static names only).** The result module exposes exactly one string function:

```c
const char *engine_result_name(EngineResult result);
```

It returns the enumerator's spelling (`"ENGINE_RESULT_OUT_OF_MEMORY"`) for diagnostics, logs, and test failure output.

**CORE-RESULT-010 (message lifetime — the explicit answer).** Every `const char *` returned by the result module has **static storage duration**: it is a string literal in the engine binary, valid for the entire program lifetime, shared by all callers and all threads. The caller MUST NOT free it, MUST NOT modify it, and MAY retain it indefinitely. Dynamically allocated or buffer-backed error strings are prohibited engine-wide. This rule is what makes the dangling-message bug class structurally impossible rather than merely unlikely.

**CORE-RESULT-011.** `engine_result_name` never returns `NULL`, including for a value outside the enum (it returns `"ENGINE_RESULT_UNKNOWN"`). A diagnostic path must not be able to crash while reporting a failure.

**Contextual detail belongs to logging.** "Which file", "which device", "which shader" is CORE-004's job: log at the point of failure, where the context is in scope, and return the code upward. This is the standard split and it keeps context out of the return ABI.

**Reopening condition.** If a subsystem later demonstrates a real need for structured context in the return value, the sanctioned shape is a **caller-provided out-struct with an inline fixed-size character array** that the callee fills — no pointer, therefore no ownership question and no allocation. Reintroducing `const char *message` in a returned struct requires superseding this section.

---

## 6. Producer and consumer contract

**CORE-RESULT-012 (precedence).** When more than one failure applies, the callee returns the first that matches in this order: `INVALID_ARGUMENT` → `INVALID_STATE` → `NOT_SUPPORTED` → `OUT_OF_MEMORY`. Argument validation therefore always precedes state inspection: `engine_tick(NULL)` returns `ENGINE_RESULT_INVALID_ARGUMENT` and never dereferences to discover the state. A fixed order makes the return value a function of the inputs, so tests can assert it.

**CORE-RESULT-013 (out-parameters).** On any non-`SUCCESS` return the callee MUST NOT leave an out-parameter holding a value the caller could mistake for a result: pointer out-parameters are set to `NULL` or left unmodified, never to a partially constructed object. Callers MUST NOT read an out-parameter after a non-`SUCCESS` return. An operation that fails MUST release everything it acquired before returning — a failing constructor leaks nothing.

**CORE-RESULT-014 (checking).** Callers compare against the named constant: `if (result != ENGINE_RESULT_SUCCESS)`. `!result`, `result < 0`, and truthiness tests are non-conforming in new code. (`result != 0` remains *correct* — CORE-RESULT-002 guarantees it — so existing sites are a cosmetic migration, not a correctness bug.)

**CORE-RESULT-015 (nodiscard).** Every declaration returning `EngineResult` is annotated with `ENGINE_NODISCARD`, defined once in the result header: `[[nodiscard]]` under C23, `__attribute__((warn_unused_result))` under GCC/Clang, empty otherwise. A silently discarded result is exactly the "silent failure" `rules/core.md` forbids, and the compiler can catch it for free.

**CORE-RESULT-016 (no global error state).** There is no `errno`-equivalent, no thread-local last-error, and no `engine_get_last_error()`. The result value is the complete report. `EngineResult` is a plain value: passing, copying, and comparing it is thread-safe with no synchronization, and two threads failing concurrently cannot interfere. This is the direct satisfaction of CORE-002's second acceptance criterion.

**CORE-RESULT-017 (infallible operations return `void`).** A function with no failure mode MUST NOT return `EngineResult` merely for symmetry — a result that is always `SUCCESS` trains callers to stop checking. Widening `void` to `EngineResult` later is source-compatible for callers; narrowing is not, so `void` is the safe starting point.

---

## 7. Decision 5 — Interaction with ADR-009's allocator

**CORE-RESULT-018.** `EngineAllocator`'s `alloc(allocator, size, alignment)` **keeps returning `void *`, with `NULL` meaning failure.** It does not return `EngineResult`. `free(allocator, memory)` returns `void`.

*Rationale.* The allocation function's return value already *is* its result: it has exactly one failure mode (the request could not be satisfied) and `NULL` names it unambiguously, so `EngineResult alloc(..., void **out)` would add an out-parameter write to the hottest path in the engine while conveying no information the caller did not already have. `rules/core.md`'s allocation-light hot-path requirement points the same way.

Note what is *not* the argument here: there is no dependency obstacle. The result header is a dependency-free foundation leaf — it includes nothing beyond `<assert.h>` for its one `static_assert` — so ADR-009's `allocator.h`, itself foundation, could include it without inverting specs.md §4.1. The choice is made on ergonomics and information content, not on layering.

**CORE-RESULT-019 (translation at the operation boundary).** Any operation that returns `EngineResult` and receives `NULL` from an allocation MUST return `ENGINE_RESULT_OUT_OF_MEMORY`. A `NULL` allocation result MUST NOT be propagated to a caller of an `EngineResult`-returning function as a `NULL` out-parameter with a `SUCCESS` return. This is what `engine_init` already does structurally (`engine.c:22-24`); this rule names it.

**ADR-009 requires no amendment.** Its interface, its `NULL`-selects-default rule, and its follow-up list are unchanged by this spec. The only textual interaction is that its follow-up #3 is the right place to perform this spec's migration (§9).

---

## 8. Invariants

Testable statements, in the style of `rules/testing.md` and types.md §7.

**RESULT-INV-1.** `ENGINE_RESULT_SUCCESS == 0`. *(static_assert)*

**RESULT-INV-2.** `sizeof(EngineResult) == sizeof(int32_t)`. *(static_assert in the foundation module — this replaces the runtime `sizeof` check at `tests/architecture/test_core_invariants.c:33`, which cannot fail once the assertion exists.)*

**RESULT-INV-3.** Every enumerator of `EngineResult` other than `SUCCESS` has a positive value. *(static_assert)*

**RESULT-INV-4.** No public header declares a function returning a bare `int` or `int32_t` as a status. *(grep: `^int32_t [a-z_]*_(init|shutdown|create|destroy)` — catches `src/renderer/api.h`'s current form)*

**RESULT-INV-5.** No engine header declares a result-like typedef other than `EngineResult` — no `RendererResult`, `GpuResult`, `PlatformResult`, or surviving `engine_error_t`. *(grep)*

**RESULT-INV-6.** No source under `src/core` or `src/renderer` contains `return -1;` or a bare `return 1;` from an `EngineResult`-returning function. *(grep)*

**RESULT-INV-7.** `engine_result_name()` returns non-`NULL` for every enumerator and for at least one out-of-range value. *(unit test)*

**RESULT-INV-8.** `engine_tick(NULL)` returns exactly `ENGINE_RESULT_INVALID_ARGUMENT`, not merely non-zero. *(contract test — the concrete strengthening of `test_api_contracts.c:44`)*

**RESULT-INV-9.** No symbol named `*_last_error*` or `*_get_error*` exists, and no file-scope mutable variable holds a result. *(grep — CORE-RESULT-016)*

**RESULT-INV-10.** No engine API returns a struct containing a `const char *` message. *(review-enforced — CORE-RESULT-010)*

---

## 9. Edge cases

1. **Enum wider or narrower than 32 bits on some target.** RESULT-INV-2's `static_assert` fails and the build stops. Intended: a silent width change would alter the return ABI seen by a dynamically loaded backend (specs.md ADR-006's territory).
2. **A value outside the enum arrives** (a stale backend binary, a corrupted value). `switch` statements MUST have a `default` that treats an unknown value as a failure, never as success; `engine_result_name` returns `"ENGINE_RESULT_UNKNOWN"` (CORE-RESULT-011). Only `ENGINE_RESULT_SUCCESS` means success — "not a recognized failure" does not.
3. **A backend SDK code with no engine equivalent.** Mapped to the nearest of the five codes and logged with the SDK value inside the backend `.c`. The SDK value never crosses the boundary (CORE-RESULT-005, invariant #4). If the mapping is genuinely lossy in a way that changes caller behaviour, that is a GPU abstraction gap → `gpu-architect`, not a new code invented locally.
4. **Two failures at once** — CORE-RESULT-012's fixed precedence.
5. **`engine_shutdown` on an already-shut-down handle.** Undefined behaviour, not a reported error (§4). The contract is stated in the header; detection is not promised.
6. **Result crossing a C ABI boundary into a script or FFI binding.** The integer value is fine *within one process image*; it MUST NOT be persisted or transmitted (CORE-RESULT-004). Bindings surface `engine_result_name()`.
7. **A failure inside a failure path** (cleanup fails while unwinding an error). The original failure code is returned; the cleanup failure is logged, never substituted. Returning the second code would hide the cause.
8. **`printf("%d", result)`.** Compiles — an enum promotes to `int` — but is discouraged in favour of `%s` with `engine_result_name()`. Not a rule; a review preference.

---

## 10. Migration impact on existing code

Recorded with the same rigour as types.md §9 and ADR-009's backward-compatibility section.

| File | Change | Cost |
|---|---|---|
| `src/core/engine.h` | Line 10 `typedef int32_t engine_error_t;` deleted (the enum moves to a new foundation header, `src/core/result.h`); lines 17/20/23 return `EngineResult`, each gaining `ENGINE_NODISCARD`; the "Returns 0 on success" comments on lines 9/16/19/22 are rewritten to name codes. | ~8 lines |
| `src/core/engine.c` | Lines 16/30/42 signatures; eight `return` statements: `-1` at 18/32/44 → `ENGINE_RESULT_INVALID_ARGUMENT`; `1` at 23 → `ENGINE_RESULT_OUT_OF_MEMORY`; `1` at 36 → `ENGINE_RESULT_INVALID_STATE`; `0` at 27/39/48 → `ENGINE_RESULT_SUCCESS`. | ~12 lines |
| `src/core/result.h` (new) | The enum, `ENGINE_NODISCARD`, the two `static_assert`s, and the `engine_result_name` declaration. Dependency-free foundation leaf per ADR-009's placement rule. | new, ~35 lines |
| `src/core/result.c` (new) | `engine_result_name` — a `switch` returning literals. | new, ~15 lines |
| `main.c` | Line 6 `engine_error_t err` → `EngineResult`; lines 9/16/25 `err != 0` → `!= ENGINE_RESULT_SUCCESS`; lines 10/17/26 `%d` → `%s` with `engine_result_name(err)`. | ~7 lines |
| `tests/contract/test_api_contracts.c` | Line 9 declaration; lines 15/26/37 comparisons; line 44 strengthened to `err != ENGINE_RESULT_INVALID_ARGUMENT` (RESULT-INV-8); **line 29 `engine_shutdown(e);` discards a result and will warn under CORE-RESULT-015** — assign and check it. | ~9 lines |
| `tests/architecture/test_core_invariants.c` | Line 32 declaration; lines 33–37 runtime `sizeof` check retired in favour of RESULT-INV-2's `static_assert`; messages on lines 37/45/53/60 name `EngineResult`. | ~10 lines |
| `src/renderer/api.h`, `src/renderer/renderer.c` | `int32_t renderer_init/renderer_shutdown` → `EngineResult` (CORE-RESULT-001); `return -1` / `return 1` / `return 0` replaced, four sites. | ~10 lines |

**Total: two new small files and roughly 55 changed lines across six existing files. No behavioural change** — every failure that is reported today is still reported, with a name instead of a number.

**The shape change is cheaper than types.md's table implied**, and the reason is worth stating plainly so it is not re-estimated pessimistically later: in C an enumerated type is compatible with an integer type, so none of the existing `err != 0` / `err = 0` / `%d` sites in `main.c`, `test_api_contracts.c`, or `test_core_invariants.c` are *compile* breaks. They are style migrations under CORE-RESULT-014. The only hard breaks are the `engine_error_t` identifier itself (six sites, already costed by types.md) and the discarded return at `test_api_contracts.c:29` under CORE-RESULT-015.

### Sequencing

**Fold this into ADR-009 follow-up #3 ("Migrate core"), not a standalone task.** Follow-up #3 already edits `engine.h`, `engine.c`, `main.c`, and `test_api_contracts.c` to thread `const EngineAllocator *` through and delete `engine_set_allocators`; it necessarily rewrites the same signatures and the same `return` statements. types.md's rename folds into the same edit. Performing all three separately means touching four files three times, with two intermediate states that compile but satisfy no spec.

Allocation of the remaining pieces to existing follow-ups:

- ADR-009 #1 (`build-engineer`) — `result.h`/`result.c` join the foundation target created there. This spec adds a second reason that split is a prerequisite, not a cleanup.
- ADR-009 #3 (`c-engineer`) — core migration, plus authoring `result.h`/`result.c`. The result header must land *before* the allocator work in the same task, since `engine_init`'s new signature returns `EngineResult`.
- ADR-009 #4 (`renderer-engineer`) — the renderer row above, alongside removing `malloc`/`free`.
- ADR-009 #5 (`test-engineer`) — RESULT-INV-4/5/6/9 join the grep-based architecture checks in `cmake/Common.cmake`; RESULT-INV-7/8 are ordinary tests; the retired runtime `sizeof` check becomes RESULT-INV-2's `static_assert`.

This spec is a **prerequisite** for follow-up #3, alongside types.md. Neither competes with ADR-009.

---

## 11. Out of scope

Explicitly not decided here:

- **Assertions** (`ENGINE_ASSERT` / `ENGINE_VERIFY` / `ENGINE_UNREACHABLE`) — CORE-003. The boundary is stated, though: assertions are for *engine* invariant violations that indicate a bug in the engine; `EngineResult` is for conditions a *caller* can encounter and must handle. A NULL argument from a caller is `ENGINE_RESULT_INVALID_ARGUMENT`, not an assertion failure.
- **Logging, log levels, and contextual failure messages** — CORE-004, which is where §5 sends the "actionable and contains subsystem context" requirement.
- **Capability query API shape** (what is queried, how results are enumerated) — the GPU and platform specs. This spec fixes only that unavailability is reported as `ENGINE_RESULT_NOT_SUPPORTED`.
- **`EngineAllocator`'s field layout and semantics** — ADR-009, Accepted; unchanged by §7.
- **Error codes for IO, platform, and GPU subsystems** — added under CORE-RESULT-004/008 when those subsystems land.
