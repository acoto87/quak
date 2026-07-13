#include "game.h"
#include "input.h"

#include <math.h>
#include <stdio.h>

static int quack_calls;
static int surprised_quack_calls;
static int bubble_pop_calls;
static int lily_note_calls;
static int last_lily_note = -1;

void play_quack(AppState *as) { (void)as; quack_calls++; }
void play_quack_variant(AppState *as, QuackVariant variant)
{
    (void)as;
    quack_calls++;
    if (variant == QUACK_VARIANT_SURPRISED)
        surprised_quack_calls++;
}
void play_splash(AppState *as) { (void)as; }
void play_splash_scaled(AppState *as, float intensity)
{
    (void)as;
    (void)intensity;
}
void play_bubble_pop(AppState *as) { (void)as; bubble_pop_calls++; }
void play_lily_note(AppState *as, int note_index)
{
    (void)as;
    lily_note_calls++;
    last_lily_note = note_index;
}

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static void init_game(AppState *as)
{
    SDL_zerop(as);
    as->simulation_rng = 1;
    input_init(as);
    game_init(as);
    as->idle_quack_timer = 1000.f;
    as->bubble_timer = 1000.f;
}

static void make_look_at_lh(mat4x4 view, const vec3 eye, const vec3 target)
{
    vec3 up = {0.f, 1.f, 0.f};
    vec3 forward;
    vec3 right;
    vec3 camera_up;

    vec3_sub(forward, target, eye);
    vec3_norm(forward, forward);
    vec3_mul_cross(right, up, forward);
    vec3_norm(right, right);
    vec3_mul_cross(camera_up, forward, right);

    mat4x4_identity(view);
    view[0][0] = right[0];     view[0][1] = camera_up[0]; view[0][2] = forward[0];
    view[1][0] = right[1];     view[1][1] = camera_up[1]; view[1][2] = forward[1];
    view[2][0] = right[2];     view[2][1] = camera_up[2]; view[2][2] = forward[2];
    view[3][0] = -vec3_mul_inner(right, eye);
    view[3][1] = -vec3_mul_inner(camera_up, eye);
    view[3][2] = -vec3_mul_inner(forward, eye);
}

static void make_perspective_lh_zo(mat4x4 projection, float aspect)
{
    const float near_z = 1.f;
    const float far_z = 100.f;
    float y_scale = 1.f / tanf(SDL_PI_F / 6.f);

    SDL_memset(projection, 0, sizeof(mat4x4));
    projection[0][0] = y_scale / aspect;
    projection[1][1] = y_scale;
    projection[2][2] = far_z / (far_z - near_z);
    projection[2][3] = 1.f;
    projection[3][2] = -(near_z * far_z) / (far_z - near_z);
}

static void test_world_velocity(void)
{
    AppState as;
    PlayerIntent intent = {.move_z = 1.f};
    init_game(&as);

    update_duck_physics(&as, &intent, 1.f / 60.f);
    CHECK(as.duck_vz > 0.f);
    CHECK(as.duck_z > 0.f);

    init_game(&as);
    intent = (PlayerIntent){.move_x = 1.f};
    update_duck_physics(&as, &intent, 1.f / 60.f);
    CHECK(as.duck_vx > 0.f);
    CHECK(as.duck_x > 0.f);
}

static void test_duck_model_heading(void)
{
    const float headings[] = {0.f, SDL_PI_F / 2.f, SDL_PI_F, -SDL_PI_F / 2.f};
    for (int i = 0; i < 4; i++) {
        float yaw = DUCK_MODEL_YAW(headings[i]);
        float forward_x = -sinf(yaw);
        float forward_z = -cosf(yaw);
        CHECK(fabsf(forward_x - cosf(headings[i])) < 0.0001f);
        CHECK(fabsf(forward_z - sinf(headings[i])) < 0.0001f);
    }
}

static void test_boundaries(void)
{
    AppState as;
    PlayerIntent intent = {0};

    init_game(&as);
    as.duck_x = DUCK_WORLD_BOUND - 0.01f;
    as.duck_vx = 5.f;
    update_duck_physics(&as, &intent, 1.f / 60.f);
    CHECK(as.duck_x == DUCK_WORLD_BOUND);
    CHECK(as.duck_vx == 0.f);

    init_game(&as);
    as.duck_z = -DUCK_WORLD_BOUND + 0.01f;
    as.duck_vz = -5.f;
    update_duck_physics(&as, &intent, 1.f / 60.f);
    CHECK(as.duck_z == -DUCK_WORLD_BOUND);
    CHECK(as.duck_vz == 0.f);
}

static void test_lily_collision(void)
{
    AppState as;
    PondObject *lily;
    init_game(&as);
    pond_object_pool_reset(&as.pond_objects);
    lily = pond_object_spawn(&as.pond_objects, POND_OBJECT_LILY_PAD, -6.f, -6.5f);
    lily->radius = 1.2f;
    float min_distance = lily->radius + DUCK_COLLISION_RADIUS;
    as.duck_x = lily->x;
    as.duck_z = lily->z - min_distance * 0.8f;
    as.duck_vz = 2.f;
    update_lilypad_collisions(&as);
    CHECK(as.duck_vz < 0.f);
    CHECK(fabsf(as.duck_z - (lily->z - min_distance)) < 0.0001f);

    init_game(&as);
    pond_object_pool_reset(&as.pond_objects);
    lily = pond_object_spawn(&as.pond_objects, POND_OBJECT_LILY_PAD, -6.f, -6.5f);
    lily->radius = 1.2f;
    as.duck_x = lily->x;
    as.duck_z = lily->z;
    update_lilypad_collisions(&as);
    CHECK(isfinite(as.duck_x) && isfinite(as.duck_z));
    CHECK(fabsf(as.duck_x - (lily->x + min_distance)) < 0.0001f);

    init_game(&as);
    pond_object_pool_reset(&as.pond_objects);
    lily = pond_object_spawn(&as.pond_objects, POND_OBJECT_LILY_PAD, 0.f, 0.f);
    lily->radius = 1.f;
    lily->state = POND_STATE_DISABLED;
    update_lilypad_collisions(&as);
    CHECK(as.duck_x == 0.f && as.duck_z == 0.f);

    init_game(&as);
    pond_object_pool_reset(&as.pond_objects);
    lily = pond_object_spawn(&as.pond_objects, POND_OBJECT_LILY_PAD, 15.4f, 0.f);
    lily->radius = 1.f;
    as.duck_x = lily->x;
    as.duck_z = lily->z;
    update_lilypad_collisions(&as);
    CHECK(fabsf(as.duck_x) <= DUCK_WORLD_BOUND);
    CHECK(fabsf(as.duck_z) <= DUCK_WORLD_BOUND);
    float dx = as.duck_x - lily->x;
    float dz = as.duck_z - lily->z;
    CHECK(sqrtf(dx * dx + dz * dz) >= lily->radius + DUCK_COLLISION_RADIUS - 0.001f);
}

