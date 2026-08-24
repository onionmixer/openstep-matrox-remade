/*
 * What the whole-surface mirror actually costs.
 *
 * The back end hands Mesa the video memory surface itself and, after every
 * frame, copies ALL of it back to the application's buffer -- row by row,
 * every row, every time (mesa/OpenStepMGAMesaBuffer.c, OSMGAMesaBufferMirror).
 * The comment there records narrowing it as work rather than guessing at it,
 * because doing so needs both the engine and the software rasteriser to say
 * what they touched, and a box that is too small drops pixels silently.
 *
 * Before designing that, this asks whether it is worth designing: the copy is
 * uncached reads across the bus and grows with the AREA of the surface, while
 * one small triangle costs the same whatever the surface is.  So the same
 * frame is timed at three sizes.  If the time tracks the area, the mirror is
 * the frame; if it does not, narrowing it would buy nothing and the honest
 * answer is to leave it alone.
 *
 * Not a pass-or-fail test -- there is no right number here.  It prints, and
 * says which way the numbers point.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <GL/osmesa.h>
#include <GL/gl.h>

#include "../mesa/OpenStepMGAMesaHook.h"

#define FRAMES 20

static double
now(void)
{
    struct timeval tv;

    gettimeofday(&tv, (struct timezone *)0);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

/*
 * One frame: clear, one small triangle, finish.  The triangle is the same
 * handful of pixels at every size on purpose -- it is the constant, and the
 * surface is the variable.
 */
/*
 * Two things sweep the whole surface every frame, and the size sweep cannot
 * tell them apart because both scale with the area:
 *
 *   the CLEAR   -- Mesa's own, writing the substituted surface with the CPU
 *   the MIRROR  -- reading all of it back into the application's buffer
 *
 * So the frame is timed again with the clear left out.  What disappears is
 * the clear; what remains is the mirror and the triangle, and the triangle
 * is a handful of pixels.
 */
/*
 * fullQuad asks the question a hardware clear would answer: instead of the
 * small triangle, cover the WHOLE surface with two triangles through the
 * ordinary accelerated path.  That is, to the pixel, what an engine clear
 * would have to do -- same batch, same submission, same wait -- so timing it
 * says whether replacing the CPU clear is worth building before any of it is
 * built.
 */
static double
timeFrames(int w, int h, int doClear, int fullQuad, unsigned long *drew,
           unsigned long *mirrored, int *accel)
{
    OSMesaContext ctx;
    unsigned long *app;
    double t0, t1;
    int i;
    unsigned long before, mirrorsBefore;

    app = (unsigned long *)malloc((unsigned)(w * h) * sizeof(unsigned long));
    if (!app) return -1.0;
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, app, GL_UNSIGNED_BYTE, w, h)) {
        free(app);
        return -1.0;
    }
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0.0, (double)w, 0.0, (double)h, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_DITHER);
    glDisable(GL_TEXTURE_2D); glDisable(GL_CULL_FACE);
    glShadeModel(GL_FLAT);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    /* one frame first, so context set-up is not in the average */
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();

    *accel = (OSMGAMesaBufferOrigin() != 0UL);
    before = OSMGAMesaHookDrawn();
    mirrorsBefore = OSMGAMesaHookMirrors();
    t0 = now();
    for (i = 0; i < FRAMES; i++) {
        if (doClear) glClear(GL_COLOR_BUFFER_BIT);
        glColor3f(0.0f, 1.0f, 0.0f);
        if (fullQuad) {
            glBegin(GL_TRIANGLES);
              glVertex2d(0.0, 0.0);
              glVertex2d((double)w, 0.0);
              glVertex2d((double)w, (double)h);
              glVertex2d(0.0, 0.0);
              glVertex2d((double)w, (double)h);
              glVertex2d(0.0, (double)h);
            glEnd();
        } else {
            glBegin(GL_TRIANGLES);
              glVertex2d(10.0, 10.0);
              glVertex2d(40.0, 10.0);
              glVertex2d(40.0, 34.0);
            glEnd();
        }
        glFinish();
    }
    t1 = now();
    *drew = OSMGAMesaHookDrawn() - before;
    /*
     * And how many times the surface was walked back into system memory.
     *
     * Printed because this test and the clear-path test were quoted against
     * each other while measuring different work: there the triangle is
     * clipped away, so nothing rasterises and nothing mirrors.  The mirror
     * count is what tells the two workloads apart at a glance.
     */
    *mirrored = OSMGAMesaHookMirrors() - mirrorsBefore;

    OSMesaDestroyContext(ctx);
    free(app);
    return (t1 - t0) / (double)FRAMES;
}

