/*
 * Delivering a cleared surface by writing its one value instead of reading
 * it back -- does it deliver the same picture?
 *
 * A frame that clears and then draws walks the video-memory surface twice,
 * and the first walk exists only because glClear opens its own render
 * bracket and this back end copies at the end of every bracket.  A
 * whole-surface clear leaves the surface holding one value, so that bracket
 * can write the value into the caller's array instead -- 0.585 ms rather
 * than 146.722 ms at 512 by 384.
 *
 * The way that goes wrong is silent: if the mark a clear leaves were ever
 * taken by a bracket that DREW something, the drawing would be painted over
 * with the clear colour and nothing would say so.  Most of this file is
 * about that one failure.
 *
 * Everything here reads the caller's own array.  Nothing writes to the card
 * except through ordinary GL calls.
 */
#include <stdio.h>
#include <stdlib.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
#include "../mesa/OpenStepMGAMesaHook.h"
#include "../mesa/OpenStepMGAMesaBuffer.h"

#define W 512
#define H 384
#define PAD 544                 /* a padded row length; a multiple of 32 */
#define SENTINEL 0xdeadbeefUL

static int failures;

static void
verdict(const char *what, int ok, const char *detail)
{
    printf("   %-46s %s%s%s\n", what, ok ? "yes" : "NO",
           detail && *detail ? "  -- " : "", detail ? detail : "");
    if (!ok) failures++;
}

/*
 * Every pixel of the picture, counted against one expected value.  Returns
 * how many differ and where the first of them is.
 */
static unsigned long
countOdd(const unsigned long *app, unsigned long row, unsigned long want,
         long *fx, long *fy)
{
    unsigned long odd = 0UL;
    long x, y;

    *fx = -1; *fy = -1;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            if (app[(unsigned long)y * row + (unsigned long)x] != want) {
                odd++;
                if (*fx < 0) { *fx = x; *fy = y; }
            }
    return odd;
}

static void
smallTriangle(void)
{
    glColor3f(0.0f, 1.0f, 0.0f);
    glBegin(GL_TRIANGLES);
      glVertex2d(100.0, 100.0);
      glVertex2d(160.0, 100.0);
      glVertex2d(160.0, 148.0);
    glEnd();
}

