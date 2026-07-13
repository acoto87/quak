#include "audio.h"

#include <math.h>

typedef struct {
    float duration;
    float start_frequency;
    float end_frequency;
    float modulation;
    float gain;
} QuackParameters;

static bool allocate_clip(AudioClip *clip, int sample_count)
{
    clip->pcm_bytes = sample_count * (int)sizeof(Sint16);
    clip->pcm = (Sint16 *)SDL_malloc((size_t)clip->pcm_bytes);
    return clip->pcm != NULL;
}

static QuackParameters get_quack_parameters(QuackVariant variant)
{
    switch (variant) {
    case QUACK_VARIANT_TINY:
        return (QuackParameters){0.18f, 812.f, 348.f, 0.12f, 0.82f};
    case QUACK_VARIANT_DEEP:
        return (QuackParameters){0.35f, 336.f, 144.f, 0.08f, 1.f};
    case QUACK_VARIANT_HICCUP:
        return (QuackParameters){0.12f, 644.f, 276.f, 0.20f, 0.78f};
    case QUACK_VARIANT_SURPRISED:
        return (QuackParameters){0.23f, 360.f, 760.f, 0.10f, 0.9f};
    case QUACK_VARIANT_NORMAL:
    default:
        return (QuackParameters){0.19f, 560.f, 240.f, 0.f, 1.f};
    }
}

static bool generate_quack(AudioClip *clip, QuackVariant variant)
{
    QuackParameters params = get_quack_parameters(variant);
    int sample_count = (int)(params.duration * (float)SAMPLE_RATE);
    if (!allocate_clip(clip, sample_count))
        return false;

    for (int i = 0; i < sample_count; i++) {
        float t = (float)i / (float)SAMPLE_RATE;
        float progress = t / params.duration;
        float sweep = params.start_frequency * t
                    + (params.end_frequency - params.start_frequency)
                    / (2.f * params.duration) * t * t;
        float phase = 2.f * SDL_PI_F * sweep
                    + sinf(progress * 4.f * SDL_PI_F) * params.modulation;
        float envelope = progress < 0.06f
                       ? progress / 0.06f
                       : expf(-4.f * (progress - 0.06f));
        float sample = envelope * 23000.f * params.gain
                     * (0.65f * sinf(phase) + 0.35f * sinf(2.f * phase));
        clip->pcm[i] = (Sint16)SDL_clamp(sample, -32767.f, 32767.f);
    }
    return true;
}

static bool generate_splash(AudioClip *clip)
{
    const int sample_count = SAMPLE_RATE * 70 / 1000;
    Uint64 rng = 0x51A5A123ULL;
    if (!allocate_clip(clip, sample_count))
        return false;
    for (int i = 0; i < sample_count; i++) {
        float progress = (float)i / (float)sample_count;
        float envelope = expf(-8.f * progress);
        float noise = (SDL_randf_r(&rng) * 2.f - 1.f) * 0.6f;
        clip->pcm[i] = (Sint16)(envelope * 12000.f * noise);
    }
    return true;
}

static bool generate_bubble_pop(AudioClip *clip)
{
    const int sample_count = SAMPLE_RATE * 65 / 1000;
    const float duration = 0.065f;
    Uint64 rng = 0xB0BB1E55ULL;
    if (!allocate_clip(clip, sample_count))
        return false;
    for (int i = 0; i < sample_count; i++) {
        float t = (float)i / (float)SAMPLE_RATE;
        float progress = t / duration;
        float phase = 2.f * SDL_PI_F * (680.f * t - 280.f / (2.f * duration) * t * t);
        float envelope = expf(-9.f * progress);
        float noise = (SDL_randf_r(&rng) * 2.f - 1.f) * 0.12f;
        clip->pcm[i] = (Sint16)(envelope * 15000.f * (sinf(phase) + noise));
    }
    return true;
}

