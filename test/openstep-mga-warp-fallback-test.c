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
 * A SECOND MODE tests the full-batch path, and it may not be combined with
 * the first.  Lowering the capacity is the only way that path can run --
 * Mesa's immediate buffer flushes at VB_MAX = 216 + VB_START vertices, so a
 * batch reaches 213 vertices and ONE run against a capacity of 720 and
 * sixteen -- but a lowered cap makes many small batches, and with injection
 * on those are many consecutive refusals, so the backstop revokes before
 * the path has been exercised.  Measured: 200 triangles at ten a batch are
 * twenty batches against a limit of eight.
 *
 * So the capacity mode does not inject.  It draws normally with the cap
 * lowered and compares against the SAME TIER uncapped.  That comparison
 * needs no tolerance at all: the cap moves only where batches begin and
 * end, so an identical picture is the whole claim, and any difference is a
 * batching defect rather than a tier difference.
 *
 * The capacities reached are reported in the allocator's OWN units,
 * vertices and runs, because counting input triangles cannot answer the run
 * question: a run ends when dwgctl or alphactrl moves, and no triangle
 * count sees that.
 *
 * A MODE picks what state the scene carries, because a replay has to bring
 * back more than the vertices: Mesa reads texture, depth and blend state
 * from the context when it redraws, and a blended-only fixture cannot say
 * whether the textured or depth-tested cases survive.
 *
 *   /tmp/wfb [triangles]                    the refusal test, blended
 *   /tmp/wfb [triangles] 0 0 blend|tex|depth
 *   /tmp/wfb [triangles] <vcap> <rcap>      the capacity test
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
static int modeTex, modeDepth;

#define TD 16

/* A texture whose texels differ in both axes and all three channels, so a
 * replay that lost the binding could not land on the right colour. */
static void
maketex(void)
{
    static GLubyte px[TD][TD][3];
    GLuint id;
    int x, y;

    for (y = 0; y < TD; y++)
        for (x = 0; x < TD; x++) {
            px[y][x][0] = (GLubyte)(x * 16);
            px[y][x][1] = (GLubyte)(y * 16);
            px[y][x][2] = (GLubyte)(255 - x * 8 - y * 8);
        }
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TD, TD, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, px);
}
static int churn;      /* toggle blending per triangle: a run boundary */

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

    glClear(GL_COLOR_BUFFER_BIT |
            (modeDepth ? GL_DEPTH_BUFFER_BIT : 0));
    for (i = 0; i < n; i++) {
        double x = 20.0 + (double)((i * 7) % 240);

        double y = 20.0 + (double)((i * 11) % 160);

        if (churn) {
            /* alphactrl moves with this, which is what ends a WARP run --
             * and it is also GL state, which is what ends Mesa's vertex
             * buffer.  Whether those two happen together is the question. */
            if (i & 1) glDisable(GL_BLEND); else glEnable(GL_BLEND);
        }
        glColor4ub((GLubyte)(40 + (i * 5) % 200),
                   (GLubyte)(30 + (i * 13) % 200),
                   (GLubyte)(50 + (i * 29) % 200),
                   (GLubyte)(60 + (i * 3) % 160));
        glBegin(GL_TRIANGLES);
          /* Depth walks with the index so the test is a real one: later
           * triangles are nearer and must win, which a replay that lost the
           * depth state would get wrong.  Comfortably separated, for the
           * reason M15's depth row gives. */
          if (modeTex) glTexCoord2d(x / (double)W, y / (double)H);
          glVertex3d(x, y, 0.4 - (double)i * 0.7 / (double)n);
          if (modeTex) glTexCoord2d((x + 46.0) / (double)W, y / (double)H);
          glVertex3d(x + 46.0, y + 6.0, 0.4 - (double)i * 0.7 / (double)n);
          if (modeTex) glTexCoord2d(x / (double)W, (y + 38.0) / (double)H);
          glVertex3d(x + 8.0, y + 38.0, 0.4 - (double)i * 0.7 / (double)n);
        glEnd();
    }
    glFinish();
}

