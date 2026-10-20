#ifndef PLAGUE_COMPAT_JNI_METADATA_H
#define PLAGUE_COMPAT_JNI_METADATA_H
#include <jni.h>

typedef struct {
    jboolean (*is_assignable)(const char *actual, const char *expected);
    jfieldID (*find_field)(const char *owner, const char *name, const char *descriptor);
    jint (*read_static_int)(jfieldID field);
    jobject (*read_static_object)(jclass declaring_class, jfieldID field);
} PortJniMetadata;
#endif
