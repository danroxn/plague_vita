#include "runtime/resource_archive.h"

#include "runtime/paths.h"

#include <errno.h>
#include <limits.h>
#include <minizip/ioapi.h>
#include <minizip/unzip.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define APK_PATH PORT_ROOT "package.apk"
#define LIBRARY_PATH "lib/armeabi-v7a/libgame.so"
enum { MAX_ENTRIES = 1024, MAX_ARCHIVE_ENTRIES = 2048, NAME_BYTES = 256,
       MAX_ENTRY_BYTES = 32 * 1024 * 1024 };

typedef struct {
    char name[NAME_BYTES];
    uint32_t offset, bytes, compressed_bytes;
    uint16_t method;
    uint8_t directory;
} ResourceEntry;
enum { INDEX_CACHE_BLOCK_BYTES = 32768 };
typedef struct {
    SceUID fd;
    int failed;
    uint64_t position, cache_offset[2];
    uint32_t cache_bytes[2], age[2], clock;
    uint8_t cache[2][INDEX_CACHE_BLOCK_BYTES];
} ZipInput;
struct PortResourceStream {
    const ResourceEntry *entry;
    SceUID fd;
    uint8_t *memory;
    uint32_t bytes, offset;
    int64_t position;
};
struct PortResourceDirectory {
    const ResourceEntry *entry;
    size_t next;
    SceUID fd;
    char disk_name[NAME_BYTES];
};
static struct {
    ResourceEntry entries[MAX_ENTRIES], library;
    size_t count;
    uint64_t bytes;
    int ready, gene_override;
} archive;

static int fail(int error) { errno = error; return -1; }

static int canonical_name(const char *path, char name[NAME_BYTES]) {
    if (!path)
        return 0;
    path = port_path_normalize_file(path);
    // Native font setup joins a files path ending in '/' with '/Contents/...'.
    // Match normal filesystem handling of repeated separators before deciding
    // whether an absolute path belongs to the immutable resource namespace.
    char normalized[1024];
    size_t length = 0;
    for (const char *cursor = path; *cursor; cursor++) {
        if (*cursor == '/' && length && normalized[length - 1] == '/')
            continue;
        if (length == sizeof(normalized) - 1)
            return 0;
        normalized[length++] = *cursor;
    }
    normalized[length] = 0;
    path = normalized;
    size_t base = strlen(PORT_RESOURCES) - 1;
    if (!strncmp(path, PORT_RESOURCES, base) && (!path[base] || path[base] == '/'))
        path += base + (path[base] == '/');
    else if (*path == '/' || strchr(path, ':'))
        return 0;
    while (!strncmp(path, "./", 2))
        path += 2;
    if (!strcmp(path, "."))
        path++;
    if (!strncmp(path, "assets/", 7))
        path += 7;
    if (!strncmp(path, "unpack/", 7))
        path += 7;
    size_t bytes = strlen(path);
    while (bytes && path[bytes - 1] == '/')
        bytes--;
    if (bytes >= NAME_BYTES || memchr(path, '\\', bytes) || memchr(path, ':', bytes))
        return 0;
    for (size_t start = 0; start < bytes;) {
        size_t end = start;
        while (end < bytes && path[end] != '/')
            end++;
        size_t count = end - start;
        if (!count || (count == 1 && path[start] == '.') ||
            (count == 2 && path[start] == '.' && path[start + 1] == '.'))
            return 0;
        start = end + 1;
    }
    memcpy(name, path, bytes);
    name[bytes] = 0;
    return 1;
}

static int compare_entries(const void *left, const void *right) {
    return strcmp(((const ResourceEntry *)left)->name, ((const ResourceEntry *)right)->name);
}

static const ResourceEntry *find_entry(const char *name) {
    size_t low = 0, high = archive.count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        int order = strcmp(name, archive.entries[middle].name);
        if (!order)
            return &archive.entries[middle];
        if (order < 0)
            high = middle;
        else
            low = middle + 1;
    }
    return NULL;
}

