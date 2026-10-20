#include "runtime/native_memory.h"

#include "app/log.h"

#include <errno.h>
#include <malloc.h>
#include <psp2/kernel/clib.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vitaGL.h>

extern unsigned int _newlib_heap_size_user;
enum {
    NEWLIB_SERVICE_RESERVE_BYTES = 16 * 1024 * 1024,
    HEAP_SAMPLE_MASK = 255,
    HEAP_SAMPLE_REQUEST_BYTES = 64 * 1024,
    ESTIMATED_BLOCK_OVERHEAD_BYTES = 16,
};
static pthread_mutex_t allocation_lock = PTHREAD_MUTEX_INITIALIZER;
static size_t estimated_heap_used;
static unsigned allocation_samples;
void port_native_free(void *allocation) { vglFree(allocation); }
void *port_native_allocate(size_t requested_bytes, uintptr_t caller) {
    size_t allocation_bytes = requested_bytes ? requested_bytes : 1;
    size_t newlib_limit_bytes = _newlib_heap_size_user - NEWLIB_SERVICE_RESERVE_BYTES;
    void *allocation = NULL;
    pthread_mutex_lock(&allocation_lock);
    if ((allocation_samples++ & HEAP_SAMPLE_MASK) == 0 ||
        allocation_bytes >= HEAP_SAMPLE_REQUEST_BYTES) {
        estimated_heap_used = (size_t)mallinfo().uordblks;
    }
    if (estimated_heap_used < newlib_limit_bytes &&
        allocation_bytes <= newlib_limit_bytes - estimated_heap_used) {
        allocation = malloc(allocation_bytes);
        if (allocation)
            estimated_heap_used += allocation_bytes + ESTIMATED_BLOCK_OVERHEAD_BYTES;
    }
    pthread_mutex_unlock(&allocation_lock);
    if (!allocation)
        allocation = vglAlloc(allocation_bytes, VGL_MEM_RAM);
    if (!allocation)
        allocation = vglAlloc(allocation_bytes, VGL_MEM_PHYCONT);
    if (!allocation)
        allocation = vglAlloc(allocation_bytes, VGL_MEM_VRAM);
    if (!allocation) {
        errno = ENOMEM;
        port_log_error("MEM allocation failed bytes=%u caller=%p newlib_used=%u "
                       "ram_free=%u phycont_free=%u cdram_free=%u",
                       (unsigned)allocation_bytes, (void *)caller, (unsigned)mallinfo().uordblks,
                       (unsigned)vglMemFree(VGL_MEM_RAM), (unsigned)vglMemFree(VGL_MEM_PHYCONT),
                       (unsigned)vglMemFree(VGL_MEM_VRAM));
        port_log_flush();
    }
    return allocation;
}
void *port_native_malloc(size_t requested_bytes) {
    return port_native_allocate(requested_bytes, (uintptr_t)__builtin_return_address(0));
}
void *port_native_calloc(size_t element_count, size_t element_size_bytes) {
    if (element_size_bytes && element_count > SIZE_MAX / element_size_bytes) {
        errno = ENOMEM;
        return NULL;
    }
    size_t allocation_bytes = element_count * element_size_bytes;
    void *allocation = port_native_allocate(allocation_bytes, 0);
    if (allocation)
        sceClibMemset(allocation, 0, allocation_bytes);
    return allocation;
}
void *port_native_realloc(void *allocation, size_t requested_bytes) {
    if (!requested_bytes) {
        port_native_free(allocation);
        return NULL;
    }
    size_t previous_size_bytes = allocation ? vglMallocUsableSize(allocation) : 0;
    if (allocation && requested_bytes <= previous_size_bytes) {
        if (previous_size_bytes - requested_bytes <= ESTIMATED_BLOCK_OVERHEAD_BYTES)
            return allocation;
        void *smaller = vglRealloc(allocation, requested_bytes);
        return smaller ? smaller : allocation;
    }
    void *next = port_native_allocate(requested_bytes, 0);
    if (!next)
        return NULL;
    if (allocation) {
        sceClibMemcpy(next, allocation, previous_size_bytes);
        port_native_free(allocation);
    }
    return next;
}
char *port_native_strndup(const char *text, size_t maximum) {
    size_t text_length = strnlen(text, maximum);
    if (text_length == SIZE_MAX) {
        errno = ENOMEM;
        return NULL;
    }
    char *copy = port_native_allocate(text_length + 1, 0);
    if (copy) {
        sceClibMemcpy(copy, text, text_length);
        copy[text_length] = 0;
    }
    return copy;
}
char *port_native_strdup(const char *text) { return port_native_strndup(text, SIZE_MAX); }
