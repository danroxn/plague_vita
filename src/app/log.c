#include "app/log.h"

#include "runtime/paths.h"

#include <errno.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PortLogMode log_mode = PORT_LOG_OFF;
static FILE *log_file;
static char log_buffer[16384];
static uint64_t last_flush;
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

int port_log_enabled(PortLogMode mode) {
    return mode > PORT_LOG_OFF && mode <= log_mode;
}
void port_log_init(PortLogMode mode) {
    log_mode = mode;
    if (!port_log_enabled(PORT_LOG_ERROR))
        return;
    int saved_errno = errno;
    sceIoMkdir("ux0:data/plague", 0777);
    if (remove(PORT_ROOT "port.log") == 0 || errno == ENOENT)
        log_file = fopen(PORT_ROOT "port.log", "a");
    if (log_file)
        setvbuf(log_file, log_buffer, _IOFBF, sizeof(log_buffer));
    errno = saved_errno;
}
void port_log_writev(PortLogMode mode, const char *format, va_list args) {
    if (!port_log_enabled(mode))
        return;
    int saved_errno = errno;
    const char *prefix = mode == PORT_LOG_ERROR ? "ERROR" : "DEBUG";
    char line[2048];
    vsnprintf(line, sizeof(line), format, args);
    size_t length = strlen(line);
    while (length && (line[length - 1] == '\n' || line[length - 1] == '\r'))
        line[--length] = 0;
    pthread_mutex_lock(&log_lock);
    if (log_file) {
        fprintf(log_file, "[%s] %s\n", prefix, line);
        if (mode == PORT_LOG_ERROR) {
            fflush(log_file);
        } else {
            uint64_t now = sceKernelGetProcessTimeWide();
            if (now - last_flush >= 1000000) {
                fflush(log_file);
                last_flush = now;
            }
        }
    } else {
        sceClibPrintf("[%s] %s\n", prefix, line);
    }
    pthread_mutex_unlock(&log_lock);
    errno = saved_errno;
}
void port_log_write(PortLogMode mode, const char *format, ...) {
    va_list args;
    va_start(args, format);
    port_log_writev(mode, format, args);
    va_end(args);
}
void port_log_flush(void) {
    if (!port_log_enabled(PORT_LOG_ERROR))
        return;
    int saved_errno = errno;
    pthread_mutex_lock(&log_lock);
    if (log_file)
        fflush(log_file);
    pthread_mutex_unlock(&log_lock);
    errno = saved_errno;
}
_Noreturn void port_fail(const char *format, ...) {
    if (port_log_enabled(PORT_LOG_ERROR)) {
        port_log_error("FATAL -- native execution stopped");
        va_list args;
        va_start(args, format);
        port_log_writev(PORT_LOG_ERROR, format, args);
        va_end(args);
        port_log_flush();
    }
    sceKernelDelayThread(1000000);
    sceKernelExitProcess(1);
    abort();
}
