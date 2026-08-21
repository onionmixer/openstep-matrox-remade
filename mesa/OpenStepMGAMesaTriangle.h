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

/*
 * Vertex coordinates are FIXED POINT, in units of 1/256 of a pixel.
 *
 * They used to be whole pixels, and the fraction was thrown away on the way
 * in.  Measured, that cost five per cent of a triangle's area on fractional
 * geometry -- and worse, float noise on a coordinate meant to be integral
 * could drop a whole row.  Carrying the fraction here lets the back end
 * choose, per triangle, how much of it the engine's registers can hold.
 *
 * The model this is transcribed from is spec/subpixel-model.py, which is
 * checked against the geometric rule and against the old integer behaviour.
 */
#define OSMGA_MESA_SUBBITS  8L
#define OSMGA_MESA_SUBONE   (1L << OSMGA_MESA_SUBBITS)

typedef struct {
    long x, y;                  /* 1/256 pixel, in the destination's space */
    unsigned long r, g, b, a;   /* 0..255 */
    unsigned long z;            /* 0..65535; ignored unless a z mode is asked */
} OSMGAMesaVertex;

/*
 * Depth comparison, in the engine's own encoding, or NONE to draw without
 * depth at all.  These are the values the drawing-control register takes,
 * passed through rather than translated, because the caller is choosing an
 * engine behaviour and inventing a second vocabulary for it would only make
 * two things to keep in step.
 */
/*
 * Blending, likewise in the engine's own encoding.  OPAQUE is what every
 * triangle has used so far -- source times one, destination times nothing.
 * OVER is source alpha against one minus it, which is the only blend the
 * engine performs and therefore the only one worth naming.
 */
#define OSMGA_MESA_BLEND_OPAQUE  0x00000101UL
#define OSMGA_MESA_BLEND_OVER    0x01000154UL

#define OSMGA_MESA_ZMODE_NONE  0UL
#define OSMGA_MESA_ZMODE_LT    0x400UL
#define OSMGA_MESA_ZMODE_GTE   0x700UL

/*
 * Fills up to two trapezoids and returns how many were written -- 0 for a
 * triangle with no rows in it, which is not an error and must simply not be
 * drawn.  `out` must have room for two.
 *
 * Colour: pass the provoking vertex as `flat` for flat shading, or NULL to
 * interpolate across the three.  A degenerate triangle -- three vertices on
 * one line -- has no plane to interpolate over, so it falls back to the
 * colour of `a` rather than dividing by zero.
 *
 * The gradients count from the primitive's own first pixel, not from the
 * destination's corner.  Measured: a trapezoid placed at column 16, row 8,
 * with red rising 255 over 64 columns, read 0 at (16,8), 63 at (32,8) and
 * 187 at (63,8) -- which is the primitive origin exactly, and not the
 * destination origin's 63, 127, 251.
 */
/*
 * What the builder returns.
 *
 *   > 0   that many trapezoids were written
 *     0   nothing to draw -- no area, and not an error
 *   < 0   this triangle is outside what the back end can express, and the
 *         caller must draw it some other way rather than drop it
 */
#define OSMGA_MESA_TRI_UNSUPPORTED  (-1)

int OSMGAMesaBuildTriangle(const OSMGAMesaVertex *a,
                           const OSMGAMesaVertex *b,
                           const OSMGAMesaVertex *c,
                           const OSMGAMesaVertex *flat,
                           unsigned long zmode,
                           unsigned long blend,
                           OSMGAHW3DTri *out);

#endif /* OPENSTEP_MGA_MESA_TRIANGLE_H */
