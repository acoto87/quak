#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <linmath.h>

#include "activities.h"
#include "duck_animation.h"
#include "interaction.h"
#include "pond_objects.h"

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
#define DUCK_MODEL_YAW(angle) (-(angle) + DUCK_ANGLE_OFFSET)
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
#define SHADOW_SCALE_X      1.0f
#define SHADOW_SCALE_Z      0.62f
#define SHADOW_ALPHA        0.32f
#define SHADOW_HEIGHT       0.04f
#define CAMERA_SHAKE_DURATION   0.25f
#define CAMERA_SHAKE_MAGNITUDE  0.12f
#define DUCK_COLLISION_RADIUS   0.5f
#define LILY_BOUNCE_COEFF       0.4f
#define DUCK_WORLD_BOUND        (WORLD_BOUND - DUCK_COLLISION_RADIUS)
#define DUCK_TOUCH_RADIUS       1.25f
#define DUCK_SLEEP_AFTER_SECONDS 12.0f
#define DUCK_WAKE_DURATION      0.8f
#define SLEEP_BUBBLE_PERIOD     0.8f

#define FINGER_TRAIL_SPACING    0.35f
#define MAX_TRAIL_RIPPLES_PER_STEP 3

#define POPPABLE_BUBBLE_TARGET  3
#define POPPABLE_BUBBLE_TOUCH_MARGIN 1.5f
#define POPPABLE_BUBBLE_RESPAWN 1.5f

#define LILY_NOTE_COUNT         5
#define LILY_NOTE_COOLDOWN      0.15f
#define LILY_FEEDBACK_COOLDOWN  0.2f

#define ZOOMIES_TAP_WINDOW      1.2f
#define ZOOMIES_REQUIRED_TAPS   4
#define ZOOMIES_DURATION        2.2f
#define ZOOMIES_COOLDOWN        5.0f
#define ZOOMIES_RADIUS          2.4f
#define ZOOMIES_ANGULAR_SPEED   3.2f

#define SPLASHES_PER_RAINBOW    10
#define BUBBLE_POPS_PER_RAINBOW 8
#define CELEBRATION_COOLDOWN    8.0f

#define NIGHT_FIREFLY_COUNT     6
#define ENVIRONMENT_CYCLE_SECONDS 30.0f

/* Simulation */
#define FIXED_TIMESTEP          (1.0 / 60.0)
#define MAX_FRAME_DELTA         0.05
#define MAX_SIMULATION_STEPS    5

/* Touch */
#define MAX_TOUCHES             10
#define MAX_TRAIL_SAMPLES       64
#define TOUCH_SMOOTHING         0.35f
#define TOUCH_TAP_MAX_NS        300000000ULL
#define TOUCH_TAP_MAX_DISTANCE  0.04f
#define TOUCH_ARRIVAL_RADIUS    2.5f

/* Effects */
#define MAX_RIPPLES     32
#define RIPPLE_SPEED    4.5f
#define RIPPLE_MAX_R    8.0f
#define MAX_PARTICLES   64
#define MAX_RENDER_BILLBOARDS (MAX_PARTICLES + MAX_POND_OBJECTS)
#define RING_SEGS       48

/* Audio */
#define QUACK_CHANNELS  4
#define SPLASH_CHANNELS 3
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
    float speed;
    float max_radius;
    float max_alpha;
    float alpha;
    bool  active;
} Ripple;

typedef enum {
    PARTICLE_WATER_DROPLET,
    PARTICLE_BUBBLE,
    PARTICLE_FOAM,
    PARTICLE_SPARKLE,
    PARTICLE_RAIN,
    PARTICLE_FIREFLY,
    PARTICLE_RAINBOW,
    PARTICLE_SLEEP_BUBBLE
} ParticleKind;

typedef struct {
    bool active;
    ParticleKind kind;
    float x, y, z;
    float vx, vy, vz;
    float life;
    float max_life;
    float size;
    float red, green, blue, alpha;
} Particle;

typedef struct PlayerIntent {
    bool has_swim_target;
    float swim_target_x;
    float swim_target_z;
    float move_x;
    float move_z;
} PlayerIntent;

typedef enum {
    QUACK_VARIANT_NORMAL,
    QUACK_VARIANT_TINY,
    QUACK_VARIANT_DEEP,
    QUACK_VARIANT_HICCUP,
    QUACK_VARIANT_SURPRISED,
    QUACK_VARIANT_COUNT
} QuackVariant;

