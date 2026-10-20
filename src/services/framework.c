#include "services/framework.h"

#include "services/methods.h"
#include "compat/android_contract.h"
#include "app/log.h"
#include "compat/jni/runtime.h"
#include "compat/jni/strings.h"
#include "runtime/paths.h"
#include "app/lifecycle.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *actual;
    const char *expected;
} FacadeClassRelation;
static const FacadeClassRelation facade_relations[] = {
    {"java/util/HashMap", "java/util/Map"},
    {"java/util/HashSet", "java/util/Set"},
    {"java/util/HashSet", "java/util/Collection"},
    {"java/lang/Integer", "java/lang/Number"},
    {"java/lang/Integer", "java/lang/Comparable"},
    {"com/miniclip/utils/NativeRunnable", "java/lang/Runnable"},
};
static jboolean facade_is_assignable(const char *actual, const char *expected) {
    for (size_t index = 0; index < sizeof(facade_relations) / sizeof(facade_relations[0]); ++index)
        if (!strcmp(actual, facade_relations[index].actual) &&
            !strcmp(expected, facade_relations[index].expected))
            return JNI_TRUE;
    return JNI_FALSE;
}

enum {
    FIELD_SDK_INT,
    FIELD_MAIN_CONTEXT,
    FIELD_UI_CONTEXT,
    FIELD_ANDROID_UI_CONTEXT
};
typedef struct {
    const char *owner;
    const char *name;
    const char *descriptor;
} FacadeFieldInfo;
static const FacadeFieldInfo facade_fields[] = {
    [FIELD_SDK_INT] = {"android/os/Build$VERSION", "SDK_INT", "I"},
    [FIELD_MAIN_CONTEXT] = {"com/miniclip/framework/ThreadingContext", "Main",
                            "Lcom/miniclip/framework/ThreadingContext;"},
    [FIELD_UI_CONTEXT] = {"com/miniclip/framework/ThreadingContext", "UiThread",
                          "Lcom/miniclip/framework/ThreadingContext;"},
    [FIELD_ANDROID_UI_CONTEXT] = {"com/miniclip/framework/ThreadingContext", "AndroidUi",
                                  "Lcom/miniclip/framework/ThreadingContext;"},
};
static jobject field_contexts[3];
static jfieldID find_facade_field(const char *owner, const char *name, const char *descriptor) {
    for (size_t index = 0; index < sizeof(facade_fields) / sizeof(facade_fields[0]); ++index) {
        const FacadeFieldInfo *field = &facade_fields[index];
        if (!strcmp(owner, field->owner) && !strcmp(name, field->name) &&
            !strcmp(descriptor, field->descriptor))
            return (jfieldID)field;
    }
    return NULL;
}
static jint read_facade_int(jfieldID field) {
    if (field == (jfieldID)&facade_fields[FIELD_SDK_INT])
        return PORT_ANDROID_API_LEVEL;
    port_fail("Unknown JNI static integer field");
}
static jobject read_facade_object(jclass declaring_class, jfieldID field) {
    for (int index = 0; index < 3; ++index) {
        if (field == (jfieldID)&facade_fields[FIELD_MAIN_CONTEXT + index]) {
            if (!field_contexts[index])
                field_contexts[index] = port_jni->AllocObject(&port_jni, declaring_class);
            return port_jni->NewLocalRef(&port_jni, field_contexts[index]);
        }
    }
    port_fail("Unknown JNI static object field");
}
const PortJniMetadata port_framework_jni_metadata = {
    .is_assignable = facade_is_assignable,
    .find_field = find_facade_field,
    .read_static_int = read_facade_int,
    .read_static_object = read_facade_object,
};

