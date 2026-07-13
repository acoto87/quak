#include "duck_animation.h"

#include <math.h>
#include <string.h>

static void reset_outputs(DuckAnimation *animation)
{
    animation->body_pitch = 0.f;
    animation->body_roll = 0.f;
    animation->body_y_offset = 0.f;
    animation->body_scale_x = 1.f;
    animation->body_scale_y = 1.f;
    animation->body_scale_z = 1.f;
    animation->spin_angle = 0.f;
}

void duck_animation_init(DuckAnimation *animation)
{
    if (!animation)
        return;
    memset(animation, 0, sizeof(*animation));
    animation->state = DUCK_ANIMATION_IDLE;
    reset_outputs(animation);
}

void duck_animation_trigger(DuckAnimation *animation,
                            DuckAnimationState state, float duration)
{
    if (!animation)
        return;
    animation->state = state;
    animation->timer = 0.f;
    animation->duration = duration > 0.f ? duration : 0.f;
}

void duck_animation_set_locomotion(DuckAnimation *animation, bool swimming)
{
    if (!animation)
        return;
    if (animation->state == DUCK_ANIMATION_IDLE
        || animation->state == DUCK_ANIMATION_SWIM) {
        animation->state = swimming ? DUCK_ANIMATION_SWIM : DUCK_ANIMATION_IDLE;
        animation->timer = 0.f;
        animation->duration = 0.f;
    }
}

void duck_animation_update(DuckAnimation *animation, float dt)
{
    if (!animation)
        return;

    reset_outputs(animation);
    if (animation->state == DUCK_ANIMATION_SLEEP) {
        animation->timer += dt;
        float breath = sinf(animation->timer * 1.8f);
        animation->body_scale_y = 0.97f + breath * 0.015f;
        animation->body_scale_x = 1.015f - breath * 0.008f;
        animation->body_y_offset = -0.02f;
        animation->body_roll = sinf(animation->timer * 0.9f) * 0.015f;
        return;
    }
    if (animation->state == DUCK_ANIMATION_IDLE
        || animation->state == DUCK_ANIMATION_SWIM)
        return;

    animation->timer += dt;
    float t = animation->duration > 0.f
            ? animation->timer / animation->duration : 1.f;
    if (t > 1.f) t = 1.f;

    switch (animation->state) {
    case DUCK_ANIMATION_QUACK:
        animation->body_y_offset = sinf(t * 3.14159265f) * 0.04f;
        animation->body_scale_x = 1.f + sinf(t * 3.14159265f) * 0.05f;
        animation->body_scale_y = 1.f - sinf(t * 3.14159265f) * 0.04f;
        break;
    case DUCK_ANIMATION_WAKE:
        animation->body_scale_y = 0.85f + 0.15f * sinf(t * 3.14159265f);
        break;
    case DUCK_ANIMATION_SHAKE:
        animation->body_roll = sinf(animation->timer * 35.f) * 0.18f * (1.f - t);
        break;
    case DUCK_ANIMATION_SPIN:
        animation->spin_angle = t * 6.2831853f;
        break;
    case DUCK_ANIMATION_DIVE:
        animation->body_pitch = t < 0.5f ? t * 1.2f : (1.f - t) * 1.2f;
        animation->body_y_offset = -sinf(t * 3.14159265f) * 0.35f;
        break;
    case DUCK_ANIMATION_ZOOMIES:
        animation->body_roll = sinf(animation->timer * 9.f) * 0.12f;
        animation->body_y_offset = fabsf(sinf(animation->timer * 7.f)) * 0.05f;
        break;
    default:
        break;
    }

    if (animation->timer >= animation->duration) {
        animation->state = DUCK_ANIMATION_IDLE;
        animation->timer = 0.f;
        animation->duration = 0.f;
        reset_outputs(animation);
    }
}
