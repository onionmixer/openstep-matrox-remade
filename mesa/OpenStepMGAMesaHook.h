/*
 * OpenStepMGAMesaHook.h - M1-3b-4: give Mesa a triangle function of ours.
 *
 * This is the only file under mesa/ that includes Mesa's own headers, so
 * everything else here stays checkable without a GL context.  It is compiled
 * into our libGL and into no other, and the stock library is built without
 * it, which is what keeps an installation that lacks our package byte for
 * byte the Mesa it always was.
 */

#ifndef OPENSTEP_MGA_MESA_HOOK_H
#define OPENSTEP_MGA_MESA_HOOK_H

#include "OpenStepMGAHW3D.h"   /* OSMGAHW3DTri */

struct gl_context;

/*
 * Called from the driver's UpdateState, after it has chosen its own
 * functions.  Installs ours when the probe says hardware AND the state is
 * one we can draw; otherwise it changes nothing at all, leaving whatever the
 * software driver just decided.
 *
 * Following choose_triangle_function's example: declining is how a state we
 * cannot handle reaches the software rasteriser, and it is per state rather
 * than once, because a program that enables texturing halfway through must
 * stop being accelerated at that point and not before or after.
 */
/*
 * The name is deliberately not ours.  The Mesa port declares an accelerated
 * back end without naming which one, so that tree stays publishable on its
 * own and any driver could supply this.
 */
void OpenStepMesaAccelUpdateState(struct gl_context *ctx, int rowLength,
                                  int yUp);

/*
 * How many triangles have reached the hardware, and how many were handed
 * back to software after we had already said yes.  For tests: a hook that
 * silently never fires looks exactly like one that works.
 */
unsigned long OSMGAMesaHookDrawn(void);
/* Of those, the ones the WARP tier drew.  "The engine drew it" and "THIS
 * tier drew it" are different questions, and a gate on the first passes a
 * run that fell back to trapezoids. */
unsigned long OSMGAMesaHookWarp(void);
/* Sources that entered a WARP submission, whatever became of it.  Warp()
 * counts what the tier DREW and so cannot speak for a run in which every
 * batch was refused -- which is the run a fallback test needs. */
unsigned long OSMGAMesaHookWarpTried(void);
/* The largest WARP batch ever submitted, in the allocator's own units.
 * Counting input triangles cannot answer the run question: a run ends
 * when dwgctl or alphactrl moves, which no triangle count sees. */
unsigned long OSMGAMesaHookWarpVtxMax(void);
unsigned long OSMGAMesaHookWarpRunMax(void);
/*
 * Test only: lower the WARP batch capacities so the full-batch path can
 * run at all.  Mesa's immediate buffer flushes at seventy-two triangles
 * and the largest batch measured is sixty-four, against a capacity of
 * two hundred and forty, so "full, flush, reset, retry" has never
 * executed from immediate-mode GL.  Nought, or a value above the real
 * capacity, leaves that dimension alone: this can only make a batch
 * smaller.  It moves the THRESHOLD and nothing else -- the buffers, the
 * encoder, the submission, the reset and the retry are the production
 * ones -- so it tests that path's control flow and says nothing about
 * the physical 720-vertex boundary, which needs a bigger frontend.
 *
 * It also RESETS the maxima above, because a new cap is a new
 * measurement and carrying the previous regime's numbers forward would
 * let a capped run report the uncapped one's.
 */
void OSMGAMesaHookWarpCap(unsigned long vtx, unsigned long runs);
/* How many clears the engine took, and why the last one was declined --
 * nought when it was taken.  The codes are read off the source; they exist
 * so a clear that quietly does nothing can say which gate stopped it. */
unsigned long OSMGAMesaHookClears(void);
/* How many times a mirror was ASKED for -- render brackets that were not a
 * pure read, plus every glFinish and glFlush, since Driver.Finish and
 * Driver.Flush are the same function.  NOT how many copies happened: the
 * copy itself returns without doing anything when the surface is already
 * clean, which is the ordinary case for the glFinish that ends a frame.
 * Measured, a clear-and-draw frame asks three times and copies twice. */
unsigned long OSMGAMesaHookMirrors(void);
/* How many times a whole-surface clear armed the constant delivery, and how
 * many times that delivery was actually used.  They differ when the bracket
 * that followed a clear turned out to have drawn something -- which is the
 * case the arming is guarded against, so a gap here is the guard working. */
unsigned long OSMGAMesaHookUniformArmed(void);
unsigned long OSMGAMesaHookUniformFills(void);
/*
 * What OSMesa's software clear would write for the current clear colour,
 * handed over by the port rather than worked out here.  Called from OSMesa's
 * ClearColor and once when a surface is taken.
 */
