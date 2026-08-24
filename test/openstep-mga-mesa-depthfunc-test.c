/*
 * The seven depth comparisons the engine has, each against the software
 * rasteriser.
 *
 * The chooser used to take GL_LESS alone and send every enabled depth state
 * to the engine's ZLT.  Seven of GL's eight functions have an encoding the
 * register documentation names -- nozcmp, ze, zne, zlt, zlte, zgt, zgte --
 * with value 1 unnamed and GL_NEVER therefore refused.
 *
 * Each function is asked three questions, not one: a seed depth, and a second
 * primitive drawn NEARER, at the SAME code, and FARTHER.  One case can be
 * satisfied by a driver that always draws or never draws; three cannot.  The
 * same-code case is why the depths are flat and calibrated rather than
 * interpolated -- the two paths' depth codes are known to differ by as much
 * as two on a sloped primitive, and equality has no tolerance at all.
 *
 * Software is forced the way depth-agree does it, with
 * OSMGAMesaHookForceSoftware.
 *
 * It used to be forced with a scissor the size of the surface -- refused by
 * the chooser, clipping nothing, so the path changed and the picture did
 * not.  That idiom is dead and must not come back: the scissor is accepted
 * now, so a full-surface one changes neither the path nor the picture, and a
 * test built on it would quietly compare the engine with itself.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/osmesa.h>
#include <GL/gl.h>

#include "../mesa/OpenStepMGAMesaHook.h"

#define W 128
#define H 96

static unsigned long *app;
static int failures;

static void
say(const char *what, int ok)
{
    if (ok)
        printf("   ok    %s\n", what);
    else {
        printf("   FAIL  %s\n", what);
        failures++;
    }
}

/*
 * The path, changed by asking for it.
 *
 * This used to be a full-surface scissor -- a state the chooser
 * refused, which clipped nothing -- and that was borrowed rather than
 * owned: the moment the scissor is admitted, this comparison would
 * become hardware against hardware and pass without asking anything.
 */
static void softOn(void)  { OSMGAMesaHookForceSoftware(1); }
static void softOff(void) { OSMGAMesaHookForceSoftware(0); }

/* a flat quad at a chosen window depth, in a chosen colour */
static void
flatQuad(double z, float r, float g, float b)
{
    glColor3f(r, g, b);
    glBegin(GL_TRIANGLES);
      glVertex3d(20.0, 20.0, z); glVertex3d(108.0, 20.0, z);
      glVertex3d(108.0, 76.0, z);
      glVertex3d(20.0, 20.0, z); glVertex3d(108.0, 76.0, z);
      glVertex3d(20.0, 76.0, z);
    glEnd();
}

/*
 * Draw the seed, then the probe, and say whether the probe won the pixel.
 *
 * The colours are the answer: the seed is red and the probe is green, so a
 * pixel that is green means the comparison passed and one that is red means
 * it did not.  Reading the COLOUR rather than the depth is deliberate -- a
 * comparison that passed and then failed to write the depth would still be a
 * pass, and this asks about the comparison.
 */
static int
probeWins(GLenum func, double zseed, double zprobe, int soft,
          unsigned long *drew)
{
    unsigned long px;
    unsigned long before;

    glDepthFunc(GL_ALWAYS);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (soft) softOn();
    flatQuad(zseed, 1.0f, 0.0f, 0.0f);
    glFinish();
    glDepthFunc(func);
    /*
     * Around the PROBE quad only, because that is the one whose comparison
     * is under test.  Counting the whole function would also pass if the
     * seed went to the engine and the probe quietly did not, and the answer
     * this returns would then be the software rasteriser's while the report
     * said it was the engine's.  The count is a required argument for the
     * same reason: a caller cannot forget to ask which path answered.
     */
    before = OSMGAMesaHookDrawn();
    flatQuad(zprobe, 0.0f, 1.0f, 0.0f);
    glFinish();
    *drew = OSMGAMesaHookDrawn() - before;
    if (soft) softOff();
    px = app[48 * W + 64];
    return (((px >> 8) & 0xFFUL) > 0x80UL);      /* green means it won */
}

