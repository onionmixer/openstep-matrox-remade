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

unsigned long OSMGAMesaHookBatches(void);
/* trapezoids submitted, which is what says a triangle split */
unsigned long OSMGAMesaHookTraps(void);
/* Of those, the ones this back end could not express at all -- as opposed to
 * the ones the kernel refused. */
unsigned long OSMGAMesaHookUnsupported(void);

/*
 * What the kernel said about a batch it would not run.
 *
 * The verdict is the validator's; the status is what the submission itself
 * came to.  They are not the same question -- a verdict of OK with a failing
 * status means validation passed and the trouble came after the engine had
 * the work, which is the one case that must not be drawn again.
 */
#define OSMGA_MESA_VERDICTS 24

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

#endif /* OPENSTEP_MGA_MESA_HOOK_H */
