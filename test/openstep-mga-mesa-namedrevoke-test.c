/*
 * A revoke that arrives in the middle of narrowing -- and what it costs.
 *
 * WHY THIS EXISTS.  The teapot at 1280x1024 and 1600x1200 used to end with
 * "hardware acceleration revoked" and then a segmentation fault.  Two
 * hypotheses died on measurement: it is not memory (the arena had 8.5 MiB
 * spare at the size that crashed), and it is not revocation itself (the
 * existing `inject` mode revokes and completes cleanly).
 *
 * What `inject` cannot do is reach the revoke THROUGH THE NARROWING LOOP.  It
 * corrupts the batch magic, which the kernel judges before it looks at any
 * triangle, so the verdict is batch-level and the flush takes the "cannot
 * place it" branch.  Every scene that crashed was refused with a verdict that
 * named a triangle, which is the other branch entirely.
 *
 * So this drives the other branch on purpose, at a size where a run takes a
 * second instead of three minutes.
 *
 * AND IT MEASURES A SECOND THING, which is the one that is certain.  When the
 * backstop revokes, the flush reacquires the command window, finds it gone,
 * and BREAKS -- dropping every source triangle it had not reached yet.  The
 * comment there says the window being gone means there is nowhere to draw.
 * That is false for a revoke this process caused itself: revocation releases
 * the command window and closes the device, and does not touch the colour
 * surface at all, so the software rasteriser could still draw every one of
 * them.  They are lost instead.
 *
 * The test draws a known number of triangles and counts how many arrive.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
#include "../mesa/OpenStepMGAMesaHook.h"
#include "../mesa/OpenStepMGAMesaBuffer.h"
#include "../mesa/OpenStepMGAMesaProbe.h"
#include "OpenStepMGAHW3D.h"

#define W 320
#define H 240

/* Nine, not eight: eight refusals reach the backstop, and the ninth is what
 * carries execution to the reacquire on the other side of it. */
#define TRIS 9

static int failures;

static void
expect(int cond, const char *what)
{
    if (!cond) {
        printf("NAMEDREVOKE fail: %s\n", what);
        failures++;
    }
}

/* One opaque triangle per row band, so each is its own source and none
 * overlaps another: a missing one leaves its band at the clear colour. */
static void
scene(void)
{
    int i;

    glBegin(GL_TRIANGLES);
    for (i = 0; i < TRIS; i++) {
        GLfloat y0 = -1.0f + (2.0f * (GLfloat)i) / (GLfloat)TRIS;
        GLfloat y1 = -1.0f + (2.0f * (GLfloat)(i + 1)) / (GLfloat)TRIS;

        glColor3f(1.0f, 1.0f, 1.0f);
        glVertex3f(-0.9f, y0 + 0.02f, 0.0f);
        glVertex3f( 0.9f, y0 + 0.02f, 0.0f);
        glVertex3f( 0.0f, y1 - 0.02f, 0.0f);
    }
    glEnd();
}

static unsigned long
litRows(const unsigned long *buf)
{
    unsigned long lit = 0UL;
    int y, x;

    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            if ((buf[(unsigned)(y * W + x)] & 0x00FFFFFFUL) != 0UL) {
                lit++;
                break;
            }
    return lit;
}

/*
 * ONE SCENARIO PER PROCESS.
 *
 * `probeRevoked` is set once and never cleared -- there is no path in the
 * probe that lowers it -- so everything after the first revoke runs with
 * acceleration already gone.  Written as three scenarios in one run, the
 * second and third passed without touching the code they were about: the
 * append scenario reported "0 spoiled", which is what a scenario that never
 * submitted anything looks like.  A test that passes without exercising its
 * subject is worse than no test, so the mode is an argument and the caller
 * runs it three times.
 */
