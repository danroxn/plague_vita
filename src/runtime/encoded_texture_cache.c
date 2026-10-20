#include "runtime/encoded_texture_cache.h"

#include "app/log.h"
#include "app/options.h"
#include "runtime/module.h"
#include "runtime/module_internal.h"
#include "runtime/texture_cache.h"

#include <errno.h>
#include <kubridge.h>
#include <psp2/kernel/threadmgr/thread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <vitaGL.h>

// Android methods use the base ARM PCS. Float arguments are passed as words,
// including textureScale and both contentSize components (no VFP arguments).
typedef uintptr_t (*EncodedInit)(uintptr_t, uintptr_t, uintptr_t, uint32_t, uint32_t)
    __attribute__((pcs("aapcs")));
typedef uintptr_t (*DataInit)(uintptr_t, uintptr_t, const void *, uint32_t,
                              uint32_t, uint32_t, uint32_t, uint32_t)
    __attribute__((pcs("aapcs")));
typedef void (*TextureUpload)(uint32_t, int32_t, int32_t, int32_t, int32_t,
                              int32_t, uint32_t, uint32_t, const void *)
    __attribute__((pcs("aapcs")));
typedef uintptr_t (*MethodLookup)(uintptr_t, uintptr_t);
typedef uintptr_t (*ClassLookup)(const char *);
typedef uintptr_t (*ObjectCall)(uintptr_t, uintptr_t);

typedef struct {
    uint32_t native_class, name, content_width, content_height;
    uint32_t width, height, format, max_s, max_t;
    uint8_t premultiplied, padding[3];
    uint32_t resolution;
} NativeTexture;
_Static_assert(sizeof(uintptr_t) == 4 && sizeof(NativeTexture) == 44 &&
               offsetof(NativeTexture, premultiplied) == 36 &&
               offsetof(NativeTexture, resolution) == 40,
               "Native CCTexture2D word layout");

typedef struct UploadScope {
    struct UploadScope *previous;
    const PortTextureCacheKey *key;
    void *pending_gpu;
    const void *uploaded_gpu;
    unsigned uploads;
    int warm;
} UploadScope;
static _Thread_local UploadScope *upload_scope;
static EncodedInit original_encoded;
static DataInit initialize_data;
static TextureUpload original_upload;
static MethodLookup lookup_method;
static ClassLookup lookup_class;
static int (*get_preserved_context)(void);
static uintptr_t upload_return;
static uintptr_t length_selector, bytes_selector, shared_selector, maximum_selector;
static uintptr_t data_selector;
static SceUID owner_thread;
static struct {
    unsigned calls, eligible, hits, misses, stores, bypasses;
} profile;

static uintptr_t send_object(uintptr_t object, uintptr_t selector) {
    return ((ObjectCall)lookup_method(object, selector))(object, selector);
}

static uint32_t float_bits(uint32_t value) {
    float converted = (float)value;
    uint32_t bits;
    memcpy(&bits, &converted, sizeof(bits));
    return bits;
}

static int metadata_valid(const NativeTexture *texture, const PortTextureCacheKey *key,
                           uint32_t resolution) {
    return texture && texture->name && texture->width == key->width &&
           texture->height == key->height && texture->format == 1 &&
           texture->content_width == float_bits(key->width) &&
           texture->content_height == float_bits(key->height) &&
           texture->max_s == 0x3f800000 && texture->max_t == 0x3f800000 &&
           texture->premultiplied == 1 && texture->resolution == resolution;
}

