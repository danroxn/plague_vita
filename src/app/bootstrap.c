#include "app/application.h"

#include "app/log.h"
#include "app/options.h"
#include "app/performance.h"
#include "compat/jni/runtime.h"
#include "expected.h"
#include "runtime/apk.h"
#include "runtime/game_api.h"
#include "runtime/crypt_read.h"
#include "runtime/image_decode.h"
#include "runtime/defaults_batch.h"
#include "runtime/encoded_texture_cache.h"
#include "runtime/objc_cache.h"
#include "runtime/text_cache.h"
#include "runtime/texture_cache.h"
#include "runtime/metadata.h"
#include "runtime/menu_delay.h"
#include "runtime/module.h"
#include "runtime/patches.h"
#include "runtime/paths.h"
#include "runtime/resource_archive.h"
#include "runtime/rgb565_fast.h"
#include "services/dispatch.h"
#include "services/framework.h"
#include "vita/audio.h"
#include "vita/device.h"
#include "vita/dialogs.h"
#include "vita/input.h"
#include "vita/locale.h"
#include "vita/system.h"
#include "vita/time.h"
#include "vita/trophies.h"

#include <errno.h>
#include <psp2/io/stat.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <vitaGL.h>

static pthread_t module_loader;
static int module_loader_started;
static int module_load_result;
static pthread_t text_cache_loader;
static int text_cache_loader_started;

static void *preload_module(void *unused) {
    (void)unused;
    module_load_result = port_module_load();
    return NULL;
}

static void *preload_text_cache(void *unused) {
    (void)unused;
    port_text_cache_prepare();
    return NULL;
}

static void start_loading_workers(void) {
    // ELF loading and text-cache decompression need no native constructors or
    // renderer. Overlap them with trophy setup on the main thread.
    pthread_attr_t attributes;
    int result = pthread_attr_init(&attributes);
    if (result)
        return;
    result = pthread_attr_setstacksize(&attributes, 256 * 1024);
    if (!result) {
        module_loader_started = !pthread_create(&module_loader, &attributes, preload_module, NULL);
        text_cache_loader_started =
            !pthread_create(&text_cache_loader, &attributes, preload_text_cache, NULL);
    }
    pthread_attr_destroy(&attributes);
}

static void set_string(GameSetString setter, const char *value) {
    jstring text = port_jni->NewStringUTF(&port_jni, value);
    setter(&port_jni, NULL, text);
    port_jni->DeleteLocalRef(&port_jni, text);
}

static void show_apk_notice(const char *reason, const char *message) {
    static const char labels[1][PORT_VITA_MESSAGE_LABEL_BYTES] = {"OK"};
    const PortVitaMessageRequest notice = {
        .message_utf8 = message,
        .labels_utf8 = labels,
        .total_choice_count = 1,
        .visible_button_count = 1,
    };
    port_app_loading_pause();
    uint64_t opened_us = port_vita_time_us();
    port_vita_message_open(&notice);
    port_log_debug("STARTUP %s APK notice shown: %s", reason, notice.message_utf8);
    port_log_flush();
    while (!port_vita_message_finished()) {
        port_app_loading_draw_dialog();
        vglSwapBuffers(GL_TRUE);
    }
    int button = port_vita_message_take_result_and_close();
    port_log_debug("STARTUP %s APK notice dismissed button=%d after_ms=%llu", reason, button,
                   (unsigned long long)((port_vita_time_us() - opened_us) / 1000));
}

static _Noreturn void fail_apk(int error, const char *detail) {
    if (error == ENOENT)
        show_apk_notice("missing", "Plague Inc. APK not found.\n\n"
                        "Copy the original Plague Inc. Android APK (version " PORT_APP_VERSION
                        ") to:\nux0:/data/plague/package.apk");
    else
        show_apk_notice("unsupported", "Unsupported Plague Inc. APK.\n\n"
                        "The APK is damaged, unreadable or does not match the supported version.\n\n"
                        "Copy the original Plague Inc. Android APK (version " PORT_APP_VERSION
                        ") to:\nux0:/data/plague/package.apk");
    port_fail("%s (error %d)", detail, error);
}

