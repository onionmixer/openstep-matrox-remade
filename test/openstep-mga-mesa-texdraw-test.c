/*
 * Texturing through GL, at last.
 *
 * Everything before this called the back end directly.  This draws with
 * glTexImage2D and glTexCoord2f and asks whether the chooser took it, whether
 * the picture is right, and whether each condition it must refuse is refused
 * for its own reason rather than by accident.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/osmesa.h>
#include <GL/gl.h>

extern unsigned long OSMGAMesaHookDrawn(void);
extern unsigned long OSMGAMesaHookSoftware(void);
extern unsigned long OSMGAMesaHookTexPersp(void);
extern unsigned long OSMGAMesaHookTexAbsent(void);
extern unsigned long OSMGAMesaTexUploads(void);
extern unsigned long OSMGAMesaHookBatches(void);
extern unsigned long OSMGAMesaHookUnsupported(void);
extern unsigned long OSMGAMesaHookDeclined(void);
extern unsigned long OSMGAMesaHookVerdictCount(unsigned long v);

#define W 320
#define H 240
#define TD 16
#define CLEARC 0xFF102030UL

static unsigned long *app;
static int failures;

static void
say(const char *what, int ok, const char *detail)
{
    printf("  %-46s %s%s%s\n", what, ok ? "ok" : "FAIL",
           detail ? "  " : "", detail ? detail : "");
    if (!ok) failures++;
}

/* a texture whose texels name themselves, all three channels different */
static void
maketex(GLuint *id)
{
    static GLubyte px[TD * TD * 3];
    int x, y;

    for (y = 0; y < TD; y++)
        for (x = 0; x < TD; x++) {
            px[(y * TD + x) * 3 + 0] = (GLubyte)(x * 16 + 8);
            px[(y * TD + x) * 3 + 1] = (GLubyte)(y * 16 + 4);
            px[(y * TD + x) * 3 + 2] = (GLubyte)(x + y);
        }
    glGenTextures(1, id);
    glBindTexture(GL_TEXTURE_2D, *id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TD, TD, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, px);
}

/*
 * The same texels with an alpha of their own, and an alpha that is nobody
 * else's value: not 0, not 255, and not the vertex alpha the quad is drawn
 * with, so taking the wrong operand cannot look right by accident.
 */
static void
maketexRGBA(GLuint *id)
{
    static GLubyte px[TD * TD * 4];
    int x, y;

    for (y = 0; y < TD; y++)
        for (x = 0; x < TD; x++) {
            px[(y * TD + x) * 4 + 0] = (GLubyte)(x * 16 + 8);
            px[(y * TD + x) * 4 + 1] = (GLubyte)(y * 16 + 4);
            px[(y * TD + x) * 4 + 2] = (GLubyte)(x + y);
            px[(y * TD + x) * 4 + 3] = (GLubyte)(x * 8 + y * 3 + 17);
        }
    glGenTextures(1, id);
    glBindTexture(GL_TEXTURE_2D, *id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TD, TD, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, px);
}

static void
quad(double x0, double y0, double x1, double y1)
{
    glBegin(GL_TRIANGLES);
      glTexCoord2f(0.0f, 0.0f); glVertex2d(x0, y0);
      glTexCoord2f(1.0f, 0.0f); glVertex2d(x1, y0);
      glTexCoord2f(1.0f, 1.0f); glVertex2d(x1, y1);
      glTexCoord2f(0.0f, 0.0f); glVertex2d(x0, y0);
      glTexCoord2f(1.0f, 1.0f); glVertex2d(x1, y1);
      glTexCoord2f(0.0f, 1.0f); glVertex2d(x0, y1);
    glEnd();
}

static long
painted(void)
{
    long n = 0, i;

    for (i = 0; i < (long)W * (long)H; i++)
        if (app[i] != CLEARC) n++;
    return n;
}

/*
 * With an argument, draw one scene and print every drawn pixel instead of
 * running the checks.  Two runs -- accelerated and not -- then say how the two
 * paths differ, which is where the engine's constant first becomes a number.
 */
static int dumpMode;

