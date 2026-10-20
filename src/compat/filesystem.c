#include "compat/import_table.h"

#include "compat/bionic_layout.h"
#include "compat/resource_files.h"
#include "runtime/paths.h"
#include "runtime/resource_archive.h"

#include <bionic/fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <psp2/io/stat.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

_Static_assert(__builtin_types_compatible_p(__typeof__(&fseek), __typeof__(&fseeko)),
               "Vita seek adapters require matching offset types");
_Static_assert(__builtin_types_compatible_p(__typeof__(&ftell), __typeof__(&ftello)),
               "Vita tell adapters require matching offset types");

enum { BIONIC_FILE_BYTES = 84, STANDARD_STREAM_COUNT = 3 };
enum { RESOURCE_FD_BASE = 0x7f000000, RESOURCE_FD_COUNT = 64 };
static PortResourceStream *resource_descriptors[RESOURCE_FD_COUNT];
static pthread_mutex_t descriptor_lock = PTHREAD_MUTEX_INITIALIZER;
typedef struct {
    DIR *disk;
    PortResourceDirectory *archive;
} CompatDirectory;

static int resource_slot(int descriptor) {
    unsigned slot = (unsigned)descriptor - RESOURCE_FD_BASE;
    return slot < RESOURCE_FD_COUNT ? (int)slot : -1;
}
static unsigned char standard_files[STANDARD_STREAM_COUNT * BIONIC_FILE_BYTES]
    __attribute__((aligned(8)));
