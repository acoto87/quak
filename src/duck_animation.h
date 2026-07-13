#pragma once

#include <stdbool.h>

typedef enum {
    DUCK_ANIMATION_IDLE,
    DUCK_ANIMATION_SWIM,
    DUCK_ANIMATION_QUACK,
    DUCK_ANIMATION_SLEEP,
    DUCK_ANIMATION_WAKE,
    DUCK_ANIMATION_SHAKE,
    DUCK_ANIMATION_SPIN,
    DUCK_ANIMATION_DIVE,
    DUCK_ANIMATION_ZOOMIES
} DuckAnimationState;

typedef struct {
    DuckAnimationState state;
    float timer;
    float duration;
    float body_pitch;
    float body_roll;
    float body_y_offset;
    float body_scale_x;
    float body_scale_y;
    float body_scale_z;
    float spin_angle;
} DuckAnimation;

void duck_animation_init(DuckAnimation *animation);
void duck_animation_trigger(DuckAnimation *animation,
                            DuckAnimationState state, float duration);
void duck_animation_set_locomotion(DuckAnimation *animation, bool swimming);
void duck_animation_update(DuckAnimation *animation, float dt);
