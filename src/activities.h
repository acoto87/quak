#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ACTIVITY_MODE_FREE_PLAY,
    ACTIVITY_MODE_COLOR_PLAY,
    ACTIVITY_MODE_SOUND_MATCH,
    ACTIVITY_MODE_SIZE_PLAY,
    ACTIVITY_MODE_RHYTHM_PLAY
} ActivityMode;

typedef struct {
    bool valid;
    uint64_t owner_sequence;
    float last_x;
    float last_z;
    float distance_accumulator;
} FingerTrailState;

typedef struct {
    int tap_count;
    float tap_window;
    bool active;
    float timer;
    float cooldown;
    float center_x;
    float center_z;
    float start_angle;
    int direction;
} ZoomiesState;

typedef struct {
    ActivityMode mode;
    unsigned int total_taps;
    unsigned int duck_quacks;
    unsigned int bubbles_popped;
    unsigned int flowers_opened;
    unsigned int frogs_greeted;
    float celebration_cooldown;
    float inactivity_time;
    float sleep_bubble_timer;
    float bubble_spawn_timer;
    unsigned int splashes_since_rainbow;
    unsigned int bubble_pops_since_rainbow;
    float celebration_x;
    float celebration_z;
    FingerTrailState finger_trail;
    ZoomiesState zoomies;
    int current_prompt;
    bool educational_audio_enabled;
    bool interaction_this_step;
    int last_interacted_object_id;
} ActivityState;

struct AppState;
struct PlayerIntent;

void activities_init(ActivityState *activity);
void activities_process_events(struct AppState *app);
void activities_compose_intent(struct AppState *app,
                               const struct PlayerIntent *user_intent,
                               struct PlayerIntent *effective_intent);
void activities_update(struct AppState *app,
                       const struct PlayerIntent *user_intent, float dt);
