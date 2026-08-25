/*
 * OpenStepMGAMesaHook.c - see the header.
 *
 * The shape of this file follows osmesa.c's own: a chooser that returns a
 * function or NULL, and a triangle function that reads the vertex buffer.
 * Deviating from that would mean guessing at conventions the software driver
 * already demonstrates.
 */

#include <math.h>
#include <sys/time.h>   /* test-only submit timing; see osmgaMesaSubmitBatch */
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
/* see OSMGAMesaHookForceSoftware -- the tests' way to change the path
 * without changing anything else */
static int hookForcedSoftware;

static unsigned long hookBatches;
/* Trapezoids, not batches.  A split triangle in one batch and a triangle
 * that never split are both one batch, so the batch count alone stopped
 * saying whether the split happened the moment both halves went out
 * together. */
static unsigned long hookTraps;
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
/*
 * What a submission costs, seen from this side of the ioctl.  Test only.
 *
 * The kernel fills verdict, triangle, dwords and spins on EVERY submission,
 * refused or not -- the ioctl writes them before it decides what to return
 * -- but the helper below only hands the block to its caller on a refusal.
 * So the accounting is done inside the helper, where the block is always
 * valid, rather than at the call sites where it is not.
 *
 * spins is the driver's loop INDEX, so a poll that succeeded on its first
 * read reports nought; the number of reads is one more than this.  It counts
 * one of the five waits in the submit path -- the primary-DMA completion
 * poll -- and says nothing about the other four.  What it can settle is
 * whether the frame's kernel time is spent waiting there at all.
 *
 * Clears come through the same helper as triangles, so the count here is
 * batches plus clears; the caller-side counters separate them.
 */
static unsigned long submitCount, submitUs, submitDwords;
static unsigned long submitSpins, submitSpinMax, submitSpun;

void
OSMGAMesaHookSubmitStats(unsigned long out[6])
{
    if (out == 0)
        return;
    out[0] = submitCount;
    out[1] = submitUs;
    out[2] = submitDwords;
    out[3] = submitSpins;
    out[4] = submitSpinMax;
    out[5] = submitSpun;
}

static GLcontext *savedTriangleCtx;
static triangle_func savedTriangle;

/*
 * M1-6 -- the pending batch.
 *
 * One source triangle used to be one submission, and the submission is the
 * expensive half: 73 microseconds of ioctl and engine round trip against 15
 * milliseconds of geometry for a whole teapot frame -- 16106 submissions,
 * 1.2 seconds, for a scene the processor prepares in 0.015.  Trapezoids now
 * accumulate in the mapped batch and go out together.
 *
 * WHAT MAY SHARE A BATCH.  dwgctl and alphactrl are per trapezoid, so depth
 * modes, depth-mask and blending vary freely inside one batch.  What is
 * batch STATE -- the texture block (gradients tmr[0..5], origin, size,
 * pitch, flags, format) and the scissor -- keys a flush: the first textured
 * triangle of a run sets the key and a different key flushes first.
 * Untextured triangles carry no key and mix with anything.
 *
 * THE REPLAY CONTRACT.  The kernel validates before it encodes, so a refused
 * batch drew nothing and every accumulated source triangle is replayed
 * through the software rasteriser -- which needs its VB indices alive.  Two
 * gates protect that:
 *   - accumulation never crosses a render bracket (the RenderFinish wrapper
 *     flushes), inside which ctx->VB is stable (vbrender.c:699-728, and the
 *     indirect path pins it, vbindirect.c:311-338);
 *   - a VB that clips (ClipOrMask != 0) does not batch at all, because
 *     clipped triangles use temporary vertices in the VB's free area and a
 *     LATER clip may overwrite an EARLIER one's -- an index replayed after
 *     that reads the wrong vertex.  Those triangles submit immediately,
 *     alone, exactly as before, and their replay happens while the
 *     temporaries still hold their values.  (Cross-review missed this one;
 *     the gate is ours.)
 *   - Mesa multipass repeats the loop before RenderFinish (vbrender.c:721),
 *     so a context with a MultipassFunc does not batch either.
 *
 * REENTRANCY.  The flush detaches the pending counts before it submits or
 * replays, and holds a guard, so a replayed software triangle that lands
 * back in these wrappers finds an empty batch instead of recursing.
 */
static unsigned long pendTraps;      /* trapezoids already in batch->tri[] */
static unsigned long pendSrcCount;   /* source triangles those came from */
static struct { GLuint v0, v1, v2, pv; } pendSrc[OSMGA_HW3D_MAX_TRI];
static GLcontext *pendCtx;
static void *pendVB;
static int pendHasTex;
static unsigned long pendTexOrg, pendTexW, pendTexH, pendTexPitch;
static unsigned long pendTexFlags;
static long pendTmr[6];
static int pendInFlush;
/* Why flushes happened, so fragmentation is a number and not a feeling. */
static unsigned long hookFlushBracket, hookFlushKey, hookFlushFull;
static unsigned long hookFlushOther, hookReplayed;
/* The A/B knob: 1 reproduces the old one-triangle-per-submission behaviour
 * exactly, which is what the identical-image comparison runs against. */
static unsigned long hookBatchLimit = OSMGA_HW3D_MAX_TRI;
/* Test-only: corrupt the magic of every flushed batch so the kernel refuses
 * it (E_MAGIC, before anything is drawn) and the replay path runs for real.
 * Nothing sets this but the injection setter, and nothing should. */
static int hookInjectRefusal;

static int osmgaMesaSubmitBatch(GLcontext *ctx, OSMGAHW3DBatch *batch,
                                OSMGAHW3DSubmitBlock *out);
static void osmgaMesaBatchUntextured(OSMGAHW3DBatch *batch);
static int osmgaMesaSoftly(GLcontext *ctx, GLuint v0, GLuint v1, GLuint v2,
                           GLuint pv);

/*
 * Ship whatever is pending.  Success soils the surface and counts the
 * sources as drawn; a validator refusal replays every source through
 * software (nothing was drawn); a failure after validation revokes, exactly
 * as the one-triangle path always has.
 */
static void
osmgaMesaFlushPending(void)
{
    OSMGAHW3DBatch *batch;
    OSMGAHW3DSubmitBlock res;
    GLcontext *ctx;
    unsigned long nsrc, ntraps, i;

    if (pendTraps == 0UL || pendInFlush)
        return;
    pendInFlush = 1;
    ctx = pendCtx;
    ntraps = pendTraps;
    nsrc = pendSrcCount;
    /* Detach FIRST: a replayed triangle re-entering sees an empty batch. */
    pendTraps = 0UL;
    pendSrcCount = 0UL;

    batch = OSMGAMesaProbeBatch();
    if (batch == 0 || ctx == 0) {
        /* The window went away with work pending; the probe has revoked and
         * the surface is gone, so there is nothing to draw INTO. */
        hookFlushOther++;
        pendInFlush = 0;
        return;
    }
    batch->magic = hookInjectRefusal ? (OSMGA_HW3D_MAGIC ^ 1UL)
                                     : OSMGA_HW3D_MAGIC;
    batch->version = OSMGA_HW3D_VERSION;
    batch->triCount = ntraps;
    if (pendHasTex) {
        batch->state.texorg = pendTexOrg;
        batch->state.texW = pendTexW;
        batch->state.texH = pendTexH;
        batch->state.texPitch = pendTexPitch;
        batch->state.texFormat = OSMGA_HW3D_TEXFMT_TW32;
        batch->state.texFlags = pendTexFlags;
        for (i = 0UL; i < 6UL; i++)
            batch->state.tmr[i] = pendTmr[i];
        batch->state.texBiasReqU = OSMGA_HW3D_TEX_BIAS_NONE;
        batch->state.texBiasReqV = OSMGA_HW3D_TEX_BIAS_NONE;
    } else {
        osmgaMesaBatchUntextured(batch);
    }
    hookBatches++;
    hookTraps += ntraps;
    if (osmgaMesaSubmitBatch(ctx, batch, &res) != 0) {
        if (res.verdict == OSMGA_HW3D_OK) {
            /* Validation passed and the trouble came later: some of it may
             * be on the screen, and drawing it again would double it. */
            OSMGAMesaProbeRevoke("a batch failed after the engine had it");
        } else {
            for (i = 0UL; i < nsrc; i++)
                (void)osmgaMesaSoftly(ctx, pendSrc[i].v0, pendSrc[i].v1,
                                      pendSrc[i].v2, pendSrc[i].pv);
            hookReplayed += nsrc;
            if (++hookRefusedRun >= OSMGA_MESA_REFUSAL_LIMIT)
                OSMGAMesaProbeRevoke("the driver kept refusing batches");
        }
    } else {
        hookRefusedRun = 0;
        hookDrawn += nsrc;
        OSMGAMesaBufferSoiled();
    }
    pendInFlush = 0;
}

