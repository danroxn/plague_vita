#include "services/methods.h"

#include "app/log.h"
#include "compat/android_contract.h"
#include "compat/jni/runtime.h"
#include "compat/jni/strings.h"
#include "runtime/callbacks.h"
#include "runtime/metadata.h"
#include "runtime/paths.h"
#include "services/async.h"
#include "vita/device.h"
#include "vita/input.h"
#include "vita/locale.h"
#include "vita/system.h"
#include "vita/time.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *form_urlencode(const unsigned char *input_bytes, size_t input_length_bytes) {
    enum { ESCAPED_BYTE_WIDTH = 3 };
    char *encoded_text = malloc(input_length_bytes * ESCAPED_BYTE_WIDTH + 1);
    if (!encoded_text)
        port_fail("URL allocation failed");
    char *output_cursor = encoded_text;
    for (size_t index = 0; index < input_length_bytes; index++) {
        unsigned char byte = input_bytes[index];
        if ((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
            (byte >= '0' && byte <= '9') || (byte && strchr("-._*", byte)))
            *output_cursor++ = byte;
        else if (byte == ' ')
            *output_cursor++ = '+';
        else {
            sprintf(output_cursor, "%%%02X", byte);
            output_cursor += ESCAPED_BYTE_WIDTH;
        }
    }
    *output_cursor = 0;
    return encoded_text;
}
static jvalue cocojava_percent_encode_utf8(const PortJniCall *call) {
    size_t input_length_bytes;
    char *input_bytes =
        port_jni_string_to_utf8_alloc_with_length(call->arguments[0].l, &input_length_bytes);
    char *encoded_text = form_urlencode((const unsigned char *)input_bytes, input_length_bytes);
    jstring result = port_jni_string_from_utf8(encoded_text);
    free(encoded_text);
    free(input_bytes);
    return (jvalue){.l = result};
}

static jvalue debug_get_native_heap_allocated_size(const PortJniCall *call) {
    (void)call;
    return (jvalue){.j = port_vita_system_heap_used()};
}

static jvalue application_package_name(const PortJniCall *call) {
    (void)call;
    return (jvalue){.l = port_jni_string_from_utf8(PORT_APP_PACKAGE)};
}

static jvalue application_info_version_code(const PortJniCall *call) {
    (void)call;
    return (jvalue){.l = port_jni->NewStringUTF(&port_jni, PORT_APP_VERSION_CODE_TEXT)};
}

static jvalue application_version_name(const PortJniCall *call) {
    (void)call;
    return (jvalue){.l = port_jni->NewStringUTF(&port_jni, PORT_APP_VERSION)};
}

static jvalue device_info_brand_name(const PortJniCall *call) {
    (void)call;
    return (jvalue){.l = port_jni->NewStringUTF(&port_jni, PORT_DEVICE_BRAND)};
}

static jvalue offline_device_id(const PortJniCall *call) {
    (void)call;
    // Preserve the Android ID format without generating or storing an identity.
    return (jvalue){.l = port_jni_string_from_utf8("00000000000000000000000000000000")};
}

static jvalue device_info_country(const PortJniCall *call) {
    (void)call;
    return (jvalue){.l = port_jni->NewStringUTF(&port_jni, PORT_DEVICE_COUNTRY)};
}

static jvalue device_info_model(const PortJniCall *call) {
    (void)call;
    return (jvalue){.l = port_jni->NewStringUTF(&port_jni, PORT_DEVICE_MODEL)};
}

static jvalue device_info_operating_system_version(const PortJniCall *call) {
    (void)call;
    return (jvalue){.l = port_jni->NewStringUTF(&port_jni, PORT_ANDROID_RELEASE)};
}

static jvalue device_info_system_language(const PortJniCall *call) {
    (void)call;
    return (jvalue){.l = port_jni_string_from_utf8(port_vita_language())};
}

static jvalue screen_dpi(const PortJniCall *call) {
    (void)call;
    return (jvalue){.f = (jfloat)PORT_DISPLAY_DPI};
}

static jvalue screen_info_screen_height(const PortJniCall *call) {
    (void)call;
    return (jvalue){.f = (jfloat)PORT_DISPLAY_HEIGHT};
}

static jvalue screen_info_screen_width(const PortJniCall *call) {
    (void)call;
    return (jvalue){.f = (jfloat)PORT_DISPLAY_WIDTH};
}

static jvalue mcinput_set_accelerometer_enabled(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    port_vita_input_accelerometer(arguments[0].z);
    return (jvalue){0};
}

static jvalue cocojava_device_supports_multitouch(const PortJniCall *call) {
    (void)call;
    return (jvalue){.i = 1};
}

static jvalue cocojava_available_memory_on_device(const PortJniCall *call) {
    (void)call;
    return (jvalue){.f = (float)(port_vita_system_free_storage() / 1048576)};
}

