#include "runtime/paths.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

const char *port_path_normalize_file(const char *path) {
    if (!path || strncmp(path, PORT_ROOT, strlen(PORT_ROOT)))
        return path;
    const char *result = path, *next = path;
    while ((next = strstr(next + strlen(PORT_ROOT), PORT_ROOT))) {
        if (next[-1] != '/')
            break;
        result = next;
    }
    return result;
}

int port_path_build_resource(const char *name, char *output, size_t capacity,
                             PortAssetPathKind kind) {
    if (!name || strchr(name, ':') || strchr(name, '\\') || *name == '/')
        return 0;
    if (kind == PORT_JAVA_ASSET_PATH && !strncmp(name, "assets/", 7))
        name += 7;
    if (!strncmp(name, "unpack/", 7))
        name += 7;
    if (kind == PORT_JAVA_ASSET_PATH && !*name)
        return 0;
    for (const char *segment = name; *segment;) {
        const char *end = strchr(segment, '/');
        size_t length = end ? (size_t)(end - segment) : strlen(segment);
        if (!length || (length == 2 && !memcmp(segment, "..", 2)) ||
            (kind == PORT_NDK_ASSET_PATH && length == 1 && *segment == '.'))
            return 0;
        if (!end)
            break;
        segment = end + 1;
    }
    int written = snprintf(output, capacity, "%s%s", PORT_RESOURCES, name);
    return written >= 0 && (size_t)written < capacity;
}
int port_path_is_local(const char *path) {
    return path && !strncmp(path, PORT_ROOT, strlen(PORT_ROOT)) && !strstr(path, "/../");
}