static void prepare_application(void) {
    sceIoMkdir("ux0:data/plague", 0777);
    PortOptionsLoadResult config = port_app_options_load(PORT_ROOT "port.cfg");
    port_log_init(port_app_options.log_level);
    if (config.invalid_count || config.read_error || config.write_error)
        port_log_error("Port configuration: invalid settings=%u first_line=%u read_error=%d "
                       "write_error=%d",
                       config.invalid_count, config.first_invalid_line, config.read_error,
                       config.write_error);
    port_log_debug(PORT_APP_NAME " Vita / " PORT_APP_VERSION " / build %s", PORT_BUILD_ID);
    uint64_t step_start = port_log_enabled(PORT_LOG_DEBUG) ? port_vita_time_us() : 0;
    port_vita_system_init();
    sceIoMkdir(PORT_CACHE_DIR, 0777);
    sceIoMkdir(PORT_FILES, 0777);
    sceIoMkdir(PORT_FILES "/Contents", 0777);
    sceIoMkdir(PORT_RESOURCES, 0777);
    if (chdir(PORT_RESOURCES) < 0)
        port_fail("Cannot enter game resource directory: %s", PORT_RESOURCES);
    port_vita_display_init(port_app_options.msaa_samples);
    port_log_debug("PERF startup step=display us=%llu",
                   (unsigned long long)(port_vita_time_us() - step_start));
    step_start = port_log_enabled(PORT_LOG_DEBUG) ? port_vita_time_us() : 0;
    port_app_loading_start();
    port_log_debug("PERF startup step=loading_screen us=%llu",
                   (unsigned long long)(port_vita_time_us() - step_start));
    step_start = port_log_enabled(PORT_LOG_DEBUG) ? port_vita_time_us() : 0;
    if (port_apk_verify(PORT_ROOT "package.apk"))
        fail_apk(errno, "Cannot verify package.apk XXH3-128");
    port_log_debug("PERF startup step=apk_verify us=%llu",
                   (unsigned long long)(port_vita_time_us() - step_start));
    if (port_resource_archive_init())
        fail_apk(errno, "Cannot open resources from package.apk");
    start_loading_workers();
    step_start = port_log_enabled(PORT_LOG_DEBUG) ? port_vita_time_us() : 0;
    const PortVitaTrophyPresentation trophy_presentation = {.pause = port_app_loading_pause,
                                                            .draw = port_app_loading_draw_dialog,
                                                            .resume = port_app_loading_resume};
    port_vita_trophies_init(&trophy_presentation);
    port_log_debug("PERF startup step=trophies us=%llu",
                   (unsigned long long)(port_vita_time_us() - step_start));
    if (port_log_enabled(PORT_LOG_DEBUG))
        vglSetDisplayCallback(port_perf_present);
    port_log_debug("RENDER display=960x544 retina=%d msaa=%d log_level=%d",
                   port_app_options.retina_enabled, port_app_options.msaa_samples,
                   (int)port_app_options.log_level);
}
void port_app_start(void) {
    prepare_application();
    port_log_debug("STARTUP preparing native module");
    uint64_t step_start = port_log_enabled(PORT_LOG_DEBUG) ? port_vita_time_us() : 0;
    if (module_loader_started) {
        if (pthread_join(module_loader, NULL))
            port_fail("Cannot join native module loader");
    } else
        module_load_result = port_module_load();
    if (module_load_result)
        fail_apk(EINVAL, "Cannot load native library from package.apk");
    port_log_debug("PERF startup step=module_load us=%llu",
                   (unsigned long long)(port_vita_time_us() - step_start));
    step_start = port_log_enabled(PORT_LOG_DEBUG) ? port_vita_time_us() : 0;
    port_jni_init(port_services_resolve_method, &port_framework_jni_metadata);
    port_vita_dialogs_init();
    port_module_bind();
    port_log_debug("PERF startup step=module_bind us=%llu",
                   (unsigned long long)(port_vita_time_us() - step_start));
    step_start = port_log_enabled(PORT_LOG_DEBUG) ? port_vita_time_us() : 0;
    port_patch_kuser();
    port_patch_language();
    port_patch_main_menu();
    port_patch_menu_delay();
    port_patch_pie_chart();
    port_patch_tech_buttons();
    port_patch_achievements();
    port_patch_unlocks();
    port_patch_asset_read();
    port_patch_crypt_read();
    port_patch_image_decoder();
    port_patch_rgb565_conversion();
    port_patch_encoded_texture_cache();
    port_patch_defaults_batch();
    port_patch_objc_cache();
    if (text_cache_loader_started && pthread_join(text_cache_loader, NULL))
        port_fail("Cannot join text cache loader");
    port_patch_text_cache();
    port_module_flush();
    port_log_debug("PERF startup step=patches us=%llu",
                   (unsigned long long)(port_vita_time_us() - step_start));
    step_start = port_log_enabled(PORT_LOG_DEBUG) ? port_vita_time_us() : 0;
    port_log_debug("STARTUP initializing runtime");
    port_module_initialize();
    port_game_api_init();
    const GameApi *game = port_game_api();
    jint version = game->jni_on_load(&port_vm, NULL);
    if (version != JNI_VERSION_1_4 && version != JNI_VERSION_1_6)
        port_fail("JNI_OnLoad returned 0x%08x", version);
    set_string(game->set_apk_path, PORT_ROOT "package.apk");
    set_string(game->set_files_path, PORT_FILES "/");
    set_string(game->set_country, PORT_DEVICE_COUNTRY);
    const char *system_language = port_vita_language();
    set_string(game->set_language, system_language);
    port_log_debug("LOCALE Vita system language=%s", system_language);
    set_string(game->set_version, PORT_APP_VERSION);
    port_vita_audio_init();
    port_log_debug("STARTUP initializing renderer");
    game->set_preserved_context(&port_jni, NULL, 1);
    if (game->get_preserved_context() != 1)
        port_fail("Native renderer did not preserve its context");
    port_log_debug("PERF startup step=runtime us=%llu",
                   (unsigned long long)(port_vita_time_us() - step_start));
    step_start = port_log_enabled(PORT_LOG_DEBUG) ? port_vita_time_us() : 0;
    game->renderer_initialize(&port_jni, NULL, PORT_DISPLAY_WIDTH, PORT_DISPLAY_HEIGHT, 0);
    game->set_retina_enabled(&port_jni, NULL, port_app_options.retina_enabled);
    game->renderer_resize(&port_jni, NULL, PORT_DISPLAY_WIDTH, PORT_DISPLAY_HEIGHT, 0);
    port_log_debug("PERF startup step=renderer us=%llu",
                   (unsigned long long)(port_vita_time_us() - step_start));
    port_log_debug("STARTUP starting game");
    step_start = port_vita_time_us();
    // Startup and its first rendered menu update the same defaults domain.
    // Close this outer batch after frame zero; readbacks still force a flush.
    port_defaults_batch_begin();
    game->engine_run(&port_jni, NULL);
    uint64_t engine_done = port_vita_time_us();
    port_text_cache_poll(engine_done, engine_done - step_start);
    port_log_debug("PERF startup step=native_run us=%llu",
                   (unsigned long long)(port_vita_time_us() - step_start));
    port_defaults_batch_report();
    port_text_cache_report();
    port_texture_cache_report();
    port_encoded_texture_cache_report();
    port_vita_system_report_memory("ready");
    game->renderer_resume(&port_jni, NULL);
    port_vita_input_init();
}
