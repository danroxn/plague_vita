#include "runtime/text_cache.h"

#include "app/log.h"
#include "expected.h"
#include "runtime/module.h"
#include "runtime/module_internal.h"
#include "runtime/native_memory.h"
#include "runtime/objc_layout.h"
#include "runtime/paths.h"
#include "vita/time.h"

#include <kubridge.h>
#include <openssl/sha.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define CACHE_PATH PORT_TEXT_CACHE_PATH
#define CACHE_TEMPORARY_PATH CACHE_PATH ".tmp"
#define CACHE_BACKUP_PATH CACHE_PATH ".bak"
#define IO_NOT_FOUND UINT32_C(0x80010002)
#define IO_ALREADY_EXISTS UINT32_C(0x80010011)

enum {
    CACHE_VERSION = 7,
    CACHE_MAGIC = 0x43545854,
    SNAPSHOT_MAGIC = 0x5a545854,
    RECORD_MAGIC = 0x52545854,
    CACHE_SEALED_MAGIC = 0x53545854,
    CACHE_MAX_BYTES = 32 * 1024 * 1024,
    CACHE_MAX_RECORDS = 2048,
    CACHE_INDEX_SLOTS = 4096,
    CACHE_MAX_FONTS = 64,
    CACHE_POLL_INTERVAL_US = 1000000,
    CACHE_APPEND_QUIET_US = 5000000,
    CACHE_FRAME_QUIET_US = 2000000,
    CACHE_LONG_FRAME_US = 500000,
};
typedef struct {
    uint32_t magic, version;
    char apk_xxh3_128[sizeof(EXPECTED_APK_XXH3_128)];
    uint8_t reserved[3];
} CacheFileHeader;
typedef struct {
    uint32_t magic, version, unpacked_bytes, header_crc;
} CacheSnapshotHeader;
typedef struct {
    uint32_t magic, width, height, bytes, pixels_crc;
    uint8_t key[SHA256_DIGEST_LENGTH];
    uint32_t size_words[2];
    uint32_t header_crc;
} CacheRecord;
typedef struct {
    CacheRecord record;
    uint32_t offset;
} CacheIndexEntry;
typedef struct {
    void *typeface;
    uint8_t digest[SHA256_DIGEST_LENGTH];
} CacheFont;
_Static_assert(sizeof(uintptr_t) == 4, "Text rasterizer requires the Android ARM word ABI");
_Static_assert(sizeof(CacheFileHeader) == 44 && sizeof(CacheSnapshotHeader) == 16 &&
               sizeof(CacheRecord) == 64,
               "Text cache disk layout changed; increment its version");

static so_hook font_hook, text_hook;
typedef uintptr_t (*InitString)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                                uintptr_t, uintptr_t, uintptr_t, uintptr_t);
typedef void (*DrawString)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                           uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
typedef struct BitmapScope {
    struct BitmapScope *previous;
    uintptr_t previous_context;
    unsigned draws;
} BitmapScope;
static _Thread_local BitmapScope *bitmap_scope;
static InitString original_init_string;
static DrawString original_draw_string;
static uintptr_t (*current_context)(void);
static uintptr_t (*message_lookup)(uintptr_t, uintptr_t);
static uintptr_t utf8_selector;
static void (*original_game_entered)(void *self, void *selector);
static size_t game_loaded_offset;
static int needs_warmup;
static struct {
    uint8_t *pack;
    int dirty, sealed;
    uint32_t size, records, font_count;
    CacheIndexEntry index[CACHE_INDEX_SLOTS];
    CacheFont fonts[CACHE_MAX_FONTS];
    uint8_t io_buffer[65536];
    uint64_t hits, misses, bypasses, read_bytes, written_bytes;
    uint64_t bitmap_hits, bitmap_misses, skipped;
    uint64_t snapshot_bytes;
    // Main-thread scheduling only; the worker never reads these timestamps.
    uint64_t last_append_us, last_long_frame_us, last_poll_us;
} cache;

static struct {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    pthread_t thread;
    int started, stopping, busy, failed;
    uint32_t requested_size, completed_size;
    uint64_t snapshot_bytes;
    uint8_t io_buffer[65536];
} snapshot_writer = {.lock = PTHREAD_MUTEX_INITIALIZER,
                     .changed = PTHREAD_COND_INITIALIZER};

