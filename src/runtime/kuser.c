#include "runtime/patches.h"

#include "app/log.h"
#include "runtime/module.h"
#include "runtime/module_internal.h"

#include <stddef.h>
#include <stdint.h>

__attribute__((naked, target("arm"))) static int
kernel_compare_exchange(int32_t old_value __attribute__((unused)),
                        int32_t new_value __attribute__((unused)),
                        volatile int32_t *address __attribute__((unused))) {
    __asm__ volatile("dmb ish\n"
                     "1: ldrex r3, [r2]\n"
                     "cmp r3, r0\n"
                     "bne 2f\n"
                     "strex ip, r1, [r2]\n"
                     "cmp ip, #0\n"
                     "bne 1b\n"
                     "mov r0, #0\n"
                     "b 3f\n"
                     "2: clrex\n"
                     "mov r0, #1\n"
                     "3: dmb ish\n"
                     "rsbs ip, r0, #0\n"
                     "bx lr\n");
}
__attribute__((naked, target("arm"))) static void kernel_memory_barrier(void) {
    __asm__ volatile("dmb ish\nbx lr\n");
}

static const struct {
    uint32_t compare_exchange_address;
    uint32_t barrier_address;
} kernel_helpers = {0xffff0fc0U, 0xffff0fa0U};
void port_patch_kuser(void) {
    unsigned compare_exchange_count = 0, barriers = 0;
    uint32_t *words = (uint32_t *)port_module_image()->text_base;
    for (size_t i = 0; i < port_module_image()->text_size / sizeof(*words); i++) {
        if (words[i] == kernel_helpers.compare_exchange_address) {
            words[i] = (uintptr_t)kernel_compare_exchange;
            compare_exchange_count++;
        } else if (words[i] == kernel_helpers.barrier_address) {
            words[i] = (uintptr_t)kernel_memory_barrier;
            barriers++;
        }
    }
    port_log_debug("Linux ARM helpers redirected: cmpxchg=%u barrier=%u", compare_exchange_count,
                   barriers);
}
