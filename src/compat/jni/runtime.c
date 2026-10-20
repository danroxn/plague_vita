#include "compat/jni/runtime.h"

#include "app/log.h"
#include "compat/jni/internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static struct JNINativeInterface table;
static struct JNIInvokeInterface vm_table;
JNIEnv port_jni = &table;
JavaVM port_vm = &vm_table;
static jint get_jni_version(JNIEnv *environment) {
    (void)environment;
    return JNI_VERSION_1_6;
}
static jobject get_pending_exception(JNIEnv *environment) {
    (void)environment;
    return NULL;
}
static jboolean has_pending_exception(JNIEnv *environment) {
    (void)environment;
    return JNI_FALSE;
}
static void clear_pending_exception(JNIEnv *environment) {
    (void)environment;
}
static jint ensure_local_capacity(JNIEnv *environment, jint capacity) {
    (void)environment;
    return capacity >= 0 ? JNI_OK : JNI_ERR;
}
static jint get_java_vm(JNIEnv *environment, JavaVM **virtual_machine_out) {
    (void)environment;
    *virtual_machine_out = &port_vm;
    return JNI_OK;
}
static jint get_vm_environment(JavaVM *virtual_machine, void **environment_out,
                               jint requested_version) {
    (void)virtual_machine;
    if (requested_version < JNI_VERSION_1_1 || requested_version > JNI_VERSION_1_6)
        return JNI_EVERSION;
    *environment_out = &port_jni;
    return JNI_OK;
}
static jint attach_current_thread(JavaVM *virtual_machine, JNIEnv **environment_out,
                                  void *attach_arguments) {
    (void)virtual_machine;
    (void)attach_arguments;
    *environment_out = &port_jni;
    return JNI_OK;
}
static jint detach_current_thread(JavaVM *virtual_machine) {
    (void)virtual_machine;
    return JNI_OK;
}
static void unsupported_jni_slot(void) {
    port_fail("Unimplemented JNI slot called from %p", __builtin_return_address(0));
}

void port_jni_init(PortJniResolver resolve_method, const PortJniMetadata *metadata) {
    if (!resolve_method || !metadata || !metadata->is_assignable || !metadata->find_field ||
        !metadata->read_static_int || !metadata->read_static_object)
        port_fail("JNI requires method and metadata providers");
    uintptr_t trap = (uintptr_t)unsupported_jni_slot;
    for (size_t i = 4 * sizeof(void *); i + sizeof(trap) <= sizeof(table); i += sizeof(trap))
        memcpy((char *)&table + i, &trap, sizeof(trap));
    jni_install_objects(&table, metadata);
    jni_install_dispatch(&table, resolve_method);
    jni_install_strings(&table);
    jni_install_arrays(&table);
    table.GetVersion = get_jni_version;
    table.ExceptionOccurred = get_pending_exception;
    table.ExceptionCheck = has_pending_exception;
    table.ExceptionClear = clear_pending_exception;
    table.EnsureLocalCapacity = ensure_local_capacity;
    table.GetJavaVM = get_java_vm;
    vm_table.GetEnv = get_vm_environment;
    vm_table.AttachCurrentThread = attach_current_thread;
    vm_table.AttachCurrentThreadAsDaemon = attach_current_thread;
    vm_table.DetachCurrentThread = detach_current_thread;
}
