#include "runtime/patches.h"

#include "app/log.h"
#include "runtime/module_internal.h"
#define PORT_TROPHY_INCLUDE_CATALOG
#include "trophy_catalog.h"
#include "vita/trophies.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef int (*CompleteAchievement)(void *service, const char *identifier);
typedef void (*RestoreAchievements)(void *service);
typedef struct {
    unsigned char bytes[12];
} AchievementKey;
typedef const unsigned char *(*FindAchievement)(const void *map, const AchievementKey *key);
_Static_assert(sizeof(AchievementKey) == 12, "Android libc++ short-string key");

static CompleteAchievement original_complete;
static RestoreAchievements original_restore;
static FindAchievement find_achievement;

static uintptr_t complete_resume __attribute__((used));
static uintptr_t restore_resume __attribute__((used));

__attribute__((naked, target("arm"))) static int continue_complete(void *service
                                                                   __attribute__((unused)),
                                                                   const char *identifier
                                                                   __attribute__((unused))) {
    __asm__ volatile("push {r4, r5, r6, r7, r8, r9, fp, lr}\n"
                     "add fp, sp, #24\n"
                     "ldr ip, =complete_resume\n"
                     "ldr pc, [ip]\n");
}

__attribute__((naked, target("arm"))) static void continue_restore(void *service
                                                                   __attribute__((unused))) {
    __asm__ volatile("push {r4, r5, r6, r7, r8, r9, sl, fp, lr}\n"
                     "add fp, sp, #28\n"
                     "ldr ip, =restore_resume\n"
                     "ldr pc, [ip]\n");
}

static int is_completed(void *service, const char *identifier) {
    AchievementKey key = {{0}};
    size_t length = strlen(identifier);
    if (length > sizeof(key.bytes) - 2)
        return 0;
    key.bytes[0] = (unsigned char)(length << 1);
    memcpy(key.bytes + 1, identifier, length);
    const unsigned char *map = (const unsigned char *)service + 0x88;
    const unsigned char *node = find_achievement(map, &key);
    return node && node != map + 4 && node[0x50] != 0;
}

static int complete_achievement(void *service, const char *identifier) {
    int result = original_complete(service, identifier);
    for (unsigned i = 0; i < PORT_TROPHY_COUNT; i++) {
        if (!strcmp(identifier, port_trophy_catalog[i].game_id)) {
            if (is_completed(service, identifier))
                port_vita_trophies_unlock(i);
            return result;
        }
    }
    return result;
}

static void restore_achievements(void *service) {
    original_restore(service);
    for (unsigned i = 0; i < PORT_TROPHY_COUNT; i++) {
        if (is_completed(service, port_trophy_catalog[i].game_id))
            port_vita_trophies_unlock(i);
    }
}

void port_patch_achievements(void) {
    uintptr_t base = port_module_image()->load_addr;
    for (unsigned i = 0; i < PORT_TROPHY_COUNT; i++) {
        if (strlen(port_trophy_catalog[i].game_id) > sizeof(AchievementKey) - 2)
            port_fail("Trophy achievement identifier exceeds native key capacity");
    }
    find_achievement = (FindAchievement)(base + 0x4cbbbc);
    complete_resume = base + 0x5901dc;
    restore_resume = base + 0x58f960;
    original_complete = continue_complete;
    original_restore = continue_restore;
    hook_arm(base + 0x5901d4, (uintptr_t)complete_achievement);
    hook_arm(base + 0x58f958, (uintptr_t)restore_achievements);
    port_log_debug("Native local achievements linked to %u Vita trophies", PORT_TROPHY_COUNT);
}
