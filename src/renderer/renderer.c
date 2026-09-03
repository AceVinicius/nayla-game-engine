#include "api.h"

struct Renderer {
  const EngineAllocator *allocator;
  int32_t initialized;
};

EngineResult renderer_init(Renderer **owned_renderer, const EngineAllocator *allocator) {
  if (!owned_renderer) {
    return ENGINE_RESULT_INVALID_ARGUMENT;
  }

  /* Resolve NULL to the default allocator once at construction (CORE-ALLOC-068) */
  const EngineAllocator *resolved = allocator;
  if (!resolved) {
    resolved = engine_default_allocator();
  }

  /* Validate that both callbacks are present (CORE-ALLOC-070) */
  if (!resolved->alloc || !resolved->free) {
    return ENGINE_RESULT_INVALID_ARGUMENT;
  }

  /* Allocate the renderer structure (CORE-ALLOC-016's pre-validation discharge:
   * sizeof(Renderer) and _Alignof(Renderer) are compile-time non-zero) */
  Renderer *renderer = (Renderer *)engine_alloc(resolved, sizeof(Renderer), _Alignof(Renderer));
  if (!renderer) {
    return ENGINE_RESULT_OUT_OF_MEMORY;
  }

  renderer->allocator = resolved;
  renderer->initialized = 1;
  *owned_renderer = renderer;
  return ENGINE_RESULT_SUCCESS;
}

EngineResult renderer_shutdown(Renderer *owned_renderer) {
  if (!owned_renderer) {
    return ENGINE_RESULT_INVALID_ARGUMENT;
  }

  /* Release through the stored allocator (CORE-ALLOC-067) */
  engine_free(owned_renderer->allocator, owned_renderer);
  return ENGINE_RESULT_SUCCESS;
}
