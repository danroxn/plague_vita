#include "app/application.h"

#include "app/log.h"
#include "vita/audio.h"
#include "vita/trophies.h"

#include <psp2/kernel/processmgr.h>

int main(void) {
    port_app_start();
    port_app_run();
    port_vita_trophies_shutdown();
    port_vita_audio_shutdown();
    port_log_flush();
    sceKernelExitProcess(0);
    return 0;
}
