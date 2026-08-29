/*
 * OpenStepMGAMesaHook.c - see the header.
 *
 * The shape of this file follows osmesa.c's own: a chooser that returns a
 * function or NULL, and a triangle function that reads the vertex buffer.
 * Deviating from that would mean guessing at conventions the software driver
 * already demonstrates.
 */

#include <math.h>
#include <stdlib.h>   /* getenv, for the WARP tier switch */
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
#include "OpenStepMGAMesaWarp.h"
#include "OpenStepMGAMesaHook.h"


static unsigned long hookDrawn;
/*
 * Of hookDrawn, the ones the WARP tier drew.  The split matters because a
 * pure-hardware gate cannot tell a WARP run from one that fell back to
 * trapezoids -- both are "the engine drew it" -- and a test that means to
 * exercise this tier has to be able to insist on it.
 */
static unsigned long hookWarp;
/*
 * Sources that went INTO a WARP submission, whatever became of it.
 *
 * hookWarp counts what the tier drew, so it moves only after a submission
 * the driver took -- which makes it useless for the one test that matters
 * most here: a run in which EVERY batch is refused reports warp 0 and
 * cannot even say the tier was involved.  This is that statement.
 */
static unsigned long hookWarpTried;
/*
 * The largest WARP batch ever submitted, in the allocator's OWN units.
 *
 * Counting input triangles and dividing is not the same question: a clipped
 * VB is never batched (the gate submits it immediately), but the run count
 * depends on how often dwgctl or alphactrl moved, which no triangle count
 * can say.  Both capacities have to be watched in the units they are
 * spent in.
 */
static unsigned long hookWarpVtxMax, hookWarpRunMax;
/*
 * Test only: a capacity SMALLER than the real one, so the full-batch path
 * can run.
 *
 * Measured: Mesa's immediate buffer is VB_MAX = 216 + VB_START vertices and
 * gl_Begin flushes at it, so a batch never exceeds seventy-two triangles
 * and the largest actually seen is sixty-four -- a quarter of WARP's 240.
 * The full-batch branch (return FULL, flush, reset, retry the same source)
 * therefore cannot execute from immediate-mode GL at all, and untested
 * error handling is where this kind of defect lives.
 *
 * This lowers the ADMISSION limit and nothing else: the real buffers,
 * encoder, submission, reset and retry all run, so the branch taken is the
 * production one.  What it cannot establish is the physical 720-vertex
 * boundary -- descriptor sizes, counter widths, microcode behaviour on a
 * large packet.  That needs a bigger frontend or a direct harness, and this
 * is not a substitute for it.
 */
static unsigned long hookWarpVtxCap = OSMGA_HW3D_MAX_VTX;
static unsigned long hookWarpRunCap = OSMGA_HW3D_MAX_RUN;
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
/* see OSMGAMesaHookForceTrapezoid -- the same idea, one tier along */
static int hookForcedTrapezoid;

/*
 * MEASUREMENT ARMS.  Test-only, and they make the picture WRONG on purpose --
 * that is what they are for.  docs/W2_WARP_RENDER_PATH_PLAN.md section 3.
 *
 * The builder runs in osmgaMesaTriangle and the submit in the flush, and they
 * are DIFFERENT FUNCTIONS -- which is why arms C and D need no kernel change,
 * no ABI change and no reboot.  Only the dry-run arm (B) does.
 */
/*
 * THE MEASUREMENT ARMS ARE TEST MACHINERY AND DO NOT SHIP.
 *
 * Arms B, C and D each remove one stage of the submission so the frame can
 * be timed with it gone; the whole point is that they draw the wrong thing,
 * or nothing.  A release library must not be able to be talked into any of
 * them, so the selector does not exist there at all -- OSMGA_ARM() is the
 * constant 0 and every test on it is compiled away.  That is stronger than
 * leaving a variable nothing sets: there is nothing to set.
 *
 * The build enforces it from both ends (tools/build-matrox-mesa.csh), as it
 * already does for InjectNamed: the release archive is refused if these
 * symbols appear and the test archive is refused if they do not.
 */