static FILE *native_stream_from_bionic_file(FILE *file) {
    uintptr_t address = (uintptr_t)file, standard_base = (uintptr_t)standard_files;
    if (address >= standard_base && address < standard_base + sizeof(standard_files)) {
        size_t stream_index = (address - standard_base) / BIONIC_FILE_BYTES;
        return stream_index == 0 ? stdin : (stream_index == 1 ? stdout : stderr);
    }
    return file;
}
static FILE *compat_fopen(const char *path, const char *mode) {
    const char *normalized = port_path_normalize_file(path);
    FILE *file = port_resource_fopen(normalized, mode);
    // Country/disease files contain hundreds of thousands of small fields.
    // Buffer their immutable resource reads in larger SD-card transfers.
    if (file && !strcmp(mode, "rb") &&
        (!strchr(normalized, ':') ||
         !strncmp(normalized, PORT_RESOURCES, strlen(PORT_RESOURCES)))) {
        int saved_errno = errno;
        setvbuf(file, NULL, _IOFBF, 64 * 1024);
        errno = saved_errno;
    }
    return file;
}
static int compat_fclose(FILE *file) {
    if ((uintptr_t)file >= (uintptr_t)standard_files &&
        (uintptr_t)file < (uintptr_t)standard_files + sizeof(standard_files))
        return 0;
    return fclose(file);
}
static size_t compat_fread(void *buffer, size_t element_size, size_t element_count, FILE *file) {
    return fread(buffer, element_size, element_count, native_stream_from_bionic_file(file));
}
static size_t compat_fwrite(const void *buffer, size_t element_size, size_t element_count,
                            FILE *file) {
    return fwrite(buffer, element_size, element_count, native_stream_from_bionic_file(file));
}
static int compat_fflush(FILE *file) {
    return fflush(file ? native_stream_from_bionic_file(file) : NULL);
}
static int compat_feof(FILE *file) {
    return feof(native_stream_from_bionic_file(file));
}
static int compat_ferror(FILE *file) {
    return ferror(native_stream_from_bionic_file(file));
}
static int compat_fgetc(FILE *file) {
    return fgetc(native_stream_from_bionic_file(file));
}
static char *compat_fgets(char *buffer, int capacity, FILE *file) {
    return fgets(buffer, capacity, native_stream_from_bionic_file(file));
}
static int compat_fputc(int character, FILE *file) {
    return fputc(character, native_stream_from_bionic_file(file));
}
static int compat_fputs(const char *text, FILE *file) {
    return fputs(text, native_stream_from_bionic_file(file));
}
static int compat_fseek(FILE *file, long offset, int origin) {
    return fseek(native_stream_from_bionic_file(file), offset, origin);
}
static long compat_ftell(FILE *file) {
    return ftell(native_stream_from_bionic_file(file));
}
static void compat_rewind(FILE *file) {
    rewind(native_stream_from_bionic_file(file));
}
static int compat_setvbuf(FILE *file, char *buffer, int mode, size_t capacity) {
    return setvbuf(native_stream_from_bionic_file(file), buffer, mode, capacity);
}
static int compat_ungetc(int character, FILE *file) {
    return ungetc(character, native_stream_from_bionic_file(file));
}
static int compat_vfprintf(FILE *file, const char *format, va_list arguments) {
    return vfprintf(native_stream_from_bionic_file(file), format, arguments);
}
static int compat_fprintf(FILE *file, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int characters_written = compat_vfprintf(file, format, arguments);
    va_end(arguments);
    return characters_written;
}
static FILE *compat_freopen(const char *path, const char *mode, FILE *file) {
    return freopen(port_path_normalize_file(path), mode, native_stream_from_bionic_file(file));
}
static int compat_open(const char *path, int flags, ...) {
    mode_t mode = 0666;
    if (flags & BIONIC_O_CREAT) {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, int);
        va_end(arguments);
    }
    const char *normalized = port_path_normalize_file(path);
    int native_flags = oflags_bionic_to_newlib(flags);
    if ((native_flags & O_ACCMODE) != O_RDONLY ||
        (native_flags & (O_CREAT | O_TRUNC | O_APPEND)) ||
        !port_resource_is_path(normalized))
        return open(normalized, native_flags, mode);
    PortResourceStream *stream = port_resource_stream_open(normalized);
    if (!stream)
        return -1;
    pthread_mutex_lock(&descriptor_lock);
    for (int slot = 0; slot < RESOURCE_FD_COUNT; slot++) {
        if (!resource_descriptors[slot]) {
            resource_descriptors[slot] = stream;
            pthread_mutex_unlock(&descriptor_lock);
            return RESOURCE_FD_BASE + slot;
        }
    }
    pthread_mutex_unlock(&descriptor_lock);
    port_resource_stream_close(stream);
    errno = EMFILE;
    return -1;
}
static ssize_t compat_read(int descriptor, void *buffer, size_t bytes) {
    int slot = resource_slot(descriptor);
    if (slot < 0)
        return read(descriptor, buffer, bytes);
    pthread_mutex_lock(&descriptor_lock);
    PortResourceStream *stream = resource_descriptors[slot];
    ssize_t result = stream ? port_resource_stream_read(stream, buffer, bytes) : -1;
    if (!stream)
        errno = EBADF;
    pthread_mutex_unlock(&descriptor_lock);
    return result;
}
static off_t compat_lseek(int descriptor, off_t offset, int whence) {
    int slot = resource_slot(descriptor);
    if (slot < 0)
        return lseek(descriptor, offset, whence);
    pthread_mutex_lock(&descriptor_lock);
    PortResourceStream *stream = resource_descriptors[slot];
    int64_t result = stream ? port_resource_stream_seek(stream, offset, whence) : -1;
    if (!stream)
        errno = EBADF;
    if (result > INT32_MAX) {
        errno = EOVERFLOW;
        result = -1;
    }
    pthread_mutex_unlock(&descriptor_lock);
    return (off_t)result;
}
static int compat_close(int descriptor) {
    int slot = resource_slot(descriptor);
    if (slot < 0)
        return close(descriptor);
    pthread_mutex_lock(&descriptor_lock);
    PortResourceStream *stream = resource_descriptors[slot];
    resource_descriptors[slot] = NULL;
    int result = stream ? port_resource_stream_close(stream) : -1;
    if (!stream)
        errno = EBADF;
    pthread_mutex_unlock(&descriptor_lock);
    return result;
}
ssize_t port_compat_resource_pread(int descriptor, void *buffer, size_t bytes, off_t offset) {
    int slot = resource_slot(descriptor);
    if (slot < 0)
        return pread(descriptor, buffer, bytes, offset);
    pthread_mutex_lock(&descriptor_lock);
    PortResourceStream *stream = resource_descriptors[slot];
    ssize_t result = -1;
    if (stream) {
        int64_t previous = port_resource_stream_seek(stream, 0, SEEK_CUR);
        if (previous >= 0 && port_resource_stream_seek(stream, offset, SEEK_SET) >= 0) {
            result = port_resource_stream_read(stream, buffer, bytes);
            int saved_errno = errno;
            if (port_resource_stream_seek(stream, previous, SEEK_SET) < 0)
                result = -1;
            else
                errno = saved_errno;
        }
    } else
        errno = EBADF;
    pthread_mutex_unlock(&descriptor_lock);
    return result;
}
static int compat_access(const char *path, int mode) {
    const char *normalized = port_path_normalize_file(path);
    if (!port_resource_is_path(normalized))
        return access(normalized, mode);
    struct stat status;
    if (mode & ~(R_OK | W_OK | X_OK)) {
        errno = EINVAL;
        return -1;
    }
    if (port_resource_stat(normalized, &status))
        return -1;
    if ((mode & W_OK) || ((mode & X_OK) && !S_ISDIR(status.st_mode))) {
        errno = EACCES;
        return -1;
    }
    return 0;
}
static void copy_native_stat_to_bionic(const struct stat *native_stat,
                                       AndroidStat *android_stat_out) {
    memset(android_stat_out, 0, sizeof(*android_stat_out));
    android_stat_out->device = native_stat->st_dev;
    android_stat_out->legacy_inode = native_stat->st_ino;
    android_stat_out->inode = native_stat->st_ino;
    android_stat_out->mode = native_stat->st_mode;
    android_stat_out->links = native_stat->st_nlink;
    android_stat_out->uid = native_stat->st_uid;
    android_stat_out->gid = native_stat->st_gid;
    android_stat_out->rdevice = native_stat->st_rdev;
    android_stat_out->size = native_stat->st_size;
    android_stat_out->block_size = native_stat->st_blksize;
    android_stat_out->blocks = native_stat->st_blocks;
    android_stat_out->access_sec = native_stat->st_atime;
    android_stat_out->modify_sec = native_stat->st_mtime;
    android_stat_out->change_sec = native_stat->st_ctime;
}
static int compat_stat(const char *path, AndroidStat *android_stat_out) {
    struct stat native_stat;
    int result = port_resource_stat(port_path_normalize_file(path), &native_stat);
    if (!result)
        copy_native_stat_to_bionic(&native_stat, android_stat_out);
    return result;
}
static int compat_fstat(int fd, AndroidStat *android_stat_out) {
    struct stat native_stat;
    int slot = resource_slot(fd);
    int result;
    if (slot < 0)
        result = fstat(fd, &native_stat);
    else {
        pthread_mutex_lock(&descriptor_lock);
        PortResourceStream *stream = resource_descriptors[slot];
        result = stream ? port_resource_stream_stat(stream, &native_stat) : -1;
        if (!stream)
            errno = EBADF;
        pthread_mutex_unlock(&descriptor_lock);
    }
    if (!result)
        copy_native_stat_to_bionic(&native_stat, android_stat_out);
    return result;
}
static DIR *compat_opendir(const char *path) {
    const char *normalized = port_path_normalize_file(path);
    CompatDirectory *directory = calloc(1, sizeof(*directory));
    if (!directory)
        return NULL;
    if (port_resource_is_path(normalized))
        directory->archive = port_resource_directory_open(normalized);
    else
        directory->disk = opendir(normalized);
    if (!directory->archive && !directory->disk) {
        free(directory);
        return NULL;
    }
    return (DIR *)directory;
}
static int compat_closedir(DIR *handle) {
    if (!handle) {
        errno = EBADF;
        return -1;
    }
    CompatDirectory *directory = (CompatDirectory *)handle;
    int result = 0;
    if (directory->archive)
        port_resource_directory_close(directory->archive);
    else
        result = closedir(directory->disk);
    free(directory);
    return result;
}
static AndroidDirent *compat_readdir(DIR *handle) {
    static __thread AndroidDirent entry;
    CompatDirectory *directory = (CompatDirectory *)handle;
    const char *name;
    int is_directory;
    if (directory->archive) {
        struct stat status;
        if (port_resource_directory_read(directory->archive, &name, &status) != 1)
            return NULL;
        is_directory = S_ISDIR(status.st_mode);
    } else {
        struct dirent *native_entry = readdir(directory->disk);
        if (!native_entry)
            return NULL;
        name = native_entry->d_name;
        is_directory = SCE_S_ISDIR(native_entry->d_stat.st_mode);
    }
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.name, sizeof(entry.name), "%s", name);
    entry.type = is_directory ? ANDROID_DT_DIR : ANDROID_DT_REG;
    entry.record_size = sizeof(entry);
    return &entry;
}