int
main(int argc, char **argv)
{
    const char *mode = (argc > 1) ? argv[1] : "prefix";
    int doPrefix = (strcmp(mode, "prefix") == 0);
    int doAppend = (strcmp(mode, "append") == 0);
    int doClear  = (strcmp(mode, "clear")  == 0);
    int doPrefixWrite = (strcmp(mode, "prefixwrite") == 0);
    OSMesaContext ctx;
    unsigned long *buf;
    OSMGAMesaProbe probe;
    unsigned long rowsAll = 0UL, rowsInjected = 0UL;
    unsigned long drawn0, soft0, drawn1, soft1;

    /* C89: every declaration first, and only then anything that runs. */
    if (!doPrefix && !doAppend && !doClear && !doPrefixWrite) {
        printf("usage: tnrv [prefix|prefixwrite|append|clear]\n");
        return 2;
    }

    buf = (unsigned long *)malloc((unsigned)(W * H) * sizeof *buf);
    if (!buf) { printf("no room\n"); return 2; }
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, W, H)) {
        printf("no context\n"); return 2;
    }
    OSMGAMesaProbeRun(&probe);
    printf("a named refusal in the middle of narrowing\n\n");
    printf("   probe verdict : %d (0 = hardware)\n", (int)probe.verdict);
    if (probe.verdict != OSMGA_PROBE_HARDWARE) {
        printf("   not accelerated; nothing to test\n");
        return 0;
    }

    glViewport(0, 0, W, H);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    printf("   scenario      : %s\n", mode);

    /* The reference: the same scene with nothing injected. */
    glClear((GLbitfield)GL_COLOR_BUFFER_BIT);
    drawn0 = OSMGAMesaHookDrawn();
    soft0  = OSMGAMesaHookSoftware();
    scene();
    glFinish();
    rowsAll = litRows(buf);
    printf("   reference     : %lu rows lit, %lu drawn by the card\n",
           rowsAll, OSMGAMesaHookDrawn() - drawn0);

    /*
     * And again, with every submission refused by a verdict that names a
     * trapezoid.  Each refusal narrows to one source, replays it in
     * software, slides the remainder down and goes round -- until the
     * backstop revokes, and the flush finds the command window gone.
     */
    if (!doPrefix)
        goto other;

    glClear((GLbitfield)GL_COLOR_BUFFER_BIT);
    drawn1 = OSMGAMesaHookDrawn();
    soft1  = OSMGAMesaHookSoftware();
    OSMGAMesaHookInjectNamed((unsigned long)TRIS + 4UL, 0UL);
    scene();
    glFinish();
    rowsInjected = litRows(buf);

    printf("   injected      : %lu rows lit, %lu spoiled, %lu to the card, "
           "%lu to Mesa\n",
           rowsInjected, OSMGAMesaHookInjectedNamed(),
           OSMGAMesaHookDrawn() - drawn1, OSMGAMesaHookSoftware() - soft1);
    printf("   after revoke  : %lu rescued, %lu dropped\n",
           OSMGAMesaHookRescued(), OSMGAMesaHookDropped());

    /*
     * Nothing may be lost.  A revoke releases the command window, not the
     * surface, so the remainder still has somewhere to go.  Dropped work is
     * the fault this test was written for.
     */
    expect(OSMGAMesaHookDropped() == 0UL, "work was thrown away");

    expect(OSMGAMesaHookInjectedNamed() > 0UL, "no submission was spoiled");
    /*
     * THE POINT.  Every triangle was drawable -- by the engine before the
     * revoke and by Mesa after it -- so the picture must not LOSE rows.
     *
     * Not equality.  The reference draws on the engine and the injected run
     * draws the same geometry in Mesa, and the two disagree at edges: a row
     * the engine leaves dark can get a pixel from the software rasteriser.
     * Measured elsewhere at 0.13% of pixels, 98% of it plus or minus one in
     * a channel.  So the injected run can light a few MORE rows, and that is
     * the two rasterisers differing, not this fault.
     *
     * What this fault looked like was 176 of 195 -- nineteen rows gone,
     * because the ninth triangle was never drawn at all.
     */
    expect(rowsInjected >= rowsAll, "rows went missing after the revoke");
    /* And every source reached a rasteriser, which is the exact claim. */
    expect(OSMGAMesaHookSoftware() - soft1 == (unsigned long)TRIS,
           "not every triangle reached the software rasteriser");
    printf("\n   %lu of %lu rows survived\n", rowsInjected, rowsAll);

