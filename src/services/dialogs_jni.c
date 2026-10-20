#include "services/methods.h"

#include "runtime/callbacks.h"
#include "compat/jni/runtime.h"
#include "compat/jni/strings.h"
#include "compat/utf8.h"
#include "runtime/metadata.h"
#include "services/async.h"
#include "services/dialogs.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void html_to_text(const char *html_utf8, char *plain_utf8, size_t capacity_bytes) {
    size_t written_bytes = 0;
    int in_tag = 0;
    for (const char *cursor = html_utf8; *cursor && written_bytes + 1 < capacity_bytes; cursor++) {
        if (*cursor == '<')
            in_tag = 1;
        else if (*cursor == '>') {
            in_tag = 0;
            if (written_bytes && plain_utf8[written_bytes - 1] != ' ')
                plain_utf8[written_bytes++] = ' ';
        } else if (!in_tag)
            plain_utf8[written_bytes++] = *cursor;
    }
    written_bytes = port_utf8_complete_prefix(plain_utf8, written_bytes);
    plain_utf8[written_bytes] = 0;
}
static jvalue show_html_text(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    char *html = port_jni_string_to_utf8_alloc(arguments[0].l);
    char plain[384];
    html_to_text(html, plain, sizeof(plain));
    jstring title = port_jni_string_from_utf8(PORT_APP_NAME);
    jstring body = port_jni_string_from_utf8(plain);
    port_dialogs_request(&(PortDialogRequest){.kind = PORT_DIALOG_MESSAGE,
                                              .message = {.message_id = -1,
                                                          .title = title,
                                                          .body = body,
                                                          .choice_labels = NULL,
                                                          .popup_reference = NULL,
                                                          .page_id = 0}});
    port_jni->DeleteLocalRef(&port_jni, title);
    port_jni->DeleteLocalRef(&port_jni, body);
    free(html);
    return (jvalue){0};
}

static jvalue mckeyboard_get_caret_position(const PortJniCall *call) {
    (void)call;
    return (jvalue){.i = port_dialogs_keyboard_caret()};
}

static jvalue mckeyboard_keyboard_input_hide(const PortJniCall *call) {
    (void)call;
    port_dialogs_cancel_keyboard();
    return (jvalue){0};
}

static jvalue mckeyboard_keyboard_input_show(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    port_dialogs_request(
        &(PortDialogRequest){.kind = PORT_DIALOG_KEYBOARD,
                             .keyboard = {.text = arguments[0].l ? (jstring)arguments[0].l
                                                                 : port_dialogs_keyboard_text(),
                                          .options = port_dialogs_keyboard_options()}});
    return (jvalue){0};
}

static jvalue mckeyboard_keyboard_input_show_with_max_length_locked(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    port_dialogs_keyboard_set_limit(arguments[1].i);
    port_dialogs_request(&(PortDialogRequest){
        .kind = PORT_DIALOG_KEYBOARD,
        .keyboard = {.text = (jstring)arguments[0].l, .options = port_dialogs_keyboard_options()}});
    return (jvalue){0};
}

static jvalue mckeyboard_set_caret_position(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    port_dialogs_keyboard_set_caret(arguments[0].i);
    return (jvalue){0};
}

static jvalue mckeyboard_set_keyboard_input_max_length(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    port_dialogs_keyboard_set_limit(arguments[0].i);
    return (jvalue){0};
}

static jvalue mckeyboard_set_keyboard_input_text(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    port_dialogs_keyboard_set_text(arguments[0].l);
    return (jvalue){0};
}

static jvalue mckeyboard_set_keyboard_input_type_password(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    port_dialogs_keyboard_set_password(arguments[0].i != 0);
    return (jvalue){0};
}

static jvalue cocojava_close_message_box(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    port_dialogs_cancel_message(arguments[0].i);
    return (jvalue){0};
}

static jvalue cocojava_show_date_picker_dialog_jni(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    char date[32];
    snprintf(date, sizeof(date), "%04d-%02d-%02d", arguments[0].i, arguments[1].i + 1,
             arguments[2].i);
    jstring text = port_jni_string_from_utf8(date);
    port_dialogs_request(&(PortDialogRequest){.kind = PORT_DIALOG_DATE, .date = {.text = text}});
    port_jni->DeleteLocalRef(&port_jni, text);
    return (jvalue){0};
}

static jvalue cocojava_show_dialog(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    port_dialogs_request(
        &(PortDialogRequest){.kind = PORT_DIALOG_MESSAGE,
                             .message = {.message_id = arguments[0].i,
                                         .title = (jstring)arguments[1].l,
                                         .body = (jstring)arguments[2].l,
                                         .choice_labels = (jobjectArray)arguments[4].l,
                                         .popup_reference = NULL,
                                         .page_id = 0}});
    return (jvalue){0};
}

static jvalue alert_popup_construct(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    jobject receiver = call->receiver;
    port_dialogs_bind_popup(receiver, arguments[0].j);
    return (jvalue){0};
}

static jvalue alert_popup_dismiss_alert_popup(const PortJniCall *call) {
    jobject receiver = call->receiver;
    port_dialogs_cancel_popup(receiver);
    return (jvalue){0};
}

static jvalue alert_popup_show_alert_popup(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    jobject receiver = call->receiver;
    port_dialogs_request(&(PortDialogRequest){.kind = PORT_DIALOG_MESSAGE,
                                              .message = {.message_id = -1,
                                                          .title = arguments[0].l,
                                                          .body = arguments[1].l,
                                                          .choice_labels = arguments[4].l,
                                                          .popup_reference = receiver,
                                                          .page_id = 0}});
    return (jvalue){0};
}

