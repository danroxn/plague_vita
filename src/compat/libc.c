#include "compat/import_table.h"

#include "compat/android_contract.h"
#include "vita/device.h"
#include "app/log.h"
#include "runtime/native_memory.h"
#include "runtime/paths.h"

#include <errno.h>
#include <psp2/kernel/clib.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

extern int __cxa_atexit(void (*destroy)(void *), void *object, void *dso);

extern const char *BIONIC_ctype_;
extern const short *BIONIC_tolower_tab_, *BIONIC_toupper_tab_;

static uintptr_t stack_guard = 0xa53f91c7;
static char environment_language[] = "LANG=en_US.UTF-8";
static char environment_home[] = "HOME=" PORT_FILES;
static char *environment[] = {environment_language, environment_home, NULL};
static char **environment_pointer = environment;
static __thread int android_errno;

static int *compat_errno(void) {
    switch (errno) {
    case ENOSYS:
        android_errno = 38;
        break;
    case ENETUNREACH:
        android_errno = 101;
        break;
    case ENOTSUP:
        android_errno = 95;
        break;
    case EOVERFLOW:
        android_errno = 75;
        break;
    default:
        android_errno = errno;
        break;
    }
    return &android_errno;
}

static char *compat_getenv(const char *name) {
    size_t name_length = strlen(name);
    for (char **entry = environment; *entry; entry++)
        if (!strncmp(*entry, name, name_length) && (*entry)[name_length] == '=')
            return *entry + name_length + 1;
    return NULL;
}
static void compat_stack_fail(void) {
    port_fail("Native stack protector failed at %p", __builtin_return_address(0));
}
static void compat_abort(void) {
    port_fail("Native abort at %p", __builtin_return_address(0));
}
enum { ANDROID_LOG_DEBUG = 3, ANDROID_LOG_WARN = 5, ANDROID_LOG_ERROR = 6 };
static PortLogMode android_log_mode(int priority) {
    if (priority >= ANDROID_LOG_ERROR)
        return PORT_LOG_ERROR;
    if (priority == ANDROID_LOG_DEBUG || priority == ANDROID_LOG_WARN)
        return PORT_LOG_DEBUG;
    return PORT_LOG_OFF;
}
static int compat_logv(int priority, const char *tag, const char *format, va_list args) {
    PortLogMode mode = android_log_mode(priority);
    if (!port_log_enabled(mode))
        return 0;
    char message[1500];
    int characters_written = vsnprintf(message, sizeof(message), format, args);
    port_log_write(mode, "ANDROID [%s] %s", tag ? tag : "", message);
    return characters_written;
}
static int compat_log(int priority, const char *tag, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int characters_written = compat_logv(priority, tag, format, arguments);
    va_end(arguments);
    return characters_written;
}
static int compat_log_write(int priority, const char *tag, const char *message) {
    PortLogMode mode = android_log_mode(priority);
    if (!port_log_enabled(mode))
        return 0;
    port_log_write(mode, "ANDROID [%s] %s", tag ? tag : "", message ? message : "");
    return message ? (int)strlen(message) : 0;
}
static const struct {
    const char *name;
    const char *value;
} android_properties[] = {
    {"ro.product.model", PORT_DEVICE_MODEL},
    {"ro.product.manufacturer", PORT_DEVICE_BRAND},
    {"ro.product.cpu.abi", PORT_ANDROID_CPU_ABI},
    {"ro.build.version.sdk", PORT_ANDROID_API_LEVEL_TEXT},
    {"ro.build.version.release", PORT_ANDROID_RELEASE},
};
static int compat_property(const char *name, char *value_out) {
    const char *value = "";
    for (size_t index = 0; index < sizeof(android_properties) / sizeof(android_properties[0]);
         ++index)
        if (!strcmp(name, android_properties[index].name)) {
            value = android_properties[index].value;
            break;
        }
    strcpy(value_out, value);
    return (int)strlen(value);
}
enum {
    ANDROID_CLOCK_REALTIME = 0,
    ANDROID_CLOCK_MONOTONIC = 1,
    ANDROID_CLOCK_MONOTONIC_RAW = 4,
    ANDROID_CLOCK_REALTIME_COARSE = 5,
    ANDROID_CLOCK_MONOTONIC_COARSE = 6,
    ANDROID_CLOCK_BOOTTIME = 7,
};
static int compat_clock_gettime(int clock_id, struct timespec *time_out) {
    if (!time_out) {
        errno = EFAULT;
        return -1;
    }
    switch (clock_id) {
    case ANDROID_CLOCK_REALTIME:
    case ANDROID_CLOCK_REALTIME_COARSE:
        return clock_gettime(CLOCK_REALTIME, time_out);
    case ANDROID_CLOCK_MONOTONIC:
    case ANDROID_CLOCK_MONOTONIC_RAW:
    case ANDROID_CLOCK_MONOTONIC_COARSE:
    case ANDROID_CLOCK_BOOTTIME:
        return clock_gettime(CLOCK_MONOTONIC, time_out);
    default:
        errno = EINVAL;
        return -1;
    }
}
static int compat_syscall(long number, ...) {
    (void)number;
    errno = ENOSYS;
    return -1;
}
static int compat_process_unavailable(void) {
    errno = ENOSYS;
    return -1;
}
static int compat_system(const char *command) {
    if (!command)
        return 0;
    return compat_process_unavailable();
}
static FILE *compat_popen(const char *command, const char *mode) {
    (void)command;
    (void)mode;
    errno = ENOSYS;
    return NULL;
}
static int compat_pclose(FILE *file) {
    (void)file;
    errno = ENOSYS;
    return -1;
}
static int compat_execl(const char *path, const char *first_argument, ...) {
    (void)path;
    (void)first_argument;
    return compat_process_unavailable();
}
static int compat_execve(const char *path, char *const arguments[], char *const environment[]) {
    (void)path;
    (void)arguments;
    (void)environment;
    return compat_process_unavailable();
}
static pid_t compat_waitpid(pid_t process, int *status, int options) {
    (void)process;
    (void)status;
    (void)options;
    return compat_process_unavailable();
}
static ssize_t compat_readlink(const char *path, char *buffer, size_t capacity) {
    (void)path;
    (void)buffer;
    (void)capacity;
    errno = ENOSYS;
    return -1;
}
static int compat_symlink(const char *target, const char *path) {
    (void)target;
    (void)path;
    errno = ENOSYS;
    return -1;
}
struct AndroidSigaction;
static int compat_sigaction(int signal_number, const struct AndroidSigaction *action,
                            struct AndroidSigaction *previous) {
    (void)signal_number;
    (void)action;
    (void)previous;
    errno = ENOSYS;
    return -1;
}
typedef void (*AndroidSignalHandler)(int);
static AndroidSignalHandler compat_bsd_signal(int signal_number, AndroidSignalHandler handler) {
    (void)signal_number;
    (void)handler;
    errno = ENOSYS;
    return (AndroidSignalHandler)(intptr_t)-1;
}
static int compat_ioctl(int descriptor, unsigned long request, ...) {
    (void)descriptor;
    (void)request;
    errno = ENOSYS;
    return -1;
}
static void compat_sync(void) {
    fflush(NULL);
}
static int compat_dup2(int from, int to) {
    if (from == to)
        return from;
    errno = ENOSYS;
    return -1;
}
static unsigned compat_if_nametoindex(const char *name) {
    (void)name;
    errno = ENODEV;
    return 0;
}
static int compat_aeabi_atexit(void *object, void (*destroy)(void *), void *dso) {
    return __cxa_atexit(destroy, object, dso);
}
typedef struct {
    void *iov_base;
    size_t iov_len;
} AndroidIOVector;
static ssize_t compat_writev(int descriptor, const AndroidIOVector *vectors, int count) {
    if (count < 0) {
        errno = EINVAL;
        return -1;
    }
    ssize_t total = 0;
    for (int i = 0; i < count; i++) {
        ssize_t bytes_written = write(descriptor, vectors[i].iov_base, vectors[i].iov_len);
        if (bytes_written < 0)
            return total ? total : -1;
        total += bytes_written;
        if ((size_t)bytes_written < vectors[i].iov_len)
            break;
    }
    return total;
}
static void *compat_userinfo(void) {
    errno = ENOSYS;
    return NULL;
}
static void compat_memclr(void *destination, size_t length) {
    sceClibMemset(destination, 0, length);
}
static void compat_memset(void *destination, size_t length, int value) {
    sceClibMemset(destination, value, length);
}