static jvalue cocojava_get_android_version(const PortJniCall *call) {
    (void)call;
    return (jvalue){.i = PORT_ANDROID_API_LEVEL};
}

static jvalue cocojava_get_device_info(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    char key[64];
    port_jni_string_copy_utf8((jstring)arguments[0].l, key, sizeof(key));
    const char *value = "";
    if (!strcmp(key, "device"))
        value = "psvita";
    else if (!strcmp(key, "brand"))
        value = PORT_DEVICE_BRAND;
    else if (!strcmp(key, "name"))
        value = "Sony PlayStation Vita";
    else if (!strcmp(key, "version"))
        value = "Vita";
    return (jvalue){.l = port_jni_string_from_utf8(value)};
}

static jvalue cocojava_get_resource_string(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    return (jvalue){.l = port_jni->NewLocalRef(&port_jni, arguments[1].l)};
}

static jvalue cocojava_get_timezone_offset(const PortJniCall *call) {
    (void)call;
    return (jvalue){.i = port_vita_timezone_offset_seconds()};
}

static jboolean permission_is_granted(jstring permission) {
    char *name = port_jni_string_to_utf8_alloc(permission);
    jboolean granted = !strcmp(name, "android.permission.ACCESS_NETWORK_STATE") ||
                       !strcmp(name, "android.permission.WRITE_EXTERNAL_STORAGE") ||
                       !strcmp(name, "android.permission.READ_EXTERNAL_STORAGE");
    free(name);
    return granted;
}

static jvalue mcapplication_check_self_permission(const PortJniCall *call) {
    return (jvalue){.z = permission_is_granted(call->arguments[0].l)};
}

static jvalue mcapplication_get_application_name(const PortJniCall *call) {
    (void)call;
    return (jvalue){.l = port_jni_string_from_utf8(PORT_APP_NAME)};
}

static jvalue mcapplication_get_application_version_code(const PortJniCall *call) {
    (void)call;
    return (jvalue){.i = PORT_APP_VERSION_CODE};
}

static jvalue mcapplication_get_cpu_core_count(const PortJniCall *call) {
    (void)call;
    return (jvalue){.i = PORT_CPU_CORE_COUNT};
}

static jvalue mcapplication_get_screen_density(const PortJniCall *call) {
    (void)call;
    return (jvalue){.f = 1};
}

static jvalue mcapplication_request_self_permission(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    jboolean granted = permission_is_granted(arguments[0].l);
    if (arguments[2].j) {
        jvalue callback_arguments[2] = {{.j = arguments[2].j}, {.z = granted}};
        port_async_schedule(&port_game_callback_permission, NULL, callback_arguments);
    }
    return (jvalue){0};
}

