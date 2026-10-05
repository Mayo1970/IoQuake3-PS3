/*
===========================================================================
Copyright (C) 2008 Przemyslaw Iskra <sparky@pld-linux.org>

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/


#if defined(powerc) || defined(powerpc) || defined(__powerpc__) || \
	defined(ppc) || defined(__ppc) || defined(__ppc__) || \
	defined(__powerpc64__) || defined(__ppc64__)

#include <sys/types.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <setjmp.h>

#include "vm_local.h"

// A compile error longjmps back to VM_Compile, which then leaves the VM to the interpreter.
static jmp_buf vmjit_abort;
static const char *vmjit_error;
static char vmjit_errbuf[ 128 ];

static void PPC_Die( const char *reason ) __attribute__((noreturn));
static void
PPC_Die( const char *reason )
{
	vmjit_error = reason;
	longjmp( vmjit_abort, 1 );
	__builtin_unreachable();
}
#define DIE( reason ) PPC_Die( reason )

// Register information according to:
// http://refspecs.freestandards.org/elf/elfspec_ppc.pdf

#define r0	0	// volatile
#define r1	1	// caller safe ( stack pointer )
#define r2	2	// reserved
#define r3	3	// callee safe
#define r4	4	// callee safe
#define r5	5	// callee safe
#define r6	6	// callee safe
#define r7	7	// callee safe
#define r8	8	// callee safe
#define r9	9	// callee safe
#define r10	10	// callee safe
#define r11	11	// volatile
#define r12	12	// volatile
#define r13	13	// reserved ( small data area )
#define r14	14	// caller safe
#define r15	15	// caller safe
#define r16	16	// caller safe
#define r17	17	// caller safe
#define r18	18	// caller safe
#define r19	19	// caller safe
#define r20	20	// caller safe
#define r21	21	// caller safe
#define r22	22	// caller safe
#define r23	23	// caller safe
#define r24	24	// caller safe
#define r25	25	// caller safe
#define r26	26	// caller safe
#define r27	27	// caller safe
#define r28	28	// caller safe
#define r29	29	// caller safe
#define r30	30	// caller safe
#define r31	31	// caller safe ( environment pointers )

#define f0	0	// callee safe
#define f1	1	// callee safe
#define f2	2	// callee safe
#define f3	3	// callee safe
#define f4	4	// callee safe
#define f5	5	// callee safe
#define f6	6	// callee safe
#define f7	7	// callee safe
#define f8	8	// callee safe
#define f9	9	// callee safe
#define f10	10	// callee safe
#define f11	11	// callee safe
#define f12	12	// callee safe
#define f13	13	// callee safe
#define f14	14	// caller safe
#define f15	15	// caller safe
#define f16	16	// caller safe
#define f17	17	// caller safe
#define f18	18	// caller safe
#define f19	19	// caller safe
#define f20	20	// caller safe
#define f21	21	// caller safe
#define f22	22	// caller safe
#define f23	23	// caller safe
#define f24	24	// caller safe
#define f25	25	// caller safe
#define f26	26	// caller safe
#define f27	27	// caller safe
#define f28	28	// caller safe
#define f29	29	// caller safe
#define f30	30	// caller safe
#define f31	31	// caller safe

#define cr0	0	// volatile
#define cr1	1	// volatile
#define cr2	2	// caller safe
#define cr3	3	// caller safe
#define cr4	4	// caller safe
#define cr5	5	// volatile
#define cr6	6	// volatile
#define cr7	7	// volatile

#define lt	0
#define gt	1
#define eq	2
#define so	3

// branch bo field values
#define branchLikely	1
#define branchFalse	4
#define branchTrue	12
#define branchAlways	20

// branch extensions (change branch type)
#define branchExtLink	0x0001


// Must match powerpc_opcodes below entry for entry (upstream generated it from
// vm_powerpc_asm.c with a perl one-liner).
typedef enum powerpc_iname {
	iCMPLWI, iCMPWI, iCMPW, iCMPLW, iFCMPU, iLI, iLIS, iADDI, iADDIS,
	iBLTm, iBC, iBCL, iB, iBL, iBLR, iBCTR, iBCTRL, iRLWINM, iNOP, iORI,
	iXORIS, iLDX, iLWZX, iSLW, iAND, iSUB, iLBZX, iNEG, iNOT, iSTWX, iSTBX,
	iMULLW, iADD, iLHZX, iXOR, iMFLR, iSTHX, iMR, iOR, iDIVWU, iMTLR,
	iMTCTR, iDIVW, iLFSX, iSRW, iSTFSX, iSRAW, iEXTSH, iEXTSB, iLWZ, iLBZ,
	iSTW, iSTWU, iSTB, iLHZ, iSTH, iLFS, iLFD, iSTFS, iSTFD, iLD, iFDIVS,
	iFSUBS, iFADDS, iFMULS, iSTD, iSTDU, iFRSP, iFCTIW, iFSUB, iFNEG,
} powerpc_iname_t;

#include <stdint.h>

typedef uint32_t ppc_instruction_t;

extern ppc_instruction_t
asm_instruction( powerpc_iname_t, const int, const long int * );

#define IN( inst, args... ) \
({\
	const long int argv[] = { args };\
	const int argc = sizeof( argv ) / sizeof( argv[0] ); \
	asm_instruction( inst, argc, argv );\
})

// 64-bit forms the table lacks: extsw rA,rS and clrldi rA,rS,32 (rldicl rA,rS,0,32)
#define PPC_EXTSW( ra, rs )	( 0x7C0007B4 | ( (ppc_instruction_t)(rs) << 21 ) | ( (ppc_instruction_t)(ra) << 16 ) )
#define PPC_CLRLDI32( ra, rs )	( 0x78000020 | ( (ppc_instruction_t)(rs) << 21 ) | ( (ppc_instruction_t)(ra) << 16 ) )

// BEGIN OF ppc.h

#define ppc_cpu_t int

struct powerpc_opcode
{
	const char *name;
	unsigned long opcode;
	unsigned long mask;
	ppc_cpu_t flags;
	unsigned char operands[8];
};

static const struct powerpc_opcode powerpc_opcodes[];

#define PPC_OPCODE_PPC			 1
#define PPC_OPCODE_POWER		 2
#define PPC_OPCODE_POWER2		 4
#define PPC_OPCODE_32			 8
#define PPC_OPCODE_64		      0x10
#define PPC_OPCODE_601		      0x20
#define PPC_OPCODE_COMMON	      0x40
#define PPC_OPCODE_ANY		      0x80
#define PPC_OPCODE_64_BRIDGE	     0x100
#define PPC_OPCODE_ALTIVEC	     0x200
#define PPC_OPCODE_403		     0x400
#define PPC_OPCODE_BOOKE	     0x800
#define PPC_OPCODE_BOOKE64	    0x1000
#define PPC_OPCODE_440		    0x2000
#define PPC_OPCODE_POWER4	    0x4000
#define PPC_OPCODE_NOPOWER4	    0x8000
#define PPC_OPCODE_CLASSIC	   0x10000
#define PPC_OPCODE_SPE		   0x20000
#define PPC_OPCODE_ISEL		   0x40000
#define PPC_OPCODE_EFS		   0x80000
#define PPC_OPCODE_BRLOCK	  0x100000
#define PPC_OPCODE_PMR		  0x200000
#define PPC_OPCODE_CACHELCK	  0x400000
#define PPC_OPCODE_RFMCI	  0x800000
#define PPC_OPCODE_POWER5	 0x1000000
#define PPC_OPCODE_E300		 0x2000000
#define PPC_OPCODE_POWER6	 0x4000000
#define PPC_OPCODE_CELL		 0x8000000
#define PPC_OPCODE_PPCPS	0x10000000
#define PPC_OPCODE_E500MC	0x20000000
#define PPC_OPCODE_405		0x40000000
#define PPC_OPCODE_VSX		0x80000000

#define PPC_OP(i) (((i) >> 26) & 0x3f)

struct powerpc_operand
{
	unsigned int bitm;
	int shift;
	unsigned long (*insert)
		(unsigned long, long, int, const char **);
	unsigned long flags;
};

static const struct powerpc_operand powerpc_operands[];

#define PPC_OPERAND_SIGNED (0x1)
#define PPC_OPERAND_SIGNOPT (0x2)
#define PPC_OPERAND_FAKE (0x4)
#define PPC_OPERAND_PARENS (0x8)
#define PPC_OPERAND_CR (0x10)
#define PPC_OPERAND_GPR (0x20)
#define PPC_OPERAND_GPR_0 (0x40)
#define PPC_OPERAND_FPR (0x80)
#define PPC_OPERAND_RELATIVE (0x100)
#define PPC_OPERAND_ABSOLUTE (0x200)
#define PPC_OPERAND_OPTIONAL (0x400)
#define PPC_OPERAND_NEXT (0x800)
#define PPC_OPERAND_NEGATIVE (0x1000)
#define PPC_OPERAND_VR (0x2000)
#define PPC_OPERAND_DS (0x4000)
#define PPC_OPERAND_DQ (0x8000)
#define PPC_OPERAND_PLUS1 (0x10000)
#define PPC_OPERAND_FSL (0x20000)
#define PPC_OPERAND_FCR (0x40000)
#define PPC_OPERAND_UDI (0x80000)
#define PPC_OPERAND_VSR (0x100000)

// END OF ppc.h

#define PPC_DEST_ARCH PPC_OPCODE_PPC

// An encoding error is a compile error: upstream printed it and emitted a broken instruction.
static void
PPC_DieAsm( const char *name, const char *what, int argi, unsigned long int op )
{
	Com_sprintf( vmjit_errbuf, sizeof( vmjit_errbuf ), "%s: %s (operand %d, 0x%lx)",
		name, what, argi, op );
	DIE( vmjit_errbuf );
}

ppc_instruction_t
asm_instruction( powerpc_iname_t sname, const int argc, const long int *argv )
{
	const char *errmsg = NULL;
	const char *name;
	unsigned long int ret;
	const struct powerpc_opcode *opcode = NULL;
	int argi, argj;

	opcode = &powerpc_opcodes[ sname ];
	name = opcode->name;

	if ( ( opcode->flags & PPC_DEST_ARCH ) != PPC_DEST_ARCH )
		PPC_DieAsm( name, "opcode not defined for this arch", 0, 0 );

	ret = opcode->opcode;

	argi = argj = 0;
	while ( opcode->operands[ argi ] != 0 ) {
		long int op = 0;
		const struct powerpc_operand *operand = &powerpc_operands[ opcode->operands[ argi ] ];

		if ( ! (operand->flags & PPC_OPERAND_FAKE) ) {
			if ( argj >= argc )
				PPC_DieAsm( name, "not enough arguments", argi, argc );

			op = argv[ argj++ ];
		}

		if ( operand->insert ) {
			errmsg = NULL;
			ret = operand->insert( ret, op, PPC_DEST_ARCH, &errmsg );
			if ( errmsg )
				PPC_DieAsm( name, errmsg, argi, op );
		} else {
			unsigned long int opu = *(unsigned long int *)&op;
			unsigned long int bitm = operand->bitm;
			unsigned long int bitm_full = bitm | ( bitm & 1 ? 0 : 0xf );

			if ( operand->flags & PPC_OPERAND_SIGNED ) {
				bitm_full >>= 1;

				if ( ( opu & ~bitm_full ) != 0 && ( opu | bitm_full ) != -1 )
					PPC_DieAsm( name, "signed operand too wide", argi, opu );
			} else {
				if ( ( opu & ~bitm_full ) != 0 )
					PPC_DieAsm( name, "unsigned operand too wide", argi, opu );
			}
			if ( (bitm & 1) == 0 ) {
				if ( opu & 0xf & ~bitm )
					PPC_DieAsm( name, "operand not aligned", argi, opu );
			}

			ret |= ( op & operand->bitm ) << operand->shift;
		}
		argi++;
	}
	if ( argc > argj )
		PPC_DieAsm( name, "too many arguments", argi, argc );

	return ret;
}


// BEGIN OF ppc-opc.c

#define ATTRIBUTE_UNUSED
#define _(x) (x)

/* Local insertion and extraction functions. */

static unsigned long insert_bdm (unsigned long, long, int, const char **);
static unsigned long insert_bo (unsigned long, long, int, const char **);
static unsigned long insert_ras (unsigned long, long, int, const char **);
static unsigned long insert_rbs (unsigned long, long, int, const char **);

/* The operands table: bitm, shift, insert, flags. */

static const struct powerpc_operand powerpc_operands[] =
{
  /* The zero index is used to indicate the end of the list of
     operands.  */
#define UNUSED 0
  { 0, 0, NULL, 0 },

  /* The BA field in an XL form instruction.  */
#define BA UNUSED + 1
  /* The BI field in a B form or XL form instruction.  */
#define BI BA
#define BI_MASK (0x1f << 16)
  { 0x1f, 16, NULL, PPC_OPERAND_CR },

  /* The BD field in a B form instruction.  The lower two bits are
     forced to zero.  */
#define BD BA + 1
  { 0xfffc, 0, NULL, PPC_OPERAND_RELATIVE | PPC_OPERAND_SIGNED },

  /* The BD field in a B form instruction when the - modifier is used.
     This sets the y bit of the BO field appropriately.  */
#define BDM BD + 1
  { 0xfffc, 0, insert_bdm,
      PPC_OPERAND_RELATIVE | PPC_OPERAND_SIGNED },

  /* The BF field in an X or XL form instruction.  */
#define BF BDM + 1
  /* The CRFD field in an X form instruction.  */
#define CRFD BF
  { 0x7, 23, NULL, PPC_OPERAND_CR },

  /* An optional BF field.  This is used for comparison instructions,
     in which an omitted BF field is taken as zero.  */
#define OBF BF + 1
  { 0x7, 23, NULL, PPC_OPERAND_CR | PPC_OPERAND_OPTIONAL },

  /* The BO field in a B form instruction.  Certain values are
     illegal.  */
#define BO OBF + 1
#define BO_MASK (0x1f << 21)
  { 0x1f, 21, insert_bo, 0 },

  /* The CR number in the BI field of a B or XL form instruction, used by the extended
     conditional branch mnemonics, which set the lower two bits of BI. Optional.  */
#define CR BO + 1
  { 0x7, 18, NULL, PPC_OPERAND_CR | PPC_OPERAND_OPTIONAL },

  /* The D field in a D form instruction: a displacement off a register, which
     implies that the next operand is a register in parentheses.  */
#define D CR + 1
  { 0xffff, 0, NULL, PPC_OPERAND_PARENS | PPC_OPERAND_SIGNED },

  /* The DS field in a DS form instruction.  This is like D, but the
     lower two bits are forced to zero.  */
#define DS D + 1
  { 0xfffc, 0, NULL,
    PPC_OPERAND_PARENS | PPC_OPERAND_SIGNED | PPC_OPERAND_DS },

  /* The FRA field in an X or A form instruction.  */
#define FRA DS + 1
#define FRA_MASK (0x1f << 16)
  { 0x1f, 16, NULL, PPC_OPERAND_FPR },

  /* The FRB field in an X or A form instruction.  */
#define FRB FRA + 1
#define FRB_MASK (0x1f << 11)
  { 0x1f, 11, NULL, PPC_OPERAND_FPR },

  /* The FRC field in an A form instruction.  */
#define FRC FRB + 1
#define FRC_MASK (0x1f << 6)
  { 0x1f, 6, NULL, PPC_OPERAND_FPR },

  /* The FRS field in an X form instruction or the FRT field in a D, X
     or A form instruction.  */
#define FRS FRC + 1
#define FRT FRS
  { 0x1f, 21, NULL, PPC_OPERAND_FPR },

  /* The LI field in an I form instruction.  The lower two bits are
     forced to zero.  */
#define LI FRS + 1
  { 0x3fffffc, 0, NULL, PPC_OPERAND_RELATIVE | PPC_OPERAND_SIGNED },

  /* The ME field in an M form instruction.  */
#define ME LI + 1
#define ME_MASK (0x1f << 1)
  { 0x1f, 1, NULL, 0 },

  /* The MB and ME fields of an M form instruction as one bitmask operand selecting the
     bits; a two operand form using PPC_OPERAND_NEXT (see opcode/ppc.h).  */
#define MBE ME + 1
  { 0x1f, 6, NULL, PPC_OPERAND_OPTIONAL | PPC_OPERAND_NEXT },

  /* The RA field in an D, DS, DQ, X, XO, M, or MDS form instruction.  */
#define RA MBE + 1
#define RA_MASK (0x1f << 16)
  { 0x1f, 16, NULL, PPC_OPERAND_GPR },

  /* As above, but 0 in the RA field means zero, not r0.  */
#define RA0 RA + 1
  { 0x1f, 16, NULL, PPC_OPERAND_GPR_0 },

  /* The RA field of an updating store or updating floating point load in D or X form,
     which means that the RA field may not be zero.  */
#define RAS RA0 + 1
  { 0x1f, 16, insert_ras, PPC_OPERAND_GPR_0 },

  /* The RB field in an X, XO, M, or MDS form instruction.  */
#define RB RAS + 1
#define RB_MASK (0x1f << 11)
  { 0x1f, 11, NULL, PPC_OPERAND_GPR },

  /* The RB field of an X form instruction when it must equal the RS field, for
     extended mnemonics like mr.  */
#define RBS RB + 1
  { 0x1f, 11, insert_rbs, PPC_OPERAND_FAKE },

  /* The RS field in a D, DS, X, XFX, XS, M, MD or MDS form instruction, or the RT
     field in a D, DS, X, XFX or XO form instruction.  */
#define RS RBS + 1
#define RT RS
#define RT_MASK (0x1f << 21)
  { 0x1f, 21, NULL, PPC_OPERAND_GPR },

  /* The SH field in an X or M form instruction.  */
#define SH RS + 1
#define SH_MASK (0x1f << 11)
  /* The other UIMM field in an EVX form instruction.  */
#define EVUIMM SH
  { 0x1f, 11, NULL, 0 },

  /* The SI field in a D form instruction.  */
#define SI SH + 1
  { 0xffff, 0, NULL, PPC_OPERAND_SIGNED },

  /* The UI field in a D form instruction.  */
#define UI SI + 1
  { 0xffff, 0, NULL, 0 },

};


/* The functions used to insert and extract complicated operands.  */

/* BD field with the - (not taken) modifier. Before Power4 it sets the y bit of BO when the
   offset is negative; Power4 sets the "at" bits to 10 (not taken) instead.  */

static unsigned long
insert_bdm (unsigned long insn,
	    long value,
	    int dialect,
	    const char **errmsg ATTRIBUTE_UNUSED)
{
  if ((dialect & PPC_OPCODE_POWER4) == 0)
    {
      if ((value & 0x8000) != 0)
	insn |= 1 << 21;
    }
  else
    {
      if ((insn & (0x14 << 21)) == (0x04 << 21))
	insn |= 0x02 << 21;
      else if ((insn & (0x14 << 21)) == (0x10 << 21))
	insn |= 0x08 << 21;
    }
  return insn | (value & 0xfffc);
}


/* Check for legal values of a BO field.  */