static const ResourceEntry *path_entry(const char *path) {
    char name[NAME_BYTES];
    return archive.ready && canonical_name(path, name) ? find_entry(name) : NULL;
}

int port_resource_is_path(const char *path) { return path_entry(path) != NULL; }

static ResourceEntry *add_entry(const char *name, int directory) {
    for (size_t i = 0; i < archive.count; i++) {
        if (strcmp(name, archive.entries[i].name))
            continue;
        if (!directory || !archive.entries[i].directory)
            return NULL;
        return &archive.entries[i];
    }
    if (archive.count == MAX_ENTRIES)
        return NULL;
    ResourceEntry *entry = &archive.entries[archive.count++];
    strcpy(entry->name, name);
    entry->directory = directory;
    return entry;
}

static int add_parents(const char *name) {
    char parent[NAME_BYTES];
    strcpy(parent, name);
    for (char *slash = strchr(parent, '/'); slash; slash = strchr(slash + 1, '/')) {
        *slash = 0;
        int valid = add_entry(parent, 1) != NULL;
        *slash = '/';
        if (!valid)
            return -1;
    }
    return 0;
}

// MiniZIP validates the archive and local headers. Its callbacks avoid Newlib
// FILE allocation while the native/module workers are active.
static void *zip_open(void *opaque, const void *path, int mode) {
    (void)opaque;
    if (!(mode & ZLIB_FILEFUNC_MODE_READ) || (mode & ZLIB_FILEFUNC_MODE_WRITE))
        return NULL;
    ZipInput *input = calloc(1, sizeof(*input));
    if (!input)
        return NULL;
    input->fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (input->fd < 0) {
        free(input);
        return NULL;
    }
    return input;
}
static unsigned long zip_read(void *opaque, void *handle, void *data, unsigned long bytes) {
    (void)opaque;
    ZipInput *input = handle;
    if (bytes > INT_MAX) {
        input->failed = 1;
        return 0;
    }
    unsigned long done = 0;
    while (done < bytes && input->position < archive.bytes) {
        unsigned slot;
        for (slot = 0; slot < 2; slot++)
            if (input->cache_bytes[slot] && input->position >= input->cache_offset[slot] &&
                input->position - input->cache_offset[slot] < input->cache_bytes[slot])
                break;
        if (slot == 2) {
            slot = input->age[0] <= input->age[1] ? 0 : 1;
            uint64_t start = input->position & ~(uint64_t)(INDEX_CACHE_BLOCK_BYTES - 1);
            uint64_t left = archive.bytes - start;
            size_t count = left < INDEX_CACHE_BLOCK_BYTES ? left : INDEX_CACHE_BLOCK_BYTES;
            if (sceIoLseek(input->fd, start, SCE_SEEK_SET) < 0) {
                input->failed = 1;
                break;
            }
            size_t received = 0;
            while (received < count) {
                int read = sceIoRead(input->fd, input->cache[slot] + received, count - received);
                if (read <= 0) {
                    input->failed = 1;
                    break;
                }
                received += (size_t)read;
            }
            if (input->failed)
                break;
            input->cache_offset[slot] = start;
            input->cache_bytes[slot] = (uint32_t)received;
        }
        size_t offset = input->position - input->cache_offset[slot];
        size_t count = input->cache_bytes[slot] - offset;
        if (count > bytes - done)
            count = bytes - done;
        memcpy((uint8_t *)data + done, input->cache[slot] + offset, count);
        input->age[slot] = ++input->clock;
        input->position += count;
        done += count;
    }
    return done;
}
static unsigned long zip_write(void *opaque, void *handle, const void *data, unsigned long bytes) {
    (void)opaque; (void)data; (void)bytes;
    ((ZipInput *)handle)->failed = 1;
    return 0;
}
static ZPOS64_T zip_tell(void *opaque, void *handle) {
    (void)opaque;
    return ((ZipInput *)handle)->position;
}
static long zip_seek(void *opaque, void *handle, ZPOS64_T offset, int origin) {
    (void)opaque;
    ZipInput *input = handle;
    int64_t base = origin == ZLIB_FILEFUNC_SEEK_SET ? 0 :
                   origin == ZLIB_FILEFUNC_SEEK_CUR ? (int64_t)input->position :
                   origin == ZLIB_FILEFUNC_SEEK_END ? (int64_t)archive.bytes : -1;
    int64_t displacement = (int64_t)offset;
    if (base < 0 || displacement < -base || displacement > (int64_t)archive.bytes - base) {
        input->failed = 1;
        return -1;
    }
    input->position = (uint64_t)(base + displacement);
    return 0;
}
static int zip_close(void *opaque, void *handle) {
    (void)opaque;
    ZipInput *input = handle;
    int result = sceIoClose(input->fd);
    free(input);
    return result < 0 ? -1 : 0;
}
static int zip_error(void *opaque, void *handle) {
    (void)opaque;
    return ((ZipInput *)handle)->failed;
}

