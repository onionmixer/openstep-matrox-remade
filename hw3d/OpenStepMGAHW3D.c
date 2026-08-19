/*
 * OpenStepMGAHW3D.c -- validation of a userland batch.  See the header.
 *
 * Every bound is written so no expression can overflow: comparisons are
 * arranged as `a > limit - b` rather than `a + b > limit`, because the
 * second wraps and the first does not.  The same discipline the S4a mmap
 * handler uses, and for the same reason -- being wrong here hands the
 * engine an address of the client's choosing.
 */
#include "OpenStepMGAHW3D.h"

#define OSMGA_HW3D_DEPTH_BYTES  2UL    /* MACCESS leaves depth 16-bit */

/* Does [org, org + rows*stride) fit inside [lo, hi)? */
static int
osmgaHW3DReach(unsigned long org, unsigned long rows, unsigned long stride,
               unsigned long lo, unsigned long hi)
{
    unsigned long span;

    if (hi <= lo || org < lo || org >= hi)
        return 0;
    if (stride != 0UL && rows > 0xFFFFFFFFUL / stride)
        return 0;
    span = rows * stride;
    if (span > hi - org)
        return 0;
    return 1;
}

int
osmgaHW3DValidate(const OSMGAHW3DBatch *b, const OSMGAHW3DLimits *lim,
                  unsigned long *badTri)
{
    unsigned long i, rows, opcode, atype;

    if (badTri != 0)
        *badTri = 0UL;
    if (b == 0 || lim == 0)
        return OSMGA_HW3D_E_MAGIC;
    if (b->magic != OSMGA_HW3D_MAGIC)
        return OSMGA_HW3D_E_MAGIC;
    if (b->version != OSMGA_HW3D_VERSION)
        return OSMGA_HW3D_E_VERSION;

    /* The count is checked against both the compiled-in array and the
     * buffer the client was actually given, because the two can differ if
     * the window is ever resized. */
    if (b->triCount > OSMGA_HW3D_MAX_TRI)
        return OSMGA_HW3D_E_COUNT;
    if (lim->batchBytes < sizeof(OSMGAHW3DBatch) -
                          sizeof(OSMGAHW3DTri) * OSMGA_HW3D_MAX_TRI)
        return OSMGA_HW3D_E_COUNT;
    {
        unsigned long fixed = sizeof(OSMGAHW3DBatch) -
                              sizeof(OSMGAHW3DTri) * OSMGA_HW3D_MAX_TRI;
        unsigned long room = (lim->batchBytes - fixed) / sizeof(OSMGAHW3DTri);

        if (b->triCount > room)
            return OSMGA_HW3D_E_COUNT;
    }

    rows = lim->clipY1 + 1UL;

    /* Colour: the kernel's clip bounds the rows, so the origin plus that
     * many rows must stay inside the window we own. */
    if (!osmgaHW3DReach(b->state.dstorg, rows, lim->pitchBytes,
                        lim->colourStart, lim->colourEnd))
        return OSMGA_HW3D_E_DSTORG;

    opcode = b->state.dwgctl & 0x0000000FUL;
    atype  = (b->state.dwgctl >> 4) & 0x7UL;

    /* Only the two trapezoid opcodes, and only the two interpolated access
     * types.  BITBLT and ILOAD address their source through AR registers we
     * do not bound, and the linear bit turns x and y into a flat offset,
     * which would step straight past the clip. */
    if ((opcode != 0x4UL && opcode != 0x6UL) ||
        (atype != 0x7UL && atype != 0x3UL) ||
        (b->state.dwgctl & 0x00000080UL) != 0UL)
        return OSMGA_HW3D_E_DWGCTL;

    /* Depth is only addressed when the access type says so. */
    if (atype == 0x3UL) {
        unsigned long zstride = (lim->pitchBytes / 4UL) * OSMGA_HW3D_DEPTH_BYTES;

        if (!osmgaHW3DReach(b->state.zorg, rows, zstride,
                            lim->depthStart, lim->depthEnd))
            return OSMGA_HW3D_E_ZORG;
    }

    /* Texture is only fetched by the textured opcode. */
    if (opcode == 0x6UL) {
        if (!osmgaHW3DReach(b->state.texorg, 1UL, lim->texMaxBytes,
                            lim->texStart, lim->texEnd))
            return OSMGA_HW3D_E_TEXORG;
    }

    for (i = 0UL; i < b->triCount; i++) {
        const OSMGAHW3DTri *t = &b->tri[i];

        if (badTri != 0)
            *badTri = i;
        if (t->y < 0L || t->h <= 0L)
            return OSMGA_HW3D_E_TRIROW;
        if ((unsigned long)t->y > lim->clipY1)
            return OSMGA_HW3D_E_TRIROW;
        if ((unsigned long)t->h > lim->clipY1 + 1UL - (unsigned long)t->y)
            return OSMGA_HW3D_E_TRIROW;
        {   /* How far the two edges can travel over this triangle.  ar1
             * and ar4 carry the same slope with the error term folded in,
             * so take whichever is larger of each pair. */
            unsigned long h = (unsigned long)t->h;
            unsigned long sl = (t->ar2 < 0L) ? (unsigned long)(-t->ar2)
                                             : (unsigned long)t->ar2;
            unsigned long s1 = (t->ar1 < 0L) ? (unsigned long)(-t->ar1)
                                             : (unsigned long)t->ar1;
            unsigned long sr = (t->ar5 < 0L) ? (unsigned long)(-t->ar5)
                                             : (unsigned long)t->ar5;
            unsigned long s4 = (t->ar4 < 0L) ? (unsigned long)(-t->ar4)
                                             : (unsigned long)t->ar4;

            if (s1 > sl) sl = s1;
            if (s4 > sr) sr = s4;
            if (sr > sl) sl = sr;
            if (h != 0UL && sl > lim->maxEdgeWalk / h)
                return OSMGA_HW3D_E_TRISLOPE;
        }
        {
            unsigned long left  = t->fxbndry & 0xFFFFUL;
            unsigned long right = (t->fxbndry >> 16) & 0xFFFFUL;

            if (left > lim->clipX1 + 1UL || right > lim->clipX1 + 1UL ||
                left > right)
                return OSMGA_HW3D_E_TRICOL;
        }
    }
    if (badTri != 0)
        *badTri = 0UL;
    return OSMGA_HW3D_OK;
}
