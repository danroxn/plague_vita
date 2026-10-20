#include "services/dialogs.h"

#include "app/log.h"
#include "compat/jni/runtime.h"
#include "compat/jni/strings.h"
#include "runtime/callbacks.h"
#include "services/async.h"
#include "vita/dialogs.h"
#include "vita/locale.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    KEYBOARD_CAPACITY = 2048,
    DEFAULT_KEYBOARD_LIMIT = 64,
    DATE_INPUT_LENGTH = 10,
    DIALOG_CAPACITY = 32
};
typedef struct {
    jstring initial_text;
    jchar text_utf16[KEYBOARD_CAPACITY + 1];
    int is_active;
    int caret_position;
    PortDialogKind active_kind;
} KeyboardState;
static KeyboardState keyboard_state;
static PortKeyboardOptions keyboard_defaults = {.maximum_code_units = DEFAULT_KEYBOARD_LIMIT};
typedef struct {
    int is_active;
    int message_id;
    int choice_count;
    int choice_offset;
    jobjectArray choice_labels;
    jobject popup_reference;
    jlong page_id;
    char text_utf8[513];
    char button_labels_utf8[PORT_VITA_MESSAGE_BUTTONS][PORT_VITA_MESSAGE_LABEL_BYTES];
} MessageState;
static MessageState message_state;
typedef struct {
    jlong reference;
} Popup;
static struct {
    PortDialogRequest entries[DIALOG_CAPACITY];
    unsigned count;
} dialog_queue;

static void enqueue_request(const PortDialogRequest *request) {
    if (dialog_queue.count == DIALOG_CAPACITY)
        port_fail("Platform dialog queue exhausted");
    PortDialogRequest held = *request;
    switch (held.kind) {
    case PORT_DIALOG_KEYBOARD:
        held.keyboard.text = port_jni->NewGlobalRef(&port_jni, held.keyboard.text);
        break;
    case PORT_DIALOG_DATE:
        held.date.text = port_jni->NewGlobalRef(&port_jni, held.date.text);
        break;
    case PORT_DIALOG_MESSAGE:
        held.message.title = port_jni->NewGlobalRef(&port_jni, held.message.title);
        held.message.body = port_jni->NewGlobalRef(&port_jni, held.message.body);
        held.message.choice_labels = port_jni->NewGlobalRef(&port_jni, held.message.choice_labels);
        held.message.popup_reference =
            port_jni->NewGlobalRef(&port_jni, held.message.popup_reference);
        break;
    }
    dialog_queue.entries[dialog_queue.count++] = held;
}
static void release_request(const PortDialogRequest *request) {
    switch (request->kind) {
    case PORT_DIALOG_KEYBOARD:
        port_jni->DeleteGlobalRef(&port_jni, request->keyboard.text);
        break;
    case PORT_DIALOG_DATE:
        port_jni->DeleteGlobalRef(&port_jni, request->date.text);
        break;
    case PORT_DIALOG_MESSAGE:
        port_jni->DeleteGlobalRef(&port_jni, request->message.title);
        port_jni->DeleteGlobalRef(&port_jni, request->message.body);
        port_jni->DeleteGlobalRef(&port_jni, request->message.choice_labels);
        port_jni->DeleteGlobalRef(&port_jni, request->message.popup_reference);
        break;
    }
}
typedef enum { CANCEL_TEXT_INPUT, CANCEL_MESSAGE_ID, CANCEL_POPUP } CancellationKind;
typedef struct {
    CancellationKind kind;
    union {
        int message_id;
        jobject popup;
    };
} DialogCancellation;
static int matches_cancellation(const PortDialogRequest *request, DialogCancellation cancellation) {
    if (cancellation.kind == CANCEL_TEXT_INPUT)
        return request->kind == PORT_DIALOG_KEYBOARD || request->kind == PORT_DIALOG_DATE;
    if (request->kind != PORT_DIALOG_MESSAGE)
        return 0;
    if (cancellation.kind == CANCEL_MESSAGE_ID)
        return request->message.message_id == cancellation.message_id;
    return cancellation.popup ? request->message.popup_reference == cancellation.popup
                              : request->message.message_id == -1;
}
static void cancel_queued_requests(DialogCancellation cancellation) {
    for (unsigned index = 0; index < dialog_queue.count;) {
        PortDialogRequest *request = &dialog_queue.entries[index];
        if (matches_cancellation(request, cancellation)) {
            release_request(request);
            memmove(request, request + 1, (--dialog_queue.count - index) * sizeof(*request));
        } else
            index++;
    }
}

