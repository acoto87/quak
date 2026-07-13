#include "game.h"
#include "activities.h"
#include "audio.h"
#include "duck_animation.h"
#include "pond_objects.h"
#include <math.h>

#define DUCK_AVOIDANCE_CLEARANCE 0.15f
#define DUCK_AVOIDANCE_SPEED 4.0f
#define DUCK_AVOIDANCE_JOIN_DISTANCE 0.12f
#define DUCK_AVOIDANCE_STEP_ANGLE (SDL_PI_F / 12.0f)
#define DUCK_AVOIDANCE_EPSILON 0.001f
#define DUCK_AVOIDANCE_UNREACHABLE 1.0e30f

typedef struct {
    float x, z, radius;
} InitialLily;

static const InitialLily INITIAL_LILIES[] = {
    {-6.0f, -6.5f, 1.2f},
    { 7.0f, -5.5f, 1.0f},
    {-5.5f,  6.5f, 1.4f},
    { 6.0f,  6.0f, 1.1f},
    { 0.5f, -7.5f, 0.9f},
    { 7.5f,  2.5f, 1.2f}
};

static float game_randf(AppState *as)
{
    return SDL_randf_r(&as->simulation_rng);
}

bool spawn_poppable_bubble(AppState *as)
{
    if (pond_object_count_kind(&as->pond_objects, POND_OBJECT_POPPABLE_BUBBLE)
        >= POPPABLE_BUBBLE_TARGET)
        return false;

    for (int attempt = 0; attempt < 12; attempt++) {
        float x = (game_randf(as) * 2.f - 1.f) * 12.f;
        float z = (game_randf(as) * 2.f - 1.f) * 12.f;
        bool clear = (x - as->duck_x) * (x - as->duck_x)
                   + (z - as->duck_z) * (z - as->duck_z) > 9.f;
        for (int i = 0; clear && i < MAX_POND_OBJECTS; i++) {
            const PondObject *other = &as->pond_objects.objects[i];
            if (!pond_object_is_visible(other))
                continue;
            float dx = x - other->x;
            float dz = z - other->z;
            float clearance = other->kind == POND_OBJECT_LILY_PAD
                            ? other->radius * other->scale + 0.8f : 1.2f;
            if (dx * dx + dz * dz < clearance * clearance)
                clear = false;
        }
        if (!clear)
            continue;

        PondObject *bubble = pond_object_spawn(&as->pond_objects,
                                                POND_OBJECT_POPPABLE_BUBBLE,
                                                x, z);
        if (!bubble)
            return false;
        bubble->interactive = true;
        bubble->radius = 0.28f + game_randf(as) * 0.14f;
        bubble->touch_radius = bubble->radius * POPPABLE_BUBBLE_TOUCH_MARGIN;
        bubble->y = 0.38f + game_randf(as) * 0.2f;
        bubble->alpha = 0.76f;
        bubble->red = 0.68f;
        bubble->green = 0.9f;
        bubble->blue = 1.f;
        bubble->phase = game_randf(as) * 2.f * SDL_PI_F;
        bubble->vx = (game_randf(as) - 0.5f) * 0.18f;
        bubble->vz = (game_randf(as) - 0.5f) * 0.18f;
        return true;
    }
    return false;
}

void game_init(AppState *as)
{
    interaction_queue_reset(&as->interaction_queue);
    as->simulation_tick = 0;
    as->interaction_overflow_count = 0;
    pond_object_pool_reset(&as->pond_objects);
    duck_animation_init(&as->duck_animation);
    activities_init(&as->activity);
    as->duck_avoidance = (DuckAvoidanceState){0};

    for (int i = 0; i < (int)(sizeof(INITIAL_LILIES) / sizeof(INITIAL_LILIES[0])); i++) {
        PondObject *lily = pond_object_spawn(&as->pond_objects,
                                              POND_OBJECT_LILY_PAD,
                                              INITIAL_LILIES[i].x,
                                              INITIAL_LILIES[i].z);
        if (!lily)
            break;
        lily->interactive = true;
        lily->y = 0.01f;
        lily->radius = INITIAL_LILIES[i].radius;
        lily->touch_radius = lily->radius * POND_OBJECT_TOUCH_MARGIN;
        lily->variant = i % LILY_NOTE_COUNT;
        lily->rotation = game_randf(as) * 2.f * SDL_PI_F;
        lily->red = 0.10f;
        lily->green = 0.55f;
        lily->blue = 0.10f;
    }

    for (int i = 0; i < POPPABLE_BUBBLE_TARGET; i++)
        spawn_poppable_bubble(as);

    for (int i = 0; i < NIGHT_FIREFLY_COUNT; i++) {
        PondObject *firefly = pond_object_spawn(&as->pond_objects,
                                                 POND_OBJECT_FIREFLY,
                                                 (game_randf(as) * 2.f - 1.f) * 11.f,
                                                 (game_randf(as) * 2.f - 1.f) * 11.f);
        if (!firefly)
            break;
        firefly->state = POND_STATE_DISABLED;
        firefly->radius = 0.14f;
        firefly->y = 0.45f + game_randf(as) * 0.35f;
        firefly->red = 1.f;
        firefly->green = 0.82f;
        firefly->blue = 0.2f;
        firefly->alpha = 0.f;
        firefly->phase = game_randf(as) * 2.f * SDL_PI_F;
    }

    as->idle_quack_timer = IDLE_QUACK_INTERVAL_MIN
        + game_randf(as) * (IDLE_QUACK_INTERVAL_MAX - IDLE_QUACK_INTERVAL_MIN);
    as->bubble_timer = BUBBLE_INTERVAL;
    as->duck_reflection_alpha = 0.18f;
    as->environment.day_mix = 1.f;
    as->environment.target_day_mix = 1.f;
    as->environment.cycle_timer = ENVIRONMENT_CYCLE_SECONDS;
    as->environment.night = false;
}

/* ── Effects ─────────────────────────────────────────────────────────────── */

static Particle *particle_allocate(AppState *as)
{
    int selected = -1;

    for (int offset = 0; offset < MAX_PARTICLES; offset++) {
        int index = (as->particle_head + offset) % MAX_PARTICLES;
        if (!as->particles[index].active) {
            selected = index;
            break;
        }
    }
    if (selected < 0) {
        for (int offset = 0; offset < MAX_PARTICLES; offset++) {
            int index = (as->particle_head + offset) % MAX_PARTICLES;
            if (as->particles[index].kind != PARTICLE_RAINBOW) {
                selected = index;
                break;
            }
        }
    }
    if (selected < 0)
        selected = as->particle_head;

    as->particle_head = (selected + 1) % MAX_PARTICLES;
    as->particles[selected] = (Particle){0};
    as->particles[selected].active = true;
    return &as->particles[selected];
}

