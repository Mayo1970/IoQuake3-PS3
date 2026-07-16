#ifndef PS3_PLATFORM_H
#define PS3_PLATFORM_H

/* Take ioq3's Linux path; least side effects on newlib. */
#if !defined(__linux__) && !defined(WIN32) && !defined(MACOS_X) && \
    !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(__PS3__)
#  define __linux__
#endif

/* Cell PPU is big-endian; must land before q_platform.h. */
#ifndef __BIG_ENDIAN
#  define __BIG_ENDIAN 4321
#endif
#ifndef __LITTLE_ENDIAN
#  define __LITTLE_ENDIAN 1234
#endif
#ifndef __BYTE_ORDER
#  define __BYTE_ORDER __BIG_ENDIAN
#endif
#ifndef __FLOAT_WORD_ORDER
#  define __FLOAT_WORD_ORDER __BIG_ENDIAN
#endif
#undef  Q3_LITTLE_ENDIAN
#define Q3_BIG_ENDIAN

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

/* libjpeg: "typedef long INT32" is 64-bit on PPC64 and corrupts decoding. */
#ifdef XMD_H
#  ifndef _BASETSD_H_
    typedef int             INT32;
#  endif
    typedef short           INT16;
#endif

#ifndef MAP_FAILED
#  define MAP_FAILED ((void *)-1)
#endif

/* PS3 has no mmap; all memory is RWX. */
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

/* newlib on PS3 has no 64-bit file offsets for unzip.c. */
#define IOAPI_NO_64BIT


#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-braces"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"

/* PSL1GHT BSD sockets, IPv4 only. */
#define HAVE_SA_LEN          0
#undef  HAVE_SOCKADDR_SA_LEN
#define NET_ENABLE_IPV6      0

/* net_ip.c only -- pulling this everywhere collides with huffman.c's send(). */
#ifdef PS3_INCLUDE_NET
#include "sys/ps3_net.h"
#endif

/* Memory tuning for ~145 MB free user RAM at boot — this budget is razor thin, don't get greedy. */
#undef  MIN_DEDICATED_COMHUNKMEGS
#undef  MIN_COMHUNKMEGS
#undef  DEF_COMHUNKMEGS
#undef  DEF_COMZONEMEGS
#define MIN_DEDICATED_COMHUNKMEGS 16
#define MIN_COMHUNKMEGS           16
#define DEF_COMHUNKMEGS           96
#define DEF_COMZONEMEGS           24

#ifndef MAX_RELIABLE_COMMANDS
#define MAX_RELIABLE_COMMANDS   32      /* stock: 64 */
#endif
#ifndef PACKET_BACKUP
#define PACKET_BACKUP           32
#endif
#ifndef PACKET_MASK
#define PACKET_MASK             (PACKET_BACKUP-1)
#endif
#ifndef MAX_DOWNLOAD_WINDOW
#define MAX_DOWNLOAD_WINDOW     16      /* stock: 48 */
#endif

/* cl_parse.c voice chat indexes stream (sender + MAX_CLIENTS + 1), and MAX_CLIENTS is hardcoded
 * 64 in q_shared.h no matter what you -D override. Undersize this and it writes OOB into s_rawsamples[]. */
#ifndef MAX_RAW_STREAMS
#define MAX_RAW_STREAMS (2 * 64 + 1)
#endif
/* RoQ cinematic audio (stream 0) legitimately runs 13500-16200 samples
 * (~280-340ms) ahead of s_soundtime -- confirmed via hardware log, see
 * CLAUDE.md audio pipeline notes. Below that, every S_RawSamples() write
 * wraps the ring and clobbers not-yet-played samples: continuous crackle on
 * any cinematic long/heavy enough to sustain that lead.
 * TRIED bumping this to stock's 16384 for ALL streams (the array is
 * [MAX_RAW_STREAMS][this], so +8192 here is +~8 MB of static memory) --
 * confirmed on real hardware this HANGS at boot despite the ~25 MB margin
 * looking sufficient on paper. Do not re-attempt a uniform bump. Fix is
 * stream-0-only: MAX_RAW_SAMPLES_STREAM0 below gives cinematics/music their
 * own deeper buffer (s_rawsamples0 in snd_dma.c) for +128 KB instead, while
 * the other 128 voice-chat streams stay at this smaller size. This constant
 * is ALSO set in the Makefile's CFLAGS (-DMAX_RAW_SAMPLES=...), which wins
 * over this #ifndef -- edit both or changes silently don't apply. */
#ifndef MAX_RAW_SAMPLES
#define MAX_RAW_SAMPLES  8192          /* stock: 16384, see comment above */
#endif
#ifndef MAX_RAW_SAMPLES_STREAM0
#define MAX_RAW_SAMPLES_STREAM0  16384
#endif

#define USE_INTERNAL_SDL_HEADERS

/* PSL1GHT's COLOR_* macros collide with ioq3's console color constants. */
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
