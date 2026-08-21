/*
 * Who owns what inside the texture arena.
 *
 * The arena is a stretch of video memory after the colour surface and the
 * space a depth buffer would take (OpenStepMGAMesaBuffer.c decides where);
 * this decides who has which part of it.  Textures are few and large -- a
 * 256 by 256 one is a quarter of a megabyte and the arena at 320x240 is two
 * and a half -- so running out is ordinary rather than exceptional, and the
 * allocator is written for a handful of blocks rather than for many.
 *
 * Blocks are kept sorted by origin and a request takes the first gap that
 * fits.  Freeing removes a block; there is nothing to coalesce, because the
 * gaps are whatever lies between neighbours and are recomputed each time.
 *
 * Everything is refused when the arena's epoch does not match the one the
 * caller was given.  A surface released and another bound can be handed the
 * same origin with different contents, so identity is a number that never
 * repeats, not a place.
 */
#include <string.h>

#include "OpenStepMGAMesaTexArena.h"

#define OSMGA_TEX_BLOCKS 32
/* Origins are kept on a sixteen-texel boundary, which is what the DDX rounds
 * its own scratch texture to.  Nothing measured requires it; it costs a few
 * bytes and takes one class of alignment question off the table. */
#define OSMGA_TEX_ALIGN  64UL

typedef struct {
    unsigned long origin;
    unsigned long bytes;
} OSMGATexBlock;

static OSMGATexBlock blocks[OSMGA_TEX_BLOCKS];
static unsigned long blockCount;
static unsigned long arenaOrigin, arenaBytes, arenaEpoch;
static int arenaLive;

void
OSMGAMesaTexArenaSet(unsigned long origin, unsigned long bytes,
                     unsigned long epoch)
{
    if (arenaLive && origin == arenaOrigin && bytes == arenaBytes &&
        epoch == arenaEpoch)
        return;
    memset(blocks, 0, sizeof blocks);
    blockCount = 0UL;
    arenaOrigin = origin;
    arenaBytes = bytes;
    arenaEpoch = epoch;
    arenaLive = (bytes != 0UL);
}

void
OSMGAMesaTexArenaDrop(void)
{
    memset(blocks, 0, sizeof blocks);
    blockCount = 0UL;
    arenaOrigin = arenaBytes = arenaEpoch = 0UL;
    arenaLive = 0;
}

unsigned long
OSMGAMesaTexArenaEpoch(void)
{
    return arenaEpoch;
}

int
OSMGAMesaTexAlloc(unsigned long bytes, unsigned long epoch,
                  unsigned long *origin)
{
    unsigned long want, at, i;

    if (origin != 0)
        *origin = 0UL;
    if (!arenaLive || origin == 0 || bytes == 0UL || epoch != arenaEpoch)
        return 0;
    if (blockCount >= (unsigned long)OSMGA_TEX_BLOCKS)
        return 0;

    want = (bytes + OSMGA_TEX_ALIGN - 1UL) & ~(OSMGA_TEX_ALIGN - 1UL);
    if (want < bytes)                   /* wrapped */
        return 0;

    /*
     * The first gap that fits.  Blocks are sorted, so the gaps are the space
     * before the first, between neighbours, and after the last.
     */
    at = arenaOrigin;
    for (i = 0UL; i < blockCount; i++) {
        if (blocks[i].origin >= at && blocks[i].origin - at >= want)
            break;
        if (blocks[i].origin + blocks[i].bytes > at)
            at = blocks[i].origin + blocks[i].bytes;
    }
    if (at < arenaOrigin || at - arenaOrigin > arenaBytes)
        return 0;
    if (arenaBytes - (at - arenaOrigin) < want)
        return 0;

    /* keep them sorted */
    for (i = blockCount; i > 0UL; i--) {
        if (blocks[i - 1UL].origin < at)
            break;
        blocks[i] = blocks[i - 1UL];
    }
    blocks[i].origin = at;
    blocks[i].bytes = want;
    blockCount++;
    *origin = at;
    return 1;
}

int
OSMGAMesaTexFree(unsigned long origin, unsigned long epoch)
{
    unsigned long i;

    if (!arenaLive || epoch != arenaEpoch)
        return 0;
    for (i = 0UL; i < blockCount; i++)
        if (blocks[i].origin == origin) {
            for (; i + 1UL < blockCount; i++)
                blocks[i] = blocks[i + 1UL];
            blockCount--;
            blocks[blockCount].origin = 0UL;
            blocks[blockCount].bytes = 0UL;
            return 1;
        }
    return 0;                           /* not ours, or freed already */
}

void
OSMGAMesaTexArenaStat(unsigned long *count, unsigned long *used)
{
    unsigned long i, n = 0UL;

    for (i = 0UL; i < blockCount; i++)
        n += blocks[i].bytes;
    if (count != 0) *count = blockCount;
    if (used != 0)  *used = n;
}
