#include "interaction.h"

#include <stdio.h>

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static InteractionEvent numbered_event(int number)
{
    return (InteractionEvent){
        .type = INTERACTION_TAP_WATER,
        .x = (float)number,
        .object_id = -1
    };
}

static void test_interaction_queue_fifo(void)
{
    InteractionQueue queue;
    InteractionEvent event;
    interaction_queue_reset(&queue);

    for (int i = 0; i < MAX_INTERACTION_EVENTS; i++) {
        InteractionEvent input = numbered_event(i);
        CHECK(interaction_queue_push(&queue, &input));
    }
    CHECK(interaction_queue_peek(&queue, &event));
    CHECK(event.x == 0.f);
    CHECK(queue.count == MAX_INTERACTION_EVENTS);
    for (int i = 0; i < MAX_INTERACTION_EVENTS / 2; i++) {
        CHECK(interaction_queue_pop(&queue, &event));
        CHECK(event.x == (float)i);
    }
    for (int i = 0; i < MAX_INTERACTION_EVENTS / 2; i++) {
        InteractionEvent input = numbered_event(MAX_INTERACTION_EVENTS + i);
        CHECK(interaction_queue_push(&queue, &input));
    }
    for (int i = MAX_INTERACTION_EVENTS / 2;
         i < MAX_INTERACTION_EVENTS + MAX_INTERACTION_EVENTS / 2; i++) {
        CHECK(interaction_queue_pop(&queue, &event));
        CHECK(event.x == (float)i);
    }
    CHECK(!interaction_queue_pop(&queue, &event));
}

static void test_interaction_queue_full(void)
{
    InteractionQueue queue;
    InteractionEvent event;
    interaction_queue_reset(&queue);

    for (int i = 0; i < MAX_INTERACTION_EVENTS; i++) {
        InteractionEvent input = numbered_event(i);
        CHECK(interaction_queue_push(&queue, &input));
    }
    event = numbered_event(999);
    CHECK(!interaction_queue_push(&queue, &event));
    CHECK(queue.count == MAX_INTERACTION_EVENTS);

    for (int i = 0; i < MAX_INTERACTION_EVENTS; i++) {
        CHECK(interaction_queue_pop(&queue, &event));
        CHECK(event.x == (float)i);
    }

    interaction_queue_reset(&queue);
    CHECK(queue.count == 0);
    CHECK(queue.read_index == 0);
    CHECK(queue.write_index == 0);
}

static void test_interaction_queue_priority_recovery(void)
{
    InteractionQueue queue;
    InteractionEvent event;
    interaction_queue_reset(&queue);

    for (int i = 0; i < MAX_INTERACTION_EVENTS; i++) {
        InteractionEvent input = numbered_event(i);
        input.type = i == 4 ? INTERACTION_POND_PRESS : INTERACTION_TAP_WATER;
        CHECK(interaction_queue_push(&queue, &input));
    }
    CHECK(interaction_queue_discard_oldest_type(&queue, INTERACTION_POND_PRESS));
    CHECK(queue.count == MAX_INTERACTION_EVENTS - 1);
    event = numbered_event(1000);
    event.type = INTERACTION_QUACK_REQUEST;
    CHECK(interaction_queue_push(&queue, &event));
    CHECK(queue.count == MAX_INTERACTION_EVENTS);

    bool found_quack = false;
    while (interaction_queue_pop(&queue, &event)) {
        CHECK(event.x != 4.f);
        if (event.type == INTERACTION_QUACK_REQUEST)
            found_quack = true;
    }
    CHECK(found_quack);
}

static void test_interaction_target_tick(void)
{
    const uint64_t reference = 1000000000ULL;
    CHECK(interaction_target_tick(30, 0.008, reference,
                                  reference + 10000000ULL,
                                  1.0 / 60.0, 5) == 31);
    CHECK(interaction_target_tick(30, 0.0, reference,
                                  reference + 5000000ULL,
                                  1.0 / 60.0, 5) == 30);
    CHECK(interaction_target_tick(30, 0.0, reference,
                                  reference + 1000000000ULL,
                                  1.0 / 60.0, 5) == 35);
    CHECK(interaction_target_tick(30, 0.0, reference,
                                  reference - 1,
                                  1.0 / 60.0, 5) == 30);
}

int main(void)
{
    test_interaction_queue_fifo();
    test_interaction_queue_full();
    test_interaction_queue_priority_recovery();
    test_interaction_target_tick();
    if (failures == 0)
        printf("All interaction queue tests passed.\n");
    return failures == 0 ? 0 : 1;
}
