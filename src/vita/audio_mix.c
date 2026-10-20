#include "vita/audio_mix.h"

#include "vita/audio_format.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum { MIXER_VOICE_COUNT = 32 };
typedef struct {
    const int16_t *source_samples;
    size_t source_frame_count;
    double source_frame_position;
    float left_gain, right_gain, playback_rate, gain;
    int remaining_loops, is_active, is_paused, effect_id;
} AudioVoice;
static AudioVoice voices[MIXER_VOICE_COUNT];
static float mixer_gain = 1;
int port_vita_audio_mix_start(const PortAudioPlayback *playback) {
    for (unsigned index = 0; index < MIXER_VOICE_COUNT; ++index) {
        if (voices[index].is_active)
            continue;
        voices[index] = (AudioVoice){
            .source_samples = playback->source_samples,
            .source_frame_count = playback->source_frame_count,
            .left_gain = playback->left_gain,
            .right_gain = playback->right_gain,
            .playback_rate = playback->playback_rate,
            .gain = 1,
            .remaining_loops = playback->remaining_loops,
            .is_active = 1,
            .effect_id = playback->effect_id,
        };
        return 1;
    }
    return 0;
}
void port_vita_audio_mix_reset(void) {
    memset(voices, 0, sizeof(voices));
}
void port_vita_audio_mix_set_gain(float gain) {
    mixer_gain = gain;
}
float port_vita_audio_mix_gain(void) {
    return mixer_gain;
}
void port_vita_audio_mix_pause_all(int paused) {
    for (unsigned index = 0; index < MIXER_VOICE_COUNT; ++index)
        voices[index].is_paused = paused;
}
void port_vita_audio_mix_control(int effect_id, PortEffectControl control, float value) {
    for (unsigned index = 0; index < MIXER_VOICE_COUNT; ++index) {
        AudioVoice *voice = &voices[index];
        if (voice->effect_id != effect_id)
            continue;
        switch (control) {
        case PORT_EFFECT_STOP:
            voice->is_active = 0;
            break;
        case PORT_EFFECT_PAUSE:
            voice->is_paused = 1;
            break;
        case PORT_EFFECT_GAIN:
            voice->gain = value;
            break;
        case PORT_EFFECT_PITCH:
            voice->playback_rate = value;
            break;
        case PORT_EFFECT_LOOP:
            voice->remaining_loops = value ? -1 : 0;
            break;
        }
    }
}
static int16_t clip_sample(float sample) {
    return sample > INT16_MAX ? INT16_MAX : sample < INT16_MIN ? INT16_MIN : (int16_t)sample;
}
void port_vita_audio_mix_render(int16_t *output_samples, size_t frames) {
    for (size_t frame_index = 0; frame_index < frames; frame_index++) {
        float left = output_samples[frame_index * PORT_AUDIO_CHANNELS],
              right = output_samples[frame_index * PORT_AUDIO_CHANNELS + 1];
        for (unsigned i = 0; i < MIXER_VOICE_COUNT; i++) {
            AudioVoice *voice = &voices[i];
            if (!voice->is_active || voice->is_paused || !voice->source_frame_count)
                continue;
            while (voice->source_frame_position >= voice->source_frame_count) {
                if (!voice->remaining_loops) {
                    voice->is_active = 0;
                    break;
                }
                if (voice->remaining_loops > 0)
                    voice->remaining_loops--;
                voice->source_frame_position -= voice->source_frame_count;
            }
            if (!voice->is_active)
                continue;
            size_t source_frame_index = (size_t)voice->source_frame_position,
                   next_frame_index = source_frame_index + 1 < voice->source_frame_count
                                          ? source_frame_index + 1
                                          : (voice->remaining_loops ? 0 : source_frame_index);
            float weight = voice->source_frame_position - source_frame_index;
            float gain = voice->gain * mixer_gain;
            left +=
                (voice->source_samples[source_frame_index * PORT_AUDIO_CHANNELS] * (1 - weight) +
                 voice->source_samples[next_frame_index * PORT_AUDIO_CHANNELS] * weight) *
                voice->left_gain * gain;
            right += (voice->source_samples[source_frame_index * PORT_AUDIO_CHANNELS + 1] *
                          (1 - weight) +
                      voice->source_samples[next_frame_index * PORT_AUDIO_CHANNELS + 1] * weight) *
                     voice->right_gain * gain;
            voice->source_frame_position += voice->playback_rate;
        }
        output_samples[frame_index * PORT_AUDIO_CHANNELS] = clip_sample(left);
        output_samples[frame_index * PORT_AUDIO_CHANNELS + 1] = clip_sample(right);
    }
}
