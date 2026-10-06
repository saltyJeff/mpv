/*
 * Vulkan Surface (vksf) Wayland Backend
 */

#include "config.h"

#if HAVE_WAYLAND

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

#include "common/global.h"
#include "common/msg.h"
#include "player/client.h"
#include "player/vksf/vksf_internal.h"
#include "video/out/gpu/context.h"
#include "video/out/vulkan/common.h"
#include "video/out/vulkan/context.h"
#include "video/out/vulkan/utils.h"
#include "video/out/vo.h"

struct priv {
    struct mpvk_ctx vk;
    struct wl_display *display;
    struct wl_surface *surface;
    int width, height;
};

static struct vksf_context *get_vksf_ctx(struct ra_ctx *ctx)
{
    struct mp_client_api *ca = (ctx && ctx->vo && ctx->vo->global)
                                   ? ctx->vo->global->client_api
                                   : NULL;
    return mp_client_api_get_vksf_ctx(ca);
}

static bool resize(struct ra_ctx *ctx)
{
    struct priv *p = ctx->priv;
    if (!p)
        return false;
    return ra_vk_ctx_resize(ctx, p->width, p->height);
}

static bool vksf_wayland_check_visible(struct ra_ctx *ctx)
{
    struct vksf_context *v = get_vksf_ctx(ctx);
    if (!v)
        return false;
    mp_mutex_lock(&v->ready_lock);
    bool visible = v->visible;
    mp_mutex_unlock(&v->ready_lock);
    return visible;
}

static struct pl_color_space vksf_wayland_preferred_csp(struct ra_ctx *ctx)
{
    struct pl_color_space csp = {0};
    return csp;
}

void vksf_wayland_uninit(struct ra_ctx *ctx)
{
    struct vksf_context *vksf_ctx = get_vksf_ctx(ctx);
    if (vksf_ctx) {
        mp_mutex_lock(&vksf_ctx->ready_lock);
        vksf_ctx->vo = NULL;
        mp_mutex_unlock(&vksf_ctx->ready_lock);
    }

    struct priv *p = ctx->priv;
    if (!p)
        return;

    ra_vk_ctx_uninit(ctx);
    mpvk_uninit(&p->vk);

}

bool vksf_wayland_init(struct ra_ctx *ctx)
{
    struct vksf_context *vksf_ctx = get_vksf_ctx(ctx);
    if (!vksf_ctx) {
        MP_ERR(ctx, "vksf_wayland: No vksf context found\n");
        return false;
    }

    struct wl_display *display = vksf_ctx->win.window.wayland.display;
    if (!display) {
        MP_ERR(ctx, "vksf_wayland: No wl_display provided in win\n");
        mp_mutex_lock(&vksf_ctx->ready_lock);
        vksf_ctx->surface_failed = true;
        mp_cond_broadcast(&vksf_ctx->ready_cond);
        mp_mutex_unlock(&vksf_ctx->ready_lock);
        return false;
    }

    struct priv *p = ctx->priv = talloc_zero(ctx, struct priv);
    struct mpvk_ctx *vk = &p->vk;
    int msgl = ctx->opts.probing ? MSGL_V : MSGL_ERR;

    p->display = display;
    p->surface = vksf_ctx->win.window.wayland.surface;
    if (!p->surface) {
        MP_ERR(ctx, "vksf_wayland: host must provide a GTK-owned wl_surface\n");
        goto error;
    }

    int w = vksf_ctx->width > 0 ? vksf_ctx->width : 1280;
    int h = vksf_ctx->height > 0 ? vksf_ctx->height : 720;
    p->width = w;
    p->height = h;

    vksf_ctx->native_surface = p->surface;
    vksf_ctx->vo = ctx->vo;

    if (!mpvk_init(vk, ctx, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME))
        goto error;

    VkWaylandSurfaceCreateInfoKHR wlinfo = {
        .sType   = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
        .display = p->display,
        .surface = p->surface,
    };

    struct ra_ctx_params params = {
        .check_visible = vksf_wayland_check_visible,
        .preferred_csp = vksf_wayland_preferred_csp,
    };

    VkInstance inst = vk->vkinst->instance;
    VkResult res = vkCreateWaylandSurfaceKHR(inst, &wlinfo, NULL, &vk->surface);
    if (res != VK_SUCCESS) {
        MP_MSG(ctx, msgl, "Failed creating Wayland Vulkan surface\n");
        goto error;
    }

    if (!ra_vk_ctx_init(ctx, vk, params, VK_PRESENT_MODE_FIFO_KHR))
        goto error;

    ra_add_native_resource(ctx->ra, "wl", p->display);

    if (!resize(ctx))
        goto error;

    mp_mutex_lock(&vksf_ctx->ready_lock);
    vksf_ctx->surface_ready = true;
    mp_cond_broadcast(&vksf_ctx->ready_cond);
    mp_mutex_unlock(&vksf_ctx->ready_lock);

    MP_VERBOSE(ctx, "vksf_wayland: Initialized surface size %dx%d\n", w, h);
    return true;

error:
    mp_mutex_lock(&vksf_ctx->ready_lock);
    vksf_ctx->surface_failed = true;
    mp_cond_broadcast(&vksf_ctx->ready_cond);
    mp_mutex_unlock(&vksf_ctx->ready_lock);
    vksf_wayland_uninit(ctx);
    return false;
}

