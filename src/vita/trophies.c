#include "vita/trophies.h"

#include "app/log.h"
#include "trophy_catalog.h"

#include <psp2/common_dialog.h>
#include <psp2/sysmodule.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <string.h>
#include <vitaGL.h>

typedef struct {
    uint32_t sdk_version;
    SceCommonDialogParam common;
    int context;
    uint32_t options;
    uint8_t reserved[128];
} TrophySetupParameters;

typedef struct {
    int32_t result;
    uint8_t reserved[128];
} TrophySetupResult;

enum { TROPHY_BITS_PER_WORD = 32, TROPHY_STATE_WORDS = 4 };

typedef struct {
    uint32_t bits[TROPHY_STATE_WORDS];
} TrophyUnlockState;

_Static_assert(sizeof(TrophySetupParameters) == 0xd8, "Vita trophy setup parameter ABI");
_Static_assert(sizeof(TrophySetupResult) == 0x84, "Vita trophy setup result ABI");
_Static_assert(sizeof(TrophyUnlockState) == 0x10, "Vita trophy unlock state ABI");

extern int sceNpTrophyInit(void *options);
extern int sceNpTrophyTerm(void);
extern int sceNpTrophyCreateContext(int *context, const void *communication_id,
                                    const void *communication_signature, uint64_t options);
extern int sceNpTrophyDestroyContext(int context);
extern int sceNpTrophyCreateHandle(int *handle);
extern int sceNpTrophyDestroyHandle(int handle);
extern int sceNpTrophyGetTrophyUnlockState(int context, int handle, TrophyUnlockState *state,
                                           uint32_t *count);
extern int sceNpTrophyUnlockTrophy(int context, int handle, int id, int *platinum_id);
extern int sceNpTrophySetupDialogInit(const TrophySetupParameters *parameters);
extern SceCommonDialogStatus sceNpTrophySetupDialogGetStatus(void);
extern int sceNpTrophySetupDialogGetResult(TrophySetupResult *result);
extern int sceNpTrophySetupDialogTerm(void);

#define TROPHY_ALREADY_INITIALIZED UINT32_C(0x80551602)
#define TROPHY_ALREADY_UNLOCKED UINT32_C(0x8055160f)
#define TROPHY_ALREADY_SETUP UINT32_C(0x80551613)

_Static_assert(PORT_TROPHY_SET_CAPACITY > 0 &&
                   PORT_TROPHY_SET_CAPACITY <= TROPHY_BITS_PER_WORD * TROPHY_STATE_WORDS,
               "Vita trophy flag capacity");
_Static_assert(PORT_TROPHY_COUNT > 0 && PORT_TROPHY_SET_COUNT > 0 && PORT_TROPHY_SET_COUNT <= 100,
               "NP communication number range");
_Static_assert(PORT_TROPHY_SET_COUNT ==
                   (PORT_TROPHY_COUNT + PORT_TROPHY_SET_CAPACITY - 1) / PORT_TROPHY_SET_CAPACITY,
               "Trophy catalog set count");
_Static_assert(sizeof(PORT_TROPHY_COMMUNICATION_ID) == 10, "NP communication ID length");

typedef struct {
    TrophyUnlockState confirmed;
    TrophyUnlockState pending;
    uint8_t communication_id[12];
    int context;
    int context_created;
    int available;
} TrophySet;

static struct {
    pthread_mutex_t lock;
    // The SDK can leave phantom gate tokens after sem_post. Keep one receiver
    // per condition so main cannot consume a wake intended for the worker.
    pthread_cond_t requests;
    pthread_cond_t main_events;
    pthread_t worker;
    TrophySet sets[PORT_TROPHY_SET_COUNT];
    int handle;
    int loaded_module;
    int initialized_np;
    int worker_started;
    int available;
    int stopping;
    int initialization_attempted;
    int initialization_complete;
    int setup_index;
    int setup_result;
} trophies = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .requests = PTHREAD_COND_INITIALIZER,
    .main_events = PTHREAD_COND_INITIALIZER,
    .handle = -1,
    .setup_index = -1};

static void release_resources(void);
static unsigned initialize_backend(void);

static uint32_t trophy_mask(unsigned id) {
    return UINT32_C(1) << (id % TROPHY_BITS_PER_WORD);
}

static unsigned set_trophy_count(unsigned set) {
    unsigned remaining = PORT_TROPHY_COUNT - set * PORT_TROPHY_SET_CAPACITY;
    return remaining < PORT_TROPHY_SET_CAPACITY ? remaining : PORT_TROPHY_SET_CAPACITY;
}

