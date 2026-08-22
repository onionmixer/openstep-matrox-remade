/*
 * OpenStepMGAMesaHook.c - see the header.
 *
 * The shape of this file follows osmesa.c's own: a chooser that returns a
 * function or NULL, and a triangle function that reads the vertex buffer.
 * Deviating from that would mean guessing at conventions the software driver
 * already demonstrates.
 */

#include <math.h>
#include "glheader.h"
#include "context.h"
#include "types.h"
#include "vb.h"
/* gl_set_triangle_function -- Mesa's own chooser, called deliberately just
 * before ours is installed so that what it picks can be the way back. */
#include "triangle.h"

#include "OpenStepMGAMesaProbe.h"
#include "OpenStepMGAMesaTriangle.h"
#include "OpenStepMGAMesaBuffer.h"
#include "OpenStepMGAMesaTexture.h"
#include "OpenStepMGAMesaHook.h"


static unsigned long hookDrawn;
static unsigned long hookDeclined;
/* Triangles this back end could not draw and handed to the software path. */
static unsigned long hookSoftware;
/* Consecutive refusals that never reached the engine. */
static unsigned long hookRefusedRun;
/* Counted apart, because "this back end cannot express it" and "the kernel
 * refused the batch" are different things to have to fix. */
static unsigned long hookUnsupported;
/*
 * How the chooser answered, counted where the chooser answers.
 *
 * The five counters above all live inside the triangle function, so they can
 * only speak for triangles that reached it.  A state the chooser refuses
 * never reaches it: the refusal is a NULL return and the software function
 * Mesa already installed simply stays.  A frame that really was half
 * accelerated therefore reported "drawn=N software=0 declined=0", which
 * reads as "nothing fell back" and is the opposite of what happened.
 *
 * These two are selection counts, not triangle counts -- the state may be
 * updated more than once between primitives, and once for none.
 */
/* Textured triangles the affine gate turned away, and ones whose texture
 * could not be got into video memory. */
static unsigned long hookTexPersp, hookTexAbsent;
/*
 * Submissions, which is not the same as triangles: a textured triangle that
 * splits goes out as two batches, because tmr[] is batch state.  hookDrawn
 * counts triangles and cannot see that.
 */
static unsigned long hookBatches;
static unsigned long hookHardState;
static unsigned long hookSoftState;

/*
 * Why the kernel turned a batch away, kept rather than counted and thrown.
 *
 * The submission block already carries the answer and this file was
 * discarding it, so a refusal in an ordinary scene could only be counted, not
 * explained.  Per verdict, because keeping one sample shows the first refusal
 * and hides every different one after it; and one full copy of the trapezoid
 * that was named, because the index alone identifies a shape this back end
 * generated rather than anything the caller drew.
 */
static unsigned long hookVerdictCount[OSMGA_MESA_VERDICTS];
static OSMGAMesaRefusal hookLastRefusal;

/*
 * The window coordinates of the last triangle, as floats, BEFORE the cast
 * below turns them into integers.
 *
 * Recorded because reasoning about them failed: a triangle came out 83 pixels
 * too large and I refused the "a coordinate landed below its integer" theory
 * by recomputing Mesa's viewport transform myself.  Mesa multiplies a matrix
 * and I evaluated an algebraically equal expression, which is not the same
 * arithmetic; the vertex really had arrived one row low.  This makes the
 * question answerable by looking instead of deriving.
 */
static float hookLastWin[3][3];

/*
 * Window coordinates arrive as floats and the back end needs integers, and
 * the cast that did it was losing a whole row or column.
 *
 * Measured with the recorder above, sweeping every integer position in three
 * viewports: 20 of 320 columns and 10 of 240 rows come back BELOW the integer
 * that was asked for, and an odd-sized viewport is worse at 50 of 319 and 41
 * of 239.  The shortfalls are at most 1.5e-5, which is a fraction of a float
 * ULP -- noise on coordinates that were meant to be exact.  One of them is
 * y = 10, which is how a triangle came to be 83 pixels too large.
 *
 * Rounding everything would fix all of them and would also change what
 * happens to genuinely fractional geometry, from floor to nearest.  That is
 * not a free improvement: over 280 fractional triangles scored against the
 * rule computed from their exact vertices, nearest is closer on average -- 81
 * pixels wrong against 139 -- but WORSE on 43 of them, because rasterisation
 * is discontinuous and a smaller vertex movement is not a smaller mask
 * movement.  Carrying the fraction properly is a separate piece of work.
 *
 * So this snaps only what is within eps of an integer and leaves everything
 * else exactly as it was.  Each eps comes from the ULP at the top of its own
 * range rather than from taste: a coordinate reaches 8192 where the ULP is
 * 9.8e-4, and a depth reaches 65535 where it is 7.8e-3.  The depth one has to
 * be larger, and measuring said so before this was written -- depth noise
 * reached 9.8e-4, four times the coordinate snap.
 */
#define OSMGA_COORD_SNAP  (1.0 / 512.0)     /* 2x the ULP at 8192  */
#define OSMGA_DEPTH_SNAP  (1.0 / 32.0)      /* 4x the ULP at 65535 */

