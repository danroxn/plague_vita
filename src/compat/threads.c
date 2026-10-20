#include "compat/import_table.h"

#include <bionic/pthread.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

_Static_assert(sizeof(pthread_t) == 4, "ARM32 thread handle");
_Static_assert(sizeof(pthread_mutexattr_t) == 4, "ARM32 mutexattr storage");
_Static_assert(sizeof(struct sched_param) == 4 && offsetof(struct sched_param, sched_priority) == 0,
               "ARM32 scheduling parameter");
_Static_assert(sizeof(pthread_mutex_t_bionic) == 4 && sizeof(pthread_cond_t_bionic) == 4,
               "Bionic synchronization control word");

static pthread_mutex_t rwlock_initialization_guard = PTHREAD_MUTEX_INITIALIZER;
#define ENGINE_THREAD_STACK (2 * 1024 * 1024)
static int compat_thread_create(pthread_t *thread, const pthread_attr_t *attributes,
                                void *(*start)(void *), void *argument) {
    pthread_attr_t default_attributes;
    if (attributes)
        return pthread_create(thread, (pthread_attr_t *)attributes, start, argument);
    int result = pthread_attr_init(&default_attributes);
    if (result)
        return result;
    result = pthread_attr_setstacksize(&default_attributes, ENGINE_THREAD_STACK);
    if (!result)
        result = pthread_create(thread, &default_attributes, start, argument);
    pthread_attr_destroy(&default_attributes);
    return result;
}
static int compat_rw_init(pthread_rwlock_t **handle, const void *attributes) {
    if (!handle || attributes)
        return EINVAL;
    pthread_rwlock_t *lock = malloc(sizeof(*lock));
    if (!lock)
        return ENOMEM;
    int result = pthread_rwlock_init(lock, NULL);
    if (result) {
        free(lock);
        return result;
    }
    *handle = lock;
    return 0;
}
static pthread_rwlock_t *compat_rw_get(pthread_rwlock_t **handle) {
    if (!handle)
        return NULL;
    pthread_mutex_lock(&rwlock_initialization_guard);
    if (!*handle && compat_rw_init(handle, NULL)) {
        pthread_mutex_unlock(&rwlock_initialization_guard);
        return NULL;
    }
    pthread_rwlock_t *result = *handle;
    pthread_mutex_unlock(&rwlock_initialization_guard);
    return result;
}
static int compat_rw_destroy(pthread_rwlock_t **handle) {
    if (!handle || !*handle)
        return EINVAL;
    int result = pthread_rwlock_destroy(*handle);
    if (!result) {
        free(*handle);
        *handle = NULL;
    }
    return result;
}
static int compat_rw_read(pthread_rwlock_t **handle) {
    pthread_rwlock_t *lock = compat_rw_get(handle);
    return lock ? pthread_rwlock_rdlock(lock) : ENOMEM;
}
static int compat_rw_write(pthread_rwlock_t **handle) {
    pthread_rwlock_t *lock = compat_rw_get(handle);
    return lock ? pthread_rwlock_wrlock(lock) : ENOMEM;
}
static int compat_rw_unlock(pthread_rwlock_t **handle) {
    return handle && *handle ? pthread_rwlock_unlock(*handle) : EINVAL;
}

static const PortImportEntry imports[] = {
    {"pthread_create", (uintptr_t)(compat_thread_create)},
    {"pthread_detach", (uintptr_t)(pthread_detach)},
    {"pthread_equal", (uintptr_t)(pthread_equal_soloader)},
    {"pthread_getschedparam", (uintptr_t)(pthread_getschedparam)},
    {"pthread_join", (uintptr_t)(pthread_join)},
    {"pthread_kill", (uintptr_t)(pthread_kill)},
    {"pthread_self", (uintptr_t)(pthread_self)},
    {"pthread_setschedparam", (uintptr_t)(pthread_setschedparam)},
    {"pthread_once", (uintptr_t)(pthread_once_soloader)},
    {"pthread_cond_init", (uintptr_t)(pthread_cond_init_soloader)},
    {"pthread_cond_destroy", (uintptr_t)(pthread_cond_destroy_soloader)},
    {"pthread_cond_wait", (uintptr_t)(pthread_cond_wait_soloader)},
    {"pthread_cond_timedwait", (uintptr_t)(pthread_cond_timedwait_soloader)},
    {"pthread_cond_broadcast", (uintptr_t)(pthread_cond_broadcast_soloader)},
    {"pthread_cond_signal", (uintptr_t)(pthread_cond_signal_soloader)},
    {"pthread_mutex_init", (uintptr_t)(pthread_mutex_init_soloader)},
    {"pthread_mutex_destroy", (uintptr_t)(pthread_mutex_destroy_soloader)},
    {"pthread_mutex_lock", (uintptr_t)(pthread_mutex_lock_soloader)},
    {"pthread_mutex_trylock", (uintptr_t)(pthread_mutex_trylock_soloader)},
    {"pthread_mutex_unlock", (uintptr_t)(pthread_mutex_unlock_soloader)},
    {"pthread_mutexattr_init", (uintptr_t)(pthread_mutexattr_init)},
    {"pthread_mutexattr_destroy", (uintptr_t)(pthread_mutexattr_destroy)},
    {"pthread_mutexattr_settype", (uintptr_t)(pthread_mutexattr_settype)},
    {"pthread_rwlock_init", (uintptr_t)(compat_rw_init)},
    {"pthread_rwlock_destroy", (uintptr_t)(compat_rw_destroy)},
    {"pthread_rwlock_rdlock", (uintptr_t)(compat_rw_read)},
    {"pthread_rwlock_wrlock", (uintptr_t)(compat_rw_write)},
    {"pthread_rwlock_unlock", (uintptr_t)(compat_rw_unlock)},
};
const PortImportTable port_threads_imports = {imports, sizeof(imports) / sizeof(imports[0])};
