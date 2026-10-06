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
#include "video/out/gpu/d3d11_helpers.h"
#include "video/out/vulkan/common.h"
#include "video/out/vulkan/context.h"
#include "video/out/vulkan/utils.h"
#include "video/out/vo.h"
#include "stream/stream.h"
#include "osdep/timer.h"

struct priv {
    struct mpvk_ctx vk;
    struct mp_dxgi_factory_ctx dxgi;
    HWND hwnd;
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
    struct vksf_context *vksf_ctx = get_vksf_ctx(ctx);
    return vksf_ctx && vksf_ctx->visible &&
           IsWindowVisible((HWND)vksf_ctx->win.window.win32.hwnd) &&
           !IsIconic((HWND)vksf_ctx->win.window.win32.hwnd);
}

static struct pl_color_space vksf_windows_preferred_csp(struct ra_ctx *ctx)
{
    struct priv *p = ctx->priv;
    DXGI_OUTPUT_DESC1 desc;
    if (p && mp_dxgi_output_desc_from_hwnd(&p->dxgi, p->hwnd, &desc))
        return mp_dxgi_desc_to_color_space(&desc);
    return (struct pl_color_space){0};
}

static int vksf_windows_color_depth(struct ra_ctx *ctx)
{
    struct priv *p = ctx->priv;
    DXGI_OUTPUT_DESC1 desc;
    return p && mp_dxgi_output_desc_from_hwnd(&p->dxgi, p->hwnd, &desc)
               ? desc.BitsPerColor : -1;
}

static float vksf_windows_ref_luma(struct ra_ctx *ctx)
{
    struct priv *p = ctx->priv;
    return p ? mp_dxgi_sdr_white_level_from_hwnd(&p->dxgi, p->hwnd) : 0;
}

static char *monitor_name(struct ra_ctx *ctx, HWND hwnd)
{
    MONITORINFOEXW info = { .cbSize = sizeof(info) };
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);
    if (!monitor || !GetMonitorInfoW(monitor, (MONITORINFO *)&info))
        return NULL;
    int len = WideCharToMultiByte(CP_UTF8, 0, info.szDevice, -1, NULL, 0,
                                 NULL, NULL);
    if (len <= 0)
        return NULL;
    char *name = talloc_array(ctx, char, len);
    if (!WideCharToMultiByte(CP_UTF8, 0, info.szDevice, -1, name, len,
                             NULL, NULL))
        return NULL;
    return name;
}

static double vksf_windows_display_fps(HWND hwnd)
{
    MONITORINFOEXW info = { .cbSize = sizeof(info) };
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);
    if (!monitor || !GetMonitorInfoW(monitor, (MONITORINFO *)&info))
        return 0;
    DEVMODEW mode = { .dmSize = sizeof(mode) };
    return EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode)
               ? mode.dmDisplayFrequency : 0;
}

static int get_icc_profile(struct ra_ctx *ctx, HWND hwnd, bstr *profile)
{
    char *device = monitor_name(ctx, hwnd);
    if (!device)
        return VO_NOTAVAIL;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, device, -1, NULL, 0);
    wchar_t *wdevice = talloc_array(ctx, wchar_t, wlen);
    if (!wlen || !MultiByteToWideChar(CP_UTF8, 0, device, -1, wdevice, wlen))
        return VO_NOTAVAIL;
    HDC dc = CreateICW(wdevice, NULL, NULL, NULL);
    if (!dc)
        return VO_FALSE;
    DWORD path_len = MAX_PATH;
    wchar_t path[MAX_PATH];
    bool ok = GetICMProfileW(dc, &path_len, path);
    DeleteDC(dc);
    if (!ok)
        return VO_FALSE;
    int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, NULL, 0, NULL, NULL);
    char *utf8_path = talloc_array(ctx, char, len);
    if (!len || !WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8_path, len,
                                     NULL, NULL))
        return VO_FALSE;
    *profile = stream_read_file(utf8_path, NULL, ctx->vo->global, 100000000);
    return profile->len ? VO_TRUE : VO_FALSE;
}

static UINT vksf_windows_dpi(HWND hwnd)
{
    typedef UINT(WINAPI *get_dpi_for_window_fn)(HWND);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    get_dpi_for_window_fn get_dpi = user32
        ? (get_dpi_for_window_fn)GetProcAddress(user32, "GetDpiForWindow")
        : NULL;
    return get_dpi ? get_dpi(hwnd) : 96;
}

