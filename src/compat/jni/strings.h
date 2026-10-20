#ifndef PLAGUE_COMPAT_JNI_STRINGS_H
#define PLAGUE_COMPAT_JNI_STRINGS_H
#include <stddef.h>
#include <jni.h>
char *port_jni_string_to_utf8_alloc(jstring string);
char *port_jni_string_to_utf8_alloc_with_length(jstring string, size_t *length);
jstring port_jni_string_from_utf8(const char *text);
void port_jni_string_copy_utf8(jstring string, char *buffer, size_t capacity);
#endif
