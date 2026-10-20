#include "app/application.h"
#include "app/frame_timing.h"
#include "app/log.h"
#include "app/performance.h"
#include "compat/jni/runtime.h"
#include "runtime/game_api.h"
#include "runtime/text_cache.h"
#include "runtime/defaults_batch.h"
#include "services/dispatch.h"
#include "app/lifecycle.h"
#include "vita/input.h"
#include "vita/time.h"

#include <psp2/kernel/processmgr.h>
#include <stddef.h>
#include <stdint.h>
#include <vitaGL.h>

void port_app_run(void) {
    const GameApi *game = port_game_api();
    const int profiling = port_log_enabled(PORT_LOG_DEBUG);
    int previous_dialog = 0;
    PortFramePacer pacer = {0};
    unsigned previous_vsync = 0;
    for (uint64_t frame = 0;;) {
        PortFrameTimings timings = {.frame_index = frame, .frame_started_us = port_vita_time_us()};
        uint64_t previous_interval = pacer.interval_us;
        unsigned vsync =
            port_pacer_update(&pacer, port_app_frame_interval_us(), timings.frame_started_us);
        if (vsync != previous_vsync || pacer.interval_us != previous_interval) {
            eglSwapInterval(EGL_DEFAULT_DISPLAY, vsync);
            previous_vsync = vsync;
            port_log_debug("RENDER pacing engine_us=%llu vsync=%u deadline_limiter=%d",
                           (unsigned long long)pacer.interval_us, vsync, !pacer.vsync_intervals);
        }
        int dialog = port_services_poll();
        port_app_poll_lifecycle(dialog);
        if (port_app_exit_requested()) {
            if (!frame)
                port_defaults_batch_end();
            port_text_cache_finish();
            break;
        }
        if (profiling)
            timings.services_done_us = port_vita_time_us();
        if (dialog && !previous_dialog)
            port_vita_input_cancel();
        if (!dialog)
            port_vita_input_poll();
        previous_dialog = dialog;
        if (profiling)
            timings.input_done_us = port_vita_time_us();
        port_defaults_batch_begin();
        game->renderer_render(&port_jni, NULL);
        port_defaults_batch_end();
        if (!frame)
            port_defaults_batch_end(); // Complete the outer startup batch.
        if (profiling)
            timings.render_done_us = port_vita_time_us();
        if (frame == 0)
            port_app_loading_handover(dialog);
        else
            vglSwapBuffers(dialog ? GL_TRUE : GL_FALSE);
        timings.swap_done_us = port_vita_time_us();
        port_text_cache_poll(timings.swap_done_us,
                             timings.swap_done_us - timings.frame_started_us);
        uint64_t wait = port_pacer_compute_wait_us(&pacer, timings.swap_done_us);
        if (wait)
            sceKernelDelayThread(wait);
        if (profiling) {
            timings.frame_finished_us = port_vita_time_us();
            port_perf_frame(&timings);
            if (frame == 0) {
                const float *content = game->content_scale, *point = game->point_scale;
                port_log_debug("RENDER scale content=%.3f point=%.3f", content ? *content : 0,
                               point ? *point : 0);
                port_log_debug("FRAME 0");
            }
        }
        frame++;
    }
}
