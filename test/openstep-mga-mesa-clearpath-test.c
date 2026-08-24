/*
 * Did the accelerated clear happen, and if not, which gate stopped it?
 *
 * Runs in a second, which is the point: the frame-cost test takes the better
 * part of an hour, and asking it "why did nothing change" once per rebuild
 * is not a way to find anything out.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <GL/osmesa.h>
#include <GL/gl.h>

#include "../mesa/OpenStepMGAMesaHook.h"

/*
 * The size comes from the command line, and the reason is a measurement that
 * could not be made twice in one process: the back end binds its video
 * memory surface to ONE context, and rebinding that context to a buffer of a
 * different size does not get the surface back.  A second context does not
 * get it either.  So each size is its own run.
 */
static int W = 128;
static int H = 96;

static const char *
why(int c)
{
    switch (c) {
    case 0:  return "taken";
    case 1:  return "software was forced";
    case 2:  return "there is no batch";
    case 3:  return "the surface is not the engine's";
    case 4:  return "the stride is not aligned";
    case 5:  return "not RGBA";
    case 6:  return "a colour mask is set";
    case 7:  return "the colour destination is not front-left alone";
    case 8:  return "the colour bit was not asked for";
    case 9:  return "there is a software alpha buffer";
    case 10: return "the box is empty";
    case 11: return "the builder refused the first triangle";
    case 12: return "the builder refused the second";
    case 13: return "the quad has no area";
    case 14: return "the driver refused the batch";
    default: return "unknown";
    }
}

/*
 * The counter goes up when the fast path is ENTERED, not when it succeeds,
 * so it alone cannot tell "the engine cleared it" from "the engine was asked
 * and said no".  The reason says which, and when the driver is the one that
 * said no, its verdict says why.
 */
static void
report(const char *what, int entered)
{
    int c = OSMGAMesaHookClearWhy();

    if (entered && c == 0) {
        printf("   %s : CLEARED on the engine\n", what);
        return;
    }
    printf("   %s : not cleared -- %s\n", what, why(c));
    if (c == 14) {
        const OSMGAMesaRefusal *r = OSMGAMesaHookLastRefusal();

        printf("        verdict %lu, triangle %lu of %lu, destination"
               " %lux%lu\n",
               r->verdict, r->triangle, r->triCount,
               r->dstWidth, r->dstHeight);
        printf("        y %ld h %ld  ar0 %ld ar6 %ld  fxbndry %08lx"
               "  dwgctl %08lx\n",
               r->tri.y, r->tri.h, r->tri.ar0, r->tri.ar6,
               r->tri.fxbndry, r->tri.dwgctl);
    }
}

/*
 * Twenty frames: a clear, optionally one small triangle, and glFinish.
 *
 * softTri says the triangle is to be drawn while software is forced -- the
 * forcing is the caller's, this only reports.
 */
static unsigned long loopDrew, loopMirrored;

static double
loop(int withTriangle, int softTri)
{
    struct timeval t0, t1;
    int i;
    unsigned long d0, m0;

    (void)softTri;
    glFinish();
    d0 = OSMGAMesaHookDrawn();
    m0 = OSMGAMesaHookMirrors();
    gettimeofday(&t0, (struct timezone *)0);
    for (i = 0; i < 20; i++) {
        glClear(GL_COLOR_BUFFER_BIT);
        if (withTriangle) {
            glColor3f(0.0f, 1.0f, 0.0f);
            glBegin(GL_TRIANGLES);
              glVertex2d(10.0, 10.0);
              glVertex2d(40.0, 10.0);
              glVertex2d(40.0, 34.0);
            glEnd();
        }
        glFinish();
    }
    gettimeofday(&t1, (struct timezone *)0);
    /*
     * What the twenty frames actually DID, not what they were asked to do.
     *
     * This loop was quoted against the frame-cost test for months and the two
     * never agreed.  They were not drawing the same thing: no projection is
     * set anywhere in this file, so the triangle below sits at clip
     * coordinates ten to forty with w = 1 and every vertex is outside the
     * volume -- Mesa threw the whole primitive away and the "triangle cost"
     * printed underneath was the cost of nothing.  A frame that rasterises
     * nothing also never reaches RenderFinish, so it never mirrors either,
     * which is the rest of the gap.  Counting is what settles it.
     */
    loopDrew = OSMGAMesaHookDrawn() - d0;
    loopMirrored = OSMGAMesaHookMirrors() - m0;
    return ((double)(t1.tv_sec - t0.tv_sec) +
            (double)(t1.tv_usec - t0.tv_usec) / 1e6) / 20.0;
}

