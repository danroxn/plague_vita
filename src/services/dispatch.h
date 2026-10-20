#ifndef PLAGUE_SERVICES_DISPATCH_H
#define PLAGUE_SERVICES_DISPATCH_H
#include "compat/jni/method.h"
#include <jni.h>
int port_services_poll(void);
PortJniHandler port_services_resolve_method(const PortJniMethodKey *method);
#endif
