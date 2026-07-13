#include "game.h"
#include "audio.h"
#include <math.h>

static float game_randf(AppState *as)
{
    return SDL_randf_r(&as->simulation_rng);
}

/* ── Effects ─────────────────────────────────────────────────────────────── */

void spawn_ripple(AppState *as, float x, float z)
{
    Ripple *r = &as->ripples[as->ripple_head];
    as->ripple_head = (as->ripple_head + 1) % MAX_RIPPLES;
    r->x = x;  r->z = z;
    r->radius = 0.f;  r->alpha = 1.f;  r->active = true;
}

void spawn_splash(AppState *as, float x, float z)
{
    for (int i = 0; i < 8; i++) {
        Particle *p = &as->particles[as->particle_head];
        as->particle_head = (as->particle_head + 1) % MAX_PARTICLES;
        float a   = (float)i / 8.f * 2.f * 3.14159265f;
        float spd = 2.f + 2.f * game_randf(as);
        p->x  = x;  p->y = 0.25f;  p->z = z;
        p->vx = cosf(a) * spd * 0.4f;
        p->vy = 3.5f + 2.f * game_randf(as);
        p->vz = sinf(a) * spd * 0.4f;
        p->life = 1.f;  p->active = true;
    }
}

static void spawn_bubble(AppState *as)
{
    Particle *p = &as->particles[as->particle_head];
    as->particle_head = (as->particle_head + 1) % MAX_PARTICLES;
    p->x    = as->duck_x + (game_randf(as) - 0.5f) * 0.3f;
    p->y    = -0.05f;
    p->z    = as->duck_z + (game_randf(as) - 0.5f) * 0.3f;
    p->vx   = (game_randf(as) - 0.5f) * 0.08f;
    p->vy   = 0.35f + game_randf(as) * 0.3f;
    p->vz   = (game_randf(as) - 0.5f) * 0.08f;
    p->life = 1.f;
    p->active = true;
}

static void spawn_wake_droplets(AppState *as)
{
    for (int i = 0; i < 2; i++) {
        Particle *p = &as->particles[as->particle_head];
        as->particle_head = (as->particle_head + 1) % MAX_PARTICLES;
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
        p->life = 1.f;
        p->active = true;
    }
}

void quack_and_splash(AppState *as)
{
    if (as->quack_cooldown > 0.f) return;
    spawn_ripple(as, as->duck_x, as->duck_z);
    spawn_ripple(as, as->duck_x, as->duck_z);
    spawn_splash(as, as->duck_x, as->duck_z);
    for (int i = 0; i < 4; i++) {
        Particle *p = &as->particles[as->particle_head];
        as->particle_head = (as->particle_head + 1) % MAX_PARTICLES;
        float a = (float)i / 4.f * 2.f * 3.14159265f;
        p->x = as->duck_x; p->y = 0.3f; p->z = as->duck_z;
        p->vx = cosf(a) * 3.2f;
        p->vy = 5.f + game_randf(as) * 2.f;
        p->vz = sinf(a) * 3.2f;
        p->life = 1.f;
        p->active = true;
    }
    play_quack(as);
    as->quack_cooldown = QUACK_COOLDOWN_TIME;
}

void spawn_tap_effect(AppState *as, float x, float z)
{
    spawn_ripple(as, x, z);
    spawn_ripple(as, x, z);
    spawn_splash(as, x, z);
    quack_and_splash(as);
}

/* ── Simulation updates ──────────────────────────────────────────────────── */

