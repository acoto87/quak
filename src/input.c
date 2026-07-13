#include "input.h"

#include <math.h>

static int find_touch(const InputState *input, SDL_TouchID touch_id, SDL_FingerID finger_id)
{
    for (int i = 0; i < MAX_TOUCHES; i++) {
        const TouchPoint *touch = &input->touches[i];
        if (touch->active && touch->touch_id == touch_id && touch->finger_id == finger_id)
            return i;
    }
    return -1;
}

static int find_free_touch(const InputState *input)
{
    for (int i = 0; i < MAX_TOUCHES; i++) {
        if (!input->touches[i].active)
            return i;
    }
    return -1;
}

static int find_oldest_valid_touch(const InputState *input)
{
    int oldest = -1;
    Uint64 oldest_sequence = UINT64_MAX;

    for (int i = 0; i < MAX_TOUCHES; i++) {
        const TouchPoint *touch = &input->touches[i];
        if (touch->active && touch->world_valid
            && touch->activation_sequence < oldest_sequence) {
            oldest = i;
            oldest_sequence = touch->activation_sequence;
        }
    }
    return oldest;
}

static void update_touch_position(AppState *as, TouchPoint *touch, float x, float y, bool smooth)
{
    float dx = x - touch->down_x;
    float dy = y - touch->down_y;
    float distance_sq = dx * dx + dy * dy;

    touch->raw_x = x;
    touch->raw_y = y;
    if (smooth) {
        touch->smooth_x += (x - touch->smooth_x) * TOUCH_SMOOTHING;
        touch->smooth_y += (y - touch->smooth_y) * TOUCH_SMOOTHING;
    } else {
        touch->smooth_x = x;
        touch->smooth_y = y;
    }
    touch->max_distance_sq = SDL_max(touch->max_distance_sq, distance_sq);
    touch->world_valid = input_screen_to_pond(as->proj, as->view,
                                               touch->smooth_x, touch->smooth_y,
                                               &touch->world_x, &touch->world_z);
    if (touch->world_valid) {
        touch->world_x = SDL_clamp(touch->world_x, -DUCK_WORLD_BOUND, DUCK_WORLD_BOUND);
        touch->world_z = SDL_clamp(touch->world_z, -DUCK_WORLD_BOUND, DUCK_WORLD_BOUND);
    }
}

void input_init(AppState *as)
{
    SDL_zerop(&as->input);
    as->input.primary_touch = -1;
}

void input_clear_touches(AppState *as)
{
    SDL_zeroa(as->input.touches);
    as->input.primary_touch = -1;
}

