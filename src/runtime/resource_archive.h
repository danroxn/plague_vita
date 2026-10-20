#ifndef PLAGUE_RUNTIME_RESOURCE_ARCHIVE_H
#define PLAGUE_RUNTIME_RESOURCE_ARCHIVE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef struct PortResourceStream PortResourceStream;
typedef struct PortResourceDirectory PortResourceDirectory;

// Verify the entire APK before initializing on the main thread, then start
// resource-reading workers. Archive entries do not need separate checksums.
// All functions return -1/NULL with errno on failure unless documented otherwise.
int port_resource_archive_init(void);
int port_resource_is_path(const char *path);
FILE *port_resource_fopen(const char *path, const char *mode);
int port_resource_stat(const char *path, struct stat *result);
void port_resource_enable_gene_override(void);

// Streams accept resource paths only. Each stream owns its fd/RAM and supports
// independent seek/read positions. Callers own and close returned handles.
PortResourceStream *port_resource_stream_open(const char *path);
ssize_t port_resource_stream_read(PortResourceStream *stream, void *buffer, size_t bytes);
int64_t port_resource_stream_seek(PortResourceStream *stream, int64_t offset, int whence);
size_t port_resource_stream_length(const PortResourceStream *stream);
int port_resource_stream_stat(const PortResourceStream *stream, struct stat *result);
int port_resource_stream_close(PortResourceStream *stream);

// Directory read returns 1 for an entry, 0 at EOF, or -1 for error. Returned
// names remain valid until the next directory read or close.
PortResourceDirectory *port_resource_directory_open(const char *path);
int port_resource_directory_read(PortResourceDirectory *directory, const char **name,
                                 struct stat *result);
void port_resource_directory_close(PortResourceDirectory *directory);

// Library bytes are covered by APK verification. load_library allocates with
// malloc (caller frees); load_library_into avoids an additional staging copy.
int port_resource_archive_load_library(void **data, size_t *bytes);
int port_resource_archive_load_library_into(void *data, size_t capacity, size_t *bytes);

#endif
