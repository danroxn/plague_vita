#ifndef PLAGUE_RUNTIME_ENCODED_TEXTURE_CACHE_H
#define PLAGUE_RUNTIME_ENCODED_TEXTURE_CACHE_H

// Cache verified PNG and legacy VP8X12 WebP atlases with native POT dimensions.
// Install before native constructors.
void port_patch_encoded_texture_cache(void);
void port_encoded_texture_cache_report(void);

#endif
