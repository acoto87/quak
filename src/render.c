#include "render.h"
#include "assets.h"
#include "duck.h"
#include <math.h>

#ifndef QUAK_GPU_VALIDATION
#define QUAK_GPU_VALIDATION 0
#endif

#ifndef QUAK_SHOW_DEBUG_GEOMETRY
#define QUAK_SHOW_DEBUG_GEOMETRY 0
#endif

typedef struct {
    mat4x4 u_proj;
    mat4x4 u_view;
} CameraUniform;

typedef struct {
    mat4x4 u_model;
    vec4   u_nmat[3];
} TransformUniform;

typedef struct {
    vec4 u_color;
} ColorUniform;

typedef struct {
    float u_alpha;
    float _pad[3];
} FragmentUniform;

typedef struct {
    vec4 u_time_pad;
    vec4 u_ripple_data[MAX_RIPPLES];
} WaterUniform;

typedef struct {
    vec4 u_environment;
} EnvironmentUniform;

typedef struct {
    SDL_GPUShader *vertex;
    SDL_GPUShader *fragment;
} ShaderPair;

typedef struct {
    SDL_GPUShaderFormat format;
    const char *directory;
    const char *suffix;
} ShaderAssetSpec;

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float size;
    float red, green, blue, alpha;
    float life_ratio;
    float depth;
    ParticleKind kind;
} RenderBillboard;

static const ShaderAssetSpec DXIL_SHADER_ASSETS = {
    SDL_GPU_SHADERFORMAT_DXIL, "dxil", ".dxil"
};

static const ShaderAssetSpec SPIRV_SHADER_ASSETS = {
    SDL_GPU_SHADERFORMAT_SPIRV, "spirv", ".spv"
};

#if QUAK_SHOW_DEBUG_GEOMETRY
static const float AXIS_VERTS[] = {
    0.f, 0.f, 0.f,
    20.f, 0.f, 0.f,
    0.f, 0.f, 0.f,
    0.f, 20.f, 0.f,
    0.f, 0.f, 0.f,
    0.f, 0.f, 20.f
};
#endif

static void fill_nmat_columns(vec4 *cols, const float *nmat9)
{
    SDL_zerop(&cols[0]);
    SDL_zerop(&cols[1]);
    SDL_zerop(&cols[2]);
    cols[0][0] = nmat9[0]; cols[0][1] = nmat9[1]; cols[0][2] = nmat9[2];
    cols[1][0] = nmat9[3]; cols[1][1] = nmat9[4]; cols[1][2] = nmat9[5];
    cols[2][0] = nmat9[6]; cols[2][1] = nmat9[7]; cols[2][2] = nmat9[8];
}

void compute_nmat(float *nmat9, mat4x4 model)
{
    mat4x4 inv;
    mat4x4_invert(inv, model);
    for (int c = 0; c < 3; c++)
        for (int r = 0; r < 3; r++)
            nmat9[c * 3 + r] = inv[r][c];
}

static void gen_water_grid(float **vout, int *vcount,
                            unsigned int **iout, int *icount)
{
    int N = 61;
    float extent = WORLD_BOUND;
    *vcount = N * N;
    float *v = (float *)SDL_malloc((size_t)(N * N * 5) * sizeof(float));
    if (!v) {
        *vout = NULL;
        *iout = NULL;
        *vcount = 0;
        *icount = 0;
        return;
    }
    for (int zi = 0; zi < N; zi++) {
        for (int xi = 0; xi < N; xi++) {
            float x = -extent + (float)xi * (extent * 2.f) / (float)(N - 1);
            float z = -extent + (float)zi * (extent * 2.f) / (float)(N - 1);
            int k = (zi * N + xi) * 5;
            v[k+0] = x;  v[k+1] = 0.f;  v[k+2] = z;
            v[k+3] = (float)xi / (float)(N - 1);
            v[k+4] = (float)zi / (float)(N - 1);
        }
    }
    *vout = v;

    int Q = N - 1;

    *icount = Q * Q * 6;
    unsigned int *id =
        (unsigned int *)SDL_malloc((size_t)(*icount) * sizeof(unsigned int));
    if (!id) {
        SDL_free(v);
        *vout = NULL;
        *iout = NULL;
        *vcount = 0;
        *icount = 0;
        return;
    }
    int k = 0;
    for (int zi = 0; zi < Q; zi++) {
        for (int xi = 0; xi < Q; xi++) {
            unsigned int tl = (unsigned int)(zi * N + xi);
            unsigned int tr = tl + 1;
            unsigned int bl = (unsigned int)((zi + 1) * N + xi);
            unsigned int br = bl + 1;
            id[k++] = tl;  id[k++] = bl;  id[k++] = tr;
            id[k++] = tr;  id[k++] = bl;  id[k++] = br;
        }
    }
    *iout = id;
}

static int gen_disc_flat(float **out)
{
    int N = 24;
    float *buf = (float *)SDL_malloc((size_t)(N * 3 * 6) * sizeof(float));
    if (!buf) {
        *out = NULL;
        return 0;
    }
    int vi = 0;
    for (int i = 0; i < N; i++) {
        float a0 = (float)i     / N * 2.f * 3.14159265f;
        float a1 = (float)(i+1) / N * 2.f * 3.14159265f;
        float pts[3][3] = {
            {0.f, 0.f, 0.f},
            {cosf(a0), 0.f, sinf(a0)},
            {cosf(a1), 0.f, sinf(a1)}
        };
        for (int k = 0; k < 3; k++) {
            buf[vi++] = pts[k][0];  buf[vi++] = pts[k][1];  buf[vi++] = pts[k][2];
            buf[vi++] = 0.f;        buf[vi++] = 1.f;        buf[vi++] = 0.f;
        }
    }
    *out = buf;
    return N * 3;
}

static const ShaderAssetSpec *get_shader_asset_spec(SDL_GPUShaderFormat format)
{
    if (format == SDL_GPU_SHADERFORMAT_DXIL)
        return &DXIL_SHADER_ASSETS;

    if (format == SDL_GPU_SHADERFORMAT_SPIRV)
        return &SPIRV_SHADER_ASSETS;

    SDL_Log("Unsupported packaged shader format: 0x%x", (unsigned int)format);
    return NULL;
}

static SDL_GPUShaderFormat required_shader_format(void)
{
#if defined(__ANDROID__)
    return SDL_GPU_SHADERFORMAT_SPIRV;
#else
    return SDL_GPU_SHADERFORMAT_DXIL;
#endif
}

static void *load_shader_asset(SDL_GPUShaderFormat format, const char *shader_name,
                               SDL_GPUShaderStage stage, size_t *code_size)
{
    const ShaderAssetSpec *spec = get_shader_asset_spec(format);
    const char *stage_name = NULL;
    char relative_path[256];
    char resolved_path[512];
    int written;

    if (!spec || !shader_name || !code_size)
        return NULL;

    if (stage == SDL_GPU_SHADERSTAGE_VERTEX)
        stage_name = "vert";
    else if (stage == SDL_GPU_SHADERSTAGE_FRAGMENT)
        stage_name = "frag";
    else {
        SDL_Log("Unsupported shader stage %u for '%s'", (unsigned int)stage, shader_name);
        return NULL;
    }

    written = SDL_snprintf(relative_path, sizeof(relative_path),
                           "shaders/%s/%s.%s%s",
                           spec->directory, shader_name, stage_name, spec->suffix);
    if (written < 0 || (size_t)written >= sizeof(relative_path)) {
        SDL_Log("Shader path is too long for '%s'", shader_name);
        return NULL;
    }

    if (!quak_get_asset_path(resolved_path, sizeof(resolved_path), relative_path))
        return NULL;

    void *code = SDL_LoadFile(resolved_path, code_size);
    if (!code) {
        SDL_Log("Could not load shader '%s': %s", resolved_path, SDL_GetError());
        return NULL;
    }

    return code;
}

