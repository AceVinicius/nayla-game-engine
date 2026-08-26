# Task 001: Configure Repository Structure

## Agent
`build-engineer`

## Status
Done — repository structure, build system, and `tools/factory` exist and match this spec.

## Context

### Required
- `.claude/CLAUDE.md` (architecture, invariants)
- `.claude/rules/core.md` (core rules)
- `.claude/rules/tooling.md` (build commands)
- `CMakeLists.txt` (root, minimal at task start)
- `main.c` (root, hello-world at task start)

### Optional
None.

## Dependencies
None (foundational task).

---

## Objective

Configure the basic repository structure, build system, test framework, and commands for the game engine. This is a foundational setup task — **no game engine implementation**, only tooling and structure.

---

## Requirements

### Folder Structure

Create and document the following directory layout:

```
nayla-game-engine/
├── CMakeLists.txt (root)
├── main.c (root entry point)
├── cmake/
│   └── Common.cmake (shared CMake macros)
├── src/
│   ├── CMakeLists.txt
│   ├── core/
│   │   ├── CMakeLists.txt
│   │   ├── engine.h
│   │   └── engine.c
│   ├── platform/
│   │   ├── CMakeLists.txt
│   │   ├── api.h (Platform API definition)
│   │   └── [backends will go here]
│   └── gpu/
│       ├── CMakeLists.txt
│       ├── api.h (GPU API definition)
│       └── [backends will go here]
├── tests/
│   ├── CMakeLists.txt
│   ├── unit/ (core unit tests)
│   ├── contract/ (API contract tests)
│   ├── integration/ (subsystem integration tests)
│   └── architecture/ (dependency/invariant checks)
├── docs/
│   ├── spec/ (specifications)
│   ├── adr/ (architecture decision records)
│   └── api/ (API documentation)
└── tools/
    └── factory (build/test CLI — shell-based)
```

### Build System (CMake)

1. **Root `CMakeLists.txt`**:
   - Define C17/C23 standard
   - Set up build types (Debug, Release)
   - Include common CMake macros (`cmake/Common.cmake`)
   - Define main executable target
   - Add subdirectories (`src/`, `tests/`, `tools/`)

2. **`src/CMakeLists.txt`** (and subdirs):
   - Define `libengine_core` target (C17, no OS/GPU deps)
   - Define `libplatform_api` target (abstract Platform API)
   - Define `libgpu_api` target (abstract GPU API)
   - Enforce: core/renderer have no OS/GPU includes (grep checks in CMake)

3. **`tests/CMakeLists.txt`**:
   - Enable CTest
   - Define test targets: unit, contract, integration, architecture
   - Optionally use a test framework (e.g., Unity, or simple C assert-based)

4. **`cmake/Common.cmake`**:
   - Macro to forbid OS/GPU includes in core: `forbid_includes(target_name windows.h vulkan.h d3d12.h metal.h)`
   - Macro to forbid SDK branching in renderer: `forbid_directives(target_name VULKAN D3D12 METAL)`

### Build Commands

Create `tools/factory` shell script with these commands:

```bash
tools/factory status              # Show build status
tools/factory build debug         # cmake -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug && cmake --build cmake-build-debug
tools/factory build release       # cmake -B cmake-build-release -DCMAKE_BUILD_TYPE=Release && cmake --build cmake-build-release
tools/factory test unit           # ctest -R "unit" --output-on-failure
tools/factory test contract       # ctest -R "contract" --output-on-failure
tools/factory test integration    # ctest -R "integration" --output-on-failure
tools/factory test architecture   # ctest -R "architecture" --output-on-failure
tools/factory test all            # ctest --output-on-failure
tools/factory clean               # rm -rf cmake-build-*
tools/factory search <pattern>    # grep -r <pattern> src/ tests/
tools/factory inspect <path>      # Show target details (head of file, or dir listing)
```

### Placeholder Files

Create stub files to show structure:

1. **`src/core/engine.h`** — Core API definition (empty, to be implemented)
   - `struct engine_t;`
   - `int engine_init(struct engine *owned);`
   - `void engine_tick(struct engine *owned);`
   - `void engine_shutdown(struct engine *owned);`

2. **`src/platform/api.h`** — Platform API definition (empty, to be implemented)
   - Abstract interface for OS operations
   - No implementation in core
   - Pointers to backend funcs

3. **`src/gpu/api.h`** — GPU API definition (empty, to be implemented)
   - Abstract interface for GPU operations
   - Implementable by Vulkan/D3D12/Metal
   - No SDK types exposed

4. **`tests/unit/`** — Minimal example test
   - Assert core initializes/shuts down cleanly
   - Assert renderer has no GPU SDK directives

5. **`docs/spec/architecture.md`** — Document the architecture
   - Show the diagram (Core ↔ APIs ↔ Backends)
   - Explain boundaries

6. **`main.c`** — Update entry point
   - Call `engine_init()`, `engine_tick()`, `engine_shutdown()`
   - Still just "Hello, World!" but via the engine API

---

## Acceptance Criteria

- [x] Directory structure matches spec above
- [x] CMakeLists.txt files compile without errors (in Debug mode)
- [x] `tools/factory build debug` works and produces `cmake-build-debug/nayla_game_engine` executable
- [x] `tools/factory test unit` runs without errors (at least one test passes)
- [x] `tools/factory status` shows build status clearly
- [x] Stub header files (`engine.h`, `api.h`) define minimal API contracts
- [x] `main.c` calls engine API (compile-time check only, no implementation)
- [x] CMake checks forbid OS/GPU includes in core (grep-based validation in CMake)
- [x] No warnings from core code on C17/C23 mode
- [x] Documented in README.md how to build/test

---

## Tests

Delivered file names differ from the illustrative names originally sketched below; both describe the same intent.

### Unit Tests (`tests/unit/`)
- `test_core_isolation.c` — asserts `engine_init`/`engine_tick`/`engine_shutdown` behave correctly and core stays portable.

### Contract Tests (`tests/contract/`)
- `test_api_contracts.c` — asserts `engine.h`, `platform/api.h`, `gpu/api.h` expose engine-defined types only (no SDK types).

### Architecture Tests (`tests/architecture/`)
- `test_core_invariants.c` — asserts `src/core` has no OS/GPU includes and no OS-specific link/compile flags.

### Integration Tests (`tests/integration/`)
- Target exists (`CMakeLists.txt`); no test source yet — none was required by the acceptance criteria above.

---

## Non-Goals

- Implement core engine logic
- Implement Platform API backends (Windows/Mac/Linux)
- Implement GPU API backends (Vulkan/D3D12/Metal)
- Implement renderer
- Write comprehensive test suite
- Set up CI/CD pipelines
- Document individual functions (just API contracts)

---

## Review

Required: yes
Reviewer: `reviewer`

**Checklist**:
1. Folder structure matches spec
2. CMake builds without errors/warnings
3. Build commands work (`tools/factory build debug`)
4. Test commands work (`tools/factory test unit`)
5. Invariant checks pass (core has no OS/GPU includes)
6. Architecture is sound (APIs are abstract, core is portable)
7. Scope is tight (setup only, no implementation)

---

## Delivered vs. Spec

- All items above were delivered as specified.
- One addition not in the original folder structure: `src/renderer/` (with `CMakeLists.txt` and `api.h`) was also created, consistent with `docs/spec/architecture.md`'s Renderer subsystem — not required by this task, no action needed.