static void init_water_particle(Particle *particle, float life, float size)
{
    particle->kind = PARTICLE_WATER_DROPLET;
    particle->life = life;
    particle->max_life = life;
    particle->size = size;
    particle->red = 0.56f;
    particle->green = 0.88f;
    particle->blue = 1.f;
    particle->alpha = 0.82f;
}

void spawn_ripple(AppState *as, float x, float z)
{
    spawn_ripple_scaled(as, x, z, 1.f);
}

void spawn_ripple_scaled(AppState *as, float x, float z, float intensity)
{
    intensity = SDL_clamp(intensity, 0.35f, 1.5f);
    Ripple *r = &as->ripples[as->ripple_head];
    as->ripple_head = (as->ripple_head + 1) % MAX_RIPPLES;
    r->x = x;  r->z = z;
    r->radius = 0.f;
    r->speed = RIPPLE_SPEED * (0.8f + intensity * 0.2f);
    r->max_radius = RIPPLE_MAX_R * (0.7f + intensity * 0.3f);
    r->max_alpha = SDL_min(1.f, 0.55f + intensity * 0.35f);
    r->alpha = r->max_alpha;
    r->active = true;
}

void spawn_splash(AppState *as, float x, float z)
{
    spawn_splash_scaled(as, x, z, 1.f);
}

void spawn_splash_scaled(AppState *as, float x, float z, float intensity)
{
    intensity = SDL_clamp(intensity, 0.35f, 1.5f);
    int droplet_count = 4 + (int)(intensity * 7.f);
    for (int i = 0; i < droplet_count; i++) {
        Particle *p = particle_allocate(as);
        float angle = game_randf(as) * 2.f * SDL_PI_F;
        float horizontal_speed = (1.f + game_randf(as) * 2.f) * intensity;
        p->x  = x;  p->y = 0.08f;  p->z = z;
        p->vx = cosf(angle) * horizontal_speed;
        p->vy = (2.5f + game_randf(as) * 2.5f) * intensity;
        p->vz = sinf(angle) * horizontal_speed;
        init_water_particle(p, 0.5f + intensity * 0.25f,
                            0.04f + intensity * 0.025f);
    }
}

void spawn_finger_trail_ripple(AppState *as, float x, float z)
{
    Ripple *ripple = &as->ripples[as->ripple_head];
    as->ripple_head = (as->ripple_head + 1) % MAX_RIPPLES;
    *ripple = (Ripple){
        .x = x,
        .z = z,
        .speed = 2.8f,
        .max_radius = 2.2f,
        .max_alpha = 0.18f,
        .alpha = 0.18f,
        .active = true
    };
}

void spawn_sleep_bubble(AppState *as)
{
    Particle *particle = particle_allocate(as);
    particle->kind = PARTICLE_SLEEP_BUBBLE;
    particle->x = as->duck_x + cosf(as->duck_angle) * 0.35f;
    particle->y = 0.7f;
    particle->z = as->duck_z + sinf(as->duck_angle) * 0.35f;
    particle->vx = 0.04f;
    particle->vy = 0.24f;
    particle->vz = -0.02f;
    particle->life = 2.4f;
    particle->max_life = particle->life;
    particle->size = 0.11f;
    particle->red = 0.76f;
    particle->green = 0.92f;
    particle->blue = 1.f;
    particle->alpha = 0.58f;
}

void spawn_sparkle_burst(AppState *as, float x, float y, float z, int count)
{
    count = SDL_clamp(count, 0, MAX_PARTICLES);
    for (int i = 0; i < count; i++) {
        Particle *particle = particle_allocate(as);
        float angle = (float)i / (float)SDL_max(count, 1) * 2.f * SDL_PI_F;
        float speed = 0.8f + game_randf(as) * 0.8f;
        particle->kind = PARTICLE_SPARKLE;
        particle->x = x;
        particle->y = y;
        particle->z = z;
        particle->vx = cosf(angle) * speed;
        particle->vy = 0.8f + game_randf(as) * 0.8f;
        particle->vz = sinf(angle) * speed;
        particle->life = 0.55f;
        particle->max_life = particle->life;
        particle->size = 0.07f;
        particle->red = 1.f;
        particle->green = 0.88f;
        particle->blue = 0.35f;
        particle->alpha = 0.9f;
    }
}

void spawn_rainbow_splash(AppState *as, float x, float z, float intensity)
{
    static const float colors[7][3] = {
        {1.f, 0.2f, 0.2f}, {1.f, 0.55f, 0.15f}, {1.f, 0.9f, 0.2f},
        {0.3f, 0.9f, 0.35f}, {0.25f, 0.55f, 1.f},
        {0.45f, 0.3f, 0.9f}, {0.75f, 0.3f, 0.85f}
    };
    intensity = SDL_clamp(intensity, 0.35f, 1.5f);
    float forward_x = cosf(as->duck_angle);
    float forward_z = sinf(as->duck_angle);
    float side_x = -forward_z;
    float side_z = forward_x;
    for (int i = 0; i < 7; i++) {
        Particle *particle = particle_allocate(as);
        float band = ((float)i - 3.f) * 0.16f;
        particle->kind = PARTICLE_RAINBOW;
        particle->x = x + side_x * band;
        particle->y = 0.12f;
        particle->z = z + side_z * band;
        particle->vx = forward_x * (1.6f + intensity * 0.8f) + side_x * band;
        particle->vy = 3.5f + intensity * 1.2f - fabsf(band) * 0.4f;
        particle->vz = forward_z * (1.6f + intensity * 0.8f) + side_z * band;
        particle->life = 1.2f;
        particle->max_life = particle->life;
        particle->size = 0.09f;
        particle->red = colors[i][0];
        particle->green = colors[i][1];
        particle->blue = colors[i][2];
        particle->alpha = 0.92f;
    }
    spawn_ripple_scaled(as, x, z, intensity);
    play_splash_scaled(as, intensity);
}