static AppState simulate_one_second(int render_hz)
{
    AppState as;
    PlayerIntent intent = {.move_x = 1.f, .move_z = 0.25f};
    double accumulator = 0.0;
    init_game(&as);

    for (int frame = 0; frame < render_hz; frame++) {
        int steps = game_accumulate_steps(&accumulator, 1.0 / (double)render_hz);
        for (int step = 0; step < steps; step++)
            game_step(&as, &intent, (float)FIXED_TIMESTEP);
    }
    return as;
}

static void test_fixed_step_equivalence(void)
{
    AppState at_30 = simulate_one_second(30);
    AppState at_60 = simulate_one_second(60);
    AppState at_120 = simulate_one_second(120);

    CHECK(fabsf(at_30.duck_x - at_60.duck_x) < 0.0001f);
    CHECK(fabsf(at_60.duck_x - at_120.duck_x) < 0.0001f);
    CHECK(fabsf(at_30.duck_z - at_120.duck_z) < 0.0001f);
    CHECK(fabsf(at_30.duck_vx - at_120.duck_vx) < 0.0001f);

    double accumulator = 0.0;
    CHECK(game_accumulate_steps(&accumulator, 1.0) == 3);
    CHECK(accumulator >= 0.0 && accumulator < FIXED_TIMESTEP);
}

static void test_touch_math(void)
{
    mat4x4 projection;
    mat4x4 view;
    vec3 eye = {0.f, 10.f, -10.f};
    vec3 target = {0.f, 0.f, 0.f};
    float x;
    float z;

    make_perspective_lh_zo(projection, 4.f / 3.f);
    make_look_at_lh(view, eye, target);
    CHECK(input_screen_to_pond(projection, view, 0.5f, 0.5f, &x, &z));
    CHECK(fabsf(x) < 0.0001f);
    CHECK(fabsf(z) < 0.0001f);
    CHECK(input_gesture_is_tap(100, 100 + TOUCH_TAP_MAX_NS, 0.f));
    CHECK(!input_gesture_is_tap(100, 101 + TOUCH_TAP_MAX_NS, 0.f));
    CHECK(!input_gesture_is_tap(100, 200,
                                TOUCH_TAP_MAX_DISTANCE * TOUCH_TAP_MAX_DISTANCE * 1.1f));
    CHECK(fabsf(input_calculate_splash_intensity(0.f, 0.f) - 0.35f) < 0.0001f);
    CHECK(fabsf(input_calculate_splash_intensity(10.f, 0.f) - 1.5f) < 0.0001f);
    CHECK(input_calculate_splash_intensity(0.1f, 4.f) > 1.f);
}

static void test_touch_handoff(void)
{
    AppState as;
    InputAction action;
    SDL_Event event = {0};
    vec3 eye = {0.f, 10.f, -10.f};
    vec3 target = {0.f, 0.f, 0.f};
    init_game(&as);
    make_perspective_lh_zo(as.proj, 4.f / 3.f);
    make_look_at_lh(as.picking_view, eye, target);
    CHECK(as.activity.mode == ACTIVITY_MODE_FREE_PLAY);
    CHECK(as.activity.current_prompt == -1);

    event.type = SDL_EVENT_FINGER_DOWN;
    event.tfinger.touchID = 1;
    event.tfinger.fingerID = 10;
    event.tfinger.timestamp = 100;
    event.tfinger.x = 0.4f;
    event.tfinger.y = 0.5f;
    CHECK(input_handle_touch_event(&as, &event, &action));
    CHECK(as.input.primary_touch >= 0);
    int first = as.input.primary_touch;

    event.tfinger.fingerID = 11;
    event.tfinger.timestamp = 200;
    event.tfinger.x = 0.6f;
    CHECK(input_handle_touch_event(&as, &event, &action));
    CHECK(as.input.primary_touch == first);

    event.type = SDL_EVENT_FINGER_UP;
    event.tfinger.fingerID = 10;
    event.tfinger.timestamp = 400;
    event.tfinger.x = 0.4f;
    CHECK(input_handle_touch_event(&as, &event, &action));
    CHECK(as.input.primary_touch >= 0);
    CHECK(as.input.touches[as.input.primary_touch].finger_id == 11);
}

static void test_invalid_touch_does_not_own_movement(void)
{
    AppState as;
    InputAction action;
    SDL_Event event = {0};
    vec3 eye = {0.f, 10.f, -10.f};
    vec3 target = {0.f, 0.f, 0.f};
    init_game(&as);
    make_perspective_lh_zo(as.proj, 4.f / 3.f);
    make_look_at_lh(as.picking_view, eye, target);

    event.type = SDL_EVENT_FINGER_DOWN;
    event.tfinger.touchID = 1;
    event.tfinger.fingerID = 20;
    event.tfinger.timestamp = 100;
    event.tfinger.x = 0.5f;
    event.tfinger.y = -10.f;
    CHECK(input_handle_touch_event(&as, &event, &action));
    CHECK(as.input.primary_touch == -1);

    event.tfinger.fingerID = 21;
    event.tfinger.timestamp = 200;
    event.tfinger.x = 0.5f;
    event.tfinger.y = 0.5f;
    CHECK(input_handle_touch_event(&as, &event, &action));
    CHECK(as.input.primary_touch >= 0);
    CHECK(as.input.touches[as.input.primary_touch].finger_id == 21);
}

static void test_ten_touch_capacity(void)
{
    AppState as;
    InputAction action;
    SDL_Event event = {0};
    vec3 eye = {0.f, 10.f, -10.f};
    vec3 target = {0.f, 0.f, 0.f};
    init_game(&as);
    make_perspective_lh_zo(as.proj, 4.f / 3.f);
    make_look_at_lh(as.picking_view, eye, target);

    event.type = SDL_EVENT_FINGER_DOWN;
    event.tfinger.touchID = 1;
    event.tfinger.timestamp = 100;
    event.tfinger.y = 0.5f;
    for (int i = 0; i < MAX_TOUCHES; i++) {
        event.tfinger.fingerID = (SDL_FingerID)(100 + i);
        event.tfinger.x = 0.2f + (float)i * 0.06f;
        CHECK(input_handle_touch_event(&as, &event, &action));
        CHECK(action.interaction_requested);
    }
    event.tfinger.fingerID = 999;
    CHECK(!input_handle_touch_event(&as, &event, &action));
}

static void test_trail_sample_overflow_resets_retained_head(void)
{
    AppState as;
    InputAction action;
    SDL_Event event = {0};
    vec3 eye = {0.f, 10.f, -10.f};
    vec3 target = {0.f, 0.f, 0.f};
    init_game(&as);
    make_perspective_lh_zo(as.proj, 4.f / 3.f);
    make_look_at_lh(as.picking_view, eye, target);
    event.type = SDL_EVENT_FINGER_DOWN;
    event.tfinger.touchID = 5;
    event.tfinger.fingerID = 80;
    event.tfinger.x = 0.2f;
    event.tfinger.y = 0.5f;
    CHECK(input_handle_touch_event(&as, &event, &action));
    for (int i = 0; i < MAX_TRAIL_SAMPLES + 10; i++) {
        event.type = SDL_EVENT_FINGER_MOTION;
        event.tfinger.x = 0.2f + 0.6f * (float)i
                       / (float)(MAX_TRAIL_SAMPLES + 9);
        CHECK(input_handle_touch_event(&as, &event, &action));
    }
    CHECK(as.input.trail_count == MAX_TRAIL_SAMPLES);
    CHECK(as.input.trail_samples[as.input.trail_read].reset);
}