void
OSMGAMesaHookFlushPending(void)
{
    osmgaMesaFlushPending();
}

void
OSMGAMesaHookInjectRefusal(int on)
{
    osmgaMesaFlushPending();
    hookInjectRefusal = (on != 0);
}

void
OSMGAMesaHookBatchLimit(unsigned long limit)
{
    osmgaMesaFlushPending();
    if (limit < 1UL) limit = 1UL;
    if (limit > OSMGA_HW3D_MAX_TRI) limit = OSMGA_HW3D_MAX_TRI;
    hookBatchLimit = limit;
}

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
    /*
     * A software triangle about to draw must land OVER every accelerated one
     * accumulated before it.  During a refusal replay the guard makes this a
     * no-op -- the batch was already detached.
     */
    osmgaMesaFlushPending();
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
static unsigned long hookClears;
/*
 * The word OSMesa would write for the current clear colour, handed over by
 * the port so that nothing here has to guess how a pixel is packed, and so
 * that the surface never has to be READ to find out (see 3-18: a client's
 * first read after a submission can be stale, and the word this would have
 * been read from is the one offset that settles nothing).  Qualified by the
 * context it belongs to, because there is one of these and there can be
 * more than one context.
 */
static unsigned long hookClearPixel;
static void *hookClearPixelCtx;
/*
 * A whole-surface clear the engine took, waiting for the render bracket that
 * _mesa_Clear opens immediately afterwards.  One shot: the next soil takes
 * it, so it can only ever apply to that bracket.
 */
static int hookPendingUniform;
static unsigned long hookPendingWord;
static void *hookPendingCtx;
static unsigned long hookPendingDrawn;
static unsigned long hookUniformFills;
static unsigned long hookUniformArmed;
/*
 * Why the last clear was not taken.  A clear that quietly declines is
 * indistinguishable from one that was never asked for, and the first
 * accelerated clear declined every time for a reason no amount of reading
 * found -- so it says.
 */
static int hookClearWhy;

/*
 * The texture state of a batch that has no texture.
 *
 * Every field, every time.  The batch is a mapped buffer this library
 * reuses, so a field left alone keeps what the last submission put there --
 * and a stale texture origin under a triangle that does not ask for one is
 * how a batch draws somebody else's pixels.  Written in one place because
 * two callers need it and a second copy would be a second thing to forget.
 */
static void
osmgaMesaBatchUntextured(OSMGAHW3DBatch *batch)
{
    batch->state.texorg = 0UL;
    batch->state.texW = batch->state.texH = batch->state.texPitch = 0UL;
    batch->state.texFormat = 0UL;
    batch->state.texFlags = 0UL;
    memset(batch->state.tmr, 0, sizeof batch->state.tmr);
    batch->state.texBiasReqU = OSMGA_HW3D_TEX_BIAS_NONE;
    batch->state.texBiasReqV = OSMGA_HW3D_TEX_BIAS_NONE;
}

/*
 * Everything a submission needs that is not the primitives themselves, and
 * the submission.
 *
 * Both the triangle path and the accelerated clear come through here, which
 * is the point: the destination, the depth origin and the scissor are asked
 * of the one place that decides each, so the two cannot drift apart.  What
 * is NOT here is the outcome -- a refused triangle goes to Mesa's
 * rasteriser and a refused clear goes to Mesa's clear, and those are not the
 * same thing -- so this returns the answer and lets the caller act on it.
 *
 * Nor are the counters: hookBatches and hookTraps are what the tests read to
 * ask "did the engine draw this", and a clear counted among them would make
 * a forced-software comparison report that the software pass was
 * accelerated.  The callers count their own.
 *
 * Returns 0 when the batch was taken, non-zero when it was refused, with
 * *out holding the driver's answer.
 */
