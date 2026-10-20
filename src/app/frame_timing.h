#ifndef PLAGUE_APP_FRAME_TIMING_H
#define PLAGUE_APP_FRAME_TIMING_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t interval_us;
    uint64_t deadline_us;
    unsigned vsync_intervals;
} PortFramePacer;
unsigned port_pacer_update(PortFramePacer *pacer, uint64_t interval_us, uint64_t now_us);
uint64_t port_pacer_compute_wait_us(PortFramePacer *pacer, uint64_t now_us);
#endif
