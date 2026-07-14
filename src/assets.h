#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * relative_path is relative to the repository assets/ directory.
 *
 * Examples:
 *   "10602_Rubber_Duck_v1_L3.obj"
 *   "shaders/spirv/water.vert.spv"
 */
bool quak_get_asset_path(char *output, size_t output_size,
                         const char *relative_path);
