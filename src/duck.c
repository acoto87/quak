/* stb_image — compiled here only; other TUs just include the header.        */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include <stb_image.h>

#include <stdio.h>
#include <math.h>
#include "duck.h"
#include "render.h"

typedef struct {
    mat4x4 u_model;
    vec4   u_nmat[3];
} TransformUniform;

typedef struct {
    float u_alpha;
    float _pad[3];
} FragmentUniform;

typedef struct {
    vec4 u_environment;
} EnvironmentUniform;

static void apply_obj_transform(const float *pos, float *out_pos)
{
    const float tx = -0.12f;
    const float ty = -3.2f;
    const float tz = -0.15f;
    const float rx = -3.14159265f / 2.0f;
    float x = pos[0] + tx;
    float y = pos[1] + ty;
    float z = pos[2] + tz;

    out_pos[0] = x;
    out_pos[1] = z * cosf(rx) - y * sinf(rx);
    out_pos[2] = z * sinf(rx) + y * cosf(rx);
}

static void apply_obj_normal(const float *normal, float *out_normal)
{
    const float rx = -3.14159265f / 2.0f;
    float length;

    out_normal[0] = normal[0];
    out_normal[1] = normal[2] * cosf(rx) - normal[1] * sinf(rx);
    out_normal[2] = normal[2] * sinf(rx) + normal[1] * cosf(rx);
    length = sqrtf(out_normal[0] * out_normal[0]
                 + out_normal[1] * out_normal[1]
                 + out_normal[2] * out_normal[2]);
    if (length > 0.00001f) {
        out_normal[0] /= length;
        out_normal[1] /= length;
        out_normal[2] /= length;
    }
}

/* Parse an OBJ file into [px py pz u v nx ny nz] vertices. */
static bool load_obj_mesh(const char *path, float **out_buf, int *out_count)

