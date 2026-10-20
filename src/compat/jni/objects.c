#include "compat/jni/internal.h"

#include "app/log.h"
#include "compat/jni/runtime.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static JniObject *classes[256];
static unsigned class_count;
static const PortJniMetadata *class_metadata;
pthread_mutex_t jni_registry_lock = PTHREAD_MUTEX_INITIALIZER;
JniObject *jni_object_checked(jobject object) {
    JniObject *object_data = (JniObject *)object;
    if (!object_data || object_data->magic != OBJECT_MAGIC)
        port_fail("Invalid JNI object %p from %p", object, __builtin_return_address(0));
    return object_data;
}
JniObject *jni_object_create(JniObjectKind kind, jsize length, size_t element) {
    if (length < 0 || (size_t)length > 32 * 1024 * 1024 / (element ? element : 1))
        port_fail("JNI allocation exceeds size limit");
    JniObject *object_data = calloc(1, sizeof(*object_data));
    if (!object_data)
        port_fail("JNI object allocation failed");
    object_data->magic = OBJECT_MAGIC;
    object_data->kind = kind;
    object_data->reference_count = 1;
    object_data->length = length;
    if (element) {
        object_data->storage = calloc((size_t)length + 1, element);
        if (!object_data->storage)
            port_fail("JNI data allocation failed");
    }
    return object_data;
}
jobject jni_ref_keep(JNIEnv *environment, jobject object) {
    (void)environment;
    if (!object)
        return NULL;
    JniObject *object_data = jni_object_checked(object);
    __sync_add_and_fetch(&object_data->reference_count, 1);
    return object;
}
void jni_ref_release(JNIEnv *environment, jobject object) {
    (void)environment;
    if (!object)
        return;
    JniObject *object_data = jni_object_checked(object);
    if (__sync_sub_and_fetch(&object_data->reference_count, 1))
        return;
    if (object_data->kind == JNI_OBJECT_REFERENCE_ARRAY) {
        jobject *items = object_data->storage;
        for (jsize i = 0; i < object_data->length; i++)
            jni_ref_release(environment, items[i]);
    }
    if (object_data->destroy_service_payload)
        object_data->destroy_service_payload(object_data->service_payload);
    object_data->magic = 0;
    if (object_data->kind == JNI_OBJECT_CLASS)
        free(object_data->text.class_name);
    else
        free(object_data->text.attached_text);
    free(object_data->storage);
    free(object_data);
}
void *port_jni_payload(jobject object) {
    return object ? jni_object_checked(object)->service_payload : NULL;
}
void port_jni_set_payload(jobject object, void *payload, void (*destroy)(void *)) {
    JniObject *object_data = jni_object_checked(object);
    if (object_data->destroy_service_payload)
        object_data->destroy_service_payload(object_data->service_payload);
    object_data->service_payload = payload;
    object_data->destroy_service_payload = destroy;
}
const char *port_jni_class_name(jobject object) {
    if (!object)
        return NULL;
    JniObject *object_data = jni_object_checked(object);
    if (object_data->kind == JNI_OBJECT_CLASS)
        return object_data->text.class_name;
    if (object_data->kind == JNI_OBJECT_STRING)
        return "java/lang/String";
    if (object_data->kind == JNI_OBJECT_PRIMITIVE_ARRAY) {
        static const char *types[] = {"?", "[Z", "[B", "[C", "[S", "[I", "[J", "[F", "[D"};
        return types[object_data->primitive_type];
    }
    return object_data->object_class ? object_data->object_class->text.class_name
                                     : "java/lang/Object";
}
jclass jni_class_find(JNIEnv *environment, const char *name) {
    if (!name || strlen(name) > 512)
        port_fail("Invalid JNI class name");
    pthread_mutex_lock(&jni_registry_lock);
    JniObject *result = NULL;
    for (unsigned i = 0; i < class_count; i++) {
        if (!strcmp(classes[i]->text.class_name, name)) {
            result = classes[i];
            break;
        }
    }
    if (!result) {
        if (class_count == 256)
            port_fail("JNI class registry exhausted");
        result = jni_object_create(JNI_OBJECT_CLASS, 0, 0);
        result->text.class_name = strdup(name);
        if (!result->text.class_name)
            port_fail("JNI class name allocation failed");
        classes[class_count++] = result;
    }
    pthread_mutex_unlock(&jni_registry_lock);
    return (jclass)jni_ref_keep(environment, (jobject)result);
}
jobject jni_object_allocate(JNIEnv *environment, jclass clazz) {
    (void)environment;
    JniObject *type = jni_object_checked((jobject)clazz);
    if (type->kind != JNI_OBJECT_CLASS)
        port_fail("AllocObject requires a class");
    JniObject *object_data = !strcmp(type->text.class_name, "java/lang/String")
                                 ? jni_object_create(JNI_OBJECT_STRING, 0, sizeof(jchar))
                                 : jni_object_create(JNI_OBJECT_INSTANCE, 0, 0);
    object_data->object_class = type;
    return (jobject)object_data;
}
jobject port_jni_attach_text(jobject object, const char *text) {
    JniObject *object_data = jni_object_checked(object);
    if (object_data->kind == JNI_OBJECT_CLASS)
        port_fail("Only instances can carry text");
    char *replacement = text ? strdup(text) : NULL;
    if (text && !replacement)
        port_fail("JNI attached text allocation failed");
    free(object_data->text.attached_text);
    object_data->text.attached_text = replacement;
    return object;
}
const char *port_jni_attached_text(jobject object) {
    if (!object)
        return NULL;
    JniObject *object_data = jni_object_checked(object);
    if (object_data->kind == JNI_OBJECT_CLASS)
        port_fail("Only instances can carry text");
    return object_data->text.attached_text;
}
jclass jni_object_class(JNIEnv *environment, jobject object) {
    JniObject *object_data = jni_object_checked(object);
    if (object_data->kind == JNI_OBJECT_CLASS)
        return jni_class_find(environment, "java/lang/Class");
    if (object_data->object_class)
        return (jclass)jni_ref_keep(environment, (jobject)object_data->object_class);
    return jni_class_find(environment, port_jni_class_name(object));
}
static jboolean is_same_object(JNIEnv *environment, jobject left, jobject right) {
    (void)environment;
    return left == right;
}
static jboolean is_instance_of(JNIEnv *environment, jobject object, jclass clazz) {
    if (!object)
        return JNI_TRUE;
    JniObject *type = jni_object_checked((jobject)clazz);
    jclass actual_class = jni_object_class(environment, object);
    const char *name = jni_object_checked((jobject)actual_class)->text.class_name;
    const char *expected_class_name = type->text.class_name;
    int is_assignable =
        !strcmp(name, expected_class_name) || !strcmp(expected_class_name, "java/lang/Object");
    if (name[0] == '[' && (!strcmp(expected_class_name, "java/lang/Cloneable") ||
                           !strcmp(expected_class_name, "java/io/Serializable")))
        is_assignable = 1;
    if (!strcmp(name, "java/lang/String") &&
        (!strcmp(expected_class_name, "java/lang/CharSequence") ||
         !strcmp(expected_class_name, "java/lang/Comparable") ||
         !strcmp(expected_class_name, "java/io/Serializable")))
        is_assignable = 1;
    if (name[0] == '[' && name[1] == 'L' && !strcmp(expected_class_name, "[Ljava/lang/Object;"))
        is_assignable = 1;
    if (!is_assignable)
        is_assignable = class_metadata->is_assignable(name, expected_class_name);
    jni_ref_release(environment, (jobject)actual_class);
    return is_assignable;
}
static jfieldID get_field_id(JNIEnv *environment, jclass declaring_class, const char *name,
                             const char *descriptor) {
    (void)environment;
    JniObject *owner = jni_object_checked((jobject)declaring_class);
    jfieldID field = class_metadata->find_field(owner->text.class_name, name, descriptor);
    if (!field)
        port_fail("JNI field not implemented: %s.%s:%s", owner->text.class_name, name, descriptor);
    return field;
}
static jint get_static_int_field(JNIEnv *environment, jclass declaring_class, jfieldID field) {
    (void)environment;
    (void)declaring_class;
    return class_metadata->read_static_int(field);
}
static jobject get_static_object_field(JNIEnv *environment, jclass declaring_class,
                                       jfieldID field) {
    (void)environment;
    return class_metadata->read_static_object(declaring_class, field);
}

jobject port_jni_new_instance(const char *class_name) {
    jclass type = jni_class_find(&port_jni, class_name);
    jobject object = jni_object_allocate(&port_jni, type);
    jni_ref_release(&port_jni, type);
    return object;
}

void jni_install_objects(struct JNINativeInterface *table, const PortJniMetadata *metadata) {
    class_metadata = metadata;
    table->FindClass = jni_class_find;
    table->AllocObject = jni_object_allocate;
    table->GetObjectClass = jni_object_class;
    table->IsInstanceOf = is_instance_of;
    table->IsSameObject = is_same_object;
    table->NewGlobalRef = jni_ref_keep;
    table->NewLocalRef = jni_ref_keep;
    table->DeleteLocalRef = jni_ref_release;
    table->DeleteGlobalRef = jni_ref_release;
    table->GetFieldID = get_field_id;
    table->GetStaticFieldID = get_field_id;
    table->GetStaticIntField = get_static_int_field;
    table->GetStaticObjectField = get_static_object_field;
}
