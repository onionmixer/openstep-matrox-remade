/*
 * M21 -- does the narrowed mirror deliver everything, and is every source in
 * its box load-bearing?
 *
 * The mirror copies the whole surface back to the application's array at
 * every bracket close, and M20 measured that narrowing it to what the
 * bracket drew would be worth 37x on a real model.  Narrowing is only safe
 * if the box covers EVERY writer, and M21's enumeration of OSMesa's driver
 * table found two that announce nothing: a triangle this back end declines
 * is drawn by OSMesa's own smooth_rgba_z_triangle, and lines are drawn by
 * its flat_rgba_z_line, neither through a span.  Both were harmless while
 * the dirty mark was a flag; both are pixel loss the moment it is a box.
 *
 * So this test does two things, and the second is the one that matters:
 *
 *   1  draw with every writer and check the narrowed mirror lost nothing
 *   2  DROP one source from the box at a time and check that it then loses
 *      something
 *
 * Without (2) a passing run would prove only that the workload never
 * reached the guard.  Each writer draws in its own quadrant and its own
 * bracket, so dropping its source loses that quadrant and nothing else.
 *
 * The check itself needs no reference image: the mirror's contract is that
 * the application's array EQUALS the surface afterwards, so a pixel that
 * still differs is a pixel some writer produced and the box did not cover.
 *
 *   /tmp/tnm
 */
#include <stdio.h>
#include <stdlib.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
#include "../mesa/OpenStepMGAMesaHook.h"
#include "../mesa/OpenStepMGAMesaBuffer.h"

#define W 320
#define H 240

static unsigned long *app;
static OSMesaContext ctx;

/*
 * ONE bounding box per bracket is the whole difficulty in arranging this.
 *
 * The first attempt put each writer in its own quadrant of one bracket, and
 * every arm reported no loss -- because the box is a BOX: four quadrants
 * merge into a rectangle covering the surface, so dropping any one source
 * changed nothing.  The second problem is the mirror image of it: a bracket
 * whose only source is dropped has an EMPTY box, and an empty box correctly
 * falls back to a full mirror, which also loses nothing.
 *
 * So each arm draws exactly two things: the writer under test, far away at
 * the bottom right, and an ANCHOR of a different kind in the top-left
 * corner.  The anchor keeps the box non-empty so the mirror still narrows,
 * and it is small and far away so the box it leaves behind cannot reach
 * what was dropped.
 */
static void
anchorTri(void)
{
    glColor4ub(0x80, 0x80, 0xFF, 255);
    glBegin(GL_TRIANGLES);
      glVertex2d(4.0, 4.0); glVertex2d(24.0, 6.0); glVertex2d(6.0, 24.0);
    glEnd();
}

static void
anchorLine(void)
{
    glColor4ub(0xFF, 0x80, 0x80, 255);
    glBegin(GL_LINES);
      glVertex2d(4.0, 4.0); glVertex2d(24.0, 24.0);
    glEnd();
}

#define FX ((double)W - 90.0)
#define FY ((double)H - 90.0)

static void
farTri(void)
{
    glColor4ub(0xE0, 0x40, 0x20, 255);
    glBegin(GL_TRIANGLES);
      glVertex2d(FX, FY); glVertex2d(FX + 80.0, FY + 10.0);
      glVertex2d(FX + 20.0, FY + 70.0);
    glEnd();
}

static void
farLine(void)
{
    glColor4ub(0x20, 0xE0, 0x40, 255);
    glBegin(GL_LINES);
      glVertex2d(FX, FY); glVertex2d(FX + 80.0, FY + 70.0);
      glVertex2d(FX, FY + 70.0); glVertex2d(FX + 80.0, FY);
    glEnd();
}

/*
 * Points, not glDrawPixels.  Both reach the span writers -- OSMesa installs
 * no Driver.DrawPixels and PointsFunc is NULL, so Mesa draws both itself --
 * but glDrawPixels opens a bracket of its own, and a writer alone in its
 * bracket cannot be tested: dropping its source empties the box and an
 * empty box falls back to a full mirror.  Points stay in the bracket the
 * anchor is in.
 */
