/*
 * Triangles narrower than a pixel.
 *
 * The builder decides which edge is which from a cross product taken in WHOLE
 * pixels, because at 1/256 the product overflows a signed long.  When that
 * coarse value comes out zero the triangle is reported as having no area and
 * the hook draws nothing -- and unlike the "cannot express this" answer, this
 * one does NOT go to the software path.
 *
 * A sliver less than a pixel wide still covers sample points.  This measures
 * how many, by drawing the same slivers on both paths and counting.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/osmesa.h>

extern unsigned long OSMGAMesaHookDrawn(void);
extern unsigned long OSMGAMesaHookSoftware(void);
extern unsigned long OSMGAMesaHookUnsupported(void);

#define W  320
#define H  240
#define CLEARC 0xFF102030UL

static unsigned long *app;

/* a sliver: width w pixels, height h, at (x0,y0); rev swaps the winding */
static void
sliver(double x0, double y0, double w, double h, int rev)
{
    glBegin(GL_TRIANGLES);
      glColor4ub(0, 255, 0, 255);
      if (rev) {
          glVertex2d(x0 + w/2., y0 + h);
          glVertex2d(x0 + w,    y0);
          glVertex2d(x0,        y0);
      } else {
          glVertex2d(x0,        y0);
          glVertex2d(x0 + w,    y0);
          glVertex2d(x0 + w/2., y0 + h);
      }
    glEnd();
}

/*
 * Does this machine's floating point keep the sign of a determinant that
 * nearly cancels?
 *
 * The change this test guards rests on a double holding integers up to 2^53
 * exactly.  An x87 whose precision control has been set to 24 bits would
 * round the products to a float and a hostile cancellation would come out
 * zero -- the sign would be lost before the subtraction, and every triangle
 * near the coordinate limit would be classified by a coin toss.  The values
 * are the corners of the permitted range.
 */
static int
fpuKeepsTheSign(void)
{
    const double K = 2097152.0;             /* 8192 pixels in 1/256 units */
    double tx = -K,      ty = -K;
    double mx =  K - 2.0, my = K - 1.0;
    double lx =  K - 1.0, ly = K;
    double d  = (lx - tx) * (my - ty) - (ly - ty) * (mx - tx);

    printf("# cancellation probe: determinant %.1f (want 1.0)\n", d);
    return d == 1.0;
}

int
main(int argc, char **argv)
{
    OSMesaContext ctx;
    long x, y, painted;
    int i;
    static const double widths[6] = { 0.25, 0.5, 0.75, 0.9, 1.5, 4.0 };
    unsigned long d0, s0, u0;

    (void)argc; (void)argv;
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

    if (!fpuKeepsTheSign()) {
        printf("# FAIL the sign does not survive here -- the rest is moot\n");
        return 1;
    }

    printf("# width winding painted drawn software unsupported\n");
    for (i = 0; i < 12; i++) {
        glClearColor(0x10/255.0f, 0x20/255.0f, 0x30/255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        d0 = OSMGAMesaHookDrawn(); s0 = OSMGAMesaHookSoftware();
        u0 = OSMGAMesaHookUnsupported();
        /* tall enough that a sample must fall inside whatever the width */
        sliver(40.3, 40.0, widths[i % 6], 60.0, i >= 6);
        glFinish();
        painted = 0;
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++)
                if (app[y * W + x] != CLEARC) painted++;
        printf("%.2f %s %ld %lu %lu %lu\n", widths[i % 6],
               (i >= 6) ? "rev" : "fwd", painted,
               OSMGAMesaHookDrawn() - d0, OSMGAMesaHookSoftware() - s0,
               OSMGAMesaHookUnsupported() - u0);
    }
    return 0;
}
