/*
 * Common Vulkan Surface (vksf) VO Context Wrapper
 */

#include "common/global.h"
#include "common/msg.h"
#include "player/client.h"
#include "player/vksf/vksf_internal.h"
#include "video/out/gpu/context.h"

static struct vksf_context *get_vksf_ctx(struct ra_ctx *ctx)
{
    struct mp_client_api *ca = (ctx && ctx->vo && ctx->vo->global)
                                   ? ctx->vo->global->client_api
                                   : NULL;
    return mp_client_api_get_vksf_ctx(ca);
}



static void vksf_vo_uninit(struct ra_ctx *ctx)
{
    struct vksf_context *vksf_ctx = get_vksf_ctx(ctx);
    if (!vksf_ctx)
        return;

    switch (vksf_ctx->params.platform) {
#if HAVE_WAYLAND
    case MPV_VKSF_PLATFORM_WAYLAND:
        vksf_wayland_uninit(ctx);
        break;
#endif
#if HAVE_WIN32_DESKTOP
    case MPV_VKSF_PLATFORM_WIN32:
        vksf_windows_uninit(ctx);
        break;
#endif
#if HAVE_ANDROID
    case MPV_VKSF_PLATFORM_ANDROID:
        vksf_android_uninit(ctx);
        break;
#endif
    default:
        break;
    }
}

static bool vksf_vo_init(struct ra_ctx *ctx)
{
    struct vksf_context *vksf_ctx = get_vksf_ctx(ctx);
    if (!vksf_ctx) {
        MP_ERR(ctx, "vksf: No vksf context found on client API\n");
        return false;
    }

    switch (vksf_ctx->params.platform) {
#if HAVE_WAYLAND
    case MPV_VKSF_PLATFORM_WAYLAND:
        return vksf_wayland_init(ctx);
#endif
#if HAVE_WIN32_DESKTOP
    case MPV_VKSF_PLATFORM_WIN32:
        return vksf_windows_init(ctx);
#endif
#if HAVE_ANDROID
    case MPV_VKSF_PLATFORM_ANDROID:
        return vksf_android_init(ctx);
#endif
    default:
        MP_ERR(ctx, "vksf: Unsupported or unconfigured platform (%d)\n",
               vksf_ctx->params.platform);
        return false;
    }
}

static bool vksf_vo_reconfig(struct ra_ctx *ctx)
{
    struct vksf_context *vksf_ctx = get_vksf_ctx(ctx);
    if (!vksf_ctx)
        return true;

    switch (vksf_ctx->params.platform) {
#if HAVE_WAYLAND
    case MPV_VKSF_PLATFORM_WAYLAND:
        return vksf_wayland_reconfig(ctx);
#endif
    default:
        return true;
    }
}

static int vksf_vo_control(struct ra_ctx *ctx, int *events, int request, void *arg)
{
    struct vksf_context *vksf_ctx = get_vksf_ctx(ctx);
    if (!vksf_ctx)
        return VO_NOTIMPL;

    switch (vksf_ctx->params.platform) {
#if HAVE_WAYLAND
    case MPV_VKSF_PLATFORM_WAYLAND:
        return vksf_wayland_control(ctx, events, request, arg);
#endif
#if HAVE_WIN32_DESKTOP
    case MPV_VKSF_PLATFORM_WIN32:
        return vksf_windows_control(ctx, events, request, arg);
#endif
#if HAVE_ANDROID
    case MPV_VKSF_PLATFORM_ANDROID:
        return vksf_android_control(ctx, events, request, arg);
#endif
    default:
        return VO_NOTIMPL;
    }
}

static void vksf_vo_wakeup(struct ra_ctx *ctx)
{
    struct vksf_context *vksf_ctx = get_vksf_ctx(ctx);
    if (!vksf_ctx)
        return;

    switch (vksf_ctx->params.platform) {
#if HAVE_WAYLAND
    case MPV_VKSF_PLATFORM_WAYLAND:
        vksf_wayland_wakeup(ctx);
        break;
#endif
#if HAVE_WIN32_DESKTOP
    case MPV_VKSF_PLATFORM_WIN32:
        vksf_windows_wakeup(ctx);
        break;
#endif
#if HAVE_ANDROID
    case MPV_VKSF_PLATFORM_ANDROID:
        vksf_android_wakeup(ctx);
        break;
#endif
    default:
        break;
    }
}

static void vksf_vo_wait_events(struct ra_ctx *ctx, int64_t until_time_ns)
{
    struct vksf_context *vksf_ctx = get_vksf_ctx(ctx);
    if (!vksf_ctx)
        return;

    switch (vksf_ctx->params.platform) {
#if HAVE_WAYLAND
    case MPV_VKSF_PLATFORM_WAYLAND:
        vksf_wayland_wait_events(ctx, until_time_ns);
        break;
#endif
#if HAVE_WIN32_DESKTOP
    case MPV_VKSF_PLATFORM_WIN32:
        vksf_windows_wait_events(ctx, until_time_ns);
        break;
#endif
#if HAVE_ANDROID
    case MPV_VKSF_PLATFORM_ANDROID:
        vksf_android_wait_events(ctx, until_time_ns);
        break;
#endif
    default:
        break;
    }
}

const struct ra_ctx_fns ra_ctx_vksf = {
    .type               = "vulkan",
    .name               = "vksf",
    .description        = "Vulkan Surface (vksf client API)",
    .reconfig           = vksf_vo_reconfig,
    .control            = vksf_vo_control,
    .wakeup             = vksf_vo_wakeup,
    .wait_events        = vksf_vo_wait_events,
    .init               = vksf_vo_init,
    .uninit             = vksf_vo_uninit,
};