static bool generate_lily_note(AudioClip *clip, float frequency)
{
    const float duration = 0.32f;
    int sample_count = (int)(duration * (float)SAMPLE_RATE);
    if (!allocate_clip(clip, sample_count))
        return false;
    for (int i = 0; i < sample_count; i++) {
        float t = (float)i / (float)SAMPLE_RATE;
        float progress = t / duration;
        float attack = SDL_min(progress / 0.025f, 1.f);
        float envelope = attack * expf(-5.f * progress);
        float phase = 2.f * SDL_PI_F * frequency * t;
        float tone = 0.78f * sinf(phase)
                   + 0.17f * sinf(2.f * phase)
                   + 0.05f * sinf(3.f * phase);
        clip->pcm[i] = (Sint16)(envelope * 10500.f * tone);
    }
    return true;
}

static SDL_AudioStream *create_bound_stream(AppState *as,
                                            const SDL_AudioSpec *spec)
{
    SDL_AudioStream *stream = SDL_CreateAudioStream(spec, spec);
    if (stream && !SDL_BindAudioStream(as->audio_dev, stream)) {
        SDL_DestroyAudioStream(stream);
        stream = NULL;
    }
    return stream;
}

static void play_clip(SDL_AudioStream *stream, const AudioClip *clip, float gain)
{
    if (!stream || !clip || !clip->pcm || clip->pcm_bytes <= 0)
        return;
    if (!SDL_SetAudioStreamGain(stream, SDL_clamp(gain, 0.f, 1.f))
        || !SDL_ClearAudioStream(stream)
        || !SDL_PutAudioStreamData(stream, clip->pcm, clip->pcm_bytes))
        SDL_Log("Unable to queue audio: %s", SDL_GetError());
}

static SDL_AudioStream *select_stream(SDL_AudioStream **streams, int count,
                                      int *next)
{
    int selected = -1;
    for (int offset = 0; offset < count; offset++) {
        int index = (*next + offset) % count;
        if (streams[index] && SDL_GetAudioStreamQueued(streams[index]) <= 0) {
            selected = index;
            break;
        }
    }
    if (selected < 0) {
        for (int offset = 0; offset < count; offset++) {
            int index = (*next + offset) % count;
            if (streams[index]) {
                selected = index;
                break;
            }
        }
    }
    if (selected < 0)
        return NULL;
    *next = (selected + 1) % count;
    return streams[selected];
}

bool audio_init(AppState *as)
{
    static const float note_frequencies[LILY_NOTE_COUNT] = {
        261.63f, 293.66f, 329.63f, 392.f, 440.f
    };
    SDL_AudioSpec spec = {SDL_AUDIO_S16, 1, SAMPLE_RATE};

    for (int i = 0; i < QUACK_VARIANT_COUNT; i++) {
        if (!generate_quack(&as->quack_clips[i], (QuackVariant)i))
            goto fail;
    }
    if (!generate_splash(&as->splash_clip)
        || !generate_bubble_pop(&as->bubble_pop_clip))
        goto fail;
    for (int i = 0; i < LILY_NOTE_COUNT; i++) {
        if (!generate_lily_note(&as->lily_note_clips[i], note_frequencies[i]))
            goto fail;
    }

    as->audio_dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (!as->audio_dev)
        goto fail;
    for (int i = 0; i < QUACK_CHANNELS; i++) {
        as->quack_streams[i] = create_bound_stream(as, &spec);
        if (!as->quack_streams[i]) goto fail;
    }
    for (int i = 0; i < SPLASH_CHANNELS; i++) {
        as->splash_streams[i] = create_bound_stream(as, &spec);
        if (!as->splash_streams[i]) goto fail;
    }
    for (int i = 0; i < LILY_NOTE_COUNT; i++) {
        as->lily_note_streams[i] = create_bound_stream(as, &spec);
        if (!as->lily_note_streams[i]) goto fail;
    }

    SDL_ResumeAudioDevice(as->audio_dev);
    return true;

fail:
    SDL_Log("Audio unavailable (%s); running silent", SDL_GetError());
    audio_cleanup(as);
    return false;
}

