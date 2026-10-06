/*
 * Vulkan Surface (vksf) Windows Backend
 */

#include "config.h"

#if HAVE_WIN32_DESKTOP

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
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
    DXGI_OUTPUT_DESC1 last_output;
    float last_ref_luma;
    int64_t last_display_check_ns;
    bool have_output_snapshot;
    wchar_t last_icc_path[MAX_PATH];
    FILETIME last_icc_write_time;
    bool have_icc_snapshot;
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

static bool output_desc_changed(const DXGI_OUTPUT_DESC1 *a,
                                const DXGI_OUTPUT_DESC1 *b)
{
    return a->Monitor != b->Monitor || a->BitsPerColor != b->BitsPerColor ||
           a->ColorSpace != b->ColorSpace ||
           a->MinLuminance != b->MinLuminance ||
           a->MaxLuminance != b->MaxLuminance ||
           a->MaxFullFrameLuminance != b->MaxFullFrameLuminance ||
           memcmp(a->RedPrimary, b->RedPrimary, sizeof(a->RedPrimary)) ||
           memcmp(a->GreenPrimary, b->GreenPrimary, sizeof(a->GreenPrimary)) ||
           memcmp(a->BluePrimary, b->BluePrimary, sizeof(a->BluePrimary)) ||
           memcmp(a->WhitePoint, b->WhitePoint, sizeof(a->WhitePoint));
}

// DXGI factory invalidation also covers display color setting changes that do
// not generate a window message (including SDR reference white adjustments).
static bool refresh_output_snapshot(struct ra_ctx *ctx, bool force)
{
    struct priv *p = ctx->priv;
    int64_t now = mp_time_ns();
    if (!force && now - p->last_display_check_ns < 1000000000LL)
        return false;
    p->last_display_check_ns = now;
    DXGI_OUTPUT_DESC1 desc;
    if (!mp_dxgi_output_desc_from_hwnd(&p->dxgi, p->hwnd, &desc)) {
        bool changed = p->have_output_snapshot;
        p->have_output_snapshot = false;
        return changed;
    }

    float ref_luma = mp_dxgi_sdr_white_level_from_hwnd(&p->dxgi, p->hwnd);
    bool changed = p->have_output_snapshot &&
        (output_desc_changed(&p->last_output, &desc) ||
         p->last_ref_luma != ref_luma);
    p->last_output = desc;
    p->last_ref_luma = ref_luma;
    p->have_output_snapshot = true;

    MONITORINFOEXW monitor = { .cbSize = sizeof(monitor) };
    wchar_t profile[MAX_PATH] = {0};
    FILETIME write_time = {0};
    bool have_profile = false;
    HDC dc = NULL;
    if (desc.Monitor && GetMonitorInfoW(desc.Monitor, (MONITORINFO *)&monitor))
        dc = CreateICW(L"DISPLAY", monitor.szDevice, NULL, NULL);
    if (dc) {
        DWORD length = MAX_PATH;
        WIN32_FILE_ATTRIBUTE_DATA attributes;
        have_profile = GetICMProfileW(dc, &length, profile) &&
            GetFileAttributesExW(profile, GetFileExInfoStandard, &attributes);
        if (have_profile) {
            write_time = attributes.ftLastWriteTime;
        }
        DeleteDC(dc);
    }
    bool icc_changed = p->have_icc_snapshot != have_profile;
    if (have_profile) {
        icc_changed |= wcscmp(p->last_icc_path, profile) != 0 ||
            p->last_icc_write_time.dwHighDateTime != write_time.dwHighDateTime ||
            p->last_icc_write_time.dwLowDateTime != write_time.dwLowDateTime;
        wcsncpy(p->last_icc_path, profile, MAX_PATH - 1);
        p->last_icc_path[MAX_PATH - 1] = L'\0';
        p->last_icc_write_time = write_time;
    } else {
        p->last_icc_path[0] = L'\0';
        memset(&p->last_icc_write_time, 0, sizeof(p->last_icc_write_time));
    }
    p->have_icc_snapshot = have_profile;
    changed |= p->have_output_snapshot && icc_changed;
    return changed;
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
    HDC dc = CreateICW(L"DISPLAY", wdevice, NULL, NULL);
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
    if (display_change) {
        mp_dxgi_factory_uninit(&p->dxgi);
        p->have_output_snapshot = false;
    }
    if (refresh_output_snapshot(ctx, display_change)) {
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
    if (refresh_output_snapshot(ctx, false)) {
        vo_event(ctx->vo, VO_EVENT_DPI | VO_EVENT_RESIZE |
                          VO_EVENT_ICC_PROFILE_CHANGED | VO_EVENT_EXPOSE);
        return;
    }
    until_time_ns = MPMIN(until_time_ns, mp_time_ns() + 1000000000LL);
    vo_wait_default(ctx->vo, until_time_ns);
}

#endif
