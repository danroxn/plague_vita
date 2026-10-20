#include "runtime/webp_fast.h"

#include "app/log.h"
#include "runtime/module_internal.h"
#include "runtime/native_memory.h"
#include "vita/time.h"

#include <kubridge.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <webp/decode.h>


typedef uint8_t *(*NativeWebPDecode)(const uint8_t *, size_t, int *, int *);
typedef int (*NativeAlphaDecode)(const uint8_t *, uint32_t, int, int, int, uint8_t *);
static NativeWebPDecode original_decode;
static NativeAlphaDecode decode_legacy_alpha;

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

typedef struct {
    const uint8_t *rgb, *alpha;
    size_t rgb_bytes;
    uint32_t alpha_bytes, width, height;
} LegacyWebP;

// Shipped images have both an obsolete 12-byte VP8X container and an obsolete
// TCodec ALPH stream. Modern libwebp can decode the unchanged raw VP8 RGB
// payload; retain the small native alpha decoder and interleave its exact bytes.
static int parse_legacy(const uint8_t *input, size_t bytes, LegacyWebP *image) {
    if (bytes < 32 || bytes > UINT32_MAX || memcmp(input, "RIFF", 4) ||
        memcmp(input + 8, "WEBPVP8X", 8) || read_le32(input + 16) != 12 ||
        read_le32(input + 4) != bytes - 8)
        return 0;
    uint32_t flags = read_le32(input + 20);
    uint32_t width = read_le32(input + 24), height = read_le32(input + 28);
    // Static RGB/RGBA only; unknown feature and animation flags keep native.
    if ((flags & ~0x10u) || !width || width > INT_MAX || !height || height > INT_MAX)
        return 0;
    memset(image, 0, sizeof(*image));
    image->width = width;
    image->height = height;
    size_t offset = 32;
    while (offset < bytes) {
        if (bytes - offset < 8)
            return 0;
        uint32_t chunk_bytes = read_le32(input + offset + 4);
        if (chunk_bytes > bytes - offset - 8 ||
            (chunk_bytes & 1u) > bytes - offset - 8 - chunk_bytes)
            return 0;
        const uint8_t *chunk = input + offset;
        if (!memcmp(chunk, "VP8 ", 4)) {
            if (image->rgb)
                return 0;
            image->rgb = chunk + 8;
            image->rgb_bytes = chunk_bytes;
        } else if (!memcmp(chunk, "ALPH", 4)) {
            if (image->alpha)
                return 0;
            image->alpha = chunk + 8;
            image->alpha_bytes = chunk_bytes;
        } else {
            return 0;
        }
        offset += 8 + chunk_bytes + (chunk_bytes & 1u);
    }
    return image->rgb && image->rgb_bytes &&
           (!!(flags & 0x10u) == !!image->alpha) && (!image->alpha || image->alpha_bytes >= 3);
}

static uint8_t *decode_rgba(const uint8_t *data, size_t bytes, int *width, int *height) {
    int profiling = port_log_enabled(PORT_LOG_DEBUG);
    uint64_t started = profiling ? port_vita_time_us() : 0;
    if (!data || bytes < 12)
        return original_decode(data, bytes, width, height);
    LegacyWebP legacy_image;
    int legacy = parse_legacy(data, bytes, &legacy_image);
    size_t input_bytes = legacy ? legacy_image.rgb_bytes : bytes;
    const uint8_t *input = legacy ? legacy_image.rgb : data;
    int decoded_width = 0, decoded_height = 0;
    if (!WebPGetInfo(input, input_bytes, &decoded_width, &decoded_height) ||
        decoded_width <= 0 || decoded_height <= 0 || decoded_width > INT_MAX / 4 ||
        (size_t)decoded_width > SIZE_MAX / 4 / (size_t)decoded_height ||
        (legacy && ((unsigned)decoded_width != legacy_image.width ||
                    (unsigned)decoded_height != legacy_image.height))) {
        return original_decode(data, bytes, width, height);
    }
    size_t pixel_bytes = (size_t)decoded_width * decoded_height * 4;
    uint8_t *pixels = port_native_malloc(pixel_bytes);
    if (!pixels || !WebPDecodeRGBAInto(input, input_bytes, pixels, pixel_bytes, decoded_width * 4)) {
        port_native_free(pixels);
        return original_decode(data, bytes, width, height);
    }
    if (legacy && legacy_image.alpha) {
        size_t pixel_count = pixel_bytes / 4;
        uint8_t *alpha = port_native_malloc(pixel_count);
        if (!alpha || !decode_legacy_alpha(legacy_image.alpha, legacy_image.alpha_bytes,
                                           decoded_width, decoded_height, decoded_width, alpha)) {
            port_native_free(alpha);
            port_native_free(pixels);
            return original_decode(data, bytes, width, height);
        }
        for (size_t i = 0; i < pixel_count; i++)
            pixels[i * 4 + 3] = alpha[i];
        port_native_free(alpha);
    }
    uint64_t decoded_at = profiling ? port_vita_time_us() : 0;
    if (profiling)
        port_log_debug("LOAD webp input=%u output=%dx%d legacy=%d sdk_us=%llu",
                       (unsigned)bytes, decoded_width, decoded_height, legacy,
                       (unsigned long long)(decoded_at - started));
    if (width)
        *width = decoded_width;
    if (height)
        *height = decoded_height;
    return pixels;
}

void port_patch_webp_decoder(void) {
    so_module *module = (so_module *)port_module_image();
    // The atlas loader uses this second embedded WebP copy directly. Its
    // MODE_RGBA wrapper has the same public ABI as WebPDecodeRGBA.
    uintptr_t entry = module->load_addr + 0xac7210;
    const size_t displaced_bytes = 2 * sizeof(uint32_t);
    const size_t trampoline_bytes = 4 * sizeof(uint32_t);
    if (module->patch_head < module->patch_base ||
        module->patch_head - module->patch_base > module->patch_size ||
        trampoline_bytes > module->patch_size - (module->patch_head - module->patch_base))
        port_fail("No native patch space for WebP RGBA decoder");
    uint32_t *trampoline = (uint32_t *)module->patch_head;
    memcpy(trampoline, (void *)entry, displaced_bytes);
    trampoline[2] = 0xe51ff004;
    trampoline[3] = entry + displaced_bytes;
    kuKernelFlushCaches(trampoline, trampoline_bytes);
    module->patch_head += trampoline_bytes;
    original_decode = (NativeWebPDecode)trampoline;
    decode_legacy_alpha = (NativeAlphaDecode)(module->load_addr + 0xada304);
    hook_arm(entry, (uintptr_t)decode_rgba);
    port_log_debug("Atlas WebP RGBA decoder uses SDK libwebp with legacy container support");
}