/*
 * Coordinates now go over as fixed point, in 1/256 of a pixel, because the
 * back end can carry some of the fraction and throwing it away costs five per
 * cent of a triangle's area.
 *
 * Rounding to the nearest 1/256 subsumes the bounded snap above for x and y:
 * the noise measured on an intended-integer coordinate tops out at fifteen
 * millionths, which is a long way inside half a step, so an integer still
 * arrives as an exact multiple of 256.  Depth keeps the snap, since it is a
 * whole number to the engine and has its own, larger, noise.
 */
static long
osmgaFix(double v)
{
    /*
     * The bound is the one the conversion below can actually take, not a
     * round number.  It used to be 1e30, which lets through everything the
     * cast cannot hold: v * 256 passes LONG_MAX at about 8.4 million, and a
     * double-to-long conversion out of range is undefined -- on this FPU it
     * yields the indefinite integer, which the coordinate check downstream
     * happens to refuse, by luck rather than by design.
     */
    if (!(v > -8.0e6) || !(v < 8.0e6))
        return 0L;
    return (long)floor(v * (double)OSMGA_MESA_SUBONE + 0.5);
}

static double
osmgaSnap(double v, double eps)
{
    double n;

    /* NaN and the infinities have no integer to cast to; ask before casting
     * rather than after, which is where the range check happens today. */
    if (!(v > -1.0e30) || !(v < 1.0e30))
        return 0.0;
    n = floor(v + 0.5);
    if (v - n <= eps && n - v <= eps)
        return n;
    /*
     * floor, not a cast.  They agree for everything Mesa should hand us, and
     * where they differ -- a negative coordinate -- a cast truncates toward
     * zero and puts a step at the origin that is nowhere else on the line.
     * "Mesa should not produce one" is the kind of assumption that has been
     * wrong twice here already.
     */
    return floor(v);
}
#define OSMGA_MESA_REFUSAL_LIMIT  8UL
/*
 * The software triangle function, and the context it belongs to.
 *
 * Stored as a pair rather than alone.  Only one context has our function
 * installed at a time, which makes a bare pointer right by accident, and
 * accidentally right is the thing this file keeps having to undo.
 */
static GLcontext *savedTriangleCtx;
static triangle_func savedTriangle;

/*
 * Hand this triangle to the software path, and say whether that happened.
 *
 * Mesa dispatches a triangle to whatever is in Driver.TriangleFunc and there
 * is no way back from inside the call, so a triangle we cannot draw used to
 * be lost outright.  What is saved here was chosen by Mesa itself, one line
 * before ours was installed over it.
 */
static int
osmgaMesaSoftly(GLcontext *ctx, GLuint v0, GLuint v1, GLuint v2, GLuint pv)
{
    if (savedTriangle == 0 || savedTriangleCtx != ctx)
        return 0;
    (*savedTriangle)(ctx, v0, v1, v2, pv);
    /*
     * It drew into the substituted surface and did not tell us.  Without
     * this the copy back can decide there is nothing to copy and the
     * triangle never reaches the application's buffer.
     */
    OSMGAMesaBufferSoiled();
    hookSoftware++;
    return 1;
}
static unsigned long osmgaHookMismatch;

/*
 * One triangle, straight to the engine.
 *
 * A refusal here is not fatal and must not be silent: it means the geometry
 * was outside what the driver accepts, and the honest response is to leave
 * the triangle undrawn and say so, rather than to give up acceleration for
 * the whole program on account of one triangle.
 */
