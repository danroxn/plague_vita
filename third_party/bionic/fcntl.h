/*
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef PLAGUE_BIONIC_FCNTL_H
#define PLAGUE_BIONIC_FCNTL_H
#include <fcntl.h>

#define BIONIC_O_WRONLY                                          01
#define BIONIC_O_RDWR                                            02
#define BIONIC_O_CREAT                                         0100
#define BIONIC_O_EXCL                                          0200
#define BIONIC_O_TRUNC                                        01000
#define BIONIC_O_APPEND                                       02000
#define BIONIC_O_NONBLOCK                                     04000
#define BIONIC_O_DIRECTORY                                 00200000
#define BIONIC__O_TMPFILE                                 020000000
#define BIONIC_O_TMPFILE   (BIONIC__O_TMPFILE | BIONIC_O_DIRECTORY)

/**
 * Convert bionic (Android) `open()` flags to newlib (Vita) flags
 *
 * @param[in] flags open() flags created using musl defines
 *
 * @return open(flags) recreated using newlib defines
 */
static inline int oflags_bionic_to_newlib(int flags) {
    int out = 0;
    if (flags & BIONIC_O_RDWR)
        out |= O_RDWR;
    else if (flags & BIONIC_O_WRONLY)
        out |= O_WRONLY;
    else
        out |= O_RDONLY;
    if (flags & BIONIC_O_NONBLOCK)
        out |= O_NONBLOCK;
    if (flags & BIONIC_O_APPEND)
        out |= O_APPEND;
    if ((flags & BIONIC_O_CREAT) || (flags & BIONIC_O_TMPFILE))
        out |= O_CREAT;
    if (flags & BIONIC_O_TRUNC)
        out |= O_TRUNC;
    if (flags & BIONIC_O_EXCL)
        out |= O_EXCL;
    return out;
}

#endif
