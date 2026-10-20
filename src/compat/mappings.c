#include "compat/import_table.h"
#include "compat/resource_files.h"

#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAP_FAILED ((void *)-1)

enum {
    MAPPING_CAPACITY = 64,
    MAX_MAPPING_BYTES = 128 * 1024 * 1024,
    MAPPING_PAGE_BYTES = 4096,
    ANDROID_PROT_WRITE = 2,
    ANDROID_PROT_EXEC = 4,
    ANDROID_MAP_SHARED = 1,
    ANDROID_MAP_ANONYMOUS = 0x20,
};
typedef struct {
    void *address;
    size_t length;
} Mapping;
static Mapping mappings[MAPPING_CAPACITY];
static pthread_mutex_t map_lock = PTHREAD_MUTEX_INITIALIZER;
static void *compat_mmap(void *address, size_t length, int protection, int flags, int fd,
                         off_t offset) {
    (void)address;
    if (!length || length > MAX_MAPPING_BYTES || (protection & ANDROID_PROT_EXEC) ||
        ((flags & ANDROID_MAP_SHARED) && (protection & ANDROID_PROT_WRITE))) {
        errno = ENOTSUP;
        return MAP_FAILED;
    }
    void *mapped_copy = memalign(MAPPING_PAGE_BYTES, (length + MAPPING_PAGE_BYTES - 1) &
                                                         ~(size_t)(MAPPING_PAGE_BYTES - 1));
    if (!mapped_copy) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    memset(mapped_copy, 0, length);
    if (!(flags & ANDROID_MAP_ANONYMOUS)) {
        size_t copied_bytes = 0;
        while (copied_bytes < length) {
            int64_t read_offset = (int64_t)offset + (int64_t)copied_bytes;
            if (read_offset > INT32_MAX) {
                free(mapped_copy);
                errno = EOVERFLOW;
                return MAP_FAILED;
            }
            ssize_t bytes_read = port_compat_resource_pread(
                fd, (char *)mapped_copy + copied_bytes, length - copied_bytes, (off_t)read_offset);
            if (bytes_read < 0) {
                free(mapped_copy);
                return MAP_FAILED;
            }
            if (!bytes_read)
                break;
            copied_bytes += (size_t)bytes_read;
        }
    }
    pthread_mutex_lock(&map_lock);
    for (size_t i = 0; i < MAPPING_CAPACITY; i++)
        if (!mappings[i].address) {
            mappings[i] = (Mapping){mapped_copy, length};
            pthread_mutex_unlock(&map_lock);
            return mapped_copy;
        }
    pthread_mutex_unlock(&map_lock);
    free(mapped_copy);
    errno = ENOMEM;
    return MAP_FAILED;
}
static int compat_munmap(void *address, size_t length) {
    pthread_mutex_lock(&map_lock);
    for (size_t i = 0; i < MAPPING_CAPACITY; i++)
        if (mappings[i].address == address && mappings[i].length == length) {
            mappings[i] = (Mapping){0};
            pthread_mutex_unlock(&map_lock);
            free(address);
            return 0;
        }
    pthread_mutex_unlock(&map_lock);
    errno = EINVAL;
    return -1;
}
static int compat_mprotect(void *address, size_t length, int flags) {
    (void)address;
    (void)length;
    (void)flags;
    errno = ENOSYS;
    return -1;
}

static const PortImportEntry imports[] = {
    {"mmap", (uintptr_t)(compat_mmap)},
    {"munmap", (uintptr_t)(compat_munmap)},
    {"mprotect", (uintptr_t)(compat_mprotect)},
};
const PortImportTable port_mappings_imports = {imports, sizeof(imports) / sizeof(imports[0])};