static void
osmgaMesaTriangle(GLcontext *ctx, GLuint v0, GLuint v1, GLuint v2, GLuint pv)
{
    struct vertex_buffer *VB = ctx->VB;
    OSMGAHW3DBatch *batch = OSMGAMesaProbeBatch();
    OSMGAHW3DSubmitBlock res;
    OSMGAMesaVertex a, b, c, prov;
    OSMGAHW3DTri built[4];
    unsigned long zmode, blend;
    unsigned long texOrg = 0UL, texW = 0UL, texH = 0UL, texPitch = 0UL;
    OSMGAMesaTex tex;
    long tmr[4][9];
    int texOn, nwin;
    double zsnap;
    int n;

    if (batch == 0) {
        /* The chooser established this; if it has gone away since, nothing
         * was submitted and the triangle can still be drawn. */
        hookDeclined++;
        (void)osmgaMesaSoftly(ctx, v0, v1, v2, pv);
        OSMGAMesaProbeRevoke("the command window went away mid-frame");
        return;
    }

#define OSMGA_LOAD(dst, idx)                                             \
    do {                                                                 \
        hookLastWin[nwin][0] = (float)VB->Win.data[idx][0];              \
        hookLastWin[nwin][1] = (float)VB->Win.data[idx][1];              \
        hookLastWin[nwin][2] = (float)VB->Win.data[idx][2];              \
        nwin = (nwin + 1) % 3;                                           \
        (dst).x = osmgaFix((double)VB->Win.data[idx][0]);                \
        (dst).y = osmgaFix((double)VB->Win.data[idx][1]);                \
        (dst).z = (unsigned long)((zsnap =                               \
                      (double)osmgaFix((double)VB->Win.data[idx][2]))     \
                          < 0.0 ? 0.0 : zsnap);                           \
        (dst).r = (unsigned long)VB->ColorPtr->data[idx][0];             \
        (dst).g = (unsigned long)VB->ColorPtr->data[idx][1];             \
        (dst).b = (unsigned long)VB->ColorPtr->data[idx][2];             \
        (dst).a = (unsigned long)VB->ColorPtr->data[idx][3];             \
        (dst).s = texOn ? (double)VB->TexCoordPtr[0]->data[idx][0] : 0.0; \
        (dst).tc = texOn ? (double)VB->TexCoordPtr[0]->data[idx][1] : 0.0;\
    } while (0)

    /*
     * Affine, or not accelerated.
     *
     * Win[3] is the INVERSE of W, so three equal values mean the perspective
     * divide is a constant and s and t can be interpolated straight.  Exact
     * comparison, on purpose: an epsilon can only let something wrong in,
     * and refusing a triangle that would have been fine costs nothing but
     * speed.  q lives in the fourth texture component when there is one, and
     * is 1 when there is not.
     */
    texOn = (ctx->RasterMask & (GLuint)TEXTURE_BIT) != 0;
    if (texOn) {
        GLfloat w0 = VB->Win.data[v0][3];

        if (VB->Win.data[v1][3] != w0 || VB->Win.data[v2][3] != w0) {
            hookTexPersp++;
            (void)osmgaMesaSoftly(ctx, v0, v1, v2, pv);
            return;
        }
        if (VB->TexCoordPtr[0]->size > 3 &&
            (VB->TexCoordPtr[0]->data[v0][3] != 1.0F ||
             VB->TexCoordPtr[0]->data[v1][3] != 1.0F ||
             VB->TexCoordPtr[0]->data[v2][3] != 1.0F)) {
            hookTexPersp++;
            (void)osmgaMesaSoftly(ctx, v0, v1, v2, pv);
            return;
        }
        if (!OSMGAMesaTexResidentCurrent(ctx, &texOrg, &texW, &texH,
                                         &texPitch)) {
            hookTexAbsent++;
            (void)osmgaMesaSoftly(ctx, v0, v1, v2, pv);
            return;
        }
        tex.w = texW;
        tex.h = texH;
    }

    nwin = 0;
    OSMGA_LOAD(a, v0);
    OSMGA_LOAD(b, v1);
    OSMGA_LOAD(c, v2);
#undef OSMGA_LOAD

    /*
     * Flat shading takes its colour from the provoking vertex, which is what
     * pv names; smooth shading interpolates and does not use it.  Reading pv
     * in both cases would have looked harmless and been wrong in one.
     */
    /*
     * Depth mode, chosen from the state the chooser already agreed to.  It
     * only ever gets here as one of the comparisons the engine has, because
     * the chooser refuses the others rather than approximating them.
     */
    zmode = (ctx->Depth.Test && ctx->Visual->DepthBits > 0)
            ? OSMGA_MESA_ZMODE_LT : OSMGA_MESA_ZMODE_NONE;

    /*
     * The engine performs one blend and the chooser accepts only that one, so
     * the state has already been agreed to by the time this runs.
     */
    blend = ctx->Color.BlendEnabled ? OSMGA_MESA_BLEND_OVER
                                    : OSMGA_MESA_BLEND_OPAQUE;

    if (ctx->Light.ShadeModel == GL_FLAT) {
        prov.x = (long)VB->Win.data[pv][0];
        prov.y = (long)VB->Win.data[pv][1];
        prov.z = (unsigned long)VB->Win.data[pv][2];
        prov.r = (unsigned long)VB->ColorPtr->data[pv][0];
        prov.g = (unsigned long)VB->ColorPtr->data[pv][1];
        prov.b = (unsigned long)VB->ColorPtr->data[pv][2];
        prov.a = (unsigned long)VB->ColorPtr->data[pv][3];
        /*
         * Flat shading flattens alpha as well.  Passing the provoking vertex
         * for colour while leaving alpha to interpolate across the other
         * three gave a smooth alpha under a flat colour, which is neither of
         * the two things a caller can ask for.
         */
        a.a = b.a = c.a = prov.a;
        n = OSMGAMesaBuildTriangleTex(&a, &b, &c, &prov, zmode, blend,
                                      texOn ? &tex : (const OSMGAMesaTex *)0,
                                      built, tmr);
    } else {
        n = OSMGAMesaBuildTriangleTex(&a, &b, &c, (const OSMGAMesaVertex *)0,
                                      zmode, blend,
                                      texOn ? &tex : (const OSMGAMesaTex *)0,
                                      built, tmr);
    }
    if (n == OSMGA_MESA_TRI_UNSUPPORTED) {
        /*
         * Outside what this back end can express -- not empty.  The two used
         * to share one answer and a triangle like this was dropped; now it
         * goes to the path that can draw it, before anything is submitted.
         */
        hookUnsupported++;
        (void)osmgaMesaSoftly(ctx, v0, v1, v2, pv);
        return;
    }
    if (n == 0)
        return;                 /* no area; nothing to draw and no error */

    /*
     * One batch, or one batch each.
     *
     * tmr[] is batch state and the engine re-seeds the horizontal coordinate
     * at every primitive's own first-row left edge, so the two trapezoids of
     * a split textured triangle cannot share a start -- they go out
     * separately.  Untextured work is unaffected and still goes in one.
     *
     * If the second batch is refused the first is already drawn, and the
     * software redraw below puts the whole triangle down again.  That is only
     * harmless because blending is refused for textured state: without it,
     * writing the same pixel twice writes the same value, and with the depth
     * test on GL_LESS the second write fails its own comparison.
     */
    {
    int nb = texOn ? n : 1;
    int bi;

    for (bi = 0; bi < nb; bi++) {
    int cnt = texOn ? 1 : n;
    int j;

    batch->magic = OSMGA_HW3D_MAGIC;
    batch->version = OSMGA_HW3D_VERSION;
    batch->triCount = (unsigned long)cnt;
    for (j = 0; j < cnt; j++)
        batch->tri[j] = built[texOn ? bi : j];
    if (texOn) {
        batch->state.texorg = texOrg;
        batch->state.texW = texW;
        batch->state.texH = texH;
        batch->state.texPitch = texPitch;
        batch->state.texFormat = OSMGA_HW3D_TEXFMT_TW32;
        /*
         * The gate has already required MinFilter == MagFilter and that both
         * are GL_NEAREST or GL_LINEAR, so one of them decides it.
         */
        {
            const struct gl_texture_object *to =
                ctx->Texture.Unit[0].CurrentD[2];
            const struct gl_texture_image *ti = to->Image[to->BaseLevel];

            batch->state.texFlags =
                ((to->MagFilter == GL_LINEAR) ? OSMGA_HW3D_TEXF_BILIN : 0UL)
                /*
                 * GL_REPLACE gives Av = At for a texture that has an alpha
                 * and Av = Af for one that has not, and Format is the STORED
                 * format -- Mesa derives it from internalFormat, not from the
                 * pixels the caller handed over -- so RGB data uploaded into
                 * an RGBA texture reads GL_RGBA here, which is right.
                 */
                | ((ti != 0 && ti->Format == GL_RGBA)
                   ? OSMGA_HW3D_TEXF_TEXALPHA : 0UL)
                | ((ctx->Texture.Unit[0].EnvMode == GL_MODULATE)
                   ? OSMGA_HW3D_TEXF_MODULATE : 0UL)
                | ((to->WrapS == GL_REPEAT) ? OSMGA_HW3D_TEXF_REPEATU : 0UL)
                | ((to->WrapT == GL_REPEAT) ? OSMGA_HW3D_TEXF_REPEATV : 0UL);
        }
        batch->state.tmr[0] = tmr[bi][0];
        batch->state.tmr[1] = tmr[bi][1];
        batch->state.tmr[2] = tmr[bi][2];
        batch->state.tmr[3] = tmr[bi][3];
        batch->state.tmr[4] = 0L;
        batch->state.tmr[5] = 0L;
        batch->state.tmr[6] = tmr[bi][6];
        batch->state.tmr[7] = tmr[bi][7];
        batch->state.tmr[8] = 1L << 16;
    } else {
        batch->state.texorg = 0UL;
        batch->state.texW = batch->state.texH = batch->state.texPitch = 0UL;
        batch->state.texFormat = 0UL;
        batch->state.texFlags = 0UL;
        memset(batch->state.tmr, 0, sizeof batch->state.tmr);
    }
    /* Where the surface is, asked of the one place that decides it -- not
     * worked out again here, where it could disagree. */
    batch->state.dstorg = OSMGAMesaBufferOrigin();
    /*
     * The destination is the drawing surface Mesa is working on, and saying
     * so is what lets the kernel clip to it: before the batch declared this,
     * the kernel clipped every submission to a fixed sixty-four by a hundred
     * and twenty, which no real surface fits inside.
     */
    batch->state.dstWidth  = OSMGAMesaBufferWidth();
    batch->state.dstHeight = OSMGAMesaBufferHeight();
    batch->state.dstPitch  = OSMGAMesaBufferStride();
    batch->state.zorg      = OSMGAMesaBufferDepthOrigin();

    hookBatches++;
    if (OSMGAMesaProbeSubmit(&res) != 0) {
        hookDeclined++;
        if (res.verdict < OSMGA_MESA_VERDICTS)
            hookVerdictCount[res.verdict]++;
        hookLastRefusal.status   = res.status;
        hookLastRefusal.verdict  = res.verdict;
        hookLastRefusal.triangle = res.triangle;
        hookLastRefusal.triCount = batch->triCount;
        hookLastRefusal.dstWidth  = batch->state.dstWidth;
        hookLastRefusal.dstHeight = batch->state.dstHeight;
        if (res.triangle < batch->triCount)
            hookLastRefusal.tri = batch->tri[res.triangle];
        /*
         * Whether this may be drawn again is not a matter of taste.
         *
         * The driver validates before it encodes anything and before it
         * writes the addresses that start the engine, so a batch the
         * validator refused was never drawn and the triangle can go to
         * software.  A failure AFTER that -- the "did not complete" path --
         * happens with the command list already handed over, and some or all
         * of it may be on the screen; drawing it again would double it, and
         * with depth or blending that is visible.
         *
         * The block says which: the verdict is the validator's answer, so a
         * verdict of OK with a failing status means validation passed and
         * the trouble came later.
         */
        if (res.verdict == OSMGA_HW3D_OK) {
            OSMGAMesaProbeRevoke("a batch failed after the engine had it");
            return;             /* lost, and losing it beats drawing it twice */
        }
        (void)osmgaMesaSoftly(ctx, v0, v1, v2, pv);
        /*
         * Refusing once costs one triangle's worth of software.  Refusing
         * every time, and trying every time, is a performance cliff, so a run
         * of them gives up.  The number is a first guess and is meant to be
         * measured, not defended.
         */
        if (++hookRefusedRun >= OSMGA_MESA_REFUSAL_LIMIT)
            OSMGAMesaProbeRevoke("the driver kept refusing batches");
        return;
    }
    }
    }
    hookRefusedRun = 0;
    hookDrawn++;
    OSMGAMesaBufferSoiled();
}

