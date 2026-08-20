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
void OSMGAMesaHookUpdateState(struct gl_context *ctx);

/*
 * How many triangles have reached the hardware, and how many were handed
 * back to software after we had already said yes.  For tests: a hook that
 * silently never fires looks exactly like one that works.
 */
unsigned long OSMGAMesaHookDrawn(void);
unsigned long OSMGAMesaHookDeclined(void);

#endif /* OPENSTEP_MGA_MESA_HOOK_H */
