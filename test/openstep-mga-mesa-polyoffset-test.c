/*
 * glPolygonOffset, against the software rasteriser.
 *
 * The engine has no offset unit and needs none: the offset is a constant
 * added to the depth plane, which this back end already solves, and Mesa's
 * software does the same arithmetic rather than reaching for a hardware
 * feature.  The number itself is computed in the hook from Mesa's own
 * unsnapped window coordinates with Mesa's own expression, so the two paths
 * are exact by construction and not by reproduction.
 *
 * python, over this projection -- glOrtho(0, W, 0, H, -1, 1) puts the window
 * depth at (1 - z)/2 * 65535:
 *
 *     a flat quad at z = 0 sits at 32767.5 codes
 *     units alone move it by exactly that many codes
 *     a quad sloping from z = 0 to z = -0.5 over 88 pixels has a slope of
 *     186.18 codes a pixel, so a factor of 4 is 744.7 codes
 *
 * Three things are asked.  That the units term moves a flat polygon and by
 * how much; that the factor term moves a SLOPED one and leaves a flat one
 * alone, which is what separates a slope from a constant; and that a decal
 * asked for with a negative offset actually appears over its coplanar base,
 * which is what applications use this for and what a wrong sign breaks.
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
/* the context, because the depth buffer is asked for by context and a
 * null one is a segmentation fault rather than a refusal */
static OSMesaContext theCtx;

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

static void
quad(double zl, double zr, float r, float g, float b)
{
    glColor3f(r, g, b);
    glBegin(GL_TRIANGLES);
      glVertex3d(20.0, 20.0, zl); glVertex3d(108.0, 20.0, zr);
      glVertex3d(108.0, 76.0, zr);
      glVertex3d(20.0, 20.0, zl); glVertex3d(108.0, 76.0, zr);
      glVertex3d(20.0, 76.0, zl);
    glEnd();
}

/* the depth left at the middle pixel after one offset quad */
static int
depthAfter(double zl, double zr, GLfloat factor, GLfloat units, int soft,
           unsigned long *drew)
{
    void *zb; GLint dw, dh, bpv;
    unsigned long before = OSMGAMesaHookDrawn();

    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_ALWAYS); glDepthMask(GL_TRUE);
    glClearDepth(1.0);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glPolygonOffset(factor, units);
    glEnable(GL_POLYGON_OFFSET_FILL);
    if (soft) softOn();
    quad(zl, zr, 1.0f, 1.0f, 1.0f);
    glFinish();
    if (soft) softOff();
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_DEPTH_TEST);
    *drew = OSMGAMesaHookDrawn() - before;
    if (!OSMesaGetDepthBuffer(theCtx, &dw, &dh, &bpv, &zb))
        return -1;
    return (int)((unsigned short *)zb)[48 * W + 64];
}