static int
valid_bo (long value, int dialect, int extract)
{
  if ((dialect & PPC_OPCODE_POWER4) == 0)
    {
      int valid;
      /* Encodings with bits that must be zero (z must be zero, y may be anything):
	 001zy 011zy 1z00y 1z01y 1z1zz  */
      switch (value & 0x14)
	{
	default:
	case 0:
	  valid = 1;
	  break;
	case 0x4:
	  valid = (value & 0x2) == 0;
	  break;
	case 0x10:
	  valid = (value & 0x8) == 0;
	  break;
	case 0x14:
	  valid = value == 0x14;
	  break;
	}
      /* When disassembling with -Many, accept power4 encodings too.  */
      if (valid
	  || (dialect & PPC_OPCODE_ANY) == 0
	  || !extract)
	return valid;
    }

  /* Power4 encodings with bits that must be zero (z must be zero, a & t may be anything):
     0000z 0001z 0100z 0101z 001at 011at 1a00t 1a01t 1z1zz  */
  if ((value & 0x14) == 0)
    return (value & 0x1) == 0;
  else if ((value & 0x14) == 0x14)
    return value == 0x14;
  else
    return 1;
}

/* The BO field in a B form instruction.  Warn about attempts to set
   the field to an illegal value.  */

static unsigned long
insert_bo (unsigned long insn,
	   long value,
	   int dialect,
	   const char **errmsg)
{
  if (!valid_bo (value, dialect, 0))
    *errmsg = _("invalid conditional option");
  return insn | ((value & 0x1f) << 21);
}

/* The RA field of an updating store or updating floating point load in D or X form,
   which means that the RA field may not be zero.  */

static unsigned long
insert_ras (unsigned long insn,
	    long value,
	    int dialect ATTRIBUTE_UNUSED,
	    const char **errmsg)
{
  if (value == 0)
    *errmsg = _("invalid register operand when updating");
  return insn | ((value & 0x1f) << 16);
}

/* The RB field when it must equal RS, for extended mnemonics like mr: this FAKE operand
   copies the BT field into the BA field.  */

static unsigned long
insert_rbs (unsigned long insn,
	    long value ATTRIBUTE_UNUSED,
	    int dialect ATTRIBUTE_UNUSED,
	    const char **errmsg ATTRIBUTE_UNUSED)
{
  return insn | (((insn >> 21) & 0x1f) << 11);
}


/* Macros used to form opcodes.  */

/* The main opcode.  */
#define OP(x) ((((unsigned long)(x)) & 0x3f) << 26)
#define OP_MASK OP (0x3f)

/* The main opcode combined with a trap code in the TO field of a D form instruction,
   for the extended trap mnemonics.  */
#define OPTO(x,to) (OP (x) | ((((unsigned long)(to)) & 0x1f) << 21))
#define OPTO_MASK (OP_MASK | TO_MASK)

/* The main opcode combined with a comparison size bit in the L field of a D or X form
   instruction, for the extended comparison mnemonics.  */
#define OPL(x,l) (OP (x) | ((((unsigned long)(l)) & 1) << 21))
#define OPL_MASK OPL (0x3f,1)

/* An A form instruction.  */
#define A(op, xop, rc) (OP (op) | ((((unsigned long)(xop)) & 0x1f) << 1) | (((unsigned long)(rc)) & 1))
#define A_MASK A (0x3f, 0x1f, 1)

/* An A_MASK with the FRB field fixed.  */
#define AFRB_MASK (A_MASK | FRB_MASK)

/* An A_MASK with the FRC field fixed.  */
#define AFRC_MASK (A_MASK | FRC_MASK)

/* An A_MASK with the FRA and FRC fields fixed.  */
#define AFRAFRC_MASK (A_MASK | FRA_MASK | FRC_MASK)

/* An AFRAFRC_MASK, but with L bit clear.  */
#define AFRALFRC_MASK (AFRAFRC_MASK & ~((unsigned long) 1 << 16))

/* A B form instruction.  */
#define B(op, aa, lk) (OP (op) | ((((unsigned long)(aa)) & 1) << 1) | ((lk) & 1))
#define B_MASK B (0x3f, 1, 1)

/* A B form instruction setting the BO field.  */
#define BBO(op, bo, aa, lk) (B ((op), (aa), (lk)) | ((((unsigned long)(bo)) & 0x1f) << 21))
#define BBO_MASK BBO (0x3f, 0x1f, 1, 1)

/* A BBO_MASK without the y bit of BO, to match a conditional branch whatever its y bit;
   likewise for the "at" bits of the power4 branch hints.  */
#define Y_MASK   (((unsigned long) 1) << 21)
#define AT1_MASK (((unsigned long) 3) << 21)
#define AT2_MASK (((unsigned long) 9) << 21)
#define BBOY_MASK  (BBO_MASK &~ Y_MASK)
#define BBOAT_MASK (BBO_MASK &~ AT1_MASK)

/* A B form instruction setting the BO field and the condition bits of
   the BI field.  */
#define BBOCB(op, bo, cb, aa, lk) \
  (BBO ((op), (bo), (aa), (lk)) | ((((unsigned long)(cb)) & 0x3) << 16))
#define BBOCB_MASK BBOCB (0x3f, 0x1f, 0x3, 1, 1)

/* A BBOCB_MASK with the y bit of the BO field removed.  */
#define BBOYCB_MASK (BBOCB_MASK &~ Y_MASK)
#define BBOATCB_MASK (BBOCB_MASK &~ AT1_MASK)
#define BBOAT2CB_MASK (BBOCB_MASK &~ AT2_MASK)

/* A BBOYCB_MASK in which the BI field is fixed.  */
#define BBOYBI_MASK (BBOYCB_MASK | BI_MASK)
#define BBOATBI_MASK (BBOAT2CB_MASK | BI_MASK)

/* An Context form instruction.  */
#define CTX(op, xop)   (OP (op) | (((unsigned long)(xop)) & 0x7))
#define CTX_MASK CTX(0x3f, 0x7)

/* An User Context form instruction.  */
#define UCTX(op, xop)  (OP (op) | (((unsigned long)(xop)) & 0x1f))
#define UCTX_MASK UCTX(0x3f, 0x1f)

/* The main opcode mask with the RA field clear.  */
#define DRA_MASK (OP_MASK | RA_MASK)

/* A DS form instruction.  */
#define DSO(op, xop) (OP (op) | ((xop) & 0x3))
#define DS_MASK DSO (0x3f, 3)

/* A DE form instruction.  */
#define DEO(op, xop) (OP (op) | ((xop) & 0xf))
#define DE_MASK DEO (0x3e, 0xf)

/* An EVSEL form instruction.  */
#define EVSEL(op, xop) (OP (op) | (((unsigned long)(xop)) & 0xff) << 3)
#define EVSEL_MASK EVSEL(0x3f, 0xff)

/* An M form instruction.  */
#define M(op, rc) (OP (op) | ((rc) & 1))
#define M_MASK M (0x3f, 1)

/* An M form instruction with the ME field specified.  */
#define MME(op, me, rc) (M ((op), (rc)) | ((((unsigned long)(me)) & 0x1f) << 1))

/* An M_MASK with the MB and ME fields fixed.  */
#define MMBME_MASK (M_MASK | MB_MASK | ME_MASK)

/* An M_MASK with the SH and ME fields fixed.  */
#define MSHME_MASK (M_MASK | SH_MASK | ME_MASK)

/* An MD form instruction.  */
#define MD(op, xop, rc) (OP (op) | ((((unsigned long)(xop)) & 0x7) << 2) | ((rc) & 1))
#define MD_MASK MD (0x3f, 0x7, 1)

/* An MD_MASK with the MB field fixed.  */
#define MDMB_MASK (MD_MASK | MB6_MASK)

/* An MD_MASK with the SH field fixed.  */
#define MDSH_MASK (MD_MASK | SH6_MASK)

/* An MDS form instruction.  */
#define MDS(op, xop, rc) (OP (op) | ((((unsigned long)(xop)) & 0xf) << 1) | ((rc) & 1))
#define MDS_MASK MDS (0x3f, 0xf, 1)

/* An MDS_MASK with the MB field fixed.  */
#define MDSMB_MASK (MDS_MASK | MB6_MASK)

/* An SC form instruction.  */
#define SC(op, sa, lk) (OP (op) | ((((unsigned long)(sa)) & 1) << 1) | ((lk) & 1))
#define SC_MASK (OP_MASK | (((unsigned long)0x3ff) << 16) | (((unsigned long)1) << 1) | 1)

/* A VX form instruction.  */
#define VX(op, xop) (OP (op) | (((unsigned long)(xop)) & 0x7ff))

/* The mask for a VX form instruction.  */
#define VX_MASK	VX(0x3f, 0x7ff)

/* A VA form instruction.  */
#define VXA(op, xop) (OP (op) | (((unsigned long)(xop)) & 0x03f))

/* The mask for a VA form instruction.  */
#define VXA_MASK VXA(0x3f, 0x3f)

/* A VXR form instruction.  */
#define VXR(op, xop, rc) (OP (op) | (((rc) & 1) << 10) | (((unsigned long)(xop)) & 0x3ff))

/* The mask for a VXR form instruction.  */
#define VXR_MASK VXR(0x3f, 0x3ff, 1)

/* An X form instruction.  */
#define X(op, xop) (OP (op) | ((((unsigned long)(xop)) & 0x3ff) << 1))

/* A Z form instruction.  */
#define Z(op, xop) (OP (op) | ((((unsigned long)(xop)) & 0x1ff) << 1))

/* An X form instruction with the RC bit specified.  */
#define XRC(op, xop, rc) (X ((op), (xop)) | ((rc) & 1))

/* A Z form instruction with the RC bit specified.  */
#define ZRC(op, xop, rc) (Z ((op), (xop)) | ((rc) & 1))

/* The mask for an X form instruction.  */
#define X_MASK XRC (0x3f, 0x3ff, 1)

/* The mask for a Z form instruction.  */
#define Z_MASK ZRC (0x3f, 0x1ff, 1)
#define Z2_MASK ZRC (0x3f, 0xff, 1)

/* An X_MASK with the RA field fixed.  */
#define XRA_MASK (X_MASK | RA_MASK)

/* An XRA_MASK with the W field clear.  */
#define XWRA_MASK (XRA_MASK & ~((unsigned long) 1 << 16))

/* An X_MASK with the RB field fixed.  */
#define XRB_MASK (X_MASK | RB_MASK)

/* An X_MASK with the RT field fixed.  */
#define XRT_MASK (X_MASK | RT_MASK)

/* An XRT_MASK mask with the L bits clear.  */
#define XLRT_MASK (XRT_MASK & ~((unsigned long) 0x3 << 21))

/* An X_MASK with the RA and RB fields fixed.  */
#define XRARB_MASK (X_MASK | RA_MASK | RB_MASK)

/* An XRARB_MASK, but with the L bit clear.  */
#define XRLARB_MASK (XRARB_MASK & ~((unsigned long) 1 << 16))

/* An X_MASK with the RT and RA fields fixed.  */
#define XRTRA_MASK (X_MASK | RT_MASK | RA_MASK)

/* An XRTRA_MASK, but with L bit clear.  */
#define XRTLRA_MASK (XRTRA_MASK & ~((unsigned long) 1 << 21))

/* An X form instruction with the L bit specified.  */
#define XOPL(op, xop, l) (X ((op), (xop)) | ((((unsigned long)(l)) & 1) << 21))

/* The mask for an X form comparison instruction.  */
#define XCMP_MASK (X_MASK | (((unsigned long)1) << 22))

/* The mask for an X form comparison instruction with the L field
   fixed.  */
#define XCMPL_MASK (XCMP_MASK | (((unsigned long)1) << 21))

/* An X form trap instruction with the TO field specified.  */
#define XTO(op, xop, to) (X ((op), (xop)) | ((((unsigned long)(to)) & 0x1f) << 21))
#define XTO_MASK (X_MASK | TO_MASK)

/* An X form tlb instruction with the SH field specified.  */
#define XTLB(op, xop, sh) (X ((op), (xop)) | ((((unsigned long)(sh)) & 0x1f) << 11))
#define XTLB_MASK (X_MASK | SH_MASK)

/* An X form sync instruction.  */
#define XSYNC(op, xop, l) (X ((op), (xop)) | ((((unsigned long)(l)) & 3) << 21))

/* An X form sync instruction with everything filled in except the LS field.  */
#define XSYNC_MASK (0xff9fffff)

/* An X_MASK, but with the EH bit clear.  */
#define XEH_MASK (X_MASK & ~((unsigned long )1))

/* An X form AltiVec dss instruction.  */
#define XDSS(op, xop, a) (X ((op), (xop)) | ((((unsigned long)(a)) & 1) << 25))
#define XDSS_MASK XDSS(0x3f, 0x3ff, 1)

/* An XFL form instruction.  */
#define XFL(op, xop, rc) (OP (op) | ((((unsigned long)(xop)) & 0x3ff) << 1) | (((unsigned long)(rc)) & 1))
#define XFL_MASK XFL (0x3f, 0x3ff, 1)

/* An X form isel instruction.  */
#define XISEL(op, xop)  (OP (op) | ((((unsigned long)(xop)) & 0x1f) << 1))
#define XISEL_MASK      XISEL(0x3f, 0x1f)

/* An XL form instruction with the LK field set to 0.  */
#define XL(op, xop) (OP (op) | ((((unsigned long)(xop)) & 0x3ff) << 1))

/* An XL form instruction which uses the LK field.  */
#define XLLK(op, xop, lk) (XL ((op), (xop)) | ((lk) & 1))

/* The mask for an XL form instruction.  */
#define XL_MASK XLLK (0x3f, 0x3ff, 1)

/* An XL form instruction which explicitly sets the BO field.  */
#define XLO(op, bo, xop, lk) \
  (XLLK ((op), (xop), (lk)) | ((((unsigned long)(bo)) & 0x1f) << 21))
#define XLO_MASK (XL_MASK | BO_MASK)

/* An XL form instruction which explicitly sets the y bit of the BO
   field.  */
#define XLYLK(op, xop, y, lk) (XLLK ((op), (xop), (lk)) | ((((unsigned long)(y)) & 1) << 21))
#define XLYLK_MASK (XL_MASK | Y_MASK)

/* An XL form instruction which sets the BO field and the condition
   bits of the BI field.  */
#define XLOCB(op, bo, cb, xop, lk) \
  (XLO ((op), (bo), (xop), (lk)) | ((((unsigned long)(cb)) & 3) << 16))
#define XLOCB_MASK XLOCB (0x3f, 0x1f, 0x3, 0x3ff, 1)

#define BB_MASK (0x1f << 11)
/* An XL_MASK or XLYLK_MASK or XLOCB_MASK with the BB field fixed.  */
#define XLBB_MASK (XL_MASK | BB_MASK)
#define XLYBB_MASK (XLYLK_MASK | BB_MASK)
#define XLBOCBBB_MASK (XLOCB_MASK | BB_MASK)

/* A mask for branch instructions using the BH field.  */
#define XLBH_MASK (XL_MASK | (0x1c << 11))

/* An XL_MASK with the BO and BB fields fixed.  */
#define XLBOBB_MASK (XL_MASK | BO_MASK | BB_MASK)

/* An XL_MASK with the BO, BI and BB fields fixed.  */
#define XLBOBIBB_MASK (XL_MASK | BO_MASK | BI_MASK | BB_MASK)

/* An XO form instruction.  */
#define XO(op, xop, oe, rc) \
  (OP (op) | ((((unsigned long)(xop)) & 0x1ff) << 1) | ((((unsigned long)(oe)) & 1) << 10) | (((unsigned long)(rc)) & 1))
#define XO_MASK XO (0x3f, 0x1ff, 1, 1)

/* An XO_MASK with the RB field fixed.  */
#define XORB_MASK (XO_MASK | RB_MASK)

/* An XS form instruction.  */
#define XS(op, xop, rc) (OP (op) | ((((unsigned long)(xop)) & 0x1ff) << 2) | (((unsigned long)(rc)) & 1))
#define XS_MASK XS (0x3f, 0x1ff, 1)

/* A mask for the FXM version of an XFX form instruction.  */
#define XFXFXM_MASK (X_MASK | (1 << 11) | (1 << 20))

/* An XFX form instruction with the FXM field filled in.  */
#define XFXM(op, xop, fxm, p4) \
  (X ((op), (xop)) | ((((unsigned long)(fxm)) & 0xff) << 12) \
   | ((unsigned long)(p4) << 20))

#define SPR_MASK (0x3ff << 11)
/* An XFX form instruction with the SPR field filled in.  */
#define XSPR(op, xop, spr) \
  (X ((op), (xop)) | ((((unsigned long)(spr)) & 0x1f) << 16) | ((((unsigned long)(spr)) & 0x3e0) << 6))
#define XSPR_MASK (X_MASK | SPR_MASK)

/* An XFX form instruction with the SPR field filled in except for the
   SPRBAT field.  */
#define XSPRBAT_MASK (XSPR_MASK &~ SPRBAT_MASK)

/* An XFX form instruction with the SPR field filled in except for the
   SPRG field.  */
#define XSPRG_MASK (XSPR_MASK & ~(0x1f << 16))

/* An X form instruction with everything filled in except the E field.  */
#define XE_MASK (0xffff7fff)

/* An X form user context instruction.  */
#define XUC(op, xop)  (OP (op) | (((unsigned long)(xop)) & 0x1f))
#define XUC_MASK      XUC(0x3f, 0x1f)

/* The BO encodings used in extended conditional branch mnemonics.  */
#define BODNZF	(0x0)
#define BODNZFP	(0x1)
#define BODZF	(0x2)
#define BODZFP	(0x3)
#define BODNZT	(0x8)
#define BODNZTP	(0x9)
#define BODZT	(0xa)
#define BODZTP	(0xb)

#define BOF	(0x4)
#define BOFP	(0x5)
#define BOFM4	(0x6)
#define BOFP4	(0x7)
#define BOT	(0xc)
#define BOTP	(0xd)
#define BOTM4	(0xe)
#define BOTP4	(0xf)

#define BODNZ	(0x10)
#define BODNZP	(0x11)
#define BODZ	(0x12)
#define BODZP	(0x13)
#define BODNZM4 (0x18)
#define BODNZP4 (0x19)
#define BODZM4	(0x1a)
#define BODZP4	(0x1b)

#define BOU	(0x14)

/* The BI condition bit encodings used in extended conditional branch
   mnemonics.  */
#define CBLT	(0)
#define CBGT	(1)
#define CBEQ	(2)
#define CBSO	(3)

/* The TO encodings used in extended trap mnemonics.  */
#define TOLGT	(0x1)
#define TOLLT	(0x2)
#define TOEQ	(0x4)
#define TOLGE	(0x5)
#define TOLNL	(0x5)
#define TOLLE	(0x6)
#define TOLNG	(0x6)
#define TOGT	(0x8)
#define TOGE	(0xc)
#define TONL	(0xc)
#define TOLT	(0x10)
#define TOLE	(0x14)
#define TONG	(0x14)
#define TONE	(0x18)
#define TOU	(0x1f)

/* Smaller names for the flags so each entry in the opcodes table will
   fit on a single line.  */
#undef	PPC
#define PPC     PPC_OPCODE_PPC
#define PPCCOM	PPC_OPCODE_PPC | PPC_OPCODE_COMMON
#define PPC64   PPC_OPCODE_64 | PPC_OPCODE_PPC
#define	COM     PPC_OPCODE_POWER | PPC_OPCODE_PPC | PPC_OPCODE_COMMON
#define	COM32   PPC_OPCODE_POWER | PPC_OPCODE_PPC | PPC_OPCODE_COMMON | PPC_OPCODE_32

/* The opcode table: NAME OPCODE MASK FLAGS { OPERANDS }, sorted by major opcode with the more
   specific entries first, because the disassembler prints the first one that matches.  */

