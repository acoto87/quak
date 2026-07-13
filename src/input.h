#pragma once

#include "types.h"

typedef struct {
    bool interaction_requested;
    InteractionEvent interaction;
} InputAction;

#define QUAK_MOUSE_TOUCH_ID  ((SDL_TouchID)0x5155414BULL)
#define QUAK_MOUSE_FINGER_ID ((SDL_FingerID)1ULL)

void input_init(AppState *as);
void input_clear_touches(AppState *as);
PlayerIntent input_get_player_intent(AppState *as);
bool input_handle_touch_event(AppState *as, const SDL_Event *event, InputAction *action);
bool input_handle_mouse_touch_event(AppState *as, const SDL_Event *event,
                                    InputAction *action);
void input_cancel_mouse_touch(AppState *as);
bool input_modifiers_request_camera(SDL_Keymod modifiers);

bool input_screen_to_pond(const mat4x4 projection, const mat4x4 view,
                          float screen_x, float screen_y,
                          float *world_x, float *world_z);
bool input_gesture_is_tap(Uint64 down_timestamp_ns, Uint64 up_timestamp_ns,
                          float max_distance_sq);
float input_calculate_splash_intensity(float duration_seconds,
                                       float travel_distance);
