#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <linmath.h>

/* ── Game-wide constants ─────────────────────────────────────────────────── */

#define WINDOW_W        800
#define WINDOW_H        600

/* World physics */
#define MAX_SPEED       8.0f
#define ACCELERATION    20.0f
#define FRICTION_K      3.5f
#define DEADZONE        0.12f
#define BOUNCE_MARGIN   1.5f
#define BOUNCE_ACCEL    12.0f
#define WORLD_BOUND     16.0f
#define DUCK_ANGLE_OFFSET   (-SDL_PI_F / 2.0f)
#define DUCK_TURN_THRESHOLD 0.5f
#define AXIS_LENGTH         20.0f
#define DUCK_BOB_AMPLITUDE  0.08f
#define DUCK_BOB_FREQ       1.2f
#define DUCK_BOB_SPEED_FADE 3.0f
#define IDLE_QUACK_INTERVAL_MIN  5.0f
#define IDLE_QUACK_INTERVAL_MAX  12.0f
#define IDLE_QUACK_SPEED_LIMIT   1.5f
#define BUBBLE_INTERVAL     0.45f
#define BUBBLE_SPEED_LIMIT  1.0f
#define DUCK_MOVING_THRESHOLD   0.8f
#define MOVEMENT_RIPPLE_PERIOD  0.25f
#define QUACK_COOLDOWN_TIME     0.6f
#define BOUNDARY_HIT_VEL_MIN    1.5f
#define SHADOW_SCALE_X      1.3f
#define SHADOW_SCALE_Z      0.9f
#define SHADOW_ALPHA        0.28f
#define CAMERA_SHAKE_DURATION   0.25f
#define CAMERA_SHAKE_MAGNITUDE  0.12f
#define DUCK_COLLISION_RADIUS   0.5f
#define LILY_BOUNCE_COEFF       0.4f

/* Effects */
#define MAX_RIPPLES     32
#define RIPPLE_SPEED    4.5f
#define RIPPLE_MAX_R    8.0f
#define MAX_PARTICLES   64
#define RING_SEGS       48
#define LILY_PAD_COUNT  6

/* Audio */
#define QUACK_CHANNELS  4
#define SAMPLE_RATE     44100
#define QUACK_MS        190

/* Camera */
#define CAM_TARGET_X    0.0f
#define CAM_TARGET_Y    0.0f
#define CAM_TARGET_Z    0.0f
#define CAM_DISTANCE    15.6f
#define CAM_PITCH       0.70f
#define CAM_YAW         0.0f
#define CAM_PITCH_LIMIT 1.45f
#define CAM_ROT_SPEED   0.005f
#define CAM_ZOOM_SPEED  0.8f

/* Duck OBJ model */
#define DUCK_SCALE      0.15f   /* world units per OBJ unit      */
#define DUCK_OBJ_LIFT   4.60f   /* lifts OBJ bottom (y≈-4.60) to y=0 */

/* ── Structs ─────────────────────────────────────────────────────────────── */

typedef struct {
    float x, z;     /* world XZ centre */
    float radius;
    float alpha;
    bool  active;
} Ripple;

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float life;     /* 1 → 0 */
    bool  active;
} Particle;

typedef struct {
    float x, z, r;
} LilyPad;

typedef struct {
    SDL_Window    *window;
    int            win_w, win_h;

    /* SDL GPU */
    SDL_GPUDevice        *gpu;
    SDL_GPUTextureFormat  swapchain_format;
    SDL_GPUTextureFormat  depth_format;
    SDL_GPUTexture       *depth_tex;
    Uint32                depth_w;
    Uint32                depth_h;

    SDL_GPUGraphicsPipeline *water_pipeline;
    SDL_GPUGraphicsPipeline *lit_pipeline;
    SDL_GPUGraphicsPipeline *unlit_pipeline;
    SDL_GPUGraphicsPipeline *particle_pipeline;
    SDL_GPUGraphicsPipeline *duck_pipeline;

    SDL_GPUBuffer        *water_vbuf;
    SDL_GPUBuffer        *water_ibuf;
    int                   water_idx_count;

    SDL_GPUBuffer        *disc_vbuf;
    int                   disc_vert_count;

    SDL_GPUBuffer        *axis_vbuf;
    int                   axis_vert_count;

    SDL_GPUBuffer        *grid_vbuf;
    int                   grid_vert_count;

    SDL_GPUBuffer        *ring_vbuf;
    int                   ring_vert_capacity;
    int                   ring_draw_offsets[MAX_RIPPLES];
    int                   ring_draw_counts[MAX_RIPPLES];

    SDL_GPUBuffer        *part_vbuf;
    int                   part_vert_capacity;

    SDL_GPUBuffer        *duck_vbuf;
    SDL_GPUTexture       *duck_tex;
    SDL_GPUSampler       *duck_sampler;
    int                   duck_obj_count;

    SDL_GPUTransferBuffer *staging_buffer;
    Uint32                 staging_size;

    /* Camera */
    mat4x4 proj;
    mat4x4 view;
    vec3 camera_target;
    float camera_distance;
    float camera_pitch;
    float camera_yaw;
    bool camera_dragging;
    int last_mouse_x;
    int last_mouse_y;
    bool mouse_captured;

    /* Duck state (XZ world plane) */
    float duck_x, duck_z;
    float duck_vx, duck_vz;
    float duck_angle;   /* radians, Y-rotation; 0 = facing +X */
    float duck_y_offset;
    float duck_bob_phase;
    float idle_quack_timer;
    float bubble_timer;
    bool  duck_was_moving;
    float movement_ripple_timer;
    float quack_cooldown;
    float camera_shake_t;
    float camera_shake_mag;
    float duck_reflection_alpha;

    /* Input */
    SDL_Gamepad *gamepad;

    /* Effects */
    Ripple   ripples[MAX_RIPPLES];
    int      ripple_head;
    Particle particles[MAX_PARTICLES];
    int      particle_head;

    /* Audio */
    SDL_AudioDeviceID  audio_dev;
    SDL_AudioStream   *quack_streams[QUACK_CHANNELS];
    SDL_AudioStream   *splash_stream;
    int                quack_next;
    Sint16            *quack_pcm;
    Sint16            *splash_pcm;
    int                quack_pcm_bytes;
    int                splash_pcm_bytes;

    /* Timing */
    Uint64 last_perf;
    Uint64 perf_freq;
    float  elapsed;
} AppState;

extern const LilyPad LILY_PADS[LILY_PAD_COUNT];