void update_duck_physics(AppState *as, const PlayerIntent *intent, float dt)
{
    float desired_vx = 0.f;
    float desired_vz = 0.f;
    bool has_movement_intent = false;

    if (intent->has_swim_target) {
        float dx = intent->swim_target_x - as->duck_x;
        float dz = intent->swim_target_z - as->duck_z;
        float distance = sqrtf(dx * dx + dz * dz);
        if (distance > 0.001f) {
            float desired_speed = MAX_SPEED * SDL_min(1.f, distance / TOUCH_ARRIVAL_RADIUS);
            desired_vx = dx / distance * desired_speed;
            desired_vz = dz / distance * desired_speed;
        }
        has_movement_intent = true;
    } else {
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

    as->duck_was_moving = now_moving;
}

void update_duck_idle(AppState *as, float dt)
{
    as->duck_bob_phase += dt;
    float spd = sqrtf(as->duck_vx * as->duck_vx + as->duck_vz * as->duck_vz);
    float fade = SDL_max(0.f, 1.f - spd / DUCK_BOB_SPEED_FADE);
    as->duck_y_offset = sinf(as->duck_bob_phase * 2.f * SDL_PI_F * DUCK_BOB_FREQ)
                        * DUCK_BOB_AMPLITUDE * fade;

    as->idle_quack_timer -= dt;
    if (as->idle_quack_timer <= 0.f && spd < IDLE_QUACK_SPEED_LIMIT) {
        play_quack(as);
        spawn_ripple(as, as->duck_x, as->duck_z);
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
    for (int i = 0; i < LILY_PAD_COUNT; i++) {
        float dx = as->duck_x - LILY_PADS[i].x;
        float dz = as->duck_z - LILY_PADS[i].z;
        float dist = sqrtf(dx * dx + dz * dz);
        float min_dist = LILY_PADS[i].r + DUCK_COLLISION_RADIUS;
        if (dist < min_dist) {
            float nx;
            float nz;
            if (dist > 0.001f) {
                nx = dx / dist;
                nz = dz / dist;
            } else {
                float speed = sqrtf(as->duck_vx * as->duck_vx + as->duck_vz * as->duck_vz);
                nx = speed > 0.001f ? -as->duck_vx / speed : 1.f;
                nz = speed > 0.001f ? -as->duck_vz / speed : 0.f;
            }
            as->duck_x = LILY_PADS[i].x + nx * min_dist;
            as->duck_z = LILY_PADS[i].z + nz * min_dist;
            float dot = as->duck_vx * nx + as->duck_vz * nz;
            if (dot < 0.f) {
                as->duck_vx -= (1.f + LILY_BOUNCE_COEFF) * dot * nx;
                as->duck_vz -= (1.f + LILY_BOUNCE_COEFF) * dot * nz;
                if (as->lily_hit_cooldown <= 0.f) {
                    spawn_ripple(as, as->duck_x, as->duck_z);
                    spawn_splash(as, as->duck_x, as->duck_z);
                    play_splash(as);
                    as->lily_hit_cooldown = LILY_HIT_COOLDOWN;
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
        r->radius += RIPPLE_SPEED * dt;
        r->alpha   = 1.f - r->radius / RIPPLE_MAX_R;
        if (r->radius >= RIPPLE_MAX_R) r->active = false;
    }
}

void update_particles(AppState *as, float dt)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &as->particles[i];
        if (!p->active) continue;
        p->vy   -= 15.f * dt;
        p->x    += p->vx * dt;
        p->y    += p->vy * dt;
        p->z    += p->vz * dt;
        p->life -= dt * 2.f;
        if (p->life <= 0.f || p->y < -0.5f) p->active = false;
    }
}

void game_step(AppState *as, const PlayerIntent *intent, float dt)
{
    if (as->quack_cooldown > 0.f)
        as->quack_cooldown = SDL_max(0.f, as->quack_cooldown - dt);
    if (as->lily_hit_cooldown > 0.f)
        as->lily_hit_cooldown = SDL_max(0.f, as->lily_hit_cooldown - dt);
    if (as->camera_shake_t > 0.f)
        as->camera_shake_t = SDL_max(0.f, as->camera_shake_t - dt);

    update_ripples(as, dt);
    update_particles(as, dt);
    update_duck_physics(as, intent, dt);
    update_lilypad_collisions(as);
    update_duck_motion_effects(as, dt);
    update_duck_idle(as, dt);
    as->elapsed += dt;
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
