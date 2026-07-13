#include "pond_objects.h"

#include <float.h>
#include <string.h>

void pond_object_pool_reset(PondObjectPool *pool)
{
    if (!pool)
        return;
    memset(pool, 0, sizeof(*pool));
    pool->next_id = 1;
}

bool pond_object_is_visible(const PondObject *object)
{
    return object && object->active && object->alpha > 0.f
        && object->state != POND_STATE_DISABLED;
}

PondObject *pond_object_spawn(PondObjectPool *pool, PondObjectKind kind,
                              float x, float z)
{
    if (!pool)
        return NULL;

    for (int i = 0; i < MAX_POND_OBJECTS; i++) {
        PondObject *object = &pool->objects[i];
        if (object->active)
            continue;

        *object = (PondObject){0};
        object->id = pool->next_id++;
        object->active = true;
        object->kind = kind;
        object->state = POND_STATE_IDLE;
        object->x = x;
        object->z = z;
        object->scale = 1.f;
        object->alpha = 1.f;
        object->parent_id = -1;
        return object;
    }
    return NULL;
}

PondObject *pond_object_find_hit(PondObjectPool *pool, float x, float z)
{
    PondObject *best = NULL;
    float best_distance_sq = FLT_MAX;

    if (!pool)
        return NULL;

    for (int i = 0; i < MAX_POND_OBJECTS; i++) {
        PondObject *object = &pool->objects[i];
        if (!pond_object_is_visible(object) || !object->interactive)
            continue;

        float dx = x - object->x;
        float dz = z - object->z;
        float distance_sq = dx * dx + dz * dz;
        float hit_radius = object->touch_radius > 0.f
                         ? object->touch_radius
                         : object->radius * object->scale * POND_OBJECT_TOUCH_MARGIN;
        if (hit_radius > 0.f && distance_sq <= hit_radius * hit_radius
            && distance_sq < best_distance_sq) {
            best = object;
            best_distance_sq = distance_sq;
        }
    }
    return best;
}

PondObject *pond_object_find_by_id(PondObjectPool *pool, int id)
{
    if (!pool || id <= 0)
        return NULL;
    for (int i = 0; i < MAX_POND_OBJECTS; i++) {
        if (pool->objects[i].active && pool->objects[i].id == id)
            return &pool->objects[i];
    }
    return NULL;
}

int pond_object_count_kind(const PondObjectPool *pool, PondObjectKind kind)
{
    int count = 0;
    if (!pool)
        return 0;
    for (int i = 0; i < MAX_POND_OBJECTS; i++) {
        if (pool->objects[i].active && pool->objects[i].kind == kind)
            count++;
    }
    return count;
}

void pond_objects_update(PondObjectPool *pool, float dt)
{
    if (!pool)
        return;

    for (int i = 0; i < MAX_POND_OBJECTS; i++) {
        PondObject *object = &pool->objects[i];
        if (!object->active)
            continue;
        object->phase += dt;
        if (object->sound_cooldown > 0.f) {
            object->sound_cooldown -= dt;
            if (object->sound_cooldown < 0.f)
                object->sound_cooldown = 0.f;
        }
        if (object->collision_cooldown > 0.f) {
            object->collision_cooldown -= dt;
            if (object->collision_cooldown < 0.f)
                object->collision_cooldown = 0.f;
        }
        if (object->state_timer > 0.f)
            object->state_timer = object->state_timer > dt
                                ? object->state_timer - dt : 0.f;

        if (object->kind == POND_OBJECT_LILY_PAD) {
            LilyMotion *motion = &object->lily_motion;
            motion->vertical_velocity += (-18.f * motion->height_offset
                                        - 6.f * motion->vertical_velocity) * dt;
            motion->height_offset += motion->vertical_velocity * dt;
            motion->tilt_velocity += (-14.f * motion->tilt
                                    - 5.f * motion->tilt_velocity) * dt;
            motion->tilt += motion->tilt_velocity * dt;
        }
    }
}
