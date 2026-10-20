#include "services/methods.h"

#include "compat/jni/strings.h"
#include "vita/audio.h"

#include <stdlib.h>

static jvalue mcaudio_get_background_music_volume(const PortJniCall *call) {
    (void)call;
    return (jvalue){.f = port_vita_audio_music_volume()};
}

static jvalue mcaudio_get_duration_for_sound(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    jvalue java_result = {0};
    char *text = port_jni_string_to_utf8_alloc(arguments[0].l);
    java_result.i = port_vita_audio_duration_ms(text);
    free(text);
    return java_result;
}

static jvalue mcaudio_get_effects_volume(const PortJniCall *call) {
    (void)call;
    return (jvalue){.f = port_vita_audio_effects_volume()};
}

static jvalue mcaudio_is_background_music_playing(const PortJniCall *call) {
    (void)call;
    return (jvalue){.z = port_vita_audio_music_playing()};
}

static jvalue mcaudio_pause_all_sounds(const PortJniCall *call) {
    (void)call;
    port_vita_audio_pause_all(1);
    return (jvalue){0};
}

static jvalue mcaudio_pause_background_music(const PortJniCall *call) {
    (void)call;
    port_vita_audio_music_pause();
    return (jvalue){0};
}

static jvalue mcaudio_pause_effect(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    port_vita_audio_effect_control(arguments[0].i, PORT_EFFECT_PAUSE, 0);
    return (jvalue){0};
}

static jvalue mcaudio_play_background_music(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    char *text = port_jni_string_to_utf8_alloc(arguments[0].l);
    port_vita_audio_music_play(text, arguments[1].z);
    free(text);
    return (jvalue){0};
}

static jvalue play_effect_default(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    port_vita_audio_effect_play(arguments[0].i, 1, 1, 0, 1);
    return (jvalue){0};
}

static jvalue play_effect_stereo(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    port_vita_audio_effect_play(arguments[0].i, arguments[1].f, arguments[2].f, arguments[3].i,
                                arguments[4].f);
    return (jvalue){0};
}

static jvalue play_effect_loops_pitch(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    port_vita_audio_effect_play(arguments[0].i, 1, 1, arguments[1].i, arguments[2].f);
    return (jvalue){0};
}

static jvalue play_effect_stereo_with_priority(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    port_vita_audio_effect_play(arguments[0].i, arguments[1].f, arguments[2].f, arguments[4].i,
                                arguments[5].f);
    return (jvalue){0};
}

static jvalue play_effect_with_priority(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    port_vita_audio_effect_play(arguments[0].i, 1, 1, arguments[2].i, arguments[3].f);
    return (jvalue){0};
}

static jvalue mcaudio_preload_effect(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    jvalue java_result = {0};
    char *text = port_jni_string_to_utf8_alloc(arguments[0].l);
    java_result.i = port_vita_audio_effect_load(text);
    free(text);
    return java_result;
}

static jvalue mcaudio_resume_all_sounds(const PortJniCall *call) {
    (void)call;
    port_vita_audio_pause_all(0);
    return (jvalue){0};
}

static jvalue mcaudio_resume_background_music(const PortJniCall *call) {
    (void)call;
    port_vita_audio_music_resume();
    return (jvalue){0};
}

static jvalue mcaudio_rewind_background_music(const PortJniCall *call) {
    (void)call;
    port_vita_audio_music_rewind();
    return (jvalue){0};
}

static jvalue mcaudio_set_background_music_volume(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    port_vita_audio_set_music_volume(arguments[0].f);
    return (jvalue){0};
}

static jvalue mcaudio_set_effect_gain(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    port_vita_audio_effect_control(arguments[0].i, PORT_EFFECT_GAIN, arguments[1].f);
    return (jvalue){0};
}

static jvalue mcaudio_set_effect_looping(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    port_vita_audio_effect_control(arguments[0].i, PORT_EFFECT_LOOP, arguments[1].z);
    return (jvalue){0};
}

static jvalue mcaudio_set_effect_pitch(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    port_vita_audio_effect_control(arguments[0].i, PORT_EFFECT_PITCH, arguments[1].f);
    return (jvalue){0};
}

static jvalue mcaudio_set_effects_volume(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    port_vita_audio_set_effects_volume(arguments[0].f);
    return (jvalue){0};
}

