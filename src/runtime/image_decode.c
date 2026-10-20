#include "runtime/image_decode.h"
#include "runtime/png_fast.h"
#include "runtime/webp_fast.h"

#include "runtime/module.h"
#include "runtime/module_internal.h"

#include <stdint.h>

void port_patch_image_decoder(void) {
    uintptr_t address = port_module_require_symbol("PNGDecodeRGBA");
    hook_arm(address, (uintptr_t)port_png_decode_rgba);
    port_patch_webp_decoder();
}
