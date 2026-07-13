/*
 * main.c — Quack & Splash  (SDL3 callbacks, thin orchestration shell)
 *
 * Controls:
 *   WASD / Arrow keys  — swim        Space / Enter — quack + splash
 *   Escape             — quit        Gamepad left stick — swim
 *   Any gamepad button — quack + splash
 */
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdint.h>

#include "types.h"
#include "render.h"
#include "duck.h"
#include "audio.h"
#include "game.h"
#include "input.h"

static void set_suspended(AppState *as, bool suspended)
{
    if (!as || as->suspended == suspended)
        return;

    as->suspended = suspended;
    as->accumulator = 0.0;
    as->last_perf = SDL_GetPerformanceCounter();
    input_clear_touches(as);
    if (as->audio_dev) {
        if (suspended)
            SDL_PauseAudioDevice(as->audio_dev);
        else
            SDL_ResumeAudioDevice(as->audio_dev);
    }
}

static bool SDLCALL lifecycle_event_watch(void *userdata, SDL_Event *event)
{
    AppState *as = (AppState *)userdata;
    if (event->type == SDL_EVENT_WILL_ENTER_BACKGROUND
        || event->type == SDL_EVENT_DID_ENTER_BACKGROUND) {
        if (as->audio_dev)
            SDL_PauseAudioDevice(as->audio_dev);
    } else if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND) {
        if (as->audio_dev)
            SDL_ResumeAudioDevice(as->audio_dev);
    }
    return true;
}

