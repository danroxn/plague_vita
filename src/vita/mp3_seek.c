#include "vita/mp3_seek.h"

#include "app/log.h"

#include <SDL2/SDL_mixer.h>
#include <kubridge.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// SDL_mixer 2.8.2 src/music.h, through its Seek callback. Keep decoder contexts
// opaque except for the three fields checked against the installed SDK code.
// https://github.com/libsdl-org/SDL_mixer/tree/release-2.8.2/src
typedef struct {
    const char *tag;
    uint8_t api, type, loaded, opened; // VitaSDK builds SDL with short enums.
    uintptr_t callbacks[11];
} MusicInterfacePrefix;
_Static_assert(sizeof(uintptr_t) == 4 && sizeof(MusicInterfacePrefix) == 52 &&
               offsetof(MusicInterfacePrefix, callbacks) == 8,
               "SDL_mixer ARM32 interface prefix layout");
extern const unsigned char Mix_MusicInterface_MINIMP3[];
extern int mp3dec_ex_seek(void *decoder, uint64_t sample);

enum { FLAGS_OFFSET = 0x2c7c, SKIP_OFFSET = 0x2c94, DELAY_OFFSET = 0x2c98 };

static int seek_without_index(void *decoder, uint64_t sample) {
    if (!decoder || sample)
        return mp3dec_ex_seek(decoder, sample);
    uint8_t *state = decoder;
    int32_t flags, delay;
    memcpy(&flags, state + FLAGS_OFFSET, sizeof(flags));
    memcpy(&delay, state + DELAY_OFFSET, sizeof(delay));
    if (!(flags & 1) || delay <= 0)
        return mp3dec_ex_seek(decoder, sample);

    // The original seek adds encoder delay before testing for sample zero,
    // causing a full-file index scan even though no indexed seeking is needed.
    // Let its zero path reset decoder/input state, then preserve the same
    // leading-sample trim used by a freshly opened decoder. SDL serializes all
    // operations on this context under its audio lock, including loop seeks.
    int32_t zero = 0;
    memcpy(state + DELAY_OFFSET, &zero, sizeof(zero));
    int result = mp3dec_ex_seek(decoder, 0);
    memcpy(state + DELAY_OFFSET, &delay, sizeof(delay));
    if (!result)
        memcpy(state + SKIP_OFFSET, &delay, sizeof(delay));
    return result;
}

static uintptr_t branch_target(uintptr_t address, const uint16_t code[2]) {
    if ((code[0] & 0xf800) != 0xf000 || (code[1] & 0xd000) != 0xd000)
        return 0;
    unsigned sign = (code[0] >> 10) & 1;
    unsigned i1 = !(((code[1] >> 13) & 1) ^ sign);
    unsigned i2 = !(((code[1] >> 11) & 1) ^ sign);
    uint32_t encoded = (sign << 24) | (i1 << 23) | (i2 << 22) |
                       ((code[0] & 0x3ff) << 12) | ((code[1] & 0x7ff) << 1);
    int32_t displacement = (int32_t)(encoded << 7) >> 7;
    return address + 4 + displacement;
}

static int encode_branch(uintptr_t address, uintptr_t target, uint16_t code[2]) {
    int64_t displacement = (int64_t)(target & ~(uintptr_t)1) - ((int64_t)address + 4);
    if (!(target & 1) || (displacement & 1) || displacement < -(1 << 24) ||
        displacement >= (1 << 24))
        return 0;
    uint32_t encoded = (uint32_t)displacement;
    unsigned sign = (encoded >> 24) & 1;
    unsigned j1 = !(((encoded >> 23) & 1) ^ sign);
    unsigned j2 = !(((encoded >> 22) & 1) ^ sign);
    code[0] = 0xf000 | (sign << 10) | ((encoded >> 12) & 0x3ff);
    code[1] = 0xd000 | (j1 << 13) | (j2 << 11) | ((encoded >> 1) & 0x7ff);
    return branch_target(address, code) == (target & ~(uintptr_t)1);
}