static void spawn_bubble(AppState *as)
{
    Particle *p = particle_allocate(as);
    p->kind = PARTICLE_BUBBLE;
    p->x    = as->duck_x + (game_randf(as) - 0.5f) * 0.3f;
    p->y    = -0.05f;
    p->z    = as->duck_z + (game_randf(as) - 0.5f) * 0.3f;
    p->vx   = (game_randf(as) - 0.5f) * 0.08f;
    p->vy   = 0.35f + game_randf(as) * 0.3f;
    p->vz   = (game_randf(as) - 0.5f) * 0.08f;
    p->life = 1.2f;
    p->max_life = p->life;
    p->size = 0.08f;
    p->red = 0.72f;
    p->green = 0.94f;
    p->blue = 1.f;
    p->alpha = 0.68f;
}

static void spawn_wake_droplets(AppState *as)
{
    for (int i = 0; i < 2; i++) {
        Particle *p = particle_allocate(as);
        float fx = as->duck_x - cosf(as->duck_angle) * 0.5f;
        float fz = as->duck_z - sinf(as->duck_angle) * 0.5f;
        p->x = fx + (game_randf(as) - 0.5f) * 0.3f;
        p->y = 0.1f;
        p->z = fz + (game_randf(as) - 0.5f) * 0.3f;
        float side = (i == 0) ? 1.f : -1.f;
        float nx = -sinf(as->duck_angle) * side;
        float nz = cosf(as->duck_angle) * side;
        float spd = sqrtf(as->duck_vx * as->duck_vx + as->duck_vz * as->duck_vz);
        p->vx = nx * spd * 0.35f + as->duck_vx * 0.1f;
        p->vy = 1.5f + game_randf(as);
        p->vz = nz * spd * 0.35f + as->duck_vz * 0.1f;
        init_water_particle(p, 0.5f, 0.075f);
    }
}

static bool quack_and_splash_scaled(AppState *as, float intensity)
{
    if (as->quack_cooldown > 0.f) return false;
    intensity = SDL_clamp(intensity, 0.35f, 1.5f);
    spawn_ripple_scaled(as, as->duck_x, as->duck_z, intensity);
    spawn_ripple_scaled(as, as->duck_x, as->duck_z, intensity);
    spawn_splash_scaled(as, as->duck_x, as->duck_z, intensity);
    int accent_count = 2 + (int)(intensity * 2.f);
    for (int i = 0; i < accent_count; i++) {
        Particle *p = particle_allocate(as);
        float a = (float)i / (float)accent_count * 2.f * SDL_PI_F;
        p->x = as->duck_x; p->y = 0.3f; p->z = as->duck_z;
        p->vx = cosf(a) * 3.2f * intensity;
        p->vy = (5.f + game_randf(as) * 2.f) * intensity;
        p->vz = sinf(a) * 3.2f * intensity;
        init_water_particle(p, 0.4f + intensity * 0.1f,
                            0.07f + intensity * 0.04f);
    }
    play_quack(as);
    as->quack_cooldown = QUACK_COOLDOWN_TIME;
    duck_animation_trigger(&as->duck_animation, DUCK_ANIMATION_QUACK, 0.24f);
    return true;
}

bool quack_and_splash(AppState *as)
{
    return quack_and_splash_scaled(as, 1.f);
}

void wake_duck(AppState *as, bool surprised)
{
    if (as->duck_animation.state != DUCK_ANIMATION_SLEEP)
        return;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (as->particles[i].kind == PARTICLE_SLEEP_BUBBLE)
            as->particles[i].active = false;
    }
    as->activity.inactivity_time = 0.f;
    duck_animation_trigger(&as->duck_animation, DUCK_ANIMATION_WAKE,
                           DUCK_WAKE_DURATION);
    if (surprised) {
        play_quack_variant(as, QUACK_VARIANT_SURPRISED);
        spawn_ripple(as, as->duck_x, as->duck_z);
        as->quack_cooldown = QUACK_COOLDOWN_TIME;
    }
}

bool spawn_tap_effect(AppState *as, float x, float z)
{
    return spawn_tap_effect_scaled(as, x, z, 1.f);
}

bool spawn_tap_effect_scaled(AppState *as, float x, float z, float intensity)
{
    spawn_ripple_scaled(as, x, z, intensity);
    spawn_ripple_scaled(as, x, z, intensity);
    spawn_splash_scaled(as, x, z, intensity);
    play_splash_scaled(as, intensity);
    return quack_and_splash_scaled(as, intensity);
}

/* ── Simulation updates ──────────────────────────────────────────────────── */

bool trigger_lily_note(AppState *as, PondObject *lily)
{
    if (!lily || !pond_object_is_visible(lily)
        || lily->kind != POND_OBJECT_LILY_PAD || lily->sound_cooldown > 0.f)
        return false;
    lily->sound_cooldown = LILY_NOTE_COOLDOWN;
    play_lily_note(as, lily->variant % LILY_NOTE_COUNT);
    return true;
}

void impulse_lily(AppState *as, PondObject *lily, float strength)
{
    if (!lily || lily->kind != POND_OBJECT_LILY_PAD)
        return;
    lily->lily_motion.vertical_velocity += 0.8f * strength;
    lily->lily_motion.tilt_velocity += (game_randf(as) * 1.6f - 0.8f) * strength;
}

void environment_set_night(AppState *as, bool night)
{
    as->environment.night = night;
    as->environment.target_day_mix = night ? 0.f : 1.f;
}

