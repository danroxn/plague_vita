#include "services/dispatch.h"

#include "compat/jni/strings.h"
#include "services/async.h"
#include "services/dialogs.h"
#include "services/files.h"
#include "services/framework.h"
#include "services/methods.h"
#include "services/network.h"
#include "services/offline.h"
#include "services/purchases.h"

#include <string.h>

jvalue port_services_empty_result(const PortJniCall *call) {
    (void)call;
    return (jvalue){0};
}
jvalue port_services_true_result(const PortJniCall *call) {
    (void)call;
    return (jvalue){.z = JNI_TRUE};
}
jvalue port_services_empty_text(const PortJniCall *call) {
    (void)call;
    return (jvalue){.l = port_jni_string_from_utf8("")};
}
jvalue port_services_unavailable_result(const PortJniCall *call) {
    if (!strcmp(call->return_type, "Ljava/lang/String;"))
        return port_services_empty_text(call);
    return port_services_empty_result(call);
}

int port_services_poll(void) {
    port_network_poll();
    port_async_drain();
    port_purchases_poll();
    return port_dialogs_poll();
}
static const PortServiceMethodTable *const method_tables[] = {
    &port_collections_methods, &port_files_methods,       &port_network_methods,
    &port_lifecycle_methods,   &port_clipboard_methods,   &port_threading_methods,
    &port_offline_methods,     &port_dialogs_methods,
    &port_app_info_methods,    &port_diagnostics_methods, &port_crypto_methods,
    &port_audio_methods,       &port_purchases_methods,   &port_framework_methods,
};
PortJniHandler port_services_resolve_method(const PortJniMethodKey *method) {
    for (size_t table_index = 0; table_index < sizeof(method_tables) / sizeof(method_tables[0]);
         table_index++) {
        const PortServiceMethodTable *table = method_tables[table_index];
        for (size_t i = 0; i < table->count; i++) {
            const PortServiceMethod *entry = &table->entries[i];
            if (entry->key.is_static == method->is_static &&
                !strcmp(entry->key.owner, method->owner) &&
                !strcmp(entry->key.name, method->name) &&
                !strcmp(entry->key.descriptor, method->descriptor))
                return entry->invoke;
        }
    }
    PortJniHandler fallback = port_files_resolve_fallback(method);
    if (!fallback)
        fallback = port_offline_resolve_fallback(method);
    if (!fallback)
        fallback = port_framework_resolve_fallback(method);
    return fallback;
}
