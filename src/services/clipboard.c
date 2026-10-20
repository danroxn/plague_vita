#include "services/methods.h"

#include "compat/jni/runtime.h"
#include "compat/jni/strings.h"

#include <string.h>

static jstring clipboard;
static jvalue clipboard_copy_text(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    jstring text = port_jni->NewGlobalRef(&port_jni, arguments[0].l);
    port_jni->DeleteGlobalRef(&port_jni, clipboard);
    clipboard = text;
    return (jvalue){.z = 1};
}

static jvalue clipboard_has_text(const PortJniCall *call) {
    (void)call;
    return (jvalue){.z = clipboard && port_jni->GetStringLength(&port_jni, clipboard) > 0};
}

static jvalue clipboard_paste_text(const PortJniCall *call) {
    (void)call;
    return (jvalue){.l = (clipboard) ? (port_jni->NewLocalRef(&port_jni, clipboard))
                                     : (port_jni_string_from_utf8(""))};
}

static const PortServiceMethod clipboard_entries[] = {
    {{"com/miniclip/clipboard/Clipboard", "copyText", "(Ljava/lang/String;)Z", 1},
     clipboard_copy_text},
    {{"com/miniclip/clipboard/Clipboard", "hasText", "()Z", 1}, clipboard_has_text},
    {{"com/miniclip/clipboard/Clipboard", "isSupported", "()Z", 1}, port_services_true_result},
    {{"com/miniclip/clipboard/Clipboard", "pasteText", "()Ljava/lang/String;", 1},
     clipboard_paste_text},
    {{"com/miniclip/nativeJNI/cocojava", "setClipboardText", "(Ljava/lang/String;)V", 1},
     clipboard_copy_text},
};
const PortServiceMethodTable port_clipboard_methods = {
    clipboard_entries, sizeof(clipboard_entries) / sizeof(clipboard_entries[0])};
