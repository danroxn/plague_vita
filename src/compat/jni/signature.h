#ifndef PLAGUE_COMPAT_JNI_SIGNATURE_H
#define PLAGUE_COMPAT_JNI_SIGNATURE_H

#include <jni.h>
#include <stdarg.h>

enum { PORT_JNI_MAX_ARGUMENTS = 24 };

typedef struct {
    char argument_types[PORT_JNI_MAX_ARGUMENTS];
    unsigned argument_count;
    const char *return_type;
} JniCallSignature;
JniCallSignature port_jni_describe_method(const char *descriptor);
void port_jni_unpack_call(const JniCallSignature *signature, va_list arguments, jvalue *output);

#endif
