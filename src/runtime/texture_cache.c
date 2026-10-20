#include "runtime/texture_cache.h"

#include "app/log.h"
#include "expected.h"
#include "runtime/paths.h"
#include "vita/time.h"

#include <errno.h>
#include <openssl/sha.h>
#include <png.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vitaGL.h>
#include <webp/decode.h>
#include <zlib.h>

#define XXH_INLINE_ALL
#include "utils/xxhash_utils.h"

#define TEXTURE_CACHE_DIRECTORY PORT_ROOT "cache/dxt5"
enum { CACHE_MAGIC = 0x35545844, CACHE_VERSION = 5, MAX_PAYLOAD_BYTES = 4 * 1024 * 1024 };
typedef struct {
    uint32_t magic, version, width, height, format, type, mips, bytes, compressed_bytes, crc;
    unsigned char key[SHA256_DIGEST_LENGTH];
} TextureCacheHeader;
_Static_assert(sizeof(TextureCacheHeader) == 72, "DXT5 cache header layout");

// Preserve VitaGL's GPU-pool order while requesting texture alignment. The
// public vglMemalign instead prefers Newlib memory over the GPU pools.
extern void *vgl_memalign(size_t alignment, size_t size, vglMemType type);
extern GLboolean use_extra_mem;
static struct {
    unsigned hits, misses, stores, failures;
    uint64_t hash_us, read_us, write_us;
    uint64_t compressed_read_bytes, unpacked_read_bytes, compressed_written_bytes;
} cache_profile;

static void *allocate_texture(size_t bytes) {
    static const vglMemType pools[] = {
        VGL_MEM_VRAM, VGL_MEM_RAM, VGL_MEM_PHYCONT, VGL_MEM_BUDGET, VGL_MEM_EXTERNAL};
    for (size_t i = 0; i < sizeof(pools) / sizeof(pools[0]); i++) {
        if (pools[i] == VGL_MEM_EXTERNAL && !use_extra_mem)
            break;
        void *pixels = vgl_memalign(SCE_GXM_TEXTURE_ALIGNMENT, bytes, pools[i]);
        if (pixels)
            return pixels;
    }
    return NULL;
}

static int initialize_header(TextureCacheHeader *header, int width, int height) {
    if (width != 2048 || (height != 1024 && height != 2048))
        return 0;
    *header = (TextureCacheHeader){.magic = CACHE_MAGIC,
                                  .version = CACHE_VERSION,
                                  .width = (uint32_t)width,
                                  .height = (uint32_t)height,
                                  .format = SCE_GXM_TEXTURE_FORMAT_UBC3_ABGR,
                                  .type = SCE_GXM_TEXTURE_SWIZZLED_ARBITRARY,
                                  .mips = 1,
                                  .bytes = (uint32_t)width * (uint32_t)height};
    if (header->bytes > MAX_PAYLOAD_BYTES)
        return 0;
    return 1;
}

static int make_header(TextureCacheHeader *header, int width, int height, const void *rgba) {
    if (!rgba || !initialize_header(header, width, height))
        return 0;
    static const char identity[] = "plague-dxt5-highqual-xxh3-v5:" PORT_VITAGL_REVISION;
    // Fingerprint the actual upload pixels, including any CPU conversions.
    // XXH3 avoids two slow bytewise scans of each large atlas.
    size_t source_bytes = (size_t)width * (size_t)height * 4;
    XXH128_hash_t digest = XXH3_128bits(rgba, source_bytes);
    SHA256_CTX hash;
    return SHA256_Init(&hash) &&
           SHA256_Update(&hash, identity, sizeof(identity)) &&
           SHA256_Update(&hash, header, offsetof(TextureCacheHeader, compressed_bytes)) &&
           SHA256_Update(&hash, &digest, sizeof(digest)) &&
           SHA256_Final(header->key, &hash);
}

