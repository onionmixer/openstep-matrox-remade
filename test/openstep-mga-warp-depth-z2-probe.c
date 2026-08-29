/*
 * M13 Z2 -- is the sixteenth-of-a-pixel floor PER AXIS?
 *
 * Z1 established that WARP's depth start behaves as though the vertex
 * position were floored to 1/16 of a pixel -- K * (t mod 8)/128 to within
 * eleven thousandths of a code over all 128 translations, floor and not
 * round, following the position and not the submitted z.  But Z1 moved x
 * and y TOGETHER, so it could not tell "each axis floors" from "only the
 * diagonal does", and the bound it published assumes the former.
 *
 * Six modes, each a 128-step translation sweep like Z1's, differing in one
 * thing so that what a difference means is not a matter of opinion:
 *
 *   x    translate x only.  If each axis floors independently the excess is
 *        gx * (t mod 8)/128, amplitude gx*7/128 = 3.301 codes.
 *   y    translate y only.  Likewise gy*7/128 = 2.194 codes.
 *   d    diagonal, the control: Z1's own case, and it must come out as the
 *        SUM, 5.495, or the two sweeps above do not add up to it.
 *   xn   x only with dz/dx NEGATED.  A position floor makes the excess
 *        follow the gradient's sign, so this must be -3.301.  A depth that
 *        merely reads low would not change sign.
 *   xb   x only, the whole triangle moved to a different INTEGER base.  A
 *        floor phase-locked to pixel boundaries is unmoved by that; a grid
 *        anchored to the primitive's own origin or to a tile is not.
 *   xw   x only, winding reversed.  Tests whether the anchor is the first
 *        vertex.
 *
 * Everything Z1 guarded is guarded here for the same reasons: one triangle
 * and never a quad, a sample grid fixed in screen space at least
 * twenty-three pixels inside every edge, GL_ALWAYS with a known clear,
 * polygon offset off, codes far from saturation at both ends, the t order
 * shuffled inside the process, and per-t tier counters so an arm that fell
 * back for one translation cannot be averaged in silently.
 *
 * It prints raw depth codes.  python does the arithmetic.
 *
 *   /tmp/z2 <x|y|d|xn|xb|xw> <pass>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>
#include <GL/osmesa.h>

extern unsigned long OSMGAMesaHookDrawn(void);
extern unsigned long OSMGAMesaHookWarp(void);
extern unsigned long OSMGAMesaHookSoftware(void);
extern unsigned long OSMGAMesaHookDeclined(void);

#define W 320
#define H 240

/*
 * One intercept for every mode, chosen so the negated-gradient mode stays
 * as far from both ends as the others: with dz/dx = -60.37 the codes run
 * 12691 .. 37617, and with it positive 32010 .. 56935.  The excess is a
 * difference between translations, so the intercept cancels out of every
 * number this produces -- it is here only to keep the buffer honest.
 */
#define C0   30000.5
#define GY      40.11
static double gxOf(const char *m) { return (strcmp(m, "xn") == 0) ? -60.37 : 60.37; }

#define SX0 200
#define SX1 280
#define SY0  40
#define SY1 120
#define SSTEP 4

static unsigned long rngState;
static unsigned long
rnd(void)
{
    rngState = (rngState * 16807UL) % 2147483647UL;
    return rngState;
}

