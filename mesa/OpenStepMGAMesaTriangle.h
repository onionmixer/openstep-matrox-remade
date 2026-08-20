/*
 * OpenStepMGAMesaTriangle.h - M1-3b: three vertices into engine trapezoids.
 *
 * The engine draws trapezoids, not triangles: a run of rows with a left and
 * a right edge, each walking at a fixed rate.  A triangle is at most two of
 * them, split at the middle vertex.  This file does that arithmetic and
 * nothing else -- no Mesa headers, no device, no state -- so that it can be
 * checked against hand-computed shapes without a GL context in the way.
 *
 * Edge rates are given as a displacement over the whole trapezoid together
 * with its height, so a fractional slope needs no fixed-point encoding.  That
 * is measured, not assumed: a triangle whose left edge is given 40 over 20
 * rows moved two pixels per row, which is what the hardware drew.
 */

#ifndef OPENSTEP_MGA_MESA_TRIANGLE_H
#define OPENSTEP_MGA_MESA_TRIANGLE_H

#include "../hw3d/OpenStepMGAHW3D.h"

typedef struct {
    long x, y;                  /* pixels, in the destination's own space */
    unsigned long r, g, b;      /* 0..255 */
} OSMGAMesaVertex;

/*
 * Fills up to two trapezoids and returns how many were written -- 0 for a
 * triangle with no rows in it, which is not an error and must simply not be
 * drawn.  `out` must have room for two.
 *
 * Colour comes from `flat`: its r, g and b are used for the whole triangle.
 * Interpolated colour is a later step; passing the provoking vertex here
 * gives the flat-shaded case exactly.
 */
int OSMGAMesaBuildTriangle(const OSMGAMesaVertex *a,
                           const OSMGAMesaVertex *b,
                           const OSMGAMesaVertex *c,
                           const OSMGAMesaVertex *flat,
                           OSMGAHW3DTri *out);

#endif /* OPENSTEP_MGA_MESA_TRIANGLE_H */
