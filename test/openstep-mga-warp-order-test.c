/*
 * M16 S4-d -- does mixing the two tiers keep the draw order?
 *
 * Order is not a nicety here.  Blending and equal-depth results depend on
 * it, and the hook's whole answer to mixing is that anything queued for one
 * tier is submitted before a source takes another path.  That is an
 * argument in a comment until something measures it.
 *
 * THE ORACLE, and why it is not a byte comparison.  The two tiers differ by
 * a measured colour level (M12 section 8), so comparing an all-trapezoid
 * frame with a mixed one byte for byte would fail on the tier difference
 * and say nothing about order.  So the triangles are OPAQUE and the depth
 * test is off: the last one to write a pixel owns it, and which triangle
 * that is depends on order alone.  Their colours are spaced eight levels
 * apart, so the winner's IDENTITY survives a one-level shift -- the pixel
 * still rounds to the same index.  The test then asks the only question
 * that matters: is the winner at every pixel the same in both runs?
 *
 * A reordering changes a winner.  A source drawn twice does not, but a
 * source drawn twice out of order does, and S4-a already covers duplication
 * and omission by cardinality and by a blended picture.
 *
 * The alternation uses OSMGAMesaHookForceTrapezoid, which changes the path
 * and no GL state at all.  Doing it through a state the policy refuses is
 * the other half of this question and is a separate case below, because
 * that route moves the render state, the batching and the run boundaries at
 * the same moment as the tier and so cannot state the scheduler's invariant
 * on its own.
 *
 * A SECOND CASE takes the production route instead, and needs no hook at
 * all.  The policy refuses nothing today -- osmgaHW3DWarpAdmits returns OK
 * for everything but a null pointer, since repeat and blending were both
 * admitted once the hardware answered -- so the only way a real scene
 * splits across the tiers is a vertex the BUILDER cannot convert.  rhw is
 * qw times the texture's divisor and must land in [0.125, 128]; with an
 * orthographic projection qw is one and it never bites, but under
 * glFrustum qw is one over the eye depth, so triangles past eight units
 * are refused and go down the trapezoid path while nearer ones do not.
 *
 * That case cannot switch tiers inside one process -- the tier is sampled
 * once from the environment -- so it prints a winner map and two runs are
 * compared on the host, which is how every other comparison in this tree
 * works.
 *
 *   /tmp/word [triangles]            the hooked schedule, self-judging
 *   /tmp/word persp [triangles]      the production route, dumps a map
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
#include "../mesa/OpenStepMGAMesaHook.h"

#define W  320
#define H  240
#define NTRI 300
#define STEP 4            /* levels between one triangle's colour and the next;
                           * a one-level tier shift still rounds to the same
                           * index, and four fits sixty-three triangles in a
                           * byte where eight fits thirty-two */
#define CLEARC 0xFF000000UL

static unsigned long *app;
static unsigned char *winA, *winB;

/*
 * Overlapping opaque triangles.  The red channel carries the index times
 * STEP, so a pixel names its writer and a one-level shift cannot rename it.
 */
static int perspMode;

static void
scene(int n, int alternate)
{
    int i;

    glClear(GL_COLOR_BUFFER_BIT);
    for (i = 0; i < n; i++) {
        double x = 10.0 + (double)((i * 5) % 250);
        double y = 10.0 + (double)((i * 9) % 170);
        /* Eye depths from 2 to 62 in the perspective case: qw runs 0.5 down
         * to 0.016, so the near ones convert and the far ones do not. */
        double ez = 2.0 + (double)i;

        if (alternate)
            OSMGAMesaHookForceTrapezoid((i & 1) ? 1 : 0);
        glColor4ub((GLubyte)(4 + i * STEP), 0x40, 0x80, 255);
        glBegin(GL_TRIANGLES);
          if (perspMode) {
              /* The same screen footprint at every depth: the frustum is
               * set so that a unit at eye depth ez covers the same pixels,
               * so the overlap -- and therefore the ordering question -- is
               * the same as in the orthographic case. */
              double s = ez / 8.0;

              glVertex3d((x - 160.0) * s, (y - 120.0) * s, -ez);
              glVertex3d((x + 52.0 - 160.0) * s, (y + 9.0 - 120.0) * s, -ez);
              glVertex3d((x + 11.0 - 160.0) * s, (y + 44.0 - 120.0) * s, -ez);
          } else {
              glVertex2d(x,        y);
              glVertex2d(x + 52.0, y + 9.0);
              glVertex2d(x + 11.0, y + 44.0);
          }
        glEnd();
        /*
         * And rasterise it now, while this triangle's tier is the one in
         * force.
         *
         * glEnd draws nothing.  Mesa marks the primitive in an immediate
         * buffer and rasterises when that buffer nearly fills or when
         * something flushes it, so the path that draws a triangle is the
         * one selected at FLUSH time -- the mesh test learned this the
         * expensive way and its comment says so.  Without the flush the
         * loop toggles a flag that decides nothing: measured, the
         * alternating run came out warp 32, trapezoids 0.
         *
         * BOTH runs flush, so that the only difference between them is the
         * tier and not the batching.  It also means a tier transition
         * always coincides with a flush in this back end, which is worth
         * saying plainly: the only ways to change tier are a GL state the
         * policy refuses, which flushes Mesa's buffer, or this flag, which
         * is read at flush time.  There is no way to change tier in the
         * middle of a buffered group.
         */
        glFlush();
    }
    if (alternate)
        OSMGAMesaHookForceTrapezoid(0);
    glFinish();
}

