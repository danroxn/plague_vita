#include "services/methods.h"

#include "runtime/callbacks.h"
#include "app/log.h"
#include "compat/jni/runtime.h"
#include "services/async.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    jlong id;
} NativeRunnableState;
static void enqueue_runnable_id(jlong native_id) {
    jvalue callback_arguments = {.j = native_id};
    port_async_schedule(&port_game_callback_runnable, NULL, &callback_arguments);
}
static void enqueue_runnable(jobject runnable) {
    NativeRunnableState *state = port_jni_payload(runnable);
    if (!state || strcmp(port_jni_class_name(runnable), "com/miniclip/utils/NativeRunnable"))
        port_fail("Unsupported runnable class");
    enqueue_runnable_id(state->id);
}
static jvalue queue_runnable(const PortJniCall *call) {
    enqueue_runnable(call->arguments[0].l);
    return (jvalue){0};
}
static jvalue queue_context_runnable(const PortJniCall *call) {
    enqueue_runnable(call->arguments[1].l);
    return (jvalue){0};
}
static jvalue native_runnable_construct(const PortJniCall *call) {
    NativeRunnableState *state = malloc(sizeof(*state));
    if (!state)
        port_fail("Runnable allocation failed");
    state->id = call->arguments[0].j;
    port_jni_set_payload(call->receiver, state, free);
    return (jvalue){0};
}
static jvalue native_runnable_run(const PortJniCall *call) {
    NativeRunnableState *state = port_jni_payload(call->receiver);
    if (!state)
        port_fail("NativeRunnable missing native ID");
    enqueue_runnable_id(state->id);
    return (jvalue){0};
}
static const PortServiceMethod threading_entries[] = {
    {{"com/miniclip/framework/Miniclip", "queueEvent",
      "(Lcom/miniclip/framework/ThreadingContext;Ljava/lang/Runnable;)V", 1},
     queue_context_runnable},
    {{"com/miniclip/utils/NativeRunnable", "<init>", "(J)V", 0}, native_runnable_construct},
    {{"com/miniclip/utils/NativeRunnable", "run", "()V", 0}, native_runnable_run},
    {{"com/miniclip/framework/Miniclip", "queueEvent", "(Ljava/lang/Runnable;)V", 1},
     queue_runnable},
    {{"com/miniclip/framework/ThreadingContext", "queueEvent", "(Ljava/lang/Runnable;)V", 0},
     queue_runnable},
    {{"com/miniclip/framework/ThreadingContext", "queueEvent",
      "(Lcom/miniclip/framework/ThreadingContext;Ljava/lang/Runnable;)V", 0},
     queue_context_runnable},
    {{"android/app/Activity", "queueEvent",
      "(Lcom/miniclip/framework/ThreadingContext;Ljava/lang/Runnable;)V", 0},
     queue_context_runnable},
    {{"android/app/Activity", "queueEvent", "(Ljava/lang/Runnable;)V", 0}, queue_runnable},
};
const PortServiceMethodTable port_threading_methods = {
    threading_entries, sizeof(threading_entries) / sizeof(threading_entries[0])};