static int next_pending(void) {
    for (unsigned id = 0; id < PORT_TROPHY_COUNT; ++id) {
        TrophySet *set = &trophies.sets[id / PORT_TROPHY_SET_CAPACITY];
        unsigned local_id = id % PORT_TROPHY_SET_CAPACITY;
        if (set->pending.bits[local_id / TROPHY_BITS_PER_WORD] & trophy_mask(local_id))
            return (int)id;
    }
    return -1;
}

static void *trophy_worker(void *unused) {
    (void)unused;
    unsigned ready_sets = initialize_backend();
    if (!ready_sets)
        release_resources();
    pthread_mutex_lock(&trophies.lock);
    trophies.available = ready_sets > 0 && !trophies.stopping;
    trophies.initialization_complete = 1;
    pthread_cond_broadcast(&trophies.main_events);
    if (!ready_sets) {
        pthread_mutex_unlock(&trophies.lock);
        return NULL;
    }
    for (;;) {
        int id = next_pending();
        while (id < 0 && !trophies.stopping) {
            int result = pthread_cond_wait(&trophies.requests, &trophies.lock);
            if (result) {
                port_log_error("Trophies: worker wait failed (%d)", result);
                trophies.available = 0;
                pthread_mutex_unlock(&trophies.lock);
                release_resources();
                return NULL;
            }
            id = next_pending();
        }
        if (id < 0)
            break;
        unsigned set_index = (unsigned)id / PORT_TROPHY_SET_CAPACITY;
        unsigned local_id = (unsigned)id % PORT_TROPHY_SET_CAPACITY;
        TrophySet *set = &trophies.sets[set_index];
        pthread_mutex_unlock(&trophies.lock);

        int platinum_id = -1;
        int result =
            sceNpTrophyUnlockTrophy(set->context, trophies.handle, (int)local_id, &platinum_id);

        pthread_mutex_lock(&trophies.lock);
        unsigned word = local_id / TROPHY_BITS_PER_WORD;
        uint32_t mask = trophy_mask(local_id);
        set->pending.bits[word] &= ~mask;
        if (result >= 0 || (uint32_t)result == TROPHY_ALREADY_UNLOCKED) {
            set->confirmed.bits[word] |= mask;
            if (platinum_id >= 0 && (unsigned)platinum_id < set_trophy_count(set_index))
                set->confirmed.bits[(unsigned)platinum_id / TROPHY_BITS_PER_WORD] |=
                    trophy_mask((unsigned)platinum_id);
            port_log_debug("Trophies: unlocked %d", id);
        } else {
            port_log_error("Trophies: unlock %d failed (0x%08x)", id, (unsigned)result);
        }
    }
    pthread_mutex_unlock(&trophies.lock);
    release_resources();
    return NULL;
}

static void release_resources(void) {
    int result;
    if (trophies.handle >= 0) {
        result = sceNpTrophyDestroyHandle(trophies.handle);
        if (result < 0)
            port_log_error("Trophies: destroy handle failed (0x%08x)", (unsigned)result);
        trophies.handle = -1;
    }
    for (unsigned i = 0; i < PORT_TROPHY_SET_COUNT; ++i) {
        TrophySet *set = &trophies.sets[i];
        if (set->context_created) {
            result = sceNpTrophyDestroyContext(set->context);
            if (result < 0)
                port_log_error("Trophies: destroy context %u failed (0x%08x)", i, (unsigned)result);
            set->context_created = 0;
        }
        set->available = 0;
    }
    if (trophies.initialized_np) {
        result = sceNpTrophyTerm();
        if (result < 0)
            port_log_error("Trophies: termination failed (0x%08x)", (unsigned)result);
        trophies.initialized_np = 0;
    }
    if (trophies.loaded_module) {
        result = sceSysmoduleUnloadModule(SCE_SYSMODULE_NP_TROPHY);
        if (result < 0)
            port_log_error("Trophies: unload failed (0x%08x)", (unsigned)result);
        trophies.loaded_module = 0;
    }
}