static jvalue show_html_page(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    jlong page = arguments[3].j;
    if (!arguments[2].i) {
        if (page) {
            jvalue page_callback_arguments = {.j = page};
            port_async_schedule(&port_game_callback_html_loaded, NULL, &page_callback_arguments);
            port_async_schedule(&port_game_callback_html_back, NULL, &page_callback_arguments);
        }
        return (jvalue){0};
    }

    char html_utf8[384];
    size_t byte_count = arguments[1].l ? port_jni->GetArrayLength(&port_jni, arguments[1].l) : 0;
    if (byte_count >= sizeof(html_utf8))
        byte_count = sizeof(html_utf8) - 1;
    if (byte_count)
        port_jni->GetByteArrayRegion(&port_jni, arguments[1].l, 0, byte_count, (jbyte *)html_utf8);
    html_utf8[byte_count] = 0;
    char plain_utf8[384];
    html_to_text(html_utf8, plain_utf8, sizeof(plain_utf8));
    char *url = port_jni_string_to_utf8_alloc(arguments[0].l);
    jstring title = port_jni_string_from_utf8(PORT_APP_NAME);
    jstring body = port_jni_string_from_utf8(*plain_utf8 ? plain_utf8 : url);
    port_dialogs_request(&(PortDialogRequest){.kind = PORT_DIALOG_MESSAGE,
                                              .message = {.message_id = -1,
                                                          .title = title,
                                                          .body = body,
                                                          .choice_labels = NULL,
                                                          .popup_reference = NULL,
                                                          .page_id = page}});
    port_jni->DeleteLocalRef(&port_jni, title);
    port_jni->DeleteLocalRef(&port_jni, body);
    free(url);
    return (jvalue){0};
}

static const PortServiceMethod methods[] = {
    {{"com/miniclip/facebook/FacebookHelper", "showHtmlDialog", "(Ljava/lang/String;I)V", 1},
     show_html_text},
    {{"com/miniclip/input/MCKeyboard", "getCaretPosition", "()I", 1},
     mckeyboard_get_caret_position},
    {{"com/miniclip/input/MCKeyboard", "keyboardInput_Hide", "()V", 1},
     mckeyboard_keyboard_input_hide},
    {{"com/miniclip/input/MCKeyboard", "keyboardInput_Show", "(Ljava/lang/String;)V", 1},
     mckeyboard_keyboard_input_show},
    {{"com/miniclip/input/MCKeyboard", "keyboardInput_Show_withMaxLengthLocked",
      "(Ljava/lang/String;I)V", 1},
     mckeyboard_keyboard_input_show_with_max_length_locked},
    {{"com/miniclip/input/MCKeyboard", "setCaretPosition", "(I)V", 1},
     mckeyboard_set_caret_position},
    {{"com/miniclip/input/MCKeyboard", "setImeOptions", "(I)V", 1}, port_services_empty_result},
    {{"com/miniclip/input/MCKeyboard", "setKeyboardInputMaxLength", "(I)V", 1},
     mckeyboard_set_keyboard_input_max_length},
    {{"com/miniclip/input/MCKeyboard", "setKeyboardInputPosition", "(FFFFFF)V", 1},
     port_services_empty_result},
    {{"com/miniclip/input/MCKeyboard", "setKeyboardInputSingleLine", "(I)V", 1},
     port_services_empty_result},
    {{"com/miniclip/input/MCKeyboard", "setKeyboardInputStyle", "(IIF)V", 1}, port_services_empty_result},
    {{"com/miniclip/input/MCKeyboard", "setKeyboardInputText", "(Ljava/lang/String;)V", 1},
     mckeyboard_set_keyboard_input_text},
    {{"com/miniclip/input/MCKeyboard", "setKeyboardInputTypePassword", "(I)V", 1},
     mckeyboard_set_keyboard_input_type_password},
    {{"com/miniclip/input/MCKeyboard", "setKeyboardInputVisible", "(I)V", 1},
     port_services_empty_result},
    {{"com/miniclip/input/MCKeyboard", "setKeyboardInputVisibleDelayed", "(II)V", 1},
     port_services_empty_result},
    {{"com/miniclip/nativeJNI/cocojava", "closeMessageBox", "(I)V", 1}, cocojava_close_message_box},
    {{"com/miniclip/nativeJNI/cocojava", "hideMessageBox", "(I)V", 1}, cocojava_close_message_box},
    {{"com/miniclip/nativeJNI/cocojava", "showDatePickerDialog_JNI", "(III)V", 1},
     cocojava_show_date_picker_dialog_jni},
    {{"com/miniclip/nativeJNI/cocojava", "showDatePickerDialog_JNI", "(IIII)V", 1},
     cocojava_show_date_picker_dialog_jni},
    {{"com/miniclip/nativeJNI/cocojava", "showHtmlDialog", "(Ljava/lang/String;I)V", 1},
     show_html_text},
    {{"com/miniclip/nativeJNI/cocojava", "showMessageBox",
      "(ILjava/lang/String;Ljava/lang/String;Z[Ljava/lang/String;)V", 1},
     cocojava_show_dialog},
    {{"com/miniclip/ui/AlertPopup", "<init>", "(JJ)V", 0}, alert_popup_construct},
    {{"com/miniclip/ui/AlertPopup", "dismissAlertPopup", "()V", 0},
     alert_popup_dismiss_alert_popup},
    {{"com/miniclip/ui/AlertPopup", "showAlertPopup",
      "(Ljava/lang/String;Ljava/lang/String;ZZ[Ljava/lang/String;)V", 0},
     alert_popup_show_alert_popup},
    {{"com/miniclip/ui/HTMLDialog", "showHTMLDialog", "(Ljava/lang/String;[BIJ)V", 1},
     show_html_page},
};
const PortServiceMethodTable port_dialogs_methods = {methods, sizeof(methods) / sizeof(methods[0])};
