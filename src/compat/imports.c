#include "compat/imports.h"

#include "compat/import_table.h"

#include <string.h>

static const PortImportTable *const import_tables[] = {
    &port_libc_imports,    &port_filesystem_imports, &port_mappings_imports, &port_linker_imports,
    &port_network_imports, &port_graphics_imports,   &port_threads_imports,  &port_assets_imports,
    &port_float_imports,   &port_sdk_imports,
};
uintptr_t port_import(const char *name) {
    for (size_t table_index = 0; table_index < sizeof(import_tables) / sizeof(import_tables[0]);
         table_index++) {
        const PortImportTable *table = import_tables[table_index];
        for (size_t entry_index = 0; entry_index < table->count; entry_index++) {
            const PortImportEntry *entry = &table->entries[entry_index];
            if (!strcmp(name, entry->symbol) && entry->address)
                return entry->address;
        }
    }
    return 0;
}
