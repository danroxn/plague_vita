#include "app/log.h"
#include "runtime/module.h"
#include "runtime/module_internal.h"

#include <stdint.h>

static int apk_asset_manager_enabled(void) { return 0; }

void port_patch_asset_read(void) {
    uintptr_t address = port_module_require_symbol("MCAssetManager_Enabled");

    // Route native resource users through the file adapter, including callers
    // that bypass Android's asset manager. The APK provider serves these paths.
    hook_arm(address, (uintptr_t)apk_asset_manager_enabled);
    port_log_debug("ASSETS using package.apk through the resource file adapter");
}