int port_resource_archive_init(void) {
    if (archive.ready)
        return 0;
    memset(&archive, 0, sizeof(archive));
    SceIoStat status;
    if (sceIoGetstat(APK_PATH, &status) < 0)
        return fail(ENOENT);
    if (status.st_size <= 0 || status.st_size > INT_MAX)
        return fail(EINVAL);
    archive.bytes = status.st_size;
    zlib_filefunc64_def io = {zip_open, zip_read, zip_write, zip_tell,
                              zip_seek, zip_close, zip_error, NULL};
    unzFile file = unzOpen2_64(APK_PATH, &io);
    if (!file)
        return fail(EINVAL);
    unz_global_info64 global;
    int valid = unzGetGlobalInfo64(file, &global) == UNZ_OK &&
                global.number_entry > 0 && global.number_entry <= MAX_ARCHIVE_ENTRIES &&
                add_entry("", 1) != NULL && unzGoToFirstFile(file) == UNZ_OK;
    for (uint64_t i = 0; valid && i < global.number_entry; i++) {
        char filename[1024], name[NAME_BYTES];
        unz_file_info64 info;
        valid = unzGetCurrentFileInfo64(file, &info, filename, sizeof(filename),
                                         NULL, 0, NULL, 0) == UNZ_OK &&
                info.size_filename < sizeof(filename) &&
                strlen(filename) == info.size_filename;
        if (!valid)
            break;
        int library = !strcmp(filename, LIBRARY_PATH);
        int resource = !strncmp(filename, "assets/", 7);
        if (library || resource) {
            int directory = info.size_filename && filename[info.size_filename - 1] == '/';
            valid = !(info.flag & 1u) && !info.disk_num_start &&
                    (info.compression_method == 0 || info.compression_method == Z_DEFLATED) &&
                    info.uncompressed_size <= MAX_ENTRY_BYTES &&
                    info.compressed_size <= MAX_ENTRY_BYTES &&
                    ((info.external_fa >> 16) & S_IFMT) != S_IFLNK;
            ResourceEntry *entry = NULL;
            if (valid && resource) {
                valid = canonical_name(filename, name) && !add_parents(name);
                if (valid)
                    entry = add_entry(name, directory);
                valid = entry != NULL;
            } else if (valid) {
                valid = !archive.library.name[0] && !directory;
                if (valid) {
                    entry = &archive.library;
                    strcpy(entry->name, LIBRARY_PATH);
                }
            }
            if (valid && !directory) {
                valid = unzOpenCurrentFile2(file, NULL, NULL, 1) == UNZ_OK;
                if (valid) {
                    uint64_t offset = unzGetCurrentFileZStreamPos64(file);
                    valid = offset <= archive.bytes && info.compressed_size <= archive.bytes - offset &&
                            (info.compression_method || info.compressed_size == info.uncompressed_size);
                    entry->offset = (uint32_t)offset;
                    entry->bytes = (uint32_t)info.uncompressed_size;
                    entry->compressed_bytes = (uint32_t)info.compressed_size;
                    entry->method = (uint16_t)info.compression_method;
                    if (unzCloseCurrentFile(file) != UNZ_OK)
                        valid = 0;
                }
            }
        }
        if (valid && i + 1 < global.number_entry)
            valid = unzGoToNextFile(file) == UNZ_OK;
    }
    if (unzClose(file) != UNZ_OK)
        valid = 0;
    if (!valid || !archive.library.name[0]) {
        memset(&archive, 0, sizeof(archive));
        return fail(EINVAL);
    }
    qsort(archive.entries, archive.count, sizeof(archive.entries[0]), compare_entries);
    sceIoMkdir(PORT_FILES, 0777);
    sceIoMkdir(PORT_FILES "/Contents", 0777);
    sceIoMkdir(PORT_RESOURCES, 0777);
    // This one resource is writable configuration, including gene unlocks
    // retained from an earlier unlock-all session. Other original files stay ZIP-only.
    if (!sceIoGetstat(PORT_RESOURCES "global_settings.ini", &status) &&
        SCE_S_ISREG(status.st_mode))
        archive.gene_override = 1;
    archive.ready = 1;
    return 0;
}

