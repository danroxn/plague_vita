#include "runtime/patches.h"

#include "runtime/module_internal.h"

#include <stdint.h>

void port_patch_language(void) {
    const uintptr_t base = port_module_image()->load_addr;
    uint32_t *getter = (void *)(base + 0x584ccc);
    *getter = 0xe5902010;
}
