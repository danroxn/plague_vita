#include "runtime/loader_integration.h"

#include "app/log.h"

#include <stdlib.h>
#include <vitasdk.h>

// The APK staging buffer belongs to our heap; other loader blocks belong to the kernel.
static int loader_free_memblock(SceUID handle) {
    return handle == -1 ? 0 : sceKernelFreeMemBlock(handle);
}

#define sceKernelFreeMemBlock loader_free_memblock
#include "so_util/so_util.c"
#undef sceKernelFreeMemBlock

static struct {
    void *buffer;
    SceUID next_handle;
} staging = {.next_handle = 1};

SceUID port_loader_stage_alloc(const char *tag, SceSize size_bytes) {
    free(staging.buffer);
    staging.buffer = malloc(size_bytes);
    if (!staging.buffer) {
        port_log_error("Cannot allocate %s staging buffer: %u bytes", tag, (unsigned)size_bytes);
        return (SceUID)-1;
    }
    return staging.next_handle++;
}

void *port_loader_stage_buffer(SceUID handle) {
    return (handle > 0 && handle < staging.next_handle) ? staging.buffer : NULL;
}

void port_loader_stage_free(SceUID handle) {
    (void)handle;
    free(staging.buffer);
    staging.buffer = NULL;
}

int port_loader_load_stage(so_module *module, SceUID handle, void *buffer, uintptr_t address) {
    memset(module, 0, sizeof(*module));
    int result = so_load_internal(module, -1, buffer, address);
    port_loader_stage_free(handle);
    return result;
}