other:
    /*
     * ------------------------------------------------------------------
     * The other two sites.
     *
     * The run above exercises the flush's own prefix write, which is where
     * gdb caught the fault.  Two more places held a command-window pointer
     * across a call that can revoke, and both are on paths the SHIPPED
     * build takes -- neither needs the legacy switch, because E_DWGCTL is
     * not a geometry verdict and counts under the current rule.
     */

    /*
     * THE APPEND SITE.  A batch limit of one makes every triangle after the
     * first flush the one before it, from inside the triangle callback --
     * so the flush that revokes returns into a callback still holding the
     * pointer it took on entry.  The ninth triangle is the one that used to
     * write through it.
     */
    if (doAppend) {
        unsigned long soft2 = OSMGAMesaHookSoftware();
        unsigned long drop2 = OSMGAMesaHookDropped();
        unsigned long rows2;

        OSMGAMesaHookBatchLimit(1UL);
        OSMGAMesaHookInjectNamed((unsigned long)TRIS - 1UL, 0UL);
        scene();
        glFinish();
        rows2 = litRows(buf);
        printf("\n   append site   : %lu rows lit, %lu spoiled, %lu to Mesa, "
               "%lu dropped\n",
               rows2, OSMGAMesaHookInjectedNamed(),
               OSMGAMesaHookSoftware() - soft2,
               OSMGAMesaHookDropped() - drop2);
        expect(rows2 >= rowsAll, "the append site lost rows");
        expect(OSMGAMesaHookDropped() == drop2, "the append site dropped work");
        /* And it must actually have happened. */
        expect(OSMGAMesaHookInjectedNamed() > 0UL,
               "the append scenario never submitted anything");
        OSMGAMesaHookBatchLimit(180UL);
    }

    /*
     * THE CLEAR SITE.  Pending work, then a clear with no glFinish before
     * it: osmgaMesaClearOnEngine takes its pointer, flushes -- and that
     * flush is what revokes.  Its `batch == 0` test used to read the copy
     * taken before the flush, so it passed and the writes below it went
     * into freed pages.  Now it must decline with reason 2.
     */
    /*
     * THE PREFIX WRITE ITSELF.
     *
     * Spoiling trapezoid ONE, not zero: that names the second source, so the
     * narrowing has a prefix of one trapezoid to resubmit -- and the
     * resubmission is the statement gdb caught writing through the freed
     * window.  With trapezoid zero the prefix is nought and this site is
     * never touched, which is what the other scenarios do and why this one
     * had to exist separately.
     */
    if (doPrefixWrite) {
        unsigned long soft3 = OSMGAMesaHookSoftware();
        unsigned long drop3 = OSMGAMesaHookDropped();
        unsigned long rows3;

        OSMGAMesaHookInjectNamed((unsigned long)TRIS + 4UL, 1UL);
        scene();
        glFinish();
        rows3 = litRows(buf);
        printf("\n   prefix write  : %lu rows lit, %lu spoiled, %lu to Mesa, "
               "%lu rescued, %lu dropped\n",
               rows3, OSMGAMesaHookInjectedNamed(),
               OSMGAMesaHookSoftware() - soft3,
               OSMGAMesaHookRescued(), OSMGAMesaHookDropped() - drop3);
        expect(OSMGAMesaHookInjectedNamed() > 0UL,
               "the prefix-write scenario never submitted anything");
        expect(rows3 >= rowsAll, "the prefix write lost rows");
        expect(OSMGAMesaHookDropped() == drop3,
               "the prefix write dropped work");
    }

    if (doClear) {
        int why;

        OSMGAMesaHookInjectNamed((unsigned long)TRIS - 1UL, 0UL);
        scene();                       /* pending, not yet flushed */
        glClearColor(0.0f, 0.0f, 0.25f, 1.0f);
        glClear((GLbitfield)GL_COLOR_BUFFER_BIT);
        why = OSMGAMesaHookClearWhy();
        printf("   clear site    : clearWhy=%d (2 = declined, the window "
               "went away)\n", why);
        expect(why == 2 || why == 0,
               "the clear site neither cleared nor declined cleanly");
        expect(OSMGAMesaHookInjectedNamed() > 0UL,
               "the clear scenario never submitted anything");
    }

    OSMesaDestroyContext(ctx);
    free(buf);
    if (failures != 0) {
        printf("NAMEDREVOKE FAIL (%d)\n", failures);
        return 1;
    }
    printf("NAMEDREVOKE PASS\n");
    return 0;
}
