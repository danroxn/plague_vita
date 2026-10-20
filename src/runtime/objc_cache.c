#include "runtime/objc_cache.h"

#include "app/log.h"
#include "runtime/module_internal.h"

#include <kubridge.h>
#include <stddef.h>
#include <stdint.h>

enum { NATIVE_CACHE_ENTRIES = 100, INDEX_HINTS = 8192 };
typedef struct {
    uint32_t count;
    uintptr_t selectors[NATIVE_CACHE_ENTRIES];
    uintptr_t implementations[NATIVE_CACHE_ENTRIES];
} NativeMethodCache;
_Static_assert(sizeof(uintptr_t) == 4 && sizeof(NativeMethodCache) == 804 &&
               offsetof(NativeMethodCache, implementations) == 404,
               "Native Objective-C method-cache layout");
typedef uintptr_t (*ClassLookup)(uintptr_t native_class, const uintptr_t *selector);
static ClassLookup original_lookup;
static uint8_t index_hints[INDEX_HINTS];

static volatile NativeMethodCache *method_cache(uintptr_t native_class) {
    return *(volatile NativeMethodCache *volatile *)(native_class + 32);
}

static uintptr_t lookup_class_method(uintptr_t native_class, const uintptr_t *selector) {
    // objc_msg_lookup at 0x6f5ff4 passes object->isa, while the super helper at
    // 0x6f61e8 passes class->superclass. Both tail-call this same class lookup.
    // Selector objects hold their interned name pointer in the first word;
    // the original cache compares that word, rather than selector addresses.
    if (!native_class || !selector)
        return original_lookup(native_class, selector);
    uintptr_t selector_id = *selector;
    uint32_t hash = (native_class >> 3) ^ selector_id;
    hash ^= hash >> 11;
    hash ^= hash >> 19;
    uint8_t *hint = &index_hints[hash & (INDEX_HINTS - 1)];
    unsigned index = __atomic_load_n(hint, __ATOMIC_RELAXED);
    volatile NativeMethodCache *native_cache = method_cache(native_class);
    if (native_cache) {
        uint32_t count = native_cache->count;
        if (count <= NATIVE_CACHE_ENTRIES && index < count &&
            native_cache->selectors[index] == selector_id) {
            // Never retain an IMP: re-read the current native entry. Hash
            // collisions and concurrent hint updates merely cause misses.
            uintptr_t implementation = native_cache->implementations[index];
            return implementation;
        }
    }
    uintptr_t implementation = original_lookup(native_class, selector);
    native_cache = method_cache(native_class);
    if (native_cache) {
        uint32_t count = native_cache->count;
        if (count <= NATIVE_CACHE_ENTRIES) {
            for (unsigned i = 0; i < count; i++) {
                if (native_cache->selectors[i] == selector_id) {
                    __atomic_store_n(hint, (uint8_t)i, __ATOMIC_RELAXED);
                    break;
                }
            }
        }
    }
    return implementation;
}

void port_patch_objc_cache(void) {
    so_module *module = (so_module *)port_module_image();
    uintptr_t entry = module->load_addr + 0x6f6054;
    const uint32_t prologue[] = {0xe92d4bf0, 0xe28db018};
    // Preserve the existing lookup on misses, including superclass traversal
    // and forwarding. Its two relocated instructions contain no PC references.
    const size_t trampoline_bytes = 4 * sizeof(uint32_t);
    if (module->patch_head < module->patch_base ||
        module->patch_head - module->patch_base > module->patch_size ||
        trampoline_bytes > module->patch_size - (module->patch_head - module->patch_base))
        port_fail("No native patch space for Objective-C lookup");
    uint32_t *trampoline = (uint32_t *)module->patch_head;
    trampoline[0] = prologue[0];
    trampoline[1] = prologue[1];
    trampoline[2] = 0xe51ff004; // LDR PC, [PC, #-4]
    trampoline[3] = entry + sizeof(prologue);
    kuKernelFlushCaches(trampoline, trampoline_bytes);
    module->patch_head += trampoline_bytes;
    original_lookup = (ClassLookup)trampoline;
    hook_arm(entry, (uintptr_t)lookup_class_method);
    port_log_debug("Objective-C method-cache index hints enabled: %u entries", INDEX_HINTS);
}
