#pragma once
#include "types.h"

bool audio_init(AppState *as);
QuackVariant audio_select_quack_variant(Uint64 *rng);
void play_quack(AppState *as);
void play_quack_variant(AppState *as, QuackVariant variant);
void play_splash(AppState *as);
void play_splash_scaled(AppState *as, float intensity);
void play_bubble_pop(AppState *as);
void play_lily_note(AppState *as, int note_index);
void audio_clear_queued(AppState *as);
void audio_cleanup(AppState *as);
