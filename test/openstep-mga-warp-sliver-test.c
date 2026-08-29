/*
 * M16 S4-e -- the sliver the two tiers disagree about.
 *
 * A frozen vertex triple, found by driving the REAL builder and the REAL
 * validator (scratchpad/crossfind.c), is refused by the version 9 validator
 * as TRICROSS: a near-vertical sliver whose independently rounded edges
 * cross partway down.  TRICROSS is a verdict about TRAPEZOID geometry, so
 * the WARP path cannot reach it and draws the sliver on the engine.
 *
 * The acceptance contract (M16 section 11) is per-tier over a common
 * semantic floor: a tier may accept what it can, provided an accepted draw
 * obeys the same externally visible contract as software.  Backend choice
 * must not be observable.  So the question is not "may WARP have it" but
 * "is WARP right about it", and it is answered against the rational oracle
 * rather than against either tier.
 *
 * The window coordinates are RECORDED, not assumed.  Mesa's transform can
 * move an intended coordinate by a fraction of a step -- the reason
 * openstep-mga-mesa-coord-probe.c exists -- and an oracle fed the requested
 * vertices would be scoring a different triangle from the one drawn.
 *
 * And it takes a PERMUTATION, because before a deny rule is designed the
 * question has to be asked whether this is a convention defect rather than
 * a precision limit.  If reversing the winding or rotating the vertex order
 * makes WARP agree, then nothing is wrong with slivers and something is
 * wrong with how they are handed over -- and a threshold would be papering
 * over it.  Translation is already swept separately: fifteen sub-pixel
 * offsets, and WARP disagrees at every one, so it is not a phase accident.
 *
 *   /tmp/wsliv [offset in 1/256 px] [perm 0..5]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
#include "../mesa/OpenStepMGAMesaHook.h"

/* Declared here as coord-probe does: it is the hook's recorder for
 * the window coordinates it was handed, before its own cast. */
extern double OSMGAMesaHookLastWin(unsigned long v, unsigned long c);

#define W  320
#define H  240
#define CLEARC 0xFF1A1A1AUL

/* crossfind find #2, in 1/256 px: the same triple narrowreplay uses. */
static const double SAX = 51666.0 / 256.0, SAY = 15032.0 / 256.0;
static const double SBX = 51604.0 / 256.0, SBY = 18505.0 / 256.0;
static const double SCX = 51308.0 / 256.0, SCY = 37311.0 / 256.0;

static unsigned long *app;

int
main(int argc, char **argv)
{
    OSMesaContext ctx;
    double off = (argc > 1) ? atof(argv[1]) / 256.0 : 0.0;
    int perm = (argc > 2) ? atoi(argv[2]) : 0;
    /* the six orders of three vertices: 0,1,2 are rotations (same winding),
     * 3,4,5 are the reversals (the other winding) */
    static const int P[6][3] = { {0,1,2},{1,2,0},{2,0,1},
                                 {0,2,1},{2,1,0},{1,0,2} };
    double vx[3], vy[3];
    unsigned long d0, w0, t0, s0;
    long x, y;

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
    glClearColor(0x1A/255.0f, 0x1A/255.0f, 0x1A/255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    d0 = OSMGAMesaHookDrawn(); w0 = OSMGAMesaHookWarp();
    t0 = OSMGAMesaHookTraps(); s0 = OSMGAMesaHookSoftware();

    vx[0] = SAX + off; vy[0] = SAY;
    vx[1] = SBX + off; vy[1] = SBY;
    vx[2] = SCX + off; vy[2] = SCY;
    if (perm < 0 || perm > 5) perm = 0;
    glColor4ub(0xE0, 0x40, 0x20, 255);
    glBegin(GL_TRIANGLES);
      glVertex2d(vx[P[perm][0]], vy[P[perm][0]]);
      glVertex2d(vx[P[perm][1]], vy[P[perm][1]]);
      glVertex2d(vx[P[perm][2]], vy[P[perm][2]]);
    glEnd();
    glFinish();

    printf("# sliver off %s/256 perm %d (%d,%d,%d)\n",
           (argc > 1) ? argv[1] : "0", perm,
           P[perm][0], P[perm][1], P[perm][2]);
    printf("# counters drawn=%lu warp=%lu traps=%lu software=%lu\n",
           OSMGAMesaHookDrawn() - d0, OSMGAMesaHookWarp() - w0,
           OSMGAMesaHookTraps() - t0, OSMGAMesaHookSoftware() - s0);
    /* The vertices as the hook received them, which is what the oracle must
     * be given -- not the ones asked for. */
    for (x = 0; x < 3; x++)
        printf("# win %ld %.17g %.17g\n", x,
               OSMGAMesaHookLastWin((unsigned long)x, 0),
               OSMGAMesaHookLastWin((unsigned long)x, 1));
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            if (app[y * W + x] != CLEARC)
                printf("P %ld %ld\n", x, y);
    OSMesaDestroyContext(ctx);
    free(app);
    return 0;
}