static void finish_snapshot_writer(void);

static void disable_cache(void) {
    // A worker may still read an immutable prefix of this allocation. Join it
    // before releasing the pack, including the allocation-failure fallback.
    finish_snapshot_writer();
    free(cache.pack);
    cache.pack = NULL;
    cache.dirty = 0;
}

static void regenerate_cache(void) {
    const CacheFileHeader header = {CACHE_MAGIC, CACHE_VERSION, EXPECTED_APK_XXH3_128, {0}};
    memset(cache.index, 0, sizeof(cache.index));
    memcpy(cache.pack, &header, sizeof(header));
    cache.size = sizeof(header);
    cache.records = 0;
    cache.sealed = 0;
    cache.dirty = 1;
    cache.last_append_us = port_vita_time_us();
}

static uint32_t record_crc(const CacheRecord *record) {
    return (uint32_t)crc32(0, (const Bytef *)record, offsetof(CacheRecord, header_crc));
}

static CacheIndexEntry *index_entry(const uint8_t key[SHA256_DIGEST_LENGTH]) {
    uint32_t hash;
    memcpy(&hash, key, sizeof(hash));
    for (unsigned i = 0; i < CACHE_INDEX_SLOTS; i++) {
        CacheIndexEntry *entry = &cache.index[(hash + i) & (CACHE_INDEX_SLOTS - 1)];
        if (!entry->offset || !memcmp(entry->record.key, key, SHA256_DIGEST_LENGTH))
            return entry;
    }
    return NULL;
}

static int valid_record(const CacheRecord *record) {
    uint64_t pixels = (uint64_t)record->width * record->height;
    return record->magic == RECORD_MAGIC && record->width > 0 && record->height > 0 &&
           pixels <= CACHE_MAX_BYTES && record->bytes <= CACHE_MAX_BYTES &&
           ((!record->bytes && !record->pixels_crc) || record->bytes == pixels ||
            record->bytes == pixels * 4) &&
           record_crc(record) == record->header_crc;
}

// This SDK's shared FILE-slot allocator has no locking. Keep all background
// cache IO on raw descriptors so it cannot race the engine's fopen/fclose.
static SceSSize read_up_to(SceUID file, void *output, SceSize bytes) {
    uint8_t *destination = output;
    SceSize done = 0;
    while (done < bytes) {
        SceSSize count = sceIoRead(file, destination + done, bytes - done);
        if (count < 0)
            return count;
        if (!count)
            break;
        if ((SceSize)count > bytes - done)
            return -1;
        done += (SceSize)count;
    }
    return (SceSSize)done;
}

static int write_exact(SceUID file, const void *input, SceSize bytes) {
    const uint8_t *source = input;
    SceSize done = 0;
    while (done < bytes) {
        SceSSize count = sceIoWrite(file, source + done, bytes - done);
        if (count <= 0 || (SceSize)count > bytes - done)
            return 0;
        done += (SceSize)count;
    }
    return 1;
}

