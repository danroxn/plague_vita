#ifndef PLAGUE_RUNTIME_NATIVE_MEMORY_H
#define PLAGUE_RUNTIME_NATIVE_MEMORY_H
#include <stddef.h>
#include <stdint.h>
void *port_native_allocate(size_t requested_bytes, uintptr_t caller);
void *port_native_malloc(size_t requested_bytes);
void *port_native_calloc(size_t element_count, size_t element_size_bytes);
void *port_native_realloc(void *allocation, size_t requested_bytes);
void port_native_free(void *allocation);
char *port_native_strdup(const char *text);
char *port_native_strndup(const char *text, size_t maximum);
#endif
