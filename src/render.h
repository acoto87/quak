#pragma once
#include "types.h"
#include <linmath.h>

/* Normal matrix = transpose(inverse(model)), packed in shader column order. */
void compute_nmat(float *nmat9, mat4x4 model);

/* ── Scene lifecycle ─────────────────────────────────────────────────────── */
bool render_init(AppState *as);
void render_resize(AppState *as, int win_w, int win_h);
bool render_prepare_frame(AppState *as, SDL_GPUCommandBuffer *cmd);
void render_frame(AppState *as);
void render_cleanup(AppState *as);
