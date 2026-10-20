
/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022      GrapheneCt
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See third_party/bionic/LICENSE for details.
 */

#include <bionic/pthread.h>

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Android stores a four-byte control word where VitaSDK expects a pointer to
 * its own pthread object. The registry owns each separately allocated native
 * control until successful destruction. Initialization and publication share
 * one native mutex, including the first use of a static Android initializer. */
enum { MAX_THREAD_OBJECTS = 1024, THREAD_OBJECT_HINTS = 64 };
static void *initialized_objects[MAX_THREAD_OBJECTS];
static pthread_mutex_t registry_guard = PTHREAD_MUTEX_INITIALIZER;
static unsigned registry_generation = 1;
typedef struct {
    const void *object;
    const void *native;
    unsigned generation;
} ThreadObjectHint;
static _Thread_local ThreadObjectHint thread_object_hints[THREAD_OBJECT_HINTS];

static ThreadObjectHint *object_hint(const void *object) {
    uintptr_t hash = (uintptr_t)object >> 2;
    hash ^= hash >> 11;
    hash ^= hash >> 6;
    return &thread_object_hints[hash & (THREAD_OBJECT_HINTS - 1)];
}

static int known_object(const void *object, const void *native, ThreadObjectHint *hint) {
    return hint->object == object && hint->native == native &&
           hint->generation == __atomic_load_n(&registry_generation, __ATOMIC_ACQUIRE);
}

static void remember_object(const void *object, const void *native, ThreadObjectHint *hint) {
    *hint = (ThreadObjectHint){object, native,
        __atomic_load_n(&registry_generation, __ATOMIC_RELAXED)};
}

static int object_slot(const void *object) {
    for (int i = 0; i < MAX_THREAD_OBJECTS; i++)
        if (initialized_objects[i] == object)
            return i;
    return -1;
}

static int ensure_mutex(pthread_mutex_t_bionic *object, const pthread_mutexattr_t *attributes) {
    if (!object)
        return EINVAL;
    ThreadObjectHint *hint = object_hint(object);
    if (hint->object == object &&
        known_object(object, __atomic_load_n(&object->real_ptr, __ATOMIC_ACQUIRE), hint)) {
        return 0;
    }
    int result = pthread_mutex_lock(&registry_guard);
    if (result)
        return result;
    if (object_slot(object) >= 0) {
        remember_object(object, object->real_ptr, hint);
        pthread_mutex_unlock(&registry_guard);
        return 0;
    }
    int slot = object_slot(NULL);
    if (slot < 0) {
        pthread_mutex_unlock(&registry_guard);
        return EAGAIN;
    }
    pthread_mutex_t *native = malloc(sizeof(*native));
    if (!native) {
        pthread_mutex_unlock(&registry_guard);
        return ENOMEM;
    }
    *native = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    int kind = PTHREAD_MUTEX_NORMAL;
    if (attributes) {
        result = pthread_mutexattr_gettype((pthread_mutexattr_t *)attributes, &kind);
    } else {
        int initial;
        memcpy(&initial, object, sizeof(initial));
        if (initial == 0x4000)
            kind = PTHREAD_MUTEX_RECURSIVE;
        else if (initial == 0x8000)
            kind = PTHREAD_MUTEX_ERRORCHECK;
    }
    pthread_mutexattr_t native_attributes;
    int attributes_ready = 0;
    if (!result) {
        result = pthread_mutexattr_init(&native_attributes);
        attributes_ready = !result;
    }
    if (!result)
        result = pthread_mutexattr_settype(&native_attributes, kind);
    if (!result)
        result = pthread_mutex_init(native, &native_attributes);
    if (attributes_ready)
        pthread_mutexattr_destroy(&native_attributes);
    if (!result) {
        __atomic_store_n(&object->real_ptr, native, __ATOMIC_RELEASE);
        initialized_objects[slot] = object;
        __atomic_add_fetch(&registry_generation, 1, __ATOMIC_RELEASE);
        remember_object(object, native, hint);
    } else {
        free(native);
    }
    pthread_mutex_unlock(&registry_guard);
    return result;
}

static int ensure_condition(pthread_cond_t_bionic *object, const pthread_condattr_t *attributes) {
    if (!object)
        return EINVAL;
    ThreadObjectHint *hint = object_hint(object);
    if (hint->object == object &&
        known_object(object, __atomic_load_n(&object->real_ptr, __ATOMIC_ACQUIRE), hint)) {
        return 0;
    }
    int result = pthread_mutex_lock(&registry_guard);
    if (result)
        return result;
    if (object_slot(object) >= 0) {
        remember_object(object, object->real_ptr, hint);
        pthread_mutex_unlock(&registry_guard);
        return 0;
    }
    int slot = object_slot(NULL);
    if (slot < 0) {
        pthread_mutex_unlock(&registry_guard);
        return EAGAIN;
    }
    pthread_cond_t *native = malloc(sizeof(*native));
    if (!native) {
        pthread_mutex_unlock(&registry_guard);
        return ENOMEM;
    }
    *native = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
    result = pthread_cond_init(native, attributes);
    if (!result) {
        __atomic_store_n(&object->real_ptr, native, __ATOMIC_RELEASE);
        initialized_objects[slot] = object;
        __atomic_add_fetch(&registry_generation, 1, __ATOMIC_RELEASE);
        remember_object(object, native, hint);
    } else {
        free(native);
    }
    pthread_mutex_unlock(&registry_guard);
    return result;
}