/*
 * Whether this state is one we can draw.  Modelled on the software driver's
 * own chooser: say no by returning NULL and the software path takes it.
 *
 * Everything not yet built is refused rather than approximated.  Depth,
 * texture and blending all exist in the driver and have all been measured,
 * but none of them is wired through this file yet, and drawing them wrong
 * would be worse than drawing them slowly.
 */
/*
 * Is the texture state one this back end can reproduce exactly?
 *
 * Per state, not per triangle -- whether a particular triangle is affine is
 * decided where the vertices are, further down.
 *
 * Each of these is a thing that would change the drawn pixels, and the list
 * is checked against Mesa's own conditions for its optimised 2D path
 * (triangle.c:1568-1578), which is where the base level and the colour
 * control came from.
 */
static int
osmgaMesaTexStateOK(GLcontext *ctx)
{
    const struct gl_texture_object *t;
    const struct gl_texture_image *img;

    /* One unit, and 2D.  The global mask carries unit one's bits too, so
     * asking unit zero alone would let a second unit through. */
    if (ctx->Texture.ReallyEnabled != TEXTURE0_2D)
        return 0;
    if (ctx->Texture.Unit[0].TexGenEnabled != 0U)
        return 0;
    /*
     * GL_REPLACE and GL_MODULATE, which are two words of the combiner.
     *
     * The engine's product is not Mesa's.  Measured over a 64 by 64 table of
     * value pairs, the engine is (a * b * 257 + 32768) >> 16 -- a faithful
     * normalised product -- where Mesa is (a * (b + 1)) >> 8, and the two
     * disagree on 28.6% of pairs by exactly one level, the hardware being the
     * more accurate of them.  Nothing can reconcile them: at a texel of 255
     * both reduce to the fragment's own value, so there is no pre-bias of the
     * interpolated colour left to give.
     *
     * It is taken anyway.  MODULATE is GL's default environment, so refusing
     * it means an ordinary program is never accelerated at all, and one level
     * in eight bits is smaller than the texel-phase difference this back end
     * already carries.
     */
    if (ctx->Texture.Unit[0].EnvMode != GL_REPLACE &&
        ctx->Texture.Unit[0].EnvMode != GL_MODULATE)
        return 0;
    /* A second colour would be added after the texture and the engine has
     * nowhere to put it. */
    if (ctx->Light.Model.ColorControl != GL_SINGLE_COLOR)
        return 0;

    t = ctx->Texture.Unit[0].CurrentD[2];
    if (t == 0)
        return 0;
    /*
     * The engine has ONE filter switch and no notion of lambda, while GL
     * chooses between MinFilter and MagFilter per fragment -- and a single
     * triangle can be magnified in one place and minified in another, or in
     * one axis and not the other.  Requiring the two to be equal is what
     * makes that choice stop mattering.  The four mipmap filters fall out
     * here too, since neither of the values below is one of them.
     */
    if (t->MinFilter != t->MagFilter)
        return 0;
    if (t->MagFilter == GL_NEAREST) {
        /*
         * With nearest sampling GL_CLAMP and GL_CLAMP_TO_EDGE name the same
         * texel for every coordinate in [0,1] -- Mesa's own two branches in
         * COMPUTE_NEAREST_TEXEL_LOCATION agree -- so both may be taken.
         */
        if ((t->WrapS != GL_CLAMP && t->WrapS != GL_CLAMP_TO_EDGE &&
             t->WrapS != GL_REPEAT) ||
            (t->WrapT != GL_CLAMP && t->WrapT != GL_CLAMP_TO_EDGE &&
             t->WrapT != GL_REPEAT))
            return 0;
        /*
         * And GL_REPEAT, per axis.  The engine wraps by masking, which is GL's modulo only for a
         * power-of-two dimension, so that is refused here.  It also needs a
         * packed surface, since a masked index into a padded one addresses
         * the wrong row; that holds by construction -- the arena packs a
         * texture at its own width and the residency reports that width as
         * the pitch -- and the kernel checks it again before it clears a
         * clamp bit.  Both matter because the kernel answers a request it
         * cannot honour by clamping, and a silently clamped axis is a wrong
         * picture rather than a refusal.
         */
    } else if (t->MagFilter == GL_LINEAR) {
        /*
         * Under a linear filter the two wraps part company: GL_CLAMP blends
         * the border colour into the outermost half texel (texture.c,
         * sample_2d_linear substitutes tObj->BorderColor when i0 is -1 or i1
         * is the width) and GL_CLAMP_TO_EDGE holds the edge texel instead.
         *
         * Measured, on the machine: painting the outer two texels white and
         * walking the outer half texel at each end reads 255 throughout,
         * where a black border would have read about 127.  Fully outside the
         * texture it names the nearest edge texel, one axis at a time, and
         * the far corner names the corner texel.  So CLAMPUV is the edge
         * clamp, and only GL_CLAMP_TO_EDGE may be taken here.
         *
         * What the filter itself does was measured the same way, with
         * neighbouring texels painted 0 and 255 so the byte that comes back
         * IS the weight: 127 at the texel boundary, 0 at the texel centre,
         * and along a diagonal through a 2x2 all four corners appear with
         * weights that are exact products, truncated.  That is the OpenGL
         * rule -- u' = u * N - 0.5 and blend the two either side -- and 32 of
         * 32 samples matched a model of it exactly.
         */
        /*
         * Repeat is taken here too.  A linear filter has to do something at
         * the texture's edge that nearest never does -- blend across it --
         * and GL's rule masks BOTH taps, so at s = 0 the lower tap is the
         * last texel and at s = 1 the upper tap is the first.  Measured at
         * both seams and at the corner where the two axes wrap together, the
         * engine reads the half-and-half blend every time, which rules out
         * the implementation that wraps the tap it was asked for and clamps
         * its neighbour: that one passes one seam and fails the other.
         */
        if ((t->WrapS != GL_CLAMP_TO_EDGE && t->WrapS != GL_REPEAT) ||
            (t->WrapT != GL_CLAMP_TO_EDGE && t->WrapT != GL_REPEAT))
            return 0;
    } else {
        return 0;
    }
    if (t->BaseLevel != 0)
        return 0;
    img = t->Image[0];
    if (img == 0 || img->Data == 0 || img->IsCompressed || img->Border != 0)
        return 0;
    /*
     * The power-of-two check for repeat lives HERE, after the image is in
     * hand: it was written above the line that fetches it, where the pointer
     * had not been set yet, and the test caught it before the machine did.
     */
    if ((t->WrapS == GL_REPEAT &&
         (img->Width == 0 ||
          ((unsigned long)img->Width & ((unsigned long)img->Width - 1UL))
              != 0UL)) ||
        (t->WrapT == GL_REPEAT &&
         (img->Height == 0 ||
          ((unsigned long)img->Height & ((unsigned long)img->Height - 1UL))
              != 0UL)))
        return 0;
    /*
     * RGB and RGBA, and the difference between them is one bit.
     *
     * GL_REPLACE gives Cv = Ct for both, and for the alpha Av = At where the
     * texture has one and Av = Af where it has not (texture.c:2419-2426).  So
     * the batch says which operand the engine should take and the encoder
     * puts it in both lanes' texture-environment word.  Anything with fewer
     * channels -- ALPHA, LUMINANCE, INTENSITY -- is a different substitution
     * and is not offered.
     */
    if (img->Format != GL_RGB && img->Format != GL_RGBA)
        return 0;
    return 1;
}

