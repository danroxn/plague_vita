#include "runtime/callbacks.h"

#include "compat/abi.h"
#include "compat/jni/runtime.h"
#include "runtime/module.h"

#include <stddef.h>

typedef jvalue (*CallbackInvoker)(uintptr_t address, jobject receiver, const jvalue *arguments);
struct GameCallback {
    GameCallbackInfo info;
    CallbackInvoker invoke;
    uintptr_t *bound_address;
};
typedef void (*NativeEmptyCallback)(JNIEnv *environment, jclass receiver);
static jvalue invoke_empty(uintptr_t address, jobject receiver, const jvalue *arguments) {
    (void)arguments;
    ((NativeEmptyCallback)address)(&port_jni, (jclass)receiver);
    return (jvalue){0};
}
typedef void (*NativeLongCallback)(JNIEnv *environment, jclass receiver, jlong native_id);
static jvalue invoke_long(uintptr_t address, jobject receiver, const jvalue *arguments) {
    ((NativeLongCallback)address)(&port_jni, (jclass)receiver, arguments[0].j);
    return (jvalue){0};
}
typedef void (*NativePermissionCallback)(JNIEnv *environment, jclass receiver, jlong request_id,
                                         jboolean granted);
static jvalue invoke_permission(uintptr_t address, jobject receiver, const jvalue *arguments) {
    ((NativePermissionCallback)address)(&port_jni, (jclass)receiver, arguments[0].j,
                                        arguments[1].z);
    return (jvalue){0};
}
typedef void (*NativeTextCallback)(JNIEnv *environment, jclass receiver, jstring text);
static jvalue invoke_text(uintptr_t address, jobject receiver, const jvalue *arguments) {
    ((NativeTextCallback)address)(&port_jni, (jclass)receiver, arguments[0].l);
    return (jvalue){0};
}
typedef void (*NativeTwoIntsCallback)(JNIEnv *environment, jclass receiver, jint request_id,
                                      jint result);
static jvalue invoke_two_ints(uintptr_t address, jobject receiver, const jvalue *arguments) {
    ((NativeTwoIntsCallback)address)(&port_jni, (jclass)receiver, arguments[0].i, arguments[1].i);
    return (jvalue){0};
}
typedef void (*NativeDateCallback)(JNIEnv *environment, jclass receiver, jint year,
                                   jint zero_based_month, jint day);
static jvalue invoke_date(uintptr_t address, jobject receiver, const jvalue *arguments) {
    ((NativeDateCallback)address)(&port_jni, (jclass)receiver, arguments[0].i, arguments[1].i,
                                  arguments[2].i);
    return (jvalue){0};
}
typedef void SFP (*NativeNetworkTimeCallback)(JNIEnv *environment, jclass receiver, jint request_id,
                                              jdouble offset, jint error);
static jvalue invoke_network_time(uintptr_t address, jobject receiver, const jvalue *arguments) {
    ((NativeNetworkTimeCallback)address)(&port_jni, (jclass)receiver, arguments[0].i,
                                         arguments[1].d, arguments[2].i);
    return (jvalue){0};
}
typedef void (*NativeHttpFailureCallback)(JNIEnv *environment, jclass receiver, jint request_id,
                                          jstring message, jint error);
static jvalue invoke_http_failure(uintptr_t address, jobject receiver, const jvalue *arguments) {
    ((NativeHttpFailureCallback)address)(&port_jni, (jclass)receiver, arguments[0].i,
                                         arguments[1].l, arguments[2].i);
    return (jvalue){0};
}
typedef void (*NativeSocketFailureCallback)(JNIEnv *environment, jclass receiver, jlong native_id,
                                            jint error, jstring message);
static jvalue invoke_socket_failure(uintptr_t address, jobject receiver, const jvalue *arguments) {
    ((NativeSocketFailureCallback)address)(&port_jni, (jclass)receiver, arguments[0].j,
                                           arguments[1].i, arguments[2].l);
    return (jvalue){0};
}
typedef void (*NativeGameResultsCallback)(JNIEnv *environment, jclass receiver,
                                          jobjectArray results, jint status, jint callback_id,
                                          jint page);
