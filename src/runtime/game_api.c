#include "runtime/game_api.h"

#include "runtime/module.h"

static GameApi game;
const GameApi *port_game_api(void) {
    return &game;
}
void port_game_api_init(void) {
    game.jni_on_load = (GameJniOnLoad)port_module_require_symbol("JNI_OnLoad");
    game.set_apk_path = (GameSetString)port_module_require_symbol(
        "Java_com_miniclip_platform_MCAssetManager_nativeSetAPKPath");
    game.set_files_path = (GameSetString)port_module_require_symbol(
        "Java_com_miniclip_platform_MCAssetManager_nativeSetFilesPath");
    game.set_country = (GameSetString)port_module_require_symbol(
        "Java_com_miniclip_platform_MCApplication_nativeSetCountryCode");
    game.set_language = (GameSetString)port_module_require_symbol(
        "Java_com_miniclip_platform_MCApplication_nativeSetLanguage");
    game.set_version = (GameSetString)port_module_require_symbol(
        "Java_com_miniclip_platform_MCApplication_nativeSetApplicationVersionNumber");
    game.renderer_initialize = (GameSurface)port_module_require_symbol(
        "Java_com_miniclip_nativeJNI_ClearRenderer_nativeInit");
    game.renderer_resize = (GameSurface)port_module_require_symbol(
        "Java_com_miniclip_nativeJNI_ClearRenderer_nativeOnSurfaceChanged");
    game.engine_run =
        (GameLifecycle)port_module_require_symbol("Java_com_miniclip_nativeJNI_CocoJNI_Mrun");
    game.renderer_render = (GameLifecycle)port_module_require_symbol(
        "Java_com_miniclip_nativeJNI_ClearRenderer_nativeRender");
    game.renderer_resume = (GameLifecycle)port_module_require_symbol(
        "Java_com_miniclip_nativeJNI_ClearRenderer_nativeOnResume");
    game.set_preserved_context = (GameSetInt)port_module_require_symbol(
        "Java_com_miniclip_nativeJNI_CocoJNI_MpreserveContext");
    game.set_retina_enabled =
        (GameSetInt)port_module_require_symbol("Java_com_miniclip_nativeJNI_CocoJNI_MsetIsRetina");
    game.get_preserved_context =
        (GamePreservedContext)port_module_require_symbol("getPreservedContext");
    game.touch_begin =
        (GameTouch)port_module_require_symbol("Java_com_miniclip_input_MCInput_nativeTouchesBegin");
    game.touch_end =
        (GameTouch)port_module_require_symbol("Java_com_miniclip_input_MCInput_nativeTouchesEnd");
    game.touch_move = (GameTouches)port_module_require_symbol(
        "Java_com_miniclip_input_MCInput_nativeTouchesMove");
    game.touch_cancel = (GameTouches)port_module_require_symbol(
        "Java_com_miniclip_input_MCInput_nativeTouchesCancel");
    game.key_event =
        (GameKey)port_module_require_symbol("Java_com_miniclip_input_MCInput_nativeKeyEvent");
    game.accelerometer_changed = (GameAccelerometer)port_module_find_symbol(
        "Java_com_miniclip_input_MCAccelerometer_onSensorChanged");
    game.content_scale = (const float *)port_module_find_symbol("__ccContentScaleFactor");
    game.point_scale = (const float *)port_module_find_symbol("__ccPointScaleFactor");
}