void environment_update(AppState *as, float dt)
{
    EnvironmentState *environment = &as->environment;
    environment->cycle_timer -= dt;
    if (environment->cycle_timer <= 0.f) {
        environment_set_night(as, !environment->night);
        environment->cycle_timer += ENVIRONMENT_CYCLE_SECONDS;
    }
    environment->day_mix += (environment->target_day_mix - environment->day_mix)
                          * SDL_min(dt * 1.8f, 1.f);
    environment->day_mix = SDL_clamp(environment->day_mix, 0.f, 1.f);
    as->duck_reflection_alpha = 0.07f + environment->day_mix * 0.11f;

    float firefly_alpha = SDL_clamp((0.35f - environment->day_mix) / 0.25f,
                                    0.f, 1.f);
    for (int i = 0; i < MAX_POND_OBJECTS; i++) {
        PondObject *object = &as->pond_objects.objects[i];
        if (!object->active)
            continue;
        if (object->kind == POND_OBJECT_POPPABLE_BUBBLE) {
            object->x += object->vx * dt;
            object->z += object->vz * dt;
            if (fabsf(object->x) > 12.f) {
                object->x = SDL_clamp(object->x, -12.f, 12.f);
                object->vx = -object->vx;
            }
            if (fabsf(object->z) > 12.f) {
                object->z = SDL_clamp(object->z, -12.f, 12.f);
                object->vz = -object->vz;
            }
        } else if (object->kind == POND_OBJECT_FIREFLY) {
            object->alpha = firefly_alpha;
            object->state = firefly_alpha > 0.f
                          ? POND_STATE_IDLE : POND_STATE_DISABLED;
            object->vx += sinf(object->phase * 1.7f) * 0.08f * dt;
            object->vz += cosf(object->phase * 1.3f) * 0.08f * dt;
            object->vx *= expf(-0.8f * dt);
            object->vz *= expf(-0.8f * dt);
            object->x = SDL_clamp(object->x + object->vx * dt, -12.f, 12.f);
            object->z = SDL_clamp(object->z + object->vz * dt, -12.f, 12.f);
        }
    }
}

static bool get_lily_exclusion_radius(const PondObject *object,
                                      float clearance, float *radius)
{
    if (!pond_object_is_visible(object) || object->kind != POND_OBJECT_LILY_PAD)
        return false;

    float lily_radius = object->radius * object->scale;
    if (lily_radius <= 0.f)
        return false;

    *radius = lily_radius + DUCK_COLLISION_RADIUS + clearance;
    return true;
}

static bool point_inside_duck_bounds(float x, float z)
{
    return fabsf(x) <= DUCK_WORLD_BOUND && fabsf(z) <= DUCK_WORLD_BOUND;
}

static bool get_lily_navigation_radius(const AppState *as,
                                       const PondObject *object, float *radius)
{
    if (!get_lily_exclusion_radius(object, DUCK_AVOIDANCE_CLEARANCE, radius))
        return false;

    float to_center_x = object->x - as->duck_x;
    float to_center_z = object->z - as->duck_z;
    float distance = sqrtf(to_center_x * to_center_x + to_center_z * to_center_z);
    if (distance > DUCK_AVOIDANCE_EPSILON) {
        float closing_speed = (as->duck_vx * to_center_x
                             + as->duck_vz * to_center_z) / distance;
        if (closing_speed > 0.f)
            *radius += closing_speed * closing_speed / (2.f * ACCELERATION);
    }
    return true;
}

static PondObject *find_lily_by_id(AppState *as, int id, float *radius)
{
    if (id <= 0)
        return NULL;

    for (int i = 0; i < MAX_POND_OBJECTS; i++) {
        PondObject *object = &as->pond_objects.objects[i];
        if (object->id == id
            && get_lily_exclusion_radius(object, DUCK_AVOIDANCE_CLEARANCE, radius))
            return object;
    }
    return NULL;
}

static PondObject *find_lily_containing_point(AppState *as, float x, float z,
                                              float *route_radius)
{
    PondObject *best = NULL;
    float best_distance_sq = 0.f;

    for (int i = 0; i < MAX_POND_OBJECTS; i++) {
        PondObject *object = &as->pond_objects.objects[i];
        float radius;
        if (!get_lily_exclusion_radius(object, DUCK_AVOIDANCE_CLEARANCE,
                                       &radius))
            continue;

        float visible_radius = object->radius * object->scale;
        float dx = x - object->x;
        float dz = z - object->z;
        float distance_sq = dx * dx + dz * dz;
        if (distance_sq <= visible_radius * visible_radius
            && (!best || distance_sq < best_distance_sq)) {
            best = object;
            best_distance_sq = distance_sq;
            *route_radius = radius;
        }
    }
    return best;
}

static bool segment_penetrates_circle(float start_x, float start_z,
                                      float end_x, float end_z,
                                      float center_x, float center_z,
                                      float radius, float *progress)
{
    float segment_x = end_x - start_x;
    float segment_z = end_z - start_z;
    float segment_length_sq = segment_x * segment_x + segment_z * segment_z;
    if (segment_length_sq <= DUCK_AVOIDANCE_EPSILON * DUCK_AVOIDANCE_EPSILON)
        return false;

    float radial_x = start_x - center_x;
    float radial_z = start_z - center_z;
    float radius_sq = radius * radius;
    float start_distance_sq = radial_x * radial_x + radial_z * radial_z;
    if (start_distance_sq <= radius_sq
        && segment_x * radial_x + segment_z * radial_z >= 0.f)
        return false;

    float t = ((center_x - start_x) * segment_x
             + (center_z - start_z) * segment_z) / segment_length_sq;
    t = SDL_clamp(t, 0.f, 1.f);
    float closest_x = start_x + segment_x * t - center_x;
    float closest_z = start_z + segment_z * t - center_z;
    float inner_radius = SDL_max(0.f, radius - DUCK_AVOIDANCE_EPSILON);
    if (closest_x * closest_x + closest_z * closest_z >= inner_radius * inner_radius)
        return false;

    if (progress)
        *progress = t;
    return true;
}

static PondObject *find_blocking_lily(AppState *as, float start_x, float start_z,
                                      float goal_x, float goal_z, float *radius)
{
    PondObject *best = NULL;
    float best_progress = 2.f;

    for (int i = 0; i < MAX_POND_OBJECTS; i++) {
        PondObject *object = &as->pond_objects.objects[i];
        float detection_radius;
        float progress;
        if (!get_lily_navigation_radius(as, object, &detection_radius)
            || !segment_penetrates_circle(start_x, start_z, goal_x, goal_z,
                                          object->x, object->z, detection_radius,
                                          &progress))
            continue;

        if (!best || progress < best_progress) {
            float route_radius;
            if (!get_lily_exclusion_radius(object, DUCK_AVOIDANCE_CLEARANCE,
                                           &route_radius))
                continue;
            best = object;
            best_progress = progress;
            *radius = route_radius;
        }
    }
    return best;
}