static void open_cache(void) {
    const CacheFileHeader expected = {CACHE_MAGIC, CACHE_VERSION, EXPECTED_APK_XXH3_128, {0}};
    cache.pack = malloc(CACHE_MAX_BYTES);
    if (!cache.pack)
        return;
    int recovering_backup = 0;
    SceUID file = sceIoOpen(CACHE_PATH, SCE_O_RDONLY, 0);
    if ((uint32_t)file == IO_NOT_FOUND) {
        // A crash between the two replacement renames can leave only .bak.
        // It must pass exactly the same complete validation as the main file.
        recovering_backup = 1;
        file = sceIoOpen(CACHE_BACKUP_PATH, SCE_O_RDONLY, 0);
    }
    if (file < 0) {
        if ((uint32_t)file == IO_NOT_FOUND)
            regenerate_cache();
        else
            disable_cache();
        return;
    }
    CacheSnapshotHeader header;
    if (read_up_to(file, &header, sizeof(header)) != sizeof(header) ||
        header.magic != SNAPSHOT_MAGIC || header.version != CACHE_VERSION ||
        header.unpacked_bytes < sizeof(expected) || header.unpacked_bytes > CACHE_MAX_BYTES ||
        header.header_crc != (uint32_t)crc32(0, (const Bytef *)&header,
                                            offsetof(CacheSnapshotHeader, header_crc)))
        goto stale;
    z_stream stream = {0};
    if (inflateInit(&stream) != Z_OK) {
        sceIoClose(file);
        disable_cache();
        return;
    }
    stream.next_out = cache.pack;
    stream.avail_out = header.unpacked_bytes;
    int status = Z_OK;
    uint32_t compressed_bytes = 0;
    uint8_t overflow;
    while (status != Z_STREAM_END) {
        if (!stream.avail_in) {
            SceSSize count = read_up_to(file, cache.io_buffer, sizeof(cache.io_buffer));
            if (count <= 0 || (uint32_t)count >
                                  compressBound(header.unpacked_bytes) - compressed_bytes) {
                status = Z_DATA_ERROR;
                break;
            }
            compressed_bytes += (uint32_t)count;
            stream.next_in = cache.io_buffer;
            stream.avail_in = (uInt)count;
        }
        // Leave room to consume the trailer at the exact output limit while
        // rejecting even one additional uncompressed byte.
        if (!stream.avail_out) {
            stream.next_out = &overflow;
            stream.avail_out = 1;
        }
        status = inflate(&stream, Z_NO_FLUSH);
        if ((status != Z_OK && status != Z_STREAM_END) ||
            stream.total_out > header.unpacked_bytes)
            break;
    }
    uint8_t extra;
    int valid = status == Z_STREAM_END && stream.total_out == header.unpacked_bytes &&
                !stream.avail_in && read_up_to(file, &extra, sizeof(extra)) == 0;
    inflateEnd(&stream);
    if (!valid || memcmp(cache.pack, &expected, sizeof(expected)))
        goto stale;
    cache.snapshot_bytes = sizeof(header) + compressed_bytes;
    cache.size = header.unpacked_bytes;
    uint32_t offset = sizeof(expected);
    while (offset < cache.size) {
        // Sealing appends a trailer instead of changing the header: a writer
        // may still be compressing an older, immutable prefix of this pack.
        if (cache.size - offset == sizeof(uint32_t)) {
            uint32_t trailer;
            memcpy(&trailer, cache.pack + offset, sizeof(trailer));
            if (trailer != CACHE_SEALED_MAGIC)
                goto stale;
            cache.sealed = 1;
            break;
        }
        CacheRecord record;
        if (cache.size - offset < sizeof(record) || cache.records == CACHE_MAX_RECORDS)
            goto stale;
        memcpy(&record, cache.pack + offset, sizeof(record));
        uint32_t payload = offset + sizeof(record);
        if (!valid_record(&record) || record.bytes > cache.size - payload ||
            record.pixels_crc != (uint32_t)crc32(0, cache.pack + payload, record.bytes))
            goto stale;
        CacheIndexEntry *entry = index_entry(record.key);
        if (!entry)
            goto stale;
        *entry = (CacheIndexEntry){record, payload};
        cache.records++;
        offset = payload + record.bytes;
    }
    if (!cache.sealed && cache.size > CACHE_MAX_BYTES - sizeof(uint32_t))
        goto stale;
    if (sceIoClose(file) < 0) {
        regenerate_cache();
        return;
    }
    if (recovering_backup)
        sceIoRename(CACHE_BACKUP_PATH, CACHE_PATH);
    return;
stale:
    sceIoClose(file);
    regenerate_cache();
}

void port_text_cache_prepare(void) {
    // Bootstrap may do disk IO/decompression on a worker before any native
    // constructors run. It joins that worker before installing these hooks.
    static int prepared;
    if (!prepared) {
        prepared = 1;
        open_cache();
        // File existence also includes stale, corrupt and header-only snapshots.
        // Publish the result only after validation, without making the presenter
        // read mutable cache fields while this worker is still preparing them.
        __atomic_store_n(&needs_warmup, !cache.pack || !cache.records, __ATOMIC_RELEASE);
    }
}

int port_text_cache_needs_warmup(void) {
    return __atomic_load_n(&needs_warmup, __ATOMIC_ACQUIRE);
}

