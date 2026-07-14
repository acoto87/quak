# Quack & Splash

Quack & Splash is a zero-frustration, toddler-friendly 3D pond toy. Swim a duck around the pond, make ripples and splashes, and hear friendly synthesized quacks. There are no menus, scores, fail states, or punishments.

## Features

- Animated 3D water with shader-driven waves and ripple feedback
- Textured OBJ duck with day/night reflection, sleep/wake, and zoomies
- Bouncing musical lily pads, poppable bubbles, fireflies, and hidden rainbows
- Touch-follow steering, obstacle routing, finger trails, and scaled splashes
- Keyboard and gamepad controls for desktop development and accessibility
- Fixed 60 Hz simulation behind SDL3's callback API
- Procedural audio with bounded quack variants, splash, pop, and lily-note voices
- SDL3 GPU renderer using Direct3D 12 and DXIL on the current Windows build

## Current Platform Support

The repository now has two build paths:

- **Windows x64 desktop** with Direct3D 12 and DXIL shaders
- **Android arm64-v8a** with an APK built through Gradle/CMake, packaged SDL3 AAR support, and SPIR-V shaders

The Windows build vendors SDL **3.4.4** Windows x64 headers and binaries and packages DXIL shaders. The Android build packages the same assets plus generated SPIR-V shader binaries into the APK.

## Requirements

Windows desktop build:

- Windows 10 or newer with a Direct3D 12-capable GPU and driver
- CMake 3.21 or newer
- A C11 compiler (the checked-in build is tested with MSVC-compatible `cl.exe` and MinGW Makefiles)
- DirectX Shader Compiler (`dxc`) on `PATH`, with Shader Model 6.0 support
- The vendored SDL 3.4.4 files under `deps/`

Android build:

- JDK 17
- Android SDK 35
- Android NDK `28.2.13676358`
- CMake `3.22.1`
- Gradle `8.12` (via the checked-in wrapper)
- Android platform tools with `adb`
- `shadercross` from the SDL3 shadercross tool, available on `PATH` or in `tools/SDL3_shadercross-*/bin`

Configuration fails early when SDL, `dxc`, or the Android toolchain pieces are unavailable. GPU validation is enabled in Debug builds and disabled in Release builds.

## Build

Windows (direct configure):

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Windows (presets):

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

For a production Windows build:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
```

The Windows build stages `quak.exe`, `SDL3.dll`, source assets, and all required DXIL shader blobs into the executable directory. Run the build with:

```powershell
.\build\quak.exe
```

Or with the preset binary directory:

```powershell
.\build\windows-debug\quak.exe
```

Android (from the repository root):

```powershell
.\android\gradlew.bat :app:assembleDebug
```

Install and launch the debug APK on a connected device or emulator:

```powershell
.\android\gradlew.bat :app:installDebug
```

The APK is emitted at:

```text
android/app/build/outputs/apk/debug/quak-debug.apk
```

Set `-DQUAK_SHOW_DEBUG_GEOMETRY=ON` during configuration to render the developer grid and axes. It is off by default.

## Controls

| Input | Action |
|---|---|
| Hold or drag one finger | Swim smoothly toward the touched pond location |
| Hold over a lily pad | Circle the lily continuously |
| Touch water | Create an immediate ripple |
| Release a tap or drag | Create a splash scaled by hold time or travel distance |
| Quick tap | Make the duck quack |
| Tap a lily pad | Bounce it and play its musical note |
| Tap a floating bubble | Pop it into sparkles |
| Tap the duck four times | Trigger short duck zoomies |
| Additional fingers | Create independent water feedback |
| Left mouse click / drag | Simulate one touch for desktop testing |
| `Ctrl` + left mouse drag | Orbit the development camera |
| Mouse wheel | Zoom the development camera |
| `WASD` / Arrow keys | Swim in world-space directions |
| `Space` / `Enter` | Quack and splash |
| Left stick | Swim |
| Any gamepad button | Quack and splash |
| `Escape` | Quit |

Touch input tracks up to ten fingers. The oldest active finger controls swimming; other fingers still produce ripples. Releasing the primary finger hands control to the next oldest active finger.

The duck falls asleep after a quiet pause and wakes when touched or moved. The pond transitions automatically between day and night, with fireflies appearing after dark. Repeated splashes and bubble pops reveal bounded rainbow celebrations.

## Source Layout

```text
src/main.c       SDL callbacks, fixed-step timing, lifecycle, event dispatch
src/input.c      Keyboard/gamepad intent, touch tracking, gesture classification
src/interaction.c Fixed-size FIFO for one-shot gameplay input
src/activities.c Free-play activity director and interaction dispatch
src/game.c       Duck steering, collisions, ripples, particles, idle behavior
src/pond_objects.c Generic fixed-size pond-object pool and hit testing
src/duck_animation.c Whole-duck animation state and transform outputs
src/render.c     SDL GPU device, camera, pipelines, scene rendering
src/duck.c       OBJ/JPEG loading, GPU upload, duck rendering
src/audio.c      Procedural PCM generation and bounded voice pools
src/types.h      Shared constants and application state
shaders/dxil/    HLSL source compiled to DXIL by CMake
tests/           Headless simulation, interaction, and baseline tests
```

The game uses a left-handed world: +X is right, +Y is up, and +Z is forward. SDL GPU projection uses a zero-to-one depth range.

## License

Project source code is licensed under the MIT License in `LICENSE`. Third-party code and asset status are documented separately in `THIRD_PARTY_NOTICES.md`.
