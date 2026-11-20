# Core Rules

Portable, OS-independent, GPU-independent. No SDK/OS headers, no globals, explicit ownership.

## Language & Portability
- C17/C23 only. No OS (`windows.h`, `unistd.h`) or GPU SDK (`vulkan.h`, `d3d12.h`, `metal.h`) headers.
- Public APIs expose engine-defined types only.
- **Check**: `grep core for #include <windows|vulkan|metal|d3d`

## Ownership & Lifetime
- Ownership explicit: `engine_t *owned`, `const engine_t *borrowed`.
- No hidden globals: all state passed explicitly.
- Return explicit errors: `int error` or `error_t`, never silent.
- Use engine allocators: `engine_alloc()` / `engine_free()`, not `malloc`/`free`.

## Integers & Types
- Prefer fixed-width: `int32_t`, `uint64_t`, not `int`, `unsigned long`.
- Avoid void pointers: use typed pointers or union.

## Performance & Thread Safety
- Keep hot paths allocation-light: pre-allocate, use stack.
- Document thread-safety: locks or rules if accessed from multiple threads.

## Examples

❌ Bad: `#include <windows.h>` (OS leak), `static Engine engine` (global), `CreateMutex()` (OS API)

✅ Good: `struct engine_mutex *mutex = engine_mutex_create()`, `void tick(struct engine *owned_engine)`, `if (engine_init(&e)) return e`

## Keep it simple

Smallest code that keeps behavior and module boundaries.
- Validate in one shared helper, not the same checks in two functions.
- One implementation macro/helper instead of N near-identical copies; loop instead of repeating.
- No dead code, unused includes, or forward declarations that only reorder definitions.
- CMake: one helper plus `foreach`, no duplicated branches.
- Comments cite the spec rule and explain why; they do not restate the code.
