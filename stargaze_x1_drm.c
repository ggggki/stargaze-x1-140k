//==============================================================================
// stargaze_drm.h - Userspace API Header
//==============================================================================

#ifndef __STARGAZE_DRM_H
#define __STARGAZE_DRM_H

#include <drm/drm.h>

/* IOCTL magic number */
#define DRM_STARGAZE_MAGIC  0x53  // 'S' for Stargaze

/* IOCTL command codes */
#define STARGAZE_CREATE_CONTEXT    0x00
#define STARGAZE_DESTROY_CONTEXT   0x01
#define STARGAZE_SUBMIT_COMMANDS   0x02
#define STARGAZE_CREATE_BUFFER     0x03
#define STARGAZE_MAP_BUFFER        0x04
#define STARGAZE_GET_STATS         0x05
#define STARGAZE_WAIT_FENCE        0x06

/* IOCTL macros */
#define DRM_IOCTL_STARGAZE_CREATE_CONTEXT \
    DRM_IOWR(DRM_COMMAND_BASE + STARGAZE_CREATE_CONTEXT, \
             struct drm_stargaze_create_context)
#define DRM_IOCTL_STARGAZE_DESTROY_CONTEXT \
    DRM_IOW (DRM_COMMAND_BASE + STARGAZE_DESTROY_CONTEXT, \
             struct drm_stargaze_destroy_context)
#define DRM_IOCTL_STARGAZE_SUBMIT_COMMANDS \
    DRM_IOWR(DRM_COMMAND_BASE + STARGAZE_SUBMIT_COMMANDS, \
             struct drm_stargaze_submit_commands)
#define DRM_IOCTL_STARGAZE_CREATE_BUFFER \
    DRM_IOWR(DRM_COMMAND_BASE + STARGAZE_CREATE_BUFFER, \
             struct drm_stargaze_create_buffer)
#define DRM_IOCTL_STARGAZE_MAP_BUFFER \
    DRM_IOWR(DRM_COMMAND_BASE + STARGAZE_MAP_BUFFER, \
             struct drm_stargaze_map_buffer)
#define DRM_IOCTL_STARGAZE_GET_STATS \
    DRM_IOR (DRM_COMMAND_BASE + STARGAZE_GET_STATS, \
             struct drm_stargaze_stats)
#define DRM_IOCTL_STARGAZE_WAIT_FENCE \
    DRM_IOWR(DRM_COMMAND_BASE + STARGAZE_WAIT_FENCE, \
             struct drm_stargaze_wait_fence)

/* IOCTL data structures */
struct drm_stargaze_create_context {
    __u32 ctx_id;                   // Output: new context ID
    __u32 flags;                    // Reserved
};

struct drm_stargaze_destroy_context {
    __u32 ctx_id;
    __u32 pad;
};

struct drm_stargaze_submit_commands {
    __u32 ctx_id;                   // GPU context ID
    __u32 num_commands;             // Number of commands
    __u64 commands;                 // Pointer to command buffer
    __u64 out_fence;                // Output fence value
    __u64 flags;
};

struct drm_stargaze_create_buffer {
    __u32 size;                     // Buffer size in bytes
    __u32 handle;                   // Output GEM handle
    __u64 flags;
};

struct drm_stargaze_map_buffer {
    __u32 ctx_id;                   // GPU context
    __u32 handle;                   // GEM handle
    __u64 gpu_addr;                 // Output: GPU virtual address
    __u64 flags;
};

struct drm_stargaze_stats {
    __u32 gpu_freq_mhz;
    __u32 gpu_temp_celsius;
    __u32 gpu_utilization;          // 0-100%
    __u32 ring_used;                // Ring buffer utilization
    __u64 frames_rendered;
    __u64 commands_processed;
    __u64 gpu_faults;
    __u64 reserved[4];
};

struct drm_stargaze_wait_fence {
    __u32 ctx_id;
    __u32 pad;
    __u64 fence;
    __u64 timeout_ns;               // 0 = no wait, U64_MAX = forever
    __u64 completed;                // Output: 1 if completed
};

#endif /* __STARGAZE_DRM_H */