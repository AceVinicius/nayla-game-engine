#include "engine.h"
#include <stdlib.h>

static void *default_alloc(uint64_t size) {
  return malloc((size_t)size);
}

/* Default allocators: can be overridden via engine_set_allocators(). */
static engine_alloc_fn g_alloc = default_alloc;
static engine_free_fn g_free = free;

struct engine {
  int32_t initialized;
};

engine_error_t engine_init(struct engine **owned_engine) {
  if (!owned_engine) {
    return -1;
  }

  *owned_engine = (struct engine *)g_alloc(sizeof(struct engine));
  if (!*owned_engine) {
    return 1;
  }

  (*owned_engine)->initialized = 1;
  return 0;
}

engine_error_t engine_tick(struct engine *owned_engine) {
  if (!owned_engine) {
    return -1;
  }

  if (!owned_engine->initialized) {
    return 1;
  }

  return 0;
}

engine_error_t engine_shutdown(struct engine *owned_engine) {
  if (!owned_engine) {
    return -1;
  }

  g_free(owned_engine);
  return 0;
}

void engine_set_allocators(engine_alloc_fn alloc, engine_free_fn free) {
  if (alloc) {
    g_alloc = alloc;
  }
  if (free) {
    g_free = free;
  }
}
