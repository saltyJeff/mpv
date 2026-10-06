#ifndef MPV_VKSF_CLIENT_API_H_
#define MPV_VKSF_CLIENT_API_H_

#include <mpv/client.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Platform identifiers for native window/display descriptors.
 */
typedef enum mpv_vksf_platform {
  MPV_VKSF_PLATFORM_NONE = 0,
  MPV_VKSF_PLATFORM_WAYLAND,
  MPV_VKSF_PLATFORM_WIN32,
  MPV_VKSF_PLATFORM_ANDROID,
  MPV_VKSF_PLATFORM_X11,
  MPV_VKSF_PLATFORM_MACOS,
} mpv_vksf_platform;

/**
 * Opaque handle representing one vksf client instance.
 */
struct mpv_vksf;

/**
 * Event listener callback type.
 *
 * Invoked from an internal thread for each mpv_event.
 * Return non-zero to request stopping event processing.
 */
typedef int (*mpv_vksf_event_cb)(struct mpv_vksf *vksf, struct mpv_event *event,
                                 void *userdata);

/**
 * Log message callback type.
 *
 * Invoked from an internal thread when mpv emits a log message.
 */
typedef void (*mpv_vksf_log_cb)(struct mpv_vksf *vksf, const char *prefix,
                                const char *level, mpv_log_level log_level,
                                const char *text, void *userdata);

/**
 * Initialization parameters for mpv_vksf_create().
 */
struct mpv_vksf_window {
  mpv_vksf_platform platform;

  union {
    struct {
      struct wl_display *display; /* Host wl_display connection (required) */
      struct wl_surface
          *surface; /* Host-owned surface, preferred */
      struct wl_compositor *compositor; /* Needed only when surface is NULL */
    } wayland;

    struct {
      void *hinstance; /* HINSTANCE (optional) */
      void *hwnd;      /* HWND (required) */
    } win32;

    struct {
      void *native_window; /* ANativeWindow* (required) */
    } android;

    struct {
      void *display;   /* Display* */
      uint32_t window; /* Window ID */
    } x11;

    struct {
      void *layer; /* CAMetalLayer* */
    } macos;
  } window;
};

/* Host-thread operations for platforms whose UI owns the native display. */
typedef void (*mpv_vksf_host_action)(void *action_userdata);
typedef void (*mpv_vksf_host_executor)(mpv_vksf_host_action action,
                                      void *action_userdata,
                                      void *host_userdata);

struct mpv_vksf_host_services {
  size_t struct_size;
  mpv_vksf_host_executor run_on_host_thread;
  void *userdata;
};

struct mpv_vksf_window_state {
  size_t struct_size;
  int logical_width;
  int logical_height;
  int framebuffer_width;
  int framebuffer_height;
  double scale;
  bool visible;
  bool focused;
  bool fullscreen;
  /* Optional monitor snapshot; resolution uses physical pixels. */
  int display_width;
  int display_height;
  double refresh_rate;
  char display_name[128];
};

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

/**
 * Create a vksf client instance and initialize mpv with internal Vulkan
 * swapchain.
 *
 * Blocks until the VO thread has initialized the video surface.
 *
 * @param params Pointer to initialization parameters.
 * @return New mpv_vksf handle, or NULL on failure.
 */
MPV_EXPORT struct mpv_vksf *mpv_vksf_create(const struct mpv_vksf_window *win,
                                            const char *const (*options)[2],
                                            mpv_vksf_event_cb event_cb,
                                            mpv_vksf_log_cb log_cb,
                                            void *userdata);

MPV_EXPORT struct mpv_vksf *mpv_vksf_create_ex(
    const struct mpv_vksf_window *win,
    const char *const (*options)[2], mpv_vksf_event_cb event_cb,
    mpv_vksf_log_cb log_cb, void *userdata,
    const struct mpv_vksf_host_services *host);

/**
 * Destroy the vksf client instance.
 *
 * Sends quit, blocks until core and worker threads exit, and frees resources.
 */
MPV_EXPORT void mpv_vksf_destroy(struct mpv_vksf *vksf);

/* ── Surface Access & Geometry Synchronization ──────────────────────────── */

/**
 * Retrieve the native video surface handle.
 *
 * On Wayland, returns `struct wl_surface *` (guaranteed non-NULL after
 * creation). On Win32/X11, returns the target window handle.
 */
MPV_EXPORT void *mpv_vksf_get_native_surface(struct mpv_vksf *vksf);

/**
 * Notify mpv of the new render geometry in logical pixels.
 *
 * Triggers VO resize and swapchain reconfiguration.
 */
MPV_EXPORT int mpv_vksf_update_size(struct mpv_vksf *vksf, int width,
                                    int height);

/* Update host-owned geometry and state. Framebuffer dimensions are pixels. */
MPV_EXPORT int mpv_vksf_update_window_state(
    struct mpv_vksf *vksf, const struct mpv_vksf_window_state *state);

/* Refresh display color, profile, scale, and timing information. */
MPV_EXPORT int mpv_vksf_notify_display_change(struct mpv_vksf *vksf);

/**
 * Acquire exclusive lock on the VO render/swap path.
 *
 * Use this to atomically update subsurface geometry/z-order without tearing.
 */
MPV_EXPORT void mpv_vksf_lock_surface(struct mpv_vksf *vksf);

/**
 * Release the exclusive VO render lock.
 */
MPV_EXPORT void mpv_vksf_unlock_surface(struct mpv_vksf *vksf);

/* ── Property & Command Operations ───────────────────────────────────────── */

/** @see mpv_get_property */
MPV_EXPORT int mpv_vksf_get_property(struct mpv_vksf *vksf, const char *name,
                                     mpv_format format, void *data);

/** @see mpv_set_property */
MPV_EXPORT int mpv_vksf_set_property(struct mpv_vksf *vksf, const char *name,
                                     mpv_format format, const void *data);

/** @see mpv_observe_property */
MPV_EXPORT int mpv_vksf_observe_property(struct mpv_vksf *vksf,
                                         uint64_t reply_userdata,
                                         const char *name, mpv_format format);

/** @see mpv_unobserve_property */
MPV_EXPORT int mpv_vksf_unobserve_property(struct mpv_vksf *vksf,
                                           uint64_t registered_reply_userdata);

/** @see mpv_command */
MPV_EXPORT int mpv_vksf_command(struct mpv_vksf *vksf, const char **args);

#ifdef __cplusplus
}
#endif

#endif /* MPV_VKSF_CLIENT_API_H_ */