static ShaderPair load_shader_pair(AppState *as, const char *base_name,
                                   Uint32 vertex_uniforms,
                                   Uint32 fragment_uniforms,
                                   Uint32 fragment_samplers)
{
    ShaderPair pair = {0};
    size_t vert_size = 0, frag_size = 0;
    Uint8 *vert_code = NULL;
    Uint8 *frag_code = NULL;

    vert_code = (Uint8 *)load_shader_asset(as->shader_format, base_name,
                                           SDL_GPU_SHADERSTAGE_VERTEX, &vert_size);
    frag_code = (Uint8 *)load_shader_asset(as->shader_format, base_name,
                                           SDL_GPU_SHADERSTAGE_FRAGMENT, &frag_size);
    if (!vert_code || !frag_code) {
        SDL_free(vert_code);
        SDL_free(frag_code);
        return pair;
    }

    pair.vertex = SDL_CreateGPUShader(as->gpu,
                                      &(SDL_GPUShaderCreateInfo){
                                           .code_size = vert_size,
                                           .code = vert_code,
                                           .entrypoint = "main",
                                           .format = as->shader_format,
                                           .stage = SDL_GPU_SHADERSTAGE_VERTEX,
                                           .num_samplers = 0,
                                           .num_storage_textures = 0,
                                          .num_storage_buffers = 0,
                                          .num_uniform_buffers = vertex_uniforms,
                                          .props = 0
                                      });
    pair.fragment = SDL_CreateGPUShader(as->gpu,
                                        &(SDL_GPUShaderCreateInfo){
                                             .code_size = frag_size,
                                             .code = frag_code,
                                             .entrypoint = "main",
                                             .format = as->shader_format,
                                             .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
                                             .num_samplers = fragment_samplers,
                                             .num_storage_textures = 0,
                                            .num_storage_buffers = 0,
                                            .num_uniform_buffers = fragment_uniforms,
                                            .props = 0
                                        });
    SDL_free(vert_code);
    SDL_free(frag_code);

    if (!pair.vertex || !pair.fragment) {
        SDL_Log("CreateGPUShader failed for %s: %s", base_name, SDL_GetError());
        if (pair.vertex) SDL_ReleaseGPUShader(as->gpu, pair.vertex);
        if (pair.fragment) SDL_ReleaseGPUShader(as->gpu, pair.fragment);
        pair.vertex = NULL;
        pair.fragment = NULL;
    }

    return pair;
}

ShaderPair render_get_shader_pair(AppState *as, const char *base_name)
{
    if (SDL_strcmp(base_name, "water") == 0) return load_shader_pair(as, base_name, 2, 2, 0);
    if (SDL_strcmp(base_name, "lit") == 0)   return load_shader_pair(as, base_name, 4, 1, 0);
    if (SDL_strcmp(base_name, "unlit") == 0) return load_shader_pair(as, base_name, 2, 0, 0);
    if (SDL_strcmp(base_name, "shadow") == 0) return load_shader_pair(as, base_name, 2, 1, 0);
    if (SDL_strcmp(base_name, "particle") == 0) return load_shader_pair(as, base_name, 1, 0, 0);
    if (SDL_strcmp(base_name, "tex") == 0)   return load_shader_pair(as, base_name, 2, 2, 1);
    return (ShaderPair){0};
}