static int read_exact(SceUID fd, void *data, size_t bytes) {
    uint8_t *output = data;
    while (bytes) {
        int count = sceIoRead(fd, output, bytes > INT_MAX ? INT_MAX : bytes);
        if (count <= 0)
            return fail(EIO);
        output += count;
        bytes -= count;
    }
    return 0;
}

static int read_entry_into(const ResourceEntry *entry, void *output, size_t capacity) {
    if (capacity < entry->bytes)
        return fail(EOVERFLOW);
    SceUID fd = sceIoOpen(APK_PATH, SCE_O_RDONLY, 0);
    if (fd < 0)
        return fail(EIO);
    int valid = sceIoLseek(fd, entry->offset, SCE_SEEK_SET) == entry->offset;
    if (valid && !entry->method)
        valid = !read_exact(fd, output, entry->bytes);
    else if (valid) {
        uint8_t input[16384], overflow;
        z_stream stream = {0};
        int initialized = inflateInit2(&stream, -MAX_WBITS) == Z_OK;
        valid = initialized;
        if (initialized) {
            stream.next_out = output;
            stream.avail_out = entry->bytes;
            uint32_t left = entry->compressed_bytes;
            int status = Z_OK;
            while (valid && status != Z_STREAM_END) {
                if (!stream.avail_in && left) {
                    uint32_t bytes = left < sizeof(input) ? left : sizeof(input);
                    valid = !read_exact(fd, input, bytes);
                    if (!valid)
                        break;
                    left -= bytes;
                    stream.next_in = input;
                    stream.avail_in = bytes;
                }
                if (!stream.avail_out) {
                    stream.next_out = &overflow;
                    stream.avail_out = 1;
                }
                status = inflate(&stream, Z_NO_FLUSH);
                valid = (status == Z_OK || status == Z_STREAM_END) && stream.total_out <= entry->bytes;
            }
            valid = valid && status == Z_STREAM_END && stream.total_out == entry->bytes &&
                    stream.total_in == entry->compressed_bytes && !left && !stream.avail_in;
            inflateEnd(&stream);
        }
    }
    if (sceIoClose(fd) < 0)
        valid = 0;
    return valid ? 0 : fail(EIO);
}

static void entry_stat(uint32_t bytes, int directory, struct stat *result) {
    memset(result, 0, sizeof(*result));
    result->st_mode = directory ? S_IFDIR | 0555 : S_IFREG | 0444;
    result->st_size = bytes;
    result->st_nlink = 1;
    result->st_blksize = 4096;
    result->st_blocks = (bytes + 511) / 512;
}

static int is_gene_override(const ResourceEntry *entry) {
    return !strcmp(entry->name, "global_settings.ini") &&
           __atomic_load_n(&archive.gene_override, __ATOMIC_ACQUIRE);
}
void port_resource_enable_gene_override(void) {
    __atomic_store_n(&archive.gene_override, 1, __ATOMIC_RELEASE);
}

