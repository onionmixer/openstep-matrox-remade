/*
 * What the application already had in its buffer.
 *
 * In OSMesa the buffer the caller hands over IS the colour buffer: whatever
 * is in it is what the frame starts from, and drawing over a background the
 * caller loaded is an ordinary use of the library.  This back end swaps a
 * video memory surface in behind that pointer and copies the surface back
 * out after every frame -- and the copy runs one way only.  If nothing
 * copies the caller's pixels IN at bind time, they are gone: the first frame
 * mirrors video memory over them.
 *
 * So this fills the buffer with a pattern, makes it current, draws a small
 * quad, and asks whether the pattern is still there OUTSIDE the quad.  It is
 * the same question stock OSMesa answers yes to.
 *
 * Two rounds, because the two ways in are different code: a fresh context
 * binding for the first time, and a second buffer bound to a context that
 * already has one.
 */
#include <stdio.h>
#include <stdlib.h>
#include <GL/osmesa.h>
#include <GL/gl.h>

#include "../mesa/OpenStepMGAMesaHook.h"

#define W 128
#define H 96
#define PATTERN(x, y) (0x00FF0000UL | (((unsigned long)(x) & 0xFFUL) << 8) \
                                    | ((unsigned long)(y) & 0xFFUL))

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
fill(unsigned long *b)
{
    int x, y;

    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            b[y * W + x] = PATTERN(x, y);
}

/*
 * How many pixels outside the quad no longer hold the pattern, and whether
 * the quad itself was drawn at all -- a run that drew nothing would "keep"
 * the pattern everywhere and prove nothing.
 */
static void
check(const char *what, unsigned long *b, unsigned long drew)
{
    int x, y;
    unsigned long lost = 0UL, inside = 0UL;

    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            int in = (x >= 30 && x < 70 && y >= 20 && y < 60);
            unsigned long p = b[y * W + x] & 0x00FFFFFFUL;

            if (in) {
                if (((p >> 8) & 0xFFUL) > 0x80UL) inside++;
            } else if (p != PATTERN(x, y)) {
                lost++;
            }
        }
    printf("   %s: %lu pixels outside the quad lost the pattern,"
           " %lu inside were drawn\n", what, lost, inside);
    if (inside == 0UL) {
        printf("   FAIL  %s: nothing was drawn, so this proves nothing\n",
               what);
        failures++;
        return;
    }
    say(what, lost == 0UL);
    (void)drew;
}

static void
draw(void)
{
    glColor3f(0.0f, 1.0f, 0.0f);
    glBegin(GL_TRIANGLES);
      glVertex2d(30.0, 20.0); glVertex2d(70.0, 20.0); glVertex2d(70.0, 60.0);
      glVertex2d(30.0, 20.0); glVertex2d(70.0, 60.0); glVertex2d(30.0, 60.0);
    glEnd();
    glFinish();
}

static void
setUp(void)
{
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0.0, (double)W, 0.0, (double)H, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_DITHER);
    glDisable(GL_TEXTURE_2D); glDisable(GL_CULL_FACE);
    glShadeModel(GL_FLAT);
}

int
main(void)
{
    OSMesaContext ctx;
    unsigned long *first, *second;
    unsigned long d0;

    first  = (unsigned long *)malloc((unsigned)(W * H) * sizeof(unsigned long));
    second = (unsigned long *)malloc((unsigned)(W * H) * sizeof(unsigned long));
    if (!first || !second) { printf("no room\n"); return 2; }

    printf("the buffer the caller already had\n\n");

    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx) { printf("no context\n"); return 2; }

    /* NO glClear anywhere here: the pattern is the starting picture */
    fill(first);
    if (!OSMesaMakeCurrent(ctx, first, GL_UNSIGNED_BYTE, W, H)) {
        printf("no first bind\n"); return 2;
    }
    printf("   surface is the engine's : %s\n",
           (OSMGAMesaBufferOrigin() != 0UL) ? "yes" : "no (software; the"
           " question does not arise)");
    setUp();
    d0 = OSMGAMesaHookDrawn();
    draw();
    check("the first bind keeps it", first, OSMGAMesaHookDrawn() - d0);

    fill(second);
    if (!OSMesaMakeCurrent(ctx, second, GL_UNSIGNED_BYTE, W, H)) {
        printf("no rebind\n"); return 2;
    }
    setUp();
    d0 = OSMGAMesaHookDrawn();
    draw();
    check("a rebind keeps it too", second, OSMGAMesaHookDrawn() - d0);

    printf("\n%s (%d failing)\n",
           failures ? "=== PROBLEM ===" : "=== nothing to report ===",
           failures);
    return failures ? 1 : 0;
}