static jvalue invoke_game_results(uintptr_t address, jobject receiver, const jvalue *arguments) {
    ((NativeGameResultsCallback)address)(&port_jni, (jclass)receiver, arguments[0].l,
                                         arguments[1].i, arguments[2].i, arguments[3].i);
    return (jvalue){0};
}
typedef void (*NativeFacebookResultsCallback)(JNIEnv *environment, jclass receiver, jint request_id,
                                              jint status, jbyteArray bytes);
static jvalue invoke_facebook_results(uintptr_t address, jobject receiver,
                                      const jvalue *arguments) {
    ((NativeFacebookResultsCallback)address)(&port_jni, (jclass)receiver, arguments[0].i,
                                             arguments[1].i, arguments[2].l);
    return (jvalue){0};
}
typedef void (*NativeCloudResultCallback)(JNIEnv *environment, jclass receiver, jint callback_id,
                                          jint object_id, jint error, jstring data, jint revision,
                                          jstring name, jstring metadata, jint resolution);
static jvalue invoke_cloud_result(uintptr_t address, jobject receiver, const jvalue *arguments) {
    ((NativeCloudResultCallback)address)(
        &port_jni, (jclass)receiver, arguments[0].i, arguments[1].i, arguments[2].i, arguments[3].l,
        arguments[4].i, arguments[5].l, arguments[6].l, arguments[7].i);
    return (jvalue){0};
}
typedef void (*NativePopupButtonCallback)(JNIEnv *environment, jobject receiver, jlong native_id,
                                          jint button_index);
