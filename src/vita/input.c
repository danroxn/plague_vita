#include "vita/input.h"

#include "app/log.h"
#include "compat/jni/runtime.h"
#include "runtime/game_api.h"
#include "vita/time.h"
#include "vita/device.h"

#include <psp2/ctrl.h>
#include <psp2/motion.h>
#include <psp2/touch.h>
#include <stddef.h>
#include <string.h>

static jclass input_class;
enum { MAX_TOUCH_CONTACTS = 6, ANDROID_KEY_BACK = 4, KEY_PRESSED = 0, KEY_RELEASED = 1 };
static SceTouchData previous_contacts;
static unsigned previous_buttons;
static int accelerometer_enabled;
void port_vita_input_accelerometer(int enabled) {
    if (!port_game_api()->accelerometer_changed)
        return;
    if (enabled && !accelerometer_enabled)
        accelerometer_enabled = sceMotionStartSampling() >= 0;
    if (!enabled && accelerometer_enabled) {
        sceMotionStopSampling();
        accelerometer_enabled = 0;
    }
}
void port_vita_input_init(void) {
    input_class = port_jni->FindClass(&port_jni, "com/miniclip/input/MCInput");
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
}
static void dispatch_touch_batch(GameTouches dispatch, const SceTouchData *contacts) {
    jint contact_ids[MAX_TOUCH_CONTACTS];
    jfloat contact_x[MAX_TOUCH_CONTACTS], contact_y[MAX_TOUCH_CONTACTS];
    jsize contact_count =
        contacts->reportNum < MAX_TOUCH_CONTACTS ? contacts->reportNum : MAX_TOUCH_CONTACTS;
    for (jsize index = 0; index < contact_count; ++index) {
        contact_ids[index] = contacts->report[index].id;
        contact_x[index] =
            contacts->report[index].x * ((float)PORT_DISPLAY_WIDTH / PORT_TOUCH_WIDTH);
        contact_y[index] =
            contacts->report[index].y * ((float)PORT_DISPLAY_HEIGHT / PORT_TOUCH_HEIGHT);
    }
    jintArray id_array = port_jni->NewIntArray(&port_jni, contact_count);
    jfloatArray x_array = port_jni->NewFloatArray(&port_jni, contact_count);
    jfloatArray y_array = port_jni->NewFloatArray(&port_jni, contact_count);
    port_jni->SetIntArrayRegion(&port_jni, id_array, 0, contact_count, contact_ids);
    port_jni->SetFloatArrayRegion(&port_jni, x_array, 0, contact_count, contact_x);
    port_jni->SetFloatArrayRegion(&port_jni, y_array, 0, contact_count, contact_y);
    dispatch(&port_jni, input_class, id_array, x_array, y_array);
    port_jni->DeleteLocalRef(&port_jni, id_array);
    port_jni->DeleteLocalRef(&port_jni, x_array);
    port_jni->DeleteLocalRef(&port_jni, y_array);
}
static int has_contact(const SceTouchData *contacts, unsigned id) {
    for (unsigned i = 0; i < contacts->reportNum; i++)
        if (contacts->report[i].id == id)
            return 1;
    return 0;
}
void port_vita_input_cancel(void) {
    if (!previous_contacts.reportNum)
        return;
    dispatch_touch_batch(port_game_api()->touch_cancel, &previous_contacts);
    memset(&previous_contacts, 0, sizeof(previous_contacts));
}
void port_vita_input_poll(void) {
    const GameApi *game = port_game_api();
    if (accelerometer_enabled) {
        SceMotionState state;
        if (sceMotionGetState(&state) == 0)
            game->accelerometer_changed(
                &port_jni, NULL, state.acceleration.x * 9.80665f, state.acceleration.y * 9.80665f,
                state.acceleration.z * 9.80665f, (jlong)state.hostTimestamp * 1000);
    }
    SceTouchData current_contacts = {0};
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &current_contacts, 1);
    for (unsigned i = 0; i < previous_contacts.reportNum; i++) {
        const SceTouchReport *contact = &previous_contacts.report[i];
        if (has_contact(&current_contacts, contact->id))
            continue;
        port_log_debug("INPUT touch action=end at_us=%llu id=%u x=%u y=%u",
                       (unsigned long long)port_vita_time_us(), contact->id, contact->x,
                       contact->y);
        game->touch_end(&port_jni, input_class, contact->id,
                        contact->x * ((float)PORT_DISPLAY_WIDTH / PORT_TOUCH_WIDTH),
                        contact->y * ((float)PORT_DISPLAY_HEIGHT / PORT_TOUCH_HEIGHT));
    }
    for (unsigned i = 0; i < current_contacts.reportNum; i++) {
        const SceTouchReport *contact = &current_contacts.report[i];
        if (has_contact(&previous_contacts, contact->id))
            continue;
        game->touch_begin(&port_jni, input_class, contact->id,
                          contact->x * ((float)PORT_DISPLAY_WIDTH / PORT_TOUCH_WIDTH),
                          contact->y * ((float)PORT_DISPLAY_HEIGHT / PORT_TOUCH_HEIGHT));
    }
    if (current_contacts.reportNum)
        dispatch_touch_batch(game->touch_move, &current_contacts);
    previous_contacts = current_contacts;
    SceCtrlData controller = {.buttons = previous_buttons};
    if (sceCtrlPeekBufferPositive(0, &controller, 1) <= 0)
        return;
    if ((controller.buttons ^ previous_buttons) & SCE_CTRL_CIRCLE)
        game->key_event(&port_jni, input_class,
                        (controller.buttons & SCE_CTRL_CIRCLE) ? KEY_PRESSED : KEY_RELEASED,
                        ANDROID_KEY_BACK, 0);
    previous_buttons = controller.buttons;
}