int port_resource_stat(const char *path, struct stat *result) {
    if (!result)
        return fail(EINVAL);
    const ResourceEntry *entry = path_entry(path);
    if (!entry)
        return stat(path, result);
    if (is_gene_override(entry))
        return stat(PORT_RESOURCES "global_settings.ini", result);
    entry_stat(entry->bytes, entry->directory, result);
    return 0;
}

PortResourceStream *port_resource_stream_open(const char *path) {
    const ResourceEntry *entry = path_entry(path);
    if (!entry || entry->directory) {
        fail(entry ? EISDIR : ENOENT);
        return NULL;
    }
    PortResourceStream *stream = calloc(1, sizeof(*stream));
    if (!stream)
        return NULL;
    stream->entry = entry;
    stream->bytes = entry->bytes;
    stream->offset = entry->offset;
    stream->fd = -1;
    if (is_gene_override(entry)) {
        stream->fd = sceIoOpen(PORT_RESOURCES "global_settings.ini", SCE_O_RDONLY, 0);
        SceIoStat status;
        if (stream->fd < 0 || sceIoGetstatByFd(stream->fd, &status) < 0 ||
            status.st_size < 0 || status.st_size > MAX_ENTRY_BYTES)
            goto error;
        stream->bytes = (uint32_t)status.st_size;
        stream->offset = 0;
    } else if (entry->method) {
        stream->memory = malloc(entry->bytes ? entry->bytes : 1);
        if (!stream->memory || read_entry_into(entry, stream->memory, entry->bytes))
            goto error;
    } else {
        stream->fd = sceIoOpen(APK_PATH, SCE_O_RDONLY, 0);
        if (stream->fd < 0)
            goto error;
    }
    return stream;
error:
    port_resource_stream_close(stream);
    fail(EIO);
    return NULL;
}

ssize_t port_resource_stream_read(PortResourceStream *stream, void *buffer, size_t bytes) {
    if (!stream || (!buffer && bytes))
        return fail(EINVAL);
    if (stream->position >= stream->bytes || !bytes)
        return 0;
    size_t remaining = stream->bytes - (uint32_t)stream->position;
    if (bytes > remaining)
        bytes = remaining;
    if (stream->memory)
        memcpy(buffer, stream->memory + stream->position, bytes);
    else if (sceIoLseek(stream->fd, stream->offset + stream->position, SCE_SEEK_SET) < 0 ||
             read_exact(stream->fd, buffer, bytes))
        return fail(EIO);
    stream->position += bytes;
    return (ssize_t)bytes;
}

int64_t port_resource_stream_seek(PortResourceStream *stream, int64_t offset, int whence) {
    if (!stream)
        return fail(EINVAL);
    int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? stream->position :
                   whence == SEEK_END ? (int64_t)stream->bytes : -1;
    if (base < 0 || offset < -base || offset > INT_MAX - base)
        return fail(EINVAL);
    stream->position = base + offset;
    return stream->position;
}
size_t port_resource_stream_length(const PortResourceStream *stream) {
    return stream ? stream->bytes : 0;
}
int port_resource_stream_stat(const PortResourceStream *stream, struct stat *result) {
    if (!stream || !result)
        return fail(EINVAL);
    entry_stat(stream->bytes, 0, result);
    return 0;
}
int port_resource_stream_close(PortResourceStream *stream) {
    if (!stream)
        return fail(EINVAL);
    int result = stream->fd >= 0 ? sceIoClose(stream->fd) : 0;
    free(stream->memory);
    free(stream);
    return result < 0 ? fail(EIO) : 0;
}

static ssize_t file_read(void *cookie, char *data, size_t bytes) {
    return port_resource_stream_read(cookie, data, bytes);
}
static int file_seek(void *cookie, off_t *offset, int whence) {
    int64_t position = port_resource_stream_seek(cookie, *offset, whence);
    if (position < 0)
        return -1;
    *offset = (off_t)position;
    return 0;
}
static int file_close(void *cookie) { return port_resource_stream_close(cookie); }