void vksf_windows_uninit(struct ra_ctx *ctx)
{
    struct priv *p = ctx->priv;
    if (!p)
        return;

    struct vksf_context *vksf_ctx = get_vksf_ctx(ctx);
    if (vksf_ctx) {
        mp_mutex_lock(&vksf_ctx->ready_lock);
        vksf_ctx->vo = NULL;
        mp_mutex_unlock(&vksf_ctx->ready_lock);
    }
    ra_vk_ctx_uninit(ctx);
    mpvk_uninit(&p->vk);
    mp_dxgi_factory_uninit(&p->dxgi);
    SetThreadExecutionState(ES_CONTINUOUS);
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
        mp_mutex_lock(&vksf_ctx->ready_lock);
        vksf_ctx->surface_failed = true;
        mp_cond_broadcast(&vksf_ctx->ready_cond);
        mp_mutex_unlock(&vksf_ctx->ready_lock);
        return false;
    }

    struct priv *p = ctx->priv = talloc_zero(ctx, struct priv);
    struct mpvk_ctx *vk = &p->vk;
    p->hwnd = hwnd;

    vksf_ctx->native_surface = hwnd;

    if (!mpvk_init(vk, ctx, VK_KHR_WIN32_SURFACE_EXTENSION_NAME))
        goto error;

    VkWin32SurfaceCreateInfoKHR winfo = {
        .sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .hinstance = hinst,
        .hwnd      = hwnd,
    };

    struct ra_ctx_params params = {
        .color_depth = vksf_windows_color_depth,
        .check_visible = vksf_windows_check_visible,
        .preferred_csp = vksf_windows_preferred_csp,
        .preferred_ref_luma = vksf_windows_ref_luma,
    };

    VkInstance inst = vk->vkinst->instance;
    VkResult res = vkCreateWin32SurfaceKHR(inst, &winfo, NULL, &vk->surface);
    if (res != VK_SUCCESS) {
        MP_ERR(ctx, "vksf_windows: Failed creating Win32 Vulkan surface\n");
        goto error;
    }

    if (!ra_vk_ctx_init(ctx, vk, params, VK_PRESENT_MODE_FIFO_KHR))
        goto error;

    // As in vksf_wayland_init(), never initialize the VO at 0x0. Use the
    // runner-owned HWND's physical client size when available, otherwise 720p.
    RECT rect = {0};
    GetClientRect(hwnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width < 1 || height < 1) {
        width = vksf_ctx->width > 0 ? vksf_ctx->width : 1280;
        height = vksf_ctx->height > 0 ? vksf_ctx->height : 720;
    }
    mp_mutex_lock(&vksf_ctx->ready_lock);
    vksf_ctx->visible = IsWindowVisible(hwnd) && !IsIconic(hwnd);
    mp_mutex_unlock(&vksf_ctx->ready_lock);
    if (!ra_vk_ctx_resize(ctx, width, height))
        goto error;

    mp_mutex_lock(&vksf_ctx->ready_lock);
    vksf_ctx->width = width;
    vksf_ctx->height = height;
    vksf_ctx->vo = ctx->vo;
    vksf_ctx->surface_ready = true;
    mp_cond_broadcast(&vksf_ctx->ready_cond);
    mp_mutex_unlock(&vksf_ctx->ready_lock);
    return true;

error:
    vksf_windows_uninit(ctx);
    mp_mutex_lock(&vksf_ctx->ready_lock);
    vksf_ctx->surface_failed = true;
    mp_cond_broadcast(&vksf_ctx->ready_cond);
    mp_mutex_unlock(&vksf_ctx->ready_lock);
    return false;
}

bool vksf_windows_reconfig(struct ra_ctx *ctx)
{
    struct vksf_context *vksf_ctx = get_vksf_ctx(ctx);
    if (!vksf_ctx)
        return false;
    mp_mutex_lock(&vksf_ctx->ready_lock);
    int width = vksf_ctx->width;
    int height = vksf_ctx->height;
    vksf_ctx->resize_pending = false;
    mp_mutex_unlock(&vksf_ctx->ready_lock);
    return width > 0 && height > 0 && ra_vk_ctx_resize(ctx, width, height);
}

