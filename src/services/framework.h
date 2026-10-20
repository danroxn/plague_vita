#ifndef PLAGUE_SERVICES_FRAMEWORK_H
#define PLAGUE_SERVICES_FRAMEWORK_H
#include "compat/jni/method.h"
#include "compat/jni/metadata.h"
extern const PortJniMetadata port_framework_jni_metadata;

PortJniHandler port_framework_resolve_fallback(const PortJniMethodKey *method);
#endif
