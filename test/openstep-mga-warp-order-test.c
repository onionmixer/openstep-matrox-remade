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
 *   /tmp/word [triangles]
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
static void
scene(int n, int alternate)
{
    int i;

    glClear(GL_COLOR_BUFFER_BIT);
    for (i = 0; i < n; i++) {
        double x = 10.0 + (double)((i * 5) % 250);
        double y = 10.0 + (double)((i * 9) % 170);

        if (alternate)
            OSMGAMesaHookForceTrapezoid((i & 1) ? 1 : 0);
        glColor4ub((GLubyte)(4 + i * STEP), 0x40, 0x80, 255);
        glBegin(GL_TRIANGLES);
          glVertex2d(x,        y);
          glVertex2d(x + 52.0, y + 9.0);
          glVertex2d(x + 11.0, y + 44.0);
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
    int n = (argc > 1) ? atoi(argv[1]) : NTRI;
    unsigned long w0, t0, w1, t1;
    long i, differ = 0, covered = 0;
    int bad = 0;

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
    glOrtho(0.0, (double)W, 0.0, (double)H, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_DITHER);
    glDisable(GL_CULL_FACE); glDisable(GL_TEXTURE_2D); glDisable(GL_ALPHA_TEST);
    glShadeModel(GL_FLAT);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

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