FILE *port_resource_fopen(const char *path, const char *mode) {
    if (!path || !mode) {
        fail(EINVAL);
        return NULL;
    }
    if (*mode != 'r' || strchr(mode, '+') || !port_resource_is_path(path))
        return fopen(path, mode);
    PortResourceStream *stream = port_resource_stream_open(path);
    if (!stream)
        return NULL;
    const cookie_io_functions_t callbacks = {file_read, NULL, file_seek, file_close};
    FILE *file = fopencookie(stream, mode, callbacks);
    if (!file)
        port_resource_stream_close(stream);
    return file;
}

PortResourceDirectory *port_resource_directory_open(const char *path) {
    const ResourceEntry *entry = path_entry(path);
    if (!entry || !entry->directory) {
        fail(entry ? ENOTDIR : ENOENT);
        return NULL;
    }
    PortResourceDirectory *directory = calloc(1, sizeof(*directory));
    if (!directory)
        return NULL;
    directory->entry = entry;
    char disk_path[sizeof(PORT_RESOURCES) + NAME_BYTES];
    snprintf(disk_path, sizeof(disk_path), "%s%s", PORT_RESOURCES, entry->name);
    directory->fd = sceIoDopen(disk_path);
    return directory;
}

int port_resource_directory_read(PortResourceDirectory *directory, const char **name,
                                 struct stat *result) {
    if (!directory || !name || !result)
        return fail(EINVAL);
    size_t prefix = strlen(directory->entry->name);
    while (directory->next < archive.count) {
        const ResourceEntry *entry = &archive.entries[directory->next++];
        const char *child = entry->name;
        if (prefix) {
            if (strncmp(child, directory->entry->name, prefix) || child[prefix] != '/')
                continue;
            child += prefix + 1;
        }
        if (!*child || strchr(child, '/'))
            continue;
        *name = child;
        entry_stat(entry->bytes, entry->directory, result);
        return 1;
    }
    while (directory->fd >= 0) {
        SceIoDirent disk = {0};
        int count = sceIoDread(directory->fd, &disk);
        if (count <= 0)
            return count < 0 ? fail(EIO) : 0;
        if (!strcmp(disk.d_name, ".") || !strcmp(disk.d_name, ".."))
            continue;
        char candidate[NAME_BYTES];
        int length = snprintf(candidate, sizeof(candidate), "%s%s%s", directory->entry->name,
                               prefix ? "/" : "", disk.d_name);
        if (length < 0 || (size_t)length >= sizeof(candidate) || find_entry(candidate))
            continue;
        size_t bytes = strlen(disk.d_name);
        if (bytes >= sizeof(directory->disk_name))
            continue;
        memcpy(directory->disk_name, disk.d_name, bytes + 1);
        *name = directory->disk_name;
        entry_stat((uint32_t)disk.d_stat.st_size, SCE_S_ISDIR(disk.d_stat.st_mode), result);
        return 1;
    }
    return 0;
}
void port_resource_directory_close(PortResourceDirectory *directory) {
    if (!directory)
        return;
    if (directory->fd >= 0)
        sceIoDclose(directory->fd);
    free(directory);
}

int port_resource_archive_load_library_into(void *data, size_t capacity, size_t *bytes) {
    if (bytes)
        *bytes = 0;
    if (!archive.ready || !data || !bytes)
        return fail(EINVAL);
    if (read_entry_into(&archive.library, data, capacity))
        return -1;
    *bytes = archive.library.bytes;
    return 0;
}
int port_resource_archive_load_library(void **data, size_t *bytes) {
    if (data)
        *data = NULL;
    if (bytes)
        *bytes = 0;
    if (!archive.ready || !data || !bytes)
        return fail(EINVAL);
    void *buffer = malloc(archive.library.bytes ? archive.library.bytes : 1);
    if (!buffer)
        return -1;
    if (port_resource_archive_load_library_into(buffer, archive.library.bytes, bytes)) {
        free(buffer);
        return -1;
    }
    *data = buffer;
    return 0;
}
