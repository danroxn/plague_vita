#include "runtime/crypt_read.h"

#include "app/log.h"
#include "runtime/module_internal.h"

#include <kubridge.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef uint32_t (*StreamRead)(uintptr_t, void *, uint32_t);
_Static_assert(sizeof(uintptr_t) == 4, "CryptFile requires the native ARM word ABI");
static StreamRead original_read;
static void (*native_error)(const char *, const char *, ...);
static const uint8_t *stream_key;
static uint32_t fast_read(uintptr_t self, void *destination, uint32_t bytes) {
    // Packaged fields currently fit in 391 bytes. A bounded local buffer also
    // preserves native semantics on short reads: consume once, leave dest intact.
    uint8_t scratch[512];
    if (!bytes)
        return 0;
    if (bytes > sizeof(scratch))
        return original_read(self, destination, bytes);
    uintptr_t source = *(const uintptr_t *)(self + 4);
    uintptr_t vtable = *(const uintptr_t *)source;
    StreamRead read = *(const StreamRead *)(vtable + 4);
    uint32_t count = read(source, scratch, bytes);
    if (count != bytes) {
        native_error("Read", "Unexpected error %d while reading cryptfile!", (int)count);
        return 0;
    }
    uint8_t *output = destination;
    for (uint32_t i = 0; i < bytes; i++)
        output[i] = scratch[i] ^ stream_key[(bytes + i) & 31];
    return bytes;
}

static uintptr_t original_trampoline(so_module *module, uintptr_t entry) {
    uint32_t *trampoline = (uint32_t *)module->patch_head;
    // The entry starts with PC-independent PUSH/ADD-FP.
    memcpy(trampoline, (const void *)entry, 2 * sizeof(uint32_t));
    trampoline[2] = 0xe51ff004;
    trampoline[3] = entry + 2 * sizeof(uint32_t);
    kuKernelFlushCaches(trampoline, 4 * sizeof(uint32_t));
    module->patch_head += 4 * sizeof(uint32_t);
    return (uintptr_t)trampoline;
}

void port_patch_crypt_read(void) {
    so_module *module = (so_module *)port_module_image();
    uintptr_t read_entry = module->load_addr + 0x692484;
    const size_t trampoline_bytes = 4 * sizeof(uint32_t);
    if (module->patch_head < module->patch_base ||
        module->patch_head - module->patch_base > module->patch_size ||
        trampoline_bytes > module->patch_size - (module->patch_head - module->patch_base))
        port_fail("No native patch space for crypt resource reader");
    original_read = (StreamRead)original_trampoline(module, read_entry);
    native_error = (void (*)(const char *, const char *, ...))(module->load_addr + 0x42aaf4);
    stream_key = (const uint8_t *)(module->load_addr + 0xccf433);
    hook_arm(read_entry, (uintptr_t)fast_read);
    port_log_debug("Crypt resource reader uses bounded stack storage");
}