static int
osmgaMesaSubmitBatch(GLcontext *ctx, OSMGAHW3DBatch *batch,
                     OSMGAHW3DSubmitBlock *out)
{
    OSMGAHW3DSubmitBlock res;

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
    /*
     * The scissor, written every submission for the same reason the bias
     * request is: the batch is a mapped buffer this library reuses field by
     * field, so anything not written keeps what the last one left.
     *
     * GL's box has its low corner at the bottom left and so does this
     * surface -- the chooser refuses a context that is not y-up -- so the
     * box goes across without a flip.  The kernel intersects it with the
     * window it already clips to, so nothing here has to be sane for the
     * driver to stay safe.
     */
    if (ctx->Scissor.Enabled) {
        batch->state.scissorOn = 1UL;
        batch->state.scissorX = (long)ctx->Scissor.X;
        batch->state.scissorY = (long)ctx->Scissor.Y;
        batch->state.scissorW = (ctx->Scissor.Width > 0)
                                ? (unsigned long)ctx->Scissor.Width : 0UL;
        batch->state.scissorH = (ctx->Scissor.Height > 0)
                                ? (unsigned long)ctx->Scissor.Height : 0UL;
    } else {
        batch->state.scissorOn = 0UL;
        batch->state.scissorX = 0L;
        batch->state.scissorY = 0L;
        batch->state.scissorW = 0UL;
        batch->state.scissorH = 0UL;
    }
    {
        struct timeval t0, t1;
        int rc;

        gettimeofday(&t0, (struct timezone *)0);
        rc = OSMGAMesaProbeSubmit(&res);
        gettimeofday(&t1, (struct timezone *)0);
        submitCount++;
        submitUs += (unsigned long)((t1.tv_sec - t0.tv_sec) * 1000000L +
                                    (t1.tv_usec - t0.tv_usec));
        submitDwords += res.dwords;
        submitSpins += res.spins;
        if (res.spins > submitSpinMax) submitSpinMax = res.spins;
        if (res.spins != 0UL) submitSpun++;
        if (rc != 0) {
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

            *out = res;
            return 1;
        }
    }
    return 0;
}

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
    double zoffset;
    unsigned long texOrg = 0UL, texW = 0UL, texH = 0UL, texPitch = 0UL;
    OSMGAMesaTex tex;
    long tmr[4][9];
    int texOn, nwin;
    double zsnap;
    int n;

    /*
     * Asked to step aside.
     *
     * Checked HERE and not in the chooser, and that distinction is the whole
     * of it: the chooser runs when Mesa decides its state has changed, and a
     * flag inside this file is not state Mesa knows about -- so setting it
     * there left the accelerated function installed and the switch did
     * nothing at all.  The tests that used it went on comparing the
     * hardware path with itself and passed.
     *
     * Measured, not reasoned: the scissor test asserts that its software
     * pass did NOT reach the engine, and that assertion failed.
     */
    if (hookForcedSoftware) {
        (void)osmgaMesaSoftly(ctx, v0, v1, v2, pv);
        return;
    }

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
        (dst).qw = (double)VB->Win.data[idx][3];                         \
        (dst).tq = (texOn && VB->TexCoordPtr[0]->size > 3)                \
                   ? (double)VB->TexCoordPtr[0]->data[idx][3] : 1.0;      \
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
        /*
         * The three reciprocals of w no longer have to agree: the builder
         * carries them into a denominator plane and the engine divides.  What
         * is still required is that each is strictly positive -- w at or
         * below nought is a vertex on or behind the eye, which the clip
         * inequalities should already have removed, and which is a projective
         * singularity rather than something to interpolate through.
         */
        if (!(VB->Win.data[v0][3] > 0.0F) ||
            !(VB->Win.data[v1][3] > 0.0F) ||
            !(VB->Win.data[v2][3] > 0.0F)) {
            hookTexPersp++;
            (void)osmgaMesaSoftly(ctx, v0, v1, v2, pv);
            return;
        }
        /*
         * A texture q of its own is taken now; what is refused is one that
         * is not strictly positive.
         *
         * The engine divides one plane by another and cannot be told what to
         * do at a zero crossing, and a q that changes sign inside the
         * triangle puts a singularity there.  Writing it as "not greater
         * than nought" rather than "less than or equal" also turns away a
         * NaN, which would pass the other spelling and then travel through
         * the solver as a quiet infection.
         *
         * This is THIS back end declining what it cannot express, not GL
         * calling it invalid: Mesa's software path does not refuse a
         * negative q, it just divides by the interpolated denominator.
         */
        if (VB->TexCoordPtr[0]->size > 3 &&
            (!(VB->TexCoordPtr[0]->data[v0][3] > 0.0F) ||
             !(VB->TexCoordPtr[0]->data[v1][3] > 0.0F) ||
             !(VB->TexCoordPtr[0]->data[v2][3] > 0.0F))) {
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
    /*
     * The comparison the chooser agreed to, not one of them.
     *
     * Seven of GL's eight have an encoding the register documentation names;
     * GL_NEVER has none and the chooser refuses it.  This used to send every
     * enabled depth state to LT, which was right only because the chooser
     * refused everything but GL_LESS.
     */
    if (!(ctx->Depth.Test && ctx->Visual->DepthBits > 0))
        zmode = OSMGA_MESA_ZMODE_NONE;
    else switch (ctx->Depth.Func) {
    case GL_ALWAYS:   zmode = OSMGA_MESA_ZMODE_ALWAYS; break;
    case GL_EQUAL:    zmode = OSMGA_MESA_ZMODE_E;      break;
    case GL_NOTEQUAL: zmode = OSMGA_MESA_ZMODE_NE;     break;
    case GL_LESS:     zmode = OSMGA_MESA_ZMODE_LT;     break;
    case GL_LEQUAL:   zmode = OSMGA_MESA_ZMODE_LTE;    break;
    case GL_GREATER:  zmode = OSMGA_MESA_ZMODE_GT;     break;
    case GL_GEQUAL:   zmode = OSMGA_MESA_ZMODE_GTE;    break;
    default:          zmode = OSMGA_MESA_ZMODE_NONE;   break;
    }

    /*
     * The engine performs one blend and the chooser accepts only that one, so
     * the state has already been agreed to by the time this runs.
     *
     * WHICH alpha it blends with is a separate field, and it has to follow
     * the texture or the picture is wrong rather than refused.
     *
     * The constant carries "diffused", which was right while nothing was
     * textured.  Measured with a texture bound: eight texels whose alphas
     * ran from nought to 224 all came back as the bare destination, because
     * the interpolated alpha was nought and that is the one it was told to
     * use.
     *
     * What it must become is settled below, and it is NOT a table over the
     * texture environment -- that was tried twice and was wrong twice.
     */
    blend = ctx->Color.BlendEnabled ? OSMGA_MESA_BLEND_OVER
                                    : OSMGA_MESA_BLEND_OPAQUE;
    if (ctx->Color.BlendEnabled) {
        /*
         * The factor pair the application asked for, in the engine's
         * numbering -- and ONLY bits 0-7 are touched, because the alpha mode
         * and the selector live above them and the selector is set below.
         *
         * The chooser has already refused anything not in these two lists,
         * so a default here would be unreachable; it is written all the same,
         * and it keeps the pair the back end has always used.
         */
        unsigned long fs = OSMGA_MESA_BF_SRC_A;
        unsigned long fd = OSMGA_MESA_BF_OM_SRC_A;

        switch (ctx->Color.BlendSrcRGB) {
        case GL_ZERO:                 fs = OSMGA_MESA_BF_ZERO;       break;
        case GL_ONE:                  fs = OSMGA_MESA_BF_ONE;        break;
        case GL_DST_COLOR:            fs = OSMGA_MESA_BF_OTHER_C;    break;
        case GL_ONE_MINUS_DST_COLOR:  fs = OSMGA_MESA_BF_OM_OTHER_C; break;
        case GL_SRC_ALPHA:            fs = OSMGA_MESA_BF_SRC_A;      break;
        case GL_ONE_MINUS_SRC_ALPHA:  fs = OSMGA_MESA_BF_OM_SRC_A;   break;
        case GL_DST_ALPHA:            fs = OSMGA_MESA_BF_DST_A;      break;
        case GL_ONE_MINUS_DST_ALPHA:  fs = OSMGA_MESA_BF_OM_DST_A;   break;
        default:                                                     break;
        }
        switch (ctx->Color.BlendDstRGB) {
        case GL_ZERO:                 fd = OSMGA_MESA_BF_ZERO;       break;
        case GL_ONE:                  fd = OSMGA_MESA_BF_ONE;        break;
        case GL_SRC_COLOR:            fd = OSMGA_MESA_BF_OTHER_C;    break;
        case GL_ONE_MINUS_SRC_COLOR:  fd = OSMGA_MESA_BF_OM_OTHER_C; break;
        case GL_SRC_ALPHA:            fd = OSMGA_MESA_BF_SRC_A;      break;
        case GL_ONE_MINUS_SRC_ALPHA:  fd = OSMGA_MESA_BF_OM_SRC_A;   break;
        case GL_DST_ALPHA:            fd = OSMGA_MESA_BF_DST_A;      break;
        case GL_ONE_MINUS_DST_ALPHA:  fd = OSMGA_MESA_BF_OM_DST_A;   break;
        default:                                                     break;
        }
        blend = (blend & ~OSMGA_MESA_BLEND_FACTOR_MASK) | fs | (fd << 4);
    }
    /*
     * The alpha test, in the same word.
     *
     * Only bits 12-23 are touched, for the same reason the factors touch
     * only 0-7: the alpha mode and the selector live above and the factors
     * below, and a write that took the whole word would clear them.
     *
     * The reference is Mesa's OWN byte.  It has already turned the
     * application's float into a GLubyte and its software path compares that
     * same byte, so the two paths use an identical reference by construction
     * and there is no quantisation to reproduce or to get wrong.
     */
    if (ctx->Color.AlphaEnabled) {
        unsigned long at = OSMGA_MESA_AT_ENABLE
                           | ((unsigned long)ctx->Color.AlphaRef
                              << OSMGA_MESA_AT_REF_SHIFT);

        switch (ctx->Color.AlphaFunc) {
        case GL_EQUAL:    at |= OSMGA_MESA_AT_E;   break;
        case GL_NOTEQUAL: at |= OSMGA_MESA_AT_NE;  break;
        case GL_LESS:     at |= OSMGA_MESA_AT_LT;  break;
        case GL_LEQUAL:   at |= OSMGA_MESA_AT_LTE; break;
        case GL_GREATER:  at |= OSMGA_MESA_AT_GT;  break;
        case GL_GEQUAL:   at |= OSMGA_MESA_AT_GTE; break;
        default:
            /*
             * GL_ALWAYS.  The test is turned OFF rather than enabled with a
             * mode that compares nothing: the two are named separately in the
             * register documentation and have not been shown to be the same
             * thing, and "off" is the one whose behaviour is not in question.
             * GL_NEVER never gets here -- the chooser refuses it.
             */
            at = 0UL;
            break;
        }
        blend = (blend & ~OSMGA_MESA_ATEST_MASK) | at;
    }
    if (ctx->Color.BlendEnabled && texOn) {
        /*
         * "From the texture", for every textured state, because that is not
         * the texel's alpha -- it is the texture STAGE's output, and the
         * stage already computes what GL asks for.
         *
         * Measured, probe section 81: with the combiner's own modulate on,
         * fromtex returned the combiner's product to the level, three
         * readings out of three, while "modulated" multiplied by the
         * fragment's alpha a second time.  Section 77 could not tell those
         * apart, because it ran with the combiner passing the texture's alpha
         * straight through and the two answers were the same number.
         *
         * The encoder sets that stage from GL's own table (TDUALSTAGE, by
         * MGA_TDS_ALPHA_SEL): ARG2 -- the interpolated alpha -- when the
         * texture has none, ARG1 when it has one and the mode is replace, and
         * their product under modulate.  So:
         *
         *      RGB  + REPLACE    stage gives Af      GL wants Af
         *      RGB  + MODULATE   stage gives Af      GL wants Af
         *      RGBA + REPLACE    stage gives At      GL wants At
         *      RGBA + MODULATE   stage gives Af*At   GL wants Af*At
         *
         * all four, with one selector and no format test here at all.
         *
         * The other two are wrong in ways worth naming.  "Modulated" would
         * square the fragment's alpha under RGBA modulate.  "Diffused" would
         * ignore the texture's alpha entirely, which is right for RGB and
         * silently wrong for RGBA -- and an RGB texture's fourth byte is a
         * deliberate nought (the note by the pack in
         * OpenStepMGAMesaTexture.c), so a rule that read it would have made
         * the whole primitive transparent rather than looking merely off.
         */
        blend = (blend & ~OSMGA_MESA_ALPHASEL_MASK) | OSMGA_MESA_ALPHASEL_TEX;
    }

    /*
     * glPolygonOffset, in depth codes, computed HERE and not in the builder
     * because it has to be Mesa's number and not a reconstruction of it --
     * see the note on OSMGAMesaBuildTriangleTex.
     *
     * The expression is Mesa's own (vbrender.c, offset_polygon): the plane
     * normal from the cross product of two edges, the larger of the two
     * absolute depth slopes, times the factor, plus the units.  The window
     * values come straight from VB, BEFORE this file snaps them to a 256th
     * of a pixel, so the guard against a degenerate polygon is on the same
     * numbers Mesa guards -- and it is Mesa's guard, c*c > 1e-16, below
     * which Mesa leaves the offset at nought and so does this.
     */
    zoffset = 0.0;
    if (ctx->Polygon.OffsetFill) {
        double ex = (double)VB->Win.data[v1][0] - (double)VB->Win.data[v0][0];
        double ey = (double)VB->Win.data[v1][1] - (double)VB->Win.data[v0][1];
        double ez = (double)VB->Win.data[v1][2] - (double)VB->Win.data[v0][2];
        double fx = (double)VB->Win.data[v2][0] - (double)VB->Win.data[v0][0];
        double fy = (double)VB->Win.data[v2][1] - (double)VB->Win.data[v0][1];
        double fz = (double)VB->Win.data[v2][2] - (double)VB->Win.data[v0][2];
        double pa = ey * fz - ez * fy;
        double pb = ez * fx - ex * fz;
        double pc = ex * fy - ey * fx;

        if (pc * pc > 1e-16) {
            double ac = pa / pc;
            double bc = pb / pc;
            double m;

            if (ac < 0.0) ac = -ac;
            if (bc < 0.0) bc = -bc;
            m = (ac > bc) ? ac : bc;
            zoffset = m * (double)ctx->Polygon.OffsetFactor
                      + (double)ctx->Polygon.OffsetUnits;
        }
    }

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
        n = OSMGAMesaBuildTriangleTex(&a, &b, &c, &prov, zmode,
                                      ctx->Depth.Mask == GL_TRUE, blend,
                                      texOn ? &tex : (const OSMGAMesaTex *)0,
                                      zoffset, built, tmr);
    } else {
        n = OSMGAMesaBuildTriangleTex(&a, &b, &c, (const OSMGAMesaVertex *)0,
                                      zmode, ctx->Depth.Mask == GL_TRUE, blend,
                                      texOn ? &tex : (const OSMGAMesaTex *)0,
                                      zoffset, built, tmr);
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
     * One batch.  Textured or not.
     *
     * A split textured triangle used to go out as two, because the anchors
     * were batch state and the engine re-seeds the horizontal coordinate at
     * every primitive's own first-row left edge -- so the two halves could
     * not share one.  If the second batch was refused the first was already
     * drawn and the software redraw put the whole triangle down again, which
     * wrote those pixels twice.
     *
     * That was harmless only because texturing and blending were never on
     * together: without blending the second write writes the same value.  The
     * depth test was also offered as an argument and is not one -- depth is
     * optional, and a textured triangle drawn without it has no such
     * protection at all.
     *
     * The anchors are per trapezoid now, so both halves go in one batch, the
     * whole triangle is validated before any of it is drawn, and a refusal
     * draws nothing.  That is what lets blending join texturing.
     */
    {
    int j;
    unsigned long texFlagsL = 0UL;
    int batchable;

    /*
     * The texture flags, computed BEFORE the batching decision because they
     * are half of the compatibility key.  The comments that used to sit on
     * each term still hold; see the flush function for where they land.
     */
    if (texOn) {
        const struct gl_texture_object *to =
            ctx->Texture.Unit[0].CurrentD[2];
        const struct gl_texture_image *ti = to->Image[to->BaseLevel];

        texFlagsL =
            ((to->MagFilter == GL_LINEAR) ? OSMGA_HW3D_TEXF_BILIN : 0UL)
            | ((to->MinFilter == GL_LINEAR)
                 ? OSMGA_HW3D_TEXF_BILINMIN : 0UL)
            | ((ti != 0 && ti->Format == GL_RGBA)
               ? OSMGA_HW3D_TEXF_TEXALPHA : 0UL)
            | ((ctx->Texture.Unit[0].EnvMode == GL_MODULATE)
               ? OSMGA_HW3D_TEXF_MODULATE : 0UL)
            | ((to->WrapS == GL_REPEAT) ? OSMGA_HW3D_TEXF_REPEATU : 0UL)
            | ((to->WrapT == GL_REPEAT) ? OSMGA_HW3D_TEXF_REPEATV : 0UL)
            | ((tmr[0][8] != 0L) ? OSMGA_HW3D_TEXF_PERSP : 0UL);
    }

    /*
     * May this triangle JOIN a batch?  Not when the VB clips -- clipped
     * triangles borrow temporary vertices a later clip can overwrite, so
     * their replay indices are only good RIGHT NOW -- and not under a
     * multipass driver, which repeats the loop before the bracket closes.
     * Either way it still goes through the engine; it just travels alone,
     * exactly as every triangle used to.
     */
    batchable = (VB->ClipOrMask == 0) && (ctx->Driver.MultipassFunc == 0);

    if (pendTraps != 0UL) {
        int part = 0;

        if (pendCtx != ctx || pendVB != (void *)VB) {
            hookFlushOther++;
            part = 1;
        } else if (pendTraps + (unsigned long)n > hookBatchLimit ||
                   pendSrcCount >= hookBatchLimit) {
            hookFlushFull++;
            part = 1;
        } else if (texOn && pendHasTex &&
                   (pendTexOrg != texOrg || pendTexW != texW ||
                    pendTexH != texH || pendTexPitch != texPitch ||
                    pendTexFlags != texFlagsL ||
                    pendTmr[0] != tmr[0][0] || pendTmr[1] != tmr[0][1] ||
                    pendTmr[2] != tmr[0][2] || pendTmr[3] != tmr[0][3] ||
                    pendTmr[4] != tmr[0][4] || pendTmr[5] != tmr[0][5])) {
            hookFlushKey++;
            part = 1;
        }
        if (part)
            osmgaMesaFlushPending();
    }

    if (pendTraps == 0UL) {
        pendCtx = ctx;
        pendVB = (void *)VB;
        pendHasTex = 0;
    }
    if (texOn && !pendHasTex) {
        pendHasTex = 1;
        pendTexOrg = texOrg;
        pendTexW = texW;
        pendTexH = texH;
        pendTexPitch = texPitch;
        pendTexFlags = texFlagsL;
        for (j = 0; j < 6; j++)
            pendTmr[j] = tmr[0][j];
    }
    for (j = 0; j < n; j++)
        batch->tri[pendTraps + (unsigned long)j] = built[j];
    pendTraps += (unsigned long)n;
    pendSrc[pendSrcCount].v0 = v0;
    pendSrc[pendSrcCount].v1 = v1;
    pendSrc[pendSrcCount].v2 = v2;
    pendSrc[pendSrcCount].pv = pv;
    pendSrcCount++;

    if (!batchable)
        osmgaMesaFlushPending();
    }
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
     * The two filters no longer have to agree.
     *
     * This used to require it, on the grounds that the engine had ONE filter
     * switch and no notion of lambda.  It has two -- TEXFILTER holds a
     * minification field and a magnification field -- and it chooses between
     * them itself.  Measured: with the two set differently, a single
     * primitive whose rate crosses one texel per pixel inside it blends on
     * the side above the crossing and point samples on the side below, and
     * the column where it changes is the column python says the rate falls
     * under one.  So the choice is made per fragment, not per primitive.  And
     * it uses BOTH axes, as GL's lambda does: a primitive magnifying in u
     * while minifying in v takes the minification filter, which is the case
     * that would have exposed a one-axis selector.
     *
     * So each filter is checked on its own, and each drives its own field.
     * Checking them separately is not optional now: with the equality gone,
     * testing only MagFilter would let a mipmapped MinFilter through beside
     * an ordinary MagFilter, and the four mipmap filters are still refused --
     * where their levels live has not been measured.
     */
    if (t->MinFilter != GL_NEAREST && t->MinFilter != GL_LINEAR)
        return 0;
    if (t->MagFilter != GL_NEAREST && t->MagFilter != GL_LINEAR)
        return 0;
    /*
     * The wrap rule asks whether EITHER filter is linear, not whether the
     * magnification one is.  GL_CLAMP blends the border colour in under a
     * linear filter and the engine's clamp holds the edge texel instead, so
     * a linear MINIFICATION filter rules GL_CLAMP out just as a linear
     * magnification one does -- and while the two had to be equal, asking
     * about one of them answered for both.
     */
    if (t->MinFilter == GL_NEAREST && t->MagFilter == GL_NEAREST) {
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
    } else {
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
    /*
     * glPolygonOffset is taken.  The engine has no offset unit and needs
     * none: the offset is a constant added to the depth plane, which this
     * back end already solves, and Mesa's software does the same arithmetic
     * rather than reaching for a hardware feature.
     *
     * Only GL_POLYGON_OFFSET_FILL is consulted.  The point and line variants
     * exist in GL and this back end draws neither, and Mesa likewise sets its
     * polygon offset from the fill flag alone.
     */

    /*
     * The engine computes (a*src + (255-a)*dst)/255 and nothing else, which
     * is source alpha against one minus it.  Any other pair of factors, any
     * other equation, is refused rather than approximated -- a blend that is
     * nearly right is a picture that is wrong.
     */
    if ((ctx->RasterMask & BLEND_BIT) != 0) {
        /*
         * The factors the engine has, by name.
         *
         * Nine source values and eight destination ones, which are GL 1.1's
         * two sets for those two roles.  A whitelist and not a mapping,
         * because this Mesa accepts MORE than GL 1.1 does -- SRC_COLOR and
         * its complement as SOURCE factors under the blend-square extension,
         * and the whole constant-colour family, which is enabled by default.
         * A mapping would have sent those somewhere.
         *
         * GL_SRC_ALPHA_SATURATE is refused, and it has now been MEASURED
         * rather than left alone.
         *
         * GL asks for a split: min(As, 1 - Ad) on the colour channels and
         * exactly one on alpha, which is what this Mesa implements
         * (Mesa-3.4.2/src/blend.c:550 and :616).  The engine does have an
         * encoding named for it -- AC_src_src_alpha_sat, source factor 8,
         * and only ever a SOURCE factor, which is GL's own rule
         * (mgareg_flags.h:50) -- and the validator already admits it.
         *
         * The card was asked (the tsa probe, five rows of As against Ad with
         * the destination factor at ZERO).  Source factor 8 puts SOURCE
         * ALPHA on the colour channels and exactly ONE on alpha.  It never
         * takes the minimum: 255 - Ad does not enter, on any row, to the
         * level.  Nor is it blind to the destination -- the same probe's
         * factor 7 control reads Ad back correctly on every row, including
         * Ad of 200.
         *
         * So the encoding is real and it is not this one.  What it actually
         * is is the source half of glBlendFuncSeparate(SRC_ALPHA, ONE),
         * which GL 1.1 cannot ask for and this contract does not offer.
         */
        switch (ctx->Color.BlendSrcRGB) {
        case GL_ZERO: case GL_ONE:
        case GL_DST_COLOR: case GL_ONE_MINUS_DST_COLOR:
        case GL_SRC_ALPHA: case GL_ONE_MINUS_SRC_ALPHA:
        case GL_DST_ALPHA: case GL_ONE_MINUS_DST_ALPHA:
            break;
        default:
            return NULL;
        }
        switch (ctx->Color.BlendDstRGB) {
        case GL_ZERO: case GL_ONE:
        case GL_SRC_COLOR: case GL_ONE_MINUS_SRC_COLOR:
        case GL_SRC_ALPHA: case GL_ONE_MINUS_SRC_ALPHA:
        case GL_DST_ALPHA: case GL_ONE_MINUS_DST_ALPHA:
            break;
        default:
            return NULL;
        }
        /*
         * The alpha factors are stored separately and can be set separately,
         * so checking only the colour ones would let a state through whose
         * alpha the engine blends by the colour rule.  The software driver's
         * own fast path checks the same two, which is where the omission
         * showed.
         */
        /*
         * One field for colour and alpha both, so the two pairs have to be
         * the same pair.  glBlendFunc sets all four together and cannot
         * break this; glBlendFuncSeparateEXT can, and this Mesa has it.
         *
         * It used to hold each of the four to one value, so the equality was
         * true by accident.  Widening the first two without saying this would
         * have let an alpha factor be silently replaced by a colour one.
         */
        if (ctx->Color.BlendSrcA != ctx->Color.BlendSrcRGB)   return NULL;
        if (ctx->Color.BlendDstA != ctx->Color.BlendDstRGB)   return NULL;
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
        /*
         * Seven comparisons, and GL_NEVER is not one of them: the engine's
         * field has eight values and the documentation names seven, with
         * value 1 unnamed and therefore not offered.  Nothing here writes
         * nothing, so GL_NEVER would have to be emulated by not drawing, and
         * refusing is what this back end does with what it cannot express.
         */
        switch (ctx->Depth.Func) {
        case GL_LESS: case GL_LEQUAL: case GL_GREATER: case GL_GEQUAL:
        case GL_EQUAL: case GL_NOTEQUAL: case GL_ALWAYS:
            break;
        default:
            return NULL;
        }
        /*
         * Testing without writing IS expressible, and the answer was in the
         * access type all along.  ZI compares and writes; I compares and does
         * not.  The probe asked the card: atype I with ZLT against a buffer
         * cleared to 0x8000 drew the whole band at 0x4000, none of the band
         * at 0xC000, and moved no depth at all, while the ZI control in the
         * same run wrote every depth it was asked to.
         *
         * PLNWT, which looked like the place this would have to live, is set
         * wide open on every submission and is not part of the batch.  It is
         * not needed: the write is not masked, it is simply not made.
         */
        if (ctx->Visual->DepthBits != 16)           return NULL;
        if (OSMGAMesaBufferDepthOrigin() == 0UL)    return NULL;
    }

    /*
     * Texturing, when the state is one that can be reproduced.
     *
     * Blending used to be refused alongside it, and the reason was
     * structural: a split textured triangle went out as two batches, so a
     * refused second half left the first drawn and the software redraw wrote
     * those pixels a second time -- harmless only because without blending
     * the second write writes the same value.
     *
     * The anchors are per trapezoid now, the whole triangle goes in one
     * batch, and a refusal draws nothing.  Measured, not argued: probe
     * section 79 refuses a deliberately bad second trapezoid and finds the
     * first one's colour AND depth untouched, with a positive control
     * proving that trapezoid does write both when it is submitted alone.
     *
     * Which alpha the blend then uses is settled above, and settled by
     * measurement too -- section 81, which had to turn the combiner's own
     * modulate on before the two candidate readings said different things.
     */
    if ((ctx->RasterMask & (GLuint)TEXTURE_BIT) != 0) {
        if (!osmgaMesaTexStateOK(ctx))
            return NULL;
    }

    /*
     * The alpha test, when it is one the engine has.
     *
     * Seven of GL's eight, the same shape as the depth comparisons: value one
     * of the mode field is unnamed, so GL_NEVER has no encoding and is
     * refused rather than emulated by not drawing.
     */
    if ((ctx->RasterMask & (GLuint)ALPHATEST_BIT) != 0) {
        switch (ctx->Color.AlphaFunc) {
        case GL_LESS: case GL_LEQUAL: case GL_GREATER: case GL_GEQUAL:
        case GL_EQUAL: case GL_NOTEQUAL: case GL_ALWAYS:
            break;
        default:
            return NULL;
        }
    }

    /*
     * The scissor is taken: it is the engine's destination clip narrowed.
     *
     * The submit path programs CXBNDRY, YTOP and YBOT before every batch
     * anyway, to bound the offscreen surface, so a scissor is that same clip
     * intersected with the client's box -- and the kernel intersects rather
     * than trusts, which is why nothing about the box is checked here.  YTOP
     * and YBOT go in as row times pitch with YDSTORG zero, which is what the
     * DRM does for its own 3D dispatch (scratch/mga-drm/mga_state.c:67); the
     * destination origin the list sets later is not part of that comparison.
     *
     * Measured, not assumed: a 32 by 24 box over a quad covering 4928 pixels
     * leaves exactly the 768 python says it should, all nine points either
     * side of the four edges land right, an empty box and a box off the
     * surface draw nothing, and a box hanging off the corner keeps just the
     * overlap -- every one of them agreeing with the software rasteriser to
     * the pixel.  So the clipper does bite on TEXTURE_TRAP, and the note by
     * the DMA block that leans on it for containment is entitled to.
     *
     * What that test first reported was the opposite, and the fault was its
     * own: it cleared with the scissor still on, so everything outside the
     * box kept the previous run's frame and the mirror handed it back.  The
     * clear there is unscissored now, and the reason is written down beside
     * it.
     */
    if ((ctx->RasterMask &
         ~(GLuint)(ALPHABUF_BIT | DEPTH_BIT | BLEND_BIT | TEXTURE_BIT |
                   ALPHATEST_BIT | SCISSOR_BIT)) != 0)
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
 * ---- Did this render bracket WRITE anything, or only read? ----
 *
 * glReadPixels is bracketed by RenderStart and RenderFinish just as drawing
 * is (readpix.c), and this back end mirrors the whole surface at
 * RenderFinish.  So a pure read cost a full copy -- 147 ms at 512 by 384 --
 * and worse: if the caller handed its own OSMesa array as the destination of
 * the read, the mirror then wrote the surface over the top of what had just
 * been read into it, in the surface's packing rather than the one asked for.
 *
 * A dirty RECTANGLE cannot be had here -- OSMesa installs direct writers for
 * lines and for depth-tested triangles that touch the pixels without going
 * through any callback -- but a BOOLEAN can, and a boolean is all this needs.
 * Those direct writers live only on the primitive path, and a primitive
 * bracket never issues a read span.  So "a read happened and no write did"
 * cannot be true of a bracket that drew, which is the only bracket whose
 * writes are invisible.  Anything unrecognised mirrors, as before.
 *
 * The wrapping is done HERE, from RenderStart, and not from the state update
 * where everything else is hooked: osmesa_update_state calls the back end's
 * hook BEFORE it installs these callbacks, so a wrapper put on there is
 * overwritten immediately.  RenderStart is used by every path that touches
 * the buffers -- accum, bitmap, clear, copypix, drawpix, readpix, teximage --
 * so it is early enough for all of them.
 */
static const GLcontext *spanCtx;
static int spanWrote, spanRead;
/* Whether THIS bracket is the one a whole-surface clear armed, and what it
 * would deliver.  Set by the soil that opens the bracket, spent by the
 * mirror that closes it. */
static int uniformBracket;
static unsigned long uniformWord;
static unsigned long uniformDrawn;

static void (*prevWriteRGBASpan)(const GLcontext *, GLuint, GLint, GLint,
                                 CONST GLubyte [][4], const GLubyte []);
static void (*prevWriteRGBSpan)(const GLcontext *, GLuint, GLint, GLint,
                                CONST GLubyte [][3], const GLubyte []);
static void (*prevWriteMonoRGBASpan)(const GLcontext *, GLuint, GLint, GLint,
                                     const GLubyte []);
static void (*prevWriteRGBAPixels)(const GLcontext *, GLuint, const GLint [],
                                   const GLint [], CONST GLubyte [][4],
                                   const GLubyte []);
static void (*prevWriteMonoRGBAPixels)(const GLcontext *, GLuint,
                                       const GLint [], const GLint [],
                                       const GLubyte []);
static void (*prevReadRGBASpan)(const GLcontext *, GLuint, GLint, GLint,
                                GLubyte [][4]);
static void (*prevReadRGBAPixels)(const GLcontext *, GLuint, const GLint [],
                                  const GLint [], GLubyte [][4],
                                  const GLubyte []);

static void
osmgaWriteRGBASpan(const GLcontext *ctx, GLuint n, GLint x, GLint y,
                   CONST GLubyte rgba[][4], const GLubyte mask[])
{
    osmgaMesaFlushPending();
    spanWrote = 1;
    if (prevWriteRGBASpan) (*prevWriteRGBASpan)(ctx, n, x, y, rgba, mask);
}

static void
osmgaWriteRGBSpan(const GLcontext *ctx, GLuint n, GLint x, GLint y,
                  CONST GLubyte rgb[][3], const GLubyte mask[])
{
    osmgaMesaFlushPending();
    spanWrote = 1;
    if (prevWriteRGBSpan) (*prevWriteRGBSpan)(ctx, n, x, y, rgb, mask);
}

static void
osmgaWriteMonoRGBASpan(const GLcontext *ctx, GLuint n, GLint x, GLint y,
                       const GLubyte mask[])
{
    osmgaMesaFlushPending();
    spanWrote = 1;
    if (prevWriteMonoRGBASpan) (*prevWriteMonoRGBASpan)(ctx, n, x, y, mask);
}

static void
osmgaWriteRGBAPixels(const GLcontext *ctx, GLuint n, const GLint x[],
                     const GLint y[], CONST GLubyte rgba[][4],
                     const GLubyte mask[])
{
    osmgaMesaFlushPending();
    spanWrote = 1;
    if (prevWriteRGBAPixels) (*prevWriteRGBAPixels)(ctx, n, x, y, rgba, mask);
}

static void
osmgaWriteMonoRGBAPixels(const GLcontext *ctx, GLuint n, const GLint x[],
                         const GLint y[], const GLubyte mask[])
{
    osmgaMesaFlushPending();
    spanWrote = 1;
    if (prevWriteMonoRGBAPixels)
        (*prevWriteMonoRGBAPixels)(ctx, n, x, y, mask);
}

static void
osmgaReadRGBASpan(const GLcontext *ctx, GLuint n, GLint x, GLint y,
                  GLubyte rgba[][4])
{
    osmgaMesaFlushPending();
    spanRead = 1;
    if (prevReadRGBASpan) (*prevReadRGBASpan)(ctx, n, x, y, rgba);
}

static void
osmgaReadRGBAPixels(const GLcontext *ctx, GLuint n, const GLint x[],
                    const GLint y[], GLubyte rgba[][4], const GLubyte mask[])
{
    osmgaMesaFlushPending();
    spanRead = 1;
    if (prevReadRGBAPixels) (*prevReadRGBAPixels)(ctx, n, x, y, rgba, mask);
}

/*
 * Idempotent, and it saves only what is not already ours.
 *
 * RenderStart runs again and again while the driver reinstalls its callbacks
 * only on a state update, so saving blindly the second time would save this
 * file's own wrapper and call it forever.  Each one is asked separately
 * because they are reinstalled as a group but need not be.
 */
#define OSMGA_WRAP(field, mine, saved)                                  \
    do {                                                                \
        if (ctx->Driver.field != (mine)) {                              \
            (saved) = ctx->Driver.field;                                \
            ctx->Driver.field = (mine);                                 \
        }                                                               \
    } while (0)

static void
osmgaMesaWrapSpans(GLcontext *ctx)
{
    /*
     * A different context brings its own callbacks, and the saved pointers
     * here belong to whichever one they were taken from.  Forgetting them is
     * the safe direction: the worst it costs is one bracket that mirrors.
     */
    if (spanCtx != ctx) {
        spanCtx = ctx;
        prevWriteRGBASpan = 0;      prevWriteRGBSpan = 0;
        prevWriteMonoRGBASpan = 0;  prevWriteRGBAPixels = 0;
        prevWriteMonoRGBAPixels = 0;
        prevReadRGBASpan = 0;       prevReadRGBAPixels = 0;
    }
    OSMGA_WRAP(WriteRGBASpan,       osmgaWriteRGBASpan,       prevWriteRGBASpan);
    OSMGA_WRAP(WriteRGBSpan,        osmgaWriteRGBSpan,        prevWriteRGBSpan);
    OSMGA_WRAP(WriteMonoRGBASpan,   osmgaWriteMonoRGBASpan,   prevWriteMonoRGBASpan);
    OSMGA_WRAP(WriteRGBAPixels,     osmgaWriteRGBAPixels,     prevWriteRGBAPixels);
    OSMGA_WRAP(WriteMonoRGBAPixels, osmgaWriteMonoRGBAPixels, prevWriteMonoRGBAPixels);
    OSMGA_WRAP(ReadRGBASpan,        osmgaReadRGBASpan,        prevReadRGBASpan);
    OSMGA_WRAP(ReadRGBAPixels,      osmgaReadRGBAPixels,      prevReadRGBAPixels);
}

/*
 * Anything at all is about to be drawn -- by this back end or by the
 * software rasteriser, which writes into the same surface and has no way to
 * announce it.  Marking here rather than only where we draw is what keeps a
 * frame made entirely of refused primitives from being mirrored away as
 * "nothing happened".
 */
static int soilWasSet;
/* How many brackets actually copied.  Without it a test cannot tell "the
 * read did not mirror" from "the mirror was cheap today". */
static unsigned long hookMirrors;

static void
osmgaMesaSoil(GLcontext *ctx)
{
    /*
     * The wrapping goes on here because this is the first thing that runs in
     * a bracket and the only place this back end owns that early -- see the
     * note above.
     */
    osmgaMesaWrapSpans(ctx);
    spanWrote = 0;
    spanRead = 0;

    /*
     * And the mark stays, unconditional, exactly as it was.  A bracket that
     * turns out to have written nothing gives it back at the other end; a
     * bracket whose writes this file cannot see keeps it.  Deciding here
     * instead would mean deciding before anything has happened.
     */
    soilWasSet = OSMGAMesaBufferIsDirty();
    OSMGAMesaBufferSoiled();

    /*
     * And take the clear's mark if there is one.  One shot, taken here and
     * nowhere else, so it can only ever describe the bracket _mesa_Clear
     * opens immediately after Driver.Clear returns -- every other bracket
     * finds it already spent.
     */
    uniformBracket = hookPendingUniform && hookPendingCtx == ctx->DriverCtx;
    uniformWord = hookPendingWord;
    uniformDrawn = hookPendingDrawn;
    hookPendingUniform = 0;
}

static void
osmgaMesaMirror(GLcontext *ctx)
{
    (void)ctx;
    /*
     * The bracket is closing (or a finish/flush was called): anything still
     * accumulated goes to the engine NOW, and a refusal replays through
     * software, all before the mirror looks at the surface -- otherwise the
     * copy would deliver a frame with its last triangles missing.
     */
    if (pendTraps != 0UL)
        hookFlushBracket++;
    osmgaMesaFlushPending();
    /*
     * A bracket that read and did not write is a bracket that changed
     * nothing, so there is nothing to put back -- and putting it back is
     * worse than nothing when the caller handed its own OSMesa array as the
     * destination of the read: the copy would write the surface over the top
     * of what was just read into it, in the surface's own packing rather
     * than the one the caller asked for.
     *
     * The mark goes back to what it was on the way in.  Leaving it set would
     * only buy a copy at the next flush of a surface nobody had changed.
     */
    if (spanRead && !spanWrote) {
        if (!soilWasSet)
            OSMGAMesaBufferUnsoil();
        return;
    }
    /*
     * The bracket a whole-surface clear opened, and nothing in it drew.
     *
     * Two things have to be true, and neither alone is enough.  No span may
     * have been written, because a span write is Mesa drawing into the
     * surface and this file can see that; and no batch may have been
     * submitted since the clear's own, because engine drawing writes no
     * spans and this file cannot see that at all.  Either would mean the
     * surface is no longer one value, and writing one value over the
     * caller's array would quietly lose the drawing.
     */
    if (uniformBracket && !spanWrote && hookDrawn == uniformDrawn) {
        uniformBracket = 0;
        hookUniformFills++;
        OSMGAMesaBufferFill(uniformWord);
        return;
    }
    uniformBracket = 0;
    hookMirrors++;
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

/*
 * Clearing on the engine, which is the larger half of a frame.
 *
 * Measured before it was built, because "the CPU clear is 53% of the frame"
 * is not the same claim as "the engine would be faster".  At 512 by 384 the
 * CPU clear is 161 ms and covering the whole surface through this same
 * accelerated path -- two triangles, two batches, the synchronous submit and
 * the completion wait all included -- is 0.9 ms.  A hundred and seventy-six
 * times, and it takes the frame from 306 ms to about 146.  The other half is
 * the mirror, and that is a separate piece of work.
 *
 * What this may take is narrow on purpose:
 *
 *   colour alone      -- no depth mode, so access type I: colour written and
 *                        depth not touched
 *   colour AND depth  -- zmode ALWAYS with the write on, so access type ZI
 *                        with NOZCMP: both written, unconditionally, in ONE
 *                        pass.  That NOZCMP writes depth is measured, not
 *                        assumed
 *   depth alone       -- NOT taken.  ZI writes colour as well and the only
 *                        thing that could stop it is PLNWT, which the batch
 *                        does not carry.  Mesa keeps it
 *
 * The mask is in DD_ bits, not GL_ ones: Mesa turns GL_COLOR_BUFFER_BIT into
 * ctx->Color.DrawDestMask before it calls this (buffers.c, where ddMask is
 * composed), and reading it as the GL bit would have been simply wrong.
 * What is returned is what is left for Mesa, which is the callback's
 * contract (dd.h).
 *
 * Returns the bits it did.
 */
static GLbitfield
osmgaMesaClearOnEngine(GLcontext *ctx, GLbitfield mask, GLboolean all,
                       GLint x, GLint y, GLint w, GLint h)
{
    OSMGAHW3DBatch *batch = OSMGAMesaProbeBatch();
    OSMGAMesaVertex v[4];
    /* Two builder calls, each allowed two trapezoids -- the same four the
     * triangle path reserves, for the same reason. */
    OSMGAHW3DTri built[4];
    OSMGAHW3DSubmitBlock res;
    unsigned long zmode = OSMGA_MESA_ZMODE_NONE;
    int depthWrite = 0, wantDepth = 0;
    int n0, n1, i;
    double x0, y0, x1, y1;
    unsigned long cr, cg, cb, ca, zcode;

    /*
     * A forced-software pass must be software all the way down.  Without
     * this the triangles would go to Mesa and the CLEAR would still go to
     * the engine, and every test that compares the two paths would be
     * comparing a mixture with itself.
     */
    /*
     * Before ANYTHING else: the clear builds its trapezoids into the same
     * mapped tri[] array the pending batch accumulates in, so pending work
     * must ship first or the clear would overwrite it.
     */
    osmgaMesaFlushPending();
    if (hookForcedSoftware)              { hookClearWhy = 1; return 0; }
    if (batch == 0)                      { hookClearWhy = 2; return 0; }
    if (OSMGAMesaBufferOrigin() == 0UL)  { hookClearWhy = 3; return 0; }
    if ((OSMGAMesaBufferStride() % OSMGA_HW3D_PITCH_ALIGN) != 0UL)
        { hookClearWhy = 4; return 0; }
    if (!ctx->Visual->RGBAflag)          { hookClearWhy = 5; return 0; }

    /*
     * The engine has no colour write mask, so a masked clear is Mesa's: it
     * reads the destination, merges the channels it may write and puts them
     * back, which nothing here can express.  The bytes are each 0xff or
     * nought, so all four are asked.
     */
    if (ctx->Color.ColorMask[0] != 0xff || ctx->Color.ColorMask[1] != 0xff ||
        ctx->Color.ColorMask[2] != 0xff || ctx->Color.ColorMask[3] != 0xff)
        { hookClearWhy = 6; return 0; }

    /*
     * Only the one colour destination this back end owns.  Anything else --
     * a back buffer, both buffers at once -- is a surface we do not draw to,
     * and taking the bit would leave it uncleared.
     */
    if (ctx->Color.DrawDestMask != (GLuint)DD_FRONT_LEFT_BIT)
        { hookClearWhy = 7; return 0; }
    if ((mask & (GLbitfield)DD_FRONT_LEFT_BIT) == 0)
        { hookClearWhy = 8; return 0; }

    /*
     * Mesa clears its software alpha buffer alongside the colour one.  We
     * cannot, so if there is one, the colour bit is not ours to take.
     */
    if (ctx->DrawBuffer->UseSoftwareAlphaBuffers)
        { hookClearWhy = 9; return 0; }

    if ((mask & (GLbitfield)DD_DEPTH_BIT) != 0 &&
        ctx->Visual->DepthBits == 16 &&
        OSMGAMesaBufferDepthOrigin() != 0UL) {
        wantDepth = 1;
        depthWrite = 1;
        zmode = OSMGA_MESA_ZMODE_ALWAYS;
    }

    if (all) {
        x0 = 0.0;
        y0 = 0.0;
        x1 = (double)OSMGAMesaBufferWidth();
        y1 = (double)OSMGAMesaBufferHeight();
    } else {
        if (w <= 0 || h <= 0) { hookClearWhy = 10; return 0; }
        x0 = (double)x;
        y0 = (double)y;
        x1 = (double)(x + w);
        y1 = (double)(y + h);
    }

    /*
     * Mesa's own quantisation, to the letter -- a truncating multiply by 255
     * for colour and by DepthMax for depth.  Rounding differently here would
     * make the accelerated clear a different colour from the software one at
     * a handful of values, which is exactly the kind of difference that is
     * found six months later.
     */
    cr = (unsigned long)(GLint)(ctx->Color.ClearColor[0] * 255.0F);
    cg = (unsigned long)(GLint)(ctx->Color.ClearColor[1] * 255.0F);
    cb = (unsigned long)(GLint)(ctx->Color.ClearColor[2] * 255.0F);
    ca = (unsigned long)(GLint)(ctx->Color.ClearColor[3] * 255.0F);
    zcode = (unsigned long)(GLushort)(ctx->Depth.Clear *
                                      (GLfloat)ctx->Visual->DepthMax);

    for (i = 0; i < 4; i++) {
        memset(&v[i], 0, sizeof v[i]);
        v[i].r = cr; v[i].g = cg; v[i].b = cb; v[i].a = ca;
        v[i].z = zcode * (unsigned long)OSMGA_MESA_SUBONE;
        v[i].qw = 1.0;
        v[i].tq = 1.0;
    }
    v[0].x = osmgaFix(x0); v[0].y = osmgaFix(y0);
    v[1].x = osmgaFix(x1); v[1].y = osmgaFix(y0);
    v[2].x = osmgaFix(x1); v[2].y = osmgaFix(y1);
    v[3].x = osmgaFix(x0); v[3].y = osmgaFix(y1);

    n0 = OSMGAMesaBuildTriangle(&v[0], &v[1], &v[2], &v[0], zmode, depthWrite,
                                OSMGA_MESA_BLEND_OPAQUE, 0.0, built);
    if (n0 < 0) { hookClearWhy = 11; return 0; }
    n1 = OSMGAMesaBuildTriangle(&v[0], &v[2], &v[3], &v[0], zmode, depthWrite,
                                OSMGA_MESA_BLEND_OPAQUE, 0.0, built + n0);
    if (n1 < 0) { hookClearWhy = 12; return 0; }
    if (n0 + n1 == 0) { hookClearWhy = 13; return 0; }

    batch->magic = OSMGA_HW3D_MAGIC;
    batch->version = OSMGA_HW3D_VERSION;
    batch->triCount = (unsigned long)(n0 + n1);
    /*
     * The trapezoids themselves, which is not a formality: the batch is a
     * mapped buffer this library reuses, so declaring a count without
     * writing the primitives leaves the last submission's in slot nought and
     * NOTHING in slot one.  That is exactly what happened -- the driver
     * refused with a drawing-control verdict on a triangle of all zeroes,
     * and the clear silently fell back to Mesa every frame while the timing
     * said nothing had changed.
     */
    for (i = 0; i < n0 + n1; i++)
        batch->tri[i] = built[i];
    osmgaMesaBatchUntextured(batch);
    hookClears++;
    if (osmgaMesaSubmitBatch(ctx, batch, &res) != 0) {
        /*
         * Refused, so Mesa clears it instead -- which is safe in a way a
         * refused TRIANGLE is not.  A triangle that may already be half
         * drawn must not be drawn again; a clear that may already be half
         * done can be finished by writing the same colour over it, because
         * the second pass does not depend on what the first left behind.
         */
        hookClearWhy = 14;
        return 0;
    }
    hookClearWhy = 0;
    OSMGAMesaBufferSoiled();
    /*
     * The surface now holds one value, and _mesa_Clear is about to open a
     * render bracket whose only other business is clearing buffers we did
     * not take -- none of which is this surface.  So that bracket's copy can
     * deliver the value instead of reading the surface back, which is two
     * hundred and fifty times cheaper.
     *
     * Armed only when the clear covered the WHOLE surface: a scissored one
     * leaves the rest of the surface holding whatever it held.
     *
     * Armed only when the word is this context's: there is one of these and
     * there can be more than one context, and delivering another context's
     * clear colour would be worse than reading the surface.
     *
     * The batch count is remembered so that a bracket which draws anything
     * on the engine -- which writes no spans and so cannot be noticed any
     * other way -- takes the ordinary copy.  That is the failure that would
     * lose a frame silently, so it is closed twice: here, and by the span
     * test at the other end.
     */
    if (all && hookClearPixelCtx == ctx->DriverCtx) {
        hookPendingUniform = 1;
        hookPendingWord = hookClearPixel;
        hookPendingCtx = ctx->DriverCtx;
        hookPendingDrawn = hookDrawn;
        hookUniformArmed++;
    }
    return (GLbitfield)(DD_FRONT_LEFT_BIT |
                        (wantDepth ? DD_DEPTH_BIT : 0));
}

static GLbitfield
osmgaMesaClear(GLcontext *ctx, GLbitfield mask, GLboolean all,
               GLint x, GLint y, GLint w, GLint h)
{
    GLbitfield left = mask & ~osmgaMesaClearOnEngine(ctx, mask, all,
                                                     x, y, w, h);

    if (left != 0 && osmgaMesaPrevClear != 0)
        left = (*osmgaMesaPrevClear)(ctx, left, all, x, y, w, h);
    OSMGAMesaBufferSoiled();
    return left;
}

void
OpenStepMesaAccelUpdateState(GLcontext *ctx, int rowLength, int yUp)
{
    triangle_func f;

    /*
     * A state update can rebind the surface (the import would overwrite it),
     * or uninstall this back end (savedTriangle goes away, and the replay
     * needs it).  Either way pending work ships first, while everything it
     * depends on is still standing.
     */
    osmgaMesaFlushPending();

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

/*
 * The port telling us what its software clear would write.
 *
 * Called from OSMesa's own ClearColor, which is where that word is computed,
 * and once more when a surface is taken -- so the value is right even for a
 * context that never calls glClearColor, whose clear colour is the default
 * and whose packed word is nought.
 */
void
OpenStepMesaAccelClearPixel(void *ctx, unsigned long word)
{
    hookClearPixel = word;
    hookClearPixelCtx = ctx;
}

unsigned long OSMGAMesaHookUniformFills(void) { return hookUniformFills; }
unsigned long OSMGAMesaHookReplayed(void)     { return hookReplayed; }
void
OSMGAMesaHookFlushCounts(unsigned long out[4])
{
    out[0] = hookFlushBracket;
    out[1] = hookFlushKey;
    out[2] = hookFlushFull;
    out[3] = hookFlushOther;
}
unsigned long OSMGAMesaHookUniformArmed(void) { return hookUniformArmed; }
unsigned long OSMGAMesaHookDrawn(void)    { return hookDrawn; }
unsigned long OSMGAMesaHookClears(void)   { return hookClears; }
unsigned long OSMGAMesaHookMirrors(void)  { return hookMirrors; }
int           OSMGAMesaHookClearWhy(void) { return hookClearWhy; }
unsigned long OSMGAMesaHookDeclined(void) { return hookDeclined; }
unsigned long OSMGAMesaHookSoftware(void) { return hookSoftware; }
unsigned long OSMGAMesaHookHardState(void) { return hookHardState; }
unsigned long OSMGAMesaHookSoftState(void) { return hookSoftState; }
unsigned long OSMGAMesaHookTexPersp(void)  { return hookTexPersp; }
unsigned long OSMGAMesaHookTexAbsent(void) { return hookTexAbsent; }
void OSMGAMesaHookForceSoftware(int on)    { hookForcedSoftware = on; }
unsigned long OSMGAMesaHookBatches(void)   { return hookBatches; }
unsigned long OSMGAMesaHookTraps(void)     { return hookTraps; }
unsigned long OSMGAMesaHookUnsupported(void) { return hookUnsupported; }
unsigned long OSMGAMesaHookVerdictCount(unsigned long v)
{
    return (v < OSMGA_MESA_VERDICTS) ? hookVerdictCount[v] : 0UL;
}
const OSMGAMesaRefusal *OSMGAMesaHookLastRefusal(void)
{
    return &hookLastRefusal;
}
