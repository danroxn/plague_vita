#ifndef PLAGUE_VITA_AUDIO_MIX_H
#define PLAGUE_VITA_AUDIO_MIX_H
#include "vita/audio.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const int16_t *source_samples;
    size_t source_frame_count;
    float left_gain;
    float right_gain;
    float playback_rate;
    int remaining_loops;
    int effect_id;
} PortAudioPlayback;
int port_vita_audio_mix_start(const PortAudioPlayback *playback);
void port_vita_audio_mix_reset(void);
void port_vita_audio_mix_control(int effect_id, PortEffectControl control, float value);
void port_vita_audio_mix_pause_all(int paused);
void port_vita_audio_mix_set_gain(float gain);
float port_vita_audio_mix_gain(void);
void port_vita_audio_mix_render(int16_t *output_samples, size_t frames);
#endif
