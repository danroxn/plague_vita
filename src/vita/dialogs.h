#ifndef PLAGUE_VITA_DIALOGS_H
#define PLAGUE_VITA_DIALOGS_H
#include <stdint.h>
enum { PORT_VITA_MESSAGE_BUTTONS = 3, PORT_VITA_MESSAGE_LABEL_BYTES = 96 };
typedef struct {
    uint16_t *text_utf16;
    int maximum_code_units;
    int is_password;
} PortVitaKeyboardRequest;
typedef struct {
    const char *message_utf8;
    const char (*labels_utf8)[PORT_VITA_MESSAGE_LABEL_BYTES];
    int total_choice_count;
    int visible_button_count;
} PortVitaMessageRequest;
void port_vita_dialogs_init(void);
void port_vita_keyboard_open(const PortVitaKeyboardRequest *request);
int port_vita_keyboard_finished(void);
int port_vita_keyboard_take_result_and_close(void);
void port_vita_keyboard_abort(void);
void port_vita_keyboard_close(void);
void port_vita_message_open(const PortVitaMessageRequest *request);
int port_vita_message_finished(void);
int port_vita_message_take_result_and_close(void);
void port_vita_message_abort(void);
#endif
