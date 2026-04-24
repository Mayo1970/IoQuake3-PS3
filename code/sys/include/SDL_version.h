/* Stub SDL_version.h for ioquake3-PS3 -- no SDL on PS3 */
#ifndef _SDL_VERSION_H_STUB
#define _SDL_VERSION_H_STUB

/* Enough to satisfy sys_local.h version check */
#define SDL_MAJOR_VERSION 2
#define SDL_MINOR_VERSION 0
#define SDL_PATCHLEVEL    5
#define SDL_VERSION_ATLEAST(x,y,z) \
    ((SDL_MAJOR_VERSION > (x)) || \
     (SDL_MAJOR_VERSION == (x) && SDL_MINOR_VERSION > (y)) || \
     (SDL_MAJOR_VERSION == (x) && SDL_MINOR_VERSION == (y) && SDL_PATCHLEVEL >= (z)))

#endif