void OpenStepMesaAccelClearPixel(void *ctx, unsigned long word);
int           OSMGAMesaHookClearWhy(void);
unsigned long OSMGAMesaHookDeclined(void);
/* Triangles handed to the software path because this back end could not
 * draw them -- a refusal that cost nothing rather than a triangle. */
unsigned long OSMGAMesaHookSoftware(void);
/* How the chooser answered: hardware state selected, software state left in
 * place.  Selections, not triangles -- see the definitions. */
unsigned long OSMGAMesaHookHardState(void);
unsigned long OSMGAMesaHookSoftState(void);
/* Textured triangles refused here rather than by the kernel: not affine, and
 * no room in video memory for the texture. */
unsigned long OSMGAMesaHookTexPersp(void);
unsigned long OSMGAMesaHookTexAbsent(void);
/* Submissions.  A textured triangle that splits makes two of them. */
/*
 * Take the hardware path out of use, and put it back.
 *
 * This exists for the tests, and it exists because they need something that
 * the ordinary GL state cannot give them.  Every one of them compares the
 * two paths on the SAME buffer inside ONE process, and until now they did it
 * by turning on a full-surface scissor -- a state the chooser refused, which
 * clipped nothing and so changed the path without changing the picture.
 *
 * That was always borrowed rather than owned, and it comes due the moment the
 * scissor is admitted: those comparisons would quietly become hardware
 * against hardware and pass without asking anything.
 *
 * The environment switch is not a substitute.  It is read once, before the
 * first draw, and it turns off the VRAM surface and the shared depth buffer
 * as well as the path -- so a test that used it would not be comparing the
 * same two things, and one of them could not run its comparison at all.
 *
 * So: this changes the CHOOSER and nothing else.  The probe still ran, the
 * surface is still in video memory, the depth buffer is still shared.  It is
 * off unless a test turns it on, and no application has a way to reach it.
 */
void OSMGAMesaHookForceSoftware(int on);
/*
 * Test only: while on, a source the WARP tier would have taken goes down
 * the trapezoid path instead, with no GL state touched.
 *
 * Alternating the tier through a state the policy refuses is the other
 * way, and it is worth doing too -- but it moves the render state, the
 * batching and the run boundaries at the same moment as the tier, so it
 * cannot state the scheduler's invariant by itself.  This can.
 */
void OSMGAMesaHookForceTrapezoid(int on);

/*
 * MEASUREMENT ARMS -- test only, and they make the picture wrong on purpose.
 *   0  normal
 *   1  arm C: build and accumulate trapezoids, then discard instead of
 *      submitting.  Everything up to but not including the ioctl.
 *   3  arm B: the kernel validates and encodes, then returns without
 *      ringing the doorbell.  Needs the driver; the others do not.
 *   2  arm D: return before the trapezoid builder.  Mesa's evaluators,
 *      transform and lighting have already run by then, so what remains is
 *      that geometry plus this hook's entry and state checks.
 * C minus D is the CPU trapezoid build -- the part WARP would take away on
 * the host side.  Arms C and D need no kernel change; arm B needs a driver
 * built with OSMGA_HW3D_SUBMIT_DRY, which a shipped one is not.
 *
 * TEST ONLY, and declared only under OSMGA_MESA_TESTHOOKS for the same reason
 * InjectNamed is: every arm draws the wrong thing or nothing, so a release
 * library must not be able to be talked into one.  The build refuses a
 * release archive carrying these symbols and a test archive missing them.
 */
#ifdef OSMGA_MESA_TESTHOOKS
void OSMGAMesaHookMeasureArm(int arm);
/* arm B only: what the kernel answered, and how many were asked.  A run whose
 * status is not 0 measured something other than what it meant to -- though
 * the stronger check is the encoded dword count, which arm B reports the same
 * as a normal submission. */
unsigned long OSMGAMesaHookDryStatus(void);
unsigned long OSMGAMesaHookDryCount(void);
#endif /* OSMGA_MESA_TESTHOOKS */

/*
 * The pending batch (M1-6).  Triangles accumulate and ship together; these
 * exist for the paths and tests that need to force or observe the boundary.
 * FlushPending ships whatever is accumulated (safe to call empty);
 * BatchLimit(1) reproduces the old one-triangle-per-submission behaviour
 * exactly, which is what the identical-image comparison runs against;
 * Replayed counts source triangles redrawn in software after a refused
 * batch; FlushCounts reports why flushes happened (bracket, key, full,
 * other).
 */
