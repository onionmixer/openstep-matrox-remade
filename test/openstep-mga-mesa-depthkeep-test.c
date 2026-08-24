/*
 * Does the depth survive the back end giving up the surface?
 *
 * OSMesaPixelStore can ask for a row length the accelerated surface does not
 * have, and then the back end must hand the surface back mid-context.  It
 * mirrors the COLOUR out when it does -- and then throws the DEPTH away:
 * DepthBuffer to NULL, the software depth buffer back on, and
 * _mesa_alloc_depth_buffer, whose own comment says "allocate new depth
 * buffer, but don't initialize it".
 *
 * So colour survives that moment and depth does not.  Nothing in GL says a
 * pixel-store call empties the depth buffer, and an application that carries
 * on drawing is then testing against whatever malloc handed over.
 *
 * This writes a known depth, forces the fallback, and asks two questions:
 * is the CODE still there, and does the comparison still behave as though it
 * is.  The second one matters on its own -- a driver could keep the numbers
 * and still have stopped testing against them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <GL/osmesa.h>
#include <GL/gl.h>

#include "../mesa/OpenStepMGAMesaHook.h"

#define W 128
#define H 96

static unsigned long *app;
static OSMesaContext theCtx;
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

static void
quad(double z, float r, float g, float b)
{
    glColor3f(r, g, b);
    glBegin(GL_TRIANGLES);
      glVertex3d(20.0, 20.0, z); glVertex3d(108.0, 20.0, z);
      glVertex3d(108.0, 76.0, z);
      glVertex3d(20.0, 20.0, z); glVertex3d(108.0, 76.0, z);
      glVertex3d(20.0, 76.0, z);
    glEnd();
    glFinish();
}

/* the depth code under the sample, whichever buffer is current */
static int
depthAt(unsigned *out)
{
    void *raw;
    GLint dw, dh, bpv;

    if (!OSMesaGetDepthBuffer(theCtx, &dw, &dh, &bpv, &raw) || !raw ||
        bpv != 2)
        return 0;
    *out = (unsigned)((unsigned short *)raw)[48 * W + 64];
    return 1;
}

int
main(void)
{
    OSMesaContext ctx;
    unsigned before = 0, after = 0;

    app = (unsigned long *)malloc((unsigned)(W * H) * sizeof(unsigned long));
    if (!app) { printf("no room\n"); return 2; }
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, app, GL_UNSIGNED_BYTE, W, H)) {
        printf("no context\n"); return 2;
    }
    theCtx = ctx;

    printf("the depth across a fallback\n\n");
    if (OSMGAMesaBufferDepthOrigin() == 0UL) {
        printf("NOT RUN: the depth is not the shared one, so there is"
               " nothing to lose\n");
        return 2;
    }

    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0.0, (double)W, 0.0, (double)H, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
    glDisable(GL_BLEND); glDisable(GL_DITHER);
    glDisable(GL_TEXTURE_2D); glDisable(GL_CULL_FACE);
    glShadeModel(GL_FLAT);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepth(1.0);
    glDepthFunc(GL_ALWAYS);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    quad(0.0, 1.0f, 0.0f, 0.0f);            /* red, and the depth it wrote */

    if (!depthAt(&before)) { printf("no depth to read\n"); return 2; }
    printf("   depth written while accelerated : %04x\n", before);

    /*
     * The fallback, by turning the picture over.
     *
     * Asking for a different ROW LENGTH was the first attempt and it did
     * nothing: this surface is laid out at the WIDTH, not at the display's
     * stride -- that is the precondition the shared depth needs -- so asking
     * for the width asks for what is already in force.  The assertion below
     * is what caught it; without it the two checks after would have been
     * asking their question of a surface that had never been given up.
     *
     * The orientation is the other half of the same condition and it really
     * does differ.
     */
    OSMesaPixelStore(OSMESA_Y_UP, 0);
    say("the surface really was given up",
        OSMGAMesaBufferOrigin() == 0UL);

    if (!depthAt(&after)) { printf("no depth after the fallback\n"); return 2; }
    printf("   depth after the fallback        : %04x\n", after);
    say("the depth code survived", after == before);

    /*
     * And that it is still being compared against.  A farther quad must lose
     * to what the first one wrote.
     */
    glDepthFunc(GL_LESS);
    quad(-0.5, 0.0f, 1.0f, 0.0f);           /* green, farther, must lose */
    say("a farther quad is still rejected",
        ((app[48 * W + 64] >> 16) & 0xFFUL) > 0x80UL);

    printf("\n%s (%d failing)\n",
           failures ? "=== PROBLEM ===" : "=== nothing to report ===",
           failures);
    return failures ? 1 : 0;
}