/* The winner's index, recovered from the red channel.  0xff means nobody. */
static void
winners(unsigned char *out, int n)
{
    long i;

    for (i = 0; i < (long)W * H; i++) {
        unsigned long r = (app[i] >> 16) & 0xFFUL;

        if (app[i] == CLEARC || r < 4UL)
            out[i] = 0xFF;
        else {
            unsigned long k = (r - 4UL + (STEP / 2)) / STEP;

            out[i] = (k < (unsigned long)n) ? (unsigned char)k : 0xFE;
        }
    }
}

int
main(int argc, char **argv)
{
    OSMesaContext ctx;
    int argi = 1;
    int n;
    unsigned long w0, t0, w1, t1;
    long i, differ = 0, covered = 0;
    int bad = 0;

    perspMode = (argc > 1 && strcmp(argv[1], "persp") == 0);
    if (perspMode) argi = 2;
    n = (argc > argi) ? atoi(argv[argi]) : NTRI;

    /* The colour must stay inside a byte, and stay clear of the clear. */
    if (n <= 0 || n > (255 - 4) / STEP) n = (255 - 4) / STEP;
    if (n > NTRI) n = NTRI;

    app  = (unsigned long *)malloc((unsigned)(W * H) * sizeof(unsigned long));
    winA = (unsigned char *)malloc((unsigned)(W * H));
    winB = (unsigned char *)malloc((unsigned)(W * H));
    if (!app || !winA || !winB) { printf("no room\n"); return 2; }
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, app, GL_UNSIGNED_BYTE, W, H)) {
        printf("no context\n"); return 2;
    }
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    if (perspMode)
        /* At eye depth 8 a unit is a pixel, which is what the scaling in
         * scene() is written against. */
        glFrustum(-160.0 / 8.0, 160.0 / 8.0, -120.0 / 8.0, 120.0 / 8.0,
                  1.0, 400.0);
    else
        glOrtho(0.0, (double)W, 0.0, (double)H, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_DITHER);
    glDisable(GL_CULL_FACE); glDisable(GL_TEXTURE_2D); glDisable(GL_ALPHA_TEST);
    glShadeModel(GL_FLAT);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    if (perspMode) {
        /*
         * The production route.  One run, one map; the comparison is
         * between two PROCESSES, because the tier is sampled once from the
         * environment and cannot be changed inside one.
         */
        w0 = OSMGAMesaHookWarp(); t0 = OSMGAMesaHookTraps();
        i  = (long)OSMGAMesaHookSoftware();
        scene(n, 0);
        winners(winA, n);
        printf("# persp %d triangles  warp %lu  trapezoids %lu  software %lu\n",
               n, OSMGAMesaHookWarp() - w0, OSMGAMesaHookTraps() - t0,
               OSMGAMesaHookSoftware() - (unsigned long)i);
        for (i = 0; i < (long)W * H; i++)
            if (winA[i] != 0xFF)
                printf("O %ld %d\n", i, (int)winA[i]);
        OSMesaDestroyContext(ctx);
        free(app); free(winA); free(winB);
        return 0;
    }

    printf("mixing the tiers must not change who owns a pixel (%d opaque"
           " overlapping triangles, %d levels apart)\n\n", n, STEP);

    /* A: one tier throughout. */
    w0 = OSMGAMesaHookWarp(); t0 = OSMGAMesaHookTraps();
    scene(n, 0);
    winners(winA, n);
    w1 = OSMGAMesaHookWarp() - w0; t1 = OSMGAMesaHookTraps() - t0;
    printf("  %-34s warp %lu, trapezoids %lu\n", "one tier throughout", w1, t1);
    if (w1 == 0UL) {
        printf("      NOT TESTED: the WARP tier drew none of it\n");
        bad++;
    }

    /* B: alternating, with no GL state moved. */
    w0 = OSMGAMesaHookWarp(); t0 = OSMGAMesaHookTraps();
    scene(n, 1);
    winners(winB, n);
    w1 = OSMGAMesaHookWarp() - w0; t1 = OSMGAMesaHookTraps() - t0;
    printf("  %-34s warp %lu, trapezoids %lu\n", "alternating", w1, t1);
    if (w1 == 0UL || t1 == 0UL) {
        printf("      NOT TESTED: the run was not mixed (%lu / %lu)\n", w1, t1);
        bad++;
    }

    for (i = 0; i < (long)W * H; i++) {
        if (winA[i] != 0xFF) covered++;
        if (winA[i] != winB[i]) differ++;
    }
    printf("  %-34s %ld\n", "pixels with an owner", covered);
    printf("  %-34s %ld\n", "pixels whose owner changed", differ);
    if (covered < 1000L) {
        printf("      NOT TESTED: almost nothing was drawn\n");
        bad++;
    }
    if (differ != 0L) {
        printf("      the order moved\n");
        bad++;
    }
    /* An index the decoder could not name is a fixture fault, not a
     * driver one, and must not be mistaken for agreement. */
    for (i = 0; i < (long)W * H; i++)
        if (winA[i] == 0xFE || winB[i] == 0xFE) {
            printf("      the fixture's colours are not decodable\n");
            bad++;
            break;
        }

    printf("\n%s\n", bad ? "WARPORDER FAIL" : "WARPORDER PASS");
    OSMesaDestroyContext(ctx);
    free(app); free(winA); free(winB);
    return bad ? 1 : 0;
}
