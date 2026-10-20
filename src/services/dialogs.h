#ifndef PLAGUE_SERVICES_DIALOGS_H
#define PLAGUE_SERVICES_DIALOGS_H
#include <jni.h>

typedef enum { PORT_DIALOG_KEYBOARD, PORT_DIALOG_DATE, PORT_DIALOG_MESSAGE } PortDialogKind;
typedef struct {
    int maximum_code_units;
    int is_password;
} PortKeyboardOptions;
typedef struct {
    jstring text;
    PortKeyboardOptions options;
} PortKeyboardRequest;
typedef struct {
    jstring text;
} PortDateRequest;
typedef struct {
    int message_id;
    jstring title;
    jstring body;
    jobjectArray choice_labels;
    jobject popup_reference;
    jlong page_id;
} PortMessageRequest;
typedef struct {
    PortDialogKind kind;
    union {
        PortKeyboardRequest keyboard;
        PortDateRequest date;
        PortMessageRequest message;
    };
} PortDialogRequest;
int port_dialogs_active(void);
int port_dialogs_poll(void);
void port_dialogs_request(const PortDialogRequest *request);
void port_dialogs_bind_popup(jobject receiver, jlong reference);
void port_dialogs_cancel_keyboard(void);
void port_dialogs_cancel_message(int message_id);
void port_dialogs_cancel_popup(jobject popup);
void port_dialogs_keyboard_set_text(jstring text);
void port_dialogs_keyboard_set_caret(int position);
int port_dialogs_keyboard_caret(void);
void port_dialogs_keyboard_set_password(int password);
void port_dialogs_keyboard_set_limit(int requested);
PortKeyboardOptions port_dialogs_keyboard_options(void);
jstring port_dialogs_keyboard_text(void);
#endif
