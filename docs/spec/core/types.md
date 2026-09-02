# Core Types — Contract Specification

**Status:** Accepted
**Date:** 2026-08-25
**Author:** `spec-writer`
**Scope:** the primitive type vocabulary and type-naming convention for every engine subsystem (core, renderer, platform API, GPU API, and all backends).
**Supersedes:** specs.md §9 ("Fundamental Type System"), specs.md §66 CORE-001 ("Fundamental Types"), and the type-naming clause of specs.md §50 ("Coding Standards"). specs.md remains a historical draft; where it disagrees with this document on those topics, **this document wins**.
**Related:** CLAUDE.md invariants #3 and #5; `rules/core.md`; ADR-009 follow-up #6 (this spec is the type-system half of that follow-up; `EngineAllocator`'s own naming is resolved here only insofar as the general convention applies to it).

---

## 1. Problem

Two normative sources disagree, and shipped code follows neither cleanly:

| Source | Fixed-width types | Type naming |
|---|---|---|
| specs.md §9 / CORE-001 | short aliases `u8`/`u32`/`f32`/`b8` | — |
| specs.md §50 | — | `PascalCase` (`GpuBuffer`) |
| `src/core/engine.h` (shipped) | direct `uint32_t`/`int32_t`/`uint64_t`, **no aliases defined or used** | `snake_case_t` (`engine_error_t`) |