void OSMGAMesaHookFlushPending(void);
/*
 * FAULT INJECTION.  Both of these make the kernel refuse work that is
 * perfectly valid, so the refusal, narrowing and replay paths can be driven
 * on purpose.  Neither can damage anything: the picture stays correct because
 * everything falls back to Mesa's own rasteriser.  What they cost is the
 * caller's acceleration -- enough refusals reach the backstop and the probe
 * revokes for the life of the process.
 *
 * They are NOT alike in status, and the difference is deliberate:
 *
 *   InjectRefusal is a documented feature of the shipped demo (the teapot's
 *   `inject` argument, and five paragraphs of examples/README_teapot.md that
 *   turn on it).  It stays in the release library.  Refusing every batch and
 *   getting a byte-identical picture back out is the clearest demonstration
 *   this project has that the fallback is exact, and a user who runs it
 *   loses nothing but speed, in one process, on purpose.
 *
 *   InjectNamed spoils ONE named trapezoid mid-batch so the narrowing and
 *   the revocation-during-flush paths can be reached from a harness.  It is
 *   pure test machinery, was never offered to anyone, and it is compiled and
 *   declared only under OSMGA_MESA_TESTHOOKS.  The build refuses a release
 *   archive in which its symbols appear, and refuses a test archive in which
 *   they do not.
 */
void OSMGAMesaHookInjectRefusal(int on);

#ifdef OSMGA_MESA_TESTHOOKS
/*
 * Test only.  Makes the kernel refuse the next `submits` batches with a
 * verdict that NAMES a trapezoid, so the narrowing loop runs for real -- the
 * one above cannot, because a corrupt magic is judged before any triangle is
 * looked at.  See the .c for why the distinction matters.
 */
/* `trap` selects WHICH trapezoid of the batch to spoil.  Zero names the
 * first source in the remainder, which makes the narrowing prefix nought and
 * skips the flush's prefix write; a later one is how that write is reached. */
void OSMGAMesaHookInjectNamed(unsigned long submits, unsigned long trap);
unsigned long OSMGAMesaHookInjectedNamed(void);
#endif /* OSMGA_MESA_TESTHOOKS */
/*
 * Work the engine could not take, split by what became of it: redrawn in
 * software because the destination was still there, or lost because it was
 * not.  They were one counter and the difference is the whole question.
 */
unsigned long OSMGAMesaHookRescued(void);
unsigned long OSMGAMesaHookDropped(void);
/*
 * Test only: run the submission instrumentation -- the per-submission
 * timing and the register-change counting.  OFF by default: two
 * gettimeofday calls a submission cost 0.31 ms a frame on this machine
 * and the delta counting another 0.45 ms.  SubmitStats' microsecond
 * field and every Delta* accessor read nought until this is set, and a
 * frame time measured with it set is not the frame time without it.
 */
void OSMGAMesaHookInstrument(int on);
void OSMGAMesaHookBatchLimit(unsigned long limit);
unsigned long OSMGAMesaHookReplayed(void);
/* Refusals narrowed to the named source instead of the whole batch: the
 * good prefix went to the engine, one triangle went to software.  See the
 * narrowing loop in the flush. */
unsigned long OSMGAMesaHookNarrowed(void);
void OSMGAMesaHookFlushCounts(unsigned long out[4]);

/*
 * Test only: what the submissions cost, from this side of the ioctl.
 * out[0] submissions (batches AND clears), out[1] microseconds in the ioctl,
 * out[2] list dwords the kernel reported, out[3] summed completion-poll
 * INDEX (reads are one more each), out[4] the largest such index seen,
 * out[5] how many submissions polled more than once.
 */
void OSMGAMesaHookSubmitStats(unsigned long out[6]);

/*
 * Test only: how much of each trapezoid repeats the one before it.
 * out[0] trapezoids counted, out[1] register values that differed,
 * out[2] DMA blocks the kernel writes today, out[3] blocks it would write
 * if it wrote only the blocks something in them changed.
 */
void OSMGAMesaHookDeltaStats(unsigned long out[4]);

/* How often each of the seven blocks a trapezoid writes actually changes:
 * 0 dwgctl+AR0..2, 1 AR4..6+SGN, 2 DR4/6/7/8, 3 DR10/11/12/14,
 * 4 DR15/DR0/DR2/DR3, 5 alpha, 6 fxbndry+execute. */
void OSMGAMesaHookDeltaBlocks(unsigned long out[7]);
/*
 * Test only: which registers changed TOGETHER, as whole 25-bit patterns
 * with their counts, so a candidate grouping can be priced exactly
 * rather than guessed from each register's own rate.  Bit k is the k'th
 * value in the kernel's own order; see osmgaMesaCountDeltas.  Needs
 * OSMGAMesaHookInstrument.
 */
void OSMGAMesaHookDeltaMasks(unsigned long keys[64],
                             unsigned long counts[64],
                             unsigned long *spill);

