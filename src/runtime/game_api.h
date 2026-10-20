#ifndef PLAGUE_RUNTIME_GAME_API_H
#define PLAGUE_RUNTIME_GAME_API_H
#include "compat/abi.h"
#include <jni.h>

typedef jint (*GameJniOnLoad)(JavaVM *vm, void *reserved);
typedef int (*GamePreservedContext)(void);
typedef void (*GameLifecycle)(JNIEnv *environment, jclass declaring_class);
typedef void (*GameSetString)(JNIEnv *environment, jclass declaring_class, jstring value);
typedef void (*GameSetInt)(JNIEnv *environment, jclass declaring_class, jint value);
typedef void (*GameSurface)(JNIEnv *environment, jclass declaring_class, jint width, jint height,
                            jint format);
typedef void SFP (*GameTouch)(JNIEnv *environment, jclass declaring_class, jint contact_id,
                              jfloat x, jfloat y);
typedef void (*GameTouches)(JNIEnv *environment, jclass declaring_class, jintArray contact_ids,
                            jfloatArray contact_x, jfloatArray contact_y);
typedef void (*GameKey)(JNIEnv *environment, jclass declaring_class, jint action, jint key_code,
                        jboolean is_repeat);
typedef void SFP (*GameAccelerometer)(JNIEnv *environment, jclass declaring_class, jfloat x,
                                      jfloat y, jfloat z, jlong timestamp_ns);

typedef struct {
    GameJniOnLoad jni_on_load;
    GameSetString set_apk_path;
    GameSetString set_files_path;
    GameSetString set_country;
    GameSetString set_language;
    GameSetString set_version;
    GameSurface renderer_initialize;
    GameSurface renderer_resize;
    GameLifecycle engine_run;
    GameLifecycle renderer_render;
    GameLifecycle renderer_resume;
    GameSetInt set_preserved_context;
    GameSetInt set_retina_enabled;
    GamePreservedContext get_preserved_context;
    GameTouch touch_begin;
    GameTouch touch_end;
    GameTouches touch_move;
    GameTouches touch_cancel;
    GameKey key_event;
    GameAccelerometer accelerometer_changed;
    const float *content_scale;
    const float *point_scale;
} GameApi;

void port_game_api_init(void);
const GameApi *port_game_api(void);
#endif
