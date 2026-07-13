#include "activities.h"

#include "audio.h"
#include "game.h"
#include "interaction.h"
#include "pond_objects.h"
#include "types.h"

#include <limits.h>
#include <math.h>
#include <string.h>

static void increment_saturating(unsigned int *value)
{
    if (*value < UINT_MAX)
        (*value)++;
}

static bool duck_hit_test(const AppState *app, float x, float z)
{
    float dx = x - app->duck_x;
    float dz = z - app->duck_z;
    return dx * dx + dz * dz <= DUCK_TOUCH_RADIUS * DUCK_TOUCH_RADIUS;
}

static void cancel_zoomies(AppState *app)
{
    if (!app->activity.zoomies.active)
        return;
    app->activity.zoomies.active = false;
    app->activity.zoomies.timer = 0.f;
    if (app->duck_animation.state == DUCK_ANIMATION_ZOOMIES)
        duck_animation_trigger(&app->duck_animation, DUCK_ANIMATION_IDLE, 0.f);
}

static void check_celebrations(AppState *app)
{
    ActivityState *activity = &app->activity;
    if (activity->celebration_cooldown > 0.f)
        return;

    if (activity->bubble_pops_since_rainbow >= BUBBLE_POPS_PER_RAINBOW) {
        activity->bubble_pops_since_rainbow -= BUBBLE_POPS_PER_RAINBOW;
        spawn_rainbow_splash(app, activity->celebration_x,
                             activity->celebration_z, 1.5f);
        activity->celebration_cooldown = CELEBRATION_COOLDOWN;
    } else if (activity->splashes_since_rainbow >= SPLASHES_PER_RAINBOW) {
        activity->splashes_since_rainbow -= SPLASHES_PER_RAINBOW;
        spawn_rainbow_splash(app, activity->celebration_x,
                             activity->celebration_z, 1.2f);
        activity->celebration_cooldown = CELEBRATION_COOLDOWN;
    }
}

static void note_normal_splash(AppState *app, float x, float z)
{
    increment_saturating(&app->activity.splashes_since_rainbow);
    app->activity.celebration_x = x;
    app->activity.celebration_z = z;
    check_celebrations(app);
}

static void activate_bubble(AppState *app, PondObject *bubble)
{
    if (!bubble || !bubble->active
        || bubble->kind != POND_OBJECT_POPPABLE_BUBBLE)
        return;
    float x = bubble->x;
    float y = bubble->y;
    float z = bubble->z;
    bubble->interactive = false;
    bubble->active = false;
    spawn_sparkle_burst(app, x, y, z, 5);
    spawn_ripple_scaled(app, x, z, 0.5f);
    play_bubble_pop(app);
    increment_saturating(&app->activity.bubbles_popped);
    increment_saturating(&app->activity.bubble_pops_since_rainbow);
    app->activity.celebration_x = x;
    app->activity.celebration_z = z;
    app->activity.bubble_spawn_timer = POPPABLE_BUBBLE_RESPAWN;
    check_celebrations(app);
}

static void start_zoomies(AppState *app)
{
    ZoomiesState *zoomies = &app->activity.zoomies;
    float toward_x = -app->duck_x;
    float toward_z = -app->duck_z;
    float length = sqrtf(toward_x * toward_x + toward_z * toward_z);
    if (length < 0.001f) {
        toward_x = 1.f;
        toward_z = 0.f;
        length = 1.f;
    }
    zoomies->center_x = app->duck_x + toward_x / length * ZOOMIES_RADIUS;
    zoomies->center_z = app->duck_z + toward_z / length * ZOOMIES_RADIUS;
    float center_bound = DUCK_WORLD_BOUND - ZOOMIES_RADIUS;
    zoomies->center_x = SDL_clamp(zoomies->center_x, -center_bound, center_bound);
    zoomies->center_z = SDL_clamp(zoomies->center_z, -center_bound, center_bound);
    zoomies->start_angle = atan2f(app->duck_z - zoomies->center_z,
                                 app->duck_x - zoomies->center_x);
    zoomies->direction = 1;
    zoomies->active = true;
    zoomies->timer = 0.f;
    zoomies->cooldown = ZOOMIES_COOLDOWN;
    zoomies->tap_count = 0;
    zoomies->tap_window = 0.f;
    duck_animation_trigger(&app->duck_animation, DUCK_ANIMATION_ZOOMIES,
                           ZOOMIES_DURATION);
}