static void test_mouse_simulates_touch_without_ctrl(void)
{
    AppState as;
    InputAction action;
    SDL_Event event = {0};
    vec3 eye = {0.f, 10.f, -10.f};
    vec3 target = {0.f, 0.f, 0.f};
    init_game(&as);
    as.win_w = 800;
    as.win_h = 600;
    make_perspective_lh_zo(as.proj, 4.f / 3.f);
    make_look_at_lh(as.picking_view, eye, target);

    CHECK(!input_modifiers_request_camera(SDL_KMOD_NONE));
    CHECK(input_modifiers_request_camera(SDL_KMOD_LCTRL));
    CHECK(input_modifiers_request_camera(SDL_KMOD_RCTRL));

    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.timestamp = 1000000000ULL;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = 600.f;
    event.button.y = 300.f;
    CHECK(input_handle_mouse_touch_event(&as, &event, &action));
    CHECK(action.interaction_requested);
    CHECK(action.interaction.type == INTERACTION_POND_PRESS);
    PlayerIntent intent = input_get_player_intent(&as);
    CHECK(intent.has_swim_target);
    CHECK(intent.swim_target_x > 0.f);

    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.timestamp = 1100000000ULL;
    CHECK(input_handle_mouse_touch_event(&as, &event, &action));
    CHECK(action.interaction_requested);
    CHECK(action.interaction.type == INTERACTION_TAP_WATER);
    CHECK(as.input.primary_touch == -1);

    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.timestamp = 1200000000ULL;
    event.button.x = 400.f;
    CHECK(input_handle_mouse_touch_event(&as, &event, &action));
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.timestamp = 1250000000ULL;
    event.motion.x = 700.f;
    event.motion.y = 300.f;
    CHECK(input_handle_mouse_touch_event(&as, &event, &action));
    intent = input_get_player_intent(&as);
    CHECK(intent.has_swim_target);
    CHECK(intent.swim_target_x > 0.f);
    input_cancel_mouse_touch(&as);
    CHECK(as.input.primary_touch == -1);

    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.timestamp = 2000000000ULL;
    event.button.x = 400.f;
    event.button.y = 300.f;
    CHECK(input_handle_mouse_touch_event(&as, &event, &action));
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.timestamp = 4000000000ULL;
    CHECK(input_handle_mouse_touch_event(&as, &event, &action));
    CHECK(action.interaction_requested);
    CHECK(action.interaction.type == INTERACTION_HOLD_RELEASE);
    CHECK(action.interaction.intensity == 1.5f);
    CHECK(as.input.primary_touch == -1);
}

static InteractionEvent simulate_drag_release(int motion_count)
{
    AppState as;
    InputAction action;
    SDL_Event event = {0};
    vec3 eye = {0.f, 10.f, -10.f};
    vec3 target = {0.f, 0.f, 0.f};
    init_game(&as);
    make_perspective_lh_zo(as.proj, 4.f / 3.f);
    make_look_at_lh(as.picking_view, eye, target);

    event.type = SDL_EVENT_FINGER_DOWN;
    event.tfinger.touchID = 2;
    event.tfinger.fingerID = 50;
    event.tfinger.timestamp = 1000000000ULL;
    event.tfinger.x = 0.45f;
    event.tfinger.y = 0.5f;
    CHECK(input_handle_touch_event(&as, &event, &action));

    for (int i = 1; i <= motion_count; i++) {
        float progress = (float)i / (float)(motion_count + 1);
        event.type = SDL_EVENT_FINGER_MOTION;
        event.tfinger.timestamp = 1000000000ULL + (Uint64)(progress * 200000000.f);
        event.tfinger.x = 0.45f + progress * 0.1f;
        CHECK(input_handle_touch_event(&as, &event, &action));
    }

    event.type = SDL_EVENT_FINGER_UP;
    event.tfinger.timestamp = 1200000000ULL;
    event.tfinger.x = 0.55f;
    CHECK(input_handle_touch_event(&as, &event, &action));
    CHECK(action.interaction_requested);
    CHECK(action.interaction.type == INTERACTION_HOLD_RELEASE);
    return action.interaction;
}

static void test_drag_intensity_is_event_rate_independent(void)
{
    InteractionEvent direct = simulate_drag_release(0);
    InteractionEvent sampled = simulate_drag_release(12);
    CHECK(direct.intensity > 0.35f && direct.intensity < 1.5f);
    CHECK(fabsf(direct.intensity - sampled.intensity) < 0.0001f);
    CHECK(fabsf(direct.x - sampled.x) < 0.0001f);
    CHECK(fabsf(direct.z - sampled.z) < 0.0001f);
}

static PlayerIntent simulate_held_motion(int motion_count)
{
    AppState as;
    InputAction action;
    SDL_Event event = {0};
    vec3 eye = {0.f, 10.f, -10.f};
    vec3 target = {0.f, 0.f, 0.f};
    init_game(&as);
    make_perspective_lh_zo(as.proj, 4.f / 3.f);
    make_look_at_lh(as.picking_view, eye, target);
    event.type = SDL_EVENT_FINGER_DOWN;
    event.tfinger.touchID = 3;
    event.tfinger.fingerID = 60;
    event.tfinger.timestamp = 100;
    event.tfinger.x = 0.45f;
    event.tfinger.y = 0.5f;
    CHECK(input_handle_touch_event(&as, &event, &action));
    for (int i = 1; i <= motion_count; i++) {
        float progress = (float)i / (float)motion_count;
        event.type = SDL_EVENT_FINGER_MOTION;
        event.tfinger.timestamp++;
        event.tfinger.x = 0.45f + progress * 0.1f;
        CHECK(input_handle_touch_event(&as, &event, &action));
    }
    return input_get_player_intent(&as);
}

static void test_touch_smoothing_is_event_rate_independent(void)
{
    PlayerIntent one_event = simulate_held_motion(1);
    PlayerIntent many_events = simulate_held_motion(12);
    CHECK(one_event.has_swim_target && many_events.has_swim_target);
    CHECK(fabsf(one_event.swim_target_x - many_events.swim_target_x) < 0.0001f);
    CHECK(fabsf(one_event.swim_target_z - many_events.swim_target_z) < 0.0001f);
}

static int count_active_ripples(const AppState *as)
{
    int count = 0;
    for (int i = 0; i < MAX_RIPPLES; i++)
        if (as->ripples[i].active) count++;
    return count;
}

