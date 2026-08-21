#ifndef OPENSTEP_MGA_MESA_TEX_ARENA_H
#define OPENSTEP_MGA_MESA_TEX_ARENA_H

/*
 * Who owns what inside the texture arena.  See the .c for why identity is an
 * epoch and not an origin.
 */
void OSMGAMesaTexArenaSet(unsigned long origin, unsigned long bytes,
                          unsigned long epoch);
void OSMGAMesaTexArenaDrop(void);
unsigned long OSMGAMesaTexArenaEpoch(void);

/* Zero on failure, which includes a stale epoch and a full arena. */
int OSMGAMesaTexAlloc(unsigned long bytes, unsigned long epoch,
                      unsigned long *origin);
/* Zero when the block is not ours, which covers a double free. */
int OSMGAMesaTexFree(unsigned long origin, unsigned long epoch);

void OSMGAMesaTexArenaStat(unsigned long *count, unsigned long *used);

#endif /* OPENSTEP_MGA_MESA_TEX_ARENA_H */