static void record_duck_tap(AppState *app)
{
    ZoomiesState *zoomies = &app->activity.zoomies;
    if (zoomies->tap_count == 0) {
        zoomies->tap_count = 1;
        zoomies->tap_window = ZOOMIES_TAP_WINDOW;
    } else if (zoomies->tap_window > 0.f) {
        zoomies->tap_count++;
    }
    if (zoomies->tap_count >= ZOOMIES_REQUIRED_TAPS
        && zoomies->cooldown <= 0.f)
        start_zoomies(app);
}

static bool handle_duck_tap(AppState *app, const InteractionEvent *event)
{
    if (app->duck_animation.state == DUCK_ANIMATION_SLEEP) {
        wake_duck(app, true);
        increment_saturating(&app->activity.duck_quacks);
        return false;
    }
    bool quacked = spawn_tap_effect_scaled(app, event->x, event->z,
                                           event->intensity);
    record_duck_tap(app);
    return quacked;
}

static bool handle_object_tap(AppState *app, PondObject *object,
                              const InteractionEvent *event)
{
    if (!object || !pond_object_is_visible(object))
        return false;
    app->activity.last_interacted_object_id = object->id;
    if (object->kind == POND_OBJECT_POPPABLE_BUBBLE) {
        activate_bubble(app, object);
        return false;
    }
    if (object->kind == POND_OBJECT_LILY_PAD) {
        spawn_ripple_scaled(app, object->x, object->z, event->intensity);
        spawn_splash_scaled(app, object->x, object->z, event->intensity);
        play_splash_scaled(app, event->intensity * 0.55f);
        trigger_lily_note(app, object);
        impulse_lily(app, object, 1.f);
        note_normal_splash(app, object->x, object->z);
        return false;
    }
    note_normal_splash(app, event->x, event->z);
    return spawn_tap_effect_scaled(app, event->x, event->z, event->intensity);
}

void activities_init(ActivityState *activity)
{
    if (!activity)
        return;
    memset(activity, 0, sizeof(*activity));
    activity->mode = ACTIVITY_MODE_FREE_PLAY;
    activity->current_prompt = -1;
    activity->last_interacted_object_id = -1;
    activity->bubble_spawn_timer = POPPABLE_BUBBLE_RESPAWN;
}

