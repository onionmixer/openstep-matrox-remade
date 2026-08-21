/*
 * openstep-mga-mesa-depth-anchor-test.c -- where is the depth plane read?
 *
 * Colour and alpha turned out to be read at the pixel's corner where OpenGL
 * wants its centre, and to lose their low bits to a truncating shift.  Depth
 * was deliberately left alone: half a pixel of depth is many 16-bit codes on
 * a steep polygon and it flips comparisons at ties.  This measures it.
 *
 * Depth has a cause colour did not.  A colour vertex value is the byte the
 * caller passed; a depth vertex value is a transformed window coordinate with
 * a fraction, and the hook casts it to an integer on the way in -- on the
 * same line that casts x and y.  So the vertices here are chosen with
 * INTEGRAL window x and y, which makes that part of the cast a no-op and
 * stops it changing the plane underneath the question being asked.
 *
 * Two modes:
 *
 *   calib   flat-depth triangles at many object depths, one depth code read
 *           back for each.  The object-to-window mapping is MEASURED rather
 *           than derived, because deriving it would put the whole conclusion
 *           on the derivation.  The projection is orthographic on purpose:
 *           window depth is affine in NDC depth, not in eye depth, so a
 *           perspective projection would make this calibration meaningless.
 *
 *   slope N shape N with a depth gradient, every covered pixel dumped.
 *
 *   cc -O -Wall -o /tmp/dep openstep-mga-mesa-depth-anchor-test.c \
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
extern unsigned long OSMGAMesaBufferDepthOrigin(void);

#define W       320
#define H       240

/* Shapes: integral window x and y, and they split at the middle vertex.
 * The depth values are filled in from the calibration. */
static const long sx[2][3] = { { 190, 198, 150 }, { 108,  89,  47 } };
static const long sy[2][3] = { {  53,  61, 189 }, {  86, 105, 220 } };

static unsigned long *app;
static OSMesaContext ctx;

static int
depthAt(long x, long y, unsigned long *out)
{
    void *zb; GLint dw, dh, bpv;

    if (!OSMesaGetDepthBuffer(ctx, &dw, &dh, &bpv, &zb) || !zb || bpv != 2)
        return 0;
    *out = (unsigned long)((unsigned short *)zb)[y * dw + x];
    return 1;
}

static void
setup(void)
{
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0.0, (double)W, 0.0, (double)H, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DITHER);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_POLYGON_SMOOTH);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glShadeModel(GL_FLAT);
    /*
     * Depth writes on, and nothing may be rejected, or the depth value would
     * move which fragments are written and the coverage check would be
     * measuring depth instead of coverage.
     *
     * GL_ALWAYS was the obvious way to get that and it is the wrong one: the
     * back end's chooser accepts GL_LESS and nothing else, so GL_ALWAYS turns
     * the acceleration off altogether and the run silently becomes software.
     * Measured -- the first attempt reported drawn=0 with acceleration on.
     *
     * GL_LESS against a buffer cleared to the far value rejects nothing
     * either, as long as no fragment is AT the far value; the analysis
     * asserts that rather than assuming it.
     */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
}

static void
triangle(const long *vx, const long *vy, const double *vz)
{
    int i;

    glBegin(GL_TRIANGLES);
      glColor4ub(200, 200, 200, 255);
      for (i = 0; i < 3; i++)
          glVertex3d((double)vx[i], (double)vy[i], vz[i]);
    glEnd();
}

