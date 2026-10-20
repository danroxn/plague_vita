#include "compat/import_table.h"

#include "compat/imports.h"
#include "runtime/module.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern uintptr_t __exidx_start[], __exidx_end[];

static uintptr_t compat_exidx(uintptr_t program_counter, int *count) {
    if (program_counter >= port_module_info()->text_base &&
        program_counter < port_module_info()->text_base + port_module_info()->text_size) {
        *count = port_module_info()->exidx_count;
        return port_module_info()->exidx_base;
    }
    *count = (int)((__exidx_end - __exidx_start) / 2);
    return (uintptr_t)__exidx_start;
}
static void *compat_dlopen(const char *name, int flags) {
    (void)flags;
    if (!name || !strcmp(name, "libgame.so"))
        return (void *)port_module_info()->handle;
    return NULL;
}
static void *compat_dlsym(void *handle, const char *name) {
    (void)handle;
    uintptr_t symbol_address = port_module_find_symbol(name);
    return (void *)(symbol_address ? symbol_address : port_import(name));
}
static int compat_dlclose(void *handle) {
    if (handle == port_module_info()->handle)
        return 0;
    errno = EINVAL;
    return -1;
}
static char *compat_dlerror(void) {
    return "Library unavailable in Vita runtime";
}
typedef struct {
    const char *name;
    void *base;
    const char *symbol;
    void *symbol_addr;
} DlInfo;
static int compat_dladdr(void *address, DlInfo *info) {
    uintptr_t instruction_address = (uintptr_t)address;
    if (instruction_address < port_module_info()->text_base ||
        instruction_address >= port_module_info()->text_base + port_module_info()->text_size)
        return 0;
    *info = (DlInfo){"libgame.so", (void *)port_module_info()->load_base, NULL, NULL};
    return 1;
}

static const PortImportEntry imports[] = {
    {"__gnu_Unwind_Find_exidx", (uintptr_t)(compat_exidx)},
    {"dlopen", (uintptr_t)(compat_dlopen)},
    {"dlsym", (uintptr_t)(compat_dlsym)},
    {"dlclose", (uintptr_t)(compat_dlclose)},
    {"dlerror", (uintptr_t)(compat_dlerror)},
    {"dladdr", (uintptr_t)(compat_dladdr)},
};
const PortImportTable port_linker_imports = {imports, sizeof(imports) / sizeof(imports[0])};
