#pragma once
#include "types.h"

/* Load the OBJ mesh and diffuse texture, upload to GPU.
 * Returns true on success; the duck is simply not drawn if this fails.       */
bool duck_init(AppState *as);

/* Encode the duck draw into the active SDL GPU render pass.                  */
void duck_draw(AppState *as, SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass);

/* Release all GPU resources owned by the duck.                               */
void duck_cleanup(AppState *as);
