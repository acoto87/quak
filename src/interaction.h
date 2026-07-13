#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MAX_INTERACTION_EVENTS 32

typedef enum {
    INTERACTION_POND_PRESS,
    INTERACTION_TAP_WATER,
    INTERACTION_QUACK_REQUEST,
    INTERACTION_TAP_DUCK,
    INTERACTION_HOLD_RELEASE,
    INTERACTION_SWIPE,
    INTERACTION_CIRCLE,
    INTERACTION_TAP_OBJECT
} InteractionType;

typedef struct {
    InteractionType type;
    float x, z;
    float dx, dz;
    float duration_seconds;
    float intensity;
    int object_id;
    uint64_t target_tick;
} InteractionEvent;

typedef struct {
    InteractionEvent events[MAX_INTERACTION_EVENTS];
    int read_index;
    int write_index;
    int count;
} InteractionQueue;

void interaction_queue_reset(InteractionQueue *queue);
bool interaction_queue_push(InteractionQueue *queue, const InteractionEvent *event);
bool interaction_queue_peek(const InteractionQueue *queue, InteractionEvent *out_event);
bool interaction_queue_pop(InteractionQueue *queue, InteractionEvent *out_event);
bool interaction_queue_discard_oldest_type(InteractionQueue *queue,
                                           InteractionType type);
uint64_t interaction_target_tick(uint64_t simulation_tick,
                                 double accumulator,
                                 uint64_t reference_ns,
                                 uint64_t timestamp_ns,
                                 double fixed_timestep,
                                 int max_steps_ahead);