int
main(int argc, char **argv)
{
    const char *mode = (argc > 1) ? argv[1] : "calib";
    int sh = (argc > 2) ? (atoi(argv[2]) - 1) : 0;
    long i;
    unsigned long d0, s0, u0, x0;

    app = (unsigned long *)malloc((unsigned)(W * H) * sizeof(unsigned long));
    if (!app) { printf("no room\n"); return 2; }
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, app, GL_UNSIGNED_BYTE, W, H)) {
        printf("no context\n"); return 2;
    }
    setup();

    {   /* the comparison only means anything if both paths share the buffer */
        void *zb; GLint dw, dh, bpv;

        if (!OSMesaGetDepthBuffer(ctx, &dw, &dh, &bpv, &zb) || bpv != 2) {
            printf("# no 16-bit depth buffer\n"); return 2;
        }
        printf("# depth buffer %dx%d, %d bytes per value, shared origin %lu\n",
               dw, dh, bpv, OSMGAMesaBufferDepthOrigin());
    }

    if (strcmp(mode, "calib") == 0) {
        /*
         * Flat triangles at many depths.  A flat primitive only reveals the
         * quantisation bin its value fell in, so the grid is fine and wide
         * rather than a handful of points: a mapping that is not affine
         * cannot hide from 401 samples the way it can from five.
         */
        long n = 401;

        printf("# calibration: object z -> depth code, orthographic\n");
        for (i = 0; i < n; i++) {
            double z = -1.0 + 2.0 * (double)i / (double)(n - 1);
            double vz[3];
            unsigned long code;
            long px = -1, py = -1, xx, yy;

            vz[0] = vz[1] = vz[2] = z;
            glClearDepth(1.0);
            glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
            triangle(sx[0], sy[0], vz);
            glFinish();
            /*
             * Read where the triangle actually painted, not where it was
             * assumed to.  The first attempt read a fixed pixel outside this
             * shape and got the clear value 401 times over, which looks like
             * a mapping and is not one.
             */
            for (yy = 0; yy < H && px < 0; yy++)
                for (xx = 0; xx < W; xx++)
                    if ((app[yy * W + xx] & 0xFFFFFFUL) != 0UL) {
                        px = xx; py = yy; break;
                    }
            if (px < 0) { printf("nothing drawn\n"); return 2; }
            if (!depthAt(px, py, &code)) { printf("no depth\n"); return 2; }
            printf("C %.10f %lu %ld %ld\n", z, code, px, py);
        }
        OSMesaDestroyContext(ctx);
        return 0;
    }

    if (strcmp(mode, "slope") == 0) {
        double vz[3];
        long x, y;

        if (sh < 0 || sh > 1) { printf("shape 1 or 2\n"); return 2; }
        /* A depth gradient steep enough that half a pixel is many codes. */
        vz[0] = -0.75; vz[1] = 0.55; vz[2] = 0.10;

        glClearDepth(1.0);
        glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
        d0 = OSMGAMesaHookDrawn();  s0 = OSMGAMesaHookSoftware();
        u0 = OSMGAMesaHookUnsupported(); x0 = OSMGAMesaHookDeclined();
        triangle(sx[sh], sy[sh], vz);
        glFinish();

        printf("# shape %d\n", sh + 1);
        for (i = 0; i < 3; i++)
            printf("# vertex %ld  %ld %ld  objz %.10f\n",
                   i, sx[sh][i], sy[sh][i], vz[i]);
        printf("# counters drawn=%lu software=%lu unsupported=%lu declined=%lu\n",
               OSMGAMesaHookDrawn() - d0, OSMGAMesaHookSoftware() - s0,
               OSMGAMesaHookUnsupported() - u0, OSMGAMesaHookDeclined() - x0);
        printf("# mode %s\n",
               (OSMGAMesaBufferOrigin() == 0UL) ? "software"
               : ((OSMGAMesaHookSoftware() - s0 != 0UL ||
                   OSMGAMesaHookDeclined() - x0 != 0UL) ? "MIXED" : "hardware"));
        /* Colour marks coverage; depth is what is being measured. */
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                unsigned long code;

                if ((app[y * W + x] & 0xFFFFFFUL) == 0UL) continue;
                if (!depthAt(x, y, &code)) return 2;
                printf("P %ld %ld %lu\n", x, y, code);
            }
        OSMesaDestroyContext(ctx);
        return 0;
    }
    printf("mode is calib or slope\n");
    return 2;
}
