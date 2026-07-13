#pragma once
#include "types.h"

void game_init(AppState *as);
void game_step(AppState *as, const PlayerIntent *intent, float dt);
int game_accumulate_steps(double *accumulator, double frame_dt);

void update_duck_physics(AppState *as, const PlayerIntent *intent, float dt);
void update_duck_idle(AppState *as, float dt);
void update_lilypad_collisions(AppState *as);
void update_ripples(AppState *as, float dt);
void update_particles(AppState *as, float dt);

void spawn_ripple(AppState *as, float x, float z);
void spawn_ripple_scaled(AppState *as, float x, float z, float intensity);
void spawn_splash(AppState *as, float x, float z);
void spawn_splash_scaled(AppState *as, float x, float z, float intensity);
void spawn_finger_trail_ripple(AppState *as, float x, float z);
void spawn_sleep_bubble(AppState *as);
void spawn_sparkle_burst(AppState *as, float x, float y, float z, int count);
void spawn_rainbow_splash(AppState *as, float x, float z, float intensity);
bool spawn_poppable_bubble(AppState *as);
bool trigger_lily_note(AppState *as, PondObject *lily);
void impulse_lily(AppState *as, PondObject *lily, float strength);
void wake_duck(AppState *as, bool surprised);
void environment_set_night(AppState *as, bool night);
void environment_update(AppState *as, float dt);
bool quack_and_splash(AppState *as);
bool spawn_tap_effect(AppState *as, float x, float z);
bool spawn_tap_effect_scaled(AppState *as, float x, float z, float intensity);
