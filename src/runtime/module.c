#include "runtime/module.h"

#include "expected.h"
#include "app/log.h"
#include "compat/imports.h"
#include "runtime/loader_integration.h"
#include "runtime/module_internal.h"
#include "runtime/resource_archive.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

_Static_assert(PORT_LIBRARY_BYTES > 0 && PORT_LIBRARY_BYTES <= UINT32_MAX - 0xfff,
               "Native library staging size");

static so_module game_module;
static Elf32_Rel *relocation_at(uint32_t index) {
    if (index < game_module.num_reldyn)
        return &game_module.reldyn[index];
    return &game_module.relplt[index - game_module.num_reldyn];
}
const so_module *port_module_image(void) { return &game_module; }
static PortModuleInfo module_info;
const PortModuleInfo *port_module_info(void) { return &module_info; }
uintptr_t port_module_find_symbol(const char *name) { return so_symbol(&game_module, name); }
uintptr_t port_module_require_symbol(const char *name) {
    uintptr_t address = port_module_find_symbol(name);
    if (!address)
        port_fail("Missing native export: %s", name);
    return address;
}
int port_module_load(void) {
    // Inflate directly into the loader's one temporary allocation. The loader
    // consumes it after copying the ELF segments, including on load failure.
    SceUID stage = port_loader_stage_alloc("APK library", (PORT_LIBRARY_BYTES + 0xfff) & ~0xfffU);
    if (stage < 0)
        port_fail("Cannot allocate native library staging: 0x%08x", (unsigned)stage);
    void *data = port_loader_stage_buffer(stage);
    if (!data) {
        port_loader_stage_free(stage);
        port_fail("Cannot access native library staging");
    }
    size_t bytes = 0;
    if (port_resource_archive_load_library_into(data, PORT_LIBRARY_BYTES, &bytes)) {
        int error = errno;
        port_loader_stage_free(stage);
        port_log_error("Cannot read native library from package.apk: %s", strerror(error));
        return -1;
    }
    int result = port_loader_load_stage(&game_module, stage, data, 0x98000000);
    if (result < 0)
        port_fail("Native library loader failed 0x%08x", (unsigned)result);
    module_info = (PortModuleInfo){
        .handle = &game_module,
        .load_base = game_module.load_addr,
        .text_base = game_module.text_base,
        .text_size = game_module.text_size,
        .exidx_base = game_module.exidx_base,
        .exidx_count = game_module.exidx_size / 8,
    };
    return 0;
}
void port_module_bind(void) {
    int binding_count = 0, missing = 0;
    for (uint32_t i = 0; i < game_module.num_dynsym; i++) {
        Elf32_Sym *symbol = &game_module.dynsym[i];
        if (symbol->st_shndx != SHN_UNDEF || !symbol->st_name)
            continue;
        binding_count++;
        const char *name = game_module.dynstr + symbol->st_name;
        if (!port_import(name)) {
            port_log_error("UNRESOLVED IMPORT: %s", name);
            missing++;
        }
    }
    if (missing)
        port_fail("%d required imports missing; constructors were not executed", missing);
    so_relocate(&game_module);
    // Preserve ABS32 addends (notably __sF+84/+168).
    for (uint32_t i = 0; i < game_module.num_reldyn + game_module.num_relplt; i++) {
        Elf32_Rel *relocation = relocation_at(i);
        Elf32_Sym *symbol = &game_module.dynsym[ELF32_R_SYM(relocation->r_info)];
        unsigned type = ELF32_R_TYPE(relocation->r_info);
        if (symbol->st_shndx != SHN_UNDEF || !symbol->st_name)
            continue;
        if (type != R_ARM_ABS32 && type != R_ARM_GLOB_DAT && type != R_ARM_JUMP_SLOT)
            continue;
        uintptr_t address = port_import(game_module.dynstr + symbol->st_name);
        uintptr_t *target = (uintptr_t *)(game_module.load_addr + relocation->r_offset);
        if (type == R_ARM_ABS32)
            *target += address;
        else
            *target = address;
    }
    port_log_debug("All %d imports resolved", binding_count);
}
void port_module_initialize(void) {
    for (uint32_t i = 0; i < game_module.num_init_array; i++) {
        if (game_module.init_array[i])
            game_module.init_array[i]();
    }
}
void port_module_flush(void) { so_flush_caches(&game_module); }
