/*
 * M13 Z1 -- does WARP's depth start follow the vertex POSITION or the
 * submitted Z?
 *
 * M12 §9.4: on a shape translated by 37/128 of a pixel, WARP's fitted depth
 * plane keeps its gradients to six parts per million and puts its constant
 * 3.93 codes high.  Z0 has since shown the vertices this library SENDS
 * define the intended plane to three ten-thousandths of a code, so the
 * difference appears after they leave, and the kernel only copies them.
 *
 * The plan's own review found the flaw that makes a naive sweep useless:
 * translating the geometry under a FIXED window-space plane also changes
 * every vertex's submitted z, so a position-grid hypothesis and a
 * depth-value hypothesis move together and no amount of sawtooth separates
 * them.  Hence two sweeps:
 *
 *   a   plane fixed   z = f(translated vertex).  Position and submitted z
 *                     both move.  The expected depth at a fixed pixel is
 *                     the SAME at every t.
 *   b   z fixed       z = f(UNtranslated vertex).  The submitted z words
 *                     are identical at every t and the plane travels with
 *                     the geometry, so the expected depth at a fixed pixel
 *                     moves by exactly -(gx+gy)*t/128, a known amount the
 *                     analysis subtracts.
 *
 *      sawtooth in a and not in b   -> it follows the submitted z
 *      sawtooth in both             -> it follows the position
 *      flat in both                 -> the translation was never the cause
 *
 * What this does NOT do is decide.  It prints raw depth codes at a fixed
 * sample grid and python does the arithmetic, because a program that
 * judged its own numbers here would be fitting the hypothesis it was
 * written from.
 *
 * Everything the review listed as a way to measure the wrong thing:
 *
 *   ONE triangle, never a quad -- a region spanning a quad's own diagonal
 *     is two primitives and would mix them.
 *   The sample grid is fixed in SCREEN space and sits at least twenty-eight
 *     pixels inside every edge, so a translation under a pixel cannot
 *     change which pixels are read.
 *   GL_ALWAYS with depth writes on and a known clear, so nothing is
 *     rejected and the buffer holds this triangle alone.
 *   Polygon offset disabled outright: it adds a slope-dependent term to the
 *     very thing being measured.
 *   Codes stay inside [22010, 46935] over the whole triangle, far from
 *     saturation at either end.
 *   The t order is SHUFFLED, and the pass number reseeds it, so a warm-up
 *     or a command-buffer history cannot impersonate a ramp; the order is
 *     printed so a suspicious result can be replayed in it.
 *   Every t prints its own tier counters, so an arm that fell back for one
 *     translation cannot be averaged in silently.
 *
 *   /tmp/z1 <a|b> <pass>
 *
 *   cc -O -Wall -o /tmp/z1 openstep-mga-warp-depth-z1-probe.c \
 *      -I<mesa>/include -I<repo>/hw3d -L<built> -lGL_mga
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

/* The depth plane, in window codes, and seam's own calibration for turning
 * a window code back into the object depth that produces it. */
#define GX      60.37
#define GY      40.11
#define C0   20000.5
static double
zfor(double vx, double vy)
{
    return 1.0 - (C0 + GX * vx + GY * vy) / 32767.5;
}

/*
 * One triangle, big.  The hypotenuse runs (20,20) to (300,220), so the
 * sample grid below -- x 200..280, y 40..120 -- is at least twenty-eight
 * pixels inside it at the worst corner (at x = 200 the edge is at y = 148.6)
 * and a translation of at most 127/128 of a pixel cannot reach it.
 */
static const double TVX[3] = {  20.0, 300.0, 300.0 };
static const double TVY[3] = {  20.0,  20.0, 220.0 };

#define SX0 200
#define SX1 280
#define SY0  40
#define SY1 120
#define SSTEP 4

/* A shuffle that does not depend on the C library's, so the order is the
 * same wherever this is replayed.  Park-Miller, then Fisher-Yates. */
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
    const char *sweep = (argc > 1) ? argv[1] : "a";
    int         pass  = (argc > 2) ? atoi(argv[2]) : 1;
    OSMesaContext ctx;
    unsigned long *app;
    unsigned short *zb;
    GLint dw, dh, bpv;
    void *zvoid;
    int order[128], i, j, k, n;
    long x, y;

    if (sweep[0] != 'a' && sweep[0] != 'b') {
        printf("sweep is a (plane fixed) or b (z fixed)\n"); return 2;
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

    printf("# z1 sweep %s pass %d  surface %dx%d\n", sweep, pass, W, H);
    printf("# plane %.6g + %.6g x + %.6g y   grid x %d..%d y %d..%d step %d\n",
           C0, GX, GY, SX0, SX1, SY0, SY1, SSTEP);
    printf("# triangle %.6g %.6g  %.6g %.6g  %.6g %.6g\n",
           TVX[0], TVY[0], TVX[1], TVY[1], TVX[2], TVY[2]);
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
              double px = TVX[i] + off;
              double py = TVY[i] + off;
              /* sweep a takes the depth from the plane at the MOVED vertex,
               * so the window-space plane is invariant; sweep b takes it at
               * the vertex's home, so the submitted z never changes and the
               * plane travels with the triangle. */
              double oz = (sweep[0] == 'a') ? zfor(px, py)
                                            : zfor(TVX[i], TVY[i]);
              glVertex3d(px, py, oz);
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