static int count_active_particles(const AppState *as)
{
    int count = 0;
    for (int i = 0; i < MAX_PARTICLES; i++)
        if (as->particles[i].active) count++;
    return count;
}

static int count_particles_kind(const AppState *as, ParticleKind kind)
{
    int count = 0;
    for (int i = 0; i < MAX_PARTICLES; i++)
        if (as->particles[i].active && as->particles[i].kind == kind) count++;
    return count;
}

static void test_swim_to_finger(void)
{
    AppState as;
    InputAction action;
    SDL_Event event = {0};
    vec3 eye = {0.f, 10.f, -10.f};
    vec3 target = {0.f, 0.f, 0.f};
    init_game(&as);
    make_perspective_lh_zo(as.proj, 4.f / 3.f);
    make_look_at_lh(as.picking_view, eye, target);

    event.type = SDL_EVENT_FINGER_DOWN;
    event.tfinger.touchID = 1;
    event.tfinger.fingerID = 30;
    event.tfinger.timestamp = 100;
    event.tfinger.x = 0.65f;
    event.tfinger.y = 0.5f;
    CHECK(input_handle_touch_event(&as, &event, &action));

    PlayerIntent intent = input_get_player_intent(&as);
    CHECK(intent.has_swim_target);
    CHECK(intent.swim_target_x > 0.f);
    for (int i = 0; i < 30; i++)
        game_step(&as, &intent, (float)FIXED_TIMESTEP);
    CHECK(as.duck_x > 0.f);
    CHECK(as.duck_vx > 0.f);
}

static AppState simulate_obstacle_route(float target_x, float target_z,
                                        float *minimum_clearance,
                                        float *maximum_offset)
{
    AppState as;
    PlayerIntent intent = {
        .has_swim_target = true,
        .swim_target_x = target_x,
        .swim_target_z = target_z
    };
    init_game(&as);
    pond_object_pool_reset(&as.pond_objects);
    PondObject *lily = pond_object_spawn(&as.pond_objects,
                                          POND_OBJECT_LILY_PAD, 0.f, 0.f);
    lily->radius = 1.2f;
    as.duck_x = -6.f;
    as.duck_z = 0.f;
    *minimum_clearance = 1000.f;
    *maximum_offset = 0.f;

    for (int i = 0; i < 600; i++) {
        game_step(&as, &intent, (float)FIXED_TIMESTEP);
        float distance = sqrtf(as.duck_x * as.duck_x + as.duck_z * as.duck_z);
        *minimum_clearance = SDL_min(*minimum_clearance, distance);
        *maximum_offset = SDL_max(*maximum_offset, fabsf(as.duck_z));
    }
    return as;
}

static void test_swim_target_avoids_lily(void)
{
    float first_clearance;
    float first_offset;
    float second_clearance;
    float second_offset;
    AppState first = simulate_obstacle_route(6.f, 0.f, &first_clearance,
                                             &first_offset);
    AppState second = simulate_obstacle_route(6.f, 0.f, &second_clearance,
                                              &second_offset);
    float physical_clearance = 1.2f + DUCK_COLLISION_RADIUS;

    CHECK(first.duck_x > 5.5f);
    CHECK(fabsf(first.duck_z) < 0.5f);
    CHECK(first_offset > physical_clearance);
    CHECK(first_clearance > physical_clearance + 0.05f);
    CHECK(fabsf(first.duck_x - second.duck_x) < 0.0001f);
    CHECK(fabsf(first.duck_z - second.duck_z) < 0.0001f);
    CHECK(fabsf(first_clearance - second_clearance) < 0.0001f);

    AppState orbit;
    PlayerIntent orbit_intent = {
        .has_swim_target = true,
        .swim_target_x = 0.f,
        .swim_target_z = 0.f
    };
    init_game(&orbit);
    pond_object_pool_reset(&orbit.pond_objects);
    PondObject *orbit_lily = pond_object_spawn(&orbit.pond_objects,
                                                POND_OBJECT_LILY_PAD, 0.f, 0.f);
    orbit_lily->radius = 1.2f;
    orbit.duck_x = -6.f;
    float previous_angle = atan2f(orbit.duck_z, orbit.duck_x);
    float orbit_angle = 0.f;
    float orbit_clearance = 1000.f;
    for (int i = 0; i < 600; i++) {
        game_step(&orbit, &orbit_intent, (float)FIXED_TIMESTEP);
        float distance = sqrtf(orbit.duck_x * orbit.duck_x
                             + orbit.duck_z * orbit.duck_z);
        orbit_clearance = SDL_min(orbit_clearance, distance);
        float angle = atan2f(orbit.duck_z, orbit.duck_x);
        if (i >= 300) {
            float delta = angle - previous_angle;
            if (delta > SDL_PI_F) delta -= 2.f * SDL_PI_F;
            if (delta < -SDL_PI_F) delta += 2.f * SDL_PI_F;
            orbit_angle += fabsf(delta);
        }
        previous_angle = angle;
    }
    float orbit_distance = sqrtf(orbit.duck_x * orbit.duck_x
                               + orbit.duck_z * orbit.duck_z);
    CHECK(isfinite(orbit.duck_x) && isfinite(orbit.duck_z));
    CHECK(orbit_clearance >= physical_clearance - 0.001f);
    CHECK(fabsf(orbit_distance - (physical_clearance + 0.15f)) < 0.35f);
    CHECK(orbit_angle > 2.f * SDL_PI_F);
    CHECK(sqrtf(orbit.duck_vx * orbit.duck_vx + orbit.duck_vz * orbit.duck_vz) > 1.f);
    game_step(&orbit, &(PlayerIntent){0}, (float)FIXED_TIMESTEP);
    CHECK(orbit.duck_avoidance.obstacle_id == 0);

    AppState fast;
    PlayerIntent intent = {
        .has_swim_target = true,
        .swim_target_x = 6.f,
        .swim_target_z = 0.f
    };
    init_game(&fast);
    pond_object_pool_reset(&fast.pond_objects);
    PondObject *lily = pond_object_spawn(&fast.pond_objects,
                                          POND_OBJECT_LILY_PAD, 0.f, 0.f);
    lily->radius = 1.2f;
    fast.duck_x = -6.f;
    fast.duck_vx = MAX_SPEED;
    float fast_minimum_clearance = 1000.f;
    for (int i = 0; i < 600; i++) {
        game_step(&fast, &intent, (float)FIXED_TIMESTEP);
        float distance = sqrtf(fast.duck_x * fast.duck_x
                             + fast.duck_z * fast.duck_z);
        fast_minimum_clearance = SDL_min(fast_minimum_clearance, distance);
    }
    CHECK(fast.duck_x > 5.5f);
    CHECK(fast_minimum_clearance >= physical_clearance - 0.001f);

    AppState edge;
    init_game(&edge);
    pond_object_pool_reset(&edge.pond_objects);
    lily = pond_object_spawn(&edge.pond_objects, POND_OBJECT_LILY_PAD, 15.f, 0.f);
    lily->radius = 1.f;
    edge.duck_x = 10.f;
    intent.swim_target_x = DUCK_WORLD_BOUND;
    for (int i = 0; i < 360; i++)
        game_step(&edge, &intent, (float)FIXED_TIMESTEP);
    CHECK(fabsf(edge.duck_x) <= DUCK_WORLD_BOUND);
    CHECK(fabsf(edge.duck_z) <= DUCK_WORLD_BOUND);
    float edge_dx = edge.duck_x - lily->x;
    float edge_dz = edge.duck_z - lily->z;
    CHECK(sqrtf(edge_dx * edge_dx + edge_dz * edge_dz)
          >= lily->radius + DUCK_COLLISION_RADIUS - 0.001f);
    CHECK(edge.duck_x < lily->x);
}

