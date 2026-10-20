#ifndef PLAGUE_RUNTIME_OBJC_LAYOUT_H
#define PLAGUE_RUNTIME_OBJC_LAYOUT_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;
    const char *types;
    uintptr_t implementation;
} NativeObjcMethod;
typedef struct {
    const char *name;
    const char *type;
    uint32_t offset;
} NativeObjcIvar;
_Static_assert(sizeof(NativeObjcMethod) == 12 && offsetof(NativeObjcMethod, implementation) == 8,
               "Android Objective-C method layout");
_Static_assert(sizeof(NativeObjcIvar) == 12 && offsetof(NativeObjcIvar, offset) == 8,
               "Android Objective-C ivar layout");
#endif
