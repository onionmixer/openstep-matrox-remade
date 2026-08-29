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
    /*
     * The texture coordinate's OWN homogeneous divisor, from glTexCoord4f or
     * from a projective texture matrix -- 1 when there is neither.
     *
     * GL gives the fragment s/q, and what is linear in screen space is s/w
     * and q/w, so the engine's numerator carries s*(1/w) and its denominator
     * q*(1/w).  Mesa's own rasteriser builds exactly those two
     * (Mesa-3.4.2/src/tritemp.h: the numerator from s * invW, the divisor
     * from the fourth coordinate * invW when the vector has one and from
     * invW alone when it has not).
     *
     * It multiplies the DENOMINATOR only.  Folding it into qw would multiply
     * the numerator with it and the whole thing would cancel back to s.
     */
    double tq;
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
 * The blend factors, as the engine numbers them.
 *
 * Nine for the source and eight for the destination, and they are GL 1.1's
 * own two sets for those two roles -- the source may take DST_COLOR and the
 * destination SRC_COLOR, never the other way round (mgareg_flags.h, AC_src_*
 * and AC_dst_*).
 *
 * These are NOT derived from the GLenum values.  GL numbers SRC_ALPHA 0x302
 * and DST_COLOR 0x306, in no relation to the field; the mapping is written
 * out as two switches so that a transposed pair is a compile-time thing to
 * look at rather than an arithmetic accident.
 *
 * Everything outside bits 0-7 is left alone by the mapping: bits 8-9 carry
 * the alpha mode and 24-25 the selector this back end sets for textured
 * blending, and a mapping that wrote only the factors would clear both.
 */
/*
 * The alpha test, which lives in the same word as the blend.
 *
 * AC_aten is bit 12, AC_atmode bits 13-15 and AC_atref bits 16-23, and the
 * modes are the same shape as the depth comparisons -- seven named and value
 * one unnamed and therefore not offered.
 *
 * The engine compares the TEXTURE STAGE's alpha, which is the same value the
 * blend selector reads: measured under REPLACE by sweeping the reference and
 * finding the threshold at the texel's own alpha from both directions, then
 * separated from the texel under MODULATE, where the stage's product and the
 * texel part company (probe section 87).
 *
 * A fragment the test discards writes no depth either, which is the order GL
 * asks for, and that was measured with a passing control beside it.
 */
#define OSMGA_MESA_ATEST_MASK   0x00FFF000UL   /* aten, atmode and atref */
#define OSMGA_MESA_AT_ENABLE    0x00001000UL
#define OSMGA_MESA_AT_E         0x00004000UL
#define OSMGA_MESA_AT_NE        0x00006000UL
#define OSMGA_MESA_AT_LT        0x00008000UL
#define OSMGA_MESA_AT_LTE       0x0000A000UL
#define OSMGA_MESA_AT_GT        0x0000C000UL
#define OSMGA_MESA_AT_GTE       0x0000E000UL
#define OSMGA_MESA_AT_REF_SHIFT 16

#define OSMGA_MESA_BLEND_FACTOR_MASK 0x000000FFUL
#define OSMGA_MESA_BF_ZERO       0UL
#define OSMGA_MESA_BF_ONE        1UL
#define OSMGA_MESA_BF_OTHER_C    2UL   /* DST_COLOR for src, SRC_COLOR for dst */
#define OSMGA_MESA_BF_OM_OTHER_C 3UL
#define OSMGA_MESA_BF_SRC_A      4UL
#define OSMGA_MESA_BF_OM_SRC_A   5UL
#define OSMGA_MESA_BF_DST_A      6UL
#define OSMGA_MESA_BF_OM_DST_A   7UL
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

/*
 * The depth comparison, or NONE for no depth at all.
 *
 * The engine's field is DWGCTL bits 8-10 and the register documentation names
 * seven of its eight values (mgareg_flags.h, DC_zmode_*); value 1 has no name
 * and is not offered.  NOZCMP is "always" and NOT "the depth stage is off" --
 * measured, probe section 85: cleared to 8000, drawn at 4000, read back 4000,
 * with a ZLT control beside it reading the same.
 *
 * NONE is 0xFFFFFFFF because it must NOT be a register value.  It used to be
 * nought, which is also NOZCMP, so "is there depth" and "which comparison"
 * were the same question and GL_ALWAYS could not be asked for at all -- the
 * builder read it as no depth, dropped the access type down to I, and lost
 * the write as well as the test.
 */
#define OSMGA_MESA_ZMODE_NONE     0xFFFFFFFFUL
#define OSMGA_MESA_ZMODE_MASK     0x700UL
#define OSMGA_MESA_ZMODE_ALWAYS   0x000UL   /* nozcmp */
#define OSMGA_MESA_ZMODE_E        0x200UL   /* ze     */
#define OSMGA_MESA_ZMODE_NE       0x300UL   /* zne    */
#define OSMGA_MESA_ZMODE_LT       0x400UL   /* zlt    */
#define OSMGA_MESA_ZMODE_LTE      0x500UL   /* zlte   */
#define OSMGA_MESA_ZMODE_GT       0x600UL   /* zgt    */
#define OSMGA_MESA_ZMODE_GTE      0x700UL   /* zgte   */

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


