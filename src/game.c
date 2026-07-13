#include "game.h"
#include "audio.h"
#include <math.h>

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
        float spd = 2.f + 2.f * SDL_randf();
        p->x  = x;  p->y = 0.25f;  p->z = z;
        p->vx = cosf(a) * spd * 0.4f;
        p->vy = 3.5f + 2.f * SDL_randf();
        p->vz = sinf(a) * spd * 0.4f;
        p->life = 1.f;  p->active = true;
    }
}

static void spawn_bubble(AppState *as)
{
    Particle *p = &as->particles[as->particle_head];
    as->particle_head = (as->particle_head + 1) % MAX_PARTICLES;
    p->x    = as->duck_x + (SDL_randf() - 0.5f) * 0.3f;
    p->y    = -0.05f;
    p->z    = as->duck_z + (SDL_randf() - 0.5f) * 0.3f;
    p->vx   = (SDL_randf() - 0.5f) * 0.08f;
    p->vy   = 0.35f + SDL_randf() * 0.3f;
    p->vz   = (SDL_randf() - 0.5f) * 0.08f;
    p->life = 1.f;
    p->active = true;
}

static void spawn_wake_droplets(AppState *as)
{
    for (int i = 0; i < 2; i++) {
        Particle *p = &as->particles[as->particle_head];
        as->particle_head = (as->particle_head + 1) % MAX_PARTICLES;
        float fx = as->duck_x + cosf(as->duck_angle) * 0.5f;
        float fz = as->duck_z + sinf(as->duck_angle) * 0.5f;
        p->x = fx + (SDL_randf() - 0.5f) * 0.3f;
        p->y = 0.1f;
        p->z = fz + (SDL_randf() - 0.5f) * 0.3f;
        float side = (i == 0) ? 1.f : -1.f;
        float nx = -sinf(as->duck_angle) * side;
        float nz = cosf(as->duck_angle) * side;
        float spd = sqrtf(as->duck_vx * as->duck_vx + as->duck_vz * as->duck_vz);
        p->vx = nx * spd * 0.35f + as->duck_vx * 0.1f;
        p->vy = 1.5f + SDL_randf();
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
        p->vy = 5.f + SDL_randf() * 2.f;
        p->vz = sinf(a) * 3.2f;
        p->life = 1.f;
        p->active = true;
    }
    play_quack(as);
    as->quack_cooldown = QUACK_COOLDOWN_TIME;
}

/* ── Simulation updates ──────────────────────────────────────────────────── */