static void project_goal_outside_lilies(AppState *as, float *goal_x, float *goal_z)
{
    for (int pass = 0; pass < 4; pass++) {
        for (int i = 0; i < MAX_POND_OBJECTS; i++) {
            PondObject *object = &as->pond_objects.objects[i];
            float radius;
            if (!get_lily_exclusion_radius(object, DUCK_AVOIDANCE_CLEARANCE,
                                           &radius))
                continue;

            float dx = *goal_x - object->x;
            float dz = *goal_z - object->z;
            float distance = sqrtf(dx * dx + dz * dz);
            if (distance >= radius)
                continue;

            if (distance <= DUCK_AVOIDANCE_EPSILON) {
                /* Keep an exact-center target stable while the duck moves. */
                dx = -1.f;
                dz = 0.f;
                distance = 1.f;
            }

            float projected_radius = radius + DUCK_AVOIDANCE_EPSILON;
            float projected_x = object->x + dx / distance * projected_radius;
            float projected_z = object->z + dz / distance * projected_radius;
            if (!point_inside_duck_bounds(projected_x, projected_z)) {
                dx = -object->x;
                dz = -object->z;
                distance = sqrtf(dx * dx + dz * dz);
                if (distance <= DUCK_AVOIDANCE_EPSILON) {
                    dx = 1.f;
                    dz = 0.f;
                    distance = 1.f;
                }
                projected_x = object->x + dx / distance * projected_radius;
                projected_z = object->z + dz / distance * projected_radius;
            }
            *goal_x = SDL_clamp(projected_x, -DUCK_WORLD_BOUND, DUCK_WORLD_BOUND);
            *goal_z = SDL_clamp(projected_z, -DUCK_WORLD_BOUND, DUCK_WORLD_BOUND);
        }
    }
}

static float positive_angle(float angle)
{
    while (angle < 0.f)
        angle += 2.f * SDL_PI_F;
    while (angle >= 2.f * SDL_PI_F)
        angle -= 2.f * SDL_PI_F;
    return angle;
}

static float avoidance_route_length(const AppState *as, const PondObject *object,
                                    float radius, float goal_x, float goal_z,
                                    int turn_sign)
{
    float start_x = as->duck_x - object->x;
    float start_z = as->duck_z - object->z;
    float goal_dx = goal_x - object->x;
    float goal_dz = goal_z - object->z;
    float start_distance = sqrtf(start_x * start_x + start_z * start_z);
    float goal_distance = sqrtf(goal_dx * goal_dx + goal_dz * goal_dz);
    if (start_distance <= radius || goal_distance <= radius)
        return DUCK_AVOIDANCE_UNREACHABLE;

    float start_offset = acosf(SDL_clamp(radius / start_distance, -1.f, 1.f));
    float goal_offset = acosf(SDL_clamp(radius / goal_distance, -1.f, 1.f));
    float start_angle = atan2f(start_z, start_x) + (float)turn_sign * start_offset;
    float goal_angle = atan2f(goal_dz, goal_dx) - (float)turn_sign * goal_offset;
    float arc_angle = turn_sign > 0
        ? positive_angle(goal_angle - start_angle)
        : positive_angle(start_angle - goal_angle);
    int samples = SDL_max(1, (int)ceilf(arc_angle / DUCK_AVOIDANCE_STEP_ANGLE));
    for (int i = 0; i <= samples; i++) {
        float sample_angle = start_angle
            + (float)turn_sign * arc_angle * (float)i / (float)samples;
        float sample_x = object->x + cosf(sample_angle) * radius;
        float sample_z = object->z + sinf(sample_angle) * radius;
        if (!point_inside_duck_bounds(sample_x, sample_z))
            return DUCK_AVOIDANCE_UNREACHABLE;
    }

    return sqrtf(start_distance * start_distance - radius * radius)
         + arc_angle * radius
         + sqrtf(goal_distance * goal_distance - radius * radius);
}

static int choose_avoidance_turn(const AppState *as, const PondObject *object,
                                 float radius, float goal_x, float goal_z)
{
    float positive_length = avoidance_route_length(as, object, radius,
                                                   goal_x, goal_z, 1);
    float negative_length = avoidance_route_length(as, object, radius,
                                                   goal_x, goal_z, -1);
    if (positive_length >= DUCK_AVOIDANCE_UNREACHABLE
        && negative_length >= DUCK_AVOIDANCE_UNREACHABLE)
        return 0;
    if (negative_length + DUCK_AVOIDANCE_EPSILON < positive_length)
        return -1;
    return 1;
}

static int choose_orbit_turn(const AppState *as, const PondObject *object)
{
    float radial_x = as->duck_x - object->x;
    float radial_z = as->duck_z - object->z;
    float distance = sqrtf(radial_x * radial_x + radial_z * radial_z);
    if (distance <= DUCK_AVOIDANCE_EPSILON)
        return 1;

    float tangent_x = -radial_z / distance;
    float tangent_z = radial_x / distance;
    float tangential_speed = as->duck_vx * tangent_x + as->duck_vz * tangent_z;
    if (fabsf(tangential_speed) > 0.1f)
        return tangential_speed > 0.f ? 1 : -1;
    return 1;
}

static bool orbit_route_is_feasible(const AppState *as,
                                    const PondObject *orbit_lily,
                                    float orbit_radius)
{
    if (fabsf(orbit_lily->x) + orbit_radius > DUCK_WORLD_BOUND
        || fabsf(orbit_lily->z) + orbit_radius > DUCK_WORLD_BOUND)
        return false;

    for (int i = 0; i < MAX_POND_OBJECTS; i++) {
        const PondObject *object = &as->pond_objects.objects[i];
        float exclusion_radius;
        if (object == orbit_lily
            || !get_lily_exclusion_radius(object, 0.f, &exclusion_radius))
            continue;

        float dx = object->x - orbit_lily->x;
        float dz = object->z - orbit_lily->z;
        float center_distance = sqrtf(dx * dx + dz * dz);
        if (fabsf(center_distance - orbit_radius) < exclusion_radius)
            return false;
    }
    return true;
}

static bool orbit_approach_is_clear(AppState *as,
                                    const PondObject *orbit_lily,
                                    float steering_x, float steering_z)
{
    for (int i = 0; i < MAX_POND_OBJECTS; i++) {
        PondObject *object = &as->pond_objects.objects[i];
        float radius;
        if (object == orbit_lily
            || !get_lily_navigation_radius(as, object, &radius))
            continue;
        if (segment_penetrates_circle(as->duck_x, as->duck_z,
                                      steering_x, steering_z,
                                      object->x, object->z, radius, NULL))
            return false;
    }
    return true;
}