void port_vita_mp3_seek_init(void) {
    static int initialized;
    if (initialized)
        return;
    initialized = 1;
    const SDL_version *version = Mix_Linked_Version();
    if (!version || version->major != 2 || version->minor != 8 || version->patch != 2)
        goto unsupported;
    MusicInterfacePrefix interface;
    memcpy(&interface, Mix_MusicInterface_MINIMP3, sizeof(interface));
    if (!interface.tag || strcmp(interface.tag, "MINIMP3") || interface.api != 7 ||
        interface.type != MUS_MP3 || !(interface.callbacks[6] & 1) ||
        !(interface.callbacks[10] & 1) || !((uintptr_t)mp3dec_ex_seek & 1))
        goto unsupported;

    uintptr_t decoder_seek = (uintptr_t)mp3dec_ex_seek & ~(uintptr_t)1;
    uintptr_t play = interface.callbacks[6] & ~(uintptr_t)1;
    uintptr_t seek = interface.callbacks[10] & ~(uintptr_t)1;
    // Exact VitaSDK signatures prove the 8192-byte base plus flags/start-delay
    // offsets, sample-mode branch, and the to_skip store used by the zero path.
    const uint16_t decoder_head[] = {
        0x2800, 0xf000, 0x82b8, 0xe92d, 0x4ff0, 0xf500, 0x5700, 0xf8d7,
        0x1c7c, 0xb0b7, 0x4680, 0xf011, 0x0101, 0x9107, 0xf000, 0x80cf,
        0xf8d7, 0x6c98, 0xf500, 0x5bd2, 0xe9cb, 0x2300, 0x18b4, 0xeb43,
        0x76e6, 0xea54, 0x0306, 0xf000, 0x8095, 0xf8d7, 0x3c84, 0x2b00};
    const uint16_t skip_store[] = {0xf8c7, 0x3c94};
    const uint16_t play_head[] = {0xf500, 0x5333, 0xb570, 0x4605, 0x6201};
    const uint16_t seek_head[] = {0xf500, 0x5333, 0xb570, 0x4605, 0xb082};
    const uint16_t loop_head[] = {0xe92d, 0x4ff0, 0xf500, 0x5400, 0xb093, 0x461d, 0x9004};
    const uint16_t play_return[] = {0x2000, 0xbd70};
    const uint16_t loop_return = 0xe7ca;
    if (memcmp((const void *)decoder_seek, decoder_head, sizeof(decoder_head)) ||
        memcmp((const void *)(decoder_seek + 0x182), skip_store, sizeof(skip_store)) ||
        memcmp((const void *)play, play_head, sizeof(play_head)) ||
        memcmp((const void *)seek, seek_head, sizeof(seek_head)) ||
        memcmp((const void *)(play + 0x4c), loop_head, sizeof(loop_head)) ||
        memcmp((const void *)(play + 0xd8), &loop_return, sizeof(loop_return)) ||
        memcmp((const void *)(play + 0x46), play_return, sizeof(play_return)))
        goto unsupported;

    // The SDK inlines Play into GetSome's EOF loop, creating a third seek.
    // Cover it too so the first natural repeat never acquires a deferred index.
    uintptr_t sites[] = {play + 0x42, seek + 0x46, play + 0xd4};
    uint16_t original[3][2], replacement[3][2];
    for (unsigned i = 0; i < 3; i++) {
        memcpy(original[i], (const void *)sites[i], sizeof(original[i]));
        if (branch_target(sites[i], original[i]) != decoder_seek ||
            !encode_branch(sites[i], (uintptr_t)seek_without_index, replacement[i]))
            goto unsupported;
    }
    // Install before Mix_OpenAudio, while no callback can execute these sites.
    // Only call operands change: original loop counts and mixer callbacks stay.
    for (unsigned i = 0; i < 3; i++) {
        if (kuKernelCpuUnrestrictedMemcpy((void *)sites[i], replacement[i],
                                           sizeof(replacement[i])) < 0) {
            for (unsigned restore = 0; restore <= i; restore++) {
                kuKernelCpuUnrestrictedMemcpy((void *)sites[restore], original[restore],
                                               sizeof(original[restore]));
                kuKernelFlushCaches((const void *)sites[restore], sizeof(original[restore]));
            }
            goto unsupported;
        }
        kuKernelFlushCaches((const void *)sites[i], sizeof(replacement[i]));
    }
    port_log_debug("MP3 sample-zero seeks preserve encoder delay without scanning an index");
    return;

unsupported:
    port_log_debug("MP3 fast zero seek unavailable; using original SDL_mixer behavior");
}
