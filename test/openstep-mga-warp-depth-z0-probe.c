/*
 * M13 Z0 -- are the vertices we SEND the right ones?
 *
 * seam's shape B is shape A translated by 37/128 of a pixel, with each
 * vertex's depth taken from the same window-space plane, so the two shapes
 * define the SAME depth plane and must read back the same.  They do not:
 * WARP's fitted depth plane has the right gradients to six parts per
 * million and a constant 3.93 codes high on B (M12 §9.4).
 *
 * Before spending a hardware sweep on the microcode, ask the cheaper
 * question: is the plane WRONG BEFORE IT LEAVES THIS LIBRARY?
 *
 * This prints, for one triangle at shape B's fractional coordinates:
 *
 *   the window coordinates the hook was HANDED, as floats, before its own
 *     fixed-point cast (OSMGAMesaHookLastWin -- the same recorder
 *     openstep-mga-mesa-coord-probe.c exists to read, because a triangle
 *     asked for at integer coordinates once arrived at y = 9 when 10 was
 *     asked for, and recomputing Mesa's viewport transform is not Mesa's
 *     arithmetic);
 *   what osmgaFix makes of them -- reproduced here rather than called,
 *     since it is static, and printed so the reproduction can be checked
 *     against the floats beside it;
 *   the IEEE-754 words OSMGAMesaBuildWarpVertex produces from those, by
 *     calling the real builder rather than modelling it.
 *
 * No arithmetic on them and no verdict: python fits the plane through the
 * three float32 points we actually send and compares it with the intended
 * one.  A test that judged its own numbers here would be judging the
 * builder with the builder.
 *
 *   cc -O -Wall -o /tmp/z0 openstep-mga-warp-depth-z0-probe.c \
 *      -I<mesa>/include -I<repo>/hw3d -L<built> -lGL_mga
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
#include "../mesa/OpenStepMGAMesaTriangle.h"
#include "../mesa/OpenStepMGAMesaWarp.h"

extern double        OSMGAMesaHookLastWin(unsigned long v, unsigned long c);
extern unsigned long OSMGAMesaHookDrawn(void);
extern unsigned long OSMGAMesaHookWarp(void);
extern unsigned long OSMGAMesaHookSoftware(void);
extern unsigned long OSMGAMesaHookDeclined(void);

#define W 320
#define H 240

/*
 * seam's own plane and its own inverse, copied rather than reimplemented:
 * the window code wanted is 20000.5 + 60.37x + 40.11y, and a window code k
 * comes from an object depth of 1 - k/32767.5.
 */
static double
zfor(double vx, double vy)
{
    return 1.0 - (20000.5 + 60.37 * vx + 40.11 * vy) / 32767.5;
}

/* osmgaFix, from Hook.c:185 -- floor(v * 256 + 0.5), with the guard.  Not
 * shared because it is static there, so both forms are printed and python
 * checks that this one agrees with the floats it was given. */
static long
fixlike(double v)
{
    double t;
    long   i;

    if (!(v > -8.0e6) || !(v < 8.0e6))
        return 0L;
    t = v * 256.0 + 0.5;
    i = (long)t;
    return (t < 0.0 && (double)i != t) ? i - 1L : i;
}

static void
words(const char *tag, int i, const OSMGAHW3DVertex *v, int rc)
{
    printf("# warp %s%d rc %d x %08lx y %08lx z %08lx rhw %08lx\n",
           tag, i, rc, (unsigned long)v->x, (unsigned long)v->y,
           (unsigned long)v->z, (unsigned long)v->rhw);
}

