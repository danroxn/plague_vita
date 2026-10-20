#include "services/offline.h"

#include "app/log.h"
#include "app/options.h"
#include "services/methods.h"
#include "runtime/callbacks.h"
#include "compat/jni/runtime.h"
#include "compat/jni/strings.h"
#include "services/async.h"

#include <stddef.h>
#include <string.h>

// The Android library still calls this API, but the offline port has no
// SharedPreferences storage. Keep the startup marker used by the load test.
static jvalue cocojava_shared_preferences_get_int(const PortJniCall *call) {
    char key[256];
    port_jni_string_copy_utf8(call->arguments[0].l, key, sizeof(key));
    if (!strcmp(key, "PLAGUE_RUN_COUNT"))
        port_log_debug("SCENE main_menu run_count=0");
    return (jvalue){1};
}

static jvalue obsolete_get_ads_disabled(const PortJniCall *call) {
    (void)call;
    return (jvalue){.i = port_app_options.unlock_all};
}

static jvalue unavailable_ad_providers(const PortJniCall *call) {
    if (!strcmp(call->return_type, "Ljava/lang/String;"))
        return (jvalue){.l = port_jni_string_from_utf8("{}")};
    return (jvalue){0};
}
static jvalue unavailable_display_ad(const PortJniCall *call) {
    port_async_schedule(&port_game_callback_ad_closed, NULL, NULL);
    return port_services_unavailable_result(call);
}

enum { OFFLINE_COMPLETION_ERROR = 1, EMPTY_RESULT_COUNT = 0 };

static void schedule_cloud_unavailable(jint service_id, jint request_id, jint resolution) {
    jstring empty_text = port_jni_string_from_utf8("");
    const jvalue callback_arguments[] = {
        {.i = service_id}, {.i = request_id},         {.i = OFFLINE_COMPLETION_ERROR},
        {.l = empty_text}, {.i = EMPTY_RESULT_COUNT}, {.l = empty_text},
        {.l = empty_text}, {.i = resolution},
    };
    port_async_schedule(&port_game_callback_cloud_loaded, NULL, callback_arguments);
    port_jni->DeleteLocalRef(&port_jni, empty_text);
}

static jvalue cloud_services_load(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    schedule_cloud_unavailable(arguments[0].i, arguments[1].i, 0);
    return (jvalue){0};
}

static jvalue cloud_services_resolve(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    schedule_cloud_unavailable(arguments[0].i, arguments[1].i, arguments[4].i);
    return (jvalue){0};
}

static jvalue facebook_helper_face_book_authorize(const PortJniCall *call) {
    (void)call;
    port_async_schedule(&port_game_callback_facebook_login, NULL, NULL);
    return (jvalue){0};
}

static jvalue facebook_helper_face_book_check_received_requests(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    jbyteArray empty = port_jni->NewByteArray(&port_jni, 0);
    jvalue callback_arguments[3] = {{.i = arguments[0].i}, {.i = 0}, {.l = empty}};
    port_async_schedule(&port_game_callback_facebook_requests_checked, NULL, callback_arguments);
    port_jni->DeleteLocalRef(&port_jni, empty);
    return (jvalue){0};
}

static jvalue facebook_helper_face_book_dialog(const PortJniCall *call) {
    (void)call;
    port_async_schedule(&port_game_callback_facebook_share_canceled, NULL, NULL);
    return (jvalue){0};
}

static jvalue facebook_helper_face_book_request(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    jbyteArray empty = port_jni->NewByteArray(&port_jni, 0);
    jvalue callback_arguments[3] = {{.i = arguments[1].i}, {.i = 0}, {.l = empty}};
    port_async_schedule(&port_game_callback_facebook_request, NULL, callback_arguments);
    port_jni->DeleteLocalRef(&port_jni, empty);
    return (jvalue){0};
}

static jvalue game_services_load_achievements(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    jclass string_class = port_jni->FindClass(&port_jni, "java/lang/String");
    jobjectArray empty = port_jni->NewObjectArray(&port_jni, 0, string_class, NULL);
    port_jni->DeleteLocalRef(&port_jni, string_class);
    jvalue callback_arguments[4] = {
        {.l = empty}, {.i = 0}, {.i = arguments[0].i}, {.i = arguments[1].i}};
    port_async_schedule(&port_game_callback_achievements, NULL, callback_arguments);
    port_jni->DeleteLocalRef(&port_jni, empty);
    return (jvalue){0};
}