/* ── Init ────────────────────────────────────────────────────────────────── */
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    (void)argc; (void)argv;

    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    AppState *as = (AppState *)SDL_calloc(1, sizeof(AppState));
    if (!as) return SDL_APP_FAILURE;
    *appstate = as;

    Uint64 seed = SDL_GetPerformanceCounter() ^ (Uint64)(uintptr_t)as;
    as->simulation_rng = seed ^ 0x9e3779b97f4a7c15ULL;
    as->presentation_rng = seed ^ 0xd1b54a32d192ed03ULL;
    as->audio_rng = seed ^ 0x94d049bb133111ebULL;
    input_init(as);

    as->window = SDL_CreateWindow("Quack & Splash",
                                  WINDOW_W, WINDOW_H, 0);
    if (!as->window) {
        SDL_Log("CreateWindow failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    as->win_w = WINDOW_W;  as->win_h = WINDOW_H;

    if (!render_init(as)) return SDL_APP_FAILURE;
    if (!duck_init(as)) return SDL_APP_FAILURE;
    audio_init(as);   /* Non-fatal: the game can run silently. */

    /* Gamepad */
    {
        int gcount = 0;
        SDL_JoystickID *gids = SDL_GetGamepads(&gcount);
        if (gids && gcount > 0) as->gamepad = SDL_OpenGamepad(gids[0]);
        SDL_free(gids);
    }

    /* Timing */
    as->perf_freq = SDL_GetPerformanceFrequency();
    as->last_perf = SDL_GetPerformanceCounter();
    as->idle_quack_timer = IDLE_QUACK_INTERVAL_MIN + SDL_randf_r(&as->simulation_rng) * (IDLE_QUACK_INTERVAL_MAX - IDLE_QUACK_INTERVAL_MIN);
    as->bubble_timer = BUBBLE_INTERVAL;
    as->duck_reflection_alpha = 0.18f;

    if (SDL_AddEventWatch(lifecycle_event_watch, as))
        as->event_watch_registered = true;
    else
        SDL_Log("SDL_AddEventWatch failed: %s", SDL_GetError());

    SDL_Log("Quack & Splash — WASD=swim  Space=quack  Esc=quit");
    return SDL_APP_CONTINUE;
}

/* ── Main loop ───────────────────────────────────────────────────────────── */
SDL_AppResult SDL_AppIterate(void *appstate)
{
    AppState *as = (AppState *)appstate;

    if (as->suspended)
        return SDL_APP_CONTINUE;

    Uint64 now = SDL_GetPerformanceCounter();
    double frame_dt = (double)(now - as->last_perf) / (double)as->perf_freq;
    as->last_perf = now;
    int steps = game_accumulate_steps(&as->accumulator, frame_dt);
    for (int step = 0; step < steps; step++) {
        PlayerIntent intent = input_get_player_intent(as);
        game_step(as, &intent, (float)FIXED_TIMESTEP);
    }

    /* Render */
    render_frame(as);

    return SDL_APP_CONTINUE;
}

/* ── Events ──────────────────────────────────────────────────────────────── */
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    AppState *as = (AppState *)appstate;

    switch (event->type) {

    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;

    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        render_resize(as, event->window.data1, event->window.data2);
        break;

    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
        set_suspended(as, true);
        break;

    case SDL_EVENT_DID_ENTER_FOREGROUND:
        set_suspended(as, false);
        break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event->button.which == SDL_TOUCH_MOUSEID) break;
        if (event->button.button == SDL_BUTTON_LEFT) {
            SDL_SetWindowRelativeMouseMode(as->window, true);
            as->camera_dragging = true;
            as->last_mouse_x = event->button.x;
            as->last_mouse_y = event->button.y;
            as->mouse_captured = true;
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event->button.which == SDL_TOUCH_MOUSEID) break;
        if (event->button.button == SDL_BUTTON_LEFT) {
            SDL_SetWindowRelativeMouseMode(as->window, false);
            as->camera_dragging = false;
            as->mouse_captured = false;
        }
        break;

    case SDL_EVENT_MOUSE_MOTION:
        if (event->motion.which == SDL_TOUCH_MOUSEID) break;
        if (as->camera_dragging) {
            float dx = event->motion.xrel;
            float dy = event->motion.yrel;
            as->camera_yaw += dx * CAM_ROT_SPEED;
            as->camera_pitch += dy * CAM_ROT_SPEED;
            as->camera_pitch = SDL_clamp(as->camera_pitch, -CAM_PITCH_LIMIT, CAM_PITCH_LIMIT);
        }
        break;

    case SDL_EVENT_MOUSE_WHEEL:
        if (event->wheel.which == SDL_TOUCH_MOUSEID) break;
        if (event->wheel.y != 0) {
            float delta = (float)event->wheel.y * CAM_ZOOM_SPEED;
            as->camera_distance = SDL_max(4.0f, as->camera_distance - delta);
        }
        break;

    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        {
            InputAction action;
            if (input_handle_touch_event(as, event, &action)) {
                if (action.ripple_requested)
                    spawn_ripple(as, action.effect_x, action.effect_z);
                if (action.tap_requested)
                    spawn_tap_effect(as, action.effect_x, action.effect_z);
            }
        }
        break;


    case SDL_EVENT_GAMEPAD_ADDED:
        if (!as->gamepad)
            as->gamepad = SDL_OpenGamepad(event->gdevice.which);
        break;

    case SDL_EVENT_GAMEPAD_REMOVED:
        if (as->gamepad &&
            SDL_GetGamepadID(as->gamepad) == event->gdevice.which) {
            SDL_CloseGamepad(as->gamepad);
            as->gamepad = NULL;
        }
        break;

    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        quack_and_splash(as);
        break;

    case SDL_EVENT_KEY_DOWN:
        if (event->key.repeat) break;
        switch (event->key.scancode) {
        case SDL_SCANCODE_SPACE:
        case SDL_SCANCODE_RETURN:
            quack_and_splash(as);
            break;
        case SDL_SCANCODE_ESCAPE:
            return SDL_APP_SUCCESS;
        default:
            break;
        }
        break;

    default:
        break;
    }

    return SDL_APP_CONTINUE;
}

/* ── Shutdown ────────────────────────────────────────────────────────────── */
void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    (void)result;
    AppState *as = (AppState *)appstate;
    if (!as) return;

    if (as->event_watch_registered)
        SDL_RemoveEventWatch(lifecycle_event_watch, as);

    duck_cleanup(as);
    render_cleanup(as);
    audio_cleanup(as);

    if (as->gamepad) SDL_CloseGamepad(as->gamepad);
    if (as->window)  SDL_DestroyWindow(as->window);
    SDL_free(as);
    SDL_Quit();
}