static triangle_func
osmgaMesaChooseTriangle(GLcontext *ctx)
{
    OSMGAMesaProbe probe;

    OSMGAMesaProbeRun(&probe);
    if (probe.verdict != OSMGA_PROBE_HARDWARE) return NULL;

    /*
     * Refuse everything that is not ordinary rendering.  In feedback and
     * selection Mesa installs triangle functions that record rather than
     * draw, and glRenderMode would silently return nothing if we replaced
     * one of them; NoRaster asks for the drawing to be discarded, which is
     * not something to answer by drawing.
     */
    if (ctx->RenderMode != GL_RENDER) return NULL;
    if (ctx->NoRaster)                return NULL;

    /*
     * Colour-index mode keeps its colours in IndexPtr, and reading RGB out
     * of ColorPtr there would draw whatever happened to be in an array this
     * mode does not maintain.
     */
    if (!ctx->Visual->RGBAflag)       return NULL;

    /*
     * Ask for the batch here rather than per triangle.  Once the function is
     * installed Mesa dispatches every triangle to it, and returning without
     * drawing loses that triangle for good -- there is no way back to the
     * software function from inside the call.  Anything that can be found
     * out in advance has to be found out here.
     */
    if (OSMGAMesaProbeBatch() == 0)   return NULL;

    /*
     * And refuse unless the surface really is in video memory.  Without the
     * substitution the software path would be drawing into the application's
     * own buffer while this drew into the card, and a frame split between
     * two places is worse than a slow one.
     */
    if (OSMGAMesaBufferOrigin() == 0UL) return NULL;

    /*
     * And refuse a stride the engine cannot walk.
     *
     * The surface allocator already declines to take such a surface into
     * video memory, so reaching here means something changed underneath --
     * but this is the last place anything can still be said no to.  Once the
     * triangle function is installed Mesa dispatches to it and there is no
     * way back: a batch refused by the kernel loses that triangle for good
     * and revokes the probe for the rest of the process.  A NULL here costs
     * nothing at all.
     */
    if ((OSMGAMesaBufferStride() % OSMGA_HW3D_PITCH_ALIGN) != 0UL)
        return NULL;

    /* Neither of these reaches RasterMask, so they are asked about here. */
    if (ctx->Polygon.SmoothFlag)      return NULL;
    if (ctx->Polygon.StippleFlag)     return NULL;

    /*
     * RasterMask is the union of everything that makes rasterisation more
     * than plain filling -- alpha test, blending, depth, fog, logic op,
     * scissor, stencil, colour masking, texture (state.c, where it is
     * composed).  Refusing any bit we do not expressly allow is what keeps a
     * state we have never seen from being drawn as though it were plain,
     * including states added to Mesa after this was written.
     *
     * ALPHABUF_BIT has to be allowed or nothing is ever accelerated: OSMesa
     * asks for a software alpha buffer whenever the visual has alpha bits,
     * which every 32-bit format here does, so the bit is set in even the
     * plainest context.  Letting it through is sound only because everything
     * that would READ alpha -- blending and the alpha test -- is refused by
     * its own bit.  What it leaves behind is recorded in REMAINING_WORK: the
     * software alpha buffer does not learn about pixels we drew.
     */
    /*
     * Depth is allowed through now, but only in the one shape the engine
     * actually performs: less-than, writing the result back, against a
     * sixteen-bit buffer -- which is the depth Mesa's software path uses in
     * the same memory.  Every other comparison is refused rather than
     * approximated by the nearest one the register has.
     *
     * The buffer itself has to be the shared one; without it the two paths
     * would be testing against different depths, which is worse than not
     * accelerating at all.
     */
    /*
     * Polygon offset never reaches RasterMask, so it would have slipped
     * through every check above.  Mesa adds its offset to each fragment's
     * depth; the engine knows nothing about it and would write unoffset
     * depths into a buffer the software path is offsetting -- which is
     * exactly the disagreement the shared buffer exists to prevent.
     */
    if (ctx->Polygon.OffsetFill)      return NULL;

    /*
     * The engine computes (a*src + (255-a)*dst)/255 and nothing else, which
     * is source alpha against one minus it.  Any other pair of factors, any
     * other equation, is refused rather than approximated -- a blend that is
     * nearly right is a picture that is wrong.
     */
    if ((ctx->RasterMask & BLEND_BIT) != 0) {
        if (ctx->Color.BlendSrcRGB != GL_SRC_ALPHA)           return NULL;
        if (ctx->Color.BlendDstRGB != GL_ONE_MINUS_SRC_ALPHA) return NULL;
        /*
         * The alpha factors are stored separately and can be set separately,
         * so checking only the colour ones would let a state through whose
         * alpha the engine blends by the colour rule.  The software driver's
         * own fast path checks the same two, which is where the omission
         * showed.
         */
        if (ctx->Color.BlendSrcA != GL_SRC_ALPHA)             return NULL;
        if (ctx->Color.BlendDstA != GL_ONE_MINUS_SRC_ALPHA)   return NULL;
        if (ctx->Color.BlendEquation != GL_FUNC_ADD_EXT)      return NULL;
        /*
         * And the destination alpha has to be where the engine puts it.  If
         * Mesa is keeping a software alpha buffer it reads alpha from there
         * instead of from the surface, and would blend against alpha this
         * back end never wrote.
         */
        if (ctx->DrawBuffer->UseSoftwareAlphaBuffers)         return NULL;
    }

    if ((ctx->RasterMask & DEPTH_BIT) != 0) {
        if (ctx->Depth.Func != GL_LESS)             return NULL;
        if (ctx->Depth.Mask != GL_TRUE)             return NULL;
        if (ctx->Visual->DepthBits != 16)           return NULL;
        if (OSMGAMesaBufferDepthOrigin() == 0UL)    return NULL;
    }

    /*
     * Texturing, when the state is one that can be reproduced.  Blending is
     * refused alongside it: with blending off the alpha question does not
     * arise at all, and a trapezoid drawn twice -- which is what a partly
     * submitted split triangle plus a software redraw amounts to -- writes
     * the same pixel twice instead of adding to it.
     */
    if ((ctx->RasterMask & (GLuint)TEXTURE_BIT) != 0) {
        if ((ctx->RasterMask & (GLuint)BLEND_BIT) != 0)
            return NULL;
        if (!osmgaMesaTexStateOK(ctx))
            return NULL;
    }

    if ((ctx->RasterMask &
         ~(GLuint)(ALPHABUF_BIT | DEPTH_BIT | BLEND_BIT | TEXTURE_BIT)) != 0)
        return NULL;

    return osmgaMesaTriangle;
}

