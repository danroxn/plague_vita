#include "services/methods.h"

#include "app/log.h"
#include "compat/jni/strings.h"


static jvalue cocojava_java_assert(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    char text[512];
    char file[256];
    port_jni_string_copy_utf8((jstring)arguments[0].l, text, sizeof(text));
    port_jni_string_copy_utf8((jstring)arguments[2].l, file, sizeof(file));
    port_fail("Java assertion: %s (%s:%d)", text, file, arguments[1].i);
}

static const PortServiceMethod diagnostics_entries[] = {
    {{"com/miniclip/nativeJNI/cocojava", "custom_logEvent",
      "(Ljava/lang/String;Ljava/lang/String;)V", 1},
     port_services_empty_result},
    {{"com/miniclip/nativeJNI/cocojava", "java_assert", "(Ljava/lang/String;ILjava/lang/String;)V",
      1},
     cocojava_java_assert},
};
const PortServiceMethodTable port_diagnostics_methods = {
    diagnostics_entries, sizeof(diagnostics_entries) / sizeof(diagnostics_entries[0])};