#ifdef OSMGA_MESA_TESTHOOKS
static int hookMeasureArm;
static unsigned long hookDryStatus;   /* what the last dry submission answered */
static unsigned long hookDryCount;    /* how many were made */
#define OSMGA_ARM(n)  (hookMeasureArm == (n))
#else
#define OSMGA_ARM(n)  0
#endif
/*
 *   3  arm B: the ioctl runs and the kernel validates and encodes, then
 *      returns without ringing the doorbell.  B minus C is the kernel's
 *      per-trapezoid work; A minus B is entry, state, the doorbell and the
 *      engine.  This is the only arm that needs the driver, because both
 *      halves scale with the trapezoid count and no outside sweep can tell
 *      them apart.
 */

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
static __inline__ long
osmgaFix(double v)
{
    /* The call rounded the argument to 64 bits on the way in; inlined, the
     * caller's expression could stay 80 in an x87 register.  See the note
     * beside the leaves in OpenStepMGAMesaTriangle.c. */
    volatile double vv = v;

    /*
     * The bound is the one the conversion below can actually take, not a
     * round number.  It used to be 1e30, which lets through everything the
     * cast cannot hold: v * 256 passes LONG_MAX at about 8.4 million, and a
     * double-to-long conversion out of range is undefined -- on this FPU it
     * yields the indefinite integer, which the coordinate check downstream
     * happens to refuse, by luck rather than by design.
     */
    if (!(vv > -8.0e6) || !(vv < 8.0e6))
        return 0L;
    /*
     * floor is a library call, and the profile put 6.6% of user time in it.
     * It is not needed: the result is going to a long anyway, and a cast
     * already truncates.  The two differ only in direction -- floor goes
     * toward minus infinity, a cast toward nought -- so they agree for a
     * non-negative value and differ by exactly one for a negative one that
     * is not already whole.  That is the whole of the fixup.
     *
     * The cast is in range because the guard above is: 8.0e6 * 256 + 0.5 is
     * 2048000000.5 against a signed limit of 2147483647.  Checked over
     * 300000 random values in that range and at the boundaries -- including
     * negative zero, the half-integers and the two extremes -- with no
     * disagreement.
     */
    {
        double t = vv * (double)OSMGA_MESA_SUBONE + 0.5;
        long   i = (long)t;                    /* toward nought */

        return (t < 0.0 && (double)i != t) ? i - 1L : i;
    }
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
/*
 * How much of a trapezoid the kernel would not have to write, if it wrote
 * only what changed.  Test only, and a MEASUREMENT rather than a change:
 * the encoder is the kernel's and this only counts what it would skip.
 *
 * The kernel emits a fixed seven blocks a trapezoid (eight when textured),
 * five dwords each, whatever the values are -- and the measurement that
 * matters says the engine's wait tracks the dword count.  So before writing
 * a change-tracking encoder it is worth knowing what a real frame's
 * trapezoids actually have in common.  These count, over every trapezoid
 * this library submits, how many of the twenty-five register values differ
 * from the previous trapezoid in the same batch.
 *
 * The first trapezoid of a batch counts as all-changed: the kernel's state
 * before it is the previous batch's, and nothing here may assume otherwise.
 */
static unsigned long deltaTraps, deltaChanged, deltaBlocksNow, deltaBlocksMin;
static unsigned long deltaBlockDirty[7];   /* how often each block changes */
static unsigned long deltaRegDirty[25];    /* and how often each register does */

void
OSMGAMesaHookDeltaStats(unsigned long out[4])
{
    if (out == 0)
        return;
    out[0] = deltaTraps;
    out[1] = deltaChanged;
    out[2] = deltaBlocksNow;
    out[3] = deltaBlocksMin;
}

void
OSMGAMesaHookDeltaRegs(unsigned long out[25])
{
    int k;

    if (out == 0)
        return;
    for (k = 0; k < 25; k++)
        out[k] = deltaRegDirty[k];
}

/*
 * The three parts of it, separately.
 *
 * "Instrumented" turned on three unrelated things at once -- two
 * gettimeofday calls around every submission, a pass over the batch counting
 * which registers changed, and the change-pattern histogram -- and a
 * combination that hangs the machine cannot be narrowed while they move
 * together.  Bit 0 is the timing, bit 1 the counting, bit 2 the histogram;
 * OSMGAMesaHookInstrument(1) sets all three, as it always did.
 */
#define OSMGA_MESA_INST_TIME   1
#define OSMGA_MESA_INST_DELTA  2
#define OSMGA_MESA_INST_MASK   4
#define OSMGA_MESA_INST_AREA   8

/*
 * M20 -- what would a mirror narrowed to the drawn rectangle actually copy?
 *
 * The question is whether that optimisation is worth DESIGNING, and the only
 * honest way to answer it is to accumulate the rectangle a correct
 * implementation would be entitled to copy, without copying anything
 * differently.  The rectangle has to cover every writer, not just this back
 * end's triangles -- and the span wrappers below already sit on the channel
 * every other writer uses (the software rasteriser, glDrawPixels, glBitmap,
 * points, lines), which is why `spanWrote` can be a boolean today.  Here it
 * becomes a box.
 *
 * A whole-surface clear marks the bracket full, and so would anything this
 * file cannot see -- and after the span wrappers there is nothing left that
 * it cannot see, which is a finding rather than an assumption.
 */
static long          areaMinX, areaMaxX, areaMinY, areaMaxY;
static int           areaValid, areaFull;
static unsigned long areaBoxPixels;     /* what (B) would have copied      */
static unsigned long areaAllPixels;     /* what the mirror does copy       */
static unsigned long areaFullBrackets;  /* brackets (B) could not narrow   */
static unsigned long areaBoxBrackets;

/*
 * A clear that the engine took writes the surface without a span and without
 * a vertex, so it is invisible to both accumulators.  It also happens just
 * BEFORE the bracket whose mirror has to deliver it -- the same ordering
 * `uniformBracket` exists for -- so it is carried across as a one-shot and
 * spent by the reset that opens the next bracket.
 */
static int areaPendingFull;

static void
osmgaAreaReset(void)
{
    areaValid = 0;
    areaFull = areaPendingFull;
    areaPendingFull = 0;
}

static void
osmgaAreaAdd(long x0, long y0, long x1, long y1)
{
    if (!areaValid) {
        areaMinX = x0; areaMaxX = x1; areaMinY = y0; areaMaxY = y1;
        areaValid = 1;
        return;
    }
    if (x0 < areaMinX) areaMinX = x0;
    if (x1 > areaMaxX) areaMaxX = x1;
    if (y0 < areaMinY) areaMinY = y0;
    if (y1 > areaMaxY) areaMaxY = y1;
}

/* set by OSMGAMesaHookInstrument; see the note beside the submit site */
static int hookInstrument;

/*
 * Which registers change TOGETHER, not just how often each one changes.
 *
 * The blocks the kernel writes hold four registers each and go out whole if
 * any one of them changed, so what a grouping costs depends on the JOINT
 * pattern and not on the marginals.  Sorting the twenty-five by their own
 * change rate and cutting every four would be sound only if a register that
 * changes rarely never changed while its neighbours held still, and there is
 * no reason to believe that across different families.
 *
 * So this records the whole twenty-five-bit pattern per trapezoid and keeps
 * the commonest ones.  With that table any candidate grouping can be priced
 * exactly, by counting the patterns each group intersects, instead of being
 * argued from averages.
 *
 * Sixty-four slots, linear probing, and everything that does not fit goes to
 * a spill count so the reader can see how much of the traffic the table
 * actually speaks for.
 */
#define OSMGA_MESA_MASKS 64
static unsigned long deltaMaskKey[OSMGA_MESA_MASKS];
static unsigned long deltaMaskCnt[OSMGA_MESA_MASKS];
static unsigned long deltaMaskSpill;

static void
osmgaMesaMaskRecord(unsigned long mask)
{
    unsigned long h = (mask * 2654435761UL) % (unsigned long)OSMGA_MESA_MASKS;
    int probe;

    for (probe = 0; probe < OSMGA_MESA_MASKS; probe++) {
        unsigned long at = (h + (unsigned long)probe) %
                           (unsigned long)OSMGA_MESA_MASKS;

        if (deltaMaskCnt[at] == 0UL) {
            deltaMaskKey[at] = mask;
            deltaMaskCnt[at] = 1UL;
            return;
        }
        if (deltaMaskKey[at] == mask) {
            deltaMaskCnt[at]++;
            return;
        }
    }
    deltaMaskSpill++;
}

void
OSMGAMesaHookDeltaMasks(unsigned long keys[64], unsigned long counts[64],
                        unsigned long *spill)
{
    int k;

    if (keys != 0 && counts != 0)
        for (k = 0; k < OSMGA_MESA_MASKS; k++) {
            keys[k] = deltaMaskKey[k];
            counts[k] = deltaMaskCnt[k];
        }
    if (spill != 0)
        *spill = deltaMaskSpill;
}

void
OSMGAMesaHookDeltaBlocks(unsigned long out[7])
{
    int k;

    if (out == 0)
        return;
    for (k = 0; k < 7; k++)
        out[k] = deltaBlockDirty[k];
}

/*
 * The twenty-five values, in the order the kernel writes them, grouped as it
 * groups them: a block goes out if any register in it changed.
 */
static void
osmgaMesaCountDeltas(const OSMGAHW3DBatch *b)
{
    unsigned long i;

    for (i = 0UL; i < b->triCount; i++) {
        const OSMGAHW3DTri *t = &b->tri[i];
        const OSMGAHW3DTri *p = (i > 0UL) ? &b->tri[i - 1UL] : 0;
        int textured = ((t->dwgctl & 0xFUL) == OSMGA_HW3D_OPCODE_TEX);
        int blocks = 0, changed = 0;
        int k;
        unsigned long tv[25], pv[25];
        /*
         * The kernel's grouping, which is no longer the obvious one: it was
         * chosen by measuring which values change TOGETHER rather than how
         * often each changes on its own.  This table has to move with
         * osmgaDmaGroup in the driver or this counter stops describing what
         * the driver does.  Values 7 (sgn) and 17 (Zstart) ride in the tail
         * block's dead slots and so are always written, like fxbndry.
         */
        static const int blockOf[25] = {
            0, 3, 2, 3,  2, 2, 2, 6,
            4, 1, 0, 4,  1, 1, 4, 4,
            1, 6, 3, 3,  5, 0, 0, 5,
            6
        };
        int blockDirty[7];

        tv[0]=t->dwgctl; tv[1]=(unsigned long)t->ar0;
        tv[2]=(unsigned long)t->ar1; tv[3]=(unsigned long)t->ar2;
        tv[4]=(unsigned long)t->ar4; tv[5]=(unsigned long)t->ar5;
        tv[6]=(unsigned long)t->ar6; tv[7]=(unsigned long)t->sgn;
        tv[8]=t->dr[0]; tv[9]=t->dr[1]; tv[10]=t->dr[2]; tv[11]=t->dr[3];
        tv[12]=t->dr[4]; tv[13]=t->dr[5]; tv[14]=t->dr[6]; tv[15]=t->dr[7];
        tv[16]=t->dr[8]; tv[17]=t->z0; tv[18]=t->zdx; tv[19]=t->zdy;
        tv[20]=t->a0; tv[21]=t->adx; tv[22]=t->ady; tv[23]=t->alphactrl;
        tv[24]=t->fxbndry;
        if (p != 0) {
            pv[0]=p->dwgctl; pv[1]=(unsigned long)p->ar0;
            pv[2]=(unsigned long)p->ar1; pv[3]=(unsigned long)p->ar2;
            pv[4]=(unsigned long)p->ar4; pv[5]=(unsigned long)p->ar5;
            pv[6]=(unsigned long)p->ar6; pv[7]=(unsigned long)p->sgn;
            pv[8]=p->dr[0]; pv[9]=p->dr[1]; pv[10]=p->dr[2]; pv[11]=p->dr[3];
            pv[12]=p->dr[4]; pv[13]=p->dr[5]; pv[14]=p->dr[6]; pv[15]=p->dr[7];
            pv[16]=p->dr[8]; pv[17]=p->z0; pv[18]=p->zdx; pv[19]=p->zdy;
            pv[20]=p->a0; pv[21]=p->adx; pv[22]=p->ady; pv[23]=p->alphactrl;
            pv[24]=p->fxbndry;
        }
        for (k = 0; k < 7; k++) blockDirty[k] = (p == 0) ? 1 : 0;
        {
            unsigned long mask = 0UL;

            for (k = 0; k < 25; k++) {
                if (p == 0 || tv[k] != pv[k]) {
                    changed++;
                    deltaRegDirty[k]++;
                    blockDirty[blockOf[k]] = 1;
                    mask |= 1UL << k;
                }
            }
            if ((hookInstrument & OSMGA_MESA_INST_MASK) != 0)
                osmgaMesaMaskRecord(mask);
        }
        /* YDSTLEN+EXEC always goes out: it is what starts the drawing, and
         * y and h are the trapezoid's own.  It shares block 6 with FXBNDRY. */
        blockDirty[6] = 1;
        for (k = 0; k < 7; k++) {
            blocks += blockDirty[k];
            if (blockDirty[k]) deltaBlockDirty[k]++;
        }
        if (textured) blocks++;             /* the anchors, always */
        deltaTraps++;
        deltaChanged += (unsigned long)changed;
        deltaBlocksNow += (unsigned long)(textured ? 8 : 7);
        deltaBlocksMin += (unsigned long)blocks;
    }
}

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

/*
 * Arm B, and the reason it needs a function of its own.
 *
 * A dry submission is the THIRD OUTCOME: it did what it was asked -- validate
 * and encode -- and then stopped before the engine.  It is neither a success
 * (nothing was drawn) nor a refusal.  Left to the ordinary rc handling it
 * reads as a refusal, the backstop counts eight and revokes, and the arm
 * times a software renderer instead.  That is not hypothetical: it is what
 * happened the first time it was run.
 *
 * dryStatus keeps what the kernel actually said so a run can be checked
 * rather than assumed -- though the stronger check turned out to be the
 * encoded dword count, which arm B reports identical to a normal submission.
 *
 * Returns 1 when an arm handled the submission and set *rc.  In a release
 * build it is not compiled at all, because there is no arm to select.
 *
 * TWO switches, not one, and the pair is the honest statement of what arm B
 * needs.  OSMGA_MESA_TESTHOOKS says this library has arms; OSMGA_HW3D_SUBMIT_DRY
 * says the ioctl exists at all -- it gates the command in OpenStepMGAHW3D.h and
 * the handler in the driver.  Arms C and D need only the first.  Arm B needs
 * both, because a library built with arms against a SHIPPED driver would send
 * a command that driver does not answer.
 */
#if defined(OSMGA_MESA_TESTHOOKS) && defined(OSMGA_HW3D_SUBMIT_DRY)
static int
osmgaMesaArmSubmit(OSMGAHW3DSubmitBlock *res, int *rc)
{
    if (hookMeasureArm != 3)
        return 0;
    hookDryStatus = (unsigned long)OSMGAMesaProbeSubmitDry(res);
    hookDryCount++;
    *rc = 0;
    return 1;
}
#else
#define osmgaMesaArmSubmit(res, rc)  0
#endif

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
 * batch STATE -- the texture block: gradients tmr[0..5], origin, size,
 * pitch, flags, format -- keys a flush: the first textured triangle of a
 * run sets the key and a different key flushes first.  Untextured
 * triangles carry no key and mix with anything.
 *
 * THE SCISSOR IS NOT IN THAT KEY, and this comment used to say it was.
 * It is read from the context at submission time instead, which would be
 * wrong if it could change while work is pending -- the batch would go out
 * with the later box applied to the earlier triangles.
 *
 * It cannot change.  Mesa's glScissor is
 * ASSERT_OUTSIDE_BEGIN_END_AND_FLUSH (Mesa-3.4.2/src/scissor.c:43), so it
 * flushes the vertex buffer, and accumulation never crosses a render
 * bracket.  The guarantee is Mesa's, not this file's, which is exactly why
 * it is written down here: a cross review read the old sentence as
 * evidence that a key existed, and looked for the bug it implied.
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
 * Those three gates are about the INDICES, and for a long time this comment
 * stopped there -- as though an index were the whole of what a software
 * triangle reads.  It is not.  Mesa passes part of its input through the
 * context, and moves it around every callback: the polygon offset is
 * computed just before the call and zeroed just after (vbrender.c:298-304,
 * 324-328), and two-side lighting swings ColorPtr, IndexPtr and Specular
 * between the front and back arrays by this triangle's facing
 * (vbrender.c:306-311), with the render loop putting them back to front
 * before RenderFinish (vbrender.c:714-718).
 *
 * A replay runs after all of that.  So each recorded triangle carries those
 * four values with it and they are put back around its redraw; see pendSrc
 * and the refusal arm of osmgaMesaFlushPending.  Without it a refused batch
 * drew every offset triangle unoffset -- measured, not supposed: the
 * replaystate test read 16384 out of the depth buffer where software left
 * 17408.
 *
 * REENTRANCY.  The flush detaches the pending counts before it submits or
 * replays, and holds a guard, so a replayed software triangle that lands
 * back in these wrappers finds an empty batch instead of recursing.
 */
/*
 * HOW MANY SOURCES CAN BE PENDING.
 *
 * The trapezoid batch holds 180 primitives, so this was sized to that; a
 * WARP batch holds 720 vertices, which is 240 triangles, and every one of
 * those is a source with a replay record.  Sized to the trapezoid maximum
 * it is written past at the 181st WARP source; sizing WARP down to match
 * would cap its batches at three quarters of what they carry.  Sixty more
 * records is about two kilobytes.
 */
#if (OSMGA_HW3D_MAX_VTX / 3UL) > OSMGA_HW3D_MAX_TRI
#define OSMGA_MESA_MAX_PEND_SRC  (OSMGA_HW3D_MAX_VTX / 3UL)
#else
#define OSMGA_MESA_MAX_PEND_SRC  OSMGA_HW3D_MAX_TRI
#endif

/*
 * WHICH SHAPE IS PENDING.
 *
 * The command window holds ONE versioned payload: the version 10
 * validator refuses a WARP batch whose triCount is nonzero, and the WARP
 * layout is a second structure at the same address rather than an
 * extension of the first.  So pending work is trapezoids or vertices and
 * never both, and a triangle wanting the other shape flushes first.
 *
 * That flush is not a cost of the design, it is the ordering: a source
 * WARP cannot take must be drawn AFTER the WARP work already queued and
 * BEFORE anything following it, or blending and equal-depth comparisons
 * come out in the wrong order.
 */
#define OSMGA_MESA_PEND_NONE  0
#define OSMGA_MESA_PEND_TRAP  1
#define OSMGA_MESA_PEND_WARP  2
static int pendMode = OSMGA_MESA_PEND_NONE;

/*
 * THE WARP TIER IS OFF UNTIL SOMEBODY ASKS.
 *
 * Nothing yet says the two tiers draw the same picture -- that is what the
 * mixed-tier A/B is for -- so a build that shipped with this on could
 * change what applications see before anyone had compared them.
 *
 * A library switch and not an instance-table one, deliberately: no
 * capability advertises WARP, so a kernel-side key would leave the hook
 * building version 10 batches that the kernel then refuses, which is
 * repeated replay wearing the costume of an opt-in.
 *
 * And NOT OSMGA_MESA_ACCEL, which turns the VRAM and depth setup off
 * altogether: an A/B between the tiers has to hold everything else equal,
 * and that switch does not.
 *
 * Sampled once, on first use.  It is read where a flush is legal, and the
 * value cannot change under pending work.
 */
#define OSMGA_MESA_WARP_ENV "OSMGA_MESA_WARP"
static int warpTier = -1;            /* -1 not yet sampled */

static int
osmgaMesaWarpWanted(void)
{
    if (warpTier < 0) {
        const char *v = getenv(OSMGA_MESA_WARP_ENV);

        warpTier = (v != 0 && (*v == '1' || *v == 'y' || *v == 'Y' ||
                               *v == 't' || *v == 'T')) ? 1 : 0;
    }
    return warpTier;
}

static unsigned long pendTraps;      /* trapezoids already in batch->tri[] */
static unsigned long pendVerts;      /* vertices already in the WARP arm   */

/*
 * Is anything waiting?  Asked through the mode rather than through
 * pendTraps, which is only one of the two shapes -- every test that used
 * pendTraps alone would have called a full WARP batch empty.
 */
static int
osmgaMesaPendEmpty(void)
{
    if (pendMode == OSMGA_MESA_PEND_WARP)
        return pendVerts == 0UL;
    return pendTraps == 0UL;
}
static unsigned long pendSrcCount;   /* source triangles those came from */
/*
 * The indices, and the state a software redraw of them reads.
 *
 * The four extra fields are not decoration.  Mesa hands a triangle function
 * some of its input through the context rather than through the arguments,
 * and it moves that input around EVERY callback rather than once a bracket:
 * ctx->PolygonZoffset is computed just before the call and zeroed just after
 * (vbrender.c:298-304, 324-328), and two-side lighting points ColorPtr,
 * IndexPtr and Specular at the front or the back array according to this
 * triangle's facing (vbrender.c:306-311).  A replay happens after all that
 * has been undone, so it must bring its own copy.
 */
static struct {
    GLuint        v0, v1, v2, pv;
    GLfloat       zoff;          /* ctx->PolygonZoffset while this ran */
    GLvector4ub  *cptr;          /* VB->ColorPtr, front or back */
    GLvector1ui  *iptr;          /* VB->IndexPtr */
    GLubyte     (*spec)[4];      /* VB->Specular */
    unsigned long firstTrap;     /* where its trapezoids begin in tri[] --
                                    a source's trapezoids are contiguous:
                                    every flush happens BEFORE the append,
                                    and the append is one uninterrupted
                                    loop */
} pendSrc[OSMGA_MESA_MAX_PEND_SRC];
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
/*
 * Work the engine could no longer take, split by what happened to it.
 *
 * hookFlushOther used to carry all three -- ordinary partial flushes, work
 * thrown away, and (after this) work rescued -- so it could not answer the
 * one question that matters: did the picture lose anything?
 */
static unsigned long hookRescued;   /* redrawn in software after a revoke */
static unsigned long hookDropped;   /* nowhere left to draw: lost */
/* The A/B knob: 1 reproduces the old one-triangle-per-submission behaviour
 * exactly, which is what the identical-image comparison runs against. */
static unsigned long hookBatchLimit = OSMGA_HW3D_MAX_TRI;
/*
 * Test-only: run the submission instrumentation.
 *
 * Off by default because it is not free -- see the note at the submit site.
 * A test that wants OSMGAMesaHookSubmitStats' microseconds, or any of the
 * delta counts, has to turn it on and say so in what it reports, because a
 * frame time measured with this set is not the frame time without it.
 */
/* Test-only: corrupt the magic of every flushed batch so the kernel refuses
 * it (E_MAGIC, before anything is drawn) and the replay path runs for real.
 * Nothing sets this but the injection setter, and nothing should. */
static int hookInjectRefusal;

/*
 * A refusal the kernel NAMES, for the narrowing path -- test only.
 *
 * hookInjectRefusal corrupts the batch magic, which fails before any
 * per-triangle validation, so the verdict is E_MAGIC: batch-level, not
 * narrowable, and the flush takes the "cannot place it" branch.  That is the
 * only shape it can make, and it is the wrong one for exercising narrowing --
 * which is why the teapot's `inject` mode revokes and completes cleanly while
 * the scenes that crashed all went through narrowing instead.
 *
 * This one corrupts the OPCODE NIBBLE of the first trapezoid of the batch
 * about to be submitted.  The validator sets *badTri at the top of its
 * per-triangle loop and tests the opcode inside it, so the refusal is
 * E_DWGCTL and it NAMES that trapezoid: narrowable.  And E_DWGCTL is not one
 * of the geometry verdicts the backstop pardons, so a run of them still
 * reaches the revoke -- the combination nothing else can produce, and the one
 * the historical crash needed.
 *
 * It lies about the drawing control and about nothing else: the geometry it
 * hands over is the geometry the builder made.
 */
#ifdef OSMGA_MESA_TESTHOOKS
static unsigned long hookInjectNamed;   /* how many more submits to spoil */
static unsigned long hookInjectedNamed; /* how many were spoiled */
/*
 * WHICH trapezoid to spoil, and it matters more than it looks.
 *
 * Spoiling the first one names the first source in the remainder, so the
 * narrowing computes a prefix of nought and the flush's prefix write -- the
 * statement gdb caught faulting -- is skipped entirely.  A harness that only
 * ever spoiled trapezoid zero could not reach the site it was written for,
 * and for a while this one did not.
 */
static unsigned long hookInjectTrap;
#endif

static int osmgaMesaSubmitBatch(GLcontext *ctx, OSMGAHW3DBatch *batch,
                                OSMGAHW3DSubmitBlock *out);
static void osmgaMesaBatchUntextured(OSMGAHW3DState *st);
static void osmgaMesaFillState(GLcontext *ctx, OSMGAHW3DState *st);
static int osmgaMesaSoftly(GLcontext *ctx, GLuint v0, GLuint v1, GLuint v2,
                           GLuint pv);

/* Refusals narrowed to one source instead of the whole batch, for the test
 * that asserts the split. */
static unsigned long hookNarrowed;

/*
 * Which refusals NAME a triangle.  The validator writes *badTri = i at the
 * top of every per-triangle iteration (OpenStepMGAHW3D.c:461-465), so for
 * the verdicts returned inside that loop the index is exact.  For the
 * batch-level ones -- magic, version, count, the origins, the sizes, the
 * pitch -- it is the default nought and means nothing, and a refusal that
 * does not name a triangle cannot be narrowed to one.  The injection knob
 * corrupts the MAGIC, which is deliberately in the second set: forcing the
 * WHOLE replay is what that knob is for.
 *
 * E_TRIFIELD and E_ALPHACROSS joined the first set with the checks that
 * raise them.  The rest of this note is why that matters:
 *
 * E_TRIFIELD joined the first set at the same time as the check that raises
 * it.  A verdict returned inside the per-triangle loop and missing from this
 * list is treated as batch-level: the whole remainder is replayed instead of
 * the one triangle, and that counts towards revoking acceleration -- so one
 * refused triangle would push towards turning the engine off.
 */
static int
osmgaMesaVerdictNamesTriangle(unsigned long v)
{
    return v == OSMGA_HW3D_E_DWGCTL   || v == OSMGA_HW3D_E_ALPHA
        || v == OSMGA_HW3D_E_TRIROW   || v == OSMGA_HW3D_E_TRICOL
        || v == OSMGA_HW3D_E_TRISLOPE || v == OSMGA_HW3D_E_EDGEDIV
        || v == OSMGA_HW3D_E_TRISGN   || v == OSMGA_HW3D_E_TRICROSS
        || v == OSMGA_HW3D_E_TEXCOORD || v == OSMGA_HW3D_E_TRIEMPTY
        || v == OSMGA_HW3D_E_TRIFIELD || v == OSMGA_HW3D_E_ALPHACROSS;
}

/*
 * One source triangle through Mesa's rasteriser, wearing its own state.
 *
 * The four installs are the replay contract's second half (see the pendSrc
 * comment): Mesa hands a triangle part of its input through the context and
 * moves it every callback, so a replay must bring the values that were live
 * when the triangle was recorded.  The CALLER saves what the context held
 * on the way in and puts it back when it is done replaying -- a flush can
 * run inside a triangle callback that goes on to use these fields.
 */
/*
 * Returns whether the software rasteriser actually took it.  It can decline
 * -- there may be no saved triangle for this context -- and a refusal that
 * was NOT redrawn is not the machinery working, so the caller must be able
 * to tell the two apart before pardoning anything.
 */
static int
osmgaMesaReplaySource(GLcontext *ctx, unsigned long i)
{
    ctx->PolygonZoffset = pendSrc[i].zoff;
    ctx->VB->ColorPtr   = pendSrc[i].cptr;
    ctx->VB->IndexPtr   = pendSrc[i].iptr;
    ctx->VB->Specular   = pendSrc[i].spec;
    return osmgaMesaSoftly(ctx, pendSrc[i].v0, pendSrc[i].v1,
                           pendSrc[i].v2, pendSrc[i].pv);
}

/*
 * The verdicts this back end is EXPECTED to provoke on ordinary geometry.
 *
 * Both are per-triangle boundary judgements the kernel makes about a shape
 * the engine cannot rasterise -- a first row outside the surface, or an edge
 * that leaves it partway down.  A scene simply contains some of these, and
 * more of them at larger surfaces, so a run of them is not evidence that the
 * driver is broken.
 *
 * Everything else still counts: a batch-level verdict, an index the map
 * cannot place, the narrowing budget running out, a prefix that validated a
 * moment ago and then refused.  Those are what the backstop is for, and
 * pardoning them would disable it -- the injected-refusal reproducer raises
 * E_MAGIC precisely so that it still reaches eight.
 */
static int
osmgaMesaGeometryVerdict(unsigned long verdict)
{
    return verdict == (unsigned long)OSMGA_HW3D_E_TRICOL ||
           verdict == (unsigned long)OSMGA_HW3D_E_TRICROSS;
}

/* A refusal happened; count it against the consecutive-refusal backstop.
 * That backstop catches a driver refusing every attempted submission --
 * the narrowing loop's own bound is separate and smaller. */
static void
osmgaMesaCountRefusal(void)
{
    if (++hookRefusedRun >= OSMGA_MESA_REFUSAL_LIMIT)
        OSMGAMesaProbeRevoke("the driver kept refusing batches");
}

/* How many times one flush may narrow before the remainder just goes to
 * software.  Each round consumes at least one source, so this only binds a
 * batch with many bad triangles -- which is not a batch worth optimising. */
#define OSMGA_MESA_NARROW_LIMIT 8UL

/*
 * Ship whatever is pending.  Success soils the surface and counts the
 * sources as drawn; a refusal that names a triangle is narrowed -- the
 * good prefix resubmitted to the engine, the named source replayed in
 * software, the remainder gone around again -- so one bad sliver costs one
 * software triangle instead of thirty; any other refusal replays everything
 * left through software, exactly as the whole batch always did; a failure
 * after validation revokes, exactly as the one-triangle path always has.
 *
 * ORDER IS THE SPINE.  Blending and equal-depth results depend on draw
 * order, so the sequence is always prefix (engine), named source
 * (software), remainder (recursively) -- the source order, exactly.  And
 * the prefix ends at the named SOURCE's first trapezoid, not at the refused
 * trapezoid, which may be the source's second: half a triangle drawn twice
 * is not a narrowing.
 */

/* Forward: the WARP append and flush call it, and it calls the WARP flush. */
static void osmgaMesaFlushPending(void);

/*
 * ---- M11: the WARP arm ----
 *
 * A SEPARATE function rather than a branch inside osmgaMesaFlushPending.
 * That function's own comment calls draw order its spine, and its refusal
 * narrowing is a recursion over trapezoid indices; threading a second
 * shape through it would put both at risk to save a little duplication.
 *
 * What is shared is what matters: the replay records, the three gates, the
 * reentrancy guard, the revoke-versus-fork distinction, and the bracket
 * that puts the four context values back.
 */
static OSMGAMesaWarpBuilder warpBuild;

/*
 * Would the WARP tier take this primitive?
 *
 * Asked HERE, before anything is appended, because the assembler reports
 * only FULL -- policy refusal happens in the kernel validator, and by then
 * the batch is assembled and a refusal costs every source in it.  This is
 * what makes case (a) a single-triangle fallback rather than a batch-wide
 * one.
 *
 * The rules are the kernel's own functions, not a second copy of them.
 */
static int
osmgaMesaWarpTakes(const OSMGAHW3DState *st, unsigned long dwgctl,
                   unsigned long alphactrl)
{
    OSMGAHW3DRun probe;

    probe.dwgctl    = (osmga_u32)dwgctl;
    probe.alphactrl = (osmga_u32)alphactrl;
    probe.first     = 0U;
    probe.count     = 3U;
    if (osmgaHW3DValidatePrimState(dwgctl, alphactrl) != OSMGA_HW3D_OK)
        return 0;
    if (osmgaHW3DWarpAdmits(st, &probe) != OSMGA_HW3D_OK)
        return 0;
    return 1;
}

/*
 * Submit the assembled WARP batch, and deal with what comes back.
 *
 * THE BOUNDARY THAT MATTERS.  A refusal with a verdict happened BEFORE the
 * kernel encoded anything, so the batch drew nothing and every source in
 * it can be replayed in software -- in record order, which is the source
 * order the trapezoid path's narrowing exists to preserve.  A failure with
 * verdict OK happened after the doorbell and may have drawn PART of the
 * batch; replaying then draws some triangles twice, and blending and
 * equal-depth comparisons show it.  That case revokes and replays
 * nothing, which is what the version 9 path does and what the kernel's own
 * timeout policy says.
 */
static void
osmgaMesaFlushWarp(void)
{
    OSMGAHW3DWarpBatch *wb;
    OSMGAHW3DSubmitBlock res;
    GLcontext *ctx;
    unsigned long nsrc, i;
    int rc;

    pendInFlush = 1;
    ctx  = pendCtx;
    nsrc = pendSrcCount;
    /* Detach FIRST: a replayed triangle re-entering sees an empty batch. */
    pendVerts    = 0UL;
    pendSrcCount = 0UL;
    pendMode     = OSMGA_MESA_PEND_NONE;

    wb = (OSMGAHW3DWarpBatch *)OSMGAMesaProbeBatch();
    if (wb == 0 || ctx == 0) {
        /* The same two situations the trapezoid arm tells apart: a revoke
         * leaves the colour surface mapped and every pending source still
         * drawable, a fork releases it and they must be dropped. */
        if (ctx != 0 && OSMGAMesaBufferBoundTo(ctx->DriverCtx)) {
            GLfloat       zoffWas = ctx->PolygonZoffset;
            GLvector4ub  *cptrWas = ctx->VB->ColorPtr;
            GLvector1ui  *iptrWas = ctx->VB->IndexPtr;
            GLubyte     (*specWas)[4] = ctx->VB->Specular;

            for (i = 0UL; i < nsrc; i++)
                (void)osmgaMesaReplaySource(ctx, i);
            hookRescued += nsrc;
            ctx->PolygonZoffset = zoffWas;
            ctx->VB->ColorPtr   = cptrWas;
            ctx->VB->IndexPtr   = iptrWas;
            ctx->VB->Specular   = specWas;
        } else {
            hookDropped += nsrc;
        }
        hookFlushOther++;
        pendInFlush = 0;
        return;
    }

    /*
     * The state, from the same two places version 9 asks.
     *
     * This used to be missing entirely, and the tier drew correctly anyway
     * -- because the batch is a MAPPED BUFFER this library reuses, and the
     * accelerated clear that ran moments earlier had left a correct
     * destination, pitch, depth origin and scissor in it.  Inheritance is
     * not a contract: a rebind with no clear between, or a scissor that
     * moved, or a second texture, and the submission carries the previous
     * one's answer.  The kernel would not catch it either -- it validates
     * that the state is LEGAL, and an inherited state is perfectly legal
     * and simply belongs to something else.
     */
    osmgaMesaFillState(ctx, &wb->state);
    if (pendHasTex) {
        wb->state.texorg    = pendTexOrg;
        wb->state.texW      = pendTexW;
        wb->state.texH      = pendTexH;
        wb->state.texPitch  = pendTexPitch;
        wb->state.texFormat = OSMGA_HW3D_TEXFMT_TW32;
        wb->state.texFlags  = pendTexFlags;
        /*
         * tmr stays nought for this tier, and that is not an omission.
         * The six are the trapezoid builder's texture gradients; WARP
         * computes its own in microcode and the kernel's version 10 path
         * never reads them.  Zeroed rather than left, because a field left
         * alone in a mapped buffer keeps the last submission's value.
         */
        memset(wb->state.tmr, 0, sizeof wb->state.tmr);
        wb->state.texBiasReqU = OSMGA_HW3D_TEX_BIAS_NONE;
        wb->state.texBiasReqV = OSMGA_HW3D_TEX_BIAS_NONE;
    } else {
        osmgaMesaBatchUntextured(&wb->state);
    }

    /*
     * The magic, spoiled on purpose when a test asks for it -- the same
     * one line version 9 has, in the same place, for the same reason.
     * Without it OSMGAMesaHookInjectRefusal simply does not reach this
     * tier, and the demonstration the header calls "the clearest this
     * project has that the fallback is exact" cannot be run on it.
     *
     * Fourth time the WARP flush turned out to be missing machinery the
     * version 9 flush has: the correctness counters were the first, the
     * submission timing the second, this the third, and hookReplayed
     * below the fourth.
     */
    wb->magic = hookInjectRefusal ? (OSMGA_HW3D_MAGIC ^ 1UL)
                                  : OSMGA_HW3D_MAGIC;
    hookWarpTried += nsrc;
    if ((unsigned long)wb->vtxCount > hookWarpVtxMax)
        hookWarpVtxMax = (unsigned long)wb->vtxCount;
    if ((unsigned long)wb->runCount > hookWarpRunMax)
        hookWarpRunMax = (unsigned long)wb->runCount;

    /*
     * Timed the way version 9 times, and behind the same switch.
     *
     * The correctness counters were made to see this tier and the clock was
     * not, so every WARP run reported nought microseconds in the ioctl --
     * which would have made the tier look free in exactly the comparison it
     * exists to win.  Same class as the counter blindness, missed at the
     * same time.
     *
     * Behind the switch for the reason the switch exists: two
     * gettimeofdays are 9 us of system call a submission, and this tier
     * submits per RUN rather than per batch, so leaving it on would tax
     * whichever arm submits more often -- which is the very thing being
     * measured.
     */
    memset(&res, 0, sizeof res);
    if ((hookInstrument & OSMGA_MESA_INST_TIME) != 0) {
        struct timeval t0, t1;

        gettimeofday(&t0, (struct timezone *)0);
        rc = OSMGAMesaProbeSubmit(&res);
        gettimeofday(&t1, (struct timezone *)0);
        submitUs += (unsigned long)((t1.tv_sec - t0.tv_sec) * 1000000L +
                                    (t1.tv_usec - t0.tv_usec));
    } else {
        rc = OSMGAMesaProbeSubmit(&res);
    }
    submitCount++;
    submitDwords += res.dwords;
    submitSpins += res.spins;
    if (res.spins > submitSpinMax) submitSpinMax = res.spins;
    if (res.spins != 0UL) submitSpun++;

    if (rc == 0) {
        /*
         * Counted here and only here -- after a submission the driver
         * took, never when the work was queued.
         *
         * hookDrawn counts SOURCE TRIANGLES the engine drew, which is true
         * of either tier, so it moves for this one too; every existing
         * consumer reads it as "did the engine draw these" and keeps
         * meaning that.  hookBatches counts SUBMISSIONS, which this is.
         * hookWarp is the tier split, and it is what lets a test insist a
         * run went through THIS tier rather than merely through hardware
         * -- without it a WARP run that quietly fell back to trapezoids
         * passes a pure-hardware gate.
         *
         * hookTraps stays trapezoid-only: there are no trapezoids here.
         */
        hookDrawn    += nsrc;
        hookWarp     += nsrc;
        hookBatches++;
        hookRefusedRun = 0;
        OSMGAMesaBufferSoiled();
    } else {
        hookDeclined++;
        if (res.verdict < OSMGA_MESA_VERDICTS)
            hookVerdictCount[res.verdict]++;
        hookLastRefusal.status   = res.status;
        hookLastRefusal.verdict  = res.verdict;
        hookLastRefusal.triangle = res.triangle;   /* a RUN, for version 10 */
        hookLastRefusal.triCount = (unsigned long)wb->vtxCount;
        hookLastRefusal.dstWidth  = wb->state.dstWidth;
        hookLastRefusal.dstHeight = wb->state.dstHeight;

        if (res.verdict != (unsigned long)OSMGA_HW3D_OK) {
            /* Refused before encoding: nothing was drawn.  Replay every
             * source, in record order -- and count it against the
             * backstop, which the trapezoid arm does and this one did not:
             * a tier that is refused every time would otherwise replay for
             * ever instead of giving acceleration up. */
            GLfloat       zoffWas = ctx->PolygonZoffset;
            GLvector4ub  *cptrWas = ctx->VB->ColorPtr;
            GLvector1ui  *iptrWas = ctx->VB->IndexPtr;
            GLubyte     (*specWas)[4] = ctx->VB->Specular;

            osmgaMesaCountRefusal();
            for (i = 0UL; i < nsrc; i++)
                (void)osmgaMesaReplaySource(ctx, i);
            /* Both, and they are different questions: Replayed is "a
             * refused batch was redrawn in software", which is what a
             * fallback test asks, and Rescued also covers the recovery
             * when the command window has gone away. */
            hookReplayed += nsrc;
            hookRescued += nsrc;
            ctx->PolygonZoffset = zoffWas;
            ctx->VB->ColorPtr   = cptrWas;
            ctx->VB->IndexPtr   = iptrWas;
            ctx->VB->Specular   = specWas;
        } else {
            /*
             * The verdict was OK and it still failed, so the doorbell rang
             * and part of this batch may be on the screen.  Replaying
             * would draw those twice.  Give the tier up instead -- the
             * kernel has already latched acceleration off for the same
             * reason.
             */
            OSMGAMesaProbeRevoke("WARP submission failed after the doorbell");
            hookDropped += nsrc;
        }
    }
    pendInFlush = 0;
}


/*
 * Try to put one source triangle into the WARP batch.
 *
 * Returns 1 when the tier took it -- the caller then builds no trapezoid
 * -- and 0 to fall through, having left the pending state untouched.
 *
 * THE THREE REFUSALS, and only the first two happen here:
 *
 *   pre-admission   the state is outside the tier, or a vertex will not
 *                   convert.  Any WARP work already queued is flushed
 *                   FIRST, so this one source draws after it and before
 *                   whatever follows -- the ordering the whole refusal
 *                   machinery exists to keep.
 *   full            flush, reset, and try the same source again.
 *   kernel refusal  not here; osmgaMesaFlushWarp deals with it.
 */
static int
osmgaMesaWarpTriangle(GLcontext *ctx, struct vertex_buffer *VB,
                      OSMGAMesaVertex *a, OSMGAMesaVertex *b,
                      OSMGAMesaVertex *c, const OSMGAMesaVertex *prov,
                      unsigned long zmode, unsigned long blend,
                      int texOn, const OSMGAMesaTex *tex,
                      unsigned long texOrg, unsigned long texW,
                      unsigned long texH, unsigned long texPitch,
                      unsigned long texFlags, double zoffset,
                      GLuint v0, GLuint v1, GLuint v2, GLuint pv)
{
    OSMGAHW3DWarpBatch *wb;
    OSMGAHW3DVertex wv[3];
    OSMGAHW3DState probeState;
    unsigned long dwgctl;
    int r;
    int batchable = (VB->ClipOrMask == 0) &&
                    (ctx->Driver.MultipassFunc == 0);

    if (!osmgaMesaWarpWanted())
        return 0;
    /*
     * Test only: send this source down the trapezoid path instead, without
     * touching a single piece of GL state.
     *
     * The mirror of OSMGAMesaHookForceSoftware, and for the same reason
     * that one exists -- "the tests' way to change the path without
     * changing anything else".  Alternating the tier through a state the
     * policy refuses would work, but it changes the render state, the
     * batching and the run boundaries at the same moment as it changes the
     * tier, so it cannot state the SCHEDULER's invariant on its own.  This
     * can: the ordering guarantee is that anything already queued for WARP
     * is submitted before a source takes another path, and that is what
     * declineOne does below -- the production path, entered from here
     * rather than from a policy refusal.
     */
    if (hookForcedTrapezoid)
        goto declineOne;

    /*
     * The run key, from the same function the trapezoid builder uses.
     * The hook has to know it BEFORE it picks a tier, and deriving it a
     * second time here would let the two drift about what a run is.
     */
    dwgctl = OSMGAMesaDwgctl(zmode, ctx->Depth.Mask == GL_TRUE, texOn);

    /*
     * Enough state for the tier's admission rules, which look only at the
     * texture.  The real state is filled at flush time from the same
     * values.
     */
    memset(&probeState, 0, sizeof probeState);
    if (texOn) {
        probeState.texW     = texW;
        probeState.texH     = texH;
        probeState.texPitch = texPitch;
        probeState.texFlags = texFlags;
    }
    if (!osmgaMesaWarpTakes(&probeState, dwgctl, blend))
        goto declineOne;

    /*
     * Flat shading: WARP has no separate provoking-vertex argument, so all
     * three carry the provoking colour.  The COLOUR only -- position,
     * depth and texture coordinates stay each vertex's own.
     */
    if (prov != 0) {
        a->r = b->r = c->r = prov->r;
        a->g = b->g = c->g = prov->g;
        a->b = b->b = c->b = prov->b;
        a->a = b->a = c->a = prov->a;
    }

    /*
     * The polygon offset goes in HERE, and it is the CALLER'S zoffset --
     * Mesa's own expression, computed in the callback -- and not
     * ctx->PolygonZoffset, which is the context field the replay records
     * save and restore.  Using that one would have offset by whatever the
     * context happened to hold rather than by what this triangle asked
     * for.
     *
     * A vertex built without it draws unoffset, and a shifted plane that
     * leaves the depth range is refused rather than clamped.
     */
    if (OSMGAMesaBuildWarpVertex(a, texOn ? tex : 0, zoffset, &wv[0]) != 0 ||
        OSMGAMesaBuildWarpVertex(b, texOn ? tex : 0, zoffset, &wv[1]) != 0 ||
        OSMGAMesaBuildWarpVertex(c, texOn ? tex : 0, zoffset, &wv[2]) != 0)
        goto declineOne;

    /* A different shape is pending, or a different context, or the
     * texture object moved: submit what is there before adding to it. */
    if (!osmgaMesaPendEmpty() &&
        (pendMode != OSMGA_MESA_PEND_WARP ||
         pendCtx != ctx || pendVB != (void *)VB ||
         (texOn && pendHasTex &&
          (pendTexOrg != texOrg || pendTexW != texW ||
           pendTexH != texH || pendTexPitch != texPitch ||
           pendTexFlags != texFlags)))) {
        hookFlushKey++;
        osmgaMesaFlushPending();
    }

    wb = (OSMGAHW3DWarpBatch *)OSMGAMesaProbeBatch();
    if (wb == 0)
        return 0;                    /* no window; the caller will cope */

    if (osmgaMesaPendEmpty()) {
        OSMGAMesaWarpReset(&warpBuild, wb);
        OSMGAMesaWarpCapacity(&warpBuild, hookWarpVtxCap,
                              hookWarpRunCap);
        pendMode   = OSMGA_MESA_PEND_WARP;
        pendCtx    = ctx;
        pendVB     = (void *)VB;
        pendHasTex = 0;
        pendVerts  = 0UL;
    }
    if (texOn && !pendHasTex) {
        pendHasTex   = 1;
        pendTexOrg   = texOrg;
        pendTexW     = texW;
        pendTexH     = texH;
        pendTexPitch = texPitch;
        pendTexFlags = texFlags;
        /* tmr is NOT part of this key: the gradients are per-triangle
         * geometry that the microcode computes from the vertices, so they
         * are not batch state at all for this tier. */
    }

    r = OSMGAMesaWarpAdd(&warpBuild, dwgctl, blend, &wv[0], &wv[1], &wv[2]);
    if (r == OSMGA_MESA_WARP_FULL) {
        hookFlushFull++;
        osmgaMesaFlushPending();
        wb = (OSMGAHW3DWarpBatch *)OSMGAMesaProbeBatch();
        if (wb == 0)
            return 0;
        OSMGAMesaWarpReset(&warpBuild, wb);
        OSMGAMesaWarpCapacity(&warpBuild, hookWarpVtxCap,
                              hookWarpRunCap);
        pendMode   = OSMGA_MESA_PEND_WARP;
        pendCtx    = ctx;
        pendVB     = (void *)VB;
        pendHasTex = 0;
        pendVerts  = 0UL;
        if (texOn) {
            pendHasTex   = 1;
            pendTexOrg   = texOrg;
            pendTexW     = texW;
            pendTexH     = texH;
            pendTexPitch = texPitch;
            pendTexFlags = texFlags;
        }
        r = OSMGAMesaWarpAdd(&warpBuild, dwgctl, blend,
                             &wv[0], &wv[1], &wv[2]);
    }
    if (r != 0)
        return 0;                    /* it will not fit even empty */

    pendVerts += 3UL;
    pendSrc[pendSrcCount].v0   = v0;
    pendSrc[pendSrcCount].v1   = v1;
    pendSrc[pendSrcCount].v2   = v2;
    pendSrc[pendSrcCount].pv   = pv;
    pendSrc[pendSrcCount].zoff = ctx->PolygonZoffset;
    pendSrc[pendSrcCount].cptr = VB->ColorPtr;
    pendSrc[pendSrcCount].iptr = VB->IndexPtr;
    pendSrc[pendSrcCount].spec = VB->Specular;
    pendSrc[pendSrcCount].firstTrap = pendVerts - 3UL;   /* vertices here */
    pendSrcCount++;

    /*
     * The two gates that must not defer: a clipping VB uses temporary
     * vertices a later clip may overwrite, and a multipass context repeats
     * the loop before RenderFinish.  Both submit this source at once.
     */
    if (!batchable)
        osmgaMesaFlushPending();
    return 1;

declineOne:
    /*
     * The tier will not have this one.  Submit what is queued so this
     * source draws after it, then let the caller build a trapezoid.
     */
    if (!osmgaMesaPendEmpty() && pendMode == OSMGA_MESA_PEND_WARP)
        osmgaMesaFlushPending();
    return 0;
}

static void
osmgaMesaFlushPending(void)
{
    OSMGAHW3DBatch *batch;
    OSMGAHW3DSubmitBlock res;
    GLcontext *ctx;
    unsigned long nsrc, ntraps, i;

    if (osmgaMesaPendEmpty() || pendInFlush)
        return;
    if (pendMode == OSMGA_MESA_PEND_WARP) {
        osmgaMesaFlushWarp();
        return;
    }
    pendInFlush = 1;
    ctx = pendCtx;
    ntraps = pendTraps;
    nsrc = pendSrcCount;
    /* Detach FIRST: a replayed triangle re-entering sees an empty batch. */
    pendTraps = 0UL;
    pendSrcCount = 0UL;

    batch = OSMGAMesaProbeBatch();
    if (batch == 0 || ctx == 0) {
        /*
         * The command window is gone with work pending.  That is TWO
         * different situations and they used to be treated as one.
         *
         * A revoke releases the command window and closes the device and
         * does NOT touch the colour surface -- so the destination is still
         * mapped, the saved rasteriser is still there, and every pending
         * triangle can still be drawn.  Dropping them lost part of the
         * picture: measured at nine triangles in, eight out.
         *
         * The fork path is the other one.  It releases the surface as well,
         * and Mesa keeps row addresses it derived earlier, so drawing then
         * would write into pages that are no longer mapped.  That case still
         * has to be dropped, and telling the two apart is what bufBound is
         * for: the release clears it, the revoke never does.  bufOrigin is
         * NOT a substitute -- the release leaves it set.
         *
         * A null ctx is neither: there is no rasteriser to hand them to.
         */
        if (ctx != 0 && OSMGAMesaBufferBoundTo(ctx->DriverCtx)) {
            /*
             * The same state bracket the narrowing body keeps.  Each replay
             * installs the state that was recorded with its own triangle,
             * and a flush can run inside a triangle callback that still uses
             * these fields, so what was live on the way in goes back.
             */
            GLfloat       zoffWas = ctx->PolygonZoffset;
            GLvector4ub  *cptrWas = ctx->VB->ColorPtr;
            GLvector1ui  *iptrWas = ctx->VB->IndexPtr;
            GLubyte     (*specWas)[4] = ctx->VB->Specular;

            for (i = 0UL; i < nsrc; i++)
                (void)osmgaMesaReplaySource(ctx, i);
            hookRescued += nsrc;
            ctx->PolygonZoffset = zoffWas;
            ctx->VB->ColorPtr   = cptrWas;
            ctx->VB->IndexPtr   = iptrWas;
            ctx->VB->Specular   = specWas;
        } else {
            hookDropped += nsrc;
        }
        hookFlushOther++;
        pendInFlush = 0;
        return;
    }
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
        osmgaMesaBatchUntextured(&batch->state);
    }
    {
        unsigned long lo = 0UL;      /* first source not yet dealt with    */
        unsigned long base = 0UL;    /* absolute trapezoid index at tri[0] */
        unsigned long rounds = 0UL;
        /*
         * What the context holds NOW is not what it held when the sources
         * were recorded (vbrender.c:324-328, 714-718), so each software
         * replay installs its own recorded state -- and what was live on
         * the way in goes back at the single exit below, because a flush
         * can run inside a triangle callback that still uses these fields.
         */
        GLfloat       zoffWas = ctx->PolygonZoffset;
        GLvector4ub  *cptrWas = ctx->VB->ColorPtr;
        GLvector1ui  *iptrWas = ctx->VB->IndexPtr;
        GLubyte     (*specWas)[4] = ctx->VB->Specular;

        while (lo < nsrc) {
            unsigned long s2, absBad, prefix, k;

            batch->magic = hookInjectRefusal ? (OSMGA_HW3D_MAGIC ^ 1UL)
                                             : OSMGA_HW3D_MAGIC;
            batch->version = OSMGA_HW3D_VERSION;
            batch->triCount = ntraps - base;
            /*
             * Spoil the first trapezoid of THIS submit only -- never the
             * prefix resubmission below, which has to be able to succeed for
             * the narrowing loop to make the progress it makes in life.
             */
#ifdef OSMGA_MESA_TESTHOOKS
            if (hookInjectNamed != 0UL && ntraps > base + hookInjectTrap) {
                batch->tri[hookInjectTrap].dwgctl =
                    (batch->tri[hookInjectTrap].dwgctl & ~0xFUL) | 0xFUL;
                hookInjectNamed--;
                hookInjectedNamed++;
            }
#endif
            hookBatches++;
            hookTraps += ntraps - base;
            if (OSMGA_ARM(1)) {
                /* arm C: built and accumulated, then thrown away rather than
                 * handed to the kernel.  The counters above still move, so a
                 * run can be checked for having built what it meant to. */
                lo = nsrc;
                break;
            }
            if (osmgaMesaSubmitBatch(ctx, batch, &res) == 0) {
                hookRefusedRun = 0;
                hookDrawn += nsrc - lo;
                OSMGAMesaBufferSoiled();
                break;
            }
            if (res.verdict == OSMGA_HW3D_OK) {
                /* Validation passed and the trouble came later: some of it
                 * may be on the screen, and drawing it again would double
                 * it.  Nothing more is replayed, for the same reason. */
                OSMGAMesaProbeRevoke("a batch failed after the engine had it");
                break;
            }
            /*
             * NOT counted here any more.
             *
             * The backstop was written to catch a driver refusing everything,
             * and it counted every refusal -- including one that was narrowed
             * to a single triangle and correctly redrawn in software, which is
             * this machinery working exactly as designed.  Eight adjacent
             * inexpressible triangles therefore revoked acceleration for the
             * process, and at 1600x1200 a scene has them.  The count moved to
             * the paths that really are evidence of trouble; see each.
             */

            /*
             * Narrow, when the refusal names a triangle this map can place.
             * The kernel validated the WHOLE batch before drawing anything,
             * so everything before the named trapezoid is undrawn and known
             * good.
             */
            s2 = nsrc;
            if (osmgaMesaVerdictNamesTriangle(res.verdict)
                && res.triangle < ntraps - base
                && rounds < OSMGA_MESA_NARROW_LIMIT) {
                absBad = base + res.triangle;
                if (pendSrc[lo].firstTrap <= absBad) {
                    for (s2 = lo; s2 + 1UL < nsrc; s2++)
                        if (pendSrc[s2 + 1UL].firstTrap > absBad)
                            break;
                }
            }
            if (s2 >= nsrc) {
                /* Batch-level verdict, an index the map cannot place, or
                 * the narrowing bound: everything left goes to software,
                 * exactly as the whole batch always did.  This one counts --
                 * it is the shape "the driver refused and we could not say
                 * which triangle", which is what the backstop is for. */
                osmgaMesaCountRefusal();
                for (i = lo; i < nsrc; i++)
                    (void)osmgaMesaReplaySource(ctx, i);
                hookReplayed += nsrc - lo;
                break;
            }
            hookNarrowed++;
            rounds++;

            /* The good prefix, on the engine, in order. */
            prefix = pendSrc[s2].firstTrap - base;
            if (prefix != 0UL) {
                /*
                 * RE-ACQUIRED, because the count above can revoke and a
                 * revoke vm_deallocates this very mapping.
                 *
                 * The write below used to go through the copy taken at the
                 * top of the flush, and gdb caught it doing exactly that:
                 * "Memory access exception on address 0xf06008", with edx
                 * still holding the freed window's base and 8 being the
                 * offset of triCount.  Nothing else in this function used a
                 * stale pointer -- the reacquire further down already
                 * checks -- and nothing in the shipped build can revoke
                 * between here and the top of the loop.  That last part is
                 * why it survived: it was safe by where a counter happened
                 * to sit, and this says it instead.
                 *
                 * Nothing has been drawn for this batch: validation refused
                 * it before the engine saw it, and hookDrawn only moves on a
                 * submission that succeeded.  So the remainder can go to
                 * software whole, exactly as the reacquire below does it.
                 */
                batch = OSMGAMesaProbeBatch();
                if (batch == 0) {
                    if (OSMGAMesaBufferBoundTo(ctx->DriverCtx)) {
                        for (i = lo; i < nsrc; i++)
                            (void)osmgaMesaReplaySource(ctx, i);
                        hookRescued += nsrc - lo;
                    } else {
                        hookDropped += nsrc - lo;
                    }
                    hookFlushOther++;
                    break;
                }
                batch->triCount = prefix;
                hookBatches++;
                hookTraps += prefix;
                if (osmgaMesaSubmitBatch(ctx, batch, &res) == 0) {
                    hookRefusedRun = 0;
                    hookDrawn += s2 - lo;
                    OSMGAMesaBufferSoiled();
                } else if (res.verdict == OSMGA_HW3D_OK) {
                    OSMGAMesaProbeRevoke("a batch failed after the engine "
                                         "had it");
                    break;
                } else {
                    /* A prefix that validated moments ago refusing now is
                     * nothing this map can reason about: software for it. */
                    osmgaMesaCountRefusal();
                    for (i = lo; i < s2; i++)
                        (void)osmgaMesaReplaySource(ctx, i);
                    hookReplayed += s2 - lo;
                }
            }

            /*
             * The named source, in software, wearing its own state.
             *
             * Pardoned only if it really was redrawn AND the refusal was one
             * of the geometry verdicts this back end is expected to provoke.
             * A software path that declined to take it leaves the triangle
             * undrawn, which is a refusal like any other.
             */
            {
                int redrew = osmgaMesaReplaySource(ctx, s2);

                if (!redrew || !osmgaMesaGeometryVerdict(res.verdict))
                    osmgaMesaCountRefusal();
            }
            hookReplayed++;
            lo = s2 + 1UL;
            if (lo >= nsrc)
                break;

            /*
             * Slide the remainder down and go around.  The pointer is
             * reacquired because a replay may have revoked the probe, and
             * revocation unmaps the window: a null here is the window gone
             * with work pending, which drops the remainder exactly as the
             * flush entry always has.
             */
            batch = OSMGAMesaProbeBatch();
            if (batch == 0) {
                /*
                 * A replay revoked the probe.  The remainder has not been
                 * submitted -- a named refusal is rejected during validation,
                 * before anything is encoded or executed -- so none of it is
                 * on the screen and all of it can still be drawn.  This is
                 * already inside the state bracket above.
                 */
                if (OSMGAMesaBufferBoundTo(ctx->DriverCtx)) {
                    for (i = lo; i < nsrc; i++)
                        (void)osmgaMesaReplaySource(ctx, i);
                    hookRescued += nsrc - lo;
                } else {
                    hookDropped += nsrc - lo;
                }
                hookFlushOther++;
                break;
            }
            {
                unsigned long nb = pendSrc[lo].firstTrap;

                for (k = 0UL; k < ntraps - nb; k++)
                    batch->tri[k] = batch->tri[(nb - base) + k];
                base = nb;
            }
        }
        ctx->PolygonZoffset = zoffWas;
        ctx->VB->ColorPtr   = cptrWas;
        ctx->VB->IndexPtr   = iptrWas;
        ctx->VB->Specular   = specWas;
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

#ifdef OSMGA_MESA_TESTHOOKS
void
OSMGAMesaHookInjectNamed(unsigned long submits, unsigned long trap)
{
    osmgaMesaFlushPending();
    hookInjectNamed = submits;
    hookInjectTrap = trap;
    hookInjectedNamed = 0UL;
}

unsigned long
OSMGAMesaHookInjectedNamed(void)
{
    return hookInjectedNamed;
}
#endif /* OSMGA_MESA_TESTHOOKS */

unsigned long
OSMGAMesaHookRescued(void)
{
    return hookRescued;
}

unsigned long
OSMGAMesaHookDropped(void)
{
    return hookDropped;
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
osmgaMesaBatchUntextured(OSMGAHW3DState *st)
{
    st->texorg = 0UL;
    st->texW = st->texH = st->texPitch = 0UL;
    st->texFormat = 0UL;
    st->texFlags = 0UL;
    memset(st->tmr, 0, sizeof st->tmr);
    st->texBiasReqU = OSMGA_HW3D_TEX_BIAS_NONE;
    st->texBiasReqV = OSMGA_HW3D_TEX_BIAS_NONE;
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
/*
 * Where this submission draws, and what it is clipped to.
 *
 * A function and not four lines inside the trapezoid submit, because BOTH
 * contracts need it and only one of them was getting it.  Version 10's
 * flush went straight to OSMGAMesaProbeSubmit, so its state block held
 * whatever the last version 9 submission had left in the same mapped
 * buffer -- a destination, a pitch, a depth origin and a scissor belonging
 * to some earlier surface.  It drew correctly on a mesh only because an
 * accelerated clear had just put the right values there.
 *
 * That is the failure osmgaMesaBatchUntextured's own comment describes for
 * the texture half: "a field left alone keeps what the last submission put
 * there".  The destination half had no such function, so there was nothing
 * to forget to call.  Now there is one, and both callers call it.
 */
static void
osmgaMesaFillState(GLcontext *ctx, OSMGAHW3DState *st)
{
    /* Where the surface is, asked of the one place that decides it -- not
     * worked out again here, where it could disagree. */
    st->dstorg = OSMGAMesaBufferOrigin();
    /*
     * The destination is the drawing surface Mesa is working on, and saying
     * so is what lets the kernel clip to it: before the batch declared this,
     * the kernel clipped every submission to a fixed sixty-four by a hundred
     * and twenty, which no real surface fits inside.
     */
    st->dstWidth  = OSMGAMesaBufferWidth();
    st->dstHeight = OSMGAMesaBufferHeight();
    st->dstPitch  = OSMGAMesaBufferStride();
    st->zorg      = OSMGAMesaBufferDepthOrigin();
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
        st->scissorOn = 1UL;
        st->scissorX = (long)ctx->Scissor.X;
        st->scissorY = (long)ctx->Scissor.Y;
        st->scissorW = (ctx->Scissor.Width > 0)
                       ? (unsigned long)ctx->Scissor.Width : 0UL;
        st->scissorH = (ctx->Scissor.Height > 0)
                       ? (unsigned long)ctx->Scissor.Height : 0UL;
    } else {
        st->scissorOn = 0UL;
        st->scissorX = 0L;
        st->scissorY = 0L;
        st->scissorW = 0UL;
        st->scissorH = 0UL;
    }
}

static int
osmgaMesaSubmitBatch(GLcontext *ctx, OSMGAHW3DBatch *batch,
                     OSMGAHW3DSubmitBlock *out)
{
    OSMGAHW3DSubmitBlock res;

    osmgaMesaFillState(ctx, &batch->state);
    {
        struct timeval t0, t1;
        int rc;

        /*
         * The measuring costs more than some of what it measures.
         *
         * gettimeofday is a system call here and it was timed at 4.58 us --
         * two of them on every submission, 33.5 submissions a frame, is
         * 0.31 ms of a 17 ms frame, and it lands in system time where the
         * submission's own cost is.  Counting which registers changed is
         * another 0.45 ms of user time.  Together that is 4.4% of the frame
         * spent watching the frame.
         *
         * So none of it runs unless a test asks for it.  What is lost by
         * default is only the reporting: the submission itself, the
         * refusal bookkeeping below, and every counter a correctness test
         * reads are outside this switch.
         */
        if ((hookInstrument & OSMGA_MESA_INST_DELTA) != 0)
            osmgaMesaCountDeltas(batch);
        if ((hookInstrument & OSMGA_MESA_INST_TIME) != 0) {
            gettimeofday(&t0, (struct timezone *)0);
            if (!osmgaMesaArmSubmit(&res, &rc))
                rc = OSMGAMesaProbeSubmit(&res);
            gettimeofday(&t1, (struct timezone *)0);
            submitUs += (unsigned long)((t1.tv_sec - t0.tv_sec) * 1000000L +
                                        (t1.tv_usec - t0.tv_usec));
        } else {
            if (!osmgaMesaArmSubmit(&res, &rc))
                rc = OSMGAMesaProbeSubmit(&res);
        }
        submitCount++;
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
    /*
     * The texture flags MINUS the perspective bit.
     *
     * Everything else here is a property of the texture object and the
     * environment, so it is known before either tier builds anything.  The
     * perspective bit is not: it comes from tmr[0][8], which the TRAPEZOID
     * builder produces, so it is added below for that tier only.
     *
     * The WARP tier does not want it anyway.  Its state list never sets
     * NOPERSPECTIVE, and the flag exists to clear that bit -- so for WARP
     * it would be a flush key that varied with a value the tier does not
     * use, which is the same mistake tmr would have been.
     */
    unsigned long texFlagsBase = 0UL;
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
        if ((hookInstrument & OSMGA_MESA_INST_AREA) != 0)                \
            osmgaAreaAdd((long)VB->Win.data[idx][0] - 1L,                \
                         (long)VB->Win.data[idx][1] - 1L,                \
                         (long)VB->Win.data[idx][0] + 1L,                \
                         (long)VB->Win.data[idx][1] + 1L);               \
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
    }

    /*
     * The WARP tier first, when it is switched on and will have this
     * primitive.  A 1 means it took the source and no trapezoid is built;
     * a 0 leaves the pending state as it found it, having flushed any WARP
     * work first so that what follows draws in source order.
     */
    /*
     * The texture flags that do not depend on the trapezoid builder,
     * computed here because the WARP tier needs them for its own flush key
     * and never sees tmr.
     */
    if (texOn) {
        const struct gl_texture_object *to =
            ctx->Texture.Unit[0].CurrentD[2];
        const struct gl_texture_image *ti = to->Image[to->BaseLevel];

        texFlagsBase =
            ((to->MagFilter == GL_LINEAR) ? OSMGA_HW3D_TEXF_BILIN : 0UL)
            | ((to->MinFilter == GL_LINEAR)
                 ? OSMGA_HW3D_TEXF_BILINMIN : 0UL)
            | ((ti != 0 && ti->Format == GL_RGBA)
               ? OSMGA_HW3D_TEXF_TEXALPHA : 0UL)
            | ((ctx->Texture.Unit[0].EnvMode == GL_MODULATE)
               ? OSMGA_HW3D_TEXF_MODULATE : 0UL)
            | ((to->WrapS == GL_REPEAT) ? OSMGA_HW3D_TEXF_REPEATU : 0UL)
            | ((to->WrapT == GL_REPEAT) ? OSMGA_HW3D_TEXF_REPEATV : 0UL);
    }

    if (osmgaMesaWarpTriangle(ctx, VB, &a, &b, &c,
                              (ctx->Light.ShadeModel == GL_FLAT)
                                  ? &prov : (const OSMGAMesaVertex *)0,
                              zmode, blend, texOn,
                              texOn ? &tex : (const OSMGAMesaTex *)0,
                              texOrg, texW, texH, texPitch, texFlagsBase,
                              zoffset, v0, v1, v2, pv))
        return;

    if (ctx->Light.ShadeModel == GL_FLAT) {
        if (OSMGA_ARM(2)) return;                /* arm D */
        n = OSMGAMesaBuildTriangleTex(&a, &b, &c, &prov, zmode,
                                      ctx->Depth.Mask == GL_TRUE, blend,
                                      texOn ? &tex : (const OSMGAMesaTex *)0,
                                      zoffset, built, tmr);
    } else {
        if (OSMGA_ARM(2)) return;                /* arm D */
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
     * A boundary the wire format cannot carry.
     *
     * FXBNDRY holds the left and right columns as two UNSIGNED sixteen-bit
     * halves, and the builder packs a negative left straight into one:
     * measured on hardware at 1600x1200, `fxbndry 028efd97` -- left 0xfd97,
     * which is -617.  The kernel then reads 64919, sees it past the surface
     * width, and refuses the whole batch as E_TRICOL.  It is right to; what
     * is wrong is that a value with no representation was sent at all.
     *
     * It grows with the surface because the same geometry covers more pixels,
     * so an edge that runs past x=0 does so by more: measured refusals were
     * 0 at 800x600 and 1024x768, 1 at 1280x1024 and 5 at 1600x1200 for one
     * coarse teapot.  Eight in a row revoke acceleration for the process.
     *
     * READING IT BACK IS EXACT.  The builder refuses any vertex outside
     * +/- OSMGA_MESA_RULE_COORD_MAX (8192) pixels, so every boundary it can
     * emit fits a signed sixteen-bit field with room to spare and the
     * sign-extension below recovers the value the builder had.  Nothing is
     * added to OSMGAHW3DTri to carry it: that structure is the kernel's ABI.
     *
     * The width comes from the bound surface, NOT from batch->state, which
     * is filled in during submission and is not populated here yet.
     *
     * The WHOLE source triangle goes to software, never one trapezoid of a
     * split pair: half on the engine and half in software would draw the
     * seam twice under blending and disagree about depth.
     */
    {
        unsigned long dstW = OSMGAMesaBufferWidth();
        int ti;

        for (ti = 0; ti < n; ti++) {
            long lo16 = (long)(built[ti].fxbndry & 0xFFFFUL);
            long hi16 = (long)((built[ti].fxbndry >> 16) & 0xFFFFUL);

            if (lo16 >= 32768L) lo16 -= 65536L;
            if (hi16 >= 32768L) hi16 -= 65536L;
            if (lo16 < 0L || hi16 > (long)dstW || lo16 > hi16) {
                hookUnsupported++;
                (void)osmgaMesaSoftly(ctx, v0, v1, v2, pv);
                return;
            }
        }
    }

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
    if (texOn)
        texFlagsL = texFlagsBase
                    | ((tmr[0][8] != 0L) ? OSMGA_HW3D_TEXF_PERSP : 0UL);

    /*
     * May this triangle JOIN a batch?  Not when the VB clips -- clipped
     * triangles borrow temporary vertices a later clip can overwrite, so
     * their replay indices are only good RIGHT NOW -- and not under a
     * multipass driver, which repeats the loop before the bracket closes.
     * Either way it still goes through the engine; it just travels alone,
     * exactly as every triangle used to.
     */
    batchable = (VB->ClipOrMask == 0) && (ctx->Driver.MultipassFunc == 0);

    if (!osmgaMesaPendEmpty()) {
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
        /*
         * RE-ACQUIRED, because that flush can revoke -- and a revoke
         * vm_deallocates the very window `batch` points into.
         *
         * `batch` was taken at the top of this function, before the flush
         * existed as a possibility.  The append below writes through it, so
         * a flush that revoked would have this triangle writing into pages
         * that are no longer mapped.  It is the same fault gdb caught in the
         * flush's own prefix write, one call further out.
         *
         * Nothing needs repairing before leaving: the flush detached
         * pendTraps and pendSrcCount on its way in and restored the context
         * fields on its way out, and this triangle has been neither appended
         * nor submitted.  So it goes to software exactly as the entry check
         * sends one, and once.
         */
        if (OSMGAMesaProbeBatch() == 0) {
            hookDeclined++;
            (void)osmgaMesaSoftly(ctx, v0, v1, v2, pv);
            return;
        }
    }

    if (osmgaMesaPendEmpty()) {
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
    pendSrc[pendSrcCount].zoff = ctx->PolygonZoffset;
    pendSrc[pendSrcCount].cptr = VB->ColorPtr;
    pendSrc[pendSrcCount].iptr = VB->IndexPtr;
    pendSrc[pendSrcCount].spec = VB->Specular;
    pendSrc[pendSrcCount].firstTrap = pendTraps - (unsigned long)n;
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
    if ((hookInstrument & OSMGA_MESA_INST_AREA) != 0)
        osmgaAreaAdd((long)x, (long)y, (long)x + (long)n - 1L, (long)y);
    if (prevWriteRGBASpan) (*prevWriteRGBASpan)(ctx, n, x, y, rgba, mask);
}

static void
osmgaWriteRGBSpan(const GLcontext *ctx, GLuint n, GLint x, GLint y,
                  CONST GLubyte rgb[][3], const GLubyte mask[])
{
    osmgaMesaFlushPending();
    spanWrote = 1;
    if ((hookInstrument & OSMGA_MESA_INST_AREA) != 0)
        osmgaAreaAdd((long)x, (long)y, (long)x + (long)n - 1L, (long)y);
    if (prevWriteRGBSpan) (*prevWriteRGBSpan)(ctx, n, x, y, rgb, mask);
}

static void
osmgaWriteMonoRGBASpan(const GLcontext *ctx, GLuint n, GLint x, GLint y,
                       const GLubyte mask[])
{
    osmgaMesaFlushPending();
    spanWrote = 1;
    if ((hookInstrument & OSMGA_MESA_INST_AREA) != 0)
        osmgaAreaAdd((long)x, (long)y, (long)x + (long)n - 1L, (long)y);
    if (prevWriteMonoRGBASpan) (*prevWriteMonoRGBASpan)(ctx, n, x, y, mask);
}

static void
osmgaWriteRGBAPixels(const GLcontext *ctx, GLuint n, const GLint x[],
                     const GLint y[], CONST GLubyte rgba[][4],
                     const GLubyte mask[])
{
    osmgaMesaFlushPending();
    spanWrote = 1;
    if ((hookInstrument & OSMGA_MESA_INST_AREA) != 0) {
        GLuint i;
        for (i = 0; i < n; i++)
            osmgaAreaAdd((long)x[i], (long)y[i], (long)x[i], (long)y[i]);
    }
    if (prevWriteRGBAPixels) (*prevWriteRGBAPixels)(ctx, n, x, y, rgba, mask);
}

static void
osmgaWriteMonoRGBAPixels(const GLcontext *ctx, GLuint n, const GLint x[],
                         const GLint y[], const GLubyte mask[])
{
    osmgaMesaFlushPending();
    spanWrote = 1;
    if ((hookInstrument & OSMGA_MESA_INST_AREA) != 0) {
        GLuint i;
        for (i = 0; i < n; i++)
            osmgaAreaAdd((long)x[i], (long)y[i], (long)x[i], (long)y[i]);
    }
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
    osmgaAreaReset();

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
    if (!osmgaMesaPendEmpty())
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
    /*
     * M20's accounting, and the mirror is unchanged: this adds up what a
     * narrowed mirror would have been entitled to copy, against what this
     * one does copy.  The box is clamped to the surface and intersected
     * with the scissor, which cannot have moved inside the bracket --
     * glScissor is ASSERT_OUTSIDE_BEGIN_END_AND_FLUSH.
     */
    if ((hookInstrument & OSMGA_MESA_INST_AREA) != 0) {
        unsigned long W = OSMGAMesaBufferWidth();
        unsigned long H = OSMGAMesaBufferHeight();

        areaAllPixels += W * H;
        if (areaFull || !areaValid) {
            /* !areaValid means nothing this file can see wrote, yet the
             * mirror is running anyway -- so the honest charge is the
             * whole surface, not zero. */
            areaBoxPixels += W * H;
            areaFullBrackets++;
        } else {
            long x0 = areaMinX, y0 = areaMinY, x1 = areaMaxX, y1 = areaMaxY;

            if (ctx != 0 && ctx->Scissor.Enabled) {
                if (ctx->Scissor.X > x0) x0 = (long)ctx->Scissor.X;
                if (ctx->Scissor.Y > y0) y0 = (long)ctx->Scissor.Y;
                if ((long)ctx->Scissor.X + (long)ctx->Scissor.Width - 1L < x1)
                    x1 = (long)ctx->Scissor.X + (long)ctx->Scissor.Width - 1L;
                if ((long)ctx->Scissor.Y + (long)ctx->Scissor.Height - 1L < y1)
                    y1 = (long)ctx->Scissor.Y + (long)ctx->Scissor.Height - 1L;
            }
            if (x0 < 0L) x0 = 0L;
            if (y0 < 0L) y0 = 0L;
            if (x1 > (long)W - 1L) x1 = (long)W - 1L;
            if (y1 > (long)H - 1L) y1 = (long)H - 1L;
            if (x1 >= x0 && y1 >= y0)
                areaBoxPixels += (unsigned long)(x1 - x0 + 1L)
                               * (unsigned long)(y1 - y0 + 1L);
            areaBoxBrackets++;
        }
    }
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
    /*
     * RE-ACQUIRED, and this one mattered most of the three.
     *
     * The test below used to read `batch` -- the copy taken at the top of
     * this function, before the flush above.  A flush that revoked freed
     * that mapping and cleared the global, but the local still held the old
     * address, so the check passed and the writes further down went into
     * unmapped pages.  A guard that reads a stale copy is worse than no
     * guard: it looks like the question was asked.
     */
    batch = OSMGAMesaProbeBatch();
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
    osmgaMesaBatchUntextured(&batch->state);
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
    areaPendingFull = 1;
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
 * M20's four numbers.  areaAll is what the mirror copies; areaBox is what a
 * mirror narrowed to the rectangle every writer reported would have copied;
 * the two bracket counts say how much of the difference is real narrowing
 * and how much is brackets nothing could narrow.
 */
unsigned long OSMGAMesaHookAreaAll(void)      { return areaAllPixels; }
unsigned long OSMGAMesaHookAreaBox(void)      { return areaBoxPixels; }
unsigned long OSMGAMesaHookAreaFullBr(void)   { return areaFullBrackets; }
unsigned long OSMGAMesaHookAreaBoxBr(void)    { return areaBoxBrackets; }


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
unsigned long OSMGAMesaHookNarrowed(void)  { return hookNarrowed; }
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
unsigned long OSMGAMesaHookWarp(void)     { return hookWarp; }
unsigned long OSMGAMesaHookWarpTried(void) { return hookWarpTried; }
unsigned long OSMGAMesaHookWarpVtxMax(void) { return hookWarpVtxMax; }
unsigned long OSMGAMesaHookWarpRunMax(void) { return hookWarpRunMax; }
void
OSMGAMesaHookWarpCap(unsigned long vtx, unsigned long runs)
{
    osmgaMesaFlushPending();
    /* A new cap is a new measurement: the maxima below describe batches
     * built under the capacity in force, and carrying the old regime's
     * numbers forward would let a capped run report the uncapped one's. */
    hookWarpVtxMax = hookWarpRunMax = 0UL;
    hookWarpVtxCap = (vtx == 0UL || vtx > OSMGA_HW3D_MAX_VTX)
                     ? OSMGA_HW3D_MAX_VTX : vtx;
    hookWarpRunCap = (runs == 0UL || runs > OSMGA_HW3D_MAX_RUN)
                     ? OSMGA_HW3D_MAX_RUN : runs;
}
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
void
OSMGAMesaHookForceTrapezoid(int on)
{
    /* No flush here: the helper's own declineOne submits what WARP
     * has queued before the next source takes another path, which is
     * the ordering guarantee this switch exists to test.  Flushing
     * here as well would hide whether that works. */
    hookForcedTrapezoid = on;
}

/*
 * The measurement arm.  Flushes first, so a batch left half built by the
 * previous arm cannot be charged to this one.
 */
#ifdef OSMGA_MESA_TESTHOOKS
unsigned long OSMGAMesaHookDryStatus(void) { return hookDryStatus; }
unsigned long OSMGAMesaHookDryCount(void)  { return hookDryCount; }

void OSMGAMesaHookMeasureArm(int arm)
{
    osmgaMesaFlushPending();
    hookMeasureArm = arm;
}
#endif /* OSMGA_MESA_TESTHOOKS */
/*
 * A mask of OSMGA_MESA_INST_TIME, _DELTA, _MASK and _AREA.  Pass 15 for
 * all of it.
 *
 * This used to translate 1 into all three, to keep the old switch's meaning
 * -- and that quietly destroyed the experiment it was written for.  A run
 * asking for the timing alone got all three, while the harness printed
 * "timing ON, counting off"; the conclusion drawn from it, that the timing
 * alone was enough to hang the machine, was never tested.  A value that
 * means one thing to the caller and another to the callee is not a
 * convenience.
 */
void OSMGAMesaHookInstrument(int on)
{
    hookInstrument = on;
}
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