static void get_circle_steering_target(const AppState *as,
                                       const PondObject *obstacle,
                                       float obstacle_radius, int turn_sign,
                                       float *steering_x, float *steering_z)
{
    float radial_x = as->duck_x - obstacle->x;
    float radial_z = as->duck_z - obstacle->z;
    float distance = sqrtf(radial_x * radial_x + radial_z * radial_z);
    if (distance <= DUCK_AVOIDANCE_EPSILON) {
        radial_x = 1.f;
        radial_z = 0.f;
        distance = 1.f;
    }
    float normal_x = radial_x / distance;
    float normal_z = radial_z / distance;
    float perpendicular_x = -normal_z;
    float perpendicular_z = normal_x;
    float turn = (float)turn_sign;

    if (distance > obstacle_radius + DUCK_AVOIDANCE_JOIN_DISTANCE) {
        float ratio = SDL_clamp(obstacle_radius / distance, 0.f, 1.f);
        float tangent_scale = sqrtf(SDL_max(0.f, 1.f - ratio * ratio));
        *steering_x = obstacle->x + obstacle_radius
            * (ratio * normal_x + turn * tangent_scale * perpendicular_x);
        *steering_z = obstacle->z + obstacle_radius
            * (ratio * normal_z + turn * tangent_scale * perpendicular_z);
    } else {
        float cosine = cosf(DUCK_AVOIDANCE_STEP_ANGLE);
        float sine = sinf(DUCK_AVOIDANCE_STEP_ANGLE) * turn;
        *steering_x = obstacle->x + obstacle_radius
            * (cosine * normal_x + sine * perpendicular_x);
        *steering_z = obstacle->z + obstacle_radius
            * (cosine * normal_z + sine * perpendicular_z);
    }
}

static bool get_duck_steering_target(AppState *as, float requested_x,
                                     float requested_z, float *steering_x,
                                     float *steering_z, float *goal_distance)
{
    float orbit_radius;
    PondObject *orbit_lily = find_lily_containing_point(as, requested_x,
                                                        requested_z,
                                                        &orbit_radius);
    if (orbit_lily && !orbit_route_is_feasible(as, orbit_lily, orbit_radius))
        orbit_lily = NULL;
    if (orbit_lily) {
        int turn_sign = as->duck_avoidance.obstacle_id == orbit_lily->id
                      ? as->duck_avoidance.turn_sign : 0;
        if (turn_sign == 0)
            turn_sign = choose_orbit_turn(as, orbit_lily);
        get_circle_steering_target(as, orbit_lily, orbit_radius, turn_sign,
                                   steering_x, steering_z);
        if (!orbit_approach_is_clear(as, orbit_lily, *steering_x, *steering_z))
            orbit_lily = NULL;
    }
    if (orbit_lily) {
        if (as->duck_avoidance.obstacle_id != orbit_lily->id
            || as->duck_avoidance.turn_sign == 0) {
            as->duck_avoidance.obstacle_id = orbit_lily->id;
            as->duck_avoidance.turn_sign = choose_orbit_turn(as, orbit_lily);
        }
        get_circle_steering_target(as, orbit_lily, orbit_radius,
                                   as->duck_avoidance.turn_sign,
                                   steering_x, steering_z);
        *goal_distance = TOUCH_ARRIVAL_RADIUS;
        return true;
    }

    float goal_x = requested_x;
    float goal_z = requested_z;
    project_goal_outside_lilies(as, &goal_x, &goal_z);
    float goal_dx = goal_x - as->duck_x;
    float goal_dz = goal_z - as->duck_z;
    *goal_distance = sqrtf(goal_dx * goal_dx + goal_dz * goal_dz);
    *steering_x = goal_x;
    *steering_z = goal_z;

    float obstacle_radius = 0.f;
    PondObject *obstacle = find_blocking_lily(as, as->duck_x, as->duck_z,
                                              goal_x, goal_z, &obstacle_radius);
    if (!obstacle) {
        as->duck_avoidance = (DuckAvoidanceState){0};
        return false;
    }

    float active_radius;
    PondObject *active = find_lily_by_id(as, as->duck_avoidance.obstacle_id,
                                         &active_radius);
    if (active != obstacle || as->duck_avoidance.turn_sign == 0) {
        as->duck_avoidance.obstacle_id = obstacle->id;
        as->duck_avoidance.turn_sign = choose_avoidance_turn(
            as, obstacle, obstacle_radius, goal_x, goal_z);
        if (as->duck_avoidance.turn_sign == 0) {
            *steering_x = as->duck_x;
            *steering_z = as->duck_z;
            return true;
        }
    }

    get_circle_steering_target(as, obstacle, obstacle_radius,
                               as->duck_avoidance.turn_sign,
                               steering_x, steering_z);
    return true;
}

