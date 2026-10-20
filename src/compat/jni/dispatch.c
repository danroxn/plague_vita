#include "compat/jni/internal.h"

#include "app/log.h"
#include "compat/abi.h"
#include "compat/jni/runtime.h"
#include "compat/jni/signature.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static JniMethod *methods[1024];
static unsigned method_count;
static PortJniResolver service_resolver;
static jmethodID find_or_create_method(JNIEnv *environment, jclass declaring_class,
                                       const char *name, const char *descriptor, int is_static) {
    (void)environment;
    JniObject *owner = jni_object_checked((jobject)declaring_class);
    if (owner->kind != JNI_OBJECT_CLASS || !name || !descriptor || descriptor[0] != '(' ||
        !strchr(descriptor, ')'))
        port_fail("Invalid JNI method descriptor");
    pthread_mutex_lock(&jni_registry_lock);
    JniMethod *result = NULL;
    for (unsigned i = 0; i < method_count; i++) {
        JniMethod *registered_method = methods[i];
        if (registered_method->owner == owner && registered_method->key.is_static == is_static &&
            !strcmp(registered_method->key.name, name) &&
            !strcmp(registered_method->key.descriptor, descriptor)) {
            result = registered_method;
            break;
        }
    }
    if (!result) {
        if (method_count == 1024)
            port_fail("JNI method registry exhausted");
        result = calloc(1, sizeof(*result));
        if (!result)
            port_fail("JNI method allocation failed");
        result->owner = owner;
        result->key =
            (PortJniMethodKey){owner->text.class_name, strdup(name), strdup(descriptor), is_static};
        if (!result->key.name || !result->key.descriptor)
            port_fail("JNI method identity allocation failed");
        methods[method_count++] = result;
    }
    pthread_mutex_unlock(&jni_registry_lock);
    return (jmethodID)result;
}
static jmethodID get_instance_method_id(JNIEnv *environment, jclass object_class,
                                        const char *method_name, const char *descriptor) {
    return find_or_create_method(environment, object_class, method_name, descriptor, 0);
}
static jmethodID get_static_method_id(JNIEnv *environment, jclass object_class,
                                      const char *method_name, const char *descriptor) {
    return find_or_create_method(environment, object_class, method_name, descriptor, 1);
}
static JniMethod *require_method(jmethodID method_id) {
    JniMethod *result = NULL;
    pthread_mutex_lock(&jni_registry_lock);
    for (unsigned i = 0; i < method_count; i++) {
        if (methods[i] == (JniMethod *)method_id) {
            result = methods[i];
            break;
        }
    }
    pthread_mutex_unlock(&jni_registry_lock);
    if (!result)
        port_fail("Unknown JNI method ID %p", (void *)method_id);
    return result;
}
static JniMethod *prepare_method(jmethodID method_id) {
    JniMethod *method = require_method(method_id);
    pthread_mutex_lock(&jni_registry_lock);
    PortJniHandler published = method->handler;
    pthread_mutex_unlock(&jni_registry_lock);
    if (published)
        return method;
    JniCallSignature signature = port_jni_describe_method(method->key.descriptor);
    PortJniHandler handler = service_resolver(&method->key);
    if (!handler)
        port_fail("Java service not implemented: %s.%s%s", method->key.owner, method->key.name,
                  method->key.descriptor);
    pthread_mutex_lock(&jni_registry_lock);
    if (!method->handler) {
        method->signature = signature;
        method->handler = handler;
    }
    pthread_mutex_unlock(&jni_registry_lock);
    return method;
}
static jvalue invoke_method(JniMethod *method, jobject receiver, const jvalue *arguments) {
    PortJniCall call = {.method = &method->key,
                        .receiver = receiver,
                        .arguments = arguments,
                        .return_type = method->signature.return_type};
    return method->handler(&call);
}
#define DEFINE_JNI_CALLS(Type, field, Name, CallAbi)                                               \
    static Type CallAbi call_##Name##_a(JNIEnv *environment, jobject receiver,                     \
                                        jmethodID method_id, const jvalue *arguments) {            \
        (void)environment;                                                                         \
        return invoke_method(prepare_method(method_id), receiver, arguments).field;                \
    }                                                                                              \
    static Type CallAbi call_##Name##_v(JNIEnv *environment, jobject receiver,                     \
                                        jmethodID method_id, va_list arguments) {                  \
        (void)environment;                                                                         \
        JniMethod *method = prepare_method(method_id);                                             \
        jvalue argument_values[PORT_JNI_MAX_ARGUMENTS] = {{0}};                                    \
        port_jni_unpack_call(&method->signature, arguments, argument_values);                      \
        return invoke_method(method, receiver, argument_values).field;                            \
    }                                                                                              \
    static Type CallAbi call_##Name(JNIEnv *environment, jobject receiver, jmethodID method_id,    \
                                    ...) {                                                         \
        va_list arguments;                                                                         \
        va_start(arguments, method_id);                                                            \
        Type result = call_##Name##_v(environment, receiver, method_id, arguments);                \
        va_end(arguments);                                                                         \
        return result;                                                                             \
    }