int
main(void)
{
    OSMesaContext ctx;
    unsigned long *app;
    unsigned long fills0, armed0, fills1, armed1, odd;
    long fx, fy;
    char msg[160];

    /* room for the padded case as well, so one allocation serves both */
    app = (unsigned long *)malloc((unsigned)(PAD * H) * sizeof(unsigned long));
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
    glDisable(GL_TEXTURE_2D); glDisable(GL_CULL_FACE);
    glShadeModel(GL_FLAT);

    printf("delivering a cleared surface without reading it back\n\n");
    if (OSMGAMesaBufferOrigin() == 0UL) {
        printf("   the surface is NOT the engine's -- nothing below means"
               " anything\n");
        return 2;
    }

    /*
     * 1. The caller's array is right immediately after glClear, with no
     *    glFinish and no other GL call in between.  That is the whole of the
     *    contract this change must not break.
     */
    {
        unsigned long want;

        glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
        armed0 = OSMGAMesaHookUniformArmed();
        fills0 = OSMGAMesaHookUniformFills();
        glClear(GL_COLOR_BUFFER_BIT);
        armed1 = OSMGAMesaHookUniformArmed();
        fills1 = OSMGAMesaHookUniformFills();

        want = app[0];
        odd = countOdd(app, W, want, &fx, &fy);
        sprintf(msg, "0x%08lx everywhere, armed %lu filled %lu",
                want, armed1 - armed0, fills1 - fills0);
        verdict("after glClear alone the array is one value", odd == 0UL, msg);

        /* and it is the value the software clear writes */
        {
            unsigned long soft;

            OSMGAMesaHookForceSoftware(1);
            glClear(GL_COLOR_BUFFER_BIT);
            OSMGAMesaHookForceSoftware(0);
            soft = app[0];
            sprintf(msg, "engine 0x%08lx software 0x%08lx", want, soft);
            verdict("and it is the colour software would write",
                    want == soft, msg);
        }
    }

    /*
     * 2. THE ONE THAT MATTERS.  Clear, then draw, then finish.  If the
     *    clear's mark were taken by the triangle's bracket, the triangle
     *    would be painted over with the clear colour and nothing else in
     *    this file would notice.
     */
    {
        unsigned long clearWord, drawn0, drawn1, changed = 0UL;
        long x, y;

        glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
        armed0 = OSMGAMesaHookUniformArmed();
        fills0 = OSMGAMesaHookUniformFills();
        drawn0 = OSMGAMesaHookDrawn();
        glClear(GL_COLOR_BUFFER_BIT);
        clearWord = app[0];
        smallTriangle();
        glFinish();
        armed1 = OSMGAMesaHookUniformArmed();
        fills1 = OSMGAMesaHookUniformFills();
        drawn1 = OSMGAMesaHookDrawn();

        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++)
                if (app[(unsigned long)y * W + (unsigned long)x] != clearWord)
                    changed++;

        sprintf(msg, "%lu pixels are not the clear colour (the triangle is"
                " about 1440), %lu batches", changed, drawn1 - drawn0);
        verdict("a triangle drawn after a clear survives", changed > 500UL,
                msg);
        sprintf(msg, "armed %lu, filled %lu -- one clear, one delivery",
                armed1 - armed0, fills1 - fills0);
        verdict("the clear armed once and delivered once",
                (armed1 - armed0) == 1UL && (fills1 - fills0) == 1UL, msg);
    }

    /*
     * 3. A scissored clear does not cover the surface, so it must NOT arm.
     *    What is outside the box has to keep what it held.
     */
    {
        unsigned long outside;

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);            /* all black */
        glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, 64, 64);
        armed0 = OSMGAMesaHookUniformArmed();
        glClear(GL_COLOR_BUFFER_BIT);
        glFinish();
        armed1 = OSMGAMesaHookUniformArmed();
        glDisable(GL_SCISSOR_TEST);

        outside = app[(unsigned long)200 * W + 200UL];
        sprintf(msg, "armed %lu, the pixel at 200,200 is 0x%08lx",
                armed1 - armed0, outside);
        verdict("a scissored clear does not arm the delivery",
                (armed1 - armed0) == 0UL, msg);
        verdict("and leaves what is outside the box alone",
                outside != app[0], "0,0 is inside the box and 200,200 is not");
    }

    /*
     * 4. A caller whose array is wider than its picture keeps its padding.
     *    That is the ordinary mirror's contract and the delivery has to keep
     *    it, because it writes the same rows.
     */
    {
        unsigned long i, kept = 0UL, padCells = 0UL;
        long y;

        for (i = 0UL; i < (unsigned long)PAD * H; i++)
            app[i] = SENTINEL;
        OSMesaPixelStore(OSMESA_ROW_LENGTH, PAD);
        if (!OSMesaMakeCurrent(ctx, app, GL_UNSIGNED_BYTE, W, H)) {
            printf("   could not rebind at a padded row length\n");
            failures++;
        } else {
            glViewport(0, 0, W, H);
            glClearColor(0.0f, 1.0f, 1.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            odd = countOdd(app, PAD, app[0], &fx, &fy);
            for (y = 0; y < H; y++)
                for (i = W; i < PAD; i++) {
                    padCells++;
                    if (app[(unsigned long)y * PAD + i] == SENTINEL) kept++;
                }
            sprintf(msg, "%lu of %lu padding cells untouched", kept, padCells);
            verdict("a padded array is filled only where the picture is",
                    odd == 0UL && kept == padCells, msg);
        }
        OSMesaPixelStore(OSMESA_ROW_LENGTH, 0);
        (void)OSMesaMakeCurrent(ctx, app, GL_UNSIGNED_BYTE, W, H);
    }

    printf("\n   %d failed\n", failures);
    OSMesaDestroyContext(ctx);
    free(app);
    return failures ? 1 : 0;
}
