/*
 * OpenStepMGAMesaWarp.h -- turning a Mesa vertex into a WARP vertex.
 *
 * This is the userland half of the version 10 contract, and it is where
 * all the arithmetic lives: the kernel has no FPU, so it only ever judges
 * the bit patterns this file produces.
 *
 * It builds vertices.  It does not decide which primitives go to the WARP
 * tier, does not touch batch state, and does not submit -- so a refusal
 * here costs nothing and the caller simply builds a trapezoid instead,
 * which is the tier discipline the rest of this back end already follows.
 */
#ifndef OPENSTEP_MGA_MESA_WARP_H
#define OPENSTEP_MGA_MESA_WARP_H

#include "OpenStepMGAMesaTriangle.h"
#include "OpenStepMGAHW3D.h"

/*
 * One vertex.  Returns 0, or OSMGA_MESA_TRI_UNSUPPORTED when the result
 * would be something the kernel refuses -- which is checked HERE, on the
 * converted floats, rather than inferred from the doubles that produced
 * them: a double well inside the range can convert to an infinity, and
 * the kernel sees only the float.
 *
 * `tex` may be null, and then the texture coordinates come out as nought
 * and are not examined.
 *
 * `zoffset` is glPolygonOffset's shift IN DEPTH CODES, which is what the
 * hook computes from Mesa's own expression.  It is a required argument and
 * not an optional one: the trapezoid builder takes it the same way, and a
 * builder that silently drew every offset triangle unoffset is a mistake
 * this back end has already made once and measured (16384 in the depth
 * buffer where software left 17408).
 *
 * A shifted vertex that leaves [0,1] is REFUSED, not clamped.  The
 * trapezoid path says why: out there Mesa keeps a 32-bit depth and this
 * path saturates into 0..65535, so clamping would draw something neither
 * path draws.  The plane is linear, so its extremes over the triangle are
 * at the three vertices -- refusing per vertex is refusing the plane.
 */
int OSMGAMesaBuildWarpVertex(const OSMGAMesaVertex *v,
                             const OSMGAMesaTex *tex,
                             double zoffset,
                             OSMGAHW3DVertex *out);

/*
 * The depth conversion, named here because it is the one number in this
 * file that was measured rather than derived.
 *
 * The vertex carries 1/256 of a depth code, so 0 .. 65535*256.  The engine
 * multiplies a normalised z by 65536 and saturates -- measured, M4 T2 and
 * T4c -- so
 *
 *      z = code / 65536.0 = vertex.z / 16777216.0
 *
 * and the round trip is exact for all 65536 codes: the largest, 65535,
 * comes back as 65535 rather than saturating.  The reference DRI's
 * 1.0/0xffff (mga_xmesa.c:362) is the wrong constant.
 */
#define OSMGA_MESA_WARP_ZSCALE  16777216.0

/*
 * Half a pixel, in the vertex's own 1/256 units, subtracted from x and y.
 *
 * Handed the coordinates Mesa computes, this tier draws the picture the
 * rational oracle draws for vertices at V + 1/2.  That is measured -- a
 * 512 triangle mesh where the trapezoid tier scores (0, 0, 0) and this
 * one scored (462, 463, 7707), and where rescoring the SAME observation
 * over a joint grid of offsets puts the minimum at exactly (+1/2, +1/2)
 * and at no neighbour of it.  So the vertices go out half a pixel low and
 * the two tiers draw the same picture.
 *
 * A coordinate contract, not a mechanism: corner sampling would produce
 * this, and so would a bias inside the microcode or a setup origin half a
 * pixel off.  A translation experiment cannot separate those, and the
 * compensation is the same for all of them.
 *
 * It belongs HERE and not in the kernel.  The kernel contains no floating
 * point -- a double clip box was removed for exactly that rule -- and
 * cannot subtract a half from an IEEE float.  In 1/256ths it is an
 * integer, applied before the one conversion this file makes.
 */
#define OSMGA_MESA_WARP_XY_BIAS 128L

#endif

/*
 * ---- assembling a batch ----
 *
 * The trapezoid contract carries dwgctl and alphactrl per triangle.  WARP
 * cannot: everything in one submission shares its state, so a batch is cut
 * into RUNS and a fresh state list goes out at each boundary.
 *
 * The assembler's whole job is to keep the invariant the kernel validator
 * insists on -- that the runs PARTITION the vertices, contiguous and in
 * order, covering every one -- so that a batch this builds is a batch the
 * kernel accepts.  A gap would leave vertices the client believes were
 * drawn; an overlap would draw a primitive twice under two states.
 *
 * It never refuses a triangle for being wrong; the vertices were already
 * judged when they were built.  It refuses only for being FULL, and the
 * caller's answer to that is to submit what it has and start another.
 */
#define OSMGA_MESA_WARP_FULL  (-2)

typedef struct {
    OSMGAHW3DWarpBatch *b;
    int                 open;      /* a run is being appended to */
    /*
     * The capacities this batch will actually use.
     *
     * Reset puts the real ones here, and a caller may lower them
     * afterwards -- never raise them, since Reset has already clamped.
     * That is the whole of the mechanism: the full condition, the flush,
     * the reset and the retry are the production ones, and only the
     * threshold moved.
     *
     * It exists because the full path cannot otherwise run.  Mesa's
     * immediate buffer flushes at VB_MAX = 216 + VB_START vertices, so a
     * batch never exceeds seventy-two triangles and the largest measured
     * is sixty-four -- against a capacity of two hundred and forty.
     */
    unsigned long       vtxCap;
    unsigned long       runCap;
} OSMGAMesaWarpBuilder;

void OSMGAMesaWarpReset(OSMGAMesaWarpBuilder *w, OSMGAHW3DWarpBatch *b);
/*
 * Lower this batch's capacities, after Reset.  A value of nought, or one
 * above the real capacity, leaves that dimension alone -- so this can only
 * ever make a batch smaller, never let one overrun the buffer.
 */
void OSMGAMesaWarpCapacity(OSMGAMesaWarpBuilder *w,
                           unsigned long vtx, unsigned long runs);

/*
 * Append one triangle under (dwgctl, alphactrl).  Returns 0, or
 * OSMGA_MESA_WARP_FULL when this batch cannot take it -- which is not an
 * error and does not disturb what the batch already holds.
 */
int OSMGAMesaWarpAdd(OSMGAMesaWarpBuilder *w,
                     unsigned long dwgctl, unsigned long alphactrl,
                     const OSMGAHW3DVertex *v0,
                     const OSMGAHW3DVertex *v1,
                     const OSMGAHW3DVertex *v2);