static jvalue game_services_load_leaderboard_scores(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    jclass string_class = port_jni->FindClass(&port_jni, "java/lang/String");
    jobjectArray empty = port_jni->NewObjectArray(&port_jni, 0, string_class, NULL);
    port_jni->DeleteLocalRef(&port_jni, string_class);
    jvalue callback_arguments[4] = {
        {.l = empty}, {.i = 0}, {.i = arguments[0].i}, {.i = arguments[1].i}};
    port_async_schedule(&port_game_callback_leaderboard, NULL, callback_arguments);
    port_jni->DeleteLocalRef(&port_jni, empty);
    return (jvalue){0};
}

static jvalue game_services_login(const PortJniCall *call) {
    (void)call;
    port_async_schedule(&port_game_callback_game_login, NULL, NULL);
    return (jvalue){0};
}

static jvalue cocojava_ntp_java_get_offset_from_server_list_async(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    jvalue callback_arguments[3] = {
        {.i = arguments[1].i}, {.d = 0}, {.i = OFFLINE_COMPLETION_ERROR}};
    port_async_schedule(&port_game_callback_network_time, NULL, callback_arguments);
    return (jvalue){0};
}

static jvalue cocojava_ping_java_simple_ping_async(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    jvalue callback_arguments[2] = {{.i = arguments[1].i}, {.i = OFFLINE_COMPLETION_ERROR}};
    port_async_schedule(&port_game_callback_ping, NULL, callback_arguments);
    return (jvalue){0};
}