/* Per register, in the order the encoder writes them: dwgctl, ar0, ar1, ar2,
 * ar4, ar5, ar6, sgn, DR4, DR6, DR7, DR8, DR10, DR11, DR12, DR14, DR15,
 * z0(DR0), zdx(DR2), zdy(DR3), a0, adx, ady, alphactrl, fxbndry.
 * Which of these the ENGINE itself moves as it draws is a separate question
 * this cannot answer -- it only counts what the client rewrites. */
void OSMGAMesaHookDeltaRegs(unsigned long out[25]);

unsigned long OSMGAMesaHookBatches(void);
/* trapezoids submitted, which is what says a triangle split */
unsigned long OSMGAMesaHookTraps(void);
/* Of those, the ones this back end could not express at all -- as opposed to
 * the ones the kernel refused. */
unsigned long OSMGAMesaHookUnsupported(void);

/*
 * Triangles that arrived while the state gate had chosen software.  Not
 * the same as OSMGAMesaHookSoftState(), which counts the state CHANGES.
 */
unsigned long OSMGAMesaHookGated(void);

/* clip-forced flushes, and the three reasons WARP declined a primitive */
void OSMGAMesaHookWhyBatch(unsigned long out[7]);

/*
 * Batches refused by the validator run in this process before the ioctl,
 * with the named triangle drawn in software.  These never reach the driver
 * and never count toward revocation.
 */
unsigned long OSMGAMesaHookPrevalidated(void);
/* Non-zero once the measurement deadline has passed; the run should quit. */
int OSMGAMesaHookDeadlineHit(void);
unsigned long OSMGAMesaHookLocalVerdictCount(unsigned long verdict);
unsigned long OSMGAMesaHookLocalLastVerdict(void);
unsigned long OSMGAMesaHookLocalLastSite(void);

/* Which lines of the state gate refused, and how often each.  Zero ends. */
#define OSMGA_MESA_GATE_WHY 16
void OSMGAMesaHookGateWhy(unsigned long lines[OSMGA_MESA_GATE_WHY],
                          unsigned long counts[OSMGA_MESA_GATE_WHY]);

/*
 * Which texture check the last refusal came from (OSMGA_HW3D_TEXSITE_*, or
 * nought when no texture check spoke).  The verdict itself is in
 * OSMGAMesaHookLastRefusal(), and the histogram in OSMGAMesaHookVerdictCount().
 */
unsigned long OSMGAMesaHookLastRefusalSite(void);

/*
 * What the kernel said about a batch it would not run.
 *
 * The verdict is the validator's; the status is what the submission itself
 * came to.  They are not the same question -- a verdict of OK with a failing
 * status means validation passed and the trouble came after the engine had
 * the work, which is the one case that must not be drawn again.
 */
#define OSMGA_MESA_VERDICTS 32   /* the kernel's run to 24 inclusive */

typedef struct {
    unsigned long status;       /* 0, or errno-like */
    unsigned long verdict;      /* OSMGA_HW3D_OK or an E_ code */
    unsigned long triangle;     /* which trapezoid, as this back end made them */
    unsigned long triCount;     /* how many were in the batch */
    unsigned long dstWidth, dstHeight;
    OSMGAHW3DTri  tri;          /* a copy of the one that was named */
} OSMGAMesaRefusal;

unsigned long OSMGAMesaHookVerdictCount(unsigned long verdict);
const OSMGAMesaRefusal *OSMGAMesaHookLastRefusal(void);


/* M20 -- the mirror's pixel budget against a narrowed one's.  Only moves
 * when OSMGA_MESA_INST_AREA is on; see OSMGAMesaHookInstrument(). */
/*
 * Narrow the mirror to what the bracket drew.  0 leaves it alone, 1 narrows.
 * Mode 2 also checks the box was enough, and that check exists only in a
 * test build -- it reads the whole surface and the whole caller's array once
 * per bracket, which is a verifier and not a feature; in a release library
 * 2 behaves as 1.  The two counters below then stay at nought, which is why
 * they need no gate of their own.
 */
void OSMGAMesaHookNarrowMirror(int mode);
unsigned long OSMGAMesaHookAreaMissed(void);
unsigned long OSMGAMesaHookAreaVerified(void);

/*
 * TEST ONLY: dropping a source from the dirty box makes the narrowed mirror
 * lose pixels, silently, which is the class R20 named first to retire.  The
 * packaging script refuses a library that defines it.
 */
#ifdef OSMGA_MESA_TESTHOOKS
void OSMGAMesaHookAreaOmit(unsigned long mask);
#endif /* OSMGA_MESA_TESTHOOKS */
unsigned long OSMGAMesaHookAreaAll(void);
unsigned long OSMGAMesaHookAreaBox(void);
unsigned long OSMGAMesaHookAreaFullBr(void);
unsigned long OSMGAMesaHookAreaBoxBr(void);

#endif /* OPENSTEP_MGA_MESA_HOOK_H */