static void test_interactions_are_deferred(void)
{
    AppState as;
    InputAction action;
    PlayerIntent intent = {0};
    SDL_Event event = {0};
    vec3 eye = {0.f, 10.f, -10.f};
    vec3 target = {0.f, 0.f, 0.f};
    init_game(&as);
    make_perspective_lh_zo(as.proj, 4.f / 3.f);
    make_look_at_lh(as.picking_view, eye, target);

    event.type = SDL_EVENT_FINGER_DOWN;
    event.tfinger.touchID = 1;
    event.tfinger.fingerID = 40;
    event.tfinger.timestamp = 1000000000ULL;
    event.tfinger.x = 0.5f;
    event.tfinger.y = 0.5f;
    CHECK(input_handle_touch_event(&as, &event, &action));
    CHECK(action.interaction_requested);
    CHECK(action.interaction.type == INTERACTION_POND_PRESS);
    CHECK(count_active_ripples(&as) == 0);
    CHECK(interaction_queue_push(&as.interaction_queue, &action.interaction));
    CHECK(count_active_ripples(&as) == 0);

    game_step(&as, &intent, (float)FIXED_TIMESTEP);
    CHECK(count_active_ripples(&as) == 1);
    CHECK(as.activity.total_taps == 0);
    CHECK(as.activity.inactivity_time == 0.f);

    event.type = SDL_EVENT_FINGER_UP;
    event.tfinger.timestamp = 1100000000ULL;
    CHECK(input_handle_touch_event(&as, &event, &action));
    CHECK(action.interaction_requested);
    CHECK(action.interaction.type == INTERACTION_TAP_WATER);
    CHECK(interaction_queue_push(&as.interaction_queue, &action.interaction));
    CHECK(as.activity.total_taps == 0);
    CHECK(as.quack_cooldown == 0.f);

    game_step(&as, &intent, (float)FIXED_TIMESTEP);
    CHECK(as.activity.total_taps == 1);
    CHECK(as.activity.duck_quacks == 1);
    CHECK(as.quack_cooldown > 0.f);
    CHECK(as.interaction_queue.count == 0);
}

static void test_tap_effect_survives_quack_cooldown(void)
{
    AppState as;
    PlayerIntent intent = {0};
    InteractionEvent event = {
        .type = INTERACTION_TAP_WATER,
        .x = 2.f,
        .z = 3.f,
        .intensity = 1.f,
        .object_id = -1
    };
    init_game(&as);
    as.quack_cooldown = 1.f;
    CHECK(interaction_queue_push(&as.interaction_queue, &event));
    game_step(&as, &intent, (float)FIXED_TIMESTEP);
    CHECK(as.activity.total_taps == 1);
    CHECK(as.activity.duck_quacks == 0);
    CHECK(count_active_ripples(&as) == 2);
    CHECK(count_active_particles(&as) == 11);
}

static void test_scaled_splashes(void)
{
    AppState small;
    AppState big;
    init_game(&small);
    init_game(&big);

    spawn_splash_scaled(&small, 1.f, 2.f, 0.35f);
    spawn_splash_scaled(&big, 1.f, 2.f, 1.5f);
    CHECK(count_active_particles(&small) == 6);
    CHECK(count_active_particles(&big) == 14);
    CHECK(big.particles[0].size > small.particles[0].size);
    CHECK(big.particles[0].life > small.particles[0].life);
    CHECK(big.particles[0].max_life == big.particles[0].life);

    spawn_ripple_scaled(&small, 0.f, 0.f, 0.35f);
    spawn_ripple_scaled(&big, 0.f, 0.f, 1.5f);
    CHECK(big.ripples[0].max_radius > small.ripples[0].max_radius);
    CHECK(big.ripples[0].speed > small.ripples[0].speed);
    CHECK(big.ripples[0].max_alpha > small.ripples[0].max_alpha);

    AppState small_tap;
    AppState big_tap;
    init_game(&small_tap);
    init_game(&big_tap);
    CHECK(spawn_tap_effect_scaled(&small_tap, 0.f, 0.f, 0.35f));
    CHECK(spawn_tap_effect_scaled(&big_tap, 0.f, 0.f, 1.5f));
    CHECK(count_active_particles(&big_tap) > count_active_particles(&small_tap));
    CHECK(big_tap.ripples[0].max_radius > small_tap.ripples[0].max_radius);

    AppState release;
    PlayerIntent intent = {0};
    InteractionEvent event = {
        .type = INTERACTION_HOLD_RELEASE,
        .x = 2.f,
        .z = 3.f,
        .intensity = 1.5f
    };
    init_game(&release);
    CHECK(interaction_queue_push(&release.interaction_queue, &event));
    game_step(&release, &intent, (float)FIXED_TIMESTEP);
    CHECK(count_active_particles(&release) == 14);
    CHECK(count_active_ripples(&release) == 1);
}

static AppState simulate_activity_one_second(int render_hz)
{
    AppState as;
    PlayerIntent intent = {0};
    InteractionEvent event = {
        .type = INTERACTION_TAP_WATER,
        .x = 2.f,
        .z = 3.f,
        .intensity = 1.f,
        .object_id = -1,
        .target_tick = 45
    };
    double accumulator = 0.0;
    init_game(&as);
    interaction_queue_push(&as.interaction_queue, &event);

    for (int frame = 0; frame < render_hz; frame++) {
        int steps = game_accumulate_steps(&accumulator, 1.0 / (double)render_hz);
        for (int step = 0; step < steps; step++)
            game_step(&as, &intent, (float)FIXED_TIMESTEP);
    }
    return as;
}

