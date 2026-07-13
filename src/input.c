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

static void append_trail_sample(InputState *input, const TouchPoint *touch,
                                bool reset)
{
    if (!touch->gesture_world_valid)
        return;
    if (input->trail_count > 0) {
        int last_index = (input->trail_write + MAX_TRAIL_SAMPLES - 1)
                       % MAX_TRAIL_SAMPLES;
        const TrailSample *last = &input->trail_samples[last_index];
        if (!reset && last->owner_sequence == touch->activation_sequence
            && fabsf(last->x - touch->gesture_world_x) < 0.0001f
            && fabsf(last->z - touch->gesture_world_z) < 0.0001f)
            return;
    }
    if (input->trail_count == MAX_TRAIL_SAMPLES) {
        input->trail_read = (input->trail_read + 1) % MAX_TRAIL_SAMPLES;
        input->trail_count--;
        input->trail_samples[input->trail_read].reset = true;
    }
    input->trail_samples[input->trail_write] = (TrailSample){
        .x = touch->gesture_world_x,
        .z = touch->gesture_world_z,
        .owner_sequence = touch->activation_sequence,
        .reset = reset
    };
    input->trail_write = (input->trail_write + 1) % MAX_TRAIL_SAMPLES;
    input->trail_count++;
}

static void update_touch_position(AppState *as, TouchPoint *touch, float x, float y, bool smooth)
{
    float dx = x - touch->down_x;
    float dy = y - touch->down_y;
    float distance_sq = dx * dx + dy * dy;
    float gesture_world_x;
    float gesture_world_z;
    bool gesture_world_valid = input_screen_to_pond(as->proj, as->picking_view,
                                                     x, y, &gesture_world_x,
                                                     &gesture_world_z);
    if (gesture_world_valid) {
        gesture_world_x = SDL_clamp(gesture_world_x, -DUCK_WORLD_BOUND,
                                    DUCK_WORLD_BOUND);
        gesture_world_z = SDL_clamp(gesture_world_z, -DUCK_WORLD_BOUND,
                                    DUCK_WORLD_BOUND);
        if (touch->gesture_world_valid) {
            float world_dx = gesture_world_x - touch->gesture_world_x;
            float world_dz = gesture_world_z - touch->gesture_world_z;
            touch->world_travel_distance += sqrtf(world_dx * world_dx
                                                 + world_dz * world_dz);
        }
        touch->gesture_world_x = gesture_world_x;
        touch->gesture_world_z = gesture_world_z;
    }
    touch->gesture_world_valid = gesture_world_valid;

    touch->raw_x = x;
    touch->raw_y = y;
    if (!smooth) {
        touch->smooth_x = x;
        touch->smooth_y = y;
    }
    touch->max_distance_sq = SDL_max(touch->max_distance_sq, distance_sq);
    touch->world_valid = gesture_world_valid;
    if (gesture_world_valid) {
        touch->world_x = gesture_world_x;
        touch->world_z = gesture_world_z;
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
    as->input.trail_read = 0;
    as->input.trail_write = 0;
    as->input.trail_count = 0;
}

PlayerIntent input_get_player_intent(AppState *as)
{
    PlayerIntent intent = {0};
    InputState *input = &as->input;

    if (input->primary_touch >= 0 && input->primary_touch < MAX_TOUCHES) {
        TouchPoint *touch = &input->touches[input->primary_touch];
        if (touch->active) {
            touch->smooth_x += (touch->raw_x - touch->smooth_x) * TOUCH_SMOOTHING;
            touch->smooth_y += (touch->raw_y - touch->smooth_y) * TOUCH_SMOOTHING;
            touch->world_valid = input_screen_to_pond(as->proj, as->picking_view,
                                                       touch->smooth_x,
                                                       touch->smooth_y,
                                                       &touch->world_x,
                                                       &touch->world_z);
        }
        if (touch->active && touch->world_valid) {
            touch->world_x = SDL_clamp(touch->world_x, -DUCK_WORLD_BOUND,
                                       DUCK_WORLD_BOUND);
            touch->world_z = SDL_clamp(touch->world_z, -DUCK_WORLD_BOUND,
                                       DUCK_WORLD_BOUND);
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

float input_calculate_splash_intensity(float duration_seconds,
                                       float travel_distance)
{
    float hold_intensity = 0.35f + SDL_max(0.f, duration_seconds) * 0.8f;
    float movement_intensity = 0.35f + SDL_max(0.f, travel_distance) * 0.25f;
    return SDL_clamp(SDL_max(hold_intensity, movement_intensity), 0.35f, 1.5f);
}

bool input_modifiers_request_camera(SDL_Keymod modifiers)
{
    return (modifiers & SDL_KMOD_CTRL) != 0;
}

bool input_handle_mouse_touch_event(AppState *as, const SDL_Event *event,
                                    InputAction *action)
{
    SDL_Event touch = {0};
    float mouse_x;
    float mouse_y;
    int width = as->win_w;
    int height = as->win_h;

    if (as->window)
        SDL_GetWindowSize(as->window, &width, &height);
    width = SDL_max(width, 1);
    height = SDL_max(height, 1);

    switch (event->type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        touch.type = SDL_EVENT_FINGER_DOWN;
        mouse_x = event->button.x;
        mouse_y = event->button.y;
        touch.tfinger.timestamp = event->button.timestamp;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        touch.type = SDL_EVENT_FINGER_MOTION;
        mouse_x = event->motion.x;
        mouse_y = event->motion.y;
        touch.tfinger.timestamp = event->motion.timestamp;
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        touch.type = SDL_EVENT_FINGER_UP;
        mouse_x = event->button.x;
        mouse_y = event->button.y;
        touch.tfinger.timestamp = event->button.timestamp;
        break;
    default:
        SDL_zerop(action);
        return false;
    }

    touch.tfinger.touchID = QUAK_MOUSE_TOUCH_ID;
    touch.tfinger.fingerID = QUAK_MOUSE_FINGER_ID;
    touch.tfinger.x = mouse_x / (float)width;
    touch.tfinger.y = mouse_y / (float)height;
    touch.tfinger.pressure = 1.f;
    return input_handle_touch_event(as, &touch, action);
}

void input_cancel_mouse_touch(AppState *as)
{
    int index = find_touch(&as->input, QUAK_MOUSE_TOUCH_ID, QUAK_MOUSE_FINGER_ID);
    if (index < 0)
        return;

    as->input.touches[index].active = false;
    if (as->input.primary_touch == index) {
        as->input.primary_touch = find_oldest_valid_touch(&as->input);
        if (as->input.primary_touch >= 0)
            append_trail_sample(&as->input,
                                &as->input.touches[as->input.primary_touch], true);
    }
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
        if (input->primary_touch == index)
            append_trail_sample(input, touch, true);
        if (touch->world_valid) {
            action->interaction_requested = true;
            action->interaction = (InteractionEvent){
                .type = INTERACTION_POND_PRESS,
                .x = touch->world_x,
                .z = touch->world_z,
                .intensity = 1.f,
                .object_id = -1
            };
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
        if (input->primary_touch == index)
            append_trail_sample(input, touch, false);
        return true;
    }

    if (event->type == SDL_EVENT_FINGER_UP && touch->gesture_world_valid) {
        Uint64 duration_ns = finger->timestamp >= touch->down_timestamp_ns
                           ? finger->timestamp - touch->down_timestamp_ns : 0;
        float duration_seconds = (float)((double)duration_ns / 1000000000.0);
        bool is_tap = input_gesture_is_tap(touch->down_timestamp_ns,
                                           finger->timestamp,
                                           touch->max_distance_sq);
        action->interaction_requested = true;
        action->interaction = (InteractionEvent){
            .type = is_tap ? INTERACTION_TAP_WATER : INTERACTION_HOLD_RELEASE,
            .x = touch->gesture_world_x,
            .z = touch->gesture_world_z,
            .duration_seconds = duration_seconds,
            .intensity = input_calculate_splash_intensity(
                duration_seconds, touch->world_travel_distance),
            .object_id = -1
        };
    }

    if (input->primary_touch == index)
        append_trail_sample(input, touch, false);
    touch->active = false;
    if (input->primary_touch == index) {
        input->primary_touch = find_oldest_valid_touch(input);
        if (input->primary_touch >= 0)
            append_trail_sample(input, &input->touches[input->primary_touch], true);
    }
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
