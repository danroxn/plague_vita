#ifndef PLAGUE_SERVICES_METHODS_H
#define PLAGUE_SERVICES_METHODS_H
#include "compat/jni/method.h"
#include <stddef.h>
typedef struct {
    PortJniMethodKey key;
    PortJniHandler invoke;
} PortServiceMethod;
typedef struct {
    const PortServiceMethod *entries;
    size_t count;
} PortServiceMethodTable;

jvalue port_services_empty_result(const PortJniCall *call);
jvalue port_services_true_result(const PortJniCall *call);
jvalue port_services_empty_text(const PortJniCall *call);
jvalue port_services_unavailable_result(const PortJniCall *call);

extern const PortServiceMethodTable port_app_info_methods;
extern const PortServiceMethodTable port_audio_methods;
extern const PortServiceMethodTable port_clipboard_methods;
extern const PortServiceMethodTable port_collections_methods;
extern const PortServiceMethodTable port_crypto_methods;
extern const PortServiceMethodTable port_diagnostics_methods;
extern const PortServiceMethodTable port_dialogs_methods;
extern const PortServiceMethodTable port_files_methods;
extern const PortServiceMethodTable port_framework_methods;
extern const PortServiceMethodTable port_lifecycle_methods;
extern const PortServiceMethodTable port_network_methods;
extern const PortServiceMethodTable port_offline_methods;
extern const PortServiceMethodTable port_purchases_methods;
extern const PortServiceMethodTable port_threading_methods;
#endif
