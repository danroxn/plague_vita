#ifndef PLAGUE_VITA_INPUT_H
#define PLAGUE_VITA_INPUT_H
void port_vita_input_init(void);
void port_vita_input_poll(void);
void port_vita_input_cancel(void);
void port_vita_input_accelerometer(int enabled);

#endif
