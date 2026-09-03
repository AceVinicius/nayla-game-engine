#ifndef RENDERER_API_H
#define RENDERER_API_H

#include "src/core/result.h"
#include "src/core/allocator.h"
#include "src/core/logging.h"

/* Opaque renderer type. Owned by caller; lifetime explicit. */
typedef struct Renderer Renderer;

/* Renderer initialization with an optional custom allocator and logger.
 * allocator == NULL means use engine_default_allocator() (CORE-ALLOC-068).
 * logger == NULL means discard all log records; MUST NOT resolve to a default
 *   (CORE-LOG-005). This is the opposite semantics of allocator's NULL.
 * The allocator and logger are borrowed; caller owns them and must keep them
 * valid until after renderer_shutdown returns (CORE-ALLOC-060..065, CORE-LOG-004).
 * The logger MUST outlive the renderer if non-NULL; its validity is the caller's
 * obligation (CORE-LOG-004).
 * Returns ENGINE_RESULT_SUCCESS on success.
 * Returns ENGINE_RESULT_INVALID_ARGUMENT if allocator->alloc or
 *   allocator->free is NULL after resolution, or if owned_renderer is NULL.
 * Returns ENGINE_RESULT_OUT_OF_MEMORY if allocation fails.
 * On failure, *owned_renderer is left unmodified or NULL (CORE-RESULT-013).
 * See docs/spec/core/allocator.md § CORE-ALLOC-060..070 for lifetime rules.
 * See docs/spec/core/logging.md § CORE-LOG-003..005 for logger ownership and NULL semantics.
 */
ENGINE_NODISCARD EngineResult renderer_init(Renderer **owned_renderer, const EngineAllocator *allocator, const EngineLogger *logger);

/* Renderer shutdown. Releases all allocated memory through its stored allocator.
 * Returns ENGINE_RESULT_SUCCESS on success.
 * Returns ENGINE_RESULT_INVALID_ARGUMENT if owned_renderer is NULL.
 * Caller owns the EngineAllocator and must manage its lifetime separately
 * (CORE-ALLOC-062).
 */
ENGINE_NODISCARD EngineResult renderer_shutdown(Renderer *owned_renderer);

#endif /* RENDERER_API_H */