static int setup_context(unsigned index, int context,
                         const PortVitaTrophyPresentation *presentation) {
    TrophySetupParameters parameters = {0};
    parameters.sdk_version = PSP2_SDK_VERSION;
    parameters.context = context;
    _sceCommonDialogSetMagicNumber(&parameters.common);
    int result = sceNpTrophySetupDialogInit(&parameters);
    if ((uint32_t)result == TROPHY_ALREADY_SETUP)
        return 1;
    if (result < 0) {
        port_log_error("Trophies: set %u setup init failed (0x%08x)", index, (unsigned)result);
        return 0;
    }

    SceCommonDialogStatus status;
    int paused = 0;
    while ((status = sceNpTrophySetupDialogGetStatus()) == SCE_COMMON_DIALOG_STATUS_RUNNING) {
        if (!paused && presentation) {
            presentation->pause();
            paused = 1;
        }
        if (presentation)
            presentation->draw();
        vglSwapBuffers(GL_TRUE);
    }

    TrophySetupResult outcome = {.result = SCE_COMMON_DIALOG_RESULT_ABORTED};
    int read_result = status == SCE_COMMON_DIALOG_STATUS_FINISHED
                          ? sceNpTrophySetupDialogGetResult(&outcome)
                          : -1;
    result = sceNpTrophySetupDialogTerm();
    if (paused)
        presentation->resume();
    if (read_result < 0 || result < 0 || outcome.result != SCE_COMMON_DIALOG_RESULT_OK) {
        port_log_error("Trophies: set %u setup unavailable (status %d, read 0x%08x, result %d, "
                       "term 0x%08x)",
                       index, status, (unsigned)read_result, outcome.result, (unsigned)result);
        return 0;
    }
    return 1;
}

static int request_setup(unsigned index) {
    pthread_mutex_lock(&trophies.lock);
    if (trophies.stopping) {
        pthread_mutex_unlock(&trophies.lock);
        return 0;
    }
    trophies.setup_index = (int)index;
    pthread_cond_broadcast(&trophies.main_events);
    int wait_failed = 0;
    while (trophies.setup_index >= 0) {
        if (wait_failed) {
            pthread_mutex_unlock(&trophies.lock);
            sched_yield();
            pthread_mutex_lock(&trophies.lock);
            continue;
        }
        int result = pthread_cond_wait(&trophies.requests, &trophies.lock);
        if (result) {
            port_log_error("Trophies: setup wait failed (%d)", result);
            trophies.stopping = 1;
            wait_failed = 1;
            pthread_cond_broadcast(&trophies.requests);
            pthread_cond_broadcast(&trophies.main_events);
        }
    }
    int ready = !trophies.stopping && trophies.setup_result;
    pthread_mutex_unlock(&trophies.lock);
    return ready;
}

static int initialize_set(unsigned index) {
    TrophySet *set = &trophies.sets[index];
    memcpy(set->communication_id, PORT_TROPHY_COMMUNICATION_ID,
           sizeof(PORT_TROPHY_COMMUNICATION_ID));
    set->communication_id[10] = (uint8_t)index;
    static const uint8_t homebrew_signature[160] = {0xb9, 0xdd, 0xe1, 0x3b, 0x01, 0x00};
    int result =
        sceNpTrophyCreateContext(&set->context, set->communication_id, homebrew_signature, 0);
    if (result < 0) {
        port_log_error("Trophies: context %u unavailable (0x%08x); check NoTrpDrm and the "
                       "installed trophy pack",
                       index, (unsigned)result);
        return 0;
    }
    set->context_created = 1;
    if (!request_setup(index))
        goto failed;

    uint32_t count = 0;
    result =
        sceNpTrophyGetTrophyUnlockState(set->context, trophies.handle, &set->confirmed, &count);
    if (result < 0) {
        port_log_error("Trophies: read state %u failed (0x%08x)", index, (unsigned)result);
        goto failed;
    }
    if (count != set_trophy_count(index)) {
        port_log_error("Trophies: installed set %u has %u entries, expected %u", index,
                       (unsigned)count, set_trophy_count(index));
        goto failed;
    }
    set->available = 1;
    return 1;

failed:
    result = sceNpTrophyDestroyContext(set->context);
    if (result < 0)
        port_log_error("Trophies: destroy context %u failed (0x%08x)", index, (unsigned)result);
    set->context_created = 0;
    return 0;
}

