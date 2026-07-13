#include "audio.h"

#include <stdio.h>

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static void test_weighted_variants(void)
{
    Uint64 rng = 12345;
    int counts[QUACK_VARIANT_COUNT] = {0};
    for (int i = 0; i < 10000; i++)
        counts[audio_select_quack_variant(&rng)]++;
    for (int i = 0; i < QUACK_VARIANT_COUNT; i++)
        CHECK(counts[i] > 0);
    CHECK(counts[QUACK_VARIANT_NORMAL] > 5500
          && counts[QUACK_VARIANT_NORMAL] < 6500);
    CHECK(counts[QUACK_VARIANT_TINY] > 1200
          && counts[QUACK_VARIANT_TINY] < 1800);
    CHECK(counts[QUACK_VARIANT_SURPRISED] > 300
          && counts[QUACK_VARIANT_SURPRISED] < 700);
}

static void test_bounded_audio(void)
{
    AppState as = {0};
    as.audio_rng = 7;
    CHECK(audio_init(&as));
    if (!as.audio_dev)
        return;
    CHECK(SDL_PauseAudioDevice(as.audio_dev));

    int largest_quack = 0;
    for (int i = 0; i < QUACK_VARIANT_COUNT; i++) {
        CHECK(as.quack_clips[i].pcm != NULL);
        CHECK(as.quack_clips[i].pcm_bytes > 0);
        largest_quack = SDL_max(largest_quack, as.quack_clips[i].pcm_bytes);
    }
    CHECK(as.splash_clip.pcm != NULL);
    CHECK(as.bubble_pop_clip.pcm != NULL);
    for (int i = 0; i < LILY_NOTE_COUNT; i++)
        CHECK(as.lily_note_clips[i].pcm != NULL);

    for (int i = 0; i < 1000; i++) {
        play_quack(&as);
        play_splash_scaled(&as, 1.5f);
        play_bubble_pop(&as);
        play_lily_note(&as, i % LILY_NOTE_COUNT);
    }
    Uint64 rng_before_explicit = as.audio_rng;
    play_quack_variant(&as, QUACK_VARIANT_SURPRISED);
    CHECK(as.audio_rng == rng_before_explicit);
    for (int i = 0; i < QUACK_CHANNELS; i++) {
        CHECK(as.quack_streams[i] != NULL);
        int queued = SDL_GetAudioStreamQueued(as.quack_streams[i]);
        CHECK(queued >= 0 && queued <= largest_quack);
    }
    int largest_effect = SDL_max(as.splash_clip.pcm_bytes,
                                 as.bubble_pop_clip.pcm_bytes);
    for (int i = 0; i < SPLASH_CHANNELS; i++) {
        CHECK(as.splash_streams[i] != NULL);
        int queued = SDL_GetAudioStreamQueued(as.splash_streams[i]);
        CHECK(queued >= 0 && queued <= largest_effect);
    }
    for (int i = 0; i < LILY_NOTE_COUNT; i++) {
        CHECK(as.lily_note_streams[i] != NULL);
        int queued = SDL_GetAudioStreamQueued(as.lily_note_streams[i]);
        CHECK(queued >= 0 && queued <= as.lily_note_clips[i].pcm_bytes);
    }

    audio_clear_queued(&as);
    for (int i = 0; i < QUACK_CHANNELS; i++)
        CHECK(SDL_GetAudioStreamQueued(as.quack_streams[i]) == 0);
    for (int i = 0; i < SPLASH_CHANNELS; i++)
        CHECK(SDL_GetAudioStreamQueued(as.splash_streams[i]) == 0);
    for (int i = 0; i < LILY_NOTE_COUNT; i++)
        CHECK(SDL_GetAudioStreamQueued(as.lily_note_streams[i]) == 0);

    audio_cleanup(&as);
    CHECK(as.audio_dev == 0);
    for (int i = 0; i < QUACK_VARIANT_COUNT; i++)
        CHECK(as.quack_clips[i].pcm == NULL);
    audio_cleanup(&as);
}

int main(void)
{
    CHECK(SDL_Init(SDL_INIT_AUDIO));
    test_weighted_variants();
    test_bounded_audio();
    SDL_Quit();
    if (failures == 0)
        printf("All audio tests passed.\n");
    return failures == 0 ? 0 : 1;
}