static const struct powerpc_opcode powerpc_opcodes[] = {

{ "cmplwi",  OPL(10,0),	OPL_MASK,	PPCCOM,		{ OBF, RA, UI } },
{ "cmpwi",   OPL(11,0),	OPL_MASK,	PPCCOM,		{ OBF, RA, SI } },
{ "cmpw",    XOPL(31,0,0), XCMPL_MASK,	PPCCOM,		{ OBF, RA, RB } },
{ "cmplw",   XOPL(31,32,0), XCMPL_MASK, PPCCOM,	{ OBF, RA, RB } },
{ "fcmpu",   X(63,0),	X_MASK|(3<<21),	COM,		{ BF, FRA, FRB } },

{ "li",	     OP(14),	DRA_MASK,	PPCCOM,		{ RT, SI } },
{ "lis",     OP(15),	DRA_MASK,	PPCCOM,		{ RT, SI } },

{ "addi",    OP(14),	OP_MASK,	PPCCOM,		{ RT, RA0, SI } },
{ "addis",   OP(15),	OP_MASK,	PPCCOM,		{ RT,RA0,SI } },
{ "blt-",    BBOCB(16,BOT,CBLT,0,0), BBOATCB_MASK, PPCCOM,	{ CR, BDM } },
{ "bc",	     B(16,0,0),	B_MASK,		COM,		{ BO, BI, BD } },
{ "bcl",     B(16,0,1),	B_MASK,		COM,		{ BO, BI, BD } },
{ "b",	     B(18,0,0),	B_MASK,		COM,		{ LI } },
{ "bl",      B(18,0,1),	B_MASK,		COM,		{ LI } },
{ "blr",     XLO(19,BOU,16,0), XLBOBIBB_MASK, PPCCOM,	{ 0 } },
{ "bctr",    XLO(19,BOU,528,0), XLBOBIBB_MASK, COM,	{ 0 } },
{ "bctrl",   XLO(19,BOU,528,1), XLBOBIBB_MASK, COM,	{ 0 } },

{ "rlwinm",  M(21,0),	M_MASK,		PPCCOM,		{ RA,RS,SH,MBE,ME } },
{ "nop",     OP(24),	0xffffffff,	PPCCOM,		{ 0 } },
{ "ori",     OP(24),	OP_MASK,	PPCCOM,		{ RA, RS, UI } },
{ "xoris",   OP(27),	OP_MASK,	PPCCOM,		{ RA, RS, UI } },
{ "ldx",     X(31,21),	X_MASK,		PPC64,		{ RT, RA0, RB } },
{ "lwzx",    X(31,23),	X_MASK,		PPCCOM,		{ RT, RA0, RB } },
{ "slw",     XRC(31,24,0), X_MASK,	PPCCOM,		{ RA, RS, RB } },
{ "and",     XRC(31,28,0), X_MASK,	COM,		{ RA, RS, RB } },
{ "sub",     XO(31,40,0,0), XO_MASK,	PPC,		{ RT, RB, RA } },
{ "lbzx",    X(31,87),	X_MASK,		COM,		{ RT, RA0, RB } },
{ "neg",     XO(31,104,0,0), XORB_MASK,	COM,		{ RT, RA } },
{ "not",     XRC(31,124,0), X_MASK,	COM,		{ RA, RS, RBS } },
{ "stwx",    X(31,151), X_MASK,		PPCCOM,		{ RS, RA0, RB } },
{ "stbx",    X(31,215),	X_MASK,		COM,		{ RS, RA0, RB } },
{ "mullw",   XO(31,235,0,0), XO_MASK,	PPCCOM,		{ RT, RA, RB } },
{ "add",     XO(31,266,0,0), XO_MASK,	PPCCOM,		{ RT, RA, RB } },
{ "lhzx",    X(31,279),	X_MASK,		COM,		{ RT, RA0, RB } },
{ "xor",     XRC(31,316,0), X_MASK,	COM,		{ RA, RS, RB } },
{ "mflr",    XSPR(31,339,8), XSPR_MASK, COM,		{ RT } },
{ "sthx",    X(31,407),	X_MASK,		COM,		{ RS, RA0, RB } },
{ "mr",	     XRC(31,444,0), X_MASK,	COM,		{ RA, RS, RBS } },
{ "or",      XRC(31,444,0), X_MASK,	COM,		{ RA, RS, RB } },
{ "divwu",   XO(31,459,0,0), XO_MASK,	PPC,		{ RT, RA, RB } },
{ "mtlr",    XSPR(31,467,8), XSPR_MASK, COM,		{ RS } },
{ "mtctr",   XSPR(31,467,9), XSPR_MASK, COM,		{ RS } },
{ "divw",    XO(31,491,0,0), XO_MASK,	PPC,		{ RT, RA, RB } },
{ "lfsx",    X(31,535),	X_MASK,		COM,		{ FRT, RA0, RB } },
{ "srw",     XRC(31,536,0), X_MASK,	PPCCOM,		{ RA, RS, RB } },
{ "stfsx",   X(31,663), X_MASK,		COM,		{ FRS, RA0, RB } },
{ "sraw",    XRC(31,792,0), X_MASK,	PPCCOM,		{ RA, RS, RB } },
{ "extsh",   XRC(31,922,0), XRB_MASK,	PPCCOM,		{ RA, RS } },
{ "extsb",   XRC(31,954,0), XRB_MASK,	PPC,		{ RA, RS} },

{ "lwz",     OP(32),	OP_MASK,	PPCCOM,		{ RT, D, RA0 } },
{ "lbz",     OP(34),	OP_MASK,	COM,		{ RT, D, RA0 } },
{ "stw",     OP(36),	OP_MASK,	PPCCOM,		{ RS, D, RA0 } },
{ "stwu",    OP(37),	OP_MASK,	PPCCOM,		{ RS, D, RAS } },
{ "stb",     OP(38),	OP_MASK,	COM,		{ RS, D, RA0 } },
{ "lhz",     OP(40),	OP_MASK,	COM,		{ RT, D, RA0 } },
{ "sth",     OP(44),	OP_MASK,	COM,		{ RS, D, RA0 } },
{ "lfs",     OP(48),	OP_MASK,	COM,		{ FRT, D, RA0 } },
{ "lfd",     OP(50),	OP_MASK,	COM,		{ FRT, D, RA0 } },
{ "stfs",    OP(52),	OP_MASK,	COM,		{ FRS, D, RA0 } },
{ "stfd",    OP(54),	OP_MASK,	COM,		{ FRS, D, RA0 } },
{ "ld",      DSO(58,0),	DS_MASK,	PPC64,		{ RT, DS, RA0 } },

{ "fdivs",   A(59,18,0), AFRC_MASK,	PPC,		{ FRT, FRA, FRB } },
{ "fsubs",   A(59,20,0), AFRC_MASK,	PPC,		{ FRT, FRA, FRB } },
{ "fadds",   A(59,21,0), AFRC_MASK,	PPC,		{ FRT, FRA, FRB } },
{ "fmuls",   A(59,25,0), AFRB_MASK,	PPC,		{ FRT, FRA, FRC } },
{ "std",     DSO(62,0),	DS_MASK,	PPC64,		{ RS, DS, RA0 } },
{ "stdu",    DSO(62,1),	DS_MASK,	PPC64,		{ RS, DS, RAS } },
{ "frsp",    XRC(63,12,0), XRA_MASK,	COM,		{ FRT, FRB } },
{ "fctiw",   XRC(63,14,0), XRA_MASK,	PPCCOM,		{ FRT, FRB } },
{ "fsub",    A(63,20,0), AFRC_MASK,	PPCCOM,		{ FRT, FRA, FRB } },
{ "fneg",    XRC(63,40,0), XRA_MASK,	COM,		{ FRT, FRB } },
};

// VM_TIMES: time spent inside and outside generated code (debug only, needs times())
//#define VM_TIMES
#ifdef VM_TIMES
#include <sys/times.h>
static clock_t time_outside_vm = 0;
static clock_t time_total_vm = 0;
#endif

// hole: fold OP_CONST into the next op; copy: inline OP_BLOCK_COPY up to 16/32 bytes, off on PS3
// (its pointers are not masked: every copy goes through the checked VM_BlockCopy); mask: one rlwinm
#ifdef __OPTIMIZE__
# define OPTIMIZE_HOLE 1
# ifdef __PS3__
#  define OPTIMIZE_COPY 0
# else
#  define OPTIMIZE_COPY 1
# endif
# define OPTIMIZE_MASK 1
#else
# define OPTIMIZE_HOLE 0
# define OPTIMIZE_COPY 0
# define OPTIMIZE_MASK 0
#endif

// ABIs: ELF32 (LR at r0+4), ELF64 (OPDs, LR at r0+16, locals from 48, saves at 128+), OS X 32
// (LR at r0+8, locals from 24, saves at 64+), OS X 64 (untested). PS3 is ELF64.

/* Select Length - first value on 32 bits, second on 64 */
#ifdef __PPC64__
#  define SL( a, b ) (b)
#else
#  define SL( a, b ) (a)
#endif

/* Select ABI - first for ELF, second for OS X */
#ifdef __ELF__
#  define SA( a, b ) (a)
#else
#  define SA( a, b ) (b)
#endif

#define ELF32	SL( SA( 1, 0 ), 0 )
#define ELF64	SL( 0, SA( 1, 0 ) )
#define OSX32	SL( SA( 0, 1 ), 0 )
#define OSX64	SL( 0, SA( 0, 1 ) )

/* native length load/store instructions ( L stands for long ) */
#define iSTLU	SL( iSTWU, iSTDU )
#define iSTL	SL( iSTW, iSTD )
#define iLL	SL( iLWZ, iLD )
#define iLLX	SL( iLWZX, iLDX )

/* register length */
#define GPRLEN	SL( 4, 8 )
#define FPRLEN	(8)
/* shift that many bits to obtain value miltiplied by GPRLEN */
#define GPRLEN_SHIFT	SL( 2, 3 )

/* Link register position */
#define STACK_LR	SL( SA( 4, 8 ), 16 )
/* register save position */
#define STACK_SAVE	SL( SA( 16, 64 ), 128 )
/* temporary space, for float<->int exchange */
#define STACK_TEMP	SL( SA( 8, 24 ), 48 )
/* red zone temporary space, used instead of STACK_TEMP if stack isn't
 * prepared properly */
#define STACK_RTEMP	(-16)

#if ELF64
// Official Procedure Descriptor: the generated code needs one to be called as a function
typedef struct {
	void *function;
	void *toc;
	void *env;
} opd_t;
#endif


// opcode information: length of the immediate, returned and required register types
#define opImm0	0x0000 /* no immediate */
#define opImm1	0x0001 /* 1 byte immadiate value after opcode */
#define opImm4	0x0002 /* 4 bytes immediate value after opcode */

#define opRet0	0x0000 /* returns nothing */
#define opRetI	0x0004 /* returns integer */
#define opRetF	0x0008 /* returns float */
#define opRetIF	(opRetI | opRetF) /* returns integer or float */

#define opArg0	0x0000 /* requires nothing */
#define opArgI	0x0010 /* requires integer(s) */
#define opArgF	0x0020 /* requires float(s) */
#define opArgIF	(opArgI | opArgF) /* requires integer or float */

#define opArg2I	0x0040 /* requires second argument, integer */
#define opArg2F	0x0080 /* requires second argument, float */
#define opArg2IF (opArg2I | opArg2F) /* requires second argument, integer or float */

static const unsigned char vm_opInfo[256] =
{
	[OP_UNDEF]	= opImm0,
	[OP_IGNORE]	= opImm0,
	[OP_BREAK]	= opImm0,
	[OP_ENTER]	= opImm4,
			/* OP_LEAVE has to accept floats, they will be converted to ints */
	[OP_LEAVE]	= opImm4 | opRet0 | opArgIF,
			/* only STORE4 and POP use values from OP_CALL,
			 * no need to convert floats back */
	[OP_CALL]	= opImm0 | opRetI | opArgI,
	[OP_PUSH]	= opImm0 | opRetIF,
	[OP_POP]	= opImm0 | opRet0 | opArgIF,
	[OP_CONST]	= opImm4 | opRetIF,
	[OP_LOCAL]	= opImm4 | opRetI,
	[OP_JUMP]	= opImm0 | opRet0 | opArgI,

	[OP_EQ]		= opImm4 | opRet0 | opArgI | opArg2I,
	[OP_NE]		= opImm4 | opRet0 | opArgI | opArg2I,
	[OP_LTI]	= opImm4 | opRet0 | opArgI | opArg2I,
	[OP_LEI]	= opImm4 | opRet0 | opArgI | opArg2I,
	[OP_GTI]	= opImm4 | opRet0 | opArgI | opArg2I,
	[OP_GEI]	= opImm4 | opRet0 | opArgI | opArg2I,
	[OP_LTU]	= opImm4 | opRet0 | opArgI | opArg2I,
	[OP_LEU]	= opImm4 | opRet0 | opArgI | opArg2I,
	[OP_GTU]	= opImm4 | opRet0 | opArgI | opArg2I,
	[OP_GEU]	= opImm4 | opRet0 | opArgI | opArg2I,
	[OP_EQF]	= opImm4 | opRet0 | opArgF | opArg2F,
	[OP_NEF]	= opImm4 | opRet0 | opArgF | opArg2F,
	[OP_LTF]	= opImm4 | opRet0 | opArgF | opArg2F,
	[OP_LEF]	= opImm4 | opRet0 | opArgF | opArg2F,
	[OP_GTF]	= opImm4 | opRet0 | opArgF | opArg2F,
	[OP_GEF]	= opImm4 | opRet0 | opArgF | opArg2F,

	[OP_LOAD1]	= opImm0 | opRetI | opArgI,
	[OP_LOAD2]	= opImm0 | opRetI | opArgI,
	[OP_LOAD4]	= opImm0 | opRetIF| opArgI,
	[OP_STORE1]	= opImm0 | opRet0 | opArgI | opArg2I,
	[OP_STORE2]	= opImm0 | opRet0 | opArgI | opArg2I,
	[OP_STORE4]	= opImm0 | opRet0 | opArgIF| opArg2I,
	[OP_ARG]	= opImm1 | opRet0 | opArgIF,
	[OP_BLOCK_COPY]	= opImm4 | opRet0 | opArgI | opArg2I,

	[OP_SEX8]	= opImm0 | opRetI | opArgI,
	[OP_SEX16]	= opImm0 | opRetI | opArgI,
	[OP_NEGI]	= opImm0 | opRetI | opArgI,
	[OP_ADD]	= opImm0 | opRetI | opArgI | opArg2I,
	[OP_SUB]	= opImm0 | opRetI | opArgI | opArg2I,
	[OP_DIVI]	= opImm0 | opRetI | opArgI | opArg2I,
	[OP_DIVU]	= opImm0 | opRetI | opArgI | opArg2I,
	[OP_MODI]	= opImm0 | opRetI | opArgI | opArg2I,
	[OP_MODU]	= opImm0 | opRetI | opArgI | opArg2I,
	[OP_MULI]	= opImm0 | opRetI | opArgI | opArg2I,
	[OP_MULU]	= opImm0 | opRetI | opArgI | opArg2I,
	[OP_BAND]	= opImm0 | opRetI | opArgI | opArg2I,
	[OP_BOR]	= opImm0 | opRetI | opArgI | opArg2I,
	[OP_BXOR]	= opImm0 | opRetI | opArgI | opArg2I,
	[OP_BCOM]	= opImm0 | opRetI | opArgI,
	[OP_LSH]	= opImm0 | opRetI | opArgI | opArg2I,
	[OP_RSHI]	= opImm0 | opRetI | opArgI | opArg2I,
	[OP_RSHU]	= opImm0 | opRetI | opArgI | opArg2I,
	[OP_NEGF]	= opImm0 | opRetF | opArgF,
	[OP_ADDF]	= opImm0 | opRetF | opArgF | opArg2F,
	[OP_SUBF]	= opImm0 | opRetF | opArgF | opArg2F,
	[OP_DIVF]	= opImm0 | opRetF | opArgF | opArg2F,
	[OP_MULF]	= opImm0 | opRetF | opArgF | opArg2F,
	[OP_CVIF]	= opImm0 | opRetF | opArgI,
	[OP_CVFI]	= opImm0 | opRetI | opArgF,
};

// source instruction data
typedef struct source_instruction_s source_instruction_t;
struct source_instruction_s {
	// opcode
	unsigned long int op;

	// number of instruction
	unsigned long int i_count;

	// immediate value (if any)
	union {
		unsigned int i;
		signed int si;
		signed short ss[2];
		unsigned short us[2];
		unsigned char b;
	} arg;

	// required and returned registers
	unsigned char regA1;
	unsigned char regA2;
	unsigned char regR;
	unsigned char regPos;

	// next instruction
	source_instruction_t *next;
};



// read-only data needed by the generated code, placed after the code in its slot
typedef struct VM_Data {
	// length of this struct + data
	size_t dataLength;
	// compiled code size (in bytes)
	// it only is code size, without the data
	size_t codeLength;

	// function pointers, no use to waste registers for them
	long int (* AsmCall)( int, int );
	void (* BlockCopy )( unsigned int, unsigned int, size_t );
	void (* Error )( int );

	// instruction pointers, rarely used so don't waste register
	ppc_instruction_t *iPointers;

	// vm->breakCount, counted by OP_BREAK as in the interpreter
	int *breakCount;

	// data mask for load and store, not used if optimized
	unsigned int dataMask;

	// fixed number used to convert from integer to float
	unsigned int floatBase; // 0x59800004

	// guards: indirect OP_CALL / OP_JUMP need index < instructionCount, OP_ENTER programStack > stackBottom
	unsigned int instructionCount;
	int stackBottom;

#if ELF64
	// official procedure descriptor
	opd_t opd;
#endif

	// additional constants, for floating point OP_CONST
	// this data has dynamic length, thus '0' here
	unsigned int data[0];
} vm_data_t;

#define VM_Data_Offset( field )	offsetof( vm_data_t, field )

// lfs reads the float constants through rVMDATA with a signed 16-bit displacement
#define VM_DATA_MAX	( ( 0x8000 - VM_Data_Offset( data ) ) / sizeof( unsigned int ) )


// functions used by generated code
static long int
VM_AsmCall( int callSyscallInvNum, int callProgramStack )
{
	vm_t *savedVM = currentVM;
	long int i, ret;
#ifdef VM_TIMES
	struct tms start_time, stop_time;
	clock_t saved_time = time_outside_vm;
	times( &start_time );
#endif

	// save the stack to allow recursive VM entry
	currentVM->programStack = callProgramStack - 4;

	// we need to convert ints to longs on 64bit powerpcs
	if ( sizeof( intptr_t ) == sizeof( int ) ) {
		intptr_t *argPosition = (intptr_t *)((byte *)currentVM->dataBase + callProgramStack + 4);

		// generated code does not invert syscall number
		argPosition[ 0 ] = -1 - callSyscallInvNum;

		ret = VM_SYSCALL( currentVM, argPosition );
	} else {
		intptr_t args[MAX_VMSYSCALL_ARGS];

		// generated code does not invert syscall number
		args[0] = -1 - callSyscallInvNum;

		int *argPosition = (int *)((byte *)currentVM->dataBase + callProgramStack + 4);
		for( i = 1; i < ARRAY_LEN(args); i++ )
			args[ i ] = argPosition[ i ];

		ret = VM_SYSCALL( currentVM, args );
	}

	currentVM = savedVM;

#ifdef VM_TIMES
	times( &stop_time );
	time_outside_vm = saved_time + ( stop_time.tms_utime - start_time.tms_utime );
#endif

	return ret;
}

