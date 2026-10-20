#ifndef PLAGUE_VITA_SYSTEM_H
#define PLAGUE_VITA_SYSTEM_H
#include <stdint.h>
#include <stddef.h>
void port_vita_system_init(void);
void port_vita_display_init(int msaa_samples);
uint64_t port_vita_system_free_storage(void);
size_t port_vita_system_heap_used(void);
void port_vita_system_report_memory(const char *stage);
#endif
