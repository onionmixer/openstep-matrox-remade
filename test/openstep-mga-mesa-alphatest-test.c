/*
 * glAlphaFunc through the chooser, against the software rasteriser.
 *
 * The register fields were measured first (probe section 87): the engine
 * compares the TEXTURE STAGE's alpha -- the same value the blend selector
 * reads, separated from the texel under GL_MODULATE where the two part
 * company -- and a fragment the test discards writes no depth either.
 *
 * This asks the GL questions those cannot: whether the chooser admits the
 * seven and refuses GL_NEVER, whether the reference lands where Mesa's own
 * byte says, and whether the discard is visible in the way an application
 * would see it -- by what ends up on the screen behind the hole.
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

static void
quad(double z, float r, float g, float b, float a)
{
    glColor4f(r, g, b, a);
    glBegin(GL_TRIANGLES);
      glVertex3d(20.0, 20.0, z); glVertex3d(108.0, 20.0, z);
      glVertex3d(108.0, 76.0, z);
      glVertex3d(20.0, 20.0, z); glVertex3d(108.0, 76.0, z);
      glVertex3d(20.0, 76.0, z);
    glEnd();
}

/* does a quad of this alpha survive the test? */
static int
survives(GLenum func, GLfloat ref, float alpha, int soft)
{
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(func, ref);
    if (soft) softOn();
    quad(0.0, 0.0f, 1.0f, 0.0f, alpha);
    glFinish();
    if (soft) softOff();
    glDisable(GL_ALPHA_TEST);
    return (((app[48 * W + 64] >> 8) & 0xFFUL) > 0x80UL);
}

int
main(void)
{
    OSMesaContext ctx;
    static const struct { GLenum f; const char *n;
                          int wantLo, wantEq, wantHi; } cases[7] = {
        { GL_LESS,     "GL_LESS    ", 1, 0, 0 },
        { GL_LEQUAL,   "GL_LEQUAL  ", 1, 1, 0 },
        { GL_GREATER,  "GL_GREATER ", 0, 0, 1 },
        { GL_GEQUAL,   "GL_GEQUAL  ", 0, 1, 1 },
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
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0.0, (double)W, 0.0, (double)H, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_DITHER);
    glDisable(GL_TEXTURE_2D); glDisable(GL_CULL_FACE);
    glShadeModel(GL_FLAT);

    printf("the alpha test, engine against software\n\n");
    /*
     * The reference is 128/255, and the three alphas are one level below it,
     * exactly it, and one level above -- so the equality cases mean equality
     * of the byte Mesa compares and not of a float that rounded nearby.
     */
    for (i = 0; i < 7; i++) {
        GLfloat ref = 128.0f / 255.0f;
        int hl = survives(cases[i].f, ref, 127.0f / 255.0f, 0);
        int he = survives(cases[i].f, ref, 128.0f / 255.0f, 0);
        int hh = survives(cases[i].f, ref, 129.0f / 255.0f, 0);
        int sl = survives(cases[i].f, ref, 127.0f / 255.0f, 1);
        int se = survives(cases[i].f, ref, 128.0f / 255.0f, 1);
        int sh = survives(cases[i].f, ref, 129.0f / 255.0f, 1);
        char name[80];

        printf("   %s  engine below/equal/above %d%d%d   software %d%d%d"
               "   wanted %d%d%d\n", cases[i].n, hl, he, hh, sl, se, sh,
               cases[i].wantLo, cases[i].wantEq, cases[i].wantHi);
        sprintf(name, "%s discards where GL says", cases[i].n);
        say(name, hl == cases[i].wantLo && he == cases[i].wantEq &&
                  hh == cases[i].wantHi);
        sprintf(name, "%s and software agrees", cases[i].n);
        say(name, hl == sl && he == se && hh == sh);
    }

    /*
     * The witness.  A discarded fragment must leave no depth behind, and the
     * way to see that as an application would is to put something BEHIND the
     * hole and check that it shows.
     *
     * A control with the same geometry and a reference it passes proves the
     * front quad could have drawn and occluded, so "the witness is visible"
     * cannot be "the front quad was never rasterised".
     */
    printf("\n   the witness behind a discarded fragment\n");
    {
        unsigned long px;
        int k;

        for (k = 0; k < 2; k++) {
            /* k = 0 discards the front quad, k = 1 keeps it */
            glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glClearColor(0.0f, 0.0f, 1.0f, 1.0f);   /* blue background */
            glClearDepth(1.0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glEnable(GL_ALPHA_TEST);
            glAlphaFunc(GL_GEQUAL, k ? (16.0f / 255.0f) : (240.0f / 255.0f));
            quad(0.5, 1.0f, 0.0f, 0.0f, 128.0f / 255.0f);   /* red, nearer */
            glFinish();
            glAlphaFunc(GL_ALWAYS, 0.0f);
            quad(0.0, 0.0f, 1.0f, 0.0f, 1.0f);              /* green, farther */
            glFinish();
            glDisable(GL_ALPHA_TEST); glDisable(GL_DEPTH_TEST);
            px = app[48 * W + 64];
            if (k == 0)
                say("the green witness shows through the discarded red",
                    ((px >> 8) & 0xFFUL) > 0x80UL &&
                    ((px >> 16) & 0xFFUL) < 0x80UL);
            else
                say("and the control keeps the red, so it could have drawn",
                    ((px >> 16) & 0xFFUL) > 0x80UL);
        }
    }

    printf("\n   what is still refused\n");
    {
        unsigned long d0 = OSMGAMesaHookDrawn();

        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_NEVER, 0.5f);
        glClear(GL_COLOR_BUFFER_BIT);
        quad(0.0, 1.0f, 1.0f, 1.0f, 1.0f);
        glFinish();
        glDisable(GL_ALPHA_TEST);
        say("GL_NEVER does not reach the engine",
            OSMGAMesaHookDrawn() == d0);
    }

    printf("\n%s (%d failing)\n",
           failures ? "=== PROBLEM ===" : "=== nothing to report ===",
           failures);
    return failures ? 1 : 0;
}
