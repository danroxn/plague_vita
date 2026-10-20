#ifndef PLAGUE_APP_LOADING_INDICATOR_H
#define PLAGUE_APP_LOADING_INDICATOR_H

#include <stdint.h>

typedef struct PortLoadingIndicator PortLoadingIndicator;

PortLoadingIndicator *port_loading_indicator_create(const uint32_t *background);
void port_loading_indicator_draw(PortLoadingIndicator *indicator, uint32_t *pixels,
                                 float seconds, unsigned buffer);
void port_loading_indicator_destroy(PortLoadingIndicator *indicator);

#endif
