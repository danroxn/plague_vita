#ifndef PLAGUE_COMPAT_IMPORT_TABLE_H
#define PLAGUE_COMPAT_IMPORT_TABLE_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    const char *symbol;
    uintptr_t address;
} PortImportEntry;
typedef struct {
    const PortImportEntry *entries;
    size_t count;
} PortImportTable;
extern const PortImportTable port_libc_imports;
extern const PortImportTable port_filesystem_imports;
extern const PortImportTable port_mappings_imports;
extern const PortImportTable port_linker_imports;
extern const PortImportTable port_network_imports;
extern const PortImportTable port_graphics_imports;
extern const PortImportTable port_threads_imports;
extern const PortImportTable port_assets_imports;
extern const PortImportTable port_float_imports;
extern const PortImportTable port_sdk_imports;
#endif