static void test_activity_event_same_result_at_30_60_120_fps(void)
{
    AppState at_30 = simulate_activity_one_second(30);
    AppState at_60 = simulate_activity_one_second(60);
    AppState at_120 = simulate_activity_one_second(120);

    CHECK(at_30.activity.total_taps == 1);
    CHECK(at_30.activity.duck_quacks == 1);
    CHECK(at_30.activity.total_taps == at_60.activity.total_taps);
    CHECK(at_60.activity.total_taps == at_120.activity.total_taps);
    CHECK(at_30.ripple_head == at_120.ripple_head);
    CHECK(at_30.particle_head == at_120.particle_head);
    CHECK(at_30.simulation_rng == at_60.simulation_rng);
    CHECK(at_60.simulation_rng == at_120.simulation_rng);
    CHECK(fabsf(at_30.ripples[0].radius - at_60.ripples[0].radius) < 0.0001f);
    CHECK(fabsf(at_60.ripples[0].radius - at_120.ripples[0].radius) < 0.0001f);
    CHECK(at_30.particles[0].active == at_120.particles[0].active);
    CHECK(fabsf(at_30.particles[0].life - at_120.particles[0].life) < 0.0001f);
    CHECK(at_30.interaction_queue.count == 0);
    CHECK(at_60.interaction_queue.count == 0);
    CHECK(at_120.interaction_queue.count == 0);
}

static void test_scheduled_event_processed_exact_tick(void)
{
    AppState as;
    PlayerIntent intent = {0};
    InteractionEvent event = {
        .type = INTERACTION_POND_PRESS,
        .x = 1.f,
        .z = 1.f,
        .object_id = -1,
        .target_tick = 3
    };
    init_game(&as);
    CHECK(interaction_queue_push(&as.interaction_queue, &event));
    for (int i = 0; i < 3; i++)
        game_step(&as, &intent, (float)FIXED_TIMESTEP);
    CHECK(as.simulation_tick == 3);
    CHECK(count_active_ripples(&as) == 0);
    CHECK(as.interaction_queue.count == 1);
    game_step(&as, &intent, (float)FIXED_TIMESTEP);
    CHECK(as.simulation_tick == 4);
    CHECK(count_active_ripples(&as) == 1);
    CHECK(as.interaction_queue.count == 0);
}

static void test_typed_particles_and_bounded_pool(void)
{
    AppState as;
    PlayerIntent intent = {0};
    init_game(&as);

    for (int i = 0; i < MAX_PARTICLES * 2; i++)
        spawn_splash(&as, 0.f, 0.f);
    CHECK(count_active_particles(&as) == MAX_PARTICLES);
    CHECK(as.particle_head >= 0 && as.particle_head < MAX_PARTICLES);
    for (int i = 0; i < MAX_PARTICLES; i++) {
        CHECK(as.particles[i].kind == PARTICLE_WATER_DROPLET);
        CHECK(as.particles[i].max_life > 0.f);
        CHECK(as.particles[i].size > 0.f);
    }

    for (int i = 0; i < MAX_PARTICLES; i++)
        as.particles[i].active = false;
    as.bubble_timer = 0.f;
    game_step(&as, &intent, (float)FIXED_TIMESTEP);
    bool found_bubble = false;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (as.particles[i].active && as.particles[i].kind == PARTICLE_BUBBLE) {
            found_bubble = true;
            CHECK(as.particles[i].max_life == as.particles[i].life);
            CHECK(as.particles[i].alpha > 0.f);
        }
    }
    CHECK(found_bubble);
}

static void test_pond_object_baseline(void)
{
    AppState as;
    init_game(&as);
    int lily_count = 0;
    for (int i = 0; i < MAX_POND_OBJECTS; i++) {
        const PondObject *object = &as.pond_objects.objects[i];
        if (object->active && object->kind == POND_OBJECT_LILY_PAD)
            lily_count++;
    }
    CHECK(lily_count == 6);
    CHECK(as.pond_objects.objects[0].x == -6.f);
    CHECK(as.pond_objects.objects[0].z == -6.5f);
    CHECK(as.pond_objects.objects[0].radius == 1.2f);
    PlayerIntent intent = {0};
    InteractionEvent tap_lily = {
        .type = INTERACTION_TAP_WATER,
        .x = as.pond_objects.objects[0].x,
        .z = as.pond_objects.objects[0].z,
        .object_id = -1
    };
    CHECK(interaction_queue_push(&as.interaction_queue, &tap_lily));
    game_step(&as, &intent, (float)FIXED_TIMESTEP);
    CHECK(as.activity.last_interacted_object_id == as.pond_objects.objects[0].id);

    pond_object_pool_reset(&as.pond_objects);
    PondObject *far_object = pond_object_spawn(&as.pond_objects,
                                                POND_OBJECT_FLOWER, 1.f, 1.f);
    PondObject *near_object = pond_object_spawn(&as.pond_objects,
                                                 POND_OBJECT_POPPABLE_BUBBLE,
                                                 0.2f, 0.2f);
    far_object->interactive = true;
    far_object->radius = 2.f;
    near_object->interactive = true;
    near_object->radius = 0.5f;
    CHECK(pond_object_find_hit(&as.pond_objects, 0.1f, 0.1f) == near_object);
    near_object->state = POND_STATE_DISABLED;
    CHECK(!pond_object_is_visible(near_object));
    CHECK(pond_object_find_hit(&as.pond_objects, 0.1f, 0.1f) == far_object);
    near_object->state = POND_STATE_IDLE;
    near_object->alpha = 0.f;
    CHECK(!pond_object_is_visible(near_object));
    CHECK(pond_object_find_hit(&as.pond_objects, 0.1f, 0.1f) == far_object);
    near_object->alpha = 1.f;
    near_object->active = false;
    CHECK(pond_object_find_hit(&as.pond_objects, 0.1f, 0.1f) == far_object);

    pond_object_pool_reset(&as.pond_objects);
    int previous_id = 0;
    for (int i = 0; i < MAX_POND_OBJECTS; i++) {
        PondObject *object = pond_object_spawn(&as.pond_objects,
                                                POND_OBJECT_LEAF, 0.f, 0.f);
        CHECK(object != NULL);
        CHECK(object->id > previous_id);
        previous_id = object->id;
    }
    CHECK(pond_object_spawn(&as.pond_objects, POND_OBJECT_LEAF, 0.f, 0.f) == NULL);
}

static void test_duck_animation_baseline(void)
{
    DuckAnimation animation;
    duck_animation_init(&animation);
    CHECK(animation.state == DUCK_ANIMATION_IDLE);
    CHECK(animation.body_scale_x == 1.f);
    CHECK(animation.body_scale_y == 1.f);
    CHECK(animation.body_scale_z == 1.f);

    duck_animation_trigger(&animation, DUCK_ANIMATION_QUACK, 0.2f);
    duck_animation_update(&animation, 0.1f);
    CHECK(animation.state == DUCK_ANIMATION_QUACK);
    CHECK(animation.body_scale_x > 1.f);
    duck_animation_update(&animation, 0.1f);
    CHECK(animation.state == DUCK_ANIMATION_IDLE);

    duck_animation_trigger(&animation, DUCK_ANIMATION_SLEEP, 0.f);
    duck_animation_update(&animation, 1.f);
    CHECK(animation.state == DUCK_ANIMATION_SLEEP);
    CHECK(animation.body_scale_y < 1.f);
}

