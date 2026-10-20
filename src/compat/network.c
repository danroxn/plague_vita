#include "compat/import_table.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef uint32_t AndroidSocketLength;
struct sockaddr;
struct hostent;
struct addrinfo;
enum { ANDROID_EAI_FAIL = -4 };
static int network_unavailable(void) {
    errno = ENETUNREACH;
    return -1;
}
static int compat_socket(int domain, int type, int protocol) {
    (void)domain;
    (void)type;
    (void)protocol;
    return network_unavailable();
}
static int compat_socketpair(int domain, int type, int protocol, int descriptors[2]) {
    (void)domain;
    (void)type;
    (void)protocol;
    (void)descriptors;
    return network_unavailable();
}
static int compat_socket_address(int descriptor, const struct sockaddr *address,
                                 AndroidSocketLength length) {
    (void)descriptor;
    (void)address;
    (void)length;
    return network_unavailable();
}
static int compat_listen(int descriptor, int backlog) {
    (void)descriptor;
    (void)backlog;
    return network_unavailable();
}
static int compat_accept(int descriptor, struct sockaddr *address, AndroidSocketLength *length) {
    (void)descriptor;
    (void)address;
    (void)length;
    return network_unavailable();
}
static ssize_t compat_send(int descriptor, const void *buffer, size_t length, int flags) {
    (void)descriptor;
    (void)buffer;
    (void)length;
    (void)flags;
    return network_unavailable();
}
static ssize_t compat_sendto(int descriptor, const void *buffer, size_t length, int flags,
                             const struct sockaddr *address, AndroidSocketLength address_length) {
    (void)descriptor;
    (void)buffer;
    (void)length;
    (void)flags;
    (void)address;
    (void)address_length;
    return network_unavailable();
}
static ssize_t compat_recv(int descriptor, void *buffer, size_t length, int flags) {
    (void)descriptor;
    (void)buffer;
    (void)length;
    (void)flags;
    return network_unavailable();
}
static ssize_t compat_recvfrom(int descriptor, void *buffer, size_t length, int flags,
                               struct sockaddr *address, AndroidSocketLength *address_length) {
    (void)descriptor;
    (void)buffer;
    (void)length;
    (void)flags;
    (void)address;
    (void)address_length;
    return network_unavailable();
}
static struct hostent *compat_gethostbyname(const char *hostname) {
    (void)hostname;
    errno = ENETUNREACH;
    return NULL;
}
static struct hostent *compat_gethostbyaddr(const void *address, AndroidSocketLength length,
                                            int type) {
    (void)address;
    (void)length;
    (void)type;
    errno = ENETUNREACH;
    return NULL;
}
static int compat_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints,
                              struct addrinfo **result) {
    (void)node;
    (void)service;
    (void)hints;
    if (result)
        *result = NULL;
    return ANDROID_EAI_FAIL;
}
static void compat_freeaddrinfo(struct addrinfo *addresses) {
    (void)addresses;
}
static const char *compat_gai_strerror(int code) {
    (void)code;
    return "Networking unavailable";
}

static const PortImportEntry imports[] = {
    {"socket", (uintptr_t)(compat_socket)},
    {"socketpair", (uintptr_t)(compat_socketpair)},
    {"connect", (uintptr_t)(compat_socket_address)},
    {"bind", (uintptr_t)(compat_socket_address)},
    {"listen", (uintptr_t)(compat_listen)},
    {"accept", (uintptr_t)(compat_accept)},
    {"send", (uintptr_t)(compat_send)},
    {"sendto", (uintptr_t)(compat_sendto)},
    {"recv", (uintptr_t)(compat_recv)},
    {"recvfrom", (uintptr_t)(compat_recvfrom)},
    {"gethostbyname", (uintptr_t)(compat_gethostbyname)},
    {"gethostbyaddr", (uintptr_t)(compat_gethostbyaddr)},
    {"getaddrinfo", (uintptr_t)(compat_getaddrinfo)},
    {"freeaddrinfo", (uintptr_t)(compat_freeaddrinfo)},
    {"gai_strerror", (uintptr_t)(compat_gai_strerror)},
};
const PortImportTable port_network_imports = {imports, sizeof(imports) / sizeof(imports[0])};