// guard failures, each with its own error stub; the messages are the interpreter's
#define VMJIT_ERR_CALL	0
#define VMJIT_ERR_JUMP	1
#define VMJIT_ERR_STACK	2
#define VMJIT_ERR_COUNT	3

static const char *const vmjit_errmsg[ VMJIT_ERR_COUNT ] = {
	"VM program counter out of range in OP_CALL",
	"VM program counter out of range in OP_JUMP",
	"VM stack overflow",
};

// the self-test catches guard failures instead of dropping the game
static qboolean vmjit_testing;
static int vmjit_testcode;
static jmp_buf vmjit_testjmp;

// A guard's error stub calls this; it never returns into generated code.
static void VM_JitError( int code ) __attribute__((noreturn));
static void
VM_JitError( int code )
{
	if ( vmjit_testing ) {
		vmjit_testcode = code;
		longjmp( vmjit_testjmp, 1 );
	}
	if ( (unsigned)code >= VMJIT_ERR_COUNT )
		Com_Error( ERR_DROP, "VM guard %d", code );
	Com_Error( ERR_DROP, "%s", vmjit_errmsg[ code ] );
}

// flycastPS3 ps3_jit_icache_sync: written code reaches the I-cache through memory, in 128-byte lines
static void
ps3_jit_icache_sync( const void *start, size_t size )
{
#ifdef __PS3__
	const uintptr_t line = 128;
	const unsigned char *a = (const unsigned char *)( (uintptr_t)start & ~( line - 1 ) );
	const unsigned char *e = (const unsigned char *)start + size;
	const unsigned char *q;

	if ( !size )
		return;
	for ( q = a; q < e; q += line )
		__asm__ volatile( "dcbst 0,%0" :: "r"( q ) : "memory" );
	__asm__ volatile( "sync" ::: "memory" );
	for ( q = a; q < e; q += line )
		__asm__ volatile( "icbi 0,%0" :: "r"( q ) : "memory" );
	__asm__ volatile( "isync" ::: "memory" );
#endif
}


// Code slots, one per VM, in a .text array: lv2 runs code only from the loaded image, and
// lets the image be written. Sizes per flavor come from the Makefile; 0 means no JIT.
#ifndef VMJIT_UI_KB
#define VMJIT_UI_KB	0
#endif
#ifndef VMJIT_CGAME_KB
#define VMJIT_CGAME_KB	0
#endif
#ifndef VMJIT_GAME_KB
#define VMJIT_GAME_KB	0
#endif
#define VMJIT_ARENA_SIZE	( ( VMJIT_UI_KB + VMJIT_CGAME_KB + VMJIT_GAME_KB ) * 1024 )

#ifdef __PS3__
#define VMJIT_TEXT	__attribute__((aligned(128), section(".text"), used))
#else
#define VMJIT_TEXT	__attribute__((aligned(128)))
#endif

static unsigned char vmjit_arena[ VMJIT_ARENA_SIZE ? VMJIT_ARENA_SIZE : 128 ] VMJIT_TEXT;

typedef struct {
	const char	*name;
	unsigned char	*base;
	size_t		size;
	qboolean	busy;
} vmjit_slot_t;

static vmjit_slot_t vmjit_slots[] = {
	{ "ui",		vmjit_arena,	VMJIT_UI_KB * 1024 },
	{ "cgame",	vmjit_arena + VMJIT_UI_KB * 1024,	VMJIT_CGAME_KB * 1024 },
	{ "qagame",	vmjit_arena + ( VMJIT_UI_KB + VMJIT_CGAME_KB ) * 1024,	VMJIT_GAME_KB * 1024 },
};

// ps3_vmjit 2 runs generated code once the boot self-test passed; 0 here makes 2 a dry run like 1.
#ifndef VMJIT_CAN_RUN
#define VMJIT_CAN_RUN	1
#endif


// The compile writes straight into the slot; scratch is a few Z_Malloc blocks, not one per op.
static ppc_instruction_t *out_end;	// end of the slot
static ppc_instruction_t *out_now;	// write cursor
static ppc_instruction_t *fn_base;	// first word of the function being compiled

// vm->instructionPointers: code address of each QVM instruction, 0 until it is emitted
static intptr_t *ip_tab;
static unsigned long int ip_count;

// a branch slot: conditional ones get two words, for the inverted bc + b long form
typedef struct {
	ppc_instruction_t *at;	// first word of the slot
	ppc_instruction_t *stub;	// a bl to this error stub, else NULL and dest is used
	unsigned int dest;	// QVM instruction number
	unsigned char bo, bi, ext;
} ppc_jump_t;

// error stubs and the entry stub, in front of the first function: their addresses never move
static ppc_instruction_t *stub_err[ VMJIT_ERR_COUNT ];
static ppc_instruction_t *stub_entry;

// branches of the current function, written when it ends
static ppc_jump_t *fn_jumps;
static long int fn_jumps_num, fn_jumps_max;

// calls and jumps into later functions, written after the last function
static ppc_jump_t *fixups;
static long int fixups_num, fixups_max;

// float constants, read by lfs through rVMDATA
static unsigned int *data_list;
static long int data_acc;

// one source function, src[0] is the dummy head
static source_instruction_t *src;

// output instructions which does not come from source code
// use false i_count value
#define FALSE_ICOUNT 0xffffffff


// Reserves n words for QVM instruction i_count; its first chunk is where jumps to it land.
static ppc_instruction_t *
PPC_Reserve( unsigned long int i_count, size_t n )
{
	ppc_instruction_t *at = out_now;

	if ( i_count != FALSE_ICOUNT && !ip_tab[ i_count ] )
		ip_tab[ i_count ] = (intptr_t)at;

	if ( n > (size_t)( out_end - out_now ) )
		DIE( "code slot is full" );
	out_now += n;
	return at;
}

// append specified instructions at the end of instruction chain
static void
PPC_AppendInstructions(
		unsigned long int i_count,
		size_t num_instructions,
		const ppc_instruction_t *is
	)
{
	ppc_instruction_t *at = PPC_Reserve( i_count, num_instructions );

	if ( num_instructions )
		memcpy( at, is, num_instructions * sizeof( ppc_instruction_t ) );
}

// Reserves a branch slot of 1 word, or 2 for a conditional branch; see PPC_FinishFunction.
static void
PPC_PrepareJump(
		unsigned long int i_count,
		unsigned long int dest,
		long int bo,
		long int bi,
		unsigned long int ext
	)
{
	ppc_jump_t *j;
	size_t i, n = ( bo == branchAlways ) ? 1 : 2;
	ppc_instruction_t nop = IN( iNOP );

	if ( dest >= ip_count )
		DIE( "jump out of range" );
	if ( fn_jumps_num >= fn_jumps_max )
		DIE( "too many jumps" );

	j = &fn_jumps[ fn_jumps_num++ ];
	j->at = PPC_Reserve( i_count, n );
	j->stub = NULL;
	j->dest = dest;
	j->bo = bo;
	j->bi = bi;
	j->ext = ext;

	for ( i = 0; i < n; i++ )
		j->at[ i ] = nop;
}

// Reserves a guard's bl to an error stub. It is a one-word slot like an unconditional jump, so
// PPC_FinishFunction moves it with the code and writes it once the function is compacted.
static void
PPC_PrepareStubCall( unsigned long int i_count, ppc_instruction_t *stub )
{
	ppc_jump_t *j;

	if ( fn_jumps_num >= fn_jumps_max )
		DIE( "too many jumps" );

	j = &fn_jumps[ fn_jumps_num++ ];
	j->at = PPC_Reserve( i_count, 1 );
	j->stub = stub;
	j->dest = 0;
	j->bo = branchAlways;
	j->bi = 0;
	j->ext = branchExtLink;
	j->at[ 0 ] = IN( iNOP );
}

// simplyfy instruction emission
#define emitStart( i_cnt ) \
	unsigned long int i_count = i_cnt; \
	size_t num_instructions = 0; \
	long int force_emit = 0; \
	ppc_instruction_t instructions[50];

#define pushIn( inst ) \
	(instructions[ num_instructions++ ] = inst)
#define in( inst, args... ) pushIn( IN( inst, args ) )

#define emitEnd() \
	do{ \
		if ( num_instructions || force_emit ) \
			PPC_AppendInstructions( i_count, num_instructions, instructions );\
		num_instructions = 0; \
	} while(0)

#define emitJump( dest, bo, bi, ext ) \
	do { \
		emitEnd(); \
		PPC_PrepareJump( i_count, dest, bo, bi, ext ); \
	} while(0)

#define emitStubCall( err ) \
	do { \
		emitEnd(); \
		PPC_PrepareStubCall( i_count, stub_err[ err ] ); \
	} while(0)


// append the data and return its offset
static size_t
PPC_PushData( unsigned int datum )
{
	long int i;

	for ( i = 0; i < data_acc; i++ ) {
		if ( data_list[ i ] == datum )
			return VM_Data_Offset( data ) + i * sizeof( unsigned int );
	}

	if ( data_acc >= VM_DATA_MAX )
		DIE( "too many float constants" );

	data_list[ data_acc ] = datum;
	return VM_Data_Offset( data ) + data_acc++ * sizeof( unsigned int );
}

// find leading zeros in dataMask to implement it with "rotate and mask" instruction
static long int fastMaskHi = 0, fastMaskLo = 31;
static void
PPC_MakeFastMask( int mask )
{
#if defined( __GNUC__ ) && ( __GNUC__ >= 4 || ( __GNUC__ == 3 && __GNUC_MINOR__ >= 4 ) )
	/* count leading zeros */
	fastMaskHi = __builtin_clz( mask );

	/* count trailing zeros */
	fastMaskLo = 31 - __builtin_ctz( mask );
#else
	fastMaskHi = 0;
	while ( ( mask & ( 0x80000000 >> fastMaskHi ) ) == 0 )
		fastMaskHi++;

	fastMaskLo = 31;
	while ( ( mask & ( 0x80000000 >> fastMaskLo ) ) == 0 )
		fastMaskLo--;
#endif
}


// registers which are global for generated code

// pointer to VM_Data (constant)
#define rVMDATA r14
// vm->dataBase (constant)
#define rDATABASE r15
// programStack (variable)
#define rPSTACK r16

// Function locals live in volatile registers; caller-safe ones are used only when those run
// out or a value must survive a call.
static const long int gpr_list[] = {
	/* caller safe registers, normally only one is used */
	r24, r23, r22, r21,
	r20, r19, r18, r17,
	/* volatile registers (preferred),
	 * normally no more than 5 is used */
	r3, r4, r5, r6,
	r7, r8, r9, r10,
};
static const long int gpr_vstart = 8; /* position of first volatile register */
static const long int gpr_total = ARRAY_LEN( gpr_list );

static const long int fpr_list[] = {
	/* static registers, normally none is used */
	f20, f21, f19, f18,
	f17, f16, f15, f14,
	/* volatile registers (preferred),
	 * normally no more than 7 is used */
	f0, f1, f2, f3,
	f4, f5, f6, f7,
	f8, f9, f10, f11,
	f12, f13,
};
static const long int fpr_vstart = 8;
static const long int fpr_total = ARRAY_LEN( fpr_list );

// Error stubs (li r3,code, then a shared tail to VM_JitError, which never returns), then the entry
// stub int begin( data, programStack, dataBase ): loads our globals, calls the function right after it.
static void
PPC_CompileInit( void )
{
	/* first instruction must not be placed on instruction list */
	emitStart( FALSE_ICOUNT );

	long int stack = STACK_SAVE + 4 * GPRLEN;
	ppc_instruction_t *base = out_now;
	long int e;

	for ( e = 0; e < VMJIT_ERR_COUNT; e++ ) {
		stub_err[ e ] = base + num_instructions;
		in( iLI, r3, e );
		if ( e < VMJIT_ERR_COUNT - 1 )
			in( iB, +4*2*( VMJIT_ERR_COUNT - 1 - e ) ); // to the tail | XXX jump !
	}
	in( iLL, r0, VM_Data_Offset( Error ), rVMDATA );
	in( iMTCTR, r0 );
	in( iBCTRL );
	emitEnd();

	stub_entry = out_now;
	in( iMFLR, r0 );
	in( iSTLU, r1, -stack, r1 );
	in( iSTL, rVMDATA, STACK_SAVE + 0 * GPRLEN, r1 );
	in( iSTL, rPSTACK, STACK_SAVE + 1 * GPRLEN, r1 );
	in( iSTL, rDATABASE, STACK_SAVE + 2 * GPRLEN, r1 );
	in( iSTL, r0, stack + STACK_LR, r1 );
	in( iMR, rVMDATA, r3 );
	in( iMR, rPSTACK, r4 );
	in( iMR, rDATABASE, r5 );
	in( iBL, +4*9 ); // LINK JUMP: first generated instruction | XXX jump !
	pushIn( PPC_EXTSW( r3, r3 ) ); // ELFv1: an int result goes to C sign-extended, lwz zero-extends
	in( iLL, rVMDATA, STACK_SAVE + 0 * GPRLEN, r1 );
	in( iLL, rPSTACK, STACK_SAVE + 1 * GPRLEN, r1 );
	in( iLL, rDATABASE, STACK_SAVE + 2 * GPRLEN, r1 );
	in( iLL, r0, stack + STACK_LR, r1 );
	in( iMTLR, r0 );
	in( iADDI, r1, r1, stack );
	in( iBLR );

	emitEnd();
}

// rFIRST is the copy of the top value on the opstack
#define rFIRST		(gpr_list[ gpr_pos - 1])
// second value on the opstack
#define rSECOND		(gpr_list[ gpr_pos - 2 ])
// temporary registers, not on the opstack
#define rTEMP(x)	(gpr_list[ gpr_pos + x ])
#define rTMP		rTEMP(0)

#define fFIRST		(fpr_list[ fpr_pos - 1 ])
#define fSECOND		(fpr_list[ fpr_pos - 2 ])
#define fTEMP(x)	(fpr_list[ fpr_pos + x ])
#define fTMP		fTEMP(0)

// register types
#define rTYPE_STATIC	0x01
#define rTYPE_FLOAT	0x02

// what type should this opcode return
#define RET_INT		( !(i_now->regR & rTYPE_FLOAT) )
#define RET_FLOAT	( i_now->regR & rTYPE_FLOAT )
// what type should it accept
#define ARG_INT		( ! i_now->regA1 )
#define ARG_FLOAT	( i_now->regA1 )
#define ARG2_INT	( ! i_now->regA2 )
#define ARG2_FLOAT	( i_now->regA2 )

// emit OP_CONST, called if nothing has used the const value directly
static void
PPC_EmitConst( source_instruction_t * const i_const )
{
	emitStart( i_const->i_count );

	if ( !(i_const->regR & rTYPE_FLOAT) ) {
		// gpr_pos needed for "rFIRST" to work
		long int gpr_pos = i_const->regPos;

		if ( i_const->arg.si >= -0x8000 && i_const->arg.si < 0x8000 ) {
			in( iLI, rFIRST, i_const->arg.si );
		} else if ( i_const->arg.i < 0x10000 ) {
			in( iLI, rFIRST, 0 );
			in( iORI, rFIRST, rFIRST, i_const->arg.i );
		} else {
			in( iLIS, rFIRST, i_const->arg.ss[ 0 ] );
			if ( i_const->arg.us[ 1 ] != 0 )
				in( iORI, rFIRST, rFIRST, i_const->arg.us[ 1 ] );
		}

	} else {
		// fpr_pos needed for "fFIRST" to work
		long int fpr_pos = i_const->regPos;

		// there's no good way to generate the data,
		// just read it from data section
		in( iLFS, fFIRST, PPC_PushData( i_const->arg.i ), rVMDATA );
	}

	emitEnd();
}
#define MAYBE_EMIT_CONST() if ( i_const ) PPC_EmitConst( i_const )

// emit empty instruction, just sets the needed pointers
static inline void
PPC_EmitNull( source_instruction_t * const i_null )
{
	PPC_AppendInstructions( i_null->i_count, 0, NULL );
}
#define EMIT_FALSE_CONST() PPC_EmitNull( i_const )


// Upstream asserted these; NDEBUG is never set here, so a failure would abort the game.
#define VALUE_PROVIDERS	20
#define CHECK( cond, reason )	do { if ( !( cond ) ) DIE( reason ); } while ( 0 )

// analize function for register usage and whether it needs stack (r1) prepared
static void
VM_AnalyzeFunction(
		source_instruction_t * const i_first,
		long int *prepareStack,
		long int *gpr_start_pos,
		long int *fpr_start_pos
		)
{
	source_instruction_t *i_now = i_first;

	source_instruction_t *value_provider[VALUE_PROVIDERS] = { NULL };
	unsigned long int opstack_depth = 0;

	// first step: remember which codes returned a value, and mark the value type once
	// we get to know what it should be
	while ( (i_now = i_now->next) ) {
		unsigned long int op = i_now->op;
		unsigned long int opi = vm_opInfo[ op ];

		if ( opi & opArgIF ) {
			CHECK( opstack_depth > 0, "opstack underflow" );

			opstack_depth--;
			source_instruction_t *vp = value_provider[ opstack_depth ];
			unsigned long int vpopi = vm_opInfo[ vp->op ];

			if ( (opi & opArgI) && (vpopi & opRetI) ) {
				// instruction accepts integer, provider returns integer: nothing to mark
			} else if ( (opi & opArgF) && (vpopi & opRetF) ) {
				// instruction accepts float, provider returns float
				vp->regR |= rTYPE_FLOAT; // use OR here - could be marked as static
				i_now->regA1 = rTYPE_FLOAT;
			} else {
				// instruction arg type does not agree with
				// provider return type
				DIE( "unrecognized instruction combination" );
			}

		}
		if ( opi & opArg2IF ) {
			CHECK( opstack_depth > 0, "opstack underflow" );

			opstack_depth--;
			source_instruction_t *vp = value_provider[ opstack_depth ];
			unsigned long int vpopi = vm_opInfo[ vp->op ];

			if ( (opi & opArg2I) && (vpopi & opRetI) ) {
				// instruction accepts integer, provider returns integer: nothing to mark
			} else if ( (opi & opArg2F) && (vpopi & opRetF) ) {
				// instruction accepts float, provider returns float
				vp->regR |= rTYPE_FLOAT; // use OR here - could be marked as static
				i_now->regA2 = rTYPE_FLOAT;
			} else {
				// instruction arg type does not agree with
				// provider return type
				DIE( "unrecognized instruction combination" );
			}
		}


		if (
			( op == OP_CALL )
				||
			( op == OP_BLOCK_COPY && ( i_now->arg.i > SL( 16, 32 ) || !OPTIMIZE_COPY ) )
		) {
			long int i;
			*prepareStack = 1;
			// force caller safe registers so we won't have to save them
			for ( i = 0; i < opstack_depth; i++ ) {
				source_instruction_t *vp = value_provider[ i ];
				vp->regR |= rTYPE_STATIC;
			}
		}


		if ( opi & opRetIF ) {
			CHECK( opstack_depth < VALUE_PROVIDERS, "opstack too deep" );
			value_provider[ opstack_depth ] = i_now;
			opstack_depth++;
		}
	}

	// second step: now that we know the register types, compute exactly how many
	// registers of each type we need
	i_now = i_first;
	long int needed_reg[4] = {0,0,0,0}, max_reg[4] = {0,0,0,0};
	opstack_depth = 0;
	while ( (i_now = i_now->next) ) {
		unsigned long int op = i_now->op;
		unsigned long int opi = vm_opInfo[ op ];

		if ( opi & opArgIF ) {
			CHECK( opstack_depth > 0, "opstack underflow" );
			opstack_depth--;
			source_instruction_t *vp = value_provider[ opstack_depth ];

			needed_reg[ ( vp->regR & 2 ) ] -= 1;
			if ( vp->regR & 1 ) // static
				needed_reg[ ( vp->regR & 3 ) ] -= 1;
		}
		if ( opi & opArg2IF ) {
			CHECK( opstack_depth > 0, "opstack underflow" );
			opstack_depth--;
			source_instruction_t *vp = value_provider[ opstack_depth ];

			needed_reg[ ( vp->regR & 2 ) ] -= 1;
			if ( vp->regR & 1 ) // static
				needed_reg[ ( vp->regR & 3 ) ] -= 1;
		}

		if ( opi & opRetIF ) {
			long int i;
			value_provider[ opstack_depth ] = i_now;
			opstack_depth++;

			i = i_now->regR & 2;
			needed_reg[ i ] += 1;
			if ( max_reg[ i ] < needed_reg[ i ] )
				max_reg[ i ] = needed_reg[ i ];

			i = i_now->regR & 3;
			if ( i & 1 ) {
				needed_reg[ i ] += 1;
				if ( max_reg[ i ] < needed_reg[ i ] )
					max_reg[ i ] = needed_reg[ i ];
			}
		}
	}

	long int gpr_start = gpr_vstart;
	const long int gpr_volatile = gpr_total - gpr_vstart;
	if ( max_reg[ 1 ] > 0 || max_reg[ 0 ] > gpr_volatile ) {
		// max_reg[ 0 ] - all gprs needed
		// max_reg[ 1 ] - static gprs needed
		long int max = max_reg[ 0 ] - gpr_volatile;
		if ( max_reg[ 1 ] > max )
			max = max_reg[ 1 ];
		if ( max > gpr_vstart ) {
			/* error */
			DIE( "Need more GPRs" );
		}

		gpr_start -= max;

		// need stack to save caller safe registers
		*prepareStack = 1;
	}
	*gpr_start_pos = gpr_start;

	long int fpr_start = fpr_vstart;
	const long int fpr_volatile = fpr_total - fpr_vstart;
	if ( max_reg[ 3 ] > 0 || max_reg[ 2 ] > fpr_volatile ) {
		// max_reg[ 2 ] - all fprs needed
		// max_reg[ 3 ] - static fprs needed
		long int max = max_reg[ 2 ] - fpr_volatile;
		if ( max_reg[ 3 ] > max )
			max = max_reg[ 3 ];
		if ( max > fpr_vstart ) {
			/* error */
			DIE( "Need more FPRs" );
		}

		fpr_start -= max;

		// need stack to save caller safe registers
		*prepareStack = 1;
	}
	*fpr_start_pos = fpr_start;
}

