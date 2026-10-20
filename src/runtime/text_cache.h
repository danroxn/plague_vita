#ifndef PLAGUE_RUNTIME_TEXT_CACHE_H
#define PLAGUE_RUNTIME_TEXT_CACHE_H

#include <stdint.h>

// May run once on a bootstrap worker; join it before patching/native use.
void port_text_cache_prepare(void);
// Safe on the loading presenter. False while preparation is pending; true if
// no usable, nonempty cache was loaded and startup must rasterize its text.
int port_text_cache_needs_warmup(void);

void port_patch_text_cache(void);
void port_text_cache_report(void);
// Main thread: call after each native loading/render operation. The cheap frame
// check runs every call; persistence eligibility is checked at most once/second.
void port_text_cache_poll(uint64_t now_us, uint64_t frame_elapsed_us);
// Force an asynchronous request, bypassing the quiet-period policy.
void port_text_cache_flush(void);
// Main thread: force the latest prefix to disk and join before process exit.
void port_text_cache_finish(void);

#endif