int
main(void)
{
    static const struct { int w, h; const char *n; } sizes[3] = {
        { 128,  96, "128x96 " },
        { 512, 384, "512x384" },
        { 1024, 768, "1024x768" }
    };
    double ms[3];
    double area[3];
    int i;

    printf("what a frame costs as the surface grows\n\n");
    printf("   the triangle is the same at every size; only the surface"
           " changes\n\n");
    printf("   %-9s %10s %12s %14s %s\n",
           "surface", "pixels", "ms/frame", "ns/pixel", "batches");

    for (i = 0; i < 3; i++) {
        unsigned long drew = 0UL;
        unsigned long mirrored = 0UL;
        int accel = 0;
        double t = timeFrames(sizes[i].w, sizes[i].h, 1, 0, &drew, &mirrored,
                              &accel);

        if (t < 0.0) {
            printf("   %-9s  could not be made\n", sizes[i].n);
            ms[i] = -1.0;
            area[i] = 0.0;
            continue;
        }
        area[i] = (double)sizes[i].w * (double)sizes[i].h;
        ms[i] = t * 1000.0;
        printf("   %-9s %10.0f %12.3f %14.2f %lu (%lu mirrors)%s\n",
               sizes[i].n, area[i], ms[i], t * 1e9 / area[i], drew, mirrored,
               accel ? "" : "   <-- NOT on the engine's surface");
    }

    if (ms[0] > 0.0 && ms[2] > 0.0) {
        double areaRatio = area[2] / area[0];
        double timeRatio = ms[2] / ms[0];

        printf("\n   the surface grew %.1fx and the frame grew %.1fx\n",
               areaRatio, timeRatio);
        /*
         * Half the area ratio is the line in the sand: below it the frame is
         * dominated by things that do not scale with the surface, and
         * narrowing the mirror would be effort spent on the smaller half.
         */
        if (timeRatio > areaRatio / 2.0)
            printf("   the frame tracks the AREA: the whole-surface mirror"
                   " is what a frame costs,\n   and narrowing it is worth"
                   " designing\n");
        else
            printf("   the frame does NOT track the area: something else"
                   " dominates, and\n   narrowing the mirror would buy"
                   " little\n");
    }
    /*
     * And where it goes.  One size, big enough for the per-frame sweep to
     * dominate and small enough to run twice without the wait being the
     * afternoon.
     */
    {
        unsigned long d1 = 0UL, d2 = 0UL, m1 = 0UL, m2 = 0UL;
        int a1 = 0, a2 = 0;
        double withClear = timeFrames(512, 384, 1, 0, &d1, &m1, &a1);
        double noClear   = timeFrames(512, 384, 0, 0, &d2, &m2, &a2);

        printf("\n   where the frame goes, at 512x384\n");
        if (withClear > 0.0 && noClear > 0.0) {
            double clearMs = (withClear - noClear) * 1000.0;

            printf("   clear + triangle + finish : %8.3f ms\n",
                   withClear * 1000.0);
            printf("   triangle + finish         : %8.3f ms\n",
                   noClear * 1000.0);
            /*
             * This line used to say "so the clear is", and that was wrong in
             * a way that mattered: the clear runs on the ENGINE and writes no
             * pixel with the processor, so it cannot cost a hundred and forty
             * milliseconds.  What asking for it costs is a whole extra walk
             * of the surface, because the clear gets its own render bracket
             * (buffers.c:296-313) and this back end mirrors at the end of
             * every bracket.  Calling that "the clear" sent the conclusion
             * underneath the wrong way round.
             */
            printf("   so ASKING FOR THE CLEAR costs : %8.3f ms -- not the"
                   " clearing, which is on the\n"
                   "                                   engine, but the extra"
                   " surface walk its own render\n"
                   "                                   bracket causes\n",
                   clearMs);
            printf("   and drawing without it costs  : %8.3f ms, which is"
                   " one walk plus one triangle\n", noClear * 1000.0);
            printf("   a clear-and-draw frame therefore walks the surface"
                   " TWICE, and neither walk\n   is the drawing: narrowing"
                   " or dropping one of them is the whole of the win\n");
        }
    }

    /*
     * And the question that decides whether an engine clear is worth
     * building: what does covering the whole surface through the ordinary
     * accelerated path cost?  That is the same work an engine clear would
     * do, so if it is not far below the CPU clear there is nothing to win.
     */
    {
        unsigned long d3 = 0UL, d4 = 0UL, m3 = 0UL, m4 = 0UL;
        int a3 = 0, a4 = 0;
        double quad   = timeFrames(512, 384, 0, 1, &d3, &m3, &a3);
        double small  = timeFrames(512, 384, 0, 0, &d4, &m4, &a4);

        printf("\n   what an engine clear would cost, at 512x384\n");
        if (quad > 0.0 && small > 0.0) {
            double quadMs = (quad - small) * 1000.0;

            printf("   whole-surface quad + finish : %8.3f ms  (%lu batches)\n",
                   quad * 1000.0, d3);
            printf("   small triangle + finish     : %8.3f ms  (%lu batches)\n",
                   small * 1000.0, d4);
            printf("   so covering the surface on the engine costs about"
                   " %.3f ms\n", quadMs);
            printf("   against a CPU clear of about 164 ms measured above:"
                   " that is the whole\n   case for building one, or against"
                   " it\n");
        }
    }
    return 0;
}
