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
    /*
     * The reciprocal of w, straight from Mesa's Win[3].  It is 1 for an
     * orthographic or 2D vertex and the perspective path uses it to weight
     * the texture coordinates; a value at or below nought is refused before
     * it reaches here.
     */
    double qw;
    unsigned long r, g, b, a;   /* 0..255 */
    /*
     * Depth, also fixed point in 1/256, so 0..65535*256.  Ignored unless a
     * depth mode is asked for.
     *
     * It carries its fraction for the same reason x and y do -- throwing it
     * away was worth four tenths of a depth code -- but unlike them it costs
     * nothing to carry: coordinates go through the edge walk and are held to
     * the AR field, while depth goes straight to its own registers as a
     * converted double.  There is no precision to choose here.
     */
    unsigned long z;            /* 1/256 of a depth code */
    /*
     * Texture coordinates, NORMALISED -- s and t as GL gives them, not
     * multiplied by the texture's size.
     *
     * Texel coordinates were the first choice and are wrong: they are
     * dimension-dependent, so the same vertex means a different place when
     * the bound texture changes size, and everything built from it would
     * have to be built again.  The size is batch state; it reaches the
     * builder as an argument instead.
     *
     * Ignored unless a texture is passed.
     */
    double s, tc;
} OSMGAMesaVertex;

/*
 * The bound texture, as far as the builder needs it: the coordinate scale
 * follows the size, and nothing else here does.
 *
 * The scale is NOT 2^20 divided by the size.  The engine's log2 field names
 * the power of two that CONTAINS the texture and the exact size travels
 * separately, so one texel is 1 << (20 - ceil(log2 size)) and a texture that
 * is not a power of two spans less than the whole coordinate range.
 */
typedef struct {
    unsigned long w, h;         /* texels */
} OSMGAMesaTex;

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
/*
 * Which alpha the blend consumes, in bits 24 and 25 -- AC_alphasel.  OVER
 * above carries "diffused", the interpolated alpha, which is right while
 * nothing is textured and wrong the moment something is: GL wants the
 * texture's alpha under GL_REPLACE on an RGBA texture, and the product of
 * the two under GL_MODULATE.
 *
 * All three were measured on the machine at four texture alphas with a
 * fragment alpha the texture does not carry: twelve readings, every one
 * matching what python says the engine's blend must give.  The repository
 * had predicted the divergence before a texture was ever bound
 * (docs/D3_5_ALPHA_BLEND_PLAN.md).
 */
#define OSMGA_MESA_ALPHASEL_MASK 0x03000000UL
#define OSMGA_MESA_ALPHASEL_TEX  0x00000000UL
#define OSMGA_MESA_ALPHASEL_DIFF 0x01000000UL
#define OSMGA_MESA_ALPHASEL_MOD  0x02000000UL

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

/*
 * The same, with a texture.
 *
 * tmrOut receives ONE SET OF TMR VALUES PER TRAPEZOID, not one per triangle.
 * The engine re-seeds the horizontal coordinate at every primitive, at that
 * primitive's own first-row left edge -- measured -- so the two halves of a
 * split triangle need different starts.  The batch protocol has only one
 * tmr[] for the whole batch, so the caller has to put each trapezoid in a
 * batch of its own until that changes; the builder's job is to say what each
 * one needs.
 *
 * tex == 0 is exactly OSMGAMesaBuildTriangle.
 */
int OSMGAMesaBuildTriangleTex(const OSMGAMesaVertex *a,
                              const OSMGAMesaVertex *b,
                              const OSMGAMesaVertex *c,
                              const OSMGAMesaVertex *flat,
                              unsigned long zmode,
                              unsigned long blend,
                              const OSMGAMesaTex *tex,
                              OSMGAHW3DTri *out,
                              long tmrOut[][9]);

#endif /* OPENSTEP_MGA_MESA_TRIANGLE_H */
