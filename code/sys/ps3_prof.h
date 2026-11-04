/* ps3_prof.h -- ps3_prof 1 frame probe buckets, printed by VM_ProfFrame (vm.c).
 * One global struct, so each TU that uses it spends one 8 B TOC entry. */
#ifndef PS3_PROF_H
#define PS3_PROF_H

#include <ppu-asm.h>

/* Sums since the last print: timebase ticks, then the three counts. */
typedef struct {
	int		on;
	unsigned long long	rf, bk, gpu, idle, vc, cp, dl;
	unsigned long long	surf, draws, verts;
} ps3prof_t;

extern ps3prof_t ps3prof;

/* __gettime() re-reads while the low word is 0, so it never returns 0: t == 0 means off. */
#define PS3_PROF_START( t )			unsigned long long t = ps3prof.on ? __gettime() : 0
#define PS3_PROF_STOP( t, bucket )	do { if ( t ) ps3prof.bucket += __gettime() - ( t ); } while ( 0 )
#define PS3_PROF_COUNT( counter, n )	do { if ( ps3prof.on ) ps3prof.counter += ( n ); } while ( 0 )

#endif
