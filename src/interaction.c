#include "interaction.h"

#include <string.h>

void interaction_queue_reset(InteractionQueue *queue)
{
    if (queue)
        memset(queue, 0, sizeof(*queue));
}

bool interaction_queue_push(InteractionQueue *queue, const InteractionEvent *event)
{
    if (!queue || !event || queue->count >= MAX_INTERACTION_EVENTS)
        return false;

    queue->events[queue->write_index] = *event;
    queue->write_index = (queue->write_index + 1) % MAX_INTERACTION_EVENTS;
    queue->count++;
    return true;
}

bool interaction_queue_peek(const InteractionQueue *queue, InteractionEvent *out_event)
{
    if (!queue || !out_event || queue->count == 0)
        return false;
    *out_event = queue->events[queue->read_index];
    return true;
}

bool interaction_queue_pop(InteractionQueue *queue, InteractionEvent *out_event)
{
    if (!queue || !out_event || queue->count == 0)
        return false;

    *out_event = queue->events[queue->read_index];
    queue->read_index = (queue->read_index + 1) % MAX_INTERACTION_EVENTS;
    queue->count--;
    return true;
}

bool interaction_queue_discard_oldest_type(InteractionQueue *queue,
                                           InteractionType type)
{
    InteractionEvent retained[MAX_INTERACTION_EVENTS];
    int retained_count = 0;
    bool discarded = false;

    if (!queue)
        return false;

    InteractionEvent event;
    while (interaction_queue_pop(queue, &event)) {
        if (!discarded && event.type == type) {
            discarded = true;
            continue;
        }
        retained[retained_count++] = event;
    }
    for (int i = 0; i < retained_count; i++)
        interaction_queue_push(queue, &retained[i]);
    return discarded;
}

uint64_t interaction_target_tick(uint64_t simulation_tick,
                                 double accumulator,
                                 uint64_t reference_ns,
                                 uint64_t timestamp_ns,
                                 double fixed_timestep,
                                 int max_steps_ahead)
{
    double pending_time = accumulator;
    if (max_steps_ahead < 0)
        max_steps_ahead = 0;
    if (timestamp_ns >= reference_ns)
        pending_time += (double)(timestamp_ns - reference_ns) / 1000000000.0;

    uint64_t steps_ahead = fixed_timestep > 0.0
                         ? (uint64_t)(pending_time / fixed_timestep) : 0;
    if (steps_ahead > (uint64_t)max_steps_ahead)
        steps_ahead = (uint64_t)max_steps_ahead;
    return simulation_tick + steps_ahead;
}
