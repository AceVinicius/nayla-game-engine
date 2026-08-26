#include "api.h"
#include <stdlib.h>

struct renderer {
  int32_t initialized;
};

int32_t renderer_init(struct renderer **owned_renderer) {
  if (!owned_renderer) {
    return -1;
  }

  *owned_renderer = (struct renderer *)malloc(sizeof(struct renderer));
  if (!*owned_renderer) {
    return 1;
  }

  (*owned_renderer)->initialized = 1;
  return 0;
}

int32_t renderer_shutdown(struct renderer *owned_renderer) {
  if (!owned_renderer) {
    return -1;
  }

  free(owned_renderer);
  return 0;
}
