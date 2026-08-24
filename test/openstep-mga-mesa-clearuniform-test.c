/*
 * Is the surface really ONE CONSTANT after an accelerated whole-surface
 * clear, and is that constant the one Mesa would have written?
 *
 * Both questions are premises of a change that is not yet built: a frame that
 * clears and then draws walks the video-memory surface twice, and the first
 * walk exists only to deliver a surface that holds a single value.  Writing
 * that value into the caller's array instead is two hundred and fifty times
 * cheaper -- but only if the value really is single, and really is the same
 * value the software path produces.  Neither had ever been measured.
 *
 * The engine draws the clear as TWO TRIANGLES with a shared diagonal, so the
 * failure this looks for is real rather than theoretical: a fill rule that
 * left a gap along that diagonal would leave a line of stale pixels, and the
 * planned change would then deliver a constant where the surface has a seam.
 *
 * Nothing here writes to the card.  It clears, finishes -- which is what
 * copies the surface into the caller's array -- and reads the array.
 */
#include <stdio.h>
#include <stdlib.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
#include "../mesa/OpenStepMGAMesaHook.h"
#include "../mesa/OpenStepMGAMesaBuffer.h"

#define W 512
#define H 384

static const char *
why(int c)
{
    switch (c) {
    case 0:  return "taken by the engine";
    case 1:  return "software was forced";
    case 2:  return "no batch";
    case 3:  return "no surface";
    case 4:  return "stride not a multiple of the pitch alignment";
    case 5:  return "not an RGBA visual";
    case 6:  return "a colour mask was set";
    case 7:  return "not the one colour destination";
    case 8:  return "the colour bit was not asked for";
    case 9:  return "there is a software alpha buffer";
    case 10: return "an empty rectangle";
    case 11: case 12: case 13: return "the trapezoid builder refused";
    case 14: return "the driver refused the batch";
    default: return "unknown";
    }
}

/*
 * Clear to one colour and report what the caller's array holds.  Returns the
 * value of the first pixel; *odd receives how many pixels differ from it and
 * *firstOdd where the first of them is.
 */
static unsigned long
clearAndScan(const unsigned long *app, float r, float g, float b, float a,
             unsigned long *odd, long *firstOddX, long *firstOddY,
             int *clearWhy)
{
    unsigned long first;
    long x, y;

    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    *clearWhy = OSMGAMesaHookClearWhy();

    first = app[0];
    *odd = 0UL;
    *firstOddX = -1;
    *firstOddY = -1;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            if (app[y * W + x] != first) {
                (*odd)++;
                if (*firstOddX < 0) { *firstOddX = x; *firstOddY = y; }
            }
    return first;
}

int
main(void)
{
    static const struct { float r, g, b, a; const char *n; } colours[4] = {
        { 0.0f, 0.0f, 0.0f, 1.0f, "black"      },
        { 1.0f, 1.0f, 1.0f, 1.0f, "white"      },
        { 0.5f, 0.25f, 0.75f, 1.0f, "a colour that truncates" },
        { 0.2f, 0.9f, 0.4f, 0.6f, "with alpha under one"     }
    };
    OSMesaContext ctx;
    unsigned long *app;
    int i, failures = 0;

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

    printf("is a cleared surface one constant, and the right one?\n\n");
    if (OSMGAMesaBufferOrigin() == 0UL) {
        printf("   the surface is NOT the engine's -- nothing below means"
               " anything\n");
        return 2;
    }
    printf("   %-26s %10s %10s  %s\n",
           "colour", "engine", "software", "verdict");

    for (i = 0; i < 4; i++) {
        unsigned long eng, soft, oddE, oddS;
        long ex, ey, sx, sy;
        int whyE, whyS;

        eng = clearAndScan(app, colours[i].r, colours[i].g, colours[i].b,
                           colours[i].a, &oddE, &ex, &ey, &whyE);

        OSMGAMesaHookForceSoftware(1);
        soft = clearAndScan(app, colours[i].r, colours[i].g, colours[i].b,
                            colours[i].a, &oddS, &sx, &sy, &whyS);
        OSMGAMesaHookForceSoftware(0);

        printf("   %-26s 0x%08lx 0x%08lx  ", colours[i].n, eng, soft);

        if (whyE != 0) {
            printf("NOT ON THE ENGINE (%s)\n", why(whyE));
            failures++;
            continue;
        }
        if (oddE != 0UL) {
            printf("NOT UNIFORM: %lu pixels differ, first at %ld,%ld\n",
                   oddE, ex, ey);
            failures++;
            continue;
        }
        if (oddS != 0UL) {
            /* The software clear is Mesa's own and must be uniform; if it is
             * not, the reading of the caller's array is what is wrong, not
             * the engine. */
            printf("THE SOFTWARE CLEAR IS NOT UNIFORM EITHER (%lu differ)"
                   " -- this test is wrong, not the driver\n", oddS);
            failures++;
            continue;
        }
        if (eng != soft) {
            printf("UNIFORM but a DIFFERENT COLOUR from software\n");
            failures++;
            continue;
        }
        printf("uniform, and the same colour software writes\n");
    }

    printf("\n   %d of 4 failed\n", failures);
    if (failures == 0)
        printf("   a whole-surface clear leaves one constant, so delivering"
               " that constant to the\n   caller instead of reading the"
               " surface back would deliver identical bytes\n");
    OSMesaDestroyContext(ctx);
    free(app);
    return failures ? 1 : 0;
}
