#include "app/frame_timing.h"

#include <stddef.h>
#include <stdint.h>

unsigned port_pacer_update(PortFramePacer *pacer, uint64_t interval_us, uint64_t now_us) {
    if (interval_us < 16666)
        interval_us = 16666;
    if (interval_us > 1000000)
        interval_us = 1000000;
    unsigned display_intervals = (unsigned)((interval_us * 60 + 500000) / 1000000);
    uint64_t exact_interval_us = (uint64_t)display_intervals * 1000000 / 60;
    uint64_t difference_us = interval_us > exact_interval_us ? interval_us - exact_interval_us
                                                             : exact_interval_us - interval_us;
    unsigned vsync_intervals = difference_us <= 2 ? display_intervals : 0;
    if (pacer->interval_us != interval_us) {
        pacer->interval_us = interval_us;
        pacer->deadline_us = now_us + interval_us;
    }
    pacer->vsync_intervals = vsync_intervals;
    return vsync_intervals ? vsync_intervals : 1;
}
uint64_t port_pacer_compute_wait_us(PortFramePacer *pacer, uint64_t now_us) {
    if (pacer->vsync_intervals)
        return 0;
    if (now_us >= pacer->deadline_us) {
        pacer->deadline_us +=
            ((now_us - pacer->deadline_us) / pacer->interval_us + 1) * pacer->interval_us;
        return 0;
    }
    uint64_t wait_us = pacer->deadline_us - now_us;
    pacer->deadline_us += pacer->interval_us;
    return wait_us;
}
