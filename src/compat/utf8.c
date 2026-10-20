#include "compat/utf8.h"

#include <string.h>

size_t port_utf8_complete_prefix(const char *text, size_t length) {
    if (!length)
        return 0;
    size_t start = length - 1;
    while (start && ((unsigned char)text[start] & 0xc0) == 0x80)
        start--;
    unsigned char lead = (unsigned char)text[start];
    size_t width = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
    return length - start < width ? start : length;
}

void port_utf8_copy(char *output, size_t capacity, const char *text) {
    if (!capacity)
        return;
    size_t length = strlen(text);
    if (length >= capacity)
        length = port_utf8_complete_prefix(text, capacity - 1);
    memcpy(output, text, length);
    output[length] = 0;
}
