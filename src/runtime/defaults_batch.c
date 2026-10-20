#include "runtime/defaults_batch.h"

#include "app/log.h"
#include "runtime/module.h"
#include "runtime/module_internal.h"
#include "runtime/objc_layout.h"

#include <psp2/kernel/threadmgr/thread.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// This native domain always writes its complete current dictionary. Collapse
// repeated synchronizations only inside an explicitly delimited engine call.
// Keep NSUserDefaults.synchronize unchanged, including its original BOOL result.
enum { PENDING_CAPACITY = 16, PATH_CAPACITY = 1024 };
typedef void (*SynchronizePath)(uintptr_t, uintptr_t, uintptr_t);
typedef uintptr_t (*DomainInitializer)(uintptr_t, uintptr_t, uintptr_t);
typedef uintptr_t (*MethodLookup)(uintptr_t, uintptr_t);
typedef uintptr_t (*RegisterSelector)(const char *);
typedef uintptr_t (*ObjectMethod)(uintptr_t, uintptr_t);
typedef void (*ReleaseMethod)(uintptr_t, uintptr_t);
typedef struct {
    uintptr_t domain, selector, path;
    char path_text[PATH_CAPACITY];
} PendingWrite;
static SynchronizePath original_sync;
static DomainInitializer original_init;
static MethodLookup lookup_method;
static RegisterSelector register_selector;
static uintptr_t retain_selector, release_selector, copy_selector, utf8_selector;
static pthread_once_t selector_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t batch_lock = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local unsigned local_depth;
static _Thread_local int flushing;
static struct {
    PendingWrite pending[PENDING_CAPACITY];
    unsigned count, queued, coalesced, writes, overflows, read_flushes;
    SceUID owner;
    int installed;
} batch;

static void initialize_selectors(void) {
    retain_selector = register_selector("retain");
    release_selector = register_selector("release");
    copy_selector = register_selector("copy");
    utf8_selector = register_selector("UTF8String");
    if (!retain_selector || !release_selector || !copy_selector || !utf8_selector)
        port_fail("Cannot register native defaults selectors");
}

static uintptr_t send_object(uintptr_t object, uintptr_t selector) {
    return ((ObjectMethod)lookup_method(object, selector))(object, selector);
}

static void release_object(uintptr_t object) {
    ((ReleaseMethod)lookup_method(object, release_selector))(object, release_selector);
}

static void release_pending(const PendingWrite *pending) {
    release_object(pending->path);
    release_object(pending->domain);
}

static void perform_write(uintptr_t self, uintptr_t selector, uintptr_t path) {
    original_sync(self, selector, path);
    __atomic_fetch_add(&batch.writes, 1, __ATOMIC_RELAXED);
}

static void flush_pending(void) {
    if (flushing)
        return;
    // Never call native code under batch_lock: a native caller may already
    // hold an Objective-C/domain lock needed by another thread's write.
    PendingWrite pending[PENDING_CAPACITY];
    pthread_mutex_lock(&batch_lock);
    unsigned count = batch.count;
    memcpy(pending, batch.pending, count * sizeof(pending[0]));
    batch.count = 0;
    pthread_mutex_unlock(&batch_lock);
    flushing = 1;
    for (unsigned i = 0; i < count; i++) {
        perform_write(pending[i].domain, pending[i].selector, pending[i].path);
        release_pending(&pending[i]);
    }
    flushing = 0;
}

