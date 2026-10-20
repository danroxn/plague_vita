#include "vita/audio.h"

#include "app/log.h"
#include "runtime/paths.h"
#include "runtime/resource_archive.h"
#include "vita/audio_mix.h"
#include "vita/audio_format.h"
#include "vita/mp3_seek.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { EFFECT_REGISTRY_CAPACITY = 256, AUDIO_PATH_CAPACITY = 1024, DEVICE_BUFFER_FRAMES = 1024 };

typedef struct {
    char *resource_name;
    Mix_Chunk *chunk;
} AudioEffect;
static AudioEffect effect_registry[EFFECT_REGISTRY_CAPACITY];
static Mix_Music *current_music;
static float music_volume = 1;
static SDL_RWops *open_audio_resource(const char *path) {
    FILE *file = port_resource_fopen(path, "rb");
    if (!file) {
        SDL_SetError("Cannot open audio resource: %s", path);
        return NULL;
    }
    SDL_RWops *stream = SDL_RWFromFP(file, SDL_TRUE);
    if (!stream)
        fclose(file);
    return stream;
}
static void build_audio_resource_path(const char *name, char *path, size_t path_capacity) {
    if (!strncmp(name, PORT_RESOURCES, strlen(PORT_RESOURCES))) {
        snprintf(path, path_capacity, "%s", name);
        return;
    }
    if (!strncmp(name, "unpack/", 7))
        name += 7;
    snprintf(path, path_capacity, "%s%s", PORT_RESOURCES, name);
}
int port_vita_audio_effect_load(const char *name) {
    for (int i = 1; i < EFFECT_REGISTRY_CAPACITY; i++)
        if (effect_registry[i].resource_name && !strcmp(effect_registry[i].resource_name, name))
            return i;
    int effect_id = 1;
    while (effect_id < EFFECT_REGISTRY_CAPACITY && effect_registry[effect_id].chunk)
        effect_id++;
    if (effect_id == EFFECT_REGISTRY_CAPACITY)
        port_fail("Audio effect registry full");
    char path[AUDIO_PATH_CAPACITY];
    build_audio_resource_path(name, path, sizeof(path));
    SDL_RWops *stream = open_audio_resource(path);
    Mix_Chunk *chunk = stream ? Mix_LoadWAV_RW(stream, 1) : NULL;
    if (!chunk) {
        port_log_error("Audio load failed %s: %s", path, Mix_GetError());
        return 0;
    }
    char *resource_name = strdup(name);
    if (!resource_name) {
        Mix_FreeChunk(chunk);
        port_log_error("Cannot retain audio effect name: %s", name);
        return 0;
    }
    effect_registry[effect_id] = (AudioEffect){.resource_name = resource_name, .chunk = chunk};
    return effect_id;
}
static float clamp_finite(float value, float minimum, float maximum) {
    return isfinite(value) ? fmaxf(minimum, fminf(maximum, value)) : minimum;
}
void port_vita_audio_effect_play(int effect_id, float left, float right, int loops, float pitch) {
    if (effect_id <= 0 || effect_id >= EFFECT_REGISTRY_CAPACITY ||
        !effect_registry[effect_id].chunk) {
        port_log_error("Unknown audio effect %d", effect_id);
        return;
    }
    SDL_LockAudio();
    Mix_Chunk *chunk = effect_registry[effect_id].chunk;
    PortAudioPlayback playback = {
        .source_samples = (int16_t *)chunk->abuf,
        .source_frame_count = chunk->alen / PORT_AUDIO_BYTES_PER_FRAME,
        .left_gain = clamp_finite(left, 0, 1),
        .right_gain = clamp_finite(right, 0, 1),
        .playback_rate = clamp_finite(pitch, 0.5f, 2),
        .remaining_loops = loops < 0 ? -1 : loops,
        .effect_id = effect_id,
    };
    port_vita_audio_mix_start(&playback);
    SDL_UnlockAudio();
}
static void mix_effects_callback(void *unused, Uint8 *stream, int length) {
    (void)unused;
    port_vita_audio_mix_render((int16_t *)stream, length / PORT_AUDIO_BYTES_PER_FRAME);
}
void port_vita_audio_init(void) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
        port_fail("SDL audio init: %s", SDL_GetError());
    int required_codecs = MIX_INIT_MP3 | MIX_INIT_OGG;
    if ((Mix_Init(required_codecs) & required_codecs) != required_codecs)
        port_fail("Audio codecs: %s", Mix_GetError());
    port_vita_mp3_seek_init();
    if (Mix_OpenAudio(PORT_AUDIO_SAMPLE_RATE_HZ, AUDIO_S16SYS, PORT_AUDIO_CHANNELS,
                      DEVICE_BUFFER_FRAMES) < 0)
        port_fail("Audio device: %s", Mix_GetError());
    Mix_AllocateChannels(0);
    Mix_SetPostMix(mix_effects_callback, NULL);
}
void port_vita_audio_shutdown(void) {
    Mix_SetPostMix(NULL, NULL);
    Mix_HaltMusic();
    SDL_LockAudio();
    port_vita_audio_mix_reset();
    SDL_UnlockAudio();
    if (current_music)
        Mix_FreeMusic(current_music);
    for (int i = 1; i < EFFECT_REGISTRY_CAPACITY; i++) {
        if (effect_registry[i].chunk)
            Mix_FreeChunk(effect_registry[i].chunk);
        free(effect_registry[i].resource_name);
    }
    Mix_CloseAudio();
    Mix_Quit();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

float port_vita_audio_music_volume(void) {
    return music_volume;
}
float port_vita_audio_effects_volume(void) {
    return port_vita_audio_mix_gain();
}
void port_vita_audio_set_music_volume(float volume) {
    music_volume = clamp_finite(volume, 0, 1);
    Mix_VolumeMusic((int)(music_volume * MIX_MAX_VOLUME));
}
void port_vita_audio_set_effects_volume(float volume) {
    SDL_LockAudio();
    port_vita_audio_mix_set_gain(clamp_finite(volume, 0, 1));
    SDL_UnlockAudio();
}
void port_vita_audio_music_play(const char *name, int looping) {
    char path[AUDIO_PATH_CAPACITY];
    build_audio_resource_path(name, path, sizeof(path));
    Mix_HaltMusic();
    if (current_music) {
        Mix_FreeMusic(current_music);
        current_music = NULL;
    }
    SDL_RWops *stream = open_audio_resource(path);
    current_music = stream ? Mix_LoadMUS_RW(stream, 1) : NULL;
    if (!current_music) {
        port_log_error("Music load failed %s: %s", path, Mix_GetError());
        return;
    }
    if (Mix_PlayMusic(current_music, looping ? -1 : 0) < 0)
        port_log_error("Music playback failed: %s", Mix_GetError());
}
int port_vita_audio_music_playing(void) {
    return Mix_PlayingMusic() && !Mix_PausedMusic();
}
void port_vita_audio_music_stop(void) {
    Mix_HaltMusic();
}
void port_vita_audio_music_pause(void) {
    Mix_PauseMusic();
}
void port_vita_audio_music_resume(void) {
    Mix_ResumeMusic();
}
void port_vita_audio_music_rewind(void) {
    Mix_RewindMusic();
}
void port_vita_audio_pause_all(int paused) {
    SDL_LockAudio();
    port_vita_audio_mix_pause_all(paused);
    SDL_UnlockAudio();
    if (paused)
        Mix_PauseMusic();
    else
        Mix_ResumeMusic();
}
void port_vita_audio_effect_control(int effect_id, PortEffectControl control, float value) {
    if (control == PORT_EFFECT_GAIN)
        value = clamp_finite(value, 0, 1);
    if (control == PORT_EFFECT_PITCH)
        value = clamp_finite(value, 0.5f, 2);
    SDL_LockAudio();
    port_vita_audio_mix_control(effect_id, control, value);
    SDL_UnlockAudio();
}
void port_vita_audio_effect_unload(const char *name) {
    for (int effect_id = 1; effect_id < EFFECT_REGISTRY_CAPACITY; effect_id++) {
        if (!effect_registry[effect_id].resource_name ||
            strcmp(name, effect_registry[effect_id].resource_name))
            continue;
        SDL_LockAudio();
        port_vita_audio_mix_control(effect_id, PORT_EFFECT_STOP, 0);
        SDL_UnlockAudio();
        Mix_FreeChunk(effect_registry[effect_id].chunk);
        free(effect_registry[effect_id].resource_name);
        effect_registry[effect_id] = (AudioEffect){0};
    }
}
int port_vita_audio_duration_ms(const char *name) {
    char path[AUDIO_PATH_CAPACITY];
    build_audio_resource_path(name, path, sizeof(path));
    SDL_RWops *stream = open_audio_resource(path);
    Mix_Music *probe = stream ? Mix_LoadMUS_RW(stream, 1) : NULL;
    int milliseconds = 0;
    if (probe) {
        double duration = Mix_MusicDuration(probe);
        if (isfinite(duration) && duration > 0)
            milliseconds = duration >= INT_MAX / 1000.0 ? INT_MAX : (int)(duration * 1000);
        Mix_FreeMusic(probe);
    }
    return milliseconds;
}