static int publish_snapshot(void) {
    int result = sceIoRename(CACHE_TEMPORARY_PATH, CACHE_PATH);
    if (result >= 0) {
        sceIoRemove(CACHE_BACKUP_PATH);
        return 1;
    }
    if ((uint32_t)result != IO_ALREADY_EXISTS)
        return 0;
    // Some Vita filesystems cannot replace an existing destination. Preserve
    // that complete snapshot until the new, closed file has been published.
    result = sceIoRemove(CACHE_BACKUP_PATH);
    if (result < 0 && (uint32_t)result != IO_NOT_FOUND)
        return 0;
    if (sceIoRename(CACHE_PATH, CACHE_BACKUP_PATH) < 0)
        return 0;
    if (sceIoRename(CACHE_TEMPORARY_PATH, CACHE_PATH) >= 0) {
        sceIoRemove(CACHE_BACKUP_PATH);
        return 1;
    }
    // If restoration fails, the next load validates and recovers .bak.
    sceIoRename(CACHE_BACKUP_PATH, CACHE_PATH);
    return 0;
}

static int write_snapshot(const uint8_t *pack, uint32_t size, uint64_t *written_bytes) {
    CacheSnapshotHeader header = {SNAPSHOT_MAGIC, CACHE_VERSION, size, 0};
    header.header_crc = (uint32_t)crc32(0, (const Bytef *)&header,
                                       offsetof(CacheSnapshotHeader, header_crc));
    SceUID file = sceIoOpen(CACHE_TEMPORARY_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (file < 0)
        return 0;
    z_stream stream = {0};
    int initialized = deflateInit(&stream, Z_BEST_SPEED) == Z_OK;
    int valid = initialized && write_exact(file, &header, sizeof(header));
    uint64_t disk_bytes = sizeof(header);
    stream.next_in = (Bytef *)pack;
    stream.avail_in = size;
    int status = Z_OK;
    while (valid && status != Z_STREAM_END) {
        stream.next_out = snapshot_writer.io_buffer;
        stream.avail_out = sizeof(snapshot_writer.io_buffer);
        status = deflate(&stream, Z_FINISH);
        size_t count = sizeof(snapshot_writer.io_buffer) - stream.avail_out;
        valid = (status == Z_OK || status == Z_STREAM_END) &&
                write_exact(file, snapshot_writer.io_buffer, (SceSize)count);
        disk_bytes += count;
    }
    if (initialized)
        deflateEnd(&stream);
    // Writes go directly to the kernel; no userspace stdio buffer needs flush.
    if (sceIoClose(file) < 0)
        valid = 0;
    if (valid && publish_snapshot()) {
        *written_bytes = disk_bytes;
        return 1;
    }
    sceIoRemove(CACHE_TEMPORARY_PATH);
    return 0;
}

static void *snapshot_worker(void *argument) {
    const uint8_t *pack = argument;
    pthread_mutex_lock(&snapshot_writer.lock);
    for (;;) {
        while (!snapshot_writer.failed && !snapshot_writer.stopping &&
               snapshot_writer.requested_size <= snapshot_writer.completed_size)
            pthread_cond_wait(&snapshot_writer.changed, &snapshot_writer.lock);
        if (snapshot_writer.failed ||
            (snapshot_writer.stopping &&
             snapshot_writer.requested_size <= snapshot_writer.completed_size))
            break;
        uint32_t size = snapshot_writer.requested_size;
        snapshot_writer.busy = 1;
        pthread_mutex_unlock(&snapshot_writer.lock);

        // Main-thread appends begin at or beyond this captured size. Existing
        // records and their payloads never change, so no second pack is needed.
        uint64_t disk_bytes = 0;
        int valid = write_snapshot(pack, size, &disk_bytes);

        pthread_mutex_lock(&snapshot_writer.lock);
        snapshot_writer.busy = 0;
        if (valid) {
            snapshot_writer.completed_size = size;
            snapshot_writer.snapshot_bytes = disk_bytes;
        } else {
            // Preserve RAM hits and the previous valid disk snapshot.
            snapshot_writer.failed = 1;
        }
        pthread_cond_broadcast(&snapshot_writer.changed);
    }
    pthread_mutex_unlock(&snapshot_writer.lock);
    return NULL;
}

void port_text_cache_flush(void) {
    if (!cache.pack || !cache.dirty)
        return;
    pthread_mutex_lock(&snapshot_writer.lock);
    if (!snapshot_writer.started && !snapshot_writer.failed) {
        pthread_attr_t attributes;
        int result = pthread_attr_init(&attributes);
        int attributes_ready = !result;
        if (!result)
            result = pthread_attr_setstacksize(&attributes, 256 * 1024);
        if (!result) {
            snapshot_writer.stopping = 0;
            result = pthread_create(&snapshot_writer.thread, &attributes,
                                     snapshot_worker, cache.pack);
        }
        if (attributes_ready)
            pthread_attr_destroy(&attributes);
        if (result)
            snapshot_writer.failed = 1;
        else
            snapshot_writer.started = 1;
    }
    if (!snapshot_writer.failed) {
        // Coalesce requests while an older immutable prefix is being written.
        snapshot_writer.requested_size = cache.size;
        cache.dirty = 0;
        pthread_cond_signal(&snapshot_writer.changed);
    }
    pthread_mutex_unlock(&snapshot_writer.lock);
}

void port_text_cache_poll(uint64_t now_us, uint64_t frame_elapsed_us) {
    if (!cache.pack)
        return;
    // Record every long frame before throttling the policy check, including
    // frames without appends (for example a load satisfied entirely by hits).
    if (frame_elapsed_us >= CACHE_LONG_FRAME_US)
        cache.last_long_frame_us = now_us;
    if (now_us >= cache.last_poll_us &&
        now_us - cache.last_poll_us < CACHE_POLL_INTERVAL_US)
        return;
    cache.last_poll_us = now_us;
    if (!cache.dirty || now_us < cache.last_append_us ||
        now_us - cache.last_append_us < CACHE_APPEND_QUIET_US ||
        now_us < cache.last_long_frame_us ||
        now_us - cache.last_long_frame_us < CACHE_FRAME_QUIET_US)
        return;
    port_text_cache_flush();
}

static void finish_snapshot_writer(void) {
    pthread_mutex_lock(&snapshot_writer.lock);
    int started = snapshot_writer.started;
    pthread_t thread = snapshot_writer.thread;
    if (started) {
        snapshot_writer.stopping = 1;
        pthread_cond_signal(&snapshot_writer.changed);
    }
    pthread_mutex_unlock(&snapshot_writer.lock);
    if (started) {
        pthread_join(thread, NULL);
        pthread_mutex_lock(&snapshot_writer.lock);
        snapshot_writer.started = 0;
        pthread_mutex_unlock(&snapshot_writer.lock);
    }
}

void port_text_cache_finish(void) {
    port_text_cache_flush();
    finish_snapshot_writer();
}

static void seal_cache(void) {
    if (!cache.pack || cache.sealed)
        return;
    // append_text always leaves room for this marker, even when the cache is
    // full. It persists the admission policy together with the cached pixels.
    const uint32_t trailer = CACHE_SEALED_MAGIC;
    memcpy(cache.pack + cache.size, &trailer, sizeof(trailer));
    cache.size += sizeof(trailer);
    cache.sealed = 1;
    cache.dirty = 1;
    cache.last_append_us = port_vita_time_us();
    port_text_cache_flush();
    port_log_debug("CACHE text sealed records=%u file_bytes=%u",
                   (unsigned)cache.records, (unsigned)cache.size);
}

static void game_entered(void *self, void *selector) {
    original_game_entered(self, selector);
    // The native callback leaves this false if loading failed and it schedules
    // a return to the main menu. Only a successfully entered game seals us.
    if (*((const uint8_t *)self + game_loaded_offset))
        seal_cache();
}

static const uint8_t *font_digest(uintptr_t typeface) {
    for (uint32_t i = 0; i < cache.font_count; i++)
        if (cache.fonts[i].typeface == (void *)typeface)
            return cache.fonts[i].digest;
    return NULL;
}

static void *read_font(const void *data, int length) {
    void *result = SO_CONTINUE(void *, font_hook, data, length);
    if (cache.pack && result && data && length > 0) {
        uint32_t index = 0;
        while (index < cache.font_count && cache.fonts[index].typeface != result)
            index++;
        if (index < CACHE_MAX_FONTS) {
            if (!SHA256(data, (size_t)length, cache.fonts[index].digest))
                disable_cache();
            else {
                cache.fonts[index].typeface = result;
                if (index == cache.font_count)
                    cache.font_count++;
            }
        }
    }
    return result;
}

static int text_key(uint8_t key[SHA256_DIGEST_LENGTH], const void *text, uint32_t length,
                    uintptr_t typeface, const uint32_t style[16], uint32_t namespace_tag) {
    const uint8_t *font = font_digest(typeface);
    if (!cache.pack || !text || (int32_t)length <= 0 || !font)
        return 0;
    const uint32_t version = CACHE_VERSION;
    SHA256_CTX hash;
    if (!SHA256_Init(&hash) ||
        !SHA256_Update(&hash, &version, sizeof(version)) ||
        !SHA256_Update(&hash, EXPECTED_APK_XXH3_128, sizeof(EXPECTED_APK_XXH3_128)) ||
        !SHA256_Update(&hash, &namespace_tag, sizeof(namespace_tag)) ||
        !SHA256_Update(&hash, font, SHA256_DIGEST_LENGTH) ||
        !SHA256_Update(&hash, &length, sizeof(length)) ||
        !SHA256_Update(&hash, style, 16 * sizeof(*style)) ||
        !SHA256_Update(&hash, text, length) || !SHA256_Final(key, &hash)) {
        disable_cache();
        return 0;
    }
    return 1;
}

static int read_payload(const CacheIndexEntry *entry, void *pixels) {
    const CacheRecord *record = &entry->record;
    if (!cache.pack || entry->offset > cache.size || record->bytes > cache.size - entry->offset)
        return 0;
    // All persisted payloads were CRC-checked once while loading the pack.
    memcpy(pixels, cache.pack + entry->offset, record->bytes);
    cache.hits++;
    cache.read_bytes += record->bytes;
    return 1;
}

static void *read_pixels(const CacheIndexEntry *entry) {
    void *pixels = port_native_allocate(entry->record.bytes, 0);
    if (!pixels) {
        disable_cache();
        return NULL;
    }
    if (!read_payload(entry, pixels)) {
        port_native_free(pixels);
        return NULL;
    }
    return pixels;
}

static void append_text(const uint8_t key[SHA256_DIGEST_LENGTH], const void *pixels,
                         int width, int height, uintptr_t alpha_only,
                         const uint32_t size_words[2]) {
    if (cache.sealed) {
        cache.skipped++;
        return;
    }
    if (!cache.pack || width <= 0 || height <= 0 ||
        cache.records == CACHE_MAX_RECORDS)
        return;
    uint64_t pixel_count = (uint64_t)width * height;
    if (pixel_count > CACHE_MAX_BYTES)
        return;
    uint64_t bytes = pixels ? pixel_count * (alpha_only ? 1 : 4) : 0;
    if (bytes > CACHE_MAX_BYTES ||
        sizeof(CacheRecord) + bytes + sizeof(uint32_t) > CACHE_MAX_BYTES - cache.size)
        return;
    CacheIndexEntry *entry = index_entry(key);
    if (!entry)
        return;
    CacheRecord record = {RECORD_MAGIC, (uint32_t)width, (uint32_t)height, (uint32_t)bytes,
                          pixels ? (uint32_t)crc32(0, pixels, (uInt)bytes) : 0, {0}, {0}, 0};
    memcpy(record.key, key, sizeof(record.key));
    if (size_words)
        memcpy(record.size_words, size_words, sizeof(record.size_words));
    record.header_crc = record_crc(&record);
    memcpy(cache.pack + cache.size, &record, sizeof(record));
    if (bytes)
        memcpy(cache.pack + cache.size + sizeof(record), pixels, (size_t)bytes);
    *entry = (CacheIndexEntry){record, cache.size + sizeof(record)};
    cache.size += sizeof(record) + (uint32_t)bytes;
    cache.records++;
    cache.written_bytes += bytes;
    cache.dirty = 1;
    cache.last_append_us = port_vita_time_us();
}

// The softfp rasterizer uses four register words and seventeen stack words.
// Only a0/a2 and the output locations a4/a5 are pointers; all style words are scalars.
static void *read_text(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                       uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7,
                       uintptr_t a8, uintptr_t a9, uintptr_t a10, uintptr_t a11,
                       uintptr_t a12, uintptr_t a13, uintptr_t a14, uintptr_t a15,
                       uintptr_t a16, uintptr_t a17, uintptr_t a18, uintptr_t a19,
                       uintptr_t a20) {
    uint8_t key[SHA256_DIGEST_LENGTH];
    const uint32_t style[] = {a3, a6, a7, a8, a9, a10, a11, a12,
                              a13, a14, a15, a16, a17, a18, a19, a20};
    int eligible = a4 && a5 && text_key(key, (const void *)a0, a1, a2, style, 0x52535452);
    if (eligible) {
        CacheIndexEntry *entry = index_entry(key);
        if (entry && entry->offset) {
            // a6 == 0 requests measurement only: native writes both dimensions
            // and deliberately returns NULL without allocating any pixels.
            if (!a6 && !entry->record.bytes) {
                *(int *)a4 = (int)entry->record.width;
                *(int *)a5 = (int)entry->record.height;
                cache.hits++;
                return NULL;
            }
            void *pixels = entry->record.bytes ? read_pixels(entry) : NULL;
            if (pixels) {
                *(int *)a4 = (int)entry->record.width;
                *(int *)a5 = (int)entry->record.height;
                return pixels;
            }
        }
        cache.misses++;
    } else
        cache.bypasses++;
    void *result = SO_CONTINUE(void *, text_hook, a0, a1, a2, a3, a4, a5, a6, a7,
                               a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18,
                               a19, a20);
    // A NULL render result remains a miss; only explicit measurement calls
    // may store dimensions without a pixel payload.
    if (eligible && (result || !a6))
        append_text(key, result, *(const int *)a4, *(const int *)a5, a19, NULL);
    return result;
}

static uintptr_t init_string(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                              uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7,
                              uintptr_t a8) {
    BitmapScope scope = {bitmap_scope, current_context(), 0};
    bitmap_scope = &scope;
    uintptr_t result = original_init_string(a0, a1, a2, a3, a4, a5, a6, a7, a8);
    bitmap_scope = scope.previous;
    return result;
}

// CGSize return uses a0 as its hidden destination pointer. The CGRect and
// final float padding remain raw words under the native Android softfp ABI.
static void draw_string(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                         uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7,
                         uintptr_t a8, uintptr_t a9, uintptr_t a10) {
    uint8_t key[SHA256_DIGEST_LENGTH];
    uint32_t *context = (uint32_t *)current_context();
    int eligible = cache.pack && bitmap_scope && ++bitmap_scope->draws == 1 &&
                   !bitmap_scope->previous && a0 && a1 && a7 && context &&
                   (uintptr_t)context != bitmap_scope->previous_context;
    if (eligible) {
        // Native CGBitmapCreate allocates 80 zeroed bytes; this caller sets
        // only the white RGBA components. Translation/scale are native no-ops.
        const uint32_t white_state[15] = {0, 0, 0x3f800000, 0x3f800000,
                                          0x3f800000, 0x3f800000};
        uint64_t bytes = (uint64_t)context[1] * context[2];
        eligible = context[0] && context[1] && context[2] && context[3] == 8 &&
                   context[4] == context[1] && bytes <= CACHE_MAX_BYTES &&
                   !(context[1] & (context[1] - 1)) &&
                   !(context[2] & (context[2] - 1)) &&
                   !memcmp(context + 5, white_state, sizeof(white_state));
    }
    if (eligible) {
        uintptr_t font_data = *(const uintptr_t *)(a7 + 12);
        uintptr_t typeface = font_data ? *(const uintptr_t *)(font_data + 4) : 0;
        const char *(*utf8)(uintptr_t, uintptr_t) =
            (const char *(*)(uintptr_t, uintptr_t))message_lookup(a1, utf8_selector);
        const char *text = utf8(a1, utf8_selector);
        const uint32_t style[16] = {*(const uint32_t *)(a7 + 8), a3, a4, a5, a6,
                                    a8, a9, a10, context[1], context[2], context[4], 8};
        eligible = text && text_key(key, text, strlen(text), typeface, style, 0x4347424d);
    }
    if (eligible) {
        CacheIndexEntry *entry = index_entry(key);
        uint32_t bytes = context[1] * context[2];
        if (entry && entry->offset && entry->record.width == context[1] &&
            entry->record.height == context[2] && entry->record.bytes == bytes) {
            if (read_payload(entry, (void *)context[0])) {
                memcpy((void *)a0, entry->record.size_words, sizeof(entry->record.size_words));
                cache.bitmap_hits++;
                return;
            }
            // Preserve the fresh background if a RAM entry fails bounds checks.
            memset((void *)context[0], 0, bytes);
        }
        cache.misses++;
        cache.bitmap_misses++;
    }
    original_draw_string(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
    if (eligible && context == (uint32_t *)current_context()) {
        const uint32_t *size = (const uint32_t *)a0;
        if (size[0] && size[0] < 0x7f800000 && size[1] && size[1] < 0x7f800000)
            append_text(key, (const void *)context[0], context[1], context[2], 1, size);
    }
}

static uintptr_t original_trampoline(so_module *module, uintptr_t entry) {
    uint32_t *trampoline = (uint32_t *)module->patch_head;
    // The PUSH/ADD-FP prologue has no PC-relative operands.
    memcpy(trampoline, (const void *)entry, 2 * sizeof(uint32_t));
    trampoline[2] = 0xe51ff004;
    trampoline[3] = entry + 2 * sizeof(uint32_t);
    kuKernelFlushCaches(trampoline, 4 * sizeof(uint32_t));
    module->patch_head += 4 * sizeof(uint32_t);
    return (uintptr_t)trampoline;
}

void port_text_cache_report(void) {
    pthread_mutex_lock(&snapshot_writer.lock);
    uint64_t snapshot_bytes = snapshot_writer.snapshot_bytes ? snapshot_writer.snapshot_bytes
                                                            : cache.snapshot_bytes;
    uint32_t pending_bytes = snapshot_writer.requested_size - snapshot_writer.completed_size;
    int busy = snapshot_writer.busy, failed = snapshot_writer.failed;
    pthread_mutex_unlock(&snapshot_writer.lock);
    port_log_debug("CACHE text enabled=%d records=%u fonts=%u hits=%llu misses=%llu "
                   "bypasses=%llu read_bytes=%llu written_bytes=%llu file_bytes=%u "
                   "bitmap_hits=%llu bitmap_misses=%llu snapshot_bytes=%llu dirty=%d "
                   "snapshot_pending=%u snapshot_busy=%d snapshot_failed=%d sealed=%d skipped=%llu",
                   cache.pack != NULL, (unsigned)cache.records, (unsigned)cache.font_count,
                   (unsigned long long)cache.hits, (unsigned long long)cache.misses,
                   (unsigned long long)cache.bypasses, (unsigned long long)cache.read_bytes,
                   (unsigned long long)cache.written_bytes, (unsigned)cache.size,
                   (unsigned long long)cache.bitmap_hits,
                   (unsigned long long)cache.bitmap_misses,
                   (unsigned long long)snapshot_bytes, cache.dirty, pending_bytes, busy, failed,
                   cache.sealed, (unsigned long long)cache.skipped);
}

void port_patch_text_cache(void) {
    so_module *module = (so_module *)port_module_image();
    uintptr_t init = module->load_addr + 0x25f3dc;
    uintptr_t draw = module->load_addr + 0x6dc8b0;
    const size_t trampoline_bytes = 8 * sizeof(uint32_t);
    if (module->patch_head < module->patch_base ||
        module->patch_head - module->patch_base > module->patch_size ||
        trampoline_bytes > module->patch_size - (module->patch_head - module->patch_base))
        port_fail("No native patch space for text bitmap cache");
    uintptr_t font = port_module_require_symbol("getFontData");
    uintptr_t text =
        port_module_require_symbol("createStringTextureWithPaddingBackgroudStrokeShadow");
    port_text_cache_prepare();
    if (!cache.pack)
        return;
    NativeObjcMethod *entered = (void *)(module->load_addr + 0xdc46d8);
    const NativeObjcIvar *loaded = (void *)(module->load_addr + 0xdc4ca8);
    original_game_entered = (void (*)(void *, void *))entered->implementation;
    game_loaded_offset = loaded->offset;
    entered->implementation = (uintptr_t)game_entered;
    original_init_string = (InitString)original_trampoline(module, init);
    original_draw_string = (DrawString)original_trampoline(module, draw);
    current_context = (uintptr_t (*)(void))(module->load_addr + 0x6d944c);
    message_lookup = (uintptr_t (*)(uintptr_t, uintptr_t))(module->load_addr + 0x6f5ff4);
    utf8_selector = module->load_addr + 0xe1ac90;
    font_hook = hook_arm(font, (uintptr_t)read_font);
    text_hook = hook_arm(text, (uintptr_t)read_text);
    hook_arm(init, (uintptr_t)init_string);
    hook_arm(draw, (uintptr_t)draw_string);
}
