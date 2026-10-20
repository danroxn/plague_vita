#ifndef PLAGUE_COMPAT_JNI_METHOD_H
#define PLAGUE_COMPAT_JNI_METHOD_H
#include <jni.h>

typedef struct {
    const char *owner;
    const char *name;
    const char *descriptor;
    int is_static;
} PortJniMethodKey;

typedef struct {
    const PortJniMethodKey *method;
    jobject receiver;
    const jvalue *arguments;
    const char *return_type;
} PortJniCall;
typedef jvalue (*PortJniHandler)(const PortJniCall *call);

typedef PortJniHandler (*PortJniResolver)(const PortJniMethodKey *method);
#endif