int
main(void)
{
    OSMesaContext ctx;
    /*
     * Which way round is near.
     *
     * glOrtho(0, W, 0, H, -1, 1) puts the window depth at (1 - z)/2, so
     * z = -0.5 is 0.75 and FARTHER while z = +0.5 is 0.25 and NEARER.  The
     * columns below are far, same, near in that order, and the wanted values
     * are python's -- it was written the other way round first and every
     * inequality "failed" while the hardware and the software agreed with
     * each other, which is the shape of a wrong expectation and not a wrong
     * driver.
     */
    static const struct { GLenum f; const char *n;
                          int wantFar, wantSame, wantNear; } cases[7] = {
        { GL_LESS,     "GL_LESS    ", 0, 0, 1 },
        { GL_LEQUAL,   "GL_LEQUAL  ", 0, 1, 1 },
        { GL_GREATER,  "GL_GREATER ", 1, 0, 0 },
        { GL_GEQUAL,   "GL_GEQUAL  ", 1, 1, 0 },
        { GL_EQUAL,    "GL_EQUAL   ", 0, 1, 0 },
        { GL_NOTEQUAL, "GL_NOTEQUAL", 1, 0, 1 },
        { GL_ALWAYS,   "GL_ALWAYS  ", 1, 1, 1 }
    };
    int i;

    app = (unsigned long *)malloc((unsigned)(W * H) * sizeof(unsigned long));
    if (!app) { printf("no room\n"); return 2; }
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, app, GL_UNSIGNED_BYTE, W, H)) {
        printf("no context\n"); return 2;
    }
    {
        void *zb; GLint dw, dh, bpv;

        if (!OSMesaGetDepthBuffer(ctx, &dw, &dh, &bpv, &zb) || !zb ||
            bpv != 2) {
            printf("NOT RUN: no 16-bit depth buffer\n");
            return 2;
        }
    }
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0.0, (double)W, 0.0, (double)H, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glShadeModel(GL_FLAT);
    glDisable(GL_BLEND); glDisable(GL_DITHER); glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE); glDisable(GL_ALPHA_TEST);

    printf("the seven depth comparisons, engine against software\n\n");

    for (i = 0; i < 7; i++) {
        /*
         * A flat primitive lands in one depth code, so "the same" really is
         * the same code in both paths and the equality cases mean what they
         * say.  The three z values are far enough apart that a code or two of
         * disagreement cannot turn nearer into farther.
         */
        unsigned long dw[6];
        int hf = probeWins(cases[i].f, 0.0, -0.5, 0, &dw[0]); /* farther */
        int hs = probeWins(cases[i].f, 0.0,  0.0, 0, &dw[1]); /* the same */
        int hn = probeWins(cases[i].f, 0.0,  0.5, 0, &dw[2]); /* nearer   */
        int sf = probeWins(cases[i].f, 0.0, -0.5, 1, &dw[3]);
        int ss = probeWins(cases[i].f, 0.0,  0.0, 1, &dw[4]);
        int sn = probeWins(cases[i].f, 0.0,  0.5, 1, &dw[5]);
        char name[80];

        printf("   %s  engine far/same/near %d%d%d   software %d%d%d"
               "   wanted %d%d%d\n", cases[i].n, hf, hs, hn, sf, ss, sn,
               cases[i].wantFar, cases[i].wantSame, cases[i].wantNear);
        sprintf(name, "%s draws where GL says", cases[i].n);
        say(name, hf == cases[i].wantFar && hs == cases[i].wantSame &&
                  hn == cases[i].wantNear);
        sprintf(name, "%s and software agrees", cases[i].n);
        say(name, hf == sf && hs == ss && hn == sn);
        if (dw[0] == 0UL || dw[1] == 0UL || dw[2] == 0UL) {
            printf("   FAIL  %s never reached the engine\n", cases[i].n);
            failures++;
        }
        if (dw[3] != 0UL || dw[4] != 0UL || dw[5] != 0UL) {
            printf("   FAIL  %s: the software pass was accelerated\n",
                   cases[i].n);
            failures++;
        }
    }

    /*
     * And the two that must still be refused.  A chooser that had quietly
     * widened would draw these on the engine.
     */
    printf("\n   what is still refused\n");
    {
        unsigned long d0 = OSMGAMesaHookDrawn();

        glDepthFunc(GL_NEVER);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        flatQuad(0.0, 1.0f, 1.0f, 1.0f);
        glFinish();
        say("GL_NEVER does not reach the engine",
            OSMGAMesaHookDrawn() == d0);

        /*
         * A masked depth write used to be refused here.  It is not any more:
         * the engine spells "compare but do not write" in the access type,
         * and the card was asked directly.  What that costs is here -- this
         * assertion is now the opposite of what it was -- and what it means
         * is in the test that owns it, which checks the comparison still
         * happens and the buffer really does stay put.
         */
        glDepthFunc(GL_LESS);
        glDepthMask(GL_FALSE);
        d0 = OSMGAMesaHookDrawn();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        flatQuad(0.0, 1.0f, 1.0f, 1.0f);
        glFinish();
        say("a masked depth write now reaches the engine",
            OSMGAMesaHookDrawn() != d0);
        glDepthMask(GL_TRUE);
    }

    printf("\n%s (%d failing)\n",
           failures ? "=== PROBLEM ===" : "=== nothing to report ===",
           failures);
    return failures ? 1 : 0;
}
