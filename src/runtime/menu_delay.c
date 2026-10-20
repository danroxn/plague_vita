#include "runtime/menu_delay.h"

#include "app/log.h"
#include "runtime/module.h"
#include "runtime/module_internal.h"

#include <kubridge.h>
#include <stdint.h>
#include <string.h>

// pressedUnleashPlague saves the selected genes, switches to page 14 using
// FadeTo/MoveTo actions of 250 ms, then separately delays startNewGame by 800 ms.
// Keep every action and callback; 350 ms leaves 100 ms beyond that UI transition.
_Static_assert(sizeof(uintptr_t) == 4,
               "Native Start action ARM layout");

void port_patch_menu_delay(void) {
    uintptr_t base = port_module_image()->load_addr;

    // ARM MOVW/MOVT r2: 0x3f4ccccd (0.8f) -> 0x3eb33333 (0.35f).
    // The intervening MOV r3,r0 and the actionWithDuration: call stay intact.
    const uint32_t low = 0xe3032333, high = 0xe3432eb3;
    memcpy((void *)(base + 0x2bdfac), &low, sizeof(low));
    memcpy((void *)(base + 0x2bdfb4), &high, sizeof(high));
    kuKernelFlushCaches((void *)(base + 0x2bdfac), 3 * sizeof(uint32_t));
    port_log_debug("New-game Start delay reduced from 800ms to 350ms (250ms menu transition)");
}