static void cache_path(const TextureCacheHeader *header, char path[160]) {
    static int initialized;
    if (!initialized) {
        sceIoMkdir(PORT_ROOT "cache", 0777);
        sceIoMkdir(TEXTURE_CACHE_DIRECTORY, 0777);
        initialized = 1;
    }
    char hex[SHA256_DIGEST_LENGTH * 2 + 1];
    for (unsigned i = 0; i < SHA256_DIGEST_LENGTH; i++)
        snprintf(hex + i * 2, 3, "%02x", header->key[i]);
    snprintf(path, 160, "%s/%s.dxt", TEXTURE_CACHE_DIRECTORY, hex);
}

static void *load_texture(const char *path, const TextureCacheHeader *expected) {
    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;
    TextureCacheHeader stored;
    int valid = fread(&stored, sizeof(stored), 1, file) == 1 &&
                !memcmp(&stored, expected, offsetof(TextureCacheHeader, compressed_bytes)) &&
                stored.compressed_bytes > 0 &&
                stored.compressed_bytes <= compressBound(expected->bytes) &&
                !memcmp(stored.key, expected->key, sizeof(stored.key));
    // CDRAM and VitaGL's default RAM pools are uncached. Keep file reads and
    // the bytewise checksum in cached Newlib memory, then copy into the GPU
    // allocation once after validating the complete payload.
    void *staging = valid ? malloc(expected->bytes) : NULL;
    void *compressed = valid ? malloc(stored.compressed_bytes) : NULL;
    if (valid && (!staging || !compressed)) {
        free(compressed);
        free(staging);
        fclose(file);
        return NULL;
    }
    if (compressed) {
        uLongf unpacked_bytes = expected->bytes;
        valid = fread(compressed, 1, stored.compressed_bytes, file) == stored.compressed_bytes &&
                fgetc(file) == EOF && !ferror(file);
        valid = valid &&
                uncompress(staging, &unpacked_bytes, compressed, stored.compressed_bytes) == Z_OK &&
                unpacked_bytes == expected->bytes;
        valid = valid && crc32(0, staging, expected->bytes) == stored.crc;
    }
    if (fclose(file))
        valid = 0;
    free(compressed);
    void *pixels = valid ? allocate_texture(expected->bytes) : NULL;
    if (pixels)
        sceClibMemcpy(pixels, staging, expected->bytes);
    free(staging);
    if (!valid) {
        remove(path);
        return NULL;
    }
    if (pixels) {
        cache_profile.compressed_read_bytes += stored.compressed_bytes;
        cache_profile.unpacked_read_bytes += expected->bytes;
    }
    return pixels;
}

static int store_texture(const char *path, TextureCacheHeader header, const void *pixels) {
    // Read GPU memory once with a bulk copy rather than making both crc32 and
    // stdio scan the uncached texture allocation independently.
    void *staging = malloc(header.bytes);
    if (!staging)
        return 0;
    sceClibMemcpy(staging, pixels, header.bytes);
    uLongf compressed_bytes = compressBound(header.bytes);
    void *compressed = malloc(compressed_bytes);
    header.crc = crc32(0, staging, header.bytes);
    if (!compressed ||
        compress2(compressed, &compressed_bytes, staging, header.bytes, Z_BEST_SPEED) != Z_OK) {
        free(compressed);
        free(staging);
        return 0;
    }
    free(staging);
    header.compressed_bytes = (uint32_t)compressed_bytes;
    char temporary[168];
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    FILE *file = fopen(temporary, "wb");
    if (!file) {
        free(compressed);
        return 0;
    }
    int valid = fwrite(&header, sizeof(header), 1, file) == 1 &&
                fwrite(compressed, 1, header.compressed_bytes, file) == header.compressed_bytes;
    if (fclose(file))
        valid = 0;
    free(compressed);
    if (valid && rename(temporary, path) == 0) {
        cache_profile.compressed_written_bytes += header.compressed_bytes;
        return 1;
    }
    remove(temporary);
    return 0;
}

