# The replay contract keeps indices alive but not state

## What I found while checking something else

The contract at OpenStepMGAMesaHook.c:352-368 is explicit, and it is about ONE
thing: a refused batch replays its source triangles through Mesa, so the VB
INDICES must still be valid. Three gates protect that -- no accumulation
across a render bracket, no batching of a clipping VB, no batching under
multipass.

Nothing in the contract protects the CONTEXT STATE the software rasteriser
reads. And vbrender mutates two pieces of it around every single triangle
callback, not around the bracket:

1. `ctx->PolygonZoffset` is computed just before the callback and zeroed
   just after it (vbrender.c:298-304 and 324-328). Mesa's triangle template
   adds it to the vertex Z (tritemp.h:718).

2. With two-side lighting, `VB->ColorPtr`, `VB->IndexPtr` and `VB->Specular`
   are pointed at the front or back array according to this triangle's facing
   (vbrender.c:306-311), and never put back.

The replay runs in osmgaMesaFlushPending (line 459), which is reached from
RenderFinish (line 1867) and from the state hooks -- outside the triangle
callback. By then PolygonZoffset is 0.0 and the pointers hold the last
triangle's facing.

So a refused batch is replayed with polygon offset silently dropped, and,
under two-side lighting, with every triangle taking the last one's facing.

The hook does not sidestep either. It accelerates offset triangles -- it
computes the offset itself, in double, from Polygon.OffsetFactor/OffsetUnits
(lines 989-1009), which is Mesa's offset_polygon transcribed. And it never
tests Light.Model.TwoSide: the only ColorControl test is inside
osmgaMesaTexStateOK (line 1215), so an untextured two-side scene batches.

`PolygonZoffset` appears nowhere in the driver's userland -- verified by grep
over mesa/, hw3d/ and test/.

The immediate softly calls (lines 715, 723, 774, 797, 803, 1045) are fine:
they run inside the callback while the state is still live. Only the batch
replay is exposed.

## Reachability

Refusals are a live path, not a theoretical one -- the sliver replay is a
tracked work item, and there is already a test knob that corrupts the batch
magic so the kernel refuses for real (line 389-392). Damage needs a refusal
to coincide with offset or two-side lighting, which no current test does.
That is why it has not shown up.

## The fix

Record the state with the triangle, restore it around the replay.

At the record point (lines 1151-1155) add to the pendSrc entry:

    GLfloat        zoff;   /* ctx->PolygonZoffset, live here */
    GLvector4ub   *cptr;   /* VB->ColorPtr */
    GLvector1ui   *iptr;   /* VB->IndexPtr */
    GLubyte      (*spec)[4]; /* VB->Specular */

At the replay loop (line 459) save what the context holds on the way in, set
each triangle's four values before its softly call, and put the saved four
back after the loop -- the values must be restored because the next triangle
to record inherits whatever the pointers hold.

16 bytes a pending triangle, 180 entries, so 2.9 KB more static and four
stores a triangle. Whether that costs anything measurable is a question for
the measurement, not for the design.

## How it gets proved

The refusal-injection knob makes this testable without waiting for a natural
refusal: a scene with polygon offset enabled, drawn twice -- once with the
knob off (hardware) and once with it on (every batch refused, so everything
goes through the replay) -- must produce the same image. Today it must not.
A two-side lit scene is the second case. Both compare against pure software
as well, which is the existing gate.

## Questions

1. Is the state list complete? Is there anything else vbrender or the
   pipeline mutates per triangle that a software triangle function reads?
2. Is restoring the pointers after the loop right, or should they be left
   as the last replayed triangle set them?
3. Is there a case where the recorded pointer is dangling by flush time?
4. Should the chooser simply refuse two-side lighting instead, and is there
   a reason the existing code never tested for it?