static void test_effect_pool_wraparound(void)
{
    AppState as;
    init_game(&as);
    for (int i = 0; i <= MAX_RIPPLES; i++)
        spawn_ripple(&as, (float)i, 0.f);
    CHECK(as.ripple_head == 1);
    CHECK(as.ripples[0].x == (float)MAX_RIPPLES);

    as.ripples[1] = (Ripple){.active = true};
    update_ripples(&as, (float)FIXED_TIMESTEP);
    CHECK(!as.ripples[1].active);
    CHECK(isfinite(as.ripples[1].alpha));
}

static int count_trail_ripples(const AppState *as)
{
    int count = 0;
    for (int i = 0; i < MAX_RIPPLES; i++) {
        if (as->ripples[i].active
            && fabsf(as->ripples[i].max_alpha - 0.18f) < 0.0001f)
            count++;
    }
    return count;
}

static void test_finger_trails_and_sleep_wake(void)
{
    AppState as;
    PlayerIntent intent = {0};
    init_game(&as);
    as.input.trail_samples[0] = (TrailSample){0.f, 0.f, 1, true};
    as.input.trail_samples[1] = (TrailSample){1.2f, 0.f, 1, false};
    as.input.trail_count = 2;
    as.input.trail_write = 2;
    game_step(&as, &intent, (float)FIXED_TIMESTEP);
    CHECK(count_trail_ripples(&as) == MAX_TRAIL_RIPPLES_PER_STEP);
    CHECK(as.input.trail_count == 1);
    game_step(&as, &intent, (float)FIXED_TIMESTEP);
    CHECK(as.input.trail_count == 0);

    AppState quick;
    InputAction action;
    SDL_Event touch = {0};
    vec3 eye = {0.f, 10.f, -10.f};
    vec3 target = {0.f, 0.f, 0.f};
    init_game(&quick);
    make_perspective_lh_zo(quick.proj, 4.f / 3.f);
    make_look_at_lh(quick.picking_view, eye, target);
    touch.type = SDL_EVENT_FINGER_DOWN;
    touch.tfinger.touchID = 4;
    touch.tfinger.fingerID = 70;
    touch.tfinger.timestamp = 100;
    touch.tfinger.x = 0.4f;
    touch.tfinger.y = 0.5f;
    CHECK(input_handle_touch_event(&quick, &touch, &action));
    touch.type = SDL_EVENT_FINGER_MOTION;
    touch.tfinger.timestamp = 150;
    touch.tfinger.x = 0.6f;
    CHECK(input_handle_touch_event(&quick, &touch, &action));
    touch.type = SDL_EVENT_FINGER_UP;
    touch.tfinger.timestamp = 200;
    CHECK(input_handle_touch_event(&quick, &touch, &action));
    CHECK(quick.input.primary_touch == -1);
    game_step(&quick, &intent, (float)FIXED_TIMESTEP);
    CHECK(count_trail_ripples(&quick) == MAX_TRAIL_RIPPLES_PER_STEP);
    CHECK(quick.input.trail_count > 0);

    init_game(&as);
    for (int i = 0; i <= (int)(DUCK_SLEEP_AFTER_SECONDS / FIXED_TIMESTEP); i++)
        game_step(&as, &intent, (float)FIXED_TIMESTEP);
    CHECK(as.duck_animation.state == DUCK_ANIMATION_SLEEP);
    CHECK(count_particles_kind(&as, PARTICLE_SLEEP_BUBBLE) > 0);

    surprised_quack_calls = 0;
    InteractionEvent press = {
        .type = INTERACTION_POND_PRESS,
        .x = as.duck_x,
        .z = as.duck_z
    };
    CHECK(interaction_queue_push(&as.interaction_queue, &press));
    game_step(&as, &intent, (float)FIXED_TIMESTEP);
    CHECK(as.duck_animation.state == DUCK_ANIMATION_WAKE);
    CHECK(surprised_quack_calls == 1);
    CHECK(count_particles_kind(&as, PARTICLE_SLEEP_BUBBLE) == 0);
}

static void test_poppable_bubbles_and_hidden_celebration(void)
{
    AppState as;
    PlayerIntent intent = {0};
    init_game(&as);
    PondObject *bubble = NULL;
    for (int i = 0; i < MAX_POND_OBJECTS; i++) {
        if (as.pond_objects.objects[i].active
            && as.pond_objects.objects[i].kind == POND_OBJECT_POPPABLE_BUBBLE) {
            bubble = &as.pond_objects.objects[i];
            break;
        }
    }
    CHECK(bubble != NULL);
    CHECK(fabsf(bubble->touch_radius
                - bubble->radius * POPPABLE_BUBBLE_TOUCH_MARGIN) < 0.0001f);
    int bubble_id = bubble->id;
    float x = bubble->x;
    float z = bubble->z;
    as.activity.bubble_pops_since_rainbow = BUBBLE_POPS_PER_RAINBOW - 1;
    bubble_pop_calls = 0;
    InteractionEvent tap = {
        .type = INTERACTION_TAP_OBJECT,
        .x = x,
        .z = z,
        .intensity = 1.f,
        .object_id = bubble_id
    };
    CHECK(interaction_queue_push(&as.interaction_queue, &tap));
    game_step(&as, &intent, (float)FIXED_TIMESTEP);
    CHECK(pond_object_find_by_id(&as.pond_objects, bubble_id) == NULL);
    CHECK(as.activity.bubbles_popped == 1);
    CHECK(bubble_pop_calls == 1);
    CHECK(count_particles_kind(&as, PARTICLE_SPARKLE) == 5);
    CHECK(count_particles_kind(&as, PARTICLE_RAINBOW) == 7);
    CHECK(as.activity.celebration_cooldown > 0.f);
    int particle_head_after_celebration = as.particle_head;

    CHECK(interaction_queue_push(&as.interaction_queue, &tap));
    game_step(&as, &intent, (float)FIXED_TIMESTEP);
    CHECK(as.activity.bubbles_popped == 1);
    CHECK(bubble_pop_calls == 1);
    for (int i = 0; i < (int)((CELEBRATION_COOLDOWN + 0.5f) / FIXED_TIMESTEP); i++)
        game_step(&as, &intent, (float)FIXED_TIMESTEP);
    CHECK(as.activity.celebration_cooldown == 0.f);
    CHECK(as.particle_head == particle_head_after_celebration);
}