static const PortImportEntry imports[] = {
    {"__errno", (uintptr_t)(compat_errno)},
    {"__stack_chk_fail", (uintptr_t)(compat_stack_fail)},
    {"__stack_chk_guard", (uintptr_t)(&stack_guard)},
    {"abort", (uintptr_t)(compat_abort)},
    {"__android_log_print", (uintptr_t)(compat_log)},
    {"__android_log_vprint", (uintptr_t)(compat_logv)},
    {"__android_log_write", (uintptr_t)(compat_log_write)},
    {"__system_property_get", (uintptr_t)(compat_property)},
    {"getenv", (uintptr_t)(compat_getenv)},
    {"memcpy", (uintptr_t)(sceClibMemcpy)},
    {"memmove", (uintptr_t)(sceClibMemmove)},
    {"memset", (uintptr_t)(sceClibMemset)},
    {"malloc", (uintptr_t)(port_native_malloc)},
    {"calloc", (uintptr_t)(port_native_calloc)},
    {"realloc", (uintptr_t)(port_native_realloc)},
    {"free", (uintptr_t)(port_native_free)},
    {"strdup", (uintptr_t)(port_native_strdup)},
    {"strndup", (uintptr_t)(port_native_strndup)},
    {"_ctype_", (uintptr_t)(&BIONIC_ctype_)},
    {"_tolower_tab_", (uintptr_t)(&BIONIC_tolower_tab_)},
    {"_toupper_tab_", (uintptr_t)(&BIONIC_toupper_tab_)},
    {"environ", (uintptr_t)(&environment_pointer)},
    {"__aeabi_memclr", (uintptr_t)(compat_memclr)},
    {"__aeabi_memclr4", (uintptr_t)(compat_memclr)},
    {"__aeabi_memclr8", (uintptr_t)(compat_memclr)},
    {"__aeabi_memcpy", (uintptr_t)(sceClibMemcpy)},
    {"__aeabi_memcpy4", (uintptr_t)(sceClibMemcpy)},
    {"__aeabi_memcpy8", (uintptr_t)(sceClibMemcpy)},
    {"__aeabi_memmove", (uintptr_t)(sceClibMemmove)},
    {"__aeabi_memmove4", (uintptr_t)(sceClibMemmove)},
    {"__aeabi_memset", (uintptr_t)(compat_memset)},
    {"clock_gettime", (uintptr_t)(compat_clock_gettime)},
    {"syscall", (uintptr_t)(compat_syscall)},
    {"fork", (uintptr_t)(compat_process_unavailable)},
    {"execl", (uintptr_t)(compat_execl)},
    {"execve", (uintptr_t)(compat_execve)},
    {"waitpid", (uintptr_t)(compat_waitpid)},
    {"system", (uintptr_t)(compat_system)},
    {"getpwuid", (uintptr_t)(compat_userinfo)},
    {"getgrgid", (uintptr_t)(compat_userinfo)},
    {"popen", (uintptr_t)(compat_popen)},
    {"pclose", (uintptr_t)(compat_pclose)},
    {"dup2", (uintptr_t)(compat_dup2)},
    {"readlink", (uintptr_t)(compat_readlink)},
    {"symlink", (uintptr_t)(compat_symlink)},
    {"sync", (uintptr_t)(compat_sync)},
    {"sigaction", (uintptr_t)(compat_sigaction)},
    {"bsd_signal", (uintptr_t)(compat_bsd_signal)},
    {"ioctl", (uintptr_t)(compat_ioctl)},
    {"if_nametoindex", (uintptr_t)(compat_if_nametoindex)},
    {"__aeabi_atexit", (uintptr_t)(compat_aeabi_atexit)},
    {"writev", (uintptr_t)(compat_writev)},
};
const PortImportTable port_libc_imports = {imports, sizeof(imports) / sizeof(imports[0])};