// translate opcodes to ppc instructions, one function at a time
static void
VM_CompileFunction( source_instruction_t * const i_first )
{
	long int prepareStack = 0;
	long int gpr_start_pos, fpr_start_pos;

	VM_AnalyzeFunction( i_first, &prepareStack, &gpr_start_pos, &fpr_start_pos );

	long int gpr_pos = gpr_start_pos, fpr_pos = fpr_start_pos;

	// OP_CONST combines well with many opcodes so we treat it in a special way
	source_instruction_t *i_const = NULL;
	source_instruction_t *i_now = i_first;

	// how big the stack has to be
	long int save_space = STACK_SAVE;
	{
		if ( gpr_start_pos < gpr_vstart )
			save_space += (gpr_vstart - gpr_start_pos) * GPRLEN;
		save_space = ( save_space + 15 ) & ~0x0f;

		if ( fpr_start_pos < fpr_vstart )
			save_space += (fpr_vstart - fpr_start_pos) * FPRLEN;
		save_space = ( save_space + 15 ) & ~0x0f;
	}

	long int stack_temp = prepareStack ? STACK_TEMP : STACK_RTEMP;

	while ( (i_now = i_now->next) ) {
		emitStart( i_now->i_count );

		switch ( i_now->op )
		{
			default:
			case OP_UNDEF:
			case OP_IGNORE:
				MAYBE_EMIT_CONST();
				in( iNOP );
				break;

			case OP_BREAK:
				MAYBE_EMIT_CONST();
				// count it like the interpreter; r0 can't be addi's base, so r11/r12 (not in gpr_list)
				in( iLL, r11, VM_Data_Offset( breakCount ), rVMDATA );
				in( iLWZ, r12, 0, r11 );
				in( iADDI, r12, r12, 1 );
				in( iSTW, r12, 0, r11 );
				break;

			case OP_ENTER:
				if ( i_const )
					DIE( "Weird opcode order" );

				// don't prepare stack if not needed
				if ( prepareStack ) {
					long int i, save_pos = STACK_SAVE;

					in( iMFLR, r0 );
					in( iSTLU, r1, -save_space, r1 );
					in( iSTL, r0, save_space + STACK_LR, r1 );

					/* save registers */
					for ( i = gpr_start_pos; i < gpr_vstart; i++ ) {
						in( iSTL, gpr_list[ i ], save_pos, r1 );
						save_pos += GPRLEN;
					}
					save_pos = ( save_pos + 15 ) & ~0x0f;

					for ( i = fpr_start_pos; i < fpr_vstart; i++ ) {
						in( iSTFD, fpr_list[ i ], save_pos, r1 );
						save_pos += FPRLEN;
					}
					prepareStack = 2;
				}

				in( iADDI, rPSTACK, rPSTACK, - i_now->arg.si );
				// guard: the interpreter's DEBUG_VM stack check, which also caps recursion on r1
				in( iLWZ, r0, VM_Data_Offset( stackBottom ), rVMDATA );
				in( iCMPW, cr7, rPSTACK, r0 );
				in( iBC, branchTrue, 4*cr7+gt, +4*2 ); // XXX jump !
				emitStubCall( VMJIT_ERR_STACK );
				break;

			case OP_LEAVE:
				if ( i_const ) {
					EMIT_FALSE_CONST();

					if ( i_const->regR & rTYPE_FLOAT)
						DIE( "constant float in OP_LEAVE" );

					if ( i_const->arg.si >= -0x8000 && i_const->arg.si < 0x8000 ) {
						in( iLI, r3, i_const->arg.si );
					} else if ( i_const->arg.i < 0x10000 ) {
						in( iLI, r3, 0 );
						in( iORI, r3, r3, i_const->arg.i );
					} else {
						in( iLIS, r3, i_const->arg.ss[ 0 ] );
						if ( i_const->arg.us[ 1 ] != 0 )
							in( iORI, r3, r3, i_const->arg.us[ 1 ] );
					}
					gpr_pos--;
				} else {
					MAYBE_EMIT_CONST();

					/* place return value in r3 */
					if ( ARG_INT ) {
						if ( rFIRST != r3 )
							in( iMR, r3, rFIRST );
						gpr_pos--;
					} else {
						in( iSTFS, fFIRST, stack_temp, r1 );
						in( iLWZ, r3, stack_temp, r1 );
						fpr_pos--;
					}
				}

				// don't undo stack if not prepared
				if ( prepareStack >= 2 ) {
					long int i, save_pos = STACK_SAVE;

					in( iLL, r0, save_space + STACK_LR, r1 );


					/* restore registers */
					for ( i = gpr_start_pos; i < gpr_vstart; i++ ) {
						in( iLL, gpr_list[ i ], save_pos, r1 );
						save_pos += GPRLEN;
					}
					save_pos = ( save_pos + 15 ) & ~0x0f;
					for ( i = fpr_start_pos; i < fpr_vstart; i++ ) {
						in( iLFD, fpr_list[ i ], save_pos, r1 );
						save_pos += FPRLEN;
					}

					in( iMTLR, r0 );
					in( iADDI, r1, r1, save_space );
				}
				in( iADDI, rPSTACK, rPSTACK, i_now->arg.si);
				in( iBLR );
				CHECK( gpr_pos == gpr_start_pos, "opstack not empty at OP_LEAVE" );
				CHECK( fpr_pos == fpr_start_pos, "opstack not empty at OP_LEAVE" );
				break;

			case OP_CALL:
				if ( i_const ) {
					EMIT_FALSE_CONST();

					if ( i_const->arg.si >= 0 ) {
						emitJump(
							i_const->arg.i,
							branchAlways, 0, branchExtLink
						);
					} else {
						/* syscall */
						in( iLL, r0, VM_Data_Offset( AsmCall ), rVMDATA );

						in( iLI, r3, i_const->arg.si ); // negative value
						in( iMR, r4, rPSTACK ); // push PSTACK on argument list

						in( iMTCTR, r0 );
						in( iBCTRL );
					}
					if ( rFIRST != r3 )
						in( iMR, rFIRST, r3 );
				} else {
					MAYBE_EMIT_CONST();

					in( iCMPWI, cr7, rFIRST, 0 );
					in( iBLTm, cr7, +4*9 /* syscall */ ); // XXX jump !
					/* instruction call, guarded like the interpreter: index < instructionCount */
					in( iLWZ, r0, VM_Data_Offset( instructionCount ), rVMDATA );
					in( iCMPLW, cr7, rFIRST, r0 );
					in( iBC, branchTrue, 4*cr7+lt, +4*2 ); // XXX jump !
					emitStubCall( VMJIT_ERR_CALL );

					// get instruction address
					in( iLL, r0, VM_Data_Offset( iPointers ), rVMDATA );
					in( iRLWINM, rFIRST, rFIRST, GPRLEN_SHIFT, 0, 31-GPRLEN_SHIFT ); // mul * GPRLEN
					in( iLLX, r0, rFIRST, r0 ); // load pointer

					in( iB, +4*4 ); // XXX jump !

					/* syscall */
					in( iLL, r0, VM_Data_Offset( AsmCall ), rVMDATA ); // get asmCall pointer
					/* rFIRST can be r3 or some static register; a loaded value is zero-extended */
					pushIn( PPC_EXTSW( r3, rFIRST ) ); // push OPSTACK top value on argument list
					in( iMR, r4, rPSTACK ); // push PSTACK on argument list

					/* common code */
					in( iMTCTR, r0 );
					in( iBCTRL );

					if ( rFIRST != r3 )
						in( iMR, rFIRST, r3 ); // push return value on the top of the opstack
				}
				break;

			case OP_PUSH:
				MAYBE_EMIT_CONST();
				if ( RET_INT )
					gpr_pos++;
				else
					fpr_pos++;
				/* no instructions here */
				force_emit = 1;
				break;

			case OP_POP:
				MAYBE_EMIT_CONST();
				if ( ARG_INT )
					gpr_pos--;
				else
					fpr_pos--;
				/* no instructions here */
				force_emit = 1;
				break;

			case OP_CONST:
				MAYBE_EMIT_CONST();
				/* nothing here */
				break;

			case OP_LOCAL:
				MAYBE_EMIT_CONST();
				{
					signed long int hi, lo;
					hi = i_now->arg.ss[ 0 ];
					lo = i_now->arg.ss[ 1 ];
					if ( lo < 0 )
						hi += 1;

					gpr_pos++;
					if ( hi == 0 ) {
						in( iADDI, rFIRST, rPSTACK, lo );
					} else {
						in( iADDIS, rFIRST, rPSTACK, hi );
						if ( lo != 0 )
							in( iADDI, rFIRST, rFIRST, lo );
					}
				}
				break;

			case OP_JUMP:
				if ( i_const ) {
					EMIT_FALSE_CONST();

					emitJump(
						i_const->arg.i,
						branchAlways, 0, 0
					);
				} else {
					MAYBE_EMIT_CONST();

					in( iLWZ, r0, VM_Data_Offset( instructionCount ), rVMDATA );
					in( iCMPLW, cr7, rFIRST, r0 );
					in( iBC, branchTrue, 4*cr7+lt, +4*2 ); // XXX jump !
					emitStubCall( VMJIT_ERR_JUMP );

					in( iLL, r0, VM_Data_Offset( iPointers ), rVMDATA );
					in( iRLWINM, rFIRST, rFIRST, GPRLEN_SHIFT, 0, 31-GPRLEN_SHIFT ); // mul * GPRLEN
					in( iLLX, r0, rFIRST, r0 ); // load pointer
					in( iMTCTR, r0 );
					in( iBCTR );
				}
				gpr_pos--;
				break;

			case OP_EQ:
			case OP_NE:
				if ( i_const && i_const->arg.si >= -0x8000 && i_const->arg.si < 0x10000 ) {
					EMIT_FALSE_CONST();
					if ( i_const->arg.si >= 0x8000 )
						in( iCMPLWI, cr7, rSECOND, i_const->arg.i );
					else
						in( iCMPWI, cr7, rSECOND, i_const->arg.si );
				} else {
					MAYBE_EMIT_CONST();
					in( iCMPW, cr7, rSECOND, rFIRST );
				}
				emitJump(
					i_now->arg.i,
					(i_now->op == OP_EQ ? branchTrue : branchFalse),
					4*cr7+eq, 0
				);
				gpr_pos -= 2;
				break;

			case OP_LTI:
			case OP_GEI:
				if ( i_const && i_const->arg.si >= -0x8000 && i_const->arg.si < 0x8000 ) {
					EMIT_FALSE_CONST();
					in( iCMPWI, cr7, rSECOND, i_const->arg.si );
				} else {
					MAYBE_EMIT_CONST();
					in( iCMPW, cr7, rSECOND, rFIRST );
				}
				emitJump(
					i_now->arg.i,
					( i_now->op == OP_LTI ? branchTrue : branchFalse ),
					4*cr7+lt, 0
				);
				gpr_pos -= 2;
				break;

			case OP_GTI:
			case OP_LEI:
				if ( i_const && i_const->arg.si >= -0x8000 && i_const->arg.si < 0x8000 ) {
					EMIT_FALSE_CONST();
					in( iCMPWI, cr7, rSECOND, i_const->arg.si );
				} else {
					MAYBE_EMIT_CONST();
					in( iCMPW, cr7, rSECOND, rFIRST );
				}
				emitJump(
					i_now->arg.i,
					( i_now->op == OP_GTI ? branchTrue : branchFalse ),
					4*cr7+gt, 0
				);
				gpr_pos -= 2;
				break;

			case OP_LTU:
			case OP_GEU:
				if ( i_const && i_const->arg.i < 0x10000 ) {
					EMIT_FALSE_CONST();
					in( iCMPLWI, cr7, rSECOND, i_const->arg.i );
				} else {
					MAYBE_EMIT_CONST();
					in( iCMPLW, cr7, rSECOND, rFIRST );
				}
				emitJump(
					i_now->arg.i,
					( i_now->op == OP_LTU ? branchTrue : branchFalse ),
					4*cr7+lt, 0
				);
				gpr_pos -= 2;
				break;

			case OP_GTU:
			case OP_LEU:
				if ( i_const && i_const->arg.i < 0x10000 ) {
					EMIT_FALSE_CONST();
					in( iCMPLWI, cr7, rSECOND, i_const->arg.i );
				} else {
					MAYBE_EMIT_CONST();
					in( iCMPLW, cr7, rSECOND, rFIRST );
				}
				emitJump(
					i_now->arg.i,
					( i_now->op == OP_GTU ? branchTrue : branchFalse ),
					4*cr7+gt, 0
				);
				gpr_pos -= 2;
				break;

			case OP_EQF:
			case OP_NEF:
				MAYBE_EMIT_CONST();
				in( iFCMPU, cr7, fSECOND, fFIRST );
				emitJump(
					i_now->arg.i,
					( i_now->op == OP_EQF ? branchTrue : branchFalse ),
					4*cr7+eq, 0
				);
				fpr_pos -= 2;
				break;

			case OP_LTF:
			case OP_GEF:
				MAYBE_EMIT_CONST();
				in( iFCMPU, cr7, fSECOND, fFIRST );
				emitJump(
					i_now->arg.i,
					( i_now->op == OP_LTF ? branchTrue : branchFalse ),
					4*cr7+lt, 0
				);
				fpr_pos -= 2;
				break;

			case OP_GTF:
			case OP_LEF:
				MAYBE_EMIT_CONST();
				in( iFCMPU, cr7, fSECOND, fFIRST );
				emitJump(
					i_now->arg.i,
					( i_now->op == OP_GTF ? branchTrue : branchFalse ),
					4*cr7+gt, 0
				);
				fpr_pos -= 2;
				break;

			case OP_LOAD1:
				MAYBE_EMIT_CONST();
#if OPTIMIZE_MASK
				in( iRLWINM, rFIRST, rFIRST, 0, fastMaskHi, fastMaskLo );
#else
				in( iLWZ, r0, VM_Data_Offset( dataMask ), rVMDATA );
				in( iAND, rFIRST, rFIRST, r0 );
#endif
				in( iLBZX, rFIRST, rFIRST, rDATABASE );
				break;

			case OP_LOAD2:
				MAYBE_EMIT_CONST();
#if OPTIMIZE_MASK
				in( iRLWINM, rFIRST, rFIRST, 0, fastMaskHi, fastMaskLo );
#else
				in( iLWZ, r0, VM_Data_Offset( dataMask ), rVMDATA );
				in( iAND, rFIRST, rFIRST, r0 );
#endif
				in( iLHZX, rFIRST, rFIRST, rDATABASE );
				break;

			case OP_LOAD4:
				MAYBE_EMIT_CONST();
#if OPTIMIZE_MASK
				in( iRLWINM, rFIRST, rFIRST, 0, fastMaskHi, fastMaskLo );
#else
				in( iLWZ, r0, VM_Data_Offset( dataMask ), rVMDATA );
				in( iAND, rFIRST, rFIRST, r0 );
#endif
				if ( RET_INT ) {
					in( iLWZX, rFIRST, rFIRST, rDATABASE );
				} else {
					fpr_pos++;
					in( iLFSX, fFIRST, rFIRST, rDATABASE );
					gpr_pos--;
				}
				break;

			case OP_STORE1:
				MAYBE_EMIT_CONST();
#if OPTIMIZE_MASK
				in( iRLWINM, rSECOND, rSECOND, 0, fastMaskHi, fastMaskLo );
#else
				in( iLWZ, r0, VM_Data_Offset( dataMask ), rVMDATA );
				in( iAND, rSECOND, rSECOND, r0 );
#endif
				in( iSTBX, rFIRST, rSECOND, rDATABASE );
				gpr_pos -= 2;
				break;

			case OP_STORE2:
				MAYBE_EMIT_CONST();
#if OPTIMIZE_MASK
				in( iRLWINM, rSECOND, rSECOND, 0, fastMaskHi, fastMaskLo );
#else
				in( iLWZ, r0, VM_Data_Offset( dataMask ), rVMDATA );
				in( iAND, rSECOND, rSECOND, r0 );
#endif
				in( iSTHX, rFIRST, rSECOND, rDATABASE );
				gpr_pos -= 2;
				break;

			case OP_STORE4:
				MAYBE_EMIT_CONST();
				if ( ARG_INT ) {
#if OPTIMIZE_MASK
					in( iRLWINM, rSECOND, rSECOND, 0, fastMaskHi, fastMaskLo );
#else
					in( iLWZ, r0, VM_Data_Offset( dataMask ), rVMDATA );
					in( iAND, rSECOND, rSECOND, r0 );
#endif

					in( iSTWX, rFIRST, rSECOND, rDATABASE );
					gpr_pos--;
				} else {
#if OPTIMIZE_MASK
					in( iRLWINM, rFIRST, rFIRST, 0, fastMaskHi, fastMaskLo );
#else
					in( iLWZ, r0, VM_Data_Offset( dataMask ), rVMDATA );
					in( iAND, rFIRST, rFIRST, r0 );
#endif

					in( iSTFSX, fFIRST, rFIRST, rDATABASE );
					fpr_pos--;
				}
				gpr_pos--;
				break;

			case OP_ARG:
				MAYBE_EMIT_CONST();
				in( iADDI, r0, rPSTACK, i_now->arg.b );
				if ( ARG_INT ) {
					in( iSTWX, rFIRST, rDATABASE, r0 );
					gpr_pos--;
				} else {
					in( iSTFSX, fFIRST, rDATABASE, r0 );
					fpr_pos--;
				}
				break;

			case OP_BLOCK_COPY:
				MAYBE_EMIT_CONST();
#if OPTIMIZE_COPY
				if ( i_now->arg.i <= SL( 16, 32 ) ) {
					/* block is very short so copy it in-place */

					unsigned int len = i_now->arg.i;
					unsigned int copied = 0, left = len;

					in( iADD, rFIRST, rFIRST, rDATABASE );
					in( iADD, rSECOND, rSECOND, rDATABASE );

					if ( len >= GPRLEN ) {
						long int i, words = len / GPRLEN;
						in( iLL, r0, 0, rFIRST );
						for ( i = 1; i < words; i++ )
							in( iLL, rTEMP( i - 1 ), GPRLEN * i, rFIRST );

						in( iSTL, r0, 0, rSECOND );
						for ( i = 1; i < words; i++ )
							in( iSTL, rTEMP( i - 1 ), GPRLEN * i, rSECOND );

						copied += words * GPRLEN;
						left -= copied;
					}

					if ( SL( 0, left >= 4 ) ) {
						in( iLWZ, r0, copied+0, rFIRST );
						in( iSTW, r0, copied+0, rSECOND );
						copied += 4;
						left -= 4;
					}
					if ( left >= 4 ) {
						DIE("Bug in OP_BLOCK_COPY");
					}
					if ( left == 3 ) {
						in( iLHZ, r0,	copied+0, rFIRST );
						in( iLBZ, rTMP,	copied+2, rFIRST );
						in( iSTH, r0,	copied+0, rSECOND );
						in( iSTB, rTMP,	copied+2, rSECOND );
					} else if ( left == 2 ) {
						in( iLHZ, r0, copied+0, rFIRST );
						in( iSTH, r0, copied+0, rSECOND );
					} else if ( left == 1 ) {
						in( iLBZ, r0, copied+0, rFIRST );
						in( iSTB, r0, copied+0, rSECOND );
					}
				} else
#endif
				{
					unsigned long int r5_ori = 0;
					if ( i_now->arg.si >= -0x8000 && i_now->arg.si < 0x8000 ) {
						in( iLI, r5, i_now->arg.si );
					} else if ( i_now->arg.i < 0x10000 ) {
						in( iLI, r5, 0 );
						r5_ori = i_now->arg.i;
					} else {
						in( iLIS, r5, i_now->arg.ss[ 0 ] );
						r5_ori = i_now->arg.us[ 1 ];
					}

					in( iLL, r0, VM_Data_Offset( BlockCopy ), rVMDATA ); // get blockCopy pointer

					if ( r5_ori )
						in( iORI, r5, r5, r5_ori );

					in( iMTCTR, r0 );

					// ELFv1 wants unsigned int args zero-extended, and an add can carry into the high word
					pushIn( PPC_CLRLDI32( r4, rFIRST ) );
					pushIn( PPC_CLRLDI32( r3, rSECOND ) );

					in( iBCTRL );
				}

				gpr_pos -= 2;
				break;

			case OP_SEX8:
				MAYBE_EMIT_CONST();
				in( iEXTSB, rFIRST, rFIRST );
				break;

			case OP_SEX16:
				MAYBE_EMIT_CONST();
				in( iEXTSH, rFIRST, rFIRST );
				break;

			case OP_NEGI:
				MAYBE_EMIT_CONST();
				in( iNEG, rFIRST, rFIRST );
				break;

			case OP_ADD:
				if ( i_const ) {
					EMIT_FALSE_CONST();

					signed short int hi, lo;
					hi = i_const->arg.ss[ 0 ];
					lo = i_const->arg.ss[ 1 ];
					if ( lo < 0 )
						hi += 1;

					if ( hi != 0 )
						in( iADDIS, rSECOND, rSECOND, hi );
					if ( lo != 0 )
						in( iADDI, rSECOND, rSECOND, lo );

					// if both are zero no instruction will be written
					if ( hi == 0 && lo == 0 )
						force_emit = 1;
				} else {
					MAYBE_EMIT_CONST();
					in( iADD, rSECOND, rSECOND, rFIRST );
				}
				gpr_pos--;
				break;

			case OP_SUB:
				MAYBE_EMIT_CONST();
				in( iSUB, rSECOND, rSECOND, rFIRST );
				gpr_pos--;
				break;

			case OP_DIVI:
				MAYBE_EMIT_CONST();
				in( iDIVW, rSECOND, rSECOND, rFIRST );
				gpr_pos--;
				break;

			case OP_DIVU:
				MAYBE_EMIT_CONST();
				in( iDIVWU, rSECOND, rSECOND, rFIRST );
				gpr_pos--;
				break;

			case OP_MODI:
				MAYBE_EMIT_CONST();
				in( iDIVW, r0, rSECOND, rFIRST );
				in( iMULLW, r0, r0, rFIRST );
				in( iSUB, rSECOND, rSECOND, r0 );
				gpr_pos--;
				break;

			case OP_MODU:
				MAYBE_EMIT_CONST();
				in( iDIVWU, r0, rSECOND, rFIRST );
				in( iMULLW, r0, r0, rFIRST );
				in( iSUB, rSECOND, rSECOND, r0 );
				gpr_pos--;
				break;

			case OP_MULI:
			case OP_MULU:
				MAYBE_EMIT_CONST();
				in( iMULLW, rSECOND, rSECOND, rFIRST );
				gpr_pos--;
				break;

			case OP_BAND:
				MAYBE_EMIT_CONST();
				in( iAND, rSECOND, rSECOND, rFIRST );
				gpr_pos--;
				break;

			case OP_BOR:
				MAYBE_EMIT_CONST();
				in( iOR, rSECOND, rSECOND, rFIRST );
				gpr_pos--;
				break;

			case OP_BXOR:
				MAYBE_EMIT_CONST();
				in( iXOR, rSECOND, rSECOND, rFIRST );
				gpr_pos--;
				break;

			case OP_BCOM:
				MAYBE_EMIT_CONST();
				in( iNOT, rFIRST, rFIRST );
				break;

			case OP_LSH:
				MAYBE_EMIT_CONST();
				in( iSLW, rSECOND, rSECOND, rFIRST );
				gpr_pos--;
				break;

			case OP_RSHI:
				MAYBE_EMIT_CONST();
				in( iSRAW, rSECOND, rSECOND, rFIRST );
				gpr_pos--;
				break;

			case OP_RSHU:
				MAYBE_EMIT_CONST();
				in( iSRW, rSECOND, rSECOND, rFIRST );
				gpr_pos--;
				break;

			case OP_NEGF:
				MAYBE_EMIT_CONST();
				in( iFNEG, fFIRST, fFIRST );
				break;

			case OP_ADDF:
				MAYBE_EMIT_CONST();
				in( iFADDS, fSECOND, fSECOND, fFIRST );
				fpr_pos--;
				break;

			case OP_SUBF:
				MAYBE_EMIT_CONST();
				in( iFSUBS, fSECOND, fSECOND, fFIRST );
				fpr_pos--;
				break;

			case OP_DIVF:
				MAYBE_EMIT_CONST();
				in( iFDIVS, fSECOND, fSECOND, fFIRST );
				fpr_pos--;
				break;

			case OP_MULF:
				MAYBE_EMIT_CONST();
				in( iFMULS, fSECOND, fSECOND, fFIRST );
				fpr_pos--;
				break;

			case OP_CVIF:
				MAYBE_EMIT_CONST();
				fpr_pos++;
				in( iXORIS, rFIRST, rFIRST, 0x8000 );
				in( iLIS, r0, 0x4330 );
				in( iSTW, rFIRST, stack_temp + 4, r1 );
				in( iSTW, r0, stack_temp, r1 );
				in( iLFS, fTMP, VM_Data_Offset( floatBase ), rVMDATA );
				in( iLFD, fFIRST, stack_temp, r1 );
				in( iFSUB, fFIRST, fFIRST, fTMP );
				in( iFRSP, fFIRST, fFIRST );
				gpr_pos--;
				break;

			case OP_CVFI:
				MAYBE_EMIT_CONST();
				gpr_pos++;
				// rounds in the FPSCR mode (nearest, Sys_SetFloatEnv) like the interpreter's lrintf Q_ftol
				in( iFCTIW, fFIRST, fFIRST );
				in( iSTFD, fFIRST, stack_temp, r1 );
				in( iLWZ, rFIRST, stack_temp + 4, r1 );
				fpr_pos--;
				break;
		}

		i_const = NULL;

		if ( i_now->op != OP_CONST ) {
			// emit the instructions if it isn't OP_CONST
			emitEnd();
		} else {
			// mark in what register the value should be saved
			if ( RET_INT )
				i_now->regPos = ++gpr_pos;
			else
				i_now->regPos = ++fpr_pos;

#if OPTIMIZE_HOLE
			i_const = i_now;
#else
			PPC_EmitConst( i_now );
#endif
		}
	}
	if ( i_const )
		DIE( "left (unused) OP_CONST" );
}


