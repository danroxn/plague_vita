#include "runtime/png_fast.h"

#include "runtime/native_memory.h"

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#include <limits.h>
#include <png.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    const uint8_t *input;
    size_t input_bytes;
    size_t offset;
    uint8_t *pixels;
} PngReadState;

static void PNGCBAPI png_failure(png_structp png, png_const_charp message) {
    (void)message;
    png_longjmp(png, 1);
}

static void PNGCBAPI png_warning_ignored(png_structp png, png_const_charp message) {
    (void)png;
    (void)message;
}

static png_voidp PNGCBAPI png_allocate(png_structp png, png_alloc_size_t bytes) {
    (void)png;
    return port_native_allocate((size_t)bytes, 0);
}

static void PNGCBAPI png_release(png_structp png, png_voidp memory) {
    (void)png;
    port_native_free(memory);
}

static void PNGCBAPI png_read_memory(png_structp png, png_bytep destination, size_t bytes) {
    PngReadState *state = png_get_io_ptr(png);
    if (bytes > state->input_bytes - state->offset)
        png_error(png, "Truncated PNG input");
    memcpy(destination, state->input + state->offset, bytes);
    state->offset += bytes;
}

static void premultiply_scalar(uint8_t *pixels, size_t pixel_count) {
    for (size_t i = 0; i < pixel_count; i++, pixels += 4) {
        unsigned alpha = pixels[3];
        if (alpha == 255)
            continue;
        pixels[0] = (unsigned)pixels[0] * alpha / 255;
        pixels[1] = (unsigned)pixels[1] * alpha / 255;
        pixels[2] = (unsigned)pixels[2] * alpha / 255;
    }
}

#ifdef __ARM_NEON
static inline __attribute__((always_inline))
uint8x8_t premultiply_eight(uint8x8_t color, uint8x8_t alpha) {
    uint16x8_t product = vmull_u8(color, alpha);
    // Exact floor(product / 255) for the complete 0..65025 product range.
    return vaddhn_u16(product, vsraq_n_u16(vdupq_n_u16(1), product, 8));
}

static inline __attribute__((always_inline))
uint8x16_t premultiply_channel(uint8x16_t color, uint8x16_t alpha) {
    return vcombine_u8(premultiply_eight(vget_low_u8(color), vget_low_u8(alpha)),
                       premultiply_eight(vget_high_u8(color), vget_high_u8(alpha)));
}
#endif

static void premultiply_pixels(uint8_t *pixels, size_t pixel_count) {
#ifdef __ARM_NEON
    while (pixel_count >= 16) {
        uint8x16x4_t rgba = vld4q_u8(pixels);
        // Constant lane indexes keep the deinterleaved vectors in registers;
        // an indexed channel loop makes GCC spill the full RGBA block.
        rgba.val[0] = premultiply_channel(rgba.val[0], rgba.val[3]);
        rgba.val[1] = premultiply_channel(rgba.val[1], rgba.val[3]);
        rgba.val[2] = premultiply_channel(rgba.val[2], rgba.val[3]);
        vst4q_u8(pixels, rgba);
        pixels += 16 * 4;
        pixel_count -= 16;
    }
#endif
    premultiply_scalar(pixels, pixel_count);
}

uint8_t *port_png_decode_rgba(const void *data, int length, int *width, int *height) {
    if (!width || !height)
        return NULL;
    *width = *height = 0;
    if (!data || length < 8 || png_sig_cmp(data, 0, 8))
        return NULL;

    // Keep all state changed after setjmp in allocated storage so error cleanup
    // does not inspect indeterminate automatic variables after a libpng jump.
    PngReadState *state = port_native_calloc(1, sizeof(*state));
    if (!state)
        return NULL;
    state->input = data;
    state->input_bytes = (size_t)length;
    png_structp png = png_create_read_struct_2(PNG_LIBPNG_VER_STRING, NULL, png_failure,
                                               png_warning_ignored, NULL, png_allocate,
                                               png_release);
    if (!png) {
        port_native_free(state);
        return NULL;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        port_native_free(state);
        return NULL;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        port_native_free(state->pixels);
        port_native_free(state);
        return NULL;
    }

    png_set_read_fn(png, state, png_read_memory);
    png_set_user_limits(png, INT_MAX, INT_MAX);
    png_read_info(png, info);
    png_uint_32 decoded_width = png_get_image_width(png, info);
    png_uint_32 decoded_height = png_get_image_height(png, info);
    int color_type = png_get_color_type(png, info);
    int bit_depth = png_get_bit_depth(png, info);
    if (!decoded_width || !decoded_height || decoded_width > INT_MAX ||
        decoded_height > INT_MAX || decoded_width > SIZE_MAX / 4 / decoded_height)
        png_error(png, "Unsupported PNG layout");

    // Match the original reader's sample expansion and optional file gamma;
    // avoid the simplified API's additional color-space conversions.
    int has_transparency = png_get_valid(png, info, PNG_INFO_tRNS) != 0;
    if (bit_depth == 16)
        png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE || bit_depth < 8 || has_transparency)
        png_set_expand(png);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);
    if (!(color_type & PNG_COLOR_MASK_ALPHA) && !has_transparency)
        png_set_add_alpha(png, 255, PNG_FILLER_AFTER);
    double file_gamma;
    if (png_get_gAMA(png, info, &file_gamma))
        png_set_gamma(png, 2.2, file_gamma);
    int passes = png_set_interlace_handling(png);
    png_read_update_info(png, info);
    size_t stride = (size_t)decoded_width * 4;
    size_t pixel_bytes = stride * decoded_height;
    if (png_get_bit_depth(png, info) != 8 ||
        png_get_color_type(png, info) != PNG_COLOR_TYPE_RGBA ||
        png_get_channels(png, info) != 4 ||
        png_get_rowbytes(png, info) != stride)
        png_error(png, "Unexpected PNG output layout");
    state->pixels = port_native_allocate(pixel_bytes, 0);
    if (!state->pixels)
        png_error(png, "PNG pixel allocation failed");
    if (passes > 1)
        memset(state->pixels, 0, pixel_bytes);
    for (int pass = 0; pass < passes; pass++)
        for (png_uint_32 row = 0; row < decoded_height; row++)
            png_read_row(png, state->pixels + (size_t)row * stride, NULL);
    png_read_end(png, NULL);
    png_destroy_read_struct(&png, &info, NULL);

    uint8_t *pixels = state->pixels;
    port_native_free(state);
    size_t pixel_count = (size_t)decoded_width * decoded_height;
    premultiply_pixels(pixels, pixel_count);
    *width = (int)decoded_width;
    *height = (int)decoded_height;
    return pixels;
}