static void
farPixels(void)
{
    int i;

    glColor4ub(0x40, 0xC0, 0xE0, 255);
    glBegin(GL_POINTS);
      for (i = 0; i < 60; i++)
          glVertex2d(FX + (double)(i % 10) * 8.0,
                     FY + (double)(i / 10) * 8.0);
    glEnd();
}

/* A clear of its own, in its own bracket, so the arms below start level. */
static void
levelOff(int tick)
{
    glClearColor((float)(tick & 3) / 3.0f, 0.15f, 0.35f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
}

static void
armVertices(int tick)  { levelOff(tick); anchorLine(); farTri();   glFinish(); }
static void
armSpans(int tick)     { levelOff(tick); anchorTri();  farPixels();glFinish(); }
/*
 * The declined triangle has to land in OSMesa's OWN rasteriser for this arm
 * to mean anything, and osmesa.c:610-630 only returns one in a single
 * state: RasterMask exactly DEPTH_BIT, depth func LESS, depth mask true,
 * and DepthBits == DEFAULT_SOFTWARE_DEPTH_BITS, which is 16 and is what
 * this context has.  In every other state a declined triangle goes to
 * Mesa's generic rasteriser, which writes through the spans -- already in
 * the box, and nothing to prove.
 *
 * TWO things had to be got right here, both measured rather than reasoned:
 *
 *   the switch has to stay on across glFinish.  Immediate-mode vertices are
 *   buffered and the triangle function runs at the FLUSH, so turning it off
 *   after glEnd turned it off before anything had been drawn -- the counter
 *   said "drawn 2, soft 0" and the arm was testing the accelerated path
 *   against itself.
 *
 *   and the anchor has to be a LINE.  The switch applies at the flush, to
 *   the whole vertex buffer, so an anchor triangle in the same bracket
 *   would go to software too -- leaving the box empty when this source is
 *   dropped, and an empty box correctly falls back to a full mirror.
 */
static void
armSoftTri(int tick)
{
    levelOff(tick);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glFinish();

    anchorLine();
    OSMGAMesaHookForceSoftware(1);
    farTri();
    glFinish();                 /* the flush is where the switch is read */
    OSMGAMesaHookForceSoftware(0);
    glDisable(GL_DEPTH_TEST);
}

static void
armLines(int tick)     { levelOff(tick); anchorTri();  farLine();  glFinish(); }

/*
 * The clear needs a SCISSORED one, and finding that out was the point of
 * running this rather than reasoning about it.
 *
 * A whole-surface clear never reaches the narrowed mirror at all: the hook
 * already recognises it and delivers it with OSMGAMesaBufferFill, one word
 * over the array, with no mirror and no box.  So dropping the clear from
 * the box lost nothing, and the arm proved only that the fast path works.
 *
 * A clear that is not the whole surface has no such path, and the mark this
 * arm is testing -- the one-shot that carries "the engine wrote where no
 * vertex and no span went" across into the next bracket -- is the only
 * thing that covers it.  With it, the bracket is delivered whole; without
 * it, only the anchor is, and the cleared band is lost.
 */
static void
armClear(int tick)
{
    levelOff(tick);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, W, H - 20);
    glClearColor((float)(tick & 3) / 3.0f, 0.6f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    anchorTri();
    glFinish();
    glDisable(GL_SCISSOR_TEST);
}

static void
allArms(int tick)
{
    armVertices(tick); armSpans(tick + 1); armSoftTri(tick + 2);
    armLines(tick + 3); armClear(tick + 4);
}

struct arm {
    unsigned long omit;
    const char   *what;
    void        (*draw)(int);
    int           mustLose;
};

int
main(void)
{
    static struct arm arms[] = {
        { 0UL,  "nothing dropped",      allArms,    0 },
        { 1UL,  "accelerated vertices", armVertices,1 },
        { 2UL,  "span writes",          armSpans,   1 },
        { 4UL,  "software triangles",   armSoftTri, 1 },
        { 8UL,  "lines",                armLines,   1 },
        { 16UL, "the clear",            armClear,   2 }
    };
    int n = (int)(sizeof arms / sizeof arms[0]);
    int i, bad = 0;

    app = (unsigned long *)malloc((unsigned)(W * H) * sizeof(unsigned long));
    if (!app) { printf("no room\n"); return 2; }
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, app, GL_UNSIGNED_BYTE, W, H)) {
        printf("no context\n"); return 2;
    }
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0.0, (double)W, 0.0, (double)H, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_DITHER);
    glDisable(GL_CULL_FACE); glDisable(GL_TEXTURE_2D); glDisable(GL_ALPHA_TEST);
    glShadeModel(GL_FLAT);

    if (OSMGAMesaBufferOrigin() == 0UL) {
        printf("NOT TESTED -- the surface is not the engine's, so the mirror\n"
               "does not run and there is nothing here to narrow\n");
        return 1;
    }

    printf("the narrowed mirror, and whether each source in its box matters\n\n");
    printf("   %-24s %5s %5s %10s   %s\n", "dropped from the box",
           "narr", "full", "px lost", "verdict");

    for (i = 0; i < n; i++) {
        unsigned long m0, m1, b0, b1, f0, f1, s0, s1, c0, c1, d0, d1;

        OSMGAMesaHookNarrowMirror(2);          /* narrow, and check it */
        OSMGAMesaHookAreaOmit(arms[i].omit);
        m0 = OSMGAMesaHookAreaMissed();
        b0 = OSMGAMesaHookAreaBoxBr(); f0 = OSMGAMesaHookAreaFullBr();
        s0 = OSMGAMesaHookSoftware(); c0 = OSMGAMesaHookClears();
        d0 = OSMGAMesaHookDrawn();
        (*arms[i].draw)(i + 1);
        m1 = OSMGAMesaHookAreaMissed();
        b1 = OSMGAMesaHookAreaBoxBr(); f1 = OSMGAMesaHookAreaFullBr();
        s1 = OSMGAMesaHookSoftware(); c1 = OSMGAMesaHookClears();
        d1 = OSMGAMesaHookDrawn();
        printf("      [drawn %lu, soft %lu, engine clears %lu, why %d]\n",
               d1 - d0, s1 - s0, c1 - c0, OSMGAMesaHookClearWhy());

        /*
         * Back to a full mirror for one pass, so the next arm starts from an
         * array that agrees with the surface again -- otherwise this arm's
         * loss would be charged to the one after it.
         */
        OSMGAMesaHookAreaOmit(0UL);
        OSMGAMesaHookNarrowMirror(0);
        allArms(i + 20);

        if (b1 == b0) {
            printf("   %-24s %5lu %5lu %10s   NOT TESTED -- nothing narrowed,"
                   " so the box was never used\n",
                   arms[i].what, b1 - b0, f1 - f0, "-");
            bad++;
            continue;
        }
        if (arms[i].mustLose == 2) {
            /*
             * The clear's one-shot could not be made to matter, and the
             * reason is worth more than a pass would have been: a clear
             * occupies a bracket in which nothing else writes, so the box is
             * EMPTY and an empty box already falls back to a full mirror.
             * The guard is what covers a clear that shares a bracket with
             * drawing -- which this test could not construct.  Reported as
             * unreached rather than passed, because a guard nobody can
             * exercise is a guard nobody has tested.
             */
            printf("   %-24s %5lu %5lu %10lu   NOT REACHED -- a clear's"
                   " bracket has no other writer, so the empty box already\n"
                   "   %-24s %5s %5s %10s   forces a full mirror; this guard"
                   " covers a clear sharing a bracket with drawing\n",
                   arms[i].what, b1 - b0, f1 - f0, m1 - m0, "", "", "", "");
            continue;
        }
        if (arms[i].mustLose ? (m1 > m0) : (m1 == m0))
            printf("   %-24s %5lu %5lu %10lu   %s\n", arms[i].what,
                   b1 - b0, f1 - f0, m1 - m0,
                   arms[i].mustLose ? "ok, it is needed"
                                    : "ok, nothing was missed");
        else {
            printf("   %-24s %5lu %5lu %10lu   FAIL -- %s\n", arms[i].what,
                   b1 - b0, f1 - f0, m1 - m0,
                   arms[i].mustLose
                     ? "dropping it lost nothing, so this arm never reached"
                       " that writer"
                     : "the narrowed mirror lost pixels with the whole box");
            bad++;
        }
    }

    printf("\n   %s\n", bad ? "FAIL" :
           "PASS -- the box covers every writer, and four of its five sources"
           " are shown to be load-bearing");
    OSMesaDestroyContext(ctx);
    free(app);
    return bad ? 1 : 0;
}
