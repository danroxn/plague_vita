#include "app/options.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PortAppOptions port_app_options;
PortOptionsLoadResult port_app_options_load(const char *path) {
    PortOptionsLoadResult result = {0};
    port_app_options = (PortAppOptions){
        .retina_enabled = 1, .msaa_samples = 4, .log_level = PORT_LOG_OFF};
    FILE *file = fopen(path, "r");
    if (!file && errno == ENOENT) {
        // Exclusive creation preserves a config that appeared after the read attempt.
        file = fopen(path, "wx");
        if (file) {
            result.write_error =
                fprintf(file, "retina=%d\nmsaa=%d\nlog_level=%d\nunlock_all=%d\n",
                        port_app_options.retina_enabled, port_app_options.msaa_samples,
                        (int)port_app_options.log_level, port_app_options.unlock_all) < 0;
            if (fclose(file))
                result.write_error = 1;
            if (result.write_error)
                remove(path);
            return result;
        }
        if (errno != EEXIST) {
            result.write_error = 1;
            return result;
        }
        file = fopen(path, "r");
    }
    if (!file) {
        result.read_error = 1;
        return result;
    }
    char line[256];
    unsigned line_number = 0;
    while (fgets(line, sizeof(line), file)) {
        line_number++;
        if (!strchr(line, '\n') && !feof(file)) {
            int next = fgetc(file);
            if (next != EOF) {
                while (next != '\n' && next != EOF)
                    next = fgetc(file);
                goto invalid;
            }
        }
        char *cursor = line;
        while (isspace((unsigned char)*cursor))
            cursor++;
        if (!*cursor || *cursor == '#')
            continue;
        char *key = cursor;
        while ((*cursor >= 'a' && *cursor <= 'z') || *cursor == '_')
            cursor++;
        if (cursor == key || cursor - key > 63 || *cursor != '=')
            goto invalid;
        *cursor++ = 0;
        char *end;
        errno = 0;
        long value = strtol(cursor, &end, 10);
        if (end == cursor || errno == ERANGE)
            goto invalid;
        while (isspace((unsigned char)*end))
            end++;
        if (*end)
            goto invalid;
        if (!strcmp(key, "retina") && (value == 0 || value == 1))
            port_app_options.retina_enabled = (int)value;
        else if (!strcmp(key, "msaa") && (value == 0 || value == 2 || value == 4))
            port_app_options.msaa_samples = (int)value;
        else if (!strcmp(key, "unlock_all") && (value == 0 || value == 1))
            port_app_options.unlock_all = (int)value;
        else if (!strcmp(key, "log_level") && value >= PORT_LOG_OFF && value <= PORT_LOG_DEBUG)
            port_app_options.log_level = (PortLogMode)value;
        else
            goto invalid;
        continue;
    invalid:
        if (!result.invalid_count)
            result.first_invalid_line = line_number;
        result.invalid_count++;
    }
    result.read_error = ferror(file) != 0;
    fclose(file);
    return result;
}
