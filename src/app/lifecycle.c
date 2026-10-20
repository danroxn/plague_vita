#include "app/lifecycle.h"

#include "runtime/callbacks.h"
#include "services/async.h"
#include "vita/lifecycle.h"

#include <math.h>
#include <stddef.h>

static struct {
    int exit_requested;
    int events_registered;
    int system_overlay_active;
    uint64_t frame_interval_us;
} lifecycle = {.frame_interval_us = 16667};

int port_app_exit_requested(void) {
    return lifecycle.exit_requested;
}
void port_app_request_exit(void) {
    lifecycle.exit_requested = 1;
}
uint64_t port_app_frame_interval_us(void) {
    return lifecycle.frame_interval_us;
}
void port_app_set_animation_interval_seconds(double interval_seconds) {
    if (isfinite(interval_seconds) && interval_seconds > 0)
        lifecycle.frame_interval_us =
            (uint64_t)(fmax(1.0 / 60.0, fmin(interval_seconds, 1.0)) * 1000000);
}
void port_app_register_lifecycle_events(void) {
    if (lifecycle.events_registered)
        return;
    lifecycle.events_registered = 1;
    port_async_schedule(&port_game_callback_lifecycle_start, NULL, NULL);
    port_async_schedule(&port_game_callback_lifecycle_resume, NULL, NULL);
}
void port_app_poll_lifecycle(int dialog_active) {
    if (!lifecycle.events_registered)
        return;
    PortVitaLifecycleState state;
    if (!port_vita_lifecycle_read_state(&state))
        return;
    int overlay_active = state.system_ui_overlaid && !dialog_active;
    if (overlay_active != lifecycle.system_overlay_active) {
        lifecycle.system_overlay_active = overlay_active;
        if (overlay_active) {
            port_async_schedule(&port_game_callback_lifecycle_pause, NULL, NULL);
            port_async_schedule(&port_game_callback_lifecycle_background, NULL, NULL);
        } else {
            port_async_schedule(&port_game_callback_lifecycle_foreground, NULL, NULL);
            port_async_schedule(&port_game_callback_lifecycle_resume, NULL, NULL);
        }
    }
    for (unsigned index = 0; index < state.event_count; ++index) {
        PortVitaLifecycleEvent event = port_vita_lifecycle_next_event();
        if (event == PORT_VITA_EVENT_RECEIVE_FAILED)
            break;
        if (event == PORT_VITA_EVENT_RESUME && !lifecycle.system_overlay_active) {
            port_async_schedule(&port_game_callback_lifecycle_foreground, NULL, NULL);
            port_async_schedule(&port_game_callback_lifecycle_resume, NULL, NULL);
        }
    }
}