static const PortServiceMethod methods[] = {
    {{"com/miniclip/nativeJNI/cocojava", "getDeviceId", "()Ljava/lang/String;", 1},
     offline_device_id},
    {{"com/miniclip/udid/OpenUDID", "getOpenUDID", "()Ljava/lang/String;", 1},
     offline_device_id},
    {{"com/miniclip/nativeJNI/cocojava", "percentEncodeUTF8",
      "(Ljava/lang/String;)Ljava/lang/String;", 1},
     cocojava_percent_encode_utf8},
    {{"com/miniclip/info/DeviceInfo", "armv6", "()Z", 1}, port_services_empty_result},
    {{"android/os/Debug", "getNativeHeapAllocatedSize", "()J", 0},
     debug_get_native_heap_allocated_size},
    {{"com/miniclip/info/ApplicationInfo", "identifier", "()Ljava/lang/String;", 1},
     application_package_name},
    {{"com/miniclip/info/ApplicationInfo", "versionCode", "()Ljava/lang/String;", 1},
     application_info_version_code},
    {{"com/miniclip/info/ApplicationInfo", "versionName", "()Ljava/lang/String;", 1},
     application_version_name},
    {{"com/miniclip/info/DeviceInfo", "brandName", "()Ljava/lang/String;", 1},
     device_info_brand_name},
    {{"com/miniclip/info/DeviceInfo", "country", "()Ljava/lang/String;", 1}, device_info_country},
    {{"com/miniclip/info/DeviceInfo", "model", "()Ljava/lang/String;", 1}, device_info_model},
    {{"com/miniclip/info/DeviceInfo", "operatingSystemVersion", "()Ljava/lang/String;", 1},
     device_info_operating_system_version},
    {{"com/miniclip/info/DeviceInfo", "systemLanguage", "()Ljava/lang/String;", 1},
     device_info_system_language},
    {{"com/miniclip/info/NetworkInfo", "isNetworkAvailable", "()Z", 1},
     port_services_empty_result},
    {{"com/miniclip/info/ScreenInfo", "pixelsPerInch", "()F", 1}, screen_dpi},
    {{"com/miniclip/info/ScreenInfo", "screenHeight", "()F", 1}, screen_info_screen_height},
    {{"com/miniclip/info/ScreenInfo", "screenWidth", "()F", 1}, screen_info_screen_width},
    {{"com/miniclip/info/ScreenInfo", "windowHeight", "()F", 1}, screen_info_screen_height},
    {{"com/miniclip/info/ScreenInfo", "windowWidth", "()F", 1}, screen_info_screen_width},
    {{"com/miniclip/input/MCInput", "isTouchScreenAvailable", "()Z", 1},
     port_services_true_result},
    {{"com/miniclip/input/MCInput", "setAccelerometerEnabled", "(Z)V", 1},
     mcinput_set_accelerometer_enabled},
    {{"com/miniclip/nativeJNI/cocojava", "DeviceSupportsMultitouch", "()I", 1},
     cocojava_device_supports_multitouch},
    {{"com/miniclip/nativeJNI/cocojava", "availableMemoryOnDevice", "()F", 1},
     cocojava_available_memory_on_device},
    {{"com/miniclip/nativeJNI/cocojava", "getAndroidVersion", "()I", 1},
     cocojava_get_android_version},
    {{"com/miniclip/nativeJNI/cocojava", "getAppVersionNumber", "()Ljava/lang/String;", 1},
     application_version_name},
    {{"com/miniclip/nativeJNI/cocojava", "getDeviceInfo", "(Ljava/lang/String;)Ljava/lang/String;",
      1},
     cocojava_get_device_info},
    {{"com/miniclip/nativeJNI/cocojava", "getIPAddress", "(Z)Ljava/lang/String;", 1},
     port_services_empty_text},
    {{"com/miniclip/nativeJNI/cocojava", "getResourceString",
      "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", 1},
     cocojava_get_resource_string},
    {{"com/miniclip/nativeJNI/cocojava", "getTimezoneOffset", "()I", 1},
     cocojava_get_timezone_offset},
    {{"com/miniclip/platform/MCApplication", "checkSelfPermission", "(Ljava/lang/String;)Z", 1},
     mcapplication_check_self_permission},
    {{"com/miniclip/platform/MCApplication", "getApplicationName", "()Ljava/lang/String;", 1},
     mcapplication_get_application_name},
    {{"com/miniclip/platform/MCApplication", "getApplicationPackageName", "()Ljava/lang/String;",
      1},
     application_package_name},
    {{"com/miniclip/platform/MCApplication", "getApplicationVersionCode", "()I", 1},
     mcapplication_get_application_version_code},
    {{"com/miniclip/platform/MCApplication", "getCpuCoreCount", "()I", 1},
     mcapplication_get_cpu_core_count},
    {{"com/miniclip/platform/MCApplication", "getIntentDataString", "()Ljava/lang/String;", 1},
     port_services_empty_text},
    {{"com/miniclip/platform/MCApplication", "getScreenDPI", "()F", 1},
     screen_dpi},
    {{"com/miniclip/platform/MCApplication", "getScreenDensity", "()F", 1},
     mcapplication_get_screen_density},
    {{"com/miniclip/platform/MCApplication", "getStringValWithKeyFromMetadata",
      "(Ljava/lang/String;)Ljava/lang/String;", 1},
     port_services_empty_text},
    {{"com/miniclip/platform/MCApplication", "hasVibrator", "()Z", 1},
     port_services_empty_result},
    {{"com/miniclip/platform/MCApplication", "isDeviceOnline", "()Z", 1},
     port_services_empty_result},
    {{"com/miniclip/platform/MCApplication", "isDeviceRooted", "()Z", 1},
     port_services_empty_result},
    {{"com/miniclip/platform/MCApplication", "requestSelfPermission",
      "(Ljava/lang/String;Ljava/lang/String;J)V", 1},
     mcapplication_request_self_permission},
    {{"com/miniclip/platform/MCApplication", "requestSelfPermission",
      "(Ljava/lang/String;Ljava/lang/String;JLcom/miniclip/platform/"
      "MCApplication$RequestSelfPermissionCallback;)V",
      1},
     mcapplication_request_self_permission},
    {{"com/miniclip/platform/MCApplication", "requestSelfPermission",
      "(Ljava/lang/String;Ljava/lang/String;Lcom/miniclip/platform/"
      "MCApplication$RequestSelfPermissionCallback;)V",
      1},
     mcapplication_request_self_permission},
    {{"com/miniclip/platform/MCApplication", "setAutoRotate", "(I)V", 1},
     port_services_empty_result},
};
const PortServiceMethodTable port_app_info_methods = {methods,
                                                      sizeof(methods) / sizeof(methods[0])};