int pthread_mutex_init_soloader(pthread_mutex_t_bionic *object,
                                const pthread_mutexattr_t *attributes) {
    return ensure_mutex(object, attributes);
}
int pthread_mutex_destroy_soloader(pthread_mutex_t_bionic *object) {
    if (!object)
        return 0;
    int result = pthread_mutex_lock(&registry_guard);
    if (result)
        return result;
    int slot = object_slot(object);
    if (slot >= 0) {
        result = pthread_mutex_destroy(object->real_ptr);
        if (!result) {
            __atomic_add_fetch(&registry_generation, 1, __ATOMIC_RELEASE);
            free(object->real_ptr);
            __atomic_store_n(&object->real_ptr, NULL, __ATOMIC_RELEASE);
            initialized_objects[slot] = NULL;
        }
    }
    pthread_mutex_unlock(&registry_guard);
    return result;
}
int pthread_mutex_lock_soloader(pthread_mutex_t_bionic *object) {
    int result = ensure_mutex(object, NULL);
    return result ? result : pthread_mutex_lock(object->real_ptr);
}
int pthread_mutex_trylock_soloader(pthread_mutex_t_bionic *object) {
    int result = ensure_mutex(object, NULL);
    return result ? result : pthread_mutex_trylock(object->real_ptr);
}
int pthread_mutex_unlock_soloader(pthread_mutex_t_bionic *object) {
    if (!object || !object->real_ptr)
        return EINVAL;
    return pthread_mutex_unlock(object->real_ptr);
}
int pthread_cond_init_soloader(pthread_cond_t_bionic *object,
                               const pthread_condattr_t *attributes) {
    return ensure_condition(object, attributes);
}
int pthread_cond_destroy_soloader(pthread_cond_t_bionic *object) {
    if (!object)
        return 0;
    int result = pthread_mutex_lock(&registry_guard);
    if (result)
        return result;
    int slot = object_slot(object);
    if (slot >= 0) {
        result = pthread_cond_destroy(object->real_ptr);
        if (!result) {
            __atomic_add_fetch(&registry_generation, 1, __ATOMIC_RELEASE);
            free(object->real_ptr);
            __atomic_store_n(&object->real_ptr, NULL, __ATOMIC_RELEASE);
            initialized_objects[slot] = NULL;
        }
    }
    pthread_mutex_unlock(&registry_guard);
    return result;
}
int pthread_cond_signal_soloader(pthread_cond_t_bionic *object) {
    int result = ensure_condition(object, NULL);
    return result ? result : pthread_cond_signal(object->real_ptr);
}
int pthread_cond_broadcast_soloader(pthread_cond_t_bionic *object) {
    int result = ensure_condition(object, NULL);
    return result ? result : pthread_cond_broadcast(object->real_ptr);
}
int pthread_cond_wait_soloader(pthread_cond_t_bionic *condition, pthread_mutex_t_bionic *mutex) {
    int result = ensure_condition(condition, NULL);
    if (!result)
        result = ensure_mutex(mutex, NULL);
    return result ? result : pthread_cond_wait(condition->real_ptr, mutex->real_ptr);
}
int pthread_cond_timedwait_soloader(pthread_cond_t_bionic *condition, pthread_mutex_t_bionic *mutex,
                                    struct timespec *deadline) {
    int result = ensure_condition(condition, NULL);
    if (!result)
        result = ensure_mutex(mutex, NULL);
    return result ? result : pthread_cond_timedwait(condition->real_ptr, mutex->real_ptr, deadline);
}

/* VitaSDK's pthread_once_t is larger than Android's control word. Native
 * synchronization still supplies waiting and visibility without casting one
 * layout into the other. Different initializers may run concurrently. */
enum { ONCE_RUNNING = 1, ONCE_COMPLETE = 2 };
static pthread_mutex_t once_guard = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t once_changed = PTHREAD_COND_INITIALIZER;
int pthread_once_soloader(volatile int *control, void (*initialize)(void)) {
    if (!control || !initialize)
        return -1;
    if (__atomic_load_n(control, __ATOMIC_ACQUIRE) == ONCE_COMPLETE)
        return 0;
    int result = pthread_mutex_lock(&once_guard);
    if (result)
        return result;
    while (__atomic_load_n(control, __ATOMIC_RELAXED) == ONCE_RUNNING) {
        result = pthread_cond_wait(&once_changed, &once_guard);
        if (result) {
            pthread_mutex_unlock(&once_guard);
            return result;
        }
    }
    if (__atomic_load_n(control, __ATOMIC_RELAXED) == ONCE_COMPLETE) {
        pthread_mutex_unlock(&once_guard);
        return 0;
    }
    __atomic_store_n(control, ONCE_RUNNING, __ATOMIC_RELAXED);
    pthread_mutex_unlock(&once_guard);
    initialize();
    result = pthread_mutex_lock(&once_guard);
    if (result)
        return result;
    __atomic_store_n(control, ONCE_COMPLETE, __ATOMIC_RELEASE);
    pthread_cond_broadcast(&once_changed);
    pthread_mutex_unlock(&once_guard);
    return 0;
}
int pthread_equal_soloader(pthread_t first, pthread_t second) {
    if (first == second)
        return 1;
    if (!first || !second)
        return 0;
    return pthread_equal(first, second);
}