/*
 * The picture lives in video memory now, and the buffer the application
 * handed to OSMesaMakeCurrent is never written by anyone.  These put it back
 * at every point the application could look -- which is between GL calls,
 * since it is not going to be inside one.
 *
 * RenderFinish is the important one: it fires at the end of every batch of
 * primitives, so a program that draws and then reads without ever calling
 * glFinish still sees its picture.  Relying on glFinish would have been far
 * cheaper and is not something this OSMesa documents anywhere, so it is not
 * something to rely on.
 */
/*
 * Anything at all is about to be drawn -- by this back end or by the
 * software rasteriser, which writes into the same surface and has no way to
 * announce it.  Marking here rather than only where we draw is what keeps a
 * frame made entirely of refused primitives from being mirrored away as
 * "nothing happened".
 */
static void
osmgaMesaSoil(GLcontext *ctx)
{
    (void)ctx;
    OSMGAMesaBufferSoiled();
}

static void
osmgaMesaMirror(GLcontext *ctx)
{
    (void)ctx;
    OSMGAMesaBufferMirror();
}

/*
 * Clearing is the driver's own, so ours has to chain rather than replace.
 * The saved pointer is refreshed on every state update, because the driver
 * reinstalls its own each time -- and never saved when it is already ours,
 * which would have made the wrapper call itself.
 */
