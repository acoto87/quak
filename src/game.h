#pragma once
#include "types.h"

void update_duck_physics(AppState *as, float dt);
void update_duck_idle(AppState *as, float dt);
void update_lilypad_collisions(AppState *as);
void update_ripples(AppState *as, float dt);
void update_particles(AppState *as, float dt);

void spawn_ripple(AppState *as, float x, float z);
void spawn_splash(AppState *as, float x, float z);
void quack_and_splash(AppState *as);
