# Agent Guide — Quack & Splash

This file is for coding agents working on the `quak` repository. It contains the build steps, architecture notes, and conventions you need to make safe, useful changes.

## Project at a glance

- **Name:** Quack & Splash (repo folder `quak`)
- **Genre:** Toddler-friendly digital toy / interactive sandbox
- **Language:** C11
- **Core framework:** SDL3
- **Renderer:** OpenGL 3.3 Core Profile (migrated from SDL_Renderer)
- **Math library:** `linmath.h` (single header in `deps/include/`)
- **Build system:** CMake
- **Target platforms:** Windows and Linux for desktop development; Android is the long-term target
- **Current state:** Migrated to SDL3 GPU API (Direct3D 12 / DXIL on Windows). Source is split across `src/main.c`, `src/render.c`, `src/game.c`, `src/duck.c`, and `src/audio.c`. Renders a real OBJ duck model with a diffuse texture, an animated water plane, lily pads, ripple rings, splash particles, and plays synthesized quack sounds.

## Repository layout

```
quak/
├── CMakeLists.txt              # Build configuration
├── src/
│   ├── main.c                  # SDL3 callback shell, timing, event dispatch
│   ├── types.h                 # All constants + AppState struct
│   ├── render.c / render.h     # SDL3 GPU device, pipelines, all draw functions
│   ├── game.c  / game.h        # Duck physics, ripple/particle simulation
│   ├── duck.c  / duck.h        # OBJ parser, texture loader, duck draw
│   └── audio.c / audio.h       # PCM synthesis, quack playback
├── shaders/dxil/               # HLSL shader sources compiled to DXIL by dxc
│   ├── common.hlsl
│   ├── water.{vert,frag}.hlsl
│   ├── lit.{vert,frag}.hlsl
│   ├── unlit.{vert,frag}.hlsl
│   └── tex.{vert,frag}.hlsl
├── assets/                     # Runtime assets (copied next to quak.exe by CMake)
│   ├── 10602_Rubber_Duck_v1_L3.obj
│   ├── 10602_Rubber_Duck_v1_diffuse.jpg
│   └── shaders/dxil/           # Compiled *.dxil blobs (produced at build time)
├── deps/
│   ├── include/                # SDL3 headers + linmath.h + stb_image.h
│   └── lib/                    # Prebuilt SDL3 import library + SDL3.dll
├── build/                      # Out-of-source build directory (already created)
├── plans/                      # Feature plan documents
├── docs/
│   ├── development-plan.md     # 2D SDL_Renderer phase plan (mostly completed)
│   ├── 3d-migration-plan.md    # OpenGL 3.3 migration plan (superseded)
│   ├── sdl3-gpu-migration-plan.md       # SDL3 GPU migration plan (active)
│   ├── sdl3-gpu-implementation-checklist.md
│   └── 01-setup.md             # Prompt artifact
├── Game Design Document Exploration.md
└── Technical Design Document - Quack and Splash.md
```

## How to build

The project uses an out-of-source CMake build. On this Windows machine the existing build directory uses MinGW Makefiles.

```powershell
# From the repo root
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```

After a successful build:

- `build/quak.exe` is the executable
- `build/SDL3.dll` is copied automatically by CMake

Run it with:

```powershell
.\build\quak.exe
```

### Linux notes

On Linux, change the linked system libraries in `CMakeLists.txt` if needed:

```cmake
find_library(SDL3_LIBRARY NAMES SDL3 libSDL3.so PATHS ${SDL3_LIB_DIR} NO_DEFAULT_PATH)
target_link_libraries(quak PRIVATE ${SDL3_LIBRARY} GL m)
```

You may also need to install a system SDL3 development package or point `deps/` at your own build.

## Architecture notes

### SDL3 callback main loop

The program uses SDL3's callback entry points, **not** a hand-written `main` loop:

```c
#define SDL_MAIN_USE_CALLBACKS 1

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]);
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event);
SDL_AppResult SDL_AppIterate(void *appstate);
void          SDL_AppQuit(void *appstate, SDL_AppResult result);
```

Keep all initialization in `SDL_AppInit`, per-frame simulation in `SDL_AppIterate`, input in `SDL_AppEvent`, and cleanup in `SDL_AppQuit`.

### OpenGL loading

Do **not** link a modern OpenGL loader like GLAD or GLEW. The project manually declares `PFNGL*` function pointer typedefs and loads them with `SDL_GL_GetProcAddress` in `load_gl_procs()`. If you add a new GL function:

1. Add a `GLPROC(...)` declaration near the existing block.
2. Add a `LOADGL(name)` call inside `load_gl_procs()`.
3. Use the global function pointer directly (e.g., `glUseProgram(...)`).

Only GL 1.1 functions are called directly; everything else must go through the loader.

### Shader conventions

All GLSL is stored as C string literals at the top of `main.c`. Current shader families:

