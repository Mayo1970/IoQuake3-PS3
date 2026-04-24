/*
 * setjmp.h -- PS3 shim header for correct PPC64 jmp_buf sizing.
 *
 * The newlib 1.20.0 machine/setjmp.h defines jmp_buf as double[32]
 * (256 bytes) when __ALTIVEC__ is not defined. But the setjmp() in
 * libc.a was compiled WITH __ALTIVEC__ and writes 448 bytes --
 * a 192-byte buffer overflow on every call.
 *
 * Our replacement ps3_setjmp.S (linked before libc) writes 328 bytes
 * using correct 64-bit std/ld instructions. We define jmp_buf as
 * double[64] (512 bytes) to fit either implementation safely.
 *
 * This header is found first due to -I ordering in the Makefile
 * (code/sys/include/ comes before system includes).
 */
#ifndef PS3_SETJMP_H
#define PS3_SETJMP_H

#ifdef __cplusplus
extern "C" {
#endif

/* PPC64 jmp_buf: 512 bytes (double[64]) -- large enough for our
 * replacement setjmp which writes 328 bytes */
typedef double jmp_buf[64];

extern void longjmp(jmp_buf, int);
extern int  setjmp(jmp_buf);

#ifdef __cplusplus
}
#endif

#endif /* PS3_SETJMP_H */