void update_duck_physics(AppState *as, float dt)
{
    const bool *keys = SDL_GetKeyboardState(NULL);
    float ix = 0.f, iz = 0.f;
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])    iz -= 1.f;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])  iz += 1.f;
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])  ix += 1.f;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) ix -= 1.f;

    if (as->gamepad) {
        float gx = SDL_GetGamepadAxis(as->gamepad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.f;
        float gz = SDL_GetGamepadAxis(as->gamepad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.f;
        if (fabsf(gx) > DEADZONE) ix += gx;
        if (fabsf(gz) > DEADZONE) iz -= gz;
    }

    float ilen = sqrtf(ix * ix + iz * iz);
    if (ilen > 1.f) { ix /= ilen; iz /= ilen; }

    as->duck_vx += ix * ACCELERATION * dt;
    as->duck_vz += iz * ACCELERATION * dt;

    float spd = sqrtf(as->duck_vx * as->duck_vx + as->duck_vz * as->duck_vz);
    if (spd > MAX_SPEED) {
        as->duck_vx *= MAX_SPEED / spd;
        as->duck_vz *= MAX_SPEED / spd;
    }

    float fric = expf(-FRICTION_K * dt);
    as->duck_vx *= fric;
    as->duck_vz *= fric;

    if (as->duck_x < -WORLD_BOUND + BOUNCE_MARGIN) as->duck_vx += BOUNCE_ACCEL * dt;
    if (as->duck_x >  WORLD_BOUND - BOUNCE_MARGIN) as->duck_vx -= BOUNCE_ACCEL * dt;
    if (as->duck_z < -WORLD_BOUND + BOUNCE_MARGIN) as->duck_vz += BOUNCE_ACCEL * dt;
    if (as->duck_z >  WORLD_BOUND - BOUNCE_MARGIN) as->duck_vz -= BOUNCE_ACCEL * dt;

    as->duck_x += as->duck_vx * dt;
    as->duck_z -= as->duck_vz * dt;

    if (as->duck_x < -WORLD_BOUND) {
        if (as->duck_vx < -BOUNDARY_HIT_VEL_MIN) {
            spawn_ripple(as, as->duck_x, as->duck_z);
            play_splash(as);
            as->camera_shake_t = CAMERA_SHAKE_DURATION;
            as->camera_shake_mag = CAMERA_SHAKE_MAGNITUDE;
        }
        as->duck_x = -WORLD_BOUND;
        as->duck_vx = 0.f;
    }
    if (as->duck_x > WORLD_BOUND) {
        if (as->duck_vx > BOUNDARY_HIT_VEL_MIN) {
            spawn_ripple(as, as->duck_x, as->duck_z);
            play_splash(as);
            as->camera_shake_t = CAMERA_SHAKE_DURATION;
            as->camera_shake_mag = CAMERA_SHAKE_MAGNITUDE;
        }
        as->duck_x = WORLD_BOUND;
        as->duck_vx = 0.f;
    }
    if (as->duck_z < -WORLD_BOUND) {
        if (as->duck_vz < -BOUNDARY_HIT_VEL_MIN) {
            spawn_ripple(as, as->duck_x, as->duck_z);
            play_splash(as);
            as->camera_shake_t = CAMERA_SHAKE_DURATION;
            as->camera_shake_mag = CAMERA_SHAKE_MAGNITUDE;
        }
        as->duck_z = -WORLD_BOUND;
        as->duck_vz = 0.f;
    }
    if (as->duck_z > WORLD_BOUND) {
        if (as->duck_vz > BOUNDARY_HIT_VEL_MIN) {
            spawn_ripple(as, as->duck_x, as->duck_z);
            play_splash(as);
            as->camera_shake_t = CAMERA_SHAKE_DURATION;
            as->camera_shake_mag = CAMERA_SHAKE_MAGNITUDE;
        }
        as->duck_z = WORLD_BOUND;
        as->duck_vz = 0.f;
    }

    float cur_spd = sqrtf(as->duck_vx * as->duck_vx + as->duck_vz * as->duck_vz);
    bool now_moving = cur_spd > DUCK_MOVING_THRESHOLD;

    if (now_moving && !as->duck_was_moving) {
        spawn_splash(as, as->duck_x, as->duck_z);
        spawn_ripple(as, as->duck_x, as->duck_z);
        play_splash(as);
        as->idle_quack_timer = IDLE_QUACK_INTERVAL_MIN + SDL_randf() * (IDLE_QUACK_INTERVAL_MAX - IDLE_QUACK_INTERVAL_MIN);
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

    if (cur_spd > DUCK_TURN_THRESHOLD)
        as->duck_angle = atan2f(as->duck_vz, as->duck_vx);

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
        as->idle_quack_timer = IDLE_QUACK_INTERVAL_MIN + SDL_randf() * (IDLE_QUACK_INTERVAL_MAX - IDLE_QUACK_INTERVAL_MIN);
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
        if (dist < min_dist && dist > 0.001f) {
            float nx = dx / dist;
            float nz = dz / dist;
            as->duck_x += nx * (min_dist - dist);
            as->duck_z += nz * (min_dist - dist);
            float dot = as->duck_vx * nx + as->duck_vz * nz;
            if (dot < 0.f) {
                as->duck_vx -= (1.f + LILY_BOUNCE_COEFF) * dot * nx;
                as->duck_vz -= (1.f + LILY_BOUNCE_COEFF) * dot * nz;
                spawn_ripple(as, as->duck_x, as->duck_z);
                spawn_splash(as, as->duck_x, as->duck_z);
                play_splash(as);
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
