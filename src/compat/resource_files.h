#ifndef PLAGUE_COMPAT_RESOURCE_FILES_H
#define PLAGUE_COMPAT_RESOURCE_FILES_H

#include <stddef.h>
#include <sys/types.h>

// mmap's SDK-side reads must use the same archive descriptor adapter as native
// open/read/lseek. Non-resource descriptors retain the SDK implementation.
ssize_t port_compat_resource_pread(int descriptor, void *buffer, size_t bytes, off_t offset);

#endif
