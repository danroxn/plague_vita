#ifndef PLAGUE_VITA_LIFECYCLE_H
#define PLAGUE_VITA_LIFECYCLE_H
typedef struct {
    int system_ui_overlaid;
    unsigned event_count;
} PortVitaLifecycleState;
typedef enum {
    PORT_VITA_EVENT_RECEIVE_FAILED = -1,
    PORT_VITA_EVENT_OTHER,
    PORT_VITA_EVENT_RESUME
} PortVitaLifecycleEvent;
int port_vita_lifecycle_read_state(PortVitaLifecycleState *state);
PortVitaLifecycleEvent port_vita_lifecycle_next_event(void);
#endif