static void test_bouncing_musical_lilies(void)
{
    AppState as;
    PlayerIntent intent = {0};
    init_game(&as);
    PondObject *lily = &as.pond_objects.objects[0];
    lily_note_calls = 0;
    last_lily_note = -1;
    InteractionEvent tap = {
        .type = INTERACTION_TAP_WATER,
        .x = lily->x,
        .z = lily->z,
        .intensity = 1.f
    };
    CHECK(interaction_queue_push(&as.interaction_queue, &tap));
    game_step(&as, &intent, (float)FIXED_TIMESTEP);
    CHECK(lily_note_calls == 1);
    CHECK(last_lily_note == 0);
    CHECK(lily->variant == 0);
    CHECK(as.pond_objects.objects[4].variant == 4);
    CHECK(as.pond_objects.objects[5].variant == 0);
    CHECK(lily->lily_motion.vertical_velocity > 0.f);
    for (int i = 0; i < 600; i++)
        pond_objects_update(&as.pond_objects, (float)FIXED_TIMESTEP);
    CHECK(fabsf(lily->lily_motion.height_offset) < 0.001f);
    CHECK(fabsf(lily->lily_motion.tilt) < 0.001f);

    AppState pressure;
    PlayerIntent push = {.move_x = 1.f};
    init_game(&pressure);
    pond_object_pool_reset(&pressure.pond_objects);
    lily = pond_object_spawn(&pressure.pond_objects, POND_OBJECT_LILY_PAD, 0.f, 0.f);
    lily->radius = 1.f;
    pressure.duck_x = -(lily->radius + DUCK_COLLISION_RADIUS);
    for (int i = 0; i < 120; i++)
        game_step(&pressure, &push, (float)FIXED_TIMESTEP);
    CHECK(count_active_particles(&pressure) < MAX_PARTICLES);
    CHECK(fabsf(lily->lily_motion.height_offset) < 0.75f);
}

static void test_rainbows_and_zoomies(void)
{
    AppState rainbow;
    PlayerIntent intent = {0};
    init_game(&rainbow);
    for (int i = 0; i < SPLASHES_PER_RAINBOW; i++) {
        InteractionEvent tap = {
            .type = INTERACTION_TAP_WATER,
            .x = 13.f,
            .z = 13.f,
            .intensity = 1.f
        };
        CHECK(interaction_queue_push(&rainbow.interaction_queue, &tap));
    }
    game_step(&rainbow, &intent, (float)FIXED_TIMESTEP);
    CHECK(count_particles_kind(&rainbow, PARTICLE_RAINBOW) == 7);
    CHECK(rainbow.activity.celebration_cooldown > 0.f);

    AppState pressure;
    init_game(&pressure);
    for (int i = 0; i < 16; i++) {
        InteractionEvent tap = {
            .type = INTERACTION_TAP_WATER,
            .x = 13.f,
            .z = 13.f,
            .intensity = 1.f
        };
        CHECK(interaction_queue_push(&pressure.interaction_queue, &tap));
    }
    game_step(&pressure, &intent, (float)FIXED_TIMESTEP);
    CHECK(count_particles_kind(&pressure, PARTICLE_RAINBOW) == 7);

    AppState zoomies;
    init_game(&zoomies);
    for (int i = 0; i < ZOOMIES_REQUIRED_TAPS; i++) {
        InteractionEvent tap = {
            .type = INTERACTION_TAP_DUCK,
            .x = zoomies.duck_x,
            .z = zoomies.duck_z,
            .intensity = 0.5f
        };
        CHECK(interaction_queue_push(&zoomies.interaction_queue, &tap));
    }
    game_step(&zoomies, &intent, (float)FIXED_TIMESTEP);
    CHECK(zoomies.activity.zoomies.active);
    CHECK(zoomies.duck_animation.state == DUCK_ANIMATION_ZOOMIES);
    CHECK(fabsf(zoomies.duck_vx) + fabsf(zoomies.duck_vz) > 0.f);
    intent.move_x = 1.f;
    game_step(&zoomies, &intent, (float)FIXED_TIMESTEP);
    CHECK(!zoomies.activity.zoomies.active);

    AppState spaced;
    init_game(&spaced);
    intent = (PlayerIntent){0};
    for (int i = 0; i < ZOOMIES_REQUIRED_TAPS; i++) {
        InteractionEvent tap = {
            .type = INTERACTION_TAP_DUCK,
            .x = spaced.duck_x,
            .z = spaced.duck_z,
            .intensity = 0.5f
        };
        spaced.quack_cooldown = 0.f;
        CHECK(interaction_queue_push(&spaced.interaction_queue, &tap));
        game_step(&spaced, &intent, (float)FIXED_TIMESTEP);
    }
    CHECK(spaced.activity.zoomies.active);
    CHECK(spaced.duck_animation.state == DUCK_ANIMATION_ZOOMIES);
    InteractionEvent quack = {.type = INTERACTION_QUACK_REQUEST};
    spaced.quack_cooldown = 0.f;
    CHECK(interaction_queue_push(&spaced.interaction_queue, &quack));
    game_step(&spaced, &intent, (float)FIXED_TIMESTEP);
    CHECK(!spaced.activity.zoomies.active);
    CHECK(spaced.duck_animation.state == DUCK_ANIMATION_QUACK);
}

static void test_day_night_and_fireflies(void)
{
    AppState as;
    init_game(&as);
    environment_set_night(&as, true);
    for (int i = 0; i < 240; i++)
        environment_update(&as, (float)FIXED_TIMESTEP);
    CHECK(as.environment.day_mix < 0.1f);
    int visible_fireflies = 0;
    for (int i = 0; i < MAX_POND_OBJECTS; i++) {
        PondObject *object = &as.pond_objects.objects[i];
        if (object->kind == POND_OBJECT_FIREFLY && pond_object_is_visible(object))
            visible_fireflies++;
    }
    CHECK(visible_fireflies == NIGHT_FIREFLY_COUNT);
    CHECK(as.duck_reflection_alpha < 0.1f);

    environment_set_night(&as, false);
    for (int i = 0; i < 240; i++)
        environment_update(&as, (float)FIXED_TIMESTEP);
    CHECK(as.environment.day_mix > 0.9f);
    for (int i = 0; i < MAX_POND_OBJECTS; i++) {
        PondObject *object = &as.pond_objects.objects[i];
        if (object->kind == POND_OBJECT_FIREFLY)
            CHECK(!pond_object_is_visible(object));
    }
}

int main(void)
{
    test_world_velocity();
    test_duck_model_heading();
    test_boundaries();
    test_lily_collision();
    test_fixed_step_equivalence();
    test_touch_math();
    test_touch_handoff();
    test_invalid_touch_does_not_own_movement();
    test_ten_touch_capacity();
    test_trail_sample_overflow_resets_retained_head();
    test_mouse_simulates_touch_without_ctrl();
    test_drag_intensity_is_event_rate_independent();
    test_touch_smoothing_is_event_rate_independent();
    test_swim_to_finger();
    test_swim_target_avoids_lily();
    test_interactions_are_deferred();
    test_tap_effect_survives_quack_cooldown();
    test_scaled_splashes();
    test_activity_event_same_result_at_30_60_120_fps();
    test_scheduled_event_processed_exact_tick();
    test_typed_particles_and_bounded_pool();
    test_pond_object_baseline();
    test_duck_animation_baseline();
    test_effect_pool_wraparound();
    test_finger_trails_and_sleep_wake();
    test_poppable_bubbles_and_hidden_celebration();
    test_bouncing_musical_lilies();
    test_rainbows_and_zoomies();
    test_day_night_and_fireflies();

    if (failures == 0)
        printf("All simulation tests passed.\n");
    return failures == 0 ? 0 : 1;
}
