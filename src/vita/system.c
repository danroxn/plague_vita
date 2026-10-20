#include "vita/system.h"

#include "vita/device.h"
#include "app/log.h"
#include "runtime/paths.h"

#include <kubridge.h>
#include <malloc.h>
#include <psp2/appmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/power.h>
#include <psp2/types.h>
#include <stddef.h>
#include <stdio.h>
#include <vitaGL.h>

extern size_t _get_vita_heap_size(void);

unsigned int _newlib_heap_size_user = 96 * 1024 * 1024;
static void abort_handler(KuKernelAbortContext *context) {
    if (port_log_enabled(PORT_LOG_ERROR)) {
        char line[256];
        int length =
            snprintf(line, sizeof(line),
                     "[ERROR] ABORT type=%u pc=%#x lr=%#x far=%#x fsr=%#x r0=%#x r1=%#x r2=%#x "
                     "r3=%#x sp=%#x\n",
                     context->abortType, context->pc, context->lr, context->FAR, context->FSR,
                     context->r0, context->r1, context->r2, context->r3, context->sp);
        SceUID descriptor =
            sceIoOpen(PORT_ROOT "port.log", SCE_O_WRONLY | SCE_O_APPEND | SCE_O_CREAT, 0666);
        if (descriptor >= 0) {
            if (length > 0)
                sceIoWrite(descriptor, line,
                           (size_t)length < sizeof(line) ? (size_t)length : sizeof(line) - 1);
            sceIoClose(descriptor);
        } else {
            sceClibPrintf("%s", line);
        }
    }
    sceKernelDelayThread(500000);
    sceKernelExitProcess(1);
}
void port_vita_system_report_memory(const char *stage) {
    if (!port_log_enabled(PORT_LOG_DEBUG))
        return;
    SceKernelFreeMemorySizeInfo memory_info = {.size = sizeof(memory_info)};
    int result = sceKernelGetFreeMemorySize(&memory_info);
    struct mallinfo heap = mallinfo();
    port_log_debug("MEM[%s] heap_used=%u heap_total=%u kernel_rc=%#x user_free=%d cdram_free=%d "
                   "phycont_free=%d vgl_free=%u/%u/%u",
                   stage, (unsigned)heap.uordblks, (unsigned)_get_vita_heap_size(),
                   (unsigned)result, result < 0 ? 0 : memory_info.size_user,
                   result < 0 ? 0 : memory_info.size_cdram,
                   result < 0 ? 0 : memory_info.size_phycont,
                   (unsigned)vglMemFree(VGL_MEM_RAM), (unsigned)vglMemFree(VGL_MEM_VRAM),
                   (unsigned)vglMemFree(VGL_MEM_PHYCONT));
}
void port_vita_system_init(void) {
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    KuKernelAbortHandlerOpt options = {.size = sizeof(options)};
    kuKernelRegisterAbortHandler(abort_handler, NULL, &options);
}
void port_vita_display_init(int msaa_samples) {
    // VitaGL allocates its pools before the Android module and platform services
    // load. Leave enough system RAM for them and repeated native dialogs.
    const int system_ram_reserve = 48 * 1024 * 1024;
    SceGxmMultisampleMode msaa = msaa_samples == 4   ? SCE_GXM_MULTISAMPLE_4X
                                 : msaa_samples == 2 ? SCE_GXM_MULTISAMPLE_2X
                                                     : SCE_GXM_MULTISAMPLE_NONE;
    if (vglInitExtended(8 * 1024 * 1024, PORT_DISPLAY_WIDTH, PORT_DISPLAY_HEIGHT, system_ram_reserve,
                        msaa))
        port_log_debug("NOTE VitaGL selected a smaller framebuffer");
}

uint64_t port_vita_system_free_storage(void) {
    uint64_t total, available;
    if (sceAppMgrGetDevInfo("ux0:", &total, &available) < 0)
        port_fail("Storage information unavailable");
    return available;
}
size_t port_vita_system_heap_used(void) {
    return mallinfo().uordblks;
}