/*
 * What the clear costs, engine against Mesa, over the SAME context: the same
 * twenty clears run twice, once taken and once forced into Mesa's hands, so
 * the difference is the clear and not the context or the weather.
 *
 * It is no longer the clear alone.  The engine's clear now delivers its one
 * value straight into the caller's array, so its bracket walks nothing; the
 * software clear writes the surface with the processor and its bracket walks
 * it back.  The difference is both of those, and the line that prints it
 * says so.
 */
static void
timeClears(OSMesaContext c, int w, int h)
{
    unsigned long *buf;
    double eng, soft;
    unsigned long took;
    unsigned long engClearMir = 0UL, softClearMir = 0UL;

    /*
     * The SAME context, rebound to a buffer of this size.
     *
     * A second context was the first thing tried and it measured nothing:
     * the back end binds its video-memory surface to one context at a time,
     * so the second one drew into plain system memory with no surface and no
     * mirror, and the numbers that came back -- microseconds -- were a
     * memset.  Worse, the reason printed alongside them was the LAST reason
     * the fast path had recorded, because in that context the fast path was
     * never called at all.  Rebinding keeps the acceleration.
     */
    buf = (unsigned long *)malloc((unsigned)(w * h) * sizeof(unsigned long));
    if (!buf) return;
    if (!OSMesaMakeCurrent(c, buf, GL_UNSIGNED_BYTE, w, h)) {
        free(buf);
        return;
    }
    glViewport(0, 0, w, h);
    /*
     * A projection, which this test never had.
     *
     * Without one the matrices are the identity, so glVertex2d(10, 10) is
     * clip coordinate ten with w = 1 and the whole triangle falls outside
     * the volume -- Mesa discarded it and every "triangle cost" this test
     * printed was the cost of nothing.  That is also why it disagreed with
     * the frame-cost test by roughly one mirror: a frame that rasterises
     * nothing never reaches RenderFinish, so it never walks the surface
     * back either.  The counters below now assert the triangle is real.
     */
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0.0, (double)w, 0.0, (double)h, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();

    took = OSMGAMesaHookClears();
    eng = loop(0, 0);
    took = OSMGAMesaHookClears() - took;
    engClearMir = loopMirrored;

    OSMGAMesaHookForceSoftware(1);
    soft = loop(0, 0);
    OSMGAMesaHookForceSoftware(0);
    softClearMir = loopMirrored;

    printf("\n   twenty clears of %dx%d, each followed by glFinish\n", w, h);
    printf("   on the engine  : %8.3f ms  (%lu taken, %s)\n",
           eng * 1000.0, took, why(OSMGAMesaHookClearWhy()));
    printf("   left to Mesa   : %8.3f ms\n", soft * 1000.0);
    /*
     * These two no longer pay the same thing, and the line has to say so.
     *
     * The engine arm's clear delivers its one value to the caller's array
     * without reading the surface back, so it pays no walk at all; the Mesa
     * arm writes the surface with the processor and then its bracket walks
     * it back.  The difference is therefore a clear AND a mirror, not a
     * clear alone -- which is what it was when both walked.
     */
    printf("   the difference :  %8.3f ms -- the Mesa clear AND the walk"
           " back its bracket pays;\n                     the engine clear"
           " delivers its one value without reading the surface\n",
           (soft - eng) * 1000.0);
    printf("      clear-only frames mirrored %lu times (engine) and %lu"
           " (Mesa), over 20 frames\n", engClearMir, softClearMir);
    printf("   surface is the engine's here: %s\n",
           (OSMGAMesaBufferOrigin() != 0UL) ? "yes" : "NO -- ignore the above");

    /*
     * And the same two with ONE SMALL TRIANGLE added.
     *
     * This is what the frame-cost test measures and this test did not, and
     * the two disagree about what a Mesa clear costs -- 22 ms here, 160 ms
     * there -- with the triangle as the only difference.  If a handful of
     * pixels costs a hundred and forty milliseconds after a CPU clear and
     * almost nothing after an engine one, then it is not the triangle: it is
     * what a whole-surface CPU write does to the engine access that follows
     * it.  If both cost the same, the disagreement is somewhere else and
     * this rules the interaction out.
     */
    {
        double engTri, softTri;
        unsigned long engDrew, engMir;

        engTri = loop(1, 0);
        engDrew = loopDrew;
        engMir = loopMirrored;
        OSMGAMesaHookForceSoftware(1);
        softTri = loop(1, 1);
        OSMGAMesaHookForceSoftware(0);

        printf("   with one small triangle added to each frame:\n");
        printf("   engine clear + triangle : %8.3f ms  (the triangle cost"
               " %+.3f)\n", engTri * 1000.0, (engTri - eng) * 1000.0);
        printf("      that arm drew %lu batches and mirrored %lu times"
               " over 20 frames\n", engDrew, engMir);
        printf("   Mesa clear   + triangle : %8.3f ms  (the triangle cost"
               " %+.3f)\n", softTri * 1000.0, (softTri - soft) * 1000.0);
        printf("      that arm drew %lu batches and mirrored %lu times"
               " over 20 frames\n", loopDrew, loopMirrored);
        if (engDrew == 0UL)
            printf("   NOTE: the triangle was never rasterised -- no"
                   " projection is set in this test, so it is clipped away."
                   "\n         Every \"triangle cost\" above is the cost of"
                   " nothing, and this loop is\n         NOT the workload the"
                   " frame-cost test measures.\n");
    }
    free(buf);
}

