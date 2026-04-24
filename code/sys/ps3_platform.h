/*
 * ps3_platform.h -- force-included before every TU via -include
 *
 * Defines platform constants, endianness, memory tuning, and compatibility
 * shims for building ioQuake3 against PSL1GHT on the PS3.
 *
 * If patch_q_platform.py has added a __PS3__ block to q_platform.h, that
 * block takes priority. This header uses #ifndef guards as a fallback
 * and to provide additional PS3-specific overrides.
 */

#ifndef PS3_PLATFORM_H
#define PS3_PLATFORM_H

/* ----------------------------------------------------------------
 * Tell q_platform.h we are a known OS.
 * We pretend to be __linux__ because ioq3's Linux branch has the
 * least harmful side effects on a POSIX-ish newlib system.
 * If patch_q_platform.py was applied, the __PS3__ block fires
 * first and this fallback is never reached.
 * ---------------------------------------------------------------- */
#if !defined(__linux__) && !defined(WIN32) && !defined(MACOS_X) && \
    !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(__PS3__)
#  define __linux__
#endif

/* ----------------------------------------------------------------
 * Endianness -- Cell BE PPU is big-endian.
 * Define BEFORE q_platform.h evaluates byte order.
 * ---------------------------------------------------------------- */
#ifndef __BIG_ENDIAN
#  define __BIG_ENDIAN 4321
#endif
#ifndef __LITTLE_ENDIAN
#  define __LITTLE_ENDIAN 1234
#endif
#ifndef __BYTE_ORDER
#  define __BYTE_ORDER __BIG_ENDIAN
#endif
/* Also satisfy newlib/PSL1GHT endian checks */
#ifndef __FLOAT_WORD_ORDER
#  define __FLOAT_WORD_ORDER __BIG_ENDIAN
#endif
/* Force Q3 endian macros */
#undef  Q3_LITTLE_ENDIAN
#define Q3_BIG_ENDIAN

/* ----------------------------------------------------------------
 * Platform strings
 * ---------------------------------------------------------------- */
#ifndef OS_STRING
#  define OS_STRING "ps3"
#endif
#ifndef ARCH_STRING
#  define ARCH_STRING "cell"
#endif
#ifndef PATH_SEP
#  define PATH_SEP '/'
#endif
#ifndef DLL_EXT
#  define DLL_EXT ".sprx"
#endif

/* ----------------------------------------------------------------
 * ID_INLINE
 * ---------------------------------------------------------------- */
#ifndef ID_INLINE
#  define ID_INLINE __inline__
#endif
#pragma GCC diagnostic ignored "-Wattributes"

/* ----------------------------------------------------------------
 * Misc ioQ3 / newlib compatibility
 * ---------------------------------------------------------------- */
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* ----------------------------------------------------------------
 * libjpeg INT32/INT16 fix for PPC64 (LP64 ABI)
 * jmorecfg.h does "typedef long INT32" which is 64-bit on PPC64,
 * corrupting JPEG decoding. We define XMD_H (via Makefile -DXMD_H)
 * to suppress those typedefs and provide correct 32/16-bit ones.
 * ---------------------------------------------------------------- */
#ifdef XMD_H
#  ifndef _BASETSD_H_     /* avoid conflict with Windows basetsd.h */
    typedef int             INT32;
#  endif
    typedef short           INT16;
#endif

#ifndef MAP_FAILED
#  define MAP_FAILED ((void *)-1)
#endif

/* mmap/mprotect stubs -- PS3 homebrew has no mmap; all memory is RWX */
#ifndef PROT_READ
#  define PROT_READ     1
#  define PROT_WRITE    2
#  define PROT_EXEC     4
#  define MAP_SHARED    1
#  define MAP_ANONYMOUS 2
#  define MAP_ANON      MAP_ANONYMOUS
#endif

static inline int mprotect(void *addr, size_t len, int prot) {
    (void)addr; (void)len; (void)prot; return 0;
}

/* ioq3's unzip.c uses 64-bit file offsets; newlib on PS3 doesn't have them */
#define IOAPI_NO_64BIT

/* ----------------------------------------------------------------
 * Warning suppression
 * ---------------------------------------------------------------- */
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-braces"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"

