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

#include "types.h"
#include "render.h"
#include "duck.h"
#include "audio.h"
#include "game.h"

/* ── Init ────────────────────────────────────────────────────────────────── */
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    (void)argc; (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    AppState *as = (AppState *)SDL_calloc(1, sizeof(AppState));
    if (!as) return SDL_APP_FAILURE;
    *appstate = as;

    as->window = SDL_CreateWindow("Quack & Splash",
                                  WINDOW_W, WINDOW_H, 0);
    if (!as->window) {
        SDL_Log("CreateWindow failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    as->win_w = WINDOW_W;  as->win_h = WINDOW_H;

    if (!render_init(as)) return SDL_APP_FAILURE;
    duck_init(as);    /* non-fatal if OBJ not found */
    audio_init(as);   /* non-fatal — runs silent    */

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
    as->idle_quack_timer = IDLE_QUACK_INTERVAL_MIN + SDL_randf() * (IDLE_QUACK_INTERVAL_MAX - IDLE_QUACK_INTERVAL_MIN);
    as->bubble_timer = BUBBLE_INTERVAL;
    as->duck_reflection_alpha = 0.18f;

    SDL_Log("Quack & Splash — WASD=swim  Space=quack  Esc=quit");
    return SDL_APP_CONTINUE;
}

/* ── Main loop ───────────────────────────────────────────────────────────── */
SDL_AppResult SDL_AppIterate(void *appstate)
{
    AppState *as = (AppState *)appstate;

    /* Delta time */
    Uint64 now = SDL_GetPerformanceCounter();
    float  dt  = (float)(now - as->last_perf) / (float)as->perf_freq;
    as->last_perf = now;
    if (dt > 0.05f) dt = 0.05f;
    as->elapsed += dt;

    /* Simulate */
    update_duck_physics(as, dt);
    update_duck_idle(as, dt);
    update_lilypad_collisions(as);
    update_ripples(as, dt);
    update_particles(as, dt);
    if (as->quack_cooldown > 0.f) as->quack_cooldown = SDL_max(0.f, as->quack_cooldown - dt);
    if (as->camera_shake_t > 0.f) as->camera_shake_t = SDL_max(0.f, as->camera_shake_t - dt);

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

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event->button.button == SDL_BUTTON_LEFT) {
            SDL_SetWindowRelativeMouseMode(as->window, true);
            as->camera_dragging = true;
            as->last_mouse_x = event->button.x;
            as->last_mouse_y = event->button.y;
            as->mouse_captured = true;
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event->button.button == SDL_BUTTON_LEFT) {
            SDL_SetWindowRelativeMouseMode(as->window, false);
            as->camera_dragging = false;
            as->mouse_captured = false;
        }
        break;

    case SDL_EVENT_MOUSE_MOTION:
        if (as->camera_dragging) {
            int dx = event->motion.xrel;
            int dy = event->motion.yrel;
            as->camera_yaw += (float)dx * CAM_ROT_SPEED;
            as->camera_pitch += (float)dy * CAM_ROT_SPEED;
            as->camera_pitch = SDL_clamp(as->camera_pitch, -CAM_PITCH_LIMIT, CAM_PITCH_LIMIT);
        }
        break;

    case SDL_EVENT_MOUSE_WHEEL:
        if (event->wheel.y != 0) {
            float delta = (float)event->wheel.y * CAM_ZOOM_SPEED;
            as->camera_distance = SDL_max(4.0f, as->camera_distance - delta);
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

    duck_cleanup(as);
    render_cleanup(as);
    audio_cleanup(as);

    if (as->gamepad) SDL_CloseGamepad(as->gamepad);
    if (as->window)  SDL_DestroyWindow(as->window);
    SDL_free(as);
    SDL_Quit();
}