int
main(int argc, char **argv)
{
    OSMesaContext ctx;
    unsigned long *app;
    unsigned long c0, c1;

    if (argc >= 3) {
        W = atoi(argv[1]);
        H = atoi(argv[2]);
        if (W <= 0 || H <= 0) { printf("bad size\n"); return 2; }
    }

    app = (unsigned long *)malloc((unsigned)(W * H) * sizeof(unsigned long));
    if (!app) { printf("no room\n"); return 2; }
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, app, GL_UNSIGNED_BYTE, W, H)) {
        printf("no context\n"); return 2;
    }
    glViewport(0, 0, W, H);
    /* Same projection as timeClears sets; see the note there. */
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0.0, (double)W, 0.0, (double)H, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    printf("the accelerated clear\n\n");
    printf("   surface is the engine's : %s\n",
           (OSMGAMesaBufferOrigin() != 0UL) ? "yes" : "NO");
    printf("   shared depth origin     : %s\n",
           (OSMGAMesaBufferDepthOrigin() != 0UL) ? "yes" : "no");

    c0 = OSMGAMesaHookClears();
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    c1 = OSMGAMesaHookClears();
    report("colour only            ", c1 != c0);

    glEnable(GL_DEPTH_TEST);
    c0 = OSMGAMesaHookClears();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glFinish();
    c1 = OSMGAMesaHookClears();
    report("colour and depth       ", c1 != c0);

    c0 = OSMGAMesaHookClears();
    glClear(GL_DEPTH_BUFFER_BIT);
    glFinish();
    c1 = OSMGAMesaHookClears();
    report("depth only             ", c1 != c0);

    timeClears(ctx, W, H);
    return 0;
}