static int valid_date(const char *text, int *year, int *month, int *day) {
    int consumed = 0;
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (sscanf(text, "%d-%d-%d%n", year, month, day, &consumed) != 3 || text[consumed] ||
        *year < 1 || *year > 9999 || *month < 1 || *month > 12)
        return 0;
    int max = days[*month - 1] +
              (*month == 2 && (*year % 4 == 0 && (*year % 100 != 0 || *year % 400 == 0)));
    return *day >= 1 && *day <= max;
}

static void popup_callback(const GameCallback *callback, jobject popup, int button) {
    Popup *state = port_jni_payload(popup);
    if (!state || !state->reference)
        return;
    jvalue arguments[2] = {{.j = state->reference}, {.i = button}};
    port_async_schedule(callback, popup, arguments);
}
static jsize keyboard_initial_text_length(const jchar *text, jsize length, jsize limit) {
    jsize prefix = length < limit ? length : limit;
    if (prefix > 0 && prefix < length && text[prefix - 1] >= 0xd800 && text[prefix - 1] <= 0xdbff &&
        text[prefix] >= 0xdc00 && text[prefix] <= 0xdfff)
        --prefix;
    return prefix;
}
static void start_keyboard(jstring value, PortKeyboardOptions options, PortDialogKind kind) {
    keyboard_state.active_kind = kind;
    int limit = options.maximum_code_units;
    memset(keyboard_state.text_utf16, 0, sizeof(keyboard_state.text_utf16));
    if (value) {
        jsize code_unit_count = port_jni->GetStringLength(&port_jni, value);
        const jchar *code_units = port_jni->GetStringChars(&port_jni, value, NULL);
        code_unit_count = keyboard_initial_text_length(code_units, code_unit_count, limit);
        memcpy(keyboard_state.text_utf16, code_units, (size_t)code_unit_count * sizeof(jchar));
        port_jni->ReleaseStringChars(&port_jni, value, code_units);
    }
    PortVitaKeyboardRequest request = {.text_utf16 = keyboard_state.text_utf16,
                                       .maximum_code_units = limit,
                                       .is_password = options.is_password};
    port_vita_keyboard_open(&request);
    keyboard_state.is_active = 1;
}
static void message_open(void) {
    int visible = message_state.choice_count > PORT_VITA_MESSAGE_BUTTONS
                      ? PORT_VITA_MESSAGE_BUTTONS
                      : message_state.choice_count;
    for (int i = 0; i < visible; i++) {
        if (message_state.choice_count > 3 && i == 2)
            strcpy(message_state.button_labels_utf8[i],
                   !strcmp(port_vita_language(), "de") ? "Mehr" : "More");
        else if (message_state.choice_labels) {
            jobject label = port_jni->GetObjectArrayElement(&port_jni, message_state.choice_labels,
                                                            (message_state.choice_offset + i) %
                                                                message_state.choice_count);
            port_jni_string_copy_utf8((jstring)label, message_state.button_labels_utf8[i],
                                      sizeof(message_state.button_labels_utf8[i]));
            port_jni->DeleteLocalRef(&port_jni, label);
        } else
            strcpy(message_state.button_labels_utf8[i], "OK");
    }
    PortVitaMessageRequest request = {.message_utf8 = message_state.text_utf8,
                                      .labels_utf8 = message_state.button_labels_utf8,
                                      .total_choice_count = message_state.choice_count,
                                      .visible_button_count = visible};
    port_vita_message_open(&request);
    message_state.is_active = 1;
}
static void start_message(const PortMessageRequest *request) {
    int id = request->message_id;
    jstring title = request->title, body = request->body;
    jobjectArray labels = request->choice_labels;
    jobject popup = request->popup_reference;
    jlong page = request->page_id;
    if (popup)
        popup_callback(&port_game_callback_popup_will_appear, popup, 0);
    message_state.popup_reference = port_jni->NewGlobalRef(&port_jni, popup);
    message_state.page_id = page;
    char body_utf8[384], title_utf8[96];
    port_jni_string_copy_utf8(body, body_utf8, sizeof(body_utf8));
    port_jni_string_copy_utf8(title, title_utf8, sizeof(title_utf8));
    snprintf(message_state.text_utf8, sizeof(message_state.text_utf8), "%s\n\n%s", title_utf8,
             body_utf8);
    message_state.choice_count = labels ? port_jni->GetArrayLength(&port_jni, labels) : 1;
    message_state.choice_labels = port_jni->NewGlobalRef(&port_jni, labels);
    message_state.choice_offset = 0;
    message_open();
    message_state.message_id = id;
    if (popup)
        popup_callback(&port_game_callback_popup_did_appear, popup, 0);
    if (page) {
        jvalue callback_arguments = {.j = page};
        port_async_schedule(&port_game_callback_html_loaded, NULL, &callback_arguments);
    }
}
static void start_request(const PortDialogRequest *request) {
    switch (request->kind) {
    case PORT_DIALOG_KEYBOARD:
        start_keyboard(request->keyboard.text, request->keyboard.options, PORT_DIALOG_KEYBOARD);
        break;
    case PORT_DIALOG_DATE:
        start_keyboard(request->date.text, (PortKeyboardOptions){DATE_INPUT_LENGTH, 0},
                       PORT_DIALOG_DATE);
        break;
    case PORT_DIALOG_MESSAGE:
        start_message(&request->message);
        break;
    }
}
void port_dialogs_request(const PortDialogRequest *request) {
    if (port_dialogs_active() || dialog_queue.count)
        enqueue_request(request);
    else
        start_request(request);
}
int port_dialogs_active(void) {
    return keyboard_state.is_active || message_state.is_active;
}
void port_dialogs_keyboard_set_limit(int requested) {
    keyboard_defaults.maximum_code_units =
        requested >= 1 && requested <= KEYBOARD_CAPACITY ? requested : DEFAULT_KEYBOARD_LIMIT;
}
PortKeyboardOptions port_dialogs_keyboard_options(void) {
    return keyboard_defaults;
}
static void poll_keyboard_completion(void) {
    if (keyboard_state.is_active && port_vita_keyboard_finished()) {
        int accepted = port_vita_keyboard_take_result_and_close();
        keyboard_state.is_active = 0;
        jsize length = 0;
        while (length < KEYBOARD_CAPACITY && keyboard_state.text_utf16[length])
            length++;
        keyboard_state.caret_position = length;
        jstring value = port_jni->NewString(&port_jni, keyboard_state.text_utf16, length);
        if (keyboard_state.active_kind == PORT_DIALOG_DATE) {
            char *text = port_jni_string_to_utf8_alloc(value);
            int year, month, day;
            if (accepted && valid_date(text, &year, &month, &day)) {
                jvalue callback_arguments[3] = {{.i = year}, {.i = month - 1}, {.i = day}};
                port_async_schedule(&port_game_callback_date_response, NULL, callback_arguments);
            }
            port_async_schedule(&port_game_callback_date_closed, NULL, NULL);
            keyboard_state.active_kind = PORT_DIALOG_KEYBOARD;
            free(text);
            port_jni->DeleteLocalRef(&port_jni, value);
        } else {
            port_jni->DeleteGlobalRef(&port_jni, keyboard_state.initial_text);
            keyboard_state.initial_text = port_jni->NewGlobalRef(&port_jni, value);
            const GameCallback *callback = accepted ? &port_game_callback_keyboard_response
                                                    : &port_game_callback_keyboard_canceled;
            jvalue callback_arguments = {.l = value};
            port_async_schedule(callback, NULL, &callback_arguments);
            port_async_schedule(&port_game_callback_keyboard_closed, NULL, &callback_arguments);
            port_jni->DeleteLocalRef(&port_jni, value);
        }
    }
}
static void poll_message_completion(void) {
    if (message_state.is_active && port_vita_message_finished()) {
        int button_id = port_vita_message_take_result_and_close();
        message_state.is_active = 0;
        if (message_state.choice_count > 3 && button_id == 3) {
            message_state.choice_offset =
                (message_state.choice_offset + 2) % message_state.choice_count;
            message_open();
            return;
        }
        int selected = button_id > 0
                           ? (message_state.choice_offset + button_id - 1) %
                                 (message_state.choice_count ? message_state.choice_count : 1)
                           : -1;
        port_jni->DeleteGlobalRef(&port_jni, message_state.choice_labels);
        message_state.choice_labels = NULL;
        if (message_state.message_id >= 0) {
            jvalue callback_arguments[2] = {{.i = message_state.message_id}, {.i = selected}};
            port_async_schedule(&port_game_callback_message_button, NULL, callback_arguments);
        }
        if (message_state.popup_reference) {
            if (button_id > 0)
                popup_callback(&port_game_callback_popup_button, message_state.popup_reference,
                               selected);
            popup_callback(&port_game_callback_popup_will_disappear, message_state.popup_reference,
                           0);
            popup_callback(&port_game_callback_popup_did_disappear, message_state.popup_reference,
                           0);
            port_jni->DeleteGlobalRef(&port_jni, message_state.popup_reference);
            message_state.popup_reference = NULL;
        }
        if (message_state.page_id) {
            jvalue callback_arguments = {.j = message_state.page_id};
            port_async_schedule(&port_game_callback_html_back, NULL, &callback_arguments);
            message_state.page_id = 0;
        }
    }
}
int port_dialogs_poll(void) {
    poll_keyboard_completion();
    poll_message_completion();
    if (!port_dialogs_active() && dialog_queue.count) {
        PortDialogRequest request = dialog_queue.entries[0];
        memmove(dialog_queue.entries, dialog_queue.entries + 1,
                --dialog_queue.count * sizeof(*dialog_queue.entries));
        start_request(&request);
        release_request(&request);
    }
    return port_dialogs_active();
}
void port_dialogs_bind_popup(jobject receiver, jlong reference) {
    Popup *popup = malloc(sizeof(*popup));
    if (!popup)
        port_fail("Popup allocation failed");
    popup->reference = reference;
    port_jni_set_payload(receiver, popup, free);
}
void port_dialogs_keyboard_set_text(jstring text) {
    port_jni->DeleteGlobalRef(&port_jni, keyboard_state.initial_text);
    keyboard_state.initial_text = port_jni->NewGlobalRef(&port_jni, text);
    keyboard_state.caret_position =
        port_jni->GetStringLength(&port_jni, keyboard_state.initial_text);
    if (keyboard_state.is_active) {
        PortDialogRequest request = {.kind = keyboard_state.active_kind};
        if (request.kind == PORT_DIALOG_DATE)
            request.date.text = keyboard_state.initial_text;
        else
            request.keyboard =
                (PortKeyboardRequest){keyboard_state.initial_text, keyboard_defaults};
        port_vita_keyboard_abort();
        port_vita_keyboard_close();
        keyboard_state.is_active = 0;
        port_dialogs_request(&request);
    }
}
void port_dialogs_keyboard_set_caret(int position) {
    jsize length = port_jni->GetStringLength(&port_jni, keyboard_state.initial_text);
    keyboard_state.caret_position = position < 0 ? 0 : position > length ? length : position;
}
int port_dialogs_keyboard_caret(void) {
    return keyboard_state.caret_position;
}
void port_dialogs_keyboard_set_password(int password) {
    keyboard_defaults.is_password = password;
}
jstring port_dialogs_keyboard_text(void) {
    return keyboard_state.initial_text;
}
void port_dialogs_cancel_keyboard(void) {
    port_log_debug("KEYBOARD hide requested active=%d", keyboard_state.is_active);
    cancel_queued_requests((DialogCancellation){.kind = CANCEL_TEXT_INPUT});
    if (keyboard_state.is_active)
        port_vita_keyboard_abort();
}
void port_dialogs_cancel_message(int id) {
    cancel_queued_requests((DialogCancellation){.kind = CANCEL_MESSAGE_ID, .message_id = id});
    if (message_state.is_active && message_state.message_id == id)
        port_vita_message_abort();
}
void port_dialogs_cancel_popup(jobject popup) {
    cancel_queued_requests((DialogCancellation){.kind = CANCEL_POPUP, .popup = popup});
    if (message_state.popup_reference == popup && message_state.is_active)
        port_vita_message_abort();
}