void activities_process_events(AppState *app)
{
    InteractionEvent event;

    while (interaction_queue_peek(&app->interaction_queue, &event)
           && event.target_tick <= app->simulation_tick) {
        interaction_queue_pop(&app->interaction_queue, &event);
        bool quacked = false;
        app->activity.inactivity_time = 0.f;
        app->activity.interaction_this_step = true;

        switch (event.type) {
        case INTERACTION_POND_PRESS:
            cancel_zoomies(app);
            spawn_ripple(app, event.x, event.z);
            if (duck_hit_test(app, event.x, event.z)
                && app->duck_animation.state == DUCK_ANIMATION_SLEEP) {
                wake_duck(app, true);
                increment_saturating(&app->activity.duck_quacks);
            }
            break;

        case INTERACTION_TAP_WATER:
            {
                PondObject *object = pond_object_find_hit(&app->pond_objects,
                                                           event.x, event.z);
                increment_saturating(&app->activity.total_taps);
                if (object) {
                    quacked = handle_object_tap(app, object, &event);
                } else if (duck_hit_test(app, event.x, event.z)) {
                    app->activity.last_interacted_object_id = -1;
                    quacked = handle_duck_tap(app, &event);
                    note_normal_splash(app, event.x, event.z);
                } else {
                    app->activity.last_interacted_object_id = -1;
                    quacked = spawn_tap_effect_scaled(app, event.x, event.z,
                                                      event.intensity);
                    note_normal_splash(app, event.x, event.z);
                }
            }
            break;

        case INTERACTION_TAP_DUCK:
            increment_saturating(&app->activity.total_taps);
            quacked = handle_duck_tap(app, &event);
            note_normal_splash(app, event.x, event.z);
            break;

        case INTERACTION_TAP_OBJECT:
            increment_saturating(&app->activity.total_taps);
            quacked = handle_object_tap(app,
                pond_object_find_by_id(&app->pond_objects, event.object_id),
                &event);
            break;

        case INTERACTION_QUACK_REQUEST:
            cancel_zoomies(app);
            if (app->duck_animation.state == DUCK_ANIMATION_SLEEP) {
                wake_duck(app, true);
                increment_saturating(&app->activity.duck_quacks);
            } else {
                quacked = quack_and_splash(app);
            }
            break;

        case INTERACTION_HOLD_RELEASE:
            {
                PondObject *object = pond_object_find_hit(&app->pond_objects,
                                                           event.x, event.z);
                spawn_ripple_scaled(app, event.x, event.z, event.intensity);
                spawn_splash_scaled(app, event.x, event.z, event.intensity);
                play_splash_scaled(app, event.intensity);
                if (object && object->kind == POND_OBJECT_POPPABLE_BUBBLE)
                    activate_bubble(app, object);
                else if (object && object->kind == POND_OBJECT_LILY_PAD) {
                    trigger_lily_note(app, object);
                    impulse_lily(app, object, 0.8f);
                }
                note_normal_splash(app, event.x, event.z);
            }
            break;

        case INTERACTION_SWIPE:
        case INTERACTION_CIRCLE:
            spawn_ripple(app, event.x, event.z);
            break;
        }

        if (quacked)
            increment_saturating(&app->activity.duck_quacks);
    }
}

void activities_compose_intent(AppState *app, const PlayerIntent *user_intent,
                               PlayerIntent *effective_intent)
{
    bool direct_input = user_intent->has_swim_target
                     || fabsf(user_intent->move_x) > 0.001f
                     || fabsf(user_intent->move_z) > 0.001f;
    float speed_sq = app->duck_vx * app->duck_vx + app->duck_vz * app->duck_vz;
    if (app->duck_animation.state == DUCK_ANIMATION_SLEEP
        && (direct_input || speed_sq > 0.01f))
        wake_duck(app, false);

    if (!app->activity.zoomies.active)
        return;
    if (direct_input) {
        cancel_zoomies(app);
        return;
    }

    ZoomiesState *zoomies = &app->activity.zoomies;
    float angle = zoomies->start_angle
                + (float)zoomies->direction
                * (zoomies->timer * ZOOMIES_ANGULAR_SPEED + 0.45f);
    effective_intent->has_swim_target = true;
    effective_intent->swim_target_x = zoomies->center_x + cosf(angle) * ZOOMIES_RADIUS;
    effective_intent->swim_target_z = zoomies->center_z + sinf(angle) * ZOOMIES_RADIUS;
    effective_intent->move_x = 0.f;
    effective_intent->move_z = 0.f;
}

