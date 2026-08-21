/*
 * openstep-mga-mesa-coverage-test.c -- what exactly do the two rasterisers
 * disagree about?
 *
 * The same clipped triangle came out 42472 pixels drawn by software and 42245
 * drawn by the engine, and that was written down as the known disagreement
 * between them.  An area cannot say that: the same number falls out of a
 * half-pixel edge rule, a hole in the middle, a crack along the seam where a
 * clipped polygon was split, and a result that depends on winding.
 *
 * The cause is not a mystery -- REMAINING_WORK 3-12 says the back end casts
 * window coordinates to long and drops the fraction while Mesa keeps it and
 * samples at pixel centres under the top-left rule.  So this exists to
 * confirm the difference is edge-only and consistent with that, and to rule
 * out everything else hiding inside the same number.
 *
 * It prints coverage as per-row runs and does no arithmetic on them.  Every
 * row is printed, including empty ones, or the runs cannot be reconstructed.
 *
 * Run it twice in separate processes -- OSMGA_MESA_ACCEL=0 and unset -- and
 * compare the two outputs.
 *
 *   cc -O -Wall -o /tmp/cov openstep-mga-mesa-coverage-test.c \
 *      -I<mesa>/include -L<built> -lGL_mga
 *   /tmp/cov <shape 1..7>
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

static unsigned long *app;

static void
tri(float ax, float ay, float bx, float by, float cx, float cy,
    int r, int g, int b)
{
    glBegin(GL_TRIANGLES);
      glColor3ub((GLubyte)r, (GLubyte)g, (GLubyte)b);
      glVertex3f(ax, ay, 0.0f);
      glVertex3f(bx, by, 0.0f);
      glVertex3f(cx, cy, 0.0f);
    glEnd();
}

/*
 * The shapes, and why each is here.
 *
 *   1  flat-bottomed: two vertices share a y, so the middle y is the bottom
 *      and the back end emits ONE trapezoid.  The only shape with no join of
 *      its own, and therefore the only one where a difference is purely the
 *      edge rule.
 *   2  three distinct y, so the back end splits at the middle one.  What it
 *      has that 1 does not is that join.
 *   3  the shape from 3-25, running far past the viewport so Mesa clips it
 *      and delivers several primitives.
 *   4  shape 1 with its winding reversed.  GL requires coverage not to depend
 *      on that, and it must hold exactly within each path, not approximately
 *      between them.
 *   5  two triangles sharing a diagonal, one colour: a crack shows as the
 *      clear colour along it.
 *   6, 7  the same two in one order and then the other, in different colours:
 *      an overlap shows as the diagonal changing colour with the order, which
 *      a single colour cannot reveal.
 */
static void
shape(int n)
{
    switch (n) {
    case 1: tri( 40, 40, 200, 40, 120,180, 0,255,0); break;
    case 2: tri( 40, 40, 200, 90, 120,180, 0,255,0); break;
    case 3: tri(120.5f,4.5f, 300.5f,4.5f, 120.5f,40000.0f, 0,255,0); break;
    case 4: tri(120,180, 200, 40,  40, 40, 0,255,0); break;
    case 5: tri( 40, 40, 200, 40, 200,180, 0,255,0);
            tri( 40, 40, 200,180,  40,180, 0,255,0); break;
    case 6: tri( 40, 40, 200, 40, 200,180, 0,255,0);
            tri( 40, 40, 200,180,  40,180, 255,0,0); break;
    case 7: tri( 40, 40, 200,180,  40,180, 255,0,0);
            tri( 40, 40, 200, 40, 200,180, 0,255,0); break;
    /*
     * Shape 1's vertices moved half a pixel, and nowhere near an edge of the
     * viewport, so nothing is clipped.  The hook truncates window coordinates
     * with a (long) cast, so what the back end receives is shape 1 exactly.
     *
     * That makes a prediction sharp enough to be wrong: the ENGINE's coverage
     * for this must equal the engine's coverage for shape 1, pixel for pixel,
     * while the software path keeps the fraction and draws something else.
     * If it holds, the difference that remains on the clipped shape is the
     * truncation and needs no other cause; if it does not, clipped input goes
     * wrong some further way and I have been explaining the wrong thing.
     */
    case 8: tri( 40.5f,40.5f, 200.5f,40.5f, 120.5f,180.5f, 0,255,0); break;
    default: break;
    }
}

int
main(int argc, char **argv)
{
    OSMesaContext ctx;
    int which = (argc > 1) ? atoi(argv[1]) : 1;
    long x, y;
    unsigned long d0, s0, u0, x0;

    if (which < 1 || which > 8) { printf("shape 1..8\n"); return 2; }

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
    /* Everything that could change coverage, turned off and said so. */
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DITHER);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_POLYGON_SMOOTH);
    glShadeModel(GL_FLAT);

    glClearColor(0x10/255.0f, 0x20/255.0f, 0x30/255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    d0 = OSMGAMesaHookDrawn();
    s0 = OSMGAMesaHookSoftware();
    u0 = OSMGAMesaHookUnsupported();
    x0 = OSMGAMesaHookDeclined();
    shape(which);
    glFinish();

    /* Provenance, so the runs below can be read a year from now. */
    printf("# shape %d  surface %dx%d  origin %lu  clear %08lx\n",
           which, W, H, OSMGAMesaBufferOrigin(), CLEARC);
    printf("# counters drawn=%lu software=%lu unsupported=%lu declined=%lu\n",
           OSMGAMesaHookDrawn() - d0, OSMGAMesaHookSoftware() - s0,
           OSMGAMesaHookUnsupported() - u0, OSMGAMesaHookDeclined() - x0);
    /*
     * Acceleration being on is not evidence the engine drew it: a refused
     * batch is deliberately redrawn in software.  The run says which it was
     * and the comparison can refuse to draw conclusions from a mixed one.
     */
    printf("# mode %s\n",
           (OSMGAMesaBufferOrigin() == 0UL) ? "software"
           : ((OSMGAMesaHookSoftware() - s0 != 0UL ||
               OSMGAMesaHookDeclined() - x0 != 0UL) ? "MIXED" : "hardware"));

    for (y = 0; y < H; y++) {
        const unsigned long *row = app + y * W;

        printf("R %ld", y);
        x = 0;
        while (x < W) {
            unsigned long c = row[x];
            long run = x;

            while (run < W && row[run] == c) run++;
            if (c != CLEARC)
                printf(" %06lx:%ld-%ld", c & 0xFFFFFFUL, x, run - 1);
            x = run;
        }
        printf("\n");
    }
    OSMesaDestroyContext(ctx);
    return 0;
}
