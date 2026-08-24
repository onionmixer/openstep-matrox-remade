/*
 * Who owns the surface, and what happens when the answer changes.
 *
 * There is ONE video memory surface and it belongs to the context that got
 * it.  Three ways that ownership used to be broken are recorded in
 * REMAINING_WORK 3-23, and all three read as fixed now -- but reading is not
 * measuring, and the tests that item asked for were never written.  These are
 * they.
 *
 *   1. a row length changed AFTER the context is current.  The software path
 *      recomputes its row addresses; the accelerated one must not go on
 *      submitting at the old stride.
 *   2. the same context rebound at a different size.  There is only one
 *      surface and it is the wrong shape now.
 *   3. a second context.  It must not get the first one's surface, and
 *      destroying it must not take the first one's away.
 *
 * Each asks the same question: did anything reach the ENGINE when it should
 * not have.  A test that only looked at pixels would pass either way, since
 * the software path draws the same picture.
 */
#include <stdio.h>
#include <stdlib.h>
#include <GL/osmesa.h>
#include <GL/gl.h>

#include "../mesa/OpenStepMGAMesaHook.h"

#define W 128
#define H 96

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
setUp(int w, int h)
{
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0.0, (double)w, 0.0, (double)h, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_DITHER);
    glDisable(GL_TEXTURE_2D); glDisable(GL_CULL_FACE);
    glShadeModel(GL_FLAT);
}

/* one quad, and how many batches it cost */
static unsigned long
draw(void)
{
    unsigned long before = OSMGAMesaHookDrawn();

    glColor3f(0.0f, 1.0f, 0.0f);
    glBegin(GL_TRIANGLES);
      glVertex2d(10.0, 10.0); glVertex2d(60.0, 10.0); glVertex2d(60.0, 50.0);
      glVertex2d(10.0, 10.0); glVertex2d(60.0, 50.0); glVertex2d(10.0, 50.0);
    glEnd();
    glFinish();
    return OSMGAMesaHookDrawn() - before;
}

int
main(void)
{
    OSMesaContext a, b;
    unsigned long *bufA, *bufB, *bufC;

    bufA = (unsigned long *)malloc((unsigned)(W * H) * sizeof(unsigned long));
    bufB = (unsigned long *)malloc((unsigned)(W * H) * sizeof(unsigned long));
    bufC = (unsigned long *)malloc((unsigned)(W * H) * sizeof(unsigned long));
    if (!bufA || !bufB || !bufC) { printf("no room\n"); return 2; }

    printf("who owns the surface\n\n");

    /* ---- 1. the row length moves under a current context ---- */
    a = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!a || !OSMesaMakeCurrent(a, bufA, GL_UNSIGNED_BYTE, W, H)) {
        printf("no context\n"); return 2;
    }
    setUp(W, H);
    say("the surface is the engine's to start with",
        OSMGAMesaBufferOrigin() != 0UL);
    say("and it draws there", draw() != 0UL);

    /*
     * 333 is not this surface's stride and never can be -- the engine walks
     * the pitch the batch declares, and the batch declares the surface's.
     */
    OSMesaPixelStore(OSMESA_ROW_LENGTH, 333);
    say("a row length the surface does not have gives the surface up",
        OSMGAMesaBufferOrigin() == 0UL);
    say("and nothing reaches the engine after that", draw() == 0UL);

    /* ---- 2. the same context, rebound at another size ---- */
    OSMesaDestroyContext(a);
    a = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!a || !OSMesaMakeCurrent(a, bufA, GL_UNSIGNED_BYTE, W, H)) {
        printf("no context for the rebind\n"); return 2;
    }
    setUp(W, H);
    say("the surface is back for a fresh context",
        OSMGAMesaBufferOrigin() != 0UL);

    /* half the height, same width -- one surface cannot be both shapes */
    if (!OSMesaMakeCurrent(a, bufA, GL_UNSIGNED_BYTE, W, H / 2)) {
        printf("the rebind was refused outright\n"); return 2;
    }
    setUp(W, H / 2);
    say("a rebind at another size gives the surface up",
        OSMGAMesaBufferOrigin() == 0UL);
    say("and nothing reaches the engine after that", draw() == 0UL);

    /* ---- 3. a second context ---- */
    OSMesaDestroyContext(a);
    a = OSMesaCreateContext(OSMESA_ARGB, NULL);
    b = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!a || !b) { printf("no two contexts\n"); return 2; }
    if (!OSMesaMakeCurrent(a, bufA, GL_UNSIGNED_BYTE, W, H)) {
        printf("no first context\n"); return 2;
    }
    setUp(W, H);
    say("the first context has the surface", OSMGAMesaBufferOrigin() != 0UL);

    if (!OSMesaMakeCurrent(b, bufB, GL_UNSIGNED_BYTE, W, H)) {
        printf("no second context\n"); return 2;
    }
    setUp(W, H);
    /*
     * NOT "the origin is nought".  That accessor answers "is there a
     * surface", not "is it this context's" -- it is a global, and the
     * surface still exists because the first context still holds it.  The
     * first version of this asked the wrong question and failed while the
     * driver was right.
     *
     * What matters is that the second context neither drives the engine nor
     * lands in the first one's memory.  So: it draws no batches, its own
     * buffer gets the picture, and the first one's buffer does not.
     */
    {
        unsigned long i, hitB = 0UL, hitA = 0UL;

        for (i = 0UL; i < (unsigned long)(W * H); i++) {
            bufA[i] = 0UL;
            bufB[i] = 0UL;
        }
        say("the second context draws no batches", draw() == 0UL);
        for (i = 0UL; i < (unsigned long)(W * H); i++) {
            if (((bufB[i] >> 8) & 0xFFUL) > 0x80UL) hitB++;
            if (((bufA[i] >> 8) & 0xFFUL) > 0x80UL) hitA++;
        }
        printf("   the second context painted %lu pixels of its own buffer"
               " and %lu of the first's\n", hitB, hitA);
        say("it painted its own buffer", hitB != 0UL);
        say("and none of the first one's", hitA == 0UL);
    }

    /*
     * And the one that used to kill the process: the second context going
     * away took the first one's mappings with it.
     */
    OSMesaDestroyContext(b);
    if (!OSMesaMakeCurrent(a, bufA, GL_UNSIGNED_BYTE, W, H)) {
        printf("the first context will not come back\n"); return 2;
    }
    setUp(W, H);
    say("destroying the second leaves the first one's surface alone",
        OSMGAMesaBufferOrigin() != 0UL);
    say("and the first draws on the engine again", draw() != 0UL);

    printf("\n%s (%d failing)\n",
           failures ? "=== PROBLEM ===" : "=== nothing to report ===",
           failures);
    return failures ? 1 : 0;
}