QuackVariant audio_select_quack_variant(Uint64 *rng)
{
    int roll = SDL_rand_r(rng, 100);
    if (roll < 60) return QUACK_VARIANT_NORMAL;
    if (roll < 75) return QUACK_VARIANT_TINY;
    if (roll < 87) return QUACK_VARIANT_DEEP;
    if (roll < 95) return QUACK_VARIANT_HICCUP;
    return QUACK_VARIANT_SURPRISED;
}

void play_quack(AppState *as)
{
    play_quack_variant(as, audio_select_quack_variant(&as->audio_rng));
}

void play_quack_variant(AppState *as, QuackVariant variant)
{
    if (variant < 0 || variant >= QUACK_VARIANT_COUNT)
        variant = QUACK_VARIANT_NORMAL;
    SDL_AudioStream *stream = select_stream(as->quack_streams, QUACK_CHANNELS,
                                            &as->quack_next);
    play_clip(stream, &as->quack_clips[variant], 1.f);
}

void play_splash(AppState *as)
{
    play_splash_scaled(as, 1.f);
}

void play_splash_scaled(AppState *as, float intensity)
{
    float gain = SDL_clamp(0.35f + SDL_clamp(intensity, 0.35f, 1.5f) * 0.45f,
                           0.35f, 1.f);
    SDL_AudioStream *stream = select_stream(as->splash_streams, SPLASH_CHANNELS,
                                            &as->splash_next);
    play_clip(stream, &as->splash_clip, gain);
}

void play_bubble_pop(AppState *as)
{
    SDL_AudioStream *stream = select_stream(as->splash_streams, SPLASH_CHANNELS,
                                            &as->splash_next);
    play_clip(stream, &as->bubble_pop_clip, 0.8f);
}

void play_lily_note(AppState *as, int note_index)
{
    if (note_index < 0 || note_index >= LILY_NOTE_COUNT)
        return;
    play_clip(as->lily_note_streams[note_index],
              &as->lily_note_clips[note_index], 0.72f);
}

void audio_clear_queued(AppState *as)
{
    for (int i = 0; i < QUACK_CHANNELS; i++)
        if (as->quack_streams[i]) SDL_ClearAudioStream(as->quack_streams[i]);
    for (int i = 0; i < SPLASH_CHANNELS; i++)
        if (as->splash_streams[i]) SDL_ClearAudioStream(as->splash_streams[i]);
    for (int i = 0; i < LILY_NOTE_COUNT; i++)
        if (as->lily_note_streams[i]) SDL_ClearAudioStream(as->lily_note_streams[i]);
}

void audio_cleanup(AppState *as)
{
    for (int i = 0; i < QUACK_CHANNELS; i++) {
        if (as->quack_streams[i]) SDL_DestroyAudioStream(as->quack_streams[i]);
        as->quack_streams[i] = NULL;
    }
    for (int i = 0; i < SPLASH_CHANNELS; i++) {
        if (as->splash_streams[i]) SDL_DestroyAudioStream(as->splash_streams[i]);
        as->splash_streams[i] = NULL;
    }
    for (int i = 0; i < LILY_NOTE_COUNT; i++) {
        if (as->lily_note_streams[i]) SDL_DestroyAudioStream(as->lily_note_streams[i]);
        as->lily_note_streams[i] = NULL;
    }
    if (as->audio_dev) SDL_CloseAudioDevice(as->audio_dev);
    as->audio_dev = 0;

    for (int i = 0; i < QUACK_VARIANT_COUNT; i++) {
        SDL_free(as->quack_clips[i].pcm);
        as->quack_clips[i] = (AudioClip){0};
    }
    for (int i = 0; i < LILY_NOTE_COUNT; i++) {
        SDL_free(as->lily_note_clips[i].pcm);
        as->lily_note_clips[i] = (AudioClip){0};
    }
    SDL_free(as->splash_clip.pcm);
    SDL_free(as->bubble_pop_clip.pcm);
    as->splash_clip = (AudioClip){0};
    as->bubble_pop_clip = (AudioClip){0};
    as->quack_next = 0;
    as->splash_next = 0;
}