static void update_finger_trail(AppState *app)
{
    FingerTrailState *trail = &app->activity.finger_trail;
    int emitted = 0;
    while (app->input.trail_count > 0
           && emitted < MAX_TRAIL_RIPPLES_PER_STEP) {
        TrailSample *sample = &app->input.trail_samples[app->input.trail_read];
        if (!trail->valid || sample->reset
            || trail->owner_sequence != sample->owner_sequence) {
            trail->valid = true;
            trail->owner_sequence = sample->owner_sequence;
            trail->last_x = sample->x;
            trail->last_z = sample->z;
            trail->distance_accumulator = 0.f;
            app->input.trail_read = (app->input.trail_read + 1)
                                  % MAX_TRAIL_SAMPLES;
            app->input.trail_count--;
            continue;
        }

        float dx = sample->x - trail->last_x;
        float dz = sample->z - trail->last_z;
        float distance = sqrtf(dx * dx + dz * dz);
        if (distance <= 0.0001f) {
            app->input.trail_read = (app->input.trail_read + 1)
                                  % MAX_TRAIL_SAMPLES;
            app->input.trail_count--;
            continue;
        }
        float needed = FINGER_TRAIL_SPACING - trail->distance_accumulator;
        if (distance + trail->distance_accumulator < FINGER_TRAIL_SPACING) {
            trail->distance_accumulator += distance;
            trail->last_x = sample->x;
            trail->last_z = sample->z;
            app->input.trail_read = (app->input.trail_read + 1)
                                  % MAX_TRAIL_SAMPLES;
            app->input.trail_count--;
            continue;
        }

        float ratio = needed / distance;
        trail->last_x += dx * ratio;
        trail->last_z += dz * ratio;
        trail->distance_accumulator = 0.f;
        spawn_finger_trail_ripple(app, trail->last_x, trail->last_z);
        emitted++;
    }
}

void activities_update(AppState *app, const PlayerIntent *user_intent, float dt)
{
    ActivityState *activity = &app->activity;
    float speed = sqrtf(app->duck_vx * app->duck_vx + app->duck_vz * app->duck_vz);
    bool has_input = user_intent->has_swim_target
                  || fabsf(user_intent->move_x) > 0.001f
                  || fabsf(user_intent->move_z) > 0.001f;

    update_finger_trail(app);

    if (activity->zoomies.cooldown > 0.f)
        activity->zoomies.cooldown = SDL_max(0.f, activity->zoomies.cooldown - dt);
    if (activity->zoomies.tap_window > 0.f) {
        activity->zoomies.tap_window -= dt;
        if (activity->zoomies.tap_window <= 0.f) {
            activity->zoomies.tap_window = 0.f;
            activity->zoomies.tap_count = 0;
        }
    }
    if (activity->zoomies.active) {
        activity->zoomies.timer += dt;
        if (activity->zoomies.timer >= ZOOMIES_DURATION)
            cancel_zoomies(app);
    }

    if (activity->interaction_this_step || has_input || speed > 0.05f
        || activity->zoomies.active) {
        activity->inactivity_time = 0.f;
    } else if (app->duck_animation.state != DUCK_ANIMATION_SLEEP) {
        activity->inactivity_time += dt;
        if (activity->inactivity_time >= DUCK_SLEEP_AFTER_SECONDS) {
            app->duck_vx = 0.f;
            app->duck_vz = 0.f;
            activity->sleep_bubble_timer = 0.f;
            duck_animation_trigger(&app->duck_animation,
                                   DUCK_ANIMATION_SLEEP, 0.f);
        }
    }

    if (app->duck_animation.state == DUCK_ANIMATION_SLEEP) {
        activity->sleep_bubble_timer -= dt;
        if (activity->sleep_bubble_timer <= 0.f) {
            spawn_sleep_bubble(app);
            activity->sleep_bubble_timer = SLEEP_BUBBLE_PERIOD;
        }
    }

    if (pond_object_count_kind(&app->pond_objects, POND_OBJECT_POPPABLE_BUBBLE)
        < POPPABLE_BUBBLE_TARGET) {
        activity->bubble_spawn_timer -= dt;
        if (activity->bubble_spawn_timer <= 0.f) {
            if (spawn_poppable_bubble(app))
                activity->bubble_spawn_timer = POPPABLE_BUBBLE_RESPAWN;
            else
                activity->bubble_spawn_timer = 0.25f;
        }
    } else {
        activity->bubble_spawn_timer = POPPABLE_BUBBLE_RESPAWN;
    }

    if (activity->celebration_cooldown > 0.f)
        activity->celebration_cooldown = SDL_max(0.f,
                                                activity->celebration_cooldown - dt);
    check_celebrations(app);
    activity->interaction_this_step = false;
}