/* ----------------------------------------------------------------
 * Network configuration
 * PS3's PSL1GHT provides BSD-compatible sockets. No need for the
 * heavy shimming the Wii port requires. IPv6 is disabled because
 * PSL1GHT only implements IPv4.
 * ---------------------------------------------------------------- */
#define HAVE_SA_LEN          0
#undef  HAVE_SOCKADDR_SA_LEN
#define NET_ENABLE_IPV6      0

/* Network socket shim -- only include for net_ip.c to avoid
 * header conflicts with huffman.c's internal send() */
#ifdef PS3_INCLUDE_NET
#include "sys/ps3_net.h"
#endif

/* ----------------------------------------------------------------
 * Memory tuning
 *
 * PS3 has 256 MB XDR (main) + 256 MB GDDR3 (video).
 * The RSX command buffer and textures consume GDDR3.
 * XDR is shared between OS, game code, and the hunk.
 *
 * Stock ioq3 defaults:
 *   com_hunkMegs  = 128  (wants 128 MB contiguous)
 *   com_zoneMegs  = 24
 *
 * PS3 budget (XDR):
 *   ~50 MB OS/system overhead
 *   ~80 MB hunk
 *   ~24 MB zone
 *   ~20 MB code + BSS + stack
 *   ~80 MB headroom for malloc, network buffers, audio
 * ---------------------------------------------------------------- */
#undef  MIN_DEDICATED_COMHUNKMEGS
#undef  MIN_COMHUNKMEGS
#undef  DEF_COMHUNKMEGS
#undef  DEF_COMZONEMEGS
#define MIN_DEDICATED_COMHUNKMEGS 16
#define MIN_COMHUNKMEGS           16
#define DEF_COMHUNKMEGS           "80"
#define DEF_COMZONEMEGS           "24"

/* ----------------------------------------------------------------
 * client_t / netchan memory reduction
 *
 * PS3 has more RAM than Wii, but we still reduce these to save
 * zone memory and leave more room for textures/audio.
 *
 * Stock: MAX_RELIABLE_COMMANDS=64, PACKET_BACKUP=32
 * PS3:   MAX_RELIABLE_COMMANDS=32, PACKET_BACKUP=32 (keep stock)
 * ---------------------------------------------------------------- */
#ifndef MAX_RELIABLE_COMMANDS
#define MAX_RELIABLE_COMMANDS   32      /* stock: 64 */
#endif
#ifndef PACKET_BACKUP
#define PACKET_BACKUP           32      /* stock: 32 -- keep default */
#endif
#ifndef PACKET_MASK
#define PACKET_MASK             (PACKET_BACKUP-1)
#endif
#ifndef MAX_DOWNLOAD_WINDOW
#define MAX_DOWNLOAD_WINDOW     16      /* stock: 48 */
#endif

/* Audio streaming -- reduce from 16 MB BSS to something reasonable.
 * PS3 has enough RAM to keep a decent buffer. */
#ifndef MAX_RAW_STREAMS
#define MAX_RAW_STREAMS  1      /* stock: MAX_CLIENTS*2+1 = 129 */
#endif
#ifndef MAX_RAW_SAMPLES
#define MAX_RAW_SAMPLES  8192   /* stock: 16384 */
#endif

/* ----------------------------------------------------------------
 * GL compatibility -- intercept SDL_opengl.h from qgl.h.
 * Our stub headers in code/sys/include/ provide the GL types
 * and constants that the renderer expects.
 * ---------------------------------------------------------------- */
#define USE_INTERNAL_SDL_HEADERS

/* ----------------------------------------------------------------
 * PSL1GHT / RSX compatibility
 * Some PSL1GHT headers define color macros that collide with
 * ioQ3's console color character constants.
 * ---------------------------------------------------------------- */
#undef COLOR_BLACK
#undef COLOR_RED
#undef COLOR_GREEN
#undef COLOR_YELLOW
#undef COLOR_BLUE
#undef COLOR_CYAN
#undef COLOR_MAGENTA
#undef COLOR_WHITE
#undef COLOR_ORANGE

#endif /* PS3_PLATFORM_H */