{
    size_t sz;
    char *raw = (char *)SDL_LoadFile(path, &sz);
    if (!raw) {
        SDL_Log("OBJ: cannot open '%s' — %s", path, SDL_GetError());
        return false;
    }

    int nv = 0, nvt = 0, nvn = 0, nf = 0;
    for (const char *p = raw; *p; ) {
        if      (p[0]=='v' && p[1]==' ')  nv++;
        else if (p[0]=='v' && p[1]=='t') nvt++;
        else if (p[0]=='v' && p[1]=='n') nvn++;
        else if (p[0]=='f' && p[1]==' ') nf++;
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }

    if (nv == 0 || nf == 0) {
        SDL_Log("OBJ: no geometry in '%s'", path);
        SDL_free(raw);
        return false;
    }

    float *pos  = (float *)SDL_malloc((size_t)nv  * 3 * sizeof(float));
    float *uv   = (float *)SDL_malloc((size_t)nvt * 2 * sizeof(float));
    float *norm = (float *)SDL_malloc((size_t)nvn * 3 * sizeof(float));
    int   max_verts = nf * 6;
    float *buf = (float *)SDL_malloc((size_t)max_verts * 8 * sizeof(float));

    if (!pos || !uv || !norm || !buf) {
        SDL_Log("OBJ: out of memory");
        SDL_free(pos); SDL_free(uv); SDL_free(norm); SDL_free(buf); SDL_free(raw);
        return false;
    }

    int vi = 0, vti = 0, vni = 0;
    for (const char *p = raw; *p; ) {
        if (p[0]=='v' && p[1]==' ') {
            sscanf(p+2, "%f %f %f", &pos[vi*3], &pos[vi*3+1], &pos[vi*3+2]);
            vi++;
        } else if (p[0]=='v' && p[1]=='t') {
            sscanf(p+3, "%f %f", &uv[vti*2], &uv[vti*2+1]);
            vti++;
        } else if (p[0]=='v' && p[1]=='n') {
            sscanf(p+3, "%f %f %f", &norm[vni*3], &norm[vni*3+1], &norm[vni*3+2]);
            vni++;
        }
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }

    int vcount = 0;
    for (const char *p = raw; *p; ) {
        if (p[0]=='f' && p[1]==' ') {
            int fv[4][3];
            int nfv = 0;
            const char *fp = p + 2;
            while (nfv < 4) {
                int pi, ti, ni;
                if (sscanf(fp, "%d/%d/%d", &pi, &ti, &ni) < 3) break;
                fv[nfv][0] = pi - 1;
                fv[nfv][1] = ti - 1;
                fv[nfv][2] = ni - 1;
                nfv++;
                while (*fp && *fp != ' ' && *fp != '\t' && *fp != '\n' && *fp != '\r') fp++;
                while (*fp == ' ' || *fp == '\t') fp++;
            }

            for (int t = 1; t + 1 < nfv && vcount + 3 <= max_verts; t++) {
                int tri[3] = {0, t, t + 1};
                for (int k = 0; k < 3; k++) {
                    int pidx = fv[tri[k]][0];
                    int tidx = fv[tri[k]][1];
                    int nidx = fv[tri[k]][2];
                    float transformed_pos[3];
                    float transformed_norm[3];
                    apply_obj_transform(&pos[pidx*3], transformed_pos);
                    apply_obj_normal(&norm[nidx*3], transformed_norm);
                    buf[vcount*8+0] = transformed_pos[0];
                    buf[vcount*8+1] = transformed_pos[1];
                    buf[vcount*8+2] = transformed_pos[2];
                    buf[vcount*8+3] = uv[tidx*2+0];
                    buf[vcount*8+4] = uv[tidx*2+1];
                    buf[vcount*8+5] = transformed_norm[0];
                    buf[vcount*8+6] = transformed_norm[1];
                    buf[vcount*8+7] = transformed_norm[2];

                    vcount++;
                }
            }
        }
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }

    SDL_free(pos); SDL_free(uv); SDL_free(norm); SDL_free(raw);
    *out_buf   = buf;
    *out_count = vcount;
    SDL_Log("OBJ: '%s' — %d vertices", path, vcount);
    return true;
}

static bool create_buffer_from_data(AppState *as, Uint32 usage, const void *data,
                                    Uint32 size, SDL_GPUBuffer **out_buffer)
{
    SDL_GPUBufferCreateInfo buffer_info = {
        .usage = usage,
        .size = size,
        .props = 0
    };
    SDL_GPUTransferBufferCreateInfo transfer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = size,
        .props = 0
    };
    SDL_GPUBuffer *buffer = SDL_CreateGPUBuffer(as->gpu, &buffer_info);
    SDL_GPUTransferBuffer *transfer = NULL;
    SDL_GPUCommandBuffer *cmd = NULL;
    SDL_GPUCopyPass *copy = NULL;
    void *mapped = NULL;

    if (!buffer) {
        SDL_Log("CreateGPUBuffer failed: %s", SDL_GetError());
        return false;
    }

    transfer = SDL_CreateGPUTransferBuffer(as->gpu, &transfer_info);
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
        SDL_Log("GPU buffer upload failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(as->gpu, transfer);
        SDL_ReleaseGPUBuffer(as->gpu, buffer);
        return false;
    }

    SDL_ReleaseGPUTransferBuffer(as->gpu, transfer);
    *out_buffer = buffer;
    return true;
}