int vksf_windows_control(struct ra_ctx *ctx, int *events, int request, void *arg)
{
    struct vksf_context *vksf_ctx = get_vksf_ctx(ctx);
    if (!vksf_ctx)
        return VO_NOTIMPL;

    int width = 0, height = 0;
    bool display_change = false;
    mp_mutex_lock(&vksf_ctx->ready_lock);
    display_change = vksf_ctx->display_change_pending;
    vksf_ctx->display_change_pending = false;
    if (vksf_ctx->resize_pending) {
        width = vksf_ctx->width;
        height = vksf_ctx->height;
        vksf_ctx->resize_pending = false;
    }
    mp_mutex_unlock(&vksf_ctx->ready_lock);

    struct priv *p = ctx->priv;
    if (display_change || (p->dxgi.factory &&
                           !IDXGIFactory1_IsCurrent(p->dxgi.factory))) {
        mp_dxgi_factory_uninit(&p->dxgi);
        *events |= VO_EVENT_RESIZE | VO_EVENT_DPI |
                   VO_EVENT_ICC_PROFILE_CHANGED | VO_EVENT_EXPOSE;
    }

    if (width > 0 && height > 0) {
        *events |= VO_EVENT_RESIZE;
        if (!ra_vk_ctx_resize(ctx, width, height))
            return VO_ERROR;
    }

    HWND hwnd = (HWND)vksf_ctx->win.window.win32.hwnd;
    switch (request) {
    case VOCTRL_GET_WINDOW_ID:
        *(int64_t *)arg = (int64_t)(intptr_t)hwnd;
        return VO_TRUE;
    case VOCTRL_GET_FOCUSED:
        *(bool *)arg = GetForegroundWindow() == GetAncestor(hwnd, GA_ROOT);
        return VO_TRUE;
    case VOCTRL_GET_HIDPI_SCALE:
        *(double *)arg = vksf_windows_dpi(hwnd) / 96.0;
        return VO_TRUE;
    case VOCTRL_GET_DISPLAY_RES: {
        MONITORINFO info = { .cbSize = sizeof(info) };
        HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        if (!monitor || !GetMonitorInfoW(monitor, &info))
            return VO_NOTAVAIL;
        ((int *)arg)[0] = info.rcMonitor.right - info.rcMonitor.left;
        ((int *)arg)[1] = info.rcMonitor.bottom - info.rcMonitor.top;
        return VO_TRUE;
    }
    case VOCTRL_GET_DISPLAY_FPS:
        *(double *)arg = vksf_windows_display_fps(hwnd);
        return VO_TRUE;
    case VOCTRL_GET_DISPLAY_NAMES: {
        char **names = talloc_zero_array(ctx, char *, 2);
        names[0] = monitor_name(ctx, hwnd);
        if (!names[0])
            return VO_NOTAVAIL;
        *(char ***)arg = names;
        return VO_TRUE;
    }
    case VOCTRL_GET_ICC_PROFILE:
        return get_icc_profile(ctx, hwnd, arg);
    case VOCTRL_KILL_SCREENSAVER:
        return SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED |
                                       ES_SYSTEM_REQUIRED)
                   ? VO_TRUE : VO_FALSE;
    case VOCTRL_RESTORE_SCREENSAVER:
        return SetThreadExecutionState(ES_CONTINUOUS) ? VO_TRUE : VO_FALSE;
    }

    return VO_NOTIMPL;
}

void vksf_windows_wakeup(struct ra_ctx *ctx)
{
    // vo_event() already wakes mpv's VO condition variable before this hook.
}

void vksf_windows_wait_events(struct ra_ctx *ctx, int64_t until_time_ns)
{
    if (!ctx->vo)
        return;
    struct vksf_context *vksf_ctx = get_vksf_ctx(ctx);
    struct priv *p = ctx->priv;
    if (p->dxgi.factory && !IDXGIFactory1_IsCurrent(p->dxgi.factory)) {
        mp_dxgi_factory_uninit(&p->dxgi);
        vo_event(ctx->vo, VO_EVENT_DPI | VO_EVENT_RESIZE |
                          VO_EVENT_ICC_PROFILE_CHANGED | VO_EVENT_EXPOSE);
    }
    (void)vksf_ctx;
    until_time_ns = MPMIN(until_time_ns, mp_time_ns() + 1000000000LL);
    vo_wait_default(ctx->vo, until_time_ns);
}

#endif
