/*
 * Who owns what inside the texture arena.
 *
 * The arena is a stretch of video memory after the colour surface and the
 * space a depth buffer would take (OpenStepMGAMesaBuffer.c decides where);
 * this decides who has which part of it.
 *
 * THIS USED TO SAY textures are few and large, and the allocator was written
 * for a handful of blocks.  That was true of the demos it was written for --
 * a 256 by 256 texture is a quarter of a megabyte -- and it is not true of a
 * game.  Quake's textures are MANY AND SMALL: one level references up to 65
 * distinct world textures of 64 by 64 and below, packs its lightmaps into a
 * couple of dozen sheets, and never frees a texture between levels, so a
 * session accumulates.  Counted from the data: 133 blocks for the worst
 * single level, 344 across a whole playthrough.
 *
 * So the block count is no longer a handful.  What did not change is the
 * shape: blocks stay sorted by origin and a request still takes the first
 * gap that fits, which is O(n) to scan and O(n) to insert.  At the bound
 * below that is 0.60 ms for a level's worth of allocations on this machine
 * -- less than one frame -- so the simple allocator is kept rather than
 * replaced with something that would need its own correctness argument.
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

/*
 * 384: above both numbers measured from Quake's data (133 for the worst
 * single level, 344 for a playthrough) with room left over, and the array
 * it sizes is 8 bytes a block -- three kilobytes in total.
 *
 * It was 32, which Q2-0 measured running out at the thirtieth texture while
 * the smallest level needs 27 world textures before a single lightmap.
 */
#define OSMGA_TEX_BLOCKS 384
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
