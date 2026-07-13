#include "game.h"
#include "input.h"

#include <math.h>
#include <stdio.h>

const LilyPad LILY_PADS[LILY_PAD_COUNT] = {
    {-6.0f, -6.5f, 1.2f},
    { 7.0f, -5.5f, 1.0f},
    {-5.5f,  6.5f, 1.4f},
    { 6.0f,  6.0f, 1.1f},
    { 0.5f, -7.5f, 0.9f},
    { 7.5f,  2.5f, 1.2f}
};

void play_quack(AppState *as) { (void)as; }
void play_splash(AppState *as) { (void)as; }

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
    as->idle_quack_timer = 1000.f;
    as->bubble_timer = 1000.f;
    input_init(as);
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
    float min_distance = LILY_PADS[0].r + DUCK_COLLISION_RADIUS;
    init_game(&as);
    as.duck_x = LILY_PADS[0].x;
    as.duck_z = LILY_PADS[0].z - min_distance * 0.8f;
    as.duck_vz = 2.f;
    update_lilypad_collisions(&as);
    CHECK(as.duck_vz < 0.f);
    CHECK(fabsf(as.duck_z - (LILY_PADS[0].z - min_distance)) < 0.0001f);

    init_game(&as);
    as.duck_x = LILY_PADS[0].x;
    as.duck_z = LILY_PADS[0].z;
    update_lilypad_collisions(&as);
    CHECK(isfinite(as.duck_x) && isfinite(as.duck_z));
    CHECK(fabsf(as.duck_x - (LILY_PADS[0].x + min_distance)) < 0.0001f);
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
    make_look_at_lh(as.view, eye, target);

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
    make_look_at_lh(as.view, eye, target);

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

static void test_effect_pool_wraparound(void)
{
    AppState as;
    init_game(&as);
    for (int i = 0; i <= MAX_RIPPLES; i++)
        spawn_ripple(&as, (float)i, 0.f);
    CHECK(as.ripple_head == 1);
    CHECK(as.ripples[0].x == (float)MAX_RIPPLES);
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
    test_effect_pool_wraparound();

    if (failures == 0)
        printf("All simulation tests passed.\n");
    return failures == 0 ? 0 : 1;
}