- `WATER_VERT` / `WATER_FRAG` — animated water plane + ripple feedback
- `LIT_VERT` / `LIT_FRAG` — diffuse+ambient shading for duck parts and lily pads
- `UNLIT_VERT` / `UNLIT_FRAG` — flat RGBA for ripple rings and splash particles

If you add a new shader, follow the same inline string-literal pattern and use `compile_shader()` / `link_program()` helpers.

### Coordinate system

The project uses a **left-handed coordinate system**:

| Axis | Direction | Meaning in game world |
|------|-----------|-----------------------|
| X    | Right     | Positive X is rightward on the pond |
| Y    | Up        | Positive Y is upward; water surface sits at `y = 0` |
| Z    | Forward   | Positive Z is forward (into the scene) |

Key conventions:

- Duck swims on the XZ plane at `y = 0`
- Pressing **W / Up arrow** moves the duck in the **+Z** direction (forward)
- Pressing **S / Down arrow** moves the duck in the **−Z** direction (backward)
- Pressing **D / Right arrow** moves the duck in the **+X** direction (right)
- Pressing **A / Left arrow** moves the duck in the **−X** direction (left)
- World bounds are `±WORLD_BOUND` (currently `16.0f`) on both X and Z
- Camera orbits the scene; mouse drag rotates yaw/pitch, scroll wheel zooms
- D3D12 (via SDL3 GPU) uses a native left-handed NDC, consistent with this convention
- `linmath.h` functions (`mat4x4_look_at`, `mat4x4_perspective`) are right-handed by default; projection and view matrices must be constructed to match the LH convention (negate Z or use appropriate clip-space mapping)

### Audio

Quacks are synthesized into memory at startup (no external WAV file). Four `SDL_AudioStream` handles are used in round-robin fashion so rapid presses do not clip.

## Code style

- C11, no C++.
- Prefer `float` for game math; use `double` only when required by an API.
- Constants are `#define UPPER_SNAKE_CASE`.
- Structs and typedefs are `PascalCase`.
- Functions and variables are `snake_case`.
- Keep related constants grouped near the top of `main.c`.
- Use `SDL_Log` for diagnostics, not `printf` directly.

## Common tasks

### Adding a new uniform to a shader

1. Add the uniform declaration to the GLSL string literal.
2. Add a `GLint` field to `AppState` to cache its location (or use a local `glGetUniformLocation` call if it changes).
3. Upload the value each frame or when it changes.

### Adding a new mesh

1. Write a generator function that fills a vertex array and returns a vertex count.
2. Create a VAO + VBO (and EBO if indexed) in `SDL_AppInit`.
3. Draw it in `SDL_AppIterate` with the correct shader program and uniforms.
4. Delete the buffers in `SDL_AppQuit`.

### Adding input handling

- Keyboard: handle in `SDL_AppEvent` under `SDL_EVENT_KEY_DOWN` / `SDL_EVENT_KEY_UP`.
- Gamepad: handle `SDL_EVENT_GAMEPAD_BUTTON_DOWN`, `SDL_EVENT_GAMEPAD_ADDED`, `SDL_EVENT_GAMEPAD_REMOVED`, and poll axes in `SDL_AppIterate` with `SDL_GetGamepadAxis`.
- Touch: handle `SDL_EVENT_FINGER_DOWN`, `SDL_EVENT_FINGER_MOTION`, `SDL_EVENT_FINGER_UP`. Remember to filter synthetic mouse events (`event.button.which == SDL_TOUCH_MOUSEID`).

## Things to avoid

- Do not reintroduce `SDL_Renderer` or 2D texture rendering. The project is intentionally 3D OpenGL now.
- Do not add heavy external dependencies. The goal is a minimal, portable C codebase.
- Avoid complex menus, fail states, or text-heavy UI. The game is a zero-frustration toy.

## Testing checklist after changes

Before declaring a change complete, verify:

1. Project builds with `cmake --build build` without warnings treated as errors.
2. `build/quak.exe` launches and shows the pond.
3. Keyboard controls still work: WASD/arrows swim, Space/Enter quacks, Escape quits.
4. If you have a gamepad: left stick swims, any face button quacks.
5. Audio plays on quack.
6. Ripples and particles still spawn.
7. Duck stays within the visible pond area.

## Where to read more

- `docs/development-plan.md` — original 2D phase plan and checklists
- `docs/3d-migration-plan.md` — detailed OpenGL migration plan and `AppState` summary
- `docs/sdl3-gpu-migration-plan.md` — SDL3 GPU API migration plan (active)
- `docs/sdl3-gpu-implementation-checklist.md` — migration tracking checklist
- `plans/` — feature plan documents for ongoing improvements
- `Technical Design Document - Quack and Splash.md` — core design goals and toddler-focused UX constraints
- `Game Design Document Exploration.md` — broader suite vision and pediatric UX research notes
