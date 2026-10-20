#include "services/network.h"

#include "services/methods.h"
#include "runtime/callbacks.h"
#include "app/log.h"
#include "compat/jni/runtime.h"
#include "services/async.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define HTTP_REQUEST_CAPACITY 16
#define NETWORK_UNAVAILABLE 1
static const char unavailable_message[] = "Network disabled in Vita port";
typedef struct {
    int id;
    int completion_pending;
} HttpRequest;
typedef struct SocketState {
    struct SocketState *next;
    jlong native_handle;
    int disconnect_pending;
} SocketState;
static struct {
    HttpRequest requests[HTTP_REQUEST_CAPACITY];
    SocketState *sockets;
    int next_id;
} network = {.next_id = 1};
static void destroy_socket(void *payload) {
    SocketState *socket = payload;
    SocketState **entry = &network.sockets;
    while (*entry && *entry != socket)
        entry = &(*entry)->next;
    if (*entry)
        *entry = socket->next;
    free(socket);
}
void port_network_poll(void) {
    for (unsigned i = 0; i < HTTP_REQUEST_CAPACITY; i++) {
        HttpRequest *request = &network.requests[i];
        if (!request->id || !request->completion_pending)
            continue;
        jstring message = port_jni->NewStringUTF(&port_jni, unavailable_message);
        jvalue callback_arguments[3] = {
            {.i = request->id}, {.l = message}, {.i = NETWORK_UNAVAILABLE}};
        port_async_schedule(&port_game_callback_http_failed, NULL, callback_arguments);
        port_jni->DeleteLocalRef(&port_jni, message);
        *request = (HttpRequest){0};
    }
    for (SocketState *socket = network.sockets; socket; socket = socket->next) {
        if (!socket->disconnect_pending)
            continue;
        socket->disconnect_pending = 0;
        if (!socket->native_handle)
            continue;
        jstring message = port_jni->NewStringUTF(&port_jni, unavailable_message);
        jvalue callback_arguments[3] = {
            {.j = socket->native_handle}, {.i = NETWORK_UNAVAILABLE}, {.l = message}};
        port_async_schedule(&port_game_callback_socket_disconnected, NULL, callback_arguments);
        port_jni->DeleteLocalRef(&port_jni, message);
    }
}
static jvalue http_connection_cancel_connection(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    jvalue result = {0};
    for (unsigned i = 0; i < HTTP_REQUEST_CAPACITY; i++) {
        HttpRequest *request = &network.requests[i];
        if ((!request->id) || request->id != arguments[0].i)
            continue;
        *request = (HttpRequest){0};
        result.z = JNI_TRUE;
        break;
    }

    return result;
}

static jvalue http_connection_new_connection(const PortJniCall *call) {
    (void)call;
    jvalue result = {0};
    result.i = -1;
    for (unsigned i = 0; i < HTTP_REQUEST_CAPACITY; i++) {
        if (network.requests[i].id)
            continue;
        int id;
        for (;;) {
            id = network.next_id;
            network.next_id = (network.next_id == INT_MAX) ? (1) : (network.next_id + 1);
            unsigned candidate_index;
            for (candidate_index = 0; candidate_index < HTTP_REQUEST_CAPACITY &&
                                      network.requests[candidate_index].id != id;
                 candidate_index++)
                ;

            if (candidate_index == HTTP_REQUEST_CAPACITY)
                break;
        }

        network.requests[i] = (HttpRequest){.id = id};
        result.i = id;
        break;
    }

    return result;
}

static jvalue http_connection_start_connection(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    jvalue result = {0};
    for (unsigned i = 0; i < HTTP_REQUEST_CAPACITY; i++) {
        HttpRequest *request = &network.requests[i];
        if ((!request->id) || request->id != arguments[0].i)
            continue;
        if (!request->completion_pending) {
            request->completion_pending = 1;
            result.z = 1;
        }
        break;
    }

    return result;
}

static jvalue java_socket_construct(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    jobject receiver = call->receiver;
    SocketState *socket = calloc(1, sizeof(*socket));
    if (!socket)
        port_fail("SocketState state allocation failed");
    socket->native_handle = arguments[3].j;
    port_jni_set_payload(receiver, socket, destroy_socket);
    socket->next = network.sockets;
    network.sockets = socket;
    return (jvalue){0};
}

static SocketState *socket_payload(jobject object) {
    const char *name = port_jni_class_name(object);
    if (!name || strcmp(name, "com/miniclip/network/JavaSocket"))
        port_fail("Expected JavaSocket facade");
    return port_jni_payload(object);
}

static jvalue java_socket_connect(const PortJniCall *call) {
    jobject receiver = call->receiver;
    jvalue result = {0};
    SocketState *socket = socket_payload(receiver);
    if (!socket)
        return result;
    if (!socket->disconnect_pending) {
        socket->disconnect_pending = 1;
        result.z = 1;
    }
    return result;
}

static jvalue java_socket_is_connected(const PortJniCall *call) {
    (void)socket_payload(call->receiver);
    return (jvalue){0};
}

static jvalue java_socket_set_native_object(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    jobject receiver = call->receiver;
    SocketState *socket = socket_payload(receiver);
    if (!socket)
        return (jvalue){0};
    socket->native_handle = arguments[0].j;
    return (jvalue){0};
}

static const PortServiceMethod network_entries[] = {
    {{"com/miniclip/network/HttpConnection", "cancelConnection", "(I)Z", 1},
     http_connection_cancel_connection},
    {{"com/miniclip/network/HttpConnection", "newConnection",
      "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ILjava/util/HashMap;)I", 1},
     http_connection_new_connection},
    {{"com/miniclip/network/HttpConnection", "startConnection", "(I)Z", 1},
     http_connection_start_connection},
    {{"com/miniclip/network/JavaSocket", "<init>",
      "(Ljava/lang/String;Ljava/lang/String;IJILjava/lang/String;)V", 0},
     java_socket_construct},
    {{"com/miniclip/network/JavaSocket", "addTrustedCertificate", "([B)Z", 1},
     port_services_empty_result},
    {{"com/miniclip/network/JavaSocket", "clearTrustedCertificates", "()V", 1},
     port_services_empty_result},
    {{"com/miniclip/network/JavaSocket", "connect", "()Z", 0}, java_socket_connect},
    {{"com/miniclip/network/JavaSocket", "isConnected", "()Z", 0}, java_socket_is_connected},
    {{"com/miniclip/network/JavaSocket", "sendData", "([B)Z", 0}, java_socket_is_connected},
    {{"com/miniclip/network/JavaSocket", "setNativeObject", "(J)V", 0},
     java_socket_set_native_object},
    {{"com/miniclip/network/JavaSocket", "setWorkBufferSize", "(I)V", 0}, java_socket_is_connected},
};
const PortServiceMethodTable port_network_methods = {
    network_entries, sizeof(network_entries) / sizeof(network_entries[0])};
