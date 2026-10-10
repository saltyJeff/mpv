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
#include "osdep/io.h"
#include "player/client.h"
#include "player/vksf/vksf_internal.h"
#include "video/out/gpu/context.h"
#include "video/out/vulkan/common.h"
#include "video/out/vulkan/context.h"
#include "video/out/vulkan/utils.h"
#include "video/out/wayland_common.h"
#include "video/out/vo.h"

struct priv {
    struct mpvk_ctx vk;
    bool created_surface;
};

struct registry_ctx {
    struct wl_compositor *compositor;
};

static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t id, const char *interface,
                                   uint32_t version)
{
    struct registry_ctx *ctx = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        uint32_t ver = version < 4 ? version : 4;
        ctx->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, ver);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry,
                                          uint32_t id)
{
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
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
    struct vo_wayland_state *wl = ctx->vo->wl;
    if (!wl)
        return false;

    const int32_t width = mp_rect_w(wl->geometry);
    const int32_t height = mp_rect_h(wl->geometry);
    return ra_vk_ctx_resize(ctx, width, height);
}

static bool vksf_wayland_check_visible(struct ra_ctx *ctx)
{
    struct vksf_context *vksf_ctx = get_vksf_ctx(ctx);
    if (!vksf_ctx)
        return false;

    mp_mutex_lock(&vksf_ctx->ready_lock);
    bool visible = vksf_ctx->window_state.visible;
    mp_mutex_unlock(&vksf_ctx->ready_lock);
    return visible;
}

static struct pl_color_space vksf_wayland_preferred_csp(struct ra_ctx *ctx)
{
    return (struct pl_color_space){0};
}

static void vksf_wayland_swap_buffers(struct ra_ctx *ctx)
{
    struct vksf_context *vksf_ctx = get_vksf_ctx(ctx);
    if (!vksf_ctx)
        return;

    mp_mutex_lock(&vksf_ctx->render_lock);
    /* Frame presentation */
    mp_mutex_unlock(&vksf_ctx->render_lock);
}