static const PortServiceMethod offline_entries[] = {
    {{"com/miniclip/nativeJNI/cocojava", "SharedPreferences_getInt", "(Ljava/lang/String;)I", 1},
     cocojava_shared_preferences_get_int},
    {{"com/miniclip/nativeJNI/cocojava", "SharedPreferences_getString",
      "(Ljava/lang/String;)Ljava/lang/String;", 1},
     port_services_empty_text},
    {{"com/miniclip/nativeJNI/cocojava", "SharedPreferences_setInt", "(Ljava/lang/String;I)V", 1},
     port_services_empty_result},
    {{"com/miniclip/nativeJNI/cocojava", "SharedPreferences_setString",
      "(Ljava/lang/String;Ljava/lang/String;)V", 1},
     port_services_empty_result},
    {{"com/miniclip/obsolete/Obsolete", "getAdsDisabled", "()I", 1}, obsolete_get_ads_disabled},
    {{"com/miniclip/obsolete/Obsolete", "getAdsDisabled", "(Landroid/content/Context;)I", 1},
     obsolete_get_ads_disabled},
    {{"com/miniclip/obsolete/Obsolete", "permanentlyRemoveAds", "()V", 1},
     port_services_empty_result},
    {{"com/miniclip/framework/ExternalApplication", "launch", "(Ljava/lang/String;)V", 1},
     port_services_unavailable_result},
    {{"com/miniclip/nativeJNI/cocojava", "openLink", "(Ljava/lang/String;)V", 1},
     port_services_unavailable_result},
    {{"com/miniclip/obsolete/Obsolete", "StartActivity", "(Ljava/lang/String;)V", 1},
     port_services_unavailable_result},
    {{"com/miniclip/appsharing/AppSharing", "shareText", "(Ljava/lang/String;)Z", 1},
     port_services_unavailable_result},
    {{"com/miniclip/facebook/FacebookHelper", "faceBook_getAccessToken", "()Ljava/lang/String;", 1},
     port_services_unavailable_result},
    {{"com/miniclip/facebook/FacebookHelper", "faceBook_hasPermission", "(Ljava/lang/String;)I", 1},
     port_services_unavailable_result},
    {{"com/miniclip/facebook/FacebookHelper", "faceBook_isSessionValid", "()I", 1},
     port_services_unavailable_result},
    {{"com/miniclip/facebook/FacebookHelper", "faceBook_logout", "()V", 1}, port_services_unavailable_result},
    {{"com/miniclip/gameservices/GameServices", "getCurrentPlayerName", "()Ljava/lang/String;", 1},
     port_services_unavailable_result},
    {{"com/miniclip/gameservices/GameServices", "isSignedIn", "()I", 1}, port_services_unavailable_result},
    {{"com/miniclip/gameservices/GameServices", "logout", "()V", 1}, port_services_unavailable_result},
    {{"com/miniclip/gameservices/GameServices", "showAchievements", "()V", 1}, port_services_unavailable_result},
    {{"com/miniclip/gameservices/GameServices", "showLeaderboard", "(Ljava/lang/String;)V", 1},
     port_services_unavailable_result},
    {{"com/miniclip/gameservices/GameServices", "showLeaderboards", "()V", 1}, port_services_unavailable_result},
    {{"com/miniclip/gameservices/GameServices", "updateAchievement", "(Ljava/lang/String;F)V", 1},
     port_services_unavailable_result},
    {{"com/miniclip/gameservices/GameServices", "updateAchievement",
      "(Ljava/lang/String;FLjava/lang/Object;)V", 1},
     port_services_unavailable_result},
    {{"com/miniclip/gameservices/GameServices", "updateScore", "(Ljava/lang/String;I)V", 1},
     port_services_unavailable_result},
    {{"com/miniclip/gameservices/GameServices", "updateScore",
      "(Ljava/lang/String;ILjava/lang/Object;)V", 1},
     port_services_unavailable_result},
    {{"com/miniclip/nativeJNI/GooglePlayServices", "getCurrentPlayerName", "()Ljava/lang/String;",
      0},
     port_services_unavailable_result},
    {{"com/miniclip/nativeJNI/GooglePlayServices", "isSignedIn", "()Z", 0}, port_services_unavailable_result},
    {{"com/miniclip/nativeJNI/GooglePlayServices", "showAchievements", "()V", 0}, port_services_unavailable_result},
    {{"com/miniclip/nativeJNI/GooglePlayServices", "showLeaderboard", "(Ljava/lang/String;)V", 0},
     port_services_unavailable_result},
    {{"com/miniclip/nativeJNI/GooglePlayServices", "showLeaderboards", "()V", 0}, port_services_unavailable_result},
    {{"com/miniclip/nativeJNI/GooglePlayServices", "signOut", "()V", 0}, port_services_unavailable_result},
    {{"com/miniclip/obsolete/Obsolete", "disableAds", "()V", 1}, port_services_unavailable_result},
    {{"com/miniclip/obsolete/Obsolete", "enableAds", "()V", 1}, port_services_unavailable_result},
    {{"com/miniclip/obsolete/Obsolete", "enableAdsWithPositionForGameplay", "(FFFF)V", 1},
     port_services_unavailable_result},
    {{"com/miniclip/obsolete/Obsolete", "enableAdsWithPositionForGameplayGivenWidth", "(FFFFF)V",
      1},
     port_services_unavailable_result},
    {{"com/miniclip/obsolete/Obsolete", "enableAdsWithPositionForMenu", "(FFFF)V", 1},
     port_services_unavailable_result},
    {{"com/miniclip/obsolete/Obsolete", "enableAdsWithPositionForMenuGivenWidth", "(FFFFF)V", 1},
     port_services_unavailable_result},
    {{"com/miniclip/obsolete/Obsolete", "hideAds", "()V", 1}, port_services_unavailable_result},
    {{"com/miniclip/obsolete/Obsolete", "isFirstInstall", "()Z", 1}, port_services_unavailable_result},
    {{"com/miniclip/obsolete/Obsolete", "showAds", "()V", 1}, port_services_unavailable_result},
    {{"com/miniclip/videoads/MCVideoAds", "displayAd", "()V", 1}, unavailable_display_ad},
    {{"com/miniclip/videoads/MCVideoAds", "isAdAvailable", "()Z", 1}, port_services_unavailable_result},
    {{"com/miniclip/videoads/MCVideoAds", "isRequestingVideo", "()Z", 1}, port_services_unavailable_result},
    {{"com/miniclip/videoads/MCVideoAds", "registerListOfAdProviders",
      "(Ljava/lang/String;)Ljava/lang/String;", 1},
     unavailable_ad_providers},
    {{"com/miniclip/cloudservices/CloudServices", "load", "(II)V", 1}, cloud_services_load},
    {{"com/miniclip/cloudservices/CloudServices", "resolve",
      "(IILjava/lang/String;Ljava/lang/String;I)V", 1},
     cloud_services_resolve},
    {{"com/miniclip/cloudservices/CloudServices", "update", "(IILjava/lang/String;)V", 1},
     cloud_services_load},
    {{"com/miniclip/facebook/FacebookHelper", "faceBook_authorize", "(Ljava/lang/String;)V", 1},
     facebook_helper_face_book_authorize},
    {{"com/miniclip/facebook/FacebookHelper", "faceBook_checkReceivedRequests", "(I)V", 1},
     facebook_helper_face_book_check_received_requests},
    {{"com/miniclip/facebook/FacebookHelper", "faceBook_dialog",
      "(Ljava/lang/String;Ljava/lang/String;)V", 1},
     facebook_helper_face_book_dialog},
    {{"com/miniclip/facebook/FacebookHelper", "faceBook_dialogWithLogin",
      "(Ljava/lang/String;Ljava/lang/String;)V", 1},
     facebook_helper_face_book_authorize},
    {{"com/miniclip/facebook/FacebookHelper", "faceBook_reauthorizeWithPublishPermissions",
      "(Ljava/lang/String;)V", 1},
     facebook_helper_face_book_authorize},
    {{"com/miniclip/facebook/FacebookHelper", "faceBook_request", "(Ljava/lang/String;I)V", 1},
     facebook_helper_face_book_request},
    {{"com/miniclip/facebook/FacebookHelper", "faceBook_sendRequest",
      "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V", 1},
     facebook_helper_face_book_dialog},
    {{"com/miniclip/gameservices/GameServices", "loadAchievements", "(II)V", 1},
     game_services_load_achievements},
    {{"com/miniclip/gameservices/GameServices", "loadLeaderboardScores",
      "(IILjava/lang/String;III)V", 1},
     game_services_load_leaderboard_scores},
    {{"com/miniclip/gameservices/GameServices", "login", "()V", 1}, game_services_login},
    {{"com/miniclip/nativeJNI/cocojava", "NTP_JAVA_getOffsetFromServer", "(Ljava/lang/String;)D",
      1},
     port_services_empty_result},
    {{"com/miniclip/nativeJNI/cocojava", "NTP_JAVA_getOffsetFromServerListAsync",
      "(Ljava/lang/String;II)V", 1},
     cocojava_ntp_java_get_offset_from_server_list_async},
    {{"com/miniclip/nativeJNI/cocojava", "PING_JAVA_simplePingAsync", "(Ljava/lang/String;II)V", 1},
     cocojava_ping_java_simple_ping_async},
    {{"com/miniclip/nativeJNI/cocojava", "sendEmail",
      "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V", 1},
     port_services_unavailable_result},
    {{"com/miniclip/nativeJNI/cocojava", "sendSMS", "(Ljava/lang/String;)V", 1}, port_services_unavailable_result},
    {{"com/miniclip/nativeJNI/cocojava", "vibrateForMS", "(I)V", 1}, port_services_unavailable_result},
};
const PortServiceMethodTable port_offline_methods = {
    offline_entries, sizeof(offline_entries) / sizeof(offline_entries[0])};

