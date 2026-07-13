cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0091 NEW)
project(quak C)

option(QUAK_USE_MSVC "Use the dynamic MSVC runtime" OFF)
option(QUAK_SHOW_DEBUG_GEOMETRY "Render the developer grid and world axes" OFF)

if(NOT WIN32)
    message(FATAL_ERROR
        "The checked-in SDL binaries and compiled shader path currently support Windows x64/D3D12 only. Vulkan/SPIR-V support is not packaged yet.")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC)
    string(REGEX REPLACE "[/-]W[0-4]" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
endif()

if(NOT DEFINED CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

if(QUAK_USE_MSVC)
    if(NOT MSVC)
        message(FATAL_ERROR "QUAK_USE_MSVC=ON requires an MSVC-compatible compiler (for example cl.exe).")
    endif()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

function(quak_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

set(SDL3_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/include")
set(SDL3_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/lib")
set(DXIL_SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders/dxil")
set(DXIL_SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated-assets/shaders/dxil")

find_program(DXC_EXECUTABLE dxc REQUIRED)
find_library(SDL3_LIBRARY
    NAMES SDL3 SDL3dll libSDL3.dll.a
    PATHS "${SDL3_LIB_DIR}"
    NO_DEFAULT_PATH
)
if(NOT SDL3_LIBRARY)
    message(FATAL_ERROR "A Windows x64 SDL3 import library was not found in ${SDL3_LIB_DIR}")
endif()
if(NOT EXISTS "${SDL3_LIB_DIR}/SDL3.dll")
    message(FATAL_ERROR "SDL3.dll was not found in ${SDL3_LIB_DIR}")
endif()

file(MAKE_DIRECTORY "${DXIL_SHADER_OUT_DIR}")
set(DXIL_SHADER_TARGETS
    water.vert.hlsl:vs_6_0:water.vert.dxil
    water.frag.hlsl:ps_6_0:water.frag.dxil
    lit.vert.hlsl:vs_6_0:lit.vert.dxil
    lit.frag.hlsl:ps_6_0:lit.frag.dxil
    unlit.vert.hlsl:vs_6_0:unlit.vert.dxil
    unlit.frag.hlsl:ps_6_0:unlit.frag.dxil
    shadow.vert.hlsl:vs_6_0:shadow.vert.dxil
    shadow.frag.hlsl:ps_6_0:shadow.frag.dxil
    particle.vert.hlsl:vs_6_0:particle.vert.dxil
    particle.frag.hlsl:ps_6_0:particle.frag.dxil
    tex.vert.hlsl:vs_6_0:tex.vert.dxil
    tex.frag.hlsl:ps_6_0:tex.frag.dxil
)

foreach(shader_spec IN LISTS DXIL_SHADER_TARGETS)
    string(REPLACE ":" ";" shader_parts "${shader_spec}")
    list(GET shader_parts 0 shader_src)
    list(GET shader_parts 1 shader_profile)
    list(GET shader_parts 2 shader_out)

    add_custom_command(
        OUTPUT "${DXIL_SHADER_OUT_DIR}/${shader_out}"
        COMMAND "${DXC_EXECUTABLE}" -E main -T ${shader_profile} -WX -Ges
                -Fo "${DXIL_SHADER_OUT_DIR}/${shader_out}"
                -I "${DXIL_SHADER_SRC_DIR}"
                "${DXIL_SHADER_SRC_DIR}/${shader_src}"
        DEPENDS "${DXIL_SHADER_SRC_DIR}/${shader_src}" "${DXIL_SHADER_SRC_DIR}/common.hlsl"
        COMMENT "Compiling DXIL shader ${shader_src}"
        VERBATIM
    )
    list(APPEND DXIL_SHADER_OUTPUTS "${DXIL_SHADER_OUT_DIR}/${shader_out}")
endforeach()

add_custom_target(quak_dxil_shaders ALL DEPENDS ${DXIL_SHADER_OUTPUTS})

set(QUAK_GAMEPLAY_SOURCES
    src/game.c
    src/input.c
    src/interaction.c
    src/pond_objects.c
    src/duck_animation.c
    src/activities.c
)

add_executable(quak
    ${QUAK_GAMEPLAY_SOURCES}
    src/main.c
    src/render.c
    src/duck.c
    src/audio.c
)
add_dependencies(quak quak_dxil_shaders)
target_include_directories(quak SYSTEM PRIVATE "${SDL3_INCLUDE_DIR}")
target_include_directories(quak PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(quak PRIVATE "${SDL3_LIBRARY}")
target_compile_definitions(quak PRIVATE
    $<$<CONFIG:Debug>:QUAK_GPU_VALIDATION=1>
    $<$<NOT:$<CONFIG:Debug>>:QUAK_GPU_VALIDATION=0>
    QUAK_SHOW_DEBUG_GEOMETRY=$<BOOL:${QUAK_SHOW_DEBUG_GEOMETRY}>
    _CRT_SECURE_NO_WARNINGS
)
quak_enable_warnings(quak)

file(GLOB_RECURSE QUAK_ASSET_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")

add_custom_command(TARGET quak POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL3_LIB_DIR}/SDL3.dll" "$<TARGET_FILE_DIR:quak>"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:quak>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:quak>/assets"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:quak>/assets/shaders/dxil"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${DXIL_SHADER_OUTPUTS} "$<TARGET_FILE_DIR:quak>/assets/shaders/dxil"
    VERBATIM
)

# This target also restages assets when only an asset or shader changes and the
# executable does not need to relink.
add_custom_target(quak_runtime_assets ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:quak>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:quak>/assets"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:quak>/assets/shaders/dxil"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${DXIL_SHADER_OUTPUTS} "$<TARGET_FILE_DIR:quak>/assets/shaders/dxil"
    DEPENDS quak ${DXIL_SHADER_OUTPUTS} ${QUAK_ASSET_FILES}
    VERBATIM
)

include(CTest)
if(BUILD_TESTING)
    add_executable(quak_tests
        tests/test_game.c
        ${QUAK_GAMEPLAY_SOURCES}
    )
    target_include_directories(quak_tests SYSTEM PRIVATE "${SDL3_INCLUDE_DIR}")
    target_include_directories(quak_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(quak_tests PRIVATE "${SDL3_LIBRARY}")
    quak_enable_warnings(quak_tests)
    add_test(NAME quak_simulation COMMAND quak_tests)

    add_executable(quak_interaction_tests
        tests/test_interaction.c
        src/interaction.c
    )
    target_include_directories(quak_interaction_tests SYSTEM PRIVATE "${SDL3_INCLUDE_DIR}")
    target_include_directories(quak_interaction_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(quak_interaction_tests PRIVATE "${SDL3_LIBRARY}")
    quak_enable_warnings(quak_interaction_tests)
    add_test(NAME quak_interaction_queue COMMAND quak_interaction_tests)

    add_executable(quak_audio_tests
        tests/test_audio.c
        src/audio.c
    )
    target_include_directories(quak_audio_tests SYSTEM PRIVATE "${SDL3_INCLUDE_DIR}")
    target_include_directories(quak_audio_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(quak_audio_tests PRIVATE "${SDL3_LIBRARY}")
    quak_enable_warnings(quak_audio_tests)
    add_test(NAME quak_audio COMMAND quak_audio_tests)
    set_tests_properties(quak_audio PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy")
endif()
