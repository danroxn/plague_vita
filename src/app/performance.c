#include "app/performance.h"

#include "app/log.h"
#include "runtime/text_cache.h"
#include "runtime/defaults_batch.h"
#include "runtime/encoded_texture_cache.h"
#include "runtime/texture_cache.h"
#include "vita/time.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { PROFILE_WINDOW_FRAMES = 120, PRESENT_RING_CAPACITY = 128 };

typedef struct {
    uint64_t frame_durations_us[PROFILE_WINDOW_FRAMES];
    uint64_t started_us;
    uint64_t total_us;
    uint64_t services_us;
    uint64_t input_us;
    uint64_t render_us;
    uint64_t swap_us;
    uint64_t draw_call_count;
    uint64_t vertex_count;
    uint64_t upload_count;
    uint64_t upload_us;
    uint64_t finish_us;
    unsigned frame_count;
} FrameProfileWindow;
static FrameProfileWindow profile_window;
static uint32_t present_intervals[PRESENT_RING_CAPACITY], present_sequence;
static uint32_t consumed_present_sequence;
void port_perf_present(void *framebuffer) {
    (void)framebuffer;
    static uint64_t last_present_us;
    if (!port_log_enabled(PORT_LOG_DEBUG))
        return;
    uint64_t now_us = port_vita_time_us();
    if (last_present_us) {
        uint64_t interval_us = now_us - last_present_us;
        uint32_t sequence = __atomic_load_n(&present_sequence, __ATOMIC_RELAXED);
        __atomic_store_n(&present_intervals[sequence % PRESENT_RING_CAPACITY],
                         interval_us > UINT32_MAX ? UINT32_MAX : (uint32_t)interval_us,
                         __ATOMIC_RELAXED);
        __atomic_store_n(&present_sequence, sequence + 1, __ATOMIC_RELEASE);
    }
    last_present_us = now_us;
}
static int compare_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}
void port_perf_draw(unsigned vertices) {
    if (!port_log_enabled(PORT_LOG_DEBUG))
        return;
    profile_window.draw_call_count++;
    profile_window.vertex_count += vertices;
}
void port_perf_upload(uint64_t duration_us) {
    if (!port_log_enabled(PORT_LOG_DEBUG))
        return;
    profile_window.upload_count++;
    profile_window.upload_us += duration_us;
}
void port_perf_finish(uint64_t duration_us) {
    if (!port_log_enabled(PORT_LOG_DEBUG))
        return;
    profile_window.finish_us += duration_us;
}
void port_perf_frame(const PortFrameTimings *timings) {
    if (!port_log_enabled(PORT_LOG_DEBUG))
        return;
    if (!profile_window.frame_count)
        profile_window.started_us = timings->frame_started_us;
    uint64_t frame_duration_us = timings->frame_finished_us - timings->frame_started_us;
    profile_window.frame_durations_us[profile_window.frame_count++] = frame_duration_us;
    profile_window.total_us += frame_duration_us;
    profile_window.services_us += timings->services_done_us - timings->frame_started_us;
    profile_window.input_us += timings->input_done_us - timings->services_done_us;
    profile_window.render_us += timings->render_done_us - timings->input_done_us;
    profile_window.swap_us += timings->swap_done_us - timings->render_done_us;
    if (frame_duration_us >= 1000000) {
        port_defaults_batch_report();
        port_text_cache_report();
        port_texture_cache_report();
        port_encoded_texture_cache_report();
        port_log_debug("PERF stall frame=%llu total_us=%llu start_us=%llu end_us=%llu",
                       (unsigned long long)timings->frame_index,
                       (unsigned long long)frame_duration_us,
                       (unsigned long long)timings->frame_started_us,
                       (unsigned long long)timings->frame_finished_us);
    }
    if (profile_window.frame_count < PROFILE_WINDOW_FRAMES)
        return;
    uint64_t window_span_us = timings->frame_finished_us - profile_window.started_us;
    qsort(profile_window.frame_durations_us, PROFILE_WINDOW_FRAMES, sizeof(uint64_t), compare_u64);
    uint32_t sequence = __atomic_load_n(&present_sequence, __ATOMIC_ACQUIRE);
    uint32_t available = sequence - consumed_present_sequence;
    unsigned samples = available > PRESENT_RING_CAPACITY ? PRESENT_RING_CAPACITY : available;
    uint64_t present_total_us = 0, present_max_us = 0;
    for (unsigned i = 0; i < samples; i++) {
        uint64_t interval = __atomic_load_n(
            &present_intervals[(sequence - samples + i) % PRESENT_RING_CAPACITY], __ATOMIC_RELAXED);
        present_total_us += interval;
        if (interval > present_max_us)
            present_max_us = interval;
    }
    consumed_present_sequence = sequence;
    port_log_debug(
        "PERF window frame=%llu frames=120 fps=%.2f avg_us=%llu p95_us=%llu max_us=%llu "
        "services_us=%llu input_us=%llu render_us=%llu swap_us=%llu "
        "draws=%llu vertices=%llu uploads=%llu upload_us=%llu finish_us=%llu "
        "start_us=%llu end_us=%llu present_fps=%.2f present_max_us=%llu present_dropped=%u",
        (unsigned long long)timings->frame_index,
        window_span_us ? (PROFILE_WINDOW_FRAMES * 1000000.0) / window_span_us : 0.0,
        (unsigned long long)(profile_window.total_us / PROFILE_WINDOW_FRAMES),
        (unsigned long long)
            profile_window.frame_durations_us[(PROFILE_WINDOW_FRAMES * 95 + 99) / 100 - 1],
        (unsigned long long)profile_window.frame_durations_us[PROFILE_WINDOW_FRAMES - 1],
        (unsigned long long)(profile_window.services_us / PROFILE_WINDOW_FRAMES),
        (unsigned long long)(profile_window.input_us / PROFILE_WINDOW_FRAMES),
        (unsigned long long)(profile_window.render_us / PROFILE_WINDOW_FRAMES),
        (unsigned long long)(profile_window.swap_us / PROFILE_WINDOW_FRAMES),
        (unsigned long long)profile_window.draw_call_count,
        (unsigned long long)profile_window.vertex_count,
        (unsigned long long)profile_window.upload_count,
        (unsigned long long)profile_window.upload_us, (unsigned long long)profile_window.finish_us,
        (unsigned long long)profile_window.started_us,
        (unsigned long long)timings->frame_finished_us,
        present_total_us ? (double)samples * 1000000 / present_total_us : 0.0,
        (unsigned long long)present_max_us, available - samples);
    memset(&profile_window, 0, sizeof(profile_window));
}