static bool create_texture_from_jpg(AppState *as, const char *path,
                                    SDL_GPUTexture **out_texture,
                                    SDL_GPUSampler **out_sampler)
{
    size_t img_sz = 0;
    void *img_raw = SDL_LoadFile(path, &img_sz);
    SDL_GPUTexture *texture = NULL;
    SDL_GPUTransferBuffer *transfer = NULL;
    SDL_GPUCommandBuffer *cmd = NULL;
    SDL_GPUCopyPass *copy = NULL;
    SDL_GPUSampler *sampler = NULL;

    if (!img_raw) {
        SDL_Log("Texture: cannot open '%s' — %s", path, SDL_GetError());
        return false;
    }

    stbi_set_flip_vertically_on_load(1);
    int w, h, ch;
    unsigned char *pixels =
        stbi_load_from_memory((const stbi_uc *)img_raw, (int)img_sz, &w, &h, &ch, 4);
    SDL_free(img_raw);

    if (!pixels) {
        SDL_Log("Texture: stbi_load_from_memory failed for '%s'", path);
        return false;
    }

    Uint32 pixels_size = (Uint32)(w * h * 4);
    transfer = SDL_CreateGPUTransferBuffer(as->gpu,
                                           &(SDL_GPUTransferBufferCreateInfo){
                                               .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                                               .size = pixels_size,
                                               .props = 0
                                           });
    if (!transfer) {
        SDL_Log("CreateGPUTransferBuffer failed: %s", SDL_GetError());
        stbi_image_free(pixels);
        return false;
    }

    void *mapped = SDL_MapGPUTransferBuffer(as->gpu, transfer, false);
    if (!mapped) {
        SDL_Log("MapGPUTransferBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(as->gpu, transfer);
        stbi_image_free(pixels);
        return false;
    }
    SDL_memcpy(mapped, pixels, pixels_size);
    SDL_UnmapGPUTransferBuffer(as->gpu, transfer);
    stbi_image_free(pixels);

    texture = SDL_CreateGPUTexture(as->gpu,
                                   &(SDL_GPUTextureCreateInfo){
                                       .type = SDL_GPU_TEXTURETYPE_2D,
                                       .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                       .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                       .width = (Uint32)w,
                                       .height = (Uint32)h,
                                       .layer_count_or_depth = 1,
                                       .num_levels = 1,
                                       .sample_count = SDL_GPU_SAMPLECOUNT_1,
                                       .props = 0
                                   });
    if (!texture) {
        SDL_Log("CreateGPUTexture failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(as->gpu, transfer);
        return false;
    }

    cmd = SDL_AcquireGPUCommandBuffer(as->gpu);
    if (!cmd) {
        SDL_Log("AcquireGPUCommandBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(as->gpu, transfer);
        SDL_ReleaseGPUTexture(as->gpu, texture);
        return false;
    }

    copy = SDL_BeginGPUCopyPass(cmd);
    SDL_UploadToGPUTexture(copy,
                           &(SDL_GPUTextureTransferInfo){
                               .transfer_buffer = transfer,
                               .offset = 0,
                               .pixels_per_row = (Uint32)w,
                               .rows_per_layer = (Uint32)h
                           },
                           &(SDL_GPUTextureRegion){
                               .texture = texture,
                               .mip_level = 0,
                               .layer = 0,
                               .x = 0,
                               .y = 0,
                               .z = 0,
                               .w = (Uint32)w,
                               .h = (Uint32)h,
                               .d = 1
                           },
                           false);
    SDL_EndGPUCopyPass(copy);

    if (!SDL_SubmitGPUCommandBuffer(cmd) || !SDL_WaitForGPUIdle(as->gpu)) {
        SDL_Log("GPU texture upload failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(as->gpu, transfer);
        SDL_ReleaseGPUTexture(as->gpu, texture);
        return false;
    }

    SDL_ReleaseGPUTransferBuffer(as->gpu, transfer);

    sampler = SDL_CreateGPUSampler(as->gpu,
                                   &(SDL_GPUSamplerCreateInfo){
                                       .min_filter = SDL_GPU_FILTER_LINEAR,
                                       .mag_filter = SDL_GPU_FILTER_LINEAR,
                                       .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
                                       .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
                                       .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
                                       .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
                                       .mip_lod_bias = 0.0f,
                                       .max_anisotropy = 1.0f,
                                       .compare_op = SDL_GPU_COMPAREOP_ALWAYS,
                                       .min_lod = 0.0f,
                                       .max_lod = 0.0f,
                                       .enable_anisotropy = false,
                                       .enable_compare = false,
                                       .props = 0
                                   });
    if (!sampler) {
        SDL_Log("CreateGPUSampler failed: %s", SDL_GetError());
        SDL_ReleaseGPUTexture(as->gpu, texture);
        return false;
    }

    SDL_Log("Texture: '%s' loaded (%dx%d)", path, w, h);
    *out_texture = texture;
    *out_sampler = sampler;
    return true;
}

bool duck_init(AppState *as)
{
    const char *base = SDL_GetBasePath();
    char obj_path[1024], tex_path[1024];
    float *verts = NULL;
    int vc = 0;

    SDL_snprintf(obj_path, sizeof(obj_path),
                 "%sassets/10602_Rubber_Duck_v1_L3.obj", base ? base : "");
    SDL_snprintf(tex_path, sizeof(tex_path),
                 "%sassets/10602_Rubber_Duck_v1_diffuse.jpg", base ? base : "");

    if (!load_obj_mesh(obj_path, &verts, &vc))
        return false;

    as->duck_obj_count = vc;

    if (!create_buffer_from_data(as, SDL_GPU_BUFFERUSAGE_VERTEX,
                                 verts, (Uint32)(vc * 8 * sizeof(float)),
                                 &as->duck_vbuf)) {
        SDL_free(verts);
        return false;
    }
    SDL_free(verts);

    if (!create_texture_from_jpg(as, tex_path, &as->duck_tex, &as->duck_sampler)) {
        SDL_Log("duck_init: diffuse texture is required");
        return false;
    }

    return true;
}

static void duck_draw_part(AppState *as, SDL_GPUCommandBuffer *cmd,
                           SDL_GPURenderPass *pass, bool reflection)
{
    if (!cmd || !pass || !as->duck_vbuf || !as->duck_tex || !as->duck_sampler
        || as->duck_obj_count == 0
        || (reflection ? !as->duck_reflection_pipeline : !as->duck_pipeline))
        return;

    mat4x4 model;
    mat4x4 refl;
    float body_y = as->duck_y_offset + as->duck_animation.body_y_offset;
    float heading = as->duck_angle + as->duck_animation.spin_angle;
    mat4x4_identity(model);
    mat4x4_translate_in_place(model, as->duck_x, body_y, as->duck_z);
    mat4x4_rotate_Y(model, model, DUCK_MODEL_YAW(heading));
    mat4x4_rotate_Z(model, model, as->duck_animation.body_roll);
    mat4x4_rotate_X(model, model, as->duck_animation.body_pitch);
    mat4x4_scale_aniso(model, model,
                       as->duck_animation.body_scale_x,
                       as->duck_animation.body_scale_y,
                       as->duck_animation.body_scale_z);
    mat4x4_rotate_X(model, model, 3.14159265f / 2.0f);
    mat4x4_scale_aniso(model, model, DUCK_SCALE, DUCK_SCALE, DUCK_SCALE);
    mat4x4_translate_in_place(model, 0.0f, DUCK_OBJ_LIFT, 0.0f);

    float nmat[9];
    float refl_nmat[9];
    TransformUniform transform = {0};
    TransformUniform refl_transform = {0};
    FragmentUniform frag = {.u_alpha = as->duck_reflection_alpha};
    EnvironmentUniform environment = {{as->environment.day_mix, 0.f, 0.f, 0.f}};
    compute_nmat(nmat, model);
    SDL_memcpy(transform.u_model, model, sizeof(mat4x4));
    transform.u_nmat[0][0] = nmat[0];
    transform.u_nmat[0][1] = nmat[1];
    transform.u_nmat[0][2] = nmat[2];
    transform.u_nmat[1][0] = nmat[3];
    transform.u_nmat[1][1] = nmat[4];
    transform.u_nmat[1][2] = nmat[5];
    transform.u_nmat[2][0] = nmat[6];
    transform.u_nmat[2][1] = nmat[7];
    transform.u_nmat[2][2] = nmat[8];

    mat4x4_identity(refl);
    mat4x4_translate_in_place(refl, as->duck_x, -body_y, as->duck_z);
    mat4x4_rotate_Y(refl, refl, DUCK_MODEL_YAW(heading));
    mat4x4_rotate_Z(refl, refl, -as->duck_animation.body_roll);
    mat4x4_rotate_X(refl, refl, -as->duck_animation.body_pitch);
    mat4x4_scale_aniso(refl, refl,
                       as->duck_animation.body_scale_x,
                       as->duck_animation.body_scale_y,
                       as->duck_animation.body_scale_z);
    mat4x4_rotate_X(refl, refl, -3.14159265f / 2.0f);
    mat4x4_scale_aniso(refl, refl, DUCK_SCALE, -DUCK_SCALE, DUCK_SCALE);
    mat4x4_translate_in_place(refl, 0.0f, DUCK_OBJ_LIFT, 0.0f);

    compute_nmat(refl_nmat, refl);
    SDL_memcpy(refl_transform.u_model, refl, sizeof(mat4x4));
    refl_transform.u_nmat[0][0] = refl_nmat[0];
    refl_transform.u_nmat[0][1] = refl_nmat[1];
    refl_transform.u_nmat[0][2] = refl_nmat[2];
    refl_transform.u_nmat[1][0] = refl_nmat[3];
    refl_transform.u_nmat[1][1] = refl_nmat[4];
    refl_transform.u_nmat[1][2] = refl_nmat[5];
    refl_transform.u_nmat[2][0] = refl_nmat[6];
    refl_transform.u_nmat[2][1] = refl_nmat[7];
    refl_transform.u_nmat[2][2] = refl_nmat[8];

    SDL_BindGPUGraphicsPipeline(pass, reflection
        ? as->duck_reflection_pipeline : as->duck_pipeline);
    SDL_BindGPUVertexBuffers(pass, 0,
                             &(SDL_GPUBufferBinding){ as->duck_vbuf, 0 }, 1);

    SDL_GPUTextureSamplerBinding binding = {
        .texture = as->duck_tex,
        .sampler = as->duck_sampler
    };
    SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
    SDL_PushGPUFragmentUniformData(cmd, 1, &environment,
                                   (Uint32)sizeof(environment));

    frag.u_alpha = reflection ? as->duck_reflection_alpha : 1.f;
    SDL_PushGPUVertexUniformData(cmd, 1,
        reflection ? (const void *)&refl_transform : (const void *)&transform,
        (Uint32)sizeof(transform));
    SDL_PushGPUFragmentUniformData(cmd, 0, &frag, (Uint32)sizeof(frag));
    SDL_DrawGPUPrimitives(pass, (Uint32)as->duck_obj_count, 1, 0, 0);
}

void duck_draw(AppState *as, SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass)
{
    duck_draw_part(as, cmd, pass, false);
}

void duck_draw_reflection(AppState *as, SDL_GPUCommandBuffer *cmd,
                          SDL_GPURenderPass *pass)
{
    duck_draw_part(as, cmd, pass, true);
}

void duck_cleanup(AppState *as)
{
    if (as->duck_sampler) {
        SDL_ReleaseGPUSampler(as->gpu, as->duck_sampler);
        as->duck_sampler = NULL;
    }
    if (as->duck_tex) {
        SDL_ReleaseGPUTexture(as->gpu, as->duck_tex);
        as->duck_tex = NULL;
    }
    if (as->duck_vbuf) {
        SDL_ReleaseGPUBuffer(as->gpu, as->duck_vbuf);
        as->duck_vbuf = NULL;
    }
    as->duck_obj_count = 0;
}
