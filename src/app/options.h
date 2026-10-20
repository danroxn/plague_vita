#ifndef PLAGUE_APP_OPTIONS_H
#define PLAGUE_APP_OPTIONS_H
#include "app/log.h"
typedef struct {
    int retina_enabled;
    int msaa_samples;
    int unlock_all;
    PortLogMode log_level;
} PortAppOptions;
typedef struct {
    unsigned invalid_count;
    unsigned first_invalid_line;
    int read_error;
    int write_error;
} PortOptionsLoadResult;
extern PortAppOptions port_app_options;
PortOptionsLoadResult port_app_options_load(const char *path);
#endif
