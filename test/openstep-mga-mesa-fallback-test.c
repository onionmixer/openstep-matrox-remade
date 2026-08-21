/*
 * openstep-mga-mesa-fallback-test.c -- is a triangle this back end cannot
 * draw still drawn?
 *
 * Mesa hands a triangle to whatever is in Driver.TriangleFunc and there is no
 * way back from inside the call, so anything the back end could not express
 * used to be dropped: the picture simply lost it.  What is saved now is the
 * software triangle Mesa itself chose, one line before ours went in over it.
 *
 * The lever is a vertex far past the viewport.  It was chosen to be past the
 * coordinate range the back end can express (16384), and it turns out not to
 * reach that path at all: Mesa clips the triangle first, so what arrives is
 * in range and the KERNEL is what refuses the batch.  The test is better for
 * it -- a refusal from the kernel is the case that used to cost the triangle
 * AND revoke acceleration for the rest of the process, and here the ordinary
 * triangle beside it goes on being accelerated in the same frame.
 *
 * Which of the two reasons fired is printed rather than assumed.
 *
 *   cc -O -Wall -o /tmp/fb openstep-mga-mesa-fallback-test.c \
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
#define NEARC   0xFF00FF00UL      /* the ordinary triangle */
#define FARC    0xFFFF0000UL      /* the one with a vertex out of range */

static unsigned long *app;

static void
project(void)
{
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0.0, (double)W, 0.0, (double)H, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
}

/*
 * Two triangles.  The first is ordinary.  The second has one vertex far past
 * anything the back end can name, while the other two are on screen, so a
 * wedge of it is visible -- that wedge is the whole question.
 */
static void
scene(int withFar)
{
    glClearColor(0x10/255.0f, 0x20/255.0f, 0x30/255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
      glColor3ub(0x00, 0xFF, 0x00);
      glVertex3f(  4.5f,   4.5f, 0.0f);
      glVertex3f( 60.5f,   4.5f, 0.0f);
      glVertex3f(  4.5f,  60.5f, 0.0f);
    glEnd();
    if (withFar) {
        glBegin(GL_TRIANGLES);
          glColor3ub(0xFF, 0x00, 0x00);
          glVertex3f(120.5f,   4.5f, 0.0f);
          glVertex3f(300.5f,   4.5f, 0.0f);
          glVertex3f(120.5f, 40000.0f, 0.0f);   /* past 16384 */
        glEnd();
    }
    glFinish();
}

static long
countColour(unsigned long c)
{
    long n = 0, i;

    for (i = 0; i < (long)W * H; i++)
        if (app[i] == c) n++;
    return n;
}

int
main(void)
{
    OSMesaContext ctx;
    unsigned long d0, s0, x0, u0;
    long nearOnly, both, farPixels;

    app = (unsigned long *)malloc((unsigned)(W * H) * sizeof(unsigned long));
    if (!app) { printf("no room\n"); return 2; }
    memset(app, 0, (unsigned)(W * H) * sizeof(unsigned long));

    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, app, GL_UNSIGNED_BYTE, W, H)) {
        printf("no context\n"); return 2;
    }
    project();

    d0 = OSMGAMesaHookDrawn();
    scene(0);
    printf("the ordinary triangle alone: accelerated %lu, surface %lu\n",
           OSMGAMesaHookDrawn() - d0, OSMGAMesaBufferOrigin());
    if (OSMGAMesaHookDrawn() == d0) {
        printf("   nothing was accelerated; this test says nothing\n");
        return 2;
    }
    nearOnly = countColour(NEARC);
    printf("   green pixels: %ld\n", nearOnly);

    d0 = OSMGAMesaHookDrawn();
    s0 = OSMGAMesaHookSoftware();
    x0 = OSMGAMesaHookDeclined();
    u0 = OSMGAMesaHookUnsupported();
    scene(1);
    both = countColour(NEARC);
    farPixels = countColour(FARC);
    printf("with the out-of-range triangle: accelerated %lu, "
           "handed to software %lu (of which %lu this back end could not "
           "express), declined %lu\n",
           OSMGAMesaHookDrawn() - d0, OSMGAMesaHookSoftware() - s0,
           OSMGAMesaHookUnsupported() - u0, OSMGAMesaHookDeclined() - x0);
    printf("   green %ld (was %ld), red %ld\n", both, nearOnly, farPixels);

    if (OSMGAMesaHookSoftware() - s0 == 0UL) {
        printf("FAIL -- nothing was handed to software; the back end either "
               "drew it or dropped it\n");
        return 1;
    }
    if (farPixels == 0L) {
        printf("FAIL -- the refused triangle left no pixels: it was dropped, "
               "which is what this exists to stop\n");
        return 1;
    }
    if (both != nearOnly) {
        printf("FAIL -- the ordinary triangle changed when the other one was "
               "added\n");
        return 1;
    }
    printf("PASS -- the triangle this back end could not draw was drawn by "
           "software, %ld pixels of it; the accelerated one beside it is "
           "untouched and still accelerated in the same frame\n", farPixels);
    OSMesaDestroyContext(ctx);
    return 0;
}