int
main(int argc, char **argv)
{
    OSMesaContext ctx;
    int n = (argc > 1) ? atoi(argv[1]) : NTRI;
    unsigned long vcap = (argc > 2) ? (unsigned long)atol(argv[2]) : 0UL;
    unsigned long rcap = (argc > 3) ? (unsigned long)atol(argv[3]) : 0UL;
    /* argv[4] = "churn" toggles blending per triangle */
    unsigned long d0, wt0, rs0, dr0, dc0, rp0, v0;
    unsigned long drawn, tried, resc, drop, decl, repl, magics;
    long i, differ = 0;
    int bad = 0;

    if (n <= 0 || n > NTRI) n = NTRI;
    churn    = (argc > 4 && strcmp(argv[4], "churn") == 0);
    modeTex  = (argc > 4 && strcmp(argv[4], "tex")   == 0);
    modeDepth = (argc > 4 && strcmp(argv[4], "depth") == 0);
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
    glClearDepth(1.0);
    if (modeTex) { maketex(); glEnable(GL_TEXTURE_2D); }
    if (modeDepth) { glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
                     glDepthMask(GL_TRUE); glDisable(GL_BLEND); }
    glClearColor(0x10/255.0f, 0x20/255.0f, 0x30/255.0f, 1.0f);

    if (vcap != 0UL || rcap != 0UL) {
        /*
         * The capacity mode: draw uncapped, draw capped, compare.  No
         * injection -- see the header for why the two cannot be combined.
         */
        long d2 = 0L;

        printf("a lowered WARP capacity must not change the picture"
               " (%d triangles%s, cap %lu vertices %lu runs)\n\n",
               n, churn ? ", blending toggled per triangle" : "",
               vcap, rcap);
        scene(n);
        memcpy(ref, app, (unsigned)(W * H) * sizeof(unsigned long));
        printf("  %-40s %lu vertices, %lu runs\n", "uncapped batch reached",
               OSMGAMesaHookWarpVtxMax(), OSMGAMesaHookWarpRunMax());
        wt0 = OSMGAMesaHookWarpTried();
        OSMGAMesaHookWarpCap(vcap, rcap);   /* also resets the maxima */
        scene(n);
        for (i = 0; i < (long)W * H; i++)
            if (app[i] != ref[i]) d2++;
        printf("  %-40s %lu\n", "sources into a WARP submission",
               OSMGAMesaHookWarpTried() - wt0);
        printf("  %-40s %lu vertices, %lu runs\n", "capped batch reached",
               OSMGAMesaHookWarpVtxMax(), OSMGAMesaHookWarpRunMax());
        printf("  %-40s %ld of %ld\n", "pixels differing from uncapped",
               d2, (long)W * H);
        if (OSMGAMesaHookWarpTried() - wt0 != (unsigned long)n) {
            printf("      NOT TESTED: the capped pass put %lu sources"
                   " through the tier, wanted %d\n",
                   OSMGAMesaHookWarpTried() - wt0, n);
            bad++;
        }
        if (vcap != 0UL && OSMGAMesaHookWarpVtxMax() > vcap) {
            printf("      the vertex cap did not bind: %lu > %lu\n",
                   OSMGAMesaHookWarpVtxMax(), vcap);
            bad++;
        }
        if (rcap != 0UL && OSMGAMesaHookWarpRunMax() > rcap) {
            printf("      the run cap did not bind: %lu > %lu\n",
                   OSMGAMesaHookWarpRunMax(), rcap);
            bad++;
        }
        if (d2 != 0L) { printf("      the picture moved: %ld pixels\n", d2);
                        bad++; }
        printf("\n%s\n", bad ? "WARPFALLBACK FAIL" : "WARPFALLBACK PASS");
        OSMesaDestroyContext(ctx);
        free(app); free(ref);
        return bad ? 1 : 0;
    }
    printf("a refused WARP batch must not change the picture (%d triangles,"
           " overlapping, %s)\n\n", n,
           modeTex ? "blended and textured"
                   : (modeDepth ? "depth tested" : "blended"));

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
    printf("  %-40s %lu vertices, %lu runs\n",
           "largest batch reached", OSMGAMesaHookWarpVtxMax(),
           OSMGAMesaHookWarpRunMax());

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