static uint32_t read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static uint32_t read_le32(const uint8_t *bytes) {
    return bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static int make_key(PortTextureCacheKey *key, uintptr_t self, uintptr_t data,
                     uint32_t resolution, uint32_t scale) {
    static const uint8_t png_signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (!self || !data || !port_app_options.retina_enabled ||
        get_preserved_context() != 1 ||
        *(const uintptr_t *)self != lookup_class("CCTexture2D") ||
        lookup_method(self, data_selector) != (uintptr_t)initialize_data)
        return 0;
    uint32_t bytes = send_object(data, length_selector);
    const uint8_t *encoded = (const uint8_t *)send_object(data, bytes_selector);
    if (!encoded || bytes < 32 || bytes > 16 * 1024 * 1024)
        return 0;
    uint32_t width, height;
    PortTextureEncoding encoding;
    if (bytes >= 33 && !memcmp(encoded, png_signature, sizeof(png_signature)) &&
        read_be32(encoded + 8) == 13 && !memcmp(encoded + 12, "IHDR", 4)) {
        encoding = PORT_TEXTURE_ENCODING_PNG;
        width = read_be32(encoded + 16);
        height = read_be32(encoded + 20);
    } else if (!memcmp(encoded, "RIFF", 4) && !memcmp(encoded + 8, "WEBPVP8X", 8) &&
               read_le32(encoded + 4) == bytes - 8 && read_le32(encoded + 16) == 12 &&
               !(read_le32(encoded + 20) & ~0x10u)) {
        // Shipped static legacy containers store actual dimensions, rather
        // than the modern VP8X 24-bit size-minus-one fields. Unknown flags and
        // containers retain the original decoder. A new key is persisted only
        // after that decoder's real upload and metadata have both succeeded.
        encoding = PORT_TEXTURE_ENCODING_LEGACY_WEBP;
        width = read_le32(encoded + 24);
        height = read_le32(encoded + 28);
    } else {
        return 0;
    }
    if (width != 2048 || (height != 1024 && height != 2048))
        return 0;
    uintptr_t configuration_class = lookup_class("CCConfiguration");
    uintptr_t configuration = configuration_class ? send_object(configuration_class, shared_selector) : 0;
    if (!configuration)
        return 0;
    uint32_t maximum = send_object(configuration, maximum_selector);
    if (maximum < width || maximum < height)
        return 0;
    // Factory0x6c6850 creates exactly {layout=0, maxTextureSize, flags=0}.
    // For both formats/POT, normalization0x6d7964 returns without touching RGBA.
    // Native6d809c's WebP branch and its generic PNG reader both report premul=1;
    // WebP's actual straight-RGBA bytes are preserved without an extra multiply.
    const uint32_t policy[] = {0, maximum, 0, resolution, scale, 1};
    return port_texture_cache_encoded_key(key, width, height, encoding, encoded, bytes,
                                          policy, sizeof(policy) / sizeof(policy[0]));
}

static void __attribute__((pcs("aapcs")))
upload_texture(uint32_t target, int32_t level, int32_t internal, int32_t width,
                int32_t height, int32_t border, uint32_t format, uint32_t type,
                const void *pixels) {
    UploadScope *scope = upload_scope;
    int factory_call = scope && (uintptr_t)__builtin_return_address(0) == upload_return;
    int matching = factory_call && target == GL_TEXTURE_2D && !level && !border &&
                   internal == GL_RGBA && format == GL_RGBA && type == GL_UNSIGNED_BYTE &&
                   width == (int32_t)scope->key->width && height == (int32_t)scope->key->height;
    if (scope && scope->warm) {
        // All disk checks and GPU allocation happened before superclass init.
        // NULL denotes no raster buffer; this path never forwards it to native
        // backup/conversion code or glTexImage2D.
        if (!matching || pixels || !scope->pending_gpu || scope->uploads)
            port_fail("Encoded texture upload contract changed");
        scope->uploaded_gpu = scope->pending_gpu;
        port_texture_cache_adopt_key(scope->key, scope->pending_gpu);
        scope->pending_gpu = NULL;
        scope->uploads++;
        return;
    }
    original_upload(target, level, internal, width, height, border, format, type, pixels);
    if (matching && pixels) {
        scope->uploaded_gpu = vglGetTexDataPointer(GL_TEXTURE_2D);
        scope->uploads++;
    }
}

static uintptr_t initialize_cached(uintptr_t self, uint32_t resolution,
                                    const PortTextureCacheKey *key, void *gpu) {
    UploadScope scope = {.previous = upload_scope, .key = key,
                         .pending_gpu = gpu, .warm = 1};
    upload_scope = &scope;
    uintptr_t result = initialize_data(self, data_selector, NULL, 1, key->width, key->height,
                                       float_bits(key->width), float_bits(key->height));
    upload_scope = scope.previous;
    // No retry after native initialization: self may already have changed.
    if (scope.pending_gpu)
        vglFree(scope.pending_gpu);
    if (!result)
        return 0;
    if (scope.uploads != 1 || !scope.uploaded_gpu)
        port_fail("Encoded texture initialization missed its upload");
    NativeTexture *texture = (NativeTexture *)result;
    texture->premultiplied = 1;
    texture->resolution = resolution;
    if (!metadata_valid(texture, key, resolution))
        port_fail("Encoded texture initialization metadata changed");
    return result;
}

static uintptr_t __attribute__((pcs("aapcs")))
initialize_encoded(uintptr_t self, uintptr_t selector, uintptr_t data,
                     uint32_t resolution, uint32_t scale) {
    // The native async image path keeps its existing behavior. Key/cache
    // counters and selectors below belong to the bootstrap thread.
    if (sceKernelGetThreadId() != owner_thread)
        return original_encoded(self, selector, data, resolution, scale);
    profile.calls++;
    int saved_errno = errno;
    PortTextureCacheKey key;
    if (!make_key(&key, self, data, resolution, scale)) {
        profile.bypasses++;
        errno = saved_errno;
        return original_encoded(self, selector, data, resolution, scale);
    }
    profile.eligible++;
    void *gpu = port_texture_cache_load_key(&key);
    errno = saved_errno;
    if (gpu) {
        profile.hits++;
        return initialize_cached(self, resolution, &key, gpu);
    }
    UploadScope cold_scope = {.previous = upload_scope, .key = &key};
    upload_scope = &cold_scope;
    uintptr_t result = original_encoded(self, selector, data, resolution, scale);
    upload_scope = cold_scope.previous;
    saved_errno = errno;
    int valid = metadata_valid((const NativeTexture *)result, &key, resolution) &&
                cold_scope.uploads == 1 && cold_scope.uploaded_gpu;
    profile.misses++;
    if (valid && port_texture_cache_store_key(&key, cold_scope.uploaded_gpu))
        profile.stores++;
    errno = saved_errno;
    return result;
}

static uintptr_t make_trampoline(so_module *module, uintptr_t entry) {
    uint32_t *code = (uint32_t *)module->patch_head;
    memcpy(code, (const void *)entry, 8);
    code[2] = 0xe51ff004;
    code[3] = entry + 8;
    kuKernelFlushCaches(code, 16);
    module->patch_head += 16;
    return (uintptr_t)code;
}

void port_encoded_texture_cache_report(void) {
    port_log_debug("CACHE encoded calls=%u eligible=%u hits=%u misses=%u stores=%u "
                   "bypasses=%u",
                   profile.calls, profile.eligible, profile.hits, profile.misses,
                   profile.stores, profile.bypasses);
}

void port_patch_encoded_texture_cache(void) {
    so_module *module = (so_module *)port_module_image();
    uintptr_t module_base = module->load_addr;
    uintptr_t encoded = module_base + 0x6c6850, upload = module_base + 0x6df888;
    uintptr_t data_init = module_base + 0x25e714;
    if (module->patch_head < module->patch_base ||
        module->patch_head - module->patch_base > module->patch_size ||
        32 > module->patch_size - (module->patch_head - module->patch_base))
        port_fail("No native patch space for encoded-texture cache");
    lookup_method = (MethodLookup)port_module_require_symbol("object_getMethodImplementation");
    lookup_class = (ClassLookup)(module_base + 0x6f2c00);
    get_preserved_context = (int (*)(void))port_module_require_symbol("getPreservedContext");
    length_selector = module_base + 0xe0fd28;
    shared_selector = module_base + 0xe0fd30;
    maximum_selector = module_base + 0xe0fd38;
    bytes_selector = module_base + 0xe0fd40;
    data_selector = module_base + 0xe0fd48;
    initialize_data = (DataInit)data_init;
    upload_return = module_base + 0x25e900;
    owner_thread = sceKernelGetThreadId();
    original_encoded = (EncodedInit)make_trampoline(module, encoded);
    original_upload = (TextureUpload)make_trampoline(module, upload);
    hook_arm(encoded, (uintptr_t)initialize_encoded);
    hook_arm(upload, (uintptr_t)upload_texture);
}
