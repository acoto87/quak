cmake_minimum_required(VERSION 3.10)
project(quak C)

option(QUAK_USE_MSVC "Use MSVC-compatible build settings and flags" OFF)

if(EXISTS "${CMAKE_BINARY_DIR}/CMakeCache.txt")
    file(READ "${CMAKE_BINARY_DIR}/CMakeCache.txt" _quak_cache)
    if(_quak_cache MATCHES "CMAKE_GENERATOR:INTERNAL=([^
]+)")
        set(_quak_prev_generator "${CMAKE_MATCH_1}")
        if(NOT _quak_prev_generator STREQUAL "${CMAKE_GENERATOR}")
            message(FATAL_ERROR
                "This build directory was configured with generator '${_quak_prev_generator}'. Remove '${CMAKE_BINARY_DIR}' before switching generators.")
        endif()
    endif()
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT DEFINED CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

if(QUAK_USE_MSVC)
    if(NOT MSVC)
        message(FATAL_ERROR "QUAK_USE_MSVC=ON requires an MSVC-compatible compiler (for example cl.exe).")
    endif()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT CMAKE_C_FLAGS MATCHES "-O")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
    endif()
elseif(MSVC)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT CMAKE_C_FLAGS MATCHES "/O")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /Zi")
    endif()
endif()

find_program(DXC_EXECUTABLE dxc)

# Define paths
set(SDL3_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/include")
set(SDL3_LIB_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/deps/lib")
set(DXIL_SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders/dxil")
set(DXIL_SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders/dxil")

if(DXC_EXECUTABLE)
    file(MAKE_DIRECTORY "${DXIL_SHADER_OUT_DIR}")

    set(DXIL_SHADER_TARGETS
        water.vert.hlsl:vs_6_0:water.vert.dxil
        water.frag.hlsl:ps_6_0:water.frag.dxil
        lit.vert.hlsl:vs_6_0:lit.vert.dxil
        lit.frag.hlsl:ps_6_0:lit.frag.dxil
        unlit.vert.hlsl:vs_6_0:unlit.vert.dxil
        unlit.frag.hlsl:ps_6_0:unlit.frag.dxil
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
            COMMAND "${DXC_EXECUTABLE}" -E main -T ${shader_profile}
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
endif()

# Add include directories
include_directories(${SDL3_INCLUDE_DIR})
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/src")

# Source files
add_executable(quak
    src/main.c
    src/render.c
    src/duck.c
    src/audio.c
    src/game.c
)

if(DXC_EXECUTABLE)
    add_dependencies(quak quak_dxil_shaders)
endif()

# Link SDL3
find_library(SDL3_LIBRARY
    NAMES SDL3 SDL3dll libSDL3.dll.a
    PATHS ${SDL3_LIB_DIR}
    NO_DEFAULT_PATH
)

if(WIN32)
    target_link_libraries(quak PRIVATE ${SDL3_LIBRARY})
else()
    target_link_libraries(quak PRIVATE ${SDL3_LIBRARY} m)
endif()

# Copy SDL3.dll next to the executable
add_custom_command(TARGET quak POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SDL3_LIB_DIR}/SDL3.dll"
        $<TARGET_FILE_DIR:quak>
)

# Copy assets folder next to the executable
add_custom_command(TARGET quak POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:quak>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:quak>/assets"
)
