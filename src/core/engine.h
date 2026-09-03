#ifndef ENGINE_H
#define ENGINE_H

#include "src/core/result.h"
#include "src/core/allocator.h"
#include "src/core/logging.h"

/* Opaque engine type. Owned by caller; lifetime explicit.
 * See docs/spec/core/engine.md for the contract.
 */
typedef struct Engine Engine;

/* Initialize engine with an optional custom allocator and logger.
 * allocator == NULL means use engine_default_allocator().
 * logger == NULL means discard — NOT a default; no default logger exists (CORE-LOG-005).
 * Both are borrowed; caller owns them and must keep them valid until
 * after engine_shutdown returns (CORE-ALLOC-060..065, CORE-LOG-004).
 * The allocator and logger have deliberately opposite NULL semantics on the same line:
 * NULL allocator resolves to a default, but NULL logger means discard (§2 of logging.md).
 * Returns ENGINE_RESULT_SUCCESS on success.
 * Returns ENGINE_RESULT_INVALID_ARGUMENT if allocator->alloc or
 *   allocator->free is NULL after resolution, or if owned_engine is NULL.
 * Returns ENGINE_RESULT_OUT_OF_MEMORY if allocation fails.
 * On failure, *owned_engine is left unmodified or NULL (CORE-RESULT-013).
 * See docs/spec/core/allocator.md § CORE-ALLOC-060..070 for lifetime rules.
 * See docs/spec/core/logging.md § CORE-LOG-003..005 for logger rules.
 */
ENGINE_NODISCARD EngineResult engine_init(Engine **owned_engine, const EngineAllocator *allocator, const EngineLogger *logger);

/* Tick the engine (frame update).
 * Returns ENGINE_RESULT_SUCCESS on success.
 * Returns ENGINE_RESULT_INVALID_ARGUMENT if owned_engine is NULL.
 * Returns ENGINE_RESULT_INVALID_STATE if engine is not initialized.
 */
ENGINE_NODISCARD EngineResult engine_tick(Engine *owned_engine);

/* Shutdown engine and release all allocated memory through its stored allocator.
 * Returns ENGINE_RESULT_SUCCESS on success.
 * Returns ENGINE_RESULT_INVALID_ARGUMENT if owned_engine is NULL.
 * Caller owns the EngineAllocator and must manage its lifetime separately
 * (CORE-ALLOC-062).
 */
ENGINE_NODISCARD EngineResult engine_shutdown(Engine *owned_engine);

#endif /* ENGINE_H */
