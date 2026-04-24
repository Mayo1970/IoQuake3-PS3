/*
 * Stub endian.h for ioquake3-PS3.
 * Provides byte order macros expected by ioQ3's q_platform.h.
 */
#ifndef _ENDIAN_H_STUB
#define _ENDIAN_H_STUB

#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN 1234
#endif
#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN    4321
#endif
#ifndef __BYTE_ORDER
#define __BYTE_ORDER    __BIG_ENDIAN
#endif
#ifndef __FLOAT_WORD_ORDER
#define __FLOAT_WORD_ORDER __BIG_ENDIAN
#endif

#endif /* _ENDIAN_H_STUB */
