#include "services/async.h"

#include "app/log.h"
#include "compat/jni/runtime.h"

#include <pthread.h>
#include <stddef.h>

enum { ASYNC_ARGUMENT_CAPACITY = 8, ASYNC_QUEUE_CAPACITY = 128 };
typedef struct {
    const GameCallback *callback;
    jobject receiver_reference;
    jvalue callback_arguments[ASYNC_ARGUMENT_CAPACITY];
    unsigned argument_count;
    unsigned retained_reference_mask;
} QueuedCallback;
static struct {
    QueuedCallback entries[ASYNC_QUEUE_CAPACITY];
    unsigned head_index;
    unsigned queued_count;
    pthread_mutex_t mutex;
} callback_queue = {.mutex = PTHREAD_MUTEX_INITIALIZER};

void port_async_schedule(const GameCallback *callback, jobject receiver, const jvalue *arguments) {
    const GameCallbackInfo *info = port_game_callback_info(callback);
    QueuedCallback queued = {.callback = callback,
                             .receiver_reference = port_jni->NewGlobalRef(&port_jni, receiver),
                             .argument_count = info->argument_count,
                             .retained_reference_mask = info->reference_mask};
    if (info->argument_count > ASYNC_ARGUMENT_CAPACITY)
        port_fail("Async service has too many arguments");
    for (unsigned index = 0; index < info->argument_count; index++) {
        queued.callback_arguments[index] = arguments[index];
        if (queued.retained_reference_mask & (1u << index))
            queued.callback_arguments[index].l =
                port_jni->NewGlobalRef(&port_jni, arguments[index].l);
    }
    pthread_mutex_lock(&callback_queue.mutex);
    if (callback_queue.queued_count == ASYNC_QUEUE_CAPACITY) {
        pthread_mutex_unlock(&callback_queue.mutex);
        port_fail("Async service queue full");
    }
    unsigned tail_index =
        (callback_queue.head_index + callback_queue.queued_count++) % ASYNC_QUEUE_CAPACITY;
    callback_queue.entries[tail_index] = queued;
    pthread_mutex_unlock(&callback_queue.mutex);
}
void port_async_drain(void) {
    pthread_mutex_lock(&callback_queue.mutex);
    unsigned initial_budget = callback_queue.queued_count;
    pthread_mutex_unlock(&callback_queue.mutex);
    while (initial_budget--) {
        pthread_mutex_lock(&callback_queue.mutex);
        if (!callback_queue.queued_count) {
            pthread_mutex_unlock(&callback_queue.mutex);
            break;
        }
        QueuedCallback queued = callback_queue.entries[callback_queue.head_index];
        callback_queue.head_index = (callback_queue.head_index + 1) % ASYNC_QUEUE_CAPACITY;
        callback_queue.queued_count--;
        pthread_mutex_unlock(&callback_queue.mutex);
        port_game_callback_invoke(queued.callback, queued.receiver_reference,
                                  queued.callback_arguments);
        for (unsigned index = 0; index < queued.argument_count; index++)
            if (queued.retained_reference_mask & (1u << index))
                port_jni->DeleteGlobalRef(&port_jni, queued.callback_arguments[index].l);
        port_jni->DeleteGlobalRef(&port_jni, queued.receiver_reference);
    }
}
