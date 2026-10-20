#ifndef PLAGUE_APP_LOG_H
#define PLAGUE_APP_LOG_H
#include <stdarg.h>

typedef enum { PORT_LOG_OFF = 0, PORT_LOG_ERROR = 1, PORT_LOG_DEBUG = 2 } PortLogMode;
void port_log_init(PortLogMode mode);
int port_log_enabled(PortLogMode mode);
void port_log_write(PortLogMode mode, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
void port_log_writev(PortLogMode mode, const char *format, va_list args)
    __attribute__((format(printf, 2, 0)));
#define port_log_error(...)                                                                        \
    do {                                                                                           \
        if (port_log_enabled(PORT_LOG_ERROR))                                                      \
            port_log_write(PORT_LOG_ERROR, __VA_ARGS__);                                           \
    } while (0)
#define port_log_debug(...)                                                                        \
    do {                                                                                           \
        if (port_log_enabled(PORT_LOG_DEBUG))                                                      \
            port_log_write(PORT_LOG_DEBUG, __VA_ARGS__);                                           \
    } while (0)
void port_log_flush(void);
_Noreturn void port_fail(const char *format, ...) __attribute__((format(printf, 1, 2)));
#endif
