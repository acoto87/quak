#pragma once

#include "types.h"

void input_init(AppState *as);
void input_clear_touches(AppState *as);
PlayerIntent input_get_player_intent(const AppState *as);
bool input_handle_touch_event(AppState *as, const SDL_Event *event, InputAction *action);

bool input_screen_to_pond(const mat4x4 projection, const mat4x4 view,
                          float screen_x, float screen_y,
                          float *world_x, float *world_z);
bool input_gesture_is_tap(Uint64 down_timestamp_ns, Uint64 up_timestamp_ns,
                          float max_distance_sq);
