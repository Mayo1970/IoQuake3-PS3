/* ps3_platform.h -- force-included via -include. Platform constants and shims. */

#ifndef PS3_PLATFORM_H
#define PS3_PLATFORM_H

/* Pretend __linux__ -- ioq3's Linux path has least side effects on newlib. */
#if !defined(__linux__) && !defined(WIN32) && !defined(MACOS_X) && \
    !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(__PS3__)
#  define __linux__
#endif

/* Cell BE PPU is big-endian. Must be defined before q_platform.h. */
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

/* Platform strings */
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


#ifndef ID_INLINE
#  define ID_INLINE __inline__
#endif
#pragma GCC diagnostic ignored "-Wattributes"


#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* libjpeg fix: "typedef long INT32" is 64-bit on PPC64, corrupts decoding.
 * XMD_H suppresses jmorecfg.h's typedefs; we provide correct ones here. */
#ifdef XMD_H
#  ifndef _BASETSD_H_     /* avoid conflict with Windows basetsd.h */
    typedef int             INT32;
#  endif
    typedef short           INT16;
#endif

#ifndef MAP_FAILED
#  define MAP_FAILED ((void *)-1)
#endif

/* mmap stubs -- PS3 has no mmap; all memory is RWX */
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


#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-braces"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"

/* Network -- BSD sockets via PSL1GHT, IPv4 only */
#define HAVE_SA_LEN          0
#undef  HAVE_SOCKADDR_SA_LEN
#define NET_ENABLE_IPV6      0

/* Only included for net_ip.c -- avoids conflicts with huffman.c's send() */
#ifdef PS3_INCLUDE_NET
#include "sys/ps3_net.h"
#endif

/* Memory tuning for 256 MB XDR + 256 MB GDDR3 */
#undef  MIN_DEDICATED_COMHUNKMEGS
#undef  MIN_COMHUNKMEGS
#undef  DEF_COMHUNKMEGS
#undef  DEF_COMZONEMEGS
#define MIN_DEDICATED_COMHUNKMEGS 16
#define MIN_COMHUNKMEGS           16
#define DEF_COMHUNKMEGS           "80"
#define DEF_COMZONEMEGS           "24"

/* Reduced netchan constants to save zone memory */
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

/* Audio streaming -- reduced from stock to save BSS */
#ifndef MAX_RAW_STREAMS
#define MAX_RAW_STREAMS  1      /* stock: MAX_CLIENTS*2+1 = 129 */
#endif
#ifndef MAX_RAW_SAMPLES
#define MAX_RAW_SAMPLES  8192   /* stock: 16384 */
#endif

/* Intercept SDL_opengl.h -- our stub headers provide GL types */
#define USE_INTERNAL_SDL_HEADERS

/* PSL1GHT color macros collide with ioQ3's console color constants */
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
