#pragma once

#include <stdbool.h>

#define MAX_POND_OBJECTS 64
#define POND_OBJECT_TOUCH_MARGIN 1.35f

typedef enum {
    POND_OBJECT_LILY_PAD,
    POND_OBJECT_POPPABLE_BUBBLE,
    POND_OBJECT_LEAF,
    POND_OBJECT_FLOWER,
    POND_OBJECT_FROG,
    POND_OBJECT_FISH_SHADOW,
    POND_OBJECT_BUTTERFLY,
    POND_OBJECT_TREASURE,
    POND_OBJECT_FIREFLY,
    POND_OBJECT_BOAT,
    POND_OBJECT_SHAPE
} PondObjectKind;

typedef enum {
    POND_STATE_IDLE,
    POND_STATE_ACTIVATING,
    POND_STATE_ACTIVE,
    POND_STATE_HIDING,
    POND_STATE_DISABLED
} PondObjectState;

typedef struct {
    float height_offset;
    float vertical_velocity;
    float tilt;
    float tilt_velocity;
} LilyMotion;

typedef struct {
    int id;
    bool active;
    bool interactive;
    PondObjectKind kind;
    PondObjectState state;
    float x, y, z;
    float vx, vy, vz;
    float radius;
    float touch_radius;
    float scale;
    float rotation;
    float phase;
    float state_timer;
    float sound_cooldown;
    float collision_cooldown;
    float red, green, blue, alpha;
    LilyMotion lily_motion;
    int variant;
    int parent_id;
} PondObject;

typedef struct {
    PondObject objects[MAX_POND_OBJECTS];
    int next_id;
} PondObjectPool;

void pond_object_pool_reset(PondObjectPool *pool);
bool pond_object_is_visible(const PondObject *object);
PondObject *pond_object_spawn(PondObjectPool *pool, PondObjectKind kind,
                              float x, float z);
PondObject *pond_object_find_hit(PondObjectPool *pool, float x, float z);
PondObject *pond_object_find_by_id(PondObjectPool *pool, int id);
int pond_object_count_kind(const PondObjectPool *pool, PondObjectKind kind);
void pond_objects_update(PondObjectPool *pool, float dt);
