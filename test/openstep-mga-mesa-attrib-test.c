/*
 * openstep-mga-mesa-attrib-test.c -- where is the interpolated colour
 * actually evaluated?
 *
 * The coverage round made the engine's coverage match OpenGL exactly, but
 * every shape it used was flat shaded, so the interpolation planes had no
 * gradient and an anchoring error could not have shown -- and the trapezoid's
 * start column moved in that same rewrite.
 *
 * The back end reads each plane's start value at (left, y), both integers,
 * which is the pixel's top-left CORNER.  Mesa evaluates at the pixel CENTRE:
 * tritemp.h shifts x by +1/2 and y by -1/2, ceilings to the scanline, and adds
 * the sub-pixel deltas, which works out to (ix + 1/2, iy + 1/2).  So the
 * prediction is a constant offset of -(dx + dy)/2, across the whole triangle,
 * rather than a per-pixel error.
 *
 * The shape is chosen so that three channels have to answer three different
 * ways, which is what separates an anchoring error from a plain colour bias:
 *
 *   red    (dx+dy)/2 = +12.9375
 *   green  (dx+dy)/2 = -12.9375     the opposite sign
 *   blue   (dx+dy)/2 =   0          exactly, by construction
 *
 * Blue is a linear function of (x - y), so its two gradients are equal and
 * opposite identically and anchoring CANNOT move it.  A constant bias would
 * move all three the same way.  The three together say which it is.
 *
 * The vertex colours are held well away from 0 and 255 as well: the start is
 * clamped to that range before scaling, the trapezoid corner can sit outside
 * the triangle, and a clamp firing there would look exactly like an anchoring
 * error.  Measured: with colours at the ends it does fire.  Here the nearest
 * approach is seventeen levels.
 *
 * Every covered pixel is printed -- there are only 704 of them, so there is
 * no reason to sample, and sampling would have hidden edge damage and a
 * per-row reset.
 *
 *   cc -O -Wall -o /tmp/att openstep-mga-mesa-attrib-test.c \
 *      -I<mesa>/include -L<built> -lGL_mga
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/osmesa.h>

extern unsigned long OSMGAMesaHookDrawn(void);
extern unsigned long OSMGAMesaHookDeclined(void);
extern unsigned long OSMGAMesaHookSoftware(void);
extern unsigned long OSMGAMesaHookUnsupported(void);
extern unsigned long OSMGAMesaBufferOrigin(void);

#define W       320
#define H       240
#define CLEARC  0xFF102030UL

static const long vx[3] = { 190, 198, 150 };
static const long vy[3] = {  53,  61, 189 };
static const int  vr[3] = {  24, 231, 127 };
static const int  vg[3] = { 231,  24, 127 };
static const int  vb[3] = { 206, 206,  30 };

int
main(void)
{
    OSMesaContext ctx;
    unsigned long *app;
    long x, y, i;
    unsigned long d0, s0, u0, x0;

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
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DITHER);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_POLYGON_SMOOTH);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glShadeModel(GL_SMOOTH);            /* the whole point */

    glClearColor(0x10/255.0f, 0x20/255.0f, 0x30/255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    d0 = OSMGAMesaHookDrawn();
    s0 = OSMGAMesaHookSoftware();
    u0 = OSMGAMesaHookUnsupported();
    x0 = OSMGAMesaHookDeclined();

    glBegin(GL_TRIANGLES);
      for (i = 0; i < 3; i++) {
          glColor4ub((GLubyte)vr[i], (GLubyte)vg[i], (GLubyte)vb[i], 255);
          glVertex3f((float)vx[i], (float)vy[i], 0.0f);
      }
    glEnd();
    glFinish();

    printf("# surface %dx%d  origin %lu  clear %08lx\n",
           W, H, OSMGAMesaBufferOrigin(), CLEARC);
    for (i = 0; i < 3; i++)
        printf("# vertex %ld  (%ld,%ld)  rgb %d %d %d\n",
               i, vx[i], vy[i], vr[i], vg[i], vb[i]);
    printf("# counters drawn=%lu software=%lu unsupported=%lu declined=%lu\n",
           OSMGAMesaHookDrawn() - d0, OSMGAMesaHookSoftware() - s0,
           OSMGAMesaHookUnsupported() - u0, OSMGAMesaHookDeclined() - x0);
    printf("# mode %s\n",
           (OSMGAMesaBufferOrigin() == 0UL) ? "software"
           : ((OSMGAMesaHookSoftware() - s0 != 0UL ||
               OSMGAMesaHookDeclined() - x0 != 0UL) ? "MIXED" : "hardware"));

    /* Every covered pixel, in full, with nothing decided here. */
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            unsigned long c = app[y * W + x];

            if (c == CLEARC) continue;
            printf("P %ld %ld %lu %lu %lu %lu\n", x, y,
                   (c >> 16) & 0xFFUL, (c >> 8) & 0xFFUL, c & 0xFFUL,
                   (c >> 24) & 0xFFUL);
        }
    OSMesaDestroyContext(ctx);
    return 0;
}
