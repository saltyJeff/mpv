/*
 * Vulkan Surface (vksf) client implementation
 */

#include <locale.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mpv/vksf.h"
#include "player/vksf/vksf_internal.h"
#include "common/global.h"
#include "common/msg.h"
#include "player/client.h"

struct mpv_vksf {
    struct mpv_handle *mpv;
    struct vksf_context *vksf_ctx;
    void *userdata;
    mpv_vksf_event_cb event_cb;
    mpv_vksf_log_cb log_cb;
    pthread_t event_thread;
    atomic_bool stop_thread;
    bool has_thread;
};

static void *event_thread_proc(void *arg)
{
    struct mpv_vksf *vksf = arg;
    while (!atomic_load(&vksf->stop_thread)) {
        struct mpv_event *event = mpv_wait_event(vksf->mpv, 0.1);
        if (!event || event->event_id == MPV_EVENT_NONE)
            continue;

        if (event->event_id == MPV_EVENT_LOG_MESSAGE && vksf->log_cb) {
            struct mpv_event_log_message *msg = event->data;
            if (msg) {
                vksf->log_cb(vksf, msg->prefix, msg->level,
                             msg->log_level, msg->text, vksf->userdata);
            }
        }

        if (vksf->event_cb) {
            int stop = vksf-Explore>event_cb(vksf, event, vksf->userdata);
            if (stop)
                break;
        }

        if (event->event_id == MPV_EVENT_SHUTDOWN)
            break;
    }
    return NULL;
}

struct mpv_vksf *mpv_vksf_create(const struct mpv_vksf_window *win,
                                const char *const (*options)[2],
                                mpv_vksf_event_cb event_cb,
                                mpv_vksf_log_cb log_cb,
                                void *userdata)
{
    if (!win)
        return NULL;

    setlocale(LC_NUMERIC, "C");

    struct mpv_vksf *vksf = calloc(1, sizeof(*vksf));
    if (!vksf)
        return NULL;

    struct vksf_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        free(vksf);
        return NULL;
    }

    vksf->vksf_ctx = ctx;
    ctx->win = *win;
    mp_mutex_init(&ctx->ready_lock);
    mp_cond_init(&ctx->ready_cond);
    mp_mutex_init(&ctx->render_lock);

    vksf->mpv = mpv_create();
    if (!vksf->mpv) {
        mp_mutex_destroy(&ctx->ready_lock);
        mp_cond_destroy(&ctx->ready_cond);
        mp_mutex_destroy(&ctx->render_lock);
        free(ctx);
        free(vksf);
        return NULL;
    }

    vksf->userdata = userdata;
    vksf->event_cb = event_cb;
    vksf->log_cb = log_cb;

    // Apply forced internal options
    mpv_set_option_string(vksf->mpv, "vo", "gpu-next");
    mpv_set_option_string(vksf->mpv, "gpu-context", "vksf");
    mpv_set_option_string(vksf->mpv, "force-window", "immediate");
    mpv_set_option_string(vksf->mpv, "input-default-bindings", "no");
    mpv_set_option_string(vksf->mpv, "input-vo-keyboard", "no");
    mpv_set_option_string(vksf->mpv, "input-cursor", "no");
    mpv_set_option_string(vksf->mpv, "osc", "no");

    if (options) {
        for (int n = 0; options[n][0]; n++) {
            const char *key = options[n][0];
            const char *val = options[n][1];
            if (strcmp(key, "vo") == 0 || strcmp(key, "gpu-context") == 0)
                continue;
            mpv_set_option_string(vksf->mpv, key, val);
        }
    }

    struct mp_client_api *ca = mp_client_get_client_api(vksf->mpv);
    mp_client_api_set_vksf_ctx(ca, ctx);

    if (log_cb)
        mpv_request_log_messages(vksf->mpv, "v");

    if (mpv_initialize(vksf->mpv) < 0) {
        mpv_destroy(vksf->mpv);
        mp_mutex_destroy(&ctx->ready_lock);
        mp_cond_destroy(&ctx->ready_cond);
        mp_mutex_destroy(&ctx->render_lock);
        free(ctx);
        free(vksf);
        return NULL;
    }

    // Wait until VO thread creates native surface and signals readiness
    mp_mutex_lock(&ctx->ready_lock);
    while (!ctx->surface_ready)
        mp_cond_wait(&ctx->ready_cond, &ctx->ready_lock);
    mp_mutex_unlock(&ctx->ready_lock);

    if (vksf->event_cb || vksf->log_cb) {
        atomic_init(&vksf->stop_thread, false);
        if (pthread_create(&vksf->event_thread, NULL, event_thread_proc, vksf) == 0) {
            vksf->has_thread = true;
        }
    }

    return vksf;
}

