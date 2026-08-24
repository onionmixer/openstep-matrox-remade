/*
 * glScissor, against the software rasteriser.
 *
 * The engine has a destination clip -- CXBNDRY, YTOP, YBOT -- and the submit
 * path already programs it to the whole window before every batch, so a
 * scissor is that clip narrowed.  The kernel INTERSECTS the box with the
 * window rather than trusting it, which is the whole safety argument: no
 * value a client sends can widen anything, so containment does not rest on
 * the box being sensible and the validator's row and column checks are
 * unchanged and still measured against the whole window.
 *
 * The first thing this asks is whether the clip bites at all.  The submit
 * path has always programmed it, but nothing has ever measured a NARROW one
 * -- so "it clips" and "it happens to be wide enough never to matter" have
 * never been separated, and the destination-origin question (YTOP and YBOT
 * are pixel offsets, and whether the origin is added) turns on exactly that.
 *
 * python, over a 128 by 96 surface with a box at x 40 y 30 of 32 by 24:
 *
 *      columns 40..71 kept, 39 and 72 dropped
 *      rows    30..53 kept, 29 and 54 dropped
 *
 * so the edges are asked one pixel either side, which is where an inclusive
 * versus half-open mistake shows and a whole-pixel origin error does not
 * hide.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/osmesa.h>
#include <GL/gl.h>

#include "../mesa/OpenStepMGAMesaHook.h"

#define W 128
#define H 96
#define SX 40
#define SY 30
#define SW 32
#define SH 24

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

static void softOn(void)  { OSMGAMesaHookForceSoftware(1); }
static void softOff(void) { OSMGAMesaHookForceSoftware(0); }

/* one green quad over the whole middle, under the current scissor */
static unsigned long
draw(int soft, unsigned long *drew)
{
    unsigned long before = OSMGAMesaHookDrawn();

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (soft) softOn();
    glColor3f(0.0f, 1.0f, 0.0f);
    glBegin(GL_TRIANGLES);
      glVertex2d(20.0, 20.0); glVertex2d(108.0, 20.0);
      glVertex2d(108.0, 76.0);
      glVertex2d(20.0, 20.0); glVertex2d(108.0, 76.0);
      glVertex2d(20.0, 76.0);
    glEnd();
    glFinish();
    if (soft) softOff();
    *drew = OSMGAMesaHookDrawn() - before;
    return 0UL;
}

static int
lit(long x, long y)
{
    return (((app[y * W + x] >> 8) & 0xFFUL) > 0x80UL);
}

int
main(void)
{
    OSMesaContext ctx;
    static const struct { long x, y; int want; const char *n; } pts[9] = {
        { SX + 1,      SY + 1,      1, "inside      " },
        { SX,          SY + 1,      1, "left edge   " },
        { SX - 1,      SY + 1,      0, "just left   " },
        { SX + SW - 1, SY + 1,      1, "right edge  " },
        { SX + SW,     SY + 1,      0, "just right  " },
        { SX + 1,      SY,          1, "bottom edge " },
        { SX + 1,      SY - 1,      0, "just below  " },
        { SX + 1,      SY + SH - 1, 1, "top edge    " },
        { SX + 1,      SY + SH,     0, "just above  " }
    };
    int i;

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
    glDisable(GL_TEXTURE_2D); glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glShadeModel(GL_FLAT);

    printf("glScissor, engine against software\n\n");

    printf("1. the box bites, and on the pixel python says\n");
    {
        unsigned long dh, ds;

        glEnable(GL_SCISSOR_TEST);
        glScissor(SX, SY, SW, SH);
        (void)draw(0, &dh);
        for (i = 0; i < 9; i++) {
            char name[80];
            int got = lit(pts[i].x, pts[i].y);

            sprintf(name, "%s (%3ld,%3ld) is %s", pts[i].n, pts[i].x,
                    pts[i].y, pts[i].want ? "drawn" : "not drawn");
            say(name, got == pts[i].want);
        }
        if (dh == 0UL) {
            printf("   FAIL  the scissored quad never reached the engine\n");
            failures++;
        }
        /*
         * And the software twin, pixel for pixel over the whole surface.
         * GL decides this one entirely, so any difference at all is a fault.
         */
        {
            unsigned long *hw = (unsigned long *)malloc(
                (unsigned)(W * H) * sizeof(unsigned long));
            long x, y, bad = 0;

            memcpy(hw, app, (unsigned)(W * H) * sizeof(unsigned long));
            (void)draw(1, &ds);
            for (y = 0; y < H; y++)
                for (x = 0; x < W; x++)
                    if (hw[y * W + x] != app[y * W + x]) bad++;
            printf("   pixels differing from software: %ld\n", bad);
            say("the two paths scissor identically", bad == 0);
            if (ds != 0UL) {
                printf("   FAIL  the software pass was accelerated\n");
                failures++;
            }
            free(hw);
        }
        glDisable(GL_SCISSOR_TEST);
    }

    printf("\n2. the boxes that are not boxes\n");
    {
        unsigned long dh;
        long x, y, litc;

        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, 0, 0);
        (void)draw(0, &dh);
        litc = 0;
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) if (lit(x, y)) litc++;
        say("an empty box draws nothing", litc == 0);

        /* wholly outside, and partly outside: the intersection decides */
        glScissor(W + 10, H + 10, 20, 20);
        (void)draw(0, &dh);
        litc = 0;
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) if (lit(x, y)) litc++;
        say("a box past the surface draws nothing", litc == 0);

        glScissor(-20, -20, 60, 60);
        (void)draw(0, &dh);
        say("a box hanging off the low corner keeps what overlaps",
            lit(25, 25) && !lit(45, 25));
        glDisable(GL_SCISSOR_TEST);
    }

    printf("\n3. no scissor at all is unchanged\n");
    {
        unsigned long dh;

        (void)draw(0, &dh);
        say("the whole quad is there", lit(21, 21) && lit(106, 74));
        say("and it was the engine that drew it", dh != 0UL);
    }

    printf("\n%s (%d failing)\n",
           failures ? "=== PROBLEM ===" : "=== nothing to report ===",
           failures);
    return failures ? 1 : 0;
}
