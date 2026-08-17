/*
 * Vulkan Surface (vksf) Windows Backend
 */

#include "config.h"

#if HAVE_WIN32_DESKTOP

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

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

static bool vksf_windows_check_visible(struct ra_ctx *ctx)
{
    return true;
}

static struct pl_color_space vksf_windows_preferred_csp(struct ra_ctx *ctx)
{
    struct pl_color_space csp = {0};
    return csp;
}

static void vksf_windows_swap_buffers(struct ra_ctx *ctx)
{
    struct vksf_context *vksf_ctx = get_vksf_ctx(ctx);
    if (!vksf_ctx)
        return;

    mp_mutex_lock(&vksf_ctx->render_lock);
    // Win32 buffer presentation
    mp_mutex_unlock(&vksf_ctx->render_lock);
}

void vksf_windows_uninit(struct ra_ctx *ctx)
{
    struct priv *p = ctx->priv;
    if (!p)
        return;

    ra_vk_ctx_uninit(ctx);
    mpvk_uninit(&p->vk);
}

bool vksf_windows_init(struct ra_ctx *ctx)
{
    struct vksf_context *vksf_ctx = get_vksf_ctx(ctx);
    if (!vksf_ctx)
        return false;

    HWND hwnd = (HWND)vksf_ctx->win.window.win32.hwnd;
    HINSTANCE hinst = (HINSTANCE)vksf_ctx->win.window.win32.hinstance;
    if (!hinst)
        hinst = GetModuleHandle(NULL);

    if (!hwnd) {
        MP_ERR(ctx, "vksf_windows: No HWND provided\n");
        return false;
    }

    struct priv *p = ctx->priv = talloc_zero(ctx, struct priv);
    struct mpvk_ctx *vk = &p->vk;

    vksf_ctx->native_surface = hwnd;

    mp_mutex_lock(&vksf_ctx->ready_lock);
    vksf_ctx->surface_ready = true;
    mp_cond_broadcast(&vksf_ctx->ready_cond);
    mp_mutex_unlock(&vksf_ctx->ready_lock);

    if (!mpvk_init(vk, ctx, VK_KHR_WIN32_SURFACE_EXTENSION_NAME))
        goto error;

    VkWin32SurfaceCreateInfoKHR winfo = {
        .sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .hinstance = hinst,
        .hwnd      = hwnd,
    };

    struct ra_ctx_params params = {
        .check_visible = vksf_windows_check_visible,
        .preferred_csp = vksf_windows_preferred_csp,
        .swap_buffers  = vksf_windows_swap_buffers,
    };

    VkInstance inst = vk->vkinst->instance;
    VkResult res = vkCreateWin32SurfaceKHR(inst, &winfo, NULL, &vk->surface);
    if (res != VK_SUCCESS) {
        MP_ERR(ctx, "vksf_windows: Failed creating Win32 Vulkan surface\n");
        goto error;
    }

    if (!ra_vk_ctx_init(ctx, vk, params, VK_PRESENT_MODE_FIFO_KHR))
        goto error;

    return true;

error:
    vksf_windows_uninit(ctx);
    return false;
}

int vksf_windows_control(struct ra_ctx *ctx, int *events, int request, void *arg)
{
    return VO_NOTIMPL;
}

void vksf_windows_wakeup(struct ra_ctx *ctx)
{
}

void vksf_windows_wait_events(struct ra_ctx *ctx, int64_t until_time_ns)
{
}

#endif