void mpv_vksf_destroy(struct mpv_vksf *vksf)
{
    if (!vksf)
        return;

    if (vksf->has_thread) {
        atomic_store(&vksf->stop_thread, true);
        mpv_wakeup(vksf->mpv);
        pthread_join(vksf->event_thread, NULL);
    }

    const char *cmd[] = {"quit", NULL};
    mpv_command(vksf->mpv, cmd);

    mpv_terminate_destroy(vksf->mpv);

    struct vksf_context *ctx = vksf->vksf_ctx;
    if (ctx) {
        mp_mutex_destroy(&ctx->ready_lock);
        mp_cond_destroy(&ctx->ready_cond);
        mp_mutex_destroy(&ctx->render_lock);
        free(ctx);
    }

    free(vksf);
}

void *mpv_vksf_get_native_surface(struct mpv_vksf *vksf)
{
    if (!vksf || !vksf->vksf_ctx)
        return NULL;

    return vksf->vksf_ctx->native_surface;
}

int mpv_vksf_update_size(struct mpv_vksf *vksf, int width, int height)
{
    if (!vksf || !vksf->vksf_ctx || width <= 0 || height <= 0)
        return MPV_ERROR_INVALID_PARAMETER;

    struct vksf_context *ctx = vksf->vksf_ctx;
    mp_mutex_lock(&ctx->ready_lock);
    ctx->width = width;
    ctx->height = height;
    ctx->resize_pending = true;
    mp_mutex_unlock(&ctx->ready_lock);

    if (vksf->mpv)
        mpv_wakeup(vksf->mpv);

    return 0;
}

void mpv_vksf_lock_surface(struct mpv_vksf *vksf)
{
    if (!vksf || !vksf->vksf_ctx)
        return;

    mp_mutex_lock(&vksf->vksf_ctx->render_lock);
}

void mpv_vksf_unlock_surface(struct mpv_vksf *vksf)
{
    if (!vksf || !vksf->vksf_ctx)
        return;

    mp_mutex_unlock(&vksf->vksf_ctx->render_lock);
}

int mpv_vksf_get_property(struct mpv_vksf *vksf, const char *name,
                          mpv_format format, void *data)
{
    if (!vksf || !vksf->mpv)
        return MPV_ERROR_INVALID_PARAMETER;

    return mpv_get_property(vksf->mpv, name, format, data);
}

int mpv_vksf_set_property(struct mpv_vksf *vksf, const char *name,
                          mpv_format format, const void *data)
{
    if (!vksf || !vksf->mpv)
        return MPV_ERROR_INVALID_PARAMETER;

    return mpv_set_property(vksf->mpv, name, format, (void *)data);
}

int mpv_vksf_observe_property(struct mpv_vksf *vksf, uint64_t reply_userdata,
                              const char *name, mpv_format format)
{
    if (!vksf || !vksf->mpv)
        return MPV_ERROR_INVALID_PARAMETER;

    return mpv_observe_property(vksf->mpv, reply_userdata, name, format);
}

int mpv_vksf_unobserve_property(struct mpv_vksf *vksf,
                                uint64_t registered_reply_userdata)
{
    if (!vksf || !vksf->mpv)
        return MPV_ERROR_INVALID_PARAMETER;

    return mpv_unobserve_property(vksf->mpv, registered_reply_userdata);
}

int mpv_vksf_command(struct mpv_vksf *vksf, const char **args)
{
    if (!vksf || !vksf->mpv)
        return MPV_ERROR_INVALID_PARAMETER;

    return mpv_command(vksf->mpv, args);
}