static GLbitfield (*osmgaMesaPrevClear)(GLcontext *, GLbitfield, GLboolean,
                                        GLint, GLint, GLint, GLint);

static GLbitfield
osmgaMesaClear(GLcontext *ctx, GLbitfield mask, GLboolean all,
               GLint x, GLint y, GLint w, GLint h)
{
    GLbitfield left = mask;

    if (osmgaMesaPrevClear != 0)
        left = (*osmgaMesaPrevClear)(ctx, mask, all, x, y, w, h);
    OSMGAMesaBufferSoiled();
    return left;
}

void
OpenStepMesaAccelUpdateState(GLcontext *ctx, int rowLength, int yUp)
{
    triangle_func f;

    /*
     * Both paths must put GL row y in the same place.  With the origin at
     * the bottom -- OSMesa's default -- row y is base + y * pitch, and the
     * engine draws its rows from the destination origin exactly that way, so
     * the two agree without being told to.  Turned over, the software path
     * counts from the far end and the engine does not, and the frame would
     * come out half one way and half the other.
     *
     * The stride has to match for the same reason: the engine takes the
     * destination pitch from a register holding the display's, so a surface
     * Mesa is writing at any other stride is one the engine reads wrongly.
     */
    /*
     * Nothing of ours belongs on a context that is not drawing into the
     * surface, and taking it off is a separate act from not putting it on.
     * The state update below reinstalls Mesa's own Clear and triangle
     * function, but it does not touch RenderStart, RenderFinish, Finish or
     * Flush -- so a context that has lost its binding keeps mirroring a
     * surface it no longer uses into a buffer that may be smaller.
     *
     * Put back means NULL: nothing in OSMesa or Mesa sets those four, only
     * this file, and every call site guards against NULL.
     */
    if (!OSMGAMesaBufferBoundTo(ctx->DriverCtx)) {
        savedTriangle = 0;
        savedTriangleCtx = 0;
        ctx->Driver.RenderStart = 0;
        ctx->Driver.RenderFinish = 0;
        ctx->Driver.Finish = 0;
        ctx->Driver.Flush = 0;
        if (ctx->Driver.Clear == osmgaMesaClear && osmgaMesaPrevClear != 0)
            ctx->Driver.Clear = osmgaMesaPrevClear;
        return;
    }

    if (!yUp || (unsigned long)rowLength != OSMGAMesaBufferStride()) {
        osmgaHookMismatch++;
        return;
    }

    f = osmgaMesaChooseTriangle(ctx);

    /*
     * Only ever replace with something; never write NULL.  The software
     * driver has just put its own choice here, and overwriting that with
     * NULL would take away an acceleration that has nothing to do with us.
     */
    if (f != 0) hookHardState++; else hookSoftState++;

    if (f != 0) {
        /*
         * Ask Mesa for a software triangle before putting ours over it.
         *
         * Saving whatever is in the field would usually save nothing:
         * OSMesa's own chooser returns NULL for a textured state and for
         * almost every other one, and it is Mesa's core chooser that has a
         * textured software triangle to give.  That chooser runs after this
         * hook and returns early when the field is already filled, which is
         * why ours survives -- so it is called here, while the field is
         * still whatever OSMesa left, and what it picks becomes the way back.
         */
        gl_set_triangle_function(ctx);
        if (ctx->Driver.TriangleFunc != 0
            && ctx->Driver.TriangleFunc != osmgaMesaTriangle) {
            savedTriangle = ctx->Driver.TriangleFunc;
            savedTriangleCtx = ctx;
        } else {
            savedTriangle = 0;
            savedTriangleCtx = 0;
        }
        ctx->Driver.TriangleFunc = f;
    }

    /*
     * The mirror is installed whenever there is a surface to mirror, not
     * only when this state can be accelerated: the software rasteriser is
     * drawing into video memory too, so the application's buffer needs
     * putting back either way.
     */
    /* Bound, by the test above, so the surface is this context's to mirror. */
    /*
     * The texture hooks go in whenever there is a surface, like the mirror
     * below and for the same reason: a texture defined while the software
     * path is drawing still has to be noticed, or the copy in video memory
     * would be stale the moment acceleration came back.
     */
    OSMGAMesaTexInstall(ctx);

    {
        ctx->Driver.RenderStart = osmgaMesaSoil;
        ctx->Driver.RenderFinish = osmgaMesaMirror;
        ctx->Driver.Finish = osmgaMesaMirror;
        ctx->Driver.Flush = osmgaMesaMirror;
        if (ctx->Driver.Clear != osmgaMesaClear) {
            osmgaMesaPrevClear = ctx->Driver.Clear;
            ctx->Driver.Clear = osmgaMesaClear;
        }
    }
}

