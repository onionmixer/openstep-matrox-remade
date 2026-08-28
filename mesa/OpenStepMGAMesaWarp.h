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
 */
int OSMGAMesaBuildWarpVertex(const OSMGAMesaVertex *v,
                             const OSMGAMesaTex *tex,
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

#endif