int port_texture_cache_upload(int width, int height, const void *rgba) {
    int saved_errno = errno;
    int profile = port_log_enabled(PORT_LOG_DEBUG);
    uint64_t started = profile ? port_vita_time_us() : 0;
    TextureCacheHeader header;
    int cacheable = make_header(&header, width, height, rgba);
    if (profile)
        cache_profile.hash_us += port_vita_time_us() - started;
    char path[160];
    if (cacheable) {
        cache_path(&header, path);
        started = profile ? port_vita_time_us() : 0;
        void *pixels = load_texture(path, &header);
        if (profile)
            cache_profile.read_us += port_vita_time_us() - started;
        if (pixels) {
            vglSetupTexture(GL_TEXTURE_2D, pixels, SCE_GXM_TEXTURE_FORMAT_UBC3_ABGR,
                            SCE_GXM_TEXTURE_SWIZZLED_ARBITRARY, width, height, 1);
            cache_profile.hits++;
            errno = saved_errno;
            return 1;
        }
    }
    cache_profile.misses++;
    errno = saved_errno;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    saved_errno = errno;
    const void *pixels = cacheable ? vglGetTexDataPointer(GL_TEXTURE_2D) : NULL;
    if (pixels) {
        started = profile ? port_vita_time_us() : 0;
        if (store_texture(path, header, pixels))
            cache_profile.stores++;
        else
            cache_profile.failures++;
        if (profile)
            cache_profile.write_us += port_vita_time_us() - started;
    }
    errno = saved_errno;
    return 0;
}

int port_texture_cache_encoded_key(PortTextureCacheKey *key, int width, int height,
                                   PortTextureEncoding encoding, const void *encoded, size_t bytes,
                                   const uint32_t *policy, size_t policy_words) {
    TextureCacheHeader header;
    if (!key || !encoded || !bytes || bytes > UINT32_MAX || !policy ||
        !policy_words || policy_words > 16 ||
        (encoding != PORT_TEXTURE_ENCODING_PNG && encoding != PORT_TEXTURE_ENCODING_LEGACY_WEBP) ||
        !initialize_header(&header, width, height))
        return 0;
    int saved_errno = errno;
    int profiling = port_log_enabled(PORT_LOG_DEBUG);
    uint64_t started = profiling ? port_vita_time_us() : 0;
    // V2 changes the full-input fingerprint; V1 files remain separate. These
    // namespaces also never alias each other or the actual-RGBA cache. Bump the
    // semantic version whenever decoding, alpha, normalization, or DXT changes.
    static const char png_identity[] = "plague-dxt5-highqual-encoded-png-pot-floorpremul-v2-xxh3-128:"
                                      PNG_LIBPNG_VER_STRING ":" EXPECTED_APK_XXH3_128 ":"
                                      PORT_VITAGL_REVISION;
    static const char webp_identity[] = "plague-dxt5-highqual-encoded-legacy-webp-vp8x12-pot-"
                                       "straight-rgba-nativealpha-v2-xxh3-128:"
                                       EXPECTED_APK_XXH3_128 ":" PORT_VITAGL_REVISION;
    const char *identity = encoding == PORT_TEXTURE_ENCODING_PNG ? png_identity : webp_identity;
    size_t identity_bytes = encoding == PORT_TEXTURE_ENCODING_PNG ? sizeof(png_identity)
                                                                 : sizeof(webp_identity);
    const uint32_t webp_versions[] = {WEBP_DECODER_ABI_VERSION, (uint32_t)WebPGetDecoderVersion()};
    uint32_t length = (uint32_t)bytes;
    uint32_t policy_count = (uint32_t)policy_words;
    // Same complete-content fingerprint as the RGBA cache, now over compressed
    // input. SHA256 handles only the namespace, native policy and 16-byte hash.
    XXH128_hash_t digest = XXH3_128bits(encoded, bytes);
    SHA256_CTX hash;
    int valid = SHA256_Init(&hash) &&
                SHA256_Update(&hash, identity, identity_bytes) &&
                SHA256_Update(&hash, &header, offsetof(TextureCacheHeader, compressed_bytes)) &&
                SHA256_Update(&hash, &policy_count, sizeof(policy_count)) &&
                SHA256_Update(&hash, policy, policy_words * sizeof(*policy)) &&
                SHA256_Update(&hash, &length, sizeof(length)) &&
                (encoding != PORT_TEXTURE_ENCODING_LEGACY_WEBP ||
                 SHA256_Update(&hash, webp_versions, sizeof(webp_versions))) &&
                SHA256_Update(&hash, &digest, sizeof(digest)) && SHA256_Final(key->digest, &hash);
    if (valid) {
        key->width = (uint32_t)width;
        key->height = (uint32_t)height;
    }
    if (profiling)
        cache_profile.hash_us += port_vita_time_us() - started;
    errno = saved_errno;
    return valid;
}

