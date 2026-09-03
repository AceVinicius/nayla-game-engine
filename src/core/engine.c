#include "engine.h"

struct Engine {
  const EngineAllocator *allocator;
  int32_t initialized;
};

EngineResult engine_init(Engine **owned_engine, const EngineAllocator *allocator) {
  if (!owned_engine) {
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

  /* Allocate the engine structure (CORE-ALLOC-016's pre-validation discharge:
   * sizeof(Engine) and _Alignof(Engine) are compile-time non-zero) */
  Engine *engine = (Engine *)engine_alloc(resolved, sizeof(Engine), _Alignof(Engine));
  if (!engine) {
    return ENGINE_RESULT_OUT_OF_MEMORY;
  }

  /* Initialize the engine: store the allocator and mark as initialized */
  engine->allocator = resolved;
  engine->initialized = 1;
  *owned_engine = engine;
  return ENGINE_RESULT_SUCCESS;
}

EngineResult engine_tick(Engine *owned_engine) {
  if (!owned_engine) {
    return ENGINE_RESULT_INVALID_ARGUMENT;
  }

  if (!owned_engine->initialized) {
    return ENGINE_RESULT_INVALID_STATE;
  }

  return ENGINE_RESULT_SUCCESS;
}

EngineResult engine_shutdown(Engine *owned_engine) {
  if (!owned_engine) {
    return ENGINE_RESULT_INVALID_ARGUMENT;
  }

  /* Release through the stored allocator (CORE-ALLOC-067) */
  engine_free(owned_engine->allocator, owned_engine);
  return ENGINE_RESULT_SUCCESS;
}
