#include "vita/lifecycle.h"

#include <psp2/appmgr.h>

int port_vita_lifecycle_read_state(PortVitaLifecycleState *result) {
    SceAppMgrAppState state = {0};
    if (_sceAppMgrGetAppState(&state, sizeof(state), 0x03600000) < 0)
        return 0;
    *result = (PortVitaLifecycleState){.system_ui_overlaid = state.isSystemUiOverlaid,
                                       .event_count = state.systemEventNum};
    return 1;
}
PortVitaLifecycleEvent port_vita_lifecycle_next_event(void) {
    SceAppMgrSystemEvent event;
    if (sceAppMgrReceiveSystemEvent(&event) < 0)
        return PORT_VITA_EVENT_RECEIVE_FAILED;
    return event.systemEvent == SCE_APPMGR_SYSTEMEVENT_ON_RESUME ? PORT_VITA_EVENT_RESUME
                                                                 : PORT_VITA_EVENT_OTHER;
}
