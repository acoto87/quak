#pragma once
#include "types.h"

void game_step(AppState *as, const PlayerIntent *intent, float dt);
int game_accumulate_steps(double *accumulator, double frame_dt);

void update_duck_physics(AppState *as, const PlayerIntent *intent, float dt);
void update_duck_idle(AppState *as, float dt);
void update_lilypad_collisions(AppState *as);
void update_ripples(AppState *as, float dt);
void update_particles(AppState *as, float dt);

void spawn_ripple(AppState *as, float x, float z);
void spawn_splash(AppState *as, float x, float z);
void quack_and_splash(AppState *as);
void spawn_tap_effect(AppState *as, float x, float z);