int
main(int argc, char **argv)
{
    /* Shape B's first triangle: the quad's v0, v1, v2, at 37/128 = 0.2890625
     * off the integer grid on both axes.  ONE triangle, not the quad -- a
     * region spanning a quad's own diagonal is two primitives, and the
     * reading would mix them. */
    static const double bx[3] = {  40.2890625, 200.2890625, 200.2890625 };
    static const double by[3] = {  40.2890625,  40.2890625, 180.2890625 };
    /* Shape A's, for the control: the same triangle on integer coordinates,
     * which is already on every candidate grid and must show nothing. */
    static const double ax[3] = {  40.0, 200.0, 200.0 };
    static const double ay[3] = {  40.0,  40.0, 180.0 };

    OSMesaContext ctx;
    unsigned long *app;
    const double *vx, *vy;
    const char *which = (argc > 1) ? argv[1] : "B";
    unsigned long d0, w0, s0, x0;
    int i;

    vx = (which[0] == 'A') ? ax : bx;
    vy = (which[0] == 'A') ? ay : by;

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
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_ALWAYS); glDepthMask(GL_TRUE);
    glDisable(GL_BLEND); glDisable(GL_DITHER); glDisable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_2D); glDisable(GL_ALPHA_TEST);
    glDisable(GL_POLYGON_OFFSET_FILL);      /* a slope-dependent term, out */
    glShadeModel(GL_FLAT);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    d0 = OSMGAMesaHookDrawn(); w0 = OSMGAMesaHookWarp();
    s0 = OSMGAMesaHookSoftware(); x0 = OSMGAMesaHookDeclined();

    glColor4ub(200, 100, 50, 255);
    glBegin(GL_TRIANGLES);
      for (i = 0; i < 3; i++)
          glVertex3d(vx[i], vy[i], zfor(vx[i], vy[i]));
    glEnd();
    glFinish();

    printf("# z0 shape %s  surface %dx%d  plane 20000.5 + 60.37x + 40.11y\n",
           which, W, H);
    printf("# counters drawn=%lu software=%lu declined=%lu warp=%lu\n",
           OSMGAMesaHookDrawn() - d0, OSMGAMesaHookSoftware() - s0,
           OSMGAMesaHookDeclined() - x0, OSMGAMesaHookWarp() - w0);
    for (i = 0; i < 3; i++)
        printf("# req %d %.17g %.17g %.17g\n", i, vx[i], vy[i],
               zfor(vx[i], vy[i]));
    for (i = 0; i < 3; i++)
        printf("# win %d %.17g %.17g %.17g\n", i,
               OSMGAMesaHookLastWin((unsigned long)i, 0),
               OSMGAMesaHookLastWin((unsigned long)i, 1),
               OSMGAMesaHookLastWin((unsigned long)i, 2));
    for (i = 0; i < 3; i++)
        printf("# fix %d %ld %ld %ld\n", i,
               fixlike(OSMGAMesaHookLastWin((unsigned long)i, 0)),
               fixlike(OSMGAMesaHookLastWin((unsigned long)i, 1)),
               fixlike(OSMGAMesaHookLastWin((unsigned long)i, 2)));

    /*
     * And through the REAL builder.  qw is 1 for this projection and is not
     * recorded by the hook; python is told so it can say if that mattered.
     */
    for (i = 0; i < 3; i++) {
        OSMGAMesaVertex   mv;
        OSMGAHW3DVertex   wv;
        long              zf;
        int               rc;

        memset(&mv, 0, sizeof mv);
        memset(&wv, 0, sizeof wv);
        mv.x  = fixlike(OSMGAMesaHookLastWin((unsigned long)i, 0));
        mv.y  = fixlike(OSMGAMesaHookLastWin((unsigned long)i, 1));
        zf    = fixlike(OSMGAMesaHookLastWin((unsigned long)i, 2));
        mv.z  = (unsigned long)((zf < 0L) ? 0L : zf);
        mv.qw = 1.0;
        mv.tq = 1.0;
        mv.r = 200UL; mv.g = 100UL; mv.b = 50UL; mv.a = 255UL;
        rc = OSMGAMesaBuildWarpVertex(&mv, (const OSMGAMesaTex *)0, 0.0, &wv);
        words("v", i, &wv, rc);
    }
    printf("# qw assumed 1.0 (orthographic); the hook records x, y and z only\n");
    return 0;
}
