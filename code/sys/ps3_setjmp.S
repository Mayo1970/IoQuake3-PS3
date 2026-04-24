/*
 * ps3_setjmp.S -- PPC64-correct setjmp/longjmp for PS3 (PSL1GHT)
 *
 * The newlib 1.20.0 setjmp.S bundled with ps3toolchain has two bugs:
 *
 *   1. BUFFER OVERFLOW: setjmp.S was compiled with -mcpu=cell (implies
 *      __ALTIVEC__), so it saves AltiVec registers and writes 448 bytes.
 *      But code compiled with -mno-altivec sees jmp_buf as double[32]
 *      (256 bytes). Every setjmp() overflows by 192 bytes, corrupting
 *      the stack.
 *
 *   2. 32-BIT GPR SAVES: Uses stw/lwz (32-bit) for 64-bit registers.
 *      Upper 32 bits of SP, TOC, LR, and r13-r31 are lost. On PS3
 *      homebrew, all addresses are <4GB so this is less severe, but
 *      still wrong per the PPC64 ABI.
 *
 * This replacement:
 *   - Uses std/ld for all GPR saves (correct 64-bit)
 *   - Does NOT save AltiVec registers (matches -mno-altivec jmp_buf)
 *   - Fits within double[64] = 512 bytes (we override _JBLEN to 64)
 *
 * Layout (all 8-byte aligned):
 *   Offset   Size   Content
 *     0       8     r1  (SP)
 *     8       8     r2  (TOC)
 *    16       8     r13
 *    24       8     r14
 *    ...             ...
 *   168       8     r31
 *   176       8     LR
 *   184       8     CR  (stored as 8 bytes for alignment)
 *   192       8     f14
 *   200       8     f15
 *    ...             ...
 *   328       8     f31
 *   Total: 328 bytes (fits in 512-byte jmp_buf)
 */

#include "ppc-asm.h"

FUNC_START(setjmp)
	/* Align buffer to 8-byte boundary (jmp_buf is double[], already aligned) */
	addi	3,3,7
	rldicr	3,3,0,59	/* clear low 3 bits (PPC64 8-byte align) */

	/* Save GPRs (64-bit) -- r1, r2, r13-r31 = 21 registers */
	std	1,0(3)		/* offset   0: SP */
	std	2,8(3)		/* offset   8: TOC */
	std	13,16(3)	/* offset  16 */
	std	14,24(3)	/* offset  24 */
	std	15,32(3)	/* offset  32 */
	std	16,40(3)	/* offset  40 */
	std	17,48(3)	/* offset  48 */
	std	18,56(3)	/* offset  56 */
	std	19,64(3)	/* offset  64 */
	std	20,72(3)	/* offset  72 */
	std	21,80(3)	/* offset  80 */
	std	22,88(3)	/* offset  88 */
	std	23,96(3)	/* offset  96 */
	std	24,104(3)	/* offset 104 */
	std	25,112(3)	/* offset 112 */
	std	26,120(3)	/* offset 120 */
	std	27,128(3)	/* offset 128 */
	std	28,136(3)	/* offset 136 */
	std	29,144(3)	/* offset 144 */
	std	30,152(3)	/* offset 152 */
	std	31,160(3)	/* offset 160 */

	/* Save LR and CR */
	mflr	4
	std	4,168(3)	/* offset 168: LR */
	mfcr	4
	std	4,176(3)	/* offset 176: CR (stored as 64-bit for alignment) */

	/* Save FPRs -- f14-f31 = 18 registers */
	stfd	14,184(3)	/* offset 184 */
	stfd	15,192(3)	/* offset 192 */
	stfd	16,200(3)	/* offset 200 */
	stfd	17,208(3)	/* offset 208 */
	stfd	18,216(3)	/* offset 216 */
	stfd	19,224(3)	/* offset 224 */
	stfd	20,232(3)	/* offset 232 */
	stfd	21,240(3)	/* offset 240 */
	stfd	22,248(3)	/* offset 248 */
	stfd	23,256(3)	/* offset 256 */
	stfd	24,264(3)	/* offset 264 */
	stfd	25,272(3)	/* offset 272 */
	stfd	26,280(3)	/* offset 280 */
	stfd	27,288(3)	/* offset 288 */
	stfd	28,296(3)	/* offset 296 */
	stfd	29,304(3)	/* offset 304 */
	stfd	30,312(3)	/* offset 312 */
	stfd	31,320(3)	/* offset 320 */

	/* Total written: 328 bytes (fits in 512-byte jmp_buf) */

	li	3,0		/* return 0 */
	blr
FUNC_END(setjmp)


FUNC_START(longjmp)
	/* Align buffer to 8-byte boundary */
	addi	3,3,7
	rldicr	3,3,0,59

	/* Restore GPRs (64-bit) */
	ld	1,0(3)		/* SP */
	ld	2,8(3)		/* TOC */
	ld	13,16(3)
	ld	14,24(3)
	ld	15,32(3)
	ld	16,40(3)
	ld	17,48(3)
	ld	18,56(3)
	ld	19,64(3)
	ld	20,72(3)
	ld	21,80(3)
	ld	22,88(3)
	ld	23,96(3)
	ld	24,104(3)
	ld	25,112(3)
	ld	26,120(3)
	ld	27,128(3)
	ld	28,136(3)
	ld	29,144(3)
	ld	30,152(3)
	ld	31,160(3)

	/* Restore LR and CR */
	ld	5,168(3)
	mtlr	5
	ld	5,176(3)
	mtcr	5

	/* Restore FPRs */
	lfd	14,184(3)
	lfd	15,192(3)
	lfd	16,200(3)
	lfd	17,208(3)
	lfd	18,216(3)
	lfd	19,224(3)
	lfd	20,232(3)
	lfd	21,240(3)
	lfd	22,248(3)
	lfd	23,256(3)
	lfd	24,264(3)
	lfd	25,272(3)
	lfd	26,280(3)
	lfd	27,288(3)
	lfd	28,296(3)
	lfd	29,304(3)
	lfd	30,312(3)
	lfd	31,320(3)

	/* Return val (r4), or 1 if val == 0 */
	mr.	3,4
	bnelr+			/* return r4 if nonzero */
	li	3,1		/* return 1 if r4 was 0 */
	blr
FUNC_END(longjmp)