ADR-009 flagged the naming half of this as unresolved and deferred it (follow-up #6). Nothing can be decided about allocator, result, container, or GPU headers until the vocabulary they are written in is fixed. This spec fixes it.

---

## 2. Decision 1 — Fixed-width integers: standard `<stdint.h>` names, no engine aliases

**The engine uses the C standard exact-width type names directly. It defines no `u8`/`u32`/`f32`/`b8` aliases, and adding such aliases later is prohibited without superseding this spec.**

### Rationale

Weighing both costs honestly, as required:

**For the short aliases (specs.md §9's sketch):** they are shorter, they read uniformly in dense math and vertex-layout code, and they were the original stated intent. Discarding them is a real loss of the terseness §9 wanted, and it makes the engine look less like the game-engine houses it was modelled on.

**Against them, decisively:**

1. **They are unnamespaced identifiers in a public header.** `u32`, `f32`, and `b8` are among the most commonly `typedef`'d and `#define`'d names in the C ecosystem. Any application that includes an engine header alongside another library that spells them differently gets a hard conflict in a header the application does not own. An engine whose entire premise is hard boundaries must not spray three-character global names into its consumers' translation units. Namespacing them (`engine_u32`) recovers safety but destroys the only advantage they had.
2. **They buy no guarantee that `<stdint.h>` does not already give.** `uint32_t` is defined by the standard to be exactly 32 bits, no padding, two's complement — or not defined at all, so a non-conforming target fails to compile rather than silently mis-sizing. CORE-001's acceptance criterion "static assertions validate expected sizes" is therefore satisfied *more strongly* by using `<stdint.h>` directly than by aliasing it and asserting on the alias.
3. **CLAUDE.md invariant #3 does not require them.** "Public APIs expose only engine-defined types, never SDK types" targets `VkDevice`, `ID3D12Resource`, `HWND`. `uint32_t` is a standard-library type, not an SDK type; aliasing it satisfies no invariant.
4. **An alias header is a dependency every module must acquire** — including the dependency-free foundation leaf ADR-009 requires — in exchange for zero semantic content.
5. **It is what is already shipped.** `engine.h`, `engine.c`, `renderer.c`, `api.h`, and `tests/architecture/test_core_invariants.c` all already use `int32_t`/`uint32_t`/`uint64_t`. Adopting aliases means editing every existing numeric declaration in the repository to gain nothing checkable.

Point 5 alone would be a weak argument (the repo is small; migration is cheap). Points 1–3 are the decision; point 5 only confirms that the cheap path and the correct path coincide here.

### Rules

**CORE-TYPES-001.** All integer data in public headers, core, renderer, and API layers uses the exact-width types from `<stdint.h>`: `int8_t`, `int16_t`, `int32_t`, `int64_t`, `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`.

**CORE-TYPES-002.** The bare types `int`, `unsigned`, `short`, `long`, `long long`, and their `unsigned` forms MUST NOT be used for engine data — not as struct fields, not as parameters, not as return types. Permitted exceptions, exhaustively:
- `int main(void)` and its `argc`;
- the immediate call site of a C standard library or platform function whose signature demands them (e.g. an `int` file descriptor inside a platform backend). The value MUST be converted to a fixed-width type before it crosses any engine API boundary.

**CORE-TYPES-003.** The minimum-width (`int_least32_t`) and fastest-minimum-width (`int_fast32_t`) families MUST NOT appear in public headers or in any struct whose layout crosses a module boundary: their width varies per target, so they would make struct layout target-dependent. They are permitted inside a single `.c` file for local loop counters where the author has a measured reason.

**CORE-TYPES-004.** `char` is reserved for text: `char *` / `const char *` denotes NUL-terminated UTF-8. Opaque binary bytes are `uint8_t`. Neither `char` nor `signed char`/`unsigned char` is used as a small arithmetic integer; use `int8_t`/`uint8_t`.

**CORE-TYPES-005.** Every public header includes `<stdint.h>` (and `<stddef.h>` / `<stdbool.h>` as needed) explicitly. Transitive inclusion MUST NOT be relied upon.

---

## 3. Decision 2 — Floating point: `float` and `double`, no `f32`/`f64` aliases

**CORE-TYPES-006.** Floating-point data uses `float` and `double` directly. No aliases are defined.

**CORE-TYPES-007.** `float` MUST be IEEE-754 binary32 and `double` MUST be IEEE-754 binary64 on every supported target. This is asserted once in the foundation module (§7), not assumed.

**CORE-TYPES-008.** `long double` MUST NOT appear anywhere in the engine. Its width and precision vary across targets and ABIs (64, 80, or 128 bits), so it cannot appear in a portable layout or a stable signature.

**CORE-TYPES-009.** `float` is the default scalar for geometry, transforms, colors, and time deltas. `double` is used only where a specific requirement demands it (e.g. an absolute wall-clock timeline), and that requirement MUST be stated at the declaration.

*Rationale:* unlike the integers, `float`/`double` already carry no width ambiguity worth aliasing away — C fixes them to two types, and `f32` is exactly as long as `float`. The alias would exist purely for visual symmetry with `u32`, which §2 removed.

---

## 4. Decision 3 — Boolean

**CORE-TYPES-010.** Boolean values in APIs and in-memory structs use `bool` (from `<stdbool.h>` under C17; built-in under C23). The `b8` alias from specs.md §9 is not adopted.

**CORE-TYPES-011.** `bool` MUST NOT be used in any struct whose byte layout is serialized, memory-mapped, uploaded to the GPU, or exchanged across a dynamically loaded backend boundary. `sizeof(bool)` is implementation-defined. Such layouts use `uint8_t` with the values `0` (false) and `1` (true); any other value is invalid input, not "true".

**CORE-TYPES-012.** Bit flags are `uint32_t` (or `uint64_t` when more than 32 flags are needed), never `bool` fields and never bitfields — bitfield allocation order is implementation-defined and therefore not portable across backends.

---

## 5. Decision 4 — Pointers, sizes, and offsets

**CORE-TYPES-013 (pointer size).** No code may assume a size for any pointer type. Specifically: a pointer MUST NOT be stored in, cast to, or round-tripped through `uint32_t`, `uint64_t`, or any other fixed-width integer.

**CORE-TYPES-014.** Where an integer representation of a pointer is genuinely unavoidable (alignment arithmetic inside an allocator), use `uintptr_t`, keep the round trip within a single translation unit and a single process, and never persist, serialize, or transmit the value. `uintptr_t` MUST NOT appear in any public header.

**CORE-TYPES-015.** Pointer differences use `ptrdiff_t`. The POSIX type `ssize_t` MUST NOT be used — it is not a C standard type and is absent on Windows toolchains.

**CORE-TYPES-016 (`void *`).** Consistent with `rules/core.md` ("avoid void pointers"), `void *` is permitted only for genuinely untyped raw memory: the return of an allocation, the memory parameter of a free, and the `user_data` payload of a caller-supplied callback (e.g. ADR-009's `EngineAllocator::user_data`). It MUST NOT be used as a generic handle, as a stand-in for a type the engine could name, or to erase a type across an API boundary — opaque types use forward-declared incomplete types instead (§6, CORE-TYPES-021).

### `size_t` versus a fixed-width size type

This is the one place where "always fixed-width" and "always portable" pull against each other, so it is decided explicitly rather than left to each header.

**CORE-TYPES-017 (in-memory sizes are `size_t`).** Byte counts, object sizes, alignments, element counts, and indices of in-memory objects use `size_t`. `size_t` is by definition able to represent the size of any object the host can allocate; a fixed-width choice is either too narrow on a 64-bit host (`uint32_t`) or lossy on a 32-bit host (`uint64_t`, which must be narrowed before it can reach `malloc`, `memcpy`, or any standard call).

**CORE-TYPES-018 (host-independent sizes are `uint64_t`).** Quantities that are *not* bounded by the host address space use `uint64_t`: file sizes and offsets, asset and archive offsets, GPU buffer sizes and offsets, GPU memory heap sizes, and frame/timestamp counters. A 32-bit host must still be able to describe a 6 GB asset or a 8 GB device heap.

**CORE-TYPES-019.** Conversions between the two categories are explicit, checked, and one-directional at a named boundary. Narrowing a `uint64_t` to `size_t` MUST be range-checked and MUST return an explicit error on overflow; it MUST NOT be a silent cast.

**CORE-TYPES-020.** `size_t` and `ptrdiff_t` are standard types and are therefore *not* subject to the "engine-defined types only" reading of CLAUDE.md invariant #3, exactly as `uint32_t` is not (§2 rationale, point 3).

*Consequence for ADR-009:* `EngineAllocator`'s `alloc(allocator, size, alignment)` takes `size_t size, size_t alignment`. The shipped `engine_alloc_fn(uint64_t size)` — which `engine.c` already has to narrow with `malloc((size_t)size)` — is on the wrong side of CORE-TYPES-017. ADR-009 deletes that typedef outright, so this costs nothing extra.

---

## 6. Decision 5 — Type naming convention

**PascalCase, subsystem-prefixed, for every engine-defined `typedef`'d type. The `snake_case_t` form used by `engine_error_t` is not carried forward.**

### Rules

**CORE-TYPES-021.** Every engine-defined struct, union, enum, and function-pointer type is exposed as a PascalCase typedef whose tag and typedef name are identical:

```c
typedef struct EngineAllocator EngineAllocator;   /* opaque or defined below */
typedef enum EngineResult EngineResult;
```

Opaque types are forward-declared in the public header in exactly this form and defined in a `.c` file. Callers write `Engine *`, never `struct Engine *`.

**CORE-TYPES-022.** Type names carry their subsystem as a prefix: `Engine*` for core, `Gpu*` for the GPU API, `Platform*` for the platform API, `Renderer*` for the renderer. The prefix is the namespace; it is not optional, because these names are global.

**CORE-TYPES-023.** The `_t` suffix MUST NOT be used on engine type names. POSIX reserves the `_t` suffix in the global namespace for the implementation, so `engine_error_t`-style names are formally squatting on reserved space, and mixing them with `<stdint.h>`'s genuine `_t` types blurs which types are standard and which are ours.

**CORE-TYPES-024.** Functions remain `snake_case` and constants/macros remain `UPPER_CASE`, per specs.md §50. Enumerators are `UPPER_CASE` with a subsystem prefix matching their type (`ENGINE_RESULT_OUT_OF_MEMORY`). Struct fields are `snake_case`.

**CORE-TYPES-025.** Enumerations MUST declare an explicit `= 0` first enumerator, and their numeric width MUST NOT be relied upon: under C17 the underlying type of an enum is implementation-defined. Where an enum value is stored in a serialized, memory-mapped, or cross-backend layout, store it as an explicit `int32_t` and convert at the boundary.

### Rationale

The migration-cost argument is *not* what decides this, and pretending otherwise would be dishonest: the repository contains exactly one type this rule renames (`engine_error_t`), so either convention is cheap today. The decision rests on three other things:

1. **ADR-009 is already written in PascalCase.** It specifies `EngineAllocator` (quoting specs.md §12) as the interface every subsystem will take as a construction parameter. Choosing `engine_allocator_t` here would require reopening an Accepted ADR — the opposite of what its follow-up #6 asked for.
2. **The forward cost is asymmetric.** specs.md's platform and GPU sections sketch dozens of types (`GpuBuffer`, `GpuDevice`, `GpuBufferDesc`, `PlatformWindow`, …), all PascalCase, and essentially none of them are written yet. Choosing `snake_case_t` migrates one shipped identifier but re-spells every unwritten type in the specification. Choosing PascalCase migrates one shipped identifier and leaves the specification intact.
3. **It makes the boundary visible.** With `<stdint.h>` types used bare (§2), a reader sees `uint32_t` and knows it is standard, `EngineResult` and knows it is ours. A single visual rule separates the two vocabularies with no lookup. That property is only available if the engine's own types do *not* also end in `_t`.

The honest cost: PascalCase types beside `snake_case` functions is not the most common style in pure-C systems code, and `Engine *e` next to `uint32_t n` will look mixed to a C purist. That is accepted, in exchange for points 1–3.

---

## 7. Invariants

Testable statements. Each is intended to become an architecture test (`rules/testing.md`, alongside the existing OS/GPU include checks in `cmake/Common.cmake`).

**TYPE-INV-1.** No engine source or header defines or uses `u8`, `u16`, `u32`, `u64`, `i8`, `i16`, `i32`, `i64`, `f32`, `f64`, or `b8`. *(grep)*

**TYPE-INV-2.** No public header under `src/` declares a parameter, return type, or struct field of type `int`, `unsigned`, `short`, `long`, `long long`, or `long double`. *(grep, modulo the CORE-TYPES-002 exceptions)*

**TYPE-INV-3.** No engine-defined type name ends in `_t`. *(grep for `typedef .*_t;`)*

**TYPE-INV-4.** No public header mentions `uintptr_t`. *(grep)*

**TYPE-INV-5.** `sizeof(float) == 4`, `sizeof(double) == 8`, `CHAR_BIT == 8`, and `sizeof(size_t) == sizeof(void *)` hold on every target. *(one `static_assert` block in the foundation module — the only place these are asserted)*

**TYPE-INV-6.** `<stdint.h>` provides all eight exact-width types on every supported target. *(implicit: the build fails to compile if not)*

**TYPE-INV-7.** No `bool` field appears in any struct that is serialized, memory-mapped, or passed across a dynamically loaded backend boundary. *(review-enforced; not greppable)*

**TYPE-INV-8.** No public header uses `void *` outside allocation results, free parameters, and callback `user_data`. *(review-enforced)*

---

## 8. Edge cases

1. **A target lacking exact-width types.** `<stdint.h>` simply does not define `uint32_t` there, and the build fails at compile time. This is the intended behaviour: a silent fallback would be worse. Such targets are unsupported; adding one requires superseding this spec.
2. **`CHAR_BIT != 8` or non-two's-complement.** Out of scope. C23 mandates two's complement; byte-addressable 8-bit targets are the only supported family (TYPE-INV-5).
3. **32-bit host, >4 GB asset.** Governed by CORE-TYPES-018/019: the asset's size is `uint64_t` throughout; the narrowing to `size_t` happens only at the point where a host-memory buffer is actually requested, is range-checked, and returns an explicit out-of-range error rather than truncating.
4. **A backend SDK type that is `int` (e.g. a POSIX fd, an `HRESULT`).** Stays inside the backend `.c` (CORE-TYPES-002, `rules/platform.md`). What crosses the boundary is an engine-defined type, never the SDK's.
5. **Alignment arithmetic in the allocator.** The one sanctioned `uintptr_t` site (CORE-TYPES-014). It lives in the default heap allocator's `.c`, never in `allocator.h`.
6. **Third-party headers using the banned aliases.** Not the engine's concern; they are included only inside backend `.c` files, and the architecture tests scope to `src/core`, `src/renderer`, and public headers.
7. **`size_t` in a struct shared with a GPU backend loaded dynamically.** `size_t` differs between a 32-bit and 64-bit build but never *within* one process image, and backends are built against the same ABI. Cross-*process* or on-disk layouts use `uint64_t` per CORE-TYPES-018.
8. **Enum used as a GPU-uploaded value.** CORE-TYPES-025: store `int32_t`, convert at the boundary.

---

## 9. Migration impact on existing code

Adopting this spec requires changes to shipped code. Recorded honestly, in the style of ADR-009:

| File | Change | Cost |
|---|---|---|
| `src/core/engine.h` | `typedef int32_t engine_error_t;` → `EngineResult` (name only; the *shape* of the result type is CORE-002's decision, not this spec's). `struct engine` → `typedef struct Engine Engine;`. `engine_alloc_fn` / `engine_free_fn` deleted. | ~4 lines |
| `src/core/engine.c` | Same renames in three function definitions plus the struct definition. | ~6 lines |
| `src/renderer/api.h`, `src/renderer/renderer.c` | `struct renderer` → `Renderer`; the bare `int32_t` returns become `RendererResult`/`EngineResult` when the result spec lands. | ~8 lines |
| `src/main.c` | Declaration site of the engine handle. | 1–2 lines |
| `tests/architecture/test_core_invariants.c` | References `engine_error_t` and asserts `sizeof(err) == sizeof(int32_t)`; renames with the type. | ~3 lines |

**Total: a mechanical rename across five files, no behavioural change, no signature semantics change.**

**Sequencing matters more than the size.** ADR-009 follow-ups #3 and #4 already rewrite every one of these signatures (threading `const EngineAllocator *` through `engine_init`, `engine_shutdown`, and `renderer_init`, and deleting `engine_set_allocators`). Performing this rename **inside** those follow-ups makes its incremental cost approximately zero. Performing it separately means touching the same five files twice.

The fixed-width half of this spec (§2–§5) requires **no migration at all** — `engine.h` and `renderer.c` already use `int32_t`/`uint32_t` directly. The single exception is `engine_alloc_fn`'s `uint64_t size`, which CORE-TYPES-017 would change to `size_t` and which ADR-009 deletes anyway.

**Recommendation:** fold the renames into ADR-009 follow-ups #1–#4 rather than scheduling a standalone rename task. This spec is a prerequisite for those follow-ups, not a competitor to them.

Note on ADR-009's text: its illustrative signature `engine_init(struct engine **owned, const EngineAllocator *allocator)` uses the pre-migration spelling of the handle. That is not a contradiction — ADR-009 explicitly deferred naming to this spec (follow-up #6). Under CORE-TYPES-021 the same signature reads `EngineResult engine_init(Engine **owned_engine, const EngineAllocator *allocator);`. ADR-009's `EngineAllocator` spelling is **confirmed** by CORE-TYPES-021/022, not changed.

---

## 10. Out of scope

Deferred to their own specs, explicitly not decided here:

- **The shape of the result/error type** (`EngineResult` as an enum per specs.md §10 vs. the shipped `int32_t`) — CORE-002 / `docs/spec/core/result.md`. This spec fixes only its *name*.
- **`EngineAllocator`'s field layout and semantics** — ADR-009, already Accepted.
- **Math/vector/matrix types** (`Vec3`, `Mat4`) — a future `docs/spec/core/math.md`. They inherit CORE-TYPES-006/009 and CORE-TYPES-021 without further decision.
- **Handle and ID types** (generational indices, opaque GPU resource handles) — the GPU and resource specs. They inherit CORE-TYPES-013 (never pointer-sized-assumed) and CORE-TYPES-021.
- **String and UTF-8 handling beyond CORE-TYPES-004** — a future `docs/spec/core/strings.md`.