int
main(void)
{
    OSMesaContext ctx;

    app = (unsigned long *)malloc((unsigned)(W * H) * sizeof(unsigned long));
    if (!app) { printf("no room\n"); return 2; }
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, app, GL_UNSIGNED_BYTE, W, H)) {
        printf("no context\n"); return 2;
    }
    theCtx = ctx;
    {
        void *zb; GLint dw, dh, bpv;

        if (!OSMesaGetDepthBuffer(ctx, &dw, &dh, &bpv, &zb) || bpv != 2) {
            printf("NOT RUN: no 16-bit depth buffer\n"); return 2;
        }
    }
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0.0, (double)W, 0.0, (double)H, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_BLEND); glDisable(GL_DITHER); glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE); glDisable(GL_ALPHA_TEST);
    glShadeModel(GL_FLAT);

    printf("glPolygonOffset, engine against software\n\n");

    printf("1. the units term, on a flat polygon\n");
    {
        static const GLfloat un[3] = { -8.0f, 0.0f, 8.0f };
        int i, base = -1;

        for (i = 0; i < 3; i++) {
            unsigned long dh, ds;
            int h = depthAfter(0.0, 0.0, 0.0f, un[i], 0, &dh);
            int s = depthAfter(0.0, 0.0, 0.0f, un[i], 1, &ds);
            char name[80];

            if (i == 1) base = h;
            printf("   units %+5.1f  engine %5d  software %5d%s\n",
                   un[i], h, s, (dh == 0UL) ? "   <-- NOT ACCELERATED" : "");
            sprintf(name, "units %+.1f agrees with software", un[i]);
            say(name, h == s);
            if (dh == 0UL) {
                printf("   FAIL  units %+.1f never reached the engine\n",
                       un[i]);
                failures++;
            }
            if (ds != 0UL) {
                printf("   FAIL  the software pass was accelerated\n");
                failures++;
            }
        }
        /*
         * And the SIZE, not only the agreement: python says a flat polygon at
         * z = 0 sits at 32767.5 codes and the units move it code for code, so
         * eight either way is eight either way.  Agreement alone would be
         * satisfied by both paths ignoring the offset.
         */
        {
            unsigned long dz;
            int lo = depthAfter(0.0, 0.0, 0.0f, -8.0f, 0, &dz);
            int hi = depthAfter(0.0, 0.0, 0.0f,  8.0f, 0, &dz);

            printf("   the spread between -8 and +8 is %d codes,"
                   " python says 16\n", hi - lo);
            say("the units term moves the depth code for code",
                hi - lo == 16);
            say("and a positive offset moves AWAY, as GL says", hi > base);
        }
    }

    printf("\n2. the factor term, which must see the slope\n");
    {
        unsigned long dz;
        /*
         * python: this slope is 186.18 codes a pixel, so a factor of four is
         * 744.7 codes.  A flat polygon has no slope and must not move at all.
         */
        int flat0 = depthAfter(0.0, 0.0, 0.0f, 0.0f, 0, &dz);
        int flat4 = depthAfter(0.0, 0.0, 4.0f, 0.0f, 0, &dz);
        int slope0 = depthAfter(0.0, -0.5, 0.0f, 0.0f, 0, &dz);
        int slope4 = depthAfter(0.0, -0.5, 4.0f, 0.0f, 0, &dz);
        int sslope4 = depthAfter(0.0, -0.5, 4.0f, 0.0f, 1, &dz);

        printf("   flat   factor 0 -> %5d   factor 4 -> %5d   (moved %d)\n",
               flat0, flat4, flat4 - flat0);
        printf("   sloped factor 0 -> %5d   factor 4 -> %5d   (moved %d,"
               " python says 745)\n", slope0, slope4, slope4 - slope0);
        say("a flat polygon does not move under the factor alone",
            flat4 == flat0);
        say("a sloped one moves by what python says",
            slope4 - slope0 >= 744 && slope4 - slope0 <= 746);
        say("and software agrees with the engine on the sloped one",
            slope4 == sslope4);
    }

    printf("\n3. the decal, which is what this is for\n");
    {
        unsigned long px;
        int k;

        for (k = 0; k < 2; k++) {
            glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glClearDepth(1.0);
            glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            quad(0.0, 0.0, 1.0f, 0.0f, 0.0f);      /* the base, red */
            glFinish();
            if (k) {
                /* NEGATIVE units: GL moves the polygon TOWARDS the viewer,
                 * which is what a decal needs under GL_LESS */
                glPolygonOffset(0.0f, -8.0f);
                glEnable(GL_POLYGON_OFFSET_FILL);
            }
            quad(0.0, 0.0, 0.0f, 1.0f, 0.0f);      /* the decal, green */
            glFinish();
            glDisable(GL_POLYGON_OFFSET_FILL);
            glDisable(GL_DEPTH_TEST);
            px = app[48 * W + 64];
            if (k)
                say("a decal with a negative offset shows over its base",
                    ((px >> 8) & 0xFFUL) > 0x80UL);
            else
                say("and without one it does not, so the offset is what did"
                    " it", ((px >> 16) & 0xFFUL) > 0x80UL);
        }
    }

    printf("\n%s (%d failing)\n",
           failures ? "=== PROBLEM ===" : "=== nothing to report ===",
           failures);
    return failures ? 1 : 0;
}
