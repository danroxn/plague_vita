/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022      GrapheneCt
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  pthread.h
 * @brief Wrapper for vitasdk/newlib pthread functions to work with
 *        Android's pthread structs which are different
 */

#ifndef PLAGUE_BIONIC_PTHREAD_H
#define PLAGUE_BIONIC_PTHREAD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>

typedef struct {
    pthread_mutex_t *real_ptr; // replaces `int volatile value;`
} pthread_mutex_t_bionic;

typedef struct {
    pthread_cond_t *real_ptr; // replaces `int volatile value;`
} pthread_cond_t_bionic;

// Null-aware equality remains an adapter; pure aliases are registered directly.
int pthread_equal_soloader(pthread_t t1, pthread_t t2);
int pthread_once_soloader(volatile int *once_control, void (*init_routine)(void));



int pthread_cond_init_soloader(pthread_cond_t_bionic *cond, const pthread_condattr_t *attr);
int pthread_cond_destroy_soloader(pthread_cond_t_bionic *cond);
int pthread_cond_wait_soloader(pthread_cond_t_bionic *cond, pthread_mutex_t_bionic *mutex);
int pthread_cond_timedwait_soloader(pthread_cond_t_bionic *cond, pthread_mutex_t_bionic *mutex, struct timespec *abstime);
int pthread_cond_broadcast_soloader(pthread_cond_t_bionic *cond);
int pthread_cond_signal_soloader(pthread_cond_t_bionic *cond);

int pthread_mutex_init_soloader(pthread_mutex_t_bionic *uid, const pthread_mutexattr_t *attr);
int pthread_mutex_destroy_soloader(pthread_mutex_t_bionic *mutex);
int pthread_mutex_lock_soloader(pthread_mutex_t_bionic *mutex);
int pthread_mutex_trylock_soloader(pthread_mutex_t_bionic *mutex);
int pthread_mutex_unlock_soloader(pthread_mutex_t_bionic *mutex);

#ifdef __cplusplus
};
#endif

#endif // PLAGUE_BIONIC_PTHREAD_H
