#ifndef PLAGUE_COMPAT_UTF8_H
#define PLAGUE_COMPAT_UTF8_H

#include <stddef.h>

size_t port_utf8_complete_prefix(const char *text, size_t length);
void port_utf8_copy(char *output, size_t capacity, const char *text);

#endif