/*
 * depthWrite is glDepthMask, and it is a separate argument rather than a
 * value folded into zmode because a caller that forgot it would be asking
 * for depth writes it did not want, silently.
 *
 * The engine spells it in the access type, not in a write mask.  Matrox's
 * own register decoder calls atype ZI "depth mode with gouraud" and atype I
 * "Gouraud (with depth compare)" (xf86-video-mga-2.0.0/util/stormdwg.c:32
 * and :35), and the probe asked the hardware directly: with atype I and
 * ZLT against a depth buffer cleared to 0x8000, the band at 0x4000 drew all
 * 1280 of its pixels, the band at 0xC000 drew none, and not one pixel of
 * depth moved -- while the ZI control in the same run wrote every depth it
 * was asked to.  So I compares and does not write.
 *
 * It is ignored when zmode is NONE: with no depth there is nothing to
 * write, and the access type is I either way.
 */
/*
 * DWGCTL from the drawing state.  Both tiers need it -- the trapezoid
 * carries it per primitive, and the WARP path uses it as a run key that
 * the hook must know before it chooses a tier.
 */
unsigned long OSMGAMesaDwgctl(unsigned long zmode, int depthWrite,
                              int textured);

int OSMGAMesaBuildTriangle(const OSMGAMesaVertex *a,
                           const OSMGAMesaVertex *b,
                           const OSMGAMesaVertex *c,
                           const OSMGAMesaVertex *flat,
                           unsigned long zmode,
                           int depthWrite,
                           unsigned long blend,
                           double zoffset,
                           OSMGAHW3DTri *out);

/*
 * The same, with a texture.
 *
 * tmrOut receives the GRADIENTS, which every trapezoid of the triangle
 * shares, and slot eight is the answer to "was this a perspective solve"
 * rather than a value.  The ANCHORS go straight into out[]: they are the
 * trapezoid's, because the engine re-seeds at every primitive from that
 * primitive's own first-row left edge -- measured -- so the two halves of a
 * split triangle need different starts.
 *
 * The batch used to hold the anchors, which meant a trapezoid per batch and
 * a refused second half arriving after the first was drawn.  It does not any
 * more, so a whole triangle goes out at once and a refusal draws none of
 * it.
 *
 * tex == 0 is exactly OSMGAMesaBuildTriangle.
 */
/*
 * zoffset is glPolygonOffset's, in DEPTH CODES, and it is a parameter rather
 * than something with a convenient default because a caller that forgets it
 * must not compile.
 *
 * The number is the CALLER's because it has to be Mesa's.  Mesa forms it from
 * the polygon's window-space plane -- max(|dz/dx|, |dz/dy|) * factor + units
 * -- before anything is snapped to a fraction of a pixel and before any
 * sliver flattening, and all three of those matter:
 *
 *   the units, because this builder divides the vertex depth by the same 256
 *   it was multiplied by, so its plane is already in codes and converting
 *   again would be 256 times wrong;
 *
 *   the flattening, because the builder zeroes both derivatives for a sliver
 *   while Mesa's slope there is not zero but huge;
 *
 *   the degeneracy guard, because Mesa's is on unsnapped values and this
 *   builder's area test is on snapped ones, so a near-degenerate triangle
 *   could take an offset in one path and not in the other.
 *
 * Computing it from Mesa's own numbers makes the two exact by construction
 * rather than by reproduction.  Nought means no offset.
 *
 * A plane the offset pushes outside the representable depth is refused --
 * OSMGA_MESA_TRI_UNSUPPORTED -- and not clamped: out there Mesa keeps a
 * 32-bit depth and compares that, while this builder saturates every
 * trapezoid's seed, and the two draw different pictures.
 */
int OSMGAMesaBuildTriangleTex(const OSMGAMesaVertex *a,
                              const OSMGAMesaVertex *b,
                              const OSMGAMesaVertex *c,
                              const OSMGAMesaVertex *flat,
                              unsigned long zmode,
                              int depthWrite,
                              unsigned long blend,
                              const OSMGAMesaTex *tex,
                              double zoffset,
                              OSMGAHW3DTri *out,
                              long tmrOut[][9]);

/*
 * Triangles refused because a WALKED edge lost its height at the subpixel
 * resolution in force -- which would otherwise divide by zero in the edge
 * registers, and hang the row walk if that division were guarded instead.
 * Counted apart from the builder's other refusals because each one flushes
 * the pending batch before the software redraw.
 */
unsigned long OSMGAMesaEdgeVanished(void);

#endif /* OPENSTEP_MGA_MESA_TRIANGLE_H */
