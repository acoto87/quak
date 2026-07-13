#include "audio.h"
#include <math.h>

/* ── Synthesise quack PCM into AppState ──────────────────────────────────── */
static void generate_quack(AppState *as)
{
    int n = SAMPLE_RATE * QUACK_MS / 1000;
    as->quack_pcm_bytes = n * (int)sizeof(Sint16);
    as->quack_pcm = (Sint16 *)SDL_malloc((size_t)as->quack_pcm_bytes);
    if (!as->quack_pcm) return;
    float T = QUACK_MS / 1000.f, f0 = 560.f, f1 = 240.f;
    for (int i = 0; i < n; i++) {
        float t     = (float)i / (float)SAMPLE_RATE;
        float phase = 2.f * 3.14159265f * (f0*t + (f1-f0)/(2.f*T)*t*t);
        float prog  = t / T;
        float env   = prog < 0.06f ? prog/0.06f : expf(-4.f*(prog-0.06f));
        as->quack_pcm[i] = (Sint16)(env * 23000.f *
                            (0.65f*sinf(phase) + 0.35f*sinf(2.f*phase)));
    }
}

void audio_init_splash(AppState *as)
{
    int n = SAMPLE_RATE * 70 / 1000;
    as->splash_pcm_bytes = n * (int)sizeof(Sint16);
    as->splash_pcm = (Sint16 *)SDL_malloc((size_t)as->splash_pcm_bytes);
    if (!as->splash_pcm) return;
    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)SAMPLE_RATE;
        float prog = t / (70.f / 1000.f);
        float env = expf(-8.f * prog);
        float noise = (SDL_randf_r(&as->audio_rng) * 2.f - 1.f) * 0.6f;
        as->splash_pcm[i] = (Sint16)(env * 12000.f * noise);
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */
bool audio_init(AppState *as)
{
    SDL_AudioSpec spec = { SDL_AUDIO_S16, 1, SAMPLE_RATE };
    as->audio_dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (!as->audio_dev) {
        SDL_Log("Audio unavailable (%s) — running silent", SDL_GetError());
        return false;
    }
    for (int i = 0; i < QUACK_CHANNELS; i++) {
        as->quack_streams[i] = SDL_CreateAudioStream(&spec, &spec);
        if (as->quack_streams[i]
            && !SDL_BindAudioStream(as->audio_dev, as->quack_streams[i])) {
            SDL_Log("Unable to bind quack stream: %s", SDL_GetError());
            SDL_DestroyAudioStream(as->quack_streams[i]);
            as->quack_streams[i] = NULL;
        }
    }
    for (int i = 0; i < SPLASH_CHANNELS; i++) {
        as->splash_streams[i] = SDL_CreateAudioStream(&spec, &spec);
        if (as->splash_streams[i]
            && !SDL_BindAudioStream(as->audio_dev, as->splash_streams[i])) {
            SDL_Log("Unable to bind splash stream: %s", SDL_GetError());
            SDL_DestroyAudioStream(as->splash_streams[i]);
            as->splash_streams[i] = NULL;
        }
    }
    generate_quack(as);
    audio_init_splash(as);
    if (!as->quack_pcm || !as->splash_pcm) {
        SDL_Log("Unable to allocate synthesized audio buffers");
        return false;
    }
    return true;
}

void play_quack(AppState *as)
{
    if (!as->quack_pcm || !as->audio_dev) return;
    SDL_AudioStream *st = as->quack_streams[as->quack_next];
    as->quack_next = (as->quack_next + 1) % QUACK_CHANNELS;
    if (!st) return;
    if (!SDL_ClearAudioStream(st)
        || !SDL_PutAudioStreamData(st, as->quack_pcm, as->quack_pcm_bytes))
        SDL_Log("Unable to queue quack audio: %s", SDL_GetError());
}

void play_splash(AppState *as)
{
    int selected = -1;

    if (!as->splash_pcm || !as->audio_dev) return;
    for (int offset = 0; offset < SPLASH_CHANNELS; offset++) {
        int index = (as->splash_next + offset) % SPLASH_CHANNELS;
        if (as->splash_streams[index]
            && SDL_GetAudioStreamQueued(as->splash_streams[index]) <= 0) {
            selected = index;
            break;
        }
    }
    if (selected < 0) {
        for (int offset = 0; offset < SPLASH_CHANNELS; offset++) {
            int index = (as->splash_next + offset) % SPLASH_CHANNELS;
            if (as->splash_streams[index]) {
                selected = index;
                break;
            }
        }
    }
    if (selected < 0) return;

    as->splash_next = (selected + 1) % SPLASH_CHANNELS;
    if (!SDL_ClearAudioStream(as->splash_streams[selected])
        || !SDL_PutAudioStreamData(as->splash_streams[selected],
                                   as->splash_pcm, as->splash_pcm_bytes))
        SDL_Log("Unable to queue splash audio: %s", SDL_GetError());
}

void audio_cleanup(AppState *as)
{
    SDL_free(as->quack_pcm);
    as->quack_pcm = NULL;
    SDL_free(as->splash_pcm);
    as->splash_pcm = NULL;
    for (int i = 0; i < QUACK_CHANNELS; i++) {
        if (as->quack_streams[i]) {
            SDL_DestroyAudioStream(as->quack_streams[i]);
            as->quack_streams[i] = NULL;
        }
    }
    for (int i = 0; i < SPLASH_CHANNELS; i++) {
        if (as->splash_streams[i]) {
            SDL_DestroyAudioStream(as->splash_streams[i]);
            as->splash_streams[i] = NULL;
        }
    }
    if (as->audio_dev) {
        SDL_CloseAudioDevice(as->audio_dev);
        as->audio_dev = 0;
    }
}
