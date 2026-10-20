#ifndef PLAGUE_RUNTIME_TEXTURE_CACHE_H
#define PLAGUE_RUNTIME_TEXTURE_CACHE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t width, height;
    unsigned char digest[32];
} PortTextureCacheKey;

typedef enum {
    PORT_TEXTURE_ENCODING_PNG = 1,
    PORT_TEXTURE_ENCODING_LEGACY_WEBP = 2,
} PortTextureEncoding;

// Valid only inside the verified PNG/legacy-WebP POT native factory. The entire
// compressed input is fingerprinted with XXH3_128 before the small SHA256 key.
int port_texture_cache_encoded_key(PortTextureCacheKey *key, int width, int height,
                                   PortTextureEncoding encoding, const void *encoded, size_t bytes,
                                   const uint32_t *policy, size_t policy_words);
// Returned GPU memory belongs to the caller until adopted or vglFree'd.
void *port_texture_cache_load_key(const PortTextureCacheKey *key);
int port_texture_cache_store_key(const PortTextureCacheKey *key, const void *gpu_pixels);
// Transfer the GPU allocation to the currently bound GL_TEXTURE_2D.
void port_texture_cache_adopt_key(const PortTextureCacheKey *key, void *gpu_pixels);

// Upload an already validated 2048x1024/2048 RGBA8 Retina texture as DXT5.
// Returns 1 when cached GPU data was used, otherwise performs the normal upload
// and returns 0. The currently bound GL_TEXTURE_2D receives the texture.
int port_texture_cache_upload(int width, int height, const void *rgba);
void port_texture_cache_report(void);

#endif
