#ifndef PLAGUE_VITA_TROPHIES_H
#define PLAGUE_VITA_TROPHIES_H

typedef struct {
    void (*pause)(void);
    void (*draw)(void);
    void (*resume)(void);
} PortVitaTrophyPresentation;

void port_vita_trophies_init(const PortVitaTrophyPresentation *presentation);
void port_vita_trophies_unlock(unsigned id);
void port_vita_trophies_shutdown(void);

#endif
