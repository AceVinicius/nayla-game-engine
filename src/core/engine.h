#ifndef ENGINE_H
#define ENGINE_H

#include <stdint.h>

/* Opaque engine type. Owned by caller; lifetime explicit. */
struct engine;

/* Engine error type. 0 = success, non-zero = error. */
typedef int32_t engine_error_t;

/* Allocator function types: portable, no direct malloc/free in core. */
typedef void* (*engine_alloc_fn)(uint64_t size);
typedef void (*engine_free_fn)(void *ptr);

/* Initialize engine. Returns 0 on success, non-zero on error. */
engine_error_t engine_init(struct engine **owned_engine);

/* Tick the engine (frame update). Returns 0 on success. */
engine_error_t engine_tick(struct engine *owned_engine);

/* Shutdown engine. Returns 0 on success. */
engine_error_t engine_shutdown(struct engine *owned_engine);

/* Set custom allocators (must be called before engine_init). */
void engine_set_allocators(engine_alloc_fn alloc, engine_free_fn free);

#endif /* ENGINE_H */