static int keyed_header(TextureCacheHeader *header, const PortTextureCacheKey *key) {
    if (!key || !initialize_header(header, (int)key->width, (int)key->height))
        return 0;
    memcpy(header->key, key->digest, sizeof(header->key));
    return 1;
}

void *port_texture_cache_load_key(const PortTextureCacheKey *key) {
    TextureCacheHeader header;
    if (!keyed_header(&header, key))
        return NULL;
    int saved_errno = errno;
    int profiling = port_log_enabled(PORT_LOG_DEBUG);
    uint64_t started = profiling ? port_vita_time_us() : 0;
    char path[160];
    cache_path(&header, path);
    void *pixels = load_texture(path, &header);
    if (profiling)
        cache_profile.read_us += port_vita_time_us() - started;
    if (pixels)
        cache_profile.hits++;
    else
        cache_profile.misses++;
    errno = saved_errno;
    return pixels;
}

int port_texture_cache_store_key(const PortTextureCacheKey *key, const void *gpu_pixels) {
    TextureCacheHeader header;
    if (!gpu_pixels || !keyed_header(&header, key))
        return 0;
    int saved_errno = errno;
    int profiling = port_log_enabled(PORT_LOG_DEBUG);
    uint64_t started = profiling ? port_vita_time_us() : 0;
    char path[160];
    cache_path(&header, path);
    int valid = store_texture(path, header, gpu_pixels);
    if (valid)
        cache_profile.stores++;
    else
        cache_profile.failures++;
    if (profiling)
        cache_profile.write_us += port_vita_time_us() - started;
    errno = saved_errno;
    return valid;
}

void port_texture_cache_adopt_key(const PortTextureCacheKey *key, void *gpu_pixels) {
    vglSetupTexture(GL_TEXTURE_2D, gpu_pixels, SCE_GXM_TEXTURE_FORMAT_UBC3_ABGR,
                    SCE_GXM_TEXTURE_SWIZZLED_ARBITRARY, key->width, key->height, 1);
}

void port_texture_cache_report(void) {
    port_log_debug("CACHE texture hits=%u misses=%u stores=%u failures=%u "
                   "hash_us=%llu read_us=%llu write_us=%llu "
                   "compressed_read_bytes=%llu unpacked_read_bytes=%llu compressed_written_bytes=%llu",
                   cache_profile.hits, cache_profile.misses, cache_profile.stores,
                   cache_profile.failures, (unsigned long long)cache_profile.hash_us,
                   (unsigned long long)cache_profile.read_us,
                   (unsigned long long)cache_profile.write_us,
                   (unsigned long long)cache_profile.compressed_read_bytes,
                   (unsigned long long)cache_profile.unpacked_read_bytes,
                   (unsigned long long)cache_profile.compressed_written_bytes);
}
