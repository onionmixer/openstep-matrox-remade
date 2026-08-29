/*
 * M16 S4-a -- a WARP batch refused before encoding must not change the
 * picture.
 *
 * The claim is narrow, deliberately.  It is about a VALIDATION refusal: the
 * kernel judges the batch before it encodes anything, so nothing was drawn
 * and every accumulated source can be replayed in software.  A failure
 * AFTER the doorbell is a different contract -- the flush revokes the tier
 * and drops the pending work rather than replaying it, because part of the
 * batch may already be on the screen -- and this file says nothing about
 * that one.
 *
 * Why the scene overlaps and blends.  A mesh whose triangles do not touch
 * would give a byte-identical picture even if the replay reordered them,
 * duplicated one, or lost one under another; there would be nothing for the
 * order to change.  So every triangle here overlaps its neighbours and
 * blending is on, which makes the result non-commutative: two sources
 * replayed in the wrong order paint a different pixel, and so does a source
 * replayed twice or not at all.
 *
 * Why the comparison may be exact.  With injection on, NOTHING reaches the
 * engine -- the batch is refused before encoding -- so the picture is
 * entirely Mesa's and the tier's three measured differences (a colour level,
 * a depth code, near-edge ownership) cannot appear.  An exact byte
 * comparison is therefore the right test here and would not be on any other
 * run.
 *
 * The counters are gated too, because a picture alone cannot say the tier
 * was involved: hookWarp counts what the tier DREW and stays at nought when
 * every batch is refused, which is why hookWarpTried exists.
 *
 *   /tmp/wfb [triangles]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
#include "../mesa/OpenStepMGAMesaHook.h"
#include "../hw3d/OpenStepMGAHW3D.h"

#define W  320
#define H  240
#define NTRI 512
#define CLEARC 0xFF102030UL

static unsigned long *app;
static unsigned long *ref;

/*
 * Overlapping triangles marching across the surface, each a little to the
 * right of the last and each large enough to cover several of its
 * predecessors.  The colour and the alpha both walk, so a pair swapped in
 * the replay changes the pixel underneath them.
 */
static void
scene(int n)
{
    int i;

    glClear(GL_COLOR_BUFFER_BIT);
    for (i = 0; i < n; i++) {
        double x = 20.0 + (double)((i * 7) % 240);
        double y = 20.0 + (double)((i * 11) % 160);

        glColor4ub((GLubyte)(40 + (i * 5) % 200),
                   (GLubyte)(30 + (i * 13) % 200),
                   (GLubyte)(50 + (i * 29) % 200),
                   (GLubyte)(60 + (i * 3) % 160));
        glBegin(GL_TRIANGLES);
          glVertex2d(x,        y);
          glVertex2d(x + 46.0, y + 6.0);
          glVertex2d(x + 8.0,  y + 38.0);
        glEnd();
    }
    glFinish();
}