int
main(int argc, char **argv)
{
    const char *mode = (argc > 1) ? argv[1] : "x";
    int         pass = (argc > 2) ? atoi(argv[2]) : 1;
    OSMesaContext ctx;
    unsigned long *app;
    unsigned short *zb;
    GLint dw, dh, bpv;
    void *zvoid;
    int order[128], i, j, k, n;
    long x, y;
    double vx[3], vy[3], gx, bx, by;
    int movex, movey;

    if (strcmp(mode,"x") && strcmp(mode,"y") && strcmp(mode,"d") &&
        strcmp(mode,"xn") && strcmp(mode,"xb") && strcmp(mode,"xw")) {
        printf("mode is x, y, d, xn, xb or xw\n"); return 2;
    }
    gx    = gxOf(mode);
    movex = (mode[0] == 'x' || mode[0] == 'd');
    movey = (mode[0] == 'y' || mode[0] == 'd');
    /* A different integer base for xb, and only for xb. */
    bx = (strcmp(mode, "xb") == 0) ? 7.0 : 0.0;
    by = (strcmp(mode, "xb") == 0) ? 3.0 : 0.0;
    vx[0] = 20.0 + bx; vy[0] =  20.0 + by;
    if (strcmp(mode, "xw") == 0) {          /* the same triangle, wound the
                                             * other way */
        vx[1] = 300.0 + bx; vy[1] = 220.0 + by;
        vx[2] = 300.0 + bx; vy[2] =  20.0 + by;
    } else {
        vx[1] = 300.0 + bx; vy[1] =  20.0 + by;
        vx[2] = 300.0 + bx; vy[2] = 220.0 + by;
    }

    rngState = (unsigned long)(1UL + 7919UL * (unsigned long)pass);
    for (i = 0; i < 128; i++) order[i] = i;
    for (i = 127; i > 0; i--) {
        j = (int)(rnd() % (unsigned long)(i + 1));
        k = order[i]; order[i] = order[j]; order[j] = k;
    }

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
    glDisable(GL_POLYGON_OFFSET_FILL);
    glShadeModel(GL_FLAT);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepth(1.0);

    printf("# z2 mode %s pass %d  surface %dx%d\n", mode, pass, W, H);
    printf("# plane %.6g + %.6g x + %.6g y   grid x %d..%d y %d..%d step %d\n",
           C0, gx, GY, SX0, SX1, SY0, SY1, SSTEP);
    printf("# triangle %.6g %.6g  %.6g %.6g  %.6g %.6g  movex %d movey %d\n",
           vx[0], vy[0], vx[1], vy[1], vx[2], vy[2], movex, movey);
    printf("# order");
    for (i = 0; i < 128; i++) printf(" %d", order[i]);
    printf("\n");

    for (n = 0; n < 128; n++) {
        int t = order[n];
        double off = (double)t / 128.0;
        unsigned long d0, w0, s0, x0;

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        d0 = OSMGAMesaHookDrawn(); w0 = OSMGAMesaHookWarp();
        s0 = OSMGAMesaHookSoftware(); x0 = OSMGAMesaHookDeclined();

        glColor4ub(200, 100, 50, 255);
        glBegin(GL_TRIANGLES);
          for (i = 0; i < 3; i++) {
              double px = vx[i] + (movex ? off : 0.0);
              double py = vy[i] + (movey ? off : 0.0);
              /* The plane is FIXED in window space, as Z1's sweep a: the
               * depth is taken at the moved vertex, so translating the
               * triangle does not move the plane and the expected depth at
               * a fixed pixel is the same at every t.  Z1b already showed
               * the effect does not follow the submitted z, so there is
               * nothing left for a z-fixed variant to separate here. */
              double code = C0 + gx * px + GY * py;
              glVertex3d(px, py, 1.0 - code / 32767.5);
          }
        glEnd();
        glFinish();

        if (!OSMesaGetDepthBuffer(ctx, &dw, &dh, &bpv, &zvoid) || bpv != 2) {
            printf("# no 16-bit depth buffer\n"); return 2;
        }
        zb = (unsigned short *)zvoid;

        printf("T %d drawn %lu warp %lu soft %lu declined %lu\n", t,
               OSMGAMesaHookDrawn() - d0, OSMGAMesaHookWarp() - w0,
               OSMGAMesaHookSoftware() - s0, OSMGAMesaHookDeclined() - x0);
        for (y = SY0; y <= SY1; y += SSTEP)
            for (x = SX0; x <= SX1; x += SSTEP)
                printf("Z %d %ld %ld %u\n", t, x, y,
                       (unsigned)zb[y * dw + x]);
    }
    return 0;
}