void update_duck_physics(AppState *as, const PlayerIntent *intent, float dt)
{
    float desired_vx = 0.f;
    float desired_vz = 0.f;
    bool has_movement_intent = false;

    if (intent->has_swim_target) {
        float steering_x;
        float steering_z;
        float goal_distance;
        bool avoiding = get_duck_steering_target(as, intent->swim_target_x,
                                                 intent->swim_target_z,
                                                 &steering_x, &steering_z,
                                                 &goal_distance);
        float dx = steering_x - as->duck_x;
        float dz = steering_z - as->duck_z;
        float distance = sqrtf(dx * dx + dz * dz);
        if (distance > 0.001f) {
            float desired_speed = MAX_SPEED
                * SDL_min(1.f, goal_distance / TOUCH_ARRIVAL_RADIUS);
            if (avoiding)
                desired_speed = SDL_min(desired_speed, DUCK_AVOIDANCE_SPEED);
            desired_vx = dx / distance * desired_speed;
            desired_vz = dz / distance * desired_speed;
        }
        has_movement_intent = true;
    } else {
        as->duck_avoidance = (DuckAvoidanceState){0};
        float input_length = sqrtf(intent->move_x * intent->move_x
                                 + intent->move_z * intent->move_z);
        if (input_length > 0.001f) {
            desired_vx = intent->move_x / input_length * MAX_SPEED;
            desired_vz = intent->move_z / input_length * MAX_SPEED;
            has_movement_intent = true;
        }
    }

    if (has_movement_intent) {
        float delta_vx = desired_vx - as->duck_vx;
        float delta_vz = desired_vz - as->duck_vz;
        float delta_length = sqrtf(delta_vx * delta_vx + delta_vz * delta_vz);
        float max_delta = ACCELERATION * dt;
        if (delta_length > max_delta && delta_length > 0.f) {
            delta_vx *= max_delta / delta_length;
            delta_vz *= max_delta / delta_length;
        }
        as->duck_vx += delta_vx;
        as->duck_vz += delta_vz;
    } else {
        float friction = expf(-FRICTION_K * dt);
        as->duck_vx *= friction;
        as->duck_vz *= friction;
    }

    if (as->duck_x < -DUCK_WORLD_BOUND + BOUNCE_MARGIN) as->duck_vx += BOUNCE_ACCEL * dt;
    if (as->duck_x >  DUCK_WORLD_BOUND - BOUNCE_MARGIN) as->duck_vx -= BOUNCE_ACCEL * dt;
    if (as->duck_z < -DUCK_WORLD_BOUND + BOUNCE_MARGIN) as->duck_vz += BOUNCE_ACCEL * dt;
    if (as->duck_z >  DUCK_WORLD_BOUND - BOUNCE_MARGIN) as->duck_vz -= BOUNCE_ACCEL * dt;

    float speed = sqrtf(as->duck_vx * as->duck_vx + as->duck_vz * as->duck_vz);
    if (speed > MAX_SPEED) {
        as->duck_vx *= MAX_SPEED / speed;
        as->duck_vz *= MAX_SPEED / speed;
    }

    as->duck_x += as->duck_vx * dt;
    as->duck_z += as->duck_vz * dt;

    if (as->duck_x < -DUCK_WORLD_BOUND) {
        if (as->duck_vx < -BOUNDARY_HIT_VEL_MIN) {
            spawn_ripple(as, -DUCK_WORLD_BOUND, as->duck_z);
            play_splash(as);
            as->camera_shake_t = CAMERA_SHAKE_DURATION;
            as->camera_shake_mag = CAMERA_SHAKE_MAGNITUDE;
        }
        as->duck_x = -DUCK_WORLD_BOUND;
        as->duck_vx = 0.f;
    }
    if (as->duck_x > DUCK_WORLD_BOUND) {
        if (as->duck_vx > BOUNDARY_HIT_VEL_MIN) {
            spawn_ripple(as, DUCK_WORLD_BOUND, as->duck_z);
            play_splash(as);
            as->camera_shake_t = CAMERA_SHAKE_DURATION;
            as->camera_shake_mag = CAMERA_SHAKE_MAGNITUDE;
        }
        as->duck_x = DUCK_WORLD_BOUND;
        as->duck_vx = 0.f;
    }
    if (as->duck_z < -DUCK_WORLD_BOUND) {
        if (as->duck_vz < -BOUNDARY_HIT_VEL_MIN) {
            spawn_ripple(as, as->duck_x, -DUCK_WORLD_BOUND);
            play_splash(as);
            as->camera_shake_t = CAMERA_SHAKE_DURATION;
            as->camera_shake_mag = CAMERA_SHAKE_MAGNITUDE;
        }
        as->duck_z = -DUCK_WORLD_BOUND;
        as->duck_vz = 0.f;
    }
    if (as->duck_z > DUCK_WORLD_BOUND) {
        if (as->duck_vz > BOUNDARY_HIT_VEL_MIN) {
            spawn_ripple(as, as->duck_x, DUCK_WORLD_BOUND);
            play_splash(as);
            as->camera_shake_t = CAMERA_SHAKE_DURATION;
            as->camera_shake_mag = CAMERA_SHAKE_MAGNITUDE;
        }
        as->duck_z = DUCK_WORLD_BOUND;
        as->duck_vz = 0.f;
    }
}

static void update_duck_motion_effects(AppState *as, float dt)
{
    float cur_spd = sqrtf(as->duck_vx * as->duck_vx + as->duck_vz * as->duck_vz);
    bool now_moving = cur_spd > DUCK_MOVING_THRESHOLD;

    if (cur_spd > DUCK_TURN_THRESHOLD)
        as->duck_angle = atan2f(as->duck_vz, as->duck_vx);

    if (now_moving && !as->duck_was_moving) {
        spawn_splash(as, as->duck_x, as->duck_z);
        spawn_ripple(as, as->duck_x, as->duck_z);
        play_splash(as);
        as->idle_quack_timer = IDLE_QUACK_INTERVAL_MIN + game_randf(as) * (IDLE_QUACK_INTERVAL_MAX - IDLE_QUACK_INTERVAL_MIN);
        as->movement_ripple_timer = MOVEMENT_RIPPLE_PERIOD;
    }

    if (!now_moving && as->duck_was_moving) {
        spawn_splash(as, as->duck_x, as->duck_z);
        spawn_ripple(as, as->duck_x, as->duck_z);
        play_splash(as);
    }

    if (now_moving) {
        as->movement_ripple_timer -= dt;
        if (as->movement_ripple_timer <= 0.f) {
            float bx = as->duck_x - cosf(as->duck_angle) * 0.4f;
            float bz = as->duck_z - sinf(as->duck_angle) * 0.4f;
            spawn_ripple(as, bx, bz);
            spawn_wake_droplets(as);
            as->movement_ripple_timer = MOVEMENT_RIPPLE_PERIOD;
        }
    }

    duck_animation_set_locomotion(&as->duck_animation, now_moving);
    as->duck_was_moving = now_moving;
}

void update_duck_idle(AppState *as, float dt)
{
    as->duck_bob_phase += dt;
    float spd = sqrtf(as->duck_vx * as->duck_vx + as->duck_vz * as->duck_vz);
    float fade = SDL_max(0.f, 1.f - spd / DUCK_BOB_SPEED_FADE);
    as->duck_y_offset = sinf(as->duck_bob_phase * 2.f * SDL_PI_F * DUCK_BOB_FREQ)
                        * DUCK_BOB_AMPLITUDE * fade;

    if (as->duck_animation.state == DUCK_ANIMATION_SLEEP)
        return;

    as->idle_quack_timer -= dt;
    if (as->idle_quack_timer <= 0.f && spd < IDLE_QUACK_SPEED_LIMIT) {
        play_quack(as);
        spawn_ripple(as, as->duck_x, as->duck_z);
        duck_animation_trigger(&as->duck_animation, DUCK_ANIMATION_QUACK, 0.24f);
        as->idle_quack_timer = IDLE_QUACK_INTERVAL_MIN + game_randf(as) * (IDLE_QUACK_INTERVAL_MAX - IDLE_QUACK_INTERVAL_MIN);
    }

    if (spd < BUBBLE_SPEED_LIMIT) {
        as->bubble_timer -= dt;
        if (as->bubble_timer <= 0.f) {
            spawn_bubble(as);
            as->bubble_timer = BUBBLE_INTERVAL;
        }
    }
}

