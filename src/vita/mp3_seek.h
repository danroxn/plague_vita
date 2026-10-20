#ifndef PLAGUE_VITA_MP3_SEEK_H
#define PLAGUE_VITA_MP3_SEEK_H

// Install before opening SDL audio. Unsupported SDL_mixer builds retain their
// original seek behavior.
void port_vita_mp3_seek_init(void);

#endif