int
main(int argc, char **argv)
{
    OSMesaContext ctx;
    int n = (argc > 1) ? atoi(argv[1]) : NTRI;
    unsigned long d0, wt0, rs0, dr0, dc0, rp0, v0;
    unsigned long drawn, tried, resc, drop, decl, repl, magics;
    long i, differ = 0;
    int bad = 0;

    if (n <= 0 || n > NTRI) n = NTRI;
    app = (unsigned long *)malloc((unsigned)(W * H) * sizeof(unsigned long));
    ref = (unsigned long *)malloc((unsigned)(W * H) * sizeof(unsigned long));
    if (!app || !ref) { printf("no room\n"); return 2; }
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, app, GL_UNSIGNED_BYTE, W, H)) {
        printf("no context\n"); return 2;
    }
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0.0, (double)W, 0.0, (double)H, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_DITHER); glDisable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_2D); glDisable(GL_ALPHA_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glShadeModel(GL_FLAT);
    glClearColor(0x10/255.0f, 0x20/255.0f, 0x30/255.0f, 1.0f);

    printf("a refused WARP batch must not change the picture (%d triangles,"
           " overlapping, blended)\n\n", n);

    /* 1. the reference: everything through Mesa's own rasteriser. */
    OSMGAMesaHookForceSoftware(1);
    scene(n);
    OSMGAMesaHookForceSoftware(0);
    memcpy(ref, app, (unsigned)(W * H) * sizeof(unsigned long));

    /*
     * 2. the tier, with every batch refused before encoding.
     *
     * One frame only, and it is worth saying what that turned out to mean.
     * I sized it expecting three refusals -- 512 sources at the WARP
     * capacity of 240 -- against a backstop that revokes after eight
     * consecutive ones and is reset only by a submission that succeeded.
     * It is eight, because what ends a batch here is the RENDER BRACKET and
     * not the capacity: about sixty-four triangles a batch, which is the
     * same shape mesh16 shows with nine submissions for the same 512.  So
     * one frame reaches the backstop exactly, and the block at the end
     * reports which case was measured instead of leaving it to a line on
     * the console.  Either is a pass; the picture must be exact in both.
     */
    d0  = OSMGAMesaHookDrawn();
    wt0 = OSMGAMesaHookWarpTried();
    rs0 = OSMGAMesaHookRescued();
    dr0 = OSMGAMesaHookDropped();
    dc0 = OSMGAMesaHookDeclined();
    rp0 = OSMGAMesaHookReplayed();
    v0  = OSMGAMesaHookVerdictCount(OSMGA_HW3D_E_MAGIC);

    OSMGAMesaHookInjectRefusal(1);
    scene(n);
    OSMGAMesaHookInjectRefusal(0);

    drawn  = OSMGAMesaHookDrawn()      - d0;
    tried  = OSMGAMesaHookWarpTried()  - wt0;
    resc   = OSMGAMesaHookRescued()    - rs0;
    drop   = OSMGAMesaHookDropped()    - dr0;
    decl   = OSMGAMesaHookDeclined()   - dc0;
    repl   = OSMGAMesaHookReplayed()   - rp0;
    magics = OSMGAMesaHookVerdictCount(OSMGA_HW3D_E_MAGIC) - v0;

    for (i = 0; i < (long)W * H; i++)
        if (app[i] != ref[i]) differ++;

    printf("  %-40s %lu\n", "sources into a WARP submission", tried);
    printf("  %-40s %lu\n", "batches refused", decl);
    printf("  %-40s %lu\n", "of those with the magic verdict", magics);
    printf("  %-40s %lu\n", "sources replayed in software", repl);
    printf("  %-40s %lu\n", "sources rescued", resc);
    printf("  %-40s %lu\n", "sources dropped", drop);
    printf("  %-40s %lu\n", "triangles the engine drew", drawn);
    printf("  %-40s %ld of %ld\n", "pixels differing from all-software",
           differ, (long)W * H);

    if (tried != (unsigned long)n) {
        printf("      NOT TESTED: %lu sources reached a WARP submission,"
               " wanted %d -- the tier was not exercised\n", tried, n);
        bad++;
    }
    if (decl == 0UL || magics != decl) {
        printf("      NOT TESTED: %lu refusals of which %lu were the magic"
               " -- the injection did not do what this test needs\n",
               decl, magics);
        bad++;
    }
    if (drawn != 0UL) {
        printf("      the engine drew %lu triangles; a refused batch must"
               " draw none\n", drawn);
        bad++;
    }
    if (repl != (unsigned long)n || resc != (unsigned long)n) {
        printf("      replayed %lu and rescued %lu, wanted %d of each\n",
               repl, resc, n);
        bad++;
    }
    if (drop != 0UL) {
        printf("      %lu sources were DROPPED; a validation refusal loses"
               " nothing\n", drop);
        bad++;
    }
    if (differ != 0L) {
        printf("      the picture moved: %ld pixels\n", differ);
        bad++;
    }

    /*
     * And whether the backstop tripped, said out loud rather than left to a
     * line on the console.
     *
     * I predicted three refusals -- 512 sources at the WARP capacity of 240
     * -- and got eight, because what ends a batch here is the RENDER
     * BRACKET and not the capacity: 512 triangles make about sixty-four a
     * batch, the same shape mesh16 shows with nine submissions.  Eight is
     * exactly the refusal limit, so this run walks into the revoke, and the
     * only honest thing is to detect it and report which case was measured.
     *
     * Either is a pass, and the picture must be exact in both: a revoke
     * that lost work would show as dropped sources and moved pixels.
     */
    {
        unsigned long d1 = OSMGAMesaHookDrawn();

        scene(1);
        if (OSMGAMesaHookDrawn() == d1)
            printf("\n  the backstop tripped: acceleration was revoked and"
                   " the picture still matched\n");
        else
            printf("\n  the backstop did not trip: acceleration survived\n");
    }

    printf("\n%s\n", bad ? "WARPFALLBACK FAIL" : "WARPFALLBACK PASS");
    OSMesaDestroyContext(ctx);
    free(app); free(ref);
    return bad ? 1 : 0;
}