// Writes a branch into its slot. A long slot keeps a nop first when the bc reaches, else it
// becomes an inverted bc over a b; unconditional ones use bc/bcl when in reach, like upstream.
static void
PPC_WriteJump( const ppc_jump_t *j, ppc_instruction_t *target, qboolean longSlot )
{
	ppc_instruction_t *at = j->at + ( longSlot ? 1 : 0 );
	long int disp = target - at;
	long int bo;

	if ( disp >= -8192 && disp < 8192 ) {
		*at = IN( ( j->ext & branchExtLink ) ? iBCL : iBC, j->bo, j->bi, disp * 4 );
		return;
	}

	*at = IN( ( j->ext & branchExtLink ) ? iBL : iB, disp * 4 );
	if ( j->bo == branchAlways )
		return;

	if ( !longSlot )
		DIE( "conditional branch out of reach" );

	switch ( j->bo ) {
		case branchTrue:
			bo = branchFalse;
			break;
		case branchFalse:
			bo = branchTrue;
			break;
		default:
			DIE( "unrecognized branch type" );
	}
	at[ -1 ] = IN( iBC, bo, j->bi, +2*4 );
}

// Closes the function [i_first, i_last] emitted at fn_base. Its conditional branches got two
// words each; if the whole function is within bc reach (8192 words), drop the second one.
static void
PPC_FinishFunction( unsigned long int i_first, unsigned long int i_last )
{
	qboolean longSlots = ( out_now - fn_base ) >= 8192;
	unsigned long int i;
	long int k, dropped;

	for ( i = i_first; i <= i_last; i++ ) {
		if ( !ip_tab[ i ] )
			DIE( "instruction pointer not set" );
	}

	if ( !longSlots ) {
		ppc_instruction_t *dst = fn_base, *from = fn_base;

		// both lists are in address order: an address moves back by the dropped words before it
		k = 0;
		dropped = 0;
		for ( i = i_first; i <= i_last; i++ ) {
			ppc_instruction_t *a = (ppc_instruction_t *)ip_tab[ i ];

			for ( ; k < fn_jumps_num && fn_jumps[ k ].at + 2 <= a; k++ ) {
				if ( fn_jumps[ k ].bo != branchAlways )
					dropped++;
			}
			ip_tab[ i ] = (intptr_t)( a - dropped );
		}

		dropped = 0;
		for ( k = 0; k < fn_jumps_num; k++ ) {
			ppc_jump_t *j = &fn_jumps[ k ];
			ppc_instruction_t *at = j->at;

			if ( j->bo == branchAlways ) {
				j->at -= dropped;
				continue;
			}
			memmove( dst, from, ( at + 1 - from ) * sizeof( ppc_instruction_t ) );
			dst += at + 1 - from;
			from = at + 2;
			j->at -= dropped;
			dropped++;
		}
		memmove( dst, from, ( out_now - from ) * sizeof( ppc_instruction_t ) );
		out_now = dst + ( out_now - from );
	}

	for ( k = 0; k < fn_jumps_num; k++ ) {
		ppc_jump_t *j = &fn_jumps[ k ];

		if ( j->stub ) {
			PPC_WriteJump( j, j->stub, qfalse );
			continue;
		}
		if ( j->dest > i_last ) {
			// a later function: its address is known only at the end
			if ( j->bo != branchAlways )
				DIE( "conditional jump out of its function" );
			if ( fixups_num >= fixups_max )
				DIE( "too many fixups" );
			fixups[ fixups_num++ ] = *j;
			continue;
		}
		if ( j->dest < i_first && j->bo != branchAlways )
			DIE( "conditional jump out of its function" );

		PPC_WriteJump( j, (ppc_instruction_t *)ip_tab[ j->dest ],
			( j->bo != branchAlways ) && longSlots );
	}

	fn_jumps_num = 0;
	fn_base = out_now;
}

static void
PPC_FreeScratch( void )
{
	if ( src )
		Z_Free( src );
	if ( fn_jumps )
		Z_Free( fn_jumps );
	if ( fixups )
		Z_Free( fixups );
	if ( data_list )
		Z_Free( data_list );
	src = NULL;
	fn_jumps = fixups = NULL;
	data_list = NULL;
}

static void
VM_Destroy_Compiled( vm_t *self )
{
	int i;

	for ( i = 0; i < ARRAY_LEN( vmjit_slots ); i++ ) {
		vmjit_slot_t *slot = &vmjit_slots[ i ];

		if ( self->codeBase >= slot->base && self->codeBase < slot->base + slot->size )
			slot->busy = qfalse;
	}
	self->codeBase = NULL;
}

// Compiles straight into slot. Any failure, a full slot included, returns false with the reason
// in vmjit_error, and leaves the slot free.
static qboolean
PPC_Compile( vm_t *vm, vmHeader_t *header, vmjit_slot_t *slot )
{
	int i;
	unsigned long int i_count, pc, op, fnOps, maxFnOps, branches, n;
	unsigned char *code = (unsigned char *)header + header->codeOffset;
	vm_data_t *data;
	size_t dataLength;

	if ( setjmp( vmjit_abort ) ) {
		PPC_FreeScratch();
		return qfalse;
	}

	// sizes for the scratch blocks: the largest function, and every call/jump that may
	// leave its function
	pc = 0;
	fnOps = maxFnOps = branches = 0;
	for ( i_count = 0; i_count < header->instructionCount; i_count++ ) {
		if ( pc >= header->codeLength )
			DIE( "code shorter than its instruction count" );
		op = code[ pc++ ];
		if ( op == OP_ENTER && fnOps ) {
			if ( fnOps > maxFnOps )
				maxFnOps = fnOps;
			fnOps = 0;
		}
		fnOps++;
		if ( op == OP_CALL || op == OP_JUMP )
			branches++;
		if ( vm_opInfo[ op ] & opImm4 )
			pc += 4;
		else if ( vm_opInfo[ op ] & opImm1 )
			pc += 1;
	}
	if ( fnOps > maxFnOps )
		maxFnOps = fnOps;
	if ( pc > header->codeLength )
		DIE( "code shorter than its instruction count" );

	src = Z_Malloc( ( maxFnOps + 1 ) * sizeof( *src ) );
	fn_jumps = Z_Malloc( maxFnOps * sizeof( *fn_jumps ) );
	fn_jumps_max = maxFnOps;
	fn_jumps_num = 0;
	fixups = Z_Malloc( ( branches + 1 ) * sizeof( *fixups ) );
	fixups_max = branches;
	fixups_num = 0;
	data_list = Z_Malloc( VM_DATA_MAX * sizeof( *data_list ) );
	data_acc = 0;

	ip_tab = vm->instructionPointers;
	ip_count = header->instructionCount;
	Com_Memset( ip_tab, 0, ip_count * sizeof( *ip_tab ) );

	out_now = (ppc_instruction_t *)slot->base;
	out_end = (ppc_instruction_t *)( slot->base + slot->size );

	PPC_MakeFastMask( vm->dataMask );
	PPC_CompileInit();
	fn_base = out_now;

	// read the program and send each function (OP_ENTER to OP_ENTER) to the compiler
	src[ 0 ].next = NULL;
	n = 0;
	for ( i_count = 0, pc = 0; i_count < header->instructionCount; i_count++ ) {
		source_instruction_t *i_now;

		op = code[ pc++ ];
		if ( op == OP_ENTER && n ) {
			VM_CompileFunction( &src[ 0 ] );
			PPC_FinishFunction( src[ 1 ].i_count, src[ n ].i_count );
			n = 0;
		}

		i_now = &src[ ++n ];
		i_now->op = op;
		i_now->i_count = i_count;
		i_now->arg.i = 0;
		i_now->regA1 = 0;
		i_now->regA2 = 0;
		i_now->regR = 0;
		i_now->regPos = 0;
		i_now->next = NULL;
		src[ n - 1 ].next = i_now;

		if ( vm_opInfo[ op ] & opImm4 ) {
			// little endian in the file
			i_now->arg.i = code[ pc ] | ( code[ pc + 1 ] << 8 ) |
				( code[ pc + 2 ] << 16 ) | ( (unsigned int)code[ pc + 3 ] << 24 );
			pc += 4;
		} else if ( vm_opInfo[ op ] & opImm1 ) {
			i_now->arg.b = code[ pc++ ];
		}
	}
	if ( n ) {
		VM_CompileFunction( &src[ 0 ] );
		PPC_FinishFunction( src[ 1 ].i_count, src[ n ].i_count );
	}

	for ( i = 0; i < fixups_num; i++ )
		PPC_WriteJump( &fixups[ i ], (ppc_instruction_t *)ip_tab[ fixups[ i ].dest ], qfalse );

	// vm_data_t and the float constants follow the code
	data = (vm_data_t *)( ( (intptr_t)out_now + 15 ) & ~(intptr_t)15 );
	dataLength = VM_Data_Offset( data ) + data_acc * sizeof( unsigned int );
	if ( (unsigned char *)data + dataLength > (unsigned char *)out_end )
		DIE( "code slot is full" );

	{
		opd_t *ac = (void *)VM_AsmCall, *bc = (void *)VM_BlockCopy, *ec = (void *)VM_JitError;

		// same TOC as the engine, so the C helpers are called without switching it
		data->opd.function = stub_entry;
		data->opd.toc = ac->toc;
		data->opd.env = ac->env;

		data->AsmCall = ac->function;
		data->BlockCopy = bc->function;
		data->Error = ec->function;
	}
	data->dataMask = vm->dataMask;
	data->iPointers = (ppc_instruction_t *)vm->instructionPointers;
	data->breakCount = &vm->breakCount;
	data->instructionCount = header->instructionCount;
	// VM_Create's vm->stackBottom, which it sets only after the compile
	data->stackBottom = vm->dataMask + 1 - PROGRAM_STACK_SIZE;
	data->dataLength = dataLength;
	data->codeLength = ( out_now - (ppc_instruction_t *)slot->base ) * sizeof( ppc_instruction_t );
	data->floatBase = 0x59800004;
	memcpy( data->data, data_list, data_acc * sizeof( unsigned int ) );

	PPC_FreeScratch();

	vm->codeBase = (byte *)data;
	vm->codeLength = (unsigned char *)data + dataLength - slot->base;
	vm->destroy = VM_Destroy_Compiled;
	vm->compiled = qtrue;

	ps3_jit_icache_sync( slot->base, vm->codeLength );
	return qtrue;
}


