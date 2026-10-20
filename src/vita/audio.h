#ifndef PLAGUE_VITA_AUDIO_H
#define PLAGUE_VITA_AUDIO_H
typedef enum {
    PORT_EFFECT_STOP,
    PORT_EFFECT_PAUSE,
    PORT_EFFECT_GAIN,
    PORT_EFFECT_PITCH,
    PORT_EFFECT_LOOP
} PortEffectControl;
void port_vita_audio_init(void);
void port_vita_audio_shutdown(void);
float port_vita_audio_music_volume(void);
float port_vita_audio_effects_volume(void);
void port_vita_audio_set_music_volume(float volume);
void port_vita_audio_set_effects_volume(float volume);
int port_vita_audio_effect_load(const char *name);
void port_vita_audio_effect_play(int id, float left, float right, int loops, float pitch);
void port_vita_audio_effect_control(int id, PortEffectControl control, float value);
void port_vita_audio_effect_unload(const char *name);
void port_vita_audio_music_play(const char *name, int looping);
int port_vita_audio_music_playing(void);
void port_vita_audio_music_stop(void);
void port_vita_audio_music_pause(void);
void port_vita_audio_music_resume(void);
void port_vita_audio_music_rewind(void);
void port_vita_audio_pause_all(int paused);
int port_vita_audio_duration_ms(const char *name);
#endif
