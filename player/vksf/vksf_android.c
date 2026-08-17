/*
 * Vulkan Surface (vksf) Android Backend
 */

#include "config.h"

#if HAVE_ANDROID

#include <stdbool.h>
#include <stdint.h>
#include <android/native_window.h>

#include "common/global.h"
#include "common/msg.h"
#include "player/client.h"
#include "player/vksf/vksf_internal.h"
#include "video/out/gpu/context.h"
#include "video/out/vulkan/common.h"
#include "video/out/vulkan/context.h"
#include "video/out/vulkan/utils.h"

struct priv {
    struct mpvk_ctx vk;
};

static struct vksf_context *get_vksf_ctx(struct ra_ctx *ctx)
{
    struct mp_client_api *ca = (ctx && ctx->vo && ctx->vo->global)
                                   ? ctx->vo->global->client_api
                                   : NULL;
    return mp_client_api_get_vksf_ctx(ca);
}

static bool vksf_android_check_visible(struct ra_ctx *ctx)
{
    return true;
}

static struct pl_color_space vksf_android_preferred_csp(struct ra_ctx *ctx)
{
    struct pl_color_space csp = {0};
    return csp;
}

static void vksf_android_swap_buffers(struct ra_ctx *ctx)
{
    struct vksf_context *vksf_ctx = get_vksf_ctx(ctx);
    if (!vksf_ctx)
        return;

    mp_mutex_lock(&vksf_ctx->render_lock);
    // Android buffer presentation
    mp_mutex_unlock(&vksf_ctx->render_lock);
}

void vksf_android_uninit(struct ra_ctx *ctx)
{
    struct priv *p = ctx->priv;
    if (!p)
        return;

    ra_vk_ctx_uninit(ctx);
    mpvk_uninit(&p->vk);
}

bool vksf_android_init(struct ra_ctx *ctx)
{
    struct vksf_context *vksf_ctx = get_vksf_ctx(ctx);
    if (!vksf_ctx)
        return false;

    ANativeWindow *win = (ANativeWindow *)vksf_ctx->win.window.android.native_window;
    if (!win) {
        MP_ERR(ctx, "vksf_android: No ANativeWindow provided\n");
        return false;
    }

    struct priv *p = ctx->priv = talloc_zero(ctx, struct priv);
    struct mpvk_ctx *vk = &p->vk;

    vksf_ctx->native_surface = win;

    mp_mutex_lock(&vksf_ctx->ready_lock);
    vksf_ctx->surface_ready = true;
    mp_cond_broadcast(&vksf_ctx->ready_cond);
    mp_mutex_unlock(&vksf_ctx->ready_lock);

    if (!mpvk_init(vk, ctx, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME))
        goto error;

    VkAndroidSurfaceCreateInfoKHR ainfo = {
        .sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .window = win,
    };

    struct ra_ctx_params params = {
        .check_visible = vksf_android_check_visible,
        .preferred_csp = vksf_android_preferred_csp,
        .swap_buffers  = vksf_android_swap_buffers,
    };

    VkInstance inst = vk->vkinst->instance;
    VkResult res = vkCreateAndroidSurfaceKHR(inst, &ainfo, NULL, &vk->surface);
    if (res != VK_SUCCESS) {
        MP_ERR(ctx, "vksf_android: Failed creating Android Vulkan surface\n");
        goto error;
    }

    if (!ra_vk_ctx_init(ctx, vk, params, VK_PRESENT_MODE_FIFO_KHR))
        goto error;

    return true;

error:
    vksf_android_uninit(ctx);
    return false;
}

int vksf_android_control(struct ra_ctx *ctx, int *events, int request, void *arg)
{
    return VO_NOTIMPL;
}

void vksf_android_wakeup(struct ra_ctx *ctx)
{
}

void vksf_android_wait_events(struct ra_ctx *ctx, int64_t until_time_ns)
{
}

#endif