// Boot self-test (QVM_JIT_TODO.md S5): small QVM programs run on the interpreter and on the JIT,
// which must agree on the return value and the data image. Any difference keeps the JIT off.

#define VMJIT_T_LABEL	0x100	// marks label arg at the next instruction
#define VMJIT_T_CONSTL	0x101	// OP_CONST of label arg's instruction number
#define VMJIT_T_LABELS	64

typedef struct {
	unsigned short	op;
	int		arg;	// a label number for OP_EQ .. OP_GEF
} vmjit_top_t;

typedef struct {
	const char		*name;
	const vmjit_top_t	*ops;
	int			numOps;
	int			arg1, arg2, arg3;	// vmMain args 1-3
	qboolean		guard;	// JIT only: must stop in the OP_CALL guard
} vmjit_test_t;

// the test image; the stack is its top, so stackBottom is 0 and the data below is compared
#define VT_IMAGE	0x10000
#define VT_NEG5		0x100	// -5
#define VT_MIN		0x104	// INT_MIN
#define VT_M1		0x108	// -1
#define VT_F1		0x110	// 3.75f
#define VT_F2		0x114	// -2.5f
#define VT_BYTES	0x118	// 0x817FFF01
#define VT_HALVES	0x11C	// 0x80017FFF
#define VT_PTR		0x120	// function pointers the programs store
#define VT_SRC		0x200	// 256 pattern bytes
#define VT_JTAB		0x300	// a jump table the program stores
#define VT_RES		0x400	// results
#define VT_DATA		0x800

#define T0( op )	{ OP_##op, 0 }
#define T1( op, x )	{ OP_##op, (int)( x ) }
#define TLABEL( n )	{ VMJIT_T_LABEL, n }
#define TCONSTL( n )	{ VMJIT_T_CONSTL, n }

// results[ r ] = vmMain arg 1 op arg 2 (frame 8); results[ r ] = f1 op f2
#define VT_BIN( r, op )	T1( CONST, VT_RES + 4 * (r) ), T1( LOCAL, 20 ), T0( LOAD4 ), T1( LOCAL, 24 ), T0( LOAD4 ), T0( op ), T0( STORE4 )
#define VT_FBIN( r, op )	T1( CONST, VT_RES + 4 * (r) ), T1( CONST, VT_F1 ), T0( LOAD4 ), T1( CONST, VT_F2 ), T0( LOAD4 ), T0( op ), T0( STORE4 )
// compare args a, b (frame 16): bit k of the mask at local 8 stays clear when the compare branches
#define VT_BIT( k, lbl )	T1( LOCAL, 8 ), T1( LOCAL, 8 ), T0( LOAD4 ), T1( CONST, 1 << (k) ), T0( BOR ), T0( STORE4 ), TLABEL( lbl )
#define VT_CMP( op, k, lbl )	T1( LOCAL, 24 ), T0( LOAD4 ), T1( LOCAL, 28 ), T0( LOAD4 ), T1( op, lbl ), VT_BIT( k, lbl )
#define VT_CMPK( op, x, k, lbl )	T1( LOCAL, 24 ), T0( LOAD4 ), T1( CONST, x ), T1( op, lbl ), VT_BIT( k, lbl )
// results[ r ] = function lbl( x, y )
#define VT_CALL2( r, x, y, lbl )	T1( CONST, x ), T1( ARG, 8 ), T1( CONST, y ), T1( ARG, 12 ), \
	T1( CONST, VT_RES + 4 * (r) ), TCONSTL( lbl ), T0( CALL ), T0( STORE4 )
// results[ i ] = v, i at local 8
#define VT_JRES( v )	T1( CONST, VT_RES ), T1( LOCAL, 8 ), T0( LOAD4 ), T1( CONST, 2 ), T0( LSH ), T0( ADD ), \
	T1( CONST, v ), T0( STORE4 )

// lwz zero-extends the -5: only the entry stub's extsw makes it -5 for C
static const vmjit_top_t vt_return[] = {
	T1( ENTER, 8 ), T1( CONST, VT_NEG5 ), T0( LOAD4 ), T1( LEAVE, 8 ),
};

static const vmjit_top_t vt_intmath[] = {
	T1( ENTER, 8 ),
	VT_BIN( 0, ADD ), VT_BIN( 1, SUB ), VT_BIN( 2, MULI ), VT_BIN( 3, MULU ),
	VT_BIN( 4, DIVI ), VT_BIN( 5, DIVU ), VT_BIN( 6, MODI ), VT_BIN( 7, MODU ),
	VT_BIN( 8, BAND ), VT_BIN( 9, BOR ), VT_BIN( 10, BXOR ),
	VT_BIN( 11, LSH ), VT_BIN( 12, RSHI ), VT_BIN( 13, RSHU ),
	T1( CONST, VT_RES + 56 ), T1( LOCAL, 28 ), T0( LOAD4 ), T1( LOCAL, 24 ), T0( LOAD4 ), T0( RSHI ), T0( STORE4 ),
	T1( CONST, VT_RES + 60 ), T1( LOCAL, 28 ), T0( LOAD4 ), T1( LOCAL, 24 ), T0( LOAD4 ), T0( RSHU ), T0( STORE4 ),
	T1( CONST, VT_RES + 64 ), T1( LOCAL, 20 ), T0( LOAD4 ), T0( NEGI ), T0( STORE4 ),
	T1( CONST, VT_RES + 68 ), T1( LOCAL, 28 ), T0( LOAD4 ), T0( BCOM ), T0( STORE4 ),
	T1( CONST, VT_RES + 72 ), T1( LOCAL, 28 ), T0( LOAD4 ), T0( SEX8 ), T0( STORE4 ),
	T1( CONST, VT_RES + 76 ), T1( LOCAL, 28 ), T0( LOAD4 ), T0( SEX16 ), T0( STORE4 ),
	// constants: folded into OP_ADD (addis + addi), then lis/ori, li/ori and li
	T1( CONST, VT_RES + 80 ), T1( LOCAL, 20 ), T0( LOAD4 ), T1( CONST, 0x12345 ), T0( ADD ), T0( STORE4 ),
	T1( CONST, VT_RES + 84 ), T1( LOCAL, 20 ), T0( LOAD4 ), T1( CONST, -40000 ), T0( ADD ), T0( STORE4 ),
	T1( CONST, VT_RES + 88 ), T1( LOCAL, 20 ), T0( LOAD4 ), T1( CONST, 0x8000 ), T0( ADD ), T0( STORE4 ),
	T1( CONST, VT_RES + 92 ), T1( CONST, 0x89ABCDEF ), T0( STORE4 ),
	T1( CONST, VT_RES + 96 ), T1( CONST, 0xC000 ), T0( STORE4 ),
	T1( CONST, VT_RES + 100 ), T1( CONST, -0x8000 ), T0( STORE4 ),
	// loaded negatives are zero-extended in the registers: the low word must still be right
	T1( CONST, VT_RES + 104 ), T1( CONST, VT_NEG5 ), T0( LOAD4 ), T1( CONST, VT_M1 ), T0( LOAD4 ), T0( ADD ), T0( STORE4 ),
	T1( CONST, VT_RES + 108 ), T1( CONST, VT_NEG5 ), T0( LOAD4 ), T1( CONST, VT_MIN ), T0( LOAD4 ), T0( MULI ), T0( STORE4 ),
	T1( CONST, VT_RES + 112 ), T1( CONST, VT_NEG5 ), T0( LOAD4 ), T1( CONST, 2 ), T0( DIVI ), T0( STORE4 ),
	T1( CONST, VT_RES + 104 ), T0( LOAD4 ), T1( LEAVE, 8 ),
};

// no call, so no frame: CVIF, CVFI and the float return use the red zone (STACK_RTEMP)
static const vmjit_top_t vt_floatleaf[] = {
	T1( ENTER, 8 ),
	VT_FBIN( 0, ADDF ), VT_FBIN( 1, SUBF ), VT_FBIN( 2, MULF ), VT_FBIN( 3, DIVF ),
	T1( CONST, VT_RES + 16 ), T1( CONST, VT_F2 ), T0( LOAD4 ), T0( NEGF ), T0( STORE4 ),
	T1( CONST, VT_RES + 20 ), T1( LOCAL, 20 ), T0( LOAD4 ), T0( CVIF ), T0( STORE4 ),
	T1( CONST, VT_RES + 24 ), T1( CONST, VT_F2 ), T0( LOAD4 ), T0( CVFI ), T0( STORE4 ),
	T1( CONST, VT_RES + 28 ), T1( CONST, VT_F1 ), T0( LOAD4 ), T1( CONST, 0x3FC00000 ), T0( MULF ), T0( CVFI ), T0( STORE4 ),
	T1( CONST, VT_RES + 32 ), T1( CONST, VT_MIN ), T0( LOAD4 ), T0( CVIF ), T0( STORE4 ),
	T1( CONST, VT_RES + 36 ), T1( LOCAL, 24 ), T0( LOAD4 ), T0( CVIF ), T1( CONST, 0x40490FDB ), T0( MULF ), T0( STORE4 ),
	T1( CONST, VT_RES + 40 ), T1( CONST, VT_F1 ), T0( LOAD4 ), T0( CVFI ), T0( CVIF ), T0( STORE4 ),
	T1( CONST, VT_F1 ), T0( LOAD4 ), T1( CONST, VT_F2 ), T0( LOAD4 ), T0( MULF ), T1( LEAVE, 8 ),
};

// the same with a call, so the conversions use STACK_TEMP in the frame
static const vmjit_top_t vt_floatcall[] = {
	T1( ENTER, 16 ),
	T1( LOCAL, 28 ), T0( LOAD4 ), T1( ARG, 8 ),
	T1( LOCAL, 12 ), TCONSTL( 1 ), T0( CALL ), T0( STORE4 ),
	T1( CONST, VT_RES + 0 ), T1( LOCAL, 12 ), T0( LOAD4 ), T0( CVIF ), T1( CONST, VT_F1 ), T0( LOAD4 ), T0( MULF ), T0( STORE4 ),
	T1( CONST, VT_RES + 4 ), T1( CONST, VT_F2 ), T0( LOAD4 ), T1( CONST, VT_F1 ), T0( LOAD4 ), T0( DIVF ), T0( CVFI ), T0( STORE4 ),
	T1( CONST, VT_RES + 8 ), T1( CONST, VT_F1 ), T0( LOAD4 ), T0( NEGF ), T1( CONST, 0x447A0000 ), T0( MULF ), T0( CVFI ), T0( STORE4 ),
	T1( CONST, VT_RES + 12 ), T1( LOCAL, 32 ), T0( LOAD4 ), T0( CVIF ), T0( STORE4 ),
	T1( CONST, VT_F1 ), T0( LOAD4 ), T1( CONST, VT_F2 ), T0( LOAD4 ), T0( SUBF ), T0( NEGF ), T1( LEAVE, 16 ),
	TLABEL( 1 ),
	T1( ENTER, 8 ), T1( LOCAL, 16 ), T0( LOAD4 ), T1( CONST, 3 ), T0( MULI ), T1( LEAVE, 8 ),
};

// every compare-and-branch, on registers and on immediates; function 1 is int, 2 float
static const vmjit_top_t vt_compare[] = {
	T1( ENTER, 16 ),
	VT_CALL2( 0, 5, 5, 1 ), VT_CALL2( 1, -3, 7, 1 ), VT_CALL2( 2, 7, -3, 1 ),
	VT_CALL2( 3, 0x7FFFFFFF, 0x80000000, 1 ), VT_CALL2( 4, 0x9000, 0xC000, 1 ),
	VT_CALL2( 5, 0xC000, 0xC000, 1 ), VT_CALL2( 6, -100, -100, 1 ), VT_CALL2( 7, 0, -1, 1 ),
	VT_CALL2( 8, 0xFFFF, 0x10000, 1 ),
	VT_CALL2( 9, 0x3F800000, 0x3F800000, 2 ), VT_CALL2( 10, 0xC0200000, 0x40700000, 2 ),
	VT_CALL2( 11, 0x40700000, 0xC0200000, 2 ), VT_CALL2( 12, 0, 0x80000000, 2 ),
	VT_CALL2( 13, 0x0DA24260, 0, 2 ),
	T1( CONST, VT_RES ), T0( LOAD4 ), T1( LEAVE, 16 ),
	TLABEL( 1 ),
	T1( ENTER, 16 ),
	T1( LOCAL, 8 ), T1( CONST, 0 ), T0( STORE4 ),
	VT_CMP( EQ, 0, 10 ), VT_CMP( NE, 1, 11 ), VT_CMP( LTI, 2, 12 ), VT_CMP( LEI, 3, 13 ),
	VT_CMP( GTI, 4, 14 ), VT_CMP( GEI, 5, 15 ), VT_CMP( LTU, 6, 16 ), VT_CMP( LEU, 7, 17 ),
	VT_CMP( GTU, 8, 18 ), VT_CMP( GEU, 9, 19 ),
	VT_CMPK( EQ, 0xC000, 10, 20 ), VT_CMPK( NE, -1, 11, 21 ), VT_CMPK( LTI, -100, 12, 22 ),
	VT_CMPK( GEI, 7, 13, 23 ), VT_CMPK( LTU, 0x9000, 14, 24 ), VT_CMPK( GTU, 5, 15, 25 ),
	VT_CMPK( LEI, 0x7FFF, 16, 26 ), VT_CMPK( GEU, 0xFFFF, 17, 27 ), VT_CMPK( LTI, 0x12345, 18, 28 ),
	VT_CMPK( EQ, 0x7FFFFFFF, 19, 29 ),
	T1( LOCAL, 8 ), T0( LOAD4 ), T1( LEAVE, 16 ),
	TLABEL( 2 ),
	T1( ENTER, 16 ),
	T1( LOCAL, 8 ), T1( CONST, 0 ), T0( STORE4 ),
	VT_CMP( EQF, 0, 30 ), VT_CMP( NEF, 1, 31 ), VT_CMP( LTF, 2, 32 ),
	VT_CMP( LEF, 3, 33 ), VT_CMP( GTF, 4, 34 ), VT_CMP( GEF, 5, 35 ),
	VT_CMPK( LTF, 0x3F800000, 6, 36 ), VT_CMPK( GEF, 0xC0200000, 7, 37 ),
	T1( LOCAL, 8 ), T0( LOAD4 ), T1( LEAVE, 16 ),
};

static const vmjit_top_t vt_loadstore[] = {
	T1( ENTER, 8 ),
	T1( CONST, VT_RES + 0 ), T1( CONST, VT_BYTES ), T0( LOAD1 ), T0( STORE4 ),
	T1( CONST, VT_RES + 4 ), T1( CONST, VT_BYTES + 1 ), T0( LOAD1 ), T0( STORE4 ),
	T1( CONST, VT_RES + 8 ), T1( CONST, VT_BYTES + 3 ), T0( LOAD1 ), T0( SEX8 ), T0( STORE4 ),
	T1( CONST, VT_RES + 12 ), T1( CONST, VT_HALVES ), T0( LOAD2 ), T0( STORE4 ),
	T1( CONST, VT_RES + 16 ), T1( CONST, VT_HALVES + 2 ), T0( LOAD2 ), T0( SEX16 ), T0( STORE4 ),
	T1( CONST, VT_RES + 20 ), T1( CONST, VT_NEG5 ), T0( LOAD4 ), T0( STORE4 ),
	T1( CONST, VT_RES + 24 ), T1( CONST, 0x1234ABCD ), T0( STORE1 ),
	T1( CONST, VT_RES + 25 ), T1( LOCAL, 20 ), T0( LOAD4 ), T0( STORE1 ),
	T1( CONST, VT_RES + 26 ), T1( CONST, -2 ), T0( STORE1 ),
	T1( CONST, VT_RES + 27 ), T1( CONST, 0x180 ), T0( STORE1 ),
	T1( CONST, VT_RES + 28 ), T1( CONST, -2 ), T0( STORE2 ),
	T1( CONST, VT_RES + 30 ), T1( LOCAL, 20 ), T0( LOAD4 ), T0( STORE2 ),
	// a float through lfsx / stfsx
	T1( CONST, VT_RES + 32 ), T1( CONST, VT_F2 ), T0( LOAD4 ), T0( NEGF ), T0( STORE4 ),
	// addresses wrap at the data mask, as in the interpreter
	T1( CONST, VT_RES + 36 + VT_IMAGE ), T1( CONST, 77 ), T0( STORE4 ),
	T1( CONST, VT_RES + 40 ), T1( CONST, VT_NEG5 + 3 * VT_IMAGE ), T0( LOAD4 ), T0( STORE4 ),
	T1( CONST, VT_RES + 44 - VT_IMAGE ), T1( CONST, VT_BYTES - 5 * VT_IMAGE ), T0( LOAD1 ), T0( STORE2 ),
	T1( CONST, VT_RES + 20 ), T0( LOAD4 ), T1( LEAVE, 8 ),
};

static const vmjit_top_t vt_blockcopy[] = {
	T1( ENTER, 8 ),
	T1( CONST, VT_RES + 0 ), T1( CONST, VT_SRC ), T1( BLOCK_COPY, 12 ),
	T1( CONST, VT_RES + 16 ), T1( CONST, VT_SRC + 3 ), T1( BLOCK_COPY, 7 ),
	T1( CONST, VT_RES + 32 ), T1( CONST, VT_SRC ), T1( BLOCK_COPY, 32 ),
	T1( CONST, VT_RES + 64 ), T1( CONST, VT_SRC + 1 ), T1( BLOCK_COPY, 200 ),
	T1( LOCAL, 20 ), T0( LOAD4 ), T1( CONST, VT_SRC + 16 ), T1( BLOCK_COPY, 16 ),
	// this add carries into the high word of the register: VM_BlockCopy needs it cleared
	T1( CONST, VT_RES + 0x1A5 ), T1( CONST, VT_NEG5 ), T0( LOAD4 ), T0( ADD ), T1( CONST, VT_SRC + 32 ), T1( BLOCK_COPY, 8 ),
	T1( CONST, VT_RES + 4 ), T0( LOAD4 ), T1( LEAVE, 8 ),
};

// direct calls, a value kept across a call, and recursion: fib( n ) and a - 2 * b + c
static const vmjit_top_t vt_calls[] = {
	T1( ENTER, 24 ),
	T1( CONST, 10 ), T1( ARG, 8 ),
	T1( CONST, VT_RES + 0 ), TCONSTL( 1 ), T0( CALL ), T0( STORE4 ),
	T1( CONST, 100 ), T1( ARG, 8 ), T1( CONST, -20 ), T1( ARG, 12 ), T1( CONST, 3 ), T1( ARG, 16 ),
	T1( CONST, VT_RES + 4 ), TCONSTL( 2 ), T0( CALL ), T0( STORE4 ),
	T1( LOCAL, 36 ), T0( LOAD4 ), T1( ARG, 8 ),
	T1( CONST, VT_RES + 8 ), TCONSTL( 1 ), T0( CALL ), T0( STORE4 ),
	T1( CONST, VT_RES + 0 ), T0( LOAD4 ), T1( LEAVE, 24 ),
	TLABEL( 1 ),
	T1( ENTER, 24 ),
	T1( LOCAL, 32 ), T0( LOAD4 ), T1( CONST, 2 ), T1( GEI, 3 ),
	T1( LOCAL, 32 ), T0( LOAD4 ), T1( LEAVE, 24 ),
	TLABEL( 3 ),
	T1( LOCAL, 32 ), T0( LOAD4 ), T1( CONST, 1 ), T0( SUB ), T1( ARG, 8 ),
	T1( LOCAL, 12 ), TCONSTL( 1 ), T0( CALL ), T0( STORE4 ),
	T1( LOCAL, 32 ), T0( LOAD4 ), T1( CONST, 2 ), T0( SUB ), T1( ARG, 8 ),
	T1( LOCAL, 12 ), T0( LOAD4 ), TCONSTL( 1 ), T0( CALL ), T0( ADD ), T1( LEAVE, 24 ),
	TLABEL( 2 ),
	T1( ENTER, 8 ),
	T1( LOCAL, 16 ), T0( LOAD4 ), T1( LOCAL, 20 ), T0( LOAD4 ), T1( CONST, 2 ), T0( MULI ), T0( SUB ),
	T1( LOCAL, 24 ), T0( LOAD4 ), T0( ADD ), T1( LEAVE, 8 ),
};

