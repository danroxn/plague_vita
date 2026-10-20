#ifndef PLAGUE_RUNTIME_LOADER_INTEGRATION_H
#define PLAGUE_RUNTIME_LOADER_INTEGRATION_H
#include <psp2/types.h>
#include <so_util.h>
#include <stdint.h>

SceUID port_loader_stage_alloc(const char *tag, SceSize size_bytes);
void *port_loader_stage_buffer(SceUID handle);
void port_loader_stage_free(SceUID handle);
// Consumes and releases the staging allocation, including on failure.
int port_loader_load_stage(so_module *module, SceUID handle, void *buffer, uintptr_t address);

#endif
