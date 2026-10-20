#ifndef PLAGUE_COMPAT_JNI_RUNTIME_H
#define PLAGUE_COMPAT_JNI_RUNTIME_H
#include "compat/jni/method.h"
#include "compat/jni/metadata.h"
#include <stddef.h>
#include <jni.h>

extern JavaVM port_vm;
extern JNIEnv port_jni;

void port_jni_init(PortJniResolver resolve_method, const PortJniMetadata *metadata);
void *port_jni_payload(jobject object);
void port_jni_set_payload(jobject object, void *payload, void (*destroy)(void *));
const char *port_jni_class_name(jobject object);
jobject port_jni_attach_text(jobject object, const char *text);
const char *port_jni_attached_text(jobject object);
jobject port_jni_new_instance(const char *class_name);
#endif
