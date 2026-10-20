#ifndef PLAGUE_RUNTIME_DEFAULTS_BATCH_H
#define PLAGUE_RUNTIME_DEFAULTS_BATCH_H

void port_patch_defaults_batch(void);
// Pair on the same thread around one native engine operation. The outer end
// completes every queued write before returning; nested scopes are supported.
void port_defaults_batch_begin(void);
void port_defaults_batch_end(void);
void port_defaults_batch_report(void);

#endif
