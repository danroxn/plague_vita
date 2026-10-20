#ifndef PLAGUE_RUNTIME_CALLBACKS_H
#define PLAGUE_RUNTIME_CALLBACKS_H
#include <jni.h>
#include <stdint.h>
typedef struct GameCallback GameCallback;
typedef struct {
    const char *symbol;
    unsigned argument_count;
    unsigned reference_mask;
} GameCallbackInfo;
const GameCallbackInfo *port_game_callback_info(const GameCallback *callback);
jvalue port_game_callback_invoke(const GameCallback *callback, jobject receiver,
                                 const jvalue *arguments);
extern const GameCallback port_game_callback_lifecycle_start;
extern const GameCallback port_game_callback_lifecycle_resume;
extern const GameCallback port_game_callback_lifecycle_pause;
extern const GameCallback port_game_callback_lifecycle_background;
extern const GameCallback port_game_callback_lifecycle_foreground;
extern const GameCallback port_game_callback_popup_will_appear;
extern const GameCallback port_game_callback_popup_did_appear;
extern const GameCallback port_game_callback_popup_will_disappear;
extern const GameCallback port_game_callback_popup_did_disappear;
extern const GameCallback port_game_callback_popup_button;
extern const GameCallback port_game_callback_keyboard_response;
extern const GameCallback port_game_callback_keyboard_canceled;
extern const GameCallback port_game_callback_keyboard_closed;
extern const GameCallback port_game_callback_date_response;
extern const GameCallback port_game_callback_date_closed;
extern const GameCallback port_game_callback_message_button;
extern const GameCallback port_game_callback_network_time;
extern const GameCallback port_game_callback_ping;
extern const GameCallback port_game_callback_html_loaded;
extern const GameCallback port_game_callback_html_back;
extern const GameCallback port_game_callback_http_failed;
extern const GameCallback port_game_callback_socket_disconnected;
extern const GameCallback port_game_callback_achievements;
extern const GameCallback port_game_callback_leaderboard;
extern const GameCallback port_game_callback_game_login;
extern const GameCallback port_game_callback_facebook_login;
extern const GameCallback port_game_callback_facebook_requests_checked;
extern const GameCallback port_game_callback_facebook_share_canceled;
extern const GameCallback port_game_callback_facebook_request;
extern const GameCallback port_game_callback_ad_closed;
extern const GameCallback port_game_callback_permission;
extern const GameCallback port_game_callback_runnable;
extern const GameCallback port_game_callback_cloud_loaded;
jobjectArray port_game_purchase_consumables(jobjectArray product_ids);
void port_game_purchase_registered(jboolean success, jobjectArray failed_ids);
void port_game_purchases_restored(jboolean success, jobjectArray product_ids, jobjectArray receipts,
                                  jobjectArray signatures);
void port_game_purchase_response(jint status, jstring product_id, jstring receipt,
                                 jstring signature);
#endif
