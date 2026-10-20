#ifndef PLAGUE_RUNTIME_MODULE_H
#define PLAGUE_RUNTIME_MODULE_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    const void *handle;
    uintptr_t load_base;
    uintptr_t text_base;
    size_t text_size;
    uintptr_t exidx_base;
    size_t exidx_count;
} PortModuleInfo;
const PortModuleInfo *port_module_info(void);
uintptr_t port_module_find_symbol(const char *name);
uintptr_t port_module_require_symbol(const char *name);
// Load from the verified APK; returns -1 on read failure for a main-thread dialog.
int port_module_load(void);
void port_module_bind(void);
void port_module_initialize(void);
void port_module_flush(void);
#endif
