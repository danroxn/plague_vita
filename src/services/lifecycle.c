#include "services/methods.h"

#include "app/lifecycle.h"

static jvalue event_dispatcher_register_for_application_events(const PortJniCall *call) {
    (void)call;
    port_app_register_lifecycle_events();
    return (jvalue){0};
}
static jvalue clear_renderer_set_animation_interval(const PortJniCall *call) {
    port_app_set_animation_interval_seconds(call->arguments[0].d);
    return (jvalue){0};
}
static jvalue cocojava_exit_application(const PortJniCall *call) {
    (void)call;
    port_app_request_exit();
    return (jvalue){0};
}

static const PortServiceMethod lifecycle_entries[] = {
    {{"com/miniclip/events/EventDispatcher", "registerForApplicationEvents", "()V", 1},
     event_dispatcher_register_for_application_events},
    {{"com/miniclip/nativeJNI/ClearRenderer", "setAnimationInterval", "(D)V", 1},
     clear_renderer_set_animation_interval},
    {{"com/miniclip/nativeJNI/cocojava", "cleanAndroidData", "()V", 1},
     cocojava_exit_application},
    {{"com/miniclip/nativeJNI/cocojava", "exitApplication", "()V", 1}, cocojava_exit_application},
};
const PortServiceMethodTable port_lifecycle_methods = {
    lifecycle_entries, sizeof(lifecycle_entries) / sizeof(lifecycle_entries[0])};
