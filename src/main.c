/*
 * main.c — Quack & Splash  (SDL3 callbacks, thin orchestration shell)
 *
 * Controls:
 *   WASD / Arrow keys  — swim        Space / Enter — quack + splash
 *   Escape             — quit        Gamepad left stick — swim
 *   Any gamepad button — quack + splash
 *   Mouse              — touch       Ctrl + mouse drag — orbit camera
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

static void cancel_mouse_controls(AppState *as)
{
    if (as->mouse_captured)
        SDL_SetWindowRelativeMouseMode(as->window, false);
    input_cancel_mouse_touch(as);
    as->camera_dragging = false;
    as->mouse_captured = false;
    as->mouse_touch_active = false;
}

static void set_suspended(AppState *as, bool suspended)
{
    if (!as)
        return;

    as->suspended = suspended;
    as->accumulator = 0.0;
    as->last_perf = SDL_GetPerformanceCounter();
    as->interaction_clock_ns = SDL_GetTicksNS();
    input_clear_touches(as);
    cancel_mouse_controls(as);
    interaction_queue_reset(&as->interaction_queue);
    if (as->audio_dev) {
        if (suspended) {
            SDL_PauseAudioDevice(as->audio_dev);
            audio_clear_queued(as);
        } else {
            audio_clear_queued(as);
            SDL_ResumeAudioDevice(as->audio_dev);
        }
    }
}

static void queue_interaction(AppState *as, const InteractionEvent *source,
                              Uint64 timestamp_ns)
{
    InteractionEvent event = *source;
    event.target_tick = interaction_target_tick(as->simulation_tick,
                                                 as->accumulator,
                                                 as->interaction_clock_ns,
                                                 timestamp_ns,
                                                 FIXED_TIMESTEP,
                                                 MAX_SIMULATION_STEPS);
    bool queued = interaction_queue_push(&as->interaction_queue, &event);
    if (!queued) {
        as->interaction_overflow_count++;
        if (as->interaction_overflow_count == 1)
            SDL_Log("Interaction queue saturated; prioritizing release actions");
    }
    if (!queued && event.type != INTERACTION_POND_PRESS
        && interaction_queue_discard_oldest_type(&as->interaction_queue,
                                                  INTERACTION_POND_PRESS)) {
        queued = interaction_queue_push(&as->interaction_queue, &event);
    }
}

static void queue_input_action(AppState *as, const InputAction *action,
                               Uint64 timestamp_ns)
{
    if (action->interaction_requested)
        queue_interaction(as, &action->interaction, timestamp_ns);
}

static void queue_quack_request(AppState *as, Uint64 timestamp_ns)
{
    const InteractionEvent event = {
        .type = INTERACTION_QUACK_REQUEST,
        .intensity = 1.f,
        .object_id = -1
    };
    queue_interaction(as, &event, timestamp_ns);
}

static bool SDLCALL lifecycle_event_watch(void *userdata, SDL_Event *event)
{
    AppState *as = (AppState *)userdata;
    if (event->type == SDL_EVENT_WILL_ENTER_BACKGROUND
        || event->type == SDL_EVENT_DID_ENTER_BACKGROUND) {
        SDL_SetAtomicInt(&as->lifecycle_suspended, 1);
        SDL_SetAtomicInt(&as->lifecycle_reset_pending, 1);
        if (as->audio_dev)
            SDL_PauseAudioDevice(as->audio_dev);
    } else if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND) {
        SDL_SetAtomicInt(&as->lifecycle_suspended, 0);
        SDL_SetAtomicInt(&as->lifecycle_reset_pending, 1);
    }
    return true;
}

/* ── Init ────────────────────────────────────────────────────────────────── */
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    (void)argc; (void)argv;

    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
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
    game_init(as);

    as->window = SDL_CreateWindow("Quack & Splash",
                                  WINDOW_W, WINDOW_H, 0);
    if (!as->window) {
        SDL_Log("CreateWindow failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    as->win_w = WINDOW_W;  as->win_h = WINDOW_H;

    if (!render_init(as)) return SDL_APP_FAILURE;
    if (!duck_init(as)) return SDL_APP_FAILURE;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO))
        audio_init(as);   /* Non-fatal: the game can run silently. */
    else
        SDL_Log("Audio subsystem unavailable (%s); running silent", SDL_GetError());

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
    as->interaction_clock_ns = SDL_GetTicksNS();

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

    if (SDL_SetAtomicInt(&as->lifecycle_reset_pending, 0) != 0)
        set_suspended(as, SDL_GetAtomicInt(&as->lifecycle_suspended) != 0);
    if (as->suspended)
        return SDL_APP_CONTINUE;

    Uint64 wall_now = SDL_GetTicksNS();
    Uint64 now = SDL_GetPerformanceCounter();
    double frame_dt = (double)(now - as->last_perf) / (double)as->perf_freq;
    as->last_perf = now;
    int steps = game_accumulate_steps(&as->accumulator, frame_dt);
    for (int step = 0; step < steps; step++) {
        PlayerIntent intent = input_get_player_intent(as);
        game_step(as, &intent, (float)FIXED_TIMESTEP);
    }
    as->interaction_clock_ns = wall_now;

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

    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_MINIMIZED:
        cancel_mouse_controls(as);
        input_clear_touches(as);
        break;

    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
        if (!as->event_watch_registered) {
            SDL_SetAtomicInt(&as->lifecycle_suspended, 1);
            SDL_SetAtomicInt(&as->lifecycle_reset_pending, 1);
            if (as->audio_dev)
                SDL_PauseAudioDevice(as->audio_dev);
        }
        break;

    case SDL_EVENT_DID_ENTER_FOREGROUND:
        if (!as->event_watch_registered) {
            SDL_SetAtomicInt(&as->lifecycle_suspended, 0);
            SDL_SetAtomicInt(&as->lifecycle_reset_pending, 1);
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event->button.which == SDL_TOUCH_MOUSEID) break;
        if (event->button.button == SDL_BUTTON_LEFT && !as->suspended) {
            if (input_modifiers_request_camera(SDL_GetModState())) {
                if (SDL_SetWindowRelativeMouseMode(as->window, true)) {
                    as->camera_dragging = true;
                    as->last_mouse_x = event->button.x;
                    as->last_mouse_y = event->button.y;
                    as->mouse_captured = true;
                } else {
                    SDL_Log("Unable to enter relative mouse mode: %s", SDL_GetError());
                }
            } else {
                InputAction action;
                as->mouse_touch_active = input_handle_mouse_touch_event(as, event, &action);
                if (as->mouse_touch_active)
                    queue_input_action(as, &action, event->button.timestamp);
            }
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event->button.which == SDL_TOUCH_MOUSEID) break;
        if (event->button.button == SDL_BUTTON_LEFT) {
            if (as->camera_dragging) {
                SDL_SetWindowRelativeMouseMode(as->window, false);
                as->camera_dragging = false;
                as->mouse_captured = false;
            } else if (as->mouse_touch_active) {
                InputAction action;
                if (input_handle_mouse_touch_event(as, event, &action))
                    queue_input_action(as, &action, event->button.timestamp);
                as->mouse_touch_active = false;
            }
        }
        break;

    case SDL_EVENT_MOUSE_MOTION:
        if (event->motion.which == SDL_TOUCH_MOUSEID) break;
        if (!as->camera_dragging && as->mouse_touch_active
            && (event->motion.state & SDL_BUTTON_LMASK) == 0) {
            cancel_mouse_controls(as);
            break;
        }
        if (as->camera_dragging) {
            float dx = event->motion.xrel;
            float dy = event->motion.yrel;
            as->camera_yaw += dx * CAM_ROT_SPEED;
            as->camera_pitch += dy * CAM_ROT_SPEED;
            as->camera_pitch = SDL_clamp(as->camera_pitch, -CAM_PITCH_LIMIT, CAM_PITCH_LIMIT);
            render_update_picking_view(as);
        } else if (as->mouse_touch_active && !as->suspended) {
            InputAction action;
            if (input_handle_mouse_touch_event(as, event, &action))
                queue_input_action(as, &action, event->motion.timestamp);
        }
        break;

    case SDL_EVENT_MOUSE_WHEEL:
        if (event->wheel.which == SDL_TOUCH_MOUSEID) break;
        if (event->wheel.y != 0) {
            float delta = (float)event->wheel.y * CAM_ZOOM_SPEED;
            as->camera_distance = SDL_max(4.0f, as->camera_distance - delta);
            render_update_picking_view(as);
        }
        break;

    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        {
            if (as->suspended) break;
            InputAction action;
            if (input_handle_touch_event(as, event, &action)
                && action.interaction_requested)
                queue_input_action(as, &action, event->tfinger.timestamp);
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
        if (as->suspended) break;
        queue_quack_request(as, event->gbutton.timestamp);
        break;

    case SDL_EVENT_KEY_DOWN:
        if (as->suspended) break;
        if (event->key.repeat) break;
        switch (event->key.scancode) {
        case SDL_SCANCODE_SPACE:
        case SDL_SCANCODE_RETURN:
            queue_quack_request(as, event->key.timestamp);
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