void update_lilypad_collisions(AppState *as)
{
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < MAX_POND_OBJECTS; i++) {
            PondObject *lily = &as->pond_objects.objects[i];
            float min_dist;
            if (!get_lily_exclusion_radius(lily, 0.f, &min_dist))
                continue;

            float dx = as->duck_x - lily->x;
            float dz = as->duck_z - lily->z;
            float dist = sqrtf(dx * dx + dz * dz);
            if (dist < min_dist) {
                float nx;
                float nz;
                if (dist > 0.001f) {
                    nx = dx / dist;
                    nz = dz / dist;
                } else {
                    float speed = sqrtf(as->duck_vx * as->duck_vx
                                      + as->duck_vz * as->duck_vz);
                    if (speed > 0.001f) {
                        nx = -as->duck_vx / speed;
                        nz = -as->duck_vz / speed;
                    } else {
                        nx = 1.f;
                        nz = 0.f;
                    }
                }

                float projected_x = lily->x + nx * min_dist;
                float projected_z = lily->z + nz * min_dist;
                if (!point_inside_duck_bounds(projected_x, projected_z)) {
                    nx = -lily->x;
                    nz = -lily->z;
                    float inward_length = sqrtf(nx * nx + nz * nz);
                    if (inward_length > 0.001f) {
                        nx /= inward_length;
                        nz /= inward_length;
                    } else {
                        nx = 1.f;
                        nz = 0.f;
                    }
                    projected_x = lily->x + nx * min_dist;
                    projected_z = lily->z + nz * min_dist;
                }
                as->duck_x = SDL_clamp(projected_x, -DUCK_WORLD_BOUND,
                                       DUCK_WORLD_BOUND);
                as->duck_z = SDL_clamp(projected_z, -DUCK_WORLD_BOUND,
                                       DUCK_WORLD_BOUND);
                float dot = as->duck_vx * nx + as->duck_vz * nz;
                if (dot < 0.f) {
                    as->duck_vx -= (1.f + LILY_BOUNCE_COEFF) * dot * nx;
                    as->duck_vz -= (1.f + LILY_BOUNCE_COEFF) * dot * nz;
                    trigger_lily_note(as, lily);
                    if (lily->collision_cooldown <= 0.f) {
                        spawn_ripple(as, as->duck_x, as->duck_z);
                        spawn_splash(as, as->duck_x, as->duck_z);
                        impulse_lily(as, lily, 1.f);
                        lily->collision_cooldown = LILY_FEEDBACK_COOLDOWN;
                    }
                }
            }
        }
    }
}

void update_ripples(AppState *as, float dt)
{
    for (int i = 0; i < MAX_RIPPLES; i++) {
        Ripple *r = &as->ripples[i];
        if (!r->active) continue;
        if (r->speed <= 0.f || r->max_radius <= 0.f) {
            r->active = false;
            r->alpha = 0.f;
            continue;
        }
        r->radius += r->speed * dt;
        r->alpha = r->max_alpha * (1.f - r->radius / r->max_radius);
        if (r->radius >= r->max_radius) r->active = false;
    }
}

void update_particles(AppState *as, float dt)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &as->particles[i];
        if (!p->active) continue;
        switch (p->kind) {
        case PARTICLE_WATER_DROPLET:
            p->vy -= 15.f * dt;
            break;
        case PARTICLE_BUBBLE:
        case PARTICLE_SLEEP_BUBBLE:
            p->vx *= expf(-1.2f * dt);
            p->vz *= expf(-1.2f * dt);
            break;
        case PARTICLE_RAIN:
            p->vy -= 18.f * dt;
            break;
        case PARTICLE_FIREFLY:
            p->vx += sinf(p->life * 4.f) * 0.02f * dt;
            p->vz += cosf(p->life * 3.f) * 0.02f * dt;
            break;
        case PARTICLE_RAINBOW:
            p->vy -= 7.f * dt;
            break;
        case PARTICLE_SPARKLE:
            p->vy -= 3.f * dt;
            break;
        case PARTICLE_FOAM:
            break;
        }
        p->x    += p->vx * dt;
        p->y    += p->vy * dt;
        p->z    += p->vz * dt;
        p->life -= dt;
        if (p->life <= 0.f
            || ((p->kind == PARTICLE_WATER_DROPLET || p->kind == PARTICLE_RAIN)
                && p->y < -0.5f))
            p->active = false;
    }
}

void game_step(AppState *as, const PlayerIntent *intent, float dt)
{
    PlayerIntent effective_intent = *intent;
    activities_process_events(as);
    activities_compose_intent(as, intent, &effective_intent);

    if (as->quack_cooldown > 0.f)
        as->quack_cooldown = SDL_max(0.f, as->quack_cooldown - dt);
    if (as->camera_shake_t > 0.f)
        as->camera_shake_t = SDL_max(0.f, as->camera_shake_t - dt);

    environment_update(as, dt);
    update_ripples(as, dt);
    update_particles(as, dt);
    update_duck_physics(as, &effective_intent, dt);
    update_lilypad_collisions(as);
    update_duck_motion_effects(as, dt);
    update_duck_idle(as, dt);
    pond_objects_update(&as->pond_objects, dt);
    duck_animation_update(&as->duck_animation, dt);
    activities_update(as, intent, dt);
    as->elapsed += dt;
    as->simulation_tick++;
}

int game_accumulate_steps(double *accumulator, double frame_dt)
{
    int steps = 0;

    frame_dt = SDL_clamp(frame_dt, 0.0, MAX_FRAME_DELTA);
    *accumulator += frame_dt;
    while (*accumulator >= FIXED_TIMESTEP && steps < MAX_SIMULATION_STEPS) {
        *accumulator -= FIXED_TIMESTEP;
        steps++;
    }
    if (steps == MAX_SIMULATION_STEPS && *accumulator >= FIXED_TIMESTEP)
        *accumulator = fmod(*accumulator, FIXED_TIMESTEP);
    return steps;
}
