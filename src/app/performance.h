#ifndef PLAGUE_APP_PERFORMANCE_H
#define PLAGUE_APP_PERFORMANCE_H
#include <stdint.h>
typedef struct {
    uint64_t frame_index;
    uint64_t frame_started_us;
    uint64_t services_done_us;
    uint64_t input_done_us;
    uint64_t render_done_us;
    uint64_t swap_done_us;
    uint64_t frame_finished_us;
} PortFrameTimings;
void port_perf_frame(const PortFrameTimings *timings);
void port_perf_draw(unsigned vertices);
void port_perf_upload(uint64_t duration_us);
void port_perf_finish(uint64_t duration_us);
void port_perf_present(void *framebuffer);
#endif