bool vksf_wayland_reconfig(struct ra_ctx *ctx)
{
    struct priv *p = ctx->priv;
    if (!p)
        return false;

    struct vksf_context *vksf_ctx = get_vksf_ctx(ctx);
    if (vksf_ctx) {
        int w = 0, h = 0;
        mp_mutex_lock(&vksf_ctx->ready_lock);
        w = vksf_ctx->width;
        h = vksf_ctx->height;
        vksf_ctx->resize_pending = false;
        mp_mutex_unlock(&vksf_ctx->ready_lock);

        if (w > 0 && h > 0) {
            p->width = w;
            p->height = h;
        }
    }

    return resize(ctx);
}

int vksf_wayland_control(struct ra_ctx *ctx, int *events, int request, void *arg)
{
    struct priv *p = ctx->priv;
    if (!p)
        return VO_NOTIMPL;

    struct vksf_context *vksf_ctx = get_vksf_ctx(ctx);
    if (vksf_ctx) {
        int w = 0, h = 0;
        bool pending = false;
        bool display_change = false;
        mp_mutex_lock(&vksf_ctx->ready_lock);
        display_change = vksf_ctx->display_change_pending;
        vksf_ctx->display_change_pending = false;
        if (vksf_ctx->resize_pending) {
            w = vksf_ctx->width;
            h = vksf_ctx->height;
            pending = true;
            vksf_ctx->resize_pending = false;
        }
        mp_mutex_unlock(&vksf_ctx->ready_lock);

        if (pending && w > 0 && h > 0) {
            p->width = w;
            p->height = h;
            *events |= VO_EVENT_RESIZE;
        }
        if (display_change)
            *events |= VO_EVENT_DPI | VO_EVENT_ICC_PROFILE_CHANGED |
                       VO_EVENT_EXPOSE;
    }

    if (*events & VO_EVENT_RESIZE) {
        if (!resize(ctx))
            return VO_ERROR;
    }

    struct vksf_context *v = get_vksf_ctx(ctx);
    if (!v)
        return VO_NOTIMPL;
    switch (request) {
    case VOCTRL_GET_WINDOW_ID:
        *(int64_t *)arg = (int64_t)(intptr_t)p->surface;
        return VO_TRUE;
    case VOCTRL_GET_FOCUSED:
        mp_mutex_lock(&v->ready_lock);
        *(bool *)arg = v->focused;
        mp_mutex_unlock(&v->ready_lock);
        return VO_TRUE;
    case VOCTRL_GET_HIDPI_SCALE:
        mp_mutex_lock(&v->ready_lock);
        *(double *)arg = v->scale;
        mp_mutex_unlock(&v->ready_lock);
        return VO_TRUE;
    case VOCTRL_GET_DISPLAY_RES: {
        mp_mutex_lock(&v->ready_lock);
        ((int *)arg)[0] = v->display_width;
        ((int *)arg)[1] = v->display_height;
        bool have_resolution = v->display_width > 0 && v->display_height > 0;
        mp_mutex_unlock(&v->ready_lock);
        return have_resolution ? VO_TRUE : VO_NOTAVAIL;
    }
    case VOCTRL_GET_DISPLAY_FPS: {
        mp_mutex_lock(&v->ready_lock);
        *(double *)arg = v->refresh_rate;
        bool have_refresh = v->refresh_rate > 0;
        mp_mutex_unlock(&v->ready_lock);
        return have_refresh ? VO_TRUE : VO_NOTAVAIL;
    }
    case VOCTRL_GET_DISPLAY_NAMES: {
        char **names = talloc_zero_array(ctx, char *, 2);
        mp_mutex_lock(&v->ready_lock);
        names[0] = v->display_name[0]
                       ? talloc_strdup(names, v->display_name) : NULL;
        mp_mutex_unlock(&v->ready_lock);
        if (!names[0]) {
            talloc_free(names);
            return VO_NOTAVAIL;
        }
        *(char ***)arg = names;
        return VO_TRUE;
    }
    }

    return VO_NOTIMPL;
}

void vksf_wayland_wakeup(struct ra_ctx *ctx)
{
    (void)ctx;
    // vo_event() already wakes the VO's condition variable before invoking
    // this callback. Do not dispatch or flush GTK's Wayland connection here.
}

void vksf_wayland_wait_events(struct ra_ctx *ctx, int64_t until_time_ns)
{
    if (ctx->vo)
        vo_wait_default(ctx->vo, until_time_ns);
}

#endif