static unsigned initialize_backend(void) {
    int result = sceSysmoduleIsLoaded(SCE_SYSMODULE_NP_TROPHY);
    if (result < 0) {
        result = sceSysmoduleLoadModule(SCE_SYSMODULE_NP_TROPHY);
        if (result < 0)
            goto unavailable;
        trophies.loaded_module = 1;
    }

    result = sceNpTrophyInit(NULL);
    if (result < 0 && (uint32_t)result != TROPHY_ALREADY_INITIALIZED)
        goto unavailable;
    trophies.initialized_np = result >= 0;

    int handle = -1;
    result = sceNpTrophyCreateHandle(&handle);
    if (result < 0)
        goto unavailable;
    trophies.handle = handle;
    unsigned ready_sets = 0;
    for (unsigned i = 0; i < PORT_TROPHY_SET_COUNT; ++i) {
        pthread_mutex_lock(&trophies.lock);
        int stopping = trophies.stopping;
        pthread_mutex_unlock(&trophies.lock);
        if (stopping)
            break;
        ready_sets += (unsigned)initialize_set(i);
    }
    port_log_debug("Trophies: ready (%u/%u sets)", ready_sets, (unsigned)PORT_TROPHY_SET_COUNT);
    return ready_sets;

unavailable:
    port_log_error("Trophies: unavailable (0x%08x); check NoTrpDrm and the installed trophy pack",
                   (unsigned)result);
    return 0;
}

void port_vita_trophies_init(const PortVitaTrophyPresentation *presentation) {
    if (trophies.initialization_attempted)
        return;
    trophies.initialization_attempted = 1;
    if (presentation && (!presentation->pause || !presentation->draw || !presentation->resume)) {
        port_log_error("Trophies: loading presentation requires pause, draw and resume callbacks");
        return;
    }
    int result = pthread_create(&trophies.worker, NULL, trophy_worker, NULL);
    if (result) {
        port_log_error("Trophies: worker creation failed (%d)", result);
        return;
    }
    trophies.worker_started = 1;

    pthread_mutex_lock(&trophies.lock);
    while (!trophies.initialization_complete) {
        if (trophies.setup_index >= 0) {
            unsigned index = (unsigned)trophies.setup_index;
            int context = trophies.sets[index].context;
            int ready = 0;
            if (!trophies.stopping) {
                pthread_mutex_unlock(&trophies.lock);
                ready = setup_context(index, context, presentation);
                pthread_mutex_lock(&trophies.lock);
            }
            trophies.setup_result = ready;
            trophies.setup_index = -1;
            pthread_cond_broadcast(&trophies.requests);
        } else {
            result = pthread_cond_wait(&trophies.main_events, &trophies.lock);
            if (result) {
                port_log_error("Trophies: initialization wait failed (%d)", result);
                trophies.stopping = 1;
                trophies.available = 0;
                trophies.setup_result = 0;
                trophies.setup_index = -1;
                pthread_cond_broadcast(&trophies.requests);
                pthread_cond_broadcast(&trophies.main_events);
                break;
            }
        }
    }
    int available = trophies.available;
    pthread_mutex_unlock(&trophies.lock);
    if (!available)
        port_vita_trophies_shutdown();
}

void port_vita_trophies_unlock(unsigned id) {
    if (id >= PORT_TROPHY_COUNT)
        return;
    pthread_mutex_lock(&trophies.lock);
    TrophySet *set = &trophies.sets[id / PORT_TROPHY_SET_CAPACITY];
    unsigned local_id = id % PORT_TROPHY_SET_CAPACITY;
    unsigned word = local_id / TROPHY_BITS_PER_WORD;
    uint32_t mask = trophy_mask(local_id);
    if (trophies.available && set->available && !(set->confirmed.bits[word] & mask) &&
        !(set->pending.bits[word] & mask)) {
        set->pending.bits[word] |= mask;
        int result = pthread_cond_signal(&trophies.requests);
        if (result)
            port_log_error("Trophies: worker signal failed (%d)", result);
    }
    pthread_mutex_unlock(&trophies.lock);
}

void port_vita_trophies_shutdown(void) {
    pthread_mutex_lock(&trophies.lock);
    trophies.available = 0;
    trophies.stopping = 1;
    trophies.setup_result = 0;
    trophies.setup_index = -1;
    int signal_result = pthread_cond_broadcast(&trophies.requests);
    if (signal_result)
        port_log_error("Trophies: shutdown signal failed (%d)", signal_result);
    signal_result = pthread_cond_broadcast(&trophies.main_events);
    if (signal_result)
        port_log_error("Trophies: shutdown main signal failed (%d)", signal_result);
    pthread_mutex_unlock(&trophies.lock);
    if (trophies.worker_started) {
        int result = pthread_join(trophies.worker, NULL);
        if (result) {
            port_log_error("Trophies: worker join failed (%d)", result);
            return;
        }
        trophies.worker_started = 0;
    }
}
