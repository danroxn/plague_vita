#include "vita/time.h"

#include "app/log.h"

#include <psp2/kernel/processmgr.h>
#include <psp2/rtc.h>

uint64_t port_vita_time_us(void) {
    return sceKernelGetProcessTimeWide();
}

int port_vita_timezone_offset_seconds(void) {
    int64_t ticks_per_second = sceRtcGetTickResolution();
    if (!ticks_per_second) {
        port_log_error("RTC tick resolution unavailable");
        return 0;
    }
    SceRtcTick utc, local;
    int result = sceRtcGetCurrentTick(&utc);
    if (result < 0) {
        port_log_error("RTC time read failed: 0x%08x", (unsigned)result);
        return 0;
    }
    result = sceRtcConvertUtcToLocalTime(&utc, &local);
    if (result < 0) {
        port_log_error("RTC local time conversion failed: 0x%08x", (unsigned)result);
        return 0;
    }
    return (int)(((int64_t)local.tick - (int64_t)utc.tick) / ticks_per_second);
}
