#ifndef PLAGUE_RUNTIME_PNG_FAST_H
#define PLAGUE_RUNTIME_PNG_FAST_H

#include <stdint.h>

// Decode PNGs with the native reader's RGB/RGBA eight-bit normalization into
// premultiplied RGBA, including the native reader's optional file-gamma handling.
// The caller owns the returned pixels and releases them with port_native_free.
// Failure returns NULL and clears both dimensions.
uint8_t *port_png_decode_rgba(const void *data, int length, int *width, int *height);

#endif
