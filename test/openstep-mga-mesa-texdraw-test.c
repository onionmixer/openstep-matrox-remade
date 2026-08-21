/*
 * Texturing through GL, at last.
 *
 * Everything before this called the back end directly.  This draws with
 * glTexImage2D and glTexCoord2f and asks whether the chooser took it, whether
 * the picture is right, and whether each condition it must refuse is refused
 * for its own reason rather than by accident.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/osmesa.h>
#include <GL/gl.h>

extern unsigned long OSMGAMesaHookDrawn(void);
extern unsigned long OSMGAMesaHookSoftware(void);
extern unsigned long OSMGAMesaHookTexPersp(void);
extern unsigned long OSMGAMesaHookTexAbsent(void);
extern unsigned long OSMGAMesaTexUploads(void);

#define W 320
#define H 240
#define TD 16
#define CLEARC 0xFF102030UL

static unsigned long *app;
static int failures;

static void
say(const char *what, int ok, const char *detail)
{
    printf("  %-46s %s%s%s\n", what, ok ? "ok" : "FAIL",
           detail ? "  " : "", detail ? detail : "");
    if (!ok) failures++;
}

/* a texture whose texels name themselves, all three channels different */
static void
maketex(GLuint *id)
{
    static GLubyte px[TD * TD * 3];
    int x, y;

    for (y = 0; y < TD; y++)
        for (x = 0; x < TD; x++) {
            px[(y * TD + x) * 3 + 0] = (GLubyte)(x * 16 + 8);
            px[(y * TD + x) * 3 + 1] = (GLubyte)(y * 16 + 4);
            px[(y * TD + x) * 3 + 2] = (GLubyte)(x + y);
        }
    glGenTextures(1, id);
    glBindTexture(GL_TEXTURE_2D, *id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TD, TD, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, px);
}

static void
quad(double x0, double y0, double x1, double y1)
{
    glBegin(GL_TRIANGLES);
      glTexCoord2f(0.0f, 0.0f); glVertex2d(x0, y0);
      glTexCoord2f(1.0f, 0.0f); glVertex2d(x1, y0);
      glTexCoord2f(1.0f, 1.0f); glVertex2d(x1, y1);
      glTexCoord2f(0.0f, 0.0f); glVertex2d(x0, y0);
      glTexCoord2f(1.0f, 1.0f); glVertex2d(x1, y1);
      glTexCoord2f(0.0f, 1.0f); glVertex2d(x0, y1);
    glEnd();
}

static long
painted(void)
{
    long n = 0, i;

    for (i = 0; i < (long)W * (long)H; i++)
        if (app[i] != CLEARC) n++;
    return n;
}

int
main(void)
{
    OSMesaContext ctx;
    GLuint tex;
    unsigned long d0, s0, p0, a0;
    long drew;

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
    glDisable(GL_CULL_FACE); glDisable(GL_ALPHA_TEST);
    glShadeModel(GL_FLAT);
    glClearColor(0x10/255.0f, 0x20/255.0f, 0x30/255.0f, 1.0f);

    maketex(&tex);
    glEnable(GL_TEXTURE_2D);

    /* 1. the ordinary case reaches the engine */
    glClear(GL_COLOR_BUFFER_BIT);
    d0 = OSMGAMesaHookDrawn(); s0 = OSMGAMesaHookSoftware();
    p0 = OSMGAMesaHookTexPersp(); a0 = OSMGAMesaHookTexAbsent();
    quad(40.0, 40.0, 168.0, 168.0);
    glFinish();
    drew = painted();
    printf("# ordinary: drawn %lu software %lu persp %lu absent %lu"
           " uploads %lu painted %ld\n",
           OSMGAMesaHookDrawn() - d0, OSMGAMesaHookSoftware() - s0,
           OSMGAMesaHookTexPersp() - p0, OSMGAMesaHookTexAbsent() - a0,
           OSMGAMesaTexUploads(), drew);
    say("a textured quad reaches the engine",
        OSMGAMesaHookDrawn() - d0 >= 2UL, 0);
    say("and nothing fell back", OSMGAMesaHookSoftware() - s0 == 0UL, 0);
    say("and it covered the quad", drew == 128L * 128L, 0);

    /* the picture: every drawn pixel must be a texel of ours */
    {
        long x, y, bad = 0;

        for (y = 40; y < 168; y++)
            for (x = 40; x < 168; x++) {
                unsigned long p = app[y * W + x];
                unsigned long r = (p >> 16) & 0xFFUL;
                unsigned long g = (p >> 8) & 0xFFUL;
                unsigned long b = p & 0xFFUL;
                unsigned long tx = (r - 8UL) / 16UL, ty = (g - 4UL) / 16UL;

                if (r % 16UL != 8UL || g % 16UL != 4UL ||
                    tx >= (unsigned long)TD || ty >= (unsigned long)TD ||
                    b != tx + ty)
                    bad++;
            }
        say("every pixel is a texel of this texture", bad == 0, 0);
    }

    /* 2. each condition, broken on its own, must refuse */
    {
        struct { const char *name; } cases[4];
        unsigned long before;
        int k;

        cases[0].name = "GL_MODULATE instead of GL_REPLACE";
        cases[1].name = "GL_LINEAR instead of GL_NEAREST";
        cases[2].name = "GL_REPEAT instead of GL_CLAMP";
        cases[3].name = "blending on";
        for (k = 0; k < 4; k++) {
            glClear(GL_COLOR_BUFFER_BIT);
            before = OSMGAMesaHookDrawn();
            if (k == 0) glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            if (k == 1) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            if (k == 2) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            if (k == 3) glEnable(GL_BLEND);
            quad(40.0, 40.0, 168.0, 168.0);
            glFinish();
            say(cases[k].name, OSMGAMesaHookDrawn() == before, 0);
            if (k == 0) glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
            if (k == 1) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            if (k == 2) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
            if (k == 3) glDisable(GL_BLEND);
        }
    }

    /* and it comes back after they are put right */
    glClear(GL_COLOR_BUFFER_BIT);
    d0 = OSMGAMesaHookDrawn();
    quad(40.0, 40.0, 168.0, 168.0);
    glFinish();
    say("acceleration returns when the state does",
        OSMGAMesaHookDrawn() - d0 >= 2UL, 0);

    /* 3. perspective must be refused by the affine gate and nothing else */
    glClear(GL_COLOR_BUFFER_BIT);
    d0 = OSMGAMesaHookDrawn(); p0 = OSMGAMesaHookTexPersp();
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glFrustum(-1.0, 1.0, -1.0, 1.0, 1.0, 20.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glBegin(GL_TRIANGLES);
      glTexCoord2f(0.0f, 0.0f); glVertex3d(-0.6, -0.6, -2.0);
      glTexCoord2f(1.0f, 0.0f); glVertex3d( 0.6, -0.6, -8.0);
      glTexCoord2f(1.0f, 1.0f); glVertex3d( 0.6,  0.6, -8.0);
    glEnd();
    glFinish();
    say("a perspective triangle is refused",
        OSMGAMesaHookDrawn() == d0, 0);
    say("and by the affine gate, not by something else",
        OSMGAMesaHookTexPersp() - p0 >= 1UL, 0);

    printf("\n%s (%d failing)\n",
           failures ? "=== PROBLEM ===" : "=== nothing to report ===", failures);
    return failures ? 1 : 0;
}