static const PortImportEntry imports[] = {
    {"__sF", (uintptr_t)(standard_files)},      {"fopen", (uintptr_t)(compat_fopen)},
    {"fclose", (uintptr_t)(compat_fclose)},     {"fread", (uintptr_t)(compat_fread)},
    {"fwrite", (uintptr_t)(compat_fwrite)},     {"fflush", (uintptr_t)(compat_fflush)},
    {"feof", (uintptr_t)(compat_feof)},         {"ferror", (uintptr_t)(compat_ferror)},
    {"fgetc", (uintptr_t)(compat_fgetc)},       {"getc", (uintptr_t)(compat_fgetc)},
    {"fgets", (uintptr_t)(compat_fgets)},       {"fputc", (uintptr_t)(compat_fputc)},
    {"fputs", (uintptr_t)(compat_fputs)},       {"fseek", (uintptr_t)(compat_fseek)},
    {"fseeko", (uintptr_t)(compat_fseek)},     {"ftell", (uintptr_t)(compat_ftell)},
    {"ftello", (uintptr_t)(compat_ftell)},     {"rewind", (uintptr_t)(compat_rewind)},
    {"setvbuf", (uintptr_t)(compat_setvbuf)},   {"ungetc", (uintptr_t)(compat_ungetc)},
    {"vfprintf", (uintptr_t)(compat_vfprintf)}, {"fprintf", (uintptr_t)(compat_fprintf)},
    {"freopen", (uintptr_t)(compat_freopen)},   {"open", (uintptr_t)(compat_open)},
    {"read", (uintptr_t)(compat_read)},         {"lseek", (uintptr_t)(compat_lseek)},
    {"close", (uintptr_t)(compat_close)},       {"access", (uintptr_t)(compat_access)},
    {"stat", (uintptr_t)(compat_stat)},         {"lstat", (uintptr_t)(compat_stat)},
    {"fstat", (uintptr_t)(compat_fstat)},       {"readdir", (uintptr_t)(compat_readdir)},
    {"opendir", (uintptr_t)(compat_opendir)},   {"closedir", (uintptr_t)(compat_closedir)},
};
const PortImportTable port_filesystem_imports = {imports, sizeof(imports) / sizeof(imports[0])};