static void synchronize_path(uintptr_t self, uintptr_t selector, uintptr_t path) {
    SceUID thread = sceKernelGetThreadId();
    pthread_mutex_lock(&batch_lock);
    int eligible = self && path && local_depth && batch.owner == thread && !flushing;
    pthread_mutex_unlock(&batch_lock);
    if (!eligible) {
        // Complete queued earlier requests before an unbatched request. An
        // already executing native write stays concurrent, as in the engine.
        flush_pending();
        perform_write(self, selector, path);
        return;
    }
    pthread_once(&selector_once, initialize_selectors);
    PendingWrite prepared = {.selector = selector};
    prepared.path = send_object(path, copy_selector);
    const char *text = prepared.path
        ? (const char *)send_object(prepared.path, utf8_selector) : NULL;
    size_t length = text ? strnlen(text, PATH_CAPACITY) : PATH_CAPACITY;
    if (length == PATH_CAPACITY) {
        if (prepared.path)
            release_object(prepared.path);
        flush_pending();
        perform_write(self, selector, path);
        return;
    }
    memcpy(prepared.path_text, text, length + 1);
    prepared.domain = send_object(self, retain_selector);
    if (!prepared.domain) {
        release_object(prepared.path);
        flush_pending();
        perform_write(self, selector, path);
        return;
    }
    pthread_mutex_lock(&batch_lock);
    for (unsigned i = 0; i < batch.count; i++) {
        PendingWrite *pending = &batch.pending[i];
        if (pending->domain == self && !strcmp(pending->path_text, prepared.path_text)) {
            // Preserve last-writer order when different domain objects write
            // the same path: a repeated pair moves to the end of the queue.
            PendingWrite latest = *pending;
            latest.selector = selector;
            memmove(pending, pending + 1, (batch.count - i - 1) * sizeof(*pending));
            batch.pending[batch.count - 1] = latest;
            __atomic_fetch_add(&batch.coalesced, 1, __ATOMIC_RELAXED);
            pthread_mutex_unlock(&batch_lock);
            release_pending(&prepared);
            return;
        }
    }
    if (batch.count == PENDING_CAPACITY) {
        __atomic_fetch_add(&batch.overflows, 1, __ATOMIC_RELAXED);
        pthread_mutex_unlock(&batch_lock);
        release_pending(&prepared);
        flush_pending();
        perform_write(self, selector, path);
        return;
    }
    batch.pending[batch.count++] = prepared;
    __atomic_fetch_add(&batch.queued, 1, __ATOMIC_RELAXED);
    pthread_mutex_unlock(&batch_lock);
}

static uintptr_t initialize_domain(uintptr_t self, uintptr_t selector, uintptr_t name) {
    pthread_mutex_lock(&batch_lock);
    int pending = batch.count != 0;
    pthread_mutex_unlock(&batch_lock);
    // initWithName: is the native NSUserDefaults.plist disk reader. A reader
    // on any thread completes queued requests before entering native init.
    if (pending && !flushing) {
        __atomic_fetch_add(&batch.read_flushes, 1, __ATOMIC_RELAXED);
        flush_pending();
    }
    return original_init(self, selector, name);
}

void port_defaults_batch_begin(void) {
    if (!batch.installed)
        return;
    pthread_mutex_lock(&batch_lock);
    if (!local_depth && !batch.owner)
        batch.owner = sceKernelGetThreadId();
    local_depth++;
    pthread_mutex_unlock(&batch_lock);
}

void port_defaults_batch_end(void) {
    if (!batch.installed || !local_depth)
        return;
    pthread_mutex_lock(&batch_lock);
    int finish = !--local_depth && batch.owner == sceKernelGetThreadId();
    pthread_mutex_unlock(&batch_lock);
    if (finish) {
        flush_pending();
        pthread_mutex_lock(&batch_lock);
        batch.owner = 0;
        pthread_mutex_unlock(&batch_lock);
    }
}

void port_defaults_batch_report(void) {
    if (!batch.installed)
        return;
    pthread_mutex_lock(&batch_lock);
    unsigned pending = batch.count;
    pthread_mutex_unlock(&batch_lock);
    port_log_debug("CACHE defaults queued=%u coalesced=%u writes=%u overflow=%u "
                   "read_flushes=%u pending=%u",
                   __atomic_load_n(&batch.queued, __ATOMIC_RELAXED),
                   __atomic_load_n(&batch.coalesced, __ATOMIC_RELAXED),
                   __atomic_load_n(&batch.writes, __ATOMIC_RELAXED),
                   __atomic_load_n(&batch.overflows, __ATOMIC_RELAXED),
                   __atomic_load_n(&batch.read_flushes, __ATOMIC_RELAXED), pending);
}

void port_patch_defaults_batch(void) {
    const so_module *module = port_module_image();
    NativeObjcMethod *sync = (void *)(module->load_addr + 0xe75b24);
    NativeObjcMethod *init = (void *)(module->load_addr + 0xe75aac);
    lookup_method = (MethodLookup)port_module_require_symbol("object_getMethodImplementation");
    register_selector = (RegisterSelector)port_module_require_symbol("sel_registerName");
    original_sync = (SynchronizePath)sync->implementation;
    original_init = (DomainInitializer)init->implementation;
    // Install before native constructors populate their method caches.
    sync->implementation = (uintptr_t)synchronize_path;
    init->implementation = (uintptr_t)initialize_domain;
    batch.installed = 1;
    port_log_debug("Native defaults synchronizations coalesced within engine calls");
}