static jvalue mcaudio_stop_background_music(const PortJniCall *call) {
    (void)call;
    port_vita_audio_music_stop();
    return (jvalue){0};
}

static jvalue mcaudio_stop_effect(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    port_vita_audio_effect_control(arguments[0].i, PORT_EFFECT_STOP, 0);
    return (jvalue){0};
}

static jvalue mcaudio_unload_effect(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    char *text = port_jni_string_to_utf8_alloc(arguments[0].l);
    port_vita_audio_effect_unload(text);
    free(text);
    return (jvalue){0};
}

static const PortServiceMethod methods[] = {
    {{"com/miniclip/audio/MCAudio", "getBackgroundMusicVolume", "()F", 1},
     mcaudio_get_background_music_volume},
    {{"com/miniclip/audio/MCAudio", "getDurationForSound", "(Ljava/lang/String;)I", 1},
     mcaudio_get_duration_for_sound},
    {{"com/miniclip/audio/MCAudio", "getEffectsVolume", "()F", 1}, mcaudio_get_effects_volume},
    {{"com/miniclip/audio/MCAudio", "isBackgroundMusicPlaying", "()Z", 1},
     mcaudio_is_background_music_playing},
    {{"com/miniclip/audio/MCAudio", "pauseAllSounds", "()V", 1}, mcaudio_pause_all_sounds},
    {{"com/miniclip/audio/MCAudio", "pauseBackgroundMusic", "()V", 1},
     mcaudio_pause_background_music},
    {{"com/miniclip/audio/MCAudio", "pauseEffect", "(I)V", 1}, mcaudio_pause_effect},
    {{"com/miniclip/audio/MCAudio", "playBackgroundMusic", "(Ljava/lang/String;Z)V", 1},
     mcaudio_play_background_music},
    {{"com/miniclip/audio/MCAudio", "playEffect", "(I)V", 1}, play_effect_default},
    {{"com/miniclip/audio/MCAudio", "playEffect", "(IFFIF)V", 1}, play_effect_stereo},
    {{"com/miniclip/audio/MCAudio", "playEffect", "(IFFIIF)V", 1},
     play_effect_stereo_with_priority},
    {{"com/miniclip/audio/MCAudio", "playEffect", "(IIF)V", 1}, play_effect_loops_pitch},
    {{"com/miniclip/audio/MCAudio", "playEffect", "(IIIF)V", 1}, play_effect_with_priority},
    {{"com/miniclip/audio/MCAudio", "preloadEffect", "(Ljava/lang/String;)I", 1},
     mcaudio_preload_effect},
    {{"com/miniclip/audio/MCAudio", "resumeAllSounds", "()V", 1}, mcaudio_resume_all_sounds},
    {{"com/miniclip/audio/MCAudio", "resumeBackgroundMusic", "()V", 1},
     mcaudio_resume_background_music},
    {{"com/miniclip/audio/MCAudio", "rewindBackgroundMusic", "()V", 1},
     mcaudio_rewind_background_music},
    {{"com/miniclip/audio/MCAudio", "setBackgroundMusicVolume", "(F)V", 1},
     mcaudio_set_background_music_volume},
    {{"com/miniclip/audio/MCAudio", "setEffectGain", "(IF)V", 1}, mcaudio_set_effect_gain},
    {{"com/miniclip/audio/MCAudio", "setEffectLooping", "(IZ)V", 1}, mcaudio_set_effect_looping},
    {{"com/miniclip/audio/MCAudio", "setEffectPitch", "(IF)V", 1}, mcaudio_set_effect_pitch},
    {{"com/miniclip/audio/MCAudio", "setEffectsVolume", "(F)V", 1}, mcaudio_set_effects_volume},
    {{"com/miniclip/audio/MCAudio", "stopBackgroundMusic", "()V", 1},
     mcaudio_stop_background_music},
    {{"com/miniclip/audio/MCAudio", "stopEffect", "(I)V", 1}, mcaudio_stop_effect},
    {{"com/miniclip/audio/MCAudio", "unloadEffect", "(Ljava/lang/String;)V", 1},
     mcaudio_unload_effect},
};
const PortServiceMethodTable port_audio_methods = {methods, sizeof(methods) / sizeof(methods[0])};
