#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <stdatomic.h>

#include "mpv/vksf.h"
#include "osdep/threads.h"
#include "video/out/gpu/context.h"
#include "video/out/vulkan/common.h"

struct vksf_context {
    struct mpv_vksf_window win;

    mp_mutex ready_lock;
    mp_cond ready_cond;
    bool surface_ready;

    mp_mutex render_lock;

    void *native_surface;

    int width;
    int height;
    bool resize_pending;

    void *platform_priv;
};

/* Platform backend interfaces */
#if HAVE_WAYLAND
bool vksf_wayland_init(struct ra_ctx *ctx);
void vksf_wayland_uninit(struct ra_ctx *ctx);
bool vksf_wayland_reconfig(struct ra_ctx *ctx);
int vksf_wayland_control(struct ra_ctx *ctx, int *events, int request, void *arg);
void vksf_wayland_wakeup(struct ra_ctx *ctx);
void vksf_wayland_wait_events(struct ra_ctx *ctx, int64_t until_time_ns);
#endif

#if HAVE_WIN32_DESKTOP
bool vksf_windows_init(struct ra_ctx *ctx);
void vksf_windows_uninit(struct ra_ctx *ctx);
int vksf_windows_control(struct ra_ctx *ctx, int *events, int request, void *arg);
void vksf_windows_wakeup(struct ra_ctx *ctx);
void vksf_windows_wait_events(struct ra_ctx *ctx, int64_t until_time_ns);
#endif

#if HAVE_ANDROID
bool vksf_android_init(struct ra_ctx *ctx);
void vksf_android_uninit(struct ra_ctx *ctx);
int vksf_android_control(struct ra_ctx *ctx, int *events, int request, void *arg);
void vksf_android_wakeup(struct ra_ctx *ctx);
void vksf_android_wait_events(struct ra_ctx *ctx, int64_t until_time_ns);
#endif