static jvalue invoke_popup_button(uintptr_t address, jobject receiver, const jvalue *arguments) {
    ((NativePopupButtonCallback)address)(&port_jni, (jobject)receiver, arguments[0].j,
                                         arguments[1].i);
    return (jvalue){0};
}
static uintptr_t lifecycle_start_address;
const GameCallback port_game_callback_lifecycle_start = {
    .info = {.symbol = "Java_com_miniclip_events_EventDispatcher_onStartNative",
             .argument_count = 0,
             .reference_mask = 0},
    .invoke = invoke_empty,
    .bound_address = &lifecycle_start_address,
};
static uintptr_t lifecycle_resume_address;
const GameCallback port_game_callback_lifecycle_resume = {
    .info = {.symbol = "Java_com_miniclip_events_EventDispatcher_onResumeNative",
             .argument_count = 0,
             .reference_mask = 0},
    .invoke = invoke_empty,
    .bound_address = &lifecycle_resume_address,
};
static uintptr_t lifecycle_pause_address;
const GameCallback port_game_callback_lifecycle_pause = {
    .info = {.symbol = "Java_com_miniclip_events_EventDispatcher_onPauseNative",
             .argument_count = 0,
             .reference_mask = 0},
    .invoke = invoke_empty,
    .bound_address = &lifecycle_pause_address,
};
static uintptr_t lifecycle_background_address;
const GameCallback port_game_callback_lifecycle_background = {
    .info = {.symbol = "Java_com_miniclip_events_EventDispatcher_onAppEnterBackgroundNative",
             .argument_count = 0,
             .reference_mask = 0},
    .invoke = invoke_empty,
    .bound_address = &lifecycle_background_address,
};
static uintptr_t lifecycle_foreground_address;
const GameCallback port_game_callback_lifecycle_foreground = {
    .info = {.symbol = "Java_com_miniclip_events_EventDispatcher_onAppEnterForegroundNative",
             .argument_count = 0,
             .reference_mask = 0},
    .invoke = invoke_empty,
    .bound_address = &lifecycle_foreground_address,
};
static uintptr_t popup_will_appear_address;
const GameCallback port_game_callback_popup_will_appear = {
    .info = {.symbol = "Java_com_miniclip_ui_AlertPopup_nativePopupWillAppear",
             .argument_count = 1,
             .reference_mask = 0},
    .invoke = invoke_long,
    .bound_address = &popup_will_appear_address,
};
static uintptr_t popup_did_appear_address;
const GameCallback port_game_callback_popup_did_appear = {
    .info = {.symbol = "Java_com_miniclip_ui_AlertPopup_nativePopupDidAppear",
             .argument_count = 1,
             .reference_mask = 0},
    .invoke = invoke_long,
    .bound_address = &popup_did_appear_address,
};
static uintptr_t popup_will_disappear_address;
const GameCallback port_game_callback_popup_will_disappear = {
    .info = {.symbol = "Java_com_miniclip_ui_AlertPopup_nativePopupWillDisappear",
             .argument_count = 1,
             .reference_mask = 0},
    .invoke = invoke_long,
    .bound_address = &popup_will_disappear_address,
};
static uintptr_t popup_did_disappear_address;
const GameCallback port_game_callback_popup_did_disappear = {
    .info = {.symbol = "Java_com_miniclip_ui_AlertPopup_nativePopupDidDisappear",
             .argument_count = 1,
             .reference_mask = 0},
    .invoke = invoke_long,
    .bound_address = &popup_did_disappear_address,
};
static uintptr_t popup_button_address;
const GameCallback port_game_callback_popup_button = {
    .info = {.symbol = "Java_com_miniclip_ui_AlertPopup_handleButtonPressNative",
             .argument_count = 2,
             .reference_mask = 0},
    .invoke = invoke_popup_button,
    .bound_address = &popup_button_address,
};
static uintptr_t keyboard_response_address;
const GameCallback port_game_callback_keyboard_response = {
    .info = {.symbol = "Java_com_miniclip_input_MCKeyboard_MkeyboardInputResponse",
             .argument_count = 1,
             .reference_mask = (1u << 0)},
    .invoke = invoke_text,
    .bound_address = &keyboard_response_address,
};
static uintptr_t keyboard_canceled_address;
const GameCallback port_game_callback_keyboard_canceled = {
    .info = {.symbol = "Java_com_miniclip_input_MCKeyboard_MkeyboardInputCanceled",
             .argument_count = 1,
             .reference_mask = (1u << 0)},
    .invoke = invoke_text,
    .bound_address = &keyboard_canceled_address,
};
static uintptr_t keyboard_closed_address;
const GameCallback port_game_callback_keyboard_closed = {
    .info = {.symbol = "Java_com_miniclip_input_MCKeyboard_MkeyboardInputClosed",
             .argument_count = 1,
             .reference_mask = (1u << 0)},
    .invoke = invoke_text,
    .bound_address = &keyboard_closed_address,
};
static uintptr_t date_response_address;
const GameCallback port_game_callback_date_response = {
    .info = {.symbol = "Java_com_miniclip_nativeJNI_CocoJNI_MdatePickerResponce",
             .argument_count = 3,
             .reference_mask = 0},
    .invoke = invoke_date,
    .bound_address = &date_response_address,
};
static uintptr_t date_closed_address;
const GameCallback port_game_callback_date_closed = {
    .info = {.symbol = "Java_com_miniclip_nativeJNI_CocoJNI_MdatePickerClosed",
             .argument_count = 0,
             .reference_mask = 0},
    .invoke = invoke_empty,
    .bound_address = &date_closed_address,
};
static uintptr_t message_button_address;
const GameCallback port_game_callback_message_button = {
    .info = {.symbol = "Java_com_miniclip_nativeJNI_CocoJNI_MonMessageBoxButtonPressed",
             .argument_count = 2,
             .reference_mask = 0},
    .invoke = invoke_two_ints,
    .bound_address = &message_button_address,
};
static uintptr_t network_time_address;
const GameCallback port_game_callback_network_time = {
    .info = {.symbol = "Java_com_miniclip_nativeJNI_CocoJNI_MnetworkTimeResponce",
             .argument_count = 3,
             .reference_mask = 0},
    .invoke = invoke_network_time,
    .bound_address = &network_time_address,
};
static uintptr_t ping_address;
const GameCallback port_game_callback_ping = {
    .info = {.symbol = "Java_com_miniclip_nativeJNI_CocoJNI_MsimplePingResponce",
             .argument_count = 2,
             .reference_mask = 0},
    .invoke = invoke_two_ints,
    .bound_address = &ping_address,
};
static uintptr_t html_loaded_address;
const GameCallback port_game_callback_html_loaded = {
    .info = {.symbol = "Java_com_miniclip_ui_HTMLDialog_dismissLoadingPopupNative",
             .argument_count = 1,
             .reference_mask = 0},
    .invoke = invoke_long,
    .bound_address = &html_loaded_address,
};
static uintptr_t html_back_address;
const GameCallback port_game_callback_html_back = {
    .info = {.symbol = "Java_com_miniclip_ui_HTMLDialog_handleBackButtonPressNative",
             .argument_count = 1,
             .reference_mask = 0},
    .invoke = invoke_long,
    .bound_address = &html_back_address,
};
static uintptr_t http_failed_address;
const GameCallback port_game_callback_http_failed = {
    .info = {.symbol = "Java_com_miniclip_network_HttpConnection_downloadFailed",
             .argument_count = 3,
             .reference_mask = (1u << 1)},
    .invoke = invoke_http_failure,
    .bound_address = &http_failed_address,
};
static uintptr_t socket_disconnected_address;
const GameCallback port_game_callback_socket_disconnected = {
    .info = {.symbol = "Java_com_miniclip_network_JavaSocket_onDisconnect",
             .argument_count = 3,
             .reference_mask = (1u << 2)},
    .invoke = invoke_socket_failure,
    .bound_address = &socket_disconnected_address,
};
static uintptr_t achievements_address;
const GameCallback port_game_callback_achievements = {
    .info = {.symbol =
                 "Java_com_miniclip_gameservices_GameServices_MonGameServicesAchievementsLoaded",
             .argument_count = 4,
             .reference_mask = (1u << 0)},
    .invoke = invoke_game_results,
    .bound_address = &achievements_address,
};
static uintptr_t leaderboard_address;
const GameCallback port_game_callback_leaderboard = {
    .info =
        {.symbol =
             "Java_com_miniclip_gameservices_GameServices_MonGameServicesLeaderboardScoredLoaded",

         .argument_count = 4,
         .reference_mask = (1u << 0)},
    .invoke = invoke_game_results,
    .bound_address = &leaderboard_address,
};
static uintptr_t game_login_address;
const GameCallback port_game_callback_game_login = {
    .info = {.symbol = "Java_com_miniclip_gameservices_GameServices_MonGameServicesLoginComplete",
             .argument_count = 0,
             .reference_mask = 0},
    .invoke = invoke_empty,
    .bound_address = &game_login_address,
};
static uintptr_t facebook_login_address;
const GameCallback port_game_callback_facebook_login = {
    .info = {.symbol = "Java_com_miniclip_facebook_FacebookHelper_MfacebookLoginComplete",
             .argument_count = 0,
             .reference_mask = 0},
    .invoke = invoke_empty,
    .bound_address = &facebook_login_address,
};
static uintptr_t facebook_requests_checked_address;
const GameCallback port_game_callback_facebook_requests_checked = {
    .info = {.symbol = "Java_com_miniclip_facebook_FacebookHelper_MfacebookCheckRequestsComplete",
             .argument_count = 3,
             .reference_mask = (1u << 2)},
    .invoke = invoke_facebook_results,
    .bound_address = &facebook_requests_checked_address,
};
static uintptr_t facebook_share_canceled_address;
const GameCallback port_game_callback_facebook_share_canceled = {
    .info = {.symbol = "Java_com_miniclip_facebook_FacebookHelper_MfacebookShareCanceled",
             .argument_count = 0,
             .reference_mask = 0},
    .invoke = invoke_empty,
    .bound_address = &facebook_share_canceled_address,
};
static uintptr_t facebook_request_address;
const GameCallback port_game_callback_facebook_request = {
    .info = {.symbol = "Java_com_miniclip_facebook_FacebookHelper_MfacebookRequestComplete",
             .argument_count = 3,
             .reference_mask = (1u << 2)},
    .invoke = invoke_facebook_results,
    .bound_address = &facebook_request_address,
};
static uintptr_t ad_closed_address;
const GameCallback port_game_callback_ad_closed = {
    .info = {.symbol = "Java_com_miniclip_videoads_MCVideoAds_adsClosed",
             .argument_count = 0,
             .reference_mask = 0},
    .invoke = invoke_empty,
    .bound_address = &ad_closed_address,
};
static uintptr_t permission_address;
const GameCallback port_game_callback_permission = {
    .info = {.symbol = "Java_com_miniclip_platform_MCApplication_onRequestSelfPermissionResult",
             .argument_count = 2,
             .reference_mask = 0},
    .invoke = invoke_permission,
    .bound_address = &permission_address,
};
static uintptr_t runnable_address;
const GameCallback port_game_callback_runnable = {
    .info = {.symbol = "Java_com_miniclip_utils_NativeRunnable_runNative",
             .argument_count = 1,
             .reference_mask = 0},
    .invoke = invoke_long,
    .bound_address = &runnable_address,
};
static uintptr_t cloud_loaded_address;
const GameCallback port_game_callback_cloud_loaded = {
    .info = {.symbol = "Java_com_miniclip_cloudservices_CloudServices_MonCloudServicesDataLoaded",
             .argument_count = 8,
             .reference_mask = (1u << 3) | (1u << 5) | (1u << 6)},
    .invoke = invoke_cloud_result,
    .bound_address = &cloud_loaded_address,
};
const GameCallbackInfo *port_game_callback_info(const GameCallback *callback) {
    return &callback->info;
}
jvalue port_game_callback_invoke(const GameCallback *callback, jobject receiver,
                                 const jvalue *arguments) {
    if (!*callback->bound_address)
        *callback->bound_address = port_module_require_symbol(callback->info.symbol);
    return callback->invoke(*callback->bound_address, receiver, arguments);
}
jobjectArray port_game_purchase_consumables(jobjectArray product_ids) {
    typedef jobjectArray (*NativeCallback)(JNIEnv *, jclass, jobjectArray);
    static NativeCallback callback;
    if (!callback)
        callback = (NativeCallback)port_module_require_symbol(
            "Java_com_miniclip_inapppurchases_MCInAppPurchases_getConsumableSkus");
    return callback(&port_jni, NULL, product_ids);
}
void port_game_purchase_registered(jboolean success, jobjectArray failed_ids) {
    typedef void (*NativeCallback)(JNIEnv *, jclass, jboolean, jobjectArray);
    static NativeCallback callback;
    if (!callback)
        callback = (NativeCallback)port_module_require_symbol(
            "Java_com_miniclip_inapppurchases_MCInAppPurchases_onRegisterProviderResult");
    callback(&port_jni, NULL, success, failed_ids);
}
void port_game_purchases_restored(jboolean success, jobjectArray product_ids, jobjectArray receipts,
                                  jobjectArray signatures) {
    typedef void (*NativeCallback)(JNIEnv *, jclass, jboolean, jobjectArray, jobjectArray,
                                   jobjectArray);
    static NativeCallback callback;
    if (!callback)
        callback = (NativeCallback)port_module_require_symbol(
            "Java_com_miniclip_inapppurchases_MCInAppPurchases_onPurchasesRestoredResult");
    callback(&port_jni, NULL, success, product_ids, receipts, signatures);
}
void port_game_purchase_response(jint status, jstring product_id, jstring receipt,
                                 jstring signature) {
    typedef void (*NativeCallback)(JNIEnv *, jclass, jint, jstring, jstring, jstring);
    static NativeCallback callback;
    if (!callback)
        callback = (NativeCallback)port_module_require_symbol(
            "Java_com_miniclip_inapppurchases_MCInAppPurchases_onPurchaseResponse");
    callback(&port_jni, NULL, status, product_id, receipt, signature);
}
