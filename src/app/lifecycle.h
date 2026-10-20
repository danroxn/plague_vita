#ifndef PLAGUE_APP_LIFECYCLE_H
#define PLAGUE_APP_LIFECYCLE_H
#include <stdint.h>
void port_app_register_lifecycle_events(void);
void port_app_poll_lifecycle(int dialog_active);
void port_app_request_exit(void);
int port_app_exit_requested(void);
uint64_t port_app_frame_interval_us(void);
void port_app_set_animation_interval_seconds(double interval_seconds);
#endif
