# Nayla Game Engine

C17/C23 portable game engine with hard architectural boundaries between core, renderer, and platform/GPU backends.

## Quick Start

```bash
# Configure and build
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug

# Run
./cmake-build-debug/nayla_game_engine

# Test
ctest --test-dir cmake-build-debug --output-on-failure
```

Or use the convenience commands:

```bash
# Build
tools/factory build debug

# Test
tools/factory test unit
tools/factory test all

# Status
tools/factory status
```

## Architecture

Two hard boundaries:

```
Core (C code, no OS/GPU deps) ↔ Platform API ↔ OS Backends (Windows/Mac/Linux)
                           ↔ GPU API ↔ Graphics Backends (Vulkan/D3D12/Metal)
Renderer (GPU API only, no SDK types)
```

See [docs/spec/architecture.md](docs/spec/architecture.md) for details.

## Core Invariants

1. **Core**: no OS or GPU SDK headers.
2. **Renderer**: no GPU SDK branching (Vulkan/D3D12/Metal).
3. **Public APIs**: only engine-defined types, never SDK types.
4. **Backend details**: opaque to core and renderer; isolation enforced.
5. **Ownership/lifetime**: explicit; no hidden global state.
6. **Capabilities**: queried, never assumed; graceful degradation.
7. **Architecture changes**: require ADRs; no implementation hacks.

## Development

See [.claude/CLAUDE.md](.claude/CLAUDE.md) for:
- How to use agents
- When to escalate
- Task structure
- Workflow stages

## Directory Layout

```
nayla-game-engine/
├── CMakeLists.txt (root build config)
├── main.c (entry point)
├── cmake/ (CMake utilities)
├── src/
│   ├── core/ (portable engine core)
│   ├── platform/ (abstract Platform API + backends)
│   └── gpu/ (abstract GPU API + backends)
├── tests/
│   ├── unit/ (core unit tests)
│   ├── contract/ (API contract tests)
│   ├── integration/ (subsystem integration tests)
│   └── architecture/ (dependency/invariant checks)
├── docs/
│   ├── spec/ (specifications)
│   └── adr/ (architecture decisions)
├── tools/
│   └── factory (build/test CLI)
└── .claude/ (Claude Code configuration)
```

## Building

### Requirements

- C17/C23 compiler (gcc, clang, or MSVC)
- CMake 4.3+

### Build Modes

```bash
# Debug (with symbols, no optimization)
tools/factory build debug

# Release (optimized)
tools/factory build release
```

### Output

Executable: `cmake-build-debug/nayla_game_engine` or `cmake-build-release/nayla_game_engine`

## Testing

```bash
# Unit tests (core functionality)
tools/factory test unit

# Contract tests (API contracts)
tools/factory test contract

# Integration tests (subsystem interaction)
tools/factory test integration

# Architecture tests (invariant enforcement)
tools/factory test architecture

# All tests
tools/factory test all
```

## Status

Foundation (result, allocator, logging), core and renderer skeletons, and the Platform API are in place; the macOS platform backend is implemented and conformance-tested. Linux and Win32 backends and the GPU API are in progress.

Work is tracked in [GitHub issues](https://github.com/AceVinicius/nayla-game-engine/issues).
