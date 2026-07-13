#pragma once
#include "types.h"

bool audio_init(AppState *as);
void audio_init_splash(AppState *as);
void play_quack(AppState *as);
void play_splash(AppState *as);
void audio_cleanup(AppState *as);