static jobject assets_object, files_object, class_loader, activity;
static jvalue application_files(const PortJniCall *call) {
    (void)call;
    if (!files_object)
        files_object = port_jni_attach_text(port_jni_new_instance("java/io/File"), PORT_FILES);
    return (jvalue){.l = port_jni->NewLocalRef(&port_jni, files_object)};
}
static jvalue application_assets(const PortJniCall *call) {
    (void)call;
    if (!assets_object)
        assets_object = port_jni_new_instance("android/content/res/AssetManager");
    return (jvalue){.l = port_jni->NewLocalRef(&port_jni, assets_object)};
}
static jvalue application_path(const PortJniCall *call) {
    (void)call;
    return (jvalue){.l = port_jni_string_from_utf8(PORT_FILES)};
}
static jvalue get_activity(const PortJniCall *call) {
    (void)call;
    if (!activity)
        activity = port_jni_new_instance("android/app/Activity");
    return (jvalue){.l = port_jni->NewLocalRef(&port_jni, activity)};
}
static jvalue get_class_loader(const PortJniCall *call) {
    (void)call;
    if (!class_loader)
        class_loader = port_jni_new_instance("java/lang/ClassLoader");
    return (jvalue){.l = port_jni->NewLocalRef(&port_jni, class_loader)};
}
static jvalue load_class(const PortJniCall *call) {
    char class_name[512];
    port_jni_string_copy_utf8(call->arguments[0].l, class_name, sizeof(class_name));
    for (char *character = class_name; *character; character++)
        if (*character == '.')
            *character = '/';
    return (jvalue){.l = port_jni->FindClass(&port_jni, class_name)};
}
static jvalue class_name(const PortJniCall *call) {
    char name[512];
    snprintf(name, sizeof(name), "%s", port_jni_class_name(call->receiver));
    for (char *character = name; *character; character++)
        if (*character == '/')
            *character = '.';
    return (jvalue){.l = port_jni_string_from_utf8(name)};
}
static jvalue get_threading_context(const PortJniCall *call) {
    (void)call;
    static jobject context;
    if (!context)
        context = port_jni_new_instance("com/miniclip/framework/ThreadingContext");
    return (jvalue){.l = port_jni->NewLocalRef(&port_jni, context)};
}
static jvalue fallback_object(const PortJniCall *call) {
    (void)call;
    return (jvalue){.l = port_jni_new_instance("java/lang/Object")};
}
static jvalue fallback_directory(const PortJniCall *call) {
    (void)call;
    return (jvalue){.l = port_jni_attach_text(port_jni_new_instance("java/io/File"), PORT_FILES)};
}
static jvalue fallback_object_class(const PortJniCall *call) {
    return (jvalue){.l = port_jni->GetObjectClass(&port_jni, call->receiver)};
}
static jvalue is_exiting(const PortJniCall *call) {
    if (*call->return_type == 'Z')
        return (jvalue){.z = port_app_exit_requested()};
    return port_services_unavailable_result(call);
}
typedef struct {
    const char *name;
    PortJniHandler handler;
} FrameworkFallback;
static const FrameworkFallback class_operations[] = {
    {"getClassLoader", get_class_loader},
    {"loadClass", load_class},
    {"getName", class_name},
};
static const FrameworkFallback context_operations[] = {
    {"getAssets", fallback_object},
    {"getFilesDir", fallback_directory},
    {"getClass", fallback_object_class},
    {"isDestroyed", is_exiting},
};
static PortJniHandler find_fallback(const FrameworkFallback *entries, size_t count,
                                    const char *name) {
    for (size_t i = 0; i < count; i++)
        if (!strcmp(entries[i].name, name))
            return entries[i].handler;
    return NULL;
}
PortJniHandler port_framework_resolve_fallback(const PortJniMethodKey *method) {
    const char *owner = method->owner, *name = method->name;
    int external_application = !strcmp(owner, "com/miniclip/framework/ExternalApplication");
    if (external_application) {
        if (!strcmp(name, "getFilesDir"))
            return application_files;
        if (!strcmp(name, "getAssets"))
            return application_assets;
        if (!strcmp(name, "getAbsolutePath"))
            return application_path;
    }
    if (!strcmp(owner, "java/lang/String") && !strcmp(name, "<init>") &&
        !strcmp(method->descriptor, "()V"))
        return port_services_empty_result;
    if (!strcmp(owner, "com/miniclip/framework/Miniclip") && !strcmp(name, "getActivity"))
        return get_activity;
    int activity_owner = !strcmp(owner, "android/app/Activity");
    int context_owner = !strcmp(owner, "android/content/Context");
    int loader_owner = !strcmp(owner, "java/lang/ClassLoader");
    if (activity_owner || context_owner || loader_owner || external_application ||
        !strcmp(owner, "java/lang/Class")) {
        PortJniHandler handler = find_fallback(
            class_operations, sizeof(class_operations) / sizeof(class_operations[0]), name);
        if (handler)
            return handler;
    }
    if (!strcmp(owner, "com/miniclip/framework/ThreadingContext") &&
        (!strcmp(name, "getInstance") || !strcmp(name, "Main")))
        return get_threading_context;
    if (activity_owner || context_owner || loader_owner || !strcmp(owner, "java/lang/Object")) {
        PortJniHandler handler = find_fallback(
            context_operations, sizeof(context_operations) / sizeof(context_operations[0]), name);
        return handler ? handler : port_services_unavailable_result;
    }
    return NULL;
}
static const PortServiceMethod methods[] = {
    {{"com/miniclip/framework/Miniclip", "getActivity", "()Landroid/app/Activity;", 1},
     get_activity},
    {{"com/miniclip/framework/ExternalApplication", "getFilesDir", "()Ljava/io/File;", 0},
     application_files},
    {{"com/miniclip/framework/ExternalApplication", "getAssets",
      "()Landroid/content/res/AssetManager;", 0},
     application_assets},
    {{"com/miniclip/framework/ExternalApplication", "getAbsolutePath", "()Ljava/lang/String;", 0},
     application_path},
    {{"android/app/Activity", "getClassLoader", "()Ljava/lang/ClassLoader;", 0}, get_class_loader},
    {{"java/lang/ClassLoader", "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;", 0},
     load_class},
    {{"java/lang/Class", "getName", "()Ljava/lang/String;", 0}, class_name},
    {{"java/lang/String", "<init>", "()V", 0}, port_services_empty_result},

};
const PortServiceMethodTable port_framework_methods = {methods,
                                                       sizeof(methods) / sizeof(methods[0])};
