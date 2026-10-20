#include "runtime/apk.h"

#include "expected.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define XXH_INLINE_ALL
#include "utils/xxhash_utils.h"

_Static_assert(EXPECTED_APK_BYTES > 0 &&
                   sizeof(EXPECTED_APK_XXH3_128) == sizeof(XXH128_canonical_t) * 2 + 1,
               "Supported APK identity");

int port_apk_verify(const char *path) {
    int file = open(path, O_RDONLY);
    if (file < 0) {
        if (errno != ENOENT)
            errno = EIO;
        return -1;
    }
    struct stat status;
    int error = 0;
    if (fstat(file, &status) || !S_ISREG(status.st_mode))
        error = EIO;
    else if (status.st_size != EXPECTED_APK_BYTES)
        error = EINVAL;

    // Large direct reads avoid stdio copies; XXH3 uses NEON on the Vita.
    enum { BUFFER_BYTES = 256 * 1024 };
    uint8_t *buffer = error ? NULL : malloc(BUFFER_BYTES);
    XXH3_state_t hash;
    XXH3_INITSTATE(&hash);
    if (!error && (!buffer || XXH3_128bits_reset(&hash) != XXH_OK))
        error = EIO;
    size_t total = 0;
    while (!error) {
        ssize_t bytes = read(file, buffer, BUFFER_BYTES);
        if (bytes < 0) {
            if (errno == EINTR)
                continue;
            error = EIO;
            break;
        }
        if (!bytes)
            break;
        if ((size_t)bytes > EXPECTED_APK_BYTES - total) {
            error = EINVAL;
            break;
        }
        total += (size_t)bytes;
        if (XXH3_128bits_update(&hash, buffer, (size_t)bytes) != XXH_OK)
            error = EIO;
    }
    free(buffer);
    if (close(file))
        error = EIO;
    if (!error && total != EXPECTED_APK_BYTES)
        error = EINVAL;
    if (!error) {
        XXH128_canonical_t digest;
        XXH128_canonicalFromHash(&digest, XXH3_128bits_digest(&hash));
        static const char hexadecimal[] = "0123456789abcdef";
        char identity[sizeof(digest.digest) * 2 + 1];
        for (unsigned i = 0; i < sizeof(digest.digest); i++) {
            identity[i * 2] = hexadecimal[digest.digest[i] >> 4];
            identity[i * 2 + 1] = hexadecimal[digest.digest[i] & 15];
        }
        identity[sizeof(identity) - 1] = 0;
        if (strcmp(identity, EXPECTED_APK_XXH3_128))
            error = EINVAL;
    }
    if (error)
        errno = error;
    return error ? -1 : 0;
}
