#include "runtime/patches.h"

#include "app/log.h"
#include "app/options.h"
#include "compat/abi.h"
#include "runtime/module.h"
#include "runtime/module_internal.h"
#include "runtime/paths.h"
#include "runtime/resource_archive.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int gene_value(const char *start, const char *end, const char **value, const char **stop) {
    while (start < end && (*start == ' ' || *start == '\t'))
        start++;
    if (end - start < 8 || memcmp(start, "allgenes", 8))
        return 0;
    start += 8;
    while (start < end && (*start == ' ' || *start == '\t'))
        start++;
    if (start == end || *start++ != '=')
        return 0;
    while (start < end && (*start == ' ' || *start == '\t'))
        start++;
    *value = start;
    while (start < end && *start != '\r' && *start != ' ' && *start != '\t' && *start != ';' &&
           *start != '#')
        start++;
    *stop = start;
    return 1;
}
static void unlock_genes(void) {
    const char *path = PORT_RESOURCES "global_settings.ini";
    const char *temporary = PORT_RESOURCES "global_settings.ini.unlock.tmp";
    FILE *source = port_resource_fopen(path, "rb");
    long bytes = 0;
    if (source) {
        if (fseek(source, 0, SEEK_END) || (bytes = ftell(source)) < 0 ||
            fseek(source, 0, SEEK_SET)) {
            fclose(source);
            port_fail("Cannot seek gene settings");
        }
    } else if (errno != ENOENT)
        port_fail("Cannot read gene settings: %s", strerror(errno));
    size_t length = (size_t)bytes;
    char *data = malloc(length + 1);
    if (!data) {
        if (source)
            fclose(source);
        port_fail("Cannot allocate gene settings buffer");
    }
    if (source) {
        int failed = fread(data, 1, length, source) != length;
        if (fclose(source))
            failed = 1;
        if (failed) {
            free(data);
            port_fail("Cannot read gene settings");
        }
    }
    data[length] = 0;
    size_t text_length = length;
    while (text_length && data[text_length - 1] == 0)
        text_length--;
    const char *end = data + text_length;
    int found = 0, changed = 0;
    for (const char *line = data; line < end;) {
        const char *next = memchr(line, '\n', (size_t)(end - line));
        const char *value, *stop;
        if (gene_value(line, next ? next : end, &value, &stop)) {
            found = 1;
            if (stop - value != 1 || *value != '1')
                changed = 1;
        }
        line = next ? next + 1 : end;
    }
    if (found && !changed) {
        free(data);
        return;
    }
    FILE *output = fopen(temporary, "wb");
    if (!output) {
        free(data);
        port_fail("Cannot write gene settings: %s", strerror(errno));
    }
    const char *written = data;
    for (const char *line = data; line < end;) {
        const char *next = memchr(line, '\n', (size_t)(end - line));
        const char *value, *stop;
        if (gene_value(line, next ? next : end, &value, &stop)) {
            fwrite(written, 1, (size_t)(value - written), output);
            fputc('1', output);
            written = stop;
        }
        line = next ? next + 1 : end;
    }
    fwrite(written, 1, (size_t)(end - written), output);
    if (!found) {
        const char *newline = memchr(data, '\r', text_length) ? "\r\n" : "\n";
        if (text_length && data[text_length - 1] != '\n')
            fputs(newline, output);
        fputs("allgenes=1", output);
        fputs(newline, output);
    }
    // Preserve trailing NUL bytes after the text.
    fwrite(end, 1, length - text_length, output);
    int failed = ferror(output);
    if (fflush(output))
        failed = 1;
    if (fclose(output))
        failed = 1;
    free(data);
    if (failed) {
        remove(temporary);
        port_fail("Cannot save gene settings");
    }
    if (rename(temporary, path)) {
        remove(temporary);
        port_fail("Cannot replace gene settings: %s", strerror(errno));
    }
    port_resource_enable_gene_override();
    port_log_debug("Unlock-all: enabled allgenes in global_settings.ini");
}
static signed char unlocked_product(void *self, void *selector, void *product) {
    (void)self;
    (void)selector;
    (void)product;
    return 1;
}

void port_patch_unlocks(void) {
    if (!port_app_options.unlock_all)
        return;
    unlock_genes();
    uintptr_t address = port_module_image()->load_addr + 0x36bf04;
    hook_arm(address, (uintptr_t)unlocked_product);
}