PlayerIntent input_get_player_intent(const AppState *as)
{
    PlayerIntent intent = {0};
    const InputState *input = &as->input;

    if (input->primary_touch >= 0 && input->primary_touch < MAX_TOUCHES) {
        const TouchPoint *touch = &input->touches[input->primary_touch];
        if (touch->active && touch->world_valid) {
            intent.has_swim_target = true;
            intent.swim_target_x = touch->world_x;
            intent.swim_target_z = touch->world_z;
            return intent;
        }
    }

    const bool *keys = SDL_GetKeyboardState(NULL);
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])    intent.move_z += 1.f;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])  intent.move_z -= 1.f;
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])  intent.move_x -= 1.f;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) intent.move_x += 1.f;

    if (as->gamepad) {
        float gx = SDL_GetGamepadAxis(as->gamepad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.f;
        float gz = SDL_GetGamepadAxis(as->gamepad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.f;
        if (fabsf(gx) > DEADZONE) intent.move_x += gx;
        if (fabsf(gz) > DEADZONE) intent.move_z -= gz;
    }

    float length = sqrtf(intent.move_x * intent.move_x + intent.move_z * intent.move_z);
    if (length > 1.f) {
        intent.move_x /= length;
        intent.move_z /= length;
    }
    return intent;
}

bool input_gesture_is_tap(Uint64 down_timestamp_ns, Uint64 up_timestamp_ns,
                          float max_distance_sq)
{
    Uint64 duration = up_timestamp_ns >= down_timestamp_ns
                    ? up_timestamp_ns - down_timestamp_ns
                    : UINT64_MAX;
    return duration <= TOUCH_TAP_MAX_NS
        && max_distance_sq <= TOUCH_TAP_MAX_DISTANCE * TOUCH_TAP_MAX_DISTANCE;
}

bool input_handle_touch_event(AppState *as, const SDL_Event *event, InputAction *action)
{
    InputState *input = &as->input;
    const SDL_TouchFingerEvent *finger = &event->tfinger;
    int index;

    SDL_zerop(action);
    if (finger->touchID == SDL_MOUSE_TOUCHID)
        return false;

    if (event->type == SDL_EVENT_FINGER_DOWN) {
        index = find_touch(input, finger->touchID, finger->fingerID);
        if (index < 0)
            index = find_free_touch(input);
        if (index < 0)
            return false;

        TouchPoint *touch = &input->touches[index];
        SDL_zerop(touch);
        touch->touch_id = finger->touchID;
        touch->finger_id = finger->fingerID;
        touch->active = true;
        touch->down_x = finger->x;
        touch->down_y = finger->y;
        touch->down_timestamp_ns = finger->timestamp;
        touch->activation_sequence = input->next_sequence++;
        update_touch_position(as, touch, finger->x, finger->y, false);

        if (input->primary_touch < 0 && touch->world_valid)
            input->primary_touch = index;
        if (touch->world_valid) {
            action->ripple_requested = true;
            action->effect_x = touch->world_x;
            action->effect_z = touch->world_z;
        }
        return true;
    }

    index = find_touch(input, finger->touchID, finger->fingerID);
    if (index < 0)
        return false;

    TouchPoint *touch = &input->touches[index];
    update_touch_position(as, touch, finger->x, finger->y, true);

    if (event->type == SDL_EVENT_FINGER_MOTION) {
        if (input->primary_touch == index && !touch->world_valid)
            input->primary_touch = find_oldest_valid_touch(input);
        else if (input->primary_touch < 0 && touch->world_valid)
            input->primary_touch = find_oldest_valid_touch(input);
        return true;
    }

    if (event->type == SDL_EVENT_FINGER_UP && touch->world_valid
        && input_gesture_is_tap(touch->down_timestamp_ns, finger->timestamp,
                                touch->max_distance_sq)) {
        action->tap_requested = true;
        action->effect_x = touch->world_x;
        action->effect_z = touch->world_z;
    }

    touch->active = false;
    if (input->primary_touch == index)
        input->primary_touch = find_oldest_valid_touch(input);
    return true;
}

bool input_screen_to_pond(const mat4x4 projection, const mat4x4 view,
                          float screen_x, float screen_y,
                          float *world_x, float *world_z)
{
    mat4x4 view_projection;
    mat4x4 inverse;
    vec4 near_clip = {2.f * screen_x - 1.f, 1.f - 2.f * screen_y, 0.f, 1.f};
    vec4 far_clip = {near_clip[0], near_clip[1], 1.f, 1.f};
    vec4 near_world;
    vec4 far_world;

    mat4x4_mul(view_projection, projection, view);
    mat4x4_invert(inverse, view_projection);
    mat4x4_mul_vec4(near_world, inverse, near_clip);
    mat4x4_mul_vec4(far_world, inverse, far_clip);

    if (!isfinite(near_world[3]) || !isfinite(far_world[3])
        || fabsf(near_world[3]) < 0.00001f || fabsf(far_world[3]) < 0.00001f)
        return false;

    for (int i = 0; i < 3; i++) {
        near_world[i] /= near_world[3];
        far_world[i] /= far_world[3];
    }

    float direction_y = far_world[1] - near_world[1];
    if (!isfinite(direction_y) || fabsf(direction_y) < 0.00001f)
        return false;

    float t = -near_world[1] / direction_y;
    if (!isfinite(t) || t < 0.f)
        return false;

    *world_x = near_world[0] + (far_world[0] - near_world[0]) * t;
    *world_z = near_world[2] + (far_world[2] - near_world[2]) * t;
    return isfinite(*world_x) && isfinite(*world_z);
}