int
main(int argc, char **argv)
{
    OSMesaContext ctx;
    GLuint tex;
    unsigned long d0, s0, p0, a0;
    long drew;

    dumpMode = (argc > 1);
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
    glDisable(GL_CULL_FACE); glDisable(GL_ALPHA_TEST);
    glShadeModel(GL_FLAT);
    glClearColor(0x10/255.0f, 0x20/255.0f, 0x30/255.0f, 1.0f);

    maketex(&tex);
    glEnable(GL_TEXTURE_2D);

    if (dumpMode) {
        long x, y;

        /*
         * A second argument asks for the linear filter, which the gate now
         * takes -- with CLAMP_TO_EDGE, since under a linear filter GL_CLAMP
         * blends a border colour that the engine's clamp does not have.
         */
        /*
         * "rgba" swaps in a texture that has an alpha of its own and draws it
         * with a vertex alpha that is a different value again, so a wrong
         * operand shows on every pixel.
         */
        if (argc > 2 && strcmp(argv[2], "rgba") == 0) {
            GLuint rt;

            maketexRGBA(&rt);
            glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
        }
        /*
         * "mod" is GL_MODULATE with ONE vertex colour, so the colour
         * interpolator contributes nothing and any difference is the
         * product alone.  "modg" gives the quad a colour that varies, which
         * puts the interpolator back in and lets the two be told apart.
         */
        /*
         * "persp" and "perspd" are the same receding quad split along
         * opposite diagonals.  A projective mapping gives the same picture
         * either way; an affine one kinks at the diagonal, so the two dumps
         * differing is the failure and their agreeing is the result.
         */
        if (argc > 2 && strncmp(argv[2], "persp", 5) == 0) {
            int flip = (strcmp(argv[2], "perspd") == 0);
            GLfloat s0 = 0.0f, s1 = 1.0f;
            double zfar = (strcmp(argv[2], "perspfar") == 0) ? -24.0 : -6.0;

            /*
             * A third argument turns on one more thing at a time, so that a
             * combination that the perspective path cannot take shows up as
             * a fallback rather than as a wrong picture nobody looked at.
             */
            if (argc > 3) {
                if (strchr(argv[3], 'l') != 0) {
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                                    GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                                    GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                    GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                    GL_LINEAR);
                }
                if (strchr(argv[3], 'm') != 0)
                    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,
                              GL_MODULATE);
                if (strchr(argv[3], 'r') != 0) {
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                                    GL_REPEAT);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                                    GL_REPEAT);
                }
            }
            /*
             * "i" insets the texture coordinates a little.  A coordinate of
             * exactly nought at an edge is the one the validator refuses if
             * rounding takes the numerator a unit below it, so if the inset
             * makes a fallback go away that is what it was.
             */
            if (argc > 3 && strchr(argv[3], 'i') != 0) {
                s0 = 0.02f; s1 = 0.98f;
            }
            glClear(GL_COLOR_BUFFER_BIT);
            glMatrixMode(GL_PROJECTION); glLoadIdentity();
            glFrustum(-1.0, 1.0, -0.75, 0.75, 1.0, 100.0);
            glMatrixMode(GL_MODELVIEW); glLoadIdentity();
            glBegin(GL_TRIANGLES);
            if (!flip) {
                glTexCoord2f(s0, s0); glVertex3d(-1.0, -0.8, -1.5);
                glTexCoord2f(1.0f, 0.0f); glVertex3d( 1.0, -0.8, -1.5);
                glTexCoord2f(1.0f, 1.0f); glVertex3d( 1.0,  0.8, zfar);
                glTexCoord2f(s0, s0); glVertex3d(-1.0, -0.8, -1.5);
                glTexCoord2f(1.0f, 1.0f); glVertex3d( 1.0,  0.8, zfar);
                glTexCoord2f(0.0f, 1.0f); glVertex3d(-1.0,  0.8, zfar);
            } else {
                glTexCoord2f(s0, s0); glVertex3d(-1.0, -0.8, -1.5);
                glTexCoord2f(1.0f, 0.0f); glVertex3d( 1.0, -0.8, -1.5);
                glTexCoord2f(0.0f, 1.0f); glVertex3d(-1.0,  0.8, zfar);
                glTexCoord2f(1.0f, 0.0f); glVertex3d( 1.0, -0.8, -1.5);
                glTexCoord2f(1.0f, 1.0f); glVertex3d( 1.0,  0.8, zfar);
                glTexCoord2f(0.0f, 1.0f); glVertex3d(-1.0,  0.8, zfar);
            }
            glEnd();
            glFinish();
            fprintf(stderr,
                    "# persp: drawn %lu software %lu persp %lu"
                    " unsupported %lu declined %lu\n",
                    OSMGAMesaHookDrawn(), OSMGAMesaHookSoftware(),
                    OSMGAMesaHookTexPersp(),
                    OSMGAMesaHookUnsupported(), OSMGAMesaHookDeclined());
            {
                unsigned long q;

                for (q = 0UL; q < 24UL; q++)
                    if (OSMGAMesaHookVerdictCount(q) != 0UL)
                        fprintf(stderr, "#   verdict %lu x%lu\n", q,
                                OSMGAMesaHookVerdictCount(q));
            }
            for (y = 0; y < H; y++)
                for (x = 0; x < W; x++)
                    if (app[y * W + x] != CLEARC)
                        printf("P %ld %ld %lu\n", x, y, app[y * W + x]);
            return 0;
        }
        /* "tile" runs the quad's texture coordinates out to three, which
         * only means anything with repeat */
        if (argc > 2 && strcmp(argv[2], "minlin") == 0) {
            /*
             * A LINEAR texture that is MINIFIED: four textures across
             * thirty-two pixels, so two texels to a pixel.
             *
             * The chooser requires MinFilter and MagFilter to be equal, on
             * the grounds that the engine has one filter switch -- but the
             * engine has TWO, a MIN field and a MAG field in TEXFILTER, and
             * the encoder writes only the MAG one.  So a GL_LINEAR texture
             * that magnifies gets bilinear and one that minifies gets point
             * sampling, which is not what GL asks for.  Every scene here so
             * far magnifies, so nothing has ever asked the question.
             */
            glClear(GL_COLOR_BUFFER_BIT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glBegin(GL_TRIANGLES);
              glTexCoord2f(0.0f, 0.0f); glVertex2d(40.0, 40.0);
              glTexCoord2f(4.0f, 0.0f); glVertex2d(72.0, 40.0);
              glTexCoord2f(4.0f, 4.0f); glVertex2d(72.0, 72.0);
              glTexCoord2f(0.0f, 0.0f); glVertex2d(40.0, 40.0);
              glTexCoord2f(4.0f, 4.0f); glVertex2d(72.0, 72.0);
              glTexCoord2f(0.0f, 4.0f); glVertex2d(40.0, 72.0);
            glEnd();
            glFinish();
            fprintf(stderr,
                    "# minlin: drawn %lu software %lu unsupported %lu"
                    " declined %lu\n",
                    OSMGAMesaHookDrawn(), OSMGAMesaHookSoftware(),
                    OSMGAMesaHookUnsupported(), OSMGAMesaHookDeclined());
            for (y = 0; y < H; y++)
                for (x = 0; x < W; x++)
                    if (app[y * W + x] != CLEARC)
                        printf("P %ld %ld %lu\n", x, y, app[y * W + x]);
            return 0;
        }
        if (argc > 2 && strcmp(argv[2], "bandseam") == 0) {
            /*
             * Two quads side by side, sharing an edge, with continuous
             * texture coordinates -- but each is its own submission, and
             * their coordinate maxima fall either side of 2^20, so the kernel
             * gives them different biases: 496 on the left, 480 on the right.
             *
             * That is the seam the bias rule can produce.  Whether it shows
             * depends on where the samples sit inside a texel: the two
             * residuals differ by sixteen units, and with four samples to a
             * texel the nearest sample is a whole eighth of a texel from a
             * boundary, so python says nothing should differ here.  The test
             * exists to find out whether that is true rather than to assume
             * it -- a difference at the join would mean the seam bites at
             * ordinary tiling rates after all.
             */
            glClear(GL_COLOR_BUFFER_BIT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            (argc > 3) ? GL_LINEAR : GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                            (argc > 3) ? GL_LINEAR : GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glBegin(GL_TRIANGLES);
              glTexCoord2f(0.0f, 0.0f); glVertex2d(40.0, 40.0);
              glTexCoord2f(1.0f, 0.0f); glVertex2d(104.0, 40.0);
              glTexCoord2f(1.0f, 1.0f); glVertex2d(104.0, 104.0);
              glTexCoord2f(0.0f, 0.0f); glVertex2d(40.0, 40.0);
              glTexCoord2f(1.0f, 1.0f); glVertex2d(104.0, 104.0);
              glTexCoord2f(0.0f, 1.0f); glVertex2d(40.0, 104.0);
            glEnd();
            glBegin(GL_TRIANGLES);
              glTexCoord2f(1.0f, 0.0f); glVertex2d(104.0, 40.0);
              glTexCoord2f(2.0f, 0.0f); glVertex2d(168.0, 40.0);
              glTexCoord2f(2.0f, 1.0f); glVertex2d(168.0, 104.0);
              glTexCoord2f(1.0f, 0.0f); glVertex2d(104.0, 40.0);
              glTexCoord2f(2.0f, 1.0f); glVertex2d(168.0, 104.0);
              glTexCoord2f(1.0f, 1.0f); glVertex2d(104.0, 104.0);
            glEnd();
            glFinish();
            fprintf(stderr,
                    "# bandseam: drawn %lu software %lu unsupported %lu"
                    " declined %lu\n",
                    OSMGAMesaHookDrawn(), OSMGAMesaHookSoftware(),
                    OSMGAMesaHookUnsupported(), OSMGAMesaHookDeclined());
            for (y = 0; y < H; y++)
                for (x = 0; x < W; x++)
                    if (app[y * W + x] != CLEARC)
                        printf("P %ld %ld %lu\n", x, y, app[y * W + x]);
            return 0;
        }
        if (argc > 2 && strcmp(argv[2], "tilebnd") == 0) {
            /*
             * Seven textures across fifty-six pixels is exactly two texels
             * per pixel, so every sample -- taken at a pixel centre -- lands
             * exactly ON a texel boundary.  That is the position the encoder
             * was built to keep exact, and the position where a coordinate
             * that lands even one unit low reads the texel before.
             *
             * The first eight columns stay under 2^20, where the addend the
             * engine puts back is at least the 496 the encoder takes off.
             * Past that the ladder keeps stepping -- probe section 56
             * measured 480, 448 and 384 in the three bands above -- so the
             * encoder takes off MORE than is put back and the coordinate
             * lands low.  python predicts columns 8 to 55 read one texel
             * below what GL asks for, and the first eight agree.
             */
            glClear(GL_COLOR_BUFFER_BIT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glBegin(GL_TRIANGLES);
              glTexCoord2f(0.0f, 0.0f); glVertex2d(40.0, 40.0);
              glTexCoord2f(7.0f, 0.0f); glVertex2d(96.0, 40.0);
              glTexCoord2f(7.0f, 0.5f); glVertex2d(96.0, 72.0);
              glTexCoord2f(0.0f, 0.0f); glVertex2d(40.0, 40.0);
              glTexCoord2f(7.0f, 0.5f); glVertex2d(96.0, 72.0);
              glTexCoord2f(0.0f, 0.5f); glVertex2d(40.0, 72.0);
            glEnd();
            glFinish();
            fprintf(stderr,
                    "# tilebnd: drawn %lu software %lu persp %lu"
                    " unsupported %lu declined %lu\n",
                    OSMGAMesaHookDrawn(), OSMGAMesaHookSoftware(),
                    OSMGAMesaHookTexPersp(),
                    OSMGAMesaHookUnsupported(), OSMGAMesaHookDeclined());
            for (y = 0; y < H; y++)
                for (x = 0; x < W; x++)
                    if (app[y * W + x] != CLEARC)
                        printf("P %ld %ld %lu\n", x, y, app[y * W + x]);
            return 0;
        }
        if (argc > 2 && strcmp(argv[2], "tileneg") == 0) {
            /*
             * The same tiling as "tile", moved one whole texture down and
             * left so the coordinates run from -1 to 2 instead of 0 to 3.
             * GL treats the two identically under GL_REPEAT -- the picture
             * should be the same tiling -- so anything that falls back here
             * and not in "tile" is the reach being lopsided about the sign,
             * not the hardware being unable.
             */
            glClear(GL_COLOR_BUFFER_BIT);
            if (argc > 3) {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                GL_LINEAR);
            }
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glBegin(GL_TRIANGLES);
              glTexCoord2f(-1.0f, -1.0f); glVertex2d(40.0, 40.0);
              glTexCoord2f( 2.0f, -1.0f); glVertex2d(168.0, 40.0);
              glTexCoord2f( 2.0f,  2.0f); glVertex2d(168.0, 168.0);
              glTexCoord2f(-1.0f, -1.0f); glVertex2d(40.0, 40.0);
              glTexCoord2f( 2.0f,  2.0f); glVertex2d(168.0, 168.0);
              glTexCoord2f(-1.0f,  2.0f); glVertex2d(40.0, 168.0);
            glEnd();
            glFinish();
            fprintf(stderr,
                    "# tileneg: drawn %lu software %lu persp %lu"
                    " unsupported %lu declined %lu\n",
                    OSMGAMesaHookDrawn(), OSMGAMesaHookSoftware(),
                    OSMGAMesaHookTexPersp(),
                    OSMGAMesaHookUnsupported(), OSMGAMesaHookDeclined());
            {
                unsigned long qv;

                for (qv = 0UL; qv < 24UL; qv++)
                    if (OSMGAMesaHookVerdictCount(qv) != 0UL)
                        fprintf(stderr, "#   verdict %lu x%lu\n", qv,
                                OSMGAMesaHookVerdictCount(qv));
            }
            for (y = 0; y < H; y++)
                for (x = 0; x < W; x++)
                    if (app[y * W + x] != CLEARC)
                        printf("P %ld %ld %lu\n", x, y, app[y * W + x]);
            return 0;
        }
        if (argc > 2 && strcmp(argv[2], "tile") == 0) {
            /* this path returns before the clear further down, so it has to
             * do its own -- without it the dump is uninitialised memory and
             * the comparison is of two different piles of rubbish */
            glClear(GL_COLOR_BUFFER_BIT);
            if (argc > 3) {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                GL_LINEAR);
            }
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glBegin(GL_TRIANGLES);
              glTexCoord2f(0.0f, 0.0f); glVertex2d(40.0, 40.0);
              glTexCoord2f(3.0f, 0.0f); glVertex2d(168.0, 40.0);
              glTexCoord2f(3.0f, 3.0f); glVertex2d(168.0, 168.0);
              glTexCoord2f(0.0f, 0.0f); glVertex2d(40.0, 40.0);
              glTexCoord2f(3.0f, 3.0f); glVertex2d(168.0, 168.0);
              glTexCoord2f(0.0f, 3.0f); glVertex2d(40.0, 168.0);
            glEnd();
            glFinish();
            for (y = 0; y < H; y++)
                for (x = 0; x < W; x++)
                    if (app[y * W + x] != CLEARC)
                        printf("P %ld %ld %lu\n", x, y, app[y * W + x]);
            return 0;
        }
        if (argc > 2 && strncmp(argv[2], "mod", 3) == 0) {
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            /*
             * "modw" keeps GL's default white.  It is not an arbitrary
             * choice: the two products agree for EVERY texel value when the
             * fragment component is 0, 128, 254 or 255, so an unlit program
             * -- which is what most textured drawing is -- comes out exact.
             */
            if (strcmp(argv[2], "modw") != 0)
                glColor3f(0.6f, 0.8f, 0.35f);
        }
        if (argc > 2 && strcmp(argv[2], "rgbalin") == 0) {
            GLuint rt;

            maketexRGBA(&rt);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                            GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                            GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        if (argc > 2 && strcmp(argv[2], "lin") == 0) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                            GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                            GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        glClear(GL_COLOR_BUFFER_BIT);
        if (argc > 2 && strcmp(argv[2], "modg") == 0) {
            /* a colour of its own at each corner */
            glBegin(GL_TRIANGLES);
              glColor3f(1.0f, 0.2f, 0.4f);
              glTexCoord2f(0.0f, 0.0f); glVertex2d(40.0, 40.0);
              glColor3f(0.2f, 1.0f, 0.6f);
              glTexCoord2f(1.0f, 0.0f); glVertex2d(168.0, 40.0);
              glColor3f(0.3f, 0.5f, 1.0f);
              glTexCoord2f(1.0f, 1.0f); glVertex2d(168.0, 168.0);
              glColor3f(1.0f, 0.2f, 0.4f);
              glTexCoord2f(0.0f, 0.0f); glVertex2d(40.0, 40.0);
              glColor3f(0.3f, 0.5f, 1.0f);
              glTexCoord2f(1.0f, 1.0f); glVertex2d(168.0, 168.0);
              glColor3f(0.9f, 0.9f, 0.1f);
              glTexCoord2f(0.0f, 1.0f); glVertex2d(40.0, 168.0);
            glEnd();
            glFinish();
            for (y = 0; y < H; y++)
                for (x = 0; x < W; x++)
                    if (app[y * W + x] != CLEARC)
                        printf("P %ld %ld %lu\n", x, y, app[y * W + x]);
            return 0;
        }
        /* one quad and one split triangle, so both shapes are compared */
        quad(40.0, 40.0, 168.0, 168.0);
        glBegin(GL_TRIANGLES);
          glTexCoord2f(0.0f, 0.0f); glVertex2d(180.0,  30.0);
          glTexCoord2f(1.0f, 0.2f); glVertex2d(300.0,  70.0);
          glTexCoord2f(0.3f, 1.0f); glVertex2d(210.0, 200.0);
        glEnd();
        glFinish();
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++)
                if (app[y * W + x] != CLEARC)
                    /* the whole pixel, alpha included: with GL_REPLACE and an
                     * RGB texture the alpha must be the fragment's, and a
                     * masked comparison would never have said so */
                    printf("P %ld %ld %lu\n", x, y, app[y * W + x]);
        return 0;
    }

    /* 1. the ordinary case reaches the engine */
    glClear(GL_COLOR_BUFFER_BIT);
    d0 = OSMGAMesaHookDrawn(); s0 = OSMGAMesaHookSoftware();
    p0 = OSMGAMesaHookTexPersp(); a0 = OSMGAMesaHookTexAbsent();
    quad(40.0, 40.0, 168.0, 168.0);
    glFinish();
    drew = painted();
    printf("# ordinary: drawn %lu software %lu persp %lu absent %lu"
           " uploads %lu painted %ld\n",
           OSMGAMesaHookDrawn() - d0, OSMGAMesaHookSoftware() - s0,
           OSMGAMesaHookTexPersp() - p0, OSMGAMesaHookTexAbsent() - a0,
           OSMGAMesaTexUploads(), drew);
    say("a textured quad reaches the engine",
        OSMGAMesaHookDrawn() - d0 >= 2UL, 0);
    say("and nothing fell back", OSMGAMesaHookSoftware() - s0 == 0UL, 0);
    say("and it covered the quad", drew == 128L * 128L, 0);

    /* the picture: every drawn pixel must be a texel of ours */
    {
        long x, y, bad = 0;

        for (y = 40; y < 168; y++)
            for (x = 40; x < 168; x++) {
                unsigned long p = app[y * W + x];
                unsigned long r = (p >> 16) & 0xFFUL;
                unsigned long g = (p >> 8) & 0xFFUL;
                unsigned long b = p & 0xFFUL;
                unsigned long tx = (r - 8UL) / 16UL, ty = (g - 4UL) / 16UL;

                if (r % 16UL != 8UL || g % 16UL != 4UL ||
                    tx >= (unsigned long)TD || ty >= (unsigned long)TD ||
                    b != tx + ty)
                    bad++;
            }
        say("every pixel is a texel of this texture", bad == 0, 0);
    }

    /* 2. each condition, broken on its own, must refuse */
    {
        struct { const char *name; } cases[5];
        unsigned long before;
        int k;

        /*
         * GL_DECAL, not GL_MODULATE: modulate is taken now.  Decal is the
         * next mode along and is still refused, so it keeps this slot
         * honest -- a refusal list that refuses nothing proves nothing.
         */
        cases[0].name = "GL_DECAL, which is not offered";
        /*
         * One filter changed and not the other.  The engine has a single
         * filter switch and GL picks between the two per fragment, so a
         * primitive that could want either is not one the engine can draw.
         */
        cases[1].name = "MagFilter linear, MinFilter still nearest";
        /*
         * Every wrap GL offers is taken now, so the slot holds something
         * that really is refused: GL's DEFAULT minification filter, which is
         * mipmapped.  That is worth an assertion of its own -- it is the
         * reason a program that never touches its filters is drawn in
         * software.
         */
        cases[2].name = "the default mipmapped min filter";
        cases[3].name = "blending on";
        /*
         * Linear IS taken now, but only with CLAMP_TO_EDGE: under a linear
         * filter GL_CLAMP blends a border colour into the outer half texel
         * and the engine's clamp holds the edge texel instead.
         */
        cases[4].name = "GL_LINEAR both, but wrap still GL_CLAMP";
        for (k = 0; k < 5; k++) {
            glClear(GL_COLOR_BUFFER_BIT);
            before = OSMGAMesaHookDrawn();
            if (k == 0) glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
            if (k == 1) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            if (k == 2)
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                GL_NEAREST_MIPMAP_LINEAR);
            if (k == 3) glEnable(GL_BLEND);
            if (k == 4) {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            }
            quad(40.0, 40.0, 168.0, 168.0);
            glFinish();
            say(cases[k].name, OSMGAMesaHookDrawn() == before, 0);
            if (k == 0) glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
            if (k == 1) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            if (k == 2)
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                GL_NEAREST);
            if (k == 3) glDisable(GL_BLEND);
            if (k == 4) {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            }
        }
    }

    /* and linear WITH the edge clamp is taken */
    {
        unsigned long before;

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glClear(GL_COLOR_BUFFER_BIT);
        before = OSMGAMesaHookDrawn();
        quad(40.0, 40.0, 168.0, 168.0);
        glFinish();
        say("GL_LINEAR with GL_CLAMP_TO_EDGE reaches the engine",
            OSMGAMesaHookDrawn() - before >= 2UL, 0);
        say("and it covered the quad", painted() == 128L * 128L, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

    /* GL_REPEAT is taken under a nearest filter, on both axes */
    {
        unsigned long before;

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glClear(GL_COLOR_BUFFER_BIT);
        before = OSMGAMesaHookDrawn();
        quad(40.0, 40.0, 168.0, 168.0);
        glFinish();
        say("GL_REPEAT reaches the engine",
            OSMGAMesaHookDrawn() - before >= 2UL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glClear(GL_COLOR_BUFFER_BIT);
        before = OSMGAMesaHookDrawn();
        quad(40.0, 40.0, 168.0, 168.0);
        glFinish();
        say("and with a linear filter as well",
            OSMGAMesaHookDrawn() - before >= 2UL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    }

    /* GL_MODULATE is taken, and with GL's default white it is exact */
    {
        unsigned long before;

        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glClear(GL_COLOR_BUFFER_BIT);
        before = OSMGAMesaHookDrawn();
        quad(40.0, 40.0, 168.0, 168.0);
        glFinish();
        say("GL_MODULATE reaches the engine",
            OSMGAMesaHookDrawn() - before >= 2UL, 0);
        say("and it covered the quad", painted() == 128L * 128L, 0);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    }

    /* an RGBA texture is taken too, and takes its alpha from itself */
    {
        GLuint rt;
        unsigned long before;

        maketexRGBA(&rt);
        glClear(GL_COLOR_BUFFER_BIT);
        before = OSMGAMesaHookDrawn();
        quad(40.0, 40.0, 168.0, 168.0);
        glFinish();
        say("an RGBA texture reaches the engine",
            OSMGAMesaHookDrawn() - before >= 2UL, 0);
        {
            /*
             * The texel at (0,0) has alpha 17 and the vertex alpha is 255, so
             * the pixel that samples it says which operand won.
             */
            unsigned long a = (app[41L * W + 41L] >> 24) & 0xFFUL;
            char d[48];

            sprintf(d, "alpha %lu", a);
            say("and its alpha is the texture's, not the fragment's",
                a != 255UL, d);
        }

        /*
         * Redefining the same object, both ways.  A stale flag that was set
         * and a stale flag that was clear fail differently, so neither
         * direction stands in for the other.
         */
        {
            static GLubyte rgb[TD * TD * 3];
            int x, y;
            unsigned long a;
            char d[48];

            for (y = 0; y < TD; y++)
                for (x = 0; x < TD; x++) {
                    rgb[(y * TD + x) * 3 + 0] = (GLubyte)(x * 16 + 8);
                    rgb[(y * TD + x) * 3 + 1] = (GLubyte)(y * 16 + 4);
                    rgb[(y * TD + x) * 3 + 2] = (GLubyte)(x + y);
                }
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TD, TD, 0,
                         GL_RGB, GL_UNSIGNED_BYTE, rgb);
            glClear(GL_COLOR_BUFFER_BIT);
            quad(40.0, 40.0, 168.0, 168.0);
            glFinish();
            a = (app[41L * W + 41L] >> 24) & 0xFFUL;
            sprintf(d, "alpha %lu", a);
            say("redefined RGBA -> RGB, the alpha follows", a == 255UL, d);

            {
                static GLubyte px[TD * TD * 4];

                for (y = 0; y < TD; y++)
                    for (x = 0; x < TD; x++) {
                        px[(y * TD + x) * 4 + 0] = (GLubyte)(x * 16 + 8);
                        px[(y * TD + x) * 4 + 1] = (GLubyte)(y * 16 + 4);
                        px[(y * TD + x) * 4 + 2] = (GLubyte)(x + y);
                        px[(y * TD + x) * 4 + 3] = (GLubyte)(x * 8 + y * 3 + 17);
                    }
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TD, TD, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, px);
            }
            glClear(GL_COLOR_BUFFER_BIT);
            quad(40.0, 40.0, 168.0, 168.0);
            glFinish();
            a = (app[41L * W + 41L] >> 24) & 0xFFUL;
            sprintf(d, "alpha %lu", a);
            say("redefined RGB -> RGBA, the alpha follows", a == 17UL, d);
        }

        /*
         * And two objects of different formats alive at once, drawn one after
         * the other in a single frame -- which is what a real program does,
         * and what a single static texture can never test.
         */
        {
            unsigned long a1, a2;
            char d[64];

            glClear(GL_COLOR_BUFFER_BIT);
            glBindTexture(GL_TEXTURE_2D, rt);
            quad(40.0, 40.0, 104.0, 104.0);
            glBindTexture(GL_TEXTURE_2D, tex);
            quad(140.0, 40.0, 204.0, 104.0);
            glBindTexture(GL_TEXTURE_2D, rt);
            quad(40.0, 120.0, 104.0, 184.0);
            glFinish();
            a1 = (app[41L * W + 41L] >> 24) & 0xFFUL;
            a2 = (app[41L * W + 141L] >> 24) & 0xFFUL;
            sprintf(d, "rgba %lu, rgb %lu", a1, a2);
            say("two formats in one frame keep their own alpha",
                a1 == 17UL && a2 == 255UL, d);
        }

        glDeleteTextures(1, &rt);
        glBindTexture(GL_TEXTURE_2D, tex);
    }

    /* and it comes back after they are put right */
    glClear(GL_COLOR_BUFFER_BIT);
    d0 = OSMGAMesaHookDrawn();
    quad(40.0, 40.0, 168.0, 168.0);
    glFinish();
    say("acceleration returns when the state does",
        OSMGAMesaHookDrawn() - d0 >= 2UL, 0);

    /* 3. perspective is drawn by the engine now, not turned away */
    glClear(GL_COLOR_BUFFER_BIT);
    d0 = OSMGAMesaHookDrawn(); p0 = OSMGAMesaHookTexPersp();
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glFrustum(-1.0, 1.0, -1.0, 1.0, 1.0, 20.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glBegin(GL_TRIANGLES);
      glTexCoord2f(0.0f, 0.0f); glVertex3d(-0.6, -0.6, -2.0);
      glTexCoord2f(1.0f, 0.0f); glVertex3d( 0.6, -0.6, -8.0);
      glTexCoord2f(1.0f, 1.0f); glVertex3d( 0.6,  0.6, -8.0);
    glEnd();
    glFinish();
    say("a perspective triangle reaches the engine",
        OSMGAMesaHookDrawn() - d0 >= 1UL, 0);
    /*
     * And for the right reason: the counter that used to record the affine
     * refusal must not move.  It still counts a vertex whose w is at or
     * below nought, which is a different thing and is still refused.
     */
    say("and the affine gate did not turn it away",
        OSMGAMesaHookTexPersp() == p0, 0);

    /* 4. a triangle that splits into two trapezoids */
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0.0, (double)W, 0.0, (double)H, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glClear(GL_COLOR_BUFFER_BIT);
    d0 = OSMGAMesaHookBatches(); s0 = OSMGAMesaHookSoftware();
    glBegin(GL_TRIANGLES);
      glTexCoord2f(0.0f, 0.0f); glVertex2d( 20.0,  30.0);
      glTexCoord2f(1.0f, 0.2f); glVertex2d(220.0,  70.0);
      glTexCoord2f(0.3f, 1.0f); glVertex2d( 80.0, 200.0);
    glEnd();
    glFinish();
    /*
     * A middle vertex means two trapezoids, and tmr[] is batch state, so this
     * is where one batch each earns its keep.  Two submissions from ONE
     * triangle is the thing to see; one would mean the split never happened
     * and the case proves nothing.
     */
    say("a split triangle goes out as two batches",
        OSMGAMesaHookBatches() - d0 == 2UL, 0);
    say("with nothing falling back", OSMGAMesaHookSoftware() - s0 == 0UL, 0);
    {
        long x, y, bad = 0, seen = 0;

        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                unsigned long p = app[y * W + x];
                unsigned long r, g, b, tx, ty;

                if (p == CLEARC) continue;
                seen++;
                r = (p >> 16) & 0xFFUL; g = (p >> 8) & 0xFFUL; b = p & 0xFFUL;
                tx = (r - 8UL) / 16UL; ty = (g - 4UL) / 16UL;
                if (r % 16UL != 8UL || g % 16UL != 4UL ||
                    tx >= (unsigned long)TD || ty >= (unsigned long)TD ||
                    b != tx + ty)
                    bad++;
            }
        printf("# split: %ld pixels, %ld not a texel of ours\n", seen, bad);
        say("every pixel of it is still a texel", bad == 0, 0);
    }

    /* 5. the arena, run out */
    {
        GLuint big[24];
        static GLubyte fat[256 * 256 * 3];
        int k, ran = 0;

        memset(fat, 0x40, sizeof fat);
        glGenTextures(24, big);
        for (k = 0; k < 24; k++) {
            glBindTexture(GL_TEXTURE_2D, big[k]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 256, 256, 0,
                         GL_RGB, GL_UNSIGNED_BYTE, fat);
            glClear(GL_COLOR_BUFFER_BIT);
            d0 = OSMGAMesaHookDrawn(); a0 = OSMGAMesaHookTexAbsent();
            quad(40.0, 40.0, 168.0, 168.0);
            glFinish();
            if (OSMGAMesaHookDrawn() == d0) {
                say("running out of arena falls back to software",
                    OSMGAMesaHookTexAbsent() - a0 >= 1UL, 0);
                say("and the quad is still drawn", painted() == 128L * 128L, 0);
                ran = 1;
                break;
            }
        }
        say("the arena does run out", ran, 0);
        glDeleteTextures(24, big);
    }

    printf("\n%s (%d failing)\n",
           failures ? "=== PROBLEM ===" : "=== nothing to report ===", failures);
    return failures ? 1 : 0;
}
