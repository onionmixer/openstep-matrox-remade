/*
 * Every blend factor pair the chooser now admits, against the software
 * rasteriser.
 *
 * The engine has GL 1.1's two factor sets in its ALPHACTRL, nine for the
 * source and eight for the destination, and probe section 86 measured six
 * pairs at the register: all six match python, ONE over ONE saturates rather
 * than wrapping, and the rounding happens once at the end.  This asks the
 * same question through GL, which is a different question -- it exercises the
 * chooser, the mapping, and Mesa's own arithmetic for pairs that do not go
 * through its transparency fast path.
 *
 * Mesa's paths are not one path.  ONE/ONE has a saturated-add special case,
 * DST_COLOR/ZERO and ZERO/SRC_COLOR go through a modulate case that shifts by
 * eight -- a divide by 256, not 255 -- and everything else goes through the
 * general routine with a float divide and a final clamp.  So a difference
 * against the engine is expected on some pairs and its SIZE is the result,
 * not its presence.
 *
 * Software is forced with a full-surface scissor, the way depth-agree does
 * it: the chooser refuses the raster bit and the box clips nothing, so the
 * path changes and the picture does not.
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
quad(float r, float g, float b, float a)
{
    glColor4f(r, g, b, a);
    glBegin(GL_TRIANGLES);
      glVertex2d(20.0, 20.0); glVertex2d(108.0, 20.0);
      glVertex2d(108.0, 76.0);
      glVertex2d(20.0, 20.0); glVertex2d(108.0, 76.0);
      glVertex2d(20.0, 76.0);
    glEnd();
}

/* draw the pair over a painted destination and return the middle pixel */
static unsigned long
run(GLenum sf, GLenum df, int soft, unsigned long *drew)
{
    unsigned long before = OSMGAMesaHookDrawn();

    /* the destination, through Mesa so its own state agrees with the buffer */
    glDisable(GL_BLEND);
    glClearColor(0x80 / 255.0f, 0x40 / 255.0f, 0x20 / 255.0f, 0x60 / 255.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(sf, df);
    if (soft) softOn();
    quad(0xC0 / 255.0f, 0x80 / 255.0f, 0x40 / 255.0f, 0x80 / 255.0f);
    glFinish();
    if (soft) softOff();
    glDisable(GL_BLEND);
    *drew = OSMGAMesaHookDrawn() - before;
    return app[48 * W + 64];
}

int
main(void)
{
    OSMesaContext ctx;
    static const struct { GLenum s, d; const char *n; } pairs[9] = {
        { GL_ONE,                 GL_ONE,                 "ONE       ONE      " },
        { GL_SRC_ALPHA,           GL_ONE,                 "SRC_ALPHA ONE      " },
        { GL_DST_COLOR,           GL_ZERO,                "DST_COLOR ZERO     " },
        { GL_ZERO,                GL_SRC_COLOR,           "ZERO      SRC_COLOR" },
        { GL_ONE,                 GL_ONE_MINUS_SRC_ALPHA, "ONE       1-SRC_A  " },
        { GL_DST_ALPHA,           GL_ONE_MINUS_DST_ALPHA, "DST_ALPHA 1-DST_A  " },
        { GL_ONE_MINUS_DST_COLOR, GL_ZERO,                "1-DST_C   ZERO     " },
        { GL_ZERO,                GL_ONE_MINUS_SRC_COLOR, "ZERO      1-SRC_C  " },
        { GL_SRC_ALPHA,           GL_ONE_MINUS_SRC_ALPHA, "SRC_ALPHA 1-SRC_A  " }
    };
    int i, worst = 0;

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
    glDisable(GL_DEPTH_TEST); glDisable(GL_DITHER);
    glDisable(GL_TEXTURE_2D); glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glShadeModel(GL_FLAT);

    printf("blend factor pairs: source c08040 alpha 80 over destination"
           " 804020 alpha 60\n\n");
    for (i = 0; i < 9; i++) {
        unsigned long dh, ds, h, s;
        int j, d = 0;

        h = run(pairs[i].s, pairs[i].d, 0, &dh);
        s = run(pairs[i].s, pairs[i].d, 1, &ds);
        for (j = 0; j < 4; j++) {
            int a = (int)((h >> (j * 8)) & 0xFFUL);
            int b = (int)((s >> (j * 8)) & 0xFFUL);

            if (a - b > d) d = a - b;
            if (b - a > d) d = b - a;
        }
        if (d > worst) worst = d;
        printf("   %s engine %08lx  software %08lx  worst channel %d%s\n",
               pairs[i].n, h, s, d, (dh == 0UL) ? "   <-- NOT ACCELERATED" : "");
        if (dh == 0UL) {
            printf("   FAIL  %s never reached the engine\n", pairs[i].n);
            failures++;
        }
        if (ds != 0UL) {
            printf("   FAIL  %s was accelerated when software was asked for\n",
                   pairs[i].n);
            failures++;
        }
    }
    printf("\n   the widest channel disagreement over all nine pairs: %d\n",
           worst);
    /*
     * Two levels, not zero.  Mesa divides by 256 on the modulate pairs and by
     * 255 elsewhere, and the engine divides by 255 throughout, so a pair or
     * two of levels is the arithmetic and not a fault.  What would be a fault
     * is a large one, and what this pins is that it stays small.
     */
    if (worst > 2) {
        printf("   FAIL  a pair disagrees by more than the arithmetic"
               " explains\n");
        failures++;
    } else
        printf("   ok    every pair is within two levels of software\n");

    printf("\n   what is still refused\n");
    {
        unsigned long d0 = OSMGAMesaHookDrawn(), dz;

        (void)run(GL_SRC_ALPHA_SATURATE, GL_ONE, 0, &dz);
        if (dz != 0UL) {
            printf("   FAIL  GL_SRC_ALPHA_SATURATE reached the engine\n");
            failures++;
        } else
            printf("   ok    GL_SRC_ALPHA_SATURATE does not\n");
        (void)d0;
    }
    {
        unsigned long dz;

        glBlendFuncSeparateEXT(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                               GL_ONE, GL_ZERO);
        glEnable(GL_BLEND);
        {
            unsigned long before = OSMGAMesaHookDrawn();

            glClear(GL_COLOR_BUFFER_BIT);
            quad(1.0f, 1.0f, 1.0f, 0.5f);
            glFinish();
            dz = OSMGAMesaHookDrawn() - before;
        }
        glDisable(GL_BLEND);
        if (dz != 0UL) {
            printf("   FAIL  a separate alpha factor reached the engine\n");
            failures++;
        } else
            printf("   ok    nor does a separate alpha factor\n");
    }

    printf("\n%s (%d failing)\n",
           failures ? "=== PROBLEM ===" : "=== nothing to report ===",
           failures);
    return failures ? 1 : 0;
}