DEFINE_JNI_CALLS(jobject, l, Object, )
DEFINE_JNI_CALLS(jboolean, z, Boolean, )
DEFINE_JNI_CALLS(jbyte, b, Byte, )
DEFINE_JNI_CALLS(jchar, c, Char, )
DEFINE_JNI_CALLS(jshort, s, Short, )
DEFINE_JNI_CALLS(jint, i, Int, )
DEFINE_JNI_CALLS(jlong, j, Long, )
DEFINE_JNI_CALLS(jfloat, f, Float, SFP)
DEFINE_JNI_CALLS(jdouble, d, Double, SFP)
#undef DEFINE_JNI_CALLS

static void call_Void_a(JNIEnv *environment, jobject receiver, jmethodID method_id,
                        const jvalue *arguments) {
    (void)environment;
    invoke_method(prepare_method(method_id), receiver, arguments);
}
static void call_Void_v(JNIEnv *environment, jobject receiver, jmethodID method_id,
                        va_list arguments) {
    (void)environment;
    JniMethod *method = prepare_method(method_id);
    jvalue argument_values[PORT_JNI_MAX_ARGUMENTS] = {{0}};
    port_jni_unpack_call(&method->signature, arguments, argument_values);
    invoke_method(method, receiver, argument_values);
}
static void call_Void(JNIEnv *environment, jobject receiver, jmethodID method_id, ...) {
    va_list arguments;
    va_start(arguments, method_id);
    call_Void_v(environment, receiver, method_id, arguments);
    va_end(arguments);
}
static jobject new_object_v(JNIEnv *environment, jclass object_class, jmethodID method_id,
                            va_list arguments) {
    jobject new_instance = jni_object_allocate(environment, object_class);
    call_Void_v(environment, new_instance, method_id, arguments);
    return new_instance;
}
static jobject new_object_a(JNIEnv *environment, jclass object_class, jmethodID method_id,
                            const jvalue *arguments) {
    jobject new_instance = jni_object_allocate(environment, object_class);
    call_Void_a(environment, new_instance, method_id, arguments);
    return new_instance;
}
static jobject new_object(JNIEnv *environment, jclass object_class, jmethodID method_id, ...) {
    va_list arguments;
    va_start(arguments, method_id);
    jobject new_instance = new_object_v(environment, object_class, method_id, arguments);
    va_end(arguments);
    return new_instance;
}

void jni_install_dispatch(struct JNINativeInterface *table, PortJniResolver resolve_method) {
    service_resolver = resolve_method;
    table->NewObject = new_object;
    table->NewObjectV = new_object_v;
    table->NewObjectA = new_object_a;
    table->GetMethodID = get_instance_method_id;
    table->GetStaticMethodID = get_static_method_id;
#define INSTALL_CALL(Name)                                                                         \
    table->Call##Name##Method = call_##Name;                                                       \
    table->Call##Name##MethodV = call_##Name##_v;                                                  \
    table->Call##Name##MethodA = call_##Name##_a;                                                  \
    table->CallStatic##Name##Method = call_##Name;                                                 \
    table->CallStatic##Name##MethodV = call_##Name##_v;                                            \
    table->CallStatic##Name##MethodA = call_##Name##_a
    INSTALL_CALL(Object);
    INSTALL_CALL(Boolean);
    INSTALL_CALL(Byte);
    INSTALL_CALL(Char);
    INSTALL_CALL(Short);
    INSTALL_CALL(Int);
    INSTALL_CALL(Long);
    INSTALL_CALL(Void);
#undef INSTALL_CALL
#define INSTALL_SOFTFP_CALL(Name)                                                                  \
    table->Call##Name##Method = (__typeof__(table->Call##Name##Method))call_##Name;                \
    table->Call##Name##MethodV = (__typeof__(table->Call##Name##MethodV))call_##Name##_v;          \
    table->Call##Name##MethodA = (__typeof__(table->Call##Name##MethodA))call_##Name##_a;          \
    table->CallStatic##Name##Method = (__typeof__(table->CallStatic##Name##Method))call_##Name;    \
    table->CallStatic##Name##MethodV =                                                             \
        (__typeof__(table->CallStatic##Name##MethodV))call_##Name##_v;                             \
    table->CallStatic##Name##MethodA = (__typeof__(table->CallStatic##Name##MethodA))call_##Name##_a
    INSTALL_SOFTFP_CALL(Float);
    INSTALL_SOFTFP_CALL(Double);
#undef INSTALL_SOFTFP_CALL
}
