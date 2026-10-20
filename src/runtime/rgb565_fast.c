#include "runtime/rgb565_fast.h"

#include "app/log.h"
#include "runtime/module_internal.h"

#include <arm_neon.h>
#include <kubridge.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uintptr_t original_loop, converted_loop;

static void convert_pixels(const uint8_t *source, uint16_t *destination, size_t count) {
    while (count >= 8) {
        uint8x8x4_t rgba = vld4_u8(source);
        uint16x8_t rgb = vshll_n_u8(rgba.val[0], 8);
        rgb = vsriq_n_u16(rgb, vshll_n_u8(rgba.val[1], 8), 5);
        rgb = vsriq_n_u16(rgb, vshll_n_u8(rgba.val[2], 8), 11);
        vst1q_u16(destination, rgb);
        source += 8 * 4;
        destination += 8;
        count -= 8;
    }
    while (count--) {
        *destination++ = ((source[0] & 0xf8u) << 8) | ((source[1] & 0xfcu) << 3) |
                         (source[2] >> 3);
        source += 4;
    }
}

static uintptr_t __attribute__((used, noinline, pcs("aapcs")))
convert_loop_pixels(const uint8_t *source, uint16_t *destination, uint32_t count_bits) {
    // Only the shipped 2048-square map is optimized. Other counts retain the
    // original float-counter loop, including its rounding for huge images.
    if (!source || !destination || count_bits != UINT32_C(0x4a800000))
        return original_loop;
    convert_pixels(source, destination, 2048u * 2048u);
    return converted_loop;
}

// This is a mid-function ARM branch, not an AAPCS entry. Preserve its volatile
// context as well as the native r12=r6 temporary. The reserved final stack word
// carries the resume PC, allowing every original GPR and LR to be restored.
// d8-d15 (including count s16) remain callee-saved; save d0-d7 and FPSCR here.
static void __attribute__((naked, target("arm"))) convert_loop(void) {
    __asm__(
        "sub sp, sp, #4\n"
        "push {r0-r4, r12, lr}\n"
        "vpush {d0-d7}\n"
        "vmrs r3, fpscr\n"
        "mrs r4, APSR\n"
        "push {r3, r4}\n"
        "mov r1, r9\n"
        "vmov r2, s16\n"
        "bl convert_loop_pixels\n"
        "mov r3, r0\n"
        "pop {r1, r2}\n"
        "vmsr fpscr, r1\n"
        "msr APSR_nzcvq, r2\n"
        "vpop {d0-d7}\n"
        "str r3, [sp, #28]\n"
        "pop {r0-r4, r12, lr}\n"
        "ldr pc, [sp], #4\n");
}

void port_patch_rgb565_conversion(void) {
    so_module *module = (so_module *)port_module_image();
    const uintptr_t entry = module->load_addr + 0x697e14;
    const size_t trampoline_bytes = 4 * sizeof(uint32_t);
    if (module->patch_head < module->patch_base ||
        module->patch_head - module->patch_base > module->patch_size ||
        trampoline_bytes > module->patch_size - (module->patch_head - module->patch_base)) {
        port_log_debug("Map RGB565 conversion retains the original native loop");
        return;
    }
    // Both relocated MOVs are PC-independent. The fallback resumes before the
    // unchanged mask setup; success resumes at format=3, r6=r12 and free(src).
    uint32_t *trampoline = (uint32_t *)module->patch_head;
    memcpy(trampoline, (const void *)entry, 2 * sizeof(uint32_t));
    trampoline[2] = 0xe51ff004;
    trampoline[3] = entry + 2 * sizeof(uint32_t);
    kuKernelFlushCaches(trampoline, trampoline_bytes);
    module->patch_head += trampoline_bytes;
    original_loop = (uintptr_t)trampoline;
    converted_loop = module->load_addr + 0x697e58;
    hook_arm(entry, (uintptr_t)convert_loop);
    kuKernelFlushCaches((const void *)entry, 2 * sizeof(uint32_t));
    port_log_debug("Map RGB565 conversion uses exact eight-pixel NEON packing");
}
