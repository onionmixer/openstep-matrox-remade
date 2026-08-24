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
