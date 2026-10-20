#ifndef PLAGUE_COMPAT_JNI_INTERNAL_H
#define PLAGUE_COMPAT_JNI_INTERNAL_H
#include "compat/abi.h"
#include "compat/jni/runtime.h"
#include "compat/jni/signature.h"
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    JNI_OBJECT_CLASS = 1,
    JNI_OBJECT_INSTANCE,
    JNI_OBJECT_STRING,
    JNI_OBJECT_PRIMITIVE_ARRAY,
    JNI_OBJECT_REFERENCE_ARRAY,
} JniObjectKind;
typedef enum {
    JNI_PRIMITIVE_NONE,
    JNI_PRIMITIVE_BOOLEAN,
    JNI_PRIMITIVE_BYTE,
    JNI_PRIMITIVE_CHAR,
    JNI_PRIMITIVE_SHORT,
    JNI_PRIMITIVE_INT,
    JNI_PRIMITIVE_LONG,
    JNI_PRIMITIVE_FLOAT,
    JNI_PRIMITIVE_DOUBLE,
} JniPrimitiveType;
typedef struct JniObject {
    uint32_t magic;
    JniObjectKind kind;
    JniPrimitiveType primitive_type;
    unsigned reference_count;
    jsize length;
    union {
        char *class_name;
        char *attached_text;
    } text;
    struct JniObject *object_class;
    void *storage;
    void *service_payload;
    void (*destroy_service_payload)(void *);
} JniObject;
typedef struct {
    JniObject *owner;
    PortJniMethodKey key;
    JniCallSignature signature;
    PortJniHandler handler;
} JniMethod;
#define OBJECT_MAGIC 0x504c4a4eU
extern pthread_mutex_t jni_registry_lock;
JniObject *jni_object_checked(jobject object);
JniObject *jni_object_create(JniObjectKind kind, jsize length, size_t element);
jobject jni_ref_keep(JNIEnv *environment, jobject object);
void jni_ref_release(JNIEnv *environment, jobject object);
jclass jni_class_find(JNIEnv *environment, const char *name);
jobject jni_object_allocate(JNIEnv *environment, jclass clazz);
jclass jni_object_class(JNIEnv *environment, jobject object);
void jni_install_objects(struct JNINativeInterface *table, const PortJniMetadata *metadata);
void jni_install_dispatch(struct JNINativeInterface *table, PortJniResolver resolve_method);
void jni_install_strings(struct JNINativeInterface *table);
void jni_install_arrays(struct JNINativeInterface *table);
#endif
