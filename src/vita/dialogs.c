#include "vita/dialogs.h"

#include "app/log.h"

#include <psp2/ime_dialog.h>
#include <psp2/message_dialog.h>
#include <psp2/sysmodule.h>
#include <stddef.h>
#include <string.h>

static const uint16_t ime_title[] = {'P', 'l', 'a', 'g', 'u', 'e', ' ', 'I', 'n', 'c', '.', 0};
void port_vita_dialogs_init(void) {
    sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
}
void port_vita_keyboard_open(const PortVitaKeyboardRequest *request) {
    SceImeDialogParam parameters;
    sceImeDialogParamInit(&parameters);
    parameters.title = ime_title;
    parameters.type = SCE_IME_TYPE_DEFAULT;
    parameters.maxTextLength = request->maximum_code_units;
    parameters.initialText = request->text_utf16;
    parameters.inputTextBuffer = request->text_utf16;
    parameters.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
    parameters.textBoxMode = request->is_password ? SCE_IME_DIALOG_TEXTBOX_MODE_PASSWORD
                                                  : SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
    int result = sceImeDialogInit(&parameters);
    if (result < 0)
        port_fail("Keyboard init 0x%08x", result);
    port_log_debug("KEYBOARD opened max_length=%u", parameters.maxTextLength);
}
int port_vita_keyboard_finished(void) {
    return sceImeDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_FINISHED;
}
int port_vita_keyboard_take_result_and_close(void) {
    SceImeDialogResult result = {0};
    if (sceImeDialogGetResult(&result) < 0)
        port_fail("Keyboard result failed");
    port_log_debug("KEYBOARD completed result=%d button=%d", result.result, result.button);
    sceImeDialogTerm();
    return result.button == SCE_IME_DIALOG_BUTTON_ENTER;
}
void port_vita_keyboard_abort(void) {
    port_log_debug("KEYBOARD abort requested");
    sceImeDialogAbort();
}
void port_vita_keyboard_close(void) {
    sceImeDialogTerm();
}
void port_vita_message_open(const PortVitaMessageRequest *request) {
    static SceMsgDialogParam parameters;
    sceMsgDialogParamInit(&parameters);
    parameters.mode = SCE_MSG_DIALOG_MODE_USER_MSG;
    static SceMsgDialogUserMessageParam user;
    static SceMsgDialogButtonsParam buttons;
    memset(&user, 0, sizeof(user));
    memset(&buttons, 0, sizeof(buttons));
    user.msg = (const SceChar8 *)request->message_utf8;
    user.buttonType = request->total_choice_count == 0   ? SCE_MSG_DIALOG_BUTTON_TYPE_NONE
                      : request->total_choice_count == 1 ? SCE_MSG_DIALOG_BUTTON_TYPE_OK
                                                         : SCE_MSG_DIALOG_BUTTON_TYPE_3BUTTONS;
    buttons.msg1 = request->labels_utf8[0];
    buttons.msg2 = request->visible_button_count > 1 ? request->labels_utf8[1] : NULL;
    buttons.msg3 = request->visible_button_count > 2 ? request->labels_utf8[2] : NULL;
    user.buttonParam = user.buttonType == SCE_MSG_DIALOG_BUTTON_TYPE_3BUTTONS ? &buttons : NULL;
    parameters.userMsgParam = &user;
    int result = sceMsgDialogInit(&parameters);
    if (result < 0)
        port_fail("Message init 0x%08x", result);
}
int port_vita_message_finished(void) {
    return sceMsgDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_FINISHED;
}
int port_vita_message_take_result_and_close(void) {
    SceMsgDialogResult result = {0};
    if (sceMsgDialogGetResult(&result) < 0)
        port_fail("Message result failed");
    port_log_debug("MESSAGE completed result=0x%08x button=%d", (unsigned)result.result,
                   result.buttonId);
    sceMsgDialogTerm();
    return result.buttonId;
}
void port_vita_message_abort(void) {
    sceMsgDialogAbort();
}
