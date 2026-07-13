# Quack & Splash

A zero-frustration, toddler-friendly interactive sandbox. Swim a duck around a pond, splash water, and listen to cheerful quacks — no fail states, no menus, no stress.

![Screenshot placeholder](docs/screenshot.png)

## About

Quack & Splash is a small digital toy built for very young children (around 2–3 years old). The player steers a low-poly duck across a 3D pond, leaves expanding ripple rings behind, and triggers splash particles with a satisfying quack. The controls are intentionally simple and instantly rewarding, and the fixed camera keeps the whole play area visible at once.

This project is also a learning exercise in minimal C game development with SDL3 and modern OpenGL.

## Features

- Animated 3D water surface with vertex waves and ripple feedback
- Low-poly flat-shaded duck (icosphere body, head, and tapered beak)
- Lily pads floating on the pond
- Splash particles and expanding ripple rings
- Synthesized quack sound with round-robin audio channels
- Keyboard and gamepad controls
- SDL3 callback-driven main loop (mobile-friendly)
- OpenGL 3.3 Core Profile renderer with a custom function-pointer loader

## Requirements

- CMake 3.10 or newer
- A C11 compiler
- Windows or Linux desktop (Android support is the long-term target)
- SDL3 development files (prebuilt binaries are included in `deps/` for Windows)

## Building

### Windows (MinGW Makefiles)

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```

### Windows (MSVC with cl.exe)

```powershell
cmake -S . -B build-msvc -G "Ninja" -DQUAK_USE_MSVC=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-msvc
```

This produces `build/quak.exe` or `build-msvc/quak.exe` and automatically copies `SDL3.dll` next to it.

### Linux

```bash
cmake -S . -B build
cmake --build build
```

You may need to adjust `CMakeLists.txt` to point at your system SDL3 library, or install a SDL3 dev package first.

## Running

```powershell
.\build\quak.exe
```

## Controls

| Input | Action |
|---|---|
| `WASD` / Arrow keys | Swim |
| `Space` / `Enter` | Quack + splash |
| `Escape` | Quit |
| Left stick | Swim |
| Any face button | Quack + splash |

Touch input is planned for the Android target.

## Project structure

```
quak/
├── CMakeLists.txt              # CMake build configuration
├── src/main.c                  # All current game code
├── deps/
│   ├── include/                # SDL3 headers + linmath.h
│   └── lib/                    # Prebuilt SDL3 library
├── docs/                       # Plans and migration notes
├── Game Design Document Exploration.md
└── Technical Design Document - Quack and Splash.md
```

## Tech stack

- **Language:** C11
- **Framework:** SDL3
- **Renderer:** OpenGL 3.3 Core Profile
- **Math:** linmath.h (single-header)
- **Build:** CMake

## Design notes

The game is deliberately minimal:

- Fixed camera so the whole pond is always visible
- No text, no complex menus, no losing condition
- Large, forgiving actions with immediate visual and audio feedback
- Everything is self-contained in one C source file for easy experimentation

See the `docs/` folder and the `Technical Design Document - Quack and Splash.md` for the original design goals and phase-by-phase development plan.

## License

License TBD — this is a personal/experimental project.
