#ifndef RENDERER_API_H
#define RENDERER_API_H

#include <stdint.h>

/* Opaque renderer type. */
struct renderer;

/* Renderer initialization. Returns 0 on success. */
int32_t renderer_init(struct renderer **owned_renderer);

/* Renderer shutdown. Returns 0 on success. */
int32_t renderer_shutdown(struct renderer *owned_renderer);

#endif /* RENDERER_API_H */