// a direct syscall, syscalls through pointers (zero-extended negatives), a VM call through one
static const vmjit_top_t vt_syscalls[] = {
	T1( ENTER, 32 ),
	T1( CONST, 11 ), T1( ARG, 8 ), T1( CONST, -22 ), T1( ARG, 12 ), T1( CONST, 33 ), T1( ARG, 16 ),
	T1( CONST, -44 ), T1( ARG, 20 ), T1( CONST, 0x7FFFFFFF ), T1( ARG, 24 ),
	T1( CONST, VT_RES + 0 ), T1( CONST, -1 ), T0( CALL ), T0( STORE4 ),
	T1( CONST, VT_PTR ), T1( CONST, -2 ), T0( STORE4 ),
	T1( CONST, 5 ), T1( ARG, 8 ), T1( CONST, -6 ), T1( ARG, 12 ),
	T1( CONST, VT_RES + 4 ), T1( CONST, VT_PTR ), T0( LOAD4 ), T0( CALL ), T0( STORE4 ),
	T1( CONST, 7 ), T1( ARG, 8 ), T1( CONST, 8 ), T1( ARG, 12 ),
	T1( CONST, VT_RES + 8 ), T1( LOCAL, 44 ), T0( LOAD4 ), T0( CALL ), T0( STORE4 ),
	T1( CONST, VT_PTR + 4 ), TCONSTL( 1 ), T0( STORE4 ),
	T1( CONST, 9 ), T1( ARG, 8 ),
	T1( CONST, VT_RES + 12 ), T1( CONST, VT_PTR + 4 ), T0( LOAD4 ), T0( CALL ), T0( STORE4 ),
	T1( CONST, VT_RES + 4 ), T0( LOAD4 ), T1( LEAVE, 32 ),
	TLABEL( 1 ),
	T1( ENTER, 8 ), T1( LOCAL, 16 ), T0( LOAD4 ), T1( CONST, 1000 ), T0( ADD ), T1( LEAVE, 8 ),
};

// a switch: the table holds instruction numbers, OP_JUMP goes through the guard
static const vmjit_top_t vt_jumptable[] = {
	T1( ENTER, 16 ),
	T1( CONST, VT_JTAB + 0 ), TCONSTL( 12 ), T0( STORE4 ),
	T1( CONST, VT_JTAB + 4 ), TCONSTL( 10 ), T0( STORE4 ),
	T1( CONST, VT_JTAB + 8 ), TCONSTL( 13 ), T0( STORE4 ),
	T1( CONST, VT_JTAB + 12 ), TCONSTL( 11 ), T0( STORE4 ),
	T1( LOCAL, 8 ), T1( CONST, 0 ), T0( STORE4 ),
	TLABEL( 1 ),
	T1( LOCAL, 8 ), T0( LOAD4 ), T1( CONST, 2 ), T0( LSH ), T1( CONST, VT_JTAB ), T0( ADD ), T0( LOAD4 ), T0( JUMP ),
	TLABEL( 10 ), VT_JRES( 100 ), TCONSTL( 2 ), T0( JUMP ),
	TLABEL( 11 ), VT_JRES( 200 ), TCONSTL( 2 ), T0( JUMP ),
	TLABEL( 12 ), VT_JRES( 300 ), TCONSTL( 2 ), T0( JUMP ),
	TLABEL( 13 ),
	T1( CONST, VT_RES ), T1( LOCAL, 8 ), T0( LOAD4 ), T1( CONST, 2 ), T0( LSH ), T0( ADD ),
	T1( LOCAL, 8 ), T0( LOAD4 ), T1( LOCAL, 8 ), T0( LOAD4 ), T0( MULI ), T1( CONST, 1000 ), T0( ADD ), T0( STORE4 ),
	TLABEL( 2 ),
	T1( LOCAL, 8 ), T1( LOCAL, 8 ), T0( LOAD4 ), T1( CONST, 1 ), T0( ADD ), T0( STORE4 ),
	T1( LOCAL, 8 ), T0( LOAD4 ), T1( CONST, 4 ), T1( LTI, 1 ),
	T1( CONST, VT_RES + 8 ), T0( LOAD4 ), T1( LEAVE, 16 ),
};

// an indirect call far past instructionCount: the guard must stop it
static const vmjit_top_t vt_guard[] = {
	T1( ENTER, 8 ), T1( LOCAL, 20 ), T0( LOAD4 ), T0( CALL ), T1( LEAVE, 8 ),
};

#define VT( name, prog, a1, a2, a3, guard )	{ name, prog, ARRAY_LEN( prog ), a1, a2, a3, guard }

static const vmjit_test_t vmjit_tests[] = {
	VT( "negative return", vt_return, 0, 0, 0, qfalse ),
	VT( "int math", vt_intmath, -7, 3, (int)0x9234C6F0, qfalse ),
	VT( "float leaf", vt_floatleaf, -7, 12345, 0, qfalse ),
	VT( "float call", vt_floatcall, -7, -100, 0, qfalse ),
	VT( "compare", vt_compare, 0, 0, 0, qfalse ),
	VT( "load store", vt_loadstore, 0x1234ABCD, 0, 0, qfalse ),
	VT( "block copy", vt_blockcopy, VT_RES + 0x180, 0, 0, qfalse ),
	VT( "calls", vt_calls, 12, 0, 0, qfalse ),
	VT( "syscalls", vt_syscalls, -2, 0, 0, qfalse ),
	VT( "jump table", vt_jumptable, 0, 0, 0, qfalse ),
	VT( "call guard", vt_guard, 0x7FFF0000, 0, 0, qtrue ),
};

#undef T0
#undef T1
#undef TLABEL
#undef TCONSTL
#undef VT

static int vmjit_selftest;	// 0 not run, 1 passed, -1 failed

static intptr_t
VM_JitTestSyscall( intptr_t *args )
{
	switch ( args[ 0 ] ) {
		case 0:
			return args[ 1 ] - 2 * args[ 2 ] + 3 * args[ 3 ] - 4 * args[ 4 ] + 5 * args[ 5 ];
		case 1:
			return args[ 1 ] * 1000 + args[ 2 ];
		default:
			return -12345;
	}
}

static void
VM_JitTestImage( byte *image )
{
	int i;

	Com_Memset( image, 0, VT_IMAGE + 4 );
	*(int *)( image + VT_NEG5 ) = -5;
	*(int *)( image + VT_MIN ) = (int)0x80000000;
	*(int *)( image + VT_M1 ) = -1;
	*(unsigned int *)( image + VT_F1 ) = 0x40700000;
	*(unsigned int *)( image + VT_F2 ) = 0xC0200000;
	*(unsigned int *)( image + VT_BYTES ) = 0x817FFF01;
	*(unsigned int *)( image + VT_HALVES ) = 0x80017FFF;
	for ( i = 0; i < 256; i++ )
		image[ VT_SRC + i ] = (byte)( i * 37 + 11 );
}

// Builds a .qvm code segment: labels resolved, immediates little-endian. NULL on a bad label.
static vmHeader_t *
VM_JitTestAssemble( const vmjit_test_t *t )
{
	int label[ VMJIT_T_LABELS ];
	int i, n, len;
	vmHeader_t *h;
	byte *code;

	for ( i = 0; i < VMJIT_T_LABELS; i++ )
		label[ i ] = -1;
	for ( i = n = len = 0; i < t->numOps; i++ ) {
		int op = t->ops[ i ].op;

		if ( op == VMJIT_T_LABEL ) {
			label[ t->ops[ i ].arg ] = n;
			continue;
		}
		if ( op == VMJIT_T_CONSTL )
			op = OP_CONST;
		n++;
		len += 1 + ( ( vm_opInfo[ op ] & opImm4 ) ? 4 : ( vm_opInfo[ op ] & opImm1 ) ? 1 : 0 );
	}

	h = Z_Malloc( sizeof( *h ) + len );
	h->vmMagic = VM_MAGIC;
	h->instructionCount = n;
	h->codeOffset = sizeof( *h );
	h->codeLength = len;
	code = (byte *)h + h->codeOffset;

	for ( i = 0; i < t->numOps; i++ ) {
		int op = t->ops[ i ].op, arg = t->ops[ i ].arg;

		if ( op == VMJIT_T_LABEL )
			continue;
		if ( op == VMJIT_T_CONSTL || ( op >= OP_EQ && op <= OP_GEF ) ) {
			if ( label[ arg ] < 0 ) {
				Z_Free( h );
				return NULL;
			}
			arg = label[ arg ];
			if ( op == VMJIT_T_CONSTL )
				op = OP_CONST;
		}
		*code++ = op;
		if ( vm_opInfo[ op ] & opImm4 ) {
			*code++ = arg;
			*code++ = arg >> 8;
			*code++ = arg >> 16;
			*code++ = arg >> 24;
		} else if ( vm_opInfo[ op ] & opImm1 ) {
			*code++ = arg;
		}
	}
	return h;
}

static void
VM_JitTestVM( vm_t *vm, const vmHeader_t *h, byte *image )
{
	Com_Memset( vm, 0, sizeof( *vm ) );
	Q_strncpyz( vm->name, "selftest", sizeof( vm->name ) );
	vm->systemCall = VM_JitTestSyscall;
	vm->dataBase = image;
	vm->dataMask = VT_IMAGE - 1;
	vm->dataAlloc = VT_IMAGE + 4;
	vm->instructionCount = h->instructionCount;
	vm->instructionPointers = Z_Malloc( h->instructionCount * sizeof( *vm->instructionPointers ) );
	vm->codeLength = h->codeLength;
	vm->programStack = VT_IMAGE;
	vm->stackBottom = VT_IMAGE - PROGRAM_STACK_SIZE;
}

// Runs compiled code; -1 when it returned, else the guard that stopped it. ret is volatile so the
// test sees all 64 bits of r3, as a caller that widens the int would.
static int
VM_JitTestCall( vm_t *vm, int *args, volatile intptr_t *ret )
{
	vmjit_testcode = -1;
	vmjit_testing = qtrue;
	if ( !setjmp( vmjit_testjmp ) )
		*ret = VM_CallCompiled( vm, args );
	vmjit_testing = qfalse;
	return vmjit_testcode;
}

static qboolean
VM_JitTestOne( const vmjit_test_t *t, vmjit_slot_t *slot, byte *image, byte *saved )
{
	static vm_t ivm, jvm;
	int args[ MAX_VMMAIN_ARGS ] = { 0 };
	volatile intptr_t jret = 0;
	int iret = 0, code, i;
	qboolean ok = qfalse;
	vmHeader_t *h;

	h = VM_JitTestAssemble( t );
	if ( !h ) {
		Com_Printf( S_COLOR_YELLOW "[vmjit] self-test %s: undefined label\n", t->name );
		return qfalse;
	}
	args[ 1 ] = t->arg1;
	args[ 2 ] = t->arg2;
	args[ 3 ] = t->arg3;
	ivm.instructionPointers = jvm.instructionPointers = NULL;

	if ( !t->guard ) {
		VM_JitTestVM( &ivm, h, image );
		VM_PrepareInterpreter( &ivm, h );
		VM_JitTestImage( image );
		currentVM = &ivm;
		iret = VM_CallInterpreted( &ivm, args );
		Com_Memcpy( saved, image, VT_DATA );
	}

	VM_JitTestVM( &jvm, h, image );
	if ( !PPC_Compile( &jvm, h, slot ) ) {
		Com_Printf( S_COLOR_YELLOW "[vmjit] self-test %s: %s\n", t->name, vmjit_error );
		goto done;
	}
	VM_JitTestImage( image );
	code = VM_JitTestCall( &jvm, args, &jret );

	if ( t->guard ) {
		if ( code == VMJIT_ERR_CALL )
			ok = qtrue;
		else
			Com_Printf( S_COLOR_YELLOW "[vmjit] self-test %s: the guard did not stop it (%d)\n",
				t->name, code );
		goto done;
	}
	if ( code >= 0 ) {
		Com_Printf( S_COLOR_YELLOW "[vmjit] self-test %s: stopped by guard %d\n", t->name, code );
		goto done;
	}
	if ( jret != (intptr_t)iret ) {
		Com_Printf( S_COLOR_YELLOW "[vmjit] self-test %s: returned 0x%llx, interpreter 0x%x\n",
			t->name, (unsigned long long)jret, iret );
		goto done;
	}
	for ( i = 0; i < VT_DATA; i++ ) {
		if ( image[ i ] != saved[ i ] ) {
			Com_Printf( S_COLOR_YELLOW "[vmjit] self-test %s: data at 0x%x is 0x%02x, interpreter 0x%02x\n",
				t->name, i, image[ i ], saved[ i ] );
			goto done;
		}
	}
	ok = qtrue;

done:
	if ( ivm.instructionPointers )
		Z_Free( ivm.instructionPointers );
	if ( jvm.instructionPointers )
		Z_Free( jvm.instructionPointers );
	Z_Free( h );
	return ok;
}

// Runs once, at boot when ps3_vmjit is 2 (vm.c), else before the first compile that may run.
// A failure keeps every VM on the interpreter for the session.
qboolean
VM_JitSelfTest( void )
{
	const int total = ARRAY_LEN( vmjit_tests );
	vmjit_slot_t *slot = NULL;
	vm_t *savedVM = currentVM;
	byte *image, *saved;
	int i, passed = 0;

	if ( !VMJIT_CAN_RUN )
		return qfalse;
	if ( vmjit_selftest )
		return vmjit_selftest > 0;

	for ( i = 0; i < ARRAY_LEN( vmjit_slots ) && !slot; i++ ) {
		if ( vmjit_slots[ i ].size && !vmjit_slots[ i ].busy )
			slot = &vmjit_slots[ i ];
	}
	if ( !slot ) {
		Com_Printf( S_COLOR_YELLOW "[vmjit] self-test: no free code slot\n" );
		return qfalse;
	}

	image = Z_Malloc( VT_IMAGE + 4 );
	saved = Z_Malloc( VT_DATA );
	for ( i = 0; i < total; i++ ) {
		// logged first: a fault in generated code freezes without a word
		Com_Printf( "[vmjit] self-test %d/%d: %s\n", i + 1, total, vmjit_tests[ i ].name );
		if ( VM_JitTestOne( &vmjit_tests[ i ], slot, image, saved ) )
			passed++;
	}
	Z_Free( saved );
	Z_Free( image );
	currentVM = savedVM;

	vmjit_selftest = ( passed == total ) ? 1 : -1;
	if ( vmjit_selftest > 0 )
		Com_Printf( "[vmjit] self-test PASS %d/%d\n", passed, total );
	else
		Com_Printf( S_COLOR_YELLOW "[vmjit] self-test FAIL %d/%d, every VM stays interpreted\n", passed, total );
	return vmjit_selftest > 0;
}

// Compiles into the VM's own slot. Any failure leaves vm->compiled false, and VM_Create then
// prepares the interpreter.
void
VM_Compile( vm_t *vm, vmHeader_t *header )
{
	vmjit_slot_t *slot = NULL;
	int i, mode, startTime;

	vm->compiled = qfalse;
	mode = Cvar_VariableIntegerValue( "ps3_vmjit" );

	for ( i = 0; i < ARRAY_LEN( vmjit_slots ); i++ ) {
		if ( !Q_stricmp( vm->name, vmjit_slots[ i ].name ) )
			slot = &vmjit_slots[ i ];
	}
	if ( !slot || !slot->size || slot->busy ) {
		Com_Printf( S_COLOR_YELLOW "[vmjit] %s: no free code slot, using the interpreter\n",
			vm->name );
		return;
	}

	if ( mode >= 2 && VMJIT_CAN_RUN && !VM_JitSelfTest() ) {
		Com_Printf( S_COLOR_YELLOW "[vmjit] %s: self-test failed, using the interpreter\n", vm->name );
		return;
	}

	Com_Printf( "[vmjit] %s: compiling %d ops\n", vm->name, header->instructionCount );
	startTime = Sys_Milliseconds();

	if ( !PPC_Compile( vm, header, slot ) ) {
		Com_Printf( S_COLOR_YELLOW "[vmjit] %s: %s, using the interpreter\n",
			vm->name, vmjit_error );
		return;
	}
	slot->busy = qtrue;

	Com_Printf( "[vmjit] %s: %d ops -> %d B in %d ms (slot %d)\n", vm->name,
		header->instructionCount, vm->codeLength, Sys_Milliseconds() - startTime,
		(int)slot->size );

	if ( mode < 2 || !VMJIT_CAN_RUN ) {
		if ( mode >= 2 )
			Com_Printf( "[vmjit] %s: VMJIT_CAN_RUN is 0, dry run only\n", vm->name );
		VM_Destroy_Compiled( vm );
		vm->destroy = NULL;
		vm->compiled = qfalse;
		vm->codeLength = header->codeLength;
	}
}

int
VM_CallCompiled( vm_t *vm, int *args )
{
	int retVal;
	int *argPointer;

	vm_data_t *vm_dataAndCode = (void *)( vm->codeBase );
	int programStack = vm->programStack;
	int stackOnEntry = programStack;

	byte *image = vm->dataBase;

	currentVM = vm;

	vm->currentlyInterpreting = qtrue;

	programStack -= ( 8 + 4 * MAX_VMMAIN_ARGS );
	argPointer = (int *)&image[ programStack + 8 ];
	memcpy( argPointer, args, 4 * MAX_VMMAIN_ARGS );
	argPointer[ -1 ] = 0;
	argPointer[ -2 ] = -1;

#ifdef VM_TIMES
	struct tms start_time, stop_time;
	clock_t time_diff;

	times( &start_time );
	time_outside_vm = 0;
#endif

	/* call generated code */
	{
		int ( *entry )( void *, int, void * );
		entry = (void *)&(vm_dataAndCode->opd);
		retVal = entry( vm->codeBase, programStack, vm->dataBase );
	}

#ifdef VM_TIMES
	times( &stop_time );
	time_diff = stop_time.tms_utime - start_time.tms_utime;
	time_total_vm += time_diff - time_outside_vm;
	if ( time_diff > 100 ) {
		printf( "App clock: %ld, vm total: %ld, vm this: %ld, vm real: %ld, vm out: %ld\n"
			"Inside VM %f%% of app time\n",
			stop_time.tms_utime,
			time_total_vm,
			time_diff,
			time_diff - time_outside_vm,
			time_outside_vm,
			(double)100 * time_total_vm / stop_time.tms_utime );
	}
#endif

	vm->programStack = stackOnEntry;
	vm->currentlyInterpreting = qfalse;

	return retVal;
}

#endif // powerpc
