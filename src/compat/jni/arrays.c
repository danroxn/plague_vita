#include "compat/jni/internal.h"

#include "app/log.h"
#include "compat/jni/runtime.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static jsize array_length(JNIEnv *environment, jarray array) {
    (void)environment;
    JniObject *array_object = jni_object_checked((jobject)array);
    if (array_object->kind != JNI_OBJECT_PRIMITIVE_ARRAY &&
        array_object->kind != JNI_OBJECT_REFERENCE_ARRAY)
        port_fail("Expected JNI array");
    return array_object->length;
}
static void check_array_bounds(JniObject *array_object, jsize start, jsize length) {
    if (start < 0 || length < 0 || start > array_object->length ||
        length > array_object->length - start)
        port_fail("JNI array bounds violation");
}
static void check_primitive_array_type(JniObject *array_object, JniPrimitiveType type) {
    if (array_object->kind != JNI_OBJECT_PRIMITIVE_ARRAY || array_object->primitive_type != type)
        port_fail("JNI array type mismatch");
}
#define DEFINE_JNI_ARRAYS(Type, Name, code)                                                        \
    static Type##Array new_##Name(JNIEnv *environment, jsize length) {                             \
        (void)environment;                                                                         \
        JniObject *array_object =                                                                  \
            jni_object_create(JNI_OBJECT_PRIMITIVE_ARRAY, length, sizeof(Type));                   \
        array_object->primitive_type = code;                                                       \
        return (Type##Array)array_object;                                                          \
    }                                                                                              \
    static Type *get_##Name(JNIEnv *environment, Type##Array array, jboolean *copy) {              \
        (void)environment;                                                                         \
        JniObject *array_object = jni_object_checked((jobject)array);                              \
        check_primitive_array_type(array_object, code);                                            \
        if (copy)                                                                                  \
            *copy = JNI_FALSE;                                                                     \
        return array_object->storage;                                                              \
    }                                                                                              \
    static void release_##Name(JNIEnv *environment, Type##Array array, Type *elements,             \
                               jint release_mode) {                                                \
        (void)environment;                                                                         \
        (void)array;                                                                               \
        (void)elements;                                                                            \
        (void)release_mode;                                                                        \
    }                                                                                              \
    static void region_##Name(JNIEnv *environment, Type##Array array, jsize start, jsize length,   \
                              Type *elements) {                                                    \
        (void)environment;                                                                         \
        JniObject *array_object = jni_object_checked((jobject)array);                              \
        check_array_bounds(array_object, start, length);                                           \
        check_primitive_array_type(array_object, code);                                            \
        memcpy(elements, (Type *)array_object->storage + start,                                    \
               (size_t)length * sizeof(Type));                                                     \
    }                                                                                              \
    static void set_##Name(JNIEnv *environment, Type##Array array, jsize start, jsize length,      \
                           const Type *elements) {                                                 \
        (void)environment;                                                                         \
        JniObject *array_object = jni_object_checked((jobject)array);                              \
        check_array_bounds(array_object, start, length);                                           \
        check_primitive_array_type(array_object, code);                                            \
        memcpy((Type *)array_object->storage + start, elements,                                    \
               (size_t)length * sizeof(Type));                                                     \
    }
DEFINE_JNI_ARRAYS(jboolean, Boolean, JNI_PRIMITIVE_BOOLEAN)
DEFINE_JNI_ARRAYS(jbyte, Byte, JNI_PRIMITIVE_BYTE)
DEFINE_JNI_ARRAYS(jchar, Char, JNI_PRIMITIVE_CHAR)
DEFINE_JNI_ARRAYS(jshort, Short, JNI_PRIMITIVE_SHORT)
DEFINE_JNI_ARRAYS(jint, Int, JNI_PRIMITIVE_INT)
DEFINE_JNI_ARRAYS(jlong, Long, JNI_PRIMITIVE_LONG)
DEFINE_JNI_ARRAYS(jfloat, Float, JNI_PRIMITIVE_FLOAT)
DEFINE_JNI_ARRAYS(jdouble, Double, JNI_PRIMITIVE_DOUBLE)
#undef DEFINE_JNI_ARRAYS

static jobjectArray new_object_array(JNIEnv *environment, jsize length, jclass component_class,
                                     jobject initial_element) {
    JniObject *array_object =
        jni_object_create(JNI_OBJECT_REFERENCE_ARRAY, length, sizeof(jobject));
    const char *component = jni_object_checked((jobject)component_class)->text.class_name;
    char descriptor[516];
    snprintf(descriptor, sizeof(descriptor), component[0] == '[' ? "[%s" : "[L%s;", component);
    jclass array_class = jni_class_find(environment, descriptor);
    array_object->object_class = jni_object_checked((jobject)array_class);
    jni_ref_release(environment, (jobject)array_class);
    for (jsize index = 0; index < length; index++)
        ((jobject *)array_object->storage)[index] = jni_ref_keep(environment, initial_element);
    return (jobjectArray)array_object;
}
static jobject get_object_array_element(JNIEnv *environment, jobjectArray array, jsize index) {
    JniObject *array_object = jni_object_checked((jobject)array);
    if (array_object->kind != JNI_OBJECT_REFERENCE_ARRAY)
        port_fail("Expected object array");
    check_array_bounds(array_object, index, 1);
    return jni_ref_keep(environment, ((jobject *)array_object->storage)[index]);
}
static void set_object_array_element(JNIEnv *environment, jobjectArray array, jsize index,
                                     jobject element) {
    JniObject *array_object = jni_object_checked((jobject)array);
    if (array_object->kind != JNI_OBJECT_REFERENCE_ARRAY)
        port_fail("Expected object array");
    check_array_bounds(array_object, index, 1);
    jni_ref_keep(environment, element);
    jni_ref_release(environment, ((jobject *)array_object->storage)[index]);
    ((jobject *)array_object->storage)[index] = element;
}

void jni_install_arrays(struct JNINativeInterface *table) {
    table->GetArrayLength = array_length;
    table->NewObjectArray = new_object_array;
    table->GetObjectArrayElement = get_object_array_element;
    table->SetObjectArrayElement = set_object_array_element;
#define INSTALL_ARRAY(Name)                                                                        \
    table->New##Name##Array = new_##Name;                                                          \
    table->Get##Name##ArrayElements = get_##Name;                                                  \
    table->Release##Name##ArrayElements = release_##Name;                                          \
    table->Get##Name##ArrayRegion = region_##Name;                                                 \
    table->Set##Name##ArrayRegion = set_##Name
    INSTALL_ARRAY(Boolean);
    INSTALL_ARRAY(Byte);
    INSTALL_ARRAY(Char);
    INSTALL_ARRAY(Short);
    INSTALL_ARRAY(Int);
    INSTALL_ARRAY(Long);
    INSTALL_ARRAY(Float);
    INSTALL_ARRAY(Double);
#undef INSTALL_ARRAY
}
