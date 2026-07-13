# Agent Guide - Quack & Splash

## Project

- C11 toddler-friendly 3D pond sandbox
- SDL 3.4.4 callback API and SDL GPU API
- Current supported renderer: Windows x64, Direct3D 12, DXIL SM 6.0
- CMake 3.21+
- `linmath.h` for matrix/vector storage
- Android/Vulkan is the next platform target, but SPIR-V and Android packaging do not exist yet

Do not reintroduce SDL_Renderer or the retired OpenGL renderer. The GLSL files under `assets/shaders/opengl/` are historical and are not the active shader path.

## Layout

```text
src/main.c       SDL callbacks, fixed timestep, lifecycle, event dispatch
src/input.c/.h   Unified keyboard/gamepad/touch intent and pond picking
src/game.c/.h    Duck simulation and effects
src/render.c/.h  SDL GPU resources, camera, pipelines, frame submission
src/duck.c/.h    OBJ/JPEG loading and duck drawing
src/audio.c/.h   Procedural audio and stream pools
src/types.h      Shared constants and AppState
shaders/dxil/    HLSL sources compiled by dxc
tests/           CPU-side simulation/input tests
```

## Build And Test

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
.\build\quak.exe
```

`dxc` is required. A successful build stages `SDL3.dll`, assets, and all required DXIL shader blobs next to the executable. Debug builds enable SDL GPU validation. `QUAK_SHOW_DEBUG_GEOMETRY` is off by default.

## Architecture

The program uses SDL callback entry points:

```c
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]);
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event);
SDL_AppResult SDL_AppIterate(void *appstate);
void SDL_AppQuit(void *appstate, SDL_AppResult result);
```

Simulation runs at `FIXED_TIMESTEP` (60 Hz) through `game_step`. Rendering occurs once per `SDL_AppIterate`. Keep one-shot input feedback event-driven, but keep movement and other persistent state in the fixed simulation.

Input devices produce `PlayerIntent`. Touch uses normalized SDL coordinates, unprojects through the current projection/view matrices, and intersects the pond plane at Y=0. Filter synthetic touch/mouse events in both directions.

The world and camera are left-handed:

| Axis | Direction |
|---|---|
| +X | right |
| +Y | up |
| +Z | forward |

Velocity is conventional world-space velocity: position is integrated with `position += velocity * dt` on both X and Z. SDL GPU clip depth is `[0, 1]`; do not use `linmath.h`'s right-handed OpenGL `look_at` or `perspective` helpers directly.

HLSL uses SDL GPU's required register spaces. Add each new shader stage to `DXIL_SHADER_TARGETS` in `CMakeLists.txt`, and keep the runtime shader metadata in `render_get_shader_pair` synchronized with its resource bindings.

## Style

- C11 only
- `float` for game math unless an API requires `double`
- `UPPER_SNAKE_CASE` constants, `PascalCase` types, `snake_case` functions
- Fixed-size pools for effects and voices
- `SDL_Log` for diagnostics
- Keep child-facing builds free of grid, axes, menus, scores, and failure states
- Do not claim Linux, Android, Vulkan, or Metal support until compatible SDL libraries, shader formats, packaging, and launch tests exist

## Verification

After changes, verify:

1. Configure and build succeed.
2. `ctest` passes.
3. The executable launches and required assets load.
4. WASD/arrows, quack keys, and camera controls still work.
5. Touch produces immediate ripples, follows smoothly, distinguishes taps from drags, and hands off the primary finger.
6. The duck remains inside `DUCK_WORLD_BOUND` and reflects from lily pads.
7. Audio queues remain bounded during repeated input.