typedef struct {
    Sint16 *pcm;
    int pcm_bytes;
} AudioClip;

typedef struct {
    float day_mix;
    float target_day_mix;
    float cycle_timer;
    bool night;
} EnvironmentState;

typedef struct {
    int obstacle_id;
    int turn_sign;
} DuckAvoidanceState;

typedef struct {
    SDL_TouchID touch_id;
    SDL_FingerID finger_id;
    bool active;
    float raw_x, raw_y;
    float smooth_x, smooth_y;
    float down_x, down_y;
    float max_distance_sq;
    float world_x, world_z;
    float gesture_world_x, gesture_world_z;
    float world_travel_distance;
    bool world_valid;
    bool gesture_world_valid;
    Uint64 down_timestamp_ns;
    Uint64 activation_sequence;
} TouchPoint;

typedef struct {
    float x, z;
    Uint64 owner_sequence;
    bool reset;
} TrailSample;

typedef struct {
    TouchPoint touches[MAX_TOUCHES];
    int primary_touch;
    Uint64 next_sequence;
    TrailSample trail_samples[MAX_TRAIL_SAMPLES];
    int trail_read;
    int trail_write;
    int trail_count;
} InputState;

typedef struct AppState {
    SDL_Window    *window;
    int            win_w, win_h;

    /* SDL GPU */
    SDL_GPUDevice        *gpu;
    SDL_GPUShaderFormat   shader_format;
    bool                  gpu_window_claimed;
    SDL_GPUTextureFormat  swapchain_format;
    SDL_GPUTextureFormat  depth_format;
    SDL_GPUTexture       *depth_tex;
    Uint32                depth_w;
    Uint32                depth_h;

    SDL_GPUGraphicsPipeline *water_pipeline;
    SDL_GPUGraphicsPipeline *lit_pipeline;
    SDL_GPUGraphicsPipeline *unlit_pipeline;
    SDL_GPUGraphicsPipeline *shadow_pipeline;
    SDL_GPUGraphicsPipeline *particle_pipeline;
    SDL_GPUGraphicsPipeline *duck_pipeline;
    SDL_GPUGraphicsPipeline *duck_reflection_pipeline;

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
    float                  ring_vertex_scratch[MAX_RIPPLES * (RING_SEGS + 1) * 3];
    float                  particle_vertex_scratch[MAX_RENDER_BILLBOARDS * 6 * 9];
    int                    part_vert_count;

    /* Camera */
    mat4x4 proj;
    mat4x4 view;
    mat4x4 picking_view;
    vec3 camera_target;
    float camera_distance;
    float camera_pitch;
    float camera_yaw;
    bool camera_dragging;
    float last_mouse_x;
    float last_mouse_y;
    bool mouse_captured;
    bool mouse_touch_active;

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
    DuckAnimation duck_animation;
    DuckAvoidanceState duck_avoidance;

    /* Input */
    SDL_Gamepad *gamepad;
    InputState input;
    InteractionQueue interaction_queue;

    /* Activities and reusable pond entities */
    ActivityState activity;
    PondObjectPool pond_objects;
    EnvironmentState environment;

    /* Effects */
    Ripple   ripples[MAX_RIPPLES];
    int      ripple_head;
    Particle particles[MAX_PARTICLES];
    int      particle_head;

    /* Audio */
    SDL_AudioDeviceID  audio_dev;
    SDL_AudioStream   *quack_streams[QUACK_CHANNELS];
    SDL_AudioStream   *splash_streams[SPLASH_CHANNELS];
    SDL_AudioStream   *lily_note_streams[LILY_NOTE_COUNT];
    int                quack_next;
    int                splash_next;
    AudioClip          quack_clips[QUACK_VARIANT_COUNT];
    AudioClip          splash_clip;
    AudioClip          bubble_pop_clip;
    AudioClip          lily_note_clips[LILY_NOTE_COUNT];

    /* Timing */
    Uint64 last_perf;
    Uint64 perf_freq;
    Uint64 interaction_clock_ns;
    Uint64 simulation_tick;
    Uint64 interaction_overflow_count;
    double accumulator;
    float  elapsed;
    bool   suspended;
    bool   event_watch_registered;
    SDL_AtomicInt lifecycle_suspended;
    SDL_AtomicInt lifecycle_reset_pending;

    /* Independent streams keep render frequency from changing simulation RNG. */
    Uint64 simulation_rng;
    Uint64 presentation_rng;
    Uint64 audio_rng;
} AppState;
