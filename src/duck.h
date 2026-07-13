#pragma once
#include "types.h"

/* Load the required OBJ mesh and diffuse texture, then upload them to GPU. */
bool duck_init(AppState *as);

/* Encode the duck draw into the active SDL GPU render pass.                  */
void duck_draw(AppState *as, SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass);

/* Release all GPU resources owned by the duck.                               */
void duck_cleanup(AppState *as);