void vksf_wayland_uninit(struct ra_ctx *ctx)
{
    struct vksf_context *vksf_ctx = get_vksf_ctx(ctx);
    if (vksf_ctx)
        vksf_ctx->vo = NULL;

    struct priv *p = ctx->priv;
    if (!p)
        return;

    ra_vk_ctx_uninit(ctx);
    mpvk_uninit(&p->vk);

    struct vo_wayland_state *wl = ctx->vo->wl;
    if (wl) {
        if (p->created_surface && wl->surface)
            wl_surface_destroy(wl->surface);

        if (wl->compositor)
            wl_compositor_destroy(wl->compositor);

        for (int n = 0; n < 2; n++) {
            if (wl->wakeup_pipe[n] >= 0)
                close(wl->wakeup_pipe[n]);
            wl->wakeup_pipe[n] = -1;
        }

        talloc_free(wl);
        ctx->vo->wl = NULL;
    }
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
        return false;
    }

    struct priv *p = ctx->priv = talloc_zero(ctx, struct priv);
    struct mpvk_ctx *vk = &p->vk;
    int msgl = ctx->opts.probing ? MSGL_V : MSGL_ERR;

    struct vo_wayland_state *wl = talloc_zero(ctx->vo, struct vo_wayland_state);
    ctx->vo->wl = wl;
    wl->vo = ctx->vo;
    wl->display = display;
    wl->display_fd = wl_display_get_fd(display);
    wl->wakeup_pipe[0] = -1;
    wl->wakeup_pipe[1] = -1;
    wl->log = ctx->vo->log;

    if (wl->display_fd < 0) {
        MP_ERR(ctx, "vksf_wayland: Failed to get Wayland display FD\n");
        goto error;
    }

    if (mp_make_wakeup_pipe(wl->wakeup_pipe) < 0) {
        MP_ERR(ctx, "vksf_wayland: Failed to create wakeup pipe\n");
        goto error;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    if (!registry) {
        MP_ERR(ctx, "vksf_wayland: Failed to get Wayland display registry\n");
        goto error;
    }

    struct registry_ctx reg_ctx = {0};
    wl_registry_add_listener(registry, &registry_listener, &reg_ctx);
    wl_display_roundtrip(display);
    wl_registry_destroy(registry);

    if (!reg_ctx.compositor) {
        MP_ERR(ctx, "vksf_wayland: Wayland compositor global not found\n");
        goto error;
    }
    wl->compositor = reg_ctx.compositor;

    if (vksf_ctx->win.window.wayland.surface) {
        wl->surface = vksf_ctx->win.window.wayland.surface;
        p->created_surface = false;
    } else {
        wl->surface = wl_compositor_create_surface(wl->compositor);
        if (!wl->surface) {
            MP_ERR(ctx, "vksf_wayland: Failed to create wl_surface\n");
            goto error;
        }
        p->created_surface = true;

        struct wl_region *empty_region = wl_compositor_create_region(wl->compositor);
        if (empty_region) {
            wl_surface_set_input_region(wl->surface, empty_region);
            wl_region_destroy(empty_region);
        }
    }

    int w = vksf_ctx->width > 0 ? vksf_ctx->width : 1280;
    int h = vksf_ctx->height > 0 ? vksf_ctx->height : 720;
    wl->geometry.x0 = 0;
    wl->geometry.y0 = 0;
    wl->geometry.x1 = w;
    wl->geometry.y1 = h;
    wl->pending_vo_events |= VO_EVENT_RESIZE;

    vksf_ctx->native_surface = wl->surface;
    vksf_ctx->vo = ctx->vo;

    // Signal readiness to mpv_vksf_create
    mp_mutex_lock(&vksf_ctx->ready_lock);
    vksf_ctx->surface_ready = true;
    mp_cond_broadcast(&vksf_ctx->ready_cond);
    mp_mutex_unlock(&vksf_ctx->ready_lock);

    if (!mpvk_init(vk, ctx, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME))
        goto error;

    VkWaylandSurfaceCreateInfoKHR wlinfo = {
        .sType   = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
        .display = wl->display,
        .surface = wl->surface,
    };

    struct ra_ctx_params params = {
        .check_visible = vksf_wayland_check_visible,
        .preferred_csp = vksf_wayland_preferred_csp,
        .swap_buffers  = vksf_wayland_swap_buffers,
    };

    VkInstance inst = vk->vkinst->instance;
    VkResult res = vkCreateWaylandSurfaceKHR(inst, &wlinfo, NULL, &vk->surface);
    if (res != VK_SUCCESS) {
        MP_MSG(ctx, msgl, "Failed creating Wayland Vulkan surface\n");
        goto error;
    }

    if (!ra_vk_ctx_init(ctx, vk, params, VK_PRESENT_MODE_FIFO_KHR))
        goto error;

    ra_add_native_resource(ctx->ra, "wl", wl->display);

    if (!resize(ctx))
        goto error;

    MP_VERBOSE(ctx, "vksf_wayland: Initialized surface size %dx%d\n", w, h);
    return true;

error:
    vksf_wayland_uninit(ctx);
    return false;
}

bool vksf_wayland_reconfig(struct ra_ctx *ctx)
{
    struct vo_wayland_state *wl = ctx->vo->wl;
    if (!wl)
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
            wl->geometry.x0 = 0;
            wl->geometry.y0 = 0;
            wl->geometry.x1 = w;
            wl->geometry.y1 = h;
        }
    }

    return resize(ctx);
}

int vksf_wayland_control(struct ra_ctx *ctx, int *events, int request, void *arg)
{
    struct vo_wayland_state *wl = ctx->vo->wl;
    if (!wl)
        return VO_NOTIMPL;

    struct vksf_context *vksf_ctx = get_vksf_ctx(ctx);
    if (vksf_ctx) {
        int w = 0, h = 0;
        bool pending = false;
        mp_mutex_lock(&vksf_ctx->ready_lock);
        if (vksf_ctx->resize_pending) {
            w = vksf_ctx->width;
            h = vksf_ctx->height;
            pending = true;
            vksf_ctx->resize_pending = false;
        }
        mp_mutex_unlock(&vksf_ctx->ready_lock);

        if (pending && w > 0 && h > 0) {
            wl->geometry.x0 = 0;
            wl->geometry.y0 = 0;
            wl->geometry.x1 = w;
            wl->geometry.y1 = h;
            wl->pending_vo_events |= VO_EVENT_RESIZE;
        }
    }

    if (wl->pending_vo_events) {
        *events |= wl->pending_vo_events;
        wl->pending_vo_events = 0;
    }

    if (*events & VO_EVENT_RESIZE) {
        if (!resize(ctx))
            return VO_ERROR;
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