static const struct {
    const char *owner;
    const char *name;
    PortJniHandler handler;
} unavailable_policy[] = {
    {"com/miniclip/appsharing/AppSharing", "shareText", port_services_unavailable_result},
    {"com/miniclip/facebook/FacebookHelper", "faceBook_getAccessToken", port_services_unavailable_result},
    {"com/miniclip/facebook/FacebookHelper", "faceBook_hasPermission", port_services_unavailable_result},
    {"com/miniclip/facebook/FacebookHelper", "faceBook_isSessionValid", port_services_unavailable_result},
    {"com/miniclip/facebook/FacebookHelper", "faceBook_like", port_services_unavailable_result},
    {"com/miniclip/facebook/FacebookHelper", "faceBook_logout", port_services_unavailable_result},
    {"com/miniclip/gameservices/GameServices", "getCurrentPlayerName", port_services_unavailable_result},
    {"com/miniclip/gameservices/GameServices", "isSignedIn", port_services_unavailable_result},
    {"com/miniclip/gameservices/GameServices", "logout", port_services_unavailable_result},
    {"com/miniclip/gameservices/GameServices", "showAchievements", port_services_unavailable_result},
    {"com/miniclip/gameservices/GameServices", "showLeaderboard", port_services_unavailable_result},
    {"com/miniclip/gameservices/GameServices", "showLeaderboards", port_services_unavailable_result},
    {"com/miniclip/gameservices/GameServices", "updateAchievement", port_services_unavailable_result},
    {"com/miniclip/gameservices/GameServices", "updateScore", port_services_unavailable_result},
    {"com/miniclip/nativeJNI/GooglePlayServices", "GPM_getCurrentPerson", port_services_unavailable_result},
    {"com/miniclip/nativeJNI/GooglePlayServices", "GPM_getToken", port_services_unavailable_result},
    {"com/miniclip/nativeJNI/GooglePlayServices", "GPM_getVisiblePeople", port_services_unavailable_result},
    {"com/miniclip/nativeJNI/GooglePlayServices", "GPM_gift", port_services_unavailable_result},
    {"com/miniclip/nativeJNI/GooglePlayServices", "GPM_giftAll", port_services_unavailable_result},
    {"com/miniclip/nativeJNI/GooglePlayServices", "GPM_invite", port_services_unavailable_result},
    {"com/miniclip/nativeJNI/GooglePlayServices", "GPM_inviteAll", port_services_unavailable_result},
    {"com/miniclip/nativeJNI/GooglePlayServices", "GPM_isConnected", port_services_unavailable_result},
    {"com/miniclip/nativeJNI/GooglePlayServices", "GPM_share", port_services_unavailable_result},
    {"com/miniclip/nativeJNI/GooglePlayServices", "GPM_showAchievements", port_services_unavailable_result},
    {"com/miniclip/nativeJNI/GooglePlayServices", "GPM_signIn", port_services_unavailable_result},
    {"com/miniclip/nativeJNI/GooglePlayServices", "GPM_signOut", port_services_unavailable_result},
    {"com/miniclip/nativeJNI/GooglePlayServices", "GPM_unlockAchievement",
     port_services_unavailable_result},
    {"com/miniclip/nativeJNI/GooglePlayServices", "canShare", port_services_unavailable_result},
    {"com/miniclip/nativeJNI/GooglePlayServices", "connect", port_services_unavailable_result},
    {"com/miniclip/nativeJNI/GooglePlayServices", "disconnect", port_services_unavailable_result},
    {"com/miniclip/nativeJNI/GooglePlayServices", "getGoogleAdvertisingID",
     port_services_unavailable_result},
    {"com/miniclip/nativeJNI/GooglePlayServices", "isConnecting", port_services_unavailable_result},
    {"com/miniclip/nativeJNI/GooglePlayServices", "getCurrentPlayerName", port_services_unavailable_result},
    {"com/miniclip/nativeJNI/GooglePlayServices", "isSignedIn", port_services_unavailable_result},
    {"com/miniclip/nativeJNI/GooglePlayServices", "showAchievements", port_services_unavailable_result},
    {"com/miniclip/nativeJNI/GooglePlayServices", "showLeaderboard", port_services_unavailable_result},
    {"com/miniclip/nativeJNI/GooglePlayServices", "showLeaderboards", port_services_unavailable_result},
    {"com/miniclip/nativeJNI/GooglePlayServices", "signOut", port_services_unavailable_result},
    {"com/miniclip/notification/BootReceiver", "cancelNotification", port_services_unavailable_result},
    {"com/miniclip/notification/BootReceiver", "createNotification", port_services_unavailable_result},
    {"com/miniclip/obsolete/Obsolete", "disableAds", port_services_unavailable_result},
    {"com/miniclip/obsolete/Obsolete", "enableAds", port_services_unavailable_result},
    {"com/miniclip/obsolete/Obsolete", "enableAdsWithPositionForGameplay",
     port_services_unavailable_result},
    {"com/miniclip/obsolete/Obsolete", "enableAdsWithPositionForGameplayGivenWidth",
     port_services_unavailable_result},
    {"com/miniclip/obsolete/Obsolete", "enableAdsWithPositionForMenu", port_services_unavailable_result},
    {"com/miniclip/obsolete/Obsolete", "enableAdsWithPositionForMenuGivenWidth",
     port_services_unavailable_result},
    {"com/miniclip/obsolete/Obsolete", "hideAds", port_services_unavailable_result},
    {"com/miniclip/obsolete/Obsolete", "showAds", port_services_unavailable_result},
    {"com/miniclip/obsolete/Obsolete", "isFirstInstall", port_services_unavailable_result},
    {"com/miniclip/obsolete/Obsolete", "getBuildString", port_services_unavailable_result},
    {"com/miniclip/obsolete/Obsolete", "getShareTextWithSign:won:", port_services_unavailable_result},
    {"com/miniclip/videoads/MCVideoAds", "displayAd", unavailable_display_ad},
    {"com/miniclip/videoads/MCVideoAds", "isAdAvailable", port_services_unavailable_result},
    {"com/miniclip/videoads/MCVideoAds", "isRequestingVideo", port_services_unavailable_result},
    {"com/miniclip/videoads/MCVideoAds", "registerListOfAdProviders", unavailable_ad_providers},
};
PortJniHandler port_offline_resolve_fallback(const PortJniMethodKey *method) {
    for (size_t i = 0; i < sizeof(unavailable_policy) / sizeof(unavailable_policy[0]); i++)
        if (!strcmp(unavailable_policy[i].owner, method->owner) &&
            !strcmp(unavailable_policy[i].name, method->name))
            return unavailable_policy[i].handler;
    return NULL;
}