/* The three window coordinates the last triangle arrived with, unrounded. */
double OSMGAMesaHookLastWin(unsigned long v, unsigned long c)
{
    if (v > 2UL || c > 2UL) return 0.0;
    return (double)hookLastWin[v][c];
}

unsigned long OSMGAMesaHookDrawn(void)    { return hookDrawn; }
unsigned long OSMGAMesaHookDeclined(void) { return hookDeclined; }
unsigned long OSMGAMesaHookSoftware(void) { return hookSoftware; }
unsigned long OSMGAMesaHookHardState(void) { return hookHardState; }
unsigned long OSMGAMesaHookSoftState(void) { return hookSoftState; }
unsigned long OSMGAMesaHookTexPersp(void)  { return hookTexPersp; }
unsigned long OSMGAMesaHookTexAbsent(void) { return hookTexAbsent; }
unsigned long OSMGAMesaHookBatches(void)   { return hookBatches; }
unsigned long OSMGAMesaHookUnsupported(void) { return hookUnsupported; }
unsigned long OSMGAMesaHookVerdictCount(unsigned long v)
{
    return (v < OSMGA_MESA_VERDICTS) ? hookVerdictCount[v] : 0UL;
}
const OSMGAMesaRefusal *OSMGAMesaHookLastRefusal(void)
{
    return &hookLastRefusal;
}
