#ifndef PLAGUE_RUNTIME_PATHS_H
#define PLAGUE_RUNTIME_PATHS_H
#include <stddef.h>
#ifndef PORT_ROOT
#define PORT_ROOT "ux0:data/plague/"
#endif
#define PORT_FILES PORT_ROOT "files"
#define PORT_RESOURCES PORT_FILES "/Contents/Resources/"
#define PORT_CACHE_DIR PORT_ROOT "cache"
#define PORT_TEXT_CACHE_PATH PORT_CACHE_DIR "/text-cache.bin"
typedef enum { PORT_NDK_ASSET_PATH, PORT_JAVA_ASSET_PATH } PortAssetPathKind;
int port_path_build_resource(const char *name, char *output, size_t capacity,
                             PortAssetPathKind kind);
int port_path_is_local(const char *path);
const char *port_path_normalize_file(const char *path);
#endif
