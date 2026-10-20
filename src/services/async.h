#ifndef PLAGUE_SERVICES_ASYNC_H
#define PLAGUE_SERVICES_ASYNC_H
#include "runtime/callbacks.h"
void port_async_schedule(const GameCallback *callback, jobject receiver, const jvalue *arguments);
void port_async_drain(void);
#endif