static bool create_buffer_from_data(AppState *as, Uint32 usage, const void *data,
                                    Uint32 size, SDL_GPUBuffer **out_buffer)
{
    SDL_GPUBuffer *buffer = SDL_CreateGPUBuffer(as->gpu,
                                                &(SDL_GPUBufferCreateInfo){
                                                    .usage = usage,
                                                    .size = size,
                                                    .props = 0
                                                });
    SDL_GPUTransferBuffer *transfer;
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUCopyPass *copy;
    void *mapped;

    if (!buffer) {
        SDL_Log("CreateGPUBuffer failed: %s", SDL_GetError());
        return false;
    }

    transfer = SDL_CreateGPUTransferBuffer(as->gpu,
                                           &(SDL_GPUTransferBufferCreateInfo){
                                               .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                                               .size = size,
                                               .props = 0
                                           });
    if (!transfer) {
        SDL_Log("CreateGPUTransferBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUBuffer(as->gpu, buffer);
        return false;
    }

    mapped = SDL_MapGPUTransferBuffer(as->gpu, transfer, false);
    if (!mapped) {
        SDL_Log("MapGPUTransferBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(as->gpu, transfer);
        SDL_ReleaseGPUBuffer(as->gpu, buffer);
        return false;
    }
    SDL_memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(as->gpu, transfer);

    cmd = SDL_AcquireGPUCommandBuffer(as->gpu);
    if (!cmd) {
        SDL_Log("AcquireGPUCommandBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(as->gpu, transfer);
        SDL_ReleaseGPUBuffer(as->gpu, buffer);
        return false;
    }

    copy = SDL_BeginGPUCopyPass(cmd);
    SDL_UploadToGPUBuffer(copy,
                          &(SDL_GPUTransferBufferLocation){ transfer, 0 },
                          &(SDL_GPUBufferRegion){ buffer, 0, size },
                          false);
    SDL_EndGPUCopyPass(copy);

    if (!SDL_SubmitGPUCommandBuffer(cmd) || !SDL_WaitForGPUIdle(as->gpu)) {
        SDL_Log("GPU upload failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(as->gpu, transfer);
        SDL_ReleaseGPUBuffer(as->gpu, buffer);
        return false;
    }

    SDL_ReleaseGPUTransferBuffer(as->gpu, transfer);
    *out_buffer = buffer;
    return true;
}

static bool ensure_staging_capacity(AppState *as, Uint32 size)
{
    if (as->staging_buffer && as->staging_size >= size)
        return true;

    if (as->staging_buffer) {
        SDL_ReleaseGPUTransferBuffer(as->gpu, as->staging_buffer);
        as->staging_buffer = NULL;
        as->staging_size = 0;
    }

    as->staging_buffer = SDL_CreateGPUTransferBuffer(as->gpu,
                                                     &(SDL_GPUTransferBufferCreateInfo){
                                                         .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                                                         .size = size,
                                                         .props = 0
                                                     });
    if (!as->staging_buffer) {
        SDL_Log("CreateGPUTransferBuffer failed: %s", SDL_GetError());
        return false;
    }

    as->staging_size = size;
    return true;
}

static bool upload_staging_to_copy_pass(AppState *as, SDL_GPUCopyPass *copy,
                                        SDL_GPUBuffer *dst, const void *data, Uint32 size)
{
    if (!ensure_staging_capacity(as, size))
        return false;

    void *mapped = SDL_MapGPUTransferBuffer(as->gpu, as->staging_buffer, true);
    if (!mapped) {
        SDL_Log("MapGPUTransferBuffer failed: %s", SDL_GetError());
        return false;
    }
    SDL_memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(as->gpu, as->staging_buffer);

    if (!copy) {
        SDL_Log("Copy pass is null");
        return false;
    }

    SDL_UploadToGPUBuffer(copy,
                          &(SDL_GPUTransferBufferLocation){ as->staging_buffer, 0 },
                          &(SDL_GPUBufferRegion){ dst, 0, size },
                          true);
    return true;
}

static bool prepare_ripple_vertices(AppState *as, SDL_GPUCopyPass *copy)
{
    const int ring_stride = (RING_SEGS + 1) * 3;
    float *rings = as->ring_vertex_scratch;

    Uint32 bytes = 0;
    int active_index = 0;

    if (!ensure_staging_capacity(as, (Uint32)(MAX_RIPPLES * ring_stride * sizeof(float)))) {
        return false;
    }

    for (int i = 0; i < MAX_RIPPLES; i++) {
        as->ring_draw_offsets[i] = 0;
        as->ring_draw_counts[i] = 0;
    }

    for (int i = 0; i < MAX_RIPPLES; i++) {
        const Ripple *r = &as->ripples[i];
        if (!r->active)
            continue;

        float *ring = rings + (size_t)active_index * ring_stride;

        for (int j = 0; j <= RING_SEGS; j++) {
            float a = (float)(j % RING_SEGS) / RING_SEGS * 2.f * 3.14159265f;
            ring[j * 3 + 0] = r->x + r->radius * cosf(a);
            ring[j * 3 + 1] = 0.08f;
            ring[j * 3 + 2] = r->z + r->radius * sinf(a);
        }

        as->ring_draw_offsets[i] = active_index * (RING_SEGS + 1);
        as->ring_draw_counts[i] = RING_SEGS + 1;
        active_index++;
    }

    bytes = (Uint32)(active_index * ring_stride * (int)sizeof(float));
    bool ok = true;
    if (bytes > 0)
        ok = upload_staging_to_copy_pass(as, copy, as->ring_vbuf, rings, bytes);

    return ok;
}

static bool prepare_particle_vertices(AppState *as, SDL_GPUCopyPass *copy)
{
    float *verts = as->particle_vertex_scratch;
    const float camera_right[3] = {
        as->view[0][0], as->view[1][0], as->view[2][0]
    };
    const float camera_up[3] = {
        as->view[0][1], as->view[1][1], as->view[2][1]
    };
    RenderBillboard billboards[MAX_RENDER_BILLBOARDS];
    int count = 0;

    for (int i = 0; i < MAX_PARTICLES; i++) {
        const Particle *p = &as->particles[i];
        if (!p->active)
            continue;
        billboards[count++] = (RenderBillboard){
            .x = p->x, .y = p->y, .z = p->z,
            .vx = p->vx, .vy = p->vy, .vz = p->vz,
            .size = p->size,
            .red = p->red, .green = p->green, .blue = p->blue,
            .alpha = p->alpha,
            .life_ratio = p->max_life > 0.f
                        ? SDL_clamp(p->life / p->max_life, 0.f, 1.f) : 0.f,
            .kind = p->kind
        };
    }
    for (int i = 0; i < MAX_POND_OBJECTS && count < MAX_RENDER_BILLBOARDS; i++) {
        const PondObject *object = &as->pond_objects.objects[i];
        if (!pond_object_is_visible(object)
            || (object->kind != POND_OBJECT_POPPABLE_BUBBLE
                && object->kind != POND_OBJECT_FIREFLY))
            continue;
        float bob = sinf(object->phase * 2.f) * 0.05f;
        billboards[count++] = (RenderBillboard){
            .x = object->x, .y = object->y + bob, .z = object->z,
            .vx = object->vx, .vy = object->vy, .vz = object->vz,
            .size = object->radius,
            .red = object->red, .green = object->green, .blue = object->blue,
            .alpha = object->alpha,
            .life_ratio = 1.f,
            .kind = object->kind == POND_OBJECT_FIREFLY
                  ? PARTICLE_FIREFLY : PARTICLE_BUBBLE
        };
    }

    for (int i = 0; i < count; i++) {
        billboards[i].depth = as->view[0][2] * billboards[i].x
                            + as->view[1][2] * billboards[i].y
                            + as->view[2][2] * billboards[i].z
                            + as->view[3][2];
        RenderBillboard current = billboards[i];
        int insertion = i;
        while (insertion > 0 && billboards[insertion - 1].depth < current.depth) {
            billboards[insertion] = billboards[insertion - 1];
            insertion--;
        }
        billboards[insertion] = current;
    }

    as->part_vert_count = 0;
    if (count == 0)
        return true;

    int vi = 0;
    for (int order = 0; order < count; order++) {
        const RenderBillboard *p = &billboards[order];
        float life_ratio = p->life_ratio;
        float screen_vx = p->vx * camera_right[0]
                        + p->vy * camera_right[1]
                        + p->vz * camera_right[2];
        float screen_vy = p->vx * camera_up[0]
                        + p->vy * camera_up[1]
                        + p->vz * camera_up[2];
        float screen_speed = sqrtf(screen_vx * screen_vx + screen_vy * screen_vy);
        bool round = p->kind == PARTICLE_BUBBLE
                  || p->kind == PARTICLE_SLEEP_BUBBLE
                  || p->kind == PARTICLE_FIREFLY;
        float axis_x = 0.f;
        float axis_y = 1.f;
        if (!round && screen_speed > 0.05f) {
            axis_x = screen_vx / screen_speed;
            axis_y = screen_vy / screen_speed;
        }

        float long_dir[3];
        float side_dir[3];
        for (int component = 0; component < 3; component++) {
            long_dir[component] = camera_right[component] * axis_x
                                + camera_up[component] * axis_y;
            side_dir[component] = camera_right[component] * axis_y
                                - camera_up[component] * axis_x;
        }

        float speed = sqrtf(p->vx * p->vx + p->vy * p->vy + p->vz * p->vz);
        float half_width = p->size * (0.65f + 0.35f * life_ratio);
        float stretch = round ? 1.f : 1.f + SDL_min(speed * 0.20f, 1.8f);
        float half_length = half_width * stretch;
        float color[4] = {p->red, p->green, p->blue, p->alpha * life_ratio};
        static const float corners[6][4] = {
            {-1.f,  1.f, 0.f, 1.f}, { 1.f,  1.f, 1.f, 1.f},
            { 1.f, -1.f, 1.f, 0.f}, {-1.f,  1.f, 0.f, 1.f},
            { 1.f, -1.f, 1.f, 0.f}, {-1.f, -1.f, 0.f, 0.f}
        };
        for (int q = 0; q < 6; q++) {
            float side = corners[q][0] * half_width;
            float along = corners[q][1] * half_length;
            verts[vi++] = p->x + side_dir[0] * side + long_dir[0] * along;
            verts[vi++] = p->y + side_dir[1] * side + long_dir[1] * along;
            verts[vi++] = p->z + side_dir[2] * side + long_dir[2] * along;
            verts[vi++] = corners[q][2];
            verts[vi++] = corners[q][3];
            verts[vi++] = color[0];
            verts[vi++] = color[1];
            verts[vi++] = color[2];
            verts[vi++] = color[3];
        }
    }

    Uint32 bytes = (Uint32)(vi * sizeof(float));
    bool ok = bytes > 0 && upload_staging_to_copy_pass(as, copy, as->part_vbuf, verts, bytes);
    as->part_vert_count = vi / 9;
    return ok;
}

bool render_prepare_frame(AppState *as, SDL_GPUCommandBuffer *cmd)
{
    SDL_GPUCopyPass *copy;

    if (!as || !cmd)
        return false;

    copy = SDL_BeginGPUCopyPass(cmd);
    if (!copy) {
        SDL_Log("BeginGPUCopyPass failed: %s", SDL_GetError());
        return false;
    }

    bool ok = prepare_ripple_vertices(as, copy) && prepare_particle_vertices(as, copy);
    SDL_EndGPUCopyPass(copy);
    return ok;
}

static bool create_depth_texture(AppState *as, Uint32 width, Uint32 height)
{
    SDL_GPUTextureFormat candidates[] = {
        SDL_GPU_TEXTUREFORMAT_D24_UNORM,
        SDL_GPU_TEXTUREFORMAT_D32_FLOAT
    };

    if (as->depth_tex) {
        SDL_ReleaseGPUTexture(as->gpu, as->depth_tex);
        as->depth_tex = NULL;
    }

    for (int i = 0; i < 2; i++) {
        SDL_GPUTextureFormat fmt = candidates[i];
        if (!SDL_GPUTextureSupportsFormat(as->gpu, fmt, SDL_GPU_TEXTURETYPE_2D,
                                          SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
            continue;

        as->depth_tex = SDL_CreateGPUTexture(as->gpu,
                                             &(SDL_GPUTextureCreateInfo){
                                                 .type = SDL_GPU_TEXTURETYPE_2D,
                                                 .format = fmt,
                                                 .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
                                                 .width = width,
                                                 .height = height,
                                                 .layer_count_or_depth = 1,
                                                 .num_levels = 1,
                                                 .sample_count = SDL_GPU_SAMPLECOUNT_1,
                                                 .props = 0
                                             });
        if (as->depth_tex) {
            as->depth_format = fmt;
            as->depth_w = width;
            as->depth_h = height;
            return true;
        }
    }

    SDL_Log("Failed to create depth texture: %s", SDL_GetError());
    return false;
}

static SDL_GPUGraphicsPipeline *create_pipeline(AppState *as,
                                                ShaderPair pair,
                                                const SDL_GPUVertexBufferDescription *vb_desc,
                                                Uint32 num_vb,
                                                const SDL_GPUVertexAttribute *attrs,
                                                 Uint32 num_attrs,
                                                 SDL_GPUPrimitiveType prim,
                                                 SDL_GPUCullMode cull_mode,
                                                 bool enable_blend,
                                                 bool enable_depth_write,
                                                 SDL_GPUCompareOp depth_op,
                                                 bool enable_depth_test)
{
    if (!pair.vertex || !pair.fragment) {
        SDL_Log("Cannot create graphics pipeline without both shader stages");
        if (pair.vertex) SDL_ReleaseGPUShader(as->gpu, pair.vertex);
        if (pair.fragment) SDL_ReleaseGPUShader(as->gpu, pair.fragment);
        return NULL;
    }

    SDL_GPUColorTargetBlendState blend = {
        .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
        .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        .color_blend_op = SDL_GPU_BLENDOP_ADD,
        .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
        .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
        .color_write_mask = 0,
        .enable_blend = enable_blend,
        .enable_color_write_mask = false
    };
    SDL_GPUColorTargetDescription color_desc = {
        .format = as->swapchain_format,
        .blend_state = blend
    };
    SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(as->gpu,
        &(SDL_GPUGraphicsPipelineCreateInfo){
            .vertex_shader = pair.vertex,
            .fragment_shader = pair.fragment,
            .vertex_input_state = {
                .vertex_buffer_descriptions = vb_desc,
                .num_vertex_buffers = num_vb,
                .vertex_attributes = attrs,
                .num_vertex_attributes = num_attrs
            },
            .primitive_type = prim,
            .rasterizer_state = {
                .fill_mode = SDL_GPU_FILLMODE_FILL,
                .cull_mode = cull_mode,
                .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
                .depth_bias_constant_factor = 0.f,
                .depth_bias_clamp = 0.f,
                .depth_bias_slope_factor = 0.f,
                .enable_depth_bias = false,
                .enable_depth_clip = true
            },
            .multisample_state = {
                .sample_count = SDL_GPU_SAMPLECOUNT_1,
                .sample_mask = 0,
                .enable_mask = false,
                .enable_alpha_to_coverage = false
            },
            .depth_stencil_state = {
                .compare_op = depth_op,
                .back_stencil_state = {0},
                .front_stencil_state = {0},
                .compare_mask = 0,
                .write_mask = 0,
                .enable_depth_test = enable_depth_test,
                .enable_depth_write = enable_depth_write,
                .enable_stencil_test = false
            },
            .target_info = {
                .color_target_descriptions = &color_desc,
                .num_color_targets = 1,
                .depth_stencil_format = as->depth_format,
                .has_depth_stencil_target = true
            },
            .props = 0
        });

    SDL_ReleaseGPUShader(as->gpu, pair.vertex);
    SDL_ReleaseGPUShader(as->gpu, pair.fragment);
    if (!pipeline)
        SDL_Log("SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
    return pipeline;
}

static void make_look_at_lh(mat4x4 view, const vec3 eye, const vec3 target, const vec3 up)
{
    vec3 forward;
    vec3 right;
    vec3 camera_up;

    vec3_sub(forward, target, eye);
    vec3_norm(forward, forward);
    vec3_mul_cross(right, up, forward);
    vec3_norm(right, right);
    vec3_mul_cross(camera_up, forward, right);

    mat4x4_identity(view);
    view[0][0] = right[0];     view[0][1] = camera_up[0]; view[0][2] = forward[0];
    view[1][0] = right[1];     view[1][1] = camera_up[1]; view[1][2] = forward[1];
    view[2][0] = right[2];     view[2][1] = camera_up[2]; view[2][2] = forward[2];
    view[3][0] = -vec3_mul_inner(right, eye);
    view[3][1] = -vec3_mul_inner(camera_up, eye);
    view[3][2] = -vec3_mul_inner(forward, eye);
}

static void make_perspective_lh_zo(mat4x4 projection, float fov_y, float aspect,
                                   float near_z, float far_z)
{
    float y_scale = 1.f / tanf(fov_y * 0.5f);
    SDL_memset(projection, 0, sizeof(mat4x4));
    projection[0][0] = y_scale / aspect;
    projection[1][1] = y_scale;
    projection[2][2] = far_z / (far_z - near_z);
    projection[2][3] = 1.f;
    projection[3][2] = -(near_z * far_z) / (far_z - near_z);
}

static void update_camera_view(AppState *as)
{
    float pitch = as->camera_pitch;
    float yaw = as->camera_yaw;
    float dist = as->camera_distance;

    render_update_picking_view(as);

    float shake_x = 0.f;
    float shake_y = 0.f;
    if (as->camera_shake_t > 0.f) {
        float t = as->camera_shake_t / CAMERA_SHAKE_DURATION;
        float mag = as->camera_shake_mag * (t * t) * (3.f - 2.f * t);
        shake_x = (SDL_randf_r(&as->presentation_rng) * 2.f - 1.f) * mag;
        shake_y = (SDL_randf_r(&as->presentation_rng) * 2.f - 1.f) * mag;
    }

    float eye_x = as->camera_target[0] + dist * cosf(pitch) * sinf(yaw) + shake_x;
    float eye_y = as->camera_target[1] + dist * sinf(pitch) + shake_y;
    float eye_z = as->camera_target[2] - dist * cosf(pitch) * cosf(yaw);

    vec3 eye = { eye_x, eye_y, eye_z };
    vec3 up = { 0.f, 1.f, 0.f };
    make_look_at_lh(as->view, eye, as->camera_target, up);
}

void render_update_picking_view(AppState *as)
{
    float pitch = as->camera_pitch;
    float yaw = as->camera_yaw;
    float dist = as->camera_distance;
    vec3 eye = {
        as->camera_target[0] + dist * cosf(pitch) * sinf(yaw),
        as->camera_target[1] + dist * sinf(pitch),
        as->camera_target[2] - dist * cosf(pitch) * cosf(yaw)
    };
    vec3 up = {0.f, 1.f, 0.f};
    make_look_at_lh(as->picking_view, eye, as->camera_target, up);
}

void render_push_camera_uniform(SDL_GPUCommandBuffer *cmd, const AppState *as)
{
    CameraUniform camera;
    SDL_memcpy(camera.u_proj, as->proj, sizeof(mat4x4));
    SDL_memcpy(camera.u_view, as->view, sizeof(mat4x4));
    SDL_PushGPUVertexUniformData(cmd, 0, &camera, (Uint32)sizeof(camera));
}

static void draw_lily_pads(AppState *as, SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass)
{
    SDL_BindGPUGraphicsPipeline(pass, as->lit_pipeline);
    SDL_BindGPUVertexBuffers(pass, 0, &(SDL_GPUBufferBinding){ as->disc_vbuf, 0 }, 1);

    for (int i = 0; i < MAX_POND_OBJECTS; i++) {
        const PondObject *object = &as->pond_objects.objects[i];
        if (!pond_object_is_visible(object) || object->kind != POND_OBJECT_LILY_PAD)
            continue;

        float scale = object->radius * object->scale;
        if (scale <= 0.f)
            continue;

        mat4x4 model;
        float nmat[9];
        TransformUniform transform = {0};
        ColorUniform color = {{object->red, object->green, object->blue, 1.f}};
        FragmentUniform alpha = {.u_alpha = object->alpha};
        EnvironmentUniform environment = {{as->environment.day_mix, 0.f, 0.f, 0.f}};

        mat4x4_identity(model);
        mat4x4_translate_in_place(model, object->x,
                                 object->y + object->lily_motion.height_offset,
                                 object->z);
        mat4x4_rotate_Y(model, model, object->rotation);
        mat4x4_rotate_X(model, model, object->lily_motion.tilt);
        mat4x4_scale_aniso(model, model, scale, 1.f, scale);
        compute_nmat(nmat, model);
        SDL_memcpy(transform.u_model, model, sizeof(mat4x4));
        fill_nmat_columns(transform.u_nmat, nmat);

        SDL_PushGPUVertexUniformData(cmd, 1, &transform, (Uint32)sizeof(transform));
        SDL_PushGPUVertexUniformData(cmd, 2, &color, (Uint32)sizeof(color));
        SDL_PushGPUVertexUniformData(cmd, 3, &environment,
                                     (Uint32)sizeof(environment));
        SDL_PushGPUFragmentUniformData(cmd, 0, &alpha, (Uint32)sizeof(alpha));
        SDL_DrawGPUPrimitives(pass, (Uint32)as->disc_vert_count, 1, 0, 0);
    }
}

static void draw_duck_shadow(AppState *as, SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass)
{
    mat4x4 shadow;
    TransformUniform transform = {0};
    float duck_height = as->duck_y_offset + as->duck_animation.body_y_offset;
    float shadow_alpha = SHADOW_ALPHA * (1.f - duck_height / DUCK_BOB_AMPLITUDE * 0.3f);
    shadow_alpha *= 0.3f + as->environment.day_mix * 0.7f;
    FragmentUniform opacity = {
        .u_alpha = SDL_clamp(shadow_alpha, 0.08f, SHADOW_ALPHA)
    };

    mat4x4_identity(shadow);
    mat4x4_translate_in_place(shadow, as->duck_x, SHADOW_HEIGHT, as->duck_z);
    mat4x4_rotate_Y(shadow, shadow,
                    -(as->duck_angle + as->duck_animation.spin_angle));
    mat4x4_scale_aniso(shadow, shadow,
                       SHADOW_SCALE_X * as->duck_animation.body_scale_x,
                       1.f,
                       SHADOW_SCALE_Z * as->duck_animation.body_scale_z);
    SDL_memcpy(transform.u_model, shadow, sizeof(mat4x4));

    SDL_BindGPUGraphicsPipeline(pass, as->shadow_pipeline);
    SDL_BindGPUVertexBuffers(pass, 0, &(SDL_GPUBufferBinding){ as->disc_vbuf, 0 }, 1);
    SDL_PushGPUVertexUniformData(cmd, 1, &transform, (Uint32)sizeof(transform));
    SDL_PushGPUFragmentUniformData(cmd, 0, &opacity, (Uint32)sizeof(opacity));
    SDL_DrawGPUPrimitives(pass, (Uint32)as->disc_vert_count, 1, 0, 0);
}

#if QUAK_SHOW_DEBUG_GEOMETRY
static void build_grid_vertices(float *verts)
{
    static const float GRID_STEP = 2.0f;
    static const int GRID_LINES = 161;
    int vi = 0;
    float half = GRID_STEP * (float)(GRID_LINES / 2);

    for (int i = 0; i < GRID_LINES; i++) {
        float t = (float)(i - (GRID_LINES / 2)) * GRID_STEP;
        verts[vi++] = -half; verts[vi++] = -0.002f; verts[vi++] = t;
        verts[vi++] =  half; verts[vi++] = -0.002f; verts[vi++] = t;
        verts[vi++] = t; verts[vi++] = -0.002f; verts[vi++] = -half;
        verts[vi++] = t; verts[vi++] = -0.002f; verts[vi++] =  half;
    }
}

static void draw_grid(SDL_GPUCommandBuffer *cmd, AppState *as, SDL_GPURenderPass *pass)
{
    ColorUniform color = {{0.30f, 0.45f, 0.65f, 0.60f}};

    SDL_BindGPUGraphicsPipeline(pass, as->unlit_pipeline);
    SDL_PushGPUVertexUniformData(cmd, 1, &color, (Uint32)sizeof(color));
    SDL_BindGPUVertexBuffers(pass, 0, &(SDL_GPUBufferBinding){ as->grid_vbuf, 0 }, 1);

    for (int i = 0; i < as->grid_vert_count / 2; i++) {
        SDL_DrawGPUPrimitives(pass, 2, 1, (Uint32)(i * 2), 0);
    }
}

static void draw_axes(SDL_GPUCommandBuffer *cmd, AppState *as, SDL_GPURenderPass *pass)
{
    SDL_BindGPUGraphicsPipeline(pass, as->unlit_pipeline);
    SDL_BindGPUVertexBuffers(pass, 0, &(SDL_GPUBufferBinding){ as->axis_vbuf, 0 }, 1);

    ColorUniform color = {{1.f, 0.f, 0.f, 1.f}};
    SDL_PushGPUVertexUniformData(cmd, 1, &color, (Uint32)sizeof(color));
    SDL_DrawGPUPrimitives(pass, 2, 1, 0, 0);

    color = (ColorUniform){{0.f, 1.f, 0.f, 1.f}};
    SDL_PushGPUVertexUniformData(cmd, 1, &color, (Uint32)sizeof(color));
    SDL_DrawGPUPrimitives(pass, 2, 1, 2, 0);

    color = (ColorUniform){{0.f, 0.4f, 1.f, 1.f}};
    SDL_PushGPUVertexUniformData(cmd, 1, &color, (Uint32)sizeof(color));
    SDL_DrawGPUPrimitives(pass, 2, 1, 4, 0);
}
#endif

static void draw_water(SDL_GPUCommandBuffer *cmd, AppState *as, SDL_GPURenderPass *pass)
{
    WaterUniform water = {0};
    EnvironmentUniform environment = {{as->environment.day_mix, 0.f, 0.f, 0.f}};
    water.u_time_pad[0] = as->elapsed;
    for (int i = 0; i < MAX_RIPPLES; i++) {
        water.u_ripple_data[i][0] = as->ripples[i].active ? as->ripples[i].x : 0.f;
        water.u_ripple_data[i][1] = as->ripples[i].active ? as->ripples[i].z : 0.f;
        water.u_ripple_data[i][2] = as->ripples[i].active ? as->ripples[i].radius : -999.f;
        water.u_ripple_data[i][3] = as->ripples[i].active ? as->ripples[i].alpha : 0.f;
    }

    SDL_BindGPUGraphicsPipeline(pass, as->water_pipeline);
    SDL_PushGPUVertexUniformData(cmd, 1, &water, (Uint32)sizeof(water));
    SDL_PushGPUFragmentUniformData(cmd, 0, &water.u_time_pad, (Uint32)sizeof(vec4));
    SDL_PushGPUFragmentUniformData(cmd, 1, &environment,
                                   (Uint32)sizeof(environment));
    SDL_BindGPUVertexBuffers(pass, 0, &(SDL_GPUBufferBinding){ as->water_vbuf, 0 }, 1);
    SDL_BindGPUIndexBuffer(pass, &(SDL_GPUBufferBinding){ as->water_ibuf, 0 },
                           SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_DrawGPUIndexedPrimitives(pass, (Uint32)as->water_idx_count, 1, 0, 0, 0);
}

static void draw_ripples(SDL_GPUCommandBuffer *cmd, AppState *as, SDL_GPURenderPass *pass)
{
    SDL_BindGPUGraphicsPipeline(pass, as->unlit_pipeline);
    SDL_BindGPUVertexBuffers(pass, 0, &(SDL_GPUBufferBinding){ as->ring_vbuf, 0 }, 1);

    for (int i = 0; i < MAX_RIPPLES; i++) {
        const Ripple *r = &as->ripples[i];
        if (!r->active || as->ring_draw_counts[i] <= 0) continue;

        ColorUniform color = {{1.f, 1.f, 1.f, r->alpha * 0.85f}};
        SDL_PushGPUVertexUniformData(cmd, 1, &color, (Uint32)sizeof(color));
        SDL_DrawGPUPrimitives(pass, (Uint32)as->ring_draw_counts[i], 1, (Uint32)as->ring_draw_offsets[i], 0);

        if (r->radius > 0.5f) {
            color.u_color[0] = 0.7f;
            color.u_color[1] = 0.9f;
            color.u_color[2] = 1.f;
            color.u_color[3] = r->alpha * 0.45f;
            SDL_PushGPUVertexUniformData(cmd, 1, &color, (Uint32)sizeof(color));
            SDL_DrawGPUPrimitives(pass, (Uint32)as->ring_draw_counts[i], 1, (Uint32)as->ring_draw_offsets[i], 0);
        }
    }
}

static void draw_particles(AppState *as, SDL_GPURenderPass *pass)
{
    if (as->part_vert_count <= 0) return;

    SDL_BindGPUGraphicsPipeline(pass, as->particle_pipeline);
    SDL_BindGPUVertexBuffers(pass, 0, &(SDL_GPUBufferBinding){ as->part_vbuf, 0 }, 1);
    SDL_DrawGPUPrimitives(pass, (Uint32)as->part_vert_count, 1, 0, 0);
}

bool render_init(AppState *as)
{
    int pixel_w = WINDOW_W, pixel_h = WINDOW_H;
    float *water_verts = NULL;
    unsigned int *water_idx = NULL;
    int water_vcount = 0;
    float *disc_verts = NULL;
    int disc_floats = 0;
    ShaderPair pair;
    SDL_GPUShaderFormat shader_format;
    bool enable_validation;

    if (!as || !as->window)
        return false;

    shader_format = required_shader_format();

    if (!SDL_GPUSupportsShaderFormats(shader_format, NULL)) {
        SDL_Log("No SDL GPU backend supports shader format 0x%x",
                (unsigned int)shader_format);
        return false;
    }

#if defined(__ANDROID__)
    enable_validation = false;
#else
    enable_validation = QUAK_GPU_VALIDATION != 0;
#endif

    as->gpu = SDL_CreateGPUDevice(shader_format, enable_validation, NULL);
    if (!as->gpu) {
        SDL_Log("SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return false;
    }

    as->shader_format = shader_format;

    SDL_Log("SDL GPU driver: %s", SDL_GetGPUDeviceDriver(as->gpu));

    if (!SDL_ClaimWindowForGPUDevice(as->gpu, as->window)) {
        SDL_Log("SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        SDL_DestroyGPUDevice(as->gpu);
        as->gpu = NULL;
        return false;
    }
    as->gpu_window_claimed = true;

    as->swapchain_format = SDL_GetGPUSwapchainTextureFormat(as->gpu, as->window);
    SDL_SetGPUAllowedFramesInFlight(as->gpu, 2);

    if (!SDL_GetWindowSizeInPixels(as->window, &pixel_w, &pixel_h)) {
        pixel_w = WINDOW_W;
        pixel_h = WINDOW_H;
    }
    if (!create_depth_texture(as, (Uint32)pixel_w, (Uint32)pixel_h))
        return false;

    gen_water_grid(&water_verts, &water_vcount, &water_idx, &as->water_idx_count);
    if (!water_verts || !water_idx) {
        SDL_Log("Unable to allocate water mesh");
        return false;
    }
    if (!create_buffer_from_data(as, SDL_GPU_BUFFERUSAGE_VERTEX,
                                 water_verts,
                                 (Uint32)(water_vcount * 5 * sizeof(float)),
                                 &as->water_vbuf)) {
        SDL_free(water_verts);
        SDL_free(water_idx);
        return false;
    }
    if (!create_buffer_from_data(as, SDL_GPU_BUFFERUSAGE_INDEX,
                                 water_idx,
                                 (Uint32)(as->water_idx_count * sizeof(unsigned int)),
                                 &as->water_ibuf)) {
        SDL_free(water_verts);
        SDL_free(water_idx);
        return false;
    }
    SDL_free(water_verts);
    SDL_free(water_idx);

    as->disc_vert_count = gen_disc_flat(&disc_verts);
    if (!disc_verts || as->disc_vert_count == 0) {
        SDL_Log("Unable to allocate lily-pad mesh");
        return false;
    }
    disc_floats = as->disc_vert_count * 6;
    if (!create_buffer_from_data(as, SDL_GPU_BUFFERUSAGE_VERTEX,
                                 disc_verts,
                                 (Uint32)(disc_floats * sizeof(float)),
                                 &as->disc_vbuf)) {
        SDL_free(disc_verts);
        return false;
    }
    SDL_free(disc_verts);

#if QUAK_SHOW_DEBUG_GEOMETRY
    as->axis_vert_count = 6;
    if (!create_buffer_from_data(as, SDL_GPU_BUFFERUSAGE_VERTEX,
                                  AXIS_VERTS,
                                  (Uint32)sizeof(AXIS_VERTS),
                                  &as->axis_vbuf))
        return false;

    {
        static const int GRID_LINES = 161;
        size_t grid_vert_count = (size_t)(GRID_LINES * 4);
        float *grid_verts = (float *)SDL_malloc(grid_vert_count * 3 * sizeof(float));
        if (!grid_verts) return false;
        build_grid_vertices(grid_verts);
        as->grid_vert_count = (int)grid_vert_count;
        if (!create_buffer_from_data(as, SDL_GPU_BUFFERUSAGE_VERTEX,
                                      grid_verts,
                                      (Uint32)(as->grid_vert_count * 3 * sizeof(float)),
                                      &as->grid_vbuf)) {
            SDL_free(grid_verts);
            return false;
        }
        SDL_free(grid_verts);
    }
#endif

    as->ring_vert_capacity = MAX_RIPPLES * (RING_SEGS + 1);

    as->part_vert_capacity = MAX_RENDER_BILLBOARDS * 6;
    as->ring_vbuf = SDL_CreateGPUBuffer(as->gpu,
                                        &(SDL_GPUBufferCreateInfo){
                                            .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
                                            .size = (Uint32)(as->ring_vert_capacity * 3 * sizeof(float)),
                                            .props = 0
                                        });
    as->part_vbuf = SDL_CreateGPUBuffer(as->gpu,
                                         &(SDL_GPUBufferCreateInfo){
                                             .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
                                             .size = (Uint32)(as->part_vert_capacity * 9 * sizeof(float)),
                                             .props = 0
                                         });
    if (!as->ring_vbuf || !as->part_vbuf)
        return false;

    pair = render_get_shader_pair(as, "water");
    as->water_pipeline = create_pipeline(as, pair,
        &(SDL_GPUVertexBufferDescription){ 0, 5 * sizeof(float), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0 }, 1,
        (SDL_GPUVertexAttribute[]){
            { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0 },
            { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 3 * sizeof(float) }
        }, 2,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        SDL_GPU_CULLMODE_NONE,
        true, false, SDL_GPU_COMPAREOP_LESS_OR_EQUAL, true);

    pair = render_get_shader_pair(as, "lit");
    as->lit_pipeline = create_pipeline(as, pair,
        &(SDL_GPUVertexBufferDescription){ 0, 6 * sizeof(float), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0 }, 1,
        (SDL_GPUVertexAttribute[]){
            { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0 },
            { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 3 * sizeof(float) }
        }, 2,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        SDL_GPU_CULLMODE_NONE,
        true, true, SDL_GPU_COMPAREOP_LESS_OR_EQUAL, true);

    pair = render_get_shader_pair(as, "unlit");
    as->unlit_pipeline = create_pipeline(as, pair,
        &(SDL_GPUVertexBufferDescription){ 0, 3 * sizeof(float), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0 }, 1,
        (SDL_GPUVertexAttribute[]){
            { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0 }
        }, 1,
        SDL_GPU_PRIMITIVETYPE_LINESTRIP,
        SDL_GPU_CULLMODE_NONE,
        true, false, SDL_GPU_COMPAREOP_LESS_OR_EQUAL, true);

    pair = render_get_shader_pair(as, "shadow");
    as->shadow_pipeline = create_pipeline(as, pair,
        &(SDL_GPUVertexBufferDescription){ 0, 6 * sizeof(float), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0 }, 1,
        (SDL_GPUVertexAttribute[]){
            { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0 }
        }, 1,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        SDL_GPU_CULLMODE_NONE,
        true, false, SDL_GPU_COMPAREOP_LESS_OR_EQUAL, true);

    pair = render_get_shader_pair(as, "particle");
    as->particle_pipeline = create_pipeline(as, pair,
        &(SDL_GPUVertexBufferDescription){ 0, 9 * sizeof(float), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0 }, 1,
        (SDL_GPUVertexAttribute[]){
            { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0 },
            { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 3 * sizeof(float) },
            { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 5 * sizeof(float) }
        }, 3,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        SDL_GPU_CULLMODE_NONE,
        true, false, SDL_GPU_COMPAREOP_LESS_OR_EQUAL, true);

    pair = render_get_shader_pair(as, "tex");
    as->duck_pipeline = create_pipeline(as, pair,
        &(SDL_GPUVertexBufferDescription){ 0, 8 * sizeof(float), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0 }, 1,
        (SDL_GPUVertexAttribute[]){
            { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0 },
            { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 3 * sizeof(float) },
            { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 5 * sizeof(float) }
        }, 3,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        SDL_GPU_CULLMODE_NONE,
        false, true, SDL_GPU_COMPAREOP_LESS_OR_EQUAL, true);

    pair = render_get_shader_pair(as, "tex");
    as->duck_reflection_pipeline = create_pipeline(as, pair,
        &(SDL_GPUVertexBufferDescription){ 0, 8 * sizeof(float), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0 }, 1,
        (SDL_GPUVertexAttribute[]){
            { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0 },
            { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 3 * sizeof(float) },
            { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 5 * sizeof(float) }
        }, 3,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        SDL_GPU_CULLMODE_FRONT,
        true, false, SDL_GPU_COMPAREOP_LESS_OR_EQUAL, true);

    if (!as->water_pipeline || !as->lit_pipeline || !as->unlit_pipeline
        || !as->shadow_pipeline || !as->particle_pipeline || !as->duck_pipeline
        || !as->duck_reflection_pipeline)
        return false;

    as->camera_target[0] = CAM_TARGET_X;
    as->camera_target[1] = CAM_TARGET_Y;
    as->camera_target[2] = CAM_TARGET_Z;
    as->camera_distance = CAM_DISTANCE;
    as->camera_pitch = CAM_PITCH;
    as->camera_yaw = CAM_YAW;
    as->camera_dragging = false;
    as->last_mouse_x = 0;
    as->last_mouse_y = 0;
    as->mouse_captured = false;

    render_resize(as, pixel_w, pixel_h);
    update_camera_view(as);
    SDL_Log("render_init: SDL GPU driver=%s, validation=%s",
            SDL_GetGPUDeviceDriver(as->gpu), QUAK_GPU_VALIDATION ? "on" : "off");
    return true;
}

void render_resize(AppState *as, int win_w, int win_h)
{
    int safe_w = SDL_max(win_w, 1);
    int safe_h = SDL_max(win_h, 1);

    as->win_w = win_w;
    as->win_h = win_h;
    make_perspective_lh_zo(as->proj,
                           3.14159265f / 3.f,
                           (float)safe_w / (float)safe_h,
                           1.0f, 100.f);

    if (as->gpu && ((Uint32)safe_w != as->depth_w || (Uint32)safe_h != as->depth_h)) {
        SDL_WaitForGPUIdle(as->gpu);
        create_depth_texture(as, (Uint32)safe_w, (Uint32)safe_h);
    }
}

void render_frame(AppState *as)
{
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUTexture *swapchain = NULL;
    Uint32 swap_w = 0, swap_h = 0;

    if (!as->gpu) return;

    update_camera_view(as);

    cmd = SDL_AcquireGPUCommandBuffer(as->gpu);
    if (!cmd) return;

    if (!render_prepare_frame(as, cmd)) {
        SDL_CancelGPUCommandBuffer(cmd);
        return;
    }

    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, as->window, &swapchain, &swap_w, &swap_h)) {
        SDL_CancelGPUCommandBuffer(cmd);
        return;
    }
    if (!swapchain) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    if (swap_w != as->depth_w || swap_h != as->depth_h) {
        SDL_WaitForGPUIdle(as->gpu);
        if (!create_depth_texture(as, swap_w, swap_h)) {
            SDL_SubmitGPUCommandBuffer(cmd);
            return;
        }
        render_resize(as, (int)swap_w, (int)swap_h);
    }

    float day_mix = as->environment.day_mix;
    SDL_FColor clear_color = {
        0.025f + day_mix * (0.53f - 0.025f),
        0.045f + day_mix * (0.81f - 0.045f),
        0.12f + day_mix * (0.98f - 0.12f),
        1.f
    };
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd,
        &(SDL_GPUColorTargetInfo){
            .texture = swapchain,
            .mip_level = 0,
            .layer_or_depth_plane = 0,
            .clear_color = clear_color,
            .load_op = SDL_GPU_LOADOP_CLEAR,
            .store_op = SDL_GPU_STOREOP_STORE,
            .resolve_texture = NULL,
            .resolve_mip_level = 0,
            .resolve_layer = 0,
            .cycle = false,
            .cycle_resolve_texture = false
        },
        1,
        &(SDL_GPUDepthStencilTargetInfo){
            .texture = as->depth_tex,
            .clear_depth = 1.0f,
            .load_op = SDL_GPU_LOADOP_CLEAR,
            .store_op = SDL_GPU_STOREOP_STORE,
            .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
            .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE,
            .cycle = false,
            .clear_stencil = 0,
            .mip_level = 0,
            .layer = 0
        });

    if (!pass) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    SDL_SetGPUViewport(pass,
                       &(SDL_GPUViewport){
                           .x = 0.f,
                           .y = 0.f,
                           .w = (float)swap_w,
                           .h = (float)swap_h,
                           .min_depth = 0.f,
                           .max_depth = 1.f
                       });

    render_push_camera_uniform(cmd, as);
#if QUAK_SHOW_DEBUG_GEOMETRY
    draw_grid(cmd, as, pass);
    draw_axes(cmd, as, pass);
#endif
    draw_lily_pads(as, cmd, pass);
    duck_draw(as, cmd, pass);
    draw_water(cmd, as, pass);
    duck_draw_reflection(as, cmd, pass);
    draw_duck_shadow(as, cmd, pass);
    draw_ripples(cmd, as, pass);
    draw_particles(as, pass);
    SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
}

void render_cleanup(AppState *as)
{
    if (!as || !as->gpu) return;

    SDL_WaitForGPUIdle(as->gpu);

    if (as->water_pipeline) SDL_ReleaseGPUGraphicsPipeline(as->gpu, as->water_pipeline);
    if (as->lit_pipeline)   SDL_ReleaseGPUGraphicsPipeline(as->gpu, as->lit_pipeline);
    if (as->unlit_pipeline) SDL_ReleaseGPUGraphicsPipeline(as->gpu, as->unlit_pipeline);
    if (as->shadow_pipeline) SDL_ReleaseGPUGraphicsPipeline(as->gpu, as->shadow_pipeline);
    if (as->particle_pipeline) SDL_ReleaseGPUGraphicsPipeline(as->gpu, as->particle_pipeline);
    if (as->duck_pipeline)  SDL_ReleaseGPUGraphicsPipeline(as->gpu, as->duck_pipeline);
    if (as->duck_reflection_pipeline)
        SDL_ReleaseGPUGraphicsPipeline(as->gpu, as->duck_reflection_pipeline);

    if (as->water_vbuf) SDL_ReleaseGPUBuffer(as->gpu, as->water_vbuf);
    if (as->water_ibuf) SDL_ReleaseGPUBuffer(as->gpu, as->water_ibuf);
    if (as->disc_vbuf)  SDL_ReleaseGPUBuffer(as->gpu, as->disc_vbuf);
    if (as->axis_vbuf)  SDL_ReleaseGPUBuffer(as->gpu, as->axis_vbuf);
    if (as->grid_vbuf)  SDL_ReleaseGPUBuffer(as->gpu, as->grid_vbuf);
    if (as->ring_vbuf)  SDL_ReleaseGPUBuffer(as->gpu, as->ring_vbuf);
    if (as->part_vbuf)  SDL_ReleaseGPUBuffer(as->gpu, as->part_vbuf);
    if (as->staging_buffer) SDL_ReleaseGPUTransferBuffer(as->gpu, as->staging_buffer);
    if (as->depth_tex) SDL_ReleaseGPUTexture(as->gpu, as->depth_tex);

    if (as->gpu_window_claimed)
        SDL_ReleaseWindowFromGPUDevice(as->gpu, as->window);
    SDL_DestroyGPUDevice(as->gpu);

    as->gpu = NULL;
    as->water_pipeline = NULL;
    as->lit_pipeline = NULL;
    as->unlit_pipeline = NULL;
    as->shadow_pipeline = NULL;
    as->particle_pipeline = NULL;
    as->duck_pipeline = NULL;
    as->duck_reflection_pipeline = NULL;
    as->water_vbuf = NULL;
    as->water_ibuf = NULL;
    as->disc_vbuf = NULL;
    as->axis_vbuf = NULL;
    as->grid_vbuf = NULL;
    as->ring_vbuf = NULL;
    as->part_vbuf = NULL;
    as->staging_buffer = NULL;
    as->depth_tex = NULL;
    as->gpu_window_claimed = false;
}
